# Migration comparison per-pair output schema, version 1

Frozen in Phase 0. Produced by the temporary `migration_compare` target and, at
value-engine cutover, by the tuner `compare-engines` mode (plan Section 14.4).
One row per seat-swapped pair. Separator: comma. No quoting; values contain no
commas. Encoding: UTF-8, LF line endings, header row first.

## Columns, in exact order

| # | Name | Meaning |
|---:|---|---|
| 1 | `pair` | Pair id, 0-based, ordered by creation |
| 2 | `seed` | Campaign seed the pair was generated from |
| 3 | `scenario_seed_p1` | Deterministic scenario seed for seat 1 |
| 4 | `scenario_seed_p2` | Deterministic scenario seed for seat 2 |
| 5 | `engine1` | Engine selector for seat 1: `legacy` or `value` |
| 6 | `engine2` | Engine selector for seat 2: `legacy` or `value` |
| 7 | `winner` | Native tuner winner encoding for game 1 (seat 1 minus seat 2 sign convention; see tuner `MatchResult`) |
| 8 | `dead1` | Seat 1 died in game 1 |
| 9 | `dead2` | Seat 2 died in game 1 |
| 10 | `capped` | Game 1 hit the round cap |
| 11 | `winner_reason` | Native tuner `MatchResult::WinnerReason` code |
| 12 | `rounds` | Rounds played in game 1 |
| 13 | `attack1` | Seat 1 total attack, pooled over game 1 |
| 14 | `attack2` | Seat 2 total attack, pooled over game 1 |
| 15 | `pieces1` | Seat 1 placed pieces, game 1 |
| 16 | `pieces2` | Seat 2 placed pieces, game 1 |
| 17 | `lines1` | Seat 1 cleared lines, game 1 |
| 18 | `lines2` | Seat 2 cleared lines, game 1 |
| 19 | `tspin_mini1` | Seat 1 T-spin mini count, game 1 |
| 20 | `tspin_mini2` | Seat 2 T-spin mini count, game 1 |
| 21 | `tspin_single1` | Seat 1 full T-spin single count, game 1 |
| 22 | `tspin_single2` | Seat 2 full T-spin single count, game 1 |
| 23 | `tspin_double1` | Seat 1 T-spin double count, game 1 |
| 24 | `tspin_double2` | Seat 2 T-spin double count, game 1 |
| 25 | `tspin_triple1` | Seat 1 T-spin triple count, game 1 |
| 26 | `tspin_triple2` | Seat 2 T-spin triple count, game 1 |
| 27 | `perfect_clear1` | Seat 1 perfect clears, game 1 |
| 28 | `perfect_clear2` | Seat 2 perfect clears, game 1 |
| 29 | `replay_failures` | Path replay failures observed in the pair (always 0 for the legacy engine; hard gate is zero) |

## Notes

- Columns 13 to 28 are per-game totals for the pair's single game. APP and APL
  are recomputed as pooled ratios over all pairs by the analysis script, never
  averaged per pair.
- Seat swapping is encoded by the scenario seeds: both seats of a pair use
  identical parameters, and the engine selectors in columns 5 and 6 record the
  assignment. A pair with `engine1=legacy, engine2=value` and the same seeds
  with `engine1=value, engine2=legacy` are the two games of one seat-swapped
  comparison.
- The winner encoding is the tuner's native integer (survivor, APL cap, both
  dead, or draw). The analysis script maps it to win-equivalent scores; it must
  not be reinterpreted by ad hoc tooling.
- `replay_failures` is instrumented from the value engine's path materializer.
  The legacy engine has no replay validation, so its rows carry 0, and the
  schema reserves the column so row width never changes across phases.
- Schema version 1 is frozen. Any column addition requires a new schema version
  token in the file and a documented migration note before the 32-pair screen.
