# Execution runbook: sanctioned control-corrected rerun, phase-7 gates 1 and 2

Date directory: results/phase7/gate12_2026-09-09
Campaign: complete five-pair block, gates 1 and 2, seed 1 fixed-work mode
Prior campaign: results/phase7/gate12_2026-09-08
Prior result: gate 1 median total ratio 1.02564 against 1.02 bar (FAIL); gate 2 median p95 ratio 1.00023 (pass on median, blocked as a pass claim by spread); pair spreads 0.071 total and 0.103 p95; first baseline block 5.4 to 7.6 percent faster than its steady state
Authority: exactly one control-corrected rerun of the complete five-pair block, in a new directory, never appending favorable pairs. See section 12 for verbatim quotes.

Runbook status at authoring time: RUNBOOK OK
Authoring checks observed: HEAD 280c140 on branch fast-reachability-migration; git status shows no tracked modifications (only untracked files); CPU 7 governor performance; CPU 7 energy_performance_preference performance; global boost file 1; 5-minute loadavg 1.36 below 1.5.

## 0. Standing rule: one fresh block, no appended pairs

This runbook executes exactly one complete five-pair block (10 timed runs) in results/phase7/gate12_2026-09-09. If any run or pair is invalid, the whole block is discarded or renamed per the abort policy in section 9. No additional pairs are run after the verdict is known. No favorable subset is selected. An aborted block does not consume the rerun sanction and a replacement fresh block may be started under this same runbook only if the abort reason is cleared.

## 1. Preconditions

All preconditions must hold before any pre-warm or timed run. If any precondition fails, stop and do not run.

1a. Tracked tree clean at HEAD 280c140.
Commands:
  git rev-parse HEAD
  git status --short
Required: first command prints 280c140b7c9a0ad9e7ca8b81aaf653207caff973. Second command shows no tracked modifications. Lines starting with M, A, D, R, or C in the first two columns are forbidden. Lines starting with ?? (untracked files) are permitted and do not block. Record both outputs in the machine record.

1b. CPU 7 scaling governor is performance.
Verify:
  cat /sys/devices/system/cpu/cpu7/cpufreq/scaling_governor
Required output: performance
Set if needed:
  echo performance | sudo tee /sys/devices/system/cpu/cpu7/cpufreq/scaling_governor
Record the before and after values.

1c. CPU 7 energy_performance_preference is performance.
Verify:
  cat /sys/devices/system/cpu/cpu7/cpufreq/energy_performance_preference
Required output: performance
Set if needed:
  echo performance | sudo tee /sys/devices/system/cpu/cpu7/cpufreq/energy_performance_preference
Record the before and after values.

1d. Global boost file is 1 and fixed.
Verify:
  cat /sys/devices/system/cpu/cpufreq/boost
Required output: 1
Do not change it. If it reads anything other than 1, stop and record status BLOCKED with reason boost-not-fixed.

1e. Machine quiet: 5-minute loadavg below 1.5.
Read:
  cat /proc/loadavg
Field 2 (the middle value) is the 5-minute average. Required: field 2 strictly below 1.5.
Procedure: sample once. If field 2 is 1.5 or higher, wait 60 seconds and sample again, repeating for up to 10 minutes (at most 10 rechecks). If field 2 falls below 1.5 within that window, proceed and record every sample. If field 2 stays at or above 1.5 after 10 minutes, runbook status is BLOCKED with reason machine-not-quiet and nothing runs. No pre-warm and no timed run may start while blocked.

## 2. Pre-warm control for the first-block anomaly

Purpose: remove the cold-start fast-first-block effect seen on 2026-09-08 where the first baseline block ran 5.4 to 7.6 percent faster than steady state.

