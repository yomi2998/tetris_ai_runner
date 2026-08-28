#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <climits>
#include <string>
#include <vector>
#include <deque>
#include <random>
#include <thread>
#include <fstream>
#include <print>
#include <nlohmann/json.hpp>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#endif

#include "tetris_core.h"
#include "rule_toj.h"
#include "search_simple.h"
#include "search_tspin.h"
#include "ai_easy.h"

namespace
{
#ifdef _WIN32
#define MATCH_CDECL __cdecl
#else
#define MATCH_CDECL
#endif

    static std::string g_exe_dir;
    static std::string g_config_dir;

    static std::string dir_of(char const *p)
    {
        std::string s(p);
        size_t slash = s.find_last_of("/\\");
        if (slash == std::string::npos) return ".";
        return s.substr(0, slash);
    }

    static std::string find_config(char const *arg, char const *argv0)
    {
        g_exe_dir = dir_of(argv0);
        std::vector<std::string> cand;
        if (arg != nullptr)
        {
            cand.push_back(arg);
            cand.push_back(g_exe_dir + "/" + arg);
        }
        cand.push_back("match.cfg");
        cand.push_back(g_exe_dir + "/match.cfg");
        for (auto const &c : cand)
        {
            FILE *f = std::fopen(c.c_str(), "r");
            if (f != nullptr)
            {
                std::fclose(f);
                return c;
            }
        }
        return arg != nullptr ? arg : "match.cfg";
    }

    // ---------- bot interface (misakamm standard TetrisAI) ----------
    typedef char *(MATCH_CDECL *TetrisAIFn)(
        int overfield[], int field[], int field_w, int field_h,
        int b2b, int combo, char next[], char hold, bool curCanHold,
        char active, int x, int y, int spin,
        bool canhold, bool can180spin,
        int upcomeAtt, int comboTable[], int maxDepth, int level, int player);
    typedef char *(MATCH_CDECL *AINameFn)(int level);

    struct Bot
    {
        std::string name;
        std::string last_error_;
        TetrisAIFn ai = nullptr;
        AINameFn ai_name = nullptr;
#ifdef _WIN32
        HMODULE mod = nullptr;
#else
        void *mod = nullptr;
#endif

        bool load(char const *path)
        {
            bool has_sep = std::strchr(path, '/') != nullptr || std::strchr(path, '\\') != nullptr;
            std::vector<std::string> cand;
            cand.push_back(path);
            if (!has_sep)
            {
                cand.push_back(std::string("./") + path);
                if (!g_config_dir.empty()) cand.push_back(g_config_dir + "/" + path);
                if (!g_exe_dir.empty()) cand.push_back(g_exe_dir + "/" + path);
            }
            std::string last_err;
            for (auto const &c : cand)
            {
                if (try_load(c)) return true;
                last_err = last_error_;
            }
            std::println("failed to load {}: {}", path, last_err);
            return false;
        }

        bool try_load(std::string const &path)
        {
            std::string resolved = path;
#ifdef _WIN32
            char full[MAX_PATH];
            DWORD n = GetFullPathNameA(path.c_str(), MAX_PATH, full, nullptr);
            if (n > 0 && n < MAX_PATH) resolved = full;
            mod = LoadLibraryA(resolved.c_str());
            if (mod == nullptr) return false;
            ai = reinterpret_cast<TetrisAIFn>(reinterpret_cast<void *>(GetProcAddress(mod, "TetrisAI")));
            ai_name = reinterpret_cast<AINameFn>(reinterpret_cast<void *>(GetProcAddress(mod, "AIName")));
#else
            char real[PATH_MAX];
            if (realpath(path.c_str(), real) != nullptr) resolved = real;
            mod = dlopen(resolved.c_str(), RTLD_NOW);
            if (mod == nullptr)
            {
                last_error_ = std::string(dlerror());
                return false;
            }
            ai = reinterpret_cast<TetrisAIFn>(dlsym(mod, "TetrisAI"));
            ai_name = reinterpret_cast<AINameFn>(dlsym(mod, "AIName"));
#endif
            if (ai == nullptr) return false;
            name = ai_name != nullptr ? ai_name(0) : path;
            if (name.empty()) name = path;
            return true;
        }
    };

