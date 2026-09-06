#include "tetris_engine.h"

#include <algorithm>

namespace tetris_engine
{
    std::optional<Queue> parse_queue(std::string_view text)
    {
        Queue queue;
        for (char c : text)
        {
            if (c == '?')
            {
                if (queue.pieces.empty())
                {
                    return std::nullopt;
                }
                queue.boundary.back() = true;
                continue;
            }
            auto piece = tetris::try_from_char(c);
            if (!piece.has_value())
            {
                return std::nullopt;
            }
            queue.pieces.push_back(*piece);
            queue.boundary.push_back(false);
        }
        if (queue.pieces.empty())
        {
            return std::nullopt;
        }
        return queue;
    }

    void Engine::init(EngineConfig const &config)
    {
        config_ = config;
        policy_.init(config_.policy);
        arena_.clear();
        arena_.reserve(config_.arena_capacity);
        queue_ = Queue{};
        stats_ = ExpansionStats{};
        exhausted_ = false;
        eval_memo_.clear();
    }

    NodeId Engine::set_root(Board board, PolicyState policy, Queue queue, HoldState hold)
    {
        queue_ = std::move(queue);
        arena_.clear();
        exhausted_ = false;
        if (arena_.size() >= config_.arena_capacity)
        {
            exhausted_ = true;
            return no_node;
        }
        Node root;
        root.board = board;
        root.policy = policy;
        root.evaluation = policy_.evaluate(board);
        root.hold = hold;
        arena_.push_back(root);
        return 0;
    }

    Evaluation Engine::evaluate_once(Board const &board)
    {
        for (auto const &entry : eval_memo_)
        {
            if (entry.first == board)
            {
                return entry.second;
            }
        }
        Evaluation evaluation = policy_.evaluate(board);
        eval_memo_.push_back({ board, evaluation });
        ++stats_.evaluated;
        return evaluation;
    }

    void Engine::expand_source(NodeId parent_id, Node const &parent, Piece played,
        BranchSource source, HoldState hold, std::size_t cursor, std::vector<Child> &out)
    {
        if (!tetris::toj::can_spawn(parent.board, played))
        {
            return;
        }
        auto candidates = tetris::toj::enumerate_candidates(parent.board, played, config_.movement);
        stats_.enumerated += candidates.size();
        for (auto const &candidate : candidates)
        {
            auto applied = tetris::toj::apply(parent.board, played, candidate);
            if (!applied.has_value())
            {
                continue;
            }
            Outcome outcome;
            outcome.spin = applied->spin;
            outcome.clear_count = applied->clear_count;
            outcome.lockout = applied->lockout;
            bool same_result = false;
            for (auto const &child : out)
            {
                if (child.source == source && child.board == applied->board
                    && child.outcome == outcome)
                {
                    same_result = true;
                    break;
                }
            }
            if (same_result)
            {
                continue;
            }
            Evaluation evaluation = evaluate_once(applied->board);
            toj_policy::DecisionContext context;
            context.next = std::span<Piece const>(queue_.pieces.data() + cursor,
                queue_.pieces.size() - cursor);
            context.hold = hold.piece;
            context.used_hold = source == BranchSource::Hold;
            context.depth = parent.depth;
            PolicyState state =
                policy_.transition(played, candidate, outcome, applied->board, parent.policy,
                    context, evaluation);
            Child child;
            child.parent = parent_id;
            child.candidate = candidate;
            child.source = source;
            child.played = played;
            child.outcome = outcome;
            child.board = applied->board;
            child.evaluation = evaluation;
            child.state = state;
            child.hold = hold;
            child.cursor = cursor;
            child.expandable = !applied->lockout;
            out.push_back(child);
            ++stats_.transitions;
        }
    }

    std::vector<Child> Engine::expand(NodeId parent)
    {
        std::vector<Child> out;
        if (parent >= arena_.size())
        {
            return out;
        }
        Node const &node = arena_[parent];
        if (!node.expandable)
        {
            return out;
        }
        stats_ = ExpansionStats{};
        eval_memo_.clear();
        bool const has_current = node.cursor < queue_.pieces.size();
        std::optional<Piece> current;
        if (has_current)
        {
            current = queue_.pieces[node.cursor];
            HoldState hold = node.hold;
            hold.locked = false;
            expand_source(parent, node, *current, BranchSource::Current, hold, node.cursor + 1,
                out);
        }
        if (!node.hold.locked)
        {
            if (node.hold.piece.has_value())
            {
                HoldState hold;
                hold.piece = current;
                hold.locked = false;
                expand_source(parent, node, *node.hold.piece, BranchSource::Hold, hold,
                    node.cursor + 1, out);
            }
            else if (has_current && node.cursor + 1 < queue_.pieces.size())
            {
                HoldState hold;
                hold.piece = current;
                hold.locked = false;
                expand_source(parent, node, queue_.pieces[node.cursor + 1], BranchSource::Hold,
                    hold, node.cursor + 2, out);
            }
        }
        return out;
    }

    NodeId Engine::materialize(Child const &child)
    {
        if (arena_.size() >= config_.arena_capacity)
        {
            exhausted_ = true;
            return no_node;
        }
        NodeId parent = child.parent;
        std::size_t depth = 0;
        if (parent < arena_.size())
        {
            depth = arena_[parent].depth + 1;
        }
        Node node;
        node.parent = child.parent;
        node.depth = depth;
        node.board = child.board;
        node.policy = child.state;
        node.evaluation = child.evaluation;
        node.source = child.source;
        node.incoming = child.candidate;
        node.has_incoming = true;
        node.hold = child.hold;
        node.cursor = child.cursor;
        node.expandable = child.expandable;
        arena_.push_back(node);
        return static_cast<NodeId>(arena_.size() - 1);
    }

    void Engine::link_children(NodeId parent, NodeId first, std::size_t count)
    {
        if (parent >= arena_.size() || count == 0)
        {
            return;
        }
        arena_[parent].first_child = first;
        arena_[parent].child_count = count;
    }

    Node const *Engine::node(NodeId id) const
    {
        if (id >= arena_.size())
        {
            return nullptr;
        }
        return &arena_[id];
    }

    Queue const &Engine::queue() const
    {
        return queue_;
    }

    ExpansionStats const &Engine::last_stats() const
    {
        return stats_;
    }

    std::size_t Engine::arena_size() const
    {
        return arena_.size();
    }

    bool Engine::arena_exhausted() const
    {
        return exhausted_;
    }
}
