#pragma once

#include "tetris_types.h"
#include "toj_pathfinder.h"
#include "toj_rule.h"
#include "toj_policy.h"

#include <array>
#include <bit>
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
    inline constexpr std::size_t transposition_entries = 1048576;
    static_assert(std::has_single_bit(transposition_entries),
        "the transposition probe masks with the table size");
    inline constexpr std::uint8_t no_piece_code = 0xFF;
    inline constexpr std::uint64_t engine_queue_reservation =
        max_queue_length * (sizeof(Piece) + sizeof(bool));
    inline constexpr std::uint64_t engine_stack_peak_allowance = 128ull << 10;

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

    // board_t is deliberately over-aligned for the reachability kernel.  Storing
    // one in every transposition slot would carry that alignment and 56 bytes of
    // padding into each key.  The table only needs the logical words, so keep a
    // compact value copy with the same exact word-wise identity.
    struct TranspositionOccupancy
    {
        std::array<std::uint64_t, Board::occupancy_t::word_count()> words{};

        TranspositionOccupancy() = default;

        TranspositionOccupancy(Board::occupancy_t const &occupancy)
        {
            *this = occupancy;
        }

        TranspositionOccupancy &operator=(Board::occupancy_t const &occupancy)
        {
            for (int i = 0; i < Board::occupancy_t::word_count(); ++i)
            {
                words[static_cast<std::size_t>(i)] = occupancy.logical_word(i);
            }
            return *this;
        }

        std::uint64_t logical_word(int index) const
        {
            return words[static_cast<std::size_t>(index)];
        }

        bool operator==(TranspositionOccupancy const &) const = default;
    };

    struct TranspositionKey
    {
        TranspositionOccupancy occupancy{};
        std::array<std::uint64_t, 4> boundary_bits{};
        PolicyState state{};
        std::uint16_t depth = 0;
        std::uint16_t cursor = 0;
        std::uint16_t boundary_count = 0;
        std::uint32_t root_child = no_node;
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

    static_assert(sizeof(TranspositionKey) == 152,
        "the transposition key stores occupancy without kernel alignment padding");

    inline double normalize_zero(double v)
    {
        return v == 0.0 ? 0.0 : v;
    }

    // Word-wise FNV-1a over the key fields in declaration order. Each field
    // contributes ceil(sizeof(field)/8) 64-bit words (short fields occupy one
    // zero-extended word); every word takes one xor/multiply step. The input
    // set, field order, and signed-zero normalization match the former
    // byte-wise FNV exactly, so merges (which key on equality, not layout)
    // and every work-vector count are unchanged — only the slot mapping
    // differs, and the table fills to capacity regardless of layout.
    inline std::uint64_t transposition_hash(TranspositionKey const &key)
    {
        std::uint64_t h = 1469598103934665603ull;
        auto mix = [&h](void const *data, std::size_t bytes) {
            auto const *p = static_cast<std::uint8_t const *>(data);
            while (bytes >= sizeof(std::uint64_t))
            {
                std::uint64_t word = 0;
                __builtin_memcpy(&word, p, sizeof word);
                h ^= word;
                h *= 1099511628211ull;
                p += sizeof word;
                bytes -= sizeof word;
            }
            if (bytes > 0)
            {
                std::uint64_t word = 0;
                __builtin_memcpy(&word, p, bytes);
                h ^= word;
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

    template <std::size_t N, class Value>
    inline void transposition_hash_mix(std::array<std::uint64_t, N> &hashes,
        std::array<TranspositionKey, N> const &keys, Value value)
    {
        for (std::size_t i = 0; i < N; ++i)
        {
            hashes[i] ^= static_cast<std::uint64_t>(value(keys[i]));
            hashes[i] *= 1099511628211ull;
        }
    }

    inline std::uint64_t transposition_double_word(double value)
    {
        return std::bit_cast<std::uint64_t>(normalize_zero(value));
    }

    template <std::size_t N>
    inline std::array<std::uint64_t, N> transposition_hash_batch(
        std::array<TranspositionKey, N> const &keys)
    {
        std::array<std::uint64_t, N> hashes;
        hashes.fill(1469598103934665603ull);
        transposition_hash_mix(hashes, keys,
            [](TranspositionKey const &key) { return key.depth; });
        transposition_hash_mix(hashes, keys,
            [](TranspositionKey const &key) { return key.cursor; });
        transposition_hash_mix(hashes, keys,
            [](TranspositionKey const &key) { return key.boundary_count; });
        transposition_hash_mix(hashes, keys,
            [](TranspositionKey const &key) { return key.root_child; });
        for (int word = 0; word < Board::occupancy_t::word_count(); ++word)
        {
            transposition_hash_mix(hashes, keys,
                [word](TranspositionKey const &key) {
                    return key.occupancy.logical_word(word);
                });
        }
        transposition_hash_mix(hashes, keys,
            [](TranspositionKey const &key) {
                return static_cast<std::uint8_t>(key.state.death);
            });
        transposition_hash_mix(hashes, keys,
            [](TranspositionKey const &key) {
                return static_cast<std::uint8_t>(key.state.combo);
            });
        transposition_hash_mix(hashes, keys,
            [](TranspositionKey const &key) {
                return static_cast<std::uint8_t>(key.state.under_attack);
            });
        transposition_hash_mix(hashes, keys,
            [](TranspositionKey const &key) {
                return static_cast<std::uint8_t>(key.state.map_rise);
            });
        transposition_hash_mix(hashes, keys,
            [](TranspositionKey const &key) {
                return static_cast<std::uint8_t>(key.state.b2b);
            });
        transposition_hash_mix(hashes, keys,
            [](TranspositionKey const &key) {
                return static_cast<std::uint16_t>(key.state.t2_value);
            });
        transposition_hash_mix(hashes, keys,
            [](TranspositionKey const &key) {
                return static_cast<std::uint16_t>(key.state.t3_value);
            });
        transposition_hash_mix(hashes, keys,
            [](TranspositionKey const &key) {
                return transposition_double_word(key.state.acc_value);
            });
        transposition_hash_mix(hashes, keys,
            [](TranspositionKey const &key) {
                return transposition_double_word(key.state.like);
            });
        transposition_hash_mix(hashes, keys,
            [](TranspositionKey const &key) {
                return transposition_double_word(key.state.value);
            });
        for (std::size_t word = 0; word < keys[0].boundary_bits.size(); ++word)
        {
            transposition_hash_mix(hashes, keys,
                [word](TranspositionKey const &key) {
                    return key.boundary_bits[word];
                });
        }
        transposition_hash_mix(hashes, keys,
            [](TranspositionKey const &key) { return key.active_piece; });
        transposition_hash_mix(hashes, keys,
            [](TranspositionKey const &key) { return key.hold_piece; });
        transposition_hash_mix(hashes, keys,
            [](TranspositionKey const &key) {
                return static_cast<std::uint8_t>(key.hold_available);
            });
        return hashes;
    }

    // docs/phase7/transposition_capacity_design.md §2: fingerprint-gated slot.
    // Merge soundness comes from exact key verification on fp match, never the
    // fingerprint alone.
    struct TranspositionEntry
    {
        std::uint64_t fp = 0;
        NodeId node = no_node;
        std::uint32_t epoch = 0;
    };

    static_assert(sizeof(TranspositionEntry) == 16,
        "the fingerprint-gated slot stays compact and naturally aligned");

    struct Child;

    struct Node
    {
        Node() = default;
        Node(Child const &child, std::size_t depth_value, NodeId root_child_value);
        Board board;
        PolicyState policy;
        Evaluation evaluation;
        std::size_t child_count = 0;
        std::size_t cursor = 0;
        std::size_t depth = 0;
        HoldState hold;
        Candidate incoming;
        Piece played = Piece::T;
        NodeId parent = no_node;
        NodeId first_child = no_node;
        NodeId pending_child = no_node;
        NodeId pending_sibling = no_node;
        NodeId root_child = no_node;
        NodeId next_sibling = no_node;
        bool has_incoming = false;
        BranchSource source = BranchSource::Current;
        bool expandable = true;
        bool registered = false;
    };

    static_assert(sizeof(Node) == 192,
        "the search node stays within three cache lines");

    class PendingHeap
    {
    public:
        struct PendingEntry
        {
            double value = 0.0;
            NodeId child = no_node;
            NodeId sibling = no_node;
        };

        static_assert(sizeof(PendingEntry) == 16,
            "the pending entry stays within one quarter cache line");

        explicit PendingHeap(std::vector<Node> &arena)
            : arena_(&arena)
        {
        }

        PendingHeap(PendingHeap &&other, std::vector<Node> &arena) noexcept
            : arena_(&arena)
            , roots_(std::move(other.roots_))
            , counts_(std::move(other.counts_))
            , entries_(std::move(other.entries_))
        {
        }

        PendingHeap(PendingHeap const &) = delete;
        PendingHeap &operator=(PendingHeap const &) = delete;

        void reserve_entries(std::size_t capacity)
        {
            entries_ = std::vector<PendingEntry>{};
            entries_.reserve(capacity);
            entries_.resize(capacity);
        }

        void release_entries()
        {
            entries_ = std::vector<PendingEntry>{};
        }

        std::size_t reserved_bytes() const
        {
            return entries_.capacity() * sizeof(PendingEntry);
        }

        void reset(std::size_t frontier_count)
        {
            roots_.assign(frontier_count, no_node);
            counts_.assign(frontier_count, 0);
        }

        void push(NodeId id, std::size_t level)
        {
            std::size_t const index = static_cast<std::size_t>(id);
            if (index >= entries_.size())
            {
                entries_.resize(index + 1);
            }
            PendingEntry &entry = entries_[index];
            entry.value = (*arena_)[id].policy.value;
            entry.child = no_node;
            entry.sibling = no_node;
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
            NodeId chain = entries_[root].child;
            entries_[root].child = no_node;
            entries_[root].sibling = no_node;
            NodeId pairs = no_node;
            while (chain != no_node)
            {
                NodeId first = chain;
                NodeId second = entries_[first].sibling;
                NodeId rest = second != no_node ? entries_[second].sibling : no_node;
                NodeId pair = second != no_node ? meld(first, second) : first;
                entries_[pair].sibling = pairs;
                pairs = pair;
                chain = rest;
            }
            NodeId acc = no_node;
            while (pairs != no_node)
            {
                NodeId next = entries_[pairs].sibling;
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
            double const va = entries_[a].value;
            double const vb = entries_[b].value;
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
            entries_[b].sibling = entries_[a].child;
            entries_[a].child = b;
            return a;
        }

        std::vector<Node> *arena_;
        std::vector<NodeId> roots_;
        std::vector<std::size_t> counts_;
        std::vector<PendingEntry> entries_;
    };

    inline constexpr std::uint64_t arena_bytes_per_node =
        sizeof(Node) + sizeof(NodeId) + sizeof(PendingHeap::PendingEntry);

    struct Child
    {
        Board board;
        PolicyState state;
        Evaluation evaluation;
        std::size_t cursor = 0;
        HoldState hold;
        Outcome outcome;
        NodeId parent = no_node;
        Candidate candidate;
        Piece played = Piece::T;
        BranchSource source = BranchSource::Current;
        bool expandable = true;
    };

    static_assert(sizeof(Child) == 192,
        "the staged child stays within three cache lines");

    inline Node::Node(Child const &child, std::size_t depth_value, NodeId root_child_value)
        : board(child.board)
        , policy(child.state)
        , evaluation(child.evaluation)
        , cursor(child.cursor)
        , depth(depth_value)
        , hold(child.hold)
        , incoming(child.candidate)
        , played(child.played)
        , parent(child.parent)
        , root_child(root_child_value)
        , has_incoming(true)
        , source(child.source)
        , expandable(child.expandable)
    {
    }

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
        // docs/phase7/transposition_capacity_design.md §5 Step 0: per-visited-entry
        // probe cost telemetry. probe_histogram buckets probe lengths
        // [1,2,3-4,5-8,9-16,17-32,33-64,65+], one increment per probe call.
        std::uint64_t probe_steps = 0;
        std::array<std::uint64_t, 8> probe_histogram{};
        std::uint64_t probe_rebuilds = 0;
        std::size_t pending_occupancy = 0;
        bool transposition_exhausted = false;
    };

    struct ComponentTimers
    {
        std::int64_t enum_ns = 0;
        std::int64_t rule_ns = 0;
        std::int64_t eval_hit_ns = 0;
        std::int64_t eval_miss_ns = 0;
        std::int64_t policy_ns = 0;
        std::int64_t materialize_ns = 0;
        std::int64_t parent_ns = 0;
        std::int64_t path_find_ns = 0;
        std::int64_t path_replay_ns = 0;
    };

    inline std::uint32_t first_move_fingerprint(Piece played, Candidate const &candidate,
        BranchSource source)
    {
        auto const &placement = candidate.placement;
        return static_cast<std::uint32_t>((static_cast<std::uint32_t>(placement.x()) << 24)
            | (static_cast<std::uint32_t>(placement.y()) << 16)
            | (static_cast<std::uint32_t>(placement.rotation()) << 12)
            | (static_cast<std::uint32_t>(source) << 8)
            | (static_cast<std::uint32_t>(played) << 4)
            | (static_cast<std::uint32_t>(candidate.arrival) & 0xFu));
    }

    struct SearchSelection
    {
        NodeId root_child = no_node;
        NodeId evidence = no_node;

        bool operator==(SearchSelection const &) const = default;
    };

    struct PathTelemetry
    {
        std::size_t calls = 0;
        std::size_t states_expanded = 0;
        std::int64_t elapsed_nanos = 0;
        std::size_t failures = 0;
    };

    struct FinalResult
    {
        bool has_selection = false;
        bool path_ok = false;
        std::optional<Candidate> candidate;
        Piece played = Piece::T;
        PolicyState state;
        bool used_hold = false;
        tetris::path::Path path;
        std::size_t states_expanded = 0;
        std::int64_t elapsed_nanos = 0;
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

    inline std::uint64_t occupancy_fingerprint(Board::occupancy_t const &occupancy)
    {
        std::uint64_t fingerprint = 0;
        for (int i = 0; i < Board::occupancy_t::word_count(); ++i)
        {
            fingerprint ^= std::rotl(occupancy.logical_word(i), i * 7);
        }
        return fingerprint;
    }

    inline constexpr std::size_t eval_memo_hash_buckets = 64;
    inline constexpr std::uint16_t eval_memo_no_entry = 0xFFFFu;
    inline constexpr std::size_t eval_memo_slot_bytes =
        sizeof(std::uint64_t) + sizeof(Board) + sizeof(Evaluation)
        + sizeof(std::uint16_t);

    inline std::size_t eval_memo_bucket(std::uint64_t fingerprint)
    {
        return static_cast<std::size_t>((fingerprint * 0x9E3779B97F4A7C15ull) >> 58);
    }

    struct CacheConfig
    {
        enum class Layout : std::uint8_t
        {
            Disabled,
            DirectMapped,
            SetAssociative,
        };

        Layout layout = Layout::Disabled;
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
        void init(CacheConfig::Layout layout, std::size_t entries, std::size_t ways,
            std::uint64_t stamp_seed = 0)
        {
            requests_ = 0;
            hits_ = 0;
            misses_ = 0;
            replacements_ = 0;
            stamp_ = stamp_seed;
            std::size_t const effective_ways =
                layout == CacheConfig::Layout::DirectMapped ? 1 : ways;
            bool const usable = entries >= 1 && effective_ways >= 1
                && entries % effective_ways == 0
                && std::has_single_bit(entries / effective_ways);
            if (!usable)
            {
                entries_.clear();
                ways_ = 1;
                return;
            }
            ways_ = effective_ways;
            entries_ = std::vector<EvalCacheEntry>(entries);
        }

        void clear()
        {
            for (auto &entry : entries_)
            {
                entry = EvalCacheEntry{};
            }
            reset_counters();
        }

        void reset_counters()
        {
            requests_ = 0;
            hits_ = 0;
            misses_ = 0;
            replacements_ = 0;
        }

        void set_telemetry_enabled(bool enabled)
        {
            telemetry_ = enabled;
        }

        std::optional<Evaluation> find(Board const &board)
        {
            if (entries_.empty())
            {
                return std::nullopt;
            }
            if (telemetry_)
            {
                ++requests_;
            }
            std::size_t const set = set_of(board);
            for (std::size_t way = 0; way < ways_; ++way)
            {
                EvalCacheEntry &entry = entries_[set * ways_ + way];
                if (entry.used && entry.occupancy == board.occupancy())
                {
                    if (telemetry_)
                    {
                        ++hits_;
                    }
                    return entry.evaluation;
                }
            }
            if (telemetry_)
            {
                ++misses_;
            }
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
                std::uint64_t oldest = age_of(entries_[victim].stamp);
                for (std::size_t way = 1; way < ways_; ++way)
                {
                    std::uint64_t const candidate_age =
                        age_of(entries_[set * ways_ + way].stamp);
                    if (candidate_age > oldest)
                    {
                        oldest = candidate_age;
                        victim = set * ways_ + way;
                    }
                }
                if (telemetry_)
                {
                    ++replacements_;
                }
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

        std::uint64_t age_of(std::uint64_t stamp) const
        {
            return stamp_ - stamp;
        }

        std::vector<EvalCacheEntry> entries_;
        std::size_t ways_ = 1;
        std::uint64_t stamp_ = 0;
        bool telemetry_ = true;
        std::size_t requests_ = 0;
        std::size_t hits_ = 0;
        std::size_t misses_ = 0;
        std::size_t replacements_ = 0;
    };

    inline constexpr std::uint64_t engine_buffer_reservation(
        std::uint64_t arena_bytes, std::uint64_t candidate_bytes, std::uint64_t child_bytes,
        std::uint64_t memo_bytes, std::uint64_t transposition_bytes,
        std::uint64_t rehash_bytes, std::uint64_t cache_bytes)
    {
        return arena_bytes + candidate_bytes + child_bytes + memo_bytes + transposition_bytes
            + rehash_bytes + cache_bytes
            + engine_queue_reservation + engine_stack_peak_allowance
            + engine_frontier_metadata
            + eval_memo_hash_buckets * sizeof(std::uint16_t);
    }

    inline constexpr std::uint64_t engine_fixed_workspace = engine_buffer_reservation(0,
        max_candidates_per_source * sizeof(Candidate),
        max_children_per_parent * sizeof(Child),
        max_children_per_parent * eval_memo_slot_bytes,
        transposition_entries * sizeof(TranspositionEntry),
        transposition_entries * sizeof(TranspositionEntry),
        16384 * sizeof(EvalCacheEntry));

    inline constexpr std::size_t default_arena_capacity =
        static_cast<std::size_t>((engine_memory_budget - engine_fixed_workspace)
            / arena_bytes_per_node);

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
        std::function<std::int64_t()> timer_nanos = steady_clock_nanos;
        CacheConfig cache;
        bool telemetry_enabled = true;
        bool timers_enabled = true;
        // Test-only hook: docs/phase7/count_partition_instrument_design.md §5. default-null; one branch when null.
        // Set only by the test-only candidate_partition target.
        std::function<void(struct ExpandSourceRecord const &)> expand_source_record;
#ifdef TETRIS_EVAL_REUSE_TRACE
        std::function<void(struct EvalTraceRecord const &)> eval_trace_record;
#endif
    };

    // Test-only count-partition record for one Engine::expand_source call.
    // board is borrowed for the callback duration; copy words synchronously.
    struct ExpandSourceCandidateRecord
    {
        Candidate candidate{};
        bool apply_ok = false;
        Outcome outcome{};
        std::uint64_t result_hash40 = 0; // FNV-1a over result rows 0..39
        bool survivor = false; // intra-source dedup survivor (implies apply_ok)
    };

    struct ExpandSourceRecord
    {
        Board const *board = nullptr;
        Piece played = Piece::T;
        BranchSource source = BranchSource::Current;
        std::span<ExpandSourceCandidateRecord const> candidates{};
        std::size_t raw_landings = 0;
        bool overflow = false; // child-buffer fail-stop path taken
    };

#ifdef TETRIS_EVAL_REUSE_TRACE
    struct EvalTraceRecord
    {
        enum class Kind : std::uint8_t
        {
            EvalRequest = 0,
            NodeLive = 1,
            ArenaReset = 2,
        };
        Kind kind = Kind::EvalRequest;
        BranchSource source = BranchSource::Current;
        std::uint32_t depth = 0;
        std::uint16_t move = 0;
        NodeId id = no_node;
        Board const *board = nullptr;
    };
#endif

    // FNV-1a over result rows 0..39; the driver computes identically.
    inline std::uint64_t result_rows_hash40(Board const &board)
    {
        std::uint64_t h = 1469598103934665603ull;
        for (int y = 0; y < 40; ++y)
        {
            h ^= static_cast<std::uint64_t>(board.row(y));
            h *= 1099511628211ull;
        }
        return h;
    }

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
            , eval_memo_fingerprints_(std::move(other.eval_memo_fingerprints_))
            , eval_memo_boards_(std::move(other.eval_memo_boards_))
            , eval_memo_evaluations_(std::move(other.eval_memo_evaluations_))
            , eval_memo_next_(std::move(other.eval_memo_next_))
            , eval_memo_heads_(other.eval_memo_heads_)
            , candidate_buffer_(std::move(other.candidate_buffer_))
            , child_buffer_(std::move(other.child_buffer_))
            , expanded_count_(other.expanded_count_)
            , expanded_max_(other.expanded_max_)
            , width_cache_(other.width_cache_)
            , transposition_(std::move(other.transposition_))
            , transposition_rehash_(std::move(other.transposition_rehash_))
            , idmap_(std::move(other.idmap_))
            , cache_(std::move(other.cache_))
            , transposition_used_(other.transposition_used_)
            , transposition_epoch_(other.transposition_epoch_)
            , max_length_(other.max_length_)
            , width_(other.width_)
            , search_complete_(other.search_complete_)
            , search_stopped_(other.search_stopped_)
            , transposition_exhausted_(other.transposition_exhausted_)
            , search_stats_(other.search_stats_)
            , timers_(other.timers_)
            , path_stats_(other.path_stats_)
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

        std::size_t transposition_used() const;

        std::uint32_t transposition_epoch_for_test() const;

        void set_transposition_epoch_for_test(std::uint32_t epoch);

        std::size_t transposition_physical_entries_for_test() const;

        TranspositionKey build_key_for_test(Child const &child) const;

        TranspositionKey key_from_node_for_test(NodeId id) const;

        bool transposition_reinsert_for_test(
            std::uint64_t fp, TranspositionKey const &key, NodeId node);

        std::size_t transposition_table_size_for_test() const;

        TranspositionEntry transposition_entry_for_test(std::size_t slot) const;

        void set_transposition_entry_for_test(
            std::size_t slot, TranspositionEntry entry);

        std::size_t arena_reserved_bytes() const;

        std::size_t idmap_reserved_bytes() const;

        std::uint64_t retained_bytes() const;

        bool arena_exhausted() const;

        bool run(std::size_t max_passes);

        bool run(SearchBudget budget);

        bool search_complete() const;

        std::size_t frontier_count() const;

        SearchStats search_stats() const;

        ComponentTimers component_timers() const;

        std::optional<SearchSelection> select_best() const;

        FinalResult finalize(Placement active_start);

        PathTelemetry path_telemetry() const;

    private:
        struct TranspositionProbe
        {
            bool merged = false;
            NodeId node = no_node;
            TranspositionEntry *slot = nullptr;
            std::uint64_t fp = 0;
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
        std::vector<std::uint64_t> eval_memo_fingerprints_;
        std::vector<Board> eval_memo_boards_;
        std::vector<Evaluation> eval_memo_evaluations_;
        std::vector<std::uint16_t> eval_memo_next_;
        std::array<std::uint16_t, eval_memo_hash_buckets> eval_memo_heads_{};
        std::vector<Candidate> candidate_buffer_;
        std::vector<Child> child_buffer_;

        std::array<std::size_t, max_frontiers> expanded_count_{};
        std::array<NodeId, max_frontiers> expanded_max_{};
        std::array<double, max_frontiers> width_cache_{};
        std::vector<TranspositionEntry> transposition_;
        std::vector<TranspositionEntry> transposition_rehash_;
        std::vector<NodeId> idmap_;
        EvalCache cache_;
        std::size_t transposition_used_ = 0;
        std::uint32_t transposition_epoch_ = 0;
        std::size_t max_length_ = 0;
        std::size_t width_ = 0;
        bool search_complete_ = false;
        bool search_stopped_ = false;
        bool transposition_exhausted_ = false;
        SearchStats search_stats_{};
        ComponentTimers timers_{};
        PathTelemetry path_stats_{};

        void reset_run_state();

        void advance_transposition_epoch();

        void refresh_pending_occupancy();

        bool reuse_matches(NodeId child, Board const &board, PolicyState const &policy,
            Queue const &queue, HoldState hold, std::size_t new_max) const;

        NodeId reroot(NodeId target, Queue const &queue, HoldState hold, std::size_t new_max);

        void rebuild_child_links();

        NodeId append_child_link(NodeId parent, NodeId child);

        NodeId append_fresh_child_link(NodeId parent, NodeId child, NodeId tail);

        std::int64_t now_nanos() const
        {
            return config_.clock_nanos ? config_.clock_nanos() : steady_clock_nanos();
        }

        std::int64_t timer_now() const
        {
            return config_.timer_nanos ? config_.timer_nanos() : steady_clock_nanos();
        }

        bool telemetry_on() const
        {
            return config_.telemetry_enabled;
        }

        bool timers_on() const
        {
            return config_.telemetry_enabled && config_.timers_enabled;
        }

        bool expand_source(NodeId parent_id, Node const &parent, Piece played,
            BranchSource source, HoldState hold, std::size_t cursor,
            std::span<Piece const> policy_next, std::vector<Child> &out);

        template <auto B>
            requires reachability::block_spec<decltype(B)>
        bool expand_source_for_block(NodeId parent_id, Node const &parent, Piece played,
            BranchSource source, HoldState hold, std::size_t cursor,
            std::span<Piece const> policy_next, std::vector<Child> &out);

        Evaluation evaluate_once(Board const &board);

#ifdef TETRIS_EVAL_REUSE_TRACE
        Evaluation evaluate_once_for_parent(Board const &board, NodeId parent,
            std::size_t depth, BranchSource source);
#endif

        bool expand_parent(NodeId parent_id);

        void clear_eval_memo();

        MaterializeOutcome search_materialize(Child const &child);

        MaterializeOutcome search_materialize_prehashed(Child const &child,
            TranspositionKey const &key, std::uint64_t fp);

        MaterializeOutcome search_materialize_inner(Child const &child,
            TranspositionKey const &key, std::uint64_t fp);

        void promote(std::size_t level);

        TranspositionProbe transposition_probe(TranspositionKey const &key);

        TranspositionProbe transposition_probe_prehashed(
            std::uint64_t fp, TranspositionKey const &expect);

        // docs/phase7/transposition_capacity_design.md §2: false on merge (the
        // first staged entry wins), true after occupying the probed slot.
        bool transposition_reinsert(
            std::uint64_t fp, TranspositionKey const &key, NodeId node);

        TranspositionKey build_key(Child const &child, NodeId id) const;

        TranspositionKey key_from_node(NodeId id) const;

        void run_pass();
    };
}
