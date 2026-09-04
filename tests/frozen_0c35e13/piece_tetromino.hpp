#pragma once
#include "block.hpp"

// Tetromino piece identifiers and I/O

namespace reachability {
    enum class block_type {
        T,
        Z,
        S,
        J,
        L,
        O,
        I
    };

} // namespace reachability

// Tetromino shape definitions and piece set

namespace reachability::rules {
    // Pure shape holder (no offsets, no kicks)
    template <Wrap<minos_p> minos_t>
    struct pure_block {
        minos_t minos;
    };

    // Raw cell data for the 7 standard tetrominoes
    inline constexpr pure_block T = {tuple{
        tuple{coord{-1, 0}, coord{0, 0}, coord{1, 0}, coord{0, 1}},  // 0
        tuple{coord{0, 1}, coord{0, 0}, coord{0, -1}, coord{1, 0}},  // R
        tuple{coord{1, 0}, coord{0, 0}, coord{-1, 0}, coord{0, -1}}, // 2
        tuple{coord{0, -1}, coord{0, 0}, coord{0, 1}, coord{-1, 0}}  // L
    }};
    inline constexpr pure_block Z = {tuple{
        tuple{coord{-1, 1}, coord{0, 1}, coord{0, 0}, coord{1, 0}},  // 0
        tuple{coord{-1, -1}, coord{-1, 0}, coord{0, 0}, coord{0, 1}} // L
    }};
    inline constexpr pure_block S = {tuple{
        tuple{coord{1, 1}, coord{0, 1}, coord{0, 0}, coord{-1, 0}},  // 0
        tuple{coord{-1, 1}, coord{-1, 0}, coord{0, 0}, coord{0, -1}} // L
    }};
    inline constexpr pure_block J = {tuple{
        tuple{coord{-1, 1}, coord{-1, 0}, coord{0, 0}, coord{1, 0}}, // 0
        tuple{coord{1, 1}, coord{0, 1}, coord{0, 0}, coord{0, -1}},  // R
        tuple{coord{1, -1}, coord{1, 0}, coord{0, 0}, coord{-1, 0}}, // 2
        tuple{coord{-1, -1}, coord{0, -1}, coord{0, 0}, coord{0, 1}} // L
    }};
    inline constexpr pure_block L = {tuple{
        tuple{coord{-1, 0}, coord{0, 0}, coord{1, 0}, coord{1, 1}},   // 0
        tuple{coord{0, 1}, coord{0, 0}, coord{0, -1}, coord{1, -1}},  // R
        tuple{coord{1, 0}, coord{0, 0}, coord{-1, 0}, coord{-1, -1}}, // 2
        tuple{coord{0, -1}, coord{0, 0}, coord{0, 1}, coord{-1, 1}}   // L
    }};
    inline constexpr pure_block O = {reachability::make_tuple(
        tuple{coord{0, 0}, coord{1, 0}, coord{0, 1}, coord{1, 1}})};
    inline constexpr pure_block I = {tuple{
        tuple{coord{-1, 0}, coord{0, 0}, coord{1, 0}, coord{2, 0}}, // 0
        tuple{coord{0, 0}, coord{0, 1}, coord{0, 2}, coord{0, 3}},  // L
    }};

    // Tetromino piece set — shapes + SRS offset table for the 7 tetrominoes
    struct Tetromino {
        using piece_type = block_type;

        static constexpr auto piece_list = tuple{
            block_type::T, block_type::Z, block_type::S,
            block_type::J, block_type::L, block_type::O, block_type::I};

        static constexpr auto all = tuple{
            make_piece_def<piece_id("T")>(T.minos, identity_offsets<4>()),
            make_piece_def<piece_id("Z")>(Z.minos, tuple{
                                                       tuple{0, coord{0, 0}},
                                                       tuple{1, coord{1, 0}},
                                                       tuple{0, coord{0, -1}},
                                                       tuple{1, coord{0, 0}},
                                                   }),
            make_piece_def<piece_id("S")>(S.minos, tuple{
                                                       tuple{0, coord{0, 0}},
                                                       tuple{1, coord{1, 0}},
                                                       tuple{0, coord{0, -1}},
                                                       tuple{1, coord{0, 0}},
                                                   }),
            make_piece_def<piece_id("J")>(J.minos, identity_offsets<4>()),
            make_piece_def<piece_id("L")>(L.minos, identity_offsets<4>()),
            make_piece_def<piece_id("O")>(O.minos, reachability::make_tuple(tuple{0, coord{0, 0}})),
            make_piece_def<piece_id("I")>(I.minos, tuple{
                                                       tuple{0, coord{0, 0}},
                                                       tuple{1, coord{1, -2}},
                                                       tuple{0, coord{0, -1}},
                                                       tuple{1, coord{0, -2}},
                                                   }),
        };

        static constexpr char name_of(piece_type p) {
            using enum block_type;
            switch (p) {
                case T:
                    return 'T';
                case Z:
                    return 'Z';
                case S:
                    return 'S';
                case J:
                    return 'J';
                case L:
                    return 'L';
                case O:
                    return 'O';
                case I:
                    return 'I';
                default:
                    return '-';
            }
        }

        static constexpr piece_type from_name(char c) {
            switch (c) {
                case 'T':
                    return block_type::T;
                case 'Z':
                    return block_type::Z;
                case 'S':
                    return block_type::S;
                case 'J':
                    return block_type::J;
                case 'L':
                    return block_type::L;
                case 'O':
                    return block_type::O;
                case 'I':
                    return block_type::I;
                default:
                    std::unreachable();
            }
        }
    };
} // namespace reachability::rules
