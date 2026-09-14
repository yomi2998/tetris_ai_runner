#pragma once

#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace tournament_bracket
{
    using CandidateId = std::uint64_t;

    inline constexpr CandidateId kNoCandidate = std::numeric_limits<std::uint64_t>::max();

    inline constexpr int kMaxEntrants = 1 << 20;

    enum class NodeKind : int
    {
        Winners,
        Losers,
        GrandFinal,
        GrandFinalReset,
    };

    enum class Stage : int
    {
        Early,
        Top8,
        WinnersFinal,
        LosersFinal,
        GrandFinal,
        GrandFinalReset,
    };

    enum class SeriesStatus : int
    {
        Pending,
        Ready,
        Complete,
        Walkover,
        Void,
        Dormant,
    };

    enum class GameWinner : int
    {
        SideA = 0,
        SideB = 1,
        Draw = 2,
    };

    enum class ReportStatus : int
    {
        Accepted,
        UnknownSeries,
        NotReady,
        GameIndexMismatch,
        InvalidWinner,
    };

    enum class CreateStatus : int
    {
        Ok,
        EmptyRoster,
        SingleEntrant,
        DuplicateEntrant,
        ReservedEntrant,
        TooManyEntrants,
    };

    struct SeriesFormat
    {
        int sets_to_win = 1;
        int first_to = 7;
    };

    struct SeriesView
    {
        int id = 0;
        NodeKind kind = NodeKind::Winners;
        Stage stage = Stage::Early;
        SeriesFormat format{};
        SeriesStatus status = SeriesStatus::Pending;
        int round = 0;
        CandidateId side_a = kNoCandidate;
        CandidateId side_b = kNoCandidate;
        int sets_a = 0;
        int sets_b = 0;
        int games_a = 0;
        int games_b = 0;
        int games_played = 0;
        CandidateId winner = kNoCandidate;
        CandidateId loser = kNoCandidate;
    };

    struct ReplayEntry
    {
        int series_id = 0;
        int game_index = 0;
        GameWinner winner = GameWinner::Draw;

        bool operator==(ReplayEntry const &) const = default;
    };

    class Bracket
    {
    public:
        static Bracket create(std::vector<CandidateId> const &seed_order);

        ~Bracket();
        Bracket(Bracket &&other) noexcept;
        Bracket &operator=(Bracket &&other) noexcept;

        Bracket(Bracket const &) = delete;
        Bracket &operator=(Bracket const &) = delete;

        bool valid() const;
        CreateStatus create_status() const;
        int entrant_count() const;
        int slot_count() const;
        int bye_count() const;
        int series_count() const;
        SeriesView series(int id) const;
        std::vector<int> ready_series() const;

        ReportStatus report_game(int series_id, int game_index, GameWinner winner);

        bool complete() const;
        CandidateId champion() const;
        int losses(CandidateId candidate) const;
        std::vector<CandidateId> standings() const;
        std::vector<CandidateId> const &seed_order() const;

        std::vector<ReplayEntry> replay_entries() const;

    private:
        struct State;

        std::unique_ptr<State> state_;
        mutable std::mutex mutex_;

        Bracket() = default;
    };

    ReportStatus apply_replay(Bracket &bracket, std::vector<ReplayEntry> const &entries);

    std::vector<std::uint8_t> encode_replay_log(std::vector<ReplayEntry> const &entries);

    std::optional<std::vector<ReplayEntry>> decode_replay_log(std::vector<std::uint8_t> const &bytes);
}
