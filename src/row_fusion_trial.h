#pragma once
#if defined(TETRIS_ROW_FUSION_TRIAL) && defined(TETRIS_DIRECT_KEY_TRIAL)
#error TETRIS_ROW_FUSION_TRIAL excludes TETRIS_DIRECT_KEY_TRIAL
#endif
#if defined(TETRIS_ROW_FUSION_TRIAL) && defined(TETRIS_CHILD_SOA_TRIAL)
#error TETRIS_ROW_FUSION_TRIAL excludes TETRIS_CHILD_SOA_TRIAL
#endif
#if defined(TETRIS_ROW_FUSION_TRIAL) && defined(TETRIS_EVAL_INDEX_TRIAL)
#error TETRIS_ROW_FUSION_TRIAL excludes TETRIS_EVAL_INDEX_TRIAL
#endif
#if defined(TETRIS_ROW_FUSION_TRIAL) && defined(TETRIS_EVAL_REUSE_TRACE)
#error TETRIS_ROW_FUSION_TRIAL excludes TETRIS_EVAL_REUSE_TRACE
#endif
#ifdef TETRIS_ROW_FUSION_TRIAL
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include "tetris_types.h"

struct RowFusionSafeInputs
{
    bool lockout = false;
    bool has_next = false;
    tetris::Piece next = tetris::Piece::I;
};

static_assert(std::is_aggregate_v<RowFusionSafeInputs>,
    "row fusion safe inputs stay a trivial aggregate");
static_assert(sizeof(RowFusionSafeInputs) <= 8,
    "row fusion safe inputs stay within one word");

inline std::size_t rf_eval_exports = 0;
inline std::size_t rf_legacy_exports = 0;
inline std::size_t rf_lockout_skips = 0;
inline std::size_t rf_row_reads = 0;
inline std::size_t rf_fused_children = 0;
inline std::size_t rf_legacy_children = 0;
inline std::size_t rf_source_mismatches = 0;

inline void row_fusion_reset_counters()
{
    rf_eval_exports = 0;
    rf_legacy_exports = 0;
    rf_lockout_skips = 0;
    rf_row_reads = 0;
    rf_fused_children = 0;
    rf_legacy_children = 0;
    rf_source_mismatches = 0;
}
#endif
