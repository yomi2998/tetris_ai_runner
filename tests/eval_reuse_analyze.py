import argparse
import gc
import heapq
import json
import os
import resource
import struct
import sys
import tempfile
from bisect import bisect_right
from collections import Counter

MAGIC = b"EVTRC02\x00"
VERSION = 1
HEADER_SIZE = 64
RECORD_SIZE = 80
WORD_COUNT = 8
KIND_EVAL = 0
KIND_NODE = 1
KIND_RESET = 2
MOVE_WARMUP = 0xFFFF
MASK64 = 0xFFFFFFFFFFFFFFFF
MASK32 = 0xFFFFFFFF
REC_FIELDS = struct.Struct("<BBBBHHII8Q")
SPOOL_FIELDS = struct.Struct("<IQQ")
PAIR_FIELDS = struct.Struct("<QI")
ZERO4 = b"\x00\x00\x00\x00"
ZERO64 = b"\x00" * 64
CHUNK_RECORDS_DEFAULT = 65536
SORT_RUN_RECORDS_DEFAULT = 1000000
SPOOL_READ_RECORDS = 262144


def rotl64(value, shift):
    shift &= 63
    if shift == 0:
        return value & MASK64
    return ((value << shift) | (value >> (64 - shift))) & MASK64


def occupancy_fingerprint(words):
    result = 0
    for index, word in enumerate(words):
        result ^= rotl64(word, index * 7)
    return result & MASK64


def occupancy_hash(words):
    result = 1469598103934665603
    for word in words:
        result ^= word
        result = (result * 1099511628211) & MASK64
    return result


def parse_header(data):
    if len(data) < HEADER_SIZE:
        raise ValueError("short header")
    if data[0:8] != MAGIC:
        raise ValueError("bad magic")
    version = struct.unpack_from("<H", data, 8)[0]
    if version != VERSION:
        raise ValueError("unsupported version")
    seed = struct.unpack_from("<I", data, 12)[0]
    iters, maxdepth, warmup, moves = struct.unpack_from("<QQQQ", data, 16)
    hold = data[48]
    return {
        "seed": seed,
        "iters": iters,
        "maxdepth": maxdepth,
        "warmup_moves": warmup,
        "moves": moves,
        "hold": hold,
    }


def parse_record(data, offset):
    kind = data[offset]
    source = data[offset + 1]
    depth = data[offset + 2]
    if data[offset + 3] != 0:
        raise ValueError("reserved byte nonzero")
    move = struct.unpack_from("<H", data, offset + 4)[0]
    if data[offset + 6] != 0 or data[offset + 7] != 0:
        raise ValueError("reserved bytes nonzero")
    node_id = struct.unpack_from("<I", data, offset + 8)[0]
    if struct.unpack_from("<I", data, offset + 12)[0] != 0:
        raise ValueError("reserved word nonzero")
    words = struct.unpack_from("<8Q", data, offset + 16)
    if kind not in (KIND_EVAL, KIND_NODE, KIND_RESET):
        raise ValueError("unknown record kind")
    if kind == KIND_RESET and any(words):
        raise ValueError("reset record carries words")
    return (kind, source, depth, move, node_id, words)


def log2_bucket(distance):
    if distance <= 0:
        return 0
    return distance.bit_length()


def lru_access(table, key, capacity):
    if key in table:
        table[key] = table.pop(key)
        return True
    if len(table) >= capacity:
        table.pop(next(iter(table)))
    table[key] = None
    return False


def simulate_fully_associative(key_ids, capacity):
    table = {}
    hits = 0
    for key in key_ids:
        if lru_access(table, key, capacity):
            hits += 1
    return hits


def simulate_set_associative(key_ids, set_indexes, capacity, ways):
    tables = {}
    hits = 0
    for key, full_index in zip(key_ids, set_indexes):
        table = tables.get(full_index)
        if table is None:
            table = {}
            tables[full_index] = table
        if lru_access(table, key, ways):
            hits += 1
    return hits


