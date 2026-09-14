#include "tournament/bracket.h"

#include <algorithm>
#include <deque>
#include <limits>
#include <unordered_map>

namespace tournament_bracket
{
    namespace
    {
        int next_power_of_two(int value)
        {
            int p = 1;
            while (p < value)
            {
                p <<= 1;
            }
            return p;
        }

        int level_count(int slots)
        {
            int levels = 0;
            while ((1 << levels) < slots)
            {
                ++levels;
            }
            return levels;
        }

        std::vector<int> balanced_seed_positions(int slots)
        {
            std::vector<int> order{0};
            while (static_cast<int>(order.size()) < slots)
            {
                int const width = static_cast<int>(order.size()) * 2;
                std::vector<int> next;
                next.reserve(static_cast<size_t>(width));
                for (int seed : order)
                {
                    next.push_back(seed);
                    next.push_back(width - 1 - seed);
                }
                order = std::move(next);
            }
            return order;
        }

        SeriesFormat format_for_stage(Stage stage)
        {
            switch (stage)
            {
            case Stage::Early:
                return SeriesFormat{1, 7};
            case Stage::Top8:
                return SeriesFormat{1, 11};
            case Stage::WinnersFinal:
            case Stage::LosersFinal:
                return SeriesFormat{2, 11};
            case Stage::GrandFinal:
            case Stage::GrandFinalReset:
            default:
                return SeriesFormat{3, 11};
            }
        }

        bool winner_valid(GameWinner winner)
        {
            switch (winner)
            {
            case GameWinner::SideA:
            case GameWinner::SideB:
            case GameWinner::Draw:
                return true;
            default:
                return false;
            }
        }

        void put_varint(std::vector<std::uint8_t> &out, std::uint64_t value)
        {
            while (value >= 0x80)
            {
                out.push_back(static_cast<std::uint8_t>((value & 0x7F) | 0x80));
                value >>= 7;
            }
            out.push_back(static_cast<std::uint8_t>(value));
        }
    }

    struct Bracket::State
    {
        struct InputKind
        {
            enum class Value : int
            {
                None,
                Entrant,
                Bye,
                WinnerOf,
                DropperOf,
            };
        };

        struct InputRef
        {
            InputKind::Value kind = InputKind::Value::None;
            int node = -1;
            int seed = -1;
        };

        struct OutputRef
        {
            int node = -1;
            int slot = -1;
        };

        struct SlotState
        {
            bool filled = false;
            bool is_void = false;
            CandidateId candidate = kNoCandidate;
        };

        struct Node
        {
            SeriesView view;
            InputRef inputs[2];
            SlotState slots[2];
            OutputRef winner_out{};
            OutputRef dropper_out{};
        };

        struct Elimination
        {
            CandidateId candidate = kNoCandidate;
            int rank = 0;
            int series_id = -1;
        };

        bool valid = false;
        CreateStatus status = CreateStatus::EmptyRoster;
        int entrants = 0;
        int slots = 0;
        int byes = 0;
        int levels = 0;
        int lb_rounds = 0;
        int grand_final_id = -1;
        int reset_id = -1;
        std::vector<CandidateId> seed_order_entries;
        std::vector<Node> nodes;
        std::vector<Elimination> eliminations;
        std::vector<ReplayEntry> replay_log;
        std::unordered_map<CandidateId, int> loss_counts;
        CandidateId champion = kNoCandidate;

        int loss_count(CandidateId candidate) const
        {
            auto const it = loss_counts.find(candidate);
            return it != loss_counts.end() ? it->second : 0;
        }

        void add_loss(CandidateId candidate)
        {
            ++loss_counts[candidate];
        }

        void record_elimination(CandidateId candidate, int rank, int series_id)
        {
            eliminations.push_back(Elimination{candidate, rank, series_id});
        }

        int lb_match_count(int round) const
        {
            return round % 2 == 1 ? (slots >> ((round + 3) / 2)) : (slots >> ((round + 2) / 2));
        }

        int wb_base(int round) const
        {
            return slots - (slots >> (round - 1));
        }

        int lb_base(int round) const
        {
            int base = 0;
            for (int k = 1; k < round; ++k)
            {
                base += lb_match_count(k);
            }
            return base;
        }

        int wb_node(int round, int index) const
        {
            return wb_base(round) + index;
        }

        int lb_node(int round, int index) const
        {
            return (slots - 1) + lb_base(round) + index;
        }

