#pragma once
#include "block.hpp"
#include "utils.hpp"
#include <limits>
#include <numeric>
#include <string_view>
#include <string>
#include <array>
#include <type_traits>
#include <cstdint>
#include <bit>
#include "bit_permutations.hpp"
#ifdef USE_STME
#include "stme.hpp"
#elif __has_include(<experimental/simd>)
#include <experimental/simd>
#else
#include "stme.hpp"
#define USE_STME
#endif

namespace reachability {
    template <unsigned W, unsigned H, typename under_t>
    concept valid_board = requires {
        requires std::numeric_limits<under_t>::is_integer;
        requires std::is_unsigned_v<under_t>;
        requires(std::numeric_limits<under_t>::digits >= W);
    };

    template <unsigned W, unsigned H, typename under_t = std::uint64_t>
        requires valid_board<W, H, under_t>
    struct board_t {
        static constexpr int under_bits = std::numeric_limits<under_t>::digits;
        static constexpr int width = W;
        static constexpr int height = H;
        static constexpr int lines_per_under = under_bits / W;
        static constexpr int used_bits_per_under = lines_per_under * W;
        static constexpr int num_of_under = (H - 1) / lines_per_under + 1;
        static constexpr int last = num_of_under - 1;
        static constexpr int remaining_per_under = under_bits - used_bits_per_under;
        static constexpr under_t mask = under_t(-1) >> remaining_per_under;
        static constexpr int remaining_in_last = num_of_under * used_bits_per_under - H * W;
        static constexpr under_t last_mask = mask >> remaining_in_last;

        using column_t = std::conditional_t<H <= 8, uint8_t,
            std::conditional_t<H <= 16, uint16_t,
                std::conditional_t<H <= 32, uint32_t, uint64_t>>>;
        using row_t = std::conditional_t<W <= 8, uint8_t,
            std::conditional_t<W <= 16, uint16_t,
                std::conditional_t<W <= 32, uint32_t, uint64_t>>>;

        constexpr board_t() = default;

        constexpr board_t(std::string_view s) : board_t(convert_to_array(s)) {}

        constexpr board_t(std::array<under_t, num_of_under> d) :
#ifdef USE_STME
                                                                 data{d} {
        }
#else
                                                                 data{d.data(), std::experimental::element_aligned} {
        }
#endif

        static constexpr std::array<under_t, num_of_under> convert_to_array(std::string_view s) {
            std::array<under_t, num_of_under> data = {};
            for (std::size_t i = 0; i < last; ++i) {
                data[i] = convert_to_under_t(s.substr(W * H - (i + 1) * used_bits_per_under, used_bits_per_under));
            }
            data[last] = convert_to_under_t(s.substr(0, used_bits_per_under - remaining_in_last));
            return data;
        }

        template <int x, int y>
        constexpr void set() {
            constexpr int yi = y / lines_per_under;
            constexpr under_t bit = under_t(1) << ((y % lines_per_under) * W + x);
            assign(data, yi, data[yi] | bit);
        }

        constexpr void set(int x, int y) {
            const int yi = y / lines_per_under;
            const under_t bit = under_t(1) << ((y % lines_per_under) * W + x);
            assign(data, yi, data[yi] | bit);
        }

        template <int x, int y>
        constexpr int get() const {
            static_assert(x >= 0 && x < int(W) && y >= 0 && y < int(H));
            return data[y / lines_per_under] & (under_t(1) << ((y % lines_per_under) * W + x)) ? 1 : 0;
        }

        template <int y>
        constexpr int get() const {
            // use highest bit as the result
            return get<W - 1, y>();
        }

        constexpr int get(int x, int y) const {
            if ((x < 0) || (x >= int(W)) || (y < 0) || (y >= int(H))) {
                return 2;
            }
            return data[y / lines_per_under] & (under_t(1) << ((y % lines_per_under) * W + x)) ? 1 : 0;
        }

