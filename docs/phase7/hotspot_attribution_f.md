# Hotspot attribution for the value engine (F, post-fingerprint candidate)

Attribution only. No performance claims, no verdicts, no pass/fail language.
All shares below are measured on the stated runs; every hypothesis is tagged
HYPOTHESIS. This re-ranking supersedes `hotspot_attribution_d.md` (whose
materialize 54.3% share is stale after the fingerprint-transposition change).
Two independent instruments agree: component timers (PROFILE_V3 rows) and
`perf` cycle sampling.

## 0. Anchors and integrity

| Artifact | Path | SHA-256 |
|---|---|---|
| F (post-fingerprint candidate) | `out/build/linux-gcc-self-release/tetris_profile_value` | `b8c426acd4123c93d5c6031de0b1f243e5519fd4cae9b4635ec83e3e0266fb70` |
| F mirror (must be identical) | `/home/icly/Documents/tetris_ai_runner_results/phase7_diag/binaries/tetris_profile_value.f_post` | `b8c426acd4123c93d5c6031de0b1f243e5519fd4cae9b4635ec83e3e0266fb70` |
| A (frozen 7.2E, contrast only) | `/home/icly/Documents/tetris_ai_runner_results/phase7/tetris_profile_value.candidate` | `816bcd7d33a7996a207d1c8bbba8bcf838baccfc6246ecb084d445f3b49938fe` |
| Param file | `/home/icly/Documents/tetris_ai_runner/artifacts/frozen_29d.bin` | `ea95ba584f4eb5a7234bf0fbdd68fb422e00e29d13373e4df9e6b9ea2ca98037` |
| Probe bench | `out/build/linux-gcc-self-release/transposition_reset_bench` | `4abaf1af6aedbad4a225e742eb6778be237f4ef32423064cf85655f2e02cd647` |

F and its mirror verified byte-identical (`cmp` clean, prefix `b8c426ac26fb`
as expected). Tracked tree clean (no tracked modifications; only untracked
scratch/research files). Branch `fast-reachability-migration`, HEAD `deaf420`
("fingerprint-gated transposition entries, cliff eliminated"). No source file
modified, no builds run, results dir read-only (A binary executed in place,
nothing written there). What landed in `deaf420` (from `git show --stat`):
16-byte fingerprint-gated transposition entries (8 fp + 4 node + 4 epoch),
table 2^20 entries, arena 705,851 nodes; probe = epoch test, fp compare, and
only on fp match rebuild the key from the arena node and verify with exact
`operator==` before merging; reroot staging re-derives keys from nodes after
the queue swap and reinserts first-staged-wins. No rule-path, eval-path, or
enum-path production code changed in this commit.

CRITICAL CONTEXT: F and A do NOT perform equal work on the 200-move fixed
workload. F runs the full budget (`widening_iters=200,000`, `texhaust_moves=0`);
A fail-stops on 174/200 moves (`widening_iters=166,334`). Every F/A delta below
is therefore a joint delta-work/delta-time measurement, not a pure cost delta
(unlike D/A in the prior attribution, whose work vectors were bit-identical).
Section 2 presents shares AND absolute seconds AND per-unit rates so volume
and cost stay separable; per-unit ratios remain confounded by divergent board
mixes (F explores states A never reaches) and are labeled accordingly.

## 1. Methods (exact)

Base full-timer command (task 1, 200 measured moves), `<bin>` = F or A:

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
env -u TETRIS_AI_PARAM_FILE perf record -F 4000 -g -o /tmp/opencode/perf_f.data \
  taskset --cpu-list 7 out/build/linux-gcc-self-release/tetris_profile_value \
  --warmup-moves 20 --moves 50 ... (same full-timer flags)
perf report -i /tmp/opencode/perf_f.data --stdio --no-children
```

Probe bench (one run, micro-workload — see §2.3 for scope):

```bash
env -u TETRIS_AI_PARAM_FILE taskset --cpu-list 7 \
  out/build/linux-gcc-self-release/transposition_reset_bench probe
