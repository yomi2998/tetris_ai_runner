# Design: O(1) transposition-table reset via epoch stamps (accepted design)

Recorded 2026-09-07 from the read-only remediation design audit. This is
the implementation spec for the `reset_run_state` hotspot. Implementation
is gated on the measurement plan in Section 5; no performance claim is
made until those measurements exist.

## 1. Hotspot

`Engine::reset_run_state()` (`src/tetris_engine.cpp`) clears all 262,144
`TranspositionEntry` values (160 bytes each, 40 MiB) by full assignment on
every cold `set_root()`, and `reroot()` contains a second full clear plus
a per-entry clear inside its compaction scan. At ~220 roots per profile
run this is roughly 8 to 9 GB of reset memory traffic per run. The
recorded fixed-work workload keeps the table near full occupancy, so the
clear touches essentially every byte every root.

## 2. Safety lemma (from the complete read-site audit)

No code site reads `entry.key` or `entry.node` unless `entry.used == true`
was established first: `transposition_probe` gates on `used` before the
key comparison and node read, and the `reroot` scan gates on `used` before
filtering. `transposition_rehash_` is reroot scratch only (never probed),
and `idmap_` is never read stale (always resized first). Therefore stale
key/node bytes under a false `used` are observationally indistinguishable
from zeroed bytes, and any reset mechanism that makes exactly the set of
logically-live slots distinguishable from dead slots — with identical
probe order, hash, equality, insertion sequence, counters, flags, and
fail-stop behavior — preserves exact semantics.

## 3. Accepted design: u32 epoch stamp replacing the `used` bool

- `TranspositionEntry` becomes `{ TranspositionKey key; NodeId node;
  std::uint32_t epoch; }` — 152 + 4 + 4 = 160 bytes exactly, alignment 8.
  Both existing static_asserts (key 152, entry 160) must survive
  unchanged, and `engine_fixed_workspace` / `default_arena_capacity` /
  `retained_bytes` arithmetic is untouched because the entry size is
  unchanged.
- The engine holds `std::uint32_t transposition_epoch_`, initialized to 1
  in `init()`. Fresh entries from `resize()` value-initialize `epoch` to 0,
  which is the reserved "never current" stamp, so no initialization loop
  is needed.
- `transposition_probe` treats `entry.epoch != transposition_epoch_` as
  the empty/insert slot (same first-empty termination as today's `!used`),
  then compares keys only on current-epoch entries. Check order: epoch
  test strictly before the key comparison.
- Insert paths (`search_materialize_inner`, both depth paths) stamp
  `probe.slot->epoch = transposition_epoch_` where they set `used = true`
  today.
- `reset_run_state` replaces the full clear with a wrap-safe epoch
  advance: if the epoch is at `UINT32_MAX`, perform one full clear and
  restart at 1; otherwise increment. `transposition_used_ = 0` unchanged.
- `reroot` keeps its O(N) compaction scan (filtering requires visiting
  every slot) but drops both O(N) store passes: stage modified copies into
  `transposition_rehash_` as today, advance the epoch once (same
  wrap-safe helper) instead of the per-entry and full clears, then
  reinsert the staged entries stamped with the new current epoch. The
  existing reinsert behavior — including the silent-overwrite quirk when
  two retained entries collide to equal keys after the depth/cursor
  shift, and `transposition_used_ = moved` — is preserved exactly.
- The engine move constructor transfers `transposition_epoch_`.
- Cross-root isolation proof: after any epoch advance, no previous stamp
  equals the current epoch, so no probe can merge across roots and every
  probe terminates at the first non-current slot exactly as it terminates
  at the first unused slot today; chain-length distributions are identical
  given the same insertion sequence. The wrap path performs today's full
  clear, so behavior at wrap is today's behavior; the wrap occurs only
  after 2^32 - 1 roots and is unit-tested by forcing it.

Ground rules preserved by construction: `transposition_hash` byte stream
and field order; `operator==` field set including signed-zero
normalization; linear-probe order; merge-vs-insert decisions;
`transposition_used_` counting points; exhaustion fail-stop with its two
different depth-path returns; telemetry gating on `telemetry_on()`;
deterministic candidate order (insertion sequence unchanged); all memory
accounting formulas (entry size unchanged).

Rejected alternatives (recorded for the fallback order): touched-slot
flag clearing (safe but near-certain non-win at recorded near-full
occupancy — scattered cache-line touches exceed one sequential 40 MiB
pass — plus 1 MiB index state to budget), and a parallel flag/bitset
array (cheap reset but a permanent two-cache-line probe path on the
hottest loop plus the largest code churn). Fallback order if measurement
surprises: bitset variant, then touched-list.

## 4. Required new tests (added, not substituted)

- Epoch isolation: an identical key across a `set_root` boundary produces
  zero merges (port of the existing merge test across roots).
- Forced wrap: epoch at `UINT32_MAX` advances to exactly one full clear,
  restarts at 1, and behaves normally afterward.
- Reroot-under-epoch warm/cold parity: extends the existing reuse tests
  without modifying their assertions.
- The existing exhaustion, pressure, budget, memory-accounting, reuse,
  and determinism tests must pass unchanged (they pin the behavior this
  design must not alter).

## 5. Measurement gates (diagnostic campaign, not binding claims)

- A test-only microbench target (pattern: `tests/eval_cache_bench.cpp`)
  measures cold-path `set_root` reset cost and reroot cost in isolation,
  built once before the epoch change (pre state) and rebuilt after (post
  state); expectation: pre ~40 MiB sequential clear per root, post ~0.
- Deterministic count twins: `unique_candidates`, `transitions`,
  `searches`, `parents`, `materialized_nodes`, cache counters, and
  exhaustion flags must be bit-identical to the pre-change binary on the
  same workload (deltas of exactly zero; stronger than any 2 percent
  bar).
- Paired off-mode totals (twin-run amendment) pre vs post: any regression
  beyond the noise band is a defect signal for this pure-cost-removal
  change; the materialized component leg is watched for probe-compare
  regression.
- `transposition_merges`, `materialized_nodes`, and `texhaust_moves`
  per-seed aggregates identical; seeds 2 and 3 parse clean with zero
  replay failures.
- Uncertainties recorded honestly: the wall-time share of reset traffic
  on the qualification machine is unmeasured until the microbench and
  paired runs exist; the per-move occupancy distribution claim ("cluster
  just under capacity") comes from the 7.2E run totals and is not
  independently re-verified here — the epoch design wins at any occupancy,
  which is why it was chosen.
