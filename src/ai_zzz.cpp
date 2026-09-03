
//by ZouZhiZhang

#include "tetris_core.h"
#include "integer_utils.h"
#include "ai_zzz.h"
#include <algorithm>
#include <bit>
#include <cstdint>

using namespace m_tetris;
using namespace zzz;

namespace ai_zzz
{
    bool TOJ::Status::operator < (Status const &other) const
    {
        return value < other.value;
    }

    int8_t TOJ::get_safe(m_tetris::TetrisMap const &m, char t) const {
        int safe = 0;
        while (map_in_danger_(m, context_->convert(t), safe + 1) == 0)
        {
            ++safe;
        }
        return safe;
    }

    void TOJ::init(m_tetris::TetrisContext const *context, Config const *config)
    {
        context_ = context;
        config_ = config;
        feature_observer_count_ = 0;
        transition_observer_count_ = 0;
        col_mask_ = context->full() & ~1;
        row_mask_ = context->full();
        map_danger_data_.resize(context->type_max());
        for (size_t i = 0; i < context->type_max(); ++i)
        {
            TetrisMap map(context->width(), context->height());
            TetrisNode const *node = context->generate(i);
            node->attach(context, map);
            std::memcpy(map_danger_data_[i].data, &map.row[18], sizeof map_danger_data_[i].data);
            for (int y = 0; y < 3; ++y)
            {
                map_danger_data_[i].data[y + 1] |= map_danger_data_[i].data[y];
            }
        }
    }

    std::string TOJ::ai_name() const
    {
        return "ZZZ TOJ v0.12";
    }

    void TOJ::Status::init_t_value(m_tetris::TetrisMap const &m, int16_t &t2_value_ref, int16_t &t3_value_ref, m_tetris::TetrisMap *out_map)
    {
        Values values;
        if (m.width == 10)
        {
            uint8_t counts[23];
            fill_counts(m, counts);
            for (int y = 0, end = std::min(20, m.roof - 2); y < end; ++y)
            {
                uint32_t rows[7];
                for (int i = 0; i < 7; ++i)
                {
                    rows[i] = m.row[y + i];
                }
                int qualifying = (counts[y] == 9) + (counts[y + 1] == 8) + (counts[y + 2] == 9);
                int total3 = counts[y] + counts[y + 1] + counts[y + 2];
                if (counts[y + 2] == 9 && qualifying >= 2 && total3 > 20)
                {
                    int hole = std::countr_zero(~rows[2] & 0x3ffu);
                    uint32_t straight = ~rows[0] & ~(rows[1] | (rows[1] >> 1)) & ~(rows[3] | (rows[3] >> 1) | (rows[3] >> 2)) & ~((rows[4] >> 1) | (rows[4] >> 2)) & 0xfeu;
                    if ((straight >> hole) & 1)
                    {
                        int value = t3a_readiness(m.row, counts, y, hole, qualifying, total3);
                        values.t3 += static_cast<int16_t>(value);
                        if (out_map != nullptr)
                        {
                            apply_overlay(Kind::T3, false, hole, y, value, *out_map);
                        }
                        y += 2;
                        continue;
                    }
                    uint32_t mirrored = ~rows[0] & ~(rows[1] | (rows[1] << 1)) & ~(rows[3] | (rows[3] << 1) | (rows[3] << 2)) & ~((rows[4] << 1) | (rows[4] << 2)) & 0x1fcu;
                    if ((mirrored >> hole) & 1)
                    {
                        int value = t3b_readiness(m.row, counts, y, hole, qualifying, total3);
                        values.t3 += static_cast<int16_t>(value);
                        if (out_map != nullptr)
                        {
                            apply_overlay(Kind::T3, true, hole, y, value, *out_map);
                        }
                        y += 2;
                        continue;
                    }
                }
                uint32_t candidates = (rows[0] & (rows[0] >> 2)) & ~(rows[0] >> 1) & ~(rows[1] | (rows[1] >> 1) | (rows[1] >> 2)) & 0xffu;
                if (candidates == 0)
                {
                    continue;
                }
                int total2 = counts[y] + counts[y + 1];
                if (total2 <= 10)
                {
                    values.t2 += static_cast<int16_t>(std::popcount(candidates) * total2);
                    continue;
                }
                int x = std::countr_zero(candidates);
                int value = t2_readiness(rows[0], rows[1], rows[2], counts[y], counts[y + 1], x);
                values.t2 += static_cast<int16_t>(value);
                if (out_map != nullptr)
                {
                    apply_overlay(Kind::T2, false, x, y, value, *out_map);
                }
                ++y;
            }
        }
        t2_value_ref = values.t2;
        t3_value_ref = values.t3;
    }

