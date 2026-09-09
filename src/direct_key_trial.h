#pragma once
#if defined(TETRIS_DIRECT_KEY_TRIAL) && defined(TETRIS_CHILD_SOA_TRIAL)
#error TETRIS_DIRECT_KEY_TRIAL excludes TETRIS_CHILD_SOA_TRIAL
#endif
#if defined(TETRIS_DIRECT_KEY_TRIAL) && defined(TETRIS_EVAL_INDEX_TRIAL)
#error TETRIS_DIRECT_KEY_TRIAL excludes TETRIS_EVAL_INDEX_TRIAL
#endif
#if defined(TETRIS_DIRECT_KEY_TRIAL) && defined(TETRIS_EVAL_REUSE_TRACE)
#error TETRIS_DIRECT_KEY_TRIAL excludes TETRIS_EVAL_REUSE_TRACE
#endif
#include <array>
#include <cstddef>
#include <cstdint>

    struct DirectKeySourceContext
    {
        std::uint16_t depth = 0;
        std::uint16_t cursor = 0;
        std::uint16_t boundary_count = 0;
        std::uint32_t fixed_root_child = no_node;
        std::uint8_t active_piece = no_piece_code;
        std::uint8_t hold_piece = no_piece_code;
        bool root_fixed = false;
        bool hold_available = false;
        std::array<std::uint64_t, 4> boundary_bits{};

        bool operator==(DirectKeySourceContext const &) const = default;
    };

    static_assert(sizeof(DirectKeySourceContext) <= 64,
        "the source context stays within one cache line");

    inline DirectKeySourceContext direct_key_begin_source(Node const &parent,
        HoldState hold, std::size_t cursor, Queue const &queue)
    {
        DirectKeySourceContext context;
        context.depth = static_cast<std::uint16_t>(parent.depth + 1);
        context.root_fixed = parent.depth != 0;
        context.fixed_root_child = context.root_fixed ? parent.root_child : no_node;
        context.cursor = static_cast<std::uint16_t>(cursor);
        std::size_t const size = queue.pieces.size();
        std::size_t const remaining = cursor < size ? size - cursor : 0;
        context.boundary_count = static_cast<std::uint16_t>(remaining);
        for (std::size_t i = 0; i < remaining; ++i)
        {
            if (queue.boundary[cursor + i])
            {
                context.boundary_bits[i / 64] |= (1ull << (i % 64));
            }
        }
        context.active_piece = cursor < size
            ? static_cast<std::uint8_t>(queue.pieces[cursor])
            : no_piece_code;
        context.hold_piece = hold.piece.has_value()
            ? static_cast<std::uint8_t>(*hold.piece)
            : no_piece_code;
        context.hold_available = !hold.locked;
        return context;
    }

    inline std::uint64_t direct_key_hash(DirectKeySourceContext const &context,
        Board const &board, PolicyState const &state, Piece played,
        Candidate const &candidate, BranchSource source)
    {
        std::uint32_t const root_child = context.root_fixed
            ? context.fixed_root_child
            : first_move_fingerprint(played, candidate, source);
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
        mix(&context.depth, sizeof context.depth);
        mix(&context.cursor, sizeof context.cursor);
        mix(&context.boundary_count, sizeof context.boundary_count);
        mix(&root_child, sizeof root_child);
        for (int i = 0; i < Board::occupancy_t::word_count(); ++i)
        {
            std::uint64_t word = board.occupancy().logical_word(i);
            mix(&word, sizeof word);
        }
        mix(&state.death, sizeof state.death);
        mix(&state.combo, sizeof state.combo);
        mix(&state.under_attack, sizeof state.under_attack);
        mix(&state.map_rise, sizeof state.map_rise);
        mix(&state.b2b, sizeof state.b2b);
        mix(&state.t2_value, sizeof state.t2_value);
        mix(&state.t3_value, sizeof state.t3_value);
        mix_double(normalize_zero(state.acc_value));
        mix_double(normalize_zero(state.like));
        mix_double(normalize_zero(state.value));
        mix(context.boundary_bits.data(), context.boundary_bits.size() * sizeof(std::uint64_t));
        mix(&context.active_piece, sizeof context.active_piece);
        mix(&context.hold_piece, sizeof context.hold_piece);
        mix(&context.hold_available, sizeof context.hold_available);
        return h;
    }

    inline bool direct_key_node_matches(Node const &node, Node const &parent_node,
        DirectKeySourceContext const &context, Queue const &queue, Board const &board,
        PolicyState const &state, Piece played, Candidate const &candidate,
        BranchSource source)
    {
        std::uint16_t const node_depth = static_cast<std::uint16_t>(node.depth);
        std::uint16_t const node_cursor = static_cast<std::uint16_t>(node.cursor);
        std::uint32_t const node_root_child = parent_node.depth == 0
            ? first_move_fingerprint(node.played, node.incoming, node.source)
            : parent_node.root_child;
        std::uint32_t const staged_root_child = context.root_fixed
            ? context.fixed_root_child
            : first_move_fingerprint(played, candidate, source);
        if (node_depth != context.depth || node_cursor != context.cursor
            || node_root_child != staged_root_child)
        {
            return false;
        }
        std::size_t const size = queue.pieces.size();
        std::size_t const remaining = node.cursor < size ? size - node.cursor : 0;
        if (remaining != context.boundary_count)
        {
            return false;
        }
        for (int i = 0; i < Board::occupancy_t::word_count(); ++i)
        {
            if (node.board.occupancy().logical_word(i)
                != board.occupancy().logical_word(i))
            {
                return false;
            }
        }
        if (node.policy.death != state.death || node.policy.combo != state.combo
            || node.policy.under_attack != state.under_attack
            || node.policy.map_rise != state.map_rise || node.policy.b2b != state.b2b
            || node.policy.t2_value != state.t2_value
            || node.policy.t3_value != state.t3_value)
        {
            return false;
        }
        if (normalize_zero(node.policy.acc_value) != normalize_zero(state.acc_value)
            || normalize_zero(node.policy.like) != normalize_zero(state.like)
            || normalize_zero(node.policy.value) != normalize_zero(state.value))
        {
            return false;
        }
        for (std::size_t i = 0; i < remaining; ++i)
        {
            bool const node_bit = queue.boundary[node.cursor + i];
            bool const staged_bit
                = (context.boundary_bits[i / 64] & (1ull << (i % 64))) != 0;
            if (node_bit != staged_bit)
            {
                return false;
            }
        }
        std::uint8_t const node_active = node.cursor < size
            ? static_cast<std::uint8_t>(queue.pieces[node.cursor])
            : no_piece_code;
        if (node_active != context.active_piece)
        {
            return false;
        }
        std::uint8_t const node_hold_piece = node.hold.piece.has_value()
            ? static_cast<std::uint8_t>(*node.hold.piece)
            : no_piece_code;
        if (node_hold_piece != context.hold_piece
            || node.hold.locked == context.hold_available)
        {
            return false;
        }
        return true;
}
