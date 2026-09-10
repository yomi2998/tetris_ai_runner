#include "legacy_host_diag_common.h"
#include "partition_format.h"
#include "reach_corpus.h"

#include <print>
#include <string>
#include <vector>

namespace
{
    using namespace legacy_diag;

    int failures = 0;
    int checks = 0;

    void check(bool ok, std::string const &name)
    {
        ++checks;
        if (!ok)
        {
            ++failures;
            std::println(stderr, "SELFTEST fail: {}", name);
        }
    }



    std::uint64_t digest_production(value_Board const &board, value_Piece piece)
    {
        tetris::toj::MovementConfig movement;
        movement.allow_180 = true;
        std::vector<value_Candidate> out(tetris::toj::max_candidates_per_source());
        auto batch = tetris::toj::enumerate_candidates_into(board, piece, movement,
            std::span<value_Candidate>(out.data(), out.size()));
        std::uint64_t h = 1469598103934665603ull;
        for (std::size_t i = 0; i < batch->count; ++i)
        {
            h = fnv_mix(h, out[i].placement.packed());
            h = fnv_mix(h, static_cast<std::uint64_t>(out[i].arrival));
        }
        return h;
    }

    std::uint64_t digest_adapter(value_Board const &board, value_Piece piece,
        Counts &counts, std::uint64_t &unmappable)
    {
        auto start = value_Placement::unchecked(4, 20, 0);
        SuppliedStartResult result = enumerate_supplied_start(board, piece,
            tetris::toj::MovementConfig{}, reachability::coord{start.x(), start.y()}, 0);
        counts.candidates += result.candidates.size();
        std::uint64_t h = 1469598103934665603ull;
        for (auto const &c : result.candidates)
        {
            if (!tetris::toj::ExternalPoseTransform::to_legacy(piece, c.placement).has_value())
            {
                ++unmappable;
            }
            h = fnv_mix(h, c.placement.packed());
            h = fnv_mix(h, static_cast<std::uint64_t>(c.arrival));
        }
        return h;
    }
}

