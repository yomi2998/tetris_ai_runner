import os
import struct
import subprocess
import sys
import tempfile

MASK64 = 0xFFFFFFFFFFFFFFFF
MAGIC = b"EVTRC02\x00"


def board_words(index):
    return tuple((index * 2654435761 + slot * 40503) & MASK64
                 for slot in range(8))


def main():
    analyzer = sys.argv[1]
    tmpdir = tempfile.mkdtemp(prefix="eval_reuse_bounds_")
    trace_path = os.path.join(tmpdir, "synthetic.evtrc")
    records = []
    warm_parents = [5, 6, 7]
    for i in range(2000):
        move = 0xFFFF
        parent = warm_parents[i % 3]
        panel = board_words(i % 900)
        if i % 10 == 0:
            records.append((2, 0, 0, move, 0, (0,) * 8))
        if i % 25 == 0:
            records.append((1, 0, 0, move, parent, panel))
        records.append((0, i % 2, 3 + (i % 4), move, parent, panel))
    for move in range(4):
        records.append((2, 0, 0, move, 0, (0,) * 8))
        records.append((1, 0, 0, move, 0, board_words(100000 + move)))
        for i in range(2000):
            parent = (i * 7 + move) % 64
            panel = board_words((i * 13 + move * 101) % 1500)
            records.append((0, i % 2, 2 + (i % 5), move, parent, panel))
    with open(trace_path, "wb") as handle:
        handle.write(MAGIC)
        handle.write(struct.pack("<H", 1))
        handle.write(bytes(2))
        handle.write(struct.pack("<IQQQQ", 1, 10, 6, 1, 4))
        handle.write(bytes([1]))
        handle.write(bytes(15))
        for kind, source, depth, move, node_id, words in records:
            handle.write(struct.pack("<BBBBHHII8Q", kind, source, depth, 0,
                                     move, 0, node_id, 0, *words))
    cmd = [sys.executable, analyzer, trace_path, "--verify-bounds",
           "--max-records", str(len(records)), "--fa", "4,64",
           "--ways", "1,2", "--assoc-caps", "4,64", "--as-limit-mib", "768",
           "--rss-cap-mib", "512", "--chunk-records", "512",
           "--sort-run-records", "2000", "--work-dir", tmpdir]
    done = subprocess.run(cmd, capture_output=True, text=True, timeout=600)
    sys.stdout.write(done.stdout)
    sys.stderr.write(done.stderr)
    if done.returncode != 0:
        print("BOUNDS CHECK FAILED")
        return 1
    if "BOUNDS OK" not in done.stdout:
        print("BOUNDS CHECK MISSING VERDICT")
        return 1
    print("BOUNDS CHECK OK records=%d" % len(records))
    return 0


if __name__ == "__main__":
    sys.exit(main())