Procedure: immediately before pair 1.1-a, after the SMT control in section 3 and after a passing load check, run each binary once with reduced work and discard the outputs entirely:
  env -u TETRIS_AI_PARAM_FILE taskset --cpu-list 7 /home/icly/Documents/tetris_ai_runner_results/phase7/tetris_profile.baseline --seed 1 --warmup-moves 20 --moves 40 --maxdepth 6 --param-file /home/icly/Documents/tetris_ai_runner/artifacts/frozen_29d.bin --iters 200 --quiet --quiet-version 2 --telemetry off
  env -u TETRIS_AI_PARAM_FILE taskset --cpu-list 7 /home/icly/Documents/tetris_ai_runner_results/phase7/tetris_profile_value.candidate-2026-09-08 --seed 1 --warmup-moves 20 --moves 40 --maxdepth 6 --param-file /home/icly/Documents/tetris_ai_runner/artifacts/frozen_29d.bin --iters 200 --quiet --quiet-version 3 --telemetry off
Discard stdout and stderr of both pre-warm runs. Do not append them to rows.txt. Do not number them in the manifest. Record only in docs/phase7/campaign_machine_record_2026-09-09.md that both binaries were pre-warmed with --moves 40 --iters 200 --telemetry off, with timestamps. Then proceed directly to pair 1.1-a with no idle gap beyond what the commands require.

## 3. SMT sibling control

Sibling identity: logical CPU 15 is the SMT sibling of logical CPU 7.
Take offline before the first timed run (after preconditions pass, before pre-warm):
  echo 0 | sudo tee /sys/devices/system/cpu/cpu15/online
Restore online after the last run completes:
  echo 1 | sudo tee /sys/devices/system/cpu/cpu15/online
Abort paths: every abort path in section 9 must restore CPU 15 online with the same echo 1 command before exiting, even if the abort happens mid-block. Record the offline window as UTC ISO Z timestamps of the two commands, plus frequency samples at block start and end, in the machine record.

## 4. Pair order

Exactly ten timed runs in this fixed order. Labels: a is the first run of the pair, b is the second run of the pair.

  Pair 1.1: 1.1-a baseline, then 1.1-b candidate
  Pair 1.2: 1.2-a candidate, then 1.2-b baseline
  Pair 1.3: 1.3-a candidate, then 1.3-b baseline
  Pair 1.4: 1.4-a baseline, then 1.4-b candidate
  Pair 1.5: 1.5-a baseline, then 1.5-b candidate

Within each pair the second run starts immediately after the first. No reordering. No repetition of a single pair. Manifest pair labels are 1.1-a through 1.5-b in execution order.

## 5. Command template

Copied from the 2026-09-08 campaign. Template:
  env -u TETRIS_AI_PARAM_FILE taskset --cpu-list 7 <binary> --seed 1 --warmup-moves 20 --moves 200 --maxdepth 6 --param-file /home/icly/Documents/tetris_ai_runner/artifacts/frozen_29d.bin --iters 1000 --quiet --quiet-version N --telemetry off
N=2 for the baseline. N=3 for the candidate.
Fully expanded baseline command:
  env -u TETRIS_AI_PARAM_FILE taskset --cpu-list 7 /home/icly/Documents/tetris_ai_runner_results/phase7/tetris_profile.baseline --seed 1 --warmup-moves 20 --moves 200 --maxdepth 6 --param-file /home/icly/Documents/tetris_ai_runner/artifacts/frozen_29d.bin --iters 1000 --quiet --quiet-version 2 --telemetry off
Fully expanded candidate command:
  env -u TETRIS_AI_PARAM_FILE taskset --cpu-list 7 /home/icly/Documents/tetris_ai_runner_results/phase7/tetris_profile_value.candidate-2026-09-08 --seed 1 --warmup-moves 20 --moves 200 --maxdepth 6 --param-file /home/icly/Documents/tetris_ai_runner/artifacts/frozen_29d.bin --iters 1000 --quiet --quiet-version 3 --telemetry off
TETRIS_AI_PARAM_FILE must be unset via env -u on every run including pre-warm. Pin to CPU 7 on every run. No other flags, no substitution of paths, no change of seed, moves, iters, warmup, depth, or telemetry setting.

## 6. Artifacts and hashes

Verify each sha256 with sha256sum before the block. Any mismatch blocks the run.

  Baseline: /home/icly/Documents/tetris_ai_runner_results/phase7/tetris_profile.baseline
    sha256 84cb7a309f59db98b33709ececc168d330991b2226956cc7b310445870aac376
  Candidate: /home/icly/Documents/tetris_ai_runner_results/phase7/tetris_profile_value.candidate-2026-09-08
    sha256 1dccaa808044593bd97d6e8af09848ce8b19e75b9dcefa50d63a17a6381db765
  Parameter file: /home/icly/Documents/tetris_ai_runner/artifacts/frozen_29d.bin
    sha256 ea95ba584f4eb5a7234bf0fbdd68fb422e00e29d13373e4df9e6b9ea2ca98037