    uint32_t TOJ::Status::pack(uint8_t x, uint8_t y, Kind kind, uint8_t readiness, bool mirrored)
    {
        return uint32_t(x) | uint32_t(y) << 4 | uint32_t(kind) << 9 | uint32_t(readiness) << 11 | uint32_t(mirrored) << 19;
    }

    uint8_t TOJ::Status::descriptor_x(uint32_t descriptor)
    {
        return static_cast<uint8_t>(descriptor & 15);
    }

    uint8_t TOJ::Status::descriptor_y(uint32_t descriptor)
    {
        return static_cast<uint8_t>((descriptor >> 4) & 31);
    }

    TOJ::Status::Kind TOJ::Status::descriptor_kind(uint32_t descriptor)
    {
        return static_cast<Kind>((descriptor >> 9) & 3);
    }

    uint8_t TOJ::Status::descriptor_readiness(uint32_t descriptor)
    {
        return static_cast<uint8_t>((descriptor >> 11) & 255);
    }

    bool TOJ::Status::descriptor_mirrored(uint32_t descriptor)
    {
        return ((descriptor >> 19) & 1) != 0;
    }

    int TOJ::Status::t2_readiness(uint32_t row0, uint32_t row1, uint32_t row2, int count0, int count1, int x)
    {
        int total = count0 + count1;
        int value = total;
        if (total <= 10)
        {
            return value;
        }
        value += (count0 == 9) * total;
        value += (count1 == 7) * total;
        switch ((row2 >> x) & 7)
        {
        case 1:
        case 4:
            value += total * 3;
            break;
        case 2:
        case 3:
        case 5:
        case 6:
        case 7:
            value = 0;
            break;
        default:
            value /= 2;
            break;
        }
        return value;
    }

    int TOJ::Status::t3a_readiness(uint32_t const *rows, uint8_t const *counts, int y, int hole, int qualifying, int total)
    {
        int value = total * qualifying;
        if ((rows[y + 4] >> hole) & 1)
        {
            value += total + counts[y + 3];
        }
        else if (((rows[y + 4] >> hole) & 7) == 1 && ((rows[y + 5] >> hole) & 7) == 1 && ((rows[y + 6] >> hole) & 7) == 1)
        {
            value = 0;
        }
        else
        {
            value /= 2;
        }
        if (((rows[y + 3] >> hole) & 8) != ((rows[y + 4] >> hole) & 8))
        {
            value = 0;
        }
        return value;
    }

    int TOJ::Status::t3b_readiness(uint32_t const *rows, uint8_t const *counts, int y, int hole, int qualifying, int total)
    {
        int value = total * qualifying;
        if ((rows[y + 4] >> hole) & 1)
        {
            value += total + counts[y + 3];
        }
        else if (((rows[y + 4] >> (hole - 2)) & 7) == 4 && ((rows[y + 5] >> (hole - 2)) & 7) == 4 && ((rows[y + 6] >> (hole - 2)) & 7) == 4)
        {
            value = 0;
        }
        else
        {
            value /= 2;
        }
        if (hole >= 3 && (((rows[y + 3] >> (hole - 3)) & 1) != ((rows[y + 4] >> (hole - 3)) & 1)))
        {
            value = 0;
        }
        return value;
    }

    void TOJ::Status::apply_overlay(Kind kind, bool mirrored, int x, int y, int readiness, m_tetris::TetrisMap &map)
    {
        if (readiness == 0)
        {
            return;
        }
        if (kind == Kind::T2)
        {
            map.row[y] |= 2u << x;
            map.row[y + 1] |= 7u << x;
        }
        else if (mirrored)
        {
            map.row[y] |= 1u << x;
            map.row[y + 1] |= 3u << (x - 1);
            map.row[y + 2] |= 1u << x;
            map.row[y + 3] |= 1u << x;
        }
        else
        {
            map.row[y] |= 1u << x;
            map.row[y + 1] |= 3u << x;
            map.row[y + 2] |= 1u << x;
            map.row[y + 3] |= 1u << x;
        }
    }

