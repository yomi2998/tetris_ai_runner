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
#ifdef TETRIS_CHILD_SOA_TRIAL
        child_soa_swap_empty();
#else
        std::vector<Child>().swap(child_buffer_);
#endif
        std::vector<std::uint64_t>().swap(eval_memo_fingerprints_);
        std::vector<Board>().swap(eval_memo_boards_);
        std::vector<Evaluation>().swap(eval_memo_evaluations_);
        std::vector<std::uint16_t>().swap(eval_memo_next_);
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
#ifdef TETRIS_CHILD_SOA_TRIAL
            max_children_per_parent * (sizeof(Board) + sizeof(ChildSoaMeta)),
#else
            max_children_per_parent * sizeof(Child),
#endif
            max_children_per_parent * eval_memo_slot_bytes,
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
#ifdef TETRIS_EVAL_INDEX_TRIAL
        if (config_.cache.layout != CacheConfig::Layout::Disabled)
        {
            config_.arena_capacity = 0;
            return false;
        }
        if (EvalIndexTrial::kIndexBytes > engine_memory_budget - fixed_non_cache - cache_bytes)
        {
            config_.arena_capacity = 0;
            return false;
        }
#endif
        std::uint64_t const allowance = engine_memory_budget - fixed_non_cache - cache_bytes
#ifdef TETRIS_EVAL_INDEX_TRIAL
            - EvalIndexTrial::kIndexBytes
#endif
            ;
        if (config_.arena_capacity > max_nodes
            || config_.arena_capacity > allowance / arena_bytes_per_node)
        {
            config_.arena_capacity = 0;
            return false;
        }
        arena_.reserve(config_.arena_capacity);
        idmap_.reserve(config_.arena_capacity);
        heap_.reserve_entries(config_.arena_capacity);
        if (static_cast<std::uint64_t>(arena_.capacity()) * sizeof(Node)
                + static_cast<std::uint64_t>(idmap_.capacity()) * sizeof(NodeId)
                + heap_.reserved_bytes()
            > allowance)
        {
            std::vector<Node>().swap(arena_);
            std::vector<NodeId>().swap(idmap_);
            heap_.release_entries();
            config_.arena_capacity = 0;
            return false;
        }
        candidate_buffer_.resize(max_candidates_per_source);
#ifdef TETRIS_CHILD_SOA_TRIAL
        child_soa_reserve(max_children_per_parent);
#else
        child_buffer_.reserve(max_children_per_parent);
#endif
        eval_memo_fingerprints_.reserve(max_children_per_parent);
        eval_memo_boards_.reserve(max_children_per_parent);
        eval_memo_evaluations_.reserve(max_children_per_parent);
        eval_memo_next_.reserve(max_children_per_parent);
        transposition_.resize(transposition_entries);
        transposition_rehash_.resize(transposition_entries);
        queue_.pieces.reserve(max_queue_length);
        queue_.boundary.reserve(max_queue_length);
        if (config_.cache.layout != CacheConfig::Layout::Disabled)
        {
            cache_.init(config_.cache.layout, config_.cache.entries, config_.cache.ways);
        }
        cache_.set_telemetry_enabled(config_.telemetry_enabled);
#ifdef TETRIS_EVAL_INDEX_TRIAL
        eval_index_.allocate();
        eval_index_.telemetry = config_.telemetry_enabled;
        eval_index_enabled_ = true;
        eval_index_digest_ = 1469598103934665603ull;
