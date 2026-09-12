#include "search_tspin.h"
#include "integer_utils.h"

using namespace m_tetris;

namespace search_tspin
{
    void Search::init(TetrisContext const *context, Config const *config)
    {
        context_ = context;
        config_ = config;
        t_tables_ready_ = false;
        t_tables_valid_ = false;
        cross_check_warned_ = false;
        cross_check_count_ = 0;
        node_mark_.init(context->node_max());
        node_mark_filtered_.init(context->node_max());
        block_data_ = std::span(block_data_buffer_);
        std::memset(block_data_buffer_, 0, sizeof block_data_buffer_);
        TetrisNode const *node = context->generate('T');
        if (node != nullptr)
        {
            int bottom = node->row, top = node->row + node->height, left = node->col, right = node->col + node->width;
            TetrisNode const *rotate = node->rotate_clockwise != nullptr ? node->rotate_clockwise : node->rotate_counterclockwise;
            if (rotate != nullptr)
            {
                bottom = std::min<int>(bottom, rotate->row);
                top = std::max<int>(top, rotate->row + rotate->height);
                left = std::min<int>(left, rotate->col);
                right = std::max<int>(right, rotate->col + rotate->width);
                x_diff_ = (right + left) / 2 - node->status.x;
                y_diff_ = (top + bottom) / 2 - node->status.y;
                for (int x = 1; x < context->width() - 1; ++x)
                {
                    block_data_[10 + x - x_diff_] = (1 << (x - 1)) | (1 << (x + 1));
                }
                block_data_[10 - x_diff_] = (1 << 1);
                block_data_[10 + context->width() - 1 - x_diff_] = (1 << (context->width() - 2));
            }
        }
    }

