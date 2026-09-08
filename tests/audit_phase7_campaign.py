#!/usr/bin/env python3
"""Audit the recorded phase-7 campaigns without running benchmarks.

This checker only reads manifests, rows, protocol text, the corpus report, the
gate-89 corpus evidence, the gate-3 partition bundle, and the current harness
source. It intentionally reports protocol gaps instead of silently
interpreting a relaxed or incomplete result as a pass.

Campaign supersession policy: historical campaigns that recorded superseded
freezes of an artifact path are reported as superseded evidence, never as
errors, as long as the recorded hash is a known historical freeze of that
artifact. Current gate statuses come from the newest gate12 campaign; results
from superseded candidates are reported with HISTORICAL- tokens so they never
pose as current verdicts. Gate 8/9 verdicts are reported as SUPPORTED only
when the recorded corpus evidence backs them.
"""

from __future__ import annotations

import argparse
import hashlib
import re
import statistics
import sys
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class Finding:
    severity: str
    code: str
    message: str


SUPERSEDED_FREEZE_PREFIXES = {
    "c4579a8707ef": "7.2B candidate freeze, superseded by the 7.2D resize freeze at the same path",
    "bf7b9f98": "7.2A candidate freeze, superseded",
    "7cadfed6": "7.2A comparator freeze, superseded",
}

GATE12_CAMPAIGN_PREFIX = "gate12"


def kv_tokens(tokens: list[str]) -> dict[str, str]:
    result: dict[str, str] = {}
    for token in tokens:
        if "=" in token:
            key, value = token.split("=", 1)
            result[key] = value
    return result


def parse_rows(manifest: Path, rows: Path) -> tuple[list[dict[str, str]], list[dict[str, str]]]:
    manifest_lines = manifest.read_text().splitlines()
    row_lines = rows.read_text().splitlines()
    manifests = [kv_tokens(line.split()) for line in manifest_lines]
    parsed_rows = []
    for line in row_lines:
        parts = line.split()
        parsed_rows.append({"_type": parts[0], **kv_tokens(parts[1:])})
    return manifests, parsed_rows


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def pair_id(pair: str) -> str:
    return re.sub(r"-(?:a|b)$", "", pair)


def number(value: str) -> float:
    return float(value)


def count(value: str) -> float:
    return float(value)


def median(values: list[float]) -> float:
    return statistics.median(values) if values else float("nan")


def supersession_note(expected: str) -> str | None:
    for prefix, note in SUPERSEDED_FREEZE_PREFIXES.items():
        if expected.startswith(prefix):
            return note
    return None


def add_manifest_findings(
    root: Path,
    campaign: str,
    findings: list[Finding],
) -> tuple[list[dict[str, str]], list[dict[str, str]]]:
    directory = root / "results" / "phase7" / campaign
    manifest_path = directory / "MANIFEST.txt"
    rows_path = directory / "rows.txt"
    if not manifest_path.exists() or not rows_path.exists():
        findings.append(Finding("ERROR", "missing-campaign", f"missing {campaign} manifest or rows"))
        return [], []

    manifests, rows = parse_rows(manifest_path, rows_path)
    if len(manifests) != len(rows):
        findings.append(Finding(
            "ERROR", "line-count", f"{campaign}: manifest has {len(manifests)} lines, rows has {len(rows)}"
        ))
    for index, record in enumerate(manifests, 1):
        if record.get("line") != str(index):
            findings.append(Finding(
                "ERROR", "line-number", f"{campaign}: manifest line {index} has line={record.get('line')}"
            ))
        artifact = record.get("artifact")
        expected = record.get("sha256")
        if not artifact or not expected:
            findings.append(Finding("ERROR", "manifest-fields", f"{campaign}: line {index} lacks artifact/hash"))
            continue
        artifact_path = Path(artifact)
        if not artifact_path.exists():
            note = supersession_note(expected)
            if note is not None:
                findings.append(Finding(
                    "INFO", "superseded-artifact",
                    f"{campaign}: line {index} records a superseded freeze that no longer exists on disk: {note}"
                ))
                continue
            findings.append(Finding("ERROR", "missing-artifact", f"{campaign}: line {index} artifact is absent: {artifact}"))
            continue
        actual = sha256(artifact_path)
        if actual != expected:
            note = supersession_note(expected)
            if note is not None:
                findings.append(Finding(
                    "INFO", "superseded-artifact",
                    f"{campaign}: line {index} records {expected[:12]}, a known superseded freeze ({note}); "
                    f"current artifact at that path is {actual[:12]}"
                ))
            else:
                findings.append(Finding(
                    "ERROR", "artifact-hash", f"{campaign}: line {index} expects {expected[:12]} but current artifact is {actual[:12]}"
                ))
    return manifests, rows


