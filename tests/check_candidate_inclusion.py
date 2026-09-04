#!/usr/bin/env python3
import collections
import subprocess
import sys


def collect(executable, extra):
    out = subprocess.run([executable, "--reps", "0", "--warmup", "0", "--dump-keys"] + extra,
        capture_output=True, text=True, check=True).stdout
    keys = collections.defaultdict(set)
    rows = {}
    corpus = ""
    for line in out.splitlines():
        parts = line.split()
        if not parts:
            continue
        if parts[0] == "KEY" and len(parts) == 4:
            keys[(parts[1], int(parts[2]))].add(parts[3])
        elif parts[0] == "CASE" and len(parts) == 5:
            rows[(parts[1], int(parts[2]))] = int(parts[3])
        elif parts[0] == "CORPUS":
            corpus = " ".join(parts[1:])
    return keys, rows, corpus


def main():
    if len(sys.argv) < 3:
        raise SystemExit("usage: check_candidate_inclusion.py <new exe> <comparator exe> [shared args...]")
    new_exe, comparator_exe = sys.argv[1], sys.argv[2]
    extra = sys.argv[3:]
    new_keys, new_rows, new_corpus = collect(new_exe, extra)
    other_keys, other_rows, other_corpus = collect(comparator_exe, extra)

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
    for case, count in other_rows.items():
        if count != len(other_keys[case]):
            print(f"DENOM {case} row count {count} differs from dumped keys {len(other_keys[case])}")
            missing_keys += 1
    if new_rows != {c: len(new_keys[c]) for c in new_keys}:
        print("DENOM new side row counts differ from dumped keys")
        missing_keys += 1

    total_new = sum(len(s) for s in new_keys.values())
    total_other = sum(len(s) for s in other_keys.values())
    print(f"INCLUSION cases {len(other_keys)} comparator_keys {total_other} new_keys {total_new} "
        f"contained {total_other - missing_keys} of {total_other} extra {extra_keys} missing {missing_keys}")
    for piece in sorted(per_piece):
        print(f"INCLUSION piece {piece} lost {per_piece[piece][0]} gained {per_piece[piece][1]}")
    print(f"CORPUS new {new_corpus}")
    print(f"CORPUS comparator {other_corpus}")
    if missing_keys or missing_cases:
        raise SystemExit(f"comparator emitted {missing_keys} keys the new implementation does not contain")
    print("PASS comparator candidate set is contained in the new implementation, key by key")


main()
