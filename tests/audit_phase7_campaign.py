#!/usr/bin/env python3
"""Audit the recorded phase-7 campaign without running benchmarks.

This checker only reads manifests, rows, protocol text, the corpus report, and
the current harness source.  It intentionally reports protocol gaps instead of
silently interpreting a relaxed or incomplete result as a pass.
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
            findings.append(Finding("ERROR", "missing-artifact", f"{campaign}: line {index} artifact is absent: {artifact}"))
            continue
        actual = sha256(artifact_path)
        if actual != expected:
            findings.append(Finding(
                "ERROR", "artifact-hash", f"{campaign}: line {index} expects {expected[:12]} but current artifact is {actual[:12]}"
            ))
    return manifests, rows


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


def median(values: list[float]) -> float:
    return statistics.median(values) if values else float("nan")


def audit_campaign_numbers(
    manifests: list[dict[str, str]], rows: list[dict[str, str]], findings: list[Finding]
) -> dict[str, str]:
    statuses: dict[str, str] = {}
    if not manifests or not rows:
        return statuses

    fixed = grouped(manifests, rows, "gates1-2", "1")
    total = median(pair_ratios(fixed, "PROFILE_V3", "PROFILE_V2", "total_s"))
    p95 = median(pair_ratios(fixed, "PROFILE_V3", "PROFILE_V2", "p95_ms"))
    statuses["gate1"] = "PASS" if total <= 1.02 else "FAIL"
    statuses["gate2"] = "PASS" if p95 <= 1.02 else "FAIL"
    findings.append(Finding("INFO", "gate1", f"7.2E seed-1 total median={total:.5f} ({statuses['gate1']})"))
    findings.append(Finding("INFO", "gate2", f"7.2E seed-1 p95 median={p95:.5f} ({statuses['gate2']})"))

    count_groups = grouped(manifests, rows, "gate3", "1")
    candidate_rows = [v["PROFILE_V3"] for v in count_groups.values() if "PROFILE_V3" in v]
    comparator_rows = [v["PROFILE_CMP"] for v in count_groups.values() if "PROFILE_CMP" in v]
    if candidate_rows and comparator_rows:
        for label, twins, fields in (
            ("candidate", candidate_rows, ("unique_candidates", "transitions")),
            ("comparator", comparator_rows, ("unique_candidates", "transitions")),
        ):
            if len(twins) != 2:
                findings.append(Finding(
                    "ERROR", "gate3-twins", f"expected two deterministic {label} twins, found {len(twins)}"
                ))
            elif any(twins[0][field] != twins[1][field] for field in fields):
                findings.append(Finding(
                    "ERROR", "gate3-twins", f"{label} twins disagree on counts: "
                    + ", ".join(f"{field}={twins[0][field]}/{twins[1][field]}" for field in fields)
                ))
        candidate_unique = count(candidate_rows[0]["unique_candidates"])
        comparator_unique = count(comparator_rows[0]["unique_candidates"])
        candidate_transitions = count(candidate_rows[0]["transitions"])
        comparator_transitions = count(comparator_rows[0]["transitions"])
        unique_delta = (candidate_unique - comparator_unique) / comparator_unique
        transition_delta = (candidate_transitions - comparator_transitions) / comparator_transitions
        statuses["gate3"] = "BLOCKED" if abs(unique_delta) > 0.02 or abs(transition_delta) > 0.02 else "PASS"
        findings.append(Finding(
            "INFO", "gate3", f"7.2E seed-1 unique={unique_delta:+.5f}, transitions={transition_delta:+.5f} ({statuses['gate3']})"
        ))
        if statuses["gate3"] == "BLOCKED":
            findings.append(Finding(
                "ERROR", "gate3-unclassified", "Gate 3 exceeds the 2% limit and has no 100%-accounted partition evidence"
            ))

    component_groups = grouped(manifests, rows, "gates4-5", "1")
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
    findings.append(Finding("INFO", "gate4", f"7.2E component legs over 1.02: {','.join(failed_components) or 'none'} ({statuses['gate4']})"))

    path_values = []
    for records in component_groups.values():
        if "PROFILE_V3" in records:
            row = records["PROFILE_V3"]
            path_values.append(number(row["path_ms"]) / sum(number(row[field]) for field in ("setup_ms", "run_ms", "path_ms", "apply_ms")))
    path_max = max(path_values, default=float("nan"))
    statuses["gate5"] = "PASS" if path_max <= 0.02 else "FAIL"
    findings.append(Finding("INFO", "gate5", f"7.2E seed-1 path max={path_max:.8f} ({statuses['gate5']})"))

    timed = grouped(manifests, rows, "gate6", "1")
    timed_medians = {field: median(rate_pair_ratios(timed, field)) for field in ("widening_iters", "parents", "transitions")}
    statuses["gate6"] = "BLOCKED" if statuses.get("gate3") == "BLOCKED" else ("PASS" if all(v >= 0.98 for v in timed_medians.values()) else "FAIL")
    findings.append(Finding("INFO", "gate6", f"7.2E timed ratios={timed_medians} ({statuses['gate6']})"))
    if statuses["gate6"] == "BLOCKED":
        findings.append(Finding("ERROR", "gate6-prerequisite", "Gate 6 is diagnostic only because protocol Gate 3 did not partition"))

    bound = grouped(manifests, rows, "bound-cmp-frozen", "1")
    bound_median = median(pair_ratios(bound, "PROFILE_CMP", "PROFILE_V2", "total_s"))
    statuses["comparator-bound"] = "PASS" if bound_median <= 1.02 else "FAIL"
    findings.append(Finding("INFO", "comparator-bound", f"7.2E comparator/frozen median={bound_median:.5f} ({statuses['comparator-bound']})"))
    return statuses


def audit_corpus_and_harness(root: Path, findings: list[Finding]) -> dict[str, str]:
    protocol = (root / "docs" / "phase7" / "qualification_protocol.md").read_text()
    harness = (root / "tests" / "run_perf_gate.py").read_text()
    corpus_path = root / "results" / "phase7" / "corpus_gate.txt"
    corpus = corpus_path.read_text() if corpus_path.exists() else ""
    statuses = {"gate8": "UNPROVEN", "gate9": "UNPROVEN"}
    if "at most 1.00 (non-T)" not in protocol:
        findings.append(Finding("ERROR", "protocol-item8", "protocol item 8 no longer states the 1.00 non-T limit"))
    if "at most 1.02 per normalized candidate" not in protocol:
        findings.append(Finding("ERROR", "protocol-item9", "protocol item 9 no longer states the 1.02 normalized-candidate limit"))

    # Structural comparator inventory: which binaries does the harness actually
    # pair? These identifiers are the harness's own target names, not prose.
    comparator_ids = sorted(set(re.findall(
        r"\b(?:arrival_candidates|raw_bench_current|raw_bench_frozen(?:_180fix)?|reference_a_frozen)\b",
        harness)))
    findings.append(Finding("INFO", "harness-comparators", f"run_perf_gate.py pairs: {', '.join(comparator_ids) or 'none'}"))
    phase2_bar = "1.020" in harness
    if phase2_bar:
        findings.append(Finding(
            "INFO", "harness-item8-phase2",
            "run_perf_gate.py enforces the Phase-2 kernel-regression bar (every non-T piece <= 1.020 versus the frozen kernel), not a legacy comparison"
        ))
    has_legacy_comparator = "legacy" in harness
    if not has_legacy_comparator:
        findings.append(Finding(
            "ERROR", "gate8-unproven",
            "Gate 8 is UNPROVEN: no frozen legacy placement-enumeration comparator exists in run_perf_gate.py; the raw leg compares the current kernel against frozen kernel builds"
        ))
        findings.append(Finding(
            "ERROR", "gate9-unproven",
            "Gate 9 is UNPROVEN: the T leg compares arrival_candidates against reference_a_frozen only; the <= 1.02 per-normalized-candidate comparison against frozen legacy search_tspin has no comparator"
        ))
    if corpus:
        findings.append(Finding(
            "INFO", "corpus-evidence-scope",
            "corpus_gate.txt proves the Phase-2 kernel regression and the Reference A >= 2x T gate; it contains no legacy placement-enumeration timings"
        ))
    return statuses


def audit(root: Path) -> tuple[int, list[Finding], dict[str, str]]:
    findings: list[Finding] = []
    statuses: dict[str, str] = {}
    campaign_manifests, campaign_rows = add_manifest_findings(root, "campaign", findings)
    recampaign_manifests, recampaign_rows = add_manifest_findings(root, "recampaign", findings)
    statuses.update(audit_campaign_numbers(recampaign_manifests, recampaign_rows, findings))
    statuses.update(audit_corpus_and_harness(root, findings))
    if campaign_manifests and campaign_rows:
        old_hash_errors = [f for f in findings if f.code == "artifact-hash" and f.message.startswith("campaign:")]
        findings.append(Finding("ERROR" if old_hash_errors else "INFO", "historical-7.2c", f"7.2C artifact hash mismatches={len(old_hash_errors)}"))
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
