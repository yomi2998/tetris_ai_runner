#include "tournament/audit.h"

#include <algorithm>
#include <cstddef>
#include <random>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace tournament_audit
{
    std::vector<tournament_wire::DeviceId> AuditReport::failed_devices() const
    {
        std::vector<tournament_wire::DeviceId> devices;
        for (AuditVerdict const &verdict : verdicts)
        {
            if (!verdict.passed)
            {
                devices.push_back(verdict.target.device);
            }
        }
        std::sort(devices.begin(), devices.end());
        devices.erase(std::unique(devices.begin(), devices.end()), devices.end());
        return devices;
    }

    std::vector<AuditTarget> select_targets(tournament_provenance::ProvenanceLedger const &ledger, double sample_rate,
                                            std::vector<tournament_wire::GameId> const &forced, std::uint64_t rng_seed)
    {
        double const clamped = std::max(0.0, std::min(1.0, sample_rate));
        std::uint64_t const threshold = static_cast<std::uint64_t>(clamped * 1000000.0);
        std::unordered_set<tournament_wire::GameId> const forced_ids(forced.begin(), forced.end());
        std::unordered_set<tournament_wire::GameId> selected;
        std::vector<AuditTarget> targets;
        std::mt19937_64 engine(rng_seed);
        for (tournament_provenance::ProvenanceRecord const *entry : ledger.entries())
        {
            if (forced_ids.find(entry->game_id) == forced_ids.end() && engine() % 1000000ULL >= threshold)
            {
                continue;
            }
            if (selected.insert(entry->game_id).second)
            {
                targets.push_back(AuditTarget{entry->game_id, entry->device});
            }
        }
        std::sort(targets.begin(), targets.end(),
                  [](AuditTarget const &a, AuditTarget const &b)
                  {
                      return a.game < b.game;
                  });
        return targets;
    }

    std::vector<AuditTarget> select_targets_rated(tournament_provenance::ProvenanceLedger const &ledger,
                                                  std::function<double(tournament_wire::DeviceId)> const &device_rate,
                                                  std::vector<tournament_wire::GameId> const &forced,
                                                  std::uint64_t rng_seed)
    {
        std::unordered_set<tournament_wire::GameId> const forced_ids(forced.begin(), forced.end());
        std::unordered_set<tournament_wire::GameId> selected;
        std::vector<AuditTarget> targets;
        std::mt19937_64 engine(rng_seed);
        for (tournament_provenance::ProvenanceRecord const *entry : ledger.entries())
        {
            if (forced_ids.find(entry->game_id) == forced_ids.end())
            {
                double const rate = std::max(0.0, std::min(1.0, device_rate(entry->device)));
                std::uint64_t const threshold = static_cast<std::uint64_t>(rate * 1000000.0);
                if (engine() % 1000000ULL >= threshold)
                {
                    continue;
                }
            }
            if (selected.insert(entry->game_id).second)
            {
                targets.push_back(AuditTarget{entry->game_id, entry->device});
            }
        }
        std::sort(targets.begin(), targets.end(),
                  [](AuditTarget const &a, AuditTarget const &b)
                  {
                      return a.game < b.game;
                  });
        return targets;
    }

    AuditReport audit_records(tournament_provenance::ProvenanceLedger const &ledger, std::vector<AuditTarget> const &targets,
                              tuning::RunConfig const &config, ReRun const &re_run)
    {
        std::vector<AuditTarget> ordered = targets;
        std::sort(ordered.begin(), ordered.end(),
                  [](AuditTarget const &a, AuditTarget const &b)
                  {
                      return a.game < b.game;
                  });
        std::vector<tuning::BatchGame> games;
        std::vector<tournament_wire::WireOutcome> reported;
        games.reserve(ordered.size());
        reported.reserve(ordered.size());
        std::unordered_set<tournament_wire::GameId> requested;
        for (AuditTarget const &target : ordered)
        {
            tournament_provenance::ProvenanceRecord const *record = ledger.find(target.game);
            if (record == nullptr)
            {
                throw std::runtime_error("audit target " + std::to_string(target.game) + " is missing from the ledger");
            }
            games.push_back(tournament_wire::from_wire(record->game));
            reported.push_back(record->reported);
            requested.insert(target.game);
        }
        std::vector<tuning::GameOutcome> const outcomes = re_run(games, config);
        if (outcomes.size() != games.size())
        {
            throw std::runtime_error("re-run outcome count does not match the requested count");
        }
        std::unordered_map<tournament_wire::GameId, tuning::GameOutcome const *> by_id;
        by_id.reserve(outcomes.size());
        for (tuning::GameOutcome const &outcome : outcomes)
        {
            if (requested.find(outcome.id) == requested.end())
            {
                throw std::runtime_error("re-run returned an outcome for unrequested game " + std::to_string(outcome.id));
            }
            if (!by_id.emplace(outcome.id, &outcome).second)
            {
                throw std::runtime_error("re-run returned duplicate outcomes for game " + std::to_string(outcome.id));
            }
        }
        AuditReport report;
        report.verdicts.reserve(ordered.size());
        for (std::size_t i = 0; i < ordered.size(); ++i)
        {
            auto const it = by_id.find(ordered[i].game);
            if (it == by_id.end())
            {
                throw std::runtime_error("re-run result is missing game " + std::to_string(ordered[i].game));
            }
            AuditVerdict verdict;
            verdict.target = ordered[i];
            verdict.reported = reported[i];
            verdict.actual = tournament_wire::to_wire(*it->second);
            verdict.passed = tournament_wire::encode_outcome(verdict.reported)
                == tournament_wire::encode_outcome(verdict.actual);
            report.verdicts.push_back(std::move(verdict));
        }
        return report;
    }
}