    // ---------- config (json) ----------
    struct PlayerConfig
    {
        std::string plugin; // dllplugin
        int level = 8;
    };

    struct Config
    {
        PlayerConfig p1, p2;
        int ft = 3;              // first to N wins
        int delay = 0;           // ms sleep before each bot call
        int max_depth = 6;       // next queue length
        size_t max_pieces = 3600;// pieces per game before cap
        unsigned seed = 0;       // 0 = random
        bool view = false;       // print board each round
        std::vector<int> combo_table = {0, 0, 0, 1, 1, 2, 2, 3, 3, 4};
        int combo_table_max = 10;
        std::string telemetry_file;
        int big_attack_threshold = 4;
        std::string bot1, bot2;
    };

    void parse_config(Config &cfg, char const *path)
    {
        std::ifstream ifs(path);
        if (!ifs.good())
        {
            std::println("no config file {}, using defaults", path);
            return;
        }
        nlohmann::json root;
        try
        {
            root = nlohmann::json::parse(ifs, nullptr, true, true); // allow_exceptions, ignore_comments
        }
        catch (std::exception const &e)
        {
            std::println("invalid json config {} ({}), using defaults", path, e.what());
            return;
        }
        cfg.ft = root.value("ft", cfg.ft);
        cfg.delay = root.value("delay", cfg.delay);
        cfg.max_depth = root.value("max_depth", cfg.max_depth);
        cfg.max_pieces = root.value("pieces", cfg.max_pieces);
        cfg.seed = root.value("seed", cfg.seed);
        cfg.telemetry_file = root.value("telemetry", cfg.telemetry_file);
        cfg.big_attack_threshold = root.value("big_attack_threshold", cfg.big_attack_threshold);
        if (root.contains("view"))
        {
            nlohmann::json const &v = root["view"];
            if (v.is_boolean()) cfg.view = v.get<bool>();
            else if (v.is_number_integer()) cfg.view = v.get<int>() != 0;
        }
        if (root.contains("combo_table") && root["combo_table"].is_array())
        {
            cfg.combo_table = root["combo_table"].get<std::vector<int>>();
            cfg.combo_table_max = static_cast<int>(cfg.combo_table.size());
        }
        auto parse_player = [](nlohmann::json const &obj, PlayerConfig &pc)
        {
            if (!obj.is_object()) return;
            pc.plugin = obj.value("dllplugin", pc.plugin);
            pc.level = obj.value("level", pc.level);
        };
        parse_player(root.value("player1", nlohmann::json()), cfg.p1);
        parse_player(root.value("player2", nlohmann::json()), cfg.p2);
    }

    // ---------- player ----------
    enum class Spin
    {
        None, Mini, Full
    };

    struct Player
    {
        Bot const *bot = nullptr;
        m_tetris::TetrisContext const *context = nullptr;
        Config const *cfg = nullptr;
        m_tetris::TetrisMap map;
        search_tspin::Search tspin_search;
        std::mt19937 r_next;
        std::mt19937 r_garbage;
        std::vector<char> next;
        std::deque<int> recv_attack;
        int send_attack = 0;
        int combo = 0;
        char hold = ' ';
        bool b2b = false;
        bool dead = false;
        int total_block = 0;
        int total_clear = 0;
        int total_attack = 0;
        int total_receive = 0;
        int total_receive_packets = 0;
        int max_pending_attack = 0;
        int attack_packets = 0;
        int big_attack_events = 0;
        int big_attack_lines = 0;
        int max_big_attack = 0;
        size_t current_round = 0;
        bool recovery_active = false;
        size_t recovery_start_round = 0;
        int recovery_completed = 0;
        size_t total_recovery_rounds = 0;
        size_t max_recovery_rounds = 0;
        int games_won = 0;
        int level = 8;
        int player = 0;

