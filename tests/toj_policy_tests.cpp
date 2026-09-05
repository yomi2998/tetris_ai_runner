#include "toj_policy.h"
#include "toj_rule.h"

#include "tetris_core.h"
#include "rule_toj.h"
#include "ai_zzz.h"
#include "search_tspin.h"
#include "random.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <print>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

#ifndef TETRIS_POLICY_FIXTURE_DIR
#define TETRIS_POLICY_FIXTURE_DIR "docs/phase5/fixtures"
#endif

#ifndef TETRIS_POLICY_FIXTURE
#define TETRIS_POLICY_FIXTURE "toj_policy_v2.csv"
#endif

namespace
{
    std::size_t checks = 0;
    std::size_t failures = 0;

    void check(bool ok, std::string const &what)
    {
        ++checks;
        if (!ok)
        {
            ++failures;
            std::println(std::cerr, "FAIL: {}", what);
        }
    }

    std::vector<std::string> split_fields(std::string const &line)
    {
        std::vector<std::string> out;
        std::istringstream in(line);
        std::string field;
        while (in >> field)
        {
            out.push_back(field);
        }
        return out;
    }

    bool parse_int(std::string const &text, long long &value)
    {
        try
        {
            std::size_t used = 0;
            value = std::stoll(text, &used, 10);
            return used == text.size();
        }
        catch (...)
        {
            return false;
        }
    }

    bool parse_hex16(std::string const &text, std::uint64_t &value)
    {
        if (text.size() != 16)
        {
            return false;
        }
        try
        {
            std::size_t used = 0;
            value = std::stoull(text, &used, 16);
            return used == text.size();
        }
        catch (...)
        {
            return false;
        }
    }

    bool parse_rows(std::string const &text, std::vector<std::uint32_t> &rows)
    {
        rows.clear();
        std::string cell;
        std::istringstream in(text);
        while (std::getline(in, cell, ','))
        {
            if (cell.empty())
            {
                continue;
            }
            try
            {
                std::size_t used = 0;
                unsigned long bits = std::stoul(cell, &used, 16);
                if (used != cell.size() || bits > 0x3ff)
                {
                    return false;
                }
                rows.push_back(static_cast<std::uint32_t>(bits));
            }
            catch (...)
            {
                return false;
            }
        }
        return rows.size() == 40;
    }

    struct Tallies
    {
        std::size_t cases = 0;
        bool clear_seen[5] = {};
        bool spin_seen[3] = {};
        bool full_single = false;
        bool full_double = false;
        bool full_triple = false;
        bool t2_gain = false;
        bool t3_gain = false;
        bool death = false;
        bool empty_result = false;
        bool arrival[2] = {};
        bool last_rotate[2] = {};
        bool ready[2] = {};
        bool mini_ready[2] = {};
        bool tall_landing = false;
        bool parent_acc = false;
        bool parent_like = false;
        bool combo_seen[5] = {};
        bool b2b_seen[2] = {};
        bool under_seen[3] = {};
        bool maprise_seen[2] = {};
        bool hold_empty = false;
        bool hold_t = false;
        bool hold_i = false;
        bool hold_other = false;
        bool next_full = false;
        bool next_single_t = false;
        bool next_absent = false;
        bool next_empty = false;
        bool next_short = false;
        bool cfg_zero = false;
        bool cfg_five = false;
        bool cfg_sixteen = false;
        bool full_double_hot = false;
        bool perfect_bonus_hot = false;
    };

    using Engine = m_tetris::TetrisEngine<rule_toj::TetrisRule, ai_zzz::TOJ, search_tspin::Search>;

    int const combo_table[] = { 0, 0, 0, 1, 1, 2, 2, 3, 3, 4 };

    Engine make_engine()
    {
        Engine engine;
        if (!engine.prepare(10, 40))
        {
            std::println(std::cerr, "engine.prepare failed");
            std::exit(1);
        }
        engine.search_config()->allow_rotate_move = false;
        engine.search_config()->allow_180 = true;
        engine.search_config()->allow_d = true;
        engine.search_config()->allow_nont_d = false;
        engine.search_config()->is_20g = false;
        engine.search_config()->last_rotate = false;
        engine.ai_config()->table = combo_table;
        engine.ai_config()->table_max = 10;
        engine.ai_config()->safe = 0;
        double theta[ai_zzz::TOJ::NUM_PARAMS];
        ai_zzz::TOJ::production_default_theta(theta);
        ai_zzz::TOJ::theta_to_param(theta, engine.ai_config()->param);
        return engine;
    }

    void rebuild_legacy_metadata(m_tetris::TetrisMap &map)
    {
        map.roof = 0;
        map.count = 0;
        for (std::size_t x = 0; x < 10; ++x)
        {
            map.top[x] = 0;
        }
        for (std::size_t y = 0; y < 40; ++y)
        {
            for (std::size_t x = 0; x < 10; ++x)
            {
                if (map.full(x, y))
                {
                    map.top[x] = static_cast<std::int32_t>(y + 1);
                    map.roof = std::max<std::int32_t>(map.roof, static_cast<std::int32_t>(y + 1));
                    ++map.count;
                }
            }
        }
    }

    m_tetris::TetrisMap legacy_map(std::vector<std::uint32_t> const &rows)
    {
        m_tetris::TetrisMap map(10, 40);
        for (std::size_t y = 0; y < 40; ++y)
        {
            map.row[y] = rows[y];
        }
        rebuild_legacy_metadata(map);
        return map;
    }

    tetris::Board value_board(std::vector<std::uint32_t> const &rows)
    {
        std::array<std::uint16_t, 48> wide = {};
        for (std::size_t y = 0; y < 40; ++y)
        {
            wide[y] = static_cast<std::uint16_t>(rows[y]);
        }
        return tetris::Board::from_rows(wide);
    }

