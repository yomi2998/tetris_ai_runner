// Per-input classification core (engine-free; see
// docs/phase7/count_partition_instrument_design.md §§1-2).
// Uniques leg per (input, source), V = value K-set, Lsem = legacy K-set:
//   (S)  nV - nL_sem == a + c2v - c2l + dV - dL
//   (N)  nL_norm - nL_sem == b_counted == b_spin_split + b_opaque
//   (R)  nRaw - nL_norm == b_collapsed
// c1 is leg-1 informational (lockout cannot change membership); c3 never
// reaches this code. Trans leg on (res40, clear, spin, lockout):
//   (T)  nCV - nCL == a_t - b_t + dVt - dLt, c1_t_V == c1_t_L.

#pragma once

#include <cstdint>
#include <map>
#include <vector>

namespace partition_class
{
    struct ValueCand
    {
        std::uint64_t khash = 0;
        bool apply_ok = false;
        std::uint8_t spin = 0; // 0=None 1=Mini 2=Full
        std::uint8_t clear_count = 0;
        bool lockout = false;
        bool survivor = false;
        std::uint64_t res40 = 0;
        std::uint32_t packed = 0;
        std::uint8_t arrival = 0; // 0=Normal, 1=TerminalRotation (T only)
        bool scalar_ok = true;
        bool replay_ok = true;
    };

    struct LegacyPoint
    {
        bool has_k = false;
        std::uint64_t khash = 0;
        bool matched = false;
        std::uint64_t channel = 0;
        std::uint32_t opaque = 0;
        std::uint8_t clear_count = 0;
        std::uint8_t spin = 0; // 0=None 1=Full 2=Mini
        bool lockout = false;
        std::uint64_t res40 = 0;
    };

    struct UniquesRow
    {
        long nV = 0;
        long nL_sem = 0;
        long nL_norm = 0;
        long nRaw = 0;
        long a = 0;
        long b_counted = 0;
        long b_spin_split = 0;
        long b_opaque = 0;
        long b_collapsed = 0;
        long c1 = 0;
        long c2v = 0;
        long c2l = 0;
        long dV = 0;
        long dL = 0;
        long dV_om = 0;
        std::vector<std::uint64_t> dV_keys;
        std::vector<std::uint64_t> dL_keys;
        std::vector<std::uint64_t> om_keys;
        std::map<std::uint64_t, char> v_kclass;
        std::map<std::uint64_t, char> l_kclass;
        bool ok_S = false;
        bool ok_N = false;
        bool ok_R = false;
        bool ok_maps = false;
    };

    inline std::uint64_t norm_group(bool matched, std::uint64_t cells_hash,
        std::uint64_t channel, std::uint32_t opaque, std::uint64_t &group_high)
    {
        if (matched)
        {
            group_high = 0;
            std::uint64_t h = 1469598103934665603ull;
            h ^= cells_hash;
            h *= 1099511628211ull;
            h ^= channel + 0x9e3779b97f4a7c15ull;
            h *= 1099511628211ull;
            return h;
        }
        group_high = 1;
        return static_cast<std::uint64_t>(opaque);
    }

