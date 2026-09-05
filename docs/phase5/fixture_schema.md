# `toj_policy_v2.csv` schema

One self-contained policy case per line. Lines starting with `#`
carry generator metadata (command, 29 parameter words, combo table)
and the column header. All other lines hold 46 space-separated
fields in this order:

| # | Field | Meaning |
|---|---|---|
| 0 | `id` | Sequential case number from 0 |
| 1 | `board` | Board tag: `s0`..`s23` seeded, `empty`, `tall`, `o1`, `o2`, `t1`, `iwell3`, `iwell4`, `slotS`, `slotM`, `mini0`, `mini1`, `mini2`, `double0`, `double1`, `double2`, `double3`, `pci`, `pci2`, `pci3`, `pci4` |
| 2 | `piece` | Current piece letter |
| 3-5 | `x y r` | Legacy landing status coordinates and rotation |
| 6 | `arrival` | Rotation witness: 1 when the search path ends with rotation |
| 7 | `spin_in` | Search-reported spin type (always 0; the legacy search never presets it) |
| 8 | `spin_eff` | Effective spin input: legacy `get` reclassification result, 0 none, 1 full, 2 mini |
| 9-12 | `is_check is_last_rotate is_ready is_mini_ready` | Legacy witness flags |
| 13 | `clear` | Lines cleared by the placement, 0 through 4 |
| 14 | `src_rows` | 40 source rows, hexadecimal, comma separated, row 0 first |
| 15 | `result_rows` | 40 rows after the placement is attached |
| 16-25 | `p_death p_combo p_under p_maprise p_b2b p_t2 p_t3 p_acc p_like p_value` | Complete parent policy state (10 fields) |
| 26-30 | `next hold node is_hold depth` | Decision context: next letters (`-` when empty), hold letter (`-` when none), current piece, hold flag, depth (5 fields) |
| 31-33 | `eval_value eval_t2 eval_t3` | Expected board evaluation |
| 34-43 | `o_death o_combo o_b2b o_under o_maprise o_t2 o_t3 o_acc o_like o_value` | Complete expected resulting policy state (10 fields) |
| 44 | `safe` | Legacy safety value for the source board and piece |
| 45 | `cfg_safe` | Engine safety configuration for the transition: 0, 5, or 16, cycling by case |

Every data line holds exactly 46 fields.

## Floating point

Every double uses 16 lowercase hexadecimal digits of its IEEE 754 bit
pattern (`std::bit_cast` to `uint64_t`). Round trips are bit-exact,
including signed zero. Integer, enum, flag, row, and cell fields
require exact equality. The transition and evaluation gates compare
new outputs directly against these frozen fields: exact integers and
at most 5 ULP of absolute bit distance on doubles.

The corpus bytes embody fused multiply-add contraction from
`-O3 -march=native` builds. The same legacy computation without FMA
(`-O0`, `-ffp-contract=off`, or generic `-march`) drifts by up to
64 ULP on one cancellation-sensitive accumulation, up to 7 ULP on a
few more values, and at most 2 ULP elsewhere, with zero non-floating
differences. The 5 ULP ceiling therefore holds for every case the
oracle itself can reproduce; the few cases beyond oracle precision
(5 under debug builds, 0 under release builds) fall back to a
same-build legacy comparison at the same 5 ULP bound, with their
frozen drift reported rather than gated. See `fixture_hashes.txt`
for the measured per-configuration tables.

## Spin encoding

The new policy takes its spin input from the rule outcome. The legacy
`get` derives it inline: with lines cleared, a checked rotation
ending, and single clearance plus mini readiness it uses mini (2);
with a checked rotation ending and corner readiness it uses full (1);
otherwise none (0). `spin_eff` records that derived input so parity
stays a pure function comparison.

## Coverage contract

The corpus must contain clears 0, 1, 2, 3, and 4; spin inputs none,
full, and mini; full spins at clears 1, 2, and 3 with nonzero safety;
minis at nonzero safety; T2 and T3 descriptor gains; death,
under-attack, map-rise, accumulation, empty-hold, T-hold, I-hold,
empty-next, T-first, T-absent, rotation-witness, and high-stack
landings; config-safe variants 0, 5, and 16; and perfect clears whose
bonus is live (empty result rows with nonzero safety and zero
map-rise). Non-spin T triples never arise from the search, so the
clear-3 waste formula is pinned instead by a directed synthetic
transition at nonzero safety. `tests/toj_policy_tests.cpp` asserts
every category.