    void TOJ::Status::fill_counts(m_tetris::TetrisMap const &map, uint8_t *counts)
    {
        std::fill_n(counts, 23, uint8_t(0));
        int end = std::min(map.roof, 23);
        for (int y = 0; y < end; ++y)
        {
            counts[y] = static_cast<uint8_t>(zzz::BitCount(map.row[y]));
        }
    }

    TOJ::Status::DescriptorSet TOJ::Status::enumerate_descriptors(m_tetris::TetrisMap const &map)
    {
        DescriptorSet result;
        if (map.width != 10)
        {
            return result;
        }
        uint8_t counts[23];
        fill_counts(map, counts);
        int end = std::min(20, map.roof - 2);
        for (int y = 0; y < end; ++y)
        {
            uint32_t const *rows = map.row + y;
            uint32_t t2 = (rows[0] & (rows[0] >> 2)) & ~(rows[0] >> 1) & ~(rows[1] | (rows[1] >> 1) | (rows[1] >> 2)) & 0xffu;
            while (t2 != 0)
            {
                int x = std::countr_zero(t2);
                t2 &= t2 - 1;
                int value = t2_readiness(rows[0], rows[1], rows[2], counts[y], counts[y + 1], x);
                result.data[result.count++] = pack(x, y, Kind::T2, value);
            }
            int qualifying = (counts[y] == 9) + (counts[y + 1] == 8) + (counts[y + 2] == 9);
            int total = counts[y] + counts[y + 1] + counts[y + 2];
            if (counts[y + 2] != 9 || qualifying < 2 || total <= 20)
            {
                continue;
            }
            int hole = std::countr_zero(~rows[2] & 0x3ffu);
            uint32_t straight = ~rows[0] & ~(rows[1] | (rows[1] >> 1)) & ~(rows[3] | (rows[3] >> 1) | (rows[3] >> 2)) & ~((rows[4] >> 1) | (rows[4] >> 2)) & 0xfeu;
            if ((straight >> hole) & 1)
            {
                int value = t3a_readiness(map.row, counts, y, hole, qualifying, total);
                result.data[result.count++] = pack(hole, y, Kind::T3, value);
                continue;
            }
            uint32_t mirrored = ~rows[0] & ~(rows[1] | (rows[1] << 1)) & ~(rows[3] | (rows[3] << 1) | (rows[3] << 2)) & ~((rows[4] << 1) | (rows[4] << 2)) & 0x1fcu;
            if ((mirrored >> hole) & 1)
            {
                int value = t3b_readiness(map.row, counts, y, hole, qualifying, total);
                result.data[result.count++] = pack(hole, y, Kind::T3, value, true);
            }
        }
        return result;
    }

    TOJ::Status::Values TOJ::Status::decode(DescriptorSet const &slots, m_tetris::TetrisMap *out_map)
    {
        Values result;
        for (int i = 0; i < slots.count; ++i)
        {
            uint32_t descriptor = slots.data[i];
            Kind kind = descriptor_kind(descriptor);
            int readiness = descriptor_readiness(descriptor);
            if (kind == Kind::T2)
            {
                result.t2 += static_cast<int16_t>(readiness);
            }
            else
            {
                result.t3 += static_cast<int16_t>(readiness);
            }
            if (out_map != nullptr)
            {
                apply_overlay(kind, descriptor_mirrored(descriptor), descriptor_x(descriptor), descriptor_y(descriptor), readiness, *out_map);
            }
        }
        return result;
    }