def scan_pass(path, header_size, max_records, chunk_records, spool_handle):
    board_to_id = {}
    last = {}
    last_parent = {}
    parent_first = {}
    parent_births = []
    req_hist = {}
    parent_hist = {}
    matrix = {}
    live_nodes = {}
    live_boards = Counter()
    record_count = 0
    eval_total = 0
    node_total = 0
    reset_total = 0
    distinct_boards = 0
    repeats = 0
    same_parent = 0
    cross_parent = 0
    attainable = 0
    attainable_cross = 0
    warm_eval = 0
    warm_node = 0
    warm_reset = 0
    measured = False
    epoch = 0
    out = bytearray()
    with open(path, "rb") as handle:
        handle.seek(header_size)
        while True:
            want = chunk_records
            if max_records is not None:
                if record_count >= max_records:
                    break
                if want > max_records - record_count:
                    want = max_records - record_count
            buf = handle.read(want * RECORD_SIZE)
            if not buf:
                break
            if len(buf) % RECORD_SIZE != 0:
                raise ValueError("truncated record stream")
            for off in range(0, len(buf), RECORD_SIZE):
                kind = buf[off]
                if kind > KIND_RESET:
                    raise ValueError("unknown record kind")
                if buf[off + 3] != 0 or buf[off + 6] != 0:
                    raise ValueError("reserved byte nonzero")
                if buf[off + 7] != 0:
                    raise ValueError("reserved byte nonzero")
                move = buf[off + 4] | (buf[off + 5] << 8)
                record_count += 1
                if move == MOVE_WARMUP:
                    if kind == KIND_EVAL:
                        warm_eval += 1
                    elif kind == KIND_NODE:
                        warm_node += 1
                    else:
                        warm_reset += 1
                    continue
                if not measured:
                    measured = True
                    live_nodes.clear()
                    live_boards.clear()
                    board_to_id.clear()
                    last.clear()
                    last_parent.clear()
                    parent_first.clear()
                    del parent_births[:]
                node_id = int.from_bytes(buf[off + 8:off + 12], "little")
                if buf[off + 12:off + 16] != ZERO4:
                    raise ValueError("reserved word nonzero")
                if kind == KIND_RESET:
                    if buf[off + 16:off + 80] != ZERO64:
                        raise ValueError("reset record carries words")
                    live_nodes = {i: k for i, k in live_nodes.items()
                                  if i < node_id}
                    live_boards = Counter(live_nodes.values())
                    reset_total += 1
                    epoch = reset_total
                elif kind == KIND_NODE:
                    panel = bytes(buf[off + 16:off + 80])
                    if node_id in live_nodes:
                        old_key = live_nodes[node_id]
                        remaining = live_boards[old_key] - 1
                        if remaining <= 0:
                            del live_boards[old_key]
                        else:
                            live_boards[old_key] = remaining
                    live_nodes[node_id] = panel
                    live_boards[panel] = live_boards.get(panel, 0) + 1
                    node_total += 1
                else:
                    words = struct.unpack_from("<8Q", buf, off + 16)
                    panel = bytes(buf[off + 16:off + 80])
                    key_id = board_to_id.get(panel)
                    if key_id is None:
                        key_id = distinct_boards
                        board_to_id[panel] = key_id
                        distinct_boards += 1
                    out += SPOOL_FIELDS.pack(key_id,
                                             occupancy_hash(words),
                                             occupancy_fingerprint(words))
                    parent_key = (epoch << 32) | node_id
                    prev = last.get(key_id)
                    if prev is not None:
                        repeats += 1
                        gap = eval_total - (prev >> 32) - 1
                        bucket = log2_bucket(gap)
                        req_hist[bucket] = req_hist.get(bucket, 0) + 1
                        if parent_key == last_parent.get(key_id):
                            same_parent += 1
                        else:
                            cross_parent += 1
                            born_after = (len(parent_births)
                                          - bisect_right(parent_births,
                                                         prev >> 32))
                            parent_hist[born_after] = parent_hist.get(
                                born_after, 0) + 1
                        if panel in live_boards:
                            attainable += 1
                            if parent_key != last_parent.get(key_id):
                                attainable_cross += 1
                    last[key_id] = (eval_total << 32) | node_id
                    last_parent[key_id] = parent_key
                    if parent_key not in parent_first:
                        parent_first[parent_key] = eval_total
                        parent_births.append(eval_total)
                    source = buf[off + 1]
                    depth = buf[off + 2]
                    matrix_key = "%d/%d" % (source, depth)
                    matrix[matrix_key] = matrix.get(matrix_key, 0) + 1
                    eval_total += 1
            spool_handle.write(out)
            del out[:]
    if out:
        spool_handle.write(out)
    spool_handle.flush()
    return {
        "record_count": record_count,
        "eval_total": eval_total,
        "node_total": node_total,
        "reset_total": reset_total,
        "distinct_boards": distinct_boards,
        "repeats": repeats,
        "same_parent": same_parent,
        "cross_parent": cross_parent,
        "requesting_parents": len(parent_first),
        "attainable": attainable,
        "attainable_cross": attainable_cross,
        "warm_eval": warm_eval,
        "warm_node": warm_node,
        "warm_reset": warm_reset,
        "req_hist": req_hist,
        "parent_hist": parent_hist,
        "matrix": matrix,
    }


