# Hotspot attribution for the value engine (D, post-epoch candidate)

Attribution only. No performance claims, no verdicts, no pass/fail language.
All shares below are measured on the stated runs; every hypothesis is tagged
HYPOTHESIS. Measured numbers come from two independent instruments that agree:
component timers (PROFILE_V3 rows) and `perf` cycle sampling.

## 0. Anchors and integrity

| Artifact | Path | SHA-256 |
|---|---|---|
| D (post-epoch candidate) | `out/build/linux-gcc-self-release/tetris_profile_value` | `77fac930547cc3ff84ab6c7712f68a010ec13f88b89cb898e85efb25fc055d6d` |
| A (frozen 7.2E candidate, contrast only) | `/home/icly/Documents/tetris_ai_runner_results/phase7/tetris_profile_value.candidate` | `816bcd7d33a7996a207d1c8bbba8bcf838baccfc6246ecb084d445f3b49938fe` |
| Param file | `/home/icly/Documents/tetris_ai_runner/artifacts/frozen_29d.bin` | passed by absolute path (hash recorded in qualification protocol) |

Both hashes verified before any run; both matched. Tracked tree clean
(`git status --porcelain` shows only untracked scratch files, no tracked
modifications). Branch `fast-reachability-migration`, HEAD `755e2fd`.
No source file modified, no builds run, results dir untouched (read-only).

D vs A work vectors are bit-identical on the 200-move workload (53,598,323
evals and transitions, 2,398,357 searches, 1,257,436 parents, 62,283,081
unique candidates, 51,971,320 materialized nodes, 1,622,222 transposition
merges, 191,721 path states), so every timing delta below is pure cost delta,
not work delta. D vs A structural repairs already landed (from git log):
epoch-stamp transposition reset (`510b356`), transposition capacity sizing to
262,144 entries (`7ecf43f`), compact 152/160-byte transposition keys plus
per-candidate landing validation (`d928a05`).

## 1. Methods (exact)

Base full-timer command (task 1, 200 measured moves):

```bash
env -u TETRIS_AI_PARAM_FILE taskset --cpu-list 7 <bin> \
  --warmup-moves 20 --moves 200 --iters 1000 --seed 1 --maxdepth 6 \
  --param-file /home/icly/Documents/tetris_ai_runner/artifacts/frozen_29d.bin \
  --quiet --quiet-version 3 --telemetry on --timers on
```

Perf runs (tasks 2-3) shorten to `--moves 50`, everything else identical:

```bash
env -u TETRIS_AI_PARAM_FILE perf stat -e cycles,instructions,cache-misses,branch-misses,task-clock \
  taskset --cpu-list 7 out/build/linux-gcc-self-release/tetris_profile_value \
  --warmup-moves 20 --moves 50 --iters 1000 --seed 1 --maxdepth 6 \
  --param-file /home/icly/Documents/tetris_ai_runner/artifacts/frozen_29d.bin \
  --quiet --quiet-version 3 --telemetry on --timers on        # full-timer leg
# ... --telemetry off                                         # off leg (timers field reports na)
env -u TETRIS_AI_PARAM_FILE perf record -F 4000 -g -o /tmp/opencode/perf_d.data \
  taskset --cpu-list 7 <D or A binary> --warmup-moves 20 --moves 50 ... (same)
perf report -i /tmp/opencode/perf_X.data --stdio --no-children
```

Machine: AMD Ryzen 7 7700, kernel 7.2.3-1-cachyos, `perf` 7.2.3-1,
GCC 16.2.1 self-release build, pinned core 7, `TETRIS_AI_PARAM_FILE` unset
via `env -u`, single-threaded binaries, no other workload running
(operator discipline; runs executed strictly sequentially on the pinned core).
Known controls from `docs/phase7/machine_record.md`: `powersave` governor,
boost enabled, frequency unpinned (expect ±2% wall noise; all A/B deltas
below are single-sample unless stated, so small deltas are noise-banded).

Timer nesting (verified by source read, `src/tetris_engine.cpp`,
`src/profile_value_runner.h`, `src/toj_rule.h`): `parent_ns` is the OUTER
span per promoted/root parent (heap pop, `expand_parent`, all
`search_materialize` calls, child linking). Inside it sit `enum_ns` (kernel
`arrival_search` + per-landing `cells()` + `std::sort` + dedup),
`rule_ns` (`toj::apply` + linear in-parent dedup scan), `eval_hit/miss_ns`
(`evaluate_once`: memo + cache + `Policy::evaluate`), `policy_ns`
(`Policy::transition`), and `materialize_ns` (`build_key` + hash +
`transposition_probe` + arena push). Components are non-overlapping with
each other; their sum is less than `parent_ns`. `parent_ns ≈ run_ms`
(D 99.94%, A 99.95%) confirms the parent span covers essentially all search.

