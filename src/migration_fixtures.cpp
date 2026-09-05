#include "tetris_core.h"
#include "rule_toj.h"
#include "ai_zzz.h"
#include "search_tspin.h"
#include "random.h"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <print>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    constexpr char const *pieces = "IOSZLJT";
    constexpr size_t board_width = 10;
    constexpr size_t board_height = 40;

    ai_zzz::TOJ::Param frozen_param()
    {
        double theta[ai_zzz::TOJ::NUM_PARAMS];
        ai_zzz::TOJ::production_default_theta(theta);
        ai_zzz::TOJ::Param param;
        ai_zzz::TOJ::theta_to_param(theta, param);
        return param;
    }

    int const combo_table[] = { 0, 0, 0, 1, 1, 2, 2, 3, 3, 4 };
    int const combo_table_max = 10;

    using Engine = m_tetris::TetrisEngine<rule_toj::TetrisRule, ai_zzz::TOJ, search_tspin::Search>;

    Engine make_engine()
    {
        Engine engine;
        if (!engine.prepare(board_width, board_height))
        {
            std::println(stderr, "engine.prepare failed");
            std::exit(1);
        }
        engine.search_config()->allow_rotate_move = false;
        engine.search_config()->allow_180 = true;
        engine.search_config()->allow_d = true;
        engine.search_config()->allow_nont_d = false;
        engine.search_config()->is_20g = false;
        engine.search_config()->last_rotate = false;
        engine.ai_config()->table = combo_table;
        engine.ai_config()->table_max = combo_table_max;
        engine.ai_config()->param = frozen_param();
        return engine;
    }

    void rebuild_metadata(m_tetris::TetrisMap &map)
    {
        map.roof = 0;
        map.count = 0;
        std::memset(map.top, 0, sizeof map.top);
        for (size_t y = 0; y < board_height; ++y)
        {
            for (size_t x = 0; x < board_width; ++x)
            {
                if (map.full(x, y))
                {
                    map.top[x] = static_cast<int32_t>(y + 1);
                    map.roof = std::max<int32_t>(map.roof, static_cast<int32_t>(y + 1));
                    ++map.count;
                }
            }
        }
    }

    m_tetris::TetrisMap seeded_board(uint32_t seed, size_t board_id)
    {
        std::mt19937 rng(seed * 1000003u + static_cast<uint32_t>(board_id) * 7919u);
        m_tetris::TetrisMap map(board_width, board_height);
        size_t const roof = 4 + rng() % 17;
        for (size_t y = 0; y < roof; ++y)
        {
            map.row[y] = static_cast<uint32_t>(rng()) & 0x3ff;
        }
        rebuild_metadata(map);
        return map;
    }

    std::string rows_text(m_tetris::TetrisMap const &map)
    {
        std::string out;
        char buf[16];
        for (size_t y = 0; y < board_height; ++y)
        {
            std::snprintf(buf, sizeof buf, "%x,", map.row[y]);
            out += buf;
        }
        return out;
    }

    std::string cells_text(m_tetris::TetrisNode const *node)
    {
        std::string out;
        for (size_t ry = 0; ry < node->height; ++ry)
        {
            for (size_t rx = 0; rx < node->width; ++rx)
            {
                if ((node->data[ry] >> (node->col + rx)) & 1)
                {
                    out += std::to_string(node->col + rx) + "," + std::to_string(node->row + static_cast<int>(ry)) + ";";
                }
            }
        }
        return out;
    }

    FILE *open_out(std::string const &dir, std::string const &name)
    {
        std::string path = dir + "/" + name;
        FILE *file = std::fopen(path.c_str(), "wb");
        if (file == nullptr)
        {
            std::println(stderr, "cannot open {}", path);
            std::exit(1);
        }
        return file;
    }

    void write_all(std::string const &dir, std::string const &name, std::string const &body)
    {
        FILE *file = open_out(dir, name);
        std::fwrite(body.data(), 1, body.size(), file);
        std::fclose(file);
    }

    void generate_geometry(std::string const &dir)
    {
        Engine engine = make_engine();
        std::string out = "# t x y r cells\n";
        int const ys[] = { 0, 10, 20, 30, 39 };
        for (char const *p = pieces; *p; ++p)
        {
            for (int r = 0; r < 4; ++r)
            {
                for (int x = 0; x < 10; ++x)
                {
                    for (int y : ys)
                    {
                        if (engine.context()->get_opertion(*p, static_cast<unsigned char>(r)).create == nullptr)
                        {
                            out += std::string(1, *p) + " " + std::to_string(x) + " " + std::to_string(y) + " " + std::to_string(r) + " invalid\n";
                            continue;
                        }
                        m_tetris::TetrisNode node;
                        if (!engine.context()->create(m_tetris::TetrisBlockStatus(*p, static_cast<int8_t>(x), static_cast<int8_t>(y), static_cast<uint8_t>(r)), node))
                        {
                            out += std::string(1, *p) + " " + std::to_string(x) + " " + std::to_string(y) + " " + std::to_string(r) + " invalid\n";
                            continue;
                        }
                        out += std::string(1, *p) + " " + std::to_string(x) + " " + std::to_string(y) + " " + std::to_string(r) + " " + cells_text(&node) + "\n";
                    }
                }
            }
        }
        write_all(dir, "geometry.csv", out);
    }

    void generate_reach(std::string const &dir, size_t boards, uint32_t seed)
    {
        Engine engine = make_engine();
        search_tspin::Search search;
        search.init(engine.context().get(), engine.search_config());
        std::string out = "# board t x y r type flags is_check is_last_rotate is_ready is_mini_ready\n";
        for (size_t b = 0; b < boards; ++b)
        {
            m_tetris::TetrisMap map = seeded_board(seed, b);
            for (char const *p = pieces; *p; ++p)
            {
                m_tetris::TetrisNode const *node = engine.context()->generate(*p);
                auto const *results = search.search(map, node, 1);
                for (auto const &land : *results)
                {
                    out += std::to_string(b) + " " + std::string(1, land.node->status.t)
                        + " " + std::to_string(land.node->status.x)
                        + " " + std::to_string(land.node->status.y)
                        + " " + std::to_string(land.node->status.r)
                        + " " + std::to_string(static_cast<int>(land.type))
                        + " " + std::to_string(land.flags)
                        + " " + std::to_string(land.is_check)
                        + " " + std::to_string(land.is_last_rotate)
                        + " " + std::to_string(land.is_ready)
                        + " " + std::to_string(land.is_mini_ready) + "\n";
                }
            }
        }
        write_all(dir, "reach.csv", out);
    }

    void generate_tspin(std::string const &dir, size_t boards, uint32_t seed)
    {
        Engine engine = make_engine();
        search_tspin::Search search;
        search.init(engine.context().get(), engine.search_config());
        std::string out = "# board x y r search_type last_rotate clear classify\n";
        for (size_t b = 0; b < boards; ++b)
        {
            m_tetris::TetrisMap map = seeded_board(seed, b);
            m_tetris::TetrisNode const *node = engine.context()->generate('T');
            auto const *results = search.search(map, node, 1);
            size_t dumped = 0;
            for (auto const &land : *results)
            {
                if (dumped >= 40) break;
                for (int last_rotate = 0; last_rotate < 2; ++last_rotate)
                {
                    for (size_t clear = 0; clear < 2; ++clear)
                    {
                        auto cls = search.classify(map, land.node, last_rotate != 0, clear);
                        out += std::to_string(b)
                            + " " + std::to_string(land.node->status.x)
                            + " " + std::to_string(land.node->status.y)
                            + " " + std::to_string(land.node->status.r)
                            + " " + std::to_string(static_cast<int>(land.type))
                            + " " + std::to_string(last_rotate)
                            + " " + std::to_string(clear)
                            + " " + std::to_string(static_cast<int>(cls)) + "\n";
                    }
                }
                ++dumped;
            }
        }
        write_all(dir, "tspin.csv", out);
    }

    void generate_policy(std::string const &dir, size_t boards, uint32_t seed)
    {
        Engine engine = make_engine();
        ai_zzz::TOJ &ai = *engine.ai();
        search_tspin::Search search;
        search.init(engine.context().get(), engine.search_config());
        std::string out = "# kind board t x y r search_type combo b2b under_attack clear value t2_value t3_value death out_combo out_b2b out_under_attack out_map_rise out_acc_value out_like out_value\n";
        for (size_t b = 0; b < boards; ++b)
        {
            m_tetris::TetrisMap src_map = seeded_board(seed, b);
            for (char const *p = pieces; *p; ++p)
            {
                int safe = ai.get_safe(src_map, *p);
                int16_t t2_init = 0, t3_init = 0;
                ai_zzz::TOJ::Status::init_t_value(src_map, t2_init, t3_init);
                out += std::string("safe") + " " + std::to_string(b) + " " + *p + " 0 0 0 0 0 0 0 0 0 "
                    + std::to_string(safe) + " " + std::to_string(t2_init) + " " + std::to_string(t3_init)
                    + " 0 0 0 0 0 0 0\n";
            }
            size_t index = 0;
            for (char const *p = pieces; *p; ++p)
            {
                int16_t t2_init = 0, t3_init = 0;
                ai_zzz::TOJ::Status::init_t_value(src_map, t2_init, t3_init);
                m_tetris::TetrisNode const *node = engine.context()->generate(*p);
                auto const *results = search.search(src_map, node, 1);
                size_t taken = 0;
                for (auto const &land : *results)
                {
                    if (taken >= 6) break;
                    m_tetris::TetrisMap map = src_map;
                    size_t clear = land.node->attach(engine.context().get(), map);
                    search_tspin::Search::TetrisNodeWithTSpinType ex(land.node);
                    ex.last = land.last;
                    ex.type = land.type;
                    ex.flags = land.flags;
                    ai_zzz::TOJ::Status status;
                    status.death = 0;
                    status.combo = static_cast<int8_t>(index % 5);
                    status.under_attack = static_cast<int8_t>(b % 3);
                    status.map_rise = 0;
                    status.b2b = static_cast<int8_t>((index / 5) % 2);
                    status.t2_value = t2_init;
                    status.t3_value = t3_init;
                    status.acc_value = 0;
                    status.like = 0;
                    status.value = 0;
                    char const next_buffer[] = "IOSZLJT";
                    m_tetris::TetrisContext::Env env{ next_buffer, 7, *p, ' ', false };
                    auto result = ai.eval(ex, map, src_map);
                    ai_zzz::TOJ::Status transition = ai.get(ex, result, clear, map, 0, status, env);
                    char buf[32];
                    std::snprintf(buf, sizeof buf, "%.9f", result.value);
                    out += std::string("eval") + " " + std::to_string(b) + " " + *p
                        + " " + std::to_string(land.node->status.x)
                        + " " + std::to_string(land.node->status.y)
                        + " " + std::to_string(land.node->status.r)
                        + " " + std::to_string(static_cast<int>(land.type))
                        + " " + std::to_string(status.combo)
                        + " " + std::to_string(status.b2b)
                        + " " + std::to_string(status.under_attack)
                        + " " + std::to_string(clear)
                        + " " + buf
                        + " " + std::to_string(result.t2_value)
                        + " " + std::to_string(result.t3_value)
                        + " 0 0 0 0 0 0 0 0\n";
                    out += std::string("get") + " " + std::to_string(b) + " " + *p
                        + " " + std::to_string(land.node->status.x)
                        + " " + std::to_string(land.node->status.y)
                        + " " + std::to_string(land.node->status.r)
                        + " " + std::to_string(static_cast<int>(land.type))
                        + " " + std::to_string(status.combo)
                        + " " + std::to_string(status.b2b)
                        + " " + std::to_string(status.under_attack)
                        + " " + std::to_string(clear)
                        + " " + buf
                        + " " + std::to_string(transition.t2_value)
                        + " " + std::to_string(transition.t3_value)
                        + " " + std::to_string(transition.death)
                        + " " + std::to_string(transition.combo)
                        + " " + std::to_string(transition.b2b)
                        + " " + std::to_string(transition.under_attack)
                        + " " + std::to_string(transition.map_rise)
                        + " " + std::to_string(transition.acc_value)
                        + " " + std::to_string(transition.like)
                        + " " + std::to_string(transition.value) + "\n";
                    ++taken;
                    ++index;
                }
            }
        }
        write_all(dir, "policy.csv", out);
    }

    std::string double_hex(double value)
    {
        char buf[17];
        std::snprintf(buf, sizeof buf, "%016llx",
            static_cast<unsigned long long>(std::bit_cast<std::uint64_t>(value)));
        return buf;
    }

    std::string param_hex(ai_zzz::TOJ::Param const &param)
    {
        double theta[ai_zzz::TOJ::NUM_PARAMS];
        ai_zzz::TOJ::theta_from_param(param, theta);
        std::string out;
        for (size_t i = 0; i < ai_zzz::TOJ::NUM_PARAMS; ++i)
        {
            if (i != 0)
            {
                out += " ";
            }
            out += double_hex(theta[i]);
        }
        return out;
    }

    m_tetris::TetrisMap tall_board()
    {
        m_tetris::TetrisMap map(board_width, board_height);
        for (size_t y = 0; y < 20; ++y)
        {
            map.row[y] = static_cast<uint32_t>(0x3ff & ~(1u << (y % board_width)));
        }
        rebuild_metadata(map);
        return map;
    }

    m_tetris::TetrisMap bank_board(std::vector<std::pair<size_t, uint32_t>> const &rows)
    {
        m_tetris::TetrisMap map(board_width, board_height);
        for (auto const &[y, bits] : rows)
        {
            map.row[y] = bits;
        }
        rebuild_metadata(map);
        return map;
    }

    void generate_policy_v2(std::string const &dir, size_t boards, uint32_t seed)
    {
        Engine engine = make_engine();
        ai_zzz::TOJ &ai = *engine.ai();
        search_tspin::Search search;
        search.init(engine.context().get(), engine.search_config());
        std::string out = "# toj_policy_v2 fixture, one case per line\n";
        out += "# generator: migration_fixtures policy_v2 <dir> <count> <seed>\n";
        out += "# param: " + param_hex(engine.ai_config()->param) + "\n";
        out += "# combo: 0,0,0,1,1,2,2,3,3,4\n";
        out += "# columns: id board piece x y r arrival spin_in spin_eff is_check is_last_rotate is_ready is_mini_ready clear src_rows result_rows p_death p_combo p_under p_maprise p_b2b p_t2 p_t3 p_acc p_like p_value next hold node is_hold depth eval_value eval_t2 eval_t3 o_death o_combo o_b2b o_under o_maprise o_t2 o_t3 o_acc o_like o_value safe\n";
        char const *next_options[] = { "IOSZLJT", "T", "IOSZLI", "", "STLI", "IOT" };
        size_t const next_lengths[] = { 7, 1, 6, 0, 4, 3 };
        char const hold_options[] = { ' ', 'T', 'I', 'O' };
        std::vector<std::pair<std::string, m_tetris::TetrisMap>> tagged;
        for (size_t b = 0; b < boards; ++b)
        {
            tagged.push_back({ "s" + std::to_string(b), seeded_board(seed, b) });
        }
        tagged.push_back({ "empty", m_tetris::TetrisMap(board_width, board_height) });
        tagged.push_back({ "tall", tall_board() });
        tagged.push_back({ "o1", bank_board({ { 0, 0x3ff & ~0x30 } }) });
        tagged.push_back({ "o2", bank_board({ { 0, 0x3ff & ~0x30 }, { 1, 0x3ff & ~0x30 } }) });
        tagged.push_back({ "t1", bank_board({ { 0, 0x3ff & ~0x70 } }) });
        tagged.push_back({ "iwell3",
            bank_board({ { 0, 0x3ff & ~0x10 }, { 1, 0x3ff & ~0x10 }, { 2, 0x3ff & ~0x10 } }) });
        tagged.push_back({ "iwell4",
            bank_board({ { 0, 0x3ff & ~0x10 }, { 1, 0x3ff & ~0x10 }, { 2, 0x3ff & ~0x10 }, { 3, 0x3ff & ~0x10 } }) });
        tagged.push_back({ "slotS",
            bank_board({ { 0, 0x3ff & ~0x10 }, { 1, 0x3ff & ~0x30 }, { 2, 0x3ff & ~0x10 }, { 3, 0x3ff & ~0x70 }, { 4, (0x3ff & ~0x60) | 0x10 } }) });
        tagged.push_back({ "slotM",
            bank_board({ { 0, 0x3ff & ~0x10 }, { 1, 0x3ff & ~0x18 }, { 2, 0x3ff & ~0x10 }, { 3, 0x3ff & ~0x1c }, { 4, (0x3ff & ~0x0c) | 0x10 } }) });
        tagged.push_back({ "mini0",
            bank_board({ { 0, 0b1101110111 }, { 1, 0b1100110111 }, { 2, 0b1111111111 }, { 3, 0b1111111011 }, { 4, 0b1110100011 }, { 5, 0b0111110011 } }) });
        tagged.push_back({ "mini1",
            bank_board({ { 0, 0b1111011111 }, { 1, 0b1101011111 }, { 2, 0b1110001111 }, { 3, 0b1111110111 }, { 4, 0b1111000101 }, { 5, 0b1101100111 } }) });
        tagged.push_back({ "mini2",
            bank_board({ { 0, 0b1111101011 }, { 1, 0b1110101111 }, { 2, 0b1110101111 }, { 3, 0b1101011111 }, { 4, 0b1111100011 }, { 5, 0b1111010011 } }) });
        tagged.push_back({ "double0",
            bank_board({ { 0, 0b1110011111 }, { 1, 0b1111111111 }, { 2, 0b0100011001 }, { 3, 0b1110111111 }, { 4, 0b1100011111 }, { 5, 0b1100111111 } }) });
        size_t id = 0;
        for (auto const &[tag, src_map] : tagged)
        {
            for (char const *p = pieces; *p; ++p)
            {
                int16_t t2_init = 0, t3_init = 0;
                ai_zzz::TOJ::Status::init_t_value(src_map, t2_init, t3_init);
                int safe = ai.get_safe(src_map, *p);
                m_tetris::TetrisNode const *node = engine.context()->generate(*p);
                auto const *results = search.search(src_map, node, 1);
                for (auto const &land : *results)
                {
                    m_tetris::TetrisMap map = src_map;
                    size_t clear = land.node->attach(engine.context().get(), map);
                    search_tspin::Search::TetrisNodeWithTSpinType ex(land.node);
                    ex.last = land.last;
                    ex.type = land.type;
                    ex.flags = land.flags;
                    ai_zzz::TOJ::Status status;
                    status.death = 0;
                    status.combo = static_cast<int8_t>(id % 5);
                    status.under_attack = static_cast<int8_t>((id / 2) % 3);
                    status.map_rise = static_cast<int8_t>((id / 6) % 2);
                    status.b2b = static_cast<int8_t>((id / 5) % 2);
                    status.t2_value = t2_init;
                    status.t3_value = t3_init;
                    status.acc_value = (id % 11 == 0) ? 12345.678 : 0.0;
                    status.like = (id % 13 == 0) ? 987.654 : 0.0;
                    status.value = 0.0;
                    size_t env_pick = id % 6;
                    std::string next_text = next_options[env_pick];
                    char hold = hold_options[id % 4];
                    m_tetris::TetrisContext::Env env{ next_text.c_str(), next_lengths[env_pick],
                        *p, hold, ((id / 4) % 2) != 0 };
                    auto result = ai.eval(ex, map, src_map);
                    ai_zzz::TOJ::Status transition = ai.get(ex, result, clear, map, id % 3, status, env);
                    out += std::to_string(id) + " " + tag + " " + *p
                        + " " + std::to_string(land.node->status.x)
                        + " " + std::to_string(land.node->status.y)
                        + " " + std::to_string(land.node->status.r)
                        + " " + std::to_string(land.is_last_rotate ? 1 : 0)
                        + " " + std::to_string(static_cast<int>(land.type))
                        + " " + std::to_string(static_cast<int>(ex.type))
                        + " " + std::to_string(land.is_check ? 1 : 0)
                        + " " + std::to_string(land.is_last_rotate ? 1 : 0)
                        + " " + std::to_string(land.is_ready ? 1 : 0)
                        + " " + std::to_string(land.is_mini_ready ? 1 : 0)
                        + " " + std::to_string(clear)
                        + " " + rows_text(src_map) + " " + rows_text(map)
                        + " " + std::to_string(status.death)
                        + " " + std::to_string(status.combo)
                        + " " + std::to_string(status.under_attack)
                        + " " + std::to_string(status.map_rise)
                        + " " + std::to_string(status.b2b)
                        + " " + std::to_string(status.t2_value)
                        + " " + std::to_string(status.t3_value)
                        + " " + double_hex(status.acc_value)
                        + " " + double_hex(status.like)
                        + " " + double_hex(status.value)
                        + " " + (next_text.empty() ? std::string("-") : next_text)
                        + " " + (hold == ' ' ? std::string("-") : std::string(1, hold))
                        + " " + *p
                        + " " + std::to_string(env.is_hold ? 1 : 0)
                        + " " + std::to_string(id % 3)
                        + " " + double_hex(result.value)
                        + " " + std::to_string(result.t2_value)
                        + " " + std::to_string(result.t3_value)
                        + " " + std::to_string(transition.death)
                        + " " + std::to_string(transition.combo)
                        + " " + std::to_string(transition.b2b)
                        + " " + std::to_string(transition.under_attack)
                        + " " + std::to_string(transition.map_rise)
                        + " " + std::to_string(transition.t2_value)
                        + " " + std::to_string(transition.t3_value)
                        + " " + double_hex(transition.acc_value)
                        + " " + double_hex(transition.like)
                        + " " + double_hex(transition.value)
                        + " " + std::to_string(safe) + "\n";
                    ++id;
                }
            }
        }
        write_all(dir, "toj_policy_v2.csv", out);
    }

    void generate_selfplay(std::string const &dir, size_t games, uint32_t seed)
    {
        Engine engine = make_engine();
        std::string out = "# game move t hold_before change_hold x y r type clear dead combo b2b next_hold rows\n";
        for (size_t g = 0; g < games; ++g)
        {
            std::mt19937 rng(seed * 31337u + static_cast<uint32_t>(g));
            m_tetris::TetrisMap map(board_width, board_height);
            std::vector<char> next;
            char hold = ' ';
            int combo = 0;
            int b2b = 0;
            size_t move = 0;
            bool dead = false;
            while (!dead && move < 200)
            {
                if (!next.empty()) next.erase(next.begin());
                while (next.size() <= 6)
                {
                    for (size_t i = 0; i < engine.context()->type_max(); ++i)
                    {
                        next.push_back(engine.context()->convert(i));
                    }
                    std::shuffle(next.end() - engine.context()->type_max(), next.end(), rng);
                }
                engine.ai_config()->safe = engine.ai()->get_safe(map, next.front());
                engine.status()->death = 0;
                engine.status()->combo = combo;
                engine.status()->under_attack = 0;
                engine.status()->map_rise = 0;
                engine.status()->b2b = !!b2b;
                engine.status()->acc_value = 0;
                engine.status()->like = 0;
                engine.status()->value = 0;
                ai_zzz::TOJ::Status::init_t_value(map, engine.status()->t2_value, engine.status()->t3_value);
                char current = next.front();
                auto result = engine.run_hold(map, engine.context()->generate(current), hold, true, next.data() + 1, 6, m_tetris::SearchBudget::by_iterations(200));
                char hold_before = hold;
                bool change_hold = result.change_hold;
                bool move_dead = result.target == nullptr || result.target->row >= 20;
                size_t clear = 0;
                if (!move_dead)
                {
                    if (result.change_hold)
                    {
                        if (hold == ' ') next.erase(next.begin());
                        hold = current;
                    }
                    clear = result.target->attach(engine.context().get(), map);
                    out += std::to_string(g) + " " + std::to_string(move)
                        + " " + std::string(1, current)
                        + " " + std::string(1, hold_before)
                        + " " + std::to_string(change_hold)
                        + " " + std::to_string(result.target->status.x)
                        + " " + std::to_string(result.target->status.y)
                        + " " + std::to_string(result.target->status.r)
                        + " " + std::to_string(static_cast<int>(result.target.type))
                        + " " + std::to_string(clear)
                        + " 0"
                        + " " + std::to_string(combo)
                        + " " + std::to_string(b2b)
                        + " " + std::string(1, hold)
                        + " " + rows_text(map) + "\n";
                    if (clear > 0) combo = combo + 1;
                    else combo = 0;
                    if (clear > 0) b2b = result.target.type != ai_zzz::TOJ::TSpinType::None || clear == 4;
                }
                else
                {
                    out += std::to_string(g) + " " + std::to_string(move)
                        + " " + std::string(1, current)
                        + " " + std::string(1, hold_before)
                        + " 0 0 0 0 0 0 1"
                        + " " + std::to_string(combo)
                        + " " + std::to_string(b2b)
                        + " " + std::string(1, hold)
                        + " " + rows_text(map) + "\n";
                    dead = true;
                }
                ++move;
            }
        }
        write_all(dir, "selfplay.csv", out);
    }

    void generate_garbage(std::string const &dir, size_t cases, uint32_t seed)
    {
        std::string out = "# case lines hole in_rows out_rows roof count\n";
        for (size_t c = 0; c < cases; ++c)
        {
            std::mt19937 rng(seed * 6151u + static_cast<uint32_t>(c) * 31u);
            m_tetris::TetrisMap map = seeded_board(seed, 1000 + c);
            int line = 1 + static_cast<int>(rng() % 4);
            uint32_t hole = 0x3ff & ~(1u << (rng() % board_width));
            std::string in_rows;
            for (size_t y = 0; y < board_height; ++y)
            {
                char buf[16];
                std::snprintf(buf, sizeof buf, "%x,", map.row[y]);
                in_rows += buf;
            }
            for (int y = board_height - 1; y >= line; --y)
            {
                map.row[y] = map.row[y - line];
            }
            for (int y = 0; y < line; ++y)
            {
                map.row[y] = hole;
            }
            rebuild_metadata(map);
            std::string out_rows = rows_text(map);
            out += std::to_string(c) + " " + std::to_string(line) + " " + std::to_string(hole)
                + " " + in_rows + " " + out_rows
                + " " + std::to_string(map.roof) + " " + std::to_string(map.count) + "\n";
        }
        write_all(dir, "garbage.csv", out);
    }

    void generate_queue(std::string const &dir)
    {
        Engine engine = make_engine();
        using TreeNode = m_tetris::TetrisTreeNode<ai_zzz::TOJ::Status, ai_zzz::TOJ, search_tspin::Search>;
        m_tetris::TetrisNode const *node = engine.context()->generate('T');
        char const *cases[] = {
            "IOSZLT",
            "?IOSZLT",
            "IO?SZLT",
            "IO??SZLT",
            "IOSZLT?",
            "IOSZLT??",
            "??????",
            "IOSZ",
        };
        std::string out = "# input sequence\n";
        for (char const *input : cases)
        {
            auto next = TreeNode::process_next(input, std::strlen(input), node);
            std::string seq;
            for (auto const &n : next)
            {
                seq += std::string(1, static_cast<char>(n)) + ",";
            }
            out += std::string(input) + " " + seq + "\n";
        }
        write_all(dir, "queue.csv", out);
    }
}

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        std::println(stderr, "usage: migration_fixtures <geometry|reach|tspin|policy|policy_v2|selfplay|garbage|queue> <outdir> [count] [seed]");
        return 1;
    }
    std::string mode = argv[1];
    std::string dir = argv[2];
    size_t count = argc > 3 ? std::strtoull(argv[3], nullptr, 10) : 24;
    uint32_t seed = argc > 4 ? static_cast<uint32_t>(std::strtoul(argv[4], nullptr, 10)) : 1;
    if (mode == "geometry") generate_geometry(dir);
    else if (mode == "reach") generate_reach(dir, count, seed);
    else if (mode == "tspin") generate_tspin(dir, count, seed);
    else if (mode == "policy") generate_policy(dir, count, seed);
    else if (mode == "policy_v2") generate_policy_v2(dir, count, seed);
    else if (mode == "selfplay") generate_selfplay(dir, count, seed);
    else if (mode == "garbage") generate_garbage(dir, count, seed);
    else if (mode == "queue") generate_queue(dir);
    else
    {
        std::println(stderr, "unknown mode: {}", mode);
        return 1;
    }
    return 0;
}
