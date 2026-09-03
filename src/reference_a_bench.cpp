#include "scalar_arrival_oracle.h"

#include <chrono>
#include <print>
#include <vector>

using namespace reachability;
using BOARD = board_t<10, 48>;
using SRS = rule_set<Tetromino, SRS_Kicks>;

namespace {

double nanos(auto f, int iters)
{
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < iters; ++i)
    {
        f();
    }
    auto t1 = std::chrono::steady_clock::now();
    return static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()) / iters;
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

template <auto B>
bool check_reference_a(char name, std::vector<std::array<uint16_t, 48>> const &boards, coord spawn, search::search_config const &cfg,
    scalar_arrival::ScalarConfig const &sc, double &arrival_ns, double &oracle_ns)
{
    auto geo = scalar_arrival::make_geometry<B>();
    bool ok = true;
    arrival_ns = 0;
    oracle_ns = 0;
    int const iters = 20;
    for (auto const &rows : boards)
    {
        BOARD board = board_from_rows(rows);
        search::search_workspace<B, BOARD> ws(board);
        auto r1 = search::template arrival_search<B>(ws, cfg, spawn, 0);
        scalar_arrival::ScalarOracle<B> oracle{geo, sc, rows};
        oracle.run(spawn, 0);
        bool const allow_float = sc.allow_softdrop && !sc.allow_20g;
        auto ow = oracle.landable_words(0, allow_float);
        auto orw = oracle.landable_words(1, allow_float);
        for (int o = 0; o < B.orientations; ++o)
        {
            for (int w = 0; w < 8; ++w)
            {
                ok = ok && uint64_t(r1.normal_landings[o].logical_word(w)) == ow[o][w];
                ok = ok && uint64_t(r1.rotation_landings[o].logical_word(w)) == orw[o][w];
            }
        }
        arrival_ns += nanos([&]() {
            volatile auto r = search::template arrival_search<B>(ws, cfg, spawn, 0);
            (void)r;
        },
            iters);
        oracle_ns += nanos([&]() {
            scalar_arrival::ScalarOracle<B> o{geo, sc, rows};
            o.run(spawn, 0);
            volatile auto w = o.landable_words(0, allow_float);
            (void)w;
        },
            iters);
    }
    arrival_ns /= static_cast<double>(boards.size());
    oracle_ns /= static_cast<double>(boards.size());
    std::println("{}: reference-A match={} arrival={:.0f}ns oracle={:.0f}ns speedup={:.2f}x", name, ok, arrival_ns, oracle_ns,
        oracle_ns / arrival_ns);
    return ok;
}

}

int main()
{
    auto boards = scalar_arrival::make_corpus();
    coord const spawn{4, 20};
    search::search_config cfg{};
    cfg.allow_180 = true;
    cfg.allow_softdrop = true;
    cfg.allow_sonicdrop = true;
    cfg.allow_20g = false;
    scalar_arrival::ScalarConfig sc{true, true, true, false};
    bool ok = true;
    double t_arrival = 0;
    double t_oracle = 0;
    for (char name : std::string_view("TZSJLOI"))
    {
        double a = 0;
        double o = 0;
        call_with_block<SRS>(Tetromino::from_name(name), [&]<block B>() {
            ok = check_reference_a<B>(name, boards, spawn, cfg, sc, a, o) && ok;
            if (name == 'T')
            {
                t_arrival = a;
                t_oracle = o;
            }
            return 0;
        });
    }
    std::println("T speedup gate (need >= 2.00x): {:.2f}x {}", t_oracle / t_arrival, t_oracle / t_arrival >= 2.0 ? "PASS" : "FAIL");
    return ok && t_oracle / t_arrival >= 2.0 ? 0 : 1;
}
