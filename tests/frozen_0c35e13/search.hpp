#pragma once
#include "block.hpp"
#include "utils.hpp"
#include <tuple>
#include <array>
#include <span>
#include <algorithm>

namespace reachability::search {
    // Search configuration — passed as runtime parameter to binary_bfs
    struct search_config {
        bool allow_180 = true;
        bool allow_softdrop = true;
        bool allow_sonicdrop = true;
        bool allow_20g = false;
    };

    template <Wrap<mino_p> auto mino, typename board_t>
    constexpr board_t usable_positions(board_t data) {
        board_t positions = ~board_t();
        static_for<std::tuple_size_v<decltype(mino)>>([&] [[gnu::always_inline]] (auto i) {
            constexpr auto move = mino[i];
            constexpr int x = move[0_szc], y = move[1_szc];
            if constexpr (y > 0) {
                positions &= (~data.template move<coord{0, -y}>()).template move<coord{-x, 0}>();
            } else {
                positions &= (~data).template move<-move>();
            }
        });
        return positions;
    }

    template <typename board_t>
    constexpr board_t landable_positions(board_t usable) {
        return usable & ~usable.template move<coord{0, 1}>();
    }

    template <typename board_t>
    constexpr bool consecutive_at(int height, board_t usable) {
        if (height >= int(board_t::height)) {
            return 1;
        }
        using under_t = std::remove_const_t<std::remove_pointer_t<decltype(usable.raw())>>;
        constexpr int L = board_t::lines_per_under;
        constexpr int W = board_t::width;
        const int block = height / L;
        const int row_in_block = height % L;
        const under_t word = usable.raw()[block];
        constexpr under_t col_W1 = [] {
            under_t m = 0;
            for (int j = 0; j < L; ++j) m |= under_t(1) << (W - 1 + W * j);
            return m;
        }();
        constexpr under_t col_0 = [] {
            under_t m = 0;
            for (int j = 0; j < L; ++j) m |= under_t(1) << (W * j);
            return m;
        }();
        under_t s1 = word | col_W1;
        s1 &= s1 - col_0;
        under_t s2 = s1 | col_W1;
        s2 &= s2 - col_0;
        const under_t res = (s1 ^ word) & ~s2;
        return (res >> (W - 1 + W * row_in_block)) & 1;
    }

    template <Wrap<mino_p> auto mino_from, Wrap<mino_p> auto mino_to, coord d, typename board_t>
    constexpr board_t move_usable(board_t data) {
        constexpr int dx = d[0_szc];
        constexpr bool need_mask = [] {
            constexpr auto range_from = mino_range<mino_from>();
            constexpr auto range_to = mino_range<mino_to>();
            if constexpr (dx == 0) {
                return false;
            } else if constexpr (dx > 0) {
                return range_from[2] - dx - range_to[0] < 0;
            } else {
                return range_to[2] + dx - range_from[0] < 0;
            }
        }();
        return data.template move<d, need_mask>();
    }

    template <Wrap<mino_p> auto mino, typename board_t>
    constexpr board_t drop_to_bottom(board_t positions, board_t usable) {
        board_t result = positions;
        while (true) {
            auto moved = move_usable<mino, mino, coord{0, -1}>(result);
            auto valid = moved & usable;
            if (result.contains(valid))
                break;
            auto moved_up = move_usable<mino, mino, coord{0, 1}>(valid);
            auto stayed = result & ~moved_up;
            result = stayed | valid;
        }
        return result;
    }

    template <bool check_consecutive, typename board_t>
    constexpr board_t direct_reachable(coord start, board_t usable) {
        const auto current = usable & usable.template move<coord{0, -1}>();
        const auto covered = usable & ~current;
        const auto expandable = can_expand(current, covered);
        auto whole_line_usable = (expandable | ~covered.get_heads()).all_bits().populate_highest_bit();
        int removed_lines = board_t::height - start[1_szc];
        if (removed_lines > 0) {
            whole_line_usable |= ~board_t::full_lines_of(start[1_szc]);
        }
        auto good_lines = whole_line_usable.remove_ones_after_zero();
        if constexpr (check_consecutive) {
            if (removed_lines > 1) {
                good_lines &= board_t::full_lines_of(start[1_szc] + 1);
            }
            if (!consecutive_at(start[1_szc], usable)) {
                auto ret = board_t();
                ret.set(start[0_szc], start[1_szc]);
                return ret;
            }
        }
        return good_lines & usable;
    }

