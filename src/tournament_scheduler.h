#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <semaphore>
#include <thread>
#include <vector>

namespace tournament_scheduler
{
    class GameExecutor;

    using LeafFn = std::function<void()>;

    class RunPair
    {
    public:
        void operator()(LeafFn first, LeafFn second) const;

    private:
        friend class GameExecutor;
        explicit RunPair(GameExecutor* owner);
        GameExecutor* owner_;
    };

    using GameBody = std::function<int(RunPair const& run_pair)>;

    struct GameJob
    {
        std::uint64_t game_id = 0;
        GameBody body;
    };

    inline constexpr int outcome_abandoned = std::numeric_limits<int>::min();
    inline constexpr int outcome_failed = std::numeric_limits<int>::max();

    int first_to_capacity(int target, int wins_a, int wins_b);

    struct SeriesDemand
    {
        std::uint64_t series_id = 0;
        int capacity = 0;
    };

    struct WaveSlot
    {
        std::uint64_t series_id = 0;
        int slot = 0;

        bool operator==(WaveSlot const&) const = default;
    };

    std::vector<WaveSlot> build_wave(std::vector<SeriesDemand> const& demands, int wave_limit);

    struct ExecutorConfig
    {
        int game_workers = 0;
        int leaf_limit = 0;
    };

    struct ExecutorStats
    {
        long long games_started = 0;
        long long games_completed = 0;
        long long turns_executed = 0;
        int peak_active_games = 0;
        int peak_concurrent_leaves = 0;
    };

    using WaveJobFactory = std::function<GameJob(WaveSlot const&)>;

    std::vector<int> run_wave(GameExecutor& executor, std::vector<WaveSlot> const& wave,
                              WaveJobFactory const& factory);

    class GameExecutor
    {
    public:
        explicit GameExecutor(ExecutorConfig config = {});
        ~GameExecutor();
        GameExecutor(GameExecutor const&) = delete;
        GameExecutor& operator=(GameExecutor const&) = delete;

        std::vector<int> run(std::vector<GameJob> jobs);
        void shutdown();
        void request_stop();

        int game_worker_count() const
        {
            return game_workers_;
        }

        int leaf_limit() const
        {
            return leaf_limit_;
        }

        ExecutorStats stats() const;

    private:
        friend class RunPair;
        struct RunState;
        struct Task;
        struct HelperTask;

        void execute_pair(LeafFn first, LeafFn second);
        void game_worker_loop();
        void helper_worker_loop();

        int game_workers_;
        int leaf_limit_;
        std::counting_semaphore<> leaf_permits_;

        std::mutex run_mutex_;
        std::mutex job_mutex_;
        std::condition_variable job_cv_;
        std::deque<std::shared_ptr<Task>> pending_;

        std::mutex helper_mutex_;
        std::condition_variable helper_cv_;
        std::deque<std::shared_ptr<HelperTask>> helper_queue_;

        std::vector<std::thread> game_threads_;
        std::vector<std::thread> helper_threads_;

        std::atomic<bool> stopping_{false};
        std::atomic<bool> draining_{false};
        std::atomic<long long> games_started_{0};
        std::atomic<long long> games_completed_{0};
        std::atomic<long long> turns_executed_{0};
        std::atomic<int> active_games_{0};
        std::atomic<int> concurrent_leaves_{0};
        std::atomic<int> peak_active_games_{0};
        std::atomic<int> peak_concurrent_leaves_{0};
    };
}