    inline UniquesRow classify_uniques(std::vector<ValueCand> const &vcands,
        std::vector<LegacyPoint> const &lpoints, bool can_spawn_value)
    {
        UniquesRow row;
        row.nV = static_cast<long>(vcands.size());
        row.nRaw = static_cast<long>(lpoints.size());

        std::map<std::uint64_t, std::vector<std::size_t>> v_by_k;
        for (std::size_t i = 0; i < vcands.size(); ++i)
        {
            v_by_k[vcands[i].khash].push_back(i);
        }
        std::map<std::uint64_t, std::vector<std::size_t>> l_by_k;
        std::map<std::pair<std::uint64_t, std::uint64_t>, long> norm_groups;
        std::map<std::pair<std::uint64_t, std::uint64_t>, long> channel_groups;
        for (std::size_t i = 0; i < lpoints.size(); ++i)
        {
            auto const &p = lpoints[i];
            if (p.has_k)
            {
                l_by_k[p.khash].push_back(i);
            }
            std::uint64_t hi = 0;
            std::uint64_t gid =
                norm_group(p.matched, p.khash, p.channel, p.opaque, hi);
            norm_groups[{hi, gid}]++;
            if (p.matched)
            {
                channel_groups[{hi, gid}]++;
            }
        }
        row.nL_sem = static_cast<long>(l_by_k.size());
        row.nL_norm = static_cast<long>(norm_groups.size());
        row.b_collapsed = row.nRaw - row.nL_norm;
        row.b_counted = row.nL_norm - row.nL_sem;
        row.b_spin_split =
            static_cast<long>(channel_groups.size()) - row.nL_sem;
        row.b_opaque = row.b_counted - row.b_spin_split;

        std::map<std::uint64_t, std::vector<std::size_t>> v_outcome_conflict;
        for (auto const &[k, idx] : v_by_k)
        {
            for (std::size_t j = 1; j < idx.size(); ++j)
            {
                auto const &x = vcands[idx[0]];
                auto const &y = vcands[idx[j]];
                if (!x.apply_ok || !y.apply_ok)
                {
                    continue;
                }
                if (x.spin != y.spin || x.clear_count != y.clear_count
                    || x.lockout != y.lockout)
                {
                    v_outcome_conflict[k] = idx;
                    break;
                }
            }
        }
        std::map<std::uint64_t, std::vector<std::size_t>> l_outcome_conflict;
        for (auto const &[k, idx] : l_by_k)
        {
            for (std::size_t j = 1; j < idx.size(); ++j)
            {
                auto const &x = lpoints[idx[0]];
                auto const &y = lpoints[idx[j]];
                if (x.clear_count != y.clear_count || x.spin != y.spin
                    || x.lockout != y.lockout)
                {
                    l_outcome_conflict[k] = idx;
                    break;
                }
            }
        }
        row.ok_maps = v_outcome_conflict.empty() && l_outcome_conflict.empty();
        for (auto const &[k, idx] : v_outcome_conflict)
        {
            (void)idx;
            row.dV++;
            row.dV_keys.push_back(k);
        }
        for (auto const &[k, idx] : l_outcome_conflict)
        {
            (void)idx;
            row.dL++;
            row.dL_keys.push_back(k);
        }

        for (auto const &[k, vidx] : v_by_k)
        {
            auto lit = l_by_k.find(k);
            if (lit == l_by_k.end())
            {
                continue;
            }
            auto const &v = vcands[vidx[0]];
            auto const &l = lpoints[lit->second[0]];
            if (!v.apply_ok)
            {
                continue;
            }
            if (v.lockout != l.lockout)
            {
                row.c1++;
            }
            int const v_spin_sem = v.spin == 1 ? 1 : (v.spin == 2 ? 2 : 0);
            int const l_spin_sem = l.spin == 1 ? 2 : (l.spin == 2 ? 1 : 0);
            if (v.clear_count != l.clear_count || v_spin_sem != l_spin_sem)
            {
                row.dV_om++;
                row.om_keys.push_back(k);
            }
        }

        for (auto const &[k, vidx] : v_by_k)
        {
            if (l_by_k.count(k) != 0)
            {
                row.v_kclass[k] = 's';
                continue;
            }
            if (!can_spawn_value)
            {
                row.c2v++;
                row.v_kclass[k] = 'c';
                continue;
            }
            auto const &v = vcands[vidx[0]];
            if (!v.apply_ok)
            {
                row.dV++;
                row.dV_keys.push_back(k);
                row.v_kclass[k] = 'V';
                continue;
            }
            bool scalar_all = true;
            bool replay_all = true;
            for (std::size_t j : vidx)
            {
                scalar_all = scalar_all && vcands[j].scalar_ok;
                replay_all = replay_all && vcands[j].replay_ok;
            }
            if (scalar_all && replay_all)
            {
                row.a++;
                row.v_kclass[k] = 'a';
            }
            else
            {
                row.dV++;
                row.dV_keys.push_back(k);
                row.v_kclass[k] = 'V';
            }
        }
        for (auto const &[k, lidx] : l_by_k)
        {
            (void)lidx;
            if (v_by_k.count(k) != 0)
            {
                row.l_kclass[k] = 's';
                continue;
            }
            if (!can_spawn_value)
            {
                row.c2l++;
                row.l_kclass[k] = 'c';
                continue;
            }
            row.dL++;
            row.dL_keys.push_back(k);
            row.l_kclass[k] = 'L';
        }
        for (auto const &[k, idx] : v_outcome_conflict)
        {
            (void)idx;
            row.v_kclass[k] = 'V';
        }
        for (auto const &[k, idx] : l_outcome_conflict)
        {
            (void)idx;
            row.l_kclass[k] = 'L';
        }

        row.ok_S = (row.nV - row.nL_sem
            == row.a + row.c2v - row.c2l + row.dV - row.dL);
        row.ok_N = (row.nL_norm - row.nL_sem == row.b_counted)
            && (row.b_counted == row.b_spin_split + row.b_opaque)
            && row.b_counted >= 0 && row.b_spin_split >= 0 && row.b_opaque >= 0;
        row.ok_R = (row.nRaw - row.nL_norm == row.b_collapsed)
            && row.b_collapsed >= 0;
        return row;
    }

