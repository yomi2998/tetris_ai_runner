#include "toj_policy.h"

#include "toj_rule.h"

#include <algorithm>
#include <bit>
#include <print>

namespace toj_policy
{
    Parameters Parameters::production_defaults()
    {
        Parameters parameters;
        double theta[count] = {
            10.507166148, 7.539860726, 13.048099725, 13.388476179, 6.728747539, 9.476881786,
            0.258534525, -0.108269503, 4.394241496, -4.892359035, 0.049148374, 1.586714505,
            8.885878229, -0.006001836, -0.004336234, -2.021765056, -0.951446468, -1.145468832,
            -1.515758227, -0.612910192, -0.476031978, 0.009596827, -0.399212013, -0.855819915,
            -0.418779377, -0.454784178, -1.417493065, 1.050941751, 0.756272086,
        };
        from_theta(theta, parameters);
        return parameters;
    }

    void Parameters::to_theta(Parameters const &parameters, double *out)
    {
        out[0] = parameters.base;
        out[1] = parameters.roof;
        out[2] = parameters.col_trans;
        out[3] = parameters.row_trans;
        out[4] = parameters.hole_count;
        out[5] = parameters.hole_line;
        out[6] = parameters.clear_width;
        out[7] = parameters.wide_2;
        out[8] = parameters.wide_3;
        out[9] = parameters.wide_4;
        out[10] = parameters.safe;
        out[11] = parameters.b2b;
        out[12] = parameters.attack;
        out[13] = parameters.hold_t;
        out[14] = parameters.hold_i;
        out[15] = parameters.waste_t;
        out[16] = parameters.waste_i;
        out[17] = parameters.clear_1;
        out[18] = parameters.clear_2;
        out[19] = parameters.clear_3;
        out[20] = parameters.clear_4;
        out[21] = parameters.t2_slot;
        out[22] = parameters.t3_slot;
        out[23] = parameters.tspin_mini;
        out[24] = parameters.tspin_1;
        out[25] = parameters.tspin_2;
        out[26] = parameters.tspin_3;
        out[27] = parameters.combo;
        out[28] = parameters.ratio;
    }

    void Parameters::from_theta(double const *in, Parameters &parameters)
    {
        parameters.base = in[0];
        parameters.roof = in[1];
        parameters.col_trans = in[2];
        parameters.row_trans = in[3];
        parameters.hole_count = in[4];
        parameters.hole_line = in[5];
        parameters.clear_width = in[6];
        parameters.wide_2 = in[7];
        parameters.wide_3 = in[8];
        parameters.wide_4 = in[9];
        parameters.safe = in[10];
        parameters.b2b = in[11];
        parameters.attack = in[12];
        parameters.hold_t = in[13];
        parameters.hold_i = in[14];
        parameters.waste_t = in[15];
        parameters.waste_i = in[16];
        parameters.clear_1 = in[17];
        parameters.clear_2 = in[18];
        parameters.clear_3 = in[19];
        parameters.clear_4 = in[20];
        parameters.t2_slot = in[21];
        parameters.t3_slot = in[22];
        parameters.tspin_mini = in[23];
        parameters.tspin_1 = in[24];
        parameters.tspin_2 = in[25];
        parameters.tspin_3 = in[26];
        parameters.combo = in[27];
        parameters.ratio = in[28];
    }

    namespace
    {
        constexpr int board_width = Board::width;
        constexpr std::uint32_t edge_mask = 0x3fe;
        constexpr std::uint32_t full_mask = 0x3ff;
        constexpr std::uint32_t side_mask = 0x387;
        constexpr int scan_height = 20;
        constexpr int count_rows = 23;
        constexpr int hole_lookahead = 8;
        constexpr int danger_limit = 19;
        constexpr int danger_slots = 4;
        constexpr int spawn_probe_first = 18;
        constexpr int t_expect_absent = 13;
        constexpr int t_expect_near = 10;
        constexpr int t2_safe_margin = 4;
        constexpr int t3_safe_margin = 10;
        constexpr int hold_window = 20;
        constexpr int field_window = 40;
        constexpr int field_scale = 20;
        constexpr int attack_window = 16;
        constexpr int combo_window = 100;
        constexpr int like_scale = 4;
        constexpr double death_penalty = 999999999.0;
        constexpr double perfect_like = 999;
        constexpr int perfect_attack = 6;
        constexpr double like_decay = 1.3;