        constexpr bool any() const {
            return *this != board_t{};
        }

        constexpr bool operator!=(board_t other) const {
#ifdef USE_STME
            return any_of(data != other.data);
#else
            // manually compare to avoid GCC 16 simd library bug
            constexpr std::size_t N = num_of_under;
            std::array<under_t, N> a, b;
            data.copy_to(a.data(), std::experimental::element_aligned);
            other.data.copy_to(b.data(), std::experimental::element_aligned);
            for (std::size_t i = 0; i < N; ++i)
                if (a[i] != b[i])
                    return true;
            return false;
#endif
        }

        constexpr bool operator==(board_t other) const {
            return !(*this != other);
        }

        [[gnu::always_inline]] constexpr bool contains(board_t other) const {
#ifdef USE_STME
            return !any_of(other.data & ~data);
#else
            // manually check to avoid GCC 16 simd library bug
            constexpr std::size_t N = num_of_under;
            std::array<under_t, N> tmp;
            (other.data & ~data).copy_to(tmp.data(), std::experimental::element_aligned);
            for (std::size_t i = 0; i < N; ++i)
                if (tmp[i] != 0)
                    return false;
            return true;
#endif
        }

        constexpr board_t operator~() const {
            board_t other;
            other.data = mask_board() & ~data;
            return other;
        }

        constexpr board_t& operator&=(board_t rhs) {
            data &= rhs.data;
            return *this;
        }

        constexpr board_t operator&(board_t rhs) const {
            board_t result = *this;
            result &= rhs;
            return result;
        }

        constexpr board_t& operator|=(board_t rhs) {
            data |= rhs.data;
            return *this;
        }

        constexpr board_t operator|(board_t rhs) const {
            board_t result = *this;
            result |= rhs;
            return result;
        }

        constexpr board_t& operator^=(board_t rhs) {
            data ^= rhs.data;
            return *this;
        }

        constexpr board_t operator^(board_t rhs) const {
            board_t result = *this;
            result ^= rhs;
            return result;
        }

        template <Wrap<mino_p> auto mino>
        static constexpr board_t put(int x, int y) {
            constexpr auto range = mino_range<mino>();
            constexpr int min_x = range[0];
            board_t shape = get_shapes<mino>()[y % lines_per_under];
            static_for<num_of_under>([&](auto i) {
                if (y / lines_per_under == i)
                    shape.template move_<coord{0, (int(i) - 1) * lines_per_under}>();
            });
            shape.data <<= x + min_x;
            return shape;
        }

        template <coord d, bool check = true>
        constexpr void move_() {
            constexpr int dx = d[0_szc], dy = d[1_szc];
            if constexpr (dy == 0) {
                if constexpr (dx > 0) {
                    data <<= dx;
                    data &= mask_board();
                } else if constexpr (dx < 0) {
                    data >>= -dx;
                }
            } else if constexpr (dy > 0) {
                constexpr int pad = (dy - 1) / lines_per_under;
                constexpr int shift = (dy - 1) % lines_per_under + 1;
                auto not_moved = my_split<pad, true>(my_shift<dx, shift>(data));
                auto moved = my_split<pad + 1, true>(my_shift<dx, shift - lines_per_under>(data));
                data = (not_moved | moved) & mask_board();
            } else {
                constexpr int pad = (-dy - 1) / lines_per_under;
                constexpr int shift = (-dy - 1) % lines_per_under + 1;
                auto not_moved = my_split<pad, false>(my_shift<dx, -shift>(data));
                auto moved = my_split<pad + 1, false>(my_shift<dx, lines_per_under - shift>(data));
                data = (not_moved | moved) & mask_board();
            }
            if constexpr (check && dx != 0) {
                data &= mask_move<dx>();
            }
        }

        template <coord d, bool check = true>
        constexpr board_t move() const {
            board_t result = *this;
            result.move_<d, check>();
            return result;
        }