    std::vector<char> Search::make_path(TetrisNode const *node, TetrisNodeWithTSpinType const &land_point, TetrisMap const &map)
    {
        //if (land_point.type != TSpinType::None)
        //{
        //    printf("T-SPIN %d\n", land_point.type);
        //}
        if (config_->is_20g)
        {
            return make_path_20g(node, land_point, map);
        }
        if (land_point.type == TSpinType::None && node->index_filtered == land_point->index_filtered)
        {
            return std::vector<char>();
        }
        bool allow_180 = config_->allow_180;
        bool allow_LR = config_->allow_LR;
        bool allow_D = config_->allow_D;
        bool allow_nont_d = config_->allow_nont_d;
        const int index = land_point.type == TSpinType::None || land_point.last == nullptr ? land_point->index_filtered : land_point.last->index_filtered;
        auto build_path = [&land_point, &map, allow_180, this](TetrisNode const *node, decltype(node_mark_) &node_mark)->std::vector<char>
        {
            size_t node_index = node->index_filtered;
            std::vector<char> path;
            while (true)
            {
                auto result = node_mark.get(node);
                node = result.first;
                if (node == nullptr)
                {
                    break;
                }
                path.push_back(result.second);
            }
            std::reverse(path.begin(), path.end());
            if (node_index != land_point->index_filtered)
            {
                TetrisNode const *last = land_point.last;
                node = land_point.node;
                if (allow_180)
                {
                    //x
                    for (TetrisNode const *wall_kick_node : last->wall_kick_opposite)
                    {
                        if (wall_kick_node)
                        {
                            if (wall_kick_node->check(map))
                            {
                                if (wall_kick_node == node)
                                {
                                    path.push_back('x');
                                    return path;
                                }
                            }
                        }
                        else
                        {
                            break;
                        }
                    }
                }
                //z
                for (TetrisNode const *wall_kick_node : last->wall_kick_counterclockwise)
                {
                    if (wall_kick_node)
                    {
                        if (wall_kick_node->check(map))
                        {
                            if (wall_kick_node == node)
                            {
                                path.push_back('z');
                                return path;
                            }
                        }
                    }
                    else
                    {
                        break;
                    }
                }
                //c
                for (TetrisNode const *wall_kick_node : last->wall_kick_clockwise)
                {
                    if (wall_kick_node)
                    {
                        if (wall_kick_node->check(map))
                        {
                            if (wall_kick_node == node)
                            {
                                path.push_back('c');
                                return path;
                            }
                        }
                    }
                    else
                    {
                        break;
                    }
                }
            }
            return path;
        };
        bool disable_d = land_point.type == TSpinType::None && node->land_point != nullptr && node->low >= map.roof && land_point->open(map);
        while (true)
        {
            node_mark_.clear();
            node_search_.clear();
            node_search_.push_back(node);
            node_mark_.set(node, nullptr, '\0');
            if (node->index_filtered == index || (land_point.type != TSpinType::None && node->index_filtered == land_point->index_filtered && config_->last_rotate))
            {
                return build_path(node, node_mark_);
            }
            size_t cache_index = 0;
            do
            {
                for (size_t max_index = node_search_.size(); cache_index < max_index; ++cache_index)
                {
                    TetrisNode const *node = node_search_[cache_index];
                    if (disable_d)
                    {
                        //D
                        TetrisNode const *node_D = node->drop(map);
                        if (node_mark_.set(node_D, node, 'D') && node_D->index_filtered == index)
                        {
                            return build_path(node_D, node_mark_);
                        }
                    }
                    //x
                    if (allow_180)
                    {
                        for (TetrisNode const *wall_kick_node : node->wall_kick_opposite)
                        {
                            if (wall_kick_node)
                            {
                                if (wall_kick_node->check(map))
                                {
                                    if (node_mark_.set(wall_kick_node, node, 'x'))
                                    {
                                        if (wall_kick_node->index_filtered == index)
                                        {
                                            return build_path(wall_kick_node, node_mark_);
                                        }
                                        else
                                        {
                                            node_search_.push_back(wall_kick_node);
                                        }
                                    }
                                    break;
                                }
                            }
                            else
                            {
                                break;
                            }
                        }
                    }
                    //z
                    for (TetrisNode const *wall_kick_node : node->wall_kick_counterclockwise)
                    {
                        if (wall_kick_node)
                        {
                            if (wall_kick_node->check(map))
                            {
                                if (node_mark_.set(wall_kick_node, node, 'z'))
                                {
                                    if (wall_kick_node->index_filtered == index)
                                    {
                                        return build_path(wall_kick_node, node_mark_);
                                    }
                                    else
                                    {
                                        node_search_.push_back(wall_kick_node);
                                    }
                                }
                                break;
                            }
                        }
                        else
                        {
                            break;
                        }
                    }
                    //c
                    for (TetrisNode const *wall_kick_node : node->wall_kick_clockwise)
                    {
                        if (wall_kick_node)
                        {
                            if (wall_kick_node->check(map))
                            {
                                if (node_mark_.set(wall_kick_node, node, 'c'))
                                {
                                    if (wall_kick_node->index_filtered == index)
                                    {
                                        return build_path(wall_kick_node, node_mark_);
                                    }
                                    else
                                    {
                                        node_search_.push_back(wall_kick_node);
                                    }
                                }
                                break;
                            }
                        }
                        else
                        {
                            break;
                        }
                    }
                    //l
                    if (node->move_left && node_mark_.set(node->move_left, node, 'l') && node->move_left->check(map))
                    {
                        if (node->move_left->index_filtered == index)
                        {
                            return build_path(node->move_left, node_mark_);
                        }
                        else
                        {
                            node_search_.push_back(node->move_left);
                        }
                        if (config_->allow_rotate_move)
                        {
                            TetrisNode const *node_left = node->move_left;
                            //X
                            if (allow_180 && node_left->rotate_opposite && node_mark_.set(node_left->rotate_opposite, node_left, 'X') && node_left->rotate_opposite->check(map))
                            {
                                if (node_left->rotate_opposite->index_filtered == index)
                                {
                                    return build_path(node_left->rotate_opposite, node_mark_);
                                }
                                else
                                {
                                    node_search_.push_back(node_left->rotate_opposite);
                                }
                            }
                            //Z
                            if (node_left->rotate_counterclockwise && node_mark_.set(node_left->rotate_counterclockwise, node_left, 'Z') && node_left->rotate_counterclockwise->check(map))
                            {
                                if (node_left->rotate_counterclockwise->index_filtered == index)
                                {
                                    return build_path(node_left->rotate_counterclockwise, node_mark_);
                                }
                                else
                                {
                                    node_search_.push_back(node_left->rotate_counterclockwise);
                                }
                            }
                            //C
                            if (node_left->rotate_clockwise && node_mark_.set(node_left->rotate_clockwise, node_left, 'C') && node_left->rotate_clockwise->check(map))
                            {
                                if (node_left->rotate_clockwise->index_filtered == index)
                                {
                                    return build_path(node_left->rotate_clockwise, node_mark_);
                                }
                                else
                                {
                                    node_search_.push_back(node_left->rotate_clockwise);
                                }
                            }
                        }
                    }
                    //r
                    if (node->move_right && node_mark_.set(node->move_right, node, 'r') && node->move_right->check(map))
                    {
                        if (node->move_right->index_filtered == index)
                        {
                            return build_path(node->move_right, node_mark_);
                        }
                        else
                        {
                            node_search_.push_back(node->move_right);
                        }
                        if (config_->allow_rotate_move)
                        {
                            TetrisNode const *node_right = node->move_right;
                            //X
                            if (allow_180 && node_right->rotate_opposite && node_mark_.set(node_right->rotate_opposite, node_right, 'X') && node_right->rotate_opposite->check(map))
                            {
                                if (node_right->rotate_opposite->index_filtered == index)
                                {
                                    return build_path(node_right->rotate_opposite, node_mark_);
                                }
                                else
                                {
                                    node_search_.push_back(node_right->rotate_opposite);
                                }
                            }
                            //Z
                            if (node_right->rotate_counterclockwise && node_mark_.set(node_right->rotate_counterclockwise, node_right, 'Z') && node_right->rotate_counterclockwise->check(map))
                            {
                                if (node_right->rotate_counterclockwise->index_filtered == index)
                                {
                                    return build_path(node_right->rotate_counterclockwise, node_mark_);
                                }
                                else
                                {
                                    node_search_.push_back(node_right->rotate_counterclockwise);
                                }
                            }
                            //C
                            if (node_right->rotate_clockwise && node_mark_.set(node_right->rotate_clockwise, node_right, 'C') && node_right->rotate_clockwise->check(map))
                            {
                                if (node_right->rotate_clockwise->index_filtered == index)
                                {
                                    return build_path(node_right->rotate_clockwise, node_mark_);
                                }
                                else
                                {
                                    node_search_.push_back(node_right->rotate_clockwise);
                                }
                            }
                        }
                    }
                    //L
                    if (allow_LR && node->move_left && node->move_left->check(map))
                    {
                        TetrisNode const* node_L = node->move_left;
                        while (node_L->move_left && node_L->move_left->check(map))
                        {
                            node_L = node_L->move_left;
                        }
                        if (node_mark_.set(node_L, node, 'L'))
                        {
                            if (node_L->index_filtered == index)
                            {
                                return build_path(node_L, node_mark_);
                            }
                            else
                            {
                                node_search_.push_back(node_L);
                            }
                        }
                    }
                    //R
                    if (allow_LR && node->move_right && node->move_right->check(map))
                    {
                        TetrisNode const* node_R = node->move_right;
                        while (node_R->move_right && node_R->move_right->check(map))
                        {
                            node_R = node_R->move_right;
                        }
                        if (node_mark_.set(node_R, node, 'R'))
                        {
                            if (node_R->index_filtered == index)
                            {
                                return build_path(node_R, node_mark_);
                            }
                            else
                            {
                                node_search_.push_back(node_R);
                            }
                        }
                    }
                    if (!disable_d)
                    {
                        if (config_->allow_d)
                        {
                            //d
                            if (node->move_down && node_mark_.set(node->move_down, node, 'd') && node->move_down->check(map))
                            {
                                if (node->move_down->index_filtered == index)
                                {
                                    return build_path(node->move_down, node_mark_);
                                }
                                else
                                {
                                    node_search_.push_back(node->move_down);
                                }
                                //D
                                if (allow_D) {
                                    TetrisNode const* node_D = node->drop(map);
                                    if (node_mark_.set(node_D, node, 'D'))
                                    {
                                        if (node_D->index_filtered == index)
                                        {
                                            return build_path(node_D, node_mark_);
                                        }
                                        else
                                        {
                                            node_search_.push_back(node_D);
                                        }
                                    }
                                }
                            }
                        }
                        else
                        {
                            //D
                            if (allow_D) {
                                TetrisNode const* node_D = node->drop(map);
                                if (node_mark_.set(node_D, node, 'D'))
                                {
                                    if (node_D->index_filtered == index)
                                    {
                                        return build_path(node_D, node_mark_);
                                    }
                                    else
                                    {
                                        node_search_.push_back(node_D);
                                    }
                                }
                            }

                        }
                    }
                }
            } while (node_search_.size() > cache_index);
            if (disable_d)
            {
                disable_d = false;
            }
            else
            {
                break;
            }
        }
        return std::vector<char>();
    }

