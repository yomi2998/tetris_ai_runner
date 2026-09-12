#include "tetris_core.h"
#include "rule_toj.h"
#include "ai_zzz.h"
#include "search_tspin.h"

#include <bit>
#include <cstdint>
#include <cstdio>
#include <format>
#include <print>
#include <random>
#include <string>
#include <vector>

namespace
{
    using Engine = m_tetris::TetrisEngine<rule_toj::TetrisRule, ai_zzz::TOJ, search_tspin::Search>;
    using Map = m_tetris::TetrisMap;
    using Node = m_tetris::TetrisNode;

    size_t popcount_total(Map const &map)
    {
        size_t total = 0;
        for (int y = 0; y < map.height; ++y)
        {
            total += static_cast<size_t>(std::popcount(map.row[y]));
        }
        return total;
    }

    void normalize(Map &map)
    {
        map.count = static_cast<int>(popcount_total(map));
        map.roof = 0;
        for (int x = 0; x < map.width; ++x)
        {
            map.top[x] = 0;
            for (int y = map.height - 1; y >= 0; --y)
            {
                if (map.full(x, y))
                {
                    map.top[x] = y + 1;
                    break;
                }
            }
            map.roof = std::max(map.roof, map.top[x]);
        }
    }

    Map reference_attach(m_tetris::TetrisContext const *context, Map map, Node const *node)
    {
        int roof = map.roof;
        int32_t stale_top[32];
        std::copy(map.top, map.top + 32, stale_top);
        for (int i = 0; i < node->height; ++i)
        {
            map.row[node->row + i] |= node->data[i];
        }
        int clear = 0;
        std::vector<uint32_t> kept;
        kept.reserve(map.height);
        for (int y = 0; y < map.height; ++y)
        {
            bool in_span = y >= node->row && y < node->row + node->height;
            if (in_span && map.row[y] == context->full())
            {
                ++clear;
            }
            else
            {
                kept.push_back(map.row[y]);
            }
        }
        int y = 0;
        for (auto row : kept)
        {
            map.row[y++] = row;
        }
        for (; y < map.height; ++y)
        {
            map.row[y] = 0;
        }
        normalize(map);
        for (int i = 0; i < node->width; ++i)
        {
            if (node->top[i] > stale_top[node->col + i])
            {
                roof = std::max(roof, node->top[i]);
            }
        }
        map.roof = roof - clear;
        return map;
    }

    bool check_case(char const *name, m_tetris::TetrisContext const *context, Map const &input, Node const *node)
    {
        Map actual = input;
        Map expected = reference_attach(context, input, node);
        size_t clear = node->attach(context, actual);
        (void)clear;
        bool ok = true;
        for (int y = 0; y < input.height; ++y)
        {
            if (actual.row[y] != expected.row[y])
            {
                std::println(stderr, "{}: row {} mismatch actual {:08x} expected {:08x}", name, y, actual.row[y], expected.row[y]);
                ok = false;
            }
        }
        for (int x = 0; x < input.width; ++x)
        {
            if (actual.top[x] != expected.top[x])
            {
                std::println(stderr, "{}: top[{}] mismatch actual {} expected {}", name, x, actual.top[x], expected.top[x]);
                ok = false;
            }
        }
        if (actual.roof != expected.roof)
        {
            std::println(stderr, "{}: roof mismatch actual {} expected {}", name, actual.roof, expected.roof);
            ok = false;
        }
        if (actual.count != expected.count)
        {
            std::println(stderr, "{}: count mismatch actual {} expected {}", name, actual.count, expected.count);
            ok = false;
        }
        if (popcount_total(actual) != static_cast<size_t>(actual.count))
        {
            std::println(stderr, "{}: count invariant broken {} vs popcount {}", name, actual.count, popcount_total(actual));
            ok = false;
        }
        if (!ok)
        {
            std::println(stderr, "{}: FAILED", name);
            std::println(stderr, "node: row {} height {} col {} width {} top {} {} {} {} data {:x} {:x} {:x} {:x}",
                node->row, node->height, node->col, node->width,
                node->top[0], node->top[1], node->top[2], node->top[3],
                node->data[0], node->data[1], node->data[2], node->data[3]);
            for (int yy = 0; yy < input.height; ++yy)
            {
                if (input.row[yy] != 0)
                {
                    std::println(stderr, "in row {:2d} {:08x} actual {:08x} expected {:08x}", yy, input.row[yy], actual.row[yy], expected.row[yy]);
                }
            }
            std::println(stderr, "in: roof {} count {} | actual: roof {} count {} | expected: roof {} count {}",
                input.roof, input.count, actual.roof, actual.count, expected.roof, expected.count);
        }
        return ok;
    }