        friend constexpr std::string to_string(board_t board) {
            std::string ret;
            static_for<H>([&] [[gnu::always_inline]] (auto y) {
                std::string this_ret = "|";
                static_for<W>([&] [[gnu::always_inline]] (auto x) {
                    this_ret += board.get<x, y>() ? "[]" : "  ";
                });
                this_ret += "|\n";
                ret = this_ret + ret;
            });
            return ret;
        }

        template <int height>
        friend constexpr std::string to_string(board_t board) {
            std::string ret;
            static_for<height>([&] [[gnu::always_inline]] (auto y) {
                std::string this_ret = "|";
                static_for<W>([&] [[gnu::always_inline]] (auto x) {
                    this_ret += board.get<x, y>() ? "[]" : "  ";
                });
                this_ret += "|\n";
                ret = this_ret + ret;
            });
            return ret;
        }

        friend constexpr std::string to_string(board_t board, int height) {
            std::string ret;
            for (int y = 0; y < height; ++y) {
                std::string this_ret = "|";
                for (int x = 0; x < int(W); ++x)
                    this_ret += board.get(x, y) ? "[]" : "  ";
                this_ret += "|\n";
                ret = this_ret + ret;
            }
            return ret;
        }

        friend constexpr std::string to_string(board_t board1, board_t board2) {
            std::string ret;
            static_for<H>([&] [[gnu::always_inline]] (auto y) {
                std::string this_ret;
                static_for<W>([&] [[gnu::always_inline]] (auto x) {
                    bool b1 = board1.get<x, y>();
                    bool b2 = board2.get<x, y>();
                    if (b1 && b2) {
                        this_ret += "%%";
                    } else if (b1) {
                        this_ret += "..";
                    } else if (b2) {
                        this_ret += "[]";
                    } else {
                        this_ret += "  ";
                    }
                });
                this_ret += '\n';
                ret = this_ret + ret;
            });
            return ret;
        }

        friend constexpr std::string to_string(board_t board1, board_t board2, board_t board_3) {
            std::string ret;
            static_for<H>([&] [[gnu::always_inline]] (auto y) {
                std::string this_ret;
                static_for<W>([&] [[gnu::always_inline]] (auto x) {
                    bool tested[2] = {bool(board1.get<x, y>()), bool(board2.get<x, y>())};
                    bool b3 = board_3.get<x, y>();
                    std::string symbols = "  <>[]%%";
                    for (int i = 0; i < 2; ++i) {
                        this_ret += symbols[b3 * 4 + tested[i] * 2 + i];
                    }
                });
                this_ret += '\n';
                ret = this_ret + ret;
            });
            return ret;
        }

        struct clear_result {
            board_t board;
            int count;
            board_t full_rows; // bit (W-1) set for each cleared row

            /// Check if a specific row @p y was cleared by this operation.
            constexpr bool is_row_cleared(int y) const {
                return full_rows.get(W - 1, y);
            }

            /// Check whether any cleared row exists strictly below height @p h
            /// (i.e. rows 0 .. h-1).
            constexpr bool has_cleared_below(int h) const {
                for (int y = 0; y < h; ++y)
                    if (full_rows.get(W - 1, y))
                        return true;
                return false;
            }
        };

