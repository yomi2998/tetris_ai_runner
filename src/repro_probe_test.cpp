#include <cstddef>
#include <cstdint>
#include <print>
#include <string>
#include <utility>
#include <vector>

#include "tournament/wire.h"
#include "tuning/domain.h"
#include "tuning/engine_match.h"
#include "tuning/match.h"
#include "tuning/toj_adapter.h"

namespace
{
    namespace tw = tournament_wire;

    using TojBackend = tuning::EngineMatchBackend<tuning_toj::TojAdapter>;

    int g_checks = 0;
    int g_failures = 0;
    std::vector<std::uint64_t> g_checksums;

    void check(bool condition, std::string const &name)
    {
        ++g_checks;
        if (condition)
        {
            std::println("PASS: {}", name);
        }
        else
        {
            ++g_failures;
            std::println("FAIL: {}", name);
        }
    }

    struct ProbeSetup
    {
        std::vector<tuning::BatchGame> games;
        tuning::RunConfig config;
    };

    ProbeSetup make_setup(int games, std::size_t iterations, int max_rounds, std::uint64_t seed)
    {
        tuning::ParamSchema const schema = tuning_toj::TojAdapter::schema();
        std::vector<double> const defaults(schema.defaults.begin(), schema.defaults.end());
        std::vector<double> theta_b(defaults.size());
        for (std::size_t i = 0; i < defaults.size(); ++i)
        {
            theta_b[i] = defaults[i] * (1.0 + 0.01 * static_cast<double>(i % 5));
        }
        ProbeSetup setup;
        setup.config.threads = 1;
        setup.config.iterations_per_move = iterations;
        setup.config.max_rounds = max_rounds;
        setup.games.reserve(static_cast<std::size_t>(games));
        for (int k = 1; k <= games; ++k)
        {
            tuning::BatchGame game;
            game.id = static_cast<tuning::GameId>(k);
            game.theta_a = defaults;
            game.theta_b = theta_b;
            game.seed_a = tuning::derive_game_seed(seed, game.id, 0);
            game.seed_b = tuning::derive_game_seed(seed, game.id, 1);
            setup.games.push_back(std::move(game));
        }
        return setup;
    }

    std::uint64_t batch_checksum(std::vector<tuning::GameOutcome> const &outcomes)
    {
        std::uint64_t hash = tuning::kFnvOffsetBasis;
        for (tuning::GameOutcome const &outcome : outcomes)
        {
            std::string const encoded = tw::encode_outcome(tw::to_wire(outcome));
            hash = tuning::fnv1a_bytes(hash, encoded.data(), encoded.size());
        }
        return hash;
    }

    void record_checksum(std::vector<tuning::GameOutcome> const &outcomes)
    {
        g_checksums.push_back(batch_checksum(outcomes));
    }

    template<class Getter>
    void check_field(std::vector<tuning::GameOutcome> const &a,
                     std::vector<tuning::GameOutcome> const &b, Getter getter, std::string const &name)
    {
        bool ok = a.size() == b.size();
        for (std::size_t i = 0; i < a.size() && ok; ++i)
        {
            ok = i < b.size() && getter(a[i]) == getter(b[i]);
        }
        check(ok, name);
    }

    void check_outcomes_match(std::vector<tuning::GameOutcome> const &a,
                              std::vector<tuning::GameOutcome> const &b, std::string const &tag)
    {
        check(a.size() == b.size(), tag + ": outcome count matches");
        check_field(a, b, [](tuning::GameOutcome const &o) { return o.id; }, tag + ": id identical");
        check_field(a, b, [](tuning::GameOutcome const &o) { return o.winner; }, tag + ": winner identical");
        check_field(a, b, [](tuning::GameOutcome const &o) { return o.dead_a; }, tag + ": dead_a identical");
        check_field(a, b, [](tuning::GameOutcome const &o) { return o.dead_b; }, tag + ": dead_b identical");
        check_field(a, b, [](tuning::GameOutcome const &o) { return o.capped; }, tag + ": capped identical");
        check_field(a, b, [](tuning::GameOutcome const &o) { return o.rounds; }, tag + ": rounds identical");
        check_field(a, b, [](tuning::GameOutcome const &o) { return o.app_a; }, tag + ": app_a identical");
        check_field(a, b, [](tuning::GameOutcome const &o) { return o.app_b; }, tag + ": app_b identical");
        check_field(a, b, [](tuning::GameOutcome const &o) { return o.apl_a; }, tag + ": apl_a identical");
        check_field(a, b, [](tuning::GameOutcome const &o) { return o.apl_b; }, tag + ": apl_b identical");
        check_field(a, b, [](tuning::GameOutcome const &o) { return o.reason; }, tag + ": reason identical");
    }

    void test_rerun_identity()
    {
        TojBackend backend;
        ProbeSetup const setup = make_setup(2, 16, 150, 555);
        auto const first = backend.run_games(setup.games, setup.config);
        auto const second = backend.run_games(setup.games, setup.config);
        record_checksum(first);
        record_checksum(second);
        check(first.size() == setup.games.size() && second.size() == setup.games.size(),
              "rerun: both runs return one outcome per game");
        check_outcomes_match(first, second, "rerun");
    }

    void test_thread_count_identity()
    {
        TojBackend backend;
        ProbeSetup const setup = make_setup(2, 16, 150, 555);
        tuning::RunConfig threaded = setup.config;
        threaded.threads = 3;
        auto const single = backend.run_games(setup.games, setup.config);
        auto const triple = backend.run_games(setup.games, threaded);
        record_checksum(single);
        record_checksum(triple);
        check(single.size() == setup.games.size() && triple.size() == setup.games.size(),
              "threads: both runs return one outcome per game");
        check_outcomes_match(single, triple, "threads");
    }

    void test_all_checksums_agree()
    {
        check(g_checksums.size() == 4, "checksums: four runs were recorded");
        bool all_equal = g_checksums.size() == 4;
        for (std::size_t i = 1; i < g_checksums.size(); ++i)
        {
            all_equal = all_equal && g_checksums[i] == g_checksums[0];
        }
        check(all_equal, "checksums: every run produced the same FNV checksum");
    }
}

int main()
{
    test_rerun_identity();
    test_thread_count_identity();
    test_all_checksums_agree();
    std::println("{} checks, {} failures", g_checks, g_failures);
    return g_failures;
}