        Player(Bot const *b, m_tetris::TetrisContext const *c, Config const *cfg_, PlayerConfig const &pc, int player_, unsigned seed)
            : bot(b), context(c), cfg(cfg_)
            , map(10, 40)
            , r_next(seed)
            , r_garbage(seed ^ 0x9e3779b9U)
            , level(pc.level)
            , player(player_)
        {
            search_tspin::Search::Config tconfig;
            tconfig.allow_180 = true;
            tconfig.allow_d = true;
            tconfig.allow_LR = true;
            tconfig.is_20g = false;
            tconfig.last_rotate = false;
            tconfig.allow_rotate_move = false;
            tspin_search.init(context, &tconfig);
        }

        void init()
        {
            map = m_tetris::TetrisMap(10, 40);
            next.clear();
            recv_attack.clear();
            send_attack = 0;
            combo = 0;
            hold = ' ';
            b2b = false;
            dead = false;
            total_block = 0;
            total_clear = 0;
            total_attack = 0;
            total_receive = 0;
            total_receive_packets = 0;
            max_pending_attack = 0;
            attack_packets = 0;
            big_attack_events = 0;
            big_attack_lines = 0;
            max_big_attack = 0;
            current_round = 0;
            recovery_active = false;
            recovery_start_round = 0;
            recovery_completed = 0;
            total_recovery_rounds = 0;
            max_recovery_rounds = 0;
        }

        void prepare()
        {
            if (!next.empty())
            {
                next.erase(next.begin());
            }
            while (next.size() <= static_cast<size_t>(cfg->max_depth))
            {
                for (size_t i = 0; i < context->type_max(); ++i)
                {
                    next.push_back(context->convert(i));
                }
                std::shuffle(next.end() - context->type_max(), next.end(), r_next);
            }
        }

        int pending_attack() const
        {
            int total = 0;
            for (int line : recv_attack)
            {
                total += line;
            }
            return total;
        }

        void finish_recovery()
        {
            if (recovery_active && recv_attack.empty())
            {
                size_t const rounds = current_round >= recovery_start_round
                    ? current_round - recovery_start_round + 1 : 0;
                ++recovery_completed;
                total_recovery_rounds += rounds;
                max_recovery_rounds = std::max(max_recovery_rounds, rounds);
                recovery_active = false;
            }
        }

        size_t incomplete_recovery_rounds() const
        {
            return recovery_active && current_round >= recovery_start_round
                ? current_round - recovery_start_round + 1 : 0;
        }

        void under_attack(int line)
        {
            if (line > 0)
            {
                ++attack_packets;
                if (line >= cfg->big_attack_threshold)
                {
                    ++big_attack_events;
                    big_attack_lines += line;
                    max_big_attack = std::max(max_big_attack, line);
                    if (!recovery_active)
                    {
                        recovery_active = true;
                        recovery_start_round = current_round + 1;
                    }
                }
                recv_attack.emplace_back(line);
                max_pending_attack = std::max(max_pending_attack, pending_attack());
            }
        }

        double apl() const
        {
            return total_clear == 0 ? 0. : 1. * total_attack / total_clear;
        }

        double app() const
        {
            return total_block == 0 ? 0. : 1. * total_attack / total_block;
        }

