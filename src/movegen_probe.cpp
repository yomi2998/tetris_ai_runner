#include "tetris_core.h"
#include "rule_toj.h"
#include "ai_zzz.h"
#include "search_tspin.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <print>
#include <random>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

namespace
{
    using Engine = m_tetris::TetrisEngine<rule_toj::TetrisRule, ai_zzz::TOJ, search_tspin::Search>;
    using Map = m_tetris::TetrisMap;

    struct Options
    {
        size_t moves = 60;
        size_t iters = 1000;
        uint32_t seed = 1;
        size_t maxdepth = 6;
        size_t synthetic = 32;
        size_t tall = 0;
        size_t synthetic_seed = 12345;
        std::string corpus_out;
        std::string corpus_in;
        std::string out;
        std::vector<size_t> depths = { 0 };
        bool allow_180 = true;
        bool allow_d = true;
        bool is_20g = false;
        bool allow_nont_d = false;
        size_t sample = 4;
    };

    struct CorpusMap
    {
        Map map;
        char source = '?';
        size_t move = 0;
    };

    struct Landing
    {
        size_t filtered = 0;
        char t = ' ';
        int x = 0;
        int y = 0;
        unsigned r = 0;
        int type = 0;
        unsigned flags = 0;
        size_t last = 0;

        bool operator<(Landing const &other) const
        {
            return std::tie(filtered, t, x, y, r, type, flags, last) <
                   std::tie(other.filtered, other.t, other.x, other.y, other.r, other.type, other.flags, other.last);
        }
    };

    Options parse_args(int argc, char **argv)
    {
        Options opt;
        for (int i = 1; i < argc; ++i)
        {
            std::string a = argv[i];
            auto next = [&](std::string const &name) -> std::string
            {
                if (i + 1 >= argc)
                {
                    std::println(stderr, "missing value for {}", name);
                    std::exit(1);
                }
                return argv[++i];
            };
            if (a == "--moves") opt.moves = std::strtoull(next(a).c_str(), nullptr, 10);
            else if (a == "--iters") opt.iters = std::strtoull(next(a).c_str(), nullptr, 10);
            else if (a == "--seed") opt.seed = static_cast<uint32_t>(std::strtoul(next(a).c_str(), nullptr, 10));
            else if (a == "--maxdepth") opt.maxdepth = std::strtoull(next(a).c_str(), nullptr, 10);
            else if (a == "--synthetic") opt.synthetic = std::strtoull(next(a).c_str(), nullptr, 10);
            else if (a == "--tall") opt.tall = std::strtoull(next(a).c_str(), nullptr, 10);
            else if (a == "--depth")
            {
                opt.depths.clear();
                std::istringstream stream(next(a));
                std::string item;
                while (std::getline(stream, item, ','))
                {
                    opt.depths.push_back(std::strtoull(item.c_str(), nullptr, 10));
                }
            }
            else if (a == "--corpus-out") opt.corpus_out = next(a);
            else if (a == "--corpus-in") opt.corpus_in = next(a);
            else if (a == "--out") opt.out = next(a);
            else if (a == "--no-180") opt.allow_180 = false;
            else if (a == "--no-d") opt.allow_d = false;
            else if (a == "--20g") opt.is_20g = true;
            else if (a == "--nont-d") opt.allow_nont_d = true;
            else if (a == "--sample") opt.sample = std::max<size_t>(1, std::strtoull(next(a).c_str(), nullptr, 10));
            else
            {
                std::println(stderr, "unknown option: {}", a);
                std::exit(1);
            }
        }
        return opt;
    }

    void write_map(FILE *file, size_t id, CorpusMap const &entry)
    {
        Map const &map = entry.map;
        std::print(file, "map {} {} {} {} {} {} {} rows", id, map.width, map.height, map.roof, map.count,
            entry.source, entry.move);
        for (int i = 0; i < map.height; ++i)
        {
            std::print(file, " {:08x}", map.row[i]);
        }
        std::print(file, " top");
        for (int i = 0; i < 32; ++i)
        {
            std::print(file, " {}", map.top[i]);
        }
        std::print(file, "\n");
    }

