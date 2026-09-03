#include "scalar_arrival_oracle.h"

#include <print>
#include <random>
#include <string>
#include <vector>

using reachability::operator""_szc;
using namespace reachability;
using BOARD = board_t<10, 48>;
using SRS = rule_set<Tetromino, SRS_Kicks>;

namespace
{
    size_t checks = 0;
    size_t failures = 0;

    void check(bool ok, std::string const &what)
    {
        ++checks;
        if (!ok)
        {
            ++failures;
            std::println(stderr, "FAIL: {}", what);
        }
    }

    void run_perft_vectors()
    {
        constexpr std::array test_data = {
            std::pair{std::string_view("IIIIII"), 33325433u},
            std::pair{std::string_view("IOLJSZT"), 2647076135u},
            std::pair{std::string_view("TIOLJSZ"), 2785677550u},
            std::pair{std::string_view("ZTIOLJS"), 2741273038u},
            std::pair{std::string_view("SZTIOLJ"), 2740055656u},
            std::pair{std::string_view("JSZTIOL"), 2801460686u},
            std::pair{std::string_view("LJSZTIO"), 2852978763u},
            std::pair{std::string_view("OLJSZTI"), 2689379684u},
        };
        for (auto const &[blocks, expected] : test_data)
        {
            BOARD state;
            auto const cfg = search::search_config{false, true, true};
            uint64_t const result = perft(state, blocks.data(), blocks.size(), 0, cfg);
            check(result == expected, "perft vector " + std::string(blocks.data()) + " exact");
        }
    }

    BOARD board_from_rows(std::array<uint16_t, 48> const &rows)
    {
        BOARD board;
        std::array<BOARD::row_t, 48> clipped = {};
        for (int y = 0; y < 48; ++y)
        {
            clipped[y] = rows[y];
        }
        board.from_row_bitboard<true>(clipped);
        return board;
    }

    bool compare_with_bitpar(auto const &result, auto const &oracle_normal, auto const &oracle_rotation, std::string const &what)
    {
        constexpr int orientations = std::remove_reference_t<decltype(result)>::orientations;
        bool ok = true;
        for (int o = 0; o < orientations; ++o)
        {
            for (int w = 0; w < 8; ++w)
            {
                ok = ok && uint64_t(result.normal_landings[o].logical_word(w)) == uint64_t(oracle_normal[o][w]);
                ok = ok && uint64_t(result.rotation_landings[o].logical_word(w)) == uint64_t(oracle_rotation[o][w]);
            }
        }
        if (!ok)
        {
            std::println(stderr, "comparison failed: {}", what);
        }
        return ok;
    }

    template <auto B2>
    void compare_selection(char name, size_t b, scalar_arrival::ScalarConfig const &sc, bool consecutive,
                           scalar_arrival::PieceGeometry<B2> const &geo,
                           search::search_workspace<B2, BOARD> const &ws,
                           std::array<uint16_t, 48> const &rows, coord spawn)
    {
        search::search_config cfg{};
        cfg.allow_180 = sc.allow_180;
        cfg.allow_softdrop = sc.allow_softdrop;
        cfg.allow_sonicdrop = sc.allow_sonicdrop;
        cfg.allow_20g = sc.allow_20g;
        std::string what = std::string(1, name) + " board " + std::to_string(b)
            + " cfg{" + std::to_string(sc.allow_180) + "," + std::to_string(sc.allow_softdrop) + ","
            + std::to_string(sc.allow_sonicdrop) + "," + std::to_string(sc.allow_20g) + "}"
            + " consecutive " + std::to_string(consecutive);
        auto run_case = [&](auto check_tag) {
            auto result = search::template arrival_search<B2, decltype(check_tag)::value>(ws, cfg, spawn, 0);
            auto raw = search::template binary_bfs<B2, decltype(check_tag)::value>(ws, cfg, spawn, 0);
            scalar_arrival::ScalarOracle<B2> oracle{geo, sc, rows};
            oracle.run(spawn, 0);
            bool const allow_float = sc.allow_softdrop && !sc.allow_20g;
            auto oracle_normal = oracle.landable_words(0, allow_float);
            auto oracle_rotation = oracle.landable_words(1, allow_float);
            check(compare_with_bitpar(result, oracle_normal, oracle_rotation, what), what);
            bool union_ok = true;
            for (int s = 0; s < B2.shapes; ++s)
            {
                for (int w = 0; w < 8; ++w)
                {
                    uint64_t both = 0;
                    for (int o = 0; o < B2.orientations; ++o)
                    {
                        if (geo.shape_of[o] != s)
                        {
                            continue;
                        }
                        both |= uint64_t(result.normal_landings[o].logical_word(w)) | uint64_t(result.rotation_landings[o].logical_word(w));
                    }
                    union_ok = union_ok && both == uint64_t(raw[s].logical_word(w));
                }
            }
            check(union_ok, "arrival union equals binary reachability " + what);
            bool landings_ok = true;
            for (int s = 0; s < B2.shapes; ++s)
            {
                for (int w = 0; w < 8; ++w)
                {
                    landings_ok = landings_ok && uint64_t(result.landings[s].logical_word(w)) == uint64_t(raw[s].logical_word(w));
                }
            }
            check(landings_ok, "arrival landings member equals binary reachability " + what);
        };
        if (consecutive)
        {
            run_case(std::true_type{});
        }
        else
        {
            run_case(std::false_type{});
        }
    }

