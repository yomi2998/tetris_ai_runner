# Session report, 2026-09-09: subagent handover of the sanctioned gate-1/2 rerun

## Scope

The owner instructed: hand over the remaining task to subagents, with the parent validating their work. The remaining authorized step was option 1 from the 2026-09-08 evening decision request: the one-time control-corrected rerun of the complete five-pair gate-1/2 block, followed by the full gates 3-11 campaign only if the rerun advances.

## Handover design

One async workflow with eight sequential stages, each child self-gating on status files under results/phase7/_pipeline/:

- Stage A (scout): write the rerun runbook from the protocol. Output RUNBOOK OK.
- Stage B (worker): execute the runbook exactly. Output RERUNRESULT line.
- Stage B2 (delegate): independent re-verification of the rerun. Output BCHECK line.
- Stages C1 to C4 (workers): full gates 3-11 campaign in segments, each gated on the prior stage's OK status.
- Stage D (delegate): independent pre-validation and per-gate verdict computation, advisory to the parent.

Children commit nothing, touch no tracked files, write only into their own results directories plus the dated machine record, and restore CPU 15 online in every path. Authority, commits, and validation stay with the parent.

One infrastructure failure occurred: stage B's first launch died on a provider rate limit (OpenAI 429, retryable). State was verified intact (stage A artifacts present, no timing runs started, tree clean, CPU 15 online, governor performance) and the pipeline was relaunched from stage B through the same protocol. No task work was lost or duplicated.

## Execution result: the rerun fails both gates

Block: 2026-09-08T16:44:12Z to 16:47:28Z, ten timed runs, all exit 0, quiet machine (load5 0.93 to 1.12), CPU 7 pinned with governor and EPP performance, boost 1, CPU 15 offline for the block, both binaries pre-warmed per the runbook (moves 40, iters 200, outputs discarded), frozen artifacts hash-verified.

Directory results/phase7/gate12_2026-09-09/ holds RUNBOOK.md, rows.txt (ten raw rows), MANIFEST.txt (ten lines with per-run loads), and verdict.md. Machine record: docs/phase7/campaign_machine_record_2026-09-09.md.

Per-pair total ratios (candidate over baseline): 1.04155, 1.05063, 1.02631, 1.05489, 1.01782.
Per-pair p95 ratios: 1.02547, 1.05264, 0.99687, 1.05064, 0.98772.

Gate 1 total median 1.04155 against bar 1.02: FAIL. Spread 0.03706.
Gate 2 p95 median 1.02547 against bar 1.02: FAIL. Spread 0.06492.

Verdict: NO-ADVANCE. The pre-warm removed the 2026-09-08 fast-first-block anomaly (pair 1.1 baseline ran inside the block range). Only one of five total pairs and two of five p95 pairs sit at or below 1.02, so the failure is not separable from noise by any reordering: the medians cannot reach 1.02. The residual gap is genuine, approximately 4.2 percent total and 2.5 percent p95, consistent with the standing 100-move measurement (1.0324 total, 1.0422 p95).

Per the owner's binding decision, on failure the pipeline halts rather than improvising. Stages C1 through C4 and D each read their precondition, wrote SKIPPED status files, and performed no timing work. No full campaign rows exist. The rerun sanction is spent; no pairs were appended and no pair was rerun.

## Parent validation

All child claims were validated independently before being treated as evidence:

- Manifest integrity: ten lines, line 1 to 10 in order, pair order matches the required ABBA+A sequence by artifact hash, all exits 0, controls with per-run loads on every line.
- Artifact verification: baseline, candidate, legacy comparator, and frozen parameter file all re-hashed to their frozen values; the candidate and baseline match the runbook's recorded hashes on every manifest line.
- Raw rows: engine tags in execution order confirm the pair sequence; the verdict's per-run table matches rows.txt exactly.
- Independent recomputation (python, parent-executed, not the children's scripts): total ratios 1.01782, 1.02631, 1.04155, 1.05063, 1.05489, median 1.04155; p95 ratios 0.98772, 0.99687, 1.02547, 1.05064, 1.05264, median 1.02547; spreads 0.03706 and 0.06492. Agreement with the worker, the delegate, and verdict.md to five decimals.
- Runbook audit: the verdict rule, abort policy, raw preservation, and four verbatim sanction quotes match the governing texts; one transcription typo (a missing space) was fixed before commit.
- Machine state after the pipeline: CPU 15 online, governor and EPP performance, boost 1, tracked tree clean.

## Audit state

tests/audit_phase7_campaign.py was extended so non-newest gate12 campaigns are reported honestly: gate12_2026-09-08 now appears as a superseded campaign while current statuses come from gate12_2026-09-09. Audit output: gate1=FAIL, gate2=FAIL from the rerun; gate3 PENDING (the full campaign correctly never ran); gates 4 to 6 and comparator-bound remain HISTORICAL from the superseded recampaign; gates 8 and 9 remain SUPPORTED from the recorded corpus evidence. No uncertainty error remains, because the failing medians cannot be flipped by any plausible noise; exit code 0. The 09-08 campaign's FAIL is preserved in git history and cited as superseded.

## Commit

5809409: rerun artifacts (RUNBOOK.md, rows.txt, MANIFEST.txt, verdict.md), pipeline status files, dated machine record, audit supersession update.

## State and decision space

The migration remains paused at qualification, exactly per the owner's binding decision. The compliant campaign and its sanctioned control-corrected rerun both fail gate 1; PGO attempt 1 is rejected on evidence. Remaining options, all requiring owner authorization:

1. Spend the second PGO training-corpus attempt (preserved; judged unlikely to flip direction since attempt 1 failed on the transformation class, not corpus shape).
2. A narrow PGO flag amendment (hot/cold partitioning, value profile indirect-call promotion) targeting the rejected transformation class; a protocol question for the owner since PGO scope was fixed before attempt 1.
3. A product-latency-justified gate revision; not permitted without an owner-declared product latency requirement.
4. Authorize a new, bounded source-optimization effort against the residual 4.2 percent total gap; the standing decision says pause rather than continue open-ended experiments, so this needs a fresh explicit authorization.
5. Stay paused.

No cutover, quality campaign, or legacy deletion proceeds without qualification. Frozen artifacts remain untouched and verified.