```

Machine: AMD Ryzen 7 7700, kernel 7.2.3-1-cachyos, `perf` 7.2.3-1,
GCC 16.2.1 self-release build, pinned core 7, `TETRIS_AI_PARAM_FILE` unset
via `env -u`, single-threaded binaries, runs executed strictly sequentially
on the pinned core. Controls from `docs/phase7/machine_record.md` still hold:
`powersave` governor (verified), boost enabled, frequency unpinned — expect
±2% wall noise at minimum; all deltas below are single-sample, so small
deltas are noise-banded.

Timer nesting (unchanged by `deaf420`; verified by source read of
`src/tetris_engine.cpp` §§521-707, 1136-1146): `parent_ns` is the OUTER span
per promoted/root parent; inside it sit `enum_ns`, `rule_ns`, `eval_hit/miss_ns`,
`policy_ns`, and `materialize_ns` (which covers `build_key` + hash +
`transposition_probe`, INCLUDING the new `key_from_node` rebuild + exact
verify on fingerprint match, plus the arena push). Components are
non-overlapping with each other; their sum is less than `parent_ns`.
`parent_ns ≈ run_ms` (F 99.88%, A 99.95%) confirms the parent span covers
essentially all search.

## 2. Component-timer decomposition (task 1, 200-move full-timer rows)

F row: `total_s=56.299 run_ms=55948.281 setup_ms=333.131 path_ms=16.905`.
A row: `total_s=128.828 run_ms=127847.242 setup_ms=967.120 path_ms=13.339`.
Move total = setup + run + path + apply reproduces `total_s` both sides
(F 56.298, A 128.828). F/A `total_s` ratio 0.4370, `run_ms` ratio 0.4376
(single full-timer pair; joint work+cost, not a gate claim).

### 2.1 Share of `run_ms` and absolute seconds

| Component | F %run | A %run | F (s) | A (s) | Abs Δ (F−A, s) | Δ (pp) |
|---|---:|---:|---:|---:|---:|---:|
| enum_ns | 14.86 | 11.67 | 8.316 | 14.926 | −6.610 | +3.19 |
| rule_ns | 17.71 | 5.26 | 9.909 | 6.720 | +3.189 | +12.46 |
| eval hit | 0.55 | 0.21 | 0.306 | 0.268 | +0.038 | +0.34 |
| eval miss | 16.57 | 5.70 | 9.268 | 7.293 | +1.976 | +10.86 |
| eval (hit+miss) | 17.11 | 5.91 | 9.574 | 7.561 | +2.014 | +11.20 |
| policy_ns | 7.33 | 2.46 | 4.103 | 3.141 | +0.962 | +4.88 |
| materialize_ns | 19.56 | 67.19 | 10.942 | 85.900 | −74.958 | −47.63 |
| parent_ns (outer, not additive) | 99.88 | 99.95 | 55.883 | 127.787 | −71.904 | −0.07 |
| path find+replay | 0.030 | 0.010 | 0.017 | 0.013 | +0.004 | +0.02 |
| setup_ms (of move) | 0.59 | 0.75 | 0.333 | 0.967 | −0.634 | — |
| Inner sum (enum+rule+eval+policy+mat) | 76.58 | 92.49 | 42.844 | 118.248 | −75.403 | −15.91 |
| parent − inner (heap/frontier/linking/scaffold/timer-reads) | 23.31 | 7.46 | 13.039 | 9.539 | +3.499 | +15.84 |

Run delta F−A = −71.90 s. The materialize delta (−74.96 s) exceeds it; every
other timed leg is UP in absolute seconds because F completes ~20-32% more
work (see §2.3). Share changes therefore mix a collapsing materialize leg
with volume growth elsewhere — read §2.2 and §2.3 together, never alone.

Headline vs the prior attribution: materialize falls from 54.3% of run (D)
to 19.6% (F). The new largest timer legs are materialize 19.6%, rule 17.7%,
eval 17.1%, enum 14.9%. The parent−inner residual rises from 9.8% (D) to
23.3% (F) — §4.2 decomposes it (heap/scaffold self-time plus ~10% timer-read
self-time now inside the parent span).

### 2.2 Per-unit rates (recomputed from own rows; VOLUME-CONFOUNDED — see note)

| Rate | F | A | F/A |
|---|---:|---:|---:|
| enum / search (F 2,855,528; A 2,398,357) | 2912.1 ns | 6223.3 ns | 0.4679 |
| rule / unique candidate (F 78,270,527; A 62,283,081) | 126.60 ns | 107.89 ns | 1.1734 |
| eval hit / request (memo+cache hits F 4,729,124; A 4,096,106) | 64.66 ns | 65.44 ns | 0.9880 |
| eval miss / computed (F 60,812,083; A 49,502,217) | 152.41 ns | 147.32 ns | 1.0346 |
| policy / transition (F 65,541,207; A 53,598,323) | 62.60 ns | 58.61 ns | 1.0682 |
| materialize / node (F 63,396,766; A 51,971,320) | 172.60 ns | 1652.84 ns | 0.1044 |
| path / state (F 192,063; A 191,721) | 86.55 ns | 67.43 ns | 1.2835 (noise; 0.03% of run) |

Cache context (F): 63,886,611 requests, 95.2% miss rate (60.8M misses),
60.8M replacements on the direct-mapped-style table — the table still
thrashes at this workload; memo hits 1.65M + cache hits 3.07M.
Raw-to-unique ratio ×1000 = 2082 (F) vs 2187 (A).

Note on the enum ratio 0.47: NO enum-path code changed in `deaf420`, and
per-search work counts are near-identical (searches/parent 1.906 both sides;
raw landings/search 57.09 vs 56.82). HYPOTHESIS: second-order cache effect —
A's exhausted table (262,144 × 160 B ≈ 40 MiB, ~100% occupancy, long probe
chains with 152-byte key compares per step) pollutes L2/L3 against the enum
kernel's working set, while F's table (2^20 × 16 B = 16 MiB, short chains,
16-byte fp compares) does not. Same class of mechanism as the epoch change's
13% end-to-end effect noted in the pre/post diagnostics. The D→F trajectory
also changes board mix (untruncated deeper searches), which independently
moves per-search kernel cost via height specialization. Do not treat 0.47 as
a pure "enum got 2× cheaper" finding; treat it as measured-per-unit on
divergent trajectories, gated by the paired equal-trajectory campaign.

### 2.3 Work-volume ratios F/A (+19–32%, as banded in the commit message)

| Counter | F | A | F/A |
|---|---:|---:|---:|
| widening_iters | 200,000 | 166,334 | 1.2024 (+20.2%) |
| parents | 1,497,864 | 1,257,436 | 1.1912 (+19.1%) |
| searches | 2,855,528 | 2,398,357 | 1.1906 (+19.1%) |
| raw_landings | 163,004,865 | 136,269,623 | 1.1962 (+19.6%) |
| unique_candidates | 78,270,527 | 62,283,081 | 1.2567 (+25.7%) |
| evals / transitions | 65,541,207 | 53,598,323 | 1.2228 (+22.3%) |
| eval_computed | 60,812,083 | 49,502,217 | 1.2285 (+22.9%) |
| cache_requests | 63,886,611 | 52,341,850 | 1.2206 (+22.1%) |
| materialized_nodes | 63,396,766 | 51,971,320 | 1.2198 (+22.0%) |
| transposition_merges | 2,144,441 | 1,622,222 | 1.3219 (+32.2%) |
| texhaust_moves | 0 | 174 | cliff eliminated |

### 2.4 Probe telemetry (bench micro-workload; PROFILE_V3 carries no probe fields)

One `transposition_reset_bench probe` run (shelf board, 2000-iteration
budget, timers off, pinned core 7):

```text
PROBE complete=1 steps=10114 probes=9450 hist=[8958,383,94,14,1,0,0,0]
PROBE_CTX merges=0 materialized=9450 nodes=9451 exhausted=0 rebuilds=0
```

Steps/probe = 1.070; 94.8% of probes land in bucket 0 (length ≤ 1); zero
rebuilds (no fingerprint match required a `key_from_node` reconstruction on
this micro-workload), zero merges, no exhaustion. SCOPE: this characterizes
the probe-length distribution on a small synthetic workload only — it does
not measure the 200-move profile workload. Its production-scale corroboration
is indirect: `perf` shows `transposition_probe_prehashed` at 4.33% self
(down from `transposition_probe` 46.94% in D) and `key_from_node` at 0.17%
self (rebuild-on-match is rare/cheap, as designed).

## 3. `perf stat` (task 2, 50-move runs, F only)

| Event | Full-timer | Telemetry off | Δ (full vs off) |
|---|---|---:|---:|
| cycles | 96,435,348,718 | 76,312,161,298 | +26.37% |
| instructions | 174,351,852,329 | 153,623,880,156 | +13.49% |
| cache-misses | 96,107,747 | 81,966,984 | +17.25% |
| branch-misses | 683,292,134 | 662,942,047 | +3.07% |
| task-clock (ms) | 18,448.38 | 14,621.30 | +26.17% |
| IPC | 1.808 | 2.013 | — |
| PROFILE total_s (measured moves) | 13.962 s | 10.931 s | +27.73% |

Known context: qualification protocol §8 records +1.4% cycles / +0.2%
instructions for counters-only vs off (structural counter/branch cost). The
+26.4%/+13.5% here is full-timer (counters AND `timer_now` reads) vs off —
the gap between the two deltas is the timer-read cost, and it grew vs D's
+9.5%/+7.2% for an arithmetic reason (HYPOTHESIS, counts only): F's absolute
run time fell ~2.3× while the timer-read count grew with work volume (~71M
spans × 2 reads ≈ 142M reads on 50 moves at ~25 ns ≈ 3.5 s ≈ 25% of the
14 s run — same order as the measured +26%). Timer self-time in §4 (≈10% of
sampled cycles) corroborates. Cache/branch-miss deltas are single-sample
noise (miss counters vary run to run; direction not reproduced) — treat as
uninformative, not as findings. CAVEAT: single unpaired samples each; the
full leg shows 0.066 s sys vs 0.015 s sys on the off leg (OS-side wobble).
Do not gate on these deltas; the paired five-pair campaign owns verdicts.
Do NOT compare full-timer totals against off-mode campaign numbers; keep the
twin-run methodology and do not "optimize" measurement.

## 4. `perf record` (task 3, 50-move full-timer, `-F 4000 -g`, pinned)

74,159 samples, 0 lost, event cycles approx 96.0G. Call graphs resolved
(the `expand_parent → expand_source → evaluate_once → Policy::evaluate`
chain and the `search_materialize_inner` probe/materialize frames confirm
attribution depth).

### 4.1 F top 15 self-time symbols (raw; LTO-mangled names shortened only where noted)

| # | Self | Symbol (shortening noted) |
|---:|---:|---|
| 1 | 12.75% | `Engine::materialize` (arena Node copy) |
| 2 | 11.25% | `Policy::evaluate` (row extract + features) |
| 3 | 8.83% | `__vdso_clock_gettime` [timers] |
| 4 | 8.14% | `toj::apply<…>::{lambda}::operator()` [rule apply+clear+classify] |
| 5 | 7.20% | `Engine::evaluate_once` (memo+cache dispatch) |
| 6 | 7.01% | `Engine::expand_source` self (dedup scan + scaffold) |
| 7 | 5.29% | `call_with_block<cells lambda>` [cells geometry dispatch] |
| 8 | 4.33% | `Engine::transposition_probe_prehashed` |
| 9 | 3.78% | `Policy::transition` |
| 10 | 3.59% | `Engine::promote` (heap + parent scaffold) |
| 11 | 2.83% | `Engine::append_child_link` |
| 12 | 2.65% | `toj::cells_empty` [via `is_landing`] |
| 13 | 2.39% | `transposition_hash` |
| 14 | 2.21% | `toj::cells` |
| 15 | 1.90% | `enumerate_candidates_into` closure [add/canonicalize piece] |

Next (for bucket completeness): more enum closures 1.39/0.72/0.71/0.63/
0.55/0.48, `__unguarded_linear_insert` 0.94%, `scan_safe_rows` 0.90%,
`__introsort_loop` pieces 0.76/0.68/0.59/0.58/0.46/0.35%,
`Engine::build_key` 0.57%, `set_root` 0.57%, `clock_gettime` 0.55%,
`steady_clock::now` 0.61%, insertion-sort pieces ~1.32% total, `swap` 0.24%,
`arrival_routing` 0.22/0.15%, `key_from_node` 0.17%, `classify_spin` 0.14%,
`is_landing` self 0.32%. Top-45 symbols sum to 98.75% of sampled cycles.

### 4.2 F bucket totals (self-time, % of sampled cycles; `~` = shared-dispatch split)

| Bucket | Share | Members |
|---|---:|---|
| Materialize + transposition (arena copy, probe, hash/key, rebuild) | ~20.2% | materialize 12.75 + probe_prehashed 4.33 + hash 2.39 + build_key 0.57 + key_from_node 0.17 |
| Eval (extract + features + cache dispatch) | ~19.4% | evaluate 11.25 + evaluate_once 7.20 + scan_safe 0.90 |
| Candidate geometry dispatch + enum closures + sort (shared enum/rule) | ~18.2% | cells-lambda 5.29 + cells 2.21 + enum closures ~5.8 + introsort ~4.4 + insertion ~1.3 + linear-insert 0.94 + swap 0.24 |
| Rule apply + clear + classify + landing checks | ~11.1% | apply-lambda 8.14 + cells_empty 2.65 + classify 0.14 + is_landing self 0.32 |
| Expansion scaffold + in-parent dedup scan | ~7.0% | expand_source self 7.01 |
| Parent scaffold (heap, child links) | ~6.4% | promote 3.59 + append_child_link 2.83 |
| Policy transition | ~3.8% | transition 3.78 |
| Timers / telemetry reads | ~10.0% | vdso 8.83 + steady_clock 0.61 + clock_gettime 0.55 |
| Root setup | ~0.6% | set_root 0.57 |
| Arrival kernel | ~0.4% | arrival_routing 0.37 + arrival_search 0.00% |

Attribution ambiguity (stated, not hidden): the 5.29% cells-dispatch splits
by caller — ~2.7 via rule-apply, ~1.2 via `is_landing`, ~0.9 via transition
(from the call-graph children). Geometry cost is therefore shared between
the rule leg and the canonicalization leg; the bucket rows above assign the
dispatch to geometry and the apply-lambda to rule. Timer-cross-check (50-move
F row from the record run: run 13.727 s; materialize 19.2%, rule 19.0%, eval
17.3%, enum 14.2%, policy 7.3%) reproduces the same rank order as the perf
buckets modulo timer self-time. Two independent instruments agree.

### 4.3 D→F structural deltas (same workload shape, cross-session singles)

probe self 46.94% (D) → 4.33% + hash 2.39% (F); `build_key` 2.92% → 0.57%;
`key_from_node` 0.17% appears (rebuild-on-match, rare by design);
`materialize` (arena copy) 5.68% → 12.75% — now the largest single symbol
because everything around it shrank, not because the copy grew (172.6 ns/node
per §2.2); cells geometry 8.93% → 5.29% dispatch (+2.21% `cells`);
`evaluate` 5.15% → 11.25% and `evaluate_once` 2.91% → 7.20% (share effect of
the probe collapse, same per-unit within ±4%); timers 3.7% → ~10.0% (see §3).

### 4.4 Structurally absent (measured)

- `arrival_search` self-time 0.00%; `arrival_routing` 0.37% total — the
  bit-parallel T arrival-channel kernel costs ~nothing measurable; plan item
  2 stays a non-target.
- malloc family 0.00% (`cfree`/`malloc`/`memcmp@plt` all 0.00%) — no per-call
  allocation in the hot loop.
- No deadline-polling symbol in the profile (plan item 7 unobservable here).
- No `reset_run_state` above 0.1% (epoch repair holds); `set_root` 0.57%.
- Selected-path BFS: 0.03% of run (plan item 8 negligible).

## 5. Synthesis: ranked remediation targets vs plan §17 order

Plan order for reference (§17, "if the end-to-end gate fails, optimize in this
order"): 1 duplicate board conversion/row extraction, 2 T arrival-channel
propagation, 3 candidate canonicalization/early dedup, 4 board apply+clear,
5 eval cache layout, 6 node materialization/frontier queues, 7 deadline
polling, 8 selected-path BFS.

Measured rank (by F share) vs plan: the transposition leg (plan 6) drops from
#1 to #4 after remediation; the lead passes to rule+dedup (plan 4+3), eval
(plan 1+5), and canonicalization (plan 3). Items 2/7/8 remain measured-cheap.

### Target 1 — Rule apply + in-parent O(n²) dedup scan (plan items 4 + 3)

- Evidence: 17.71% of run (timer; largest non-materialize leg), +3.19 s abs
  on higher volume; perf apply-lambda 8.14% self + `expand_source` self
  7.01% (the linear `child.source/board/outcome` scan over `out` per
  candidate is quadratic in fan-out and its self-time hides here) +
  landing-check geometry (`cells_empty` 2.65%).
- Mechanism (HYPOTHESIS): per-candidate `is_landing` cell checks + mask build
  + clear + `classify_spin`, plus the intra-source result-dedup scan whose
  cost grows with fan-out (F's untruncated searches have larger per-parent
  fan-out than A's truncated ones, which also confounds the +17.3%/unique
  in §2.2).
- Gate a fix with: `rule_ns/unique_candidates` plus a NEW split timer
  (apply vs dedup-scan — see adjudication below) and a diagnostic
  dedup-comparisons counter, before/after paired full-timer rows at equal
  work vectors; watch `rule_transitions`/`unique_candidates` for semantic
  drift. Directions (not prescriptions): hash-based early dedup on
  (occupancy, spin, clear) vs cheaper apply geometry.
- Count-safe or volume-changing: the split timer and counters are count-safe
  (measurement only). Any fix is count-safe ONLY if `unique_candidates`,
  `rule_transitions`, and downstream counters are bit-identical before/after;
  otherwise it is volume-changing and requires the partition-instrument
  re-run (`docs/phase7/count_partition_instrument_design.md`).

### Target 2 — Eval row extraction + cache layout (plan items 1 + 5)

- Evidence: 17.11% of run; miss path 152.4 ns × 60.8M at 95.2% miss rate
  with 60.8M replacements (direct-mapped-style table thrashes — replacements
  ≈ misses); `Policy::evaluate` (40× `row()` + feature scans) 11.25% self,
  the #2 single symbol; `evaluate_once` dispatch 7.20%.
- Mechanism (HYPOTHESIS): per-miss 40-row extraction from packed `board_t`
  words plus full feature rescan; the layout buys little reuse at this
  workload because the table thrashes.
- Gate with: `eval_miss_ns/eval_computed` + `cache_hits/cache_requests`
  before/after; layout variants behind the existing switch; confirm the
  `eval_computed` board set is unchanged.
- Count-safe or volume-changing: count-safe if the computed-eval set is
  identical and transitions unchanged (cache-hit counter drift is expected
  under a layout change and must be reported, not hidden); else re-run.

### Target 3 — Candidate canonicalization: cells() + sort + early dedup (plan item 3)

- Evidence: enum 14.86% of run at 2912 ns/search (volume-confounded, §2.2
  note); perf geometry+sort ≈ 18% with the shared-dispatch caveat (§4.2);
  sort instantiations ~5.7% total; enum closures ~5.8%.
- Mechanism (HYPOTHESIS, carried from D and still consistent): redundant
  cell-mask recomputation — `cells()` per raw landing in `add`, then again
  per sort comparison (`key_less` recomputes both masks — O(n log n)
  recomputation) and in `key_equal`. The kernel BFS itself is ~free (§4.4).
- Gate with: `enum_ns/searches` + raw/unique ratio before/after; count
  `cells()` calls per search with a diagnostic counter to confirm the
  recomputation factor, then verify it drops (compute mask once per landing,
  sort indices/small keys, canonicalize once).
- Count-safe or volume-changing: count-safe if `unique_candidates` and
  `raw_landings` are bit-identical before/after (ordering/canonical form
  preserved); else re-run.

### Target 4 — Materialize remainder: arena copy + hash (plan item 6 remainder)

- Evidence: 19.56% of run but 172.6 ns/node (−89.6%/node vs A) — largely
  remediated. Remaining perf mass: arena Node copy 12.75% + hash 2.39%.
- Mechanism (HYPOTHESIS): full Node (Board + state) copy into the arena per
  materialization plus a full-field hash over occupancy + policy state +
  queue boundary bits per child.
- Gate with: `materialize_ns/materialized_nodes` + probe-steps/materialize
  (diagnostic counter from the existing `probe_histogram` telemetry, now
  emitted at profile scale) before/after; watch `transposition_merges` for
  semantic drift. Directions (not prescriptions): smaller hashed prefix,
  cheaper hash over fewer fields, slimmer Node copy.
- Count-safe or volume-changing: count-safe if merge/node counts identical;
  any key-shape change is volume-risky (merge behavior may shift) and needs
  the re-run.

### Target 5 — Parent-inner scaffold: heap, links, timer reads (plan item 6 remainder)

- Evidence: parent−inner = 13.04 s, 23.3% of run (up from 9.8% in D);
  `promote` 3.59% + `append_child_link` 2.83% + `expand_source` self share +
  ~10% timer-read self-time inside the parent span (§§3–4.2).
- HYPOTHESIS: `heap_.pop_max/push` per child + `append_child_link` +
  `child_buffer_` traffic, plus inflated timer-read residence. Smaller than
  targets 1–4 in fixable share and partly the fixed cost of the widening
  policy; pursue only after targets 1–3 land.
- Gate with: parent_ns − (inner sum) residual before/after in off-mode rows
  (to exclude timer residence) plus paired full-timer rows.

### Explicitly NOT targets (measured)

- T arrival-channel propagation (plan 2): kernel ~0.4% — do not touch.
- Duplicate board conversion (plan 1, search side): none exists (direct
  `board_t` use); the remaining row-extraction instance is Target 2.
- Deadline polling (plan 7): no symbol observed.
- Selected-path BFS (plan 8): 0.03% of run.
- Policy transition body: 7.33% of run, +6.8%/unit volume-confounded;
  revisit only if transitions/work grows.
- Timers/telemetry: ~10% self in full-timer rows, zero in off-mode
  production rows by construction; keep the twin-run methodology, do not
  "optimize" measurement.

### Adjudication: the rule leg (+4.2% regressor) and the split-timer question

Is the rule leg still visible? Yes as share (17.71%, the #2 timer leg, up
from 7.71% in D), and per-unit F/A is +17.3%/unique (up from +4.2% D/A). BUT
the +4.2%→+17.3% growth CANNOT be a growing validation cost: `deaf420`
touches no rule-path code (`git show --stat`: only `src/tetris_engine.*`
transposition paths plus tests), and F/A trajectories diverge (truncated-A
vs full-budget-F board mixes; rule cost is geometry- and fan-out-dependent).
The pre/post diagnostics already flagged that the expected `is_landing`
benefit is NOT demonstrated in per-unit data (rule/unique B/A 1.0757, D/A
1.0603) — either the per-candidate workspace construction was never the
dominant rule cost, or the effect sits below single-run resolution. The
clean test of the validation-cost hypothesis is a same-engine A/B with the
landing validation behind a flag on identical trajectories — forwarded,
not concluded here.

Does it warrant a split timer? Yes. `rule_ns` conflates two different cost
shapes with different fixes: per-candidate apply geometry (fix = cheaper
apply) vs the intra-source linear dedup scan, quadratic in fan-out, whose
self-time hides in `expand_source` (fix = early-dedup hash). The split
(apply vs dedup-scan, plus the already-proposed dedup-comparisons counter)
is count-safe diagnostic work and gates which fix to attempt. It does not
require the partition re-run; any fix it motivates does unless work vectors
verify bit-identical.

## 6. Estimated candidate-vs-legacy ratio and campaign needs

Cross-session arithmetic (APPROXIMATE — single-sample F/A full-timer pair
times the recorded 7.2E medians; volumes differ across all three legs;
not a gate claim):

| Leg | Recorded (A vs legacy, 7.2E medians) | F/A (this session, single) | Estimated F vs legacy |
|---|---:|---:|---:|
| total time (seed-1 fixed) | 6.81284 | 0.43701 | ≈ 2.98× |
| p95 latency (seed-1 fixed) | 7.66833 | 0.41763 | ≈ 3.20× |
| enum/search | 3.82578 | 0.4679 | ≈ 1.79× |
| unique-candidate | 1.08216 | 1.1734 | ≈ 1.27× |
| eval-hit | 1.44282 | 0.9880 | ≈ 1.43× |
| eval-miss | 1.90900 | 1.0346 | ≈ 1.98× |
| transition | 1.31386 | 1.0682 | ≈ 1.40× |
| materialized | 12.89630 | 0.1044 | ≈ 1.35× |
| path-state | 0.64999 | 1.2835 | ≈ 0.83× |

Every projected component rate still exceeds its 1.02 bar except path (all
approximate, all volume-confounded). The materialize leg improves by nearly
an order of magnitude yet remains above bar in projection.

What the binding campaign needs next (numbers for the coordinator; the
coordinator decides remediation-first vs campaign-now):

- Re-freeze: F binary (`b8c426ac…`, byte-identical mirror already present),
  comparator, and param file (`ea95ba58…`) under the full protocol —
  re-freeze is still required (this session ran singles, not the protocol).
- Full protocol: five paired ABBA+A repetitions per mode (fixed-work and
  20 ms timed), seeds 1–3, pinned core, twin-run (counters-only binding
  rows plus full-timer attribution rows), gates 8/9 legacy comparators
  re-run with the rest of the gates, telemetry-overhead gate.
- Partition instrument: REQUIRED before any gate-3 verdict — the fingerprint
  change is volume-changing by design (+19–32% counters, 174→0 exhaust
  moves), so the count-partition re-run
  (`docs/phase7/count_partition_instrument_design.md`) is a prerequisite,
  not optional. Note in its favor: fixed-work searches are now
  budget-complete rather than truncation-cut, which is the cleaner corpus
  per that design's §3.
- Remediation-first alternative: targets 1–3 (§5) address the remaining
  projected gap (rule/eval/enum at ≈1.27–1.98×); each is gated by count-safe
  before/after rows, with the partition re-run held until a volume change
  is attempted or the campaign runs.

## 7. Limitations

Single-sample everything: one 200-move F/A timer pair, one 50-move leg each
for `perf stat`, one 74k-sample `perf record`, one probe-bench micro-run —
per-unit deltas under ~±4% are noise-banded, and the large deltas carry the
volume confound of §2.3. `powersave` drift applies (F ran before A; adjacent
but unpaired). Full-timer rows carry ~26% timer overhead on F — never compare
them against off-mode campaign numbers. LTO inlining boundaries differ
between F and A, so cross-binary symbol-name comparison below the top
symbols is unreliable; the component timers (stable scopes) are the
authoritative F/A instrument, perf is the authoritative within-F instrument.
Workload is seed 1, iters 1000, maxdepth 6 only. The probe bench is a shelf-
board micro-workload; its production-scale counterpart is the §4 probe/hash/
rebuild bucket, not the bench numbers directly.

## Addendum: remediation round negative result (2026-09-08, coordinator)

The three-bounded-task remediation round over this attribution (rule
split timer; in-parent dedup hash set; eval-cache rebalance to
set-associative 524,288 x 4 ways / 64 MiB) was implemented, verified
count-safe where required (0/29 and 0/26 gated fields identical; 31/31
CTest on four presets; tetris_engine_tests 59,182 checks), and MEASURED:

- Split timer (micro-workload, zero-duplicate): apply 80.8 percent /
  dedup 19.2 percent of the old rule scope — bounding the dedup-fix
  upside at ~3 percent of run.
- Eval cache: hit rate 4.812 -> 17.712 percent (eval_computed -13.55
  percent), gated work fields identical.
- Paired off-mode totals (three pairs, tight spreads): post/pre median
  1.0250 (p95 1.0272) — a consistent ~2.5 percent wall-time REGRESSION.

Adjudication: the round failed its wall-time gate and was REVERTED
(working tree restored to 65ab235; pre/post binaries preserved read-only
as tetris_profile_value.g_pre / .g_post in phase7_diag/binaries, post
hash d21017d6...). Two mechanisms are implicated (single-sample
hypotheses, not separated): dedup hashing costing more than the linear
scan on small fan-outs, and the 64 MiB cache footprint polluting L3
beyond what the hit-rate gain recovers (~1.2 s saved versus
multi-second pollution). Consequence: these two targets are now BOUNDED
as small-or-negative levers in this design space; future rounds need
either structurally different mechanisms (e.g. eval memo scope changes,
which are decision-adjacent and need partition adjudication) or
acceptance that the remaining ~2.98x gap is distributed broadly (rule
geometry 17.7, eval 17.1, enum 14.9, scaffold 23.3 percent) with no
single dominant lever remaining.