    std::vector<Search::TetrisNodeWithTSpinType> const *Search::search(TetrisMap const &map, TetrisNode const *node, size_t depth)
    {
        land_point_cache_.clear();
        if (!node->check(map))
        {
            return &land_point_cache_;
        }
        bool allow_180 = config_->allow_180;
        bool is_20g = config_->is_20g;
        bool allow_LR = config_->allow_LR;
        bool allow_nont_d = config_->allow_nont_d;
        if (is_20g)
        {
            node = node->drop(map);
        }
        node_mark_.clear();
        node_mark_filtered_.clear();
        node_search_.clear();
        if (node->status.t == 'T')
        {
            if (is_20g)
            {
                return search_t(map, node, depth);
            }
            if (cross_check_)
            {
                if (!t_tables_ready_)
                {
                    build_t_bitboard_tables();
                }
                if (!t_tables_valid_)
                {
                    if (!cross_check_warned_)
                    {
                        std::fprintf(stderr, "CROSS_CHECK unavailable: bitboard tables invalid for this context\n");
                        cross_check_warned_ = true;
                    }
                }
                else if (config_->is_20g)
                {
                    if (!cross_check_warned_)
                    {
                        std::fprintf(stderr, "CROSS_CHECK unavailable: bitboard 20G route not supported\n");
                        cross_check_warned_ = true;
                    }
                }
                else
                {
                search_t_bitboard(map, node, depth);
                cross_check_buffer_.clear();
                cross_check_buffer_.insert(cross_check_buffer_.end(), land_point_cache_.begin(), land_point_cache_.end());
                land_point_cache_.clear();
                search_t(map, node, depth);
                auto key = [](TetrisNodeWithTSpinType const &entry)
                {
                    return std::tuple(entry->index_filtered, static_cast<int>(entry.type), entry.flags, entry->status.t, entry->status.x, entry->status.y, entry->status.r);
                };
                std::vector<std::tuple<size_t, int, uint32_t, char, int, int, int>> left, right;
                for (auto const &entry : cross_check_buffer_) left.push_back(key(entry));
                for (auto const &entry : land_point_cache_) right.push_back(key(entry));
                std::sort(left.begin(), left.end());
                std::sort(right.begin(), right.end());
                ++cross_check_count_;
                if (left != right)
                {
                    std::fprintf(stderr, "CROSS_CHECK MISMATCH: bitboard %zu landings, bfs %zu landings\n", left.size(), right.size());
                    for (size_t i = 0; i < std::max(left.size(), right.size()); ++i)
                    {
                        auto const &l = i < left.size() ? left[i] : std::tuple<size_t, int, uint32_t, char, int, int, int>{};
                        auto const &r = i < right.size() ? right[i] : std::tuple<size_t, int, uint32_t, char, int, int, int>{};
                        if (l != r)
                        {
                            std::fprintf(stderr, "  first diff at %zu: bb(idx %zu type %d flags %u) vs bfs(idx %zu type %d flags %u)\n",
                                i, std::get<0>(l), std::get<1>(l), std::get<2>(l), std::get<0>(r), std::get<1>(r), std::get<2>(r));
                            break;
                        }
                    }
                    std::abort();
                }
                }
            }
            return search_t_bitboard(map, node, depth);
        }
        if (!is_20g && node->land_point != nullptr && node->low >= map.roof && !allow_nont_d)
        {
            for (auto const *land_point_node : *node->land_point)
            {
                TetrisNode const *drop_node = land_point_node->drop(map);
                if (node_mark_filtered_.mark(drop_node))
                {
                    land_point_cache_.push_back(drop_node);
                }
                node_search_.push_back(drop_node);
            }
            TetrisNode const *last_node = nullptr;
            size_t cache_index = node_search_.size();
            for (size_t i = 0; i != cache_index; ++i)
            {
                node = node_search_[i];
                if (last_node != nullptr)
                {
                    if (last_node->status.r == node->status.r && std::abs(last_node->status.x - node->status.x) == 1 && std::abs(last_node->status.y - node->status.y) > 1)
                    {
                        if (last_node->status.y > node->status.y)
                        {
                            TetrisNode const *check_node = (last_node->*(last_node->status.x > node->status.x ? &TetrisNode::move_left : &TetrisNode::move_right))->move_down->move_down;
                            if (node_mark_.mark(check_node))
                            {
                                node_search_.push_back(check_node);
                            }
                        }
                        else
                        {
                            TetrisNode const *check_node = (node->*(node->status.x > last_node->status.x ? &TetrisNode::move_left : &TetrisNode::move_right))->move_down->move_down;
                            if (node_mark_.mark(check_node))
                            {
                                node_search_.push_back(check_node);
                            }
                        }
                    }
                }
                last_node = node;
            }
            do
            {
                for (size_t max_index = node_search_.size(); cache_index < max_index; ++cache_index)
                {
                    node = node_search_[cache_index];
                    if (!node->open(map) && (!node->move_down || !node->move_down->check(map)))
                    {
                        if (node_mark_filtered_.mark(node))
                        {
                            land_point_cache_.push_back(node);
                        }
                    }
                    if (allow_180)
                    {
                        //x
                        for (TetrisNode const *wall_kick_node : node->wall_kick_opposite)
                        {
                            if (wall_kick_node)
                            {
                                if (wall_kick_node->check(map))
                                {
                                    if (node_mark_.mark(wall_kick_node) && !wall_kick_node->open(map))
                                    {
                                        node_search_.push_back(wall_kick_node);
                                    }
                                    break;
                                }
                            }
                            else
                            {
                                break;
                            }
                        }
                    }
                    //z
                    for (TetrisNode const *wall_kick_node : node->wall_kick_counterclockwise)
                    {
                        if (wall_kick_node)
                        {
                            if (wall_kick_node->check(map))
                            {
                                if (node_mark_.mark(wall_kick_node) && !wall_kick_node->open(map))
                                {
                                    node_search_.push_back(wall_kick_node);
                                }
                                break;
                            }
                        }
                        else
                        {
                            break;
                        }
                    }
                    //c
                    for (TetrisNode const *wall_kick_node : node->wall_kick_clockwise)
                    {
                        if (wall_kick_node)
                        {
                            if (wall_kick_node->check(map))
                            {
                                if (node_mark_.mark(wall_kick_node) && !wall_kick_node->open(map))
                                {
                                    node_search_.push_back(wall_kick_node);
                                }
                                break;
                            }
                        }
                        else
                        {
                            break;
                        }
                    }
                    //l
                    if (node->move_left && node_mark_.mark(node->move_left) && !node->move_left->open(map) && node->move_left->check(map))
                    {
                        node_search_.push_back(node->move_left);
                    }
                    //r
                    if (node->move_right && node_mark_.mark(node->move_right) && !node->move_right->open(map) && node->move_right->check(map))
                    {
                        node_search_.push_back(node->move_right);
                    }
                    //d
                    if (node->move_down && node_mark_.mark(node->move_down) && node->move_down->check(map))
                    {
                        node_search_.push_back(node->move_down);
                    }
                }
            } while (node_search_.size() > cache_index);
        }
        else
        {
            node_search_.push_back(node);
            node_mark_.mark(node);
            size_t cache_index = 0;
            do
            {
                for (size_t max_index = node_search_.size(); cache_index < max_index; ++cache_index)
                {
                    node = node_search_[cache_index];
                    if (is_20g)
                    {
                        node = node->drop(map);
                    }
                    if (!node->move_down || !node->move_down->check(map))
                    {
                        if (node_mark_filtered_.mark(node))
                        {
                            land_point_cache_.push_back(node);
                        }
                    }
                    if (allow_180)
                    {
                        //x
                        for (TetrisNode const *wall_kick_node : node->wall_kick_opposite)
                        {
                            if (wall_kick_node)
                            {
                                if (wall_kick_node->check(map))
                                {
                                    if (node_mark_.mark(wall_kick_node))
                                    {
                                        node_search_.push_back(wall_kick_node);
                                    }
                                    break;
                                }
                            }
                            else
                            {
                                break;
                            }
                        }
                    }
                    //z
                    for (TetrisNode const *wall_kick_node : node->wall_kick_counterclockwise)
                    {
                        if (wall_kick_node)
                        {
                            if (wall_kick_node->check(map))
                            {
                                if (node_mark_.mark(wall_kick_node))
                                {
                                    node_search_.push_back(wall_kick_node);
                                }
                                break;
                            }
                        }
                        else
                        {
                            break;
                        }
                    }
                    //c
                    for (TetrisNode const *wall_kick_node : node->wall_kick_clockwise)
                    {
                        if (wall_kick_node)
                        {
                            if (wall_kick_node->check(map))
                            {
                                if (node_mark_.mark(wall_kick_node))
                                {
                                    node_search_.push_back(wall_kick_node);
                                }
                                break;
                            }
                        }
                        else
                        {
                            break;
                        }
                    }
                    //l
                    if (node->move_left && node_mark_.mark(node->move_left) && node->move_left->check(map))
                    {
                        node_search_.push_back(node->move_left);
                    }
                    //r
                    if (node->move_right && node_mark_.mark(node->move_right) && node->move_right->check(map))
                    {
                        node_search_.push_back(node->move_right);
                    }
                    //d
                    if (node->move_down && node_mark_.mark(node->move_down) && node->move_down->check(map))
                    {
                        node_search_.push_back(node->move_down);
                    }
                }
            } while (node_search_.size() > cache_index);
        }
        return &land_point_cache_;
    }


