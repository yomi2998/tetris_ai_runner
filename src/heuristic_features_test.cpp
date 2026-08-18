#include "ai_zzz.h"

#include <cassert>

using ai_zzz::TOJ;
using m_tetris::TetrisMap;

int main()
{
    TetrisMap empty(10, 40);
    TOJ::FeatureSnapshot f = TOJ::feature_snapshot(empty);
    assert(f.aggregate_height == 0);
    assert(f.max_height == 0);
    assert(f.hole_count == 0);
    assert(f.covered_blocks == 0);
    assert(f.surface_roughness == 0);
    assert(f.well_cells == 0);

    TetrisMap stack(10, 40);
    stack.row[0] = 1u << 4;
    stack.row[1] = 1u << 4;
    f = TOJ::feature_snapshot(stack);
    assert(f.column_height[4] == 2);
    assert(f.aggregate_height == 2);
    assert(f.max_height == 2);
    assert(f.hole_count == 0);
    assert(f.covered_blocks == 0);

    TetrisMap hole(10, 40);
    hole.row[0] = 1u << 4;
    hole.row[2] = 1u << 4;
    f = TOJ::feature_snapshot(hole);
    assert(f.column_height[4] == 3);
    assert(f.hole_count == 1);
    assert(f.hole_rows == 1);
    assert(f.covered_blocks == 1);
    assert(f.max_hole_depth == 1);
    assert(f.column_hole_count[4] == 1);

    TetrisMap well(10, 40);
    well.row[0] = (1u << 3) | (1u << 5);
    well.row[1] = (1u << 3) | (1u << 5);
    well.row[2] = (1u << 3) | (1u << 5);
    f = TOJ::feature_snapshot(well);
    assert(f.deepest_well == 3);
    assert(f.well_cells == 3);
    assert(f.surface_roughness == 6);

    TetrisMap full(10, 40);
    full.row[0] = (1u << 10) - 1;
    f = TOJ::feature_snapshot(full);
    assert(f.full_rows == 1);
    assert(f.horizontal_transitions == 2);
    assert(f.vertical_transitions == 20);

    return 0;
}