## 2. Component-timer decomposition (task 1, 200-move full-timer rows)

D row: `total_s=92.061 run_ms=91802.342 setup_ms=244.103 path_ms=13.704`.
A row: `total_s=123.354 run_ms=122391.771 setup_ms=948.381 path_ms=13.573`.
Move total = setup + run + path + apply.

### 2.1 Share of `run_ms` and of move total

| Component | D %run | A %run | D %move | A %move | Abs D (s) | Abs A (s) | Abs Δ (D−A, s) |
|---|---:|---:|---:|---:|---:|---:|---:|
| enum_ns | 16.70 | 12.63 | 16.65 | 12.53 | 15.33 | 15.45 | −0.12 |
| rule_ns | 7.71 | 5.55 | 7.69 | 5.50 | 7.07 | 6.79 | +0.29 |
| eval (hit+miss) | 8.01 | 6.18 | 7.99 | 6.13 | 7.35 | 7.56 | −0.21 |
| policy_ns | 3.42 | 2.59 | 3.41 | 2.57 | 3.14 | 3.17 | −0.03 |
| materialize_ns | 54.29 | 65.22 | 54.14 | 64.71 | 49.84 | 79.82 | −29.98 |
| parent_ns (outer, not additive) | 99.94 | 99.95 | 99.66 | 99.17 | 91.75 | 122.33 | −30.58 |
| path find+replay | 0.015 | 0.011 | 0.015 | 0.011 | 0.013 | 0.013 | +0.000 |
| setup_ms (of move) | — | — | 0.27 | 0.77 | 0.24 | 0.95 | −0.70 |
| Inner sum (enum+rule+eval+policy+mat) | 90.12 | 92.15 | — | — | 82.74 | 112.79 | −30.04 |
| parent − inner (heap/frontier/linking/scaffold) | 9.82 | 7.80 | — | — | 9.01 | 9.54 | −0.53 |

Run delta D−A = −30.59 s. The materialize delta (−29.98 s) accounts for
98% of it. Setup delta (−0.70 s) is the epoch-reset repair made visible.

### 2.2 Per-unit rates (recomputed from own rows)

| Rate | D | A | Δ |
|---|---:|---:|---:|
| enum / search (2,398,357 searches) | 6391.1 ns | 6443.0 ns | −0.8% |
| rule / unique candidate (62,283,081) | 113.59 ns | 108.99 ns | +4.2% |
| eval hit / request (memo+cache hits 4,096,106) | 61.04 ns | 62.17 ns | −1.8% |
| eval miss / computed (49,502,217) | 143.49 ns | 147.62 ns | −2.8% |
| policy / transition (53,598,323) | 58.56 ns | 59.08 ns | −0.9% |
| materialize / node (51,971,320) | 959.0 ns | 1535.8 ns | −37.6% |
| path / state (191,721) | 69.65 ns | 68.64 ns | +1.5% (noise) |

Cache context (identical both sides): 52,341,850 requests, 94.6% miss rate
(49.5M misses), 12.6M replacements on a direct-mapped-style table; memo hits
1.26M + cache hits 2.84M. Raw-to-unique candidate ratio ×1000 = 2187
(136.3M raw landings → 62.3M unique).

### 2.3 What dominates D, what shrank vs A (measured)

- D's remaining cost is materialize (54.3% of run, 959 ns/node — still 6.7×
  the next per-unit cost), then enum (16.7%), eval miss (7.7%), rule
  (7.7%), policy (3.4%). Parent-inner unattributed work is 9.8% of run.
- Shrunk vs A: materialize −37.6%/node (−30.0 s abs), setup −74%
  (−0.70 s abs), everything else within ±4% per-unit (noise band for
  single-sample paired-remote runs; only materialize/setup exceed it).
- Rule per-unit ROSE +4.2% (+0.29 s abs) — measured; HYPOTHESIS: the
  per-candidate landing validation added in `d928a05`. Small but the only
  component that regressed.