        [[gnu::always_inline]] constexpr clear_result clear_full_lines() const {
            const board_t is_full = all_bits();

            const data_t is_full_single = is_full.data & one_bit<W - 1>();
            const board_t full_rows = to_board(is_full_single);
            if (!full_rows.any()) {
                return {*this, 0, {}};
            }
            std::array<int, num_of_under> lines = {};
            static_for<num_of_under>([&] [[gnu::always_inline]] (auto i) {
                lines[i] = std::popcount(under_t(is_full_single[i]));
            });
            const int all_lines = std::accumulate(lines.begin(), lines.end(), 0);

            const data_t useful_bits_mask = ~is_full.populate_highest_bit().data;
            data_t cleared{[&] [[gnu::always_inline]] (auto i) {
                return cxx26bp::bit_compress<under_t>(data[i], useful_bits_mask[i]);
            }};

            std::array<int, num_of_under> prefix_sum = {};
            std::partial_sum(lines.begin(), lines.end() - 1, prefix_sum.begin() + 1);
            [[assume(all_lines < lines_per_under +
                (W * lines_per_under == std::numeric_limits<under_t>::digits ? 0 : 1))]];

            data_t moved_down{[&] [[gnu::always_inline]] (auto i) -> under_t {
                if constexpr (i == num_of_under - 1)
                    return 0;
                else if constexpr (W * lines_per_under == std::numeric_limits<under_t>::digits) {
                    return prefix_sum[i + 1] == 0
                        ? under_t(0)
                        : cleared[i + 1] << (W * (lines_per_under - prefix_sum[i + 1]));
                } else
                    return cleared[i + 1] << (W * (lines_per_under - prefix_sum[i + 1]));
            }};
            data_t remained{[&] [[gnu::always_inline]] (auto i) -> under_t {
                return cleared[i] >> (W * prefix_sum[i]);
            }};

            return {to_board((moved_down | remained) & mask_board()), all_lines, full_rows};
        }

        constexpr board_t has_single_bit() const {
            auto saturated = data | one_bit<W - 1>();
            saturated &= saturated - one_bit<0>();
            // if data has no 1 in not-highest bits: 0
            // if data has exactly one 1 in not-highest bits: 10...0
            // if data has more than one 1 in not-highest bits: 1...1...
            auto saturated2 = saturated | one_bit<W - 1>();
            saturated2 &= saturated2 - one_bit<0>();
            auto result = (saturated ^ data) & ~saturated2;
            return to_board(result);
        }

        constexpr board_t all_bits() const {
            auto low = data & ~one_bit<W - 1>();
            return to_board(data & (low + one_bit<0>()));
        }

        constexpr board_t any_bit() const {
            return ~to_board(~data).all_bits();
        }

        constexpr board_t no_bit() const {
            return ~any_bit();
        }

        constexpr board_t remove_ones_after_zero() const {
            auto board = data | ~mask_board();
            std::array<int, num_of_under> ones;
            static_for<num_of_under>([&] [[gnu::always_inline]] (auto i) {
                ones[i] = std::countl_one(under_t(board[i]));
            });
            bool found = false;
#pragma unroll num_of_under
            for (int i = num_of_under - 1; i >= 0; --i) {
                if (found) {
                    assign(board, i, 0);
                } else if (ones[i] < std::numeric_limits<under_t>::digits) {
                    found = true;
                    assign(board, i, board[i] & ~((~under_t(0)) >> ones[i]));
                }
            }
            return to_board(board & mask_board());
        }

        constexpr board_t populate_highest_bit() const {
            // result is in highest bit (0 or 1), other bits are 0
            // populate the result to all bits
            auto result = data & one_bit<W - 1>();
            auto pre_result = one_bit<W - 1>() - (result >> (W - 1));
            return to_board(pre_result ^ one_bit<W - 1>());
        }

        constexpr board_t get_heads() const {
            return (*this) & ~move<coord{-1, 0}>();
        }

        friend constexpr board_t can_expand(board_t current, board_t possible) {
            const auto starts = possible & current.template move<coord{-1, 0}>();
            const auto ends = possible & current.template move<coord{1, 0}>();
            const auto all_heads = possible.get_heads();
            const auto heads = starts.data | (ends.data + (possible & ~all_heads).data);
            return to_board(heads);
        }

        constexpr int popcount() const {
            int acc = 0;
            static_for<num_of_under>([&] [[gnu::always_inline]] (auto i) {
                acc += std::popcount(data[i]);
            });
            return acc;
        }