    std::vector<char> Search::make_path_20g(TetrisNode const *node, TetrisNodeWithTSpinType const &land_point, TetrisMap const &map)
    {
        node = node->drop(map);
        if (land_point.type == TSpinType::None && node->index_filtered == land_point->index_filtered)
        {
            return std::vector<char>();
        }
        bool allow_180 = config_->allow_180;
        bool allow_LR = config_->allow_LR;
        bool allow_D = config_->allow_D;
        const int index = land_point.type == TSpinType::None || land_point.last == nullptr ? land_point->index_filtered : land_point.last->index_filtered;
        auto build_path = [&land_point, &map, allow_180, this](TetrisNode const *node, decltype(node_mark_) &node_mark)->std::vector<char>
        {
            size_t node_index = node->index_filtered;
            std::vector<char> path;
            while (true)
            {
                auto result = node_mark.get(node);
                node = result.first;
                if (node == nullptr)
                {
                    break;
                }
                path.push_back(result.second);
            }
            std::reverse(path.begin(), path.end());
            if (node_index != land_point->index_filtered)
            {
                TetrisNode const *last = land_point.last;
                node = land_point.node;
                if (allow_180)
                {
                    //x
                    for (TetrisNode const *wall_kick_node : last->wall_kick_opposite)
                    {
                        if (wall_kick_node)
                        {
                            if (wall_kick_node->check(map))
                            {
                                wall_kick_node = wall_kick_node->drop(map);
                                if (wall_kick_node == node)
                                {
                                    path.push_back('x');
                                    return path;
                                }
                            }
                        }
                        else
                        {
                            break;
                        }
                    }
                }
                //z
                for (TetrisNode const *wall_kick_node : last->wall_kick_counterclockwise)
                {
                    if (wall_kick_node)
                    {
                        if (wall_kick_node->check(map))
                        {
                            wall_kick_node = wall_kick_node->drop(map);
                            if (wall_kick_node == node)
                            {
                                path.push_back('z');
                                return path;
                            }
                        }
                    }
                    else
                    {
                        break;
                    }
                }
                //c
                for (TetrisNode const *wall_kick_node : last->wall_kick_clockwise)
                {
                    if (wall_kick_node)
                    {
                        if (wall_kick_node->check(map))
                        {
                            wall_kick_node = wall_kick_node->drop(map);
                            if (wall_kick_node == node)
                            {
                                path.push_back('c');
                                return path;
                            }
                        }
                    }
                    else
                    {
                        break;
                    }
                }
            }
            return path;
        };
        node_mark_.clear();
        node_search_.clear();
        node_search_.push_back(node);
        node_mark_.set(node, nullptr, '\0');
        if (node->index_filtered == index || (land_point.type != TSpinType::None && node->index_filtered == land_point->index_filtered && config_->last_rotate))
        {
            return build_path(node, node_mark_);
        }
        size_t cache_index = 0;
        do
        {
            for (size_t max_index = node_search_.size(); cache_index < max_index; ++cache_index)
            {
                TetrisNode const *node = node_search_[cache_index];
                TetrisNode const *node_test;
                assert(node == node->drop(map));
                //x
                if (allow_180)
                {
                    for (TetrisNode const *wall_kick_node : node->wall_kick_opposite)
                    {
                        if (wall_kick_node)
                        {
                            if (wall_kick_node->check(map))
                            {
                                wall_kick_node = wall_kick_node->drop(map);
                                if (node_mark_.set(wall_kick_node, node, 'x'))
                                {
                                    if (wall_kick_node->index_filtered == index)
                                    {
                                        return build_path(wall_kick_node, node_mark_);
                                    }
                                    else
                                    {
                                        node_search_.push_back(wall_kick_node);
                                    }
                                }
                                break;
                            }
                        }
                        else
                        {
                            break;
                        }
                    }
                }
                //z
                for (TetrisNode const *wall_kick_node : node->wall_kick_counterclockwise)
                {
                    if (wall_kick_node)
                    {
                        if (wall_kick_node->check(map))
                        {
                            wall_kick_node = wall_kick_node->drop(map);
                            if (node_mark_.set(wall_kick_node, node, 'z'))
                            {
                                if (wall_kick_node->index_filtered == index)
                                {
                                    return build_path(wall_kick_node, node_mark_);
                                }
                                else
                                {
                                    node_search_.push_back(wall_kick_node);
                                }
                            }
                            break;
                        }
                    }
                    else
                    {
                        break;
                    }
                }
                //c
                for (TetrisNode const *wall_kick_node : node->wall_kick_clockwise)
                {
                    if (wall_kick_node)
                    {
                        if (wall_kick_node->check(map))
                        {
                            wall_kick_node = wall_kick_node->drop(map);
                            if (node_mark_.set(wall_kick_node, node, 'c'))
                            {
                                if (wall_kick_node->index_filtered == index)
                                {
                                    return build_path(wall_kick_node, node_mark_);
                                }
                                else
                                {
                                    node_search_.push_back(wall_kick_node);
                                }
                            }
                            break;
                        }
                    }
                    else
                    {
                        break;
                    }
                }
                //l
                if ((node_test = node->move_left) && node_test->check(map) && (node_test = node_test->drop(map), node_mark_.set(node_test, node, 'l')))
                {
                    if (node_test->index_filtered == index)
                    {
                        return build_path(node_test, node_mark_);
                    }
                    else
                    {
                        node_search_.push_back(node_test);
                    }
                }
                //r
                if ((node_test = node->move_right) && node_test->check(map) && (node_test = node_test->drop(map), node_mark_.set(node_test, node, 'r')))
                {
                    TetrisNode const *move_right = node->move_right->drop(map);
                    if (move_right->index_filtered == index)
                    {
                        return build_path(move_right, node_mark_);
                    }
                    else
                    {
                        node_search_.push_back(move_right);
                    }
                }
                //L
                if (allow_LR && node->move_left && node->move_left->check(map))
                {
                    node_test = node->move_left->drop(map);
                    while (node_test->move_left && node_test->move_left->check(map))
                    {
                        node_test = node_test->move_left->drop(map);
                    }
                    if (node_mark_.set(node_test, node, 'L'))
                    {
                        if (node_test->index_filtered == index)
                        {
                            return build_path(node_test, node_mark_);
                        }
                        else
                        {
                            node_search_.push_back(node_test);
                        }
                    }
                }
                //R
                if (allow_LR && node->move_right && node->move_right->check(map))
                {
                    node_test = node->move_right->drop(map);
                    while (node_test->move_right && node_test->move_right->check(map))
                    {
                        node_test = node_test->move_right->drop(map);
                    }
                    if (node_mark_.set(node_test, node, 'R'))
                    {
                        if (node_test->index_filtered == index)
                        {
                            return build_path(node_test, node_mark_);
                        }
                        else
                        {
                            node_search_.push_back(node_test);
                        }
                    }
                }
            }
        } while (node_search_.size() > cache_index);
        return std::vector<char>();
    }