int main()
{
    auto boards = reach_corpus::make();
    Counts counts{};

    std::uint64_t parity_failures = 0;
    std::uint64_t unmappable_total = 0;
    for (auto idx : reach_corpus::legacy_subcorpus_indices())
    {
        std::array<std::uint16_t, 48> rows{};
        for (int y = 0; y < 40; ++y)
        {
            rows[static_cast<std::size_t>(y)] = boards[idx][static_cast<std::size_t>(y)];
        }
        value_Board board = value_Board::from_rows(rows);
        for (int p = 0; p < 7; ++p)
        {
            value_Piece piece = *tetris::try_from_char(reach_corpus::pieces[p]);
            std::uint64_t d_production = digest_production(board, piece);
            std::uint64_t d_adapter = digest_adapter(board, piece, counts, unmappable_total);
            if (d_production != d_adapter)
            {
                ++parity_failures;
            }
        }
    }
    check(parity_failures == 0, "adapter-versus-production ordered digest parity on c1");
    check(unmappable_total == 0, "c1 fully legacy-mappable");

    std::uint64_t parity_c2 = 0;
    std::uint64_t c2_checked = 0;
    bool const has_env_inputs = std::getenv("DIAG_INPUTS") != nullptr;
    {
        char const *inputs = std::getenv("DIAG_INPUTS");
        if (inputs != nullptr)
        {
            ReplayStream stream(inputs);
            std::uint32_t word_count = 0;
            std::uint64_t n_inputs = 0;
            if (stream.ok() && stream.header(word_count, n_inputs))
            {
                ReplayInput in;
                std::vector<partition_fmt::ValueCandidate> recorded;
                std::uint64_t digest_recorded = 1469598103934665603ull;
                while (stream.next(in, recorded) && c2_checked < 2000)
                {
                    auto rows = rows_from_words(in.words);
                    if (!map_rows_fit(rows))
                    {
                        continue;
                    }
                    auto piece_opt = tetris::try_from_char(in.piece);
                    if (!piece_opt.has_value())
                    {
                        continue;
                    }
                    value_Board board = board_from_words(in.words);
                    std::uint64_t recorded_digest = 1469598103934665603ull;
                    for (auto const &c : recorded)
                    {
                        recorded_digest = fnv_mix(recorded_digest, c.packed);
                        recorded_digest = fnv_mix(recorded_digest,
                            static_cast<std::uint64_t>(c.arrival));
                    }
                    std::uint64_t d_adapter = digest_adapter(board, *piece_opt, counts,
                        unmappable_total);
                    std::uint64_t d_production = digest_production(board, *piece_opt);
                    if (d_adapter != recorded_digest || d_production != recorded_digest)
                    {
                        if (parity_c2 < 3)
                        {
                            std::vector<value_Candidate> out(
                                4 * value_Board::width * value_Board::height * 2);
                            auto batch = tetris::toj::enumerate_candidates_into(board,
                                *piece_opt, tetris::toj::MovementConfig{},
                                std::span<value_Candidate>(out.data(), out.size()));
                        }
                        ++parity_c2;
                    }
                    digest_recorded = fnv_mix(digest_recorded, recorded_digest);
                    ++c2_checked;
                }
                (void)digest_recorded;
            }
        }
        check(parity_c2 == 0, "adapter and production match recorded candidates on replay corpus");
        if (has_env_inputs)
        {
            check(c2_checked > 0, "replay corpus sample consumed");
        }
    }

    {
        Counts a{};
        Counts b{};
        std::uint64_t unmappable_a = 0;
        std::uint64_t unmappable_b = 0;
        auto boards_all = reach_corpus::make();
        for (auto idx : reach_corpus::legacy_subcorpus_indices())
        {
            std::array<std::uint16_t, 48> rows{};
            for (int y = 0; y < 40; ++y)
            {
                rows[static_cast<std::size_t>(y)] = boards_all[idx][static_cast<std::size_t>(y)];
            }
            value_Board board = value_Board::from_rows(rows);
            for (int p = 0; p < 7; ++p)
            {
                value_Piece piece = *tetris::try_from_char(reach_corpus::pieces[p]);
                std::uint64_t da = digest_adapter(board, piece, a, unmappable_a);
                std::uint64_t db = digest_adapter(board, piece, b, unmappable_b);
                check(da == db, "adapter determinism");
            }
        }
    }

    {
        m_tetris::TetrisEngine<rule_toj::TetrisRule, ai_zzz::TOJ, AdapterSearch> engine;
        check(engine.prepare(10, 40), "legacy engine prepares with adapter search");
        engine.search_config()->allow_180 = true;
        std::uint64_t adapter_unmappable = 0;
        auto boards_all = reach_corpus::make();
        std::size_t const idx = reach_corpus::legacy_subcorpus_indices().front();
        std::array<std::uint16_t, 48> rows{};
        for (int y = 0; y < 40; ++y)
        {
            rows[static_cast<std::size_t>(y)] = boards_all[idx][static_cast<std::size_t>(y)];
        }
        m_tetris::TetrisMap map(10, 40);
        for (int y = 0; y < 40; ++y)
        {
            map.row[static_cast<std::size_t>(y)] = rows[static_cast<std::size_t>(y)];
        }
        rebuild_metadata(map);
        auto piece_opt = tetris::try_from_char('T');
        value_Board board = value_Board::from_rows(rows);
        auto spawn_status = tetris::toj::ExternalPoseTransform::to_legacy(*piece_opt,
            value_Placement::unchecked(4, 20, 0));
        check(spawn_status.has_value(), "t spawn maps to legacy domain");
        auto const *node = engine.context()->get(m_tetris::TetrisBlockStatus{'T',
            static_cast<std::int8_t>((*spawn_status)[0]),
            static_cast<std::int8_t>((*spawn_status)[1]),
            static_cast<std::uint8_t>((*spawn_status)[2])});
        check(node != nullptr, "spawn pose resolves to legacy node");
        AdapterSearch adapter;
        adapter.init(engine.context().get(), engine.search_config());
        adapter.counts = &counts;
        auto const *results = adapter.search(map, node, 1);
        check(results != nullptr && !results->empty(), "adapter search returns land points");
        check(counts.candidates > 0, "adapter counted candidates");
        bool path_ok = false;
        if (results != nullptr && !results->empty())
        {
            auto path = adapter.make_path(node, (*results)[0], map);
            path_ok = !path.empty();
        }
        check(path_ok, "adapter path generation succeeds for reachable landing");
        check(counts.path_relabels == 0,
            "no TerminalRotation-to-Normal path relabels (gate 3)");
        check(adapter.board_conversion_reserved_bytes() > 0, "memory accounting present");
        (void)board;
        (void)adapter_unmappable;
    }

    {
        std::array<std::uint16_t, 48> rows{};
        for (int y = 0; y < 40; ++y)
        {
            rows[static_cast<std::size_t>(y)] = 0x3ff;
        }
        value_Board board = value_Board::from_rows(rows);
        Counts local{};
        std::uint64_t unmappable = 0;
        std::uint64_t before = local.spawn_blocked;
        auto start = value_Placement::unchecked(4, 20, 0);
        SuppliedStartResult result = enumerate_supplied_start(board, value_Piece::T,
            tetris::toj::MovementConfig{}, reachability::coord{start.x(), start.y()}, 0);
        check(result.candidates.empty(), "blocked spawn yields no candidates");
        (void)before;
        (void)unmappable;
        ++checks;
    }

    {
        auto rows = std::array<std::uint16_t, 48>{};
        rows[45] = 0x3ff;
        check(!map_rows_fit(rows), "upper-row occupancy flagged out of legacy domain");
        check(!partition_fmt::rows40_47_empty(rows), "rows40_47_empty agrees");
    }

    {
        value_Board board = value_Board::from_rows(std::array<std::uint16_t, 48>{});
        Counts local{};
        std::uint64_t unmappable = 0;
        auto start = value_Placement::unchecked(4, 20, 0);
        SuppliedStartResult empty_board = enumerate_supplied_start(board, value_Piece::I,
            tetris::toj::MovementConfig{}, reachability::coord{start.x(), start.y()}, 0);
        check(!empty_board.candidates.empty(), "empty board enumerates candidates");
        (void)local;
        (void)unmappable;
    }

    {
        std::uint64_t hwm = 0;
        std::uint64_t rss = 0;
        check(read_vmhwm_vmrss(hwm, rss) && hwm > 0 && rss > 0, "memory fields readable");
    }


    {
        legacy_host_diag::audit_enabled() = true;
        legacy_host_diag::exact_only() = false;
        auto &audit = legacy_host_diag::EvalAudit<ai_zzz::TOJ::Result>::instance();
        struct ShimSearch : search_tspin::Search
        {
            AdapterSearch adapter;
            void init(m_tetris::TetrisContext const *context, Config const *config)
            {
                search_tspin::Search::init(context, config);
                adapter.init(context, config);
            }
            std::vector<TetrisNodeWithTSpinType> const *search(
                m_tetris::TetrisMap const &map, m_tetris::TetrisNode const *node, std::size_t depth)
            {
                return adapter.search(map, node, depth);
            }
            std::vector<char> make_path(m_tetris::TetrisNode const *node,
                TetrisNodeWithTSpinType const &land, m_tetris::TetrisMap const &map)
            {
                return adapter.make_path(node, land, map);
            }
        };
        m_tetris::TetrisEngine<rule_toj::TetrisRule, ai_zzz::TOJ, ShimSearch> engine;
        check(engine.prepare(10, 40), "audit test engine prepares");
        engine.memory_limit(256ull << 20);
        engine.search_config()->allow_rotate_move = false;
        engine.search_config()->allow_180 = true;
        engine.search_config()->allow_d = true;
        engine.search_config()->is_20g = false;
        engine.search_config()->last_rotate = false;
        static int const combo_table[] = {0, 0, 0, 1, 1, 2, 2, 3, 3, 4};
        engine.ai_config()->table = combo_table;
        engine.ai_config()->table_max = 10;

        std::uint64_t const epoch_before = audit.epoch();
        audit.reset();
        check(audit.epoch() == epoch_before + 1, "audit epoch advances on reset");

        m_tetris::TetrisMap map(10, 40);
        auto const *gen = engine.context()->generate('T');
        std::vector<char> next(7, 'T');
        bool first_run = true;
        auto run_once = [&](bool poison) {
            if (!first_run)
            {
                engine.update();
            }
            first_run = false;
            audit.reset();
            std::uint64_t const requests_before = audit.requests;
            std::uint64_t const fresh_before = audit.fresh_evals;
            std::uint64_t const collisions_before = audit.collisions;
            std::uint64_t const exact_before = audit.exact_hits;
            if (poison)
            {
                for (std::size_t depth = 0; depth < engine.diag_tt_depths(); ++depth)
                {
                    auto *table = engine.diag_tt(depth);
                    if (table == nullptr)
                    {
                        continue;
                    }
                    for (std::size_t i = 0; i < table->diag_size(); ++i)
                    {
                        auto &entry = table->diag_entries()[i];
                        if (entry.hash != 0)
                        {
                            entry.result = ai_zzz::TOJ::Result{-12345.0, -100, -100};
                        }
                    }
                }
            }
            auto result = engine.run_hold(map, gen, ' ', true, next.data(), next.size(),
                m_tetris::SearchBudget::by_iterations(200));
            std::uint64_t const requests_delta = audit.requests - requests_before;
            return std::tuple{result, requests_delta,
                audit.fresh_evals - fresh_before,
                audit.collisions - collisions_before,
                audit.exact_hits - exact_before};
        };

        auto [result, requests_delta, fresh_delta, collisions_delta, exact_delta]
            = run_once(false);
        check(requests_delta > 0, "audit hook counted eval requests");
        check(fresh_delta > 0, "audit recorded fresh evaluations");
        check(exact_delta + fresh_delta == requests_delta,
            "audit request identity holds");
        check(result.target != nullptr, "audit test run produced a selection");
        auto const clean_status = result.target.node->status;

        auto [result2, requests_delta2, fresh_delta2, collisions_delta2, exact_delta2]
            = run_once(false);
        check(result2.target != nullptr, "second audit run produced a selection");
        check(requests_delta2 > 0 && fresh_delta2 > 0,
            "epoch reset forces fresh re-evaluation");
        check(exact_delta2 + fresh_delta2 == requests_delta2,
            "audit request identity holds after reset");

        auto [result3, requests_delta3, fresh_delta3, collisions_delta3, exact_delta3]
            = run_once(true);
        check(collisions_delta3 > 0,
            "forced legacy-hash collision counted by the audit");
        check(result3.target != nullptr, "poisoned run still selects");

        legacy_host_diag::exact_only() = true;
        std::uint64_t const retargets_before = audit.retargets;
        auto [result4, requests_delta4, fresh_delta4, collisions_delta4, exact_delta4]
            = run_once(true);
        check(result4.target != nullptr, "exact-only poisoned run selects");
        check(audit.retargets - retargets_before == requests_delta4,
            "every audited eval retargeted the result pointer under exact_only");
        check(requests_delta4 > 0 && exact_delta4 + fresh_delta4 == requests_delta4,
            "exact-only run keeps the audit identity");
        check(audit.retargets > 0 && result4.target != nullptr
                && collisions_delta3 > 0,
            "exact_only retargeted results to audited exact entries despite poison");
        check(result4.target.node != nullptr,
            "exact-only run selection resolves to a pose node");
        legacy_host_diag::exact_only() = false;
        legacy_host_diag::audit_enabled() = false;
    }
    std::println(stdout, "SELFTEST legacy_host_diag: {} checks, {} failures", checks, failures);
    return failures == 0 ? 0 : 1;
}
