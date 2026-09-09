#ifndef TETRIS_ROW_FUSION_TRIAL
#error TETRIS_ROW_FUSION_TRIAL is required
#endif
#include "tetris_engine.h"
#include "toj_rule.h"
#include "toj_policy.h"
#include "profile_value_runner.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <print>
#include <span>
#include <string>
#include <vector>

namespace engine_alias = tetris_engine;
namespace support = profile_value;

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
            std::println(stderr, "FAIL: {}", what);
        }
    }

    int const combo_table[] = { 0, 0, 0, 1, 1, 2, 2, 3, 3, 4 };

    struct SplitMix64
    {
        std::uint64_t state = 0;

        explicit SplitMix64(std::uint64_t seed)
            : state(seed)
        {
        }

        std::uint64_t next()
        {
            state += 0x9e3779b97f4a7c15ull;
            std::uint64_t z = state;
            z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
            z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
            return z ^ (z >> 31);
        }
    };

    struct PolicyFixture
    {
        toj_policy::Config config;
        toj_policy::Policy policy;

        explicit PolicyFixture(int safe = 5, bool zero_parameters = false)
        {
            config.combo_table = combo_table;
            config.combo_table_max = 10;
            config.safe = safe;
            config.parameters = zero_parameters ? toj_policy::Parameters{}
                                                : toj_policy::Parameters::production_defaults();
            policy.init(&config);
        }
    };

    bool eval_bits_equal(toj_policy::Evaluation const &a, toj_policy::Evaluation const &b)
    {
        return std::bit_cast<std::uint64_t>(a.value) == std::bit_cast<std::uint64_t>(b.value)
            && a.t2_value == b.t2_value && a.t3_value == b.t3_value;
    }

    bool state_bits_equal(toj_policy::State const &a, toj_policy::State const &b)
    {
        return a.death == b.death && a.combo == b.combo && a.under_attack == b.under_attack
            && a.map_rise == b.map_rise && a.b2b == b.b2b && a.t2_value == b.t2_value
            && a.t3_value == b.t3_value
            && std::bit_cast<std::uint64_t>(a.acc_value) == std::bit_cast<std::uint64_t>(b.acc_value)
            && std::bit_cast<std::uint64_t>(a.like) == std::bit_cast<std::uint64_t>(b.like)
            && std::bit_cast<std::uint64_t>(a.value) == std::bit_cast<std::uint64_t>(b.value);
    }

    constexpr int reference_spawn_frame = 22;
    constexpr int reference_danger_limit = 19;
    constexpr int reference_height = 40;

    int reference_local_roof(
        std::array<std::uint32_t, reference_height> const &rows)
    {
        for (int y = reference_height - 1; y >= 0; --y)
        {
            if (rows[static_cast<std::size_t>(y)] != 0)
            {
                return y + 1;
            }
        }
        return 0;
    }

    int reference_scan_safe(toj_policy::Policy const &policy,
        std::array<std::uint32_t, reference_height> const &rows, toj_policy::Piece next)
    {
        int safe = 0;
        while (true)
        {
            int up = safe + 1;
            std::uint32_t threat = 0;
            if (up >= reference_danger_limit)
            {
                threat = 1;
            }
            else
            {
                int height = reference_spawn_frame - up;
                threat = policy.danger_bits(next, 0) & rows[static_cast<std::size_t>(height - 4)]
                    | policy.danger_bits(next, 1) & rows[static_cast<std::size_t>(height - 3)]
                    | policy.danger_bits(next, 2) & rows[static_cast<std::size_t>(height - 2)]
                    | policy.danger_bits(next, 3) & rows[static_cast<std::size_t>(height - 1)];
            }
            if (threat != 0)
            {
                break;
            }
            ++safe;
        }
        return safe;
    }

    int reference_safe(toj_policy::Policy const &policy, tetris::Board const &board,
        bool lockout, bool has_next, toj_policy::Piece next)
    {
        if (lockout)
        {
            return -1;
        }
        if (has_next)
        {
            std::array<std::uint32_t, reference_height> rows = {};
            for (int y = 0; y < reference_spawn_frame - 1; ++y)
            {
                rows[static_cast<std::size_t>(y)] = board.row(y);
            }
            return reference_scan_safe(policy, rows, next);
        }
        std::array<std::uint32_t, reference_height> rows = {};
        for (int y = 0; y < reference_height; ++y)
        {
            rows[static_cast<std::size_t>(y)] = board.row(y);
        }
        return reference_spawn_frame - reference_local_roof(rows);
    }

    std::vector<tetris::Board> directed_boards()
    {
        std::vector<tetris::Board> out;
        auto add = [&](std::array<std::uint16_t, 48> rows) {
            out.push_back(tetris::Board::from_rows(rows));
        };
        std::array<std::uint16_t, 48> rows = {};
        add(rows);
        for (int y : { 0, 1, 20, 21, 39, 40, 47 })
        {
            rows = {};
            rows[static_cast<std::size_t>(y)] = 1u << 4;
            add(rows);
        }
        rows = {};
        for (int y = 0; y <= 5; ++y)
        {
            rows[static_cast<std::size_t>(y)] = 0x3ff;
        }
        add(rows);
        rows = {};
        for (int y = 0; y <= 20; ++y)
        {
            rows[static_cast<std::size_t>(y)] = 0x3ff;
        }
        add(rows);
        for (int y = 17; y <= 24; ++y)
        {
            rows = {};
            rows[static_cast<std::size_t>(y)] = static_cast<std::uint16_t>(1u << (y % 10));
            add(rows);
        }
        rows = {};
        for (int y = 0; y < 48; y += 2)
        {
            rows[static_cast<std::size_t>(y)] = 0x201;
        }
        add(rows);
        rows = {};
        for (int y = 0; y < 40; ++y)
        {
            for (int x = 0; x <= y % 10; ++x)
            {
                rows[static_cast<std::size_t>(y)] |= static_cast<std::uint16_t>(1u << x);
            }
        }
        add(rows);
        rows = {};
        for (int y = 0; y < 40; ++y)
        {
            rows[static_cast<std::size_t>(y)] = 0x3ff;
        }
        add(rows);
        return out;
    }

    std::vector<tetris::Board> seeded_boards(std::uint64_t seed, int density_permille,
        std::size_t count)
    {
        SplitMix64 rng(seed);
        std::vector<tetris::Board> out;
        out.reserve(count);
        for (std::size_t i = 0; i < count; ++i)
        {
            std::array<std::uint16_t, 48> rows = {};
            for (std::size_t y = 0; y < 48; ++y)
            {
                std::uint32_t row = 0;
                for (int x = 0; x < 10; ++x)
                {
                    if (static_cast<int>(rng.next() % 1000) < density_permille)
                    {
                        row |= 1u << x;
                    }
                }
                rows[y] = static_cast<std::uint16_t>(row);
            }
            out.push_back(tetris::Board::from_rows(rows));
        }
        return out;
    }

    struct RunnerHarness
    {
        toj_policy::Config policy_config;
        toj_policy::Policy seed_policy;
        engine_alias::EngineConfig engine_config;
        engine_alias::Engine engine;
        std::optional<support::Runner> runner;
    };

    void init_runner_harness(RunnerHarness &harness, bool telemetry, std::size_t iters,
        std::uint32_t seed)
    {
        harness.policy_config.combo_table = combo_table;
        harness.policy_config.combo_table_max = 10;
        harness.policy_config.safe = 5;
        harness.policy_config.parameters = toj_policy::Parameters::production_defaults();
        harness.engine_config.policy = &harness.policy_config;
        harness.engine_config.telemetry_enabled = telemetry;
        harness.engine_config.timers_enabled = false;
        check(harness.engine.init(harness.engine_config), "row fusion runner engine initializes");
        harness.seed_policy.init(&harness.policy_config);
        support::Runner::Config config;
        config.maxdepth = 6;
        config.hold = true;
        config.iters = iters;
        config.budget_ms = 0;
        harness.runner.emplace(harness.policy_config, harness.seed_policy, harness.engine,
            seed, config);
    }

    std::vector<tetris::Board> harvested_boards(std::size_t limit)
    {
        RunnerHarness harness;
        init_runner_harness(harness, true, 64, 11u);
        std::vector<tetris::Board> out;
        for (int move = 0; move < 20 && out.size() < limit; ++move)
        {
            std::size_t before = harness.engine.arena_size();
            harness.runner->step();
            std::size_t after = harness.engine.arena_size();
            for (std::size_t id = before; id < after && out.size() < limit; ++id)
            {
                auto const *node = harness.engine.node(static_cast<engine_alias::NodeId>(id));
                if (node != nullptr)
                {
                    out.push_back(node->board);
                }
            }
        }
        return out;
    }

    void run_evaluation_identity_tests()
    {
        PolicyFixture fixture;
        RowFusionSafeInputs inputs;
        inputs.lockout = false;
        inputs.has_next = true;
        inputs.next = toj_policy::Piece::T;
        int fused = 0;
        std::size_t probes = 0;
        auto one_board = [&](tetris::Board const &board, std::string const &what) {
            toj_policy::Evaluation plain = fixture.policy.evaluate(board);
            toj_policy::Evaluation fused_eval = fixture.policy.evaluate(board, &inputs, &fused);
            check(eval_bits_equal(plain, fused_eval), "row fusion evaluation bits " + what);
            check(fused_eval.value == fused_eval.value, "row fusion value not nan " + what);
            ++probes;
        };
        std::size_t index = 0;
        for (auto const &board : directed_boards())
        {
            one_board(board, "directed " + std::to_string(index++));
        }
        std::size_t corpus = 0;
        for (int density : { 80, 500, 900 })
        {
            for (auto const &board : seeded_boards(
                     0x5eed1234ull + static_cast<std::uint64_t>(density) * 7ull, density, 8000))
            {
                one_board(board, "random " + std::to_string(corpus++));
            }
        }
        std::size_t harvested = 0;
        for (auto const &board : harvested_boards(20000))
        {
            one_board(board, "harvested " + std::to_string(harvested++));
        }
        check(probes > 44000, "row fusion evaluation corpus is large");
        int sweep_k = 0;
        for (int safe_value : { 0, 5, 16 })
        {
            PolicyFixture sweep(safe_value);
            PolicyFixture zero(safe_value, true);
            std::size_t k = 0;
            for (auto const &board : directed_boards())
            {
                toj_policy::Evaluation a = sweep.policy.evaluate(board);
                toj_policy::Evaluation b = sweep.policy.evaluate(board, &inputs, &fused);
                check(eval_bits_equal(a, b),
                    "row fusion sweep production " + std::to_string(safe_value) + " "
                        + std::to_string(k));
                toj_policy::Evaluation c = zero.policy.evaluate(board);
                toj_policy::Evaluation d = zero.policy.evaluate(board, &inputs, &fused);
                check(eval_bits_equal(c, d),
                    "row fusion sweep zero " + std::to_string(safe_value) + " "
                        + std::to_string(k));
                ++k;
            }
            ++sweep_k;
        }
        check(sweep_k == 3, "row fusion sweep ran all parameter sets");
    }

    void run_safe_margin_tests()
    {
        PolicyFixture fixture;
        int fused = 0;
        std::size_t combos = 0;
        std::size_t lockout_cases = 0;
        std::size_t empty_cases = 0;
        std::vector<std::vector<tetris::Board>> per_piece_boards;
        for (int piece_index = 0; piece_index < 7; ++piece_index)
        {
            per_piece_boards.push_back(seeded_boards(
                0xab1e5eedull + static_cast<std::uint64_t>(piece_index) * 913ull, 250, 2000));
        }
        for (int piece_index = 0; piece_index < 7; ++piece_index)
        {
            auto next = static_cast<toj_policy::Piece>(piece_index);
            std::size_t boards = 0;
            for (auto const &board : per_piece_boards[static_cast<std::size_t>(piece_index)])
            {
                for (bool has_next : { false, true })
                {
                    for (bool lockout : { false, true })
                    {
                        std::uint64_t before_reads = rf_row_reads;
                        RowFusionSafeInputs inputs;
                        inputs.lockout = lockout;
                        inputs.has_next = has_next;
                        inputs.next = next;
                        int fused_safe = 0;
                        toj_policy::Evaluation evaluation =
                            fixture.policy.evaluate(board, &inputs, &fused_safe);
                        std::uint64_t fused_reads = rf_row_reads - before_reads;
                        toj_policy::DecisionContext context;
                        std::vector<toj_policy::Piece> next_pieces;
                        if (has_next)
                        {
                            next_pieces.push_back(next);
                            context.next = next_pieces;
                        }
                        toj_policy::State parent;
                        int t_expect = toj_policy::Policy::expected_t_distance(context);
                        toj_policy::Candidate candidate;
                        toj_policy::Outcome outcome;
                        toj_policy::State fused_state =
                            fixture.policy.transition_known_lockout(next, candidate, outcome,
                                board, parent, context, evaluation, lockout, t_expect,
                                &fused_safe);
                        std::uint64_t after_fused_reads = rf_row_reads;
                        check(fused_reads == reference_height,
                            "row fusion fused reads only the evaluate export "
                                + std::to_string(combos));
                        check(after_fused_reads == before_reads + reference_height,
                            "row fusion fused transition adds no reads "
                                + std::to_string(combos));
                        toj_policy::State legacy_state =
                            fixture.policy.transition_known_lockout(next, candidate, outcome,
                                board, parent, context, evaluation, lockout, t_expect);
                        std::uint64_t legacy_reads = rf_row_reads - after_fused_reads;
                        check(state_bits_equal(fused_state, legacy_state),
                            "row fusion state bits " + std::to_string(combos));
                        int expect_safe = reference_safe(fixture.policy, board, lockout,
                            has_next, next);
                        check(fused_safe == expect_safe,
                            "row fusion fused safe equals reference "
                                + std::to_string(combos));
                        if (lockout)
                        {
                            check(fused_safe == -1, "row fusion lockout safe is minus one");
                            check(legacy_reads == 0,
                                "row fusion lockout reference reads no safe rows");
                            ++lockout_cases;
                        }
                        else if (has_next)
                        {
                            check(legacy_reads == reference_spawn_frame - 1,
                                "row fusion next reference reads 21 rows");
                            ++combos;
                        }
                        else
                        {
                            check(legacy_reads == reference_height,
                                "row fusion empty reference reads 40 rows");
                            ++combos;
                            ++empty_cases;
                        }
                        ++boards;
                    }
                }
            }
            check(boards == 8000, "row fusion safe margin board count");
        }
        check(lockout_cases > 0, "row fusion lockout cases ran");
        check(empty_cases > 0, "row fusion empty queue cases ran");
    }

    void run_boundary_and_hold_tests()
    {
        PolicyFixture fixture;
        int fused = 0;
        std::size_t combos = 0;
        std::vector<std::optional<toj_policy::Piece>> holds = {
            std::nullopt, toj_policy::Piece::T, toj_policy::Piece::I, toj_policy::Piece::O
        };
        for (std::size_t next_length : { std::size_t{0}, std::size_t{1}, std::size_t{2}, std::size_t{4} })
        {
            for (int piece_index = 0; piece_index < 7; ++piece_index)
            {
                auto next_first = static_cast<toj_policy::Piece>(piece_index);
                for (auto const &hold : holds)
                {
                    std::vector<tetris::Board> boards = seeded_boards(
                        0xfeedbeefull + static_cast<std::uint64_t>(piece_index) * 977ull, 300,
                        1);
                    tetris::Board const board = boards.front();
                    RowFusionSafeInputs inputs;
                    inputs.lockout = false;
                    inputs.has_next = next_length > 0;
                    inputs.next = next_first;
                    int fused_safe = 0;
                    toj_policy::Evaluation evaluation =
                        fixture.policy.evaluate(board, &inputs, &fused_safe);
                    std::vector<toj_policy::Piece> next_pieces(next_length, next_first);
                    toj_policy::DecisionContext context;
                    context.next = next_pieces;
                    context.hold = hold;
                    toj_policy::State parent;
                    int t_expect = toj_policy::Policy::expected_t_distance(context);
                    toj_policy::Candidate candidate;
                    toj_policy::Outcome outcome;
                    toj_policy::State fused_state =
                        fixture.policy.transition_known_lockout(next_first, candidate, outcome,
                            board, parent, context, evaluation, false, t_expect, &fused_safe);
                    toj_policy::State legacy_state =
                        fixture.policy.transition_known_lockout(next_first, candidate, outcome,
                            board, parent, context, evaluation, false, t_expect);
                    check(state_bits_equal(fused_state, legacy_state),
                        "row fusion boundary hold state bits " + std::to_string(combos));
                    check(fused_safe == reference_safe(fixture.policy, board, false,
                              next_length > 0, next_first),
                        "row fusion boundary hold safe " + std::to_string(combos));
                    ++combos;
                }
            }
        }
        check(combos == 4 * 7 * 4, "row fusion boundary hold combos ran");
        RunnerHarness harness;
        init_runner_harness(harness, true, 64, 23u);
        row_fusion_reset_counters();
        std::size_t placed = 0;
        for (int move = 0; move < 20; ++move)
        {
            auto record = harness.runner->step();
            if (record.kind == support::MoveRecord::Kind::Placed)
            {
                ++placed;
            }
        }
        check(rf_source_mismatches == 0, "row fusion per source hoisting held over 20 moves");
        check(rf_eval_exports >= rf_legacy_exports, "row fusion exports at least legacy exports");
    }

    void run_overlay_hazard_tests()
    {
        PolicyFixture fixture;
        int fused = 0;
        std::size_t witnesses = 0;
        std::array<std::uint16_t, 48> rows = {};
        rows[1] = 0x3f7;
        rows[2] = 0x003;
        rows[3] = 0x004;
        tetris::Board witness = tetris::Board::from_rows(rows);
        for (int piece_index = 0; piece_index < 7; ++piece_index)
        {
            auto next = static_cast<toj_policy::Piece>(piece_index);
            int clean = 0;
            int post = 0;
            fixture.policy.row_fusion_overlay_witness_for_test(witness, next, &clean, &post);
            check(clean == 18, "row fusion witness clean margin is maximal for piece "
                    + std::to_string(piece_index));
            check(post < clean, "row fusion witness post overlay margin diverges for piece "
                    + std::to_string(piece_index));
            std::uint64_t before_reads = rf_row_reads;
            RowFusionSafeInputs inputs;
            inputs.lockout = false;
            inputs.has_next = true;
            inputs.next = next;
            int fused_safe = 0;
            toj_policy::Evaluation evaluation =
                fixture.policy.evaluate(witness, &inputs, &fused_safe);
            check(rf_row_reads - before_reads == reference_height,
                "row fusion witness reads attributed to clean export");
            check(fused_safe == clean, "row fusion witness fused equals clean rows");
            check(fused_safe != post, "row fusion witness fused differs from post overlay");
            toj_policy::Evaluation plain = fixture.policy.evaluate(witness);
            check(eval_bits_equal(plain, evaluation),
                "row fusion witness evaluation bits unchanged");
            if (clean != post && fused_safe == clean)
            {
                ++witnesses;
            }
        }
        check(witnesses == 7, "row fusion witness found for all seven pieces");
    }

    void run_early_path_tests()
    {
        {
            RunnerHarness harness;
            init_runner_harness(harness, true, 64, 31u);
            std::size_t hit_moves = 0;
            for (int move = 0; move < 20; ++move)
            {
                row_fusion_reset_counters();
                auto record = harness.runner->step();
                engine_alias::SearchStats stats = harness.engine.search_stats();
                check(stats.eval_computed + stats.eval_memo_hits == stats.policy_transitions,
                    "row fusion conservation identity per move " + std::to_string(move));
                check(static_cast<std::uint64_t>(rf_fused_children)
                        + static_cast<std::uint64_t>(rf_legacy_children)
                    == stats.policy_transitions,
                    "row fusion children partition per move " + std::to_string(move));
                check(static_cast<std::uint64_t>(rf_fused_children)
                        == stats.eval_computed,
                    "row fusion fused children equal computed per move "
                        + std::to_string(move));
                check(static_cast<std::uint64_t>(rf_legacy_children)
                        == stats.eval_memo_hits,
                    "row fusion legacy children equal hits per move "
                        + std::to_string(move));
                check(static_cast<std::uint64_t>(rf_eval_exports)
                        >= stats.eval_computed,
                    "row fusion exports cover computed per move " + std::to_string(move));
                if (stats.eval_memo_hits > 0)
                {
                    ++hit_moves;
                }
            }
            check(hit_moves > 0, "row fusion memo hit leg ran");
            {
                row_fusion_reset_counters();
                std::size_t before = harness.engine.arena_size();
                engine_alias::SearchStats stats_before = harness.engine.search_stats();
                auto children = harness.engine.expand(engine_alias::NodeId{0});
                (void)children;
                engine_alias::SearchStats stats_after = harness.engine.search_stats();
                std::size_t computed =
                    stats_after.eval_computed - stats_before.eval_computed;
                std::size_t hits =
                    stats_after.eval_memo_hits - stats_before.eval_memo_hits;
                std::size_t transitions =
                    stats_after.policy_transitions - stats_before.policy_transitions;
                check(harness.engine.arena_size() >= before,
                    "row fusion scoped expand runs");
                check(computed + hits == transitions,
                    "row fusion scoped expand conserves");
                check(static_cast<std::uint64_t>(rf_fused_children) == computed,
                    "row fusion scoped expand fuses exactly the computed boards");
                check(static_cast<std::uint64_t>(rf_legacy_children) == hits,
                    "row fusion scoped expand leaves hits unfused");
                check(static_cast<std::uint64_t>(rf_eval_exports) == computed,
                    "row fusion exactly one export per computed board");
            }
            auto selection = harness.engine.select_best();
            check(selection.has_value(), "row fusion early path selects");
            check(harness.engine.finalize(tetris::Placement::unchecked(
                      tetris::toj::spawn_x, tetris::toj::spawn_y, 0)).path_ok,
                "row fusion early path finalizes");
        }
        {
            RunnerHarness harness;
            init_runner_harness(harness, false, 64, 31u);
            for (int move = 0; move < 20; ++move)
            {
                auto record = harness.runner->step();
                engine_alias::SearchStats stats = harness.engine.search_stats();
                check(stats.eval_computed == 0 && stats.eval_memo_hits == 0
                        && stats.policy_transitions == 0,
                    "row fusion telemetry off keeps exported counters zero");
            }
        }
        {
            RunnerHarness harness;
            init_runner_harness(harness, true, 64, 37u);
            harness.engine_config.cache.layout =
                engine_alias::CacheConfig::Layout::DirectMapped;
            check(harness.engine.init(harness.engine_config),
                "row fusion cache engine reinitializes");
            std::uint64_t legacy_before = rf_legacy_children;
            std::uint64_t fused_before = rf_fused_children;
            for (int move = 0; move < 10; ++move)
            {
                harness.runner->step();
            }
            check(rf_fused_children - fused_before > 0,
                "row fusion cache engine still fuses computed children");
            check(rf_legacy_children - legacy_before > 0,
                "row fusion cache engine keeps legacy path reachable");
        }
        {
            RunnerHarness harness;
            init_runner_harness(harness, true, 64, 41u);
            harness.engine_config.arena_capacity = 4096;
            check(harness.engine.init(harness.engine_config),
                "row fusion small arena engine initializes");
            std::uint64_t legacy_before = rf_legacy_children;
            for (int move = 0; move < 6; ++move)
            {
                harness.runner->step();
            }
            check(rf_legacy_children >= legacy_before,
                "row fusion exhausted path keeps counters consistent");
        }
    }

    void run_search_identity_tests()
    {
        std::uint64_t const digest_seed = 0x600df00dull;
        auto node_digest = [](engine_alias::Engine &engine, std::size_t begin,
                              std::size_t end) {
            std::uint64_t h = digest_seed;
            auto mix = [&h](std::uint64_t word) {
                h ^= word;
                h *= 1099511628211ull;
            };
            for (std::size_t id = begin; id < end; ++id)
            {
                auto const *node = engine.node(static_cast<engine_alias::NodeId>(id));
                if (node == nullptr)
                {
                    mix(0xdead);
                    continue;
                }
                for (int y = 0; y < 48; ++y)
                {
                    mix(node->board.row(y));
                }
                mix(node->played == engine_alias::Piece::T ? 3 : 1);
                mix(node->cursor);
            }
            return h;
        };
        std::vector<std::uint64_t> digests;
        for (int pass = 0; pass < 2; ++pass)
        {
            RunnerHarness harness;
            init_runner_harness(harness, true, 64, 53u);
            std::vector<std::uint64_t> step_digests;
            for (int move = 0; move < 20; ++move)
            {
                std::size_t before = harness.engine.arena_size();
                harness.runner->step();
                std::size_t after = harness.engine.arena_size();
                step_digests.push_back(node_digest(harness.engine, before, after));
            }
            digests.insert(digests.end(), step_digests.begin(), step_digests.end());
            check(harness.engine.select_best().has_value(),
                "row fusion identity run selects " + std::to_string(pass));
        }
        std::size_t const per_pass = digests.size() / 2;
        bool all_equal = true;
        for (std::size_t i = 0; i < per_pass; ++i)
        {
            all_equal = all_equal && digests[i] == digests[per_pass + i];
        }
        check(all_equal, "row fusion node order digests equal across passes");
    }

    void run_memory_tests()
    {
        static_assert(sizeof(engine_alias::Child) == 192,
            "row fusion keeps child storage unchanged");
        static_assert(sizeof(toj_policy::Evaluation) == 16,
            "row fusion keeps evaluation storage unchanged");
        static_assert(sizeof(RowFusionSafeInputs) <= 8,
            "row fusion safe inputs add no real storage");
        RunnerHarness harness;
        init_runner_harness(harness, true, 128, 59u);
        for (int move = 0; move < 12; ++move)
        {
            harness.runner->step();
        }
        std::uint64_t retained = harness.engine.retained_bytes();
        check(retained < 268435456ull, "row fusion retained bytes under budget");
        check(retained > 0, "row fusion retained bytes positive");
        std::uint64_t again = [&] {
            RunnerHarness other;
            init_runner_harness(other, true, 128, 59u);
            for (int move = 0; move < 12; ++move)
            {
                other.runner->step();
            }
            return other.engine.retained_bytes();
        }();
        check(retained == again, "row fusion retained bytes reproducible");
    }
}

int main()
{
    row_fusion_reset_counters();
    run_evaluation_identity_tests();
    run_safe_margin_tests();
    run_boundary_and_hold_tests();
    run_overlay_hazard_tests();
    run_early_path_tests();
    run_search_identity_tests();
    run_memory_tests();
    std::println("row_fusion_trial_tests: {} checks, {} failures", checks, failures);
    return failures == 0 ? 0 : 1;
}