    std::uint64_t ulp_distance(double a, double b)
    {
        if (a == b)
        {
            return 0;
        }
        if (!std::isfinite(a) || !std::isfinite(b))
        {
            return UINT64_MAX;
        }
        std::uint64_t ua = std::bit_cast<std::uint64_t>(a);
        std::uint64_t ub = std::bit_cast<std::uint64_t>(b);
        if ((ua >> 63) != (ub >> 63))
        {
            return UINT64_MAX;
        }
        return ua > ub ? ua - ub : ub - ua;
    }

    struct ParityMax
    {
        std::uint64_t eval_value = 0;
        std::uint64_t out_acc = 0;
        std::uint64_t out_like = 0;
        std::uint64_t out_value = 0;
    };

    constexpr std::uint64_t frozen_allowance = 5;

    bool frozen_float_match(double got, std::uint64_t want_bits)
    {
        if (!std::isfinite(got))
        {
            return false;
        }
        double want = std::bit_cast<double>(want_bits);
        if (!std::isfinite(want))
        {
            return false;
        }
        return ulp_distance(got, want) <= frozen_allowance;
    }

    bool state_matches_frozen(toj_policy::State const &got, std::int8_t death, std::int8_t combo,
        std::int8_t b2b, std::int8_t under_attack, std::int8_t map_rise, std::int16_t t2,
        std::int16_t t3, std::uint64_t acc_bits, std::uint64_t like_bits, std::uint64_t value_bits)
    {
        return got.death == death && got.combo == combo && got.b2b == b2b
            && got.under_attack == under_attack && got.map_rise == map_rise
            && got.t2_value == t2 && got.t3_value == t3
            && frozen_float_match(got.acc_value, acc_bits)
            && frozen_float_match(got.like, like_bits)
            && frozen_float_match(got.value, value_bits);
    }

    bool eval_matches_frozen(toj_policy::Evaluation const &got, long long t2, long long t3,
        std::uint64_t value_bits)
    {
        return got.t2_value == t2 && got.t3_value == t3
            && frozen_float_match(got.value, value_bits);
    }

    void run_negative_oracle_tests()
    {
        toj_policy::State exact;
        check(state_matches_frozen(exact, 0, 0, 0, 0, 0, 0, 0,
            std::bit_cast<std::uint64_t>(0.0), std::bit_cast<std::uint64_t>(0.0),
            std::bit_cast<std::uint64_t>(0.0)),
            "uncorrupted frozen state matches");
        check(!state_matches_frozen(exact, 9, 0, 0, 0, 0, 0, 0,
            std::bit_cast<std::uint64_t>(0.0), std::bit_cast<std::uint64_t>(0.0),
            std::bit_cast<std::uint64_t>(0.0)),
            "corrupted frozen integer fails");
        check(!state_matches_frozen(exact, 0, 0, 0, 0, 0, 0, 0,
            std::bit_cast<std::uint64_t>(std::numeric_limits<double>::infinity()),
            std::bit_cast<std::uint64_t>(0.0), std::bit_cast<std::uint64_t>(0.0)),
            "nonfinite frozen float fails");
        toj_policy::Evaluation eval_exact;
        check(eval_matches_frozen(eval_exact, 0, 0, std::bit_cast<std::uint64_t>(0.0)),
            "uncorrupted frozen evaluation matches");
        check(!eval_matches_frozen(eval_exact, 0, 0,
            std::bit_cast<std::uint64_t>(std::numeric_limits<double>::infinity())),
            "nonfinite frozen evaluation fails");
        std::println("negative oracle: corrupted frozen outputs fail the gate");
    }

    search_tspin::Search::TSpinType legacy_spin(long long spin_eff)
    {
        if (spin_eff == 1)
        {
            return search_tspin::Search::TSpinType::TSpin;
        }
        if (spin_eff == 2)
        {
            return search_tspin::Search::TSpinType::TSpinMini;
        }
        return search_tspin::Search::TSpinType::None;
    }

    tetris::SpinType value_spin(long long spin_eff)
    {
        if (spin_eff == 1)
        {
            return tetris::SpinType::Full;
        }
        if (spin_eff == 2)
        {
            return tetris::SpinType::Mini;
        }
        return tetris::SpinType::None;
    }

    void run_theta_parity()
    {
        double theta[ai_zzz::TOJ::NUM_PARAMS];
        ai_zzz::TOJ::production_default_theta(theta);
        ai_zzz::TOJ::Param legacy_param;
        ai_zzz::TOJ::theta_to_param(theta, legacy_param);
        toj_policy::Parameters fresh = toj_policy::Parameters::production_defaults();
        double round_trip[toj_policy::Parameters::count];
        toj_policy::Parameters::to_theta(fresh, round_trip);
        static_assert(std::is_standard_layout_v<ai_zzz::TOJ::Param>);
        static_assert(std::is_standard_layout_v<toj_policy::Parameters>);
        static_assert(sizeof(ai_zzz::TOJ::Param) == sizeof(toj_policy::Parameters));
        bool exact = std::memcmp(&legacy_param, &fresh, sizeof(fresh)) == 0;
        for (std::size_t k = 0; k < toj_policy::Parameters::count; ++k)
        {
            if (round_trip[k] != theta[k])
            {
                exact = false;
            }
        }
        check(exact, "production parameters match the legacy contract exactly");
        check(toj_policy::Parameters::count == ai_zzz::TOJ::NUM_PARAMS,
            "parameter count preserves the 29 value contract");
    }

