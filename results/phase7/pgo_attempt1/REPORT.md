# PGO attempt 1 report, 2026-09-08: rejected

## Setup

- Corpus: frozen in `CORPUS.md` before any instrumented build. Seeds 4, 5, 6, 7 (fixed work, 200/100 moves, 1000 iterations, depth 6) and seeds 4, 5 (timed 20 ms, 200 moves), all telemetry off, warmup 20, frozen production parameters by absolute path, CPU 7 pinned, `TETRIS_AI_PARAM_FILE` unset. Raw training rows: `corpus_rows.txt`.
- Instrumented build: HEAD `1b046db`, `linux-gcc-self-release` flags `-O3 -march=native -flto=2` plus `-fprofile-generate` into `gcda/`, built in `out/build/pgo-generate`. Instrumented binary sha256 `45fb81b1d70b8a3d501efb76fbdf6da6b0db4e83c729bc9b65a8066b4d1f37d3`.
- Profile data: three gcda files (tetris_engine.cpp, tetris_profile_value.cpp, toj_policy.cpp), 280 KiB total, hashes in `gcda.sha256`. The profile-use build consumed byte-identical copies renamed to the `pgo-use` object-path key in `gcda-use/`.
- Profile-use build: same preset flags plus `-fprofile-use=gcda-use`, built in `out/build/pgo-use`, no missing-profile warnings (verified on a forced recompile of the engine TU). Binary sha256 `12b3e108e58cff00259ce26ca09a6ff6c8dbb29d9dabfbd1d9b6881acf335498`.
- Compiler: GCC 16.2.1 20260810. Clang correctness builds unaffected; no Clang artifacts are shipped.

## Required gates

- Count ABBA (80 moves, telemetry on, timers off, PGO versus the frozen non-PGO candidate `tetris_profile_value.candidate-2026-09-08`): every counter field bit-identical across all four runs (evals, transitions, searches, parents, widening, landings, unique candidates, memo and cache counters, materialized nodes, merges, promotions refused, pending end max, memory fields). PGO changed no observable work.
- Held-out screening (seed 8, not in the corpus and not a qualification seed; three interleaved 200-move ABBA pairs, telemetry off, PGO over non-PGO):
  - Total pair ratios 1.05980, 1.06698, 1.05184. Median 1.05980: a 5.98 percent regression, not the roughly 3 percent improvement required.
  - p95 pair ratios 1.08353, 1.07187, 1.06415. Median 1.07187: a 7.19 percent regression.
  - Within-kind blocks were tight (PGO 19.547 to 19.865 s; non-PGO 18.444 to 18.886 s), so this is a consistent regression, not noise.

## Mechanism

A perf sample of the PGO build (seed 8, 100 moves, off-mode) against the retained-tree profile shows where the time went:

| Function | Non-PGO self-time | PGO self-time |
|---|---:|---:|
| Policy::evaluate | 15.31 | 22.61 (now a `.isra` clone) |
| Engine::promote | 5.46 | 19.46 |
| transition_known_lockout | 6.75 | 11.89 |
| search_materialize_inner | 2.52 | 9.95 |
| Engine::materialize | 12.40 | gone from the top (inlined into callers) |
| build_key, probe, scan_safe_rows | 4.80, 4.06, 3.68 | gone from the top (inlined into callers) |

PGO merged the small hot helpers into their callers and applied interprocedural scalar replacement clones. The enlarged hot functions lose the tight per-function code layout this engine's hot loops depend on; promote's meld loop, which the entry-array swap had just brought from 7.1 to 5.5 percent, is the clearest victim at 19.5 percent. The same sensitivity appeared in the earlier source experiments: forced `always_inline` on materialize regressed 9.6 percent, and the evaluate_once inline regressed 1.0 percent. The corpus is not the lever here; the transformation class is. The training profile was saturated by six full fixed-work runs and representative of the gate workload's exact shape, seeds excluded as required.

## Verdict

Rejected. The screening bar (approximately 3 percent improvement on both total and p95, no counter drift) failed in the wrong direction by 6 to 7 percent. Per the standing owner instruction, the campaign pauses rather than continuing. The second authorized training-corpus attempt is deliberately not spent: the mechanism evidence indicates a different corpus would reproduce the same inlining and cloning decisions on the same saturated profile shape, and the attempt is preserved for a future owner decision (for example after a flag amendment or on different hardware).

## Reproduction

```bash
cmake -S . -B out/build/pgo-generate -G Ninja \
  -DCMAKE_C_COMPILER=/usr/bin/gcc -DCMAKE_CXX_COMPILER=/usr/bin/g++ \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_FLAGS="-O3 -march=native -flto=2 -fprofile-generate=$PWD/results/phase7/pgo_attempt1/gcda"
cmake --build out/build/pgo-generate --target tetris_profile_value -j 2
# run the six corpus commands from CORPUS.md with out/build/pgo-generate/tetris_profile_value
cp results/phase7/pgo_attempt1/gcda-use/* results/phase7/pgo_attempt1/gcda-use/  # already mirrored
cmake -S . -B out/build/pgo-use -G Ninja \
  -DCMAKE_C_COMPILER=/usr/bin/gcc -DCMAKE_CXX_COMPILER=/usr/bin/g++ \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_FLAGS="-O3 -march=native -flto=2 -fprofile-use=$PWD/results/phase7/pgo_attempt1/gcda-use"
cmake --build out/build/pgo-use --target tetris_profile_value -j 2
```
