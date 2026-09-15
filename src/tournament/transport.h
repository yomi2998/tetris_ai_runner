#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <vector>

#include "tournament/wire.h"

namespace tournament_transport
{
    using tournament_wire::AssignmentBatch;
    using tournament_wire::DeviceId;
    using tournament_wire::SignedResult;

    enum class DeliveryStatus
    {
        Delivered,
        Unreachable,
        Timeout,
        Malformed,
    };

    struct Delivery
    {
        DeliveryStatus status = DeliveryStatus::Unreachable;
        SignedResult result{};
    };

    class Transport
    {
    public:
        virtual ~Transport() = default;
        virtual std::vector<DeviceId> devices() const = 0;
        virtual Delivery request(DeviceId device, AssignmentBatch const &assignment) = 0;
    };

    class Clock
    {
    public:
        virtual ~Clock() = default;
        virtual std::uint64_t now_ms() const = 0;
    };

    class SystemClock : public Clock
    {
    public:
        std::uint64_t now_ms() const override
        {
            return static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch())
                    .count());
        }
    };

    class ManualClock : public Clock
    {
    public:
        std::uint64_t now_ms() const override
        {
            std::lock_guard<std::mutex> lock(mutex_);
            return now_ms_;
        }

        void set(std::uint64_t value)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            now_ms_ = value;
        }

        void advance(std::uint64_t delta_ms)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            now_ms_ += delta_ms;
        }

    private:
        mutable std::mutex mutex_;
        std::uint64_t now_ms_ = 0;
    };
}