    template <auto b>
        requires block_spec<decltype(b)>
    constexpr int downmost_position = [] {
        std::array<int, b.orientations> ys{};
        static_for<b.orientations>([&](auto i) {
            constexpr auto diff = b.mino_index[i];
            constexpr auto mino = b.minos[index_c<diff[0_szc]>];
            constexpr auto range = mino_range<mino>();
            ys[i] = range[1] + diff[1_szc][1_szc];
        });
        return *std::min_element(ys.begin(), ys.end());
    }();

    template <auto b>
        requires block_spec<decltype(b)>
    constexpr int upmost_position = [] {
        std::array<int, b.orientations> ys{};
        static_for<b.orientations>([&](auto i) {
            constexpr auto diff = b.mino_index[i];
            constexpr auto mino = b.minos[index_c<diff[0_szc]>];
            constexpr auto range = mino_range<mino>();
            ys[i] = range[3] + diff[1_szc][1_szc];
        });
        return *std::max_element(ys.begin(), ys.end());
    }();

    template <auto b>
        requires block_spec<decltype(b)>
    constexpr int leftmost_position = [] {
        std::array<int, b.orientations> xs{};
        static_for<b.orientations>([&](auto i) {
            constexpr auto diff = b.mino_index[i];
            constexpr auto mino = b.minos[index_c<diff[0_szc]>];
            constexpr auto range = mino_range<mino>();
            xs[i] = range[0] + diff[1_szc][0_szc];
        });
        return *std::min_element(xs.begin(), xs.end());
    }();

    template <auto b>
        requires block_spec<decltype(b)>
    constexpr int rightmost_position = [] {
        std::array<int, b.orientations> xs{};
        static_for<b.orientations>([&](auto i) {
            constexpr auto diff = b.mino_index[i];
            constexpr auto mino = b.minos[index_c<diff[0_szc]>];
            constexpr auto range = mino_range<mino>();
            xs[i] = range[2] + diff[1_szc][0_szc];
        });
        return *std::max_element(xs.begin(), xs.end());
    }();

    template <auto b>
        requires block_spec<decltype(b)>
    constexpr int max_width = [] {
        int w = 0;
        static_for<b.orientations>([&](auto i) {
            constexpr auto diff = b.mino_index[i];
            constexpr auto mino = b.minos[index_c<diff[0_szc]>];
            constexpr auto range = mino_range<mino>();
            int rot_w = range[2] - range[0] + 1;
            if (rot_w > w)
                w = rot_w;
        });
        return w;
    }();

    template <auto b>
        requires block_spec<decltype(b)>
    constexpr int max_height = [] {
        int h = 0;
        static_for<b.orientations>([&](auto i) {
            constexpr auto diff = b.mino_index[i];
            constexpr auto mino = b.minos[index_c<diff[0_szc]>];
            constexpr auto range = mino_range<mino>();
            int rot_h = range[3] - range[1] + 1;
            if (rot_h > h)
                h = rot_h;
        });
        return h;
    }();

