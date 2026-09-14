#include "tournament_scheduler.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace tournament_scheduler;

namespace
{
    thread_local int tl_alloc_budget = -1;
    thread_local int tl_alloc_count = 0;

    void* scheduler_test_alloc(std::size_t n)
    {
        if (tl_alloc_budget >= 0 && tl_alloc_count++ == tl_alloc_budget)
        {
            tl_alloc_budget = -1;
            throw std::bad_alloc();
        }
        void* p = std::malloc(n != 0 ? n : 1);
        if (p == nullptr)
        {
            throw std::bad_alloc();
        }
        return p;
    }
}

void* operator new(std::size_t n)
{
    return scheduler_test_alloc(n);
}

void* operator new[](std::size_t n)
{
    return scheduler_test_alloc(n);
}

void operator delete(void* p) noexcept
{
    std::free(p);
}

void operator delete[](void* p) noexcept
{
    std::free(p);
}

void operator delete(void* p, std::size_t) noexcept
{
    std::free(p);
}

void operator delete[](void* p, std::size_t) noexcept
{
    std::free(p);
}

namespace
{
    int failures = 0;

    void check(bool condition, std::string const& name)
    {
        if (condition)
        {
            std::printf("PASS: %s\n", name.c_str());
        }
        else
        {
            std::printf("FAIL: %s\n", name.c_str());
            ++failures;
        }
    }

    std::uint64_t splitmix64(std::uint64_t x)
    {
        x += 0x9E3779B97F4A7C15ULL;
        x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
        x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
        return x ^ (x >> 31);
    }

    int fake_winner(std::uint64_t series, int game_index)
    {
        return static_cast<int>(splitmix64(series * 0x9E3779B97F4A7C15ULL
                                               + static_cast<std::uint64_t>(game_index) + 1)
                                & 1);
    }

    int thread_count()
    {
        std::ifstream status("/proc/self/status");
        std::string line;
        while (std::getline(status, line))
        {
            if (line.rfind("Threads:", 0) == 0)
            {
                return std::atoi(line.c_str() + 8);
            }
        }
        return -1;
    }