        void route(OutputRef const &out, bool is_void, CandidateId candidate, std::deque<int> &pending)
        {
            if (out.node < 0)
            {
                return;
            }
            SlotState &slot = nodes[static_cast<size_t>(out.node)].slots[static_cast<size_t>(out.slot)];
            slot.filled = true;
            slot.is_void = is_void;
            slot.candidate = candidate;
            pending.push_back(out.node);
        }

        void try_resolve(int id, std::deque<int> &pending)
        {
            Node &nd = nodes[static_cast<size_t>(id)];
            if (nd.view.status != SeriesStatus::Pending)
            {
                return;
            }
            if (!nd.slots[0].filled || !nd.slots[1].filled)
            {
                return;
            }
            bool const a_real = !nd.slots[0].is_void;
            bool const b_real = !nd.slots[1].is_void;
            if (!a_real && !b_real)
            {
                nd.view.status = SeriesStatus::Void;
                route(nd.winner_out, true, kNoCandidate, pending);
                route(nd.dropper_out, true, kNoCandidate, pending);
            }
            else if (!a_real || !b_real)
            {
                nd.view.status = SeriesStatus::Walkover;
                nd.view.winner = a_real ? nd.slots[0].candidate : nd.slots[1].candidate;
                route(nd.winner_out, false, nd.view.winner, pending);
                route(nd.dropper_out, true, kNoCandidate, pending);
            }
            else
            {
                nd.view.status = SeriesStatus::Ready;
                nd.view.side_a = nd.slots[0].candidate;
                nd.view.side_b = nd.slots[1].candidate;
            }
        }

        void process(std::deque<int> &pending)
        {
            while (!pending.empty())
            {
                int const id = pending.front();
                pending.pop_front();
                try_resolve(id, pending);
            }
        }

        void set_series_meta(Node &nd, NodeKind kind, Stage stage, int round, int id)
        {
            nd.view.id = id;
            nd.view.kind = kind;
            nd.view.stage = stage;
            nd.view.format = format_for_stage(stage);
            nd.view.status = SeriesStatus::Pending;
            nd.view.round = round;
        }