    void run_danger_parity(Engine &engine, toj_policy::Policy const &policy)
    {
        for (char const *p = "TZSJLOI"; *p; ++p)
        {
            auto piece = tetris::try_from_char(*p);
            check(piece.has_value(), "danger probe piece converts");
            m_tetris::TetrisMap map(10, 40);
            engine.context()->generate(*p)->attach(engine.context().get(), map);
            std::uint32_t expected[4] = { map.row[18], map.row[19], map.row[20], map.row[21] };
            for (int y = 0; y < 3; ++y)
            {
                expected[y + 1] |= expected[y];
            }
            bool match = true;
            for (int slot = 0; slot < 4; ++slot)
            {
                if (policy.danger_bits(*piece, slot) != expected[slot])
                {
                    match = false;
                }
            }
            check(match, std::string("danger mask matches legacy spawn rows for ") + *p);
        }
    }

    void run_height_contract(Engine &engine, toj_policy::Policy const &policy)
    {
        check(toj_policy::policy_height == 40, "policy height is named as 40");
        check(toj_policy::lockout_row == 20, "lockout row is named as 20");
        m_tetris::TetrisMap map(10, 40);
        map.row[0] = 0x1f;
        map.row[1] = 0x155;
        rebuild_legacy_metadata(map);
        std::vector<std::uint32_t> rows(40, 0);
        rows[0] = 0x1f;
        rows[1] = 0x155;
        tetris::Board clean = value_board(rows);
        std::array<std::uint16_t, 48> dirty_wide = {};
        dirty_wide[0] = 0x1f;
        dirty_wide[1] = 0x155;
        for (int y = 40; y < 48; ++y)
        {
            dirty_wide[y] = 0x3ff;
        }
        tetris::Board dirty = tetris::Board::from_rows(dirty_wide);
        auto before = policy.evaluate(clean);
        auto after = policy.evaluate(dirty);
        check(before.value == after.value && before.t2_value == after.t2_value
            && before.t3_value == after.t3_value,
            "evaluation ignores rows above the policy height");
        (void)engine;
    }

    void run_lockout_contract()
    {
        auto vertical_i = tetris::try_from_char('I');
        check(vertical_i.has_value(), "lockout probe piece converts");
        auto tucked = tetris::Placement::unchecked(4, 17, 1);
        auto tucked_lowest = tetris::toj::lowest_occupied_row(*vertical_i, tucked);
        check(tucked_lowest.has_value() && *tucked_lowest == 17,
            "vertical I at anchor 17 has lowest row 17");
        check(!toj_policy::Policy::is_lockout(*vertical_i, tucked),
            "lockout stays clear when minos sit below row 20");
        auto buried = tetris::Placement::unchecked(4, 20, 1);
        auto buried_lowest = tetris::toj::lowest_occupied_row(*vertical_i, buried);
        check(buried_lowest.has_value() && *buried_lowest == 20,
            "vertical I at anchor 20 has lowest row 20");
        check(toj_policy::Policy::is_lockout(*vertical_i, buried),
            "lockout fires when the lowest mino row reaches 20");
        for (char const *p = "TZSJLOI"; *p; ++p)
        {
            auto piece = tetris::try_from_char(*p);
            check(piece.has_value(), "lockout sweep piece converts");
            for (int r = 0; r < 4; ++r)
            {
                auto placement =
                    tetris::toj::ExternalPoseTransform::to_placement(*piece, 2, 30, r);
                if (!placement.has_value())
                {
                    continue;
                }
                for (int floor = 19; floor <= 20; ++floor)
                {
                    std::array<std::uint16_t, 48> rows = {};
                    rows[static_cast<std::size_t>(floor - 1)] = 0x3ff;
                    tetris::Board board = tetris::Board::from_rows(rows);
                    int rest = floor;
                    while (rest < 47
                        && !tetris::toj::fits(*piece,
                            tetris::Placement::unchecked(placement->x(), rest, r), board))
                    {
                        ++rest;
                    }
                    tetris::Placement rest_pose =
                        tetris::Placement::unchecked(placement->x(), rest, r);
                    if (!tetris::toj::fits(*piece, rest_pose, board))
                    {
                        continue;
                    }
                    auto lowest = tetris::toj::lowest_occupied_row(*piece, rest_pose);
                    check(lowest.has_value(), "lockout sweep pose has a lowest row");
                    if (!lowest.has_value())
                    {
                        continue;
                    }
                    check(toj_policy::Policy::is_lockout(*piece, rest_pose)
                        == (*lowest >= 20),
                        std::string("lockout follows the lowest row for ") + *p + " r"
                            + std::to_string(r));
                }
            }
        }
    }