def fa_pass(spool_path, capacities):
    tables = []
    for capacity in capacities:
        tables.append([capacity, {}, 0])
    with open(spool_path, "rb") as handle:
        while True:
            buf = handle.read(SPOOL_READ_RECORDS * SPOOL_FIELDS.size)
            if not buf:
                break
            for key_id, _oh, _fp in SPOOL_FIELDS.iter_unpack(buf):
                for entry in tables:
                    if lru_access(entry[1], key_id, entry[0]):
                        entry[2] += 1
    return {str(entry[0]): entry[2] for entry in tables}


def assoc_pass(spool_path, ways_list, caps_list):
    configs = []
    for ways in ways_list:
        for capacity in caps_list:
            sets = capacity // ways
            configs.append(["%d-way/%d" % (ways, capacity), sets, ways,
                            {}, 0])
    by_sets = {}
    for config in configs:
        by_sets.setdefault(config[1], []).append(config)
    with open(spool_path, "rb") as handle:
        while True:
            buf = handle.read(SPOOL_READ_RECORDS * SPOOL_FIELDS.size)
            if not buf:
                break
            for key_id, oh, _fp in SPOOL_FIELDS.iter_unpack(buf):
                for sets, group in by_sets.items():
                    index = oh & (sets - 1)
                    for config in group:
                        outer = config[3]
                        table = outer.get(index)
                        if table is None:
                            table = {}
                            outer[index] = table
                        if lru_access(table, key_id, config[2]):
                            config[4] += 1
    return {config[0]: config[4] for config in configs}


def count_sorted_groups(ordered):
    groups = 0
    cur_fp = None
    cur_id = None
    multi = False
    for fp, key_id in ordered:
        if fp != cur_fp:
            if multi:
                groups += 1
            cur_fp = fp
            cur_id = key_id
            multi = False
        elif key_id != cur_id:
            cur_id = key_id
            multi = True
    if multi:
        groups += 1
    return groups


def run_writer(pairs, work_dir):
    run_file = tempfile.NamedTemporaryFile(prefix="fp_", suffix=".bin",
                                           dir=work_dir, delete=False)
    try:
        for pair in pairs:
            run_file.write(PAIR_FIELDS.pack(pair[0], pair[1]))
        run_file.flush()
        return run_file.name
    finally:
        run_file.close()


def run_reader(run_path):
    with open(run_path, "rb") as handle:
        while True:
            buf = handle.read(SPOOL_READ_RECORDS * PAIR_FIELDS.size)
            if not buf:
                break
            for pair in PAIR_FIELDS.iter_unpack(buf):
                yield pair


def collision_pass(spool_path, work_dir, sort_run_records):
    run_paths = []
    try:
        with open(spool_path, "rb") as handle:
            while True:
                buf = handle.read(sort_run_records * SPOOL_FIELDS.size)
                if not buf:
                    break
                pairs = [(fp, key_id)
                         for key_id, _oh, fp in SPOOL_FIELDS.iter_unpack(buf)]
                pairs.sort()
                run_paths.append(run_writer(pairs, work_dir))
        if not run_paths:
            return 0
        streams = [run_reader(p) for p in run_paths]
        merged = heapq.merge(*streams, key=lambda pair: pair[0])
        return count_sorted_groups(merged)
    finally:
        for run_path in run_paths:
            try:
                os.unlink(run_path)
            except OSError:
                pass


