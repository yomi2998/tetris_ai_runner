#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "tournament/provenance.h"
#include "tournament/wire.h"
#include "tuning/match.h"

namespace tournament_audit
{
    using ReRun = std::function<std::vector<tuning::GameOutcome>(std::vector<tuning::BatchGame> const &, tuning::RunConfig const &)>;

    struct AuditTarget
    {
        tournament_wire::GameId game = 0;
        tournament_wire::DeviceId device = 0;

        bool operator==(AuditTarget const &) const = default;
    };

    struct AuditVerdict
    {
        AuditTarget target;
        bool passed = false;
        tournament_wire::WireOutcome reported;
        tournament_wire::WireOutcome actual;
    };

    struct AuditReport
    {
        std::vector<AuditVerdict> verdicts;

        std::vector<tournament_wire::DeviceId> failed_devices() const;
    };

    std::vector<AuditTarget> select_targets(tournament_provenance::ProvenanceLedger const &ledger, double sample_rate,
                                            std::vector<tournament_wire::GameId> const &forced, std::uint64_t rng_seed);

    AuditReport audit_records(tournament_provenance::ProvenanceLedger const &ledger, std::vector<AuditTarget> const &targets,
                              tuning::RunConfig const &config, ReRun const &re_run);
}