def discover_gate12_campaigns(root: Path) -> list[str]:
    base = root / "results" / "phase7"
    if not base.is_dir():
        return []
    return sorted(
        entry.name for entry in base.iterdir()
        if entry.is_dir() and entry.name.startswith(GATE12_CAMPAIGN_PREFIX)
    )


def grouped(
    manifests: list[dict[str, str]], rows: list[dict[str, str]], gate: str, seed: str
) -> dict[str, dict[str, dict[str, str]]]:
    result: dict[str, dict[str, dict[str, str]]] = defaultdict(dict)
    for manifest, row in zip(manifests, rows):
        if manifest.get("gate") != gate or row.get("seed") != seed:
            continue
        result[pair_id(manifest.get("pair", ""))][row["_type"]] = row
    return result


def pair_ratios(
    groups: dict[str, dict[str, dict[str, str]]], numerator: str, denominator: str, field: str
) -> list[float]:
    values = []
    for group in sorted(groups):
        rows = groups[group]
        if numerator in rows and denominator in rows:
            values.append(number(rows[numerator][field]) / number(rows[denominator][field]))
    return values


def rate_pair_ratios(
    groups: dict[str, dict[str, dict[str, str]]], field: str
) -> list[float]:
    values = []
    for group in sorted(groups):
        rows = groups[group]
        if "PROFILE_V3" not in rows or "PROFILE_CMP" not in rows:
            continue
        candidate = rows["PROFILE_V3"]
        comparator = rows["PROFILE_CMP"]
        values.append(
            (count(candidate[field]) / number(candidate["total_s"]))
            / (count(comparator[field]) / number(comparator["total_s"]))
        )
    return values


def gate12_status(
    manifests: list[dict[str, str]],
    rows: list[dict[str, str]],
    findings: list[Finding],
    source: str,
) -> dict[str, str]:
    statuses: dict[str, str] = {}
    fixed = grouped(manifests, rows, "gates1-2", "1")
    total_ratios = pair_ratios(fixed, "PROFILE_V3", "PROFILE_V2", "total_s")
    p95_ratios = pair_ratios(fixed, "PROFILE_V3", "PROFILE_V2", "p95_ms")
    if not total_ratios or not p95_ratios:
        statuses["gate1"] = "PENDING"
        statuses["gate2"] = "PENDING"
        findings.append(Finding(
            "INFO", "gate12-pending", f"{source}: no seed-1 fixed-work gate-1/2 rows yet"
        ))
        return statuses
    total = median(total_ratios)
    p95 = median(p95_ratios)
    statuses["gate1"] = "PASS" if total <= 1.02 else "FAIL"
    statuses["gate2"] = "PASS" if p95 <= 1.02 else "FAIL"
    findings.append(Finding(
        "INFO", "gate1",
        f"{source}: seed-1 total pair ratios={','.join(f'{v:.5f}' for v in total_ratios)} median={total:.5f} ({statuses['gate1']})"
    ))
    findings.append(Finding(
        "INFO", "gate2",
        f"{source}: seed-1 p95 pair ratios={','.join(f'{v:.5f}' for v in p95_ratios)} median={p95:.5f} ({statuses['gate2']})"
    ))
    if statuses["gate1"] == "PASS" or statuses["gate2"] == "PASS":
        spreads_total = max(total_ratios) - min(total_ratios)
        spreads_p95 = max(p95_ratios) - min(p95_ratios)
        if spreads_total > 0.04 or spreads_p95 > 0.04:
            findings.append(Finding(
                "ERROR", "gate12-uncertainty",
                f"{source}: pair spread (total {spreads_total:.5f}, p95 {spreads_p95:.5f}) can flip a 2 percent verdict; "
                "controls must be corrected and the complete five-pair block rerun once"
            ))
    return statuses


