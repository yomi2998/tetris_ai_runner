#ifdef _WIN32
#define DECLSPEC_EXPORT __declspec(dllexport)
#define WINAPI __stdcall
#else
#define DECLSPEC_EXPORT
#define WINAPI
#define __cdecl
#endif

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include "tetris_core.h"
#include "search_tspin.h"
#include "ai_zzz.h"
#include "rule_toj.h"
#include "random.h"

extern "C" void attach_init()
{
    ege::mtsrand((unsigned int)(time(nullptr)));
}

/*
 ***********************************************************************************************
 * 用于多next版本的ST...我自己MOD过的...参与比赛http://misakamm.com/blog/504请参照demo.cpp的AIPath
 ***********************************************************************************************
 * path 用于接收操作过程并返回，操作字符集：
 *      'l': 左移一格
 *      'r': 右移一格
 *      'd': 下移一格
 *      'L': 左移到头
 *      'R': 右移到头
 *      'D': 下移到底（但不粘上，可继续移动）
 *      'z': 逆时针旋转
 *      'c': 顺时针旋转
 * 字符串末尾要加'\0'，表示落地操作（或硬降落）
 *
 * 本函数支持任意路径操作，若不需要此函数只想使用上面一个的话，则删掉本函数即可
 *
 ***********************************************************************************************
 * 将此文件(ai.cpp)从工程排除,增加demo.cpp进来就可以了.如果直接使用标准的ST调用...会发生未定义的行为!
 ***********************************************************************************************
 */
m_tetris::TetrisEngine<rule_toj::TetrisRule, ai_zzz::TOJ, search_tspin::Search> srs_ai;
std::mutex srs_ai_lock;

namespace
{
    ai_zzz::TOJ::Param default_toj_param()
    {
        double values[ai_zzz::TOJ::NUM_PARAMS];
        ai_zzz::TOJ::production_default_theta(values);
        ai_zzz::TOJ::Param result;
        ai_zzz::TOJ::theta_to_param(values, result);
        return result;
    }

    bool read_toj_param(char const *path, ai_zzz::TOJ::Param &out)
    {
        double values[ai_zzz::TOJ::NUM_PARAMS];
        FILE *file = std::fopen(path, "rb");
        if (file == nullptr)
        {
            return false;
        }
        size_t const count = std::fread(values, sizeof(double),
                                        ai_zzz::TOJ::NUM_PARAMS, file);
        std::fclose(file);
        if (count != ai_zzz::TOJ::NUM_PARAMS)
        {
            return false;
        }
        for (double value : values)
        {
            if (!std::isfinite(value))
            {
                return false;
            }
        }
        ai_zzz::TOJ::theta_to_param(values, out);
        return true;
    }

    ai_zzz::TOJ::Param const &toj_param()
    {
        static ai_zzz::TOJ::Param const param = []
        {
            ai_zzz::TOJ::Param result = default_toj_param();
            char const *path = std::getenv("TETRIS_AI_PARAM_FILE");
            if (path == nullptr || *path == '\0')
            {
                path = "best_param.bin";
            }
            if (read_toj_param(path, result))
            {
                std::fprintf(stderr, "[TetrisAI] loaded TOJ parameters from %s\n", path);
            }
            else if (std::getenv("TETRIS_AI_PARAM_FILE") != nullptr)
            {
                std::fprintf(stderr, "[TetrisAI] failed to load %s; using built-in TOJ parameters\n", path);
            }
            return result;
        }();
        return param;
    }
}

extern "C" DECLSPEC_EXPORT int __cdecl AIDllVersion()
{
    return 2;
}

extern "C" DECLSPEC_EXPORT char *__cdecl AIName(int level)
{
    static char name[200];
    strcpy(name, srs_ai.ai_name().c_str());
    return name;
}