    void run_piece_comparisons(char name, std::vector<std::array<uint16_t, 48>> const &boards, coord spawn)
    {
        call_with_block<SRS>(Tetromino::from_name(name), [&]<block B2>() {
            auto geo = scalar_arrival::make_geometry<B2>();
            std::array<scalar_arrival::ScalarConfig, 8> const configs = {{
                {true, true, true, false},
                {false, true, true, false},
                {true, false, true, false},
                {true, true, false, false},
                {true, false, false, false},
                {true, false, true, true},
                {true, true, true, true},
                {false, false, false, false},
            }};
            constexpr int necessary = 20 + search::downmost_position<B2>;
            for (size_t b = 0; b < boards.size(); ++b)
            {
                BOARD board = board_from_rows(boards[b]);
                search::search_workspace<B2, BOARD> ws(board);
                unsigned occupied = board.highest_y();
                int const cut = occupied + 3 <= 6 ? 6 : occupied + 3 <= 12 ? 12 : occupied + 3 <= 24 ? 24 : 48;
                bool const selected = !(cut < necessary) && occupied > unsigned(necessary);
                bool const false_valid = occupied <= unsigned(necessary);
                for (auto const &sc : configs)
                {
                    compare_selection<B2>(name, b, sc, selected, geo, ws, boards[b], spawn);
                    if (false_valid && !selected)
                    {
                        compare_selection<B2>(name, b, sc, true, geo, ws, boards[b], spawn);
                    }
                }
            }
            return 0;
        });
    }

    void run_oracle_comparisons()
    {
        auto boards = scalar_arrival::make_corpus();
        coord const spawn{4, 20};
        for (char name : std::string_view("TZSJLOI"))
        {
            run_piece_comparisons(name, boards, spawn);
        }
    }