def analyze_trace(path, fa_capacities, assoc_ways, assoc_capacities,
                  max_records=None, work_dir=None,
                  chunk_records=CHUNK_RECORDS_DEFAULT,
                  sort_run_records=SORT_RUN_RECORDS_DEFAULT):
    with open(path, "rb") as handle:
        header = parse_header(handle.read(HEADER_SIZE))
    spool_file = tempfile.NamedTemporaryFile(prefix="req_", suffix=".bin",
                                             dir=work_dir, delete=False)
    spool_path = spool_file.name
    try:
        scan = scan_pass(path, HEADER_SIZE, max_records, chunk_records,
                         spool_file)
        spool_file.close()
        spool_bytes = os.path.getsize(spool_path)
        distinct = scan["distinct_boards"]
        full_capacity = 1
        while full_capacity < distinct:
            full_capacity *= 2
        fa_caps = list(dict.fromkeys(list(fa_capacities)
                                       + list(assoc_capacities)
                                       + [full_capacity]))
        fa_all = fa_pass(spool_path, fa_caps)
        calibration_hits = fa_all[str(full_capacity)]
        fa_hits = {str(k): fa_all[str(k)] for k in fa_capacities}
        gc.collect()
        assoc_hits = assoc_pass(spool_path, assoc_ways, assoc_capacities)
        gc.collect()
        collisions = collision_pass(spool_path, work_dir, sort_run_records)
        eval_total = scan["eval_total"]
        distinct = scan["distinct_boards"]
        fa_part = {}
        for cap in fa_capacities:
            hits = fa_all[str(cap)]
            fa_part[str(cap)] = {
                "hits": hits,
                "cold_misses": distinct,
                "capacity_misses": eval_total - hits - distinct,
                "conflict_misses": 0,
            }
        sa_part = {}
        for name in sorted(assoc_hits):
            cap = int(name.split("/")[1])
            full_hits = fa_all[str(cap)]
            hits = assoc_hits[name]
            sa_part[name] = {
                "hits": hits,
                "cold_misses": distinct,
                "capacity_misses": eval_total - full_hits - distinct,
                "conflict_misses": full_hits - hits,
            }
        for part in list(fa_part.values()) + list(sa_part.values()):
            check(part["capacity_misses"] >= 0, "nonnegative capacity")
            check(part["conflict_misses"] >= 0, "nonnegative conflict")
            check(part["hits"] + part["cold_misses"]
                  + part["capacity_misses"] + part["conflict_misses"]
                  == eval_total, "miss partition total")
    finally:
        try:
            os.unlink(spool_path)
        except OSError:
            pass
    report = {
        "trace": path,
        "header": header,
        "measured_only": True,
        "parent_identity": "reset-epoch-plus-nodeid",
        "parent_distance_definition": "parent distance is defined only for cross-parent repeats: the number of distinct request-bearing expansion parents first observed after the previous request for the same board and before the current request is processed; a current parent first seen on this request is excluded; same-parent repeats and first requests have no parent distance",
        "request_distance_definition": "request distance is the number of eval requests issued strictly between two consecutive requests for the same board, reported as log2 buckets",
        "parent_distance_scope": "request-bearing-parent (PROFILE row not compared)",
        "records": scan["record_count"],
        "records_scanned": scan["record_count"],
        "max_records": max_records,
        "eval_requests": eval_total,
        "node_records": scan["node_total"],
        "reset_records": scan["reset_total"],
        "distinct_boards": distinct,
        "compulsory_requests": distinct,
        "repeats": scan["repeats"],
        "same_parent_repeats": scan["same_parent"],
        "cross_parent_repeats": scan["cross_parent"],
        "requesting_parent_expansions": scan["requesting_parents"],
        "request_distance_log2_hist": {
            str(k): v for k, v in sorted(scan["req_hist"].items())},
        "parent_distance_hist": {
            str(k): v for k, v in sorted(scan["parent_hist"].items())},
        "source_depth_matrix": dict(sorted(scan["matrix"].items())),
        "fingerprint_collisions": collisions,
        "fully_associative_lru_hits": {
            str(k): fa_hits[str(k)] for k in fa_capacities},
        "set_associative_hits": dict(sorted(assoc_hits.items())),
        "fully_associative_miss_partition": fa_part,
        "set_associative_miss_partition": sa_part,
        "calibration_full_capacity": full_capacity,
        "calibration_hits": calibration_hits,
        "calibration_expected_hits": eval_total - distinct,
        "live_attainable_hits": scan["attainable"],
        "live_attainable_cross_hits": scan["attainable_cross"],
        "warmup_eval_skipped": scan["warm_eval"],
        "warmup_node_skipped": scan["warm_node"],
        "warmup_reset_skipped": scan["warm_reset"],
        "spool_bytes": spool_bytes,
    }
    return report


checks_made = [0]


def check(condition, message):
    checks_made[0] += 1
    if not condition:
        raise AssertionError(message)