    std::vector<Search::TetrisNodeWithTSpinType> const *Search::search_t(TetrisMap const &map, TetrisNode const *node, size_t depth)
    {
        TetrisMapSnap snap;
        node->build_snap(map, context_, snap);
        bool allow_180 = config_->allow_180;
        bool is_20g = config_->is_20g;
        if (is_20g)
        {
            node = node->drop(map);
        }
        node_search_.push_back(node);
        node_mark_.mark(node);
        node_incomplete_.clear();
        size_t cache_index = 0;
        do
        {
            for (size_t max_index = node_search_.size(); cache_index < max_index; ++cache_index)
            {
                node = node_search_[cache_index];
                if (is_20g)
                {
                    node = node->drop(map);
                }
                if (!node->move_down || !node->move_down->check(snap))
                {
                    if (node_mark_filtered_.mark(node))
                    {
                        node_incomplete_.push_back(node);
                    }
                }
                //d
                if (node->move_down && node_mark_.set(node->move_down, node, ' ') && node->move_down->check(snap))
                {
                    node_search_.push_back(node->move_down);
                }
                //l
                if (node->move_left && node_mark_.set(node->move_left, node, ' ') && node->move_left->check(snap))
                {
                    node_search_.push_back(node->move_left);
                }
                //r
                if (node->move_right && node_mark_.set(node->move_right, node, ' ') && node->move_right->check(snap))
                {
                    node_search_.push_back(node->move_right);
                }
                if (allow_180)
                {
                    //x
                    for (TetrisNode const *wall_kick_node : node->wall_kick_opposite)
                    {
                        if (wall_kick_node)
                        {
                            if (wall_kick_node->check(snap))
                            {
                                                                if (node_mark_.cover_if(wall_kick_node, node, ' ', 'x'))
                                {
                                    node_search_.push_back(wall_kick_node);
                                }
                                break;
                            }
                        }
                        else
                        {
                            break;
                        }
                    }
                }
                //z
                for (TetrisNode const *wall_kick_node : node->wall_kick_counterclockwise)
                {
                    if (wall_kick_node)
                    {
                        if (wall_kick_node->check(snap))
                        {
                            if (node_mark_.cover_if(wall_kick_node, node, ' ', 'z'))
                            {
                                node_search_.push_back(wall_kick_node);
                            }
                            break;
                        }
                    }
                    else
                    {
                        break;
                    }
                }
                //c
                for (TetrisNode const *wall_kick_node : node->wall_kick_clockwise)
                {
                    if (wall_kick_node)
                    {
                        if (wall_kick_node->check(snap))
                        {
                            if (node_mark_.cover_if(wall_kick_node, node, ' ', 'c'))
                            {
                                node_search_.push_back(wall_kick_node);
                            }
                            break;
                        }
                    }
                    else
                    {
                        break;
                    }
                }
            }
        } while (node_search_.size() > cache_index);
        for (auto const &node : node_incomplete_)
        {
            auto last = node_mark_.get(node);
            TetrisNodeWithTSpinType node_ex(node);
            node_ex.last = last.first;
            node_ex.is_check = true;
            node_ex.is_last_rotate = last.second != ' ' || (node_ex.last == nullptr && depth == 0 && config_->last_rotate);
            node_ex.is_ready = check_ready(map, node);
            node_ex.is_mini_ready = check_mini_ready(snap, node_ex);
            land_point_cache_.push_back(node_ex);
        }
        return &land_point_cache_;
    }

