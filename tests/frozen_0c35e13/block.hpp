#pragma once
#include <array>
#include <utility>
#include <concepts>
#include <cstdint>
#include "utils.hpp"

namespace reachability {
    using coord = tuple<int, int>;

    constexpr coord operator-(const coord& co) {
        return {-co[0_szc], -co[1_szc]};
    }

    constexpr coord operator+(const coord& co1, const coord& co2) {
        return {co1[0_szc] + co2[0_szc], co1[1_szc] + co2[1_szc]};
    }

    // Pack up to 8 ASCII characters into a uint64_t for piece identification
    struct piece_id {
        uint64_t value;

        template <std::size_t N>
        constexpr piece_id(const char (&s)[N]) : value(0) {
            constexpr std::size_t len = N - 1 < 8 ? N - 1 : 8;
            for (std::size_t i = 0; i < len; ++i)
                value |= (uint64_t)(unsigned char)s[i] << (i * 8);
        }

        constexpr bool operator==(const piece_id&) const = default;
    };

    // ========== Concept helpers ==========

    constexpr auto mino_p = vec_of<type_of<coord>>;
    constexpr auto minos_p = vec_of<mino_p>;

    // ========== Piece Definition ==========

    template <typename Shapes, typename Offsets, auto Id_ = piece_id("")>
    struct piece_def {
        static constexpr auto id = Id_;
        Shapes shapes;   // mino definitions per orientation
        Offsets offsets; // orientation → {shape_index, translation}
    };

    // Helper to create a piece_def with an explicit piece ID
    template <auto Id, typename Shapes, typename Offsets>
    constexpr auto make_piece_def(Shapes shapes, Offsets offsets) {
        return piece_def<Shapes, Offsets, Id>{shapes, offsets};
    }

    // ========== Kick Table ==========

    template <typename Kicks>
    struct kick_table {
        Kicks kicks;
    };

    // No kicks (default for block template)
    inline constexpr kick_table<tuple<>> no_kicks{tuple{}};

    // ========== Block ==========

    template <typename Shapes, typename Offsets, typename Kicks, piece_id Id_ = piece_id("")>
    struct block {
        static constexpr int shapes = std::tuple_size_v<Shapes>;
        static constexpr int orientations = std::tuple_size_v<Offsets>;
        static constexpr piece_id piece_identity = Id_;

        Shapes minos;
        Offsets mino_index;
        Kicks kicks;
    };

    template <class T>
    struct is_block_impl : std::false_type {};

    template <typename Shapes, typename Offsets, typename Kicks, auto Id_>
    struct is_block_impl<block<Shapes, Offsets, Kicks, Id_>> : std::true_type {};
    template <class T>
    concept block_spec = is_block_impl<T>::value;

    // ========== combined variable template ==========

    template <auto P, auto K>
    constexpr auto combined = [] {
        using shapes_t = std::remove_cvref_t<decltype(P.shapes)>;
        using offsets_t = std::remove_cvref_t<decltype(P.offsets)>;
        using kicks_t = std::remove_cvref_t<decltype(K.kicks)>;

        auto kicks = K.kicks;
        static_for<std::tuple_size_v<kicks_t>>([&](auto i) {
            auto& [transition, kick_list] = kicks[i];
            constexpr auto diff = K.kicks[i][0_szc];
            const auto offset = -P.offsets[index_c<diff[0_szc]>][1_szc] + P.offsets[index_c<diff[1_szc]>][1_szc];
            static_for<std::tuple_size_v<decltype(kick_list)>>([&](auto j) {
                kick_list[j] = kick_list[j] + offset;
            });
        });
        return block<shapes_t, offsets_t, kicks_t, P.id>{P.shapes, P.offsets, kicks};
    }();

    // Alias for compat
    inline constexpr auto no_rotation = no_kicks;

