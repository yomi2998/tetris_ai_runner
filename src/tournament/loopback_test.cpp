#include "tournament/loopback.h"

#include <atomic>
#include <latch>
#include <mutex>
#include <optional>
#include <print>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace
{
    namespace tl = tournament_loopback;
    namespace tt = tournament_transport;
    namespace tw = tournament_wire;

    int g_checks = 0;
    int g_failures = 0;

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

    tw::AssignmentBatch sample_assignment(tw::DeviceId device)
    {
        tw::AssignmentBatch assignment;
        assignment.nonce = 777;
        assignment.device = device;
        assignment.config.threads = 2;
        assignment.config.iterations_per_move = 500;
        assignment.config.max_rounds = 800;

        tw::WireGame first;
        first.id = 11;
        first.theta_a = {0.1, 0.2};
        first.theta_b = {0.3, 0.4};
        first.seed_a = 1;
        first.seed_b = 2;

        tw::WireGame second;
        second.id = 12;
        second.theta_a = {0.5, 0.6};
        second.theta_b = {0.7, 0.8};
        second.seed_a = 3;
        second.seed_b = 4;

        assignment.games = {first, second};
        return assignment;
    }

    tw::WireOutcome sample_outcome(tw::GameId id, int winner)
    {
        tw::WireOutcome outcome;
        outcome.id = id;
        outcome.winner = winner;
        outcome.rounds = 40 + static_cast<int>(id);
        outcome.app_a = 0.5;
        outcome.app_b = 0.25;
        outcome.apl_a = 100.0;
        outcome.apl_b = 90.0;
        outcome.reason = winner == 1 ? tuning::WinReason::ASurvivor : tuning::WinReason::BSurvivor;
        return outcome;
    }

    tw::SignedResult echo_result(tw::AssignmentBatch const &assignment)
    {
        tw::SignedResult result;
        result.batch.nonce = assignment.nonce;
        result.batch.device = assignment.device;
        result.batch.outcomes.push_back(sample_outcome(assignment.games[0].id, 1));
        result.batch.outcomes.push_back(sample_outcome(assignment.games[1].id, 2));
        return result;
    }

    tl::DeviceHandler silent_handler()
    {
        return [](tw::AssignmentBatch const &)
        {
            return std::nullopt;
        };
    }

    bool same_assignment(tw::AssignmentBatch const &left, tw::AssignmentBatch const &right)
    {
        return left.nonce == right.nonce && left.device == right.device
            && left.config.threads == right.config.threads
            && left.config.iterations_per_move == right.config.iterations_per_move
            && left.config.max_rounds == right.config.max_rounds && left.games == right.games;
    }

    void test_delivered()
    {
        tl::LoopbackTransport transport;
        std::optional<tw::AssignmentBatch> received;
        transport.add_device(42, [&](tw::AssignmentBatch const &assignment)
        {
            received = assignment;
            return echo_result(assignment);
        });

        tw::AssignmentBatch const assignment = sample_assignment(42);
        tt::Delivery const delivery = transport.request(42, assignment);

        check(delivery.status == tt::DeliveryStatus::Delivered, "delivered: status is delivered");
        check(received.has_value(), "delivered: the handler was invoked");
        check(received && same_assignment(*received, assignment),
              "delivered: the handler received the exact assignment contents");

        tw::ResultBatch expected;
        expected.nonce = 777;
        expected.device = 42;
        expected.outcomes.push_back(sample_outcome(11, 1));
        expected.outcomes.push_back(sample_outcome(12, 2));
        check(delivery.result.batch == expected,
              "delivered: the result echoes the nonce, device, and both outcomes");
        check(delivery.result.signature.empty(), "delivered: the signature is empty");
    }

    void test_timeout()
    {
        tl::LoopbackTransport transport;
        transport.add_device(5, silent_handler());
        tt::Delivery const delivery = transport.request(5, sample_assignment(5));
        check(delivery.status == tt::DeliveryStatus::Timeout,
              "timeout: a silent handler reports timeout");
        check(delivery.result.batch.nonce == 0 && delivery.result.batch.device == 0
                  && delivery.result.batch.outcomes.empty(),
              "timeout: the result stays empty");
    }

    void test_unreachable()
    {
        tl::LoopbackTransport transport;
        transport.add_device(3, silent_handler());
        transport.remove_device(3);

        tt::Delivery const unknown = transport.request(77, sample_assignment(77));
        check(unknown.status == tt::DeliveryStatus::Unreachable,
              "unreachable: an unknown id is unreachable");
        check(unknown.result.batch.nonce == 0 && unknown.result.batch.outcomes.empty(),
              "unreachable: an unknown id carries no result");

        tt::Delivery const removed = transport.request(3, sample_assignment(3));
        check(removed.status == tt::DeliveryStatus::Unreachable,
              "unreachable: a removed id is unreachable");
    }

    void test_malformed()
    {
        tl::LoopbackTransport transport;
        transport.add_device(9,
                             [](tw::AssignmentBatch const &) -> std::optional<tw::SignedResult>
                             {
                                 throw std::runtime_error("device exploded");
                             });
        tt::Delivery const delivery = transport.request(9, sample_assignment(9));
        check(delivery.status == tt::DeliveryStatus::Malformed,
              "malformed: a throwing handler reports malformed");
        check(delivery.result.batch.nonce == 0 && delivery.result.batch.outcomes.empty(),
              "malformed: the result stays empty");
    }

    void test_device_listing()
    {
        tl::LoopbackTransport transport;
        check(transport.devices().empty(), "devices: a fresh transport has no devices");
        transport.add_device(30, silent_handler());
        transport.add_device(10, silent_handler());
        transport.add_device(20, silent_handler());
        check(transport.devices() == std::vector<tw::DeviceId>{10, 20, 30},
              "devices: ids come back sorted ascending");
        transport.remove_device(20);
        check(transport.devices() == std::vector<tw::DeviceId>{10, 30},
              "devices: removal takes effect");
        transport.remove_device(999);
        check(transport.devices() == std::vector<tw::DeviceId>{10, 30},
              "devices: removing an unknown id is a no-op");
    }

    void test_concurrent_requests()
    {
        tl::LoopbackTransport transport;
        std::vector<tw::DeviceId> const device_ids = {101, 202};
        for (tw::DeviceId const device : device_ids)
        {
            transport.add_device(device, [device](tw::AssignmentBatch const &assignment)
            {
                tw::SignedResult result;
                result.batch.nonce = assignment.nonce;
                result.batch.device = device;
                return result;
            });
        }

        std::mutex merged_mutex;
        std::vector<std::pair<tw::DeviceId, tt::Delivery>> merged;
        auto const worker = [&]
        {
            std::vector<std::pair<tw::DeviceId, tt::Delivery>> local;
            for (int round = 0; round < 25; ++round)
            {
                for (tw::DeviceId const device : device_ids)
                {
                    local.emplace_back(device, transport.request(device, sample_assignment(device)));
                }
            }
            std::lock_guard<std::mutex> lock(merged_mutex);
            merged.insert(merged.end(), local.begin(), local.end());
        };

        std::vector<std::thread> threads;
        for (int i = 0; i < 4; ++i)
        {
            threads.emplace_back(worker);
        }
        for (std::thread &thread : threads)
        {
            thread.join();
        }

        check(merged.size() == 200, "concurrency: every request produced a delivery");
        bool all_delivered = true;
        bool all_tagged = true;
        for (auto const &entry : merged)
        {
            if (entry.second.status != tt::DeliveryStatus::Delivered)
            {
                all_delivered = false;
            }
            if (entry.second.result.batch.device != entry.first
                || entry.second.result.batch.nonce != 777)
            {
                all_tagged = false;
            }
        }
        check(all_delivered, "concurrency: all deliveries report delivered");
        check(all_tagged, "concurrency: every result carries its device tag and nonce");
    }

    void test_handler_overlap()
    {
        tl::LoopbackTransport transport;
        std::latch overlap(2);
        auto const blocking_handler = [&](tw::AssignmentBatch const &assignment)
        {
            overlap.arrive_and_wait();
            return echo_result(assignment);
        };
        transport.add_device(7, blocking_handler);
        transport.add_device(8, blocking_handler);

        std::vector<tt::Delivery> deliveries(2);
        std::thread first([&]
        {
            deliveries[0] = transport.request(7, sample_assignment(7));
        });
        std::thread second([&]
        {
            deliveries[1] = transport.request(8, sample_assignment(8));
        });
        first.join();
        second.join();

        check(deliveries[0].status == tt::DeliveryStatus::Delivered,
              "overlap: the first blocked request was delivered");
        check(deliveries[1].status == tt::DeliveryStatus::Delivered,
              "overlap: the second blocked request was delivered");
        check(deliveries[0].result.batch.device == 7 && deliveries[0].result.batch.nonce == 777,
              "overlap: the first result carries its device tag");
        check(deliveries[1].result.batch.device == 8 && deliveries[1].result.batch.nonce == 777,
              "overlap: the second result carries its device tag");
    }

    void test_manual_clock()
    {
        tt::ManualClock clock;
        check(clock.now_ms() == 0, "clock: a fresh manual clock reads zero");
        clock.set(100);
        check(clock.now_ms() == 100, "clock: set takes effect");
        clock.advance(50);
        check(clock.now_ms() == 150, "clock: advance adds to the current value");

        std::atomic<bool> violation{false};
        auto const reader = [&]
        {
            std::uint64_t last = clock.now_ms();
            for (int i = 0; i < 20000; ++i)
            {
                std::uint64_t const now = clock.now_ms();
                if (now < last)
                {
                    violation.store(true, std::memory_order_relaxed);
                }
                last = now;
            }
        };
        auto const advancer = [&]
        {
            for (int i = 0; i < 10000; ++i)
            {
                clock.advance(1);
            }
        };

        std::vector<std::thread> threads;
        for (int i = 0; i < 2; ++i)
        {
            threads.emplace_back(reader);
        }
        for (int i = 0; i < 2; ++i)
        {
            threads.emplace_back(advancer);
        }
        for (std::thread &thread : threads)
        {
            thread.join();
        }

        check(!violation.load(), "clock: concurrent readers never observe time going backwards");
        check(clock.now_ms() == 150 + 20000, "clock: the final value counts every advance");
    }
}

int main()
{
    test_delivered();
    test_timeout();
    test_unreachable();
    test_malformed();
    test_device_listing();
    test_concurrent_requests();
    test_handler_overlap();
    test_manual_clock();
    std::println("{} checks, {} failures", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
