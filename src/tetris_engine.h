#pragma once

#include "tetris_types.h"
#include "toj_rule.h"
#include "toj_policy.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace tetris_engine
{
    using Board = tetris::Board;
    using Piece = tetris::Piece;
    using Placement = tetris::Placement;
    using Candidate = tetris::Candidate;
    using Outcome = tetris::Outcome;
    using PolicyState = toj_policy::State;
    using Evaluation = toj_policy::Evaluation;

    inline constexpr std::uint64_t engine_memory_budget = 256ull << 20;
    inline constexpr std::size_t max_queue_length = 256;
    inline constexpr std::uint64_t max_nodes = 0xFFFFFFFEull;
    inline constexpr std::size_t max_frontiers = max_queue_length + 1;
    inline constexpr std::size_t max_candidates_per_source =
        4 * Board::width * Board::height * 2;
    inline constexpr std::size_t max_children_per_parent = 2 * max_candidates_per_source;
    inline constexpr std::size_t transposition_entries = 8192;
    inline constexpr std::uint8_t no_piece_code = 0xFF;
    inline constexpr std::uint64_t engine_queue_reservation =
        max_queue_length * (sizeof(Piece) + sizeof(bool));
    inline constexpr std::uint64_t engine_stack_peak_allowance = 64ull << 10;

    enum class BranchSource : std::uint8_t
    {
        Current,
        Hold,
    };

    struct Queue
    {
        std::vector<Piece> pieces;
        std::vector<bool> boundary;
        std::size_t marker_count = 0;
    };

    std::optional<Queue> parse_queue(std::string_view text);

    struct HoldState
    {
        std::optional<Piece> piece;
        bool locked = false;
    };

    using NodeId = std::uint32_t;
    inline constexpr NodeId no_node = 0xFFFFFFFFu;
    inline constexpr std::uint64_t engine_frontier_metadata =
        max_frontiers * (2 * sizeof(NodeId) + 2 * sizeof(std::size_t))
        + max_frontiers * sizeof(double);

    struct TranspositionKey
    {
        std::uint16_t depth = 0;
        std::uint16_t cursor = 0;
        std::uint16_t boundary_count = 0;
        std::uint32_t root_child = no_node;
        Board::occupancy_t occupancy{};
        PolicyState state{};
        std::array<std::uint64_t, 4> boundary_bits{};
        std::uint8_t active_piece = no_piece_code;
        std::uint8_t hold_piece = no_piece_code;
        bool hold_available = false;

        bool operator==(TranspositionKey const &other) const
        {
            return depth == other.depth && cursor == other.cursor
                && boundary_count == other.boundary_count && root_child == other.root_child
                && occupancy == other.occupancy
                && state.death == other.state.death && state.combo == other.state.combo
                && state.under_attack == other.state.under_attack
                && state.map_rise == other.state.map_rise && state.b2b == other.state.b2b
                && state.t2_value == other.state.t2_value
                && state.t3_value == other.state.t3_value
                && state.acc_value == other.state.acc_value && state.like == other.state.like
                && state.value == other.state.value
                && boundary_bits == other.boundary_bits
                && active_piece == other.active_piece && hold_piece == other.hold_piece
                && hold_available == other.hold_available;
        }
    };

    inline double normalize_zero(double v)
    {
        return v == 0.0 ? 0.0 : v;
    }

    inline std::uint64_t transposition_hash(TranspositionKey const &key)
    {
        std::uint64_t h = 1469598103934665603ull;
        auto mix = [&h](void const *data, std::size_t bytes) {
            auto const *p = static_cast<std::uint8_t const *>(data);
            for (std::size_t i = 0; i < bytes; ++i)
            {
                h ^= p[i];
                h *= 1099511628211ull;
            }
        };
        auto mix_double = [&mix](double d) {
            std::uint64_t bits = 0;
            static_assert(sizeof bits == sizeof d);
            __builtin_memcpy(&bits, &d, sizeof bits);
            mix(&bits, sizeof bits);
        };
        mix(&key.depth, sizeof key.depth);
        mix(&key.cursor, sizeof key.cursor);
        mix(&key.boundary_count, sizeof key.boundary_count);
        mix(&key.root_child, sizeof key.root_child);
        for (int i = 0; i < Board::occupancy_t::word_count(); ++i)
        {
            std::uint64_t word = key.occupancy.logical_word(i);
            mix(&word, sizeof word);
        }
        mix(&key.state.death, sizeof key.state.death);
        mix(&key.state.combo, sizeof key.state.combo);
        mix(&key.state.under_attack, sizeof key.state.under_attack);
        mix(&key.state.map_rise, sizeof key.state.map_rise);
        mix(&key.state.b2b, sizeof key.state.b2b);
        mix(&key.state.t2_value, sizeof key.state.t2_value);
        mix(&key.state.t3_value, sizeof key.state.t3_value);
        mix_double(normalize_zero(key.state.acc_value));
        mix_double(normalize_zero(key.state.like));
        mix_double(normalize_zero(key.state.value));
        mix(key.boundary_bits.data(), key.boundary_bits.size() * sizeof(std::uint64_t));
        mix(&key.active_piece, sizeof key.active_piece);
        mix(&key.hold_piece, sizeof key.hold_piece);
        mix(&key.hold_available, sizeof key.hold_available);
        return h;
    }

    struct TranspositionEntry
    {
        TranspositionKey key{};
        NodeId node = no_node;
        bool used = false;
    };

    struct Node
    {
        Board board;
        PolicyState policy;
        Evaluation evaluation;
        NodeId parent = no_node;
        NodeId first_child = no_node;
        NodeId pending_child = no_node;
        NodeId pending_sibling = no_node;
        NodeId root_child = no_node;
        std::size_t child_count = 0;
        Candidate incoming;
        Piece played = Piece::T;
        bool has_incoming = false;
        BranchSource source = BranchSource::Current;
        HoldState hold;
        std::size_t cursor = 0;
        std::size_t depth = 0;
        bool expandable = true;
    };

    class PendingHeap
    {
    public:
        explicit PendingHeap(std::vector<Node> &arena)
            : arena_(&arena)
        {
        }

        PendingHeap(PendingHeap &&other, std::vector<Node> &arena) noexcept
            : arena_(&arena)
            , roots_(std::move(other.roots_))
            , counts_(std::move(other.counts_))
        {
        }

        PendingHeap(PendingHeap const &) = delete;
        PendingHeap &operator=(PendingHeap const &) = delete;

        void reset(std::size_t frontier_count)
        {
            roots_.assign(frontier_count, no_node);
            counts_.assign(frontier_count, 0);
        }

        void push(NodeId id, std::size_t level)
        {
            Node &node = (*arena_)[id];
            node.pending_child = no_node;
            node.pending_sibling = no_node;
            if (roots_[level] == no_node)
            {
                roots_[level] = id;
            }
            else
            {
                roots_[level] = meld(roots_[level], id);
            }
            ++counts_[level];
        }

        NodeId pop_max(std::size_t level)
        {
            NodeId root = roots_[level];
            NodeId chain = (*arena_)[root].pending_child;
            (*arena_)[root].pending_child = no_node;
            (*arena_)[root].pending_sibling = no_node;
            NodeId pairs = no_node;
            while (chain != no_node)
            {
                NodeId first = chain;
                NodeId second = (*arena_)[first].pending_sibling;
                NodeId rest = second != no_node ? (*arena_)[second].pending_sibling : no_node;
                NodeId pair = second != no_node ? meld(first, second) : first;
                (*arena_)[pair].pending_sibling = pairs;
                pairs = pair;
                chain = rest;
            }
            NodeId acc = no_node;
            while (pairs != no_node)
            {
                NodeId next = (*arena_)[pairs].pending_sibling;
                acc = acc == no_node ? pairs : meld(acc, pairs);
                pairs = next;
            }
            roots_[level] = acc;
            --counts_[level];
            return root;
        }

        NodeId best(std::size_t level) const
        {
            return roots_[level];
        }

        std::size_t size(std::size_t level) const
        {
            return counts_[level];
        }

    private:
        bool better(NodeId a, NodeId b) const
        {
            double const va = (*arena_)[a].policy.value;
            double const vb = (*arena_)[b].policy.value;
            if (va != vb)
            {
                return va > vb;
            }
            return a < b;
        }

        NodeId meld(NodeId a, NodeId b)
        {
            if (better(b, a))
            {
                NodeId tmp = a;
                a = b;
                b = tmp;
            }
            (*arena_)[b].pending_sibling = (*arena_)[a].pending_child;
            (*arena_)[a].pending_child = b;
            return a;
        }

        std::vector<Node> *arena_;
        std::vector<NodeId> roots_;
        std::vector<std::size_t> counts_;
    };

    struct Child
    {
        NodeId parent = no_node;
        Candidate candidate;
        BranchSource source = BranchSource::Current;
        Piece played = Piece::T;
        Outcome outcome;
        Board board;
        Evaluation evaluation;
        PolicyState state;
        HoldState hold;
        std::size_t cursor = 0;
        bool expandable = true;
    };

    struct ExpansionStats
    {
        std::size_t enumerated = 0;
        std::size_t evaluated = 0;
        std::size_t transitions = 0;
    };

    struct SearchStats
    {
        std::size_t widening_passes = 0;
        std::size_t expanded_parents = 0;
        std::size_t enumeration_calls = 0;
        std::size_t raw_kernel_landings = 0;
        std::size_t unique_candidates = 0;
        std::size_t rule_applications = 0;
        std::size_t eval_requests = 0;
        std::size_t eval_memo_hits = 0;
        std::size_t eval_computed = 0;
        std::size_t cache_requests = 0;
        std::size_t cache_hits = 0;
        std::size_t cache_misses = 0;
        std::size_t cache_replacements = 0;
        std::size_t policy_transitions = 0;
        std::size_t materialized_nodes = 0;
        std::size_t transposition_merges = 0;
        std::size_t promotions_refused = 0;
        std::size_t pending_occupancy = 0;
        bool transposition_exhausted = false;
    };

    struct SearchSelection
    {
        NodeId root_child = no_node;
        NodeId evidence = no_node;

        bool operator==(SearchSelection const &) const = default;
    };

    inline std::uint64_t occupancy_hash(Board::occupancy_t const &occupancy)
    {
        std::uint64_t h = 1469598103934665603ull;
        for (int i = 0; i < Board::occupancy_t::word_count(); ++i)
        {
            h ^= occupancy.logical_word(i);
            h *= 1099511628211ull;
        }
        return h;
    }

    struct CacheConfig
    {
        enum class Layout : std::uint8_t
        {
            Disabled,
            DirectMapped,
            SetAssociative,
        };

        Layout layout = Layout::DirectMapped;
        std::size_t entries = 16384;
        std::size_t ways = 4;
    };

    struct EvalCacheEntry
    {
        Board::occupancy_t occupancy{};
        Evaluation evaluation{};
        std::uint64_t stamp = 0;
        bool used = false;
    };

    class EvalCache
    {
    public:
        void init(std::size_t entries, std::size_t ways)
        {
            entries_ = std::vector<EvalCacheEntry>(entries);
            ways_ = ways < 1 ? 1 : ways;
            requests_ = 0;
            hits_ = 0;
            misses_ = 0;
            replacements_ = 0;
            stamp_ = 0;
        }

        void clear()
        {
            for (auto &entry : entries_)
            {
                entry = EvalCacheEntry{};
            }
            requests_ = 0;
            hits_ = 0;
            misses_ = 0;
            replacements_ = 0;
            stamp_ = 0;
        }

        std::optional<Evaluation> find(Board const &board)
        {
            if (entries_.empty())
            {
                return std::nullopt;
            }
            ++requests_;
            std::size_t const set = set_of(board);
            for (std::size_t way = 0; way < ways_; ++way)
            {
                EvalCacheEntry &entry = entries_[set * ways_ + way];
                if (entry.used && entry.occupancy == board.occupancy())
                {
                    ++hits_;
                    return entry.evaluation;
                }
            }
            ++misses_;
            return std::nullopt;
        }

        void insert(Board const &board, Evaluation const &evaluation)
        {
            if (entries_.empty())
            {
                return;
            }
            std::size_t const set = set_of(board);
            std::size_t victim = entries_.size();
            for (std::size_t way = 0; way < ways_; ++way)
            {
                EvalCacheEntry &entry = entries_[set * ways_ + way];
                if (!entry.used)
                {
                    victim = set * ways_ + way;
                    break;
                }
            }
            if (victim == entries_.size())
            {
                victim = set * ways_;
                for (std::size_t way = 1; way < ways_; ++way)
                {
                    if (entries_[set * ways_ + way].stamp
                        < entries_[victim].stamp)
                    {
                        victim = set * ways_ + way;
                    }
                }
                ++replacements_;
            }
            EvalCacheEntry &entry = entries_[victim];
            entry.occupancy = board.occupancy();
            entry.evaluation = evaluation;
            entry.stamp = ++stamp_;
            entry.used = true;
        }

        std::size_t requests() const
        {
            return requests_;
        }

        std::size_t hits() const
        {
            return hits_;
        }

        std::size_t misses() const
        {
            return misses_;
        }

        std::size_t replacements() const
        {
            return replacements_;
        }

        std::size_t reserved_bytes() const
        {
            return entries_.capacity() * sizeof(EvalCacheEntry);
        }

    private:
        std::size_t set_of(Board const &board) const
        {
            std::size_t const sets = entries_.size() / ways_;
            return static_cast<std::size_t>(occupancy_hash(board.occupancy()) & (sets - 1));
        }

        std::vector<EvalCacheEntry> entries_;
        std::size_t ways_ = 1;
        std::uint64_t stamp_ = 0;
        std::size_t requests_ = 0;
        std::size_t hits_ = 0;
        std::size_t misses_ = 0;
        std::size_t replacements_ = 0;
    };

    inline constexpr std::uint64_t engine_buffer_reservation(
        std::uint64_t arena_bytes, std::uint64_t candidate_bytes, std::uint64_t child_bytes,
        std::uint64_t memo_bytes, std::uint64_t transposition_bytes,
        std::uint64_t cache_bytes)
    {
        return arena_bytes + candidate_bytes + child_bytes + memo_bytes + transposition_bytes
            + cache_bytes
            + engine_queue_reservation + engine_stack_peak_allowance + engine_frontier_metadata;
    }

    inline constexpr std::uint64_t engine_fixed_workspace = engine_buffer_reservation(0,
        max_candidates_per_source * sizeof(Candidate),
        max_children_per_parent * sizeof(Child),
        max_children_per_parent * sizeof(std::pair<Board, Evaluation>),
        transposition_entries * sizeof(TranspositionEntry),
        16384 * sizeof(EvalCacheEntry));

    inline constexpr std::size_t default_arena_capacity =
        static_cast<std::size_t>((engine_memory_budget - engine_fixed_workspace) / sizeof(Node));

    inline std::int64_t steady_clock_nanos()
    {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }

    struct SearchBudget
    {
        enum class Kind : std::uint8_t
        {
            Time,
            Iterations,
        };

        Kind kind = Kind::Time;
        std::uint64_t milliseconds = 0;
        std::uint64_t iterations = 0;

        static SearchBudget by_time(std::uint64_t ms)
        {
            SearchBudget budget;
            budget.kind = Kind::Time;
            budget.milliseconds = ms;
            return budget;
        }

        static SearchBudget by_iterations(std::uint64_t n)
        {
            SearchBudget budget;
            budget.kind = Kind::Iterations;
            budget.iterations = n;
            return budget;
        }
    };

    struct EngineConfig
    {
        toj_policy::Config const *policy = nullptr;
        tetris::toj::MovementConfig movement;
        std::size_t arena_capacity = default_arena_capacity;
        std::function<std::int64_t()> clock_nanos = steady_clock_nanos;
        CacheConfig cache;
    };

    class Engine
    {
    public:
        Engine() = default;
        Engine(Engine &&other) noexcept
            : config_(std::move(other.config_))
            , policy_(std::move(other.policy_))
            , arena_(std::move(other.arena_))
            , heap_(std::move(other.heap_), arena_)
            , queue_(std::move(other.queue_))
            , stats_(other.stats_)
            , exhausted_(other.exhausted_)
            , eval_memo_(std::move(other.eval_memo_))
            , candidate_buffer_(std::move(other.candidate_buffer_))
            , child_buffer_(std::move(other.child_buffer_))
            , expanded_count_(other.expanded_count_)
            , expanded_max_(other.expanded_max_)
            , width_cache_(other.width_cache_)
            , transposition_(std::move(other.transposition_))
            , transposition_used_(other.transposition_used_)
            , max_length_(other.max_length_)
            , width_(other.width_)
            , search_complete_(other.search_complete_)
            , search_stopped_(other.search_stopped_)
            , transposition_exhausted_(other.transposition_exhausted_)
            , search_stats_(other.search_stats_)
        {
        }

        Engine(Engine const &) = delete;
        Engine &operator=(Engine const &) = delete;
        Engine &operator=(Engine &&) = delete;

        bool init(EngineConfig const &config);

        NodeId set_root(Board board, PolicyState policy, Queue queue, HoldState hold);

        std::vector<Child> expand(NodeId parent);

        NodeId materialize(Child const &child);

        void link_children(NodeId parent, NodeId first, std::size_t count);

        Node const *node(NodeId id) const;

        Queue const &queue() const;

        ExpansionStats const &last_stats() const;

        std::size_t arena_size() const;

        std::size_t arena_reserved_bytes() const;

        std::uint64_t retained_bytes() const;

        bool arena_exhausted() const;

        bool run(std::size_t max_passes);

        bool run(SearchBudget budget);

        bool search_complete() const;

        std::size_t frontier_count() const;

        SearchStats search_stats() const;

        std::optional<SearchSelection> select_best() const;

    private:
        struct TranspositionProbe
        {
            bool merged = false;
            NodeId node = no_node;
            TranspositionEntry *slot = nullptr;
        };

        struct MaterializeOutcome
        {
            bool merged = false;
            NodeId id = no_node;
        };

        EngineConfig config_{};
        toj_policy::Policy policy_;
        std::vector<Node> arena_;
        PendingHeap heap_{ arena_ };
        Queue queue_;
        ExpansionStats stats_{};
        bool exhausted_ = false;
        std::vector<std::pair<Board, Evaluation>> eval_memo_;
        std::vector<Candidate> candidate_buffer_;
        std::vector<Child> child_buffer_;

        std::array<std::size_t, max_frontiers> expanded_count_{};
        std::array<NodeId, max_frontiers> expanded_max_{};
        std::array<double, max_frontiers> width_cache_{};
        std::vector<TranspositionEntry> transposition_;
        EvalCache cache_;
        std::size_t transposition_used_ = 0;
        std::size_t max_length_ = 0;
        std::size_t width_ = 0;
        bool search_complete_ = false;
        bool search_stopped_ = false;
        bool transposition_exhausted_ = false;
        SearchStats search_stats_{};

        void reset_run_state();

        std::int64_t now_nanos() const
        {
            return config_.clock_nanos ? config_.clock_nanos() : steady_clock_nanos();
        }

        bool expand_source(NodeId parent_id, Node const &parent, Piece played,
            BranchSource source, HoldState hold, std::size_t cursor,
            std::span<Piece const> policy_next, std::vector<Child> &out);

        Evaluation evaluate_once(Board const &board);

        bool expand_parent(NodeId parent_id);

        MaterializeOutcome search_materialize(Child const &child);

        void promote(std::size_t level);

        TranspositionProbe transposition_probe(TranspositionKey const &key);

        TranspositionKey build_key(Child const &child, NodeId id) const;

        void run_pass();
    };
}
