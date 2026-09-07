#!/usr/bin/env python3
"""Gate-3 volume table from partition CSVs + full-run PROFILE rows."""
import csv
import struct
import sys

PART = "/home/icly/Documents/tetris_ai_runner/results/phase7/partition"


def read_totals(path):
    out = {}
    with open(path) as f:
        for line in f:
            k, v = line.rstrip("\n").split("\t")
            out[k] = int(v)
    return out


def read_profile_counts(path, prefix):
    with open(path) as f:
        text = f.read().strip()
    assert text.startswith(prefix), path
    out = {}
    for tok in text.split():
        if "=" in tok:
            k, v = tok.split("=", 1)
            try:
                out[k] = int(v)
            except ValueError:
                pass
    return out


def read_mults(path):
    with open(path, "rb") as f:
        assert f.read(8) == b"PARTV1\n\x00"
        assert struct.unpack("<I", f.read(4))[0] == 1
        wc = struct.unpack("<I", f.read(4))[0]
        n = struct.unpack("<Q", f.read(8))[0]
        mults, nvs = [], []
        for _ in range(n):
            f.read(8 * wc)
            f.read(4)
            mults.append(struct.unpack("<Q", f.read(8))[0])
            nc = struct.unpack("<I", f.read(4))[0]
            nvs.append(nc)
            f.read(nc * 18)
    return mults, nvs


def main():
    lines = [("seed", "leg", "metric", "value")]
    for seed in (1, 2, 3):
        t = read_totals(f"{PART}/seed{seed}.run_totals.tsv")
        c = read_profile_counts(
            f"{PART}/seed{seed}.profile_cmp.txt", "PROFILE_CMP")
        mults, nvs = read_mults(f"{PART}/seed{seed}.inputs.bin")
        assert sum(m * n for m, n in zip(mults, nvs)) == t["unique_candidates"]
        assert sum(mults) == t["enum_calls"]
        s_delta = s_a = s_nV = s_nLsem = s_nLnorm = 0
        s_mv_d = s_mv_nV = s_mv_nL = 0
        nS = 0
        with open(f"{PART}/seed{seed}.uniques.csv") as f:
            for i, r in enumerate(csv.DictReader(f)):
                if r["ood"] == "1" or r["missing"] == "1":
                    continue
                nS += 1
                d = int(r["delta"])
                s_delta += d
                s_a += int(r["a"])
                s_nV += int(r["nV"])
                s_nLsem += int(r["nL_sem"])
                s_nLnorm += int(r["nL_norm"])
                m = mults[i]
                s_mv_d += m * d
                s_mv_nV += m * int(r["nV"])
                s_mv_nL += m * int(r["nL_sem"])
        st_dC = st_nCV = st_nCL = 0
        with open(f"{PART}/seed{seed}.trans.csv") as f:
            for r in csv.DictReader(f):
                if r["ood"] == "1":
                    continue
                st_dC += int(r["deltaC"])
                st_nCV += int(r["nCV"])
                st_nCL += int(r["nCL"])
        v_u, l_u = t["unique_candidates"], c["unique_candidates"]
        v_t, l_t = t["policy_transitions"], c["transitions"]
        rows = [
            ("uniques", "V_campaign", v_u),
            ("uniques", "L_campaign", l_u),
            ("uniques", "Delta_campaign", v_u - l_u),
            ("uniques", "S_inputs", nS),
            ("uniques", "sumS_delta_distinct", s_delta),
            ("uniques", "sumS_a", s_a),
            ("uniques", "sumS_nV_distinct", s_nV),
            ("uniques", "sumS_nLsem_distinct", s_nLsem),
            ("uniques", "sumS_delta_multV", s_mv_d),
            ("uniques", "sumS_nV_multV", s_mv_nV),
            ("uniques", "sumS_nLsem_multV", s_mv_nL),
            ("uniques", "volume_residual_distinct",
             (v_u - l_u) - s_delta),
            ("uniques", "volume_residual_multV",
             (v_u - l_u) - s_mv_d),
            ("uniques", "V_parents", t["parents"]),
            ("uniques", "L_parents", c["parents"]),
            ("uniques", "V_enum_calls", t["enum_calls"]),
            ("uniques", "L_searches", c["searches"]),
            ("trans", "V_campaign", v_t),
            ("trans", "L_campaign", l_t),
            ("trans", "Delta_campaign", v_t - l_t),
            ("trans", "sumS_dC_distinct", st_dC),
            ("trans", "sumS_nCV_distinct", st_nCV),
            ("trans", "sumS_nCL_distinct", st_nCL),
            ("trans", "volume_residual_distinct", (v_t - l_t) - st_dC),
            ("trans", "V_merges", t["transposition_merges"]),
            ("trans", "V_materialized", t["materialized_nodes"]),
            ("trans", "L_materialized", c["materialized_nodes"]),
        ]
        for leg, k, v in rows:
            lines.append((str(seed), leg, k, str(v)))
    with open(f"{PART}/volume_table.tsv", "w") as f:
        for s, leg, k, v in lines:
            f.write(f"{s}\t{leg}\t{k}\t{v}\n")
    for s, leg, k, v in lines:
        print(f"{s}\t{leg}\t{k}\t{v}")


main()
