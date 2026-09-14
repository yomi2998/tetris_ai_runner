#include "tournament_scheduler.h"

#include <algorithm>
#include <exception>
#include <future>
#include <iterator>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace tournament_scheduler
{
    namespace
    {
        enum class WorkerRole
        {
            None,
            Game,
            Helper,
        };

        thread_local WorkerRole tl_role = WorkerRole::None;
        thread_local GameExecutor* tl_owner = nullptr;
        thread_local int tl_pair_depth = 0;

        void record_peak(std::atomic<int>& peak, int value)
        {
            int current = peak.load(std::memory_order_relaxed);
            while (value > current
                   && !peak.compare_exchange_weak(current, value, std::memory_order_release,
                                                  std::memory_order_relaxed))
            {
            }
        }

        int resolve_game_workers(int requested)
        {
            if (requested > 0)
            {
                return requested;
            }
            unsigned const hardware = std::thread::hardware_concurrency();
            unsigned const usable = hardware > 1 ? hardware - 1 : 1;
            int const workers = static_cast<int>(usable / 2);
            return workers > 0 ? workers : 1;
        }

        int resolve_leaf_limit(int requested, int game_workers)
        {
            int const upper = game_workers * 2;
            if (requested <= 0)
            {
                return upper;
            }
            return std::min(requested, upper);
        }
    }

    RunPair::RunPair(GameExecutor* owner)
        : owner_(owner)
    {
    }

    void RunPair::operator()(LeafFn first, LeafFn second) const
    {
        owner_->execute_pair(std::move(first), std::move(second));
    }

    int first_to_capacity(int target, int wins_a, int wins_b)
    {
        if (target <= 0)
        {
            return 0;
        }
        if (wins_a < 0 || wins_b < 0)
        {
            throw std::invalid_argument("negative win count");
        }
        int const leader = std::max(wins_a, wins_b);
        int const capacity = target - leader;
        return capacity > 0 ? capacity : 0;
    }

    std::vector<WaveSlot> build_wave(std::vector<SeriesDemand> const& demands, int wave_limit)
    {
        if (wave_limit < 0)
        {
            throw std::invalid_argument("negative wave limit");
        }
        std::vector<WaveSlot> wave;
        if (wave_limit == 0 || demands.empty())
        {
            return wave;
        }
        long long total = 0;
        std::unordered_set<std::uint64_t> seen;
        seen.reserve(demands.size());
        for (SeriesDemand const& demand : demands)
        {
            if (demand.capacity < 0)
            {
                throw std::invalid_argument("negative series capacity");
            }
            if (!seen.insert(demand.series_id).second)
            {
                throw std::invalid_argument("duplicate series id");
            }
            total += demand.capacity;
        }
        if (total == 0)
        {
            return wave;
        }
        wave.reserve(static_cast<std::size_t>(std::min<long long>(wave_limit, total)));
        std::size_t const limit = static_cast<std::size_t>(wave_limit);
        std::vector<int> taken(demands.size(), 0);
        bool progress = true;
        while (wave.size() < limit && progress)
        {
            progress = false;
            for (std::size_t i = 0; i < demands.size() && wave.size() < limit; ++i)
            {
                if (taken[i] < demands[i].capacity)
                {
                    wave.push_back(WaveSlot{demands[i].series_id, taken[i]});
                    ++taken[i];
                    progress = true;
                }
            }
        }
        return wave;
    }

    struct GameExecutor::RunState
    {
        std::vector<int> results;
        std::mutex mutex;
        std::condition_variable cv;
        std::size_t remaining = 0;
    };

    struct GameExecutor::Task
    {
        std::size_t index = 0;
        GameJob job;
        std::shared_ptr<RunState> state;
    };

    struct GameExecutor::HelperTask
    {
        LeafFn fn;
        std::function<void()> abandon;
    };

    GameExecutor::GameExecutor(ExecutorConfig config)
        : game_workers_(resolve_game_workers(config.game_workers))
        , leaf_limit_(resolve_leaf_limit(config.leaf_limit, game_workers_))
        , leaf_permits_(leaf_limit_)
    {
        try
        {
            game_threads_.reserve(static_cast<std::size_t>(game_workers_));
            helper_threads_.reserve(static_cast<std::size_t>(game_workers_));
            for (int i = 0; i < game_workers_; ++i)
            {
                game_threads_.emplace_back([this] { game_worker_loop(); });
            }
            for (int i = 0; i < game_workers_; ++i)
            {
                helper_threads_.emplace_back([this] { helper_worker_loop(); });
            }
        }
        catch (...)
        {
            stopping_.store(true, std::memory_order_release);
            job_cv_.notify_all();
            helper_cv_.notify_all();
            for (std::thread& t : game_threads_)
            {
                if (t.joinable())
                {
                    t.join();
                }
            }
            for (std::thread& t : helper_threads_)
            {
                if (t.joinable())
                {
                    t.join();
                }
            }
            game_threads_.clear();
            helper_threads_.clear();
            throw;
        }
    }

    GameExecutor::~GameExecutor()
    {
        try
        {
            shutdown();
        }
        catch (...)
        {
            std::terminate();
        }
    }

    std::vector<int> GameExecutor::run(std::vector<GameJob> jobs)
    {
        if (tl_role != WorkerRole::None)
        {
            throw std::runtime_error("run called from an executor thread");
        }
        std::lock_guard<std::mutex> run_lock(run_mutex_);
        if (draining_.load(std::memory_order_acquire) || stopping_.load(std::memory_order_acquire))
        {
            throw std::runtime_error("executor has been shut down");
        }
        auto state = std::make_shared<RunState>();
        state->results.assign(jobs.size(), outcome_abandoned);
        state->remaining = jobs.size();
        if (!jobs.empty())
        {
            std::vector<std::shared_ptr<Task>> staged;
            staged.reserve(jobs.size());
            for (std::size_t i = 0; i < jobs.size(); ++i)
            {
                auto task = std::make_shared<Task>();
                task->index = i;
                task->job = std::move(jobs[i]);
                task->state = state;
                staged.push_back(std::move(task));
            }
            {
                std::lock_guard<std::mutex> job_lock(job_mutex_);
                if (draining_.load(std::memory_order_acquire)
                    || stopping_.load(std::memory_order_acquire))
                {
                    throw std::runtime_error("executor has been shut down");
                }
                std::size_t published = 0;
                try
                {
                    for (std::shared_ptr<Task> const& task : staged)
                    {
                        pending_.push_back(task);
                        ++published;
                    }
                }
                catch (...)
                {
                    while (published > 0)
                    {
                        pending_.pop_back();
                        --published;
                    }
                    throw;
                }
            }
            job_cv_.notify_all();
            std::unique_lock<std::mutex> state_lock(state->mutex);
            state->cv.wait(state_lock, [&] { return state->remaining == 0; });
        }
        return std::move(state->results);
    }

    void GameExecutor::shutdown()
    {
        if (tl_role != WorkerRole::None)
        {
            throw std::runtime_error("shutdown called from an executor thread");
        }
        {
            std::lock_guard<std::mutex> run_lock(run_mutex_);
            std::lock_guard<std::mutex> job_lock(job_mutex_);
            draining_.store(true, std::memory_order_release);
        }
        job_cv_.notify_all();
        helper_cv_.notify_all();
        for (std::thread& t : game_threads_)
        {
            if (t.joinable())
            {
                t.join();
            }
        }
        for (std::thread& t : helper_threads_)
        {
            if (t.joinable())
            {
                t.join();
            }
        }
        game_threads_.clear();
        helper_threads_.clear();
    }

    void GameExecutor::request_stop()
    {
        if (stopping_.exchange(true, std::memory_order_acq_rel))
        {
            return;
        }
        std::vector<std::shared_ptr<Task>> abandoned_jobs;
        std::vector<std::shared_ptr<HelperTask>> abandoned_helpers;
        {
            std::lock_guard<std::mutex> job_lock(job_mutex_);
            abandoned_jobs.assign(std::make_move_iterator(pending_.begin()),
                                  std::make_move_iterator(pending_.end()));
            pending_.clear();
        }
        {
            std::lock_guard<std::mutex> helper_lock(helper_mutex_);
            abandoned_helpers.assign(std::make_move_iterator(helper_queue_.begin()),
                                     std::make_move_iterator(helper_queue_.end()));
            helper_queue_.clear();
        }
        for (std::shared_ptr<Task> const& task : abandoned_jobs)
        {
            std::lock_guard<std::mutex> state_lock(task->state->mutex);
            task->state->results[task->index] = outcome_abandoned;
            if (--task->state->remaining == 0)
            {
                task->state->cv.notify_all();
            }
        }
        for (std::shared_ptr<HelperTask> const& helper : abandoned_helpers)
        {
            if (helper->abandon)
            {
                helper->abandon();
            }
        }
        job_cv_.notify_all();
        helper_cv_.notify_all();
    }

    void GameExecutor::game_worker_loop()
    {
        tl_role = WorkerRole::Game;
        tl_owner = this;
        for (;;)
        {
            std::shared_ptr<Task> task;
            {
                std::unique_lock<std::mutex> job_lock(job_mutex_);
                job_cv_.wait(job_lock, [&]
                {
                    return !pending_.empty() || draining_.load(std::memory_order_acquire)
                        || stopping_.load(std::memory_order_acquire);
                });
                if (pending_.empty())
                {
                    if (draining_.load(std::memory_order_acquire)
                        || stopping_.load(std::memory_order_acquire))
                    {
                        break;
                    }
                    continue;
                }
                task = std::move(pending_.front());
                pending_.pop_front();
            }
            if (stopping_.load(std::memory_order_acquire))
            {
                std::lock_guard<std::mutex> state_lock(task->state->mutex);
                task->state->results[task->index] = outcome_abandoned;
                if (--task->state->remaining == 0)
                {
                    task->state->cv.notify_all();
                }
                continue;
            }
            int const active = active_games_.fetch_add(1, std::memory_order_acq_rel) + 1;
            record_peak(peak_active_games_, active);
            games_started_.fetch_add(1, std::memory_order_relaxed);
            int outcome = outcome_failed;
            {
                RunPair pair(this);
                try
                {
                    if (task->job.body)
                    {
                        outcome = task->job.body(pair);
                    }
                }
                catch (...)
                {
                    outcome = outcome_failed;
                }
            }
            active_games_.fetch_sub(1, std::memory_order_acq_rel);
            games_completed_.fetch_add(1, std::memory_order_relaxed);
            {
                std::lock_guard<std::mutex> state_lock(task->state->mutex);
                task->state->results[task->index] = outcome;
                if (--task->state->remaining == 0)
                {
                    task->state->cv.notify_all();
                }
            }
        }
        tl_role = WorkerRole::None;
        tl_owner = nullptr;
    }

    void GameExecutor::helper_worker_loop()
    {
        tl_role = WorkerRole::Helper;
        tl_owner = this;
        for (;;)
        {
            std::shared_ptr<HelperTask> task;
            {
                std::unique_lock<std::mutex> helper_lock(helper_mutex_);
                helper_cv_.wait(helper_lock, [&]
                {
                    return !helper_queue_.empty() || draining_.load(std::memory_order_acquire)
                        || stopping_.load(std::memory_order_acquire);
                });
                if (helper_queue_.empty())
                {
                    if (draining_.load(std::memory_order_acquire)
                        || stopping_.load(std::memory_order_acquire))
                    {
                        break;
                    }
                    continue;
                }
                task = std::move(helper_queue_.front());
                helper_queue_.pop_front();
            }
            if (stopping_.load(std::memory_order_acquire))
            {
                if (task->abandon)
                {
                    task->abandon();
                }
                continue;
            }
            bool permit_held = false;
            try
            {
                leaf_permits_.acquire();
                permit_held = true;
            }
            catch (...)
            {
            }
            if (permit_held)
            {
                int const leaves = concurrent_leaves_.fetch_add(1, std::memory_order_acq_rel) + 1;
                record_peak(peak_concurrent_leaves_, leaves);
                try
                {
                    if (task->fn)
                    {
                        task->fn();
                    }
                    else if (task->abandon)
                    {
                        task->abandon();
                    }
                }
                catch (...)
                {
                }
                concurrent_leaves_.fetch_sub(1, std::memory_order_acq_rel);
                leaf_permits_.release();
            }
            else if (task->abandon)
            {
                task->abandon();
            }
        }
        tl_role = WorkerRole::None;
        tl_owner = nullptr;
    }

    void GameExecutor::execute_pair(LeafFn first, LeafFn second)
    {
        if (tl_role != WorkerRole::Game || tl_owner != this)
        {
            throw std::runtime_error("run_pair called outside a game worker");
        }
        if (tl_pair_depth != 0)
        {
            throw std::runtime_error("run_pair called inside a leaf callback");
        }
        if (stopping_.load(std::memory_order_acquire))
        {
            throw std::runtime_error("executor has been stopped");
        }
        ++tl_pair_depth;
        struct PairDepthGuard
        {
            int* depth;
            ~PairDepthGuard()
            {
                --*depth;
            }
        } depth_guard{&tl_pair_depth};

        auto done = std::make_shared<std::promise<void>>();
        std::future<void> completed = done->get_future();
        LeafFn side = std::move(second);
        auto helper = std::make_shared<HelperTask>();
        helper->fn = [side = std::move(side), done]() mutable
        {
            try
            {
                if (side)
                {
                    side();
                }
                done->set_value();
            }
            catch (...)
            {
                try
                {
                    done->set_exception(std::current_exception());
                }
                catch (...)
                {
                }
            }
        };
        helper->abandon = [done]()
        {
            try
            {
                done->set_exception(
                    std::make_exception_ptr(std::runtime_error("executor was stopped")));
            }
            catch (...)
            {
            }
        };
        {
            std::lock_guard<std::mutex> helper_lock(helper_mutex_);
            if (stopping_.load(std::memory_order_acquire))
            {
                throw std::runtime_error("executor has been stopped");
            }
            helper_queue_.push_back(std::move(helper));
        }
        helper_cv_.notify_one();

        std::exception_ptr error;
        leaf_permits_.acquire();
        int const leaves = concurrent_leaves_.fetch_add(1, std::memory_order_acq_rel) + 1;
        record_peak(peak_concurrent_leaves_, leaves);
        try
        {
            if (first)
            {
                first();
            }
        }
        catch (...)
        {
            error = std::current_exception();
        }
        concurrent_leaves_.fetch_sub(1, std::memory_order_acq_rel);
        leaf_permits_.release();
        try
        {
            completed.get();
        }
        catch (...)
        {
            if (!error)
            {
                error = std::current_exception();
            }
        }
        turns_executed_.fetch_add(1, std::memory_order_relaxed);
        if (error)
        {
            std::rethrow_exception(error);
        }
    }

    ExecutorStats GameExecutor::stats() const
    {
        ExecutorStats result;
        result.games_started = games_started_.load(std::memory_order_relaxed);
        result.games_completed = games_completed_.load(std::memory_order_relaxed);
        result.turns_executed = turns_executed_.load(std::memory_order_relaxed);
        result.peak_active_games = peak_active_games_.load(std::memory_order_relaxed);
        result.peak_concurrent_leaves = peak_concurrent_leaves_.load(std::memory_order_relaxed);
        return result;
    }

    std::vector<int> run_wave(GameExecutor& executor, std::vector<WaveSlot> const& wave,
                              WaveJobFactory const& factory)
    {
        std::vector<GameJob> jobs;
        jobs.reserve(wave.size());
        for (WaveSlot const& slot : wave)
        {
            jobs.push_back(factory ? factory(slot) : GameJob{});
        }
        return executor.run(std::move(jobs));
    }
}