        template <class F>
        [[gnu::always_inline]] void for_each_bit(F&& f) const {
            reachability::static_for<num_of_under>([&] [[gnu::always_inline]] (auto i) {
                for (under_t data_i = data[i]; data_i; data_i &= data_i - 1) {
                    int pos = std::countr_zero(data_i);
                    [[assume(pos / W < lines_per_under && pos / W >= 0)]];
                    f(pos % W, pos / W + i * lines_per_under);
                }
            });
        }

        [[gnu::always_inline]] constexpr int highest_y() const {
            for (int i = num_of_under - 1; i >= 0; --i) {
                const under_t w = data[i] & (i == last ? last_mask : mask);
                if (w) {
                    const int pos = under_bits - 1 - std::countl_zero(w);
                    return i * lines_per_under + pos / W + 1;
                }
            }
            return 0;
        }
        [[gnu::always_inline]] constexpr std::array<size_t, W> column_tops() const {
            std::array<size_t, W> res{};
            auto cols = to_column_bitboard();
            for (size_t x = 0; x < W; ++x) {
                column_t c = cols[x];
                res[x] = c == 0 ? 0 : static_cast<size_t>(std::bit_width(c));
            }
            return res;
        }

        template <int H2>
            requires(H2 <= H)
        constexpr board_t<W, H2, under_t> cut_to_height() const {
            using result_t = board_t<W, H2, under_t>;
            typename result_t::data_t result_data{[&] [[gnu::always_inline]] (auto i) -> under_t {
                if constexpr (i < result_t::num_of_under) {
                    return data[i];
                } else {
                    return 0;
                }
            }};
            return result_t::to_board(result_data);
        }

        template <Wrap<vec_of<type_of<int>>> auto hs>
        [[gnu::always_inline]] constexpr void call_with_height(unsigned height, auto&& f) const {
            static_assert(std::tuple_size_v<decltype(hs)> > 0);
            static_for<std::tuple_size_v<decltype(hs)>>([&](auto i) {
                static_assert(hs[i] > 0);
            });
            static_for<std::tuple_size_v<decltype(hs)> - 1>([&](auto i) {
                static_assert(hs[i] < hs[index_c<i + 1>]);
            });
            bool found = false;
            static_for<std::tuple_size_v<decltype(hs)>>([&] [[gnu::always_inline]] (auto i) {
                if (!found && height <= hs[i]) {
                    found = true;
                    f(cut_to_height<hs[i]>());
                }
            });
            if (!found) {
                f(*this);
            }
        }

        const under_t* raw() const noexcept {
            return reinterpret_cast<const under_t*>(&data);
        }

        // Fast path requires 64-bit words and height <= 48 (rows fit in 64-bit
        // column bitboards; the SIMD variant is built for 48 rows). The multiply
        // gather/expand is correct only while its copies never overlap:
        //   gather (to):   W*k + (64-L) - (W-1)*j  distinct -> W >= 8
        //   expand (from): k + (W-1)*j + x         distinct -> W >= 9
        // Below that (e.g. W=4), L = 64/W > W-1 rows per word: copies collide
        // and the carries corrupt column-x bits, so those widths keep the
        // generic pext/pdep path.
        static constexpr bool fast_gather_path =
            std::is_same_v<under_t, std::uint64_t> && width >= 8 && height <= 48;
        static constexpr bool fast_expand_path =
            std::is_same_v<under_t, std::uint64_t> && width >= 9 && height <= 48;

