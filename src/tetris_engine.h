#pragma once

#include "tetris_types.h"
#include "toj_rule.h"
#include "toj_policy.h"

#include <array>
#include <cstddef>
#include <cstdint>
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
    };

    inline constexpr std::uint64_t engine_fixed_workspace =
        max_candidates_per_source * sizeof(Candidate)
        + max_children_per_parent * sizeof(Child)
        + max_children_per_parent * sizeof(std::pair<Board, Evaluation>)
        + transposition_entries * sizeof(TranspositionEntry)
        + max_frontiers * (2 * sizeof(NodeId) + 2 * sizeof(std::size_t))
        + max_frontiers * sizeof(double);

    inline constexpr std::size_t default_arena_capacity =
        static_cast<std::size_t>((engine_memory_budget - engine_fixed_workspace) / sizeof(Node));

    struct EngineConfig
    {
        toj_policy::Config const *policy = nullptr;
        tetris::toj::MovementConfig movement;
        std::size_t arena_capacity = default_arena_capacity;
    };

    class Engine
    {
    public:
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

        bool arena_exhausted() const;

        bool run(std::size_t max_passes);

        bool search_complete() const;

        std::size_t frontier_count() const;

        SearchStats const &search_stats() const;

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
        Queue queue_;
        ExpansionStats stats_{};
        bool exhausted_ = false;
        std::vector<std::pair<Board, Evaluation>> eval_memo_;
        std::vector<Candidate> candidate_buffer_;
        std::vector<Child> child_buffer_;

        std::array<NodeId, max_frontiers> pending_root_{};
        std::array<std::size_t, max_frontiers> pending_count_{};
        std::array<std::size_t, max_frontiers> expanded_count_{};
        std::array<NodeId, max_frontiers> expanded_max_{};
        std::array<double, max_frontiers> width_cache_{};
        std::vector<TranspositionEntry> transposition_;
        std::size_t transposition_used_ = 0;
        std::size_t max_length_ = 0;
        std::size_t width_ = 0;
        bool search_complete_ = false;
        bool search_stopped_ = false;
        bool transposition_exhausted_ = false;
        SearchStats search_stats_{};

        bool expand_source(NodeId parent_id, Node const &parent, Piece played,
            BranchSource source, HoldState hold, std::size_t cursor,
            std::span<Piece const> policy_next, std::vector<Child> &out);

        Evaluation evaluate_once(Board const &board);

        bool expand_parent(NodeId parent_id);

        MaterializeOutcome search_materialize(Child const &child);

        void promote(std::size_t level);

        void pending_push(NodeId id, std::size_t level);

        NodeId pending_pop_max(std::size_t level);

        NodeId meld(NodeId a, NodeId b);

        bool value_better(NodeId a, NodeId b) const;

        TranspositionProbe transposition_probe(TranspositionKey const &key);

        TranspositionKey build_key(Child const &child, NodeId id) const;

        void run_pass();
    };
}
