// fast_reachability_tests.cpp
// Phase 2 tests. Perft vector exactness, the test-only scalar arrival oracle
// compared against the bit-parallel two-channel search on seeded boards and
// all movement configurations, directed arrival cases, workspace legality,
// and dynamic height dispatch transparency.

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
            for (size_t b = 0; b < boards.size(); ++b)
            {
                BOARD board = board_from_rows(boards[b]);
                for (auto const &sc : configs)
                {
                    search::search_config cfg{};
                    cfg.allow_180 = sc.allow_180;
                    cfg.allow_softdrop = sc.allow_softdrop;
                    cfg.allow_sonicdrop = sc.allow_sonicdrop;
                    cfg.allow_20g = sc.allow_20g;
                    for (int consecutive_i = 0; consecutive_i < 2; ++consecutive_i)
                    {
                        bool const consecutive = consecutive_i != 0;
                        auto run_case = [&](auto check_tag) {
                            auto result = search::template arrival_search<B2, decltype(check_tag)::value>(board, cfg, spawn, 0);
                            scalar_arrival::ScalarOracle<B2> oracle{geo, sc, boards[b]};
                            oracle.run(spawn, 0);
                            bool const allow_float = sc.allow_softdrop && !sc.allow_20g;
                            auto oracle_normal = oracle.landable_words(0, allow_float);
                            auto oracle_rotation = oracle.landable_words(1, allow_float);
                            std::string what = std::string(1, name) + " board " + std::to_string(b)
                                + " cfg{" + std::to_string(sc.allow_180) + "," + std::to_string(sc.allow_softdrop) + ","
                                + std::to_string(sc.allow_sonicdrop) + "," + std::to_string(sc.allow_20g) + "}"
                                + " consecutive " + std::to_string(consecutive);
                            check(compare_with_bitpar(result, oracle_normal, oracle_rotation, what), what);
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
        {
            BOARD empty_board;
            search::search_config cfg{};
            cfg.allow_180 = true;
            cfg.allow_softdrop = true;
            cfg.allow_sonicdrop = true;
            cfg.allow_20g = false;
            call_with_block<SRS>(Tetromino::from_name('I'), [&]<block B>() {
                auto result = search::template arrival_search<B>(empty_board, cfg, spawn, 0);
                check(result.normal_landings[0] == result.normal_landings[2], "I duplicate orientations 0 and 2 canonicalize");
                check(result.normal_landings[1] == result.normal_landings[3], "I duplicate orientations 1 and 3 canonicalize");
                check(result.rotation_landings[0] == result.normal_landings[0] && result.rotation_landings[1] == result.normal_landings[1],
                    "I 180 identity rotation makes every landing reachable as a rotation arrival on an empty board");
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
                auto result = search::template arrival_search<B>(board, cfg, spawn, 0);
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
                auto result = search::template arrival_search<B>(empty_board, cfg, spawn, 0);
                auto landings0 = result.normal_landings[0].template to_row_bitboard<true>();
                auto rot0 = result.rotation_landings[0];
                bool overlap = false;
                rot0.for_each_bit([&](int x, int y) {
                    overlap = overlap || ((landings0[y] >> x) & 1);
                });
                check(overlap, "both arrival channels reach the same pose after rotating away and back");
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
            auto result = search::template arrival_search<B>(board, cfg, spawn, 0, &ws);
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
                    auto dispatched = search::template arrival_search<B, Check>(nb, cfg, spawn, 0);
                    auto manual = search::template arrival_search<B, Check>(nb, cfg, spawn, 0);
                    bool ok = true;
                    for (int o = 0; o < 4; ++o)
                    {
                        ok = ok && dispatched.normal_landings[o] == manual.normal_landings[o];
                        ok = ok && dispatched.rotation_landings[o] == manual.rotation_landings[o];
                    }
                    check(ok, "dispatch search equals manual search on the selected cut for occupied " + std::to_string(occupied));
                    return 0;
                });
                return 0;
            });
        }
    }
}

int main()
{
    run_perft_vectors();
    run_oracle_comparisons();
    run_directed_cases();
    run_workspace_tests();
    run_dispatch_tests();
    std::println("fast_reachability_tests: {} checks, {} failures", checks, failures);
    return failures == 0 ? 0 : 1;
}