        bool apply_path(char &current, int &clear_out, Spin &spin_out, char const *path)
        {
            m_tetris::TetrisNode const *node = context->get(current, 3, 21, 0);
            if (node == nullptr)
            {
                return false;
            }
            bool last_rotate = false;
            bool hd = false;
            for (char const *p = path; *p != '\0'; ++p)
            {
                switch (*p)
                {
                case 'v': // hold
                    if (hold == ' ')
                    {
                        hold = current;
                        if (next.empty())
                        {
                            return false;
                        }
                        next.erase(next.begin());
                        current = next.front();
                    }
                    else
                    {
                        std::swap(hold, current);
                    }
                    node = context->get(current, 3, 21, 0);
                    if (node == nullptr)
                    {
                        return false;
                    }
                    last_rotate = false;
                    break;
                case 'L':
                    while (node->move_left != nullptr && node->move_left->check(map)) node = node->move_left;
                    last_rotate = false;
                    break;
                case 'R':
                    while (node->move_right != nullptr && node->move_right->check(map)) node = node->move_right;
                    last_rotate = false;
                    break;
                case 'l':
                    if (node->move_left != nullptr && node->move_left->check(map)) node = node->move_left;
                    last_rotate = false;
                    break;
                case 'r':
                    if (node->move_right != nullptr && node->move_right->check(map)) node = node->move_right;
                    last_rotate = false;
                    break;
                case 'd':
                    if (node->move_down != nullptr && node->move_down->check(map)) node = node->move_down;
                    last_rotate = false;
                    break;
                case 'D':
                    node = node->drop(map);
                    last_rotate = false;
                    break;
                case 'z':
                    for (m_tetris::TetrisNode const *n : node->wall_kick_counterclockwise)
                    {
                        if (n == nullptr) break;
                        if (n->check(map)) { node = n; break; }
                    }
                    last_rotate = true;
                    break;
                case 'x':
                    for (m_tetris::TetrisNode const *n : node->wall_kick_opposite)
                    {
                        if (n == nullptr) break;
                        if (n->check(map)) { node = n; break; }
                    }
                    last_rotate = true;
                    break;
                case 'c':
                    for (m_tetris::TetrisNode const *n : node->wall_kick_clockwise)
                    {
                        if (n == nullptr) break;
                        if (n->check(map)) { node = n; break; }
                    }
                    last_rotate = true;
                    break;
                case 'V': // hard drop + lock
                    node = node->drop(map);
                    hd = true;
                    break;
                default:
                    break;
                }
                if (hd)
                {
                    break;
                }
            }
            if (node == nullptr)
            {
                return false;
            }
            if (node->row >= 20) // top out
            {
                return false;
            }
            m_tetris::TetrisMap pre_map = map;
            clear_out = static_cast<int>(node->attach(context, map));
            spin_out = Spin::None;
            if (current == 'T' && last_rotate)
            {
                switch (tspin_search.classify(pre_map, node, last_rotate, static_cast<size_t>(clear_out)))
                {
                case search_tspin::Search::TSpinType::TSpinMini:
                    spin_out = Spin::Mini;
                    break;
                case search_tspin::Search::TSpinType::TSpin:
                    spin_out = Spin::Full;
                    break;
                case search_tspin::Search::TSpinType::None:
                default:
                    spin_out = Spin::None;
                    break;
                }
            }
            return true;
        }