    bool Search::check_ready(TetrisMap const &map, TetrisNode const *node)
    {
        int count;
        int y = node->status.y + y_diff_;
        int row = block_data_[10 + node->status.x];
        if (y == 0)
        {
            return zzz::BitCount(map.row[1] & row) + 2 >= 3;
        }
        else
        {
            int x = node->status.x + x_diff_;
            if (x == 0 || x == map.width - 1)
            {
                count = 2;
            }
            else
            {
                count = 0;
            }
            return zzz::BitCount(map.row[y - 1] & row) + zzz::BitCount(map.row[y + 1] & row) + count >= 3;
        }
    }

    bool Search::check_mini_ready(TetrisMapSnap const &snap, TetrisNodeWithTSpinType const &node)
    {
        return node.is_ready && !(node->rotate_opposite && node->rotate_opposite->check(snap) || node->rotate_counterclockwise && node->rotate_counterclockwise->check(snap) || node->rotate_clockwise && node->rotate_clockwise->check(snap));
    }

    Search::TSpinType Search::classify(TetrisMap const &map, TetrisNode const *node, bool last_rotate, size_t clear)
    {
        if (clear > 0 && last_rotate)
        {
            TetrisMapSnap snap;
            node->build_snap(map, context_, snap);
            TetrisNodeWithTSpinType node_ex(node);
            node_ex.is_check = true;
            node_ex.is_last_rotate = true;
            node_ex.is_ready = check_ready(map, node);
            node_ex.is_mini_ready = check_mini_ready(snap, node_ex);
            if (clear == 1 && node_ex.is_mini_ready)
            {
                return TSpinType::TSpinMini;
            }
            else if (node_ex.is_ready)
            {
                return TSpinType::TSpin;
            }
        }
        return TSpinType::None;
    }
}