    void run_synthetic_formula_tests()
    {
        constexpr std::uint64_t ulp_allowance = 5;
        Engine engine = make_engine();
        engine.ai_config()->safe = 5;
        ai_zzz::TOJ &legacy = *engine.ai();
        toj_policy::Config config;
        config.combo_table = combo_table;
        config.combo_table_max = 10;
        config.safe = 5;
        config.parameters = toj_policy::Parameters::production_defaults();
        toj_policy::Policy policy;
        policy.init(&config);
        {
            m_tetris::TetrisMap board(10, 40);
            m_tetris::TetrisNode node;
            bool made = engine.context()->create(
                m_tetris::TetrisBlockStatus('T', 4, 10, 0), node);
            check(made, "synthetic triple node rebuilds");
            search_tspin::Search::TetrisNodeWithTSpinType ex(&node);
            ex.is_check = true;
            ex.is_last_rotate = false;
            ex.is_ready = false;
            ex.is_mini_ready = false;
            ex.type = search_tspin::Search::TSpinType::None;
            ai_zzz::TOJ::Status parent;
            std::memset(&parent, 0, sizeof(parent));
            std::string next("T");
            m_tetris::TetrisContext::Env env{ next.c_str(), next.size(), 'T', ' ', false };
            auto legacy_eval = legacy.eval(ex, board, board);
            auto legacy_out = legacy.get(ex, legacy_eval, 3, board, 0, parent, env);
            auto piece = tetris::try_from_char('T');
            auto placement =
                tetris::toj::ExternalPoseTransform::to_placement(*piece, 4, 10, 0);
            check(placement.has_value(), "synthetic triple maps to a value candidate");
            std::array<std::uint16_t, 48> wide = {};
            tetris::Board value_board = tetris::Board::from_rows(wide);
            toj_policy::State value_parent;
            std::memset(&value_parent, 0, sizeof(value_parent));
            std::vector<tetris::Piece> next_pieces{ *piece };
            toj_policy::DecisionContext context;
            context.next = next_pieces;
            context.hold = std::nullopt;
            context.used_hold = false;
            context.depth = 0;
            tetris::Outcome outcome;
            outcome.spin = tetris::SpinType::None;
            outcome.clear_count = 3;
            outcome.lockout = false;
            toj_policy::Evaluation evaluation = policy.evaluate(value_board);
            toj_policy::State got = policy.transition(*piece,
                tetris::Candidate{ *placement, tetris::ArrivalClass::Normal }, outcome,
                value_board, value_parent, context, evaluation);
            bool match = got.death == legacy_out.death && got.combo == legacy_out.combo
                && got.b2b == legacy_out.b2b && got.under_attack == legacy_out.under_attack
                && got.map_rise == legacy_out.map_rise && got.t2_value == legacy_out.t2_value
                && got.t3_value == legacy_out.t3_value
                && ulp_distance(got.acc_value, legacy_out.acc_value) <= ulp_allowance
                && ulp_distance(got.like, legacy_out.like) <= ulp_allowance
                && ulp_distance(got.value, legacy_out.value) <= ulp_allowance;
            check(match, "synthetic non-spin triple matches at nonzero safety");
        }
        {
            std::array<std::uint16_t, 48> high_wide = {};
            high_wide[40] = 0x155;
            high_wide[41] = 0x2aa;
            tetris::Board high = tetris::Board::from_rows(high_wide);
            std::array<std::uint16_t, 48> empty_wide = {};
            tetris::Board empty = tetris::Board::from_rows(empty_wide);
            auto piece = tetris::try_from_char('O');
            tetris::Placement pose = tetris::Placement::unchecked(4, 0, 0);
            tetris::Candidate candidate{ pose, tetris::ArrivalClass::Normal };
            tetris::Outcome outcome;
            outcome.spin = tetris::SpinType::None;
            outcome.clear_count = 0;
            outcome.lockout = false;
            toj_policy::State parent;
            std::memset(&parent, 0, sizeof(parent));
            std::vector<tetris::Piece> next_pieces;
            for (char c : std::string("IOSZLJT"))
            {
                next_pieces.push_back(*tetris::try_from_char(c));
            }
            toj_policy::DecisionContext context;
            context.next = next_pieces;
            context.hold = std::nullopt;
            context.used_hold = false;
            context.depth = 0;
            toj_policy::Evaluation high_eval = policy.evaluate(high);
            toj_policy::Evaluation empty_eval = policy.evaluate(empty);
            check(high_eval.value == empty_eval.value && high_eval.t2_value == empty_eval.t2_value
                && high_eval.t3_value == empty_eval.t3_value,
                "high rows leave evaluation at the horizon value");
            toj_policy::State got_high =
                policy.transition(*piece, candidate, outcome, high, parent, context, high_eval);
            toj_policy::State got_empty =
                policy.transition(*piece, candidate, outcome, empty, parent, context, empty_eval);
            check(got_high.death == got_empty.death && got_high.combo == got_empty.combo
                && got_high.b2b == got_empty.b2b
                && got_high.under_attack == got_empty.under_attack
                && got_high.map_rise == got_empty.map_rise
                && got_high.t2_value == got_empty.t2_value
                && got_high.t3_value == got_empty.t3_value,
                "high rows earn no perfect-clear state");
            check(got_high.acc_value < got_empty.acc_value
                && got_high.like < got_empty.like && got_high.value < got_empty.value,
                "high rows miss exactly the perfect-clear preference");
        }
        std::println("synthetic formula: non-spin triple and high-row perfect clear");
    }

    void check_contract_pair(std::string const &dir)
    {
        std::vector<std::string> names{ "toj_policy_v2.csv", "toj_policy_v2_nofma.csv" };
        std::vector<std::vector<std::string>> files;
        for (auto const &name : names)
        {
            std::ifstream file(dir + "/" + name);
            check(file.good(), "contract file opens: " + name);
            if (!file.good())
            {
                return;
            }
            std::vector<std::string> lines;
            std::string line;
            while (std::getline(file, line))
            {
                if (!line.empty() && line[0] != '#')
                {
                    lines.push_back(line);
                }
            }
            files.push_back(std::move(lines));
        }
        check(files[0].size() == files[1].size() && !files[0].empty(),
            "contract files hold the same case count");
        if (files[0].size() != files[1].size() || files[0].empty())
        {
            return;
        }
        std::size_t float_cells = 0;
        for (std::size_t k = 0; k < files[0].size(); ++k)
        {
            auto left = split_fields(files[0][k]);
            auto right = split_fields(files[1][k]);
            if (left.size() != 46 || right.size() != 46 || left[0] != right[0])
            {
                check(false, "contract pair aligns case " + std::to_string(k));
                return;
            }
            for (int f = 0; f < 46; ++f)
            {
                bool is_float = f == 23 || f == 24 || f == 25 || f == 31 || f == 41 || f == 42
                    || f == 43;
                if (is_float)
                {
                    std::uint64_t a = 0;
                    std::uint64_t b = 0;
                    if (parse_hex16(left[f], a) && parse_hex16(right[f], b) && a != b)
                    {
                        ++float_cells;
                    }
                    continue;
                }
                if (left[f] != right[f])
                {
                    check(false, "contract pair shares non-floating fields");
                    return;
                }
            }
        }
        check(float_cells > 0, "contract files differ in proven float cells");
        std::println("contract pair: {} cases share all non-floating fields", files[0].size());
    }