        void construct(std::vector<CandidateId> const &seed_order_list)
        {
            seed_order_entries = seed_order_list;
            if (seed_order_list.size() > static_cast<size_t>(kMaxEntrants))
            {
                status = CreateStatus::TooManyEntrants;
                return;
            }
            if (seed_order_list.empty())
            {
                status = CreateStatus::EmptyRoster;
                return;
            }
            if (seed_order_list.size() == 1)
            {
                status = CreateStatus::SingleEntrant;
                return;
            }
            entrants = static_cast<int>(seed_order_list.size());
            for (CandidateId const candidate : seed_order_list)
            {
                if (candidate == kNoCandidate)
                {
                    status = CreateStatus::ReservedEntrant;
                    return;
                }
            }
            {
                std::vector<CandidateId> sorted = seed_order_list;
                std::sort(sorted.begin(), sorted.end());
                if (std::adjacent_find(sorted.begin(), sorted.end()) != sorted.end())
                {
                    status = CreateStatus::DuplicateEntrant;
                    return;
                }
            }
            valid = true;
            status = CreateStatus::Ok;
            slots = next_power_of_two(entrants);
            byes = slots - entrants;
            levels = level_count(slots);
            lb_rounds = slots >= 4 ? 2 * levels - 2 : 0;

            int const total = 2 * slots - 1;
            nodes.resize(static_cast<size_t>(total));
            grand_final_id = (slots - 1) + (slots - 2);
            reset_id = total - 1;

            std::vector<int> const positions = balanced_seed_positions(slots);

            for (int r = 1; r <= levels; ++r)
            {
                int const count = slots >> r;
                for (int m = 0; m < count; ++m)
                {
                    int const id = wb_node(r, m);
                    Node &nd = nodes[static_cast<size_t>(id)];
                    Stage const stage = r == levels ? Stage::WinnersFinal
                        : r == levels - 1 ? Stage::Top8
                                          : Stage::Early;
                    set_series_meta(nd, NodeKind::Winners, stage, r, id);
                    if (r == 1)
                    {
                        for (int slot = 0; slot < 2; ++slot)
                        {
                            int const seed = positions[static_cast<size_t>(2 * m + slot)];
                            nd.inputs[slot].seed = seed;
                            nd.inputs[slot].kind = seed < entrants ? InputKind::Value::Entrant
                                                                   : InputKind::Value::Bye;
                        }
                    }
                    else
                    {
                        nd.inputs[0].kind = InputKind::Value::WinnerOf;
                        nd.inputs[0].node = wb_node(r - 1, 2 * m);
                        nd.inputs[1].kind = InputKind::Value::WinnerOf;
                        nd.inputs[1].node = wb_node(r - 1, 2 * m + 1);
                    }
                    if (r < levels)
                    {
                        nd.winner_out = OutputRef{wb_node(r + 1, m / 2), m % 2};
                    }
                    else
                    {
                        nd.winner_out = OutputRef{grand_final_id, 0};
                    }
                    if (lb_rounds == 0)
                    {
                        nd.dropper_out = OutputRef{grand_final_id, 1};
                    }
                    else if (r == 1)
                    {
                        int const partner = (slots >> 1) - 1 - m;
                        int const match = std::min(m, partner);
                        int const slot = m < partner ? 0 : 1;
                        nd.dropper_out = OutputRef{lb_node(1, match), slot};
                    }
                    else
                    {
                        int const lb_round = 2 * r - 2;
                        int const droppers = lb_match_count(lb_round);
                        nd.dropper_out = OutputRef{lb_node(lb_round, droppers - 1 - m), 1};
                    }
                }
            }

            for (int k = 1; k <= lb_rounds; ++k)
            {
                int const count = lb_match_count(k);
                for (int m = 0; m < count; ++m)
                {
                    int const id = lb_node(k, m);
                    Node &nd = nodes[static_cast<size_t>(id)];
                    Stage const stage = k == lb_rounds ? Stage::LosersFinal
                        : k >= 2 * (levels - 3) + 1 ? Stage::Top8
                                                    : Stage::Early;
                    set_series_meta(nd, NodeKind::Losers, stage, k, id);
                    if (k == 1)
                    {
                        nd.inputs[0].kind = InputKind::Value::DropperOf;
                        nd.inputs[0].node = wb_node(1, m);
                        nd.inputs[1].kind = InputKind::Value::DropperOf;
                        nd.inputs[1].node = wb_node(1, (slots >> 1) - 1 - m);
                    }
                    else if (k % 2 == 1)
                    {
                        int const prev_count = lb_match_count(k - 1);
                        nd.inputs[0].kind = InputKind::Value::WinnerOf;
                        nd.inputs[0].node = lb_node(k - 1, m);
                        nd.inputs[1].kind = InputKind::Value::WinnerOf;
                        nd.inputs[1].node = lb_node(k - 1, prev_count - 1 - m);
                    }
                    else
                    {
                        nd.inputs[0].kind = InputKind::Value::WinnerOf;
                        nd.inputs[0].node = lb_node(k - 1, m);
                        nd.inputs[1].kind = InputKind::Value::DropperOf;
                        nd.inputs[1].node = wb_node(k / 2 + 1, count - 1 - m);
                    }
                    if (k < lb_rounds)
                    {
                        if (k % 2 == 1)
                        {
                            nd.winner_out = OutputRef{lb_node(k + 1, m), 0};
                        }
                        else
                        {
                            int const own = count;
                            int const match = std::min(m, own - 1 - m);
                            int const slot = m < own - 1 - m ? 0 : 1;
                            nd.winner_out = OutputRef{lb_node(k + 1, match), slot};
                        }
                    }
                    else
                    {
                        nd.winner_out = OutputRef{grand_final_id, 1};
                    }
                }
            }

            {
                Node &nd = nodes[static_cast<size_t>(grand_final_id)];
                set_series_meta(nd, NodeKind::GrandFinal, Stage::GrandFinal, 0, grand_final_id);
                nd.inputs[0].kind = InputKind::Value::WinnerOf;
                nd.inputs[0].node = wb_node(levels, 0);
                if (lb_rounds >= 1)
                {
                    nd.inputs[1].kind = InputKind::Value::WinnerOf;
                    nd.inputs[1].node = lb_node(lb_rounds, 0);
                }
            }
            {
                Node &nd = nodes[static_cast<size_t>(reset_id)];
                set_series_meta(nd, NodeKind::GrandFinalReset, Stage::GrandFinalReset, 0, reset_id);
                nd.view.status = SeriesStatus::Dormant;
            }

            std::deque<int> pending;
            for (int m = 0; m < (slots >> 1); ++m)
            {
                Node &nd = nodes[static_cast<size_t>(wb_node(1, m))];
                for (int slot = 0; slot < 2; ++slot)
                {
                    if (nd.inputs[slot].kind == InputKind::Value::Entrant)
                    {
                        nd.slots[slot] = SlotState{true, false, seed_order_entries[static_cast<size_t>(nd.inputs[slot].seed)]};
                    }
                    else
                    {
                        nd.slots[slot] = SlotState{true, true, kNoCandidate};
                    }
                }
                pending.push_back(wb_node(1, m));
            }
            process(pending);
        }