def audit_gate3(
    manifests: list[dict[str, str]],
    rows: list[dict[str, str]],
    findings: list[Finding],
    source: str,
) -> str | None:
    count_groups = grouped(manifests, rows, "gate3", "1")
    candidate_rows = [v["PROFILE_V3"] for v in count_groups.values() if "PROFILE_V3" in v]
    comparator_rows = [v["PROFILE_CMP"] for v in count_groups.values() if "PROFILE_CMP" in v]
    if not candidate_rows or not comparator_rows:
        return None
    for label, twins, fields in (
        ("candidate", candidate_rows, ("unique_candidates", "transitions")),
        ("comparator", comparator_rows, ("unique_candidates", "transitions")),
    ):
        if len(twins) != 2:
            findings.append(Finding(
                "ERROR", "gate3-twins", f"{source}: expected two deterministic {label} twins, found {len(twins)}"
            ))
        elif any(twins[0][field] != twins[1][field] for field in fields):
            findings.append(Finding(
                "ERROR", "gate3-twins", f"{source}: {label} twins disagree on counts: "
                + ", ".join(f"{field}={twins[0][field]}/{twins[1][field]}" for field in fields)
            ))
    if len(candidate_rows) < 2 or len(comparator_rows) < 2:
        return None
    candidate_unique = count(candidate_rows[0]["unique_candidates"])
    comparator_unique = count(comparator_rows[0]["unique_candidates"])
    candidate_transitions = count(candidate_rows[0]["transitions"])
    comparator_transitions = count(comparator_rows[0]["transitions"])
    unique_delta = (candidate_unique - comparator_unique) / comparator_unique
    transition_delta = (candidate_transitions - comparator_transitions) / comparator_transitions
    if abs(unique_delta) <= 0.02 and abs(transition_delta) <= 0.02:
        findings.append(Finding(
            "INFO", "gate3",
            f"{source}: unique={unique_delta:+.5f}, transitions={transition_delta:+.5f} (PASS)"
        ))
        return "PASS"
    findings.append(Finding(
        "INFO", "gate3",
        f"{source}: unique={unique_delta:+.5f}, transitions={transition_delta:+.5f} exceeds 2 percent, partition evidence required"
    ))
    return "OVER-2-PERCENT"


def audit_partition_bundle(root: Path, findings: list[Finding]) -> bool:
    directory = root / "results" / "phase7" / "partition"
    manifest = directory / "MANIFEST.txt"
    if not manifest.exists():
        findings.append(Finding("ERROR", "partition-bundle", "gate-3 partition bundle manifest is missing"))
        return False
    complete = True
    for seed in (1, 2, 3):
        class_table = directory / f"seed{seed}.class_table.tsv"
        defects = directory / f"seed{seed}.defects.tsv"
        if not class_table.exists() or not defects.exists():
            findings.append(Finding("ERROR", "partition-bundle", f"seed {seed} class table or defects file is missing"))
            complete = False
            continue
        table: dict[tuple[str, str], float] = {}
        lines = class_table.read_text().splitlines()
        for line in lines[1:]:
            parts = line.split("\t")
            if len(parts) == 3:
                table[(parts[0], parts[1])] = float(parts[2])
        defect_rows = defects.read_text().splitlines()
        if len(defect_rows) > 1:
            findings.append(Finding("ERROR", "partition-bundle", f"seed {seed} records {len(defect_rows) - 1} defect rows"))
            complete = False
        for flag in ("exact_S", "exact_N", "exact_R", "exact_T"):
            if table.get(("uniques", flag), table.get(("trans", flag), 0.0)) != 1.0:
                findings.append(Finding("ERROR", "partition-bundle", f"seed {seed} is missing exact flag {flag}"))
                complete = False
        for defect_class in ("dV", "dL", "dV_om", "dVt", "dLt"):
            legs = [leg for leg in ("uniques", "trans") if (leg, defect_class) in table]
            if any(table[(leg, defect_class)] != 0.0 for leg in legs):
                findings.append(Finding("ERROR", "partition-bundle", f"seed {seed} classifies nonzero defects in {defect_class}"))
                complete = False
        for volume_metric in ("ood_inputs", "ood_nV", "missing_legacy", "overflow_inputs", "divergent_inputs"):
            if ("volume", volume_metric) in table and table[("volume", volume_metric)] != 0.0:
                findings.append(Finding("ERROR", "partition-bundle", f"seed {seed} volume check {volume_metric} is nonzero"))
                complete = False
        if ("uniques", "delta") in table:
            b_total = sum(table.get(("uniques", name), 0.0) for name in ("b_counted", "b_spin_split", "b_opaque", "b_collapsed"))
            if table[("uniques", "a")] - b_total != table[("uniques", "delta")]:
                findings.append(Finding("ERROR", "partition-bundle", f"seed {seed} uniques leg classes do not close on delta"))
                complete = False
        if ("trans", "deltaC") in table:
            if table[("trans", "a_t")] - table[("trans", "b_t")] != table[("trans", "deltaC")]:
                findings.append(Finding("ERROR", "partition-bundle", f"seed {seed} trans leg classes do not close on deltaC"))
                complete = False
    return complete