namespace search_tspin
{
    void Search::build_t_bitboard_tables()
    {
        t_tables_ready_ = true;
        t_tables_valid_ = false;
        int const width = context_->width();
        int const height = context_->height();
        for (int r = 0; r < 4; ++r)
        {
            for (int y = 0; y < m_tetris::max_height; ++y)
            {
                for (int x = 0; x < 32; ++x)
                {
                    t_box_[r][y][x] = nullptr;
                }
            }
            for (int d = 0; d < 3; ++d)
            {
                t_kick_count_[r][d] = 0;
            }
        }
        for (int r = 0; r < 4; ++r)
        {
            for (int sx = -4; sx <= width + 4; ++sx)
            {
                for (int sy = -4; sy <= height + 4; ++sy)
                {
                    TetrisNode const *entry = context_->get('T', static_cast<int8_t>(sx), static_cast<int8_t>(sy), static_cast<uint8_t>(r));
                    if (entry == nullptr)
                    {
                        continue;
                    }
                    int const col = entry->col;
                    int const row = entry->row;
                    if (col < 0 || col >= 32 || row < 0 || row >= m_tetris::max_height)
                    {
                        continue;
                    }
                    t_box_[r][row][col] = entry;
                }
            }
        }
        for (int r = 0; r < 4; ++r)
        {
            TetrisOpertion const op = context_->get_opertion('T', static_cast<uint8_t>(r));
            TetrisNode const *src_ref = context_->get('T', static_cast<int8_t>(width / 2), static_cast<int8_t>(height / 2), static_cast<uint8_t>(r));
            if (src_ref == nullptr)
            {
                return;
            }
            bool (*rot_ops[3])(TetrisNode &, TetrisContext const *) = { op.rotate_counterclockwise, op.rotate_clockwise, op.rotate_opposite };
            TetrisWallKickOpertion const *tables[3] = { &op.wall_kick_counterclockwise, &op.wall_kick_clockwise, &op.wall_kick_opposite };
            uint8_t to_r[3] = { static_cast<uint8_t>(r), static_cast<uint8_t>(r), static_cast<uint8_t>(r) };
            for (int d = 0; d < 3; ++d)
            {
                if (rot_ops[d] != nullptr)
                {
                    TetrisNode copy = *context_->generate('T');
                    if (rot_ops[d](copy, context_))
                    {
                        to_r[d] = copy.status.r;
                    }
                }
            }
            for (int d = 0; d < 3; ++d)
            {
                size_t index = 0;
                if (rot_ops[d] != nullptr)
                {
                    TetrisNode const *dst_ref = context_->get('T', static_cast<int8_t>(width / 2), static_cast<int8_t>(height / 2), to_r[d]);
                    if (dst_ref == nullptr)
                    {
                        return;
                    }
                    t_kicks_[r][d][index].dcol = static_cast<int8_t>(dst_ref->col - src_ref->col);
                    t_kicks_[r][d][index].drow = static_cast<int8_t>(dst_ref->row - src_ref->row);
                    t_kicks_[r][d][index].to = to_r[d];
                    ++index;
                }
                for (size_t i = 0; i < tables[d]->length && index < max_wall_kick; ++i)
                {
                    TetrisNode const *dst_ref = context_->get('T', static_cast<int8_t>(width / 2 + tables[d]->data[i].x), static_cast<int8_t>(height / 2 + tables[d]->data[i].y), to_r[d]);
                    if (dst_ref == nullptr)
                    {
                        continue;
                    }
                    t_kicks_[r][d][index].dcol = static_cast<int8_t>(dst_ref->col - src_ref->col);
                    t_kicks_[r][d][index].drow = static_cast<int8_t>(dst_ref->row - src_ref->row);
                    t_kicks_[r][d][index].to = to_r[d];
                    ++index;
                }
                t_kick_count_[r][d] = index;
            }
        }
        t_tables_valid_ = true;
    }

