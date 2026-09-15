
#pragma once

#include "tetris_core.h"
#include "search_tspin.h"
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace ai_zzz
{
    namespace qq
    {
        class Attack
        {
        public:
            struct Config
            {
                size_t level;
                int mode;
            };
            struct Result
            {
                double land_point, map;
                int danger;
            };
            struct Status
            {
                double land_point;
                double attack;
                double rubbish;
                double value;
                bool operator < (Status const &) const;
            };
        public:
            void init(m_tetris::TetrisContext const *context, Config const *config);
            std::string ai_name() const;
            Result eval(m_tetris::TetrisNode const *node, m_tetris::TetrisMap const &map, m_tetris::TetrisMap const &src_map) const;
            Status get(m_tetris::TetrisNode const *node, Result const &eval_result, size_t clear, m_tetris::TetrisMap const &map, size_t depth, Status const &status) const;

        private:
            uint32_t check_line_1_[32];
            uint32_t check_line_2_[32];
            uint32_t *check_line_1_end_;
            uint32_t *check_line_2_end_;
            Config const *config_;
            m_tetris::TetrisContext const *context_;
            int col_mask_, row_mask_;
            struct MapInDangerData
            {
                uint32_t data[4];
            };
            std::vector<MapInDangerData> map_danger_data_;
            size_t map_in_danger_(m_tetris::TetrisMap const &map) const;
        };
    }

    class Dig
    {
    public:
        struct Config
        {
            std::array<double, 100> p =
            {
                0 ,     1,
                0 ,     1,
                0 ,     1,
                0 ,    96,
                0 ,   160,
                0 ,   128,
                0 ,    60,
                0 ,   380,
                0 ,   100,
                0 ,    40,
                0 , 50000,
                32,  0.25,
            };
        };
        void init(m_tetris::TetrisContext const *context, Config const *config);
        std::string ai_name() const;
        double eval(m_tetris::TetrisNode const *node, m_tetris::TetrisMap const &map, m_tetris::TetrisMap const &src_map) const;
        double get(m_tetris::TetrisNode const *node, double const &eval_result, m_tetris::TetrisMap const &map) const;
    private:
        struct MapInDangerData
        {
            int data[4];
        };
        std::vector<MapInDangerData> map_danger_data_;
        m_tetris::TetrisContext const *context_;
        Config const *config_;
        size_t map_in_danger_(m_tetris::TetrisMap const &map) const;
        int col_mask_, row_mask_;
    };

    class TOJ_PC
    {
    public:
        using TSpinType = search_tspin::Search::TSpinType;
        using TetrisNodeEx = search_tspin::Search::TetrisNodeWithTSpinType;
        struct Config
        {
            int const *table;
            int table_max;
        };
        struct Result
        {
            double value;
            int roof;
        };
        struct Status
        {
            int under_attack;
            int recv_attack;
            int attack;
            int like;
            int combo;
            bool b2b;
            bool pc;
            double value;
            bool operator < (Status const &) const;
        };
    public:
        void init(m_tetris::TetrisContext const *context, Config const *config);
        std::string ai_name() const;
        double ratio() const
        {
            return 0.5;
        }
        Result eval(TetrisNodeEx const &node, m_tetris::TetrisMap const &map, m_tetris::TetrisMap const &src_map) const;
        Status get(TetrisNodeEx &node, Result const &eval_result, size_t clear, m_tetris::TetrisMap const &map, size_t depth, Status const & status) const;

    private:
        m_tetris::TetrisContext const *context_;
        Config const *config_;
        int col_mask_, row_mask_;
    };

    class TOJ_v08
    {
    public:
        using TSpinType = search_tspin::Search::TSpinType;
        using TetrisNodeEx = search_tspin::Search::TetrisNodeWithTSpinType;
        struct Config
        {
            int const *table;
            int table_max;
        };
        struct Result
        {
            double value;
            int count;
            int t2_value;
            int t3_value;
        };
        struct Status
        {
            int max_combo;
            int max_attack;
            int death;
            int combo;
            int attack;
            int under_attack;
            int map_rise;
            bool b2b;
            double like;
            double value;
            bool operator < (Status const &) const;
        };
    public:
        int8_t get_safe(m_tetris::TetrisMap const &m, char t) const;
        void init(m_tetris::TetrisContext const *context, Config const *config);
        std::string ai_name() const;
        double ratio() const
        {
            return 1.5;
        }
        Result eval(TetrisNodeEx const &node, m_tetris::TetrisMap const &map, m_tetris::TetrisMap const &src_map) const;
        Status get(TetrisNodeEx &node, Result const &eval_result, size_t clear, m_tetris::TetrisMap const &map, size_t depth, Status const & status, m_tetris::TetrisContext::Env const &env) const;
    private:
        m_tetris::TetrisContext const *context_;
        Config const *config_;
        int col_mask_, row_mask_;
        int full_count_;
        struct MapInDangerData
        {
            int data[4];
        };
        std::vector<MapInDangerData> map_danger_data_;
        size_t map_in_danger_(m_tetris::TetrisMap const &map, size_t t, size_t up) const;
    };

    class TOJ
    {
    public:
        using TSpinType = search_tspin::Search::TSpinType;
        using TetrisNodeEx = search_tspin::Search::TetrisNodeWithTSpinType;
        struct Param {
            double base = 40;
            double roof = 160;
            double col_trans = 160;
            double row_trans = 160;
            double hole_count = 256;
            double hole_line = 256;
            double clear_width = 24;
            double wide_2 = -64;
            double wide_3 = -64;
            double wide_4 = 8;
            double safe = 16;
            double b2b = 128;
            double attack = 128;
            double hold_t = 0.25;
            double hold_i = 0.25;
            double waste_t = -16;
            double waste_i = -8;
            double clear_1 = -64;
            double clear_2 = -64;
            double clear_3 = -64;
            double clear_4 = 0;
            double t2_slot = 0.75;
            double t3_slot = 0.75;
            double tspin_mini = -2;
            double tspin_1 = 0;
            double tspin_2 = 4;
            double tspin_3 = 4;
            double combo = 80;
            double ratio = 0;
            double cover = 2;
            double bump = 0.5;
            double bump_sq = 0.15;
            double well = 3;
            double height_max = 0.2;
            double height_half = 0.5;
            double height_quarter = 1;
            double well_use = 0.05;
            double pc_like = 999;
            double pc_attack = 6;
            double garb_cancel = 1;
            double v08_hole = 0;
            double v08_well = 0;
            double debt_ratio = 0.5;
            double feed = 0.5;
            double no_attack = 0.4;
        };

        static constexpr size_t NUM_PARAMS = 45;
        static constexpr double kProductionDefaultTheta[NUM_PARAMS] = {
            10.075375282029107, 6.022185326100125, 13.34712587204267, 12.98659197541254, 0.8192369673975549, 26.57754051950049,
            0.8388642998295932, 0.2249972058101183, -5.020314924902048, -3.180091889846865, 0.02101002546646566, -1.4633111070691347,
            13.693510652803226, -0.0053741942339516225, -0.005095696735068245, -5.61810597290098, -0.9266740600897814, -1.323415137952715,
            -2.3606779704936933, -1.260157440951084, 0.320891788585604, -0.01001411945357239, -0.2900994773507524, -1.622108659747985,
            -0.4245358075797152, -0.32923956258401127, -0.9389905328927785, 3.863364417623918, 0.003349698445717242,
            0.4121685774090638, 0.33325498907746487, 0.06458473414327098, 1.1584417041878194,
            0.19292427164032416, 0.10399094962739909, 1.1281571171236608, 0.06476485092531535, 1004.2016075923741, 6.143857052439124,
            0.6968844564975664, -2.146366876909819, 1.431759242002924, 0.9824973301554685, 0.7797671333399014,
            0.2071640840562659,
        };

        static void production_default_theta(double *out)
        {
            for (size_t i = 0; i < NUM_PARAMS; ++i)
            {
                out[i] = kProductionDefaultTheta[i];
            }
        }

        static void theta_from_param(Param const &p, double *out)
        {
            out[0] = p.base;         out[1] = p.roof;
            out[2] = p.col_trans;    out[3] = p.row_trans;
            out[4] = p.hole_count;   out[5] = p.hole_line;
            out[6] = p.clear_width;  out[7] = p.wide_2;
            out[8] = p.wide_3;       out[9] = p.wide_4;
            out[10] = p.safe;        out[11] = p.b2b;
            out[12] = p.attack;      out[13] = p.hold_t;
            out[14] = p.hold_i;      out[15] = p.waste_t;
            out[16] = p.waste_i;     out[17] = p.clear_1;
            out[18] = p.clear_2;     out[19] = p.clear_3;
            out[20] = p.clear_4;     out[21] = p.t2_slot;
            out[22] = p.t3_slot;     out[23] = p.tspin_mini;
            out[24] = p.tspin_1;     out[25] = p.tspin_2;
            out[26] = p.tspin_3;     out[27] = p.combo;
            out[28] = p.ratio;
            out[29] = p.cover;       out[30] = p.bump;
            out[31] = p.bump_sq;     out[32] = p.well;
            out[33] = p.height_max;  out[34] = p.height_half;
            out[35] = p.height_quarter;
            out[36] = p.well_use;    out[37] = p.pc_like;
            out[38] = p.pc_attack;
            out[39] = p.garb_cancel;
            out[40] = p.v08_hole;    out[41] = p.v08_well;
            out[42] = p.debt_ratio;
            out[43] = p.feed;
            out[44] = p.no_attack;
        }

        static void theta_to_param(double const *in, Param &p)
        {
            p.base = in[0];          p.roof = in[1];
            p.col_trans = in[2];     p.row_trans = in[3];
            p.hole_count = in[4];    p.hole_line = in[5];
            p.clear_width = in[6];   p.wide_2 = in[7];
            p.wide_3 = in[8];        p.wide_4 = in[9];
            p.safe = in[10];         p.b2b = in[11];
            p.attack = in[12];       p.hold_t = in[13];
            p.hold_i = in[14];       p.waste_t = in[15];
            p.waste_i = in[16];      p.clear_1 = in[17];
            p.clear_2 = in[18];      p.clear_3 = in[19];
            p.clear_4 = in[20];      p.t2_slot = in[21];
            p.t3_slot = in[22];      p.tspin_mini = in[23];
            p.tspin_1 = in[24];      p.tspin_2 = in[25];
            p.tspin_3 = in[26];      p.combo = in[27];
            p.ratio = in[28];
            p.cover = in[29];        p.bump = in[30];
            p.bump_sq = in[31];      p.well = in[32];
            p.height_max = in[33];   p.height_half = in[34];
            p.height_quarter = in[35];
            p.well_use = in[36];     p.pc_like = in[37];
            p.pc_attack = in[38];
            p.garb_cancel = in[39];
            p.v08_hole = in[40];     p.v08_well = in[41];
            p.debt_ratio = in[42];
            p.feed = in[43];
            p.no_attack = in[44];
        }

        static void struct_defaults_theta(double *out)
        {
            Param const p;
            theta_from_param(p, out);
        }

        static bool all_finite(double const *v, size_t n)
        {
            if (v == nullptr)
            {
                return false;
            }
            for (size_t i = 0; i < n; ++i)
            {
                if (!std::isfinite(v[i]))
                {
                    return false;
                }
            }
            return true;
        }
        struct Config
        {
            int const *table;
            int table_max;
            int safe;
            Param param;
        };
        struct Result
        {
            double value;
            int16_t t2_value;
            int16_t t3_value;
            int16_t well_depth;
        };
        struct Status
        {
            int8_t death;
            int8_t combo;
            int8_t under_attack;
            int8_t map_rise;
            int8_t b2b;
            int8_t just_attacked = 0;
            int16_t since_attack = 0;
            int16_t t2_value;
            int16_t t3_value;
            double combo_debt = 0;
            double acc_value;
            double like;
            double value;
            bool operator < (Status const &) const;

            enum class Kind : uint8_t
            {
                T2,
                T3
            };
            struct Values
            {
                int16_t t2 = 0;
                int16_t t3 = 0;
            };

            static void init_t_value(m_tetris::TetrisMap const &m, int16_t &t2_value_ref, int16_t &t3_value_ref, m_tetris::TetrisMap *out_map = nullptr);
            static int t2_readiness(uint32_t row0, uint32_t row1, uint32_t row2, int count0, int count1, int x);
            static int t3a_readiness(uint32_t const *rows, uint8_t const *counts, int y, int hole, int qualifying, int total);
            static int t3b_readiness(uint32_t const *rows, uint8_t const *counts, int y, int hole, int qualifying, int total);
            static void apply_overlay(Kind kind, bool mirrored, int x, int y, int readiness, m_tetris::TetrisMap &map);
            static void fill_counts(m_tetris::TetrisMap const &map, uint8_t *counts);
        };
    public:
        int8_t get_safe(m_tetris::TetrisMap const &m, char t) const;
        void init(m_tetris::TetrisContext const *context, Config const *config);
        std::string ai_name() const;
        double ratio() const
        {
            return config_->param.ratio;
        }
        Result eval(TetrisNodeEx const &node, m_tetris::TetrisMap const &map, m_tetris::TetrisMap const &src_map) const;
        Status get(TetrisNodeEx &node, Result const &eval_result, size_t clear, m_tetris::TetrisMap const &map, size_t depth, Status const & status, m_tetris::TetrisContext::Env const &env) const;
        m_tetris::TetrisBlockStatus spawn(char t, int clear, int spawn_w, int spawn_h, bool is_hold, m_tetris::TetrisMap const &map, Status const &status) const;
    private:
        m_tetris::TetrisContext const *context_;
        Config const *config_;
        int col_mask_, row_mask_;
        mutable size_t feature_observer_count_ = 0;
        mutable size_t transition_observer_count_ = 0;
        struct MapInDangerData
        {
            int data[4];
        };
        std::vector<MapInDangerData> map_danger_data_;
        size_t map_in_danger_(m_tetris::TetrisMap const &map, size_t t, size_t up) const;
    };

    class C2
    {
    public:
        struct Config
        {
            std::array<double, 100> p;
            double p_rate;
            int safe;
            int mode;
            int danger;
            int soft_drop;
        };
        struct Status
        {
            double attack;
            double map;
            size_t combo;
            size_t combo_limit;
            double value;
            bool operator < (Status const &) const;
        };
        struct Result
        {
            double attack;
            double map;
            double fill;
            double hole;
            double new_hole;
            bool soft_drop;
        };
    public:
        void init(m_tetris::TetrisContext const *context, Config const *config);
        std::string ai_name() const;
        Result eval(m_tetris::TetrisNode const *node, m_tetris::TetrisMap const &map, m_tetris::TetrisMap const &src_map) const;
        Status get(m_tetris::TetrisNode const *node, Result const &eval_result, size_t clear, m_tetris::TetrisMap const &map, size_t depth, Status const &status, m_tetris::TetrisContext::Env const &env) const;
        Status iterate(Status const **status, size_t status_length) const;

    private:
        m_tetris::TetrisContext const *context_;
        Config const *config_;
        int col_mask_, row_mask_;
        struct MapInDangerData
        {
            int data[4];
        };
        std::vector<MapInDangerData> map_danger_data_;
        size_t map_in_danger_(m_tetris::TetrisMap const &map) const;
    };

}