        void run()
        {
            char current = next.front();

            m_tetris::TetrisNode const *spawn = context->get(current, 3, 21, 0);
            if (spawn == nullptr || !spawn->check(map))
            {
                dead = true;
                return;
            }

            int field[23] = {0};
            for (int r = 0; r < 22; ++r)
            {
                field[r] = map.row[22 - r];
            }
            field[22] = map.row[0];
            int overfield[8] = {0};
            for (int k = 0; k < 8; ++k)
            {
                overfield[k] = map.row[23 + k];
            }

            int upcome = 0;
            for (int v : recv_attack)
            {
                upcome += v;
            }

            std::vector<int> combo_buf = cfg->combo_table;
            combo_buf.push_back(-1);

            if (cfg->delay > 0)
            {
#ifdef _WIN32
                Sleep(cfg->delay);
#else
                usleep(cfg->delay * 1000);
#endif
            }

            char *path = bot->ai(
                overfield, field, 10, 22,
                b2b ? 1 : 0, combo,
                next.data() + 1, hold, true,
                current, 3, 1, 0,
                true, true,
                upcome, combo_buf.data(), cfg->max_depth, level, player);
            if (path == nullptr || path[0] == '\0')
            {
                dead = true;
                return;
            }

            int clear = 0;
            Spin spin = Spin::None;
            if (!apply_path(current, clear, spin, path))
            {
                dead = true;
                return;
            }

            int attack = 0;
            auto get_combo_attack = [&](int c)
            {
                return cfg->combo_table[std::min(cfg->combo_table_max - 1, c)];
            };
            switch (clear)
            {
            case 0:
                combo = 0;
                break;
            case 1:
                if (spin == Spin::Mini)
                {
                    attack += 1 + b2b;
                    b2b = true;
                }
                else if (spin == Spin::Full)
                {
                    attack += 2 + b2b;
                    b2b = true;
                }
                else
                {
                    b2b = false;
                }
                attack += get_combo_attack(++combo);
                break;
            case 2:
                if (spin != Spin::None)
                {
                    attack += 4 + b2b;
                    b2b = true;
                }
                else
                {
                    attack += 1;
                    b2b = false;
                }
                attack += get_combo_attack(++combo);
                break;
            case 3:
                if (spin != Spin::None)
                {
                    attack += 6 + b2b * 2;
                    b2b = true;
                }
                else
                {
                    attack += 2;
                    b2b = false;
                }
                attack += get_combo_attack(++combo);
                break;
            case 4:
                attack += get_combo_attack(++combo) + 4 + b2b;
                b2b = true;
                break;
            }
            if (map.count == 0)
            {
                attack += 6; // perfect clear
            }
            ++total_block;
            total_clear += clear;
            total_attack += attack;
            send_attack = attack;

            // incoming garbage: blocked by attack/combo, otherwise lands at bottom
            while (!recv_attack.empty())
            {
                if (send_attack > 0)
                {
                    if (recv_attack.front() <= send_attack)
                    {
                        send_attack -= recv_attack.front();
                        recv_attack.pop_front();
                        continue;
                    }
                    recv_attack.front() -= send_attack;
                    send_attack = 0;
                }
                if (send_attack > 0 || combo > 0)
                {
                    break;
                }
                int line = recv_attack.front();
                total_receive += line;
                ++total_receive_packets;
                recv_attack.pop_front();
                for (int y = map.height - 1; y >= line; --y)
                {
                    map.row[y] = map.row[y - line];
                }
                uint32_t hole = context->full() & ~(1u << std::uniform_int_distribution<uint32_t>(0, context->width() - 1)(r_garbage));
                for (int y = 0; y < line; ++y)
                {
                    map.row[y] = hole;
                }
                map.count = 0;
                for (int my = 0; my < map.height; ++my)
                {
                    for (int mx = 0; mx < map.width; ++mx)
                    {
                        if (map.full(mx, my))
                        {
                            map.top[mx] = map.roof = my + 1;
                            ++map.count;
                        }
                    }
                }
            }
            finish_recovery();
        }
    };

    enum class WinnerReason
    {
        P1Survivor,
        P2Survivor,
        P1CapAPL,
        P2CapAPL,
        P1BothDeadAPL,
        P2BothDeadAPL,
        BothDeadDraw,
        CapDraw,
    };

    struct GameResult
    {
        int winner = 0;
        WinnerReason reason = WinnerReason::CapDraw;
        bool capped = false;
        size_t rounds = 0;
    };

