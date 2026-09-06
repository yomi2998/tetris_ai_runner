# Slice 6.6 design note: selected-result and final-path materialization

## API and ownership

```cpp
struct PathTelemetry
{
    std::size_t calls = 0;
    std::size_t states_expanded = 0;
    std::int64_t elapsed_nanos = 0;
    std::size_t failures = 0;
};

struct FinalResult
{
    bool has_selection = false;
    bool path_ok = false;
    std::optional<Candidate> candidate;
    Piece played = Piece::T;
    PolicyState state;
    bool used_hold = false;
    tetris::path::Path path;
    std::size_t states_expanded = 0;
    std::int64_t elapsed_nanos = 0;
};

FinalResult finalize(Placement active_start);
PathTelemetry path_telemetry() const;
```

`finalize` selects through the existing `select_best()` and returns
the result by value (caller-owned, about 1.2 KiB, no heap). The
engine keeps no result member: "once" means once per explicit
finalization request, with no result cache or invalidation
machinery. Path telemetry is cumulative engine state, reset only
by `init` and transferred by the move constructor; search and
reroot never touch it. `select_best`, `run`, `set_root`, and the
expansion pipeline stay exactly as they are for search-only
callers.

## Result contents

- Candidate, played piece, and post-move policy state come from the
  selected root child, never from the deeper evidence node.
- `used_hold` follows branch history (`source == Hold`), not piece
  comparison, so swapping equal piece types keeps the `v` operation.
- The path holds movement commands only. The later DLL layer owns
  the envelope: it prepends `v` when `used_hold` is set and appends
  `V` with the null terminator per the preserved 1,024-byte slot
  contract (§14.1 keeps output assembly in `ai.cpp`, so the engine
  does not build envelopes here).
- Empty selection returns `has_selection == false` with no
  pathfinder construction and no telemetry change. A searched but
  unreachable selection returns the candidate and state with
  `path_ok == false` and an invalid empty path; there is no
  fallback search over alternative candidates in this slice.

## Start pose and movement rules

- The caller supplies the actual active pose as a `Placement` value.
  The current-piece branch pathfinds from it verbatim on the
  pre-move root board; an unfitting or out-of-range start yields an
  explicit failure, never a disguised empty path.
- Hold branches ignore the supplied pose and start from canonical
  spawn `(4, 20, 0)`: the held piece after `v` for occupied hold,
  the correct next concrete piece (`node.played`) after `v` for
  empty hold. External-coordinate adaptation stays at the later DLL
  boundary; this slice accepts canonical poses only.
- The pathfinder piece is always the selected `node.played`, and
  the movement mapping is exact: `PathConfig.allow_180` copies the
  engine movement configuration, matching enumeration, so a
  candidate reachable in search is reachable from spawn.
- A valid zero-movement path (`valid`, size zero) and failure
  (`!valid`) are distinguished by `Path.valid`, mirrored in
  `path_ok`.

## Workspace lifetime and memory accounting

- The `Pathfinder` (measured 24,976 bytes) is constructed as a
  stack local inside `finalize`, plus its build temporary and a
  small find/result peak. The measured worst case without return
  elision is near 80 KiB, so `engine_stack_peak_allowance` grows
  from 64 KiB to 128 KiB; the whole-engine budget formulas absorb
  it with no other change (about two hundred fewer arena nodes).
- No heap allocation occurs in `finalize`; allocation-free search
  is preserved. The returned `Path` is caller-owned and excluded
  from `retained_bytes()`.
- Exactly one pathfinder construction and one `find()` call happen
  per searching finalization; search, reroot, materialization, and
  transposition paths construct none.

## Telemetry

- Cumulative `calls` counts finalizations that ran a path search;
  `failures` counts searched but unreached selections;
  `states_expanded` sums the finder BFS push counts (`queue_tail`);
  `elapsed_nanos` sums the construction-plus-extraction clock
  delta. Each result also carries its own states and elapsed time.
- Path counters stay separate from `SearchStats` and search-budget
  timing; the full operation (setup, root update, search, final
  path) is left for production qualification.