def check_partitions(rep):
    total = rep["eval_requests"]
    distinct = rep["distinct_boards"]
    fa_part = rep["fully_associative_miss_partition"]
    for cap in sorted(fa_part):
        part = fa_part[cap]
        check(part["hits"] + part["cold_misses"]
              + part["capacity_misses"] + part["conflict_misses"]
              == total, "fa partition total")
        check(part["cold_misses"] == distinct, "fa cold")
    for name in sorted(rep["set_associative_miss_partition"]):
        part = rep["set_associative_miss_partition"][name]
        check(part["hits"] + part["cold_misses"]
              + part["capacity_misses"] + part["conflict_misses"]
              == total, "sa partition total")
        check(part["cold_misses"] == distinct, "sa cold")
        cap = name.split("/")[1]
        check(part["capacity_misses"] == fa_part[cap]["capacity_misses"],
              "sa capacity matches fa")


def encode_record(kind, source, depth, move, node_id, words):
    return REC_FIELDS.pack(kind, source, depth, 0, move, 0, node_id, 0,
                           *words)


def write_trace_file(path, header_fields, records):
    seed, iters, maxdepth, warmup, moves, hold = header_fields
    with open(path, "wb") as handle:
        handle.write(MAGIC)
        handle.write(struct.pack("<H", VERSION))
        handle.write(bytes(2))
        handle.write(struct.pack("<IQQQQ", seed, iters, maxdepth, warmup,
                                 moves))
        handle.write(bytes([hold]))
        handle.write(bytes(15))
        for record in records:
            handle.write(encode_record(*record))


def reference_fa(stream, capacity):
    order = []
    present = set()
    hits = 0
    for key in stream:
        if key in present:
            hits += 1
            order.remove(key)
            order.append(key)
        else:
            if len(order) >= capacity:
                present.discard(order.pop(0))
            order.append(key)
            present.add(key)
    return hits


