#include "tetris_engine.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>

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
                ++queue.marker_count;
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

    namespace
    {
        bool board_has_full_row(Board const &board)
        {
            for (int y = 0; y < Board::height; ++y)
            {
                if (board.row(y) == Board::row_mask)
                {
                    return true;
                }
            }
            return false;
        }

        std::size_t probe_length_bucket(std::size_t length)
        {
            if (length <= 1)
            {
                return 0;
            }
            if (length == 2)
            {
                return 1;
            }
            if (length <= 4)
            {
                return 2;
            }
            if (length <= 8)
            {
                return 3;
            }
            if (length <= 16)
            {
                return 4;
            }
            if (length <= 32)
            {
                return 5;
            }
            if (length <= 64)
            {
                return 6;
            }
            return 7;
        }
    }

    bool Engine::init(EngineConfig const &config)
    {
        config_ = config;
        policy_.init(config_.policy);
        std::vector<Node>().swap(arena_);
        std::vector<Candidate>().swap(candidate_buffer_);
        std::vector<Child>().swap(child_buffer_);
        std::vector<std::pair<Board, Evaluation>>().swap(eval_memo_);
        std::vector<TranspositionEntry>().swap(transposition_);
        std::vector<TranspositionEntry>().swap(transposition_rehash_);
        std::vector<NodeId>().swap(idmap_);
        cache_ = EvalCache{};
        queue_ = Queue{};
        path_stats_ = PathTelemetry{};
        reset_run_state();
        transposition_epoch_ = 1;
        bool const cache_enabled = config_.cache.layout != CacheConfig::Layout::Disabled;
        std::size_t const effective_ways =
            config_.cache.layout == CacheConfig::Layout::DirectMapped
            ? 1
            : config_.cache.ways;
        bool const cache_geometry_ok = !cache_enabled
            || (config_.cache.entries >= 1 && effective_ways >= 1
                && config_.cache.entries % effective_ways == 0
                && std::has_single_bit(config_.cache.entries / effective_ways)
                && config_.cache.entries
                    <= std::numeric_limits<std::uint64_t>::max() / sizeof(EvalCacheEntry));
        std::uint64_t cache_bytes = 0;
        if (cache_geometry_ok && cache_enabled)
        {
            cache_bytes = static_cast<std::uint64_t>(config_.cache.entries)
                * sizeof(EvalCacheEntry);
        }
        std::uint64_t const fixed_non_cache = engine_buffer_reservation(0,
            max_candidates_per_source * sizeof(Candidate),
            max_children_per_parent * sizeof(Child),
            max_children_per_parent * sizeof(std::pair<Board, Evaluation>),
            transposition_entries * sizeof(TranspositionEntry),
            transposition_entries * sizeof(TranspositionEntry),
            0);
        bool const budget_ok = fixed_non_cache <= engine_memory_budget
            && cache_bytes <= engine_memory_budget - fixed_non_cache;
        if (!cache_geometry_ok || !budget_ok)
        {
            config_.arena_capacity = 0;
            return false;
        }
        std::uint64_t const allowance = engine_memory_budget - fixed_non_cache - cache_bytes;
        if (config_.arena_capacity > max_nodes
            || config_.arena_capacity > allowance / (sizeof(Node) + sizeof(NodeId)))
        {
            config_.arena_capacity = 0;
            return false;
        }
        arena_.reserve(config_.arena_capacity);
        idmap_.reserve(config_.arena_capacity);
        if (static_cast<std::uint64_t>(arena_.capacity()) * sizeof(Node)
                + static_cast<std::uint64_t>(idmap_.capacity()) * sizeof(NodeId)
            > allowance)
        {
            std::vector<Node>().swap(arena_);
            std::vector<NodeId>().swap(idmap_);
            config_.arena_capacity = 0;
            return false;
        }
        candidate_buffer_.resize(max_candidates_per_source);
        child_buffer_.reserve(max_children_per_parent);
        eval_memo_.reserve(max_children_per_parent);
        transposition_.resize(transposition_entries);
        transposition_rehash_.resize(transposition_entries);
        queue_.pieces.reserve(max_queue_length);
        queue_.boundary.reserve(max_queue_length);
        if (config_.cache.layout != CacheConfig::Layout::Disabled)
        {
            cache_.init(config_.cache.layout, config_.cache.entries, config_.cache.ways);
        }
        cache_.set_telemetry_enabled(config_.telemetry_enabled);
        std::uint64_t const used = engine_buffer_reservation(
            static_cast<std::uint64_t>(arena_.capacity()) * sizeof(Node)
                + static_cast<std::uint64_t>(idmap_.capacity()) * sizeof(NodeId),
            static_cast<std::uint64_t>(candidate_buffer_.capacity()) * sizeof(Candidate),
            static_cast<std::uint64_t>(child_buffer_.capacity()) * sizeof(Child),
            static_cast<std::uint64_t>(eval_memo_.capacity())
                * sizeof(std::pair<Board, Evaluation>),
            static_cast<std::uint64_t>(transposition_.capacity())
                * sizeof(TranspositionEntry),
            static_cast<std::uint64_t>(transposition_rehash_.capacity())
                * sizeof(TranspositionEntry),
            static_cast<std::uint64_t>(cache_.reserved_bytes()));
        if (used > engine_memory_budget)
        {
            std::vector<Node>().swap(arena_);
            std::vector<Candidate>().swap(candidate_buffer_);
            std::vector<Child>().swap(child_buffer_);
            std::vector<std::pair<Board, Evaluation>>().swap(eval_memo_);
            std::vector<TranspositionEntry>().swap(transposition_);
            std::vector<TranspositionEntry>().swap(transposition_rehash_);
            std::vector<NodeId>().swap(idmap_);
            cache_ = EvalCache{};
            queue_ = Queue{};
            config_.arena_capacity = 0;
            return false;
        }
        return true;
    }

    void Engine::advance_transposition_epoch()
    {
        if (transposition_epoch_ == std::numeric_limits<std::uint32_t>::max())
        {
            for (auto &entry : transposition_)
            {
                entry = TranspositionEntry{};
            }
            transposition_epoch_ = 1;
        }
        else
        {
            ++transposition_epoch_;
        }
    }

    void Engine::reset_run_state()
    {
        max_length_ = 0;
        width_ = 0;
        search_complete_ = false;
        search_stopped_ = false;
        transposition_exhausted_ = false;
        search_stats_ = SearchStats{};
        timers_ = ComponentTimers{};
        stats_ = ExpansionStats{};
        exhausted_ = false;
        cache_.reset_counters();
        heap_.reset(max_frontiers);
        for (std::size_t i = 0; i < max_frontiers; ++i)
        {
            expanded_count_[i] = 0;
            expanded_max_[i] = no_node;
        }
        advance_transposition_epoch();
        transposition_used_ = 0;
    }

    bool Engine::reuse_matches(NodeId child, Board const &board, PolicyState const &policy,
        Queue const &queue, HoldState hold, std::size_t new_max) const
    {
        Node const &node = arena_[child];
        if (!(node.board.occupancy() == board.occupancy()))
        {
            return false;
        }
        if (node.policy.death != policy.death || node.policy.combo != policy.combo
            || node.policy.under_attack != policy.under_attack
            || node.policy.map_rise != policy.map_rise || node.policy.b2b != policy.b2b
            || node.policy.t2_value != policy.t2_value
            || node.policy.t3_value != policy.t3_value
            || node.policy.acc_value != policy.acc_value || node.policy.like != policy.like
            || node.policy.value != policy.value)
        {
            return false;
        }
        if (node.hold.piece != hold.piece || node.hold.locked != hold.locked)
        {
            return false;
        }
        std::size_t const old_size = queue_.pieces.size();
        std::size_t const played_cursor = node.cursor;
        if (played_cursor == 0 || played_cursor > old_size
            || played_cursor > max_length_)
        {
            return false;
        }
        if (queue.pieces.size() != old_size - played_cursor
            || queue.boundary.size() != queue.pieces.size())
        {
            return false;
        }
        for (std::size_t i = 0; i + played_cursor < old_size; ++i)
        {
            if (queue.pieces[i] != queue_.pieces[played_cursor + i])
            {
                return false;
            }
            if (queue.boundary[i] != queue_.boundary[played_cursor + i])
            {
                return false;
            }
        }
        if (queue.marker_count > queue_.marker_count)
        {
            return false;
        }
        if (new_max > max_length_ || new_max + played_cursor < max_length_)
        {
            return false;
        }
        return true;
    }

    NodeId Engine::reroot(NodeId target, Queue const &queue, HoldState hold,
        std::size_t new_max)
    {
        std::size_t const played_cursor = arena_[target].cursor;
        idmap_.resize(arena_.size());
        std::uint32_t const retained_identity = arena_[target].root_child;
        std::size_t new_count = 0;
        for (std::size_t old = 0; old < arena_.size(); ++old)
        {
            Node const &node = arena_[old];
            bool keep = old == static_cast<std::size_t>(target);
            if (!keep && node.root_child == retained_identity && node.depth >= 1
                && node.cursor >= played_cursor && node.depth - 1 <= new_max + 1
                && node.parent < old
                && node.depth == arena_[node.parent].depth + 1
                && idmap_[node.parent] != no_node)
            {
                keep = true;
            }
            idmap_[old] = keep ? static_cast<NodeId>(new_count++) : no_node;
        }
        for (std::size_t old = 0; old < arena_.size(); ++old)
        {
            if (idmap_[old] != no_node)
            {
                arena_[idmap_[old]] = std::move(arena_[old]);
            }
        }
        for (std::size_t n = 0; n < new_count; ++n)
        {
            Node &node = arena_[n];
            if (n == 0)
            {
                node.parent = no_node;
                node.depth = 0;
                node.root_child = no_node;
                node.cursor = 0;
            }
            else
            {
                node.parent = idmap_[node.parent];
                node.depth = node.depth - 1;
                node.cursor = node.cursor - played_cursor;
                node.root_child = node.depth == 1
                    ? first_move_fingerprint(node.played, node.incoming, node.source)
                    : arena_[node.parent].root_child;
            }
            node.registered = false;
            node.pending_child = no_node;
            node.pending_sibling = no_node;
            node.first_child = no_node;
            node.child_count = 0;
        }
        // Keys are re-derived from the rewritten nodes against the new queue, so
        // the queue swap precedes staging (docs/phase7/transposition_capacity_design.md §2).
        queue_.pieces.clear();
        queue_.boundary.clear();
        for (Piece piece : queue.pieces)
        {
            queue_.pieces.push_back(piece);
        }
        for (bool bit : queue.boundary)
        {
            queue_.boundary.push_back(bit);
        }
        queue_.marker_count = queue.marker_count;
        max_length_ = new_max;
        std::size_t moved = 0;
        for (auto &entry : transposition_)
        {
            if (entry.epoch != transposition_epoch_)
            {
                continue;
            }
            if (entry.node >= idmap_.size() || idmap_[entry.node] == no_node)
            {
                continue;
            }
            NodeId const new_node = idmap_[entry.node];
            if (arena_[new_node].depth < 1 || arena_[new_node].depth > new_max + 1)
            {
                continue;
            }
            TranspositionKey key = key_from_node(new_node);
            transposition_rehash_[moved++] =
                TranspositionEntry{ transposition_hash(key), new_node, entry.epoch };
        }
        advance_transposition_epoch();
        transposition_used_ = 0;
        for (std::size_t i = 0; i < moved; ++i)
        {
            TranspositionKey key = key_from_node(transposition_rehash_[i].node);
            transposition_reinsert(transposition_rehash_[i].fp, key,
                transposition_rehash_[i].node);
        }
        arena_.resize(new_count);
        rebuild_child_links();
        width_ = 0;
        search_complete_ = false;
        search_stopped_ = false;
        transposition_exhausted_ = false;
        search_stats_ = SearchStats{};
        timers_ = ComponentTimers{};
        stats_ = ExpansionStats{};
        exhausted_ = false;
        cache_.reset_counters();
        eval_memo_.clear();
        child_buffer_.clear();
        heap_.reset(max_frontiers);
        for (std::size_t i = 0; i < max_frontiers; ++i)
        {
            expanded_count_[i] = 0;
            expanded_max_[i] = no_node;
        }
        return 0;
    }

    void Engine::rebuild_child_links()
    {
        for (std::size_t n = 0; n < arena_.size(); ++n)
        {
            arena_[n].first_child = no_node;
            arena_[n].next_sibling = no_node;
            arena_[n].child_count = 0;
        }
        for (std::size_t n = 0; n < arena_.size(); ++n)
        {
            Node const &node = arena_[n];
            if (node.parent == no_node || node.parent >= arena_.size())
            {
                continue;
            }
            append_child_link(node.parent, static_cast<NodeId>(n));
        }
    }

    void Engine::append_child_link(NodeId parent, NodeId child)
    {
        if (parent >= arena_.size() || child >= arena_.size() || parent == child)
        {
            return;
        }
        NodeId cursor = arena_[parent].first_child;
        if (cursor == no_node)
        {
            arena_[parent].first_child = child;
            arena_[parent].child_count = 1;
            arena_[child].next_sibling = no_node;
            return;
        }
        while (true)
        {
            if (cursor == child)
            {
                return;
            }
            NodeId sibling = arena_[cursor].next_sibling;
            if (sibling == no_node)
            {
                arena_[cursor].next_sibling = child;
                arena_[child].next_sibling = no_node;
                ++arena_[parent].child_count;
                return;
            }
            cursor = sibling;
        }
    }

    NodeId Engine::set_root(Board board, PolicyState policy, Queue queue, HoldState hold)
    {
        if (queue.pieces.empty() || queue.pieces.size() > max_queue_length
            || queue.boundary.size() != queue.pieces.size() || board_has_full_row(board))
        {
            return no_node;
        }
        std::size_t const pieces = queue.pieces.size();
        std::size_t const next_pieces = pieces - 1;
        std::size_t const marker_count = queue.marker_count;
        bool const raw_next_beyond_one = next_pieces > 1
            || (next_pieces == 1 && marker_count >= 1)
            || (next_pieces == 0 && marker_count >= 2);
        std::size_t new_max = next_pieces;
        if (hold.piece.has_value() && (raw_next_beyond_one || !hold.locked))
        {
            ++new_max;
        }
        if (arena_.size() > 1)
        {
            for (std::size_t id = 1; id < arena_.size(); ++id)
            {
                if (arena_[id].depth == 1
                    && reuse_matches(static_cast<NodeId>(id), board, policy, queue, hold,
                        new_max))
                {
                    return reroot(static_cast<NodeId>(id), queue, hold, new_max);
                }
            }
        }
        reset_run_state();
        queue_.pieces.clear();
        queue_.boundary.clear();
        for (Piece piece : queue.pieces)
        {
            queue_.pieces.push_back(piece);
        }
        for (bool bit : queue.boundary)
        {
            queue_.boundary.push_back(bit);
        }
        queue_.marker_count = queue.marker_count;
        arena_.clear();
        exhausted_ = false;
        eval_memo_.clear();
        child_buffer_.clear();
        max_length_ = new_max;
        if (config_.arena_capacity == 0)
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
        bool const count = telemetry_on();
        bool const time = timers_on();
        std::int64_t const start = time ? timer_now() : 0;
        if (count)
        {
            ++search_stats_.eval_requests;
        }
        for (auto const &entry : eval_memo_)
        {
            if (entry.first == board)
            {
                if (count)
                {
                    ++search_stats_.eval_memo_hits;
                }
                if (time)
                {
                    timers_.eval_hit_ns += timer_now() - start;
                }
                return entry.second;
            }
        }
        if (config_.cache.layout != CacheConfig::Layout::Disabled)
        {
            if (count)
            {
                ++search_stats_.cache_requests;
            }
            if (auto cached = cache_.find(board))
            {
                if (eval_memo_.size() < eval_memo_.capacity())
                {
                    eval_memo_.push_back({ board, *cached });
                }
                if (count)
                {
                    ++search_stats_.cache_hits;
                }
                if (time)
                {
                    timers_.eval_hit_ns += timer_now() - start;
                }
                return *cached;
            }
            if (count)
            {
                ++search_stats_.cache_misses;
            }
        }
        Evaluation evaluation = policy_.evaluate(board);
        if (eval_memo_.size() < eval_memo_.capacity())
        {
            eval_memo_.push_back({ board, evaluation });
        }
        if (config_.cache.layout != CacheConfig::Layout::Disabled)
        {
            cache_.insert(board, evaluation);
        }
        if (count)
        {
            ++search_stats_.eval_computed;
            ++stats_.evaluated;
        }
        if (time)
        {
            timers_.eval_miss_ns += timer_now() - start;
        }
        return evaluation;
    }

    bool Engine::expand_source(NodeId parent_id, Node const &parent, Piece played,
        BranchSource source, HoldState hold, std::size_t cursor,
        std::span<Piece const> policy_next, std::vector<Child> &out)
    {
        if (!tetris::toj::can_spawn(parent.board, played))
        {
            return true;
        }
        bool const count = telemetry_on();
        bool const time = timers_on();
        std::int64_t const enum_start = time ? timer_now() : 0;
        auto batch = tetris::toj::enumerate_candidates_into(parent.board, played,
            config_.movement, std::span<Candidate>(candidate_buffer_));
        if (time)
        {
            timers_.enum_ns += timer_now() - enum_start;
        }
        if (!batch.has_value())
        {
            return false;
        }
        if (count)
        {
            ++search_stats_.enumeration_calls;
            search_stats_.raw_kernel_landings += batch->raw_landings;
            search_stats_.unique_candidates += batch->count;
            stats_.enumerated += batch->count;
        }
        // Test-only hook: docs/phase7/count_partition_instrument_design.md §5. default-null; one branch when null.
        bool const record = static_cast<bool>(config_.expand_source_record);
        std::vector<ExpandSourceCandidateRecord> record_entries;
        if (record)
        {
            record_entries.reserve(batch->count);
        }
        auto record_push = [&](Candidate candidate, bool apply_ok, Outcome outcome,
            std::uint64_t hash40, bool survivor) {
            if (!record)
            {
                return;
            }
            ExpandSourceCandidateRecord entry;
            entry.candidate = candidate;
            entry.apply_ok = apply_ok;
            entry.outcome = outcome;
            entry.result_hash40 = hash40;
            entry.survivor = survivor;
            record_entries.push_back(entry);
        };
        auto record_emit = [&](bool overflow) {
            if (!record)
            {
                return;
            }
            ExpandSourceRecord rec;
            rec.board = &parent.board;
            rec.played = played;
            rec.source = source;
            rec.candidates =
                std::span<ExpandSourceCandidateRecord const>(record_entries);
            rec.raw_landings = batch->raw_landings;
            rec.overflow = overflow;
            config_.expand_source_record(rec);
        };
        for (std::size_t index = 0; index < batch->count; ++index)
        {
            Candidate const &candidate = candidate_buffer_[index];
            std::int64_t const rule_start = time ? timer_now() : 0;
            if (count)
            {
                ++search_stats_.rule_applications;
            }
            auto applied = tetris::toj::apply(parent.board, played, candidate);
            if (!applied.has_value())
            {
                if (time)
                {
                    timers_.rule_ns += timer_now() - rule_start;
                }
                record_push(candidate, false, Outcome{}, 0, false);
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
            if (time)
            {
                timers_.rule_ns += timer_now() - rule_start;
            }
            if (same_result)
            {
                record_push(candidate, true, outcome,
                    result_rows_hash40(applied->board), false);
                continue;
            }
            Evaluation evaluation = evaluate_once(applied->board);
            toj_policy::DecisionContext context;
            context.next = policy_next;
            context.hold = hold.piece;
            context.used_hold = source == BranchSource::Hold;
            context.depth = parent.depth;
            std::int64_t const policy_start = time ? timer_now() : 0;
            PolicyState state =
                policy_.transition(played, candidate, outcome, applied->board, parent.policy,
                    context, evaluation);
            if (time)
            {
                timers_.policy_ns += timer_now() - policy_start;
            }
            if (out.size() >= out.capacity())
            {
                record_emit(true);
                return false;
            }
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
            if (count)
            {
                ++search_stats_.policy_transitions;
                ++stats_.transitions;
            }
            record_push(candidate, true, outcome,
                result_rows_hash40(applied->board), true);
        }
        record_emit(false);
        return true;
    }

    bool Engine::expand_parent(NodeId parent_id)
    {
        eval_memo_.clear();
        child_buffer_.clear();
        if (parent_id >= arena_.size())
        {
            return true;
        }
        Node const &node = arena_[parent_id];
        if (!node.expandable)
        {
            return true;
        }
        std::size_t const size = queue_.pieces.size();
        std::size_t const next_start =
            node.cursor + 1 <= size ? node.cursor + 1 : size;
        std::span<Piece const> const policy_next(
            queue_.pieces.data() + next_start, size - next_start);
        bool const has_current = node.cursor < size;
        std::optional<Piece> current;
        if (has_current)
        {
            current = queue_.pieces[node.cursor];
            HoldState hold = node.hold;
            hold.locked = false;
            if (!expand_source(parent_id, node, *current, BranchSource::Current, hold,
                    node.cursor + 1, policy_next, child_buffer_))
            {
                return false;
            }
        }
        if (!node.hold.locked)
        {
            if (node.hold.piece.has_value())
            {
                HoldState hold;
                hold.piece = current;
                hold.locked = false;
                std::size_t const held_cursor =
                    node.cursor + 1 <= size ? node.cursor + 1 : size;
                if (!expand_source(parent_id, node, *node.hold.piece, BranchSource::Hold,
                        hold, held_cursor, policy_next, child_buffer_))
                {
                    return false;
                }
            }
            else if (has_current && node.cursor + 1 < size)
            {
                HoldState hold;
                hold.piece = current;
                hold.locked = false;
                if (!expand_source(parent_id, node, queue_.pieces[node.cursor + 1],
                        BranchSource::Hold, hold, node.cursor + 2, policy_next,
                        child_buffer_))
                {
                    return false;
                }
            }
        }
        return true;
    }

    std::vector<Child> Engine::expand(NodeId parent)
    {
        std::vector<Child> out;
        stats_ = ExpansionStats{};
        if (!expand_parent(parent))
        {
            return out;
        }
        out = child_buffer_;
        return out;
    }

    NodeId Engine::materialize(Child const &child)
    {
        if (arena_.size() >= config_.arena_capacity || arena_.size() >= max_nodes)
        {
            exhausted_ = true;
            return no_node;
        }
        NodeId parent = child.parent;
        std::size_t depth = 0;
        bool parent_is_root = false;
        if (parent < arena_.size())
        {
            depth = arena_[parent].depth + 1;
            parent_is_root = parent == 0;
        }
        Node node;
        node.parent = child.parent;
        node.depth = depth;
        node.board = child.board;
        node.policy = child.state;
        node.evaluation = child.evaluation;
        node.source = child.source;
        node.incoming = child.candidate;
        node.played = child.played;
        node.has_incoming = true;
        node.hold = child.hold;
        node.cursor = child.cursor;
        node.expandable = child.expandable;
        node.root_child = parent_is_root
            ? first_move_fingerprint(child.played, child.candidate, child.source)
            : (parent < arena_.size() ? arena_[parent].root_child : no_node);
        arena_.push_back(node);
        return static_cast<NodeId>(arena_.size() - 1);
    }

    void Engine::link_children(NodeId parent, NodeId first, std::size_t count)
    {
        if (parent >= arena_.size() || count == 0 || first >= arena_.size()
            || count > arena_.size() - first)
        {
            return;
        }
        arena_[parent].first_child = first;
        arena_[parent].child_count = count;
        for (std::size_t k = 0; k < count; ++k)
        {
            NodeId id = first + static_cast<NodeId>(k);
            arena_[id].next_sibling =
                k + 1 < count ? static_cast<NodeId>(id + 1) : no_node;
        }
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

    std::size_t Engine::transposition_used() const
    {
        return transposition_used_;
    }

    std::uint32_t Engine::transposition_epoch_for_test() const
    {
        return transposition_epoch_;
    }

    void Engine::set_transposition_epoch_for_test(std::uint32_t epoch)
    {
        transposition_epoch_ = epoch;
    }

    std::size_t Engine::transposition_physical_entries_for_test() const
    {
        std::size_t count = 0;
        for (auto const &entry : transposition_)
        {
            if (entry.epoch != 0)
            {
                ++count;
            }
        }
        return count;
    }

    TranspositionKey Engine::build_key_for_test(Child const &child) const
    {
        return build_key(child, no_node);
    }

    TranspositionKey Engine::key_from_node_for_test(NodeId id) const
    {
        return key_from_node(id);
    }

    bool Engine::transposition_reinsert_for_test(
        std::uint64_t fp, TranspositionKey const &key, NodeId node)
    {
        return transposition_reinsert(fp, key, node);
    }

    std::size_t Engine::transposition_table_size_for_test() const
    {
        return transposition_.size();
    }

    TranspositionEntry Engine::transposition_entry_for_test(std::size_t slot) const
    {
        return transposition_[slot];
    }

    void Engine::set_transposition_entry_for_test(
        std::size_t slot, TranspositionEntry entry)
    {
        transposition_[slot] = entry;
    }

    std::size_t Engine::arena_reserved_bytes() const
    {
        return arena_.capacity() * sizeof(Node);
    }

    std::uint64_t Engine::retained_bytes() const
    {
        return engine_buffer_reservation(
            static_cast<std::uint64_t>(arena_.capacity()) * sizeof(Node)
                + static_cast<std::uint64_t>(idmap_.capacity()) * sizeof(NodeId),
            static_cast<std::uint64_t>(candidate_buffer_.capacity()) * sizeof(Candidate),
            static_cast<std::uint64_t>(child_buffer_.capacity()) * sizeof(Child),
            static_cast<std::uint64_t>(eval_memo_.capacity())
                * sizeof(std::pair<Board, Evaluation>),
            static_cast<std::uint64_t>(transposition_.capacity())
                * sizeof(TranspositionEntry),
            static_cast<std::uint64_t>(transposition_rehash_.capacity())
                * sizeof(TranspositionEntry),
            static_cast<std::uint64_t>(cache_.reserved_bytes()));
    }

    std::size_t Engine::idmap_reserved_bytes() const
    {
        return idmap_.capacity() * sizeof(NodeId);
    }

    bool Engine::arena_exhausted() const
    {
        return exhausted_;
    }

    bool Engine::search_complete() const
    {
        return search_complete_;
    }

    std::size_t Engine::frontier_count() const
    {
        return max_length_ + 1;
    }

    SearchStats Engine::search_stats() const
    {
        SearchStats stats = search_stats_;
        stats.cache_requests = cache_.requests();
        stats.cache_hits = cache_.hits();
        stats.cache_misses = cache_.misses();
        stats.cache_replacements = cache_.replacements();
        return stats;
    }

    TranspositionKey Engine::build_key(Child const &child, NodeId id) const
    {
        TranspositionKey key;
        Node const &parent_node = arena_[child.parent];
        key.depth = static_cast<std::uint16_t>(parent_node.depth + 1);
        key.cursor = static_cast<std::uint16_t>(child.cursor);
        key.root_child = parent_node.depth == 0
            ? first_move_fingerprint(child.played, child.candidate, child.source)
            : parent_node.root_child;
        key.occupancy = child.board.occupancy();
        key.state = child.state;
        key.state.acc_value = normalize_zero(key.state.acc_value);
        key.state.like = normalize_zero(key.state.like);
        key.state.value = normalize_zero(key.state.value);
        assert(key.state.acc_value == key.state.acc_value
            && key.state.like == key.state.like && key.state.value == key.state.value);
        std::size_t const size = queue_.pieces.size();
        std::size_t const remaining = child.cursor < size ? size - child.cursor : 0;
        key.boundary_count = static_cast<std::uint16_t>(remaining);
        for (std::size_t i = 0; i < remaining; ++i)
        {
            if (queue_.boundary[child.cursor + i])
            {
                key.boundary_bits[i / 64] |= (1ull << (i % 64));
            }
        }
        key.active_piece = child.cursor < size
            ? static_cast<std::uint8_t>(queue_.pieces[child.cursor])
            : no_piece_code;
        key.hold_piece = child.hold.piece.has_value()
            ? static_cast<std::uint8_t>(*child.hold.piece)
            : no_piece_code;
        key.hold_available = !child.hold.locked;
        return key;
    }

    TranspositionKey Engine::key_from_node(NodeId id) const
    {
        TranspositionKey key;
        Node const &node = arena_[id];
        Node const &parent_node = arena_[node.parent];
        key.depth = static_cast<std::uint16_t>(node.depth);
        key.cursor = static_cast<std::uint16_t>(node.cursor);
        key.root_child = parent_node.depth == 0
            ? first_move_fingerprint(node.played, node.incoming, node.source)
            : parent_node.root_child;
        key.occupancy = node.board.occupancy();
        key.state = node.policy;
        key.state.acc_value = normalize_zero(key.state.acc_value);
        key.state.like = normalize_zero(key.state.like);
        key.state.value = normalize_zero(key.state.value);
        assert(key.state.acc_value == key.state.acc_value
            && key.state.like == key.state.like && key.state.value == key.state.value);
        std::size_t const size = queue_.pieces.size();
        std::size_t const remaining = node.cursor < size ? size - node.cursor : 0;
        key.boundary_count = static_cast<std::uint16_t>(remaining);
        for (std::size_t i = 0; i < remaining; ++i)
        {
            if (queue_.boundary[node.cursor + i])
            {
                key.boundary_bits[i / 64] |= (1ull << (i % 64));
            }
        }
        key.active_piece = node.cursor < size
            ? static_cast<std::uint8_t>(queue_.pieces[node.cursor])
            : no_piece_code;
        key.hold_piece = node.hold.piece.has_value()
            ? static_cast<std::uint8_t>(*node.hold.piece)
            : no_piece_code;
        key.hold_available = !node.hold.locked;
        return key;
    }

    Engine::TranspositionProbe Engine::transposition_probe(TranspositionKey const &key)
    {
        return transposition_probe_prehashed(transposition_hash(key), key);
    }

    Engine::TranspositionProbe Engine::transposition_probe_prehashed(
        std::uint64_t fp, TranspositionKey const &expect)
    {
        std::size_t slot = static_cast<std::size_t>(fp) & (transposition_entries - 1);
        auto count_probe = [this](std::size_t length) {
            if (telemetry_on())
            {
                search_stats_.probe_steps += length;
                ++search_stats_.probe_histogram[probe_length_bucket(length)];
            }
        };
        for (std::size_t i = 0; i < transposition_entries; ++i)
        {
            TranspositionEntry &entry = transposition_[slot];
            if (entry.epoch != transposition_epoch_)
            {
                count_probe(i + 1);
                return { false, no_node, &entry, fp };
            }
            if (entry.fp == fp)
            {
                if (telemetry_on())
                {
                    ++search_stats_.probe_rebuilds;
                }
                if (key_from_node(entry.node) == expect)
                {
                    count_probe(i + 1);
                    return { true, entry.node, nullptr, fp };
                }
            }
            slot = (slot + 1) & (transposition_entries - 1);
        }
        count_probe(transposition_entries);
        search_stopped_ = true;
        transposition_exhausted_ = true;
        if (telemetry_on())
        {
            search_stats_.transposition_exhausted = true;
        }
        return { false, no_node, nullptr, fp };
    }

    bool Engine::transposition_reinsert(
        std::uint64_t fp, TranspositionKey const &key, NodeId node)
    {
        TranspositionProbe probe = transposition_probe_prehashed(fp, key);
        if (probe.merged || probe.slot == nullptr)
        {
            return false;
        }
        probe.slot->fp = fp;
        probe.slot->node = node;
        probe.slot->epoch = transposition_epoch_;
        ++transposition_used_;
        return true;
    }

    Engine::MaterializeOutcome Engine::search_materialize(Child const &child)
    {
        bool const time = timers_on();
        std::int64_t const start = time ? timer_now() : 0;
        MaterializeOutcome outcome = search_materialize_inner(child);
        if (time)
        {
            timers_.materialize_ns += timer_now() - start;
        }
        return outcome;
    }

    Engine::MaterializeOutcome Engine::search_materialize_inner(Child const &child)
    {
        if (child.parent >= arena_.size())
        {
            return {};
        }
        Node const &parent_node = arena_[child.parent];
        if (parent_node.depth != 0)
        {
            TranspositionKey key = build_key(child, no_node);
            TranspositionProbe probe = transposition_probe(key);
            if (search_stopped_)
            {
                return {};
            }
            if (probe.merged)
            {
                if (telemetry_on())
                {
                    ++search_stats_.transposition_merges;
                }
                return { true, probe.node };
            }
            NodeId id = materialize(child);
            if (id == no_node)
            {
                return {};
            }
            probe.slot->fp = probe.fp;
            probe.slot->node = id;
            probe.slot->epoch = transposition_epoch_;
            ++transposition_used_;
            arena_[id].registered = true;
            if (telemetry_on())
            {
                ++search_stats_.materialized_nodes;
            }
            return { false, id };
        }
        NodeId id = materialize(child);
        if (id == no_node)
        {
            return {};
        }
        TranspositionKey key = build_key(child, id);
        TranspositionProbe probe = transposition_probe(key);
        if (search_stopped_)
        {
            return { false, id };
        }
        if (probe.merged)
        {
            if (telemetry_on())
            {
                ++search_stats_.transposition_merges;
            }
            arena_.pop_back();
            return { true, probe.node };
        }
        probe.slot->fp = probe.fp;
        probe.slot->node = id;
        probe.slot->epoch = transposition_epoch_;
        ++transposition_used_;
        if (telemetry_on())
        {
            ++search_stats_.materialized_nodes;
        }
        return { false, id };
    }

    void Engine::promote(std::size_t level)
    {
        bool const count = telemetry_on();
        bool const time = timers_on();
        std::int64_t const start = time ? timer_now() : 0;
        NodeId id = heap_.pop_max(level);
        if (count)
        {
            ++search_stats_.expanded_parents;
        }
        ++expanded_count_[level];
        if (expanded_max_[level] == no_node
            || arena_[id].policy.value > arena_[expanded_max_[level]].policy.value)
        {
            expanded_max_[level] = id;
        }
        if (!expand_parent(id))
        {
            search_stopped_ = true;
            return;
        }
        std::size_t const child_level = level - 1;
        for (Child const &child : child_buffer_)
        {
            MaterializeOutcome outcome = search_materialize(child);
            if (search_stopped_ || exhausted_)
            {
                break;
            }
            if (outcome.merged)
            {
                if (!arena_[outcome.id].registered)
                {
                    arena_[outcome.id].registered = true;
                    heap_.push(outcome.id, child_level);
                }
                if (arena_[outcome.id].parent == id)
                {
                    append_child_link(id, outcome.id);
                }
                continue;
            }
            append_child_link(id, outcome.id);
            heap_.push(outcome.id, child_level);
        }
        if (time)
        {
            timers_.parent_ns += timer_now() - start;
        }
    }

    void Engine::refresh_pending_occupancy()
    {
        if (!telemetry_on())
        {
            return;
        }
        std::size_t total_pending = 0;
        for (std::size_t i = 0; i <= max_length_; ++i)
        {
            total_pending += heap_.size(i);
        }
        search_stats_.pending_occupancy = total_pending;
    }

    void Engine::run_pass()
    {
        bool const count = telemetry_on();
        bool const time = timers_on();
        if (width_ == 0)
        {
            if (arena_.empty())
            {
                search_complete_ = true;
                return;
            }
            std::int64_t const start = time ? timer_now() : 0;
            if (!expand_parent(0))
            {
                search_stopped_ = true;
                return;
            }
            if (count)
            {
                ++search_stats_.expanded_parents;
            }
            for (std::size_t child_index = 0; child_index < child_buffer_.size();
                ++child_index)
            {
                MaterializeOutcome outcome =
                    search_materialize(child_buffer_[child_index]);
                if (search_stopped_ || exhausted_)
                {
                    break;
                }
                if (outcome.merged)
                {
                    if (!arena_[outcome.id].registered)
                    {
                        arena_[outcome.id].registered = true;
                        heap_.push(outcome.id, max_length_);
                    }
                    if (arena_[outcome.id].parent == 0)
                    {
                        append_child_link(0, outcome.id);
                    }
                    continue;
                }
                append_child_link(0, outcome.id);
                heap_.push(outcome.id, max_length_);
            }
            if (time)
            {
                timers_.parent_ns += timer_now() - start;
            }
            width_ = 2;
        }
        else
        {
            width_ += 1;
        }
        if (count)
        {
            ++search_stats_.widening_passes;
        }
        double div_ratio = 1.0;
        if (max_length_ > 0)
        {
            double const ratio =
                config_.policy != nullptr ? config_.policy->parameters.ratio : 0.0;
            for (std::size_t k = 0; k < max_length_; ++k)
            {
                width_cache_[k] = std::pow(static_cast<double>(k + 2), ratio);
            }
            div_ratio = 2.0
                / *std::max_element(width_cache_.begin(),
                    width_cache_.begin() + static_cast<std::ptrdiff_t>(max_length_));
        }
        bool complete = true;
        for (std::size_t level = max_length_; level >= 1; --level)
        {
            if (heap_.size(level) == 0)
            {
                continue;
            }
            complete = false;
            double const quota =
                width_cache_[level - 1] * static_cast<double>(width_) * div_ratio;
            std::size_t const hold = std::max<std::size_t>(1, static_cast<std::size_t>(quota));
            if (expanded_count_[level] >= hold)
            {
                NodeId const top = heap_.best(level);
                if (expanded_max_[level] != no_node
                    && arena_[expanded_max_[level]].policy.value < arena_[top].policy.value)
                {
                    promote(level);
                    if (search_stopped_ || exhausted_)
                    {
                        refresh_pending_occupancy();
                        return;
                    }
                }
                else
                {
                    if (count)
                    {
                        ++search_stats_.promotions_refused;
                    }
                }
            }
            else
            {
                while (expanded_count_[level] < hold && heap_.size(level) > 0)
                {
                    promote(level);
                    if (search_stopped_ || exhausted_)
                    {
                        refresh_pending_occupancy();
                        return;
                    }
                }
            }
        }
        if (complete)
        {
            search_complete_ = true;
        }
        refresh_pending_occupancy();
    }

    bool Engine::run(std::size_t max_passes)
    {
        if (search_complete_ || arena_.empty())
        {
            return search_complete_;
        }
        for (std::size_t pass = 0; pass < max_passes; ++pass)
        {
            if (search_complete_ || search_stopped_ || exhausted_)
            {
                break;
            }
            run_pass();
        }
        return search_complete_;
    }

    bool Engine::run(SearchBudget budget)
    {
        if (search_complete_ || arena_.empty())
        {
            return search_complete_;
        }
        std::int64_t const deadline = [this, budget]() {
            if (budget.kind != SearchBudget::Kind::Time)
            {
                return std::numeric_limits<std::int64_t>::max();
            }
            std::int64_t const budget_nanos =
                budget.milliseconds
                    > static_cast<std::uint64_t>(
                        std::numeric_limits<std::int64_t>::max() / 1'000'000)
                ? std::numeric_limits<std::int64_t>::max()
                : static_cast<std::int64_t>(budget.milliseconds) * 1'000'000;
            std::int64_t const start = now_nanos();
            return start > std::numeric_limits<std::int64_t>::max() - budget_nanos
                ? std::numeric_limits<std::int64_t>::max()
                : start + budget_nanos;
        }();
        std::uint64_t passes = 0;
        do
        {
            if (search_complete_ || search_stopped_ || exhausted_)
            {
                break;
            }
            run_pass();
            ++passes;
        } while (!search_complete_ && !search_stopped_ && !exhausted_
            && (budget.kind == SearchBudget::Kind::Time
                    ? now_nanos() < deadline
                    : passes < budget.iterations));
        return search_complete_;
    }

    std::optional<SearchSelection> Engine::select_best() const
    {
        NodeId best = no_node;
        for (std::size_t i = 0; i <= max_length_; ++i)
        {
            NodeId wait_best = heap_.size(i) > 0 ? heap_.best(i) : no_node;
            NodeId sort_best = expanded_max_[i];
            if (wait_best == no_node)
            {
                if (sort_best == no_node)
                {
                    continue;
                }
                best = sort_best;
            }
            else if (sort_best == no_node)
            {
                best = wait_best;
            }
            else
            {
                best = arena_[sort_best].policy.value < arena_[wait_best].policy.value
                    ? wait_best
                    : sort_best;
            }
            break;
        }
        if (best == no_node)
        {
            return std::nullopt;
        }
        NodeId evidence = best;
        while (arena_[best].parent != 0)
        {
            best = arena_[best].parent;
        }
        return SearchSelection{ best, evidence };
    }

    PathTelemetry Engine::path_telemetry() const
    {
        return path_stats_;
    }

    FinalResult Engine::finalize(Placement active_start)
    {
        FinalResult result;
        std::optional<SearchSelection> selection = select_best();
        if (!selection.has_value())
        {
            return result;
        }
        Node const &child = arena_[selection->root_child];
        result.has_selection = true;
        result.candidate = child.incoming;
        result.played = child.played;
        result.state = child.policy;
        result.used_hold = child.source == BranchSource::Hold;
        if (!child.has_incoming)
        {
            if (telemetry_on())
            {
                ++path_stats_.failures;
            }
            return result;
        }
        tetris::path::PathConfig path_config{};
        path_config.allow_180 = config_.movement.allow_180;
        Placement start = child.source == BranchSource::Hold
            ? Placement::unchecked(tetris::toj::spawn_x, tetris::toj::spawn_y, 0)
            : active_start;
        bool const count = telemetry_on();
        bool const time = timers_on();
        std::int64_t const find_start = time ? timer_now() : 0;
        tetris::path::Pathfinder finder(arena_[0].board, child.played, start,
            path_config);
        result.states_expanded = finder.queue_tail;
        result.path = finder.find(child.incoming);
        std::int64_t find_ns = 0;
        if (time)
        {
            find_ns = timer_now() - find_start;
            timers_.path_find_ns += find_ns;
        }
        bool path_ok = result.path.valid;
        std::int64_t replay_ns = 0;
        if (path_ok)
        {
            std::int64_t const replay_start = time ? timer_now() : 0;
            tetris::path::ReplayResult replayed = tetris::path::replay_path(
                arena_[0].board, child.played, start, result.path.view(),
                path_config, true);
            path_ok = replayed.valid
                && replayed.placement == child.incoming.placement
                && (child.played != Piece::T
                    || replayed.arrival == child.incoming.arrival);
            if (time)
            {
                replay_ns = timer_now() - replay_start;
                timers_.path_replay_ns += replay_ns;
            }
        }
        result.elapsed_nanos = find_ns + replay_ns;
        result.path_ok = path_ok;
        if (count)
        {
            ++path_stats_.calls;
            path_stats_.states_expanded += result.states_expanded;
            path_stats_.elapsed_nanos += result.elapsed_nanos;
        }
        if (!result.path_ok)
        {
            result.path = tetris::path::Path{};
            if (count)
            {
                ++path_stats_.failures;
            }
        }
        return result;
    }

    ComponentTimers Engine::component_timers() const
    {
        return timers_;
    }
}
