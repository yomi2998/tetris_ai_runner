# Screen stage verdict, 2026-09-10

Terminal classification: ADVANCE.

## QUALITY_MATCH row

games=256 value_win_equiv=111.0 wr=0.433594 ci95_two=[0.374311,0.494840] lo95_one=0.383603 app_value=1.029365 app_other=1.004978 apl_value=1.595474 apl_other=1.477128 rounds_mean=242.637 deaths_value=145 deaths_other=111 replay_failures=0 arena_exhaustions=0 deaths_spawn=71 deaths_lockout=2 deaths_invalid=0 value_wall_ms_median=20.638 value_wall_ms_p95=22.280 total_wall_s=177.273

## Go/no-go gate (frozen, section 6 of the protocol)

- Zero replay failures: pass (0 of 256 games).
- No memory kill: pass (clean exit, CPU 15 restored).
- WR point estimate not below 0.40: pass (0.433594).
- APP not below 0.90 relative: pass (1.024264).
- APL not below 0.90 relative: pass (1.080126).

All five conditions hold. The sole terminal verdict is ADVANCE to the final 1000-pair campaign.

## Read-only observations (diagnostic, not campaign criteria)

The value engine lost the screen (win-equivalence 43.36 percent, two-sided CI [0.374, 0.495]) while attacking more per piece (1.024x) and more per line (1.080x). It died more often (145 versus 111), with spawn deaths the largest class (71). The one-sided 95 percent lower bound 0.3836 sits below the campaign non-inferiority bound of 0.47. These are screening observations at 256 games; the campaign decides the frozen criteria on 2000 fresh games.

Parent verification: arithmetic independently reproduced from matches.csv (WR and both Wilson bounds to six decimals), CSV seat-pair structure verified, hashes verified, CPU 15 online, no surviving process.