        static constexpr under_t column_bits_mask() {
            under_t m = 0;
            for (int k = 0; k < lines_per_under; ++k) m |= under_t(1) << (width * k);
            return m;
        }
        static constexpr under_t column_gather_magic() {
            under_t m = 0;
            for (int j = 0; j < lines_per_under; ++j)
                m |= under_t(1) << (under_bits - lines_per_under - (width - 1) * j);
            return m;
        }
        static constexpr under_t column_expand_magic() {
            under_t m = 0;
            for (int j = 0; j < lines_per_under; ++j) m |= under_t(1) << ((width - 1) * j);
            return m;
        }
        static_assert(width != 10 || !fast_gather_path || column_bits_mask() ==
            ((1ull << 0) | (1ull << 10) | (1ull << 20) | (1ull << 30) | (1ull << 40) | (1ull << 50)));
        static_assert(width != 10 || !fast_gather_path || column_gather_magic() ==
            ((1ull << 13) | (1ull << 22) | (1ull << 31) | (1ull << 40) | (1ull << 49) | (1ull << 58)));
        static_assert(width != 10 || !fast_expand_path || column_expand_magic() ==
            ((1ull << 0) | (1ull << 9) | (1ull << 18) | (1ull << 27) | (1ull << 36) | (1ull << 45)));

        [[gnu::always_inline]] std::array<column_t, width> to_column_bitboard() const {
            std::array<column_t, width> columns = {};
            if constexpr (fast_gather_path) {
#if defined(__clang__)
                if constexpr (width == 10) {
                    using u32x4 = unsigned int __attribute__((ext_vector_type(4)));
                    using u8x16 = unsigned char __attribute__((ext_vector_type(16)));
                    using u8x32 = unsigned char __attribute__((ext_vector_type(32)));
                    std::uint16_t rows48[48] = {};
                    auto rows = this->template to_row_bitboard<true>();
                    for (int y = 0; y < height; ++y) rows48[y] = rows[y];
                    constexpr unsigned int G = (1u << 3) | (1u << 10) | (1u << 17) | (1u << 24);
                    const u32x4 Gv = {G, G, G, G};
                    const u32x4 ONES = {0x01010101u, 0x01010101u, 0x01010101u, 0x01010101u};
                    u32x4 acc[10] = {};
                    u32x4 accHi[10] = {};
                    for (int p = 0; p < 3; ++p) {
                        u8x32 r32;
                        __builtin_memcpy(&r32, &rows48[16 * p], 32);
                        u8x16 evens = __builtin_shufflevector(r32, r32, 0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30);
                        u8x16 odds = __builtin_shufflevector(r32, r32, 1, 3, 5, 7, 9, 11, 13, 15, 17, 19, 21, 23, 25, 27, 29, 31);
                        const u32x4 B = __builtin_bit_cast(u32x4, evens);
                        const u32x4 HB = __builtin_bit_cast(u32x4, odds);
                        u32x4* accp = p < 2 ? acc : accHi;
                        const u32x4 SH = {16u * (p % 2), 16u * (p % 2) + 4, 16u * (p % 2) + 8, 16u * (p % 2) + 12};
                        for (int x = 0; x < 8; ++x) {
                            const u32x4 g = (((B >> x) & ONES) * Gv) >> 24;
                            accp[x] += g << SH;
                        }
                        const u32x4 g8 = (((HB >> 0) & ONES) * Gv) >> 24;
                        const u32x4 g9 = (((HB >> 1) & ONES) * Gv) >> 24;
                        accp[8] += g8 << SH;
                        accp[9] += g9 << SH;
                    }
                    for (int x = 0; x < width; ++x) {
                        u32x4 a = acc[x];
                        a |= __builtin_shufflevector(a, a, 1, 0, 3, 2);
                        a |= __builtin_shufflevector(a, a, 2, 2, 0, 0);
                        u32x4 h = accHi[x];
                        h |= __builtin_shufflevector(h, h, 1, 0, 3, 2);
                        h |= __builtin_shufflevector(h, h, 2, 2, 0, 0);
                        columns[x] = static_cast<column_t>((std::uint64_t)a[0] | ((std::uint64_t)h[0] << 32));
                    }
                    return columns;
                }
#endif

                constexpr under_t mask = column_bits_mask();
                constexpr under_t mg = column_gather_magic();
                for (int x = 0; x < width; ++x) {
                    for (int i = 0; i < num_of_under; ++i) {
                        const under_t u = (data[i] >> x) & mask;
                        columns[x] |= static_cast<column_t>(((u * mg) >> (under_bits - lines_per_under)) << (lines_per_under * i));
                    }
                }
                return columns;
            }
            reachability::static_for<width>([&] [[gnu::always_inline]] (auto x) {
                auto col_mask = one_bit<x>();
                for (std::size_t i = 0; i < std::size_t(num_of_under); ++i) {
                    columns[x] |= static_cast<column_t>(cxx26bp::bit_compress<under_t>(data[i], col_mask[i])) << (i * lines_per_under);
                }
            });
            return columns;
        }