    std::vector<CorpusMap> read_corpus(std::string const &path)
    {
        std::vector<CorpusMap> corpus;
        FILE *file = std::fopen(path.c_str(), "rb");
        if (file == nullptr)
        {
            std::println(stderr, "cannot open corpus: {}", path);
            std::exit(1);
        }
        char line[4096];
        while (std::fgets(line, sizeof line, file) != nullptr)
        {
            std::istringstream stream(line);
            std::string tag;
            size_t id = 0;
            CorpusMap entry;
            stream >> tag >> id >> entry.map.width >> entry.map.height >> entry.map.roof >> entry.map.count;
            stream >> entry.source >> entry.move;
            std::string rows;
            stream >> rows;
            if (tag != "map" || rows != "rows")
            {
                continue;
            }
            std::memset(entry.map.row, 0, sizeof entry.map.row);
            for (int i = 0; i < entry.map.height; ++i)
            {
                std::string word;
                stream >> word;
                entry.map.row[i] = static_cast<uint32_t>(std::strtoul(word.c_str(), nullptr, 16));
            }
            std::string top;
            stream >> top;
            if (top != "top")
            {
                continue;
            }
            for (int i = 0; i < 32; ++i)
            {
                stream >> entry.map.top[i];
            }
            corpus.push_back(entry);
        }
        std::fclose(file);
        return corpus;
    }

    std::vector<CorpusMap> replay_corpus(Engine &engine, Options const &opt)
    {
        std::vector<CorpusMap> corpus;
        Map map(10, 40);
        std::mt19937 rng(opt.seed);
        std::vector<char> next;
        char hold = ' ';
        size_t moves_done = 0;
        while (moves_done < opt.moves)
        {
            if (!next.empty()) next.erase(next.begin());
            while (next.size() <= opt.maxdepth)
            {
                for (size_t i = 0; i < engine.context()->type_max(); ++i)
                {
                    next.push_back(engine.context()->convert(i));
                }
                std::shuffle(next.end() - engine.context()->type_max(), next.end(), rng);
            }
            engine.ai_config()->safe = engine.ai()->get_safe(map, next.front());
            engine.status()->death = 0;
            engine.status()->combo = 0;
            engine.status()->under_attack = 0;
            engine.status()->map_rise = 0;
            engine.status()->b2b = 0;
            engine.status()->acc_value = 0;
            engine.status()->like = 0;
            engine.status()->value = 0;
            ai_zzz::TOJ::Status::init_t_value(map, engine.status()->t2_value, engine.status()->t3_value);

            char current = next.front();
            auto result = engine.run_hold(map, engine.context()->generate(current), hold, true, next.data() + 1,
                opt.maxdepth, m_tetris::SearchBudget::by_iterations(opt.iters));
            if (result.target == nullptr || result.target->row >= 20)
            {
                map = Map(10, 40);
                next.clear();
                hold = ' ';
                ++moves_done;
                continue;
            }
            if (result.change_hold)
            {
                if (hold == ' ')
                {
                    next.erase(next.begin());
                }
                hold = current;
            }
            result.target->attach(engine.context().get(), map);
            ++moves_done;
            if (moves_done % opt.sample == 0)
            {
                corpus.push_back({ map, 'p', moves_done });
            }
        }
        return corpus;
    }

