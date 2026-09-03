# tetris_profile quiet output schema, version 2

Added in Phase 0. Version 1 output (the record without a schema token, produced by
`--quiet` alone or `--quiet-version 1`) is unchanged and remains valid for existing
scripts.

## Invocation

```bash
tetris_profile ... --quiet --quiet-version 2
```

## Record format

A single line beginning with the schema token `PROFILE_V2`, followed by space-separated
`key=value` fields in exactly the order listed below. Values contain no spaces.

| Order | Key | Unit | Meaning |
|---:|---|---|---|
| 1 | `moves` | count | Number of measured moves (warmup excluded) |
| 2 | `total_s` | seconds | Wall time over measured moves only |
| 3 | `min_ms` | ms | Minimum per-move time |
| 4 | `median_ms` | ms | Per-move median |
| 5 | `p95_ms` | ms | Per-move 95th percentile |
| 6 | `p99_ms` | ms | Per-move 99th percentile |
| 7 | `max_ms` | ms | Maximum per-move time |
| 8 | `evals` | count | Board evaluation calls |
| 9 | `transitions` | count | Policy transition (`get`) calls |
| 10 | `searches` | count | Placement search calls |
| 11 | `dead_moves` | count | Measured moves that died or topped out |
| 12 | `games` | count | Completed games within measured moves |
| 13 | `node_pool_bytes` | bytes | Tree node pool growth (byte delta, not an allocation count) |
| 14 | `evals_per_s` | 1/s | Evaluation rate |
| 15 | `transitions_per_s` | 1/s | Policy transition rate |
| 16 | `searches_per_s` | 1/s | Search rate |
| 17 | `warmup_moves` | count | Warmup moves executed but excluded from aggregates |
| 18 | `seed` | integer | RNG seed |
| 19 | `iters` | count | Iteration budget (0 when timing mode) |
| 20 | `maxdepth` | count | Search depth |
| 21 | `budget_ms` | ms | Time budget in timing mode (0 in iteration mode) |
| 22 | `mode` | token | `iters` or `ms` |
| 23 | `telemetry` | token | `on` or `off`; `off` zeroes fields 8 to 16 |

## Notes

- `--warmup-moves N` executes N moves first. They are excluded from every time and
  work aggregate but they advance the shared RNG state, so measured work counts differ
  from a run without warmup. Paired comparisons must use identical warmup settings.
- The `telemetry=off` run of the same binary exists for the telemetry-overhead gate
  only; it produces no work counts.
- Version 2 carries the complete work vector available in the legacy engine. The value
  engine's component telemetry (parent expansions, raw kernel landings, semantic
  candidates, rule transitions, cache hits and misses, materialized nodes, widening
  iterations, selected-path states, replay failures) will be added to this schema when
  those components exist, before the Phase 8 acceptance campaign.
