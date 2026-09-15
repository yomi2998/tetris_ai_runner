#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

#include "tournament/wire.h"

namespace tournament_provenance
{
    using tournament_wire::DeviceId;
    using tournament_wire::GameId;
    using tournament_wire::Nonce;
    using tournament_wire::Signature;
    using tournament_wire::WireGame;
    using tournament_wire::WireOutcome;

    struct ProvenanceRecord
    {
        GameId game_id = 0;
        DeviceId device = 0;
        Nonce nonce = 0;
        Signature signature;
        WireGame game;
        WireOutcome reported;
        std::uint64_t assigned_at_ms = 0;
        std::uint64_t accepted_at_ms = 0;
    };

    class ProvenanceLedger
    {
    public:
        bool record(ProvenanceRecord &&entry)
        {
            if (records_.find(entry.game_id) != records_.end())
            {
                return false;
            }
            GameId const id = entry.game_id;
            records_.emplace(id, std::move(entry));
            return true;
        }

        ProvenanceRecord const *find(GameId game_id) const
        {
            auto const it = records_.find(game_id);
            return it == records_.end() ? nullptr : &it->second;
        }

        std::vector<ProvenanceRecord const *> entries() const
        {
            std::vector<ProvenanceRecord const *> listed;
            listed.reserve(records_.size());
            for (auto const &pair : records_)
            {
                listed.push_back(&pair.second);
            }
            std::sort(listed.begin(), listed.end(),
                      [](ProvenanceRecord const *a, ProvenanceRecord const *b)
                      {
                          return a->game_id < b->game_id;
                      });
            return listed;
        }

        std::vector<GameId> games_of_device(DeviceId device) const
        {
            std::vector<GameId> games;
            for (auto const &pair : records_)
            {
                if (pair.second.device == device)
                {
                    games.push_back(pair.first);
                }
            }
            std::sort(games.begin(), games.end());
            return games;
        }

        std::size_t size() const
        {
            return records_.size();
        }

        bool empty() const
        {
            return records_.empty();
        }

    private:
        std::unordered_map<GameId, ProvenanceRecord> records_;
    };
}