/*
all 'char' type is using the characters in ' ITLJZSO'

field data like this:
00........   -> 0x3
00.0......   -> 0xb
00000.....   -> 0x1f

b2b: the count of special attack, the first one set b2b=1, but no extra attack. Have extra attacks when b2b>=2
combo: first clear set combo=1, so the comboTable in toj rule is [0, 0, 0, 1, 1, 2, 2, 3, ...]
next: array size is 'maxDepth'
x, y, spin: the active piece's x/y/orientation,
x/y is the up-left corner's position of the active piece.
see tetris_gem.cpp for the bitmaps.
curCanHold: indicates whether you can use hold on current move.
might be caused by re-think after a hold move.
canhold: false if hold is completely disabled.
comboTable: -1 is the end of the table.
*/
extern "C" DECLSPEC_EXPORT char *__cdecl TetrisAI(int overfield[], int field[], int field_w, int field_h, int b2b, int combo, char next[], char hold, bool curCanHold, char active, int x, int y, int spin, bool canhold, bool can180spin, int upcomeAtt, int comboTable[], int maxDepth, int level, int player)
{
    static char result_buffer[8][1024];
    char *result = result_buffer[player];
    std::unique_lock<std::mutex> lock(srs_ai_lock);

    if (field_w != 10 || field_h != 22 || !srs_ai.prepare(10, 40))
    {
        *result = '\0';
        return result;
    }
    m_tetris::TetrisMap map(10, 40);
    for (size_t d = 0, s = 22; d < 23; ++d, --s)
    {
        map.row[d] = field[s];
    }
    for (size_t d = 23, s = 0; s < 8; ++d, ++s)
    {
        map.row[d] = overfield[s];
    }
    for (int my = 0; my < map.height; ++my)
    {
        for (int mx = 0; mx < map.width; ++mx)
        {
            if (map.full(mx, my))
            {
                map.top[mx] = map.roof = my + 1;
                map.row[my] |= 1 << mx;
                ++map.count;
            }
        }
    }
    srs_ai.search_config()->allow_rotate_move = false;
    srs_ai.search_config()->allow_180 = can180spin;
    srs_ai.search_config()->allow_d = true;
    srs_ai.search_config()->allow_nont_d = false;
    srs_ai.search_config()->is_20g = false;
    srs_ai.search_config()->last_rotate = false;
    struct ComboTable {
        int table[24] = {0};
        int table_max = 0;
    };
    static ComboTable table;
    if (table.table_max == 0) {
        size_t max = 0;
        while (comboTable[max] != -1)
        {
            table.table[max] = comboTable[max];
            ++max;
        }
        table.table_max = max;
    }
    srs_ai.ai_config()->table = table.table;
    srs_ai.ai_config()->table_max = table.table_max;
    srs_ai.memory_limit(256ull << 20);
    srs_ai.ai_config()->safe = srs_ai.ai()->get_safe(map, active);
    // srs_ai.ai_config()->param = { 36.118271157, 202.203495764, 200.737909778, 170.781301529, 277.040476787, 247.783175303, 3.729165582, -55.949272093, -30.745551429, 11.519702458, 3.400517468, 112.960485307, 171.678755503, -0.004778355, -0.111297405, -22.246305463, -7.869832591, -56.390368723, -70.581632887, -63.004355360, -1.839383519, 1.285416709, -0.143928932, -3.284161895, 5.967192336, 3.808250892, 3.238022919, 83.284536559, 0.309568618 };
    srs_ai.ai_config()->param = toj_param();
    // srs_ai.ai_config()->param = { 9.751914367, 6.771584511, 13.984778367, 20.368342456, 6.585961649, 20.332921226, 0.356373036, -0.386894461, -3.709699649, -1.675111576, 0.023687142, 10.297308519, 8.682478710, 0.001170853, 0.001022283, -1.241649334, -0.733500168, -1.611358148, -1.038454063, -0.445952144, -0.207823940, 0.000092385, -0.797795083, -6.153751596, -1.118142645, -0.641180767, -0.193865538, 0.505326328, 3.467200242 };
    srs_ai.status()->death = 0;
    srs_ai.status()->combo = combo;
    if (srs_ai.status()->under_attack != upcomeAtt)
    {
        srs_ai.update();
    }
    srs_ai.status()->under_attack = upcomeAtt;
    srs_ai.status()->map_rise = 0;
    srs_ai.status()->b2b = !!b2b;
    srs_ai.status()->acc_value = 0;
    srs_ai.status()->like = 0;
    srs_ai.status()->value = 0;
    ai_zzz::TOJ::Status::init_t_value(map, srs_ai.status()->t2_value, srs_ai.status()->t3_value);

    m_tetris::TetrisBlockStatus status(active, x, 22 - y, (4 - spin) % 4);
    m_tetris::TetrisNode const *node = srs_ai.get(status);
    static double const base_time = std::pow(100, 1.0 / 8);
    if (canhold)
    {
        auto run_result = srs_ai.run_hold(map, node, hold, curCanHold, next, maxDepth, time_t(std::pow(base_time, level)));
        if (run_result.change_hold)
        {
            result++[0] = 'v';
            if (run_result.target != nullptr)
            {
                std::vector<char> ai_path = srs_ai.make_path(srs_ai.context()->generate(run_result.target->status.t), run_result.target, map);
                std::memcpy(result, ai_path.data(), ai_path.size());
                result += ai_path.size();
            }
        }
        else
        {
            if (run_result.target != nullptr)
            {
                std::vector<char> ai_path = srs_ai.make_path(node, run_result.target, map);
                std::memcpy(result, ai_path.data(), ai_path.size());
                result += ai_path.size();
            }
        }
    }
    else
    {
        auto run_result = srs_ai.run(map, node, next, maxDepth, time_t(std::pow(base_time, level)));
        if (run_result.target != nullptr)
        {
            std::vector<char> ai_path = srs_ai.make_path(node, run_result.target, map);
            std::memcpy(result, ai_path.data(), ai_path.size());
            result += ai_path.size();
        }
    }
    result++[0] = 'V';
    result[0] = '\0';
    return result_buffer[player];
}