- Path overhead (plan gate 5 shape): path_ms/move = 0.0149% (D),
  0.0110% (A) — measured negligible, ~3 orders of magnitude under 2%.

## 3. `perf stat` (task 2, 50-move runs, D only)

| Event | Full-timer | Telemetry off | Δ (full vs off) |
|---|---|---:|---:|
| cycles | 175,311,171,902 | 160,064,628,378 | +9.53% |
| instructions | 241,129,090,800 | 225,046,134,139 | +7.15% |
| cache-misses | 553,166,400 | 571,765,642 | −3.25% |
| branch-misses | 1,985,203,157 | 2,118,644,400 | −6.30% |
| task-clock (ms) | 34,216.55 | 31,297.89 | +9.33% |
| IPC | 1.375 | 1.406 | — |
| PROFILE total_s (measured moves) | 25.019 s | 24.011 s | +4.20% |

Known context: qualification protocol §8 records +1.4% cycles / +0.2%
instructions for counters-only vs off (structural counter/branch cost, ~50M
increments). The +9.5%/+7.2% here is full-timer (counters AND `timer_now`
reads) vs off — the difference between the two deltas is the timer-read
cost. Scale check (HYPOTHESIS, arithmetic only): ~110M `timer_now` reads on
50 moves (2 per span × ~55M spans: 15.6M rule + 13.4M eval + 13.4M policy +
13.1M materialize + 0.6M enum + 0.3M parent) at ~25 ns ≈ 2.75 s ≈ 11% of the
25 s run — same order as the measured +9.5% cycles. Cache/branch-miss
deltas are negative single-sample noise (miss counters vary run to run;
these events did not reproduce directionally — treat as uninformative, not
as "timers improve misses"). CAVEAT: single unpaired samples each; wall
noise alone is ±2%, and the off leg shows 0.14 s sys vs 0.80 s sys on the
full leg (unexplained OS-side wobble, possibly page-fault/tick placement).
Do not gate on these deltas; the paired five-pair campaign owns verdicts.

## 4. `perf record` (task 3, 50-move full-timer, `-F 4000 -g`)

D: 136,462 samples, 0 lost, event cycles approx 173.7G.
A: 181,078 samples, 0 lost. Call graphs resolved cleanly both sides
(sample chain `promote → search_materialize_inner → transposition_probe`
at 46.9% confirms attribution depth).

### 4.1 D top 15 self-time symbols (raw, LTO-mangled names shortened only where noted)

| # | Self | Symbol (shortening noted) |
|---:|---:|---|
| 1 | 46.94% | `Engine::transposition_probe` |
| 2 | 8.93% | `call_with_block<cells lambda>` [cells geometry] |
| 3 | 5.68% | `Engine::materialize` (arena Node copy) |
| 4 | 5.15% | `Policy::evaluate` (row extract + features) |
| 5 | 4.72% | `toj::apply<B>` [rule apply+clear+classify] |
| 6 | 3.57% | `Engine::promote` (heap + parent scaffold) |
| 7 | 3.18% | `__vdso_clock_gettime` [timers] |
| 8 | 2.92% | `Engine::build_key` |
| 9 | 2.91% | `Engine::evaluate_once` (memo+cache dispatch) |
| 10 | 2.49% | `Engine::expand_source` (dedup scan + scaffold) |
| 11 | 1.78% | `Policy::transition` |
| 12 | 0.96% | `__introsort_loop<enumerate…>` [candidate sort] |
| 13 | 0.95% | `enumerate_candidates_into` closure [add/canonicalize] |
| 14 | 0.92% | `__introsort_loop` (2nd piece instantiation) [sort] |
| 15 | 0.85% | `__introsort_loop` (3rd) [sort] |

Remaining: more sort instantiations (0.82/0.43/0.41), more enum closures
(0.50/0.48/0.45/0.45/0.40/0.32/0.21/0.21/0.14/0.14/0.13/0.13),
`scan_safe_rows` 0.56%, `steady_clock::now` 0.30%, `set_root` 0.25%,
`classify_spin` 0.20%, `search_materialize_inner` frame 0.19%,
`clock_gettime` 0.14%, `steady_clock_nanos` 0.11%. malloc/memcpy/memcmp:
0.00% — measured zero heap churn in the hot loop. `set_root` 0.25% and no
`reset_run_state` above 0.1% (epoch repair confirmed in profile).

### 4.2 D bucket totals (self-time, % of sampled cycles)

