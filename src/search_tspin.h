
#pragma once

#include "tetris_core.h"
#include <vector>
#include <cstddef>
#include <span>

namespace search_tspin
{
    class Search
    {
    public:
        enum class TSpinType
        {
            None, TSpin, TSpinMini
        };
        struct Config
        {
            bool allow_rotate_move = false;
            bool allow_180 = true;
            bool allow_d = true;
            bool allow_D = true;
            bool allow_LR = true;
            bool allow_nont_d = false;
            bool is_20g = false;
            bool last_rotate = false;
        };
        struct TetrisNodeWithTSpinType
        {
            TetrisNodeWithTSpinType()
            {
                std::memset(this, 0, sizeof(*this));
            }
            TetrisNodeWithTSpinType(m_tetris::TetrisNode const *_node) : node(_node), last(), type(TSpinType::None), flags()
            {

            }
            m_tetris::TetrisNode const *node;
            m_tetris::TetrisNode const *last;
            TSpinType type;
            union
            {
                struct
                {
                    bool is_check;
                    bool is_last_rotate;
                    bool is_ready;
                    bool is_mini_ready;
                };
                uint32_t flags;
            };
            operator m_tetris::TetrisNode const *() const
            {
                return node;
            }
            m_tetris::TetrisNode const *operator->() const
            {
                return node;
            }
            bool operator == (TetrisNodeWithTSpinType const &other)
            {
                return node == other.node && last == other.last && type == other.type && flags == other.flags;
            }
            bool operator == (std::nullptr_t)
            {
                return node == nullptr;
            }
            bool operator != (std::nullptr_t)
            {
                return node != nullptr;
            }
        };
        void init(m_tetris::TetrisContext const *context, Config const *config);
        std::vector<char> make_path(m_tetris::TetrisNode const *node, TetrisNodeWithTSpinType const &land_point, m_tetris::TetrisMap const &map);
        std::vector<TetrisNodeWithTSpinType> const *search(m_tetris::TetrisMap const &map, m_tetris::TetrisNode const *node, size_t depth);
        TSpinType classify(m_tetris::TetrisMap const &map, m_tetris::TetrisNode const *node, bool last_rotate, size_t clear);
        void cross_check(bool value)
        {
            cross_check_ = value;
        }
        size_t cross_check_count() const
        {
            return cross_check_count_;
        }
        size_t bitboard_executions() const
        {
            return bitboard_executions_;
        }
    private:
        struct KickOffset
        {
            int8_t dcol;
            int8_t drow;
            uint8_t to;
        };
        std::vector<char> make_path_20g(m_tetris::TetrisNode const *node, TetrisNodeWithTSpinType const &land_point, m_tetris::TetrisMap const &map);
        std::vector<TetrisNodeWithTSpinType> const *search_t(m_tetris::TetrisMap const &map, m_tetris::TetrisNode const *node, size_t depth);
        std::vector<TetrisNodeWithTSpinType> const *search_t_bitboard(m_tetris::TetrisMap const &map, m_tetris::TetrisNode const *node, size_t depth);
        void build_t_bitboard_tables();
        bool check_ready(m_tetris::TetrisMap const &map, m_tetris::TetrisNode const *node);
        bool check_mini_ready(m_tetris::TetrisMapSnap const &snap, TetrisNodeWithTSpinType const &node);
        std::vector<TetrisNodeWithTSpinType> land_point_cache_;
        std::vector<m_tetris::TetrisNode const *> node_incomplete_;
        std::vector<m_tetris::TetrisNode const *> node_search_;
        m_tetris::TetrisNodeMark node_mark_;
        m_tetris::TetrisNodeMarkFiltered node_mark_filtered_;
        std::span<uint32_t> block_data_;
        uint32_t block_data_buffer_[52];
        int x_diff_, y_diff_;
        Config const *config_;
        m_tetris::TetrisContext const *context_;
        bool cross_check_ = false;
        bool cross_check_warned_ = false;
        size_t cross_check_count_ = 0;
        size_t bitboard_executions_ = 0;
        std::vector<TetrisNodeWithTSpinType> cross_check_buffer_;
        bool t_tables_ready_ = false;
        bool t_tables_valid_ = false;
        m_tetris::TetrisNode const *t_box_[4][m_tetris::max_height][32] = {};
        uint64_t t_usable_c_[4][32] = {};
        uint64_t t_reach_c_[4][32] = {};
        uint64_t t_rot_c_[4][32] = {};
        uint8_t t_rot_src_r_[4][32][m_tetris::max_height] = {};
        uint8_t t_rot_src_x_[4][32][m_tetris::max_height] = {};
        uint8_t t_rot_src_y_[4][32][m_tetris::max_height] = {};
        uint64_t t_temp_c_[32] = {};
        KickOffset t_kicks_[4][3][m_tetris::max_wall_kick] = {};
        size_t t_kick_count_[4][3] = {};
    };
}
