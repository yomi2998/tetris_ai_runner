#!/usr/bin/env python3
import hashlib
import os
import platform
import statistics
import subprocess
import sys

BUILD = sys.argv[1] if len(sys.argv) > 1 else "out/build/linux-gcc-self-release"
CORE = os.environ.get("PERF_GATE_CORE", "2")
REPS = int(os.environ.get("PERF_GATE_REPS", "5"))
ITERS_T = os.environ.get("PERF_GATE_ITERS_T", "60")
ITERS_RAW = os.environ.get("PERF_GATE_ITERS_RAW", "600")
WARMUP = os.environ.get("PERF_GATE_WARMUP", "10")

PIECES = "TZSJLOI"


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def shell(command):
    return subprocess.run(command, shell=True, capture_output=True, text=True).stdout.strip()


def run(binary, extra, iters):
    command = ["taskset", "-c", CORE, os.path.join(BUILD, binary),
        "--reps", str(iters), "--warmup", WARMUP] + extra
    out = subprocess.run(command, capture_output=True, text=True, check=True)
    samples = {piece: [] for piece in PIECES}
    corpus = ""
    for line in out.stdout.splitlines():
        parts = line.split()
        if parts and parts[0] == "REP" and len(parts) >= 5:
            samples[parts[2]].append((float(parts[4]), 0.0))
        elif parts and parts[0] == "RAWREP" and len(parts) >= 6:
            samples[parts[2]].append((float(parts[4]), float(parts[5])))
        elif parts and parts[0] in ("CORPUS", "RAWCORPUS"):
            corpus = line
    totals = {}
    for piece, pairs in samples.items():
        totals[piece] = (statistics.mean(t for t, e in pairs), statistics.mean(e for t, e in pairs))
    return totals, corpus


def sequence(label_a, bin_a, label_b, bin_b, extra, iters, raw_lines):
    order = ["A", "B", "B", "A"] * REPS + ["A"]
    obs = {"A": [], "B": []}
    for index, side in enumerate(order):
        rows, corpus = run(bin_a if side == "A" else bin_b, extra, iters)
        obs[side].append(rows)
        raw_lines.append(f"{label_a}vs{label_b} seq={index:02d} side={side} "
            + " ".join(f"{piece}={rows[piece][0]:.1f}+{rows[piece][1]:.1f}" for piece in PIECES)
            + f" | {corpus}")
    return obs


def summarize(obs, label_a, label_b, raw_lines):
    ratios = {}
    for piece in PIECES:
        search_only = []
        values = []
        for rep in range(REPS):
            a = statistics.mean([obs["A"][rep * 2][piece][0], obs["A"][rep * 2 + 1][piece][0]])
            ae = statistics.mean([obs["A"][rep * 2][piece][1], obs["A"][rep * 2 + 1][piece][1]])
            b = statistics.mean([obs["B"][rep * 2][piece][0], obs["B"][rep * 2 + 1][piece][0]])
            be = statistics.mean([obs["B"][rep * 2][piece][1], obs["B"][rep * 2 + 1][piece][1]])
            values.append(a / b)
            search_only.append((a - ae) / (b - be))
        ratios[piece] = (values, search_only)
        raw_lines.append(f"RATIO {label_a}/{label_b} {piece} total "
            + " ".join(f"{value:.4f}" for value in values)
            + f" median={statistics.median(values):.4f}")
        raw_lines.append(f"RATIO {label_a}/{label_b} {piece} search "
            + " ".join(f"{value:.4f}" for value in search_only)
            + f" median={statistics.median(search_only):.4f}")
    return ratios


def provenance(binary, name, lines):
    out = subprocess.run([os.path.join(BUILD, binary), "--reps", "0", "--warmup", "0"],
        capture_output=True, text=True, check=True)
    for line in out.stdout.splitlines():
        if line.startswith("PRODUCER"):
            lines.append(f"PROVENANCE {name} {line}")