    template <auto block, bool check_consecutive, typename board_t>
        requires block_spec<decltype(block)>
    [[gnu::always_inline]] constexpr std::array<board_t, block.shapes> binary_bfs_impl(
        board_t (&usable)[block.shapes],
        std::array<board_t, block.orientations>& cache,
        std::array<bool, block.orientations>& need_visit,
        std::array<board_t, block.orientations>* out_cache,
        const search_config& cfg) {
        constexpr int orientations = block.orientations;
        constexpr int shapes = block.shapes;
        bool allow_float = cfg.allow_softdrop && !cfg.allow_20g;
        bool sonicdrop_mode = cfg.allow_sonicdrop || cfg.allow_20g;
        const auto dump_cache = [](auto* dst, auto& src) {
            if (dst) {
                constexpr auto N = std::tuple_size_v<std::remove_reference_t<decltype(src)>>;
                static_for<N>([&](auto i) { (*dst)[i] = src[i]; });
            }
        };
        const auto quick_check = [&] [[gnu::always_inline]] () {
            bool found_all = true;
            std::array<board_t, shapes> ret;
            static_for<orientations>([&] [[gnu::always_inline]] (auto i) {
                constexpr auto index = block.mino_index[i][0_szc];
                ret[index] |= cache[i];
            });
            static_for<shapes>([&] [[gnu::always_inline]] (auto i) {
                const auto landable = landable_positions(usable[i]);
                found_all = found_all && ret[i].contains(landable);
                ret[i] &= landable;
            });
            return std::pair{found_all, ret};
        };
        if (!cfg.allow_20g || allow_float) {
            for (bool updated = true; updated;) [[unlikely]] {
                auto [found_all, ret] = quick_check();
                if (found_all) {
                    dump_cache(out_cache, cache);
                    return ret;
                }
                updated = false;
                static_for<orientations>([&] [[gnu::always_inline]] (auto i) {
                    if (!need_visit[i]) {
                        return;
                    }
                    constexpr auto index = index_c<block.mino_index[i][0_szc]>;
                    need_visit[i] = false;
                    while (true) {
                        board_t result{};
                        if (allow_float) {
                            constexpr auto moves = std::array{coord{-1, 0}, coord{1, 0}, coord{0, -1}};
                            static_for<moves.size()>([&](auto j) {
                                result |= move_usable<block.minos[index], block.minos[index], moves[j]>(cache[i]);
                            });
                        } else {
                            constexpr auto moves = std::array{coord{-1, 0}, coord{1, 0}};
                            static_for<moves.size()>([&](auto j) {
                                result |= move_usable<block.minos[index], block.minos[index], moves[j]>(cache[i]);
                            });
                        }
                        result &= usable[index];
                        if (cache[i].contains(result)) [[unlikely]] {
                            break;
                        }
                        cache[i] |= result;
                    }
                    static_for<std::tuple_size_v<decltype(block.kicks)>>([&] [[gnu::always_inline]] (auto j) {
                        constexpr auto this_kick = block.kicks[j];
                        constexpr auto diff = this_kick[0_szc];
                        constexpr auto kick_table = this_kick[1_szc];
                        if constexpr (diff[0_szc] != i) {
                            return;
                        } else if constexpr ((diff[0_szc] + 2) % 4 == diff[1_szc]) {
                            if (!cfg.allow_180) {
                                return;
                            }
                        } else {
                            constexpr auto target = index_c<diff[1_szc]>;
                            static_assert(target != i);
                            board_t to;
                            constexpr auto index2 = index_c<block.mino_index[target][0_szc]>;
                            board_t temp = cache[i];
                            static_for<std::tuple_size_v<decltype(kick_table)>>([&] [[gnu::always_inline]] (auto k) {
                                to |= move_usable<block.minos[index], block.minos[index2], kick_table[k]>(temp);
                                temp &= ~move_usable<block.minos[index2], block.minos[index], -kick_table[k]>(usable[index2]);
                            });
                            to &= usable[index2];
                            if (!cache[target].contains(to)) {
                                need_visit[target] = true;
                                if constexpr (target < i)
                                    updated = true;
                            }
                            cache[target] |= to;
                        }
                    });
                });
            }
        }
        if (!allow_float) {
            static_for<orientations>([&] [[gnu::always_inline]] (auto i) {
                constexpr auto index = index_c<block.mino_index[i][0_szc]>;
                cache[i] = drop_to_bottom<block.minos[index]>(cache[i], usable[index]);
            });
            if (!sonicdrop_mode) {
                dump_cache(out_cache, cache);
            } else {
                need_visit.fill(true);
                for (bool updated = true; updated;) [[unlikely]] {
                    auto [found_all, ret] = quick_check();
                    if (found_all) {
                        dump_cache(out_cache, cache);
                        return ret;
                    }
                    updated = false;
                    static_for<orientations>([&] [[gnu::always_inline]] (auto i) {
                        if (!need_visit[i])
                            return;
                        constexpr auto index = index_c<block.mino_index[i][0_szc]>;
                        need_visit[i] = false;
                        while (true) {
                            board_t result{};
                            if (allow_float) {
                                constexpr auto moves = std::array{coord{-1, 0}, coord{1, 0}, coord{0, -1}};
                                static_for<moves.size()>([&](auto j) {
                                    result |= move_usable<block.minos[index], block.minos[index], moves[j]>(cache[i]);
                                });
                            } else {
                                constexpr auto moves = std::array{coord{-1, 0}, coord{1, 0}};
                                static_for<moves.size()>([&](auto j) {
                                    result |= move_usable<block.minos[index], block.minos[index], moves[j]>(cache[i]);
                                });
                            }
                            result &= usable[index];
                            result = drop_to_bottom<block.minos[index]>(result, usable[index]);
                            if (cache[i].contains(result)) [[unlikely]]
                                break;
                            cache[i] |= result;
                        }
                        static_for<std::tuple_size_v<decltype(block.kicks)>>([&] [[gnu::always_inline]] (auto j) {
                            constexpr auto this_kick = block.kicks[j];
                            constexpr auto diff = this_kick[0_szc];
                            constexpr auto kick_table = this_kick[1_szc];
                            if constexpr (diff[0_szc] != i)
                                return;
                            else if (!cfg.allow_180 && ((diff[0_szc] + 2) % 4 == diff[1_szc]))
                                return;
                            else {
                                constexpr auto target = index_c<diff[1_szc]>;
                                static_assert(target != i);
                                board_t to;
                                constexpr auto index2 = index_c<block.mino_index[target][0_szc]>;
                                board_t temp = cache[i];
                                static_for<std::tuple_size_v<decltype(kick_table)>>([&] [[gnu::always_inline]] (auto k) {
                                    to |= move_usable<block.minos[index], block.minos[index2], kick_table[k]>(temp);
                                    temp &= ~move_usable<block.minos[index2], block.minos[index], -kick_table[k]>(usable[index2]);
                                });
                                to &= usable[index2];
                                to = drop_to_bottom<block.minos[index2]>(to, usable[index2]);
                                if (!cache[target].contains(to)) {
                                    need_visit[target] = true;
                                    if constexpr (target < i)
                                        updated = true;
                                }
                                cache[target] |= to;
                            }
                        });
                    });
                }
                dump_cache(out_cache, cache);
            }
        }
        if (allow_float) {
            dump_cache(out_cache, cache);
        }
        auto [_, ret] = quick_check();
        return ret;
    }