    std::vector<CorpusMap> synthetic_corpus(Options const &opt)
    {
        std::vector<CorpusMap> corpus;
        std::mt19937 rng(static_cast<uint32_t>(opt.synthetic_seed));
        for (size_t i = 0; i < opt.synthetic + opt.tall; ++i)
        {
            Map map(10, 40);
            int fill = i < opt.synthetic ? static_cast<int>(rng() % 9) + 2 : 18 + static_cast<int>(rng() % 4);
            for (int y = 0; y < fill; ++y)
            {
                uint32_t row = static_cast<uint32_t>(rng() & 0x3ffu);
                if (y + 1 == fill)
                {
                    row |= 0x3ffu;
                }
                map.row[y] = row;
            }
            for (int x = 0; x < 10; ++x)
            {
                int top = 0;
                for (int y = 39; y >= 0; --y)
                {
                    if ((map.row[y] >> x) & 1)
                    {
                        top = y + 1;
                        break;
                    }
                }
                map.top[x] = top;
            }
            map.roof = 0;
            map.count = 0;
            for (int y = 39; y >= 0; --y)
            {
                if (map.row[y] != 0)
                {
                    map.roof = y + 1;
                    break;
                }
            }
            for (int y = 0; y < map.roof; ++y)
            {
                map.count += std::popcount(map.row[y]);
            }
            corpus.push_back({ map, 's', i });
        }
        return corpus;
    }
}

