#pragma once

#include "tetris_board.h"
#include "tetris_types.h"

#ifdef TETRIS_ROW_FUSION_TRIAL
#include "row_fusion_trial.h"
#endif

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace toj_policy
{
    using Board = tetris::Board;
    using Piece = tetris::Piece;
    using Placement = tetris::Placement;
    using Candidate = tetris::Candidate;
    using Outcome = tetris::Outcome;
    using DecisionContext = tetris::DecisionContext;

    inline constexpr int policy_height = 40;
    inline constexpr int spawn_frame_height = 22;
    inline constexpr int lockout_row = 20;

    struct Parameters
    {
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

        static constexpr std::size_t count = 29;

        static Parameters production_defaults();

        static void to_theta(Parameters const &parameters, double *out);

        static void from_theta(double const *in, Parameters &parameters);
    };

    struct Config
    {
        int const *combo_table = nullptr;
        int combo_table_max = 0;
        int safe = 0;
        Parameters parameters;
    };

    struct State
    {
        int8_t death = 0;
        int8_t combo = 0;
        int8_t under_attack = 0;
        int8_t map_rise = 0;
        int8_t b2b = 0;
        int16_t t2_value = 0;
        int16_t t3_value = 0;
        double acc_value = 0;
        double like = 0;
        double value = 0;
    };

    struct Evaluation
    {
        double value = 0;
        int16_t t2_value = 0;
        int16_t t3_value = 0;
    };

    class Policy
    {
    public:
        void init(Config const *config);

        Evaluation evaluate(Board const &result
#ifdef TETRIS_ROW_FUSION_TRIAL
            , RowFusionSafeInputs const *safe_in = nullptr, int *safe_out = nullptr
#endif
        ) const;

        State transition(Piece piece, Candidate candidate, Outcome outcome, Board const &result,
            State const &parent, DecisionContext const &context,
            Evaluation const &evaluation) const;

        State transition_known_lockout(Piece piece, Candidate candidate, Outcome outcome,
            Board const &result, State const &parent, DecisionContext const &context,
            Evaluation const &evaluation, bool lockout, int t_expect
#ifdef TETRIS_ROW_FUSION_TRIAL
            , int const *supplied_safe = nullptr
#endif
        ) const;

        static int expected_t_distance(DecisionContext const &context);

        static bool is_lockout(Piece piece, Placement placement);

        int8_t safe_margin(Board const &board, Piece next) const;

#ifdef TETRIS_ROW_FUSION_TRIAL
        int row_fusion_overlay_witness_for_test(Board const &result, Piece next,
            int *clean_out, int *post_out) const;
#endif

        std::uint32_t danger_bits(Piece piece, int slot) const;

    private:
        Config const *config_ = nullptr;
        std::array<std::array<std::uint32_t, 4>, 7> danger_ = {};

        int scan_safe_rows(std::uint32_t const *rows, Piece next) const;
    };
}