    char const *winner_reason_name(WinnerReason reason)
    {
        switch (reason)
        {
        case WinnerReason::P1Survivor: return "p1_survivor";
        case WinnerReason::P2Survivor: return "p2_survivor";
        case WinnerReason::P1CapAPL: return "p1_cap_apl";
        case WinnerReason::P2CapAPL: return "p2_cap_apl";
        case WinnerReason::P1BothDeadAPL: return "p1_both_dead_apl";
        case WinnerReason::P2BothDeadAPL: return "p2_both_dead_apl";
        case WinnerReason::BothDeadDraw: return "both_dead_draw";
        case WinnerReason::CapDraw: return "cap_draw";
        }
        return "unknown";
    }

    // ---------- display ----------
    void view(Player const &p1, Player const &p2)
    {
        std::print("\x1b[H\x1b[2J");
        int up1 = 0, up2 = 0;
        for (int v : p1.recv_attack) up1 += v;
        for (int v : p2.recv_attack) up2 += v;
        std::println(
            "HOLD={} NEXT={} COMBO={} B2B={} UP={} P={} L={} A={} APL={:.2f} APP={:.2f} {} W={}\n"
            "HOLD={} NEXT={} COMBO={} B2B={} UP={} P={} L={} A={} APL={:.2f} APP={:.2f} {} W={}",
            p1.hold, std::string(p1.next.begin() + 1, p1.next.begin() + 1 + p1.cfg->max_depth),
            p1.combo, p1.b2b, up1, p1.total_block, p1.total_clear, p1.total_attack, p1.apl(), p1.app(), p1.bot->name, p1.games_won,
            p2.hold, std::string(p2.next.begin() + 1, p2.next.begin() + 1 + p2.cfg->max_depth),
            p2.combo, p2.b2b, up2, p2.total_block, p2.total_clear, p2.total_attack, p2.apl(), p2.app(), p2.bot->name, p2.games_won);
        for (int y = 21; y >= 0; --y)
        {
            for (int x = 0; x < 10; ++x)
            {
                std::print("{}", p1.map.full(x, y) ? "[]" : "  ");
            }
            std::print("  ");
            for (int x = 0; x < 10; ++x)
            {
                std::print("{}", p2.map.full(x, y) ? "[]" : "  ");
            }
            std::println("");
        }
        std::fflush(stdout);
    }

    // ---------- match ----------
    GameResult run_game(Player &p1, Player &p2, Config const &cfg)
    {
        size_t round = 0;
        bool capped = false;
        for (;;)
        {
            ++round;
            p1.current_round = round;
            p2.current_round = round;
            p1.prepare();
            p2.prepare();
            if (cfg.view)
            {
                view(p1, p2);
            }
            std::thread t1([&p1] { p1.run(); });
            std::thread t2([&p2] { p2.run(); });
            t1.join();
            t2.join();
            if (p1.dead || p2.dead)
            {
                break;
            }
            if (round >= cfg.max_pieces)
            {
                capped = true;
                break;
            }
            int min_attack = std::min(p1.send_attack, p2.send_attack);
            p1.send_attack -= min_attack;
            p2.send_attack -= min_attack;
            p1.under_attack(p2.send_attack);
            p2.under_attack(p1.send_attack);
        }
        // winner: 1 = p1, 2 = p2, 0 = draw
        if (p1.dead && !p2.dead) return {2, WinnerReason::P2Survivor, capped, round};
        if (p2.dead && !p1.dead) return {1, WinnerReason::P1Survivor, capped, round};
        double a1 = p1.apl(), a2 = p2.apl();
        if (a1 > a2)
        {
            return {1, capped ? WinnerReason::P1CapAPL : WinnerReason::P1BothDeadAPL, capped, round};
        }
        if (a2 > a1)
        {
            return {2, capped ? WinnerReason::P2CapAPL : WinnerReason::P2BothDeadAPL, capped, round};
        }
        return {0, capped ? WinnerReason::CapDraw : WinnerReason::BothDeadDraw, capped, round};
    }