        // clean: removes garbage bits beyond board width
        template <bool clean = false>
        [[gnu::always_inline]] std::array<row_t, height> to_row_bitboard() const {
            std::array<row_t, height> rows = {};
            constexpr row_t row_mask = (row_t(1) << width) - 1;
            reachability::static_for<H>([&](auto y) {
                int bit_pos = int(y) * W;
                int ui = bit_pos / used_bits_per_under;
                int off = bit_pos % used_bits_per_under;
                unsigned int r;
                if (off + W > under_bits) {
                    int lo = under_bits - off;
                    r = ((data[ui] >> off) | (data[ui + 1] << lo));
                } else {
                    r = (data[ui] >> off);
                }
                if constexpr (clean) {
                    r &= row_mask;
                }
                rows[y] = r;
            });
            return rows;
        }

        [[gnu::always_inline]] void from_column_bitboard(std::array<column_t, width> columns) {
            if constexpr (fast_expand_path) {
                constexpr under_t mask = column_bits_mask();
                constexpr under_t me = column_expand_magic();
                for (int x = 0; x < width; ++x) {
                    const under_t M = me << x;
                    const under_t mx = mask << x;
                    for (int i = 0; i < num_of_under; ++i) {
                        const under_t u = under_t(columns[x] >> (lines_per_under * i)) & under_t((under_t(1) << lines_per_under) - 1);
                        assign(data, i, data[i] | ((u * M) & mx));
                    }
                }
            } else {
                static_for<width>([&] [[gnu::always_inline]] (auto x) {
                    auto col_mask = one_bit<x>();
                    for (std::size_t i = 0; i < std::size_t(num_of_under); ++i) {
                        column_t col_bits = columns[x] >> (i * lines_per_under);
                        assign(data, i, data[i] | cxx26bp::bit_expand<under_t>(static_cast<under_t>(col_bits), col_mask[i]));
                    }
                });
            }
        }

        template <bool clean = false>
        [[gnu::always_inline]] void from_row_bitboard(std::array<row_t, height> rows) {
            data = {};
            if constexpr(clean) {
                constexpr row_t row_mask = (row_t(1) << width) - 1;
                for (auto& r : rows) {
                    r &= row_mask;
                }
            }
            static_for<H>([&](auto y) {
                int bit_pos = int(y) * W;
                int ui = bit_pos / used_bits_per_under;
                int off = bit_pos % used_bits_per_under;
                under_t r = static_cast<under_t>(rows[y]);
                if (off + W > under_bits) {
                    int lo = under_bits - off;
                    assign(data, ui, data[ui] | (r << off));
                    assign(data, ui + 1, data[ui + 1] | (r >> lo));
                } else {
                    assign(data, ui, data[ui] | (r << off));
                }
            });
        }

    private:
        template <unsigned W2, unsigned H2, typename under_t2>
            requires valid_board<W2, H2, under_t2>
        friend struct board_t;
        using data_t =
#ifdef USE_STME
            Shak::stme<under_t, num_of_under>;
#else
            std::experimental::simd<under_t, std::experimental::simd_abi::deduce_t<under_t, num_of_under>>;
#endif
        data_t data{0};

        static constexpr board_t to_board(data_t data) {
            board_t ret;
            ret.data = data;
            return ret;
        }