    int settled_thread_count(int expected)
    {
        int count = thread_count();
        for (int retry = 0; expected >= 0 && count != expected && retry < 100; ++retry)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            count = thread_count();
        }
        return count;
    }

    std::shared_ptr<std::atomic<int>> pair_hits(RunPair const& pair)
    {
        auto hits = std::make_shared<std::atomic<int>>(0);
        pair([hits]
             {
                 std::this_thread::yield();
                 hits->fetch_add(1, std::memory_order_relaxed);
             },
             [hits]
             {
                 std::this_thread::yield();
                 hits->fetch_add(1, std::memory_order_relaxed);
             });
        return hits;
    }

    void test_capacity_rule()
    {
        check(first_to_capacity(11, 9, 2) == 2, "ft11 at 9-2 exposes two games");
        check(first_to_capacity(11, 10, 10) == 1, "ft11 at 10-10 exposes one game");
        check(first_to_capacity(11, 0, 0) == 11, "ft11 at 0-0 exposes eleven games");
        check(first_to_capacity(7, 0, 0) == 7, "ft7 at 0-0 exposes seven games");
        check(first_to_capacity(7, 6, 5) == 1, "ft7 at 6-5 exposes one game");
        check(first_to_capacity(11, 2, 9) == 2, "leader can be either side");
        check(first_to_capacity(11, 11, 3) == 0, "decided series exposes nothing");
        check(first_to_capacity(11, 14, 0) == 0, "overshoot clamps to zero");
        bool threw = false;
        try
        {
            (void)first_to_capacity(11, -1, 0);
        }
        catch (...)
        {
            threw = true;
        }
        check(threw, "negative wins rejected");
    }

    void test_wave_9_2_exposes_two()
    {
        std::vector<SeriesDemand> demands{
            {100, first_to_capacity(11, 9, 2)},
            {200, first_to_capacity(7, 0, 0)},
        };
        std::vector<WaveSlot> wave = build_wave(demands, 64);
        int count_first = 0;
        int count_second = 0;
        bool slots_valid = true;
        for (WaveSlot const& slot : wave)
        {
            if (slot.series_id == 100)
            {
                ++count_first;
                slots_valid = slots_valid && slot.slot >= 0 && slot.slot < 2;
            }
            else if (slot.series_id == 200)
            {
                ++count_second;
                slots_valid = slots_valid && slot.slot >= 0 && slot.slot < 7;
            }
        }
        check(count_first == 2, "ft11 at 9-2 contributes exactly two jobs to the wave");
        check(count_second == 7, "ft7 series contributes its full demand");
        check(slots_valid, "wave slots stay within per-series capacity");
        check(wave.size() == 9, "wave size equals total demand");
        bool const order = wave[0].series_id == 100 && wave[0].slot == 0
            && wave[1].series_id == 200 && wave[1].slot == 0
            && wave[2].series_id == 100 && wave[2].slot == 1
            && wave[3].series_id == 200 && wave[3].slot == 1
            && wave[4].series_id == 200 && wave[4].slot == 2;
        check(order, "wave interleaves series round robin");
    }

    void test_wave_fairness_and_determinism()
    {
        std::vector<SeriesDemand> demands{{1, 3}, {2, 3}, {3, 3}};
        std::vector<WaveSlot> wave = build_wave(demands, 5);
        std::map<std::uint64_t, int> counts;
        for (WaveSlot const& slot : wave)
        {
            ++counts[slot.series_id];
        }
        check(counts[1] == 2 && counts[2] == 2 && counts[3] == 1,
              "limited wave balances ready series");
        bool const order = wave[0].series_id == 1 && wave[1].series_id == 2
            && wave[2].series_id == 3 && wave[3].series_id == 1 && wave[4].series_id == 2;
        check(order, "wave order is deterministic round robin");
        std::vector<WaveSlot> const full = build_wave(demands, 64);
        check(full.size() == 9, "unlimited wave serves all demand");
        check(build_wave(demands, 0).empty(), "zero limit produces an empty wave");
        std::vector<SeriesDemand> const with_zero{{5, 0}, {6, 2}};
        std::vector<WaveSlot> const skipped = build_wave(with_zero, 8);
        check(skipped.size() == 2 && skipped[0].series_id == 6,
              "zero capacity series is skipped");
        std::vector<WaveSlot> const single = build_wave(std::vector<SeriesDemand>{{9, 4}}, 3);
        check(single.size() == 3 && single[2].series_id == 9 && single[2].slot == 2,
              "single series fills the wave in slot order");
        check(build_wave(demands, 5) == wave, "wave construction is repeatable");
        bool threw = false;
        try
        {
            std::vector<SeriesDemand> const bad{{1, -2}};
            (void)build_wave(bad, 4);
        }
        catch (...)
        {
            threw = true;
        }
        check(threw, "negative capacity rejected");
    }

    void test_duplicate_series_rejected()
    {
        bool threw = false;
        try
        {
            std::vector<SeriesDemand> const dup{{1, 2}, {2, 1}, {1, 3}};
            (void)build_wave(dup, 4);
        }
        catch (...)
        {
            threw = true;
        }
        check(threw, "duplicate series ids rejected");
        std::vector<SeriesDemand> const distinct{{1, 2}, {2, 1}, {3, 3}};
        check(build_wave(distinct, 64).size() == 6, "distinct series ids accepted");
    }

    void test_body_result_preservation()
    {
        check(outcome_abandoned == -2147483647 - 1, "abandoned sentinel is the int minimum");
        check(outcome_failed == 2147483647, "failed sentinel is the int maximum");
        check(outcome_abandoned != -1 && outcome_abandoned != 0 && outcome_abandoned != 1
                  && outcome_failed != -1 && outcome_failed != 0 && outcome_failed != 1,
              "sentinels do not collide with -1, 0, or 1");
        ExecutorConfig config;
        config.game_workers = 2;
        GameExecutor executor(config);
        std::vector<GameJob> job_list;
        int const expected[] = {-1, 0, 1};
        for (int i = 0; i < 3; ++i)
        {
            GameJob job;
            job.game_id = static_cast<std::uint64_t>(i);
            int const want = expected[i];
            job.body = [want](RunPair const& pair) -> int
            {
                std::shared_ptr<std::atomic<int>> hits = pair_hits(pair);
                return hits->load(std::memory_order_relaxed) == 2 ? want : -3;
            };
            job_list.push_back(std::move(job));
        }
        std::vector<int> results = executor.run(std::move(job_list));
        check(results.size() == 3 && results[0] == -1 && results[1] == 0 && results[2] == 1,
              "body results -1, 0, and 1 are preserved verbatim");
    }

    void test_paired_overlap_and_peaks()
    {
        int const turns = 3;
        int const game_count = 2;
        int const needed = 2 * game_count;
        std::vector<std::shared_ptr<std::atomic<int>>> gates;
        for (int t = 0; t < turns; ++t)
        {
            gates.push_back(std::make_shared<std::atomic<int>>(0));
        }
        auto marks = std::make_shared<std::vector<std::atomic<bool>>>(
            static_cast<std::size_t>(turns));
        ExecutorConfig config;
        config.game_workers = game_count;
        GameExecutor executor(config);
        std::vector<GameJob> job_list;
        for (int j = 0; j < game_count; ++j)
        {
            GameJob job;
            job.game_id = static_cast<std::uint64_t>(j + 1);
            job.body = [gates, marks, turns, needed](RunPair const& pair) -> int
            {
                for (int t = 0; t < turns; ++t)
                {
                    std::shared_ptr<std::atomic<int>> gate = gates[static_cast<std::size_t>(t)];
                    auto leaf = [gate, marks, t, needed](int side)
                    {
                        gate->fetch_add(1, std::memory_order_acq_rel);
                        auto const deadline = std::chrono::steady_clock::now()
                            + std::chrono::seconds(10);
                        while (gate->load(std::memory_order_acquire) < needed
                               && std::chrono::steady_clock::now() < deadline)
                        {
                            std::this_thread::yield();
                        }
                        if (gate->load(std::memory_order_acquire) >= needed && side == 0)
                        {
                            (*marks)[static_cast<std::size_t>(t)].store(true,
                                                                       std::memory_order_release);
                        }
                    };
                    pair([&leaf] { leaf(0); }, [&leaf] { leaf(1); });
                }
                return turns;
            };
            job_list.push_back(std::move(job));
        }
        std::vector<int> results = executor.run(std::move(job_list));
        bool outcomes = results.size() == static_cast<std::size_t>(game_count);
        for (int r : results)
        {
            outcomes = outcomes && r == turns;
        }
        check(outcomes, "both paired jobs completed all turns");
        bool overlapped = true;
        for (std::atomic<bool> const& mark : *marks)
        {
            overlapped = overlapped && mark.load(std::memory_order_acquire);
        }
        check(overlapped, "leaf callbacks overlapped in every turn");
        ExecutorStats const stats = executor.stats();
        check(stats.peak_active_games == game_count, "peak active games equals worker count");
        check(stats.peak_concurrent_leaves == needed,
              "peak leaf concurrency reaches two per game");
        check(stats.peak_active_games <= executor.game_worker_count(),
              "active games within cap");
        check(stats.peak_concurrent_leaves <= executor.leaf_limit(),
              "leaf callbacks within cap");
        check(stats.games_completed == game_count && stats.turns_executed == turns * game_count,
              "stats count games and turns");
    }

    struct FakeSeries
    {
        std::uint64_t id;
        int target;
        int wins[2] = {0, 0};

        bool done() const
        {
            return wins[0] >= target || wins[1] >= target;
        }
    };

    struct TournamentReport
    {
        std::map<std::pair<std::uint64_t, int>, int> ledger;
        int peak_active_games = 0;
        int peak_concurrent_leaves = 0;
        int capacity_violations = 0;
        int clinch_violations = 0;
        int failed_games = 0;
        int length_violations = 0;
        int ledger_violations = 0;
    };

    TournamentReport run_fake_tournament(int game_workers, int wave_limit)
    {
        std::vector<FakeSeries> series{
            {0x11, 11, {0, 0}},
            {0x22, 7, {0, 0}},
            {0x33, 11, {0, 0}},
        };
        ExecutorConfig config;
        config.game_workers = game_workers;
        GameExecutor executor(config);
        TournamentReport report;
        auto lookup = [&series](std::uint64_t id) -> FakeSeries&
        {
            for (FakeSeries& s : series)
            {
                if (s.id == id)
                {
                    return s;
                }
            }
            throw std::logic_error("unknown series");
        };
        for (int wave_no = 0; wave_no < 10000; ++wave_no)
        {
            bool all_done = true;
            std::vector<SeriesDemand> demands;
            for (FakeSeries const& s : series)
            {
                int const cap = s.done() ? 0 : first_to_capacity(s.target, s.wins[0], s.wins[1]);
                demands.push_back(SeriesDemand{s.id, cap});
                if (!s.done())
                {
                    all_done = false;
                }
            }
            if (all_done)
            {
                break;
            }
            std::vector<WaveSlot> wave = build_wave(demands, wave_limit);
            std::map<std::uint64_t, int> played;
            for (FakeSeries const& s : series)
            {
                played[s.id] = s.wins[0] + s.wins[1];
            }
            std::map<std::uint64_t, int> wave_counts;
            for (WaveSlot const& slot : wave)
            {
                ++wave_counts[slot.series_id];
            }
            for (FakeSeries const& s : series)
            {
                int const count = wave_counts.count(s.id) != 0 ? wave_counts[s.id] : 0;
                int const cap = s.done() ? 0 : first_to_capacity(s.target, s.wins[0], s.wins[1]);
                if (count > cap)
                {
                    ++report.capacity_violations;
                }
                if (!s.done() && std::max(s.wins[0], s.wins[1]) + count > s.target)
                {
                    ++report.clinch_violations;
                }
            }
            std::vector<int> outcomes = run_wave(executor, wave,
                                                 [&lookup, &played](WaveSlot const& slot)
            {
                FakeSeries& s = lookup(slot.series_id);
                int const game_index = played[slot.series_id] + slot.slot;
                int const winner = fake_winner(s.id, game_index);
                GameJob job;
                job.game_id = (s.id << 20) | static_cast<std::uint64_t>(game_index);
                job.body = [winner](RunPair const& pair) -> int
                {
                    std::shared_ptr<std::atomic<int>> hits = pair_hits(pair);
                    return hits->load(std::memory_order_relaxed) == 2 ? winner : -3;
                };
                return job;
            });
            for (std::size_t i = 0; i < wave.size(); ++i)
            {
                FakeSeries& s = lookup(wave[i].series_id);
                int const game_index = played[wave[i].series_id] + wave[i].slot;
                if (outcomes[i] != 0 && outcomes[i] != 1)
                {
                    ++report.failed_games;
                    continue;
                }
                report.ledger[std::make_pair(s.id, game_index)] = outcomes[i];
                ++s.wins[outcomes[i]];
            }
        }
        for (FakeSeries const& s : series)
        {
            int w0 = 0;
            int w1 = 0;
            for (auto const& entry : report.ledger)
            {
                if (entry.first.first == s.id)
                {
                    if (entry.second == 0)
                    {
                        ++w0;
                    }
                    else
                    {
                        ++w1;
                    }
                }
            }
            if (w0 != s.wins[0] || w1 != s.wins[1])
            {
                ++report.ledger_violations;
            }
            int const games = s.wins[0] + s.wins[1];
            if (s.wins[0] > s.target || s.wins[1] > s.target || games > 2 * s.target - 1
                || !s.done())
            {
                ++report.length_violations;
            }
        }
        ExecutorStats const stats = executor.stats();
        report.peak_active_games = stats.peak_active_games;
        report.peak_concurrent_leaves = stats.peak_concurrent_leaves;
        return report;
    }

    void test_tournament_scheduling()
    {
        TournamentReport report = run_fake_tournament(2, 3);
        check(report.capacity_violations == 0 && report.clinch_violations == 0
                  && report.failed_games == 0 && report.length_violations == 0
                  && report.ledger_violations == 0,
              "fake tournament respects capacity, clinch, and ledger rules");
        check(report.peak_active_games <= 2, "active games capped by worker count");
        check(report.peak_concurrent_leaves <= 4, "leaf callbacks capped by limit");
        check(report.ledger.size() >= 29, "tournament played a full schedule");

        TournamentReport baseline = run_fake_tournament(1, 3);
        bool repeatable = true;
        bool capped = true;
        for (int workers : {2, 4, 7})
        {
            TournamentReport other = run_fake_tournament(workers, 3);
            repeatable = repeatable && other.ledger == baseline.ledger;
            capped = capped && other.capacity_violations == 0 && other.clinch_violations == 0
                && other.failed_games == 0 && other.ledger_violations == 0
                && other.length_violations == 0 && other.peak_active_games <= workers
                && other.peak_concurrent_leaves <= 2 * other.peak_active_games;
        }
        check(repeatable, "identical game ledgers across worker counts");
        check(capped, "every worker count respects scheduling caps");
    }

    void test_result_indexing_under_reordering()
    {
        ExecutorConfig config;
        config.game_workers = 4;
        GameExecutor executor(config);
        std::vector<GameJob> job_list;
        int const count = 8;
        for (int i = 0; i < count; ++i)
        {
            GameJob job;
            job.game_id = static_cast<std::uint64_t>(100 + i);
            int const delay_ms = (count - i) * 3;
            job.body = [i, delay_ms](RunPair const& pair) -> int
            {
                auto hits = std::make_shared<std::atomic<int>>(0);
                pair([hits, delay_ms]
                     {
                         std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
                         hits->fetch_add(1, std::memory_order_relaxed);
                     },
                     [hits]
                     {
                         std::this_thread::yield();
                         hits->fetch_add(1, std::memory_order_relaxed);
                     });
                return hits->load(std::memory_order_relaxed) == 2 ? i * 10 : -3;
            };
            job_list.push_back(std::move(job));
        }
        std::vector<int> results = executor.run(std::move(job_list));
        bool indexed = results.size() == static_cast<std::size_t>(count);
        for (int i = 0; i < count; ++i)
        {
            indexed = indexed && results[static_cast<std::size_t>(i)] == i * 10;
        }
        check(indexed, "results are indexed by job position, not completion order");
    }

    void test_wave_result_alignment()
    {
        ExecutorConfig config;
        config.game_workers = 2;
        GameExecutor executor(config);
        std::vector<SeriesDemand> demands{{7, 3}, {9, 2}};
        std::vector<WaveSlot> wave = build_wave(demands, 5);
        std::vector<int> outcomes = run_wave(executor, wave, [](WaveSlot const& slot)
        {
            GameJob job;
            job.game_id = slot.series_id * 100 + static_cast<std::uint64_t>(slot.slot);
            int const delay_ms = 3 * (static_cast<int>(slot.series_id) + 2 * slot.slot) % 10;
            job.body = [slot, delay_ms](RunPair const& pair) -> int
            {
                auto hits = std::make_shared<std::atomic<int>>(0);
                pair([hits, delay_ms]
                     {
                         if (delay_ms > 0)
                         {
                             std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
                         }
                         hits->fetch_add(1, std::memory_order_relaxed);
                     },
                     [hits]
                     {
                         std::this_thread::yield();
                         hits->fetch_add(1, std::memory_order_relaxed);
                     });
                return hits->load(std::memory_order_relaxed) == 2
                    ? static_cast<int>(slot.series_id * 10
                                       + static_cast<std::uint64_t>(slot.slot))
                    : -3;
            };
            return job;
        });
        bool aligned = outcomes.size() == wave.size();
        for (std::size_t i = 0; i < wave.size(); ++i)
        {
            int const expected = static_cast<int>(wave[i].series_id * 10
                                                  + static_cast<std::uint64_t>(wave[i].slot));
            aligned = aligned && outcomes[i] == expected;
        }
        check(aligned, "wave outcomes align with wave slots regardless of completion order");
    }

    void test_leaf_limit_cap()
    {
        ExecutorConfig config;
        config.game_workers = 2;
        config.leaf_limit = 2;
        GameExecutor executor(config);
        check(executor.leaf_limit() == 2, "configured leaf limit is honored");
        std::vector<GameJob> job_list;
        for (int j = 0; j < 2; ++j)
        {
            GameJob job;
            job.game_id = static_cast<std::uint64_t>(j);
            job.body = [](RunPair const& pair) -> int
            {
                for (int t = 0; t < 3; ++t)
                {
                    std::shared_ptr<std::atomic<int>> hits = pair_hits(pair);
                    if (hits->load(std::memory_order_relaxed) != 2)
                    {
                        return -3;
                    }
                }
                return 3;
            };
            job_list.push_back(std::move(job));
        }
        std::vector<int> results = executor.run(std::move(job_list));
        bool ok = results.size() == 2;
        for (int r : results)
        {
            ok = ok && r == 3;
        }
        check(ok, "tight leaf limit still completes paired turns");
        ExecutorStats const stats = executor.stats();
        check(stats.peak_concurrent_leaves <= 2, "leaf peak respects explicit limit");
    }

    void test_nested_use_rejected()
    {
        ExecutorConfig config;
        config.game_workers = 2;
        GameExecutor executor(config);
        GameExecutor* exec_ptr = &executor;
        std::vector<GameJob> job_list;
        GameJob nested_run;
        nested_run.game_id = 1;
        nested_run.body = [exec_ptr](RunPair const&) -> int
        {
            try
            {
                std::vector<GameJob> empty;
                (void)exec_ptr->run(std::move(empty));
                return 1;
            }
            catch (...)
            {
                return 0;
            }
        };
        job_list.push_back(std::move(nested_run));
        GameJob nested_pair;
        nested_pair.game_id = 2;
        nested_pair.body = [](RunPair const& pair) -> int
        {
            int marker = 0;
            pair([&] { ++marker; }, [&] { ++marker; });
            bool inner_rejected = false;
            pair([&pair, &inner_rejected, &marker]
                 {
                     try
                     {
                         pair([] {}, [] {});
                     }
                     catch (...)
                     {
                         inner_rejected = true;
                     }
                 },
                 [&] { ++marker; });
            return inner_rejected && marker == 3 ? 0 : 1;
        };
        job_list.push_back(std::move(nested_pair));
        GameJob nested_helper;
        nested_helper.game_id = 3;
        nested_helper.body = [](RunPair const& pair) -> int
        {
            bool rejected = false;
            pair([]
                 {},
                 [&pair, &rejected]
                 {
                     try
                     {
                         pair([] {}, [] {});
                     }
                     catch (...)
                     {
                         rejected = true;
                     }
                 });
            return rejected ? 0 : 1;
        };
        job_list.push_back(std::move(nested_helper));
        std::vector<int> results = executor.run(std::move(job_list));
        check(results.size() == 3 && results[0] == 0 && results[1] == 0 && results[2] == 0,
              "nested run and nested run_pair calls are rejected");
    }

    void test_request_stop_abandons_queue()
    {
        ExecutorConfig config;
        config.game_workers = 2;
        GameExecutor executor(config);
        std::vector<GameJob> job_list;
        int const total = 16;
        for (int i = 0; i < total; ++i)
        {
            GameJob job;
            job.game_id = static_cast<std::uint64_t>(i);
            job.body = [](RunPair const& pair) -> int
            {
                auto hits = std::make_shared<std::atomic<int>>(0);
                pair([hits]
                     {
                         std::this_thread::sleep_for(std::chrono::milliseconds(40));
                         hits->fetch_add(1, std::memory_order_relaxed);
                     },
                     [hits]
                     {
                         std::this_thread::sleep_for(std::chrono::milliseconds(40));
                         hits->fetch_add(1, std::memory_order_relaxed);
                     });
                return hits->load(std::memory_order_relaxed) == 2 ? 1 : -3;
            };
            job_list.push_back(std::move(job));
        }
        std::vector<int> results;
        std::atomic<bool> returned{false};
        std::thread runner(
            [&]
            {
                results = executor.run(std::move(job_list));
                returned.store(true, std::memory_order_release);
            });
        auto const deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (executor.stats().games_started < 1
               && std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        executor.request_stop();
        runner.join();
        check(returned.load(std::memory_order_acquire), "stopped run returns to caller");
        check(results.size() == static_cast<std::size_t>(total),
              "stop yields one outcome per job");
        int completed = 0;
        int abandoned = 0;
        int failed = 0;
        for (int r : results)
        {
            if (r >= 0)
            {
                ++completed;
            }
            else if (r == outcome_abandoned)
            {
                ++abandoned;
            }
            else
            {
                ++failed;
            }
        }
        check(completed + abandoned + failed == total, "every outcome accounted after stop");
        check(completed < total, "stop prevented later jobs from running");
        check(executor.stats().games_started == completed + failed,
              "no abandoned job ever started");
        executor.shutdown();
        check(true, "shutdown after stop joins cleanly");
    }

    void test_shutdown_rejects_further_work()
    {
        ExecutorConfig config;
        config.game_workers = 2;
        GameExecutor executor(config);
        std::vector<GameJob> job_list;
        GameJob job;
        job.game_id = 1;
        job.body = [](RunPair const& pair) -> int
        {
            std::shared_ptr<std::atomic<int>> hits = pair_hits(pair);
            return hits->load(std::memory_order_relaxed);
        };
        job_list.push_back(std::move(job));
        std::vector<int> results = executor.run(std::move(job_list));
        check(results.size() == 1 && results[0] == 2, "simple paired job completes");
        executor.shutdown();
        bool threw = false;
        try
        {
            std::vector<GameJob> empty;
            (void)executor.run(std::move(empty));
        }
        catch (...)
        {
            threw = true;
        }
        check(threw, "run after shutdown rejected");
        executor.shutdown();
        check(true, "repeated shutdown is harmless");
    }

    void test_clean_destruction()
    {
        int const base = thread_count();
        {
            ExecutorConfig config;
            config.game_workers = 3;
            GameExecutor executor(config);
            std::vector<GameJob> job_list;
            for (int i = 0; i < 6; ++i)
            {
                GameJob job;
                job.game_id = static_cast<std::uint64_t>(i);
                job.body = [](RunPair const& pair) -> int
                {
                    std::shared_ptr<std::atomic<int>> hits = pair_hits(pair);
                    return hits->load(std::memory_order_relaxed);
                };
                job_list.push_back(std::move(job));
            }
            std::vector<int> results = executor.run(std::move(job_list));
            bool ok = results.size() == 6;
            for (int r : results)
            {
                ok = ok && r == 2;
            }
            check(ok, "jobs complete before destruction");
        }
        int const after_workers = settled_thread_count(base);
        {
            GameExecutor idle;
            check(idle.game_worker_count() >= 1, "default executor has workers");
        }
        int const after_idle = settled_thread_count(base);
        check(base < 0 || (after_workers == base && after_idle == base),
              "destruction joins all worker threads");
    }

    void test_no_per_turn_thread_growth()
    {
        ExecutorConfig config;
        config.game_workers = 2;
        GameExecutor executor(config);
        int const before = thread_count();
        bool ok = true;
        for (int round = 0; round < 20; ++round)
        {
            std::vector<GameJob> job_list;
            for (int i = 0; i < 4; ++i)
            {
                GameJob job;
                job.game_id = static_cast<std::uint64_t>(round * 4 + i);
                job.body = [](RunPair const& pair) -> int
                {
                    for (int t = 0; t < 3; ++t)
                    {
                        std::shared_ptr<std::atomic<int>> hits = pair_hits(pair);
                        if (hits->load(std::memory_order_relaxed) != 2)
                        {
                            return -3;
                        }
                    }
                    return 3;
                };
                job_list.push_back(std::move(job));
            }
            std::vector<int> results = executor.run(std::move(job_list));
            for (int r : results)
            {
                ok = ok && r == 3;
            }
        }
        int const after = thread_count();
        check(ok, "multi-turn jobs complete across repeated runs");
        check(before < 0 || after == before, "turns reuse persistent workers");
    }

    void test_enqueue_failure_is_transactional()
    {
        ExecutorConfig config;
        config.game_workers = 2;
        GameExecutor executor(config);
        std::atomic<int> bodies_run{0};
        std::atomic<int> bodies_in_flight{0};
        int const total = 40;
        auto make_batch = [total, &bodies_run, &bodies_in_flight]()
        {
            std::vector<GameJob> batch;
            batch.reserve(static_cast<std::size_t>(total));
            for (int i = 0; i < total; ++i)
            {
                GameJob job;
                job.game_id = static_cast<std::uint64_t>(i);
                job.body = [&bodies_run, &bodies_in_flight](RunPair const& pair) -> int
                {
                    bodies_in_flight.fetch_add(1, std::memory_order_acq_rel);
                    std::shared_ptr<std::atomic<int>> hits = pair_hits(pair);
                    bodies_run.fetch_add(1, std::memory_order_acq_rel);
                    bodies_in_flight.fetch_sub(1, std::memory_order_acq_rel);
                    return hits->load(std::memory_order_relaxed) == 2 ? 1 : -3;
                };
                batch.push_back(std::move(job));
            }
            return batch;
        };
        int threw_count = 0;
        int ran_count = 0;
        int violations = 0;
        for (int k = 0; k <= 80; ++k)
        {
            std::vector<GameJob> batch = make_batch();
            int const before_bodies = bodies_run.load(std::memory_order_acquire);
            long long const before_started = executor.stats().games_started;
            tl_alloc_budget = k;
            tl_alloc_count = 0;
            bool threw = false;
            try
            {
                std::vector<int> results = executor.run(std::move(batch));
                (void)results;
                ++ran_count;
            }
            catch (...)
            {
                threw = true;
            }
            tl_alloc_budget = -1;
            if (threw)
            {
                ++threw_count;
                for (int settle = 0; settle < 200
                     && (bodies_in_flight.load(std::memory_order_acquire) != 0
                         || bodies_run.load(std::memory_order_acquire) != before_bodies);
                     ++settle)
                {
                    std::this_thread::yield();
                }
                if (bodies_run.load(std::memory_order_acquire) != before_bodies
                    || bodies_in_flight.load(std::memory_order_acquire) != 0
                    || executor.stats().games_started != before_started)
                {
                    ++violations;
                }
            }
        }
        check(threw_count >= 3, "allocation injection triggered during enqueue");
        check(ran_count >= 1, "some injected attempts completed normally");
        check(violations == 0, "failed enqueue never started or left a game body running");
        std::vector<GameJob> batch = make_batch();
        std::vector<int> results = executor.run(std::move(batch));
        bool reusable = results.size() == static_cast<std::size_t>(total)
            && bodies_in_flight.load(std::memory_order_acquire) == 0;
        for (int r : results)
        {
            reusable = reusable && r == 1;
        }
        check(reusable, "executor reusable after failed enqueue");
        check(bodies_run.load(std::memory_order_acquire) == (ran_count + 1) * total,
              "body count matches successful runs only");
    }
}

int main()
{
    test_capacity_rule();
    test_wave_9_2_exposes_two();
    test_wave_fairness_and_determinism();
    test_duplicate_series_rejected();
    test_body_result_preservation();
    test_paired_overlap_and_peaks();
    test_tournament_scheduling();
    test_result_indexing_under_reordering();
    test_wave_result_alignment();
    test_leaf_limit_cap();
    test_nested_use_rejected();
    test_request_stop_abandons_queue();
    test_shutdown_rejects_further_work();
    test_clean_destruction();
    test_no_per_turn_thread_growth();
    test_enqueue_failure_is_transactional();
    if (failures == 0)
    {
        std::printf("ALL TOURNAMENT SCHEDULER TESTS PASSED\n");
        return 0;
    }
    std::printf("%d TOURNAMENT SCHEDULER TEST(S) FAILED\n", failures);
    return 1;
}
