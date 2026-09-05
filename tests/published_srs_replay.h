#pragma once

#include "tetris_core.h"
#include "rule_toj.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace published_replay
{
    using Cells = std::array<std::pair<int, int>, 4>;

    Cells sorted_cells(Cells cells)
    {
        std::sort(cells.begin(), cells.end());
        return cells;
    }

    struct Kick
    {
        int from;
        int to;
        std::array<std::pair<int, int>, 5> offsets;
        int count;
    };

    inline constexpr std::array<Kick, 12> common_kicks = {{
        {0, 1, {{{0, 0}, {-1, 0}, {-1, 1}, {0, -2}, {-1, -2}}}, 5},
        {0, 3, {{{0, 0}, {1, 0}, {1, 1}, {0, -2}, {1, -2}}}, 5},
        {0, 2, {{{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, 1},
        {1, 0, {{{0, 0}, {1, 0}, {1, -1}, {0, 2}, {1, 2}}}, 5},
        {1, 2, {{{0, 0}, {1, 0}, {1, -1}, {0, 2}, {1, 2}}}, 5},
        {1, 3, {{{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, 1},
        {2, 1, {{{0, 0}, {-1, 0}, {-1, 1}, {0, -2}, {-1, -2}}}, 5},
        {2, 3, {{{0, 0}, {1, 0}, {1, 1}, {0, -2}, {1, -2}}}, 5},
        {2, 0, {{{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, 1},
        {3, 2, {{{0, 0}, {-1, 0}, {-1, -1}, {0, 2}, {-1, 2}}}, 5},
        {3, 0, {{{0, 0}, {-1, 0}, {-1, -1}, {0, 2}, {-1, 2}}}, 5},
        {3, 1, {{{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, 1},
    }};

    inline constexpr std::array<Kick, 12> i_kicks = {{
        {0, 1, {{{0, 0}, {-2, 0}, {1, 0}, {-2, -1}, {1, 2}}}, 5},
        {0, 3, {{{0, 0}, {-1, 0}, {2, 0}, {-1, 2}, {2, -1}}}, 5},
        {0, 2, {{{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, 1},
        {1, 0, {{{0, 0}, {2, 0}, {-1, 0}, {2, 1}, {-1, -2}}}, 5},
        {1, 2, {{{0, 0}, {-1, 0}, {2, 0}, {-1, 2}, {2, -1}}}, 5},
        {1, 3, {{{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, 1},
        {2, 1, {{{0, 0}, {1, 0}, {-2, 0}, {1, -2}, {-2, 1}}}, 5},
        {2, 3, {{{0, 0}, {2, 0}, {-1, 0}, {2, 1}, {-1, -2}}}, 5},
        {2, 0, {{{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, 1},
        {3, 2, {{{0, 0}, {-2, 0}, {1, 0}, {-2, -1}, {1, 2}}}, 5},
        {3, 0, {{{0, 0}, {1, 0}, {-2, 0}, {1, -2}, {-2, 1}}}, 5},
        {3, 1, {{{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}}, 1},
    }};

    struct Frame
    {
        Cells pattern{};
        int ax = 0;
        int ay = 0;
        bool valid = false;
    };

    struct Model
    {
        std::array<std::array<Frame, 4>, 7> frames{};
    };

    inline int piece_index(char piece)
    {
        switch (piece)
        {
        case 'T': return 0;
        case 'Z': return 1;
        case 'S': return 2;
        case 'J': return 3;
        case 'L': return 4;
        case 'O': return 5;
        case 'I': return 6;
        default: return -1;
        }
    }

    template <typename Engine>
    Model measure_frames(Engine &engine)
    {
        Model model;
        for (char piece : std::string("TZSJLOI"))
        {
            for (int r = 0; r < 4; ++r)
            {
                if (engine.context()->get_opertion(piece, static_cast<unsigned char>(r)).create == nullptr)
                {
                    continue;
                }
                m_tetris::TetrisNode node;
                if (!engine.context()->create(m_tetris::TetrisBlockStatus(piece, 0, 39,
                        static_cast<uint8_t>(r)), node))
                {
                    continue;
                }
                Cells cells{};
                int index = 0;
                int min_x = 10;
                int min_y = 40;
                for (int ry = 0; ry < node.height; ++ry)
                {
                    for (int rx = 0; rx < node.width; ++rx)
                    {
                        if ((node.data[ry] >> (node.col + rx)) & 1)
                        {
                            cells[index++] = {node.col + rx, node.row + ry};
                            min_x = std::min(min_x, node.col + rx);
                            min_y = std::min(min_y, node.row + ry);
                        }
                    }
                }
                Frame frame;
                for (int k = 0; k < 4; ++k)
                {
                    frame.pattern[k] = {cells[k].first - min_x, cells[k].second - min_y};
                }
                frame.pattern = sorted_cells(frame.pattern);
                frame.ax = min_x;
                frame.ay = min_y - 39;
                frame.valid = true;
                model.frames[piece_index(piece)][r] = frame;
            }
        }
        return model;
    }

    struct ReplayResult
    {
        Cells cells{};
        int rotation = 0;
        int arrival = 0;
        bool valid = false;
    };

    inline bool cells_fit(Cells const &cells, std::array<std::uint16_t, 48> const &rows)
    {
        for (auto const &cell : cells)
        {
            if (cell.first < 0 || cell.first >= 10 || cell.second < 0 || cell.second >= 48)
            {
                return false;
            }
            if ((rows[static_cast<std::size_t>(cell.second)] >> cell.first) & 1)
            {
                return false;
            }
        }
        return true;
    }

    inline Cells cells_at(Model const &model, char piece, int rotation, int x, int y)
    {
        Frame const &frame = model.frames[piece_index(piece)][rotation];
        Cells cells{};
        for (int k = 0; k < 4; ++k)
        {
            cells[k] = {frame.pattern[k].first + x + frame.ax,
                frame.pattern[k].second + y + frame.ay};
        }
        return sorted_cells(cells);
    }

    inline ReplayResult replay(Model const &model, std::array<std::uint16_t, 48> const &rows,
        char piece, int start_x, int start_y, int start_r, std::string_view commands, bool lock,
        bool allow_180)
    {
        auto const &kicks = piece == 'I' ? i_kicks : common_kicks;
        int x = start_x;
        int y = start_y;
        int r = start_r;
        if (piece_index(piece) < 0 || !model.frames[piece_index(piece)][r].valid
            || !cells_fit(cells_at(model, piece, r, x, y), rows))
        {
            return {};
        }
        int arrival = 0;
        for (char command : commands)
        {
            if (command == 'l' || command == 'r' || command == 'd')
            {
                int const nx = x + (command == 'l' ? -1 : (command == 'r' ? 1 : 0));
                int const ny = y + (command == 'd' ? -1 : 0);
                if (!cells_fit(cells_at(model, piece, r, nx, ny), rows))
                {
                    return {};
                }
                x = nx;
                y = ny;
                arrival = 0;
            }
            else if (command == 'z' || command == 'c' || command == 'x')
            {
                if (command == 'x' && !allow_180)
                {
                    return {};
                }
                int const to = command == 'c' ? (r + 1) % 4 : (command == 'z' ? (r + 3) % 4 : (r + 2) % 4);
                bool rotated = false;
                for (auto const &rule : kicks)
                {
                    if (rule.from != r || rule.to != to)
                    {
                        continue;
                    }
                    for (int i = 0; i < rule.count; ++i)
                    {
                        int const nx = x + rule.offsets[i].first;
                        int const ny = y + rule.offsets[i].second;
                        if (!model.frames[piece_index(piece)][to].valid)
                        {
                            break;
                        }
                        if (!cells_fit(cells_at(model, piece, to, nx, ny), rows))
                        {
                            continue;
                        }
                        x = nx;
                        y = ny;
                        r = to;
                        arrival = 1;
                        rotated = true;
                        break;
                    }
                    break;
                }
                if (!rotated)
                {
                    return {};
                }
            }
            else if (command == 'D')
            {
                while (cells_fit(cells_at(model, piece, r, x, y - 1), rows))
                {
                    --y;
                }
                arrival = 0;
            }
            else
            {
                return {};
            }
        }
        if (lock)
        {
            int const pre_lock_y = y;
            while (cells_fit(cells_at(model, piece, r, x, y - 1), rows))
            {
                --y;
            }
            if (y != pre_lock_y)
            {
                arrival = 0;
            }
        }
        return {cells_at(model, piece, r, x, y), r, arrival, true};
    }
}
