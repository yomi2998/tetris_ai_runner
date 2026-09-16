#pragma once

#include <cstdint>
#include <string>

#include "tournament/provenance.h"

namespace tournament_journal
{
    class ProvenanceJournal
    {
    public:
        explicit ProvenanceJournal(std::string path);
        bool load(tournament_provenance::ProvenanceLedger &ledger, std::string &error);
        bool append(tournament_provenance::ProvenanceRecord const &record, std::string &error);
        bool clear(std::string &error);
        std::uint64_t appended_count() const;

    private:
        std::string path_;
        std::uint64_t appended_count_ = 0;
    };
}