    // Compute bounding box of a mino
    template <Wrap<mino_p> auto mino>
    constexpr std::array<int, 4> mino_range() {
        static_assert(std::tuple_size_v<decltype(mino)> >= 1);
        int min_x = mino[0_szc][0_szc], max_x = min_x;
        int min_y = mino[0_szc][1_szc], max_y = min_y;
        static_for<std::tuple_size_v<decltype(mino)>>([&](auto i) {
            int x = mino[i][0_szc], y = mino[i][1_szc];
            if (x < min_x)
                min_x = x;
            if (x > max_x)
                max_x = x;
            if (y < min_y)
                min_y = y;
            if (y > max_y)
                max_y = y;
        });
        return {min_x, min_y, max_x, max_y};
    }

    // Identity offset table: each rotation maps to its own shape, zero translation
    template <std::size_t N>
    constexpr auto identity_offsets() {
        tuple_array<tuple<int, coord>, N> r;
        static_for<N>([&](auto i) { r[i] = {int(i), coord{0, 0}}; });
        return r;
    }
} // namespace reachability

// ========== Concepts for rule system ==========

namespace reachability::rules {
    // ----- Concepts -----

    template <typename T>
    concept piece_set = requires {
        typename T::piece_type;
        T::all;
        T::piece_list;
        { T::name_of(typename T::piece_type{}) } -> std::same_as<char>;
        { T::from_name(char{}) } -> std::same_as<typename T::piece_type>;
    };

    template <typename T>
    concept kick_set = requires {
        T::all;
    };

    template <typename T>
    concept rule_system = requires {
        typename T::piece_type;
        T::piece_list;
        T::block_list;
        { T::name_of(typename T::piece_type{}) } -> std::same_as<char>;
        { T::from_name(char{}) } -> std::same_as<typename T::piece_type>;
    };
} // namespace reachability::rules

// ========== Generic call_with_block ==========

namespace reachability {
    template <typename RS>
    [[gnu::always_inline]] constexpr auto
    call_with_block(typename RS::piece_type piece, auto f) {
        constexpr auto N = std::tuple_size_v<decltype(RS::piece_list)>;
        using result_t = decltype(f.template operator()<std::get<0>(RS::block_list)>());
        result_t result{};
        const bool matched = [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            return ((piece == std::get<Is>(RS::piece_list) &&
                        ((void)(result = f.template operator()<std::get<Is>(RS::block_list)>()), true)) ||
                    ...);
        }(std::make_index_sequence<N>{});
        if (!matched) [[unlikely]]
            std::unreachable();
        return result;
    }

} // namespace reachability

// ========== Rule system: combiner ==========

namespace reachability::rules {
    template <piece_set Pieces, kick_set Kicks>
    struct rule_set {
        using piece_type = typename Pieces::piece_type;

        static constexpr std::size_t count = std::tuple_size_v<decltype(Pieces::all)>;

        template <std::size_t... Is>
        static constexpr auto make_blocks(std::index_sequence<Is...>) {
            using pieces_t = std::remove_cvref_t<decltype(Pieces::all)>;
            using kicks_t = std::remove_cvref_t<decltype(Kicks::all)>;
            return tuple{
                reachability::combined<
                    std::tuple_element_t<Is, pieces_t>(std::get<Is>(Pieces::all)),
                    std::tuple_element_t<Is, kicks_t>(std::get<Is>(Kicks::all))>...};
        }

        static constexpr auto block_list = make_blocks(std::make_index_sequence<count>{});

        static constexpr auto piece_list = Pieces::piece_list;

        static constexpr char name_of(piece_type p) {
            return Pieces::name_of(p);
        }

        static constexpr piece_type from_name(char c) {
            return Pieces::from_name(c);
        }
    };

    template <class T>
    struct is_rule_set_impl : std::false_type {};

    template <class Pieces, class Kicks>
    struct is_rule_set_impl<rule_set<Pieces, Kicks>> : std::true_type {};
    template <class T>
    concept rule_set_type = is_rule_set_impl<T>::value;
} // namespace reachability::rules