def run_selftest():
    checks_made[0] = 0
    check(rotl64(1, 1) == 2, "rotl")
    check(rotl64(1, 0) == 1, "rotl zero")
    ref = 1469598103934665603
    for _ in range(8):
        ref ^= 0
        ref = (ref * 1099511628211) & MASK64
    check(occupancy_hash((0,) * 8) == ref, "fnv zeros")
    check(log2_bucket(0) == 0, "bucket zero")
    check(log2_bucket(1) == 1, "bucket one")
    check(log2_bucket(8) == 4, "bucket eight")

    stream_a = [1, 2, 3, 1, 2, 3, 1]
    check(simulate_fully_associative(stream_a, 2) == 0, "fa thrash")
    check(simulate_fully_associative(stream_a, 8) == 4, "fa cold only")
    check(simulate_fully_associative(stream_a, 3) == 4, "fa exact fit")

    converter = {1: 0, 2: 1, 3: 2, 4: 3}
    stream_b = [converter[v] for v in [1, 2, 1, 3, 1, 4, 1]]
    hashes_b = [v * 2 for v in stream_b]
    sets_b = [h & 1 for h in hashes_b]
    fa_b = simulate_fully_associative(stream_b, 4)
    dm_b = simulate_set_associative(stream_b, sets_b, 2, 1)
    check(fa_b == 3, "fa small")
    check(dm_b <= fa_b, "assoc bounded by fa")

    lcg = 12345
    stream_c = []
    for _ in range(5000):
        lcg = (lcg * 1103515245 + 12345) & 0x7FFFFFFF
        stream_c.append(lcg % 50)
    check(simulate_fully_associative(stream_c, 7) ==
          reference_fa(stream_c, 7), "lru reference")
    check(simulate_fully_associative(iter(stream_c), 7) ==
          reference_fa(stream_c, 7), "lru stream input")
    fa7 = simulate_fully_associative(stream_c, 7)
    fa64 = simulate_fully_associative(stream_c, 64)
    check(fa64 >= fa7, "fa monotone capacity")
    idx7 = [(occupancy_hash((v,) * 8)) & 3 for v in stream_c]
    a2 = simulate_set_associative(stream_c, idx7, 8, 2)
    check(a2 <= fa7, "assoc bounded strided")
    a1 = simulate_set_associative(stream_c, idx7, 8, 1)
    check(a1 <= a2, "assoc monotone ways")

    import tempfile
    words_x = tuple(range(8))
    words_y = tuple(range(100, 108))
    records = []
    records.append((KIND_NODE, 0, 0, 0, 0, words_x))
    records.append((KIND_EVAL, 0, 3, 0, 7, words_x))
    records.append((KIND_EVAL, 0, 3, 0, 7, words_y))
    records.append((KIND_EVAL, 1, 4, 0, 9, words_x))
    records.append((KIND_RESET, 0, 0, 0, 0, (0,) * 8))
    records.append((KIND_EVAL, 0, 3, 1, 11, words_x))
    with tempfile.NamedTemporaryFile(
            suffix=".evtrc", delete=False) as tmp:
        tmp_path = tmp.name
    try:
        write_trace_file(tmp_path, (1, 10, 6, 0, 2, 1), records)
        report = analyze_trace(tmp_path, [4], [1], [4], chunk_records=2,
                               sort_run_records=3)
        twin = analyze_trace(tmp_path, [4], [1], [4], chunk_records=997,
                             sort_run_records=1000000)
    finally:
        os.unlink(tmp_path)
    check(report["eval_requests"] == 4, "eval count")
    check(report["distinct_boards"] == 2, "distinct count")
    check(report["repeats"] == 2, "repeat count")
    check(report["same_parent_repeats"] == 0, "same parent")
    check(report["cross_parent_repeats"] == 2, "cross parent")
    check(report["live_attainable_hits"] == 1, "live attainable")
    check(report["live_attainable_cross_hits"] == 1, "live cross")
    check(report["fingerprint_collisions"] == 0, "no collisions")
    check(report["calibration_hits"] == report["calibration_expected_hits"],
          "calibration")
    check(report["fully_associative_lru_hits"]["4"] == 2, "fa tiny")
    check(report["header"]["seed"] == 1, "header seed")
    check(report["warmup_eval_skipped"] == 0, "no warmup skipped")
    check(report == twin, "chunk invariance")
    check(report["requesting_parent_expansions"] == 3, "requesting count")
    check_partitions(report)
    check(report["fully_associative_miss_partition"]["4"][
        "conflict_misses"] == 0, "fa no conflict")

    words_a = tuple(range(8))
    words_b = tuple(range(100, 108))
    words_c = tuple(range(200, 208))
    words_r = (5,) * 8
    mixed = []
    mixed.append((KIND_RESET, 0, 0, MOVE_WARMUP, 0, (0,) * 8))
    mixed.append((KIND_NODE, 0, 0, MOVE_WARMUP, 5, words_a))
    mixed.append((KIND_EVAL, 0, 3, MOVE_WARMUP, 5, words_a))
    mixed.append((KIND_EVAL, 0, 3, MOVE_WARMUP, 5, words_a))
    mixed.append((KIND_EVAL, 0, 3, MOVE_WARMUP, 6, words_a))
    mixed.append((KIND_RESET, 0, 0, 0, 0, (0,) * 8))
    mixed.append((KIND_NODE, 0, 0, 0, 0, words_r))
    mixed.append((KIND_EVAL, 0, 3, 0, 7, words_a))
    mixed.append((KIND_EVAL, 0, 3, 0, 7, words_b))
    mixed.append((KIND_EVAL, 0, 3, 0, 7, words_a))
    mixed.append((KIND_EVAL, 0, 4, 0, 9, words_a))
    mixed.append((KIND_RESET, 0, 0, 1, 1, (0,) * 8))
    mixed.append((KIND_NODE, 0, 0, 1, 0, words_a))
    mixed.append((KIND_EVAL, 0, 3, 1, 3, words_a))
    mixed.append((KIND_EVAL, 0, 3, 1, 3, words_c))
    with tempfile.NamedTemporaryFile(
            suffix=".evtrc", delete=False) as tmp:
        warm_path = tmp.name
    try:
        write_trace_file(warm_path, (1, 10, 6, 1, 2, 1), mixed)
        warm = analyze_trace(warm_path, [8], [1], [8], chunk_records=3,
                             sort_run_records=4)
    finally:
        os.unlink(warm_path)
    check(warm["warmup_eval_skipped"] == 3, "warmup eval skipped")
    check(warm["warmup_node_skipped"] == 1, "warmup node skipped")
    check(warm["warmup_reset_skipped"] == 1, "warmup reset skipped")
    check(warm["eval_requests"] == 6, "measured eval count")
    check(warm["node_records"] == 2, "measured node count")
    check(warm["reset_records"] == 2, "measured reset count")
    check(warm["distinct_boards"] == 3, "warmup boards excluded")
    check(warm["repeats"] == 3, "measured repeats")
    check(warm["same_parent_repeats"] == 1, "measured same parent")
    check(warm["cross_parent_repeats"] == 2, "measured cross parent")
    check(warm["live_attainable_hits"] == 1, "warmup live state reset")
    check(warm["live_attainable_cross_hits"] == 1, "measured live cross")
    check(warm["calibration_hits"] == warm["calibration_expected_hits"],
          "warmup calibration")
    check(warm["fully_associative_lru_hits"]["8"] == 3, "warmup fa exact")
    check(warm["request_distance_log2_hist"] == {"0": 2, "1": 1},
          "warmup req distance")
    check(sum(int(v) for v in warm["parent_distance_hist"].values()) == 2,
          "warmup parent distance total")
    check(sum(warm["source_depth_matrix"].values()) == 6,
          "warmup matrix total")
    check(warm["requesting_parent_expansions"] == 3, "warm requesting")
    check_partitions(warm)
    words_q = tuple(range(300, 308))
    words_s = (9,) * 8
    regen = []
    regen.append((KIND_NODE, 0, 0, 0, 0, words_s))
    regen.append((KIND_EVAL, 0, 3, 0, 5, words_q))
    regen.append((KIND_EVAL, 0, 3, 0, 5, words_q))
    regen.append((KIND_RESET, 0, 0, 1, 1, (0,) * 8))
    regen.append((KIND_NODE, 0, 0, 1, 0, words_q))
    regen.append((KIND_EVAL, 0, 3, 1, 5, words_q))
    regen.append((KIND_EVAL, 0, 3, 1, 5, words_q))
    with tempfile.NamedTemporaryFile(
            suffix=".evtrc", delete=False) as tmp:
        regen_path = tmp.name
    try:
        write_trace_file(regen_path, (1, 10, 6, 0, 2, 1), regen)
        reg = analyze_trace(regen_path, [8], [1], [8], chunk_records=2,
                            sort_run_records=4)
        reg_twin = analyze_trace(regen_path, [8], [1], [8],
                                 chunk_records=997,
                                 sort_run_records=1000000)
    finally:
        os.unlink(regen_path)
    check(reg["eval_requests"] == 4, "regen eval count")
    check(reg["distinct_boards"] == 1, "regen distinct")
    check(reg["repeats"] == 3, "regen repeats")
    check(reg["same_parent_repeats"] == 2, "regen same across reset")
    check(reg["cross_parent_repeats"] == 1, "regen cross across reset")
    check(reg["requesting_parent_expansions"] == 2, "regen requesting")
    check(sum(int(v) for v in reg["parent_distance_hist"].values()) == 1,
          "regen parent distance total")
    check(reg == reg_twin, "regen chunk invariance")
    check_partitions(reg)
    check(reg["fully_associative_lru_hits"]["8"] == 3, "regen fa exact")
    row_all = ("PROFILE_V3 moves=2 evals=4 eval_memo_hits=2 parents=2 "
               "warmup_moves=0")
    check_profile_row(reg, row_all)
    check(reg["parent_distance_scope"] == "all-expansions",
          "regen all expansions scope")
    row_part = ("PROFILE_V3 moves=2 evals=4 eval_memo_hits=2 parents=99 "
                "warmup_moves=0")
    check_profile_row(reg, row_part)
    check(reg["parent_distance_scope"] == "request-bearing-parent",
          "regen partial scope")
    check(reg["profile_parents"] == 99, "regen profile parents")
    print("SELFTEST OK checks=%d" % checks_made[0])