    std::vector<Search::TetrisNodeWithTSpinType> const *Search::search_t_bitboard(TetrisMap const &map, TetrisNode const *node, size_t depth)
    {
        ++bitboard_executions_;
        land_point_cache_.clear();
        if (!t_tables_ready_)
        {
            build_t_bitboard_tables();
        }
        if (!t_tables_valid_)
        {
            return search_t(map, node, depth);
        }
        TetrisMapSnap snap;
        node->build_snap(map, context_, snap);
        if (config_->is_20g)
        {
            node = node->drop(map);
        }
        int const width = static_cast<int>(context_->width());
        int const height = static_cast<int>(context_->height());
        int const start_r = node->status.r < 4 ? node->status.r : 0;
        int const start_y = node->row;
        int const start_x = node->col;
        if (start_y < 0 || start_y >= height || start_x < 0 || start_x >= width || t_box_[start_r][start_y][start_x] == nullptr)
        {
            return &land_point_cache_;
        }
        int const max_y = height;
        uint64_t const band = max_y >= 64 ? ~0ull : ((1ull << max_y) - 1);
        for (int r = 0; r < 4; ++r)
        {
            for (int x = 0; x < 32; ++x)
            {
                t_usable_c_[r][x] = 0;
                t_reach_c_[r][x] = 0;
                t_rot_c_[r][x] = 0;
            }
            for (int x = 0; x < width; ++x)
            {
                uint64_t mask = 0;
                for (int y = 0; y < max_y; ++y)
                {
                    if (((snap.row[r][y] >> x) & 1) == 0)
                    {
                        mask |= 1ull << y;
                    }
                }
                t_usable_c_[r][x] = mask;
            }
        }
        uint64_t *const u0 = t_usable_c_[0];
        (void)u0;
        t_reach_c_[start_r][start_x] |= 1ull << start_y;

        for (;;)
        {
            bool changed = false;
            for (int r = 0; r < 4; ++r)
            {
                uint64_t *const usable = t_usable_c_[r];
                uint64_t *const reach = t_reach_c_[r];
                for (int x = 0; x < width; ++x)
                {
                    uint64_t seeds = reach[x] & ~(reach[x] << 1);
                    while (seeds != 0)
                    {
                        int const y = std::countr_zero(seeds);
                        seeds &= seeds - 1;
                        uint64_t const below = (1ull << (y + 1)) - 1;
                        uint64_t const blocked = ~usable[x] & below;
                        uint64_t const low = (blocked == 0) ? 0 : (1ull << (64 - std::countl_zero(blocked)));
                        uint64_t const run = ((low == 0 ? below : (below & ~(low - 1))) & usable[x]);
                        if (run & ~reach[x])
                        {
                            reach[x] |= run;
                            changed = true;
                        }
                    }
                }
                for (;;)
                {
                    bool moved = false;
                    for (int x = 0; x < width; ++x)
                    {
                        uint64_t const add = ((x > 0 ? reach[x - 1] : 0) | (x + 1 < width ? reach[x + 1] : 0)) & usable[x];
                        if (add & ~reach[x])
                        {
                            reach[x] |= add;
                            moved = true;
                            changed = true;
                        }
                    }
                    if (!moved)
                    {
                        break;
                    }
                }
            }
            for (int r = 0; r < 4; ++r)
            {
                int const d_max = config_->allow_180 ? 3 : 2;
                for (int d = 0; d < d_max; ++d)
                {
                    size_t const kick_count = t_kick_count_[r][d];
                    if (kick_count == 0)
                    {
                        continue;
                    }
                    for (int x = 0; x < width; ++x)
                    {
                        t_temp_c_[x] = t_reach_c_[r][x];
                    }
                    for (size_t i = 0; i < kick_count; ++i)
                    {
                        KickOffset const &kick = t_kicks_[r][d][i];
                        int const to = kick.to < 4 ? kick.to : r;
                        for (int x = 0; x < width; ++x)
                        {
                            uint64_t const src = t_temp_c_[x];
                            if (src == 0)
                            {
                                continue;
                            }
                            int const tx = x + kick.dcol;
                            if (tx < 0 || tx >= width)
                            {
                                continue;
                            }
                            uint64_t const moved = ((kick.drow >= 0 ? (src << kick.drow) : (src >> -kick.drow)) & band);
                            uint64_t const arrived = moved & t_usable_c_[to][tx];
                            if (arrived == 0)
                            {
                                continue;
                            }
                            {
                                uint64_t newly = arrived & ~t_rot_c_[to][tx];
                                t_rot_c_[to][tx] |= arrived;
                                while (newly != 0)
                                {
                                    int const ty = std::countr_zero(newly);
                                    newly &= newly - 1;
                                    t_rot_src_r_[to][tx][ty] = static_cast<uint8_t>(r);
                                    t_rot_src_x_[to][tx][ty] = static_cast<uint8_t>(x);
                                    t_rot_src_y_[to][tx][ty] = static_cast<uint8_t>(ty - kick.drow);
                                }
                            }
                            if (arrived & ~t_reach_c_[to][tx])
                            {
                                t_reach_c_[to][tx] |= arrived;
                                changed = true;
                            }
                        }
                        if (i + 1 < kick_count)
                        {
                            for (int x = 0; x < width; ++x)
                            {
                                int const bx = x + kick.dcol;
                                uint64_t blocked = 0;
                                if (bx >= 0 && bx < width)
                                {
                                    uint64_t const u = t_usable_c_[to][bx];
                                    blocked = (kick.drow >= 0 ? (u >> kick.drow) : (u << -kick.drow)) & band;
                                }
                                t_temp_c_[x] &= ~blocked;
                            }
                        }
                    }
                }
            }
            if (!changed)
            {
                break;
            }
        }

        for (int r = 0; r < 4; ++r)
        {
            uint64_t const *const usable = t_usable_c_[r];
            uint64_t const *const reach = t_reach_c_[r];
            uint64_t const *const rotated = t_rot_c_[r];
            for (int x = 0; x < width; ++x)
            {
                uint64_t rest = reach[x] & ~(usable[x] << 1);
                while (rest != 0)
                {
                    int const y = std::countr_zero(rest);
                    rest &= rest - 1;
                    TetrisNode const *landing = t_box_[r][y][x];
                    if (landing == nullptr)
                    {
                        continue;
                    }
                    TetrisNode const *last = nullptr;
                    bool const was_rotated = ((rotated[x] >> y) & 1) != 0;
                    if (was_rotated)
                    {
                        uint8_t const src_r = t_rot_src_r_[r][x][y];
                        uint8_t const src_x = t_rot_src_x_[r][x][y];
                        uint8_t const src_y = t_rot_src_y_[r][x][y];
                        if (src_y < m_tetris::max_height && src_x < 32)
                        {
                            last = t_box_[src_r][src_y][src_x];
                        }
                    }
                    if (last == nullptr)
                    {
                        if (y + 1 < max_y && ((reach[x] >> (y + 1)) & 1))
                        {
                            last = t_box_[r][y + 1][x];
                        }
                        else if (x + 1 < width && ((reach[x + 1] >> y) & 1))
                        {
                            last = t_box_[r][y][x + 1];
                        }
                        else if (x > 0 && ((reach[x - 1] >> y) & 1))
                        {
                            last = t_box_[r][y][x - 1];
                        }
                    }
                    bool const is_start = r == start_r && y == start_y && x == start_x;
                    TetrisNodeWithTSpinType node_ex(landing);
                    node_ex.last = last;
                    node_ex.is_check = true;
                    node_ex.is_last_rotate = was_rotated || (is_start && depth == 0 && config_->last_rotate);
                    node_ex.is_ready = check_ready(map, landing);
                    node_ex.is_mini_ready = check_mini_ready(snap, node_ex);
                    land_point_cache_.push_back(node_ex);
                }
            }
        }
        std::sort(land_point_cache_.begin(), land_point_cache_.end(),
            [](TetrisNodeWithTSpinType const &left, TetrisNodeWithTSpinType const &right)
            {
                return left->index_filtered < right->index_filtered;
            });
        return &land_point_cache_;
    }
}