    uint32_t complement_piece_row(m_tetris::TetrisContext const *context, Node const *node, int i)
    {
        return context->full() & ~node->data[i];
    }
}

int main()
{
    Engine engine;
    if (!engine.prepare(10, 40))
    {
        std::println(stderr, "engine.prepare failed");
        return 2;
    }
    m_tetris::TetrisContext const *context = engine.context().get();
    bool ok = true;

    {
        Node const *node = context->get('O', 4, 39, 0);
        Map map(10, 40);
        map.row[38] = complement_piece_row(context, node, 0);
        map.row[39] = complement_piece_row(context, node, 1);
        normalize(map);
        ok &= check_case("O completes rows 38-39 near top bound", context, map, node);
    }

    {
        Node const *node = context->get('I', 3, 39, 1);
        Map map(10, 40);
        for (int i = 0; i < 4; ++i)
        {
            map.row[node->row + i] = complement_piece_row(context, node, i);
        }
        normalize(map);
        ok &= check_case("vertical I completes rows at top bound", context, map, node);
    }

    {
        Node const *node = context->get('O', 4, 1, 0);
        Map map(10, 40);
        map.row[0] = complement_piece_row(context, node, 0);
        map.row[1] = complement_piece_row(context, node, 1);
        for (int y = 2; y < 6; ++y)
        {
            map.row[y] = 0b0000000001;
        }
        normalize(map);
        ok &= check_case("O completes floor rows 0-1", context, map, node);
    }

    {
        Node const *node = context->get('T', 4, 20, 0);
        Map map(10, 40);
        map.row[node->row] = complement_piece_row(context, node, 0);
        map.row[node->row + 1] = complement_piece_row(context, node, 1);
        for (int y = node->row + 2; y < node->row + 7; ++y)
        {
            map.row[y] = 0b1111100111;
        }
        normalize(map);
        ok &= check_case("T completes mid-board rows", context, map, node);
    }

    {
        Node const *node = context->get('O', 0, 39, 0);
        Map map(10, 40);
        map.row[38] = complement_piece_row(context, node, 0);
        map.row[39] = complement_piece_row(context, node, 1);
        map.top[0] = 0;
        normalize(map);
        ok &= check_case("O completes at left wall", context, map, node);
    }

    {
        std::mt19937 rng(424242);
        char const *pieces = "IJLOSTZ";
        int tested = 0;
        for (int trial = 0; trial < 200000 && tested < 20000; ++trial)
        {
            Map map(10, 40);
            int fill_height = 5 + static_cast<int>(rng() % 30);
            for (int y = 0; y < fill_height; ++y)
            {
                map.row[y] = rng() % 0x3FF;
            }
            normalize(map);
            char piece = pieces[rng() % 7];
            int x = static_cast<int>(rng() % 10);
            int y = 2 + static_cast<int>(rng() % 37);
            int r = static_cast<int>(rng() % 4);
            Node const *node = context->get(piece, static_cast<int8_t>(x), static_cast<int8_t>(y), static_cast<uint8_t>(r));
            if (node == nullptr || !node->check(map) || node->row < 0 || node->row + node->height > map.height)
            {
                continue;
            }
            ++tested;
            std::string name = std::format("random {} trial {}", piece, trial);
            ok &= check_case(name.c_str(), context, map, node);
            if (!ok)
            {
                break;
            }
        }
        std::println("random cases tested: {}", tested);
        ok &= tested >= 10000;
    }

    if (!ok)
    {
        std::println(stderr, "attach_test: FAILURES");
        return 1;
    }
    std::println("attach_test: all cases passed");
    return 0;
}