def parse_int_list(text):
    return [int(part) for part in text.split(",") if part.strip()]


def parse_profile_row(text):
    fields = {}
    for token in text.strip().split():
        if "=" not in token:
            continue
        key, _, value = token.partition("=")
        fields[key] = value
    return fields


def check_profile_row(report, text):
    row = parse_profile_row(text)
    check("evals" in row, "row carries evals")
    check(int(row["evals"]) == report["eval_requests"], "row evals match")
    check(report["repeats"] + report["distinct_boards"] ==
          report["eval_requests"], "repeat accounting")
    check(report["calibration_hits"] ==
          report["calibration_expected_hits"], "row calibration")
    if int(report["header"]["warmup_moves"]) > 0:
        check(report["warmup_eval_skipped"] > 0, "warmup excluded")
    check(sum(report["source_depth_matrix"].values()) ==
          report["eval_requests"], "row matrix total")
    if "eval_memo_hits" in row:
        check(int(row["eval_memo_hits"]) == report["same_parent_repeats"],
              "same parent equals memo hits")
    if "parents" in row:
        report["profile_parents"] = int(row["parents"])
        if int(row["parents"]) == report["requesting_parent_expansions"]:
            report["parent_distance_scope"] = "all-expansions"
        else:
            report["parent_distance_scope"] = "request-bearing-parent"
    print("ROWCHECK OK eval=%d distinct=%d repeats=%d same=%d cross=%d "
          "attainable=%d collisions=%d requesting=%d scope=%s" % (
              report["eval_requests"], report["distinct_boards"],
              report["repeats"], report["same_parent_repeats"],
              report["cross_parent_repeats"],
              report["live_attainable_hits"],
              report["fingerprint_collisions"],
              report["requesting_parent_expansions"],
              report["parent_distance_scope"]))