Artifacts under /home/icly/Documents/tetris_ai_runner_results/ must never be modified, overwritten, re-frozen, or chmodded by this runbook. Read-only execution only.

## 7. Manifest line format

File: results/phase7/gate12_2026-09-09/MANIFEST.txt
One line per timed run, lines 1 through 10 in execution order. Format identical to the 2026-09-08 manifest with load1 and load5 added to the controls field:
  line=<n> gate=gates1-2 pair=<label> artifact=<path> sha256=<hash> started=<UTC ISO Z> exit=<n> controls=cpu7,governor-performance,epp-performance,boost-fixed,smt-sibling-offline,load1=<x>,load5=<y> cmd=<binary> --seed 1 --warmup-moves 20 --moves 200 --maxdepth 6 --param-file <param path> --iters 1000 --quiet --quiet-version <N> --telemetry off
Rules:
  <n> counts 1 to 10 in execution order.
  <label> is one of 1.1-a, 1.1-b, 1.2-a, 1.2-b, 1.3-a, 1.3-b, 1.4-a, 1.4-b, 1.5-a, 1.5-b.
  artifact and sha256 come from section 6 for the binary actually run.
  started is the UTC ISO Z timestamp captured immediately before that run starts.
  exit is the observed exit code of that run.
  load1 and load5 are fields 1 and 2 of /proc/loadavg read immediately before that run starts.
  cmd records the binary path actually executed plus the flags after the binary, with quiet-version 2 for baseline and 3 for candidate.
  Pre-warm runs are never manifest lines.

## 8. Raw preservation

File: results/phase7/gate12_2026-09-09/rows.txt
rows.txt holds one raw output row per timed run in execution order, nothing else, no aggregation. Ten lines on success: the exact stdout row of each run, unedited, in the order runs executed. Pre-warm output never enters rows.txt. Manifest line <n> maps to rows.txt line <n>. No means, medians, ratios, or commentary inside rows.txt.

## 9. Abort policy

Stop the block, append nothing further, rename the directory to gate12_2026-09-09_aborted, restore CPU 15 online, and record status ABORTED with the reason. An aborted block does not consume the rerun sanction; a fresh complete block may be attempted once the cause is cleared.

Abort triggers:
  Any timed run exits nonzero. Record the manifest line with its nonzero exit, keep its row if one was emitted, then stop. Do not start the next run.
  Or: the 5-minute loadavg (field 2 of /proc/loadavg) measured immediately before a not-yet-started pair is 2.0 or higher. Then wait, rechecking once per minute for up to 5 minutes. If field 2 falls below 1.5 within that window, start the pair and record all samples. If it does not fall below 1.5 within 5 minutes, abort the block.
On every abort: run the CPU 15 restore command from section 3 before exiting, record ABORTED and the triggering reason in the machine record and in verdict.md, and rename results/phase7/gate12_2026-09-09 to results/phase7/gate12_2026-09-09_aborted. Never delete partial rows or manifest lines. Never backfill the missing runs with older data.

## 10. Verdict computation

Compute from rows.txt only, after all 10 runs complete with exit 0.

Per-pair ratios, candidate over baseline within each pair, for total_s and for p95_ms:
  pair 1.1 total ratio = total_s of 1.1-b divided by total_s of 1.1-a
  pair 1.2 total ratio = total_s of 1.2-a divided by total_s of 1.2-b
  pair 1.3 total ratio = total_s of 1.3-a divided by total_s of 1.3-b
  pair 1.4 total ratio = total_s of 1.4-b divided by total_s of 1.4-a
  pair 1.5 total ratio = total_s of 1.5-b divided by total_s of 1.5-a
  Same five assignments for p95_ms using the p95_ms fields.