def audit_gate45(
    manifests: list[dict[str, str]],
    rows: list[dict[str, str]],
    findings: list[Finding],
    source: str,
) -> dict[str, str]:
    statuses: dict[str, str] = {}
    component_groups = grouped(manifests, rows, "gates4-5", "1")
    if not component_groups:
        return statuses
    component_specs = {
        "enumeration": lambda v, c: (number(v["enum_ns"]) / count(v["searches"])) / (number(c["search_ns"]) / count(c["searches"])),
        "unique": lambda v, c: (number(v["rule_ns"]) / count(v["unique_candidates"])) / ((number(c["eval_hit_ns"]) + number(c["eval_miss_ns"]) + number(c["transition_ns"])) / count(c["unique_candidates"])),
        "eval-hit": lambda v, c: (number(v["eval_hit_ns"]) / (count(v["eval_memo_hits"]) + count(v["cache_hits"]))) / (number(c["eval_hit_ns"]) / count(c["eval_hits"])),
        "eval-miss": lambda v, c: (number(v["eval_miss_ns"]) / count(v["eval_computed"])) / (number(c["eval_miss_ns"]) / count(c["eval_calls"])),
        "transition": lambda v, c: (number(v["policy_ns"]) / count(v["transitions"])) / (number(c["transition_ns"]) / count(c["transitions"])),
        "materialized": lambda v, c: (number(v["materialize_ns"]) / count(v["materialized_nodes"])) / (number(c["alloc_ns"]) / count(c["materialized_nodes"])),
        "path": lambda v, c: ((number(v["path_find_ns"]) + number(v["path_replay_ns"])) / count(v["path_states"])) / (number(c["path_ms"]) * 1e6 / count(c["path_states"])),
    }
    component_medians = {}
    for name, formula in component_specs.items():
        values = []
        for records in component_groups.values():
            if "PROFILE_V3" in records and "PROFILE_CMP" in records:
                values.append(formula(records["PROFILE_V3"], records["PROFILE_CMP"]))
        component_medians[name] = median(values)
    failed_components = [name for name, value in component_medians.items() if value > 1.02]
    statuses["gate4"] = "FAIL" if failed_components else "PASS"
    findings.append(Finding(
        "INFO", "gate4", f"{source}: component legs over 1.02: {','.join(failed_components) or 'none'} ({statuses['gate4']})"
    ))

    path_values = []
    for records in component_groups.values():
        if "PROFILE_V3" in records:
            row = records["PROFILE_V3"]
            path_values.append(number(row["path_ms"]) / sum(number(row[field]) for field in ("setup_ms", "run_ms", "path_ms", "apply_ms")))
    path_max = max(path_values, default=float("nan"))
    statuses["gate5"] = "PASS" if path_max <= 0.02 else "FAIL"
    findings.append(Finding("INFO", "gate5", f"{source}: seed-1 path max={path_max:.8f} ({statuses['gate5']})"))
    return statuses


