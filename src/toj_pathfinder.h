#pragma once

#include "tetris_board.h"
#include "tetris_types.h"
#include "toj_rule.h"

#include "fast-reachability/block.hpp"
#include "fast-reachability/kick_srs.hpp"
#include "fast-reachability/piece_tetromino.hpp"
#include "fast-reachability/search.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace tetris::path
{
    using Board = tetris::Board;
    using Piece = tetris::Piece;
    using Placement = tetris::Placement;
    using Candidate = tetris::Candidate;
    using ArrivalClass = tetris::ArrivalClass;

    struct PathConfig
    {
        bool allow_180 = true;
    };

    struct Path
    {
        static constexpr std::size_t capacity = 1024;
        static constexpr std::size_t max_payload = capacity - 3;
        std::array<char, capacity> data{};
        std::size_t size = 0;
        bool valid = false;

        std::string_view view() const
        {
            return {data.data(), size};
        }
    };

    struct Pathfinder
    {
        static constexpr int rotations = 4;
        static constexpr int states = rotations * Board::width * Board::height;
        static constexpr std::uint16_t none = 0xffff;

        std::array<std::uint8_t, states> seen{};
        std::array<std::uint16_t, states * 2> parent{};
        std::array<char, states * 2> parent_command{};
        std::array<std::uint16_t, states * 2> queue{};
        std::array<std::uint16_t, states> drop_to{};
        std::size_t queue_tail = 0;
        int orientations = rotations;
        bool piece_is_t = false;

        Pathfinder() = default;

        Pathfinder(Board const &board, Piece piece, Placement start, PathConfig config)
        {
            *this = reachability::call_with_block<toj::SRS>(piece, [&]<reachability::block B>() {
                return build<B>(board, start, config);
            });
        }

        Path find(Candidate candidate) const
        {
            Path out;
            int const target_rotation = candidate.placement.rotation();
            int const target_x = candidate.placement.x();
            int const target_y = candidate.placement.y();
            if (target_rotation < 0 || target_rotation >= orientations || target_x < 0
                || target_x >= Board::width || target_y < 0 || target_y >= Board::height)
            {
                return out;
            }
            if (candidate.arrival == ArrivalClass::TerminalRotation && !piece_is_t)
            {
                return out;
            }
            if (candidate.arrival == ArrivalClass::TerminalRotation)
            {
                std::size_t const slot = slot_index(target_rotation, target_x, target_y);
                if ((seen[slot] & 2) == 0 || drop_to[slot] != pack(target_x, target_y))
                {
                    return out;
                }
                return reconstruct(full_index(target_rotation, target_x, target_y, 1));
            }
            Path normal = find_normal_goal(target_rotation, target_x, target_y, false);
            if (normal.valid)
            {
                return normal;
            }
            if (piece_is_t)
            {
                return out;
            }
            return find_normal_goal(target_rotation, target_x, target_y, true);
        }

        bool normal_path_exists(Candidate candidate) const
        {
            int const target_rotation = candidate.placement.rotation();
            int const target_x = candidate.placement.x();
            int const target_y = candidate.placement.y();
            if (target_rotation < 0 || target_rotation >= orientations || target_x < 0
                || target_x >= Board::width || target_y < 0 || target_y >= Board::height)
            {
                return false;
            }
            return find_normal_goal(target_rotation, target_x, target_y, false).valid;
        }

    private:
        Path find_normal_goal(int target_rotation, int target_x, int target_y,
            bool terminal_channel_ok) const
        {
            Path out;
            std::uint16_t const wanted = pack(target_x, target_y);
            for (std::size_t order = 0; order < queue_tail; ++order)
            {
                std::uint16_t const state = queue[order];
                int const channel = static_cast<int>(state & 1);
                if (channel != 0 && !terminal_channel_ok)
                {
                    continue;
                }
                std::size_t const slot = state >> 1;
                int const rotation = static_cast<int>(slot / (Board::width * Board::height));
                if (rotation != target_rotation || drop_to[slot] != wanted)
                {
                    continue;
                }
                return reconstruct(state);
            }
            return out;
        }

        static constexpr std::size_t slot_index(int rotation, int x, int y)
        {
            return (static_cast<std::size_t>(rotation) * Board::width + static_cast<std::size_t>(x))
                    * Board::height
                + static_cast<std::size_t>(y);
        }

        static constexpr std::size_t full_index(int rotation, int x, int y, int channel)
        {
            return slot_index(rotation, x, y) * 2 + static_cast<std::size_t>(channel);
        }

        static constexpr std::uint16_t pack(int x, int y)
        {
            return static_cast<std::uint16_t>((x << 6) | y);
        }

        Path reconstruct(std::size_t goal) const
        {
            Path out;
            std::size_t cursor = goal;
            while (parent[cursor] != none)
            {
                if (out.size >= Path::capacity)
                {
                    return Path{};
                }
                out.data[out.size++] = parent_command[cursor];
                cursor = parent[cursor];
            }
            if (out.size > Path::max_payload)
            {
                return Path{};
            }
            for (std::size_t i = 0; i < out.size / 2; ++i)
            {
                char const swap = out.data[i];
                out.data[i] = out.data[out.size - 1 - i];
                out.data[out.size - 1 - i] = swap;
            }
            out.valid = true;
            return out;
        }

        template <auto B>
            requires reachability::block_spec<decltype(B)>
        static Pathfinder build(Board const &board, Placement start, PathConfig config)
        {
            Pathfinder out;
            out.orientations = B.orientations;
            out.piece_is_t = B.piece_identity == reachability::piece_id("T");
            reachability::search::search_workspace<B, Board::occupancy_t> ws(board.occupancy());
            auto checker = ws.checker();
            std::size_t head = 0;
            auto push = [&](int rotation, int x, int y, int channel, char command, std::size_t from) {
                if (rotation < 0 || rotation >= B.orientations || x < 0 || x >= Board::width || y < 0
                    || y >= Board::height)
                {
                    return;
                }
                if (!checker.is_valid(rotation, x, y))
                {
                    return;
                }
                std::size_t const slot = slot_index(rotation, x, y);
                std::uint8_t const bit = static_cast<std::uint8_t>(1 << channel);
                if ((out.seen[slot] & bit) != 0)
                {
                    return;
                }
                out.seen[slot] |= bit;
                int rest = y;
                while (checker.is_valid(rotation, x, rest - 1))
                {
                    --rest;
                }
                out.drop_to[slot] = pack(x, rest);
                std::size_t const to = full_index(rotation, x, y, channel);
                out.parent[to] = static_cast<std::uint16_t>(from);
                out.parent_command[to] = command;
                out.queue[out.queue_tail++] = static_cast<std::uint16_t>(to);
            };
            push(start.rotation(), start.x(), start.y(), 0, 0, none);
            while (head < out.queue_tail)
            {
                std::size_t const state = out.queue[head++];
                std::size_t const slot = state >> 1;
                int const y = static_cast<int>(slot % Board::height);
                int const x = static_cast<int>((slot / Board::height) % Board::width);
                int const rotation = static_cast<int>(slot / (Board::width * Board::height));
                push(rotation, x - 1, y, 0, 'l', state);
                push(rotation, x + 1, y, 0, 'r', state);
                push(rotation, x, y - 1, 0, 'd', state);
                int const clockwise = (rotation + 1) % 4;
                int const counter = (rotation + 3) % 4;
                auto kicked = checker.try_rotate(rotation, clockwise, x, y);
                if (kicked.rot == clockwise)
                {
                    push(clockwise, kicked.x, kicked.y, 1, 'c', state);
                }
                kicked = checker.try_rotate(rotation, counter, x, y);
                if (kicked.rot == counter)
                {
                    push(counter, kicked.x, kicked.y, 1, 'z', state);
                }
                if (config.allow_180)
                {
                    int const opposite = (rotation + 2) % 4;
                    kicked = checker.try_rotate(rotation, opposite, x, y);
                    if (kicked.rot == opposite)
                    {
                        push(opposite, kicked.x, kicked.y, 1, 'x', state);
                    }
                }
                int rest = y;
                while (checker.is_valid(rotation, x, rest - 1))
                {
                    --rest;
                }
                if (rest != y)
                {
                    push(rotation, x, rest, 0, 'D', state);
                }
            }
            return out;
        }
    };

    struct ReplayResult
    {
        Placement placement = Placement::unchecked(0, 0, 0);
        ArrivalClass arrival = ArrivalClass::Normal;
        bool valid = false;
    };

    inline ReplayResult replay_path(Board const &board, Piece piece, Placement start,
        std::string_view commands, PathConfig config, bool lock)
    {
        if (start.rotation() < 0 || start.rotation() >= toj::orientation_count(piece))
        {
            return {};
        }
        return reachability::call_with_block<toj::SRS>(piece, [&]<reachability::block B>() {
            reachability::search::search_workspace<B, Board::occupancy_t> ws(board.occupancy());
            auto checker = ws.checker();
            int rotation = start.rotation();
            int x = start.x();
            int y = start.y();
            if (!checker.is_valid(rotation, x, y))
            {
                return ReplayResult{};
            }
            ArrivalClass arrival = ArrivalClass::Normal;
            for (char command : commands)
            {
                if (command == 'l')
                {
                    if (!checker.is_valid(rotation, x - 1, y))
                    {
                        return ReplayResult{};
                    }
                    --x;
                    arrival = ArrivalClass::Normal;
                }
                else if (command == 'r')
                {
                    if (!checker.is_valid(rotation, x + 1, y))
                    {
                        return ReplayResult{};
                    }
                    ++x;
                    arrival = ArrivalClass::Normal;
                }
                else if (command == 'd')
                {
                    if (!checker.is_valid(rotation, x, y - 1))
                    {
                        return ReplayResult{};
                    }
                    --y;
                    arrival = ArrivalClass::Normal;
                }
                else if (command == 'z' || command == 'c' || command == 'x')
                {
                    if (command == 'x' && !config.allow_180)
                    {
                        return ReplayResult{};
                    }
                    int const to = command == 'c' ? (rotation + 1) % 4
                        : (command == 'z' ? (rotation + 3) % 4 : (rotation + 2) % 4);
                    auto result = checker.try_rotate(rotation, to, x, y);
                    if (result.rot != to)
                    {
                        return ReplayResult{};
                    }
                    rotation = result.rot;
                    x = result.x;
                    y = result.y;
                    arrival = ArrivalClass::TerminalRotation;
                }
                else if (command == 'D')
                {
                    while (checker.is_valid(rotation, x, y - 1))
                    {
                        --y;
                    }
                    arrival = ArrivalClass::Normal;
                }
                else if (command == 'L')
                {
                    while (checker.is_valid(rotation, x - 1, y))
                    {
                        --x;
                    }
                    arrival = ArrivalClass::Normal;
                }
                else if (command == 'R')
                {
                    while (checker.is_valid(rotation, x + 1, y))
                    {
                        ++x;
                    }
                    arrival = ArrivalClass::Normal;
                }
                else
                {
                    return ReplayResult{};
                }
            }
            if (lock)
            {
                int const pre_lock_y = y;
                while (checker.is_valid(rotation, x, y - 1))
                {
                    --y;
                }
                if (y != pre_lock_y)
                {
                    arrival = ArrivalClass::Normal;
                }
            }
            auto placement = Placement::try_make(x, y, rotation);
            if (!placement)
            {
                return ReplayResult{};
            }
            return ReplayResult{*placement, arrival, true};
        });
    }
}
