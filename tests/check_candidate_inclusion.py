#!/usr/bin/env python3
import collections
import subprocess
import sys

EXPECTED_PIECES = frozenset("TZSJLOI")
EXPECTED_BOARD_COUNT = 37


def collect(executable, extra):
    out = subprocess.run([executable, "--reps", "0", "--warmup", "0", "--dump-keys"] + extra,
        capture_output=True, text=True, check=True).stdout
    keys = {}
    rows = {}
    corpus = ""
    for line in out.splitlines():
        parts = line.split()
        if not parts:
            continue
        if parts[0] == "KEY" and len(parts) == 4:
            keys.setdefault((parts[1], int(parts[2])), set()).add(parts[3])
        elif parts[0] == "CASE" and len(parts) == 5:
            rows[(parts[1], int(parts[2]))] = int(parts[3])
        elif parts[0] == "CORPUS":
            corpus = " ".join(parts[1:])
    for case in rows:
        keys.setdefault(case, set())
    denom = [case for case, count in rows.items() if count != len(keys[case])]
    for case in denom:
        print(f"DENOM {case[0]} {case[1]} row count {rows[case]} differs from dumped keys {len(keys[case])}")
    return keys, rows, len(denom), corpus


def require_corpus(rows, name):
    pieces = {piece for piece, _ in rows}
    boards = {board for _, board in rows}
    problems = []
    if pieces != EXPECTED_PIECES:
        problems.append(f"pieces seen {sorted(pieces)}")
    if len(boards) != EXPECTED_BOARD_COUNT:
        problems.append(f"{len(boards)} distinct boards")
    if len(rows) != len(EXPECTED_PIECES) * EXPECTED_BOARD_COUNT:
        problems.append(f"{len(rows)} cases")
    for piece in sorted(EXPECTED_PIECES):
        for board in sorted(boards):
            if (piece, board) not in rows:
                problems.append(f"missing case {piece} board {board}")
    if problems:
        raise SystemExit(f"{name} corpus incomplete: " + "; ".join(problems))


def main():
    if len(sys.argv) < 3:
        raise SystemExit("usage: check_candidate_inclusion.py <new exe> <comparator exe> [shared args...]")
    new_exe, comparator_exe = sys.argv[1], sys.argv[2]
    extra = sys.argv[3:]
    new_keys, new_rows, new_denom, new_corpus = collect(new_exe, extra)
    other_keys, other_rows, other_denom, other_corpus = collect(comparator_exe, extra)
    require_corpus(new_rows, "new")
    require_corpus(other_rows, "comparator")

    missing_cases = 0
    missing_keys = 0
    extra_keys = 0
    per_piece = collections.defaultdict(lambda: [0, 0])
    for case in sorted(other_keys):
        if case not in new_keys:
            missing_cases += 1
            print(f"MISSING CASE {case}")
            continue
        lost = other_keys[case] - new_keys[case]
        gained = new_keys[case] - other_keys[case]
        missing_keys += len(lost)
        extra_keys += len(gained)
        per_piece[case[0]][0] += len(lost)
        per_piece[case[0]][1] += len(gained)
        for key in sorted(lost):
            print(f"LOST {case[0]} {case[1]} {key}")
    missing_keys += new_denom + other_denom
    if set(new_rows) != set(other_rows):
        print(f"CASE SET mismatch: {len(new_rows)} new rows versus {len(other_rows)} comparator rows")
        missing_cases += 1

    total_new = sum(len(s) for s in new_keys.values())
    total_other = sum(len(s) for s in other_keys.values())
    print(f"INCLUSION cases {len(other_keys)} comparator_keys {total_other} new_keys {total_new} "
        f"contained {total_other - missing_keys} of {total_other} extra {extra_keys} missing {missing_keys}")
    for piece in sorted(per_piece):
        print(f"INCLUSION piece {piece} lost {per_piece[piece][0]} gained {per_piece[piece][1]}")
    print(f"CORPUS new {new_corpus}")
    print(f"CORPUS comparator {other_corpus}")
    print(f"GRID {len(EXPECTED_PIECES)} pieces {EXPECTED_BOARD_COUNT} boards complete on both sides")
    print(f"DENOM checked {len(new_rows)} new and {len(other_rows)} comparator rows")
    if missing_keys or missing_cases:
        raise SystemExit(f"comparator emitted {missing_keys} keys the new implementation does not contain")
    print("PASS comparator candidate set is contained in the new implementation, key by key")


main()
