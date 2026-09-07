# Design note: the two missing legacy corpus comparators (gates 8 and 9)

Status: design RESOLVED 2026-09-07 (all four questions answered with
evidence; see the resolved section). Implementation may proceed from a
clean committed tree. This note exists because the 2026-09-07 audit
(`audit_71c_72e.md`) established that plan Section 17.3 items 8 and 9
have no comparator: `tests/run_perf_gate.py` pairs only kernel builds
and Reference A. Gates 8 and 9 are UNPROVEN until the comparators below
exist, are frozen, and are run under the pinned-core protocol.

## What the plan requires

- Item 8: median paired time-per-parent of raw non-T enumeration versus
  frozen legacy placement enumeration at most 1.00, on the frozen search
  corpus.
- Item 9: the new T enumerator at most 1.02 per normalized semantic
  candidate versus the frozen legacy `search_tspin` enumerator (and at
  least 2x versus Reference A, which is already covered).

Both legs need a frozen legacy-side corpus binary that times the legacy
search per parent and reports per-case normalized candidate counts.

## Proposed comparators

One new test-only binary, `legacy_corpus_bench`, built from the legacy
sources exactly like `tetris_profile_legacy_cmp` (same engine preparation,
same `search_config`: `allow_rotate_move=false, allow_180=true,
allow_d=true, is_20g=false, last_rotate=false`, `TETRIS_LEGACY_CMP`
unset so zero hook code compiles):

1. Prepare the legacy engine once (`prepare(10, 40)`, 256 MiB limit).
2. For every corpus board and piece: import the board into a fresh
   `TetrisMap`, generate the piece node, call `search_tspin::Search::
   search(map, node, depth)` once, and time the call (nanoseconds per
   parent).
3. Normalize every returned land point with the existing
   `legacy_cmp::normalize_land_point` machinery, but emit through
   `candfmt::Report` with the arrival-class channel used by the corpus
   harness: channel = `is_last_rotate ? 1 : 0` for T, channel 0 and
   `keep_arrival=false` for non-T. This matches `arrival_candidates`'
   counting contract exactly (sorted-cell occupancy identity, T arrival
   channel, non-T orientation collapse).
4. Emit `REP <rep> <piece> <sink> <ns_per_parent> legacy_corpus` rows and
   a `CORPUS legacy_corpus cases N candidates M <hash>` line, so
   `run_perf_gate.py` can pair it with `arrival_candidates` without new
   plumbing beyond a sequence entry and gate table rows.

Gate table additions: non-T time-per-parent `current/legacy_corpus` at
most 1.00 (item 8); T `current/legacy_corpus` time-per-parent divided by
the per-side normalized candidate counts at most 1.02 (item 9). The
per-candidate normalization uses each side's own candidate count on the
same boards, computed inside the harness from the CORPUS lines, never a
count borrowed from a different comparison.

## Resolved design questions (2026-09-07 audit; evidence-graded)

1. Corpus compatibility — RESOLVED. The gate uses the
   `reach_corpus::make()` (`tests/reach_corpus.h`) grid restricted to the
   legacy-comparable subcorpus: all boards except indices {22, 23, 24}
   (stack height 44, occupancy rows 0..43) and {26} (near-top, rows
   38..45), which exceed the legacy `TetrisMap` height 40
   (`src/tetris_core.h`, `prepare` rejects height > 40). The remaining 33
   boards (original indices {0..21, 25, 27..36}) are imported unmodified
   on both sides by the identity map `row[y] = rows[y]` (legacy import
   pattern with metadata rebuild as in `tests/rule_differential.cpp`; new
   side as in `src/arrival_candidates.cpp`). Both sides emit CASE rows
   keyed by the ORIGINAL board index so rows pair 1:1 with the existing
   `arrival_candidates` output (33 x 7 = 231 CASE rows per side).
   Clipping is forbidden. The Phase-0 `reach.csv` fixture corpus is not
   used (low variety, second loader, different grid).
   Intentional asymmetry, stated so no implementer "fixes" it: subcorpus
   boards have empty rows 40..47, so the new side may enumerate
   placements with cells above row 39 that the legacy net cannot
   represent. That is the migration premise (fast reachability discovers
   legal placements the old search misses); gate 8 compares time per
   parent on identical occupancy and gate 9 divides by each side's own
   normalized candidate count, which is the honest normalization.
   Suppressing or clipping new-side sky placements is forbidden.
   Identity check (untimed, debug/ASan build): for each included board
   the bench asserts max occupied row <= 39 and emits
   `BOARDCASE <piece> <board> <fnv-of-40-row-masks>`; the implementer
   verifies these 231 hashes against an independent recomputation from
   `tests/reach_corpus.h`, recorded with the gate. Candidate CORPUS
   hashes are NOT expected to match across engines; only inputs and the
   counting contract must be identical.
   Contingency: subcorpus boards 19..21 (stack 40) place occupancy at row
   39 and legacy `Search::check_ready` reads `map.row[y+1]`, which can
   touch `row[40]` (adjacent `top[0]`, deterministic in-process). If the
   ASan identity run reports it, drop {19, 20, 21} as well (30-board
   fallback) and record which corpus was used with the gate.