| Bucket | Share | Members |
|---|---:|---|
| Materialize + transposition (probe, hash/key, arena) | ~55.7% | probe 46.94 + materialize 5.68 + build_key 2.92 + inner frame 0.19 |
| Candidate canonicalization (cells, sort, add/dedup) | ~18.2% | cells 8.93 + sorts ~4.7 + enum closures ~4.5 |
| Eval (extract + features + cache dispatch) | ~8.6% | evaluate 5.15 + evaluate_once 2.91 + scan_safe 0.56 |
| Rule apply + clear + classify | ~4.9% | toj::apply 4.72 + classify 0.20 |
| Parent scaffold (heap pop/push, links) | ~3.6% | promote 3.57 |
| Expansion scaffold + in-parent dedup scan | ~2.5% | expand_source 2.49 |
| Policy transition | ~1.8% | transition 1.78 |
| Timers / telemetry reads | ~3.7% | vdso 3.18 + chrono 0.30 + clock_gettime 0.14 + nanos 0.11 |
| Root setup | ~0.3% | set_root 0.25 |

Timer-cross-check (50-move D row: run 24.645 s): materialize_ns 55.6%,
enum 16.9%, eval 7.8%, rule 7.3%, policy 3.2% — same rank order as perf
buckets. Two independent instruments agree.

### 4.3 A contrast (same workload, 181k samples; selected deltas)

probe 60.86% (vs D 46.94%), materialize 4.51%, build_key 0.49%,
`reset_run_state` 0.55% (vs D: absent — epoch repair),
cells 5.86%, expand_source 3.97% (vs 2.49%),
evaluate 3.99%, evaluate_once 2.31%, transition 1.44%,
rule-apply split across `occupancy_mask` 1.31% + `lowest_occupied_row`
0.47% + `Board::cleared` 0.38% + `apply_unchecked` 0.21% (vs D's fused
`toj::apply` 4.72% — LTO inlining boundary moved with the landing-validation
repair; compare via the rule_ns timer, not across these frames),
vdso 2.44%. Absolute-scale probe: ≈20.1 s (A) vs ≈11.6 s (D).
The repairs already fixed: probe chain cost (−13.9 pts share, −37.6%/node),
per-move table reset (0.55% + 0.77%→0.27% setup share).

### 4.4 Structurally absent (measured)

- `arrival_search` / `binary_bfs` / settle / kick-propagation symbols: no
  self-time above noise anywhere (only `move_checker::is_valid/try_rotate`
  at 0.00%). The bit-parallel T arrival-channel kernel is fully inlined and
  costs ~nothing measurable — plan item 2 is already cheap.
- No per-call allocation (malloc family 0.00%).
- No deadline-polling symbol in the profile (plan item 7 unobservable here).
- Selected-path BFS: 0.015% of run (plan item 8 negligible).

## 5. Synthesis: ranked remediation targets vs plan §17 order

Plan order for reference: 1 duplicate board conversion/row extraction,
2 T arrival-channel propagation, 3 candidate canonicalization/early dedup,
4 board apply+clear, 5 eval cache layout, 6 node materialization/frontier
queues, 7 deadline polling, 8 selected-path BFS.

Measured rank (by D share) inverts the plan's middle: 6 ≫ 3 > 5/1 > 4 >
7/2/8 (already-cheap). The plan order assumed BFS/adapter costs would lead;
measurement says the transposition table leads by 3× the next target.

### Target 1 — Transposition probe + key build + arena materialize (plan item 6; key/hash layout touches 5)

- Evidence: 54.3% of run (timer), ~55.7% of cycles; 959 ns/node (37.6%
  better than A but still 6.7× eval-miss); probe alone 46.9% self.
  A→D already paid down 30 s here; ~50 s/200-move remains.
- Likely mechanism (HYPOTHESIS): 160-byte key equality + occupancy/state/
  boundary-bit compare executed per linear-probe step over a 262,144-entry
  table at high per-move occupancy (~450k distinct states measured at
  sizing); `build_key` copies full occupancy + policy state + queue
  boundary bits per child before probing; `materialize` copies a full Node
  (Board + state) into the arena.
- Gate a fix with: `materialize_ns/materialized_nodes` per-unit plus a NEW
  counter (probe steps per materialize — does not exist today; add as
  diagnostic counter first), before/after paired full-timer rows at equal
  work vectors; watch `transposition_merges` for semantic drift. Candidate
  directions (not prescriptions): smaller key (fingerprint-first compare),
  cheaper hash over fewer fields, probe-length histogram to test
  sizing/load hypotheses.

