#pragma once

#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <vector>

#include "tournament/transport.h"
#include "tournament/wire.h"

namespace tournament_loopback
{
    using tournament_wire::AssignmentBatch;
    using tournament_wire::DeviceId;
    using tournament_wire::SignedResult;

    using DeviceHandler = std::function<std::optional<tournament_wire::SignedResult>(
        tournament_wire::AssignmentBatch const &)>;

    class LoopbackTransport : public tournament_transport::Transport
    {
    public:
        void add_device(DeviceId device, DeviceHandler handler);
        void remove_device(DeviceId device);

        std::vector<DeviceId> devices() const override;
        tournament_transport::Delivery request(DeviceId device, AssignmentBatch const &assignment) override;

    private:
        mutable std::mutex mutex_;
        std::map<DeviceId, DeviceHandler> handlers_;
    };
}
