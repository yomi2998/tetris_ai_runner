# `toj_policy_v2.csv` schema

One self-contained policy case per line. Lines starting with `#`
carry generator metadata (command, 29 parameter words, combo table)
and the column header. All other lines hold 45 space-separated
fields in this order:

| # | Field | Meaning |
|---|---|---|
| 0 | `id` | Sequential case number from 0 |
| 1 | `board` | Board tag: `s0`..`s23` seeded, `empty`, `tall`, `o1`, `o2`, `t1`, `iwell3`, `iwell4`, `slotS`, `slotM`, `mini0`, `mini1`, `mini2`, `double0` |
| 2 | `piece` | Current piece letter |
| 3-5 | `x y r` | Legacy landing status coordinates and rotation |
| 6 | `arrival` | Rotation witness: 1 when the search path ends with rotation |
| 7 | `spin_in` | Search-reported spin type (always 0; the legacy search never presets it) |
| 8 | `spin_eff` | Effective spin input: legacy `get` reclassification result, 0 none, 1 full, 2 mini |
| 9-12 | `is_check is_last_rotate is_ready is_mini_ready` | Legacy witness flags |
| 13 | `clear` | Lines cleared by the placement, 0 through 4 |
| 14 | `src_rows` | 40 source rows, hexadecimal, comma separated, row 0 first |
| 15 | `result_rows` | 40 rows after the placement is attached |
| 16-24 | `p_death p_combo p_under p_maprise p_b2b p_t2 p_t3 p_acc p_like p_value` | Complete parent policy state |
| 25-29 | `next hold node is_hold depth` | Decision context: next letters (`-` when empty), hold letter (`-` when none), current piece, hold flag, depth |
| 30-32 | `eval_value eval_t2 eval_t3` | Expected board evaluation |
| 33-43 | `o_death o_combo o_b2b o_under o_maprise o_t2 o_t3 o_acc o_like o_value` | Complete expected resulting policy state (10 fields) |
| 44 | `safe` | Legacy safety value for the source board and piece |

Every data line holds exactly 45 fields.

## Floating point

Every double uses 16 lowercase hexadecimal digits of its IEEE 754 bit
pattern (`std::bit_cast` to `uint64_t`). Round trips are bit-exact,
including signed zero. Integer, enum, flag, row, and cell fields
require exact equality. Expected evaluation and resulting-state
doubles allow up to 5 ULP of absolute bit distance: the legacy oracle
itself varies by up to 4 ULP between unoptimized and optimized builds
(see `fixture_hashes.txt`), so parity permits the observed distance
plus one ULP, under the 8 ULP ceiling.

## Spin encoding

The new policy takes its spin input from the rule outcome. The legacy
`get` derives it inline: with lines cleared, a checked rotation
ending, and single clearance plus mini readiness it uses mini (2);
with a checked rotation ending and corner readiness it uses full (1);
otherwise none (0). `spin_eff` records that derived input so parity
stays a pure function comparison.

## Coverage contract

The corpus must contain clears 0, 1, 2, 3, and 4; spin inputs none,
full, and mini; full spins at clears 1, 2, and 3; T2 and T3 descriptor
gains; death, under-attack, map-rise, accumulation, empty-hold,
T-hold, I-hold, empty-next, T-first, T-absent, rotation-witness, and
high-stack landings; and at least one perfect-clear (empty result
rows) case. `tests/toj_policy_tests.cpp` asserts every category.