int main(int argc, char **argv)
{
    Options opt = parse_args(argc, argv);

    Engine engine;
    if (!engine.prepare(10, 40))
    {
        std::println(stderr, "engine.prepare(10, 40) failed");
        return 1;
    }
    engine.memory_limit(256ull << 20);
    engine.search_config()->allow_rotate_move = false;
    engine.search_config()->allow_180 = opt.allow_180;
    engine.search_config()->allow_d = opt.allow_d;
    engine.search_config()->is_20g = opt.is_20g;
    engine.search_config()->allow_nont_d = opt.allow_nont_d;
    engine.search_config()->last_rotate = false;

    int const combo_table[] = { 0, 0, 0, 1, 1, 2, 2, 3, 3, 4 };
    engine.ai_config()->table = combo_table;
    engine.ai_config()->table_max = 10;
    engine.ai_config()->param = {
        10.507166148, 7.539860726, 13.048099725, 13.388476179, 6.728747539, 9.476881786,
        0.258534525, -0.108269503, 4.394241496, -4.892359035, 0.049148374, 1.586714505,
        8.885878229, -0.006001836, -0.004336234, -2.021765056, -0.951446468, -1.145468832,
        -1.515758227, -0.612910192, -0.476031978, 0.009596827, -0.399212013, -0.855819915,
        -0.418779377, -0.454784178, -1.417493065, 1.050941751, 0.756272086,
    };

    search_tspin::Search probe;
    search_tspin::Search::Config config = *engine.search_config();
    probe.init(engine.context().get(), &config);

    std::vector<CorpusMap> corpus;
    if (!opt.corpus_in.empty())    {
        corpus = read_corpus(opt.corpus_in);
        std::println(stderr, "loaded {} maps from {}", corpus.size(), opt.corpus_in);
    }
    else
    {
        corpus = replay_corpus(engine, opt);
        auto extra = synthetic_corpus(opt);
        corpus.insert(corpus.end(), extra.begin(), extra.end());
        std::println(stderr, "built corpus: {} replay maps, {} synthetic maps", corpus.size() - extra.size(), extra.size());
        if (!opt.corpus_out.empty())
        {
            FILE *file = std::fopen(opt.corpus_out.c_str(), "wb");
            if (file == nullptr)
            {
                std::println(stderr, "cannot write corpus: {}", opt.corpus_out);
                return 1;
            }
            for (size_t i = 0; i < corpus.size(); ++i)
            {
                write_map(file, i, corpus[i]);
            }
            std::fclose(file);
            std::println(stderr, "wrote corpus to {}", opt.corpus_out);
        }
    }

    FILE *out = stdout;    if (!opt.out.empty())
    {
        out = std::fopen(opt.out.c_str(), "wb");
        if (out == nullptr)
        {
            std::println(stderr, "cannot write output: {}", opt.out);
            return 1;
        }
    }

    size_t calls = 0, route_precomputed = 0, route_bfs = 0, route_t = 0, route_empty = 0;
    size_t landings = 0;
    size_t spin_landings = 0;
    double ns_precomputed = 0, ns_bfs = 0, ns_t = 0;

    for (size_t map_id = 0; map_id < corpus.size(); ++map_id)
    {
        Map const &map = corpus[map_id].map;
        for (size_t type = 0; type < engine.context()->type_max(); ++type)
        {
            char piece = engine.context()->convert(type);
            m_tetris::TetrisNode const *node = engine.context()->generate(piece);
            if (node == nullptr)
            {
                continue;
            }
            for (size_t depth : opt.depths)
            {
                ++calls;
                bool const is_t = node->status.t == 'T';
                bool const precomputed = !config.is_20g && node->land_point != nullptr && node->low >= map.roof && !config.allow_nont_d;
                auto const t0 = std::chrono::steady_clock::now();
                auto const *result = probe.search(map, node, depth);
                auto const t1 = std::chrono::steady_clock::now();
                double const ns = std::chrono::duration<double, std::nano>(t1 - t0).count();
                if (is_t)
                {
                    ++route_t;
                    ns_t += ns;
                }
                else if (precomputed)
                {
                    ++route_precomputed;
                    ns_precomputed += ns;
                }
                else
                {
                    ++route_bfs;
                    ns_bfs += ns;
                }
                if (result->empty())
                {
                    ++route_empty;
                }
                std::vector<Landing> list;
                list.reserve(result->size());
                for (auto const &entry : *result)
                {
                    Landing landing;
                    landing.filtered = entry->index_filtered;
                    landing.t = entry->status.t;
                    landing.x = entry->status.x;
                    landing.y = entry->status.y;
                    landing.r = entry->status.r;
                    landing.type = static_cast<int>(entry.type);
                    landing.flags = entry.flags;
                    landing.last = entry.last != nullptr ? entry.last->index_filtered : static_cast<size_t>(-1);
                    if (landing.type != 0)
                    {
                        ++spin_landings;
                    }
                    list.push_back(landing);
                }
                std::sort(list.begin(), list.end());
                std::print(out, "call {} {} {} {} {} {}\n", map_id, piece, depth, node->index_filtered,
                    precomputed && !is_t ? 1 : 0, list.size());
                for (auto const &landing : list)
                {
                    std::print(out, "land {} {} {} {} {} {} {} {} {} {} {}\n", map_id, piece, depth, landing.filtered,
                        landing.t, landing.x, landing.y, landing.r, landing.type, landing.flags, landing.last);
                }
                landings += list.size();
            }
        }
    }

    if (out != stdout)
    {
        std::fclose(out);
    }

    std::println(stderr, "bitboard executions {} | bitboard T coverage {:.1f}% of {} T-route calls",
        probe.bitboard_executions(), route_t != 0 ? 100.0 * probe.bitboard_executions() / route_t : 0.0, route_t);
    std::println(stderr, "calls {} | precomputed route {} ({:.1f}%) | bfs route {} ({:.1f}%) | T route {} ({:.1f}%) | empty results {}",
        calls,
        route_precomputed, 100.0 * double(route_precomputed) / double(calls),
        route_bfs, 100.0 * double(route_bfs) / double(calls),
        route_t, 100.0 * double(route_t) / double(calls),
        route_empty);
    std::println(stderr, "landings {} (spin-tagged {}), maps {}",
        landings, spin_landings, corpus.size());
    auto report_route = [](char const *name, size_t count, double ns)
    {
        std::println(stderr, "{:>12}: {:>7} calls | {:>8.0f} ns/call | {:>8.3f} ms total",
            name, count, count != 0 ? ns / double(count) : 0.0, ns / 1e6);
    };
    report_route("precomputed", route_precomputed, ns_precomputed);
    report_route("full bfs", route_bfs, ns_bfs);
    report_route("T search", route_t, ns_t);
    if (!corpus.empty())
    {
        std::vector<int> roofs;
        roofs.reserve(corpus.size());
        for (auto const &entry : corpus)
        {
            roofs.push_back(entry.map.roof);
        }
        std::sort(roofs.begin(), roofs.end());
        std::println(stderr, "corpus roofs: min {} median {} max {} ({} maps)",
            roofs.front(), roofs[roofs.size() / 2], roofs.back(), roofs.size());
    }
    return 0;
}