        void complete_series(int id)
        {
            Node &nd = nodes[static_cast<size_t>(id)];
            nd.view.status = SeriesStatus::Complete;
            bool const a_won = nd.view.sets_a >= nd.view.format.sets_to_win;
            nd.view.winner = a_won ? nd.view.side_a : nd.view.side_b;
            nd.view.loser = a_won ? nd.view.side_b : nd.view.side_a;
            add_loss(nd.view.loser);
            std::deque<int> pending;
            if (nd.view.kind == NodeKind::Winners)
            {
                route(nd.winner_out, false, nd.view.winner, pending);
                route(nd.dropper_out, false, nd.view.loser, pending);
            }
            else if (nd.view.kind == NodeKind::Losers)
            {
                route(nd.winner_out, false, nd.view.winner, pending);
                record_elimination(nd.view.loser, 2 + (lb_rounds - nd.view.round), id);
            }
            else if (nd.view.kind == NodeKind::GrandFinal)
            {
                if (loss_count(nd.view.loser) >= 2)
                {
                    record_elimination(nd.view.loser, 1, id);
                    champion = nd.view.winner;
                }
                else
                {
                    Node &reset = nodes[static_cast<size_t>(reset_id)];
                    reset.view.status = SeriesStatus::Ready;
                    reset.view.side_a = nd.view.side_a;
                    reset.view.side_b = nd.view.side_b;
                }
            }
            else
            {
                record_elimination(nd.view.loser, 1, id);
                champion = nd.view.winner;
            }
            process(pending);
        }
    };

    Bracket Bracket::create(std::vector<CandidateId> const &seed_order)
    {
        Bracket bracket;
        bracket.state_ = std::make_unique<State>();
        bracket.state_->construct(seed_order);
        return bracket;
    }

    Bracket::~Bracket() = default;

    Bracket::Bracket(Bracket &&other) noexcept = default;

    Bracket &Bracket::operator=(Bracket &&other) noexcept = default;

    bool Bracket::valid() const
    {
        return state_ != nullptr && state_->valid;
    }

    int Bracket::entrant_count() const
    {
        return state_ != nullptr ? state_->entrants : 0;
    }

    int Bracket::slot_count() const
    {
        return state_ != nullptr ? state_->slots : 0;
    }

    int Bracket::bye_count() const
    {
        return state_ != nullptr ? state_->byes : 0;
    }

    int Bracket::series_count() const
    {
        return state_ != nullptr ? static_cast<int>(state_->nodes.size()) : 0;
    }

    SeriesView Bracket::series(int id) const
    {
        if (state_ == nullptr || id < 0 || id >= static_cast<int>(state_->nodes.size()))
        {
            return SeriesView{};
        }
        return state_->nodes[static_cast<size_t>(id)].view;
    }

    std::vector<int> Bracket::ready_series() const
    {
        std::vector<int> ready;
        if (state_ == nullptr)
        {
            return ready;
        }
        for (size_t i = 0; i < state_->nodes.size(); ++i)
        {
            if (state_->nodes[i].view.status == SeriesStatus::Ready)
            {
                ready.push_back(static_cast<int>(i));
            }
        }
        return ready;
    }

    ReportStatus Bracket::report_game(int series_id, int game_index, GameWinner winner)
    {
        if (state_ == nullptr || !state_->valid || series_id < 0
            || series_id >= static_cast<int>(state_->nodes.size()))
        {
            return ReportStatus::UnknownSeries;
        }
        if (!winner_valid(winner))
        {
            return ReportStatus::InvalidWinner;
        }
        State::Node &nd = state_->nodes[static_cast<size_t>(series_id)];
        if (nd.view.status != SeriesStatus::Ready)
        {
            return ReportStatus::NotReady;
        }
        if (game_index != nd.view.games_played)
        {
            return ReportStatus::GameIndexMismatch;
        }
        switch (winner)
        {
        case GameWinner::SideA:
            ++nd.view.games_a;
            break;
        case GameWinner::SideB:
            ++nd.view.games_b;
            break;
        case GameWinner::Draw:
            break;
        }
        ++nd.view.games_played;
        state_->replay_log.push_back(ReplayEntry{series_id, game_index, winner});
        int const first_to = nd.view.format.first_to;
        if (nd.view.games_a == first_to || nd.view.games_b == first_to)
        {
            if (nd.view.games_a == first_to)
            {
                ++nd.view.sets_a;
            }
            else
            {
                ++nd.view.sets_b;
            }
            nd.view.games_a = 0;
            nd.view.games_b = 0;
        }
        if (nd.view.sets_a >= nd.view.format.sets_to_win || nd.view.sets_b >= nd.view.format.sets_to_win)
        {
            state_->complete_series(series_id);
        }
        return ReportStatus::Accepted;
    }