2. Freeze provenance — RESOLVED, superseded by direct evidence. No
   document pins the exact source commit behind `tetris_profile.baseline`
   (`84cb7a30...`), but the 2026-09-07 audit rebuilt the default
   `tetris_profile` target from the current tree with the
   `linux-gcc-self-release` preset and obtained a byte-identical binary
   (same sha256 `84cb7a30...`). This proves the committed flag-off
   legacy sources compile to exactly the frozen baseline bytes, which is
   stronger than commit identification. Corroborated by source history:
   `rule_toj.*`, `ai_zzz.*`, `search_tspin.h` are byte-identical from
   `82bed15` to HEAD; `tetris_core.*` / `search_tspin.cpp` differ only by
   additions inside `#ifdef TETRIS_LEGACY_CMP` plus one inert include;
   the frozen `tetris_profile` target never defines the flag. Build
   `legacy_corpus_bench` from a CLEAN checkout of the recorded commit
   (`git status --porcelain` empty), with the same preset, provenance
   stamps, and artifact-hash recording used by the existing corpus
   binaries, and re-run the `legacy_cmp_parity_frozen` CTest on the same
   tree as the behavioral-identity proof. If any future commit touches a
   legacy source outside an `#ifdef TETRIS_LEGACY_CMP` block, this
   resolution lapses and the byte-identity must be re-proven.

3. Timing scope — RESOLVED. `Search::search` caches nothing across calls
   (all scratch state cleared on entry; mark clears are O(1) version
   bumps; the land-point fast path reads static prepare-time net data).
   Only vector capacity persists, absorbed by warmup. Exact per-case
   timed sequence (mirrors `run_timed` in `src/arrival_candidates.cpp`):
   fresh `TetrisMap(10, 40)` → copy rows 0..39 → metadata rebuild →
   `context->generate(piece)` → `search(map, node, 1)` → sink accumulate,
   timed as one span per parent. Engine/`Search` setup
   (`prepare(10, 40)`, 256 MiB limit, full search_config with
   `allow_rotate_move=false, allow_180=true, allow_d=true, allow_D=true,
   allow_LR=true, allow_nont_d=false, is_20g=false, last_rotate=false`,
   `Search::init` once) happens once per process, outside timing.
   `depth=1` matches both existing per-board harnesses; with
   `last_rotate=false` the depth argument is immaterial. Normalization
   (`legacy_cmp::normalize_land_point` + `candfmt::Report`; T arrival
   channel via `is_last_rotate`, non-T channel 0 with `keep_arrival=false`)
   runs in a separate untimed pass emitting `REP`/`CORPUS legacy_corpus`
   rows, exactly like `run_report` versus `run_timed`; normalization
   never enters the timed span. `REP` lines must keep the positional
   shape `REP <rep> <piece> <sink> <ns> <producer>` that
   `tests/run_perf_gate.py` parses (ns is the fifth token).

4. Fresh-engine divergence — RESOLVED. Per
   `docs/phase7/comparator_schema.md`, sequentially constructed fresh
   legacy engines in one process can disagree (heap-reuse-dependent,
   predates all comparator work). The bench constructs exactly ONE
   engine per process (`prepare(10, 40)` + `Search::init` once) and
   drives all 231 cases through it; it never constructs a second engine
   or compares engines in-process. Cross-binary agreement is established
   across separate deterministic processes.

## Ordering

Design resolved 2026-09-07. Implementation may proceed from a clean
committed tree after the audit commit, ahead of any rerun of the binding
campaign, since gates 8 and 9 feed the same qualification decision as
gates 1 through 7. The bench binary and gate-table additions must be
frozen with provenance stamps before the gate runs.