        template <int dx>
        static constexpr data_t mask_move() {
            board_t mask;
            if constexpr (dx > 0) {
                static_for<dx>([&] [[gnu::always_inline]] (auto i) {
                    static_for<H>([&] [[gnu::always_inline]] (auto j) {
                        mask.set<i, j>();
                    });
                });
            } else if constexpr (dx < 0) {
                static_for<-dx>([&] [[gnu::always_inline]] (auto i) {
                    static_for<H>([&] [[gnu::always_inline]] (auto j) {
                        mask.set<W - 1 - i, j>();
                    });
                });
            }
            return (~mask).data;
        }

        template <int dx>
        static constexpr data_t one_bit() {
            board_t ret;
            static_for<H>([&] [[gnu::always_inline]] (auto j) {
                ret.set<dx, j>();
            });
            return ret.data;
        }

        static constexpr data_t mask_board() {
            return data_t{[](auto i) {
                if constexpr (i == last) {
                    return last_mask;
                } else {
                    return mask;
                }
            }};
        }

    public:
        static constexpr board_t full_lines_of(int n) {
            size_t full_unders = n / lines_per_under, remaining_filled_line = n % lines_per_under;
            return to_board(data_t{[=](auto i) -> under_t {
                if (i < full_unders)
                    return mask;
                else if (i > full_unders)
                    return 0;
                else
                    return (under_t(1) << (W * remaining_filled_line)) - 1;
            }});
        }

    private:
        template <int removed, bool from_right>
        static constexpr data_t my_split(data_t data) {
            return data_t([=] [[gnu::always_inline]] (auto i) {
                constexpr size_t index = from_right ? i - removed : i + removed;
                if constexpr (index >= num_of_under) {
                    return (under_t)0;
                } else {
                    return data[index];
                }
            });
        }

        template <int x_shift, int y_shift>
        static constexpr data_t my_shift(data_t data) {
            if constexpr (y_shift == lines_per_under || y_shift == -lines_per_under) {
                data = data_t(0);
            } else if constexpr (y_shift < 0) {
                data >>= -y_shift * W;
            } else if constexpr (y_shift > 0) {
                data <<= y_shift * W;
            }
            if constexpr (x_shift > 0) {
                data <<= x_shift;
            } else if constexpr (x_shift < 0) {
                data >>= -x_shift;
            }
            return data;
        }

        static constexpr under_t convert_to_under_t(std::string_view in) {
            under_t res = 0;
            for (char c : in) {
                res *= 2;
                if (c == 'X')
                    res += 1;
            }
            return res;
        }

        template <Wrap<mino_p> auto mino>
        static constexpr board_t standard_shape() {
            auto [min_x, min_y, max_x, max_y] = mino_range<mino>();
            board_t b;
            static_for<std::tuple_size_v<decltype(mino)>>([&](auto i) {
                int x = mino[i][0_szc], y = mino[i][1_szc];
                x -= min_x;
                y -= min_y;
                b.set(x, y);
            });
            return b;
        }

        template <Wrap<mino_p> auto mino, int y>
        static constexpr board_t shape_at_y() {
            constexpr auto range = mino_range<mino>();
            return standard_shape<mino>().template move<coord{0, y + range[1] + lines_per_under}>();
        }

        template <Wrap<mino_p> auto mino>
        static constexpr const std::array<board_t, lines_per_under>& get_shapes() {
            static std::array<board_t, lines_per_under> s = [] {
                std::array<board_t, lines_per_under> a;
                static_for<lines_per_under>([&](auto i) { a[i].data = shape_at_y<mino, i>().data; });
                return a;
            }();
            return s;
        }
#ifndef USE_STME
        static void assign(data_t& data, int i, under_t value) {
            data[i] = value;
        }
#endif
    };

    template <class T>
    struct is_board_impl : std::false_type {};

    template <unsigned W, unsigned H, typename under_t>
    struct is_board_impl<board_t<W, H, under_t>> : std::true_type {};
    template <class T>
    concept board_type = is_board_impl<T>::value;
} // namespace reachability