def audit_gate6(
    manifests: list[dict[str, str]],
    rows: list[dict[str, str]],
    findings: list[Finding],
    source: str,
    partitioned: bool,
) -> str | None:
    timed = grouped(manifests, rows, "gate6", "1")
    if not timed:
        return None
    timed_medians = {field: median(rate_pair_ratios(timed, field)) for field in ("widening_iters", "parents", "transitions")}
    verdict = "PASS" if all(v >= 0.98 for v in timed_medians.values()) else "FAIL"
    findings.append(Finding("INFO", "gate6", f"{source}: timed ratios={timed_medians} ({verdict})"))
    if not partitioned:
        findings.append(Finding(
            "ERROR", "gate6-prerequisite", f"{source}: gate 6 is diagnostic only because protocol gate 3 did not partition"
        ))
    return verdict


def audit_comparator_bound(
    manifests: list[dict[str, str]],
    rows: list[dict[str, str]],
    findings: list[Finding],
    source: str,
) -> str | None:
    bound = grouped(manifests, rows, "bound-cmp-frozen", "1")
    bound_values = pair_ratios(bound, "PROFILE_CMP", "PROFILE_V2", "total_s")
    if not bound_values:
        return None
    bound_median = median(bound_values)
    verdict = "PASS" if bound_median <= 1.02 else "FAIL"
    findings.append(Finding(
        "INFO", "comparator-bound", f"{source}: comparator/frozen median={bound_median:.5f} ({verdict})"
    ))
    return verdict


def audit_gate89_evidence(root: Path, findings: list[Finding]) -> dict[str, str]:
    statuses = {"gate8": "UNPROVEN", "gate9": "UNPROVEN"}
    directory = root / "results" / "phase7" / "gate89"
    manifest = directory / "MANIFEST.txt"
    corpus = directory / "gate89_corpus.txt"
    summary = directory / "summary.md"
    if not manifest.exists() or not corpus.exists() or not summary.exists():
        findings.append(Finding(
            "ERROR", "gate89-evidence", "gate 8/9 corpus evidence bundle is incomplete; verdicts stay UNPROVEN"
        ))
        return statuses

    corpus_text = corpus.read_text()
    gate8_row = next(
        (line for line in corpus_text.splitlines()
         if "Raw non-T enumeration versus frozen legacy search" in line),
        None,
    )
    gate9_row = next(
        (line for line in corpus_text.splitlines()
         if "T per normalized candidate versus frozen legacy search" in line),
        None,
    )
    if gate8_row is None or gate9_row is None:
        findings.append(Finding(
            "ERROR", "gate89-evidence", "gate89_corpus.txt lacks the harness-printed gate-8/9 verdict rows"
        ))
        return statuses
    if not gate8_row.rstrip().endswith("PASS |") or not gate9_row.rstrip().endswith("PASS |"):
        findings.append(Finding(
            "ERROR", "gate89-evidence", "recorded harness verdicts for gate 8 or gate 9 are not PASS"
        ))
        return statuses

    manifest_text = manifest.read_text()
    root_match = re.search(r"HEAD: ([0-9a-f]{40})", manifest_text)
    binary_hashes = re.findall(r"^  sha256 ([0-9a-f]{64})", manifest_text, re.MULTILINE)
    findings.append(Finding(
        "INFO", "gate89-evidence",
        f"gate 8/9 verdicts are recorded by the harness at root={root_match.group(1)[:12] if root_match else 'unrecorded'} "
        f"with {len(binary_hashes)} hashed binaries; summary.md states verdicts are quoted, coordinator decides"
    ))
    for digest in binary_hashes:
        recorded = digest[:12]
        found = None
        for path in (root / "out" / "build").rglob("*"):
            if path.is_file() and path.stat().st_size > 1_000_000:
                try:
                    if sha256(path) == digest:
                        found = path
                        break
                except OSError:
                    continue
        if found is not None:
            findings.append(Finding(
                "INFO", "gate89-binary", f"recorded binary {recorded} still present at {found}"
            ))
        else:
            findings.append(Finding(
                "INFO", "gate89-binary",
                f"recorded binary {recorded} is no longer present in the build tree; the evidence bytes live in the gate89 manifest"
            ))
    statuses["gate8"] = "SUPPORTED"
    statuses["gate9"] = "SUPPORTED"
    return statuses


