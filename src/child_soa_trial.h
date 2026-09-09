#pragma once
#if defined(TETRIS_CHILD_SOA_TRIAL) && defined(TETRIS_EVAL_INDEX_TRIAL)
#error TETRIS_CHILD_SOA_TRIAL excludes TETRIS_EVAL_INDEX_TRIAL
#endif
#if defined(TETRIS_CHILD_SOA_TRIAL) && defined(TETRIS_EVAL_REUSE_TRACE)
#error TETRIS_CHILD_SOA_TRIAL excludes TETRIS_EVAL_REUSE_TRACE
#endif
#ifdef TETRIS_CHILD_SOA_TRIAL
#include <cstddef>
struct ChildSoaMeta
{
    PolicyState state{};
    Evaluation evaluation{};
    std::size_t cursor = 0;
    HoldState hold{};
    Outcome outcome{};
    NodeId parent = no_node;
    Candidate candidate{};
    Piece played = Piece::T;
    BranchSource source = BranchSource::Current;
    bool expandable = true;
};
struct ChildSoaConstView
{
    Board const &board;
    ChildSoaMeta const &meta;
};
#endif