def verify_bounds(path, max_records, fa_capacities, assoc_ways,
                  assoc_capacities, as_limit_mib, rss_cap_mib, work_dir,
                  chunk_records, sort_run_records):
    as_bytes = as_limit_mib * 1048576
    resource.setrlimit(resource.RLIMIT_AS, (as_bytes, as_bytes))
    report = analyze_trace(path, fa_capacities, assoc_ways, assoc_capacities,
                           max_records=max_records, work_dir=work_dir,
                           chunk_records=chunk_records,
                           sort_run_records=sort_run_records)
    peak_kib = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
    check(peak_kib <= rss_cap_mib * 1024,
          "peak rss %d KiB exceeds %d MiB cap" % (peak_kib, rss_cap_mib))
    print("BOUNDS OK peak_rss_kib=%d cap_mib=%d records=%d eval=%d" % (
        peak_kib, rss_cap_mib, report["records_scanned"],
        report["eval_requests"]))
    return 0


def main(argv):
    parser = argparse.ArgumentParser(prog="eval_reuse_analyze.py")
    parser.add_argument("trace", nargs="?", default="")
    parser.add_argument("--summary", default="")
    parser.add_argument("--fa", default="1024,4096,16384,65536,262144,1048576")
    parser.add_argument("--ways", default="1,2,4,8")
    parser.add_argument("--assoc-caps",
                        default="1024,4096,16384,65536,262144,1048576")
    parser.add_argument("--selftest", action="store_true")
    parser.add_argument("--max-records", default="", type=str)
    parser.add_argument("--work-dir", default="")
    parser.add_argument("--chunk-records", default=CHUNK_RECORDS_DEFAULT,
                        type=int)
    parser.add_argument("--sort-run-records",
                        default=SORT_RUN_RECORDS_DEFAULT, type=int)
    parser.add_argument("--check-profile-row", default="")
    parser.add_argument("--verify-bounds", action="store_true")
    parser.add_argument("--as-limit-mib", default=768, type=int)
    parser.add_argument("--rss-cap-mib", default=512, type=int)
    args = parser.parse_args(argv)
    if args.selftest:
        run_selftest()
        return 0
    if not args.trace:
        parser.error("trace path required")
    max_records = int(args.max_records) if args.max_records else None
    if max_records is not None and max_records < 0:
        parser.error("max records must be nonnegative")
    work_dir = args.work_dir if args.work_dir else None
    if args.verify_bounds:
        return verify_bounds(args.trace, max_records,
                             parse_int_list(args.fa),
                             parse_int_list(args.ways),
                             parse_int_list(args.assoc_caps),
                             args.as_limit_mib, args.rss_cap_mib, work_dir,
                             args.chunk_records, args.sort_run_records)
    report = analyze_trace(args.trace, parse_int_list(args.fa),
                           parse_int_list(args.ways),
                           parse_int_list(args.assoc_caps),
                           max_records=max_records, work_dir=work_dir,
                           chunk_records=args.chunk_records,
                           sort_run_records=args.sort_run_records)
    total = report["eval_requests"]
    distinct = report["distinct_boards"]
    repeats = report["repeats"]
    same = report["same_parent_repeats"]
    cross = report["cross_parent_repeats"]
    attain = report["live_attainable_hits"]
    attain_cross = report["live_attainable_cross_hits"]
    print("eval=%d distinct=%d repeats=%d same=%d cross=%d attainable=%d attainable_cross=%d collisions=%d requesting=%d" % (
        total, distinct, repeats, same, cross, attain,
        attain_cross, report["fingerprint_collisions"],
        report["requesting_parent_expansions"]))
    if args.check_profile_row:
        check_profile_row(report, args.check_profile_row)
    if args.summary:
        with open(args.summary, "w") as handle:
            json.dump(report, handle, indent=2, sort_keys=True)
            handle.write("\n")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
