#include "legacy_cmp_normalize.h"
#include "legacy_cmp_observer.h"
#include "tetris_core.h"
#include "rule_toj.h"
#include "ai_zzz.h"
#include "search_tspin.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <print>
#include <string>
#include <utility>
#include <vector>

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

    using Engine = m_tetris::TetrisEngine<rule_toj::TetrisRule, ai_zzz::TOJ,
        search_tspin::Search>;

    int const combo_table[] = { 0, 0, 0, 1, 1, 2, 2, 3, 3, 4 };

    ai_zzz::TOJ::Param const default_param = {
        10.507166148, 7.539860726, 13.048099725, 13.388476179, 6.728747539, 9.476881786,
        0.258534525, -0.108269503, 4.394241496, -4.892359035, 0.049148374, 1.586714505,
        8.885878229, -0.006001836, -0.004336234, -2.021765056, -0.951446468, -1.145468832,
        -1.515758227, -0.612910192, -0.476031978, 0.009596827, -0.399212013, -0.855819915,
        -0.418779377, -0.454784178, -1.417493065, 1.050941751, 0.756272086,
    };

    void init_engine(Engine &engine)
    {
        if (!engine.prepare(10, 40))
        {
            check(false, "legacy engine prepares");
        }
        engine.memory_limit(256ull << 20);
        engine.search_config()->allow_rotate_move = false;
        engine.search_config()->allow_180 = true;
        engine.search_config()->allow_d = true;
        engine.search_config()->is_20g = false;
        engine.search_config()->last_rotate = false;
        engine.ai_config()->table = combo_table;
        engine.ai_config()->table_max = 10;
        engine.ai_config()->param = default_param;
    }

    void run_normalizer_tests()
    {
        auto key_o0 = legacy_cmp::normalize_land_point('O', 4, 10, 0, 0, false, 1);
        auto key_o1 = legacy_cmp::normalize_land_point('O', 4, 10, 1, 0, false, 2);
        check(key_o0.matched && key_o1.matched, "O placements convert");
        check(key_o0 == key_o1, "O rotation collapses to one semantic candidate");
        auto key_t_plain = legacy_cmp::normalize_land_point('T', 4, 10, 0, 0, false, 3);
        auto key_t_last = legacy_cmp::normalize_land_point('T', 4, 10, 0, 0, true, 4);
        auto key_t_spin = legacy_cmp::normalize_land_point('T', 4, 10, 0, 1, true, 5);
        check(key_t_plain.matched && key_t_last.matched && key_t_spin.matched,
            "T placements convert");
        check(!(key_t_plain == key_t_last), "T arrival channel distinguishes last rotation");
        check(!(key_t_last == key_t_spin), "T spin class distinguishes full spins");
        auto key_i_plain = legacy_cmp::normalize_land_point('I', 4, 10, 0, 0, false, 6);
        auto key_i_last = legacy_cmp::normalize_land_point('I', 4, 10, 0, 0, true, 7);
        check(key_i_plain == key_i_last, "non-T pieces ignore the arrival channel");
        auto bad_piece = legacy_cmp::normalize_land_point('X', 4, 0, 0, 0, false, 8);
        auto bad_rotation = legacy_cmp::normalize_land_point('T', 4, 10, 5, 0, false, 9);
        auto bad_rotation_same = legacy_cmp::normalize_land_point('T', 4, 10, 5, 0, false, 9);
        auto bad_rotation_other = legacy_cmp::normalize_land_point('T', 4, 10, 5, 0, false, 10);
        check(!bad_piece.matched && !bad_rotation.matched,
            "unconvertible land points report unmatched");
        check(bad_rotation == bad_rotation_same, "unmatched keys compare by status bits");
        check(!(bad_rotation == bad_rotation_other),
            "distinct unmatched statuses stay distinct");
        check(!(bad_piece == key_t_plain), "matched and unmatched keys never compare equal");
    }

    void run_directed_fixture_tests()
    {
        Engine engine;
        init_engine(engine);
        search_tspin::Search search;
        search.init(engine.context().get(), engine.search_config());
        m_tetris::TetrisMap map(10, 40);
        std::vector<std::pair<char, std::size_t>> expected = {
            { 'O', 9 }, { 'I', 17 }, { 'T', 34 }, { 'S', 17 }, { 'Z', 17 },
            { 'J', 34 }, { 'L', 34 },
        };
        for (auto [piece, want] : expected)
        {
            auto const *results = search.search(map, engine.context()->generate(piece), 0);
            std::map<legacy_cmp::NormalizedKey, int> distinct;
            int unmatched = 0;
            for (auto const &land : *results)
            {
                int spin = land.type == search_tspin::Search::TSpinType::TSpin ? 1
                    : (land.type == search_tspin::Search::TSpinType::TSpinMini ? 2 : 0);
                auto key = legacy_cmp::normalize_land_point(piece, land->status.x,
                    land->status.y, land->status.r, spin, land.is_last_rotate,
                    land->status.status);
                distinct[key] += 1;
                if (!key.matched)
                {
                    ++unmatched;
                }
            }
            check(results->size() == want, "directed fixture raw count matches");
            check(distinct.size() == want && unmatched == 0,
                "directed fixture normalizes one to one with nothing unmatched");
        }
    }

    void run_observer_tests()
    {
        {
            Engine engine;
        init_engine(engine);
            m_tetris::TetrisMap map(10, 40);
            for (int y = 0; y < 10; ++y)
            {
                map.row[y] = 0x1ff;
            }
            map.roof = 10;
            legacy_cmp::Observer observer;
            legacy_cmp::AttachGuard guard(&observer);
            engine.ai_config()->safe = engine.ai()->get_safe(map, 'T');
            auto result = engine.run_hold(map, engine.context()->generate('T'), ' ',
                true, "TOJ", 3, m_tetris::SearchBudget::by_iterations(4));
            (void)result;
            auto const &counts = observer.counts;
            check(counts.widening_iters == 4, "observer counts requested iterations");
            check(counts.eval_requests == counts.eval_hits + counts.eval_calls,
                "requests split exactly into hits and calls");
            check(counts.eval_calls > 0 && counts.eval_hits > 0,
                "observer sees both hit and miss paths");
            check(counts.fresh_nodes + counts.recycled_nodes == counts.eval_requests,
                "every evaluation materializes exactly one search child");
            check(counts.root_nodes <= 1, "at most one search root per run");
            check(counts.parent_expansions > 0, "observer counts parent expansions");
            check(counts.eval_hit_ns > 0 && counts.eval_miss_ns > 0
                    && counts.parent_ns > 0,
                "observer timers accumulate while enabled");
        }
        {
            Engine engine;
        init_engine(engine);
            m_tetris::TetrisMap map(10, 40);
            legacy_cmp::Observer observer;
            observer.timers_enabled = false;
            legacy_cmp::AttachGuard guard(&observer);
            auto result = engine.run_hold(map, engine.context()->generate('T'), ' ',
                true, "TOJ", 3, m_tetris::SearchBudget::by_iterations(2));
            (void)result;
            check(observer.counts.eval_requests > 0,
                "attached counting continues with timers off");
            check(observer.counts.eval_hit_ns == 0
                    && observer.counts.eval_miss_ns == 0
                    && observer.counts.parent_ns == 0,
                "timer accumulators stay zero with timers off");
        }
        {
            Engine engine;
            init_engine(engine);
            m_tetris::TetrisMap map(10, 40);
            legacy_cmp::Observer observer;
            m_tetris::TetrisEngine<rule_toj::TetrisRule, ai_zzz::TOJ,
                search_tspin::Search>::RunResult result;
            {
                legacy_cmp::AttachGuard guard(&observer);
                result = engine.run_hold(map, engine.context()->generate('T'), ' ',
                    true, "TOJ", 3, m_tetris::SearchBudget::by_iterations(4));
            }
            auto const &counts = observer.counts;
            check(result.target != nullptr, "observed search selects a target");
            check(counts.widening_iters == 4, "observed search completes its budget");
            check(counts.eval_requests == counts.eval_hits + counts.eval_calls,
                "observed requests partition exactly");
            check(counts.fresh_nodes + counts.recycled_nodes == counts.eval_requests,
                "observed materialization matches evaluation");
            check(counts.fresh_nodes + counts.recycled_nodes + counts.reused_nodes > 0,
                "observed search links children");
            check(engine.memory_usage() > 0, "observed search retains storage");
            observer.reset();
            check(observer.counts.eval_requests == 0 && observer.path_marks.empty(),
                "observer reset clears counts and mark state");
        }
    }

    void run_path_state_tests()
    {
        Engine engine;
        init_engine(engine);
        m_tetris::TetrisMap map(10, 40);
        search_tspin::Search search;
        search.init(engine.context().get(), engine.search_config());
        auto const *results = search.search(map, engine.context()->generate('T'), 0);
        check(!results->empty(), "path probe enumerates land points");
        legacy_cmp::Observer observer;
        {
            legacy_cmp::AttachGuard guard(&observer);
            std::vector<char> path = search.make_path(
                engine.context()->generate('T'), results->back(), map);
            (void)path;
        }
        check(observer.counts.path_valid_states > 1,
            "a traversed path reports multiple valid discovered states");
        {
            legacy_cmp::Observer scoped;
            legacy_cmp::AttachGuard guard(&scoped);
            search_tspin::Search::TetrisNodeWithTSpinType self(
                engine.context()->generate('T'));
            std::vector<char> path = search.make_path(
                engine.context()->generate('T'), self, map);
            check(path.empty(), "start equal to goal yields an empty path");
            check(scoped.counts.path_valid_states == 1,
                "start equal to goal counts exactly the start state");
        }
    }
}

int main()
{
    run_normalizer_tests();
    run_directed_fixture_tests();
    run_observer_tests();
    run_path_state_tests();
    std::println("legacy_cmp_tests: {} checks, {} failures", checks, failures);
    return failures == 0 ? 0 : 1;
}