    void write_telemetry_header(FILE *file)
    {
        std::fprintf(file,
            "game,seed,max_pieces,max_depth,big_attack_threshold,p1_level,p2_level,"
            "winner,winner_reason,capped,rounds,bot1,bot2,"
            "p1_dead,p1_blocks,p1_lines,p1_attack,p1_receive,p1_receive_packets,"
            "p1_attack_packets,p1_big_attack_events,p1_big_attack_lines,p1_max_big_attack,"
            "p1_max_pending_attack,p1_recovery_completed,p1_recovery_incomplete,"
            "p1_total_recovery_rounds,p1_max_recovery_rounds,p1_app,p1_apl,"
            "p2_dead,p2_blocks,p2_lines,p2_attack,p2_receive,p2_receive_packets,"
            "p2_attack_packets,p2_big_attack_events,p2_big_attack_lines,p2_max_big_attack,"
            "p2_max_pending_attack,p2_recovery_completed,p2_recovery_incomplete,"
            "p2_total_recovery_rounds,p2_max_recovery_rounds,p2_app,p2_apl\n");
    }

    void write_telemetry_row(FILE *file, size_t game, GameResult const &result,
                             Player const &p1, Player const &p2, Bot const &bot1, Bot const &bot2,
                             Config const &cfg)
    {
        std::fprintf(file,
            "%zu,%u,%zu,%d,%d,%d,%d,%d,%s,%d,%zu,%s,%s,"
            "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%zu,%zu,%zu,%.17g,%.17g,"
            "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%zu,%zu,%zu,%.17g,%.17g\n",
            game, cfg.seed, cfg.max_pieces, cfg.max_depth, cfg.big_attack_threshold,
            p1.level, p2.level, result.winner, winner_reason_name(result.reason), result.capped ? 1 : 0, result.rounds,
            bot1.name.c_str(), bot2.name.c_str(),
            p1.dead ? 1 : 0, p1.total_block, p1.total_clear, p1.total_attack, p1.total_receive,
            p1.total_receive_packets, p1.attack_packets, p1.big_attack_events, p1.big_attack_lines,
            p1.max_big_attack, p1.max_pending_attack, p1.recovery_completed, p1.incomplete_recovery_rounds(),
            p1.total_recovery_rounds, p1.max_recovery_rounds, p1.app(), p1.apl(),
            p2.dead ? 1 : 0, p2.total_block, p2.total_clear, p2.total_attack, p2.total_receive,
            p2.total_receive_packets, p2.attack_packets, p2.big_attack_events, p2.big_attack_lines,
            p2.max_big_attack, p2.max_pending_attack, p2.recovery_completed, p2.incomplete_recovery_rounds(),
            p2.total_recovery_rounds, p2.max_recovery_rounds, p2.app(), p2.apl());
    }
}

