
#include "tetris_core.h"
#include "search_tspin.h"
#include <array>

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
        typedef search_tspin::Search::TSpinType TSpinType;
        typedef search_tspin::Search::TetrisNodeWithTSpinType TetrisNodeEx;
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
        typedef search_tspin::Search::TSpinType TSpinType;
        typedef search_tspin::Search::TetrisNodeWithTSpinType TetrisNodeEx;
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
        typedef search_tspin::Search::TSpinType TSpinType;
        typedef search_tspin::Search::TetrisNodeWithTSpinType TetrisNodeEx;
        // Diagnostic-only raw features. These are deliberately not part of Result,
        // because Result is cached by the engine's map transposition table.
        struct FeatureSnapshot
        {
            std::array<int16_t, 32> column_height{};
            std::array<int16_t, 32> column_hole_count{};
            int32_t aggregate_height = 0;
            int32_t max_height = 0;
            int32_t center_sum_height = 0;
            int32_t center_max_height = 0;
            int32_t height_range = 0;
            int32_t surface_roughness = 0;
            int32_t max_surface_step = 0;
            int32_t well_cells = 0;
            int32_t deepest_well = 0;
            int32_t hole_count = 0;
            int32_t hole_rows = 0;
            int32_t covered_blocks = 0;
            int32_t max_hole_depth = 0;
            int32_t horizontal_transitions = 0;
            int32_t vertical_transitions = 0;
            int32_t side_height = 0;
            int32_t full_rows = 0;
            int32_t t2_slot = 0;
            int32_t t3_slot = 0;
        };
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
        struct Result;
        struct Status;
        struct TransitionSnapshot;
        using FeatureObserver = void (*)(FeatureSnapshot const &, Result const &, TetrisNodeEx const &,
                                          m_tetris::TetrisMap const &, m_tetris::TetrisMap const &, void *);
        using TransitionObserver = void (*)(TransitionSnapshot const &, Status const &, Status const &,
                                             TetrisNodeEx const &, m_tetris::TetrisMap const &,
                                             m_tetris::TetrisContext::Env const &, void *);
        struct Config
        {
            int const *table;
            int table_max;
            int safe;
            Param param;
            FeatureObserver feature_observer = nullptr;
            void *feature_observer_user = nullptr;
            size_t feature_observer_every = 1;
            TransitionObserver transition_observer = nullptr;
            void *transition_observer_user = nullptr;
            size_t transition_observer_every = 1;
        };
        struct Result
        {
            double value;
            int8_t top_out;
            int16_t count;
            int16_t t2_value;
            int16_t t3_value;
            TSpinType t_spin;
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

            static void init_t_value(m_tetris::TetrisMap const &m, int16_t &t2_value_ref, int16_t &t3_value_ref, m_tetris::TetrisMap *out_map = nullptr);
        };
        struct TransitionSnapshot
        {
            size_t clear = 0;
            size_t depth = 0;
            int attack = 0;
            int t_attack = 0;
            int config_safe = 0;
            int safe = 0;
            int map_rise = 0;
            double like = 0;
            double dislike = 0;
            double t_like = 0;
            double t_dislike = 0;
            double field = 0;
        };
    public:
        int8_t get_safe(m_tetris::TetrisMap const &m, char t) const;
        void init(m_tetris::TetrisContext const *context, Config const *config);
        std::string ai_name() const;
        static FeatureSnapshot feature_snapshot(m_tetris::TetrisMap const &map);
        double ratio() const
        {
            return config_->param.ratio;
        }
        Result eval(TetrisNodeEx const &node, m_tetris::TetrisMap const &map, m_tetris::TetrisMap const &src_map) const;
        Status get(TetrisNodeEx &node, Result const &eval_result, size_t clear, m_tetris::TetrisMap const &map, size_t depth, Status const & status, m_tetris::TetrisContext::Env const &env) const;
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