def audit_corpus_and_harness(root: Path, findings: list[Finding]) -> None:
    protocol = (root / "docs" / "phase7" / "qualification_protocol.md").read_text()
    harness = (root / "tests" / "run_perf_gate.py").read_text()
    corpus_path = root / "results" / "phase7" / "corpus_gate.txt"
    corpus = corpus_path.read_text() if corpus_path.exists() else ""
    if "at most 1.00 (non-T)" not in protocol:
        findings.append(Finding("ERROR", "protocol-item8", "protocol item 8 no longer states the 1.00 non-T limit"))
    if "at most 1.02 per normalized candidate" not in protocol:
        findings.append(Finding("ERROR", "protocol-item9", "protocol item 9 no longer states the 1.02 normalized-candidate limit"))

    comparator_ids = sorted(set(re.findall(
        r"\b(?:arrival_candidates|raw_bench_current|raw_bench_frozen(?:_180fix)?|reference_a_frozen)\b",
        harness)))
    findings.append(Finding("INFO", "harness-comparators", f"run_perf_gate.py pairs: {', '.join(comparator_ids) or 'none'}"))
    if "legacy" in harness:
        findings.append(Finding(
            "INFO", "harness-legacy",
            "run_perf_gate.py contains a legacy corpus comparator sequence; the gate-8/9 status is decided by the recorded evidence"
        ))
    else:
        findings.append(Finding(
            "ERROR", "gate8-unproven",
            "Gate 8 is UNPROVEN: no frozen legacy placement-enumeration comparator exists in run_perf_gate.py"
        ))
        findings.append(Finding(
            "ERROR", "gate9-unproven",
            "Gate 9 is UNPROVEN: the T leg compares arrival_candidates against reference_a_frozen only; no legacy search_tspin comparator exists"
        ))
    if corpus:
        findings.append(Finding(
            "INFO", "corpus-evidence-scope",
            "corpus_gate.txt proves the Phase-2 kernel regression and the Reference A >= 2x T gate; the legacy comparison lives in results/phase7/gate89/"
        ))