    void run_directed_cases()
    {
        coord const spawn{4, 20};
        for (char name : std::string_view("TZSJLI"))
        {
            call_with_block<SRS>(Tetromino::from_name(name), [&]<block B>() {
                auto geo = scalar_arrival::make_geometry<B>();
                for (int f = 0; f < 4; ++f)
                {
                    int t = (f + 2) % 4;
                    bool found = false;
                    for (auto const &rule : geo.kicks)
                    {
                        found = found || (rule.from == f && rule.to == t && !rule.offsets.empty());
                    }
                    std::string what = std::string("piece ") + name + " has a 180 table entry";
                    check(found, what);
                }
                return 0;
            });
        }
        {
            BOARD empty_board;
            search::search_config cfg{};
            cfg.allow_180 = true;
            cfg.allow_softdrop = true;
            cfg.allow_sonicdrop = true;
            cfg.allow_20g = false;
            call_with_block<SRS>(Tetromino::from_name('I'), [&]<block B>() {
                search::search_workspace<B, BOARD> ws(empty_board);
                auto result = search::template arrival_search<B>(ws, cfg, spawn, 0);
                check(result.normal_landings[0] == result.normal_landings[2], "I duplicate orientations 0 and 2 canonicalize");
                check(result.normal_landings[1] == result.normal_landings[3], "I duplicate orientations 1 and 3 canonicalize");
                check(result.rotation_landings[2] == result.normal_landings[2] && result.rotation_landings[2].any(),
                    "I direct 180 identity rotation reaches the drop column as rotation arrivals");
                search::search_config no180 = cfg;
                no180.allow_180 = false;
                auto gated = search::template arrival_search<B>(ws, no180, spawn, 0);
                check(!gated.rotation_landings[2].any() && gated.normal_landings[2] == result.normal_landings[2],
                    "disabling 180 removes only the direct 180 rotation arrivals");
                return 0;
            });
        }
        {
            std::array<uint16_t, 48> rows = {};
            for (int y = 0; y <= 20; ++y)
            {
                if (y == 20)
                {
                    rows[y] = 0x3ff & ~uint16_t((1 << 3) | (1 << 4) | (1 << 5));
                }
                else if (y == 19)
                {
                    rows[y] = 0x3ff & ~uint16_t(1 << 4);
                }
                else
                {
                    rows[y] = 0x3ff;
                }
            }
            BOARD board = board_from_rows(rows);
            search::search_config cfg{};
            cfg.allow_180 = true;
            cfg.allow_softdrop = true;
            cfg.allow_sonicdrop = true;
            cfg.allow_20g = false;
            call_with_block<SRS>(Tetromino::from_name('T'), [&]<block B>() {
                search::search_workspace<B, BOARD> ws(board);
                auto result = search::template arrival_search<B>(ws, cfg, spawn, 0);
                bool rotation_exists = false;
                for (int o = 0; o < 4; ++o)
                {
                    rotation_exists = rotation_exists || result.rotation_landings[o].any();
                }
                check(rotation_exists, "T in a notch reaches rotation-only landings");
                check(result.normal_landings[0].any(), "T in a notch keeps its spawn landing");
                return 0;
            });
        }
        {
            BOARD empty_board;
            search::search_config cfg{};
            cfg.allow_180 = true;
            cfg.allow_softdrop = true;
            cfg.allow_sonicdrop = true;
            cfg.allow_20g = false;
            call_with_block<SRS>(Tetromino::from_name('T'), [&]<block B>() {
                search::search_workspace<B, BOARD> ws(empty_board);
                auto result = search::template arrival_search<B>(ws, cfg, spawn, 0);
                check(result.normal_landings[0].any(), "T keeps its spawn landing on an empty board");
                check(result.rotation_landings[2].any(), "T reaches orientation 2 via rotation on an empty board");
                return 0;
            });
        }
        {
            std::array<uint16_t, 48> rows = {};
            rows[0] = 0x18;
            BOARD board = board_from_rows(rows);
            search::search_config cfg{};
            cfg.allow_180 = true;
            cfg.allow_softdrop = true;
            cfg.allow_sonicdrop = true;
            cfg.allow_20g = false;
            call_with_block<SRS>(Tetromino::from_name('T'), [&]<block B>() {
                search::search_workspace<B, BOARD> ws(board);
                auto result = search::template arrival_search<B>(ws, cfg, spawn, 0);
                check(result.normal_landings[0].get(4, 1), "T rests at (4,1) above the blocked row");
                check(result.rotation_landings[1].get(3, 2), "T reaches (3,2) via the third 0 to 1 kick");
                return 0;
            });
        }
    }