        int local_roof(std::uint32_t const *rows)
        {
            for (int y = policy_height - 1; y >= 0; --y)
            {
                if (rows[y] != 0)
                {
                    return y + 1;
                }
            }
            return 0;
        }

        int count_bits(std::uint32_t value)
        {
            return static_cast<int>(std::popcount(value));
        }

        void fill_counts(std::uint32_t const *rows, int roof, std::uint8_t *counts)
        {
            std::fill_n(counts, count_rows, std::uint8_t(0));
            int end = std::min(roof, count_rows);
            for (int y = 0; y < end; ++y)
            {
                counts[y] = static_cast<std::uint8_t>(count_bits(rows[y]));
            }
        }

        int t2_readiness(std::uint32_t row0, std::uint32_t row1, std::uint32_t row2, int count0,
            int count1, int x)
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

        int t3a_readiness(std::uint32_t const *rows, std::uint8_t const *counts, int y, int hole,
            int qualifying, int total)
        {
            int value = total * qualifying;
            if ((rows[y + 4] >> hole) & 1)
            {
                value += total + counts[y + 3];
            }
            else if (((rows[y + 4] >> hole) & 7) == 1 && ((rows[y + 5] >> hole) & 7) == 1
                && ((rows[y + 6] >> hole) & 7) == 1)
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

        int t3b_readiness(std::uint32_t const *rows, std::uint8_t const *counts, int y, int hole,
            int qualifying, int total)
        {
            int value = total * qualifying;
            if ((rows[y + 4] >> hole) & 1)
            {
                value += total + counts[y + 3];
            }
            else if (((rows[y + 4] >> (hole - 2)) & 7) == 4
                && ((rows[y + 5] >> (hole - 2)) & 7) == 4
                && ((rows[y + 6] >> (hole - 2)) & 7) == 4)
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

        void apply_overlay(bool is_t2, bool mirrored, int x, int y, int readiness,
            std::uint32_t *rows)
        {
            if (readiness == 0 || rows == nullptr)
            {
                return;
            }
            if (is_t2)
            {
                rows[y] |= 2u << x;
                rows[y + 1] |= 7u << x;
            }
            else if (mirrored)
            {
                rows[y] |= 1u << x;
                rows[y + 1] |= 3u << (x - 1);
                rows[y + 2] |= 1u << x;
                rows[y + 3] |= 1u << x;
            }
            else
            {
                rows[y] |= 1u << x;
                rows[y + 1] |= 3u << x;
                rows[y + 2] |= 1u << x;
                rows[y + 3] |= 1u << x;
            }
        }

        void init_t_value(std::uint32_t *rows, int roof, std::int16_t &t2_value_ref,
            std::int16_t &t3_value_ref)
        {
            int t2 = 0;
            int t3 = 0;
            std::uint8_t counts[count_rows];
            fill_counts(rows, roof, counts);
            for (int y = 0, end = std::min(scan_height, roof - 2); y < end; ++y)
            {
                int qualifying = (counts[y] == 9) + (counts[y + 1] == 8) + (counts[y + 2] == 9);
                int total3 = counts[y] + counts[y + 1] + counts[y + 2];
                if (counts[y + 2] == 9 && qualifying >= 2 && total3 > 20)
                {
                    int hole = std::countr_zero(~rows[y + 2] & 0x3ffu);
                    std::uint32_t straight = ~rows[y] & ~(rows[y + 1] | (rows[y + 1] >> 1))
                        & ~(rows[y + 3] | (rows[y + 3] >> 1) | (rows[y + 3] >> 2))
                        & ~((rows[y + 4] >> 1) | (rows[y + 4] >> 2)) & 0xfeu;
                    if ((straight >> hole) & 1)
                    {
                        int value = t3a_readiness(rows, counts, y, hole, qualifying, total3);
                        t3 += static_cast<std::int16_t>(value);
                        apply_overlay(false, false, hole, y, value, rows);
                        y += 2;
                        continue;
                    }
                    std::uint32_t mirrored = ~rows[y] & ~(rows[y + 1] | (rows[y + 1] << 1))
                        & ~(rows[y + 3] | (rows[y + 3] << 1) | (rows[y + 3] << 2))
                        & ~((rows[y + 4] << 1) | (rows[y + 4] << 2)) & 0x1fcu;
                    if ((mirrored >> hole) & 1)
                    {
                        int value = t3b_readiness(rows, counts, y, hole, qualifying, total3);
                        t3 += static_cast<std::int16_t>(value);
                        apply_overlay(false, true, hole, y, value, rows);
                        y += 2;
                        continue;
                    }
                }
                std::uint32_t candidates = (rows[y] & (rows[y] >> 2)) & ~(rows[y] >> 1)
                    & ~(rows[y + 1] | (rows[y + 1] >> 1) | (rows[y + 1] >> 2)) & 0xffu;
                if (candidates == 0)
                {
                    continue;
                }
                int total2 = counts[y] + counts[y + 1];
                if (total2 <= 10)
                {
                    t2 += static_cast<std::int16_t>(count_bits(candidates) * total2);
                    continue;
                }
                int x = std::countr_zero(candidates);
                int value =
                    t2_readiness(rows[y], rows[y + 1], rows[y + 2], counts[y], counts[y + 1], x);
                t2 += static_cast<std::int16_t>(value);
                apply_overlay(true, false, x, y, value, rows);
                ++y;
            }
            t2_value_ref = static_cast<std::int16_t>(t2);
            t3_value_ref = static_cast<std::int16_t>(t3);
        }

        int combo_attack(int const *table, int table_max, int combo)
        {
            return table[std::min<int>(table_max - 1, combo)];
        }
    }

    void Policy::init(Config const *config)
    {
        config_ = config;
        for (int piece_index = 0; piece_index < 7; ++piece_index)
        {
            Piece piece = static_cast<Piece>(piece_index);
            auto cells = tetris::toj::cells(piece,
                tetris::Placement::unchecked(tetris::toj::spawn_x, tetris::toj::spawn_y, 0));
            std::uint32_t rows[policy_height] = {};
            if (cells.has_value())
            {
                for (auto [x, y] : *cells)
                {
                    if (x >= 0 && x < board_width && y >= 0 && y < policy_height)
                    {
                        rows[y] |= 1u << x;
                    }
                }
            }
            for (int slot = 0; slot < danger_slots; ++slot)
            {
                danger_[piece_index][slot] = rows[spawn_probe_first + slot];
            }
            for (int y = 0; y < danger_slots - 1; ++y)
            {
                danger_[piece_index][y + 1] |= danger_[piece_index][y];
            }
        }
    }

    Evaluation Policy::evaluate(Board const &result) const
    {
        std::uint32_t rows[policy_height] = {};
        for (int y = 0; y < policy_height; ++y)
        {
            rows[y] = result.row(y);
        }
        int roof = local_roof(rows);
        int side_roof = roof;
        while (side_roof > 0 && (rows[side_roof - 1] & side_mask) == 0)
        {
            --side_roof;
        }
        Evaluation out;
        init_t_value(rows, roof, out.t2_value, out.t3_value);
        std::size_t col_trans = static_cast<std::size_t>(2 * (policy_height - roof));
        std::size_t row_trans = roof == policy_height ? 0 : static_cast<std::size_t>(board_width);
        for (int y = 0; y < roof; ++y)
        {
            col_trans += static_cast<std::size_t>(!(rows[y] & 1u)
                + !((rows[y] >> (board_width - 1)) & 1u)
                + count_bits((rows[y] ^ (rows[y] << 1)) & edge_mask));
            if (y != 0)
            {
                row_trans += static_cast<std::size_t>(count_bits(rows[y - 1] ^ rows[y]));
            }
        }
        row_trans += static_cast<std::size_t>(count_bits(full_mask & ~rows[0]));
        if (roof != 0)
        {
            row_trans += static_cast<std::size_t>(count_bits(
                roof == policy_height ? full_mask & ~rows[roof - 1] : rows[roof - 1]));
        }
        int hole_count = 0;
        int hole_line = 0;
        int wide[10] = {};
        std::uint32_t line_cover = 0;
        int clear_width = 0;
        int wide_count = board_width - 1;
        for (int y = roof - 1; y >= 0; --y)
        {
            line_cover |= rows[y];
            std::uint32_t line_hole = line_cover ^ rows[y];
            if (line_hole != 0)
            {
                hole_count += count_bits(line_hole);
                ++hole_line;
                for (int hy = y + 1, hy_max = std::min(roof, hy + hole_lookahead); hy < hy_max;
                    ++hy)
                {
                    if ((line_hole & rows[hy]) > 0)
                    {
                        clear_width += (board_width - count_bits(rows[hy])) * hy;
                    }
                }
            }
            wide_count = std::min<int>(wide_count, board_width - count_bits(line_cover));
            if (hole_line == 0)
            {
                ++wide[wide_count];
            }
        }
        auto const &p = config_->parameters;
        out.value = (0. - side_roof * p.roof - col_trans * p.col_trans - row_trans * p.row_trans
            - hole_count * p.hole_count - hole_line * p.hole_line - clear_width * p.clear_width
            + wide[2] * p.wide_2 + wide[3] * p.wide_3 + wide[4] * p.wide_4);
        return out;
    }

    bool Policy::is_lockout(Piece piece, Placement placement)
    {
        auto lowest = tetris::toj::lowest_occupied_row(piece, placement);
        return lowest.has_value() && *lowest >= lockout_row;
    }

    std::uint32_t Policy::danger_bits(Piece piece, int slot) const
    {
        return danger_[static_cast<int>(piece)][slot];
    }

    int Policy::scan_safe_rows(std::uint32_t const *rows, Piece next) const
    {
        int piece_index = static_cast<int>(next);
        int safe = 0;
        while (true)
        {
            int up = safe + 1;
            std::uint32_t threat = 0;
            if (up >= danger_limit)
            {
                threat = 1;
            }
            else
            {
                int height = spawn_frame_height - up;
                threat = danger_[piece_index][0] & rows[height - 4]
                    | danger_[piece_index][1] & rows[height - 3]
                    | danger_[piece_index][2] & rows[height - 2]
                    | danger_[piece_index][3] & rows[height - 1];
            }
            if (threat != 0)
            {
                break;
            }
            ++safe;
        }
        return safe;
    }

    int8_t Policy::safe_margin(Board const &board, Piece next) const
    {
        std::uint32_t rows[policy_height] = {};
        for (int y = 0; y < policy_height; ++y)
        {
            rows[y] = board.row(y);
        }
        return static_cast<int8_t>(scan_safe_rows(rows, next));
    }

    int Policy::expected_t_distance(DecisionContext const &context)
    {
        if (context.hold.has_value() && *context.hold == tetris::Piece::T)
        {
            return 0;
        }
        for (std::size_t k = 0; k < context.next.size(); ++k)
        {
            if (context.next[k] == tetris::Piece::T)
            {
                return static_cast<int>(k);
            }
        }
        return t_expect_absent;
    }

    State Policy::transition(Piece piece, Candidate candidate, Outcome outcome, Board const &result,
        State const &parent, DecisionContext const &context, Evaluation const &evaluation) const
    {
        return transition_known_lockout(piece, candidate, outcome, result, parent, context,
            evaluation, is_lockout(piece, candidate.placement), expected_t_distance(context));
    }

    State Policy::transition_known_lockout(Piece piece, Candidate candidate, Outcome outcome,
        Board const &result, State const &parent, DecisionContext const &context,
        Evaluation const &evaluation, bool lockout, int t_expect) const
    {
        char piece_char = tetris::to_char(piece);
        tetris::SpinType spin = outcome.spin;
        int clear = outcome.clear_count;
        State next = parent;
        int attack = 0;
        int t_attack = 0;
        double like = 0;
        double dislike = 0;
        auto update_like = [&](double v) {
            if (v > 0)
            {
                like += v;
            }
            else
            {
                dislike -= v;
            }
        };
        int safe = 0;
        if (lockout)
        {
            safe = -1;
        }
        else if (!context.next.empty())
        {
            std::uint32_t rows[spawn_frame_height - 1] = {};
            for (int y = 0; y < spawn_frame_height - 1; ++y)
            {
                rows[y] = result.row(y);
            }
            safe = scan_safe_rows(rows, context.next[0]);
        }
        else
        {
            std::uint32_t rows[policy_height] = {};
            for (int y = 0; y < policy_height; ++y)
            {
                rows[y] = result.row(y);
            }
            safe = spawn_frame_height - local_roof(rows);
        }
        auto const &p = config_->parameters;
        switch (clear)
        {
        case 0:
            next.combo = 0;
            if (parent.under_attack > 0)
            {
                next.map_rise = parent.under_attack;
                if (next.map_rise > safe)
                {
                    next.death = 1;
                }
                next.under_attack = 0;
            }
            update_like((piece_char == 'I') * p.waste_i);
            update_like((piece_char == 'T') * p.waste_t);
            break;
        case 1:
            if (spin == tetris::SpinType::Mini)
            {
                attack = 1 + parent.b2b;
                update_like(p.tspin_mini);
            }
            else if (spin == tetris::SpinType::Full)
            {
                attack = 2 + parent.b2b;
                update_like(p.tspin_1);
                t_attack = 1;
            }
            else
            {
                update_like((piece_char == 'I') * p.waste_i);
                update_like((piece_char == 'T') * p.waste_t);
                update_like(p.clear_1);
            }
            attack += combo_attack(config_->combo_table, config_->combo_table_max, ++next.combo);
            next.b2b = spin != tetris::SpinType::None;
            break;
        case 2:
            if (spin != tetris::SpinType::None)
            {
                attack += 4 + parent.b2b;
                next.b2b = true;
                update_like(p.tspin_2);
                t_attack = 1;
            }
            else
            {
                ++attack;
                next.b2b = false;
                update_like((piece_char == 'I') * p.waste_i);
                update_like((piece_char == 'T') * p.waste_t);
                update_like(p.clear_2);
            }
            attack += combo_attack(config_->combo_table, config_->combo_table_max, ++next.combo);
            break;
        case 3:
            if (spin != tetris::SpinType::None)
            {
                attack = 6 + parent.b2b * 2;
                next.b2b = true;
                update_like(p.tspin_3);
                t_attack = 1;
            }
            else
            {
                attack += 2;
                next.b2b = false;
                update_like((piece_char == 'I') * p.waste_i);
                update_like(p.clear_3);
            }
            attack += combo_attack(config_->combo_table, config_->combo_table_max, ++next.combo);
            break;
        case 4:
            next.b2b = true;
            attack = combo_attack(config_->combo_table, config_->combo_table_max, ++next.combo) + 4
                + parent.b2b;
            update_like(p.clear_4);
            break;
        }
        next.under_attack = std::max(0, next.under_attack - attack);
        int config_safe = std::max(0, config_->safe - next.under_attack - next.map_rise);
        if (context.hold.has_value() && *context.hold == tetris::Piece::T)
        {
            if (spin == tetris::SpinType::None)
            {
                update_like(static_cast<double>(hold_window + config_safe) * p.hold_t);
            }
        }
        else if (context.hold.has_value() && *context.hold == tetris::Piece::I)
        {
            if (clear != 4)
            {
                update_like(static_cast<double>(field_window - config_safe) * p.hold_i);
            }
        }
        safe -= next.map_rise;
        if (safe < 0)
        {
            next.death = 1;
            safe = 0;
        }
        if (result.empty() && next.map_rise == 0)
        {
            like += perfect_like;
            attack += perfect_attack;
        }
        double field =
            evaluation.value * static_cast<double>(field_window - config_safe) / field_scale;
        double t_like = 0;
        double t_dislike = 0;
        if (t_attack == 0)
        {
            double t2_safe = std::max(0, config_safe - t2_safe_margin);
            double t3_safe = std::max(0, config_safe - t3_safe_margin);
            if (evaluation.t2_value > parent.t2_value)
            {
                t_like += (evaluation.t2_value - parent.t2_value) * t2_safe
                    * std::max(t_expect_near - t_expect, 5) * p.t2_slot;
            }
            else
            {
                t_dislike += (parent.t2_value - evaluation.t2_value) * t2_safe * 3 * p.t2_slot;
            }
            if (evaluation.t3_value > parent.t3_value)
            {
                t_like += (evaluation.t3_value - parent.t3_value) * t3_safe
                    * std::max(t_expect_near - t_expect, 4) * (3 + next.b2b) * p.t3_slot;
            }
            else
            {
                t_dislike += (parent.t3_value - evaluation.t3_value) * t3_safe * 4 * p.t3_slot;
            }
        }
        next.t2_value = evaluation.t2_value;
        next.t3_value = evaluation.t3_value;
        next.acc_value += (0 + attack * (config_safe + attack_window) * p.attack
            + combo_attack(config_->combo_table, config_->combo_table_max, next.combo) * next.combo
                * (combo_window - config_safe) * p.combo
            + (next.b2b - parent.b2b) * (config_safe + attack_window) * p.b2b - t_dislike
            - dislike * config_safe * (config_safe + like_scale) * like_scale
            - next.death * death_penalty);
        next.like = (parent.like * like_decay + safe * (field_window - config_safe) * p.safe
            + like * config_safe * (config_safe + like_scale) * like_scale + t_like);
        next.value = (next.acc_value - next.map_rise * (field_window - config_safe) * p.safe
            + next.like + field * p.base);
        return next;
    }
}
