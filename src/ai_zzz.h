
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
        };

        static constexpr size_t NUM_PARAMS = 29;
        static constexpr double kProductionDefaultTheta[NUM_PARAMS] = {
            10.507166148, 7.539860726, 13.048099725, 13.388476179, 6.728747539, 9.476881786,
            0.258534525, -0.108269503, 4.394241496, -4.892359035, 0.049148374, 1.586714505,
            8.885878229, -0.006001836, -0.004336234, -2.021765056, -0.951446468, -1.145468832,
            -1.515758227, -0.612910192, -0.476031978, 0.009596827, -0.399212013, -0.855819915,
            -0.418779377, -0.454784178, -1.417493065, 1.050941751, 0.756272086,
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
        };
        struct Status
        {
            int8_t death;
            int8_t combo;
            int8_t under_attack;
            int8_t map_rise;
            int8_t b2b;
            int16_t t2_value;
            int16_t t3_value;
            double acc_value;
            double like;
            double value;
            bool operator < (Status const &) const;

            enum class Kind : uint8_t
            {
                T2,
                T3A,
                T3B
            };
            struct Values
            {
                int16_t t2 = 0;
                int16_t t3 = 0;
            };
            struct DescriptorSet
            {
                std::array<uint32_t, 120> data;
                uint8_t count = 0;
            };
            struct SlotAnalysis
            {
                DescriptorSet slots;
                Values legacy;
            };

            static void init_t_value(m_tetris::TetrisMap const &m, int16_t &t2_value_ref, int16_t &t3_value_ref, m_tetris::TetrisMap *out_map = nullptr);
            static uint32_t pack(uint8_t x, uint8_t y, Kind kind, uint8_t readiness);
            static uint8_t descriptor_x(uint32_t descriptor);
            static uint8_t descriptor_y(uint32_t descriptor);
            static Kind descriptor_kind(uint32_t descriptor);
            static uint8_t descriptor_readiness(uint32_t descriptor);
            static int t2_readiness(uint32_t row0, uint32_t row1, uint32_t row2, int count0, int count1, int x);
            static int t3a_readiness(uint32_t const *rows, uint8_t const *counts, int y, int x, int qualifying, int total);
            static int t3b_readiness(uint32_t const *rows, int y, int x, int qualifying, int total);
            static void apply_overlay(Kind kind, int x, int y, int readiness, m_tetris::TetrisMap &map);
            static void fill_counts(m_tetris::TetrisMap const &map, uint8_t *counts);
            static Values reduce_legacy_with_counts(m_tetris::TetrisMap const &map, m_tetris::TetrisMap *out_map, uint8_t const *counts);
            static Values reduce_legacy(m_tetris::TetrisMap const &map, m_tetris::TetrisMap *out_map);
            static DescriptorSet enumerate_descriptors_with_counts(m_tetris::TetrisMap const &map, uint8_t const *counts);
            static DescriptorSet enumerate_descriptors(m_tetris::TetrisMap const &map);
            static SlotAnalysis analyze(m_tetris::TetrisMap const &map, m_tetris::TetrisMap *out_map);
            static Values decode(DescriptorSet const &slots, m_tetris::TetrisMap *out_map);
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