    void run_workspace_tests()
    {
        BOARD board;
        std::mt19937 rng(4242u);
        for (int y = 0; y < 6; ++y)
        {
            uint32_t row = rng();
            for (int x = 0; x < 10; ++x)
            {
                if ((row >> x) & 1)
                {
                    board.set(x, y);
                }
            }
        }
        coord const spawn{4, 20};
        search::search_config cfg{};
        cfg.allow_180 = true;
        cfg.allow_softdrop = true;
        cfg.allow_sonicdrop = true;
        call_with_block<SRS>(Tetromino::from_name('T'), [&]<block B>() {
            search::search_workspace<B, BOARD> ws(board);
            auto checker = ws.checker();
            auto result = search::template arrival_search<B>(ws, cfg, spawn, 0);
            bool agree = true;
            for (int o = 0; o < 4; ++o)
            {
                for (int x = 0; x < 10; ++x)
                {
                    for (int y = 0; y < 48; ++y)
                    {
                        bool in_any = result.normal_landings[o].get(x, y) || result.rotation_landings[o].get(x, y);
                        agree = agree && (!in_any || checker.is_valid(o, x, y));
                    }
                }
            }
            check(agree, "workspace checker legality backs every landing");
            check(checker.is_valid(0, 4, 20), "workspace checker accepts T spawn");
            return 0;
        });
    }

    void run_dispatch_tests()
    {
        coord const spawn{4, 20};
        std::vector<unsigned> roofs = {0, 5, 9, 10, 17, 18, 21, 22, 25, 40, 45};
        std::mt19937 rng(20260906u);
        for (unsigned occupied : roofs)
        {
            BOARD board;
            for (unsigned y = 0; y < occupied && y < 48; ++y)
            {
                uint32_t row = rng();
                for (int x = 0; x < 10; ++x)
                {
                    if ((row >> x) & 1)
                    {
                        board.set(x, static_cast<int>(y));
                    }
                }
            }
            call_with_block<SRS>(Tetromino::from_name('T'), [&]<block B>() {
                constexpr int necessary = 20 + search::downmost_position<B>;
                search::template dispatch_with_height<B, 20>(board, occupied, [&]<class Cut, bool Check>(Cut &nb, std::integral_constant<bool, Check>) {
                    int const expected_cut = occupied + 3 <= 6 ? 6 : occupied + 3 <= 12 ? 12 : occupied + 3 <= 24 ? 24 : 48;
                    check(Cut::height == expected_cut, "dispatch cut height for occupied " + std::to_string(occupied));
                    bool expected_check = !(Cut::height < necessary) && occupied > unsigned(necessary);
                    check(Check == expected_check, "dispatch check_consecutive for occupied " + std::to_string(occupied) + ": got " + std::to_string(Check));
                    search::search_config cfg{};
                    cfg.allow_180 = true;
                    cfg.allow_softdrop = true;
                    cfg.allow_sonicdrop = true;
                    search::search_workspace<B, Cut> cut_ws(nb);
                    auto dispatched = search::template arrival_search<B, Check>(cut_ws, cfg, spawn, 0);
                    auto cut_rows = nb.to_row_bitboard();
                    Cut rebuilt{};
                    std::array<typename Cut::row_t, Cut::height> reclipped = {};
                    for (int y = 0; y < Cut::height; ++y)
                    {
                        reclipped[y] = cut_rows[y];
                    }
                    rebuilt.template from_row_bitboard<true>(reclipped);
                    search::search_workspace<B, Cut> manual_ws(rebuilt);
                    auto manual = expected_check ? search::template arrival_search<B, true>(manual_ws, cfg, spawn, 0)
                                                 : search::template arrival_search<B, false>(manual_ws, cfg, spawn, 0);
                    bool ok = true;
                    for (int o = 0; o < 4; ++o)
                    {
                        ok = ok && dispatched.normal_landings[o] == manual.normal_landings[o];
                        ok = ok && dispatched.rotation_landings[o] == manual.rotation_landings[o];
                    }
                    check(ok, "dispatch threads cut board and flag correctly for occupied " + std::to_string(occupied));
                    return 0;
                });
                return 0;
            });
        }
    }
}

int main()
{
#if defined(FAST_REACHABILITY_PERFT_ONLY) || defined(FAST_REACHABILITY_RUN_PERFT)
    run_perft_vectors();
#endif
    run_oracle_comparisons();
    run_directed_cases();
    run_workspace_tests();
    run_dispatch_tests();
    std::println("fast_reachability_tests: {} checks, {} failures", checks, failures);
    return failures == 0 ? 0 : 1;
}