### Target 2 — Candidate canonicalization: cells() + sort + early dedup (plan items 3, partly 1)

- Evidence: ~18.2% cycles; enum 16.7% of run at 6391 ns/search.
  `cells()` geometry 8.9% is called once per RAW landing (136M/200-move)
  in `add`, then AGAIN twice per sort comparison (`key_less` recomputes
  both cell masks — O(n log n) recomputation) and again in `key_equal`.
  Sort instantiations total ~4.7%.
- Likely mechanism (HYPOTHESIS): redundant cell-mask recomputation is the
  bulk; the kernel BFS itself is ~free (§4.4). T pieces pay double
  (two channels × same geometry).
- Gate with: `enum_ns/searches` + raw/unique ratio before/after; count
  `cells()` calls per search with a diagnostic counter to confirm the
  recomputation factor, then verify it drops (e.g., compute mask once per
  landing, sort indices/small keys, canonicalize once).

### Target 3 — Eval row extraction + cache layout (plan items 1 + 5)

- Evidence: eval 8.0% of run; miss path 143.5 ns × 49.5M = 7.1 s at 94.6%
  miss rate with 12.5M replacements; `Policy::evaluate` (40× `row()` +
  feature scans, `src/toj_policy.cpp:373`) 5.2% self; `evaluate_once`
  dispatch 2.9%.
- Likely mechanism (HYPOTHESIS): per-miss 40-row extraction from packed
  `board_t` words plus full feature rescan; direct-mapped-style table
  thrashes (replacements ≈ misses), so the layout buys little reuse at
  this workload.
- Gate with: `eval_miss_ns/eval_computed` + `cache_hits/cache_requests`
  before/after; try set-associativity or bypass-cache-when-thrashing
  variants behind the existing layout switch; confirm `eval_computed`
  work vector unchanged.

### Target 4 — Rule apply + in-parent O(n²) dedup scan (plan items 4 + 3)

- Evidence: rule 7.7% of run, 113.6 ns/unique — the ONLY component whose
  per-unit rose vs A (+4.2%, +0.29 s abs; HYPOTHESIS: landing validation
  from `d928a05`). `toj::apply` 4.7% self; the `rule_ns` span also covers
  the linear `child.source/board/outcome` scan over `out` per candidate
  (quadratic in fan-out; its self-time hides in `expand_source` 2.5%).
- Gate with: `rule_ns/unique_candidates` + a diagnostic dedup-comparisons
  counter; separate apply cost from dedup cost with a split timer before
  choosing between early-dedup (hash on occupancy+spin+clear) vs cheaper
  apply.

### Target 5 — Parent-inner scaffold: heap, frontier queues, child links (plan item 6 remainder)

- Evidence: parent−inner = 9.0 s, 9.8% of run; `promote` self 3.6%.
- HYPOTHESIS: `heap_.pop_max/push` per child + `append_child_link` +
  `child_buffer_` traffic. Smaller than targets 1–4 and partly fixed cost
  of the widening policy.
- Gate with: parent_ns − (inner sum) residual before/after; only pursue
  after targets 1–2 land.

### Explicitly NOT targets (measured)

- T arrival-channel propagation (plan 2): kernel BFS ~0% — do not touch.
- Duplicate board conversion (plan 1, search side): none exists (direct
  `board_t` use); the remaining row-extraction instance is Target 3.
- Deadline polling (plan 7): no symbol observed.
- Selected-path BFS (plan 8): 0.015% of run.
- Policy transition body: 3.4% of run, −0.9%/unit vs A; revisit only if
  transitions/work grows.
- Timers/telemetry: ~3.7% self in full-timer rows, zero in off-mode
  production rows by construction; keep the twin-run methodology, do not
  "optimize" measurement.

## 6. Limitations

Single-sample A/B timer rows (one 200-move pair) — per-unit deltas under
~±4% are noise-banded, not findings. `perf stat` legs are single unpaired
samples with an unexplained sys-time wobble; only the direction and order
of the cycles delta (consistent with the §8 counters-only result plus
counted timer reads) is interpretable. LTO inlining boundaries differ
between A and D, so cross-binary symbol-name comparison is unreliable
below the top symbols; the component timers (stable scopes) are the
authoritative A/B instrument, perf is the authoritative within-D
instrument. Workload is seed 1, iters 1000, maxdepth 6 only.