int main(int argc, char **argv)
{
    std::println(
        "usage: match [bot1] [bot2] [config]\n"
        "  match <config>        - bots from player1/player2.dllplugin in config\n"
        "  match <bot1> <bot2>   - bots from args, config = match.cfg\n"
        "  match <bot1> <bot2> <config>\n"
        "config (json): ft, delay, max_depth, pieces, seed, view, combo_table, telemetry, big_attack_threshold\n"
        "per-player (json): player1/player2 {{ dllplugin, level }}");

    Config cfg;
    char const *config_arg = "match.cfg";
    if (argc == 2)
    {
        config_arg = argv[1];
    }
    else if (argc >= 4)
    {
        cfg.bot1 = argv[1];
        cfg.bot2 = argv[2];
        config_arg = argv[3];
    }
    else if (argc == 3)
    {
        cfg.bot1 = argv[1];
        cfg.bot2 = argv[2];
    }
    std::string config_path = find_config(config_arg, argv[0]);
    g_config_dir = dir_of(config_path.c_str());
    parse_config(cfg, config_path.c_str());
    if (cfg.ft < 1) cfg.ft = 1;
    if (cfg.big_attack_threshold < 1) cfg.big_attack_threshold = 1;

    // CLI bot args override config plugins
    if (!cfg.bot1.empty()) cfg.p1.plugin = cfg.bot1;
    if (!cfg.bot2.empty()) cfg.p2.plugin = cfg.bot2;
    if (cfg.p1.plugin.empty() || cfg.p2.plugin.empty())
    {
        std::println("missing bot plugin for player {} (set dllplugin in {} or pass as argument)",
            cfg.p1.plugin.empty() ? 1 : 2, config_path);
        return 1;
    }
    if (cfg.seed == 0)
    {
        cfg.seed = static_cast<unsigned>(std::time(nullptr));
    }

    Bot bot1, bot2;
    if (!bot1.load(cfg.p1.plugin.c_str()))
    {
        std::println("failed to load bot1: {}", cfg.p1.plugin);
        return 1;
    }
    if (!bot2.load(cfg.p2.plugin.c_str()))
    {
        std::println("failed to load bot2: {}", cfg.p2.plugin);
        return 1;
    }

    std::println("=== {} vs {} ===", bot1.name, bot2.name);
    std::println(
        "p1: {} level={}\np2: {} level={}\n"
        "ft={} delay={} max_depth={} pieces={} seed={} view={}",
        cfg.p1.plugin, cfg.p1.level,
        cfg.p2.plugin, cfg.p2.level,
        cfg.ft, cfg.delay, cfg.max_depth, cfg.max_pieces, cfg.seed, static_cast<int>(cfg.view));

    FILE *telemetry = nullptr;
    if (!cfg.telemetry_file.empty())
    {
        telemetry = std::fopen(cfg.telemetry_file.c_str(), "w");
        if (telemetry == nullptr)
        {
            std::println("failed to open telemetry file: {}", cfg.telemetry_file);
            return 1;
        }
            write_telemetry_header(telemetry);
    }

    m_tetris::TetrisEngine<rule_toj::TetrisRule, ai_easy::AI, search_simple::Search> global_ai;
    if (!global_ai.prepare(10, 40))
    {
        std::println("engine prepare failed");
        return 1;
    }
    m_tetris::TetrisContext const *context = global_ai.context().get();

    Player p1(&bot1, context, &cfg, cfg.p1, 0, cfg.seed);
    Player p2(&bot2, context, &cfg, cfg.p2, 1, cfg.seed ^ 0x9e3779b9U);

    int wins1 = 0, wins2 = 0, draws = 0, games = 0;
    while (wins1 < cfg.ft && wins2 < cfg.ft)
    {
        ++games;
        p1.init();
        p2.init();
        GameResult const game_result = run_game(p1, p2, cfg);
        if (telemetry != nullptr)
        {
            write_telemetry_row(telemetry, games, game_result, p1, p2, bot1, bot2, cfg);
            std::fflush(telemetry);
        }
        std::println(
            "game {}: {} vs {} -> {}  (pieces {}/{} lines {}/{} attack {}/{} apl {:.2f}/{:.2f} app {:.2f}/{:.2f})",
            games, bot1.name, bot2.name,
            game_result.winner == 1 ? "P1 WIN" : (game_result.winner == 2 ? "P2 WIN" : "draw"),
            p1.total_block, p2.total_block, p1.total_clear, p2.total_clear,
            p1.total_attack, p2.total_attack, p1.apl(), p2.apl(), p1.app(), p2.app());
        if (game_result.winner == 1)
        {
            ++wins1;
            ++p1.games_won;
        }
        else if (game_result.winner == 2)
        {
            ++wins2;
            ++p2.games_won;
        }
        else ++draws;
        if (games >= 10000) break;
    }

    if (telemetry != nullptr)
    {
        std::fclose(telemetry);
    }

    std::println(
        "=== match result: {} {} - {} {} (draws {}) ===",
        bot1.name, wins1, wins2, bot2.name, draws);
    return wins1 >= wins2 ? 0 : 1;
}