    void run_parity(std::string const &path)
    {
        Engine engine = make_engine();
        ai_zzz::TOJ &legacy = *engine.ai();
        toj_policy::Config config;
        config.combo_table = combo_table;
        config.combo_table_max = 10;
        config.safe = 0;
        config.parameters = toj_policy::Parameters::production_defaults();
        toj_policy::Policy policy;
        policy.init(&config);
        run_theta_parity();
        run_danger_parity(engine, policy);
        run_height_contract(engine, policy);
        run_lockout_contract();
        std::ifstream file(path);
        check(file.good(), "parity pass reopens the fixture");
        if (!file.good())
        {
            return;
        }
        ParityMax peak;
        std::size_t agree_count = 0;
        std::size_t divergent_count = 0;
        std::string line;
        while (std::getline(file, line))
        {
            if (line.empty() || line[0] == '#')
            {
                continue;
            }
            auto fields = split_fields(line);
            if (fields.size() != 46)
            {
                check(false, "parity line holds 46 fields");
                continue;
            }
            long long id = 0;
            long long x = 0;
            long long y = 0;
            long long r = 0;
            long long spin_eff = 0;
            long long clear = 0;
            long long depth = 0;
            long long cfg_safe = 0;
            std::uint64_t eval_bits = 0;
            std::uint64_t acc_bits = 0;
            std::uint64_t like_bits = 0;
            std::uint64_t value_bits = 0;
            std::uint64_t parent_acc = 0;
            std::uint64_t parent_like = 0;
            std::uint64_t parent_value = 0;
            std::vector<std::uint32_t> src_rows;
            std::vector<std::uint32_t> result_rows;
            bool parsed = parse_int(fields[0], id) && parse_int(fields[3], x) && parse_int(fields[4], y)
                && parse_int(fields[5], r) && parse_int(fields[8], spin_eff)
                && parse_int(fields[13], clear) && parse_int(fields[30], depth)
                && parse_hex16(fields[31], eval_bits) && parse_hex16(fields[41], acc_bits)
                && parse_hex16(fields[42], like_bits) && parse_hex16(fields[43], value_bits)
                && parse_hex16(fields[23], parent_acc) && parse_hex16(fields[24], parent_like)
                && parse_hex16(fields[25], parent_value) && parse_rows(fields[14], src_rows)
                && parse_rows(fields[15], result_rows) && parse_int(fields[45], cfg_safe);
            check(parsed, "parity case parses: " + fields[0]);
            if (!parsed)
            {
                continue;
            }
            std::string what = "parity case " + std::to_string(id);
            engine.ai_config()->safe = static_cast<int>(cfg_safe);
            config.safe = static_cast<int>(cfg_safe);
            auto piece = tetris::try_from_char(fields[2][0]);
            check(piece.has_value(), what + " piece converts");
            if (!piece.has_value())
            {
                continue;
            }
            auto candidate =
                tetris::toj::ExternalPoseTransform::to_placement(*piece, static_cast<int>(x),
                    static_cast<int>(y), static_cast<int>(r));
            check(candidate.has_value(), what + " maps to a value candidate");
            if (!candidate.has_value())
            {
                continue;
            }
            auto lowest = tetris::toj::lowest_occupied_row(*piece, *candidate);
            check(lowest.has_value(), what + " has a lowest occupied row");
            if (!lowest.has_value())
            {
                continue;
            }
            m_tetris::TetrisMap src_map = legacy_map(src_rows);
            m_tetris::TetrisMap result_map = legacy_map(result_rows);
            m_tetris::TetrisNode node{};
            bool made = engine.context()->create(
                m_tetris::TetrisBlockStatus(fields[2][0], static_cast<std::int8_t>(x),
                    static_cast<std::int8_t>(y), static_cast<std::uint8_t>(r)),
                node);
            check(made, what + " legacy node rebuilds from its status");
            if (!made)
            {
                continue;
            }
            search_tspin::Search::TetrisNodeWithTSpinType ex(&node);
            long long flag = 0;
            parse_int(fields[9], flag);
            ex.is_check = flag != 0;
            parse_int(fields[10], flag);
            ex.is_last_rotate = flag != 0;
            parse_int(fields[11], flag);
            ex.is_ready = flag != 0;
            parse_int(fields[12], flag);
            ex.is_mini_ready = flag != 0;
            ex.type = legacy_spin(spin_eff);
            ai_zzz::TOJ::Status parent{};
            long long word = 0;
            parse_int(fields[16], word);
            parent.death = static_cast<std::int8_t>(word);
            parse_int(fields[17], word);
            parent.combo = static_cast<std::int8_t>(word);
            parse_int(fields[18], word);
            parent.under_attack = static_cast<std::int8_t>(word);
            parse_int(fields[19], word);
            parent.map_rise = static_cast<std::int8_t>(word);
            parse_int(fields[20], word);
            parent.b2b = static_cast<std::int8_t>(word);
            parse_int(fields[21], word);
            parent.t2_value = static_cast<std::int16_t>(word);
            parse_int(fields[22], word);
            parent.t3_value = static_cast<std::int16_t>(word);
            parent.acc_value = std::bit_cast<double>(parent_acc);
            parent.like = std::bit_cast<double>(parent_like);
            parent.value = std::bit_cast<double>(parent_value);
            std::string next_text = fields[26] == "-" ? std::string() : fields[26];
            char hold_char = fields[27] == "-" ? ' ' : fields[27][0];
            m_tetris::TetrisContext::Env env{ next_text.c_str(), next_text.size(), fields[2][0],
                hold_char, fields[29] == "1" };
            auto legacy_eval = legacy.eval(ex, result_map, src_map);
            long long eval_t2 = 0;
            long long eval_t3 = 0;
            parse_int(fields[32], eval_t2);
            parse_int(fields[33], eval_t3);
            bool eval_faithful = ulp_distance(legacy_eval.value, std::bit_cast<double>(eval_bits))
                    <= frozen_allowance
                && legacy_eval.t2_value == eval_t2 && legacy_eval.t3_value == eval_t3;
            check(eval_faithful, what + " legacy synthesis reproduces the evaluation");
            auto legacy_out = legacy.get(ex, legacy_eval, static_cast<std::size_t>(clear),
                result_map, static_cast<std::size_t>(depth), parent, env);
            long long o_death = 0;
            long long o_combo = 0;
            long long o_b2b = 0;
            long long o_under = 0;
            long long o_maprise = 0;
            long long o_t2 = 0;
            long long o_t3 = 0;
            parse_int(fields[34], o_death);
            parse_int(fields[35], o_combo);
            parse_int(fields[36], o_b2b);
            parse_int(fields[37], o_under);
            parse_int(fields[38], o_maprise);
            parse_int(fields[39], o_t2);
            parse_int(fields[40], o_t3);
            bool out_faithful = legacy_out.death == o_death && legacy_out.combo == o_combo
                && legacy_out.b2b == o_b2b && legacy_out.under_attack == o_under
                && legacy_out.map_rise == o_maprise && legacy_out.t2_value == o_t2
                && legacy_out.t3_value == o_t3
                && ulp_distance(legacy_out.acc_value, std::bit_cast<double>(acc_bits))
                    <= frozen_allowance
                && ulp_distance(legacy_out.like, std::bit_cast<double>(like_bits))
                    <= frozen_allowance
                && ulp_distance(legacy_out.value, std::bit_cast<double>(value_bits))
                    <= frozen_allowance;
            check(out_faithful, what + " legacy synthesis reproduces the transition");
            if (!eval_faithful)
            {
                check(false, what + " legacy synthesis reproduces the evaluation");
                continue;
            }
            bool legacy_lockout = node.row >= 20;
            bool value_lockout = *lowest >= 20;
            tetris::Board boards = value_board(result_rows);
            toj_policy::State value_parent;
            value_parent.death = parent.death;
            value_parent.combo = parent.combo;
            value_parent.under_attack = parent.under_attack;
            value_parent.map_rise = parent.map_rise;
            value_parent.b2b = parent.b2b;
            value_parent.t2_value = parent.t2_value;
            value_parent.t3_value = parent.t3_value;
            value_parent.acc_value = parent.acc_value;
            value_parent.like = parent.like;
            value_parent.value = parent.value;
            toj_policy::DecisionContext value_context;
            std::vector<tetris::Piece> next_pieces;
            bool next_ok = true;
            for (char c : next_text)
            {
                auto converted = tetris::try_from_char(c);
                if (!converted.has_value())
                {
                    next_ok = false;
                }
                else
                {
                    next_pieces.push_back(*converted);
                }
            }
            check(next_ok, what + " next sequence converts");
            value_context.next = next_pieces;
            if (hold_char == ' ')
            {
                value_context.hold = std::nullopt;
            }
            else
            {
                value_context.hold = tetris::try_from_char(hold_char);
            }
            value_context.used_hold = fields[29] == "1";
            value_context.depth = static_cast<std::size_t>(depth);
            tetris::Outcome outcome;
            outcome.spin = value_spin(spin_eff);
            outcome.clear_count = static_cast<int>(clear);
            outcome.lockout = false;
            toj_policy::Evaluation got_eval = policy.evaluate(boards);
            peak.eval_value = std::max(peak.eval_value,
                ulp_distance(got_eval.value, std::bit_cast<double>(eval_bits)));
            check(eval_matches_frozen(got_eval, eval_t2, eval_t3, eval_bits),
                what + " evaluation matches the frozen fields");
            long long arrival_class = 0;
            parse_int(fields[6], arrival_class);
            tetris::Candidate value_candidate{ *candidate,
                arrival_class == 1 ? tetris::ArrivalClass::TerminalRotation
                                   : tetris::ArrivalClass::Normal };
            toj_policy::State got = policy.transition(*piece, value_candidate, outcome, boards,
                value_parent, value_context, got_eval);
            if (legacy_lockout != value_lockout)
            {
                ++divergent_count;
                m_tetris::TetrisNode patched_node{};
                bool patched_made = engine.context()->create(
                    m_tetris::TetrisBlockStatus(fields[2][0], static_cast<std::int8_t>(x),
                        static_cast<std::int8_t>(y), static_cast<std::uint8_t>(r)),
                    patched_node);
                check(patched_made, what + " patched legacy node rebuilds");
                if (!patched_made)
                {
                    continue;
                }
                patched_node.row = *lowest;
                search_tspin::Search::TetrisNodeWithTSpinType patched(&patched_node);
                patched.is_check = ex.is_check;
                patched.is_last_rotate = ex.is_last_rotate;
                patched.is_ready = ex.is_ready;
                patched.is_mini_ready = ex.is_mini_ready;
                patched.type = ex.type;
                ai_zzz::TOJ::Status approved = legacy.get(patched, legacy_eval,
                    static_cast<std::size_t>(clear), result_map, static_cast<std::size_t>(depth),
                    parent, env);
                bool approved_ok = got.death == approved.death && got.combo == approved.combo
                    && got.b2b == approved.b2b && got.under_attack == approved.under_attack
                    && got.map_rise == approved.map_rise && got.t2_value == approved.t2_value
                    && got.t3_value == approved.t3_value
                    && ulp_distance(got.acc_value, approved.acc_value) <= frozen_allowance
                    && ulp_distance(got.like, approved.like) <= frozen_allowance
                    && ulp_distance(got.value, approved.value) <= frozen_allowance;
                check(approved_ok, what + " divergent transition follows the approved semantic");
                continue;
            }
            ++agree_count;
            peak.out_acc = std::max(peak.out_acc,
                ulp_distance(got.acc_value, std::bit_cast<double>(acc_bits)));
            peak.out_like = std::max(peak.out_like,
                ulp_distance(got.like, std::bit_cast<double>(like_bits)));
            peak.out_value = std::max(peak.out_value,
                ulp_distance(got.value, std::bit_cast<double>(value_bits)));
            check(state_matches_frozen(got, static_cast<std::int8_t>(o_death),
                    static_cast<std::int8_t>(o_combo), static_cast<std::int8_t>(o_b2b),
                    static_cast<std::int8_t>(o_under), static_cast<std::int8_t>(o_maprise),
                    static_cast<std::int16_t>(o_t2), static_cast<std::int16_t>(o_t3), acc_bits,
                    like_bits, value_bits),
                what + " transition matches the frozen fields");
        }
        check(agree_count == 7167 && divergent_count == 0,
            "lockout split pins 7167 shared and 0 divergent corpus cases");
        check(peak.eval_value <= frozen_allowance, "evaluation drift stays within allowance");
        check(peak.out_acc <= frozen_allowance, "accumulation drift stays within allowance");
        check(peak.out_like <= frozen_allowance, "affinity drift stays within allowance");
        check(peak.out_value <= frozen_allowance, "value drift stays within allowance");
        std::println("parity peak ULP: eval {} acc {} like {} value {} over {} shared and {} divergent",
            peak.eval_value, peak.out_acc, peak.out_like, peak.out_value, agree_count,
            divergent_count);
    }
}