    bool Bracket::complete() const
    {
        return state_ != nullptr && state_->champion != kNoCandidate;
    }

    CandidateId Bracket::champion() const
    {
        return state_ != nullptr ? state_->champion : kNoCandidate;
    }

    int Bracket::losses(CandidateId candidate) const
    {
        return state_ != nullptr ? state_->loss_count(candidate) : 0;
    }

    std::vector<CandidateId> Bracket::standings() const
    {
        std::vector<CandidateId> ordered;
        if (state_ == nullptr || !state_->valid)
        {
            return ordered;
        }
        if (state_->champion != kNoCandidate)
        {
            ordered.push_back(state_->champion);
        }
        std::vector<State::Elimination> eliminations = state_->eliminations;
        std::sort(eliminations.begin(), eliminations.end(),
                  [](State::Elimination const &a, State::Elimination const &b)
                  {
                      if (a.rank != b.rank)
                      {
                          return a.rank < b.rank;
                      }
                      return a.series_id < b.series_id;
                  });
        for (State::Elimination const &e : eliminations)
        {
            ordered.push_back(e.candidate);
        }
        return ordered;
    }

    std::vector<CandidateId> const &Bracket::seed_order() const
    {
        static std::vector<CandidateId> const empty;
        return state_ != nullptr ? state_->seed_order_entries : empty;
    }

    CreateStatus Bracket::create_status() const
    {
        return state_ != nullptr ? state_->status : CreateStatus::EmptyRoster;
    }

    std::vector<ReplayEntry> Bracket::replay_entries() const
    {
        if (state_ == nullptr || !state_->valid)
        {
            return {};
        }
        std::vector<ReplayEntry> entries = state_->replay_log;
        std::sort(entries.begin(), entries.end(),
                  [](ReplayEntry const &a, ReplayEntry const &b)
                  {
                      if (a.series_id != b.series_id)
                      {
                          return a.series_id < b.series_id;
                      }
                      return a.game_index < b.game_index;
                  });
        return entries;
    }

    ReportStatus apply_replay(Bracket &bracket, std::vector<ReplayEntry> const &entries)
    {
        for (ReplayEntry const &entry : entries)
        {
            ReportStatus const reported = bracket.report_game(entry.series_id, entry.game_index, entry.winner);
            if (reported != ReportStatus::Accepted)
            {
                return reported;
            }
        }
        return ReportStatus::Accepted;
    }

    std::vector<std::uint8_t> encode_replay_log(std::vector<ReplayEntry> const &entries)
    {
        std::vector<std::uint8_t> bytes;
        for (ReplayEntry const &entry : entries)
        {
            if (entry.series_id < 0 || entry.game_index < 0)
            {
                return {};
            }
            put_varint(bytes, static_cast<std::uint64_t>(entry.series_id));
            put_varint(bytes, static_cast<std::uint64_t>(entry.game_index));
            bytes.push_back(static_cast<std::uint8_t>(static_cast<int>(entry.winner)));
        }
        return bytes;
    }

    std::optional<std::vector<ReplayEntry>> decode_replay_log(std::vector<std::uint8_t> const &bytes)
    {
        std::vector<ReplayEntry> entries;
        size_t pos = 0;
        auto read_varint = [&](std::uint64_t &value)
        {
            value = 0;
            int shift = 0;
            for (;;)
            {
                if (pos >= bytes.size() || shift > 63)
                {
                    return false;
                }
                std::uint8_t const byte = bytes[pos++];
                value |= static_cast<std::uint64_t>(byte & 0x7F) << shift;
                if ((byte & 0x80) == 0)
                {
                    return true;
                }
                shift += 7;
            }
        };
        while (pos < bytes.size())
        {
            std::uint64_t series = 0;
            std::uint64_t game = 0;
            if (!read_varint(series) || !read_varint(game) || pos >= bytes.size())
            {
                return std::nullopt;
            }
            std::uint8_t const raw_winner = bytes[pos++];
            if (raw_winner > 2 || series > static_cast<std::uint64_t>(std::numeric_limits<int>::max())
                || game > static_cast<std::uint64_t>(std::numeric_limits<int>::max()))
            {
                return std::nullopt;
            }
            entries.push_back(ReplayEntry{static_cast<int>(series), static_cast<int>(game),
                                          static_cast<GameWinner>(raw_winner)});
        }
        return entries;
    }
}
