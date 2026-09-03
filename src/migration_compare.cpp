// migration_compare.cpp
// Phase 0 temporary tool. Temporary migration comparison harness, removed
// together with the legacy code after final acceptance.
//
// Runs seat-swapped pairs of games through the tuner simulator and writes one
// machine-readable row per pair, frozen in docs/phase0/pair_output_schema.md.
// At Phase 0 both seats use the legacy engine, so the harness validates its
// own determinism and schema before a second engine exists. The final
// comparison mode gains an explicit engine selector per seat at value-engine
// cutover.
//
// usage: migration_compare <pairs> <out.csv> [threads] [seed] [iters] [max_rounds]

#include "tuner_match.h"

#include <print>
#include <string>
#include <vector>

namespace
{
    constexpr uint64_t default_seed = 1;

    uint64_t scenario_seed_for(uint64_t seed, size_t pair, int seat)
    {
        return tuner_match::splitmix64(seed * 0x9e3779b97f4a7c15ull + static_cast<uint64_t>(pair) * 2ull + static_cast<uint64_t>(seat));
    }

    std::string outcome_row(size_t pair, uint64_t seed, tuner_match::MatchOutcome const &o)
    {
        auto i = [](size_t v) { return std::to_string(v); };
        return std::to_string(pair)
            + "," + std::to_string(seed)
            + "," + std::to_string(scenario_seed_for(seed, pair, 1))
            + "," + std::to_string(scenario_seed_for(seed, pair, 2))
            + ",legacy,legacy"
            + "," + std::to_string(o.winner)
            + "," + (o.dead1 ? "1" : "0") + "," + (o.dead2 ? "1" : "0")
            + "," + (o.capped ? "1" : "0")
            + "," + std::to_string(o.winner_reason)
            + "," + std::to_string(o.rounds)
            + "," + i(o.attack1) + "," + i(o.attack2)
            + "," + i(o.pieces1) + "," + i(o.pieces2)
            + "," + i(o.lines1) + "," + i(o.lines2)
            + "," + i(o.tspin_mini1) + "," + i(o.tspin_mini2)
            + "," + i(o.tspin_single1) + "," + i(o.tspin_single2)
            + "," + i(o.tspin_double1) + "," + i(o.tspin_double2)
            + "," + i(o.tspin_triple1) + "," + i(o.tspin_triple2)
            + "," + i(o.perfect_clear1) + "," + i(o.perfect_clear2)
            + "," + i(o.replay_failures)
            + "\n";
    }

    std::vector<tuner_match::MatchOutcome> run_pairs(size_t pairs, uint64_t seed, int threads, int iters, int max_rounds)
    {
        std::vector<tuner_match::MatchJob> jobs(pairs);
        double const *theta = ai_zzz::TOJ::kProductionDefaultTheta;
        for (size_t p = 0; p < pairs; ++p)
        {
            for (size_t k = 0; k < tuner_match::NUM_PARAMS; ++k)
            {
                jobs[p].p1[k] = theta[k];
                jobs[p].p2[k] = theta[k];
            }
            jobs[p].scenario_seed_p1 = scenario_seed_for(seed, p, 1);
            jobs[p].scenario_seed_p2 = scenario_seed_for(seed, p, 2);
            jobs[p].job_id = p;
            jobs[p].budget_iters_p1 = iters;
            jobs[p].budget_iters_p2 = iters;
            jobs[p].budget_ms_p1 = -1;
            jobs[p].budget_ms_p2 = -1;
        }
        std::atomic<bool> view{ false };
        std::mutex view_mutex;
        std::atomic<uint32_t> view_index{ 0 };
        return tuner_match::run_batch(jobs, threads, iters > 0 ? iters : -1, -1, max_rounds, view, view_mutex, view_index);
    }
}

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        std::println(stderr, "usage: migration_compare <pairs> <out.csv> [threads] [seed] [iters] [max_rounds]");
        return 1;
    }
    size_t const pairs = std::strtoull(argv[1], nullptr, 10);
    std::string const out_path = argv[2];
    int const threads = argc > 3 ? std::atoi(argv[3]) : 1;
    uint64_t const seed = argc > 4 ? std::strtoull(argv[4], nullptr, 10) : default_seed;
    int const iters = argc > 5 ? std::atoi(argv[5]) : 200;
    int const max_rounds = argc > 6 ? std::atoi(argv[6]) : 3600;

    std::vector<tuner_match::MatchOutcome> first = run_pairs(pairs, seed, threads, iters, max_rounds);

    FILE *file = std::fopen(out_path.c_str(), "wb");
    if (file == nullptr)
    {
        std::println(stderr, "cannot open {}", out_path);
        return 1;
    }
    std::string header = "pair,seed,scenario_seed_p1,scenario_seed_p2,engine1,engine2,winner,dead1,dead2,capped,winner_reason,rounds,"
        "attack1,attack2,pieces1,pieces2,lines1,lines2,"
        "tspin_mini1,tspin_mini2,tspin_single1,tspin_single2,tspin_double1,tspin_double2,tspin_triple1,tspin_triple2,"
        "perfect_clear1,perfect_clear2,replay_failures\n";
    std::fwrite(header.data(), 1, header.size(), file);
    for (size_t p = 0; p < first.size(); ++p)
    {
        std::string row = outcome_row(p, seed, first[p]);
        std::fwrite(row.data(), 1, row.size(), file);
    }
    std::fclose(file);

    std::vector<tuner_match::MatchOutcome> second = run_pairs(pairs, seed, threads, iters, max_rounds);
    size_t mismatched = 0;
    for (size_t p = 0; p < first.size() && p < second.size(); ++p)
    {
        tuner_match::MatchOutcome const &a = first[p];
        tuner_match::MatchOutcome const &b = second[p];
        bool same = a.winner == b.winner && a.dead1 == b.dead1 && a.dead2 == b.dead2 && a.capped == b.capped
            && a.winner_reason == b.winner_reason && a.rounds == b.rounds
            && a.attack1 == b.attack1 && a.attack2 == b.attack2
            && a.pieces1 == b.pieces1 && a.pieces2 == b.pieces2
            && a.lines1 == b.lines1 && a.lines2 == b.lines2
            && a.tspin_mini1 == b.tspin_mini1 && a.tspin_mini2 == b.tspin_mini2
            && a.tspin_single1 == b.tspin_single1 && a.tspin_single2 == b.tspin_single2
            && a.tspin_double1 == b.tspin_double1 && a.tspin_double2 == b.tspin_double2
            && a.tspin_triple1 == b.tspin_triple1 && a.tspin_triple2 == b.tspin_triple2
            && a.perfect_clear1 == b.perfect_clear1 && a.perfect_clear2 == b.perfect_clear2;
        if (!same)
        {
            ++mismatched;
            std::println(stderr, "nondeterministic pair {}", p);
        }
    }
    std::println("migration_compare: {} pairs, {} nondeterministic, written {}", first.size(), mismatched, out_path);
    return mismatched == 0 && first.size() == pairs ? 0 : 1;
}