    TOJ::Result TOJ::eval(TetrisNodeEx const &node, TetrisMap const &map, TetrisMap const &src_map) const
    {
        const int width_m1 = map.width - 1;

        Result result;
        memset(&result, 0, sizeof result);

        TetrisMap t_map = map;
        Status::init_t_value(t_map, result.t2_value, result.t3_value, &t_map);

        size_t ColTrans = 2 * (t_map.height - t_map.roof);
        size_t RowTrans = t_map.roof == t_map.height ? 0 : t_map.width;
        for (int y = 0; y < t_map.roof; ++y)
        {
            ColTrans += !t_map.full(0, y) + !t_map.full(width_m1, y) + zzz::BitCount((t_map.row[y] ^ (t_map.row[y] << 1)) & col_mask_);
            if (y != 0)
            {
                RowTrans += zzz::BitCount(t_map.row[y - 1] ^ t_map.row[y]);
            }
        }
        RowTrans += zzz::BitCount(row_mask_ & ~t_map.row[0]);
        if (t_map.roof != 0)
        {
            RowTrans += zzz::BitCount(t_map.roof == t_map.height ? row_mask_ & ~t_map.row[t_map.roof - 1] : t_map.row[t_map.roof - 1]);
        }
        struct
        {
            int HoleCount;
            int HoleLine;

            int Wide[31];

            int LineCoverBits;
            int ClearWidth;
        } v;
        std::memset(&v, 0, sizeof v);
        int WideCount = t_map.width - 1;

        for (int y = t_map.roof - 1; y >= 0; --y)
        {
            v.LineCoverBits |= t_map.row[y];
            int LineHole = v.LineCoverBits ^ t_map.row[y];
            if (LineHole != 0)
            {
                v.HoleCount += zzz::BitCount(LineHole);
                ++v.HoleLine;
                for (int hy = y + 1, hy_max = std::min(t_map.roof, hy + 8); hy < hy_max; ++hy)
                {
                    uint32_t CheckLine = LineHole & t_map.row[hy];
                    if (CheckLine > 0)
                    {
                        v.ClearWidth += (t_map.width - zzz::BitCount(t_map.row[hy])) * hy;
                    }
                }
            }
            WideCount = std::min<int>(WideCount, t_map.width - zzz::BitCount(v.LineCoverBits));
            if (v.HoleLine == 0)
            {
                ++v.Wide[WideCount];
            }
        }
        int side_roof = std::max({map.top[0], map.top[1], map.top[2], map.top[width_m1], map.top[width_m1 - 1], map.top[width_m1 - 2]});
        auto& p = config_->param;
        result.value = (0.
            - side_roof * p.roof
            - ColTrans * p.col_trans
            - RowTrans * p.row_trans
            - v.HoleCount * p.hole_count
            - v.HoleLine * p.hole_line
            - v.ClearWidth * p.clear_width
            + v.Wide[2] * p.wide_2
            + v.Wide[3] * p.wide_3
            + v.Wide[4] * p.wide_4
            );
        return result;
    }