    struct TransChild
    {
        std::uint64_t res40 = 0;
        std::uint8_t clear_count = 0;
        std::uint8_t spin_sem = 0; // 0=None 1=Mini 2=Full (semantic codes)
        bool lockout = false;
        std::uint64_t khash = 0;
        // 0 = Vonly-a, 1 = Vonly-d/other, 2 = shared, 3 = Lonly.
        int kclass = 2;
    };

    struct TransKey
    {
        std::uint64_t res40 = 0;
        std::uint8_t clear_count = 0;
        std::uint8_t spin_sem = 0;
        bool lockout = false;

        bool operator<(TransKey const &o) const
        {
            if (res40 != o.res40)
            {
                return res40 < o.res40;
            }
            if (clear_count != o.clear_count)
            {
                return clear_count < o.clear_count;
            }
            if (spin_sem != o.spin_sem)
            {
                return spin_sem < o.spin_sem;
            }
            return lockout < o.lockout;
        }
    };

    struct TransRow
    {
        long nCV = 0;
        long nCL = 0;
        long a_t = 0;
        long b_t = 0;
        long c1_t_V = 0;
        long c1_t_L = 0;
        long dVt = 0;
        long dLt = 0;
        bool ok_T = false;
        bool ok_c1 = false;
    };

    inline TransRow classify_trans(std::vector<TransChild> const &vc,
        std::vector<TransChild> const &lc)
    {
        TransRow row;
        row.nCV = static_cast<long>(vc.size());
        row.nCL = static_cast<long>(lc.size());
        std::map<TransKey, std::vector<TransChild>> vm;
        std::map<TransKey, std::vector<TransChild>> lm;
        for (auto const &c : vc)
        {
            vm[{c.res40, c.clear_count, c.spin_sem, c.lockout}].push_back(c);
        }
        for (auto const &c : lc)
        {
            lm[{c.res40, c.clear_count, c.spin_sem, c.lockout}].push_back(c);
        }
        std::map<TransKey, std::vector<TransChild>> surV;
        std::map<TransKey, std::vector<TransChild>> surL;
        for (auto &[k, vec] : vm)
        {
            auto it = lm.find(k);
            long m = 0;
            if (it != lm.end())
            {
                m = std::min<long>(static_cast<long>(vec.size()),
                    static_cast<long>(it->second.size()));
                it->second.erase(it->second.begin(), it->second.begin() + m);
            }
            if (m < static_cast<long>(vec.size()))
            {
                surV[k] = std::vector<TransChild>(
                    vec.begin() + m, vec.end());
            }
        }
        for (auto &[k, vec] : lm)
        {
            if (!vec.empty())
            {
                surL[k] = vec;
            }
        }
        for (auto &[k, vec] : surV)
        {
            if (vec.empty())
            {
                continue;
            }
            TransKey flip{k.res40, k.clear_count, k.spin_sem, !k.lockout};
            auto it = surL.find(flip);
            if (it == surL.end() || it->second.empty())
            {
                continue;
            }
            long p = std::min<long>(
                static_cast<long>(vec.size()), static_cast<long>(it->second.size()));
            row.c1_t_V += p;
            row.c1_t_L += p;
            vec.erase(vec.begin(), vec.begin() + p);
            it->second.erase(it->second.begin(), it->second.begin() + p);
        }
        for (auto const &[k, vec] : surV)
        {
            (void)k;
            for (auto const &c : vec)
            {
                if (c.kclass == 0)
                {
                    row.a_t++;
                }
                else
                {
                    row.dVt++;
                }
            }
        }
        for (auto const &[k, vec] : surL)
        {
            auto vit = vm.find(k);
            bool value_has_key =
                vit != vm.end() && !vit->second.empty();
            if (value_has_key)
            {
                row.b_t += static_cast<long>(vec.size());
            }
            else
            {
                row.dLt += static_cast<long>(vec.size());
            }
        }
        row.ok_c1 = (row.c1_t_V == row.c1_t_L);
        row.ok_T = (row.nCV - row.nCL
            == row.a_t - row.b_t + row.dVt - row.dLt);
        return row;
    }

    struct Verdict
    {
        bool all_ok = true;
        long d_total = 0;
        long missing = 0;
        long overflow = 0;
    };

    inline int exit_code_for(Verdict const &v)
    {
        return (v.all_ok && v.d_total == 0 && v.missing == 0 && v.overflow == 0)
            ? 0
            : 1;
    }
} // namespace partition_class