Candidate total_s and p95_ms come from PROFILE_V3 rows; baseline total_s and p95_ms come from PROFILE_V2 rows. Parse total_s and p95_ms as printed.

Summaries: median of the five total ratios; median of the five p95 ratios; spread of total ratios as max minus min; spread of p95 ratios as max minus min. Medians only over the five within-pair ratios. Independently aggregated medians are never divided.

Advancement rule, all three conditions required:
  gate 1 median at most 1.02 AND gate 2 median at most 1.02 AND both spreads at most 0.04.
If any condition fails, verdict is NO-ADVANCE.

Write results/phase7/gate12_2026-09-09/verdict.md containing: all ten pair ratios (five total, five p95) with the per-pair inputs; both medians; both spreads; the raw per-run total_s and p95_ms values in execution order; the controls actually observed (governor, EPP, boost, CPU 7 pin, CPU 15 offline window, load samples per run, artifact hashes, HEAD); any anomalies (including any fast first block or load excursions); and the verdict word ADVANCE or NO-ADVANCE on its own line. Under the standing noise rule, a spread that can flip a 2 percent verdict blocks any pass claim, which the 0.04 spread bar enforces.

## 11. Machine record

Write docs/phase7/campaign_machine_record_2026-09-09.md as a new dated file. Do not modify docs/phase7/campaign_machine_record_2026-09-08.md. Include: HEAD and git status output; CPU model; kernel from uname -r; compiler version; CPU 7 pin statement; governor and EPP before and after values with the verification commands; boost value; CPU 15 offline window with UTC timestamps and frequency samples at block start and end; every load sample taken (pre-block quiet check plus per-run load1 and load5); ambient notes (desktop state, competing work statement); and the pre-warm procedure statement with timestamps confirming both binaries were pre-warmed with --moves 40 --iters 200 --telemetry off and outputs discarded.

## 12. Sanction verification: one complete rerun, no appended pairs

Quoted from the governing texts (ASCII-safe transcription, wording preserved):

From results/phase7/gate12_2026-09-08/verdict.md:
  Quote 1: Overall campaign result: NOT QUALIFIED at gate 1. No favorable pairs are appended and no rerun is taken on a failing number; the sanctioned one-time control-corrected rerun remains available to the owner if the background-load controls can be strengthened.
  This verifies: failing numbers are not reworked by appending favorable pairs, and exactly one control-corrected rerun is sanctioned.

From docs/phase7/session_report_2026-09-08_evening.md:
  Quote 2: No favorable pairs were appended; the sanctioned one-time control-corrected rerun remains available to the owner.
  This verifies: the first campaign appended nothing, and a single rerun sanction is still open.

From docs/phase7/qualification_protocol.md section 9, Noise rule:
  Quote 3: Gate verdicts report all five pair ratios. Measurement uncertainty that can flip a 2 percent verdict blocks any pass claim; the execution report carries per-gate spreads alongside medians.
  This verifies: the rerun verdict must report all five pair ratios with spreads, and spread can block a pass claim even when a median is under the bar. The section 10 spread bar of 0.04 implements this rule.

From docs/phase7/qualification_protocol.md section 3:
  Quote 4: Raw preservation: results/phase7/<campaign>/rows.txt holds one output row per run in execution order, plus MANIFEST.txt mapping each line to artifact path and hash, full command, telemetry mode, seed, started timestamp, exit status, and pair assignment. No aggregation happens during collection.
  This verifies: the rerun must be a fresh complete block with raw rows and manifest, no in-collection aggregation and no cherry-picked additions.

## Execution checklist (in order)

  1. Confirm section 1 preconditions and record them. If loadavg is at or above 1.5, run the 10-minute wait loop; if still loud, set status BLOCKED with reason machine-not-quiet and run nothing.
  2. Verify section 6 hashes.
  3. Take CPU 15 offline per section 3.
  4. Run section 2 pre-warm for both binaries, discard outputs.
  5. Run pairs 1.1 through 1.5 per sections 4 and 5, recording per-run load samples, manifest lines per section 7, and rows per section 8. Apply section 9 abort checks before each pair.
  6. Restore CPU 15 online per section 3.
  7. Compute verdict per section 10 and write verdict.md.
  8. Write the dated machine record per section 11.