    TOJ::Status TOJ::get(TetrisNodeEx &node, Result const &eval_result, size_t clear, TetrisMap const &map, size_t depth, Status const &status, TetrisContext::Env const &env) const
    {
        if (clear > 0 && node.is_check && node.is_last_rotate)
        {
            if (clear == 1 && node.is_mini_ready)
            {
                node.type = TSpinType::TSpinMini;
            }
            else if (node.is_ready)
            {
                node.type = TSpinType::TSpin;
            }
            else
            {
                node.type = TSpinType::None;
            }
        }
        Status result;
        memcpy(&result, &status, sizeof status);
        int attack = 0;
        int t_attack = 0;
        double like = 0;
        double dislike = 0;
        auto get_combo_attack = [&](int c)
        {
            return config_->table[std::min<int>(config_->table_max - 1, c)];
        };
        auto update_like = [&](double v)
        {
            v > 0 ? like += v : dislike -= v;
        };
        int safe = node->row >= 20 ? -1 : env.length > 0 ? get_safe(map,  *env.next) : (22 - map.roof);
        auto& p = config_->param;
        switch (clear)
        {
        case 0:
            result.combo = 0;
            if (status.under_attack > 0)
            {
                result.map_rise = status.under_attack;
                if (result.map_rise > safe)
                {
                    result.death = 1;
                }
                result.under_attack = 0;
            }
            update_like((node->status.t == 'I') * p.waste_i);
            update_like((node->status.t == 'T') * p.waste_t);
            break;
        case 1:
            if (node.type == TSpinType::TSpinMini)
            {
                attack = 1 + status.b2b;
                update_like(p.tspin_mini);
            }
            else if (node.type == TSpinType::TSpin)
            {
                attack = 2 + status.b2b;
                update_like(p.tspin_1);
                t_attack = 1;
            }
            else
            {
                update_like((node->status.t == 'I') * p.waste_i);
                update_like((node->status.t == 'T') * p.waste_t);
                update_like(p.clear_1);
            }
            attack += get_combo_attack(++result.combo);
            result.b2b = node.type != TSpinType::None;
            break;
        case 2:
            if (node.type != TSpinType::None)
            {
                attack += 4 + status.b2b;
                result.b2b = true;
                update_like(p.tspin_2);
                t_attack = 1;
            }
            else
            {
                ++attack;
                result.b2b = false;
                update_like((node->status.t == 'I') * p.waste_i);
                update_like((node->status.t == 'T') * p.waste_t);
                update_like(p.clear_2);
            }
            attack += get_combo_attack(++result.combo);
            break;
        case 3:
            if (node.type != TSpinType::None)
            {
                attack = 6 + status.b2b * 2;
                result.b2b = true;
                update_like(p.tspin_3);
                t_attack = 1;
            }
            else
            {
                attack += 2;
                result.b2b = false;
                update_like((node->status.t == 'I') * p.waste_i);
                update_like(p.clear_3);
            }
            attack += get_combo_attack(++result.combo);
            break;
        case 4:
            result.b2b = true;
            attack = get_combo_attack(++result.combo) + 4 + status.b2b;
            update_like(p.clear_4);
            break;
        }
        result.under_attack = std::max(0, result.under_attack - attack);
        int config_safe = std::max(0, config_->safe - result.under_attack - result.map_rise);
        int t_expect = [=]()->int
        {
            if (env.hold == 'T')
            {
                return 0;
            }
            for (size_t i = 0; i < env.length; ++i)
            {
                if (env.next[i] == 'T')
                {
                    return i;
                }
            }
            return 13;
        }();
        switch (env.hold)
        {
        case 'T':
            if (node.type == TSpinType::None)
            {
                update_like(double(20 + config_safe) * p.hold_t);
            }
            break;
        case 'I':
            if (clear != 4)
            {
                update_like(double(40 - config_safe) * p.hold_i);
            }
            break;
        }
        safe -= result.map_rise;
        if (safe < 0)
        {
            result.death = 1;
            safe = 0;
        }
        if (map.count == 0 && result.map_rise == 0)
        {
            like += 999;
            attack += 6;
        }
        double field = eval_result.value * double(40 - config_safe) / 20;
        double t_like = 0;
        double t_dislike = 0;
        if (t_attack == 0)
        {
            double t2_safe = std::max(0, config_safe - 4);
            double t3_safe = std::max(0, config_safe - 10);
            if (eval_result.t2_value > status.t2_value)
            {
                t_like += (eval_result.t2_value - status.t2_value) * t2_safe * std::max(10 - t_expect, 5) * p.t2_slot;
            }
            else
            {
                t_dislike += (status.t2_value - eval_result.t2_value) * t2_safe * 3 * p.t2_slot;
            }
            if (eval_result.t3_value > status.t3_value)
            {
                t_like += (eval_result.t3_value - status.t3_value) * t3_safe * std::max(10 - t_expect, 4) * (3 + result.b2b) * p.t3_slot;
            }
            else
            {
                t_dislike += (status.t3_value - eval_result.t3_value) * t3_safe * 4 * p.t3_slot;
            }
        }
        result.t2_value = eval_result.t2_value;
        result.t3_value = eval_result.t3_value;
        result.acc_value += (0
            + attack * (config_safe + 16) * p.attack
            + get_combo_attack(result.combo) * result.combo * (100 - config_safe) * p.combo
            + (result.b2b - status.b2b) * (config_safe + 16) * p.b2b
            - t_dislike
            - dislike * config_safe * (config_safe + 4) * 4
            - result.death * 999999999.0
            );
        result.like = (status.like * 1.3
            + safe * (40 - config_safe) * p.safe
            + like * config_safe * (config_safe + 4) * 4
            + t_like
            );
        result.value = (result.acc_value
            - result.map_rise * (40 - config_safe) * p.safe
            + result.like
            + field * p.base
            );
        return result;
    }

    TetrisBlockStatus TOJ::spawn(char t, int clear, int spawn_w, int spawn_h, bool is_hold, m_tetris::TetrisMap const &map, Status const &status) const
    {
        TetrisBlockStatus result;
        result.t = t;
        result.x = spawn_w;
        result.y = spawn_h;
        result.r = 0;
        return result;
    }

    size_t TOJ::map_in_danger_(m_tetris::TetrisMap const &map, size_t t, size_t up) const
    {
        if (up >= 19)
        {
            return 1;
        }
        size_t height = 22 - up;
        return map_danger_data_[t].data[0] & map.row[height - 4] | map_danger_data_[t].data[1] & map.row[height - 3] | map_danger_data_[t].data[2] & map.row[height - 2] | map_danger_data_[t].data[3] & map.row[height - 1];
    }
}