    template <auto block, bool check_consecutive = true, typename board_t>
        requires block_spec<decltype(block)>
    constexpr std::array<board_t, block.shapes> binary_bfs(board_t data, const search_config& cfg, coord start, unsigned init_rot, std::array<board_t, block.orientations>* out_cache = nullptr) {
        constexpr int orientations = block.orientations;
        constexpr int shapes = block.shapes;
        board_t usable[shapes];
        static_for<shapes>([&] [[gnu::always_inline]] (auto i) {
            usable[i] = usable_positions<block.minos[i]>(data);
        });
        // Resolve start position for the given init_rot at runtime
        unsigned init_shape = 0;
        coord offset{0, 0};
        static_for<orientations>([&] [[gnu::always_inline]] (auto i) {
            if ((unsigned)i == init_rot) [[likely]] {
                constexpr auto entry = block.mino_index[i];
                init_shape = entry[0_szc];
                offset = entry[1_szc];
            }
        });
        int sx = start[0_szc] + offset[0_szc];
        int sy = std::min(start[1_szc] + offset[1_szc], int(board_t::height) - 1);
        if (!usable[init_shape].get(sx, sy)) [[unlikely]] {
            if (out_cache)
                out_cache->fill({});
            return {};
        }
        std::array<bool, orientations> need_visit{};
        std::array<board_t, orientations> cache;
        if (!cfg.allow_softdrop) {
            static_for<orientations>([&] [[gnu::always_inline]] (auto i) {
                constexpr auto entry = block.mino_index[i];
                constexpr auto entry_off = entry[1_szc];
                int tx = start[0_szc] + entry_off[0_szc];
                int ty = std::min(start[1_szc] + entry_off[1_szc], int(board_t::height) - 1);
                unsigned shape_i = entry[0_szc];
                if (usable[shape_i].get(tx, ty)) {
                    board_t start_pos{};
                    start_pos.set(tx, ty);
                    cache[i] = start_pos;
                    need_visit[i] = true;
                }
            });
        } else if (cfg.allow_20g) {
            static_for<orientations>([&] [[gnu::always_inline]] (auto i) {
                constexpr auto entry = block.mino_index[i];
                constexpr auto entry_off = entry[1_szc];
                constexpr auto shape_idx = index_c<entry[0_szc]>;
                int tx = start[0_szc] + entry_off[0_szc];
                int ty = std::min(start[1_szc] + entry_off[1_szc], int(board_t::height) - 1);
                if (usable[shape_idx].get(tx, ty)) {
                    board_t start_pos{};
                    start_pos.set(tx, ty);
                    cache[i] = drop_to_bottom<block.minos[shape_idx]>(start_pos, usable[shape_idx]);
                    need_visit[i] = true;
                }
            });
        } else {
            bool all_found = true;
            static_for<orientations>([&](auto i) {
                constexpr auto entry = block.mino_index[i];
                unsigned shape_i = entry[0_szc];
                coord entry_off = entry[1_szc];
                int tx = start[0_szc] + entry_off[0_szc];
                int ty = std::min(start[1_szc] + entry_off[1_szc], int(board_t::height) - 1);
                if (!usable[shape_i].get(tx, ty))
                    all_found = false;
            });
            if (all_found) {
                need_visit.fill(true);
                static_for<orientations>([&](auto i) {
                    constexpr auto entry = block.mino_index[i];
                    unsigned shape_i = entry[0_szc];
                    coord entry_off = entry[1_szc];
                    coord this_start{start[0_szc] + entry_off[0_szc], std::min(start[1_szc] + entry_off[1_szc], int(board_t::height) - 1)};
                    cache[i] = direct_reachable<check_consecutive>(this_start, usable[shape_i]);
                });
            } else {
                board_t start_pos{};
                start_pos.set(sx, sy);
                cache[init_rot] = start_pos;
                need_visit[init_rot] = true;
            }
        }
        return binary_bfs_impl<block, check_consecutive>(usable, cache, need_visit, out_cache, cfg);
    }

