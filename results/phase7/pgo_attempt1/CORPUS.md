# PGO attempt 1 corpus definition, frozen 2026-09-08 before any instrumented build

This document freezes the training corpus before building, per the owner's
bounded PGO authorization. Qualification seeds 1, 2, and 3 are excluded from
training. Every run uses the frozen production parameters
`/home/icly/Documents/tetris_ai_runner/artifacts/frozen_29d.bin`
(sha256 `ea95ba584f4eb5a7234bf0fbdd68fb422e00e29d13373e4df9e6b9ea2ca98037`)
passed by absolute path, `TETRIS_AI_PARAM_FILE` unset, telemetry off, CPU 7
pinned. The instrumented binary is the same `tetris_profile_value` target
built from HEAD `1b046db` with the `linux-gcc-self-release` flags
(`-O3 -march=native -flto=2`) plus
`-fprofile-generate=/home/icly/Documents/tetris_ai_runner/results/phase7/pgo_attempt1/gcda`.

Corpus commands, executed in order with the instrumented build:

1. `--seed 4 --warmup-moves 20 --moves 200 --iters 1000 --maxdepth 6 --telemetry off --quiet --quiet-version 3` (fixed work)
2. `--seed 5 --warmup-moves 20 --moves 200 --iters 1000 --maxdepth 6 --telemetry off --quiet --quiet-version 3` (fixed work)
3. `--seed 6 --warmup-moves 20 --moves 200 --iters 1000 --maxdepth 6 --telemetry off --quiet --quiet-version 3` (fixed work)
4. `--seed 7 --warmup-moves 20 --moves 100 --iters 1000 --maxdepth 6 --telemetry off --quiet --quiet-version 3` (fixed work, shorter horizon for path diversity)
5. `--seed 4 --warmup-moves 20 --moves 200 --ms 20 --maxdepth 6 --telemetry off --quiet --quiet-version 3` (timed)
6. `--seed 5 --warmup-moves 20 --moves 200 --ms 20 --maxdepth 6 --telemetry off --quiet --quiet-version 3` (timed)

Held-out screening seed: 8 (not in the corpus, not a qualification seed).
Screening is candidate-to-candidate, the PGO build against the frozen
non-PGO candidate `tetris_profile_value.candidate-2026-09-08`, three
interleaved ABBA pairs at 200 moves, telemetry off. The advancement bar is
approximately 3 percent improvement on both total time and p95, without
counter drift. Before screening, the PGO build must reproduce the non-PGO
counters bit for bit on an 80-move count twin
(`--moves 80 --iters 1000 --telemetry on --timers off`).

Compiler: GCC 16.2.1 20260810. The profile-use build uses the same preset
flags plus `-fprofile-use=<gcda directory>`. GCC stays the binding compiler;
Clang correctness builds are unaffected. At most two corpus attempts are
authorized; this is attempt 1.