def audit(root: Path) -> tuple[int, list[Finding], dict[str, str]]:
    findings: list[Finding] = []
    statuses: dict[str, str] = {}

    campaign_manifests, campaign_rows = add_manifest_findings(root, "campaign", findings)
    recampaign_manifests, recampaign_rows = add_manifest_findings(root, "recampaign", findings)
    if campaign_manifests and campaign_rows:
        old_hash_errors = [f for f in findings if f.code == "artifact-hash" and f.message.startswith("campaign:")]
        findings.append(Finding("ERROR" if old_hash_errors else "INFO", "historical-7.2c", f"7.2C artifact hash mismatches={len(old_hash_errors)}"))
        if not old_hash_errors:
            findings.append(Finding(
                "INFO", "historical-7.2c",
                "the 7.2C campaign is preserved history; its candidate freeze was superseded and its verdicts are not current claims"
            ))

    gate12_names = discover_gate12_campaigns(root)
    if gate12_names:
        primary = gate12_names[-1]
        findings.append(Finding(
            "INFO", "campaign-source", f"current gate-1/2 statuses come from {primary}"
        ))
        primary_manifests, primary_rows = add_manifest_findings(root, primary, findings)
    else:
        primary = None
        primary_manifests, primary_rows = [], []
        findings.append(Finding(
            "INFO", "campaign-source",
            "no gate12 campaign exists yet; gate-1/2 statuses fall back to the superseded recampaign as HISTORICAL"
        ))

    if primary is not None:
        statuses.update(gate12_status(primary_manifests, primary_rows, findings, primary))

    recampaign_gate12: dict[str, str] = {}
    if recampaign_manifests and recampaign_rows:
        recampaign_gate12 = gate12_status(recampaign_manifests, recampaign_rows, findings, "recampaign")
        for key, value in recampaign_gate12.items():
            if value == "PENDING":
                continue
            if statuses.get(key) in (None, "PENDING"):
                statuses[key] = f"HISTORICAL-{value}"

    partition_ok = audit_partition_bundle(root, findings)
    if partition_ok:
        findings.append(Finding(
            "INFO", "partition-bundle",
            "the gate-3 partition bundle validates: exact flags set, defect classes zero, classes close on the deltas, defects files empty"
        ))

    gate3_primary = None
    if primary is not None:
        gate3_primary = audit_gate3(primary_manifests, primary_rows, findings, primary)
    if gate3_primary == "OVER-2-PERCENT":
        statuses["gate3"] = "PARTITIONED" if partition_ok else "BLOCKED"
        if not partition_ok:
            findings.append(Finding(
                "ERROR", "gate3-unclassified",
                f"{primary}: gate 3 exceeds the 2 percent limit and the partition bundle does not validate"
            ))
    elif gate3_primary == "PASS":
        statuses["gate3"] = "PASS"
    elif primary is not None and gate3_primary is None:
        statuses["gate3"] = "PENDING"
        findings.append(Finding(
            "INFO", "gate3",
            f"{primary} has no gate-3 rows yet; the historical partition evidence is cited above"
        ))
    else:
        recampaign_gate3 = audit_gate3(recampaign_manifests, recampaign_rows, findings, "recampaign")
        if recampaign_gate3 == "OVER-2-PERCENT":
            statuses["gate3"] = "HISTORICAL-PARTITIONED" if partition_ok else "HISTORICAL-BLOCKED"
            if not partition_ok:
                findings.append(Finding(
                    "ERROR", "gate3-unclassified",
                    "recampaign gate 3 exceeded 2 percent and the partition bundle does not validate"
                ))
            else:
                findings.append(Finding(
                    "INFO", "gate3",
                    "the recampaign gate-3 delta exceeds 2 percent and the partition bundle classifies it fully; "
                    "the new campaign will re-measure gate 3 on the current candidate"
                ))
        elif recampaign_gate3 == "PASS":
            statuses["gate3"] = "HISTORICAL-PASS"
        else:
            statuses["gate3"] = "PENDING"

    gate45_primary = (
        audit_gate45(primary_manifests, primary_rows, findings, primary)
        if primary is not None else {}
    )
    gate45_recampaign = audit_gate45(recampaign_manifests, recampaign_rows, findings, "recampaign")
    for key in ("gate4", "gate5"):
        if gate45_primary.get(key):
            statuses[key] = gate45_primary[key]
        elif gate45_recampaign.get(key):
            statuses[key] = f"HISTORICAL-{gate45_recampaign[key]}"
        else:
            statuses[key] = "PENDING"

    partitioned_for_gate6 = partition_ok or statuses.get("gate3") in ("PASS", "PARTITIONED")
    gate6_primary = (
        audit_gate6(primary_manifests, primary_rows, findings, primary, partitioned_for_gate6)
        if primary is not None else None
    )
    gate6_recampaign = (
        audit_gate6(recampaign_manifests, recampaign_rows, findings, "recampaign", partition_ok)
        if recampaign_manifests and recampaign_rows else None
    )
    if gate6_primary is not None:
        statuses["gate6"] = gate6_primary
    elif gate6_recampaign is not None:
        statuses["gate6"] = f"HISTORICAL-{gate6_recampaign}"
    else:
        statuses["gate6"] = "PENDING"

    bound_primary = (
        audit_comparator_bound(primary_manifests, primary_rows, findings, primary)
        if primary is not None else None
    )
    bound_recampaign = (
        audit_comparator_bound(recampaign_manifests, recampaign_rows, findings, "recampaign")
        if recampaign_manifests and recampaign_rows else None
    )
    if bound_primary is not None:
        statuses["comparator-bound"] = bound_primary
    elif bound_recampaign is not None:
        statuses["comparator-bound"] = f"HISTORICAL-{bound_recampaign}"
    else:
        statuses["comparator-bound"] = "PENDING"

    statuses.update(audit_gate89_evidence(root, findings))
    audit_corpus_and_harness(root, findings)
    exit_code = 1 if any(f.severity == "ERROR" for f in findings) else 0
    return exit_code, findings, statuses


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args(argv)
    exit_code, findings, statuses = audit(args.repo_root.resolve())
    for key in sorted(statuses):
        print(f"STATUS {key}={statuses[key]}")
    for finding in findings:
        print(f"{finding.severity} {finding.code}: {finding.message}")
    return exit_code


if __name__ == "__main__":
    sys.exit(main())