#endif
        std::uint64_t const used = engine_buffer_reservation(
            static_cast<std::uint64_t>(arena_.capacity()) * sizeof(Node)
                + static_cast<std::uint64_t>(idmap_.capacity()) * sizeof(NodeId)
                + heap_.reserved_bytes(),
            static_cast<std::uint64_t>(candidate_buffer_.capacity()) * sizeof(Candidate),
#ifdef TETRIS_CHILD_SOA_TRIAL
            static_cast<std::uint64_t>(child_board_buffer_.capacity()) * sizeof(Board)
                + static_cast<std::uint64_t>(child_meta_buffer_.capacity())
                    * sizeof(ChildSoaMeta),
#else
            static_cast<std::uint64_t>(child_buffer_.capacity()) * sizeof(Child),
#endif
            static_cast<std::uint64_t>(eval_memo_fingerprints_.capacity())
                    * sizeof(std::uint64_t)
                + static_cast<std::uint64_t>(eval_memo_boards_.capacity()) * sizeof(Board)
                + static_cast<std::uint64_t>(eval_memo_evaluations_.capacity())
                    * sizeof(Evaluation)
                + static_cast<std::uint64_t>(eval_memo_next_.capacity())
                    * sizeof(std::uint16_t),
            static_cast<std::uint64_t>(transposition_.capacity())
                * sizeof(TranspositionEntry),
            static_cast<std::uint64_t>(transposition_rehash_.capacity())
                * sizeof(TranspositionEntry),
            static_cast<std::uint64_t>(cache_.reserved_bytes())
#ifdef TETRIS_EVAL_INDEX_TRIAL
                + static_cast<std::uint64_t>(eval_index_reserved_bytes())
#endif
            );
        if (used > engine_memory_budget)
        {
            std::vector<Node>().swap(arena_);
            std::vector<Candidate>().swap(candidate_buffer_);
#ifdef TETRIS_CHILD_SOA_TRIAL
            child_soa_swap_empty();
#else
            std::vector<Child>().swap(child_buffer_);
#endif
            std::vector<std::uint64_t>().swap(eval_memo_fingerprints_);
            std::vector<Board>().swap(eval_memo_boards_);
            std::vector<Evaluation>().swap(eval_memo_evaluations_);
            std::vector<std::uint16_t>().swap(eval_memo_next_);
            std::vector<TranspositionEntry>().swap(transposition_);
            std::vector<TranspositionEntry>().swap(transposition_rehash_);
            std::vector<NodeId>().swap(idmap_);
            heap_.release_entries();
            cache_ = EvalCache{};
#ifdef TETRIS_EVAL_INDEX_TRIAL
            eval_index_ = EvalIndexTrial{};
#endif
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
#ifdef TETRIS_EVAL_INDEX_TRIAL
        eval_index_reset_for_new_move();
#endif
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
        clear_eval_memo();
#ifdef TETRIS_EVAL_INDEX_TRIAL
        eval_index_reset_for_new_move();
        for (std::size_t n = 0; n < new_count; ++n)
        {
            eval_index_insert_live(static_cast<NodeId>(n));
        }
#endif
#ifdef TETRIS_CHILD_SOA_TRIAL
        child_soa_clear();
#else
        child_buffer_.clear();
#endif
#ifdef TETRIS_DIRECT_KEY_TRIAL
        direct_key_context_count_ = 0;
#endif
        heap_.reset(max_frontiers);
        for (std::size_t i = 0; i < max_frontiers; ++i)
        {
            expanded_count_[i] = 0;
            expanded_max_[i] = no_node;
        }
#ifdef TETRIS_EVAL_REUSE_TRACE
        if (config_.eval_trace_record)
        {
            EvalTraceRecord reset;
            reset.kind = EvalTraceRecord::Kind::ArenaReset;
            reset.id = static_cast<NodeId>(new_count);
            config_.eval_trace_record(reset);
            for (std::size_t n = 0; n < new_count; ++n)
            {
                EvalTraceRecord node;
                node.kind = EvalTraceRecord::Kind::NodeLive;
                node.id = static_cast<NodeId>(n);
                node.board = &arena_[n].board;
                config_.eval_trace_record(node);
            }
        }
#endif
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

    NodeId Engine::append_child_link(NodeId parent, NodeId child)
    {
        if (parent >= arena_.size() || child >= arena_.size() || parent == child)
        {
            return no_node;
        }
        NodeId cursor = arena_[parent].first_child;
        if (cursor == no_node)
        {
            arena_[parent].first_child = child;
            arena_[parent].child_count = 1;
            arena_[child].next_sibling = no_node;
            return child;
        }
        bool present = false;
        NodeId tail = cursor;
        while (true)
        {
            if (tail == child)
            {
                present = true;
            }
            NodeId sibling = arena_[tail].next_sibling;
            if (sibling == no_node)
            {
                break;
            }
            tail = sibling;
        }
        if (present)
        {
            return tail;
        }
        arena_[tail].next_sibling = child;
        arena_[child].next_sibling = no_node;
        ++arena_[parent].child_count;
        return child;
    }

    NodeId Engine::append_fresh_child_link(NodeId parent, NodeId child, NodeId tail)
    {
        if (parent >= arena_.size() || child >= arena_.size() || parent == child)
        {
            return tail;
        }
        if (tail == no_node || tail >= arena_.size())
        {
            if (arena_[parent].first_child == no_node)
            {
                arena_[parent].first_child = child;
                arena_[parent].child_count = 1;
                arena_[child].next_sibling = no_node;
                return child;
            }
            tail = arena_[parent].first_child;
        }
        while (arena_[tail].next_sibling != no_node)
        {
            tail = arena_[tail].next_sibling;
        }
        arena_[tail].next_sibling = child;
        arena_[child].next_sibling = no_node;
        ++arena_[parent].child_count;
        return child;
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
        clear_eval_memo();
#ifdef TETRIS_CHILD_SOA_TRIAL
        child_soa_clear();
#else
        child_buffer_.clear();
#endif
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
#ifdef TETRIS_EVAL_INDEX_TRIAL
        eval_index_insert_live(0);
#endif
#ifdef TETRIS_EVAL_REUSE_TRACE
        if (config_.eval_trace_record)
        {
            EvalTraceRecord reset;
            reset.kind = EvalTraceRecord::Kind::ArenaReset;
            reset.id = 0;
            config_.eval_trace_record(reset);
            EvalTraceRecord node;
            node.kind = EvalTraceRecord::Kind::NodeLive;
            node.id = 0;
            node.board = &arena_[0].board;
            config_.eval_trace_record(node);
        }
#endif
        return 0;
    }

    void Engine::clear_eval_memo()
    {
        eval_memo_fingerprints_.clear();
        eval_memo_boards_.clear();
        eval_memo_evaluations_.clear();
        eval_memo_next_.clear();
        eval_memo_heads_.fill(eval_memo_no_entry);
    }
#ifdef TETRIS_EVAL_INDEX_TRIAL
    void Engine::eval_index_reset_for_new_move()
    {
        eval_index_.clear_slots();
        eval_index_.reset_counters();
    }
    void Engine::eval_index_insert_live(NodeId id)
    {
        if (!eval_index_enabled_ || id >= arena_.size())
        {
            return;
        }
        Board const &board = arena_[id].board;
        std::uint64_t const finalized =
            eval_index_finalize(occupancy_fingerprint(board.occupancy()));
        std::size_t const slot =
            static_cast<std::size_t>(finalized & (EvalIndexTrial::kSlots - 1));
        std::uint32_t const tag = static_cast<std::uint32_t>(finalized >> 32);
        std::uint32_t const stored = eval_index_.slots[slot];
        if (stored != EvalIndexTrial::kEmpty
            && (!EvalIndexTrial::kTagged || eval_index_.tags[slot] == tag)
            && stored < arena_.size() && arena_[stored].board == board)
        {
            eval_index_.slots[slot] = id;
            if (eval_index_.telemetry)
            {
                ++eval_index_.insertions;
            }
            return;
        }
        if (stored != EvalIndexTrial::kEmpty && eval_index_.telemetry)
        {
            ++eval_index_.replacements;
        }
        eval_index_.slots[slot] = id;
        if (EvalIndexTrial::kTagged)
        {
            eval_index_.tags[slot] = tag;
        }
        if (eval_index_.telemetry)
        {
            ++eval_index_.insertions;
        }
    }
    std::optional<Evaluation> Engine::eval_index_find(
        Board const &board, std::uint64_t fingerprint)
    {
        if (!eval_index_enabled_)
        {
            return std::nullopt;
        }
        if (eval_index_.telemetry)
        {
            ++eval_index_.requests;
        }
        std::uint64_t const finalized = eval_index_finalize(fingerprint);
        std::size_t const slot =
            static_cast<std::size_t>(finalized & (EvalIndexTrial::kSlots - 1));
        std::uint32_t const tag = static_cast<std::uint32_t>(finalized >> 32);
        std::uint32_t const stored = eval_index_.slots[slot];
        bool const tag_ok = !EvalIndexTrial::kTagged || eval_index_.tags[slot] == tag;
        bool const live = stored != EvalIndexTrial::kEmpty && stored < arena_.size();
        bool const board_eq = tag_ok && live && (arena_[stored].board == board);
        if (!board_eq)
        {
            if (eval_index_.telemetry)
            {
                ++eval_index_.misses;
                if (tag_ok && live)
                {
                    ++eval_index_.tag_mismatches;
                }
            }
            return std::nullopt;
        }
        if (eval_index_.telemetry)
        {
            ++eval_index_.hits;
        }
        return arena_[stored].evaluation;
    }
    std::size_t Engine::eval_index_reserved_bytes() const
    {
        return eval_index_.reserved_bytes();
    }
    void Engine::eval_index_mix_digest(Board const &board, Evaluation const &evaluation)
    {
        std::uint64_t mixed = eval_index_digest_;
        for (int i = 0; i < Board::occupancy_t::word_count(); ++i)
        {
            mixed ^= board.occupancy().logical_word(i);
            mixed *= 1099511628211ull;
        }
        std::uint64_t bits = 0;
        std::memcpy(&bits, &evaluation.value, sizeof(double));
        mixed ^= bits;
        mixed *= 1099511628211ull;
        mixed ^= static_cast<std::uint64_t>(static_cast<std::uint16_t>(evaluation.t2_value));
        mixed *= 1099511628211ull;
        mixed ^= static_cast<std::uint64_t>(static_cast<std::uint16_t>(evaluation.t3_value));
        mixed *= 1099511628211ull;
        eval_index_digest_ = mixed;
    }
    std::uint64_t Engine::eval_index_digest_for_test() const
    {
        return eval_index_digest_;
    }
    std::size_t Engine::eval_index_requests_for_test() const
    {
        return static_cast<std::size_t>(eval_index_.requests);
    }
    std::size_t Engine::eval_index_hits_for_test() const
    {
        return static_cast<std::size_t>(eval_index_.hits);
    }
    std::size_t Engine::eval_index_misses_for_test() const
    {
        return static_cast<std::size_t>(eval_index_.misses);
    }
    std::size_t Engine::eval_index_replacements_for_test() const
    {
        return static_cast<std::size_t>(eval_index_.replacements);
    }
    std::size_t Engine::eval_index_insertions_for_test() const
    {
        return static_cast<std::size_t>(eval_index_.insertions);
    }
    std::size_t Engine::eval_index_clears_for_test() const
    {
        return static_cast<std::size_t>(eval_index_.clears);
    }
    std::size_t Engine::eval_index_tag_mismatches_for_test() const
    {
        return static_cast<std::size_t>(eval_index_.tag_mismatches);
    }
    std::size_t Engine::eval_index_occupied_for_test() const
    {
        std::size_t occupied = 0;
        for (std::uint32_t stored : eval_index_.slots)
        {
            if (stored != EvalIndexTrial::kEmpty)
            {
                ++occupied;
            }
        }
        return occupied;
    }
    std::size_t Engine::eval_index_reserved_for_test() const
    {
        return eval_index_reserved_bytes();
    }
    std::uint32_t Engine::eval_index_slot_for_test(std::size_t slot) const
    {
        if (slot >= eval_index_.slots.size())
        {
            return EvalIndexTrial::kEmpty;
        }
        return eval_index_.slots[slot];
    }
    void Engine::eval_index_overwrite_for_test(std::size_t slot, std::uint32_t tag, NodeId id)
    {
        if (slot >= eval_index_.slots.size())
        {
            return;
        }
        eval_index_.slots[slot] = id;
        if (EvalIndexTrial::kTagged)
        {
            eval_index_.tags[slot] = tag;
        }
    }
    void Engine::set_eval_index_enabled_for_test(bool enabled)
    {
        eval_index_enabled_ = enabled;
    }
    void Engine::eval_index_search_materialize_for_test(
        Child const &child, NodeId &id, bool &merged)
    {
        MaterializeOutcome outcome = search_materialize(child);
        id = outcome.id;
        merged = outcome.merged;
    }
#endif

#ifdef TETRIS_EVAL_REUSE_TRACE
    Evaluation Engine::evaluate_once(Board const &board)
    {
        return evaluate_once_for_parent(board, no_node, 0, BranchSource::Current);
    }

    Evaluation Engine::evaluate_once_for_parent(Board const &board, NodeId parent,
        std::size_t depth, BranchSource source)
#else
    Evaluation Engine::evaluate_once(Board const &board
#ifdef TETRIS_ROW_FUSION_TRIAL
        , bool safe_lockout, bool safe_has_next, Piece safe_next, int *safe_out,
        bool *safe_supplied
#endif
    )
#endif
    {
        bool const count = telemetry_on();
        bool const time = timers_on();
        std::int64_t const start = time ? timer_now() : 0;
        if (count)
        {
            ++search_stats_.eval_requests;
#ifdef TETRIS_EVAL_REUSE_TRACE
            if (config_.eval_trace_record)
            {
                EvalTraceRecord record;
                record.kind = EvalTraceRecord::Kind::EvalRequest;
                record.source = source;
                record.depth = depth > 0xFFFFFFFFu
                    ? 0xFFFFFFFFu
                    : static_cast<std::uint32_t>(depth);
                record.id = parent;
                record.board = &board;
                config_.eval_trace_record(record);
            }
#endif
        }
        std::uint64_t const fingerprint = occupancy_fingerprint(board.occupancy());
        std::size_t const bucket = eval_memo_bucket(fingerprint);
        for (std::uint16_t index = eval_memo_heads_[bucket];
            index != eval_memo_no_entry; index = eval_memo_next_[index])
        {
            if (eval_memo_fingerprints_[index] == fingerprint
                && eval_memo_boards_[index] == board)
            {
                if (count)
                {
                    ++search_stats_.eval_memo_hits;
                }
                if (time)
                {
                    timers_.eval_hit_ns += timer_now() - start;
                }
#ifdef TETRIS_EVAL_INDEX_TRIAL
                if (count)
                {
                    eval_index_mix_digest(board, eval_memo_evaluations_[index]);
                }
#endif
                return eval_memo_evaluations_[index];
            }
        }
#ifdef TETRIS_EVAL_INDEX_TRIAL
        if (auto indexed = eval_index_find(board, fingerprint))
        {
            if (eval_memo_fingerprints_.size() < eval_memo_fingerprints_.capacity())
            {
                std::uint16_t const slot =
                    static_cast<std::uint16_t>(eval_memo_fingerprints_.size());
                eval_memo_fingerprints_.push_back(fingerprint);
                eval_memo_boards_.push_back(board);
                eval_memo_evaluations_.push_back(*indexed);
                eval_memo_next_.push_back(eval_memo_heads_[bucket]);
                eval_memo_heads_[bucket] = slot;
            }
            if (time)
            {
                timers_.eval_hit_ns += timer_now() - start;
            }
            if (count)
            {
                eval_index_mix_digest(board, *indexed);
            }
            return *indexed;
        }
#endif
        if (config_.cache.layout != CacheConfig::Layout::Disabled)
        {
            if (count)
            {
                ++search_stats_.cache_requests;
            }
            if (auto cached = cache_.find(board))
            {
                if (eval_memo_fingerprints_.size() < eval_memo_fingerprints_.capacity())
                {
                    std::uint16_t const slot =
                        static_cast<std::uint16_t>(eval_memo_fingerprints_.size());
                    eval_memo_fingerprints_.push_back(fingerprint);
                    eval_memo_boards_.push_back(board);
                    eval_memo_evaluations_.push_back(*cached);
                    eval_memo_next_.push_back(eval_memo_heads_[bucket]);
                    eval_memo_heads_[bucket] = slot;
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
#ifdef TETRIS_ROW_FUSION_TRIAL
        RowFusionSafeInputs safe_inputs;
        safe_inputs.lockout = safe_lockout;
        safe_inputs.has_next = safe_has_next;
        safe_inputs.next = safe_next;
        Evaluation evaluation = policy_.evaluate(board,
            safe_out != nullptr ? &safe_inputs : nullptr, safe_out);
        if (safe_out != nullptr)
        {
            *safe_supplied = true;
        }
#else
        Evaluation evaluation = policy_.evaluate(board);
#endif
        if (eval_memo_fingerprints_.size() < eval_memo_fingerprints_.capacity())
        {
            std::uint16_t const slot =
                static_cast<std::uint16_t>(eval_memo_fingerprints_.size());
            eval_memo_fingerprints_.push_back(fingerprint);
            eval_memo_boards_.push_back(board);
            eval_memo_evaluations_.push_back(evaluation);
            eval_memo_next_.push_back(eval_memo_heads_[bucket]);
            eval_memo_heads_[bucket] = slot;
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
#ifdef TETRIS_EVAL_INDEX_TRIAL
        if (count)
        {
            eval_index_mix_digest(board, evaluation);
        }
#endif
        return evaluation;
    }

#ifndef TETRIS_CHILD_SOA_TRIAL
    bool Engine::expand_source(NodeId parent_id, Node const &parent, Piece played,
        BranchSource source, HoldState hold, std::size_t cursor,
        std::span<Piece const> policy_next, std::vector<Child> &out)
    {
        if (!tetris::toj::can_spawn(parent.board, played))
        {
            return true;
        }
#ifdef TETRIS_DIRECT_KEY_TRIAL
        if (direct_key_context_count_ < direct_key_max_sources)
        {
            direct_key_contexts_[direct_key_context_count_++] =
                direct_key_begin_source(parent, hold, cursor, queue_);
        }
#endif
        return reachability::call_with_block<tetris::toj::SRS>(played,
            [&]<reachability::block B>() {
                return expand_source_for_block<B>(parent_id, parent, played, source, hold,
                    cursor, policy_next, out);
            });
    }

    template <auto B>
        requires reachability::block_spec<decltype(B)>
    bool Engine::expand_source_for_block(NodeId parent_id, Node const &parent, Piece played,
        BranchSource source, HoldState hold, std::size_t cursor,
        std::span<Piece const> policy_next, std::vector<Child> &out)
    {
        bool const count = telemetry_on();
        bool const time = timers_on();
        std::size_t const source_begin = out.size();
        std::int64_t const enum_start = time ? timer_now() : 0;
        auto batch = tetris::toj::detail::enumerate_into_for_block<B>(parent.board, played,
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
            Board const *result_board, bool survivor) {
            if (!record)
            {
                return;
            }
            ExpandSourceCandidateRecord entry;
            entry.candidate = candidate;
            entry.apply_ok = apply_ok;
            entry.outcome = outcome;
            entry.result_hash40 = result_board == nullptr
                ? 0
                : result_rows_hash40(*result_board);
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
        toj_policy::DecisionContext context;
        context.next = policy_next;
        context.hold = hold.piece;
        context.used_hold = source == BranchSource::Hold;
        context.depth = parent.depth;
#ifdef TETRIS_ROW_FUSION_TRIAL
        bool const row_fusion_has_next = !policy_next.empty();
        Piece const row_fusion_next_piece = row_fusion_has_next ? policy_next[0] : Piece::I;
        if (context.next.data() != policy_next.data())
        {
            ++rf_source_mismatches;
        }
#endif
        std::int64_t const context_start = time ? timer_now() : 0;
        int const t_expect = toj_policy::Policy::expected_t_distance(context);
        if (time)
        {
            timers_.policy_ns += timer_now() - context_start;
        }
        for (std::size_t index = 0; index < batch->count; ++index)
        {
            Candidate const &candidate = candidate_buffer_[index];
            std::int64_t const rule_start = time ? timer_now() : 0;
            if (count)
            {
                ++search_stats_.rule_applications;
            }
            auto applied =
                tetris::toj::detail::apply_for_block<B>(parent.board, candidate);
            if (!applied.has_value())
            {
                if (time)
                {
                    timers_.rule_ns += timer_now() - rule_start;
                }
                record_push(candidate, false, Outcome{}, nullptr, false);
                continue;
            }
            Outcome outcome;
            outcome.spin = applied->spin;
            outcome.clear_count = applied->clear_count;
            outcome.lockout = applied->lockout;
            bool same_result = false;
            for (std::size_t child_index = source_begin; child_index < out.size(); ++child_index)
            {
                Child const &child = out[child_index];
                if (child.board == applied->board && child.outcome == outcome)
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
                record_push(candidate, true, outcome, &applied->board, false);
                continue;
            }
#ifdef TETRIS_EVAL_REUSE_TRACE
            Evaluation evaluation = evaluate_once_for_parent(applied->board,
                parent_id, parent.depth, source);
#else
#ifdef TETRIS_ROW_FUSION_TRIAL
            int fused_safe = 0;
            bool fused_supplied = false;
            Evaluation evaluation = evaluate_once(applied->board, outcome.lockout,
                row_fusion_has_next, row_fusion_next_piece, &fused_safe, &fused_supplied);
#else
            Evaluation evaluation = evaluate_once(applied->board);
#endif
#endif
            std::int64_t const policy_start = time ? timer_now() : 0;
            PolicyState state =
                policy_.transition_known_lockout(played, candidate, outcome, applied->board,
                    parent.policy, context, evaluation, outcome.lockout, t_expect
#ifdef TETRIS_ROW_FUSION_TRIAL
                    , fused_supplied ? &fused_safe : nullptr
#endif
                );
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
            record_push(candidate, true, outcome, &applied->board, true);
        }
        record_emit(false);
        return true;
    }
#endif

#ifdef TETRIS_CHILD_SOA_TRIAL
    std::size_t Engine::child_soa_size() const
    {
        return child_board_buffer_.size();
    }
    std::size_t Engine::child_soa_capacity() const
    {
        return child_board_buffer_.capacity() < child_meta_buffer_.capacity()
            ? child_board_buffer_.capacity()
            : child_meta_buffer_.capacity();
    }
    void Engine::child_soa_clear()
    {
        child_board_buffer_.clear();
        child_meta_buffer_.clear();
    }
    void Engine::child_soa_reserve(std::size_t count)
    {
        child_board_buffer_.reserve(count);
        child_meta_buffer_.reserve(count);
    }
    void Engine::child_soa_swap_empty()
    {
        std::vector<Board>().swap(child_board_buffer_);
        std::vector<ChildSoaMeta>().swap(child_meta_buffer_);
    }
    bool Engine::child_soa_try_push(Board const &board, ChildSoaMeta const &meta)
    {
        if (child_soa_size() >= child_soa_capacity())
        {
            return false;
        }
        child_board_buffer_.push_back(board);
        child_meta_buffer_.push_back(meta);
        return true;
    }
    ChildSoaConstView Engine::child_soa_view(std::size_t index) const
    {
        return ChildSoaConstView{ child_board_buffer_[index], child_meta_buffer_[index] };
    }
    Child Engine::child_soa_gather(std::size_t index) const
    {
        ChildSoaConstView view = child_soa_view(index);
        Child child;
        child.board = view.board;
        child.state = view.meta.state;
        child.evaluation = view.meta.evaluation;
        child.cursor = view.meta.cursor;
        child.hold = view.meta.hold;
        child.outcome = view.meta.outcome;
        child.parent = view.meta.parent;
        child.candidate = view.meta.candidate;
        child.played = view.meta.played;
        child.source = view.meta.source;
        child.expandable = view.meta.expandable;
        return child;
    }
    std::uint64_t Engine::child_soa_reserved_bytes() const
    {
        return static_cast<std::uint64_t>(child_board_buffer_.capacity()) * sizeof(Board)
            + static_cast<std::uint64_t>(child_meta_buffer_.capacity())
                * sizeof(ChildSoaMeta);
    }
    bool Engine::expand_source(NodeId parent_id, Node const &parent, Piece played,
        BranchSource source, HoldState hold, std::size_t cursor,
        std::span<Piece const> policy_next)
    {
        if (!tetris::toj::can_spawn(parent.board, played))
        {
            return true;
        }
        return reachability::call_with_block<tetris::toj::SRS>(played,
            [&]<reachability::block B>() {
                return expand_source_for_block<B>(parent_id, parent, played, source, hold,
                    cursor, policy_next);
            });
    }
    template <auto B>
        requires reachability::block_spec<decltype(B)>
    bool Engine::expand_source_for_block(NodeId parent_id, Node const &parent, Piece played,
        BranchSource source, HoldState hold, std::size_t cursor,
        std::span<Piece const> policy_next)
    {
        bool const count = telemetry_on();
        bool const time = timers_on();
        std::size_t const source_begin = child_soa_size();
        std::int64_t const enum_start = time ? timer_now() : 0;
        auto batch = tetris::toj::detail::enumerate_into_for_block<B>(parent.board, played,
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
        bool const record = static_cast<bool>(config_.expand_source_record);
        std::vector<ExpandSourceCandidateRecord> record_entries;
        if (record)
        {
            record_entries.reserve(batch->count);
        }
        auto record_push = [&](Candidate candidate, bool apply_ok, Outcome outcome,
            Board const *result_board, bool survivor) {
            if (!record)
            {
                return;
            }
            ExpandSourceCandidateRecord entry;
            entry.candidate = candidate;
            entry.apply_ok = apply_ok;
            entry.outcome = outcome;
            entry.result_hash40 = result_board == nullptr
                ? 0
                : result_rows_hash40(*result_board);
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
        toj_policy::DecisionContext context;
        context.next = policy_next;
        context.hold = hold.piece;
        context.used_hold = source == BranchSource::Hold;
        context.depth = parent.depth;
#ifdef TETRIS_ROW_FUSION_TRIAL
        bool const row_fusion_has_next = !policy_next.empty();
        Piece const row_fusion_next_piece = row_fusion_has_next ? policy_next[0] : Piece::I;
        if (context.next.data() != policy_next.data())
        {
            ++rf_source_mismatches;
        }
#endif
        std::int64_t const context_start = time ? timer_now() : 0;
        int const t_expect = toj_policy::Policy::expected_t_distance(context);
        if (time)
        {
            timers_.policy_ns += timer_now() - context_start;
        }
        for (std::size_t index = 0; index < batch->count; ++index)
        {
            Candidate const &candidate = candidate_buffer_[index];
            std::int64_t const rule_start = time ? timer_now() : 0;
            if (count)
            {
                ++search_stats_.rule_applications;
            }
            auto applied =
                tetris::toj::detail::apply_for_block<B>(parent.board, candidate);
            if (!applied.has_value())
            {
                if (time)
                {
                    timers_.rule_ns += timer_now() - rule_start;
                }
                record_push(candidate, false, Outcome{}, nullptr, false);
                continue;
            }
            Outcome outcome;
            outcome.spin = applied->spin;
            outcome.clear_count = applied->clear_count;
            outcome.lockout = applied->lockout;
            bool same_result = false;
            for (std::size_t child_index = source_begin; child_index < child_soa_size();
                ++child_index)
            {
                if (child_board_buffer_[child_index] == applied->board
                    && child_meta_buffer_[child_index].outcome == outcome)
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
                record_push(candidate, true, outcome, &applied->board, false);
                continue;
            }
            Evaluation evaluation = evaluate_once(applied->board);
            std::int64_t const policy_start = time ? timer_now() : 0;
            PolicyState state =
                policy_.transition_known_lockout(played, candidate, outcome, applied->board,
                    parent.policy, context, evaluation, outcome.lockout, t_expect);
            if (time)
            {
                timers_.policy_ns += timer_now() - policy_start;
            }
            ChildSoaMeta meta;
            meta.state = state;
            meta.evaluation = evaluation;
            meta.cursor = cursor;
            meta.hold = hold;
            meta.outcome = outcome;
            meta.parent = parent_id;
            meta.candidate = candidate;
            meta.played = played;
            meta.source = source;
            meta.expandable = !applied->lockout;
            if (!child_soa_try_push(applied->board, meta))
            {
                record_emit(true);
                return false;
            }
            if (count)
            {
                ++search_stats_.policy_transitions;
                ++stats_.transitions;
            }
            record_push(candidate, true, outcome, &applied->board, true);
        }
        record_emit(false);
        return true;
    }
    TranspositionKey Engine::build_key_soa(std::size_t index, NodeId id) const
    {
        (void)id;
        TranspositionKey key;
        ChildSoaMeta const &meta = child_meta_buffer_[index];
        Node const &parent_node = arena_[meta.parent];
        key.depth = static_cast<std::uint16_t>(parent_node.depth + 1);
        key.cursor = static_cast<std::uint16_t>(meta.cursor);
        key.root_child = parent_node.depth == 0
            ? first_move_fingerprint(meta.played, meta.candidate, meta.source)
            : parent_node.root_child;
        key.occupancy = child_board_buffer_[index].occupancy();
        key.state = meta.state;
        key.state.acc_value = normalize_zero(key.state.acc_value);
        key.state.like = normalize_zero(key.state.like);
        key.state.value = normalize_zero(key.state.value);
        assert(key.state.acc_value == key.state.acc_value
            && key.state.like == key.state.like && key.state.value == key.state.value);
        std::size_t const size = queue_.pieces.size();
        std::size_t const remaining = meta.cursor < size ? size - meta.cursor : 0;
        key.boundary_count = static_cast<std::uint16_t>(remaining);
        for (std::size_t i = 0; i < remaining; ++i)
        {
            if (queue_.boundary[meta.cursor + i])
            {
                key.boundary_bits[i / 64] |= (1ull << (i % 64));
            }
        }
        key.active_piece = meta.cursor < size
            ? static_cast<std::uint8_t>(queue_.pieces[meta.cursor])
            : no_piece_code;
        key.hold_piece = meta.hold.piece.has_value()
            ? static_cast<std::uint8_t>(*meta.hold.piece)
            : no_piece_code;
        key.hold_available = !meta.hold.locked;
        return key;
    }
    NodeId Engine::materialize_soa(std::size_t index)
    {
        ChildSoaMeta const &meta = child_meta_buffer_[index];
        NodeId const parent = meta.parent;
        if (arena_.size() >= config_.arena_capacity || arena_.size() >= max_nodes)
        {
            exhausted_ = true;
            return no_node;
        }
        std::size_t depth = 0;
        bool parent_is_root = false;
        if (parent < arena_.size())
        {
            depth = arena_[parent].depth + 1;
            parent_is_root = parent == 0;
        }
        NodeId const root_child = parent_is_root
            ? first_move_fingerprint(meta.played, meta.candidate, meta.source)
            : (parent < arena_.size() ? arena_[parent].root_child : no_node);
        NodeId const id = static_cast<NodeId>(arena_.size());
        arena_.emplace_back(child_soa_view(index), depth, root_child);
        return id;
    }
    Engine::MaterializeOutcome Engine::search_materialize_soa(std::size_t index)
    {
        bool const time = timers_on();
        std::int64_t const start = time ? timer_now() : 0;
        if (child_meta_buffer_[index].parent >= arena_.size())
        {
            return {};
        }
        TranspositionKey const key = build_key_soa(index, no_node);
        MaterializeOutcome outcome =
            search_materialize_inner_soa(index, key, transposition_hash(key));
        if (time)
        {
            timers_.materialize_ns += timer_now() - start;
        }
        return outcome;
    }
    Engine::MaterializeOutcome Engine::search_materialize_prehashed_soa(std::size_t index,
        TranspositionKey const &key, std::uint64_t fp)
    {
        bool const time = timers_on();
        std::int64_t const start = time ? timer_now() : 0;
        MaterializeOutcome outcome = search_materialize_inner_soa(index, key, fp);
        if (time)
        {
            timers_.materialize_ns += timer_now() - start;
        }
        return outcome;
    }
    Engine::MaterializeOutcome Engine::search_materialize_inner_soa(std::size_t index,
        TranspositionKey const &key, std::uint64_t fp)
    {
        if (child_meta_buffer_[index].parent >= arena_.size())
        {
            return {};
        }
        Node const &parent_node = arena_[child_meta_buffer_[index].parent];
        if (parent_node.depth != 0)
        {
            TranspositionProbe probe = transposition_probe_prehashed(fp, key);
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
            NodeId id = materialize_soa(index);
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
        NodeId id = materialize_soa(index);
        if (id == no_node)
        {
            return {};
        }
        TranspositionProbe probe = transposition_probe_prehashed(fp, key);
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
    bool Engine::accept_child_soa(MaterializeOutcome outcome, NodeId parent,
        std::size_t child_level, NodeId &tail)
    {
        if (search_stopped_ || exhausted_)
        {
            return false;
        }
        if (outcome.merged)
        {
            if (!arena_[outcome.id].registered)
            {
                arena_[outcome.id].registered = true;
                heap_.push(outcome.id, child_level);
            }
            if (arena_[outcome.id].parent == parent)
            {
                tail = append_child_link(parent, outcome.id);
            }
            return true;
        }
        tail = append_fresh_child_link(parent, outcome.id, tail);
        heap_.push(outcome.id, child_level);
        return true;
    }
    std::size_t Engine::child_soa_size_for_test() const
    {
        return child_soa_size();
    }
    std::size_t Engine::child_soa_capacity_for_test() const
    {
        return child_soa_capacity();
    }
    std::size_t Engine::child_soa_board_capacity_for_test() const
    {
        return child_board_buffer_.capacity();
    }
    std::size_t Engine::child_soa_meta_capacity_for_test() const
    {
        return child_meta_buffer_.capacity();
    }
    bool Engine::child_soa_lockstep_for_test() const
    {
        return child_board_buffer_.size() == child_meta_buffer_.size();
    }
    bool Engine::child_soa_board_alignment_for_test() const
    {
        if (child_board_buffer_.empty())
        {
            return true;
        }
        std::uintptr_t address =
            reinterpret_cast<std::uintptr_t>(child_board_buffer_.data());
        return address % alignof(Board) == 0;
    }
    std::uint64_t Engine::child_soa_reserved_for_test() const
    {
        return child_soa_reserved_bytes();
    }
    Child Engine::child_soa_gather_for_test(std::size_t index) const
    {
        return child_soa_gather(index);
    }
    TranspositionKey Engine::child_soa_key_for_test(std::size_t index) const
    {
        return build_key_soa(index, no_node);
    }
    NodeId Engine::child_soa_materialize_for_test(std::size_t index)
    {
        return materialize_soa(index);
    }
    bool Engine::child_soa_try_push_for_test(Child const &child)
    {
        ChildSoaMeta meta;
        meta.state = child.state;
        meta.evaluation = child.evaluation;
        meta.cursor = child.cursor;
        meta.hold = child.hold;
        meta.outcome = child.outcome;
        meta.parent = child.parent;
        meta.candidate = child.candidate;
        meta.played = child.played;
        meta.source = child.source;
        meta.expandable = child.expandable;
        return child_soa_try_push(child.board, meta);
    }
    bool Engine::child_soa_search_materialize_for_test(std::size_t index, NodeId &id,
        bool &merged)
    {
        MaterializeOutcome outcome = search_materialize_soa(index);
        id = outcome.id;
        merged = outcome.merged;
        return search_stopped_ == false;
    }
#endif

    bool Engine::expand_parent(NodeId parent_id)
    {
        clear_eval_memo();
#ifdef TETRIS_CHILD_SOA_TRIAL
        child_soa_clear();
#else
        child_buffer_.clear();
#endif
#ifdef TETRIS_DIRECT_KEY_TRIAL
        direct_key_context_count_ = 0;
#endif
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
#ifdef TETRIS_CHILD_SOA_TRIAL
            if (!expand_source(parent_id, node, *current, BranchSource::Current, hold,
                    node.cursor + 1, policy_next))
#else
            if (!expand_source(parent_id, node, *current, BranchSource::Current, hold,
                    node.cursor + 1, policy_next, child_buffer_))
#endif
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
#ifdef TETRIS_CHILD_SOA_TRIAL
                if (!expand_source(parent_id, node, *node.hold.piece, BranchSource::Hold,
                        hold, held_cursor, policy_next))
#else
                if (!expand_source(parent_id, node, *node.hold.piece, BranchSource::Hold,
                        hold, held_cursor, policy_next, child_buffer_))
#endif
                {
                    return false;
                }
            }
            else if (has_current && node.cursor + 1 < size)
            {
                HoldState hold;
                hold.piece = current;
                hold.locked = false;
#ifdef TETRIS_CHILD_SOA_TRIAL
                if (!expand_source(parent_id, node, queue_.pieces[node.cursor + 1],
                        BranchSource::Hold, hold, node.cursor + 2, policy_next))
#else
                if (!expand_source(parent_id, node, queue_.pieces[node.cursor + 1],
                        BranchSource::Hold, hold, node.cursor + 2, policy_next,
                        child_buffer_))
#endif
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
#ifdef TETRIS_CHILD_SOA_TRIAL
        out.reserve(child_soa_size());
        for (std::size_t index = 0; index < child_soa_size(); ++index)
        {
            out.push_back(child_soa_gather(index));
        }
#else
        out = child_buffer_;
#endif
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
        NodeId const root_child = parent_is_root
            ? first_move_fingerprint(child.played, child.candidate, child.source)
            : (parent < arena_.size() ? arena_[parent].root_child : no_node);
        NodeId const id = static_cast<NodeId>(arena_.size());
        arena_.emplace_back(child, depth, root_child);
        return id;
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

#ifdef TETRIS_DIRECT_KEY_TRIAL
    std::uint64_t Engine::direct_key_hash_for_test(Child const &child) const
    {
        return direct_key_hash_child(child);
    }

    bool Engine::direct_key_node_matches_for_test(NodeId node_id, Child const &child) const
    {
        if (node_id >= arena_.size() || child.parent >= arena_.size())
        {
            return false;
        }
        Node const &node = arena_[node_id];
        if (node.parent >= arena_.size())
        {
            return false;
        }
        Node const &parent_node = arena_[node.parent];
        DirectKeySourceContext const &context = direct_key_select_context(child);
        return direct_key_node_matches(node, parent_node, context, queue_, child.board,
            child.state, child.played, child.candidate, child.source);
    }

    std::size_t Engine::direct_key_context_count_for_test() const
    {
        return direct_key_context_count_;
    }

    DirectKeySourceContext const &Engine::direct_key_context_for_test(
        std::size_t index) const
    {
        return direct_key_contexts_[index];
    }

    void Engine::direct_key_probe_outcome_for_test(std::uint64_t fp, Child const &child,
        bool &merged, NodeId &node)
    {
        merged = false;
        node = no_node;
        if (child.parent >= arena_.size())
        {
            return;
        }
        DirectKeySourceContext const &context = direct_key_select_context(child);
        TranspositionProbe probe = transposition_probe_direct(fp, child, context);
        merged = probe.merged;
        node = probe.node;
    }

    void Engine::direct_key_probe_materialized_outcome_for_test(std::uint64_t fp,
        TranspositionKey const &key, bool &merged, NodeId &node)
    {
        TranspositionProbe probe = transposition_probe_prehashed(fp, key);
        merged = probe.merged;
        node = probe.node;
    }
#endif

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
                + static_cast<std::uint64_t>(idmap_.capacity()) * sizeof(NodeId)
                + heap_.reserved_bytes(),
            static_cast<std::uint64_t>(candidate_buffer_.capacity()) * sizeof(Candidate),
#ifdef TETRIS_CHILD_SOA_TRIAL
            static_cast<std::uint64_t>(child_board_buffer_.capacity()) * sizeof(Board)
                + static_cast<std::uint64_t>(child_meta_buffer_.capacity())
                    * sizeof(ChildSoaMeta),
#else
            static_cast<std::uint64_t>(child_buffer_.capacity()) * sizeof(Child),
#endif
            static_cast<std::uint64_t>(eval_memo_fingerprints_.capacity())
                    * sizeof(std::uint64_t)
                + static_cast<std::uint64_t>(eval_memo_boards_.capacity()) * sizeof(Board)
                + static_cast<std::uint64_t>(eval_memo_evaluations_.capacity())
                    * sizeof(Evaluation)
                + static_cast<std::uint64_t>(eval_memo_next_.capacity())
                    * sizeof(std::uint16_t),
            static_cast<std::uint64_t>(transposition_.capacity())
                * sizeof(TranspositionEntry),
            static_cast<std::uint64_t>(transposition_rehash_.capacity())
                * sizeof(TranspositionEntry),
            static_cast<std::uint64_t>(cache_.reserved_bytes())
#ifdef TETRIS_EVAL_INDEX_TRIAL
                + static_cast<std::uint64_t>(eval_index_reserved_bytes())
#endif
            );
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
#ifdef TETRIS_EVAL_INDEX_TRIAL
        stats.cache_requests = static_cast<std::size_t>(eval_index_.requests);
        stats.cache_hits = static_cast<std::size_t>(eval_index_.hits);
        stats.cache_misses = static_cast<std::size_t>(eval_index_.misses);
        stats.cache_replacements = static_cast<std::size_t>(eval_index_.replacements);
#endif
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

#ifdef TETRIS_DIRECT_KEY_TRIAL
    DirectKeySourceContext const &Engine::direct_key_select_context(
        Child const &child) const
    {
        Node const &parent_node = arena_[child.parent];
        std::uint16_t const depth = static_cast<std::uint16_t>(parent_node.depth + 1);
        std::uint16_t const cursor = static_cast<std::uint16_t>(child.cursor);
        std::uint8_t const hold_piece = child.hold.piece.has_value()
            ? static_cast<std::uint8_t>(*child.hold.piece)
            : no_piece_code;
        bool const hold_available = !child.hold.locked;
        for (std::size_t i = 0; i < direct_key_context_count_; ++i)
        {
            DirectKeySourceContext const &context = direct_key_contexts_[i];
            if (context.depth == depth && context.cursor == cursor
                && context.hold_piece == hold_piece
                && context.hold_available == hold_available)
            {
                return context;
            }
        }
        direct_key_fallback_ =
            direct_key_begin_source(parent_node, child.hold, child.cursor, queue_);
        return direct_key_fallback_;
    }

    std::uint64_t Engine::direct_key_hash_child(Child const &child) const
    {
        DirectKeySourceContext const &context = direct_key_select_context(child);
        return direct_key_hash(context, child.board, child.state, child.played,
            child.candidate, child.source);
    }

    Engine::TranspositionProbe Engine::transposition_probe_direct(std::uint64_t fp,
        Child const &child, DirectKeySourceContext const &context)
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
                Node const &node = arena_[entry.node];
                Node const &parent_node = arena_[node.parent];
                if (direct_key_node_matches(node, parent_node, context, queue_,
                        child.board, child.state, child.played, child.candidate,
                        child.source))
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

    Engine::MaterializeOutcome Engine::search_materialize_direct(Child const &child)
    {
        bool const time = timers_on();
        std::int64_t const start = time ? timer_now() : 0;
        MaterializeOutcome outcome{};
        if (child.parent < arena_.size())
        {
            DirectKeySourceContext const &context = direct_key_select_context(child);
            std::uint64_t const fp = direct_key_hash(context, child.board, child.state,
                child.played, child.candidate, child.source);
            outcome = search_materialize_inner_direct(child, context, fp);
        }
        if (time)
        {
            timers_.materialize_ns += timer_now() - start;
        }
        return outcome;
    }

    Engine::MaterializeOutcome Engine::search_materialize_prehashed_direct(
        Child const &child, std::uint64_t fp)
    {
        bool const time = timers_on();
        std::int64_t const start = time ? timer_now() : 0;
        MaterializeOutcome outcome{};
        if (child.parent < arena_.size())
        {
            DirectKeySourceContext const &context = direct_key_select_context(child);
            outcome = search_materialize_inner_direct(child, context, fp);
        }
        if (time)
        {
            timers_.materialize_ns += timer_now() - start;
        }
        return outcome;
    }

    Engine::MaterializeOutcome Engine::search_materialize_inner_direct(
        Child const &child, DirectKeySourceContext const &context, std::uint64_t fp)
    {
        if (child.parent >= arena_.size())
        {
            return {};
        }
        Node const &parent_node = arena_[child.parent];
        if (parent_node.depth != 0)
        {
            TranspositionProbe probe = transposition_probe_direct(fp, child, context);
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
        TranspositionProbe probe = transposition_probe_direct(fp, child, context);
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
#endif

    Engine::MaterializeOutcome Engine::search_materialize(Child const &child)
    {
        bool const time = timers_on();
        std::int64_t const start = time ? timer_now() : 0;
        if (child.parent >= arena_.size())
        {
            return {};
        }
        TranspositionKey const key = build_key(child, no_node);
        MaterializeOutcome outcome =
            search_materialize_inner(child, key, transposition_hash(key));
        if (time)
        {
            timers_.materialize_ns += timer_now() - start;
        }
        return outcome;
    }

    Engine::MaterializeOutcome Engine::search_materialize_prehashed(Child const &child,
        TranspositionKey const &key, std::uint64_t fp)
    {
        bool const time = timers_on();
        std::int64_t const start = time ? timer_now() : 0;
        MaterializeOutcome outcome = search_materialize_inner(child, key, fp);
        if (time)
        {
            timers_.materialize_ns += timer_now() - start;
        }
        return outcome;
    }

    Engine::MaterializeOutcome Engine::search_materialize_inner(Child const &child,
        TranspositionKey const &key, std::uint64_t fp)
    {
        if (child.parent >= arena_.size())
        {
            return {};
        }
        Node const &parent_node = arena_[child.parent];
        if (parent_node.depth != 0)
        {
            TranspositionProbe probe = transposition_probe_prehashed(fp, key);
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
#ifdef TETRIS_EVAL_INDEX_TRIAL
            eval_index_insert_live(id);
#endif
            if (telemetry_on())
            {
                ++search_stats_.materialized_nodes;
            }
#ifdef TETRIS_EVAL_REUSE_TRACE
            if (config_.eval_trace_record)
            {
                EvalTraceRecord record;
                record.kind = EvalTraceRecord::Kind::NodeLive;
                record.id = id;
                record.board = &child.board;
                config_.eval_trace_record(record);
            }
#endif
            return { false, id };
        }
        NodeId id = materialize(child);
        if (id == no_node)
        {
            return {};
        }
        TranspositionProbe probe = transposition_probe_prehashed(fp, key);
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
#ifdef TETRIS_EVAL_INDEX_TRIAL
        eval_index_insert_live(id);
#endif
        if (telemetry_on())
        {
            ++search_stats_.materialized_nodes;
        }
#ifdef TETRIS_EVAL_REUSE_TRACE
        if (config_.eval_trace_record)
        {
            EvalTraceRecord record;
            record.kind = EvalTraceRecord::Kind::NodeLive;
            record.id = id;
            record.board = &child.board;
            config_.eval_trace_record(record);
        }
#endif
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
        NodeId tail = arena_[id].first_child;
        while (tail != no_node && arena_[tail].next_sibling != no_node)
        {
            tail = arena_[tail].next_sibling;
        }
        auto accept_child = [&](Child const &child, MaterializeOutcome outcome) {
            if (search_stopped_ || exhausted_)
            {
                return false;
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
                    tail = append_child_link(id, outcome.id);
                }
                return true;
            }
            tail = append_fresh_child_link(id, outcome.id, tail);
            heap_.push(outcome.id, child_level);
            return true;
        };
        constexpr std::size_t hash_batch_size = 8;
        std::size_t child_index = 0;
        bool halted = false;
#ifdef TETRIS_CHILD_SOA_TRIAL
        for (; child_index + hash_batch_size <= child_soa_size();
            child_index += hash_batch_size)
        {
            std::int64_t const hash_start = time ? timer_now() : 0;
            std::array<TranspositionKey, hash_batch_size> const keys{
                build_key_soa(child_index, no_node),
                build_key_soa(child_index + 1, no_node),
                build_key_soa(child_index + 2, no_node),
                build_key_soa(child_index + 3, no_node),
                build_key_soa(child_index + 4, no_node),
                build_key_soa(child_index + 5, no_node),
                build_key_soa(child_index + 6, no_node),
                build_key_soa(child_index + 7, no_node),
            };
            auto const hashes = transposition_hash_batch(keys);
            if (time)
            {
                timers_.materialize_ns += timer_now() - hash_start;
            }
            for (std::size_t k = 0; k < hash_batch_size; ++k)
            {
                MaterializeOutcome outcome = search_materialize_prehashed_soa(
                    child_index + k, keys[k], hashes[k]);
                if (!accept_child_soa(outcome, id, child_level, tail))
                {
                    halted = true;
                    break;
                }
            }
            if (halted)
            {
                break;
            }
        }
        for (; !halted && child_index < child_soa_size(); ++child_index)
        {
            if (!accept_child_soa(search_materialize_soa(child_index), id, child_level,
                    tail))
            {
                break;
            }
        }
#else
#ifdef TETRIS_DIRECT_KEY_TRIAL
        for (; child_index + hash_batch_size <= child_buffer_.size();
            child_index += hash_batch_size)
        {
            std::int64_t const hash_start = time ? timer_now() : 0;
            std::array<std::uint64_t, hash_batch_size> hashes{};
            for (std::size_t k = 0; k < hash_batch_size; ++k)
            {
                hashes[k] = direct_key_hash_child(child_buffer_[child_index + k]);
            }
            if (time)
            {
                timers_.materialize_ns += timer_now() - hash_start;
            }
            for (std::size_t k = 0; k < hash_batch_size; ++k)
            {
                MaterializeOutcome outcome = search_materialize_prehashed_direct(
                    child_buffer_[child_index + k], hashes[k]);
                if (!accept_child(child_buffer_[child_index + k], outcome))
                {
                    halted = true;
                    break;
                }
            }
            if (halted)
            {
                break;
            }
        }
        for (; !halted && child_index < child_buffer_.size(); ++child_index)
        {
            Child const &child = child_buffer_[child_index];
            if (!accept_child(child, search_materialize_direct(child)))
            {
                break;
            }
        }
#else
        for (; child_index + hash_batch_size <= child_buffer_.size();
            child_index += hash_batch_size)
        {
            std::int64_t const hash_start = time ? timer_now() : 0;
            std::array<TranspositionKey, hash_batch_size> const keys{
                build_key(child_buffer_[child_index], no_node),
                build_key(child_buffer_[child_index + 1], no_node),
                build_key(child_buffer_[child_index + 2], no_node),
                build_key(child_buffer_[child_index + 3], no_node),
                build_key(child_buffer_[child_index + 4], no_node),
                build_key(child_buffer_[child_index + 5], no_node),
                build_key(child_buffer_[child_index + 6], no_node),
                build_key(child_buffer_[child_index + 7], no_node),
            };
            auto const hashes = transposition_hash_batch(keys);
            if (time)
            {
                timers_.materialize_ns += timer_now() - hash_start;
            }
            for (std::size_t k = 0; k < hash_batch_size; ++k)
            {
                MaterializeOutcome outcome = search_materialize_prehashed(
                    child_buffer_[child_index + k], keys[k], hashes[k]);
                if (!accept_child(child_buffer_[child_index + k], outcome))
                {
                    halted = true;
                    break;
                }
            }
            if (halted)
            {
                break;
            }
        }
        for (; !halted && child_index < child_buffer_.size(); ++child_index)
        {
            Child const &child = child_buffer_[child_index];
            if (!accept_child(child, search_materialize(child)))
            {
                break;
            }
        }
#endif
#endif
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
            NodeId tail = arena_[0].first_child;
            while (tail != no_node && arena_[tail].next_sibling != no_node)
            {
                tail = arena_[tail].next_sibling;
            }
#ifdef TETRIS_CHILD_SOA_TRIAL
            for (std::size_t child_index = 0; child_index < child_soa_size();
                ++child_index)
            {
                MaterializeOutcome outcome = search_materialize_soa(child_index);
#else
            for (std::size_t child_index = 0; child_index < child_buffer_.size();
                ++child_index)
            {
#ifdef TETRIS_DIRECT_KEY_TRIAL
                MaterializeOutcome outcome =
                    search_materialize_direct(child_buffer_[child_index]);
#else
                MaterializeOutcome outcome =
                    search_materialize(child_buffer_[child_index]);
#endif
#endif
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
                        tail = append_child_link(0, outcome.id);
                    }
                    continue;
                }
                tail = append_fresh_child_link(0, outcome.id, tail);
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
