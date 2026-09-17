#include "tournament/loopback.h"

#include <utility>

namespace tournament_loopback
{
    void LoopbackTransport::add_device(DeviceId device, DeviceHandler handler)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        handlers_.insert_or_assign(device, std::move(handler));
    }

    void LoopbackTransport::remove_device(DeviceId device)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        handlers_.erase(device);
    }

    std::vector<DeviceId> LoopbackTransport::devices() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<DeviceId> ids;
        ids.reserve(handlers_.size());
        for (auto const &entry : handlers_)
        {
            ids.push_back(entry.first);
        }
        return ids;
    }

    tournament_transport::Delivery LoopbackTransport::request(DeviceId device,
                                                              AssignmentBatch const &assignment,
                                                              std::uint64_t)
    {
        DeviceHandler handler;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto const it = handlers_.find(device);
            if (it == handlers_.end())
            {
                return {};
            }
            handler = it->second;
        }

        std::optional<SignedResult> response;
        try
        {
            response = handler(assignment);
        }
        catch (...)
        {
            return {tournament_transport::DeliveryStatus::Malformed, {}};
        }
        if (!response)
        {
            return {tournament_transport::DeliveryStatus::Timeout, {}};
        }
        return {tournament_transport::DeliveryStatus::Delivered, std::move(*response)};
    }
}