    template <typename RS, bool check_consecutive = true, typename board_t>
    constexpr static_vector<board_t, 4> binary_bfs(board_t data, const search_config& cfg, typename RS::piece_type b, coord start, unsigned init_rot) {
        return call_with_block<RS>(b, [=]<block B>() {
            auto ret = binary_bfs<B, check_consecutive>(data, cfg, start, init_rot);
            return static_vector<board_t, 4>{std::span{ret}};
        });
    }

    template <auto B, typename board_t>
        requires block_spec<decltype(B)>
    struct move_checker {
        static constexpr int orientations = B.orientations;
        static constexpr int shapes = B.shapes;
        static constexpr std::array<int, orientations> shape_for_rot = [] {
            std::array<int, orientations> arr{};
            static_for<orientations>([&](auto i) {
                arr[i] = std::get<0>(std::get<i>(B.mino_index));
            });
            return arr;
        }();
        std::array<board_t, orientations> cache;
        std::array<board_t, shapes> usable;

        constexpr move_checker(const std::array<board_t, orientations>& c, const board_t& board)
            : cache(c) {
            static_for<shapes>([&](auto i) {
                usable[i] = usable_positions<B.minos[i]>(board);
            });
        }

        template <bool check_consecutive = true>
        constexpr move_checker(const board_t& board, const search_config& cfg, coord start, unsigned init_rot)
            : cache{} {
            static_for<shapes>([&](auto i) {
                usable[i] = usable_positions<B.minos[i]>(board);
            });
            binary_bfs<B, check_consecutive>(board, cfg, start, init_rot, &cache);
        }

        constexpr bool is_valid(int rot, int x, int y) const {
            return usable[shape_for_rot[rot]].get(x, y);
        }

        constexpr bool can_move_left(int rot, int x, int y) const {
            if (x <= 0)
                return false;
            return is_valid(rot, x - 1, y);
        }

        constexpr bool can_move_right(int rot, int x, int y) const {
            if (x + 1 >= int(board_t::width))
                return false;
            return is_valid(rot, x + 1, y);
        }

        constexpr bool can_move_up(int rot, int x, int y) const {
            if (y + 1 >= int(board_t::height))
                return false;
            return is_valid(rot, x, y + 1);
        }

        constexpr bool can_move_down(int rot, int x, int y) const {
            if (y <= 0)
                return false;
            return is_valid(rot, x, y - 1);
        }

        struct rotate_result {
            int rot;
            int x;
            int y;
        };

        constexpr rotate_result try_rotate(int rot_from, int rot_to, int x, int y) const {
            if (rot_from == rot_to)
                return {rot_from, x, y};

            int new_x = x, new_y = y;
            bool found = false;

            static_for<std::tuple_size_v<decltype(B.kicks)>>([&](auto i) {
                constexpr auto entry = B.kicks[i];
                constexpr auto diff = entry[0_szc];
                if (diff[0_szc] != rot_from || diff[1_szc] != rot_to)
                    return;
                constexpr auto table = entry[1_szc];
                static_for<std::tuple_size_v<std::remove_const_t<decltype(table)>>>([&](auto j) {
                    if (found)
                        return;
                    constexpr auto kick = table[j];
                    constexpr int dx = kick[0_szc], dy = kick[1_szc];
                    int nx = x + dx, ny = y + dy;
                    if (nx >= 0 && nx < static_cast<int>(board_t::width) && ny >= 0 && is_valid(rot_to, nx, ny)) {
                        new_x = nx;
                        new_y = ny;
                        found = true;
                    }
                });
            });

            if (found)
                return {rot_to, new_x, new_y};
            return {rot_from, x, y};
        }
    };

    template <class T>
    struct is_move_checker_impl : std::false_type {};

    template <auto B, class Board>
    struct is_move_checker_impl<move_checker<B, Board>> : std::true_type {};
    template <class T>
    concept move_checker_type = is_move_checker_impl<T>::value;
} // namespace reachability::search