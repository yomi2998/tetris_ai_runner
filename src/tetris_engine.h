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
    inline constexpr std::uint64_t engine_workspace_reserve = 1ull << 20;
    inline constexpr std::size_t max_queue_length = 256;
    inline constexpr std::uint64_t max_nodes = 0xFFFFFFFEull;

    enum class BranchSource : std::uint8_t
    {
        Current,
        Hold,
    };

    struct Queue
    {
        std::vector<Piece> pieces;
        std::vector<bool> boundary;
    };

    std::optional<Queue> parse_queue(std::string_view text);

    struct HoldState
    {
        std::optional<Piece> piece;
        bool locked = false;
    };

    using NodeId = std::uint32_t;
    inline constexpr NodeId no_node = 0xFFFFFFFFu;

    struct Node
    {
        Board board;
        PolicyState policy;
        Evaluation evaluation;
        NodeId parent = no_node;
        NodeId first_child = no_node;
        std::size_t child_count = 0;
        Candidate incoming;
        bool has_incoming = false;
        BranchSource source = BranchSource::Current;
        HoldState hold;
        std::size_t cursor = 0;
        std::size_t depth = 0;
        bool expandable = true;
    };

    inline constexpr std::size_t default_arena_capacity =
        static_cast<std::size_t>((engine_memory_budget - engine_workspace_reserve) / sizeof(Node));

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

    struct EngineConfig
    {
        toj_policy::Config const *policy = nullptr;
        tetris::toj::MovementConfig movement;
        std::size_t arena_capacity = default_arena_capacity;
    };

    class Engine
    {
    public:
        void init(EngineConfig const &config);

        NodeId set_root(Board board, PolicyState policy, Queue queue, HoldState hold);

        std::vector<Child> expand(NodeId parent);

        NodeId materialize(Child const &child);

        void link_children(NodeId parent, NodeId first, std::size_t count);

        Node const *node(NodeId id) const;

        Queue const &queue() const;

        ExpansionStats const &last_stats() const;

        std::size_t arena_size() const;

        bool arena_exhausted() const;

    private:
        EngineConfig config_{};
        toj_policy::Policy policy_;
        std::vector<Node> arena_;
        Queue queue_;
        ExpansionStats stats_{};
        bool exhausted_ = false;
        std::vector<std::pair<Board, Evaluation>> eval_memo_;

        void expand_source(NodeId parent_id, Node const &parent, Piece played,
            BranchSource source, HoldState hold, std::size_t cursor,
            std::span<Piece const> policy_next, std::vector<Child> &out);

        Evaluation evaluate_once(Board const &board);
    };
}