def main():
    raw_lines = []
    print("=== environment")
    print("host", platform.node(), "kernel", platform.release())
    print("cpu", shell("grep -m1 'model name' /proc/cpuinfo | cut -d: -f2-"))
    print("logical_cpus", os.cpu_count(), "pin_core", CORE)
    print("governor", shell(f"cat /sys/devices/system/cpu/cpu{CORE}/cpufreq/scaling_governor"))
    print("current_freq_khz", shell(f"cat /sys/devices/system/cpu/cpu{CORE}/cpufreq/scaling_cur_freq"))
    print("min_max_freq", shell(f"cat /sys/devices/system/cpu/cpu{CORE}/cpufreq/scaling_min_freq /sys/devices/system/cpu/cpu{CORE}/cpufreq/scaling_max_freq | tr '\\n' ' '"))
    print("boost", shell("cat /sys/devices/system/cpu/cpufreq/boost 2>/dev/null || echo none"))
    print("compiler", shell("g++ --version | head -1"), shell("cmake --version | head -1"))
    print("build_dir", BUILD, "build_preset_flags",
        shell(f"grep -m1 CMAKE_CXX_FLAGS:STRING {BUILD}/CMakeCache.txt | cut -d= -f2-"))
    print("ordering", "ABBA per repetition plus a trailing A control", "repetitions", REPS)
    print("internal_reps_per_run", "T", ITERS_T, "raw", ITERS_RAW, "warmup", WARMUP)
    for binary in ("reference_a_frozen", "arrival_candidates", "raw_bench_current",
            "raw_bench_frozen_180fix", "raw_bench_frozen"):
        path = os.path.join(BUILD, binary)
        if os.path.exists(path):
            print("binary_sha256", binary, sha256(path))
    for binary, name in (("reference_a_frozen", "reference_a_frozen"),
            ("arrival_candidates", "current_arrival"),
            ("raw_bench_current", "current_raw"),
            ("raw_bench_frozen_180fix", "frozen_180fix_raw"),
            ("raw_bench_frozen", "frozen_raw")):
        provenance(binary, name, raw_lines)

    print("=== T semantic enumeration versus Reference A (production flags)")
    obs = sequence("current_arrival", "arrival_candidates", "reference_a_frozen",
        "reference_a_frozen", [], ITERS_T, raw_lines)
    t_ratios = summarize(obs, "current_arrival", "reference_a_frozen", raw_lines)

    print("=== raw BFS versus frozen 0c35e13 with the isolated 180 fix (production flags)")
    obs = sequence("current_raw", "raw_bench_current", "frozen_180fix_raw",
        "raw_bench_frozen_180fix", [], ITERS_RAW, raw_lines)
    raw_ratios = summarize(obs, "current_raw", "frozen_180fix_raw", raw_lines)

    print("=== raw BFS versus plain frozen 0c35e13 with 180 disabled on both sides")
    obs = sequence("current_raw", "raw_bench_current", "frozen_raw",
        "raw_bench_frozen", ["--no-180"], ITERS_RAW, raw_lines)
    no180_ratios = summarize(obs, "current_raw", "frozen_raw", raw_lines)

    print("=== gates")
    t_medians = {piece: (statistics.median(values), statistics.median(search)) for piece, (values, search) in t_ratios.items()}
    print("speedup_medians_current_over_reference_a total",
        " ".join(f"{piece}={t_medians[piece][0]:.3f}" for piece in PIECES),
        "search_only", " ".join(f"{piece}={t_medians[piece][1]:.3f}" for piece in PIECES),
        "(higher is better; gate applies to T at >= 2.000)",
        "PASS" if min(t_medians["T"]) >= 2.0 else "FAIL")
    raw_medians = {piece: (statistics.median(values), statistics.median(search)) for piece, (values, search) in raw_ratios.items()}
    for kind, label in ((0, "total"), (1, "search_only")):
        print("raw_ratio_medians_current_over_frozen_180fix", label,
            " ".join(f"{piece}={raw_medians[piece][kind]:.4f}" for piece in PIECES),
            "(lower is better; gate is every non-T piece <= 1.020)",
            "PASS" if max(raw_medians[p][kind] for p in PIECES if p != "T") <= 1.02 else "FAIL")
    no180_medians = {piece: (statistics.median(values), statistics.median(search)) for piece, (values, search) in no180_ratios.items()}
    for kind, label in ((0, "total"), (1, "search_only")):
        print("raw_ratio_medians_current_over_plain_frozen_180off", label,
            " ".join(f"{piece}={no180_medians[piece][kind]:.4f}" for piece in PIECES))
    print("=== detail lines")
    for line in raw_lines:
        print(line)


main()