int main()
{
    std::string const dir = std::string(TETRIS_POLICY_FIXTURE_DIR);
    std::string const path = dir + "/" + std::string(TETRIS_POLICY_FIXTURE);
    std::ifstream file(path);
    check(file.good(), "policy v2 fixture opens at " + path);
    if (!file.good())
    {
        std::println("toj_policy_tests: {} checks, {} failures", checks, failures);
        return 1;
    }
    constexpr int field_count = 46;
    std::vector<std::string> want_tags = { "empty", "tall", "o1", "o2", "t1", "iwell3",
        "iwell4", "slotS", "slotM", "mini0", "mini1", "mini2", "double0", "double1", "double2",
        "double3", "pci", "pci2", "pci3", "pci4" };
    for (int b = 0; b < 24; ++b)
    {
        want_tags.push_back("s" + std::to_string(b));
    }
    std::vector<bool> tag_seen(want_tags.size(), false);
    Tallies tallies;
    std::string line;
    long long expected_id = 0;
    bool ids_sequential = true;
    bool fields_parse = true;
    while (std::getline(file, line))
    {
        if (line.empty() || line[0] == '#')
        {
            continue;
        }
        auto fields = split_fields(line);
        if (fields.size() != field_count)
        {
            fields_parse = false;
            continue;
        }
        long long id = -1;
        long long clear = -1;
        long long spin = -1;
        long long y = -1;
        if (!parse_int(fields[0], id) || !parse_int(fields[13], clear) || !parse_int(fields[8], spin)
            || !parse_int(fields[4], y))
        {
            fields_parse = false;
            continue;
        }
        if (id != expected_id)
        {
            ids_sequential = false;
        }
        expected_id = id + 1;
        std::uint64_t bits = 0;
        std::vector<std::uint32_t> src_rows;
        std::vector<std::uint32_t> result_rows;
        long long eval_t2 = 0;
        long long eval_t3 = 0;
        long long o_death = 0;
        long long p_combo = 0;
        long long p_b2b = 0;
        long long p_under = 0;
        long long p_maprise = 0;
        long long last_rotate = 0;
        long long ready = 0;
        long long mini_ready = 0;
        long long arrival = 0;
        long long cfg_safe = -1;
        if (!parse_hex16(fields[31], bits) || !parse_hex16(fields[41], bits)
            || !parse_hex16(fields[42], bits) || !parse_hex16(fields[43], bits)
            || !parse_hex16(fields[23], bits) || !parse_hex16(fields[24], bits)
            || !parse_hex16(fields[25], bits) || !parse_rows(fields[14], src_rows)
            || !parse_rows(fields[15], result_rows) || !parse_int(fields[32], eval_t2)
            || !parse_int(fields[33], eval_t3) || !parse_int(fields[34], o_death)
            || !parse_int(fields[17], p_combo) || !parse_int(fields[20], p_b2b)
            || !parse_int(fields[18], p_under) || !parse_int(fields[19], p_maprise)
            || !parse_int(fields[10], last_rotate) || !parse_int(fields[11], ready)
            || !parse_int(fields[12], mini_ready) || !parse_int(fields[6], arrival)
            || !parse_int(fields[45], cfg_safe))
        {
            fields_parse = false;
            continue;
        }
        ++tallies.cases;
        if (cfg_safe == 0)
        {
            tallies.cfg_zero = true;
        }
        if (cfg_safe == 5)
        {
            tallies.cfg_five = true;
        }
        if (cfg_safe == 16)
        {
            tallies.cfg_sixteen = true;
        }
        for (std::size_t t = 0; t < want_tags.size(); ++t)
        {
            if (fields[1] == want_tags[t])
            {
                tag_seen[t] = true;
            }
        }
        if (clear >= 0 && clear <= 4)
        {
            tallies.clear_seen[clear] = true;
        }
        if (spin >= 0 && spin <= 2)
        {
            tallies.spin_seen[spin] = true;
        }
        if (spin == 1 && clear == 1)
        {
            tallies.full_single = true;
        }
        if (spin == 1 && clear == 2)
        {
            tallies.full_double = true;
        }
        if (spin == 1 && clear == 3)
        {
            tallies.full_triple = true;
        }
        if (eval_t2 > 0)
        {
            tallies.t2_gain = true;
        }
        if (eval_t3 > 0)
        {
            tallies.t3_gain = true;
        }
        if (o_death == 1)
        {
            tallies.death = true;
        }
        bool all_empty = true;
        for (auto row : result_rows)
        {
            if (row != 0)
            {
                all_empty = false;
            }
        }
        if (all_empty)
        {
            tallies.empty_result = true;
        }
        if (all_empty && cfg_safe != 0 && p_maprise == 0)
        {
            tallies.perfect_bonus_hot = true;
        }
        if (spin == 1 && clear == 2 && cfg_safe != 0)
        {
            tallies.full_double_hot = true;
        }
        if (arrival == 0 || arrival == 1)
        {
            tallies.arrival[arrival] = true;
        }
        if (last_rotate == 0 || last_rotate == 1)
        {
            tallies.last_rotate[last_rotate] = true;
        }
        if (ready == 0 || ready == 1)
        {
            tallies.ready[ready] = true;
        }
        if (mini_ready == 0 || mini_ready == 1)
        {
            tallies.mini_ready[mini_ready] = true;
        }
        if (y >= 20)
        {
            tallies.tall_landing = true;
        }
        if (fields[23] != "0000000000000000")
        {
            tallies.parent_acc = true;
        }
        if (fields[24] != "0000000000000000")
        {
            tallies.parent_like = true;
        }
        if (p_combo >= 0 && p_combo <= 4)
        {
            tallies.combo_seen[p_combo] = true;
        }
        if (p_b2b == 0 || p_b2b == 1)
        {
            tallies.b2b_seen[p_b2b] = true;
        }
        if (p_under >= 0 && p_under <= 2)
        {
            tallies.under_seen[p_under] = true;
        }
        if (p_maprise == 0 || p_maprise == 1)
        {
            tallies.maprise_seen[p_maprise] = true;
        }
        if (fields[27] == "-")
        {
            tallies.hold_empty = true;
        }
        if (fields[27] == "T")
        {
            tallies.hold_t = true;
        }
        if (fields[27] == "I")
        {
            tallies.hold_i = true;
        }
        if (fields[27] == "O")
        {
            tallies.hold_other = true;
        }
        if (fields[26] == "IOSZLJT")
        {
            tallies.next_full = true;
        }
        if (fields[26] == "T")
        {
            tallies.next_single_t = true;
        }
        if (fields[26] == "IOSZLI")
        {
            tallies.next_absent = true;
        }
        if (fields[26] == "-")
        {
            tallies.next_empty = true;
        }
        if (fields[26] == "STLI" || fields[26] == "IOT")
        {
            tallies.next_short = true;
        }
    }
    check(fields_parse, "every data line parses with 46 fields");
    check(ids_sequential, "case ids are sequential from zero");
    check(tallies.cases > 1000, "corpus holds a substantive case count");
    for (std::size_t t = 0; t < want_tags.size(); ++t)
    {
        check(tag_seen[t], "board tag present: " + want_tags[t]);
    }
    for (int c = 0; c <= 4; ++c)
    {
        check(tallies.clear_seen[c], "clear count present: " + std::to_string(c));
    }
    check(tallies.spin_seen[0], "non-spin transitions present");
    check(tallies.spin_seen[1], "full T-spin transitions present");
    check(tallies.spin_seen[2], "mini T-spin transitions present");
    check(tallies.full_single, "full T-spin single present");
    check(tallies.full_double, "full T-spin double present");
    check(tallies.full_triple, "full T-spin triple present");
    check(tallies.t2_gain, "T2 descriptor gains present");
    check(tallies.t3_gain, "T3 descriptor gains present");
    check(tallies.death, "death transitions present");
    check(tallies.empty_result, "perfect-clear empty results present");
    check(tallies.arrival[0] && tallies.arrival[1], "both arrival classes present");
    check(tallies.last_rotate[0] && tallies.last_rotate[1], "rotation and non-rotation endings present");
    check(tallies.ready[0] && tallies.ready[1], "ready and unready witnesses present");
    check(tallies.mini_ready[0] && tallies.mini_ready[1], "mini-ready and non-mini witnesses present");
    check(tallies.tall_landing, "high-stack landings present");
    check(tallies.parent_acc && tallies.parent_like, "nonzero parent accumulation present");
    for (int c = 0; c <= 4; ++c)
    {
        check(tallies.combo_seen[c], "parent combo present: " + std::to_string(c));
    }
    check(tallies.b2b_seen[0] && tallies.b2b_seen[1], "both parent B2B states present");
    for (int c = 0; c <= 2; ++c)
    {
        check(tallies.under_seen[c], "parent under-attack present: " + std::to_string(c));
    }
    check(tallies.maprise_seen[0] && tallies.maprise_seen[1], "both parent map-rise states present");
    check(tallies.hold_empty && tallies.hold_t && tallies.hold_i && tallies.hold_other,
        "empty, T, I, and other hold states present");
    check(tallies.next_full && tallies.next_single_t && tallies.next_absent && tallies.next_empty
        && tallies.next_short,
        "full, single-T, T-absent, empty, and short next sequences present");
    check(tallies.cfg_zero && tallies.cfg_five && tallies.cfg_sixteen,
        "config-safe variants 0, 5, and 16 present");
    check(tallies.full_double_hot, "full T-spin double at nonzero safety present");
    check(tallies.perfect_bonus_hot, "perfect clear with live bonus at nonzero safety present");
    run_negative_oracle_tests();
    check_contract_pair(dir);
    run_parity(path);
    run_synthetic_formula_tests();
    std::println("toj_policy_tests: {} checks, {} failures", checks, failures);
    return failures == 0 ? 0 : 1;
}
