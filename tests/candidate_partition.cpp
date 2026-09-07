// candidate_partition: docs/phase7/count_partition_instrument_design.md §5.

#include "partition_classify.h"
#include "partition_format.h"
#include "partition_oracles.h"

#include "candidate_format.h"
#include "legacy_cmp_normalize.h"
#include "profile_value_runner.h"
#include "profile_value_support.h"
#include "tetris_board.h"
#include "tetris_engine.h"
#include "tetris_types.h"
#include "toj_policy.h"
#include "toj_rule.h"

#include "ai_zzz.h"
#include "rule_toj.h"
#include "search_tspin.h"
#include "tetris_core.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <print>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
    namespace engine_alias = tetris_engine;
    namespace support = profile_value;
    namespace fmt = partition_fmt;
    namespace cls = partition_class;
    namespace oracle = partition_oracle;

    int fail(std::string const &message)
    {
        std::println(stderr, "candidate_partition: {}", message);
        return 1;
    }

    bool read_param_file(std::string const &path, double *out)
    {
        FILE *file = std::fopen(path.c_str(), "rb");
        if (file == nullptr)
        {
            return false;
        }
        std::size_t const count = std::fread(out, sizeof(double), 29, file);
        std::fclose(file);
        return count == 29;
    }

    std::uint64_t khash_of(std::uint64_t cells_hash, std::uint8_t arrival)
    {
        return candfmt::fnv_mix(cells_hash, static_cast<std::uint64_t>(arrival));
    }

    std::uint64_t cells_hash_of(tetris::Piece piece, tetris::Placement placement)
    {
        auto cells = tetris::toj::cells(piece, placement);
        candfmt::Cells sorted = *cells;
        std::sort(sorted.begin(), sorted.end());
        return candfmt::occupancy_hash(sorted);
    }

    tetris::Placement unpack_placement(std::uint16_t packed)
    {
        int const x = packed & 0xf;
        int const y = (packed >> 4) & 0x3f;
        int const r = (packed >> 10) & 0x3;
        return tetris::Placement::unchecked(x, y, r);
    }

    std::array<std::uint16_t, 48> rows_from_words(std::vector<std::uint64_t> const &words)
    {
        tetris::Board::occupancy_t occ{};
        for (std::size_t i = 0; i < words.size(); ++i)
        {
            occ.set_logical_word(static_cast<int>(i), words[i]);
        }
        auto rb = occ.template to_row_bitboard<true>();
        std::array<std::uint16_t, 48> rows{};
        for (int y = 0; y < 48; ++y)
        {
            rows[static_cast<std::size_t>(y)] =
                static_cast<std::uint16_t>(rb[static_cast<std::size_t>(y)] & 0x3ff);
        }
        return rows;
    }

    void rebuild_metadata(m_tetris::TetrisMap &map)
    {
        map.roof = 0;
        map.count = 0;
        std::memset(map.top, 0, sizeof map.top);
        for (int y = 0; y < 40; ++y)
        {
            for (int x = 0; x < 10; ++x)
            {
                if (map.full(static_cast<std::size_t>(x), static_cast<std::size_t>(y)))
                {
                    map.top[x] = y + 1;
                    map.roof = std::max(map.roof, y + 1);
                    ++map.count;
                }
            }
        }
    }

    bool fits_pose(oracle::Engine &engine, tetris::Board const &board, char piece,
        int x, int y, int r)
    {
        m_tetris::TetrisNode node;
        if (!oracle::create_legacy(engine, piece, x, y, r, node))
        {
            return false;
        }
        for (int ry = 0; ry < node.height; ++ry)
        {
            for (int rx = 0; rx < node.width; ++rx)
            {
                if ((node.data[ry] >> (node.col + rx)) & 1)
                {
                    if (board.full(node.col + rx, node.row + ry))
                    {
                        return false;
                    }
                }
            }
        }
        return true;
    }

    struct DistinctKey
    {
        std::array<std::uint64_t, 8> words{};
        std::uint8_t word_count = 0;
        char piece = '?';
        std::uint8_t source = 0;

        bool operator==(DistinctKey const &) const = default;
    };

    struct DistinctKeyHash
    {
        std::size_t operator()(DistinctKey const &k) const
        {
            std::uint64_t h = 1469598103934665603ull;
            for (std::uint8_t i = 0; i < k.word_count; ++i)
            {
                h ^= k.words[i];
                h *= 1099511628211ull;
            }
            h ^= static_cast<std::uint64_t>(k.piece);
            h *= 1099511628211ull;
            h ^= k.source;
            h *= 1099511628211ull;
            return static_cast<std::size_t>(h);
        }
    };

    struct Args
    {
        std::map<std::string, std::string> kv;
        bool has(std::string const &k) const
        {
            return kv.count(k) != 0;
        }
        std::string get(std::string const &k, std::string dflt = "") const
        {
            auto it = kv.find(k);
            return it == kv.end() ? dflt : it->second;
        }
    };

    Args parse_args(int argc, char **argv)
    {
        Args args;
        for (int i = 2; i < argc; ++i)
        {
            std::string a = argv[i];
            if (a.rfind("--", 0) == 0 && i + 1 < argc
                && std::string(argv[i + 1]).rfind("--", 0) != 0)
            {
                args.kv[a] = argv[++i];
            }
            else
            {
                args.kv[a] = "";
            }
        }
        return args;
    }

    std::size_t parse_usize(std::string const &text)
    {
        return static_cast<std::size_t>(std::stoull(text));
    }

    int run_record(Args const &args)
    {
        support::Options opt;
        opt.seed = static_cast<std::uint32_t>(parse_usize(args.get("--seed", "1")));
        opt.moves = parse_usize(args.get("--moves", "200"));
        opt.warmup_moves = parse_usize(args.get("--warmup-moves", "0"));
        opt.maxdepth = parse_usize(args.get("--maxdepth", "6"));
        opt.param_file = args.get("--param-file", "");
        opt.hold = !args.has("--no-hold");
        opt.telemetry = true;
        opt.timers = true;
        if (args.has("--ms"))
        {
            opt.ms = std::stod(args.get("--ms"));
        }
        if (args.has("--iters"))
        {
            opt.iters = parse_usize(args.get("--iters"));
        }
        std::string const prefix = args.get("--record-out", "");
        if (prefix.empty())
        {
            return fail("record: --record-out PREFIX is required");
        }
        std::uint64_t budget_ms = 0;
        if (!support::resolve_budget_ms(opt, budget_ms))
        {
            return fail("record: time budget is not representable");
        }

        toj_policy::Config policy_config;
        policy_config.combo_table = support::combo_table;
        policy_config.combo_table_max = support::combo_table_max;
        policy_config.safe = 0;
        policy_config.parameters = toj_policy::Parameters::production_defaults();
        if (!opt.param_file.empty())
        {
            double theta[29];
            if (!read_param_file(opt.param_file, theta))
            {
                return fail("record: failed to read 29-double parameter file");
            }
            toj_policy::Parameters::from_theta(theta, policy_config.parameters);
        }
        toj_policy::Policy seed_policy;
        seed_policy.init(&policy_config);

        std::unordered_map<DistinctKey, std::size_t, DistinctKeyHash> index;
        std::vector<fmt::ValueInput> distinct;
        std::uint64_t calls = 0;
        std::uint64_t sum_nV = 0;
        std::uint64_t n_overflow = 0;
        bool record_failed = false;
        std::string record_error;

        engine_alias::EngineConfig engine_config;
        engine_config.policy = &policy_config;
        engine_config.movement.allow_180 = true;
        engine_config.telemetry_enabled = true;
        engine_config.timers_enabled = true;
        engine_config.expand_source_record =
            [&](engine_alias::ExpandSourceRecord const &rec) {
                ++calls;
                DistinctKey key;
                key.word_count = static_cast<std::uint8_t>(
                    tetris::Board::occupancy_t::word_count());
                for (int i = 0; i < tetris::Board::occupancy_t::word_count(); ++i)
                {
                    key.words[static_cast<std::size_t>(i)] =
                        rec.board->occupancy().logical_word(i);
                }
                key.piece = tetris::to_char(rec.played);
                key.source =
                    rec.source == engine_alias::BranchSource::Current ? 0 : 1;
                sum_nV += rec.candidates.size();
                n_overflow += rec.overflow ? 1 : 0;
                auto it = index.find(key);
                if (it != index.end())
                {
                    fmt::ValueInput &prev = distinct[it->second];
                    bool same = prev.candidates.size() == rec.candidates.size()
                        && prev.overflow == rec.overflow;
                    for (std::size_t i = 0; same && i < rec.candidates.size(); ++i)
                    {
                        auto const &a = prev.candidates[i];
                        auto const &b = rec.candidates[i];
                        same = a.packed == b.candidate.placement.packed()
                            && a.arrival
                                == static_cast<std::uint8_t>(b.candidate.arrival)
                            && a.apply_ok == (b.apply_ok ? 1 : 0)
                            && (!b.apply_ok
                                || (a.spin == static_cast<std::uint8_t>(b.outcome.spin)
                                    && a.clear_count
                                        == static_cast<std::uint8_t>(b.outcome.clear_count)
                                    && a.lockout == (b.outcome.lockout ? 1 : 0)
                                    && a.result_hash40 == b.result_hash40))
                            && a.survivor == (b.survivor ? 1 : 0);
                    }
                    if (!same)
                    {
                        record_failed = true;
                        record_error = "repeat enumeration disagrees with first record";
                        return;
                    }
                    ++prev.multiplicity;
                    return;
                }
                if (distinct.size() >= fmt::distinct_cap)
                {
                    record_failed = true;
                    record_error = "distinct-input cap exceeded";
                    return;
                }
                fmt::ValueInput in;
                in.words.assign(key.words.begin(),
                    key.words.begin() + key.word_count);
                in.piece = key.piece;
                in.source = key.source;
                in.overflow = rec.overflow;
                in.multiplicity = 1;
                for (auto const &b : rec.candidates)
                {
                    fmt::ValueCandidate c;
                    c.packed = b.candidate.placement.packed();
                    c.arrival = static_cast<std::uint8_t>(b.candidate.arrival);
                    c.apply_ok = b.apply_ok ? 1 : 0;
                    c.spin = static_cast<std::uint8_t>(b.outcome.spin);
                    c.clear_count =
                        static_cast<std::uint8_t>(b.outcome.clear_count);
                    c.lockout = b.outcome.lockout ? 1 : 0;
                    c.survivor = b.survivor ? 1 : 0;
                    c.result_hash40 = b.result_hash40;
                    in.candidates.push_back(c);
                }
                index[key] = distinct.size();
                distinct.push_back(std::move(in));
            };

        engine_alias::Engine engine;
        if (!engine.init(engine_config))
        {
            return fail("record: engine init failed");
        }
        if (!distinct.empty() || calls != 0)
        {
            return fail("record: internal error (non-empty recorder state)");
        }

        support::Runner::Config runner_config;
        runner_config.maxdepth = opt.maxdepth;
        runner_config.hold = opt.hold;
        runner_config.iters = opt.iters;
        runner_config.budget_ms = budget_ms;
        FILE *moves_file = std::fopen((prefix + ".moves.tsv").c_str(), "w");
        if (moves_file == nullptr)
        {
            return fail("record: cannot open moves output");
        }
        std::println(moves_file, "move\tkind\twidening\tparents\tenum_calls\t"
                                 "raw\tunique\trule_apps\tpolicy_trans\tmaterialized\t"
                                 "merges\trefused\texhausted\tpending\ttrans_used");
        std::size_t move_index = 0;
        runner_config.on_move = [&](support::MoveRecord const &record) {
            char kind = '?';
            switch (record.kind)
            {
            case support::MoveRecord::Kind::Placed: kind = 'P'; break;
            case support::MoveRecord::Kind::SpawnDeath: kind = 'S'; break;
            case support::MoveRecord::Kind::LockoutDeath: kind = 'L'; break;
            case support::MoveRecord::Kind::Invalid: kind = 'I'; break;
            }
            std::println(moves_file, "{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t"
                                     "{}\t{}\t{}\t{}\t{}",
                move_index++, kind, record.stats.widening_passes,
                record.stats.expanded_parents, record.stats.enumeration_calls,
                record.stats.raw_kernel_landings, record.stats.unique_candidates,
                record.stats.rule_applications, record.stats.policy_transitions,
                record.stats.materialized_nodes, record.stats.transposition_merges,
                record.stats.promotions_refused,
                record.stats.transposition_exhausted ? 1 : 0,
                record.stats.pending_occupancy, record.table_used);
        };

        support::Runner runner(
            policy_config, seed_policy, engine, opt.seed, runner_config);
        support::Totals totals;
        std::size_t moves_done = 0;
        std::size_t const total_moves = opt.warmup_moves + opt.moves;
        bool valid = true;
        std::string invalid_reason;
        auto t_total0 = std::chrono::steady_clock::now();
        bool timer_started = opt.warmup_moves == 0;
        while (valid && moves_done < total_moves)
        {
            bool const warming = moves_done < opt.warmup_moves;
            support::MoveRecord record = runner.step();
            if (record.kind == support::MoveRecord::Kind::Invalid)
            {
                valid = false;
                invalid_reason = record.invalid_reason;
                break;
            }
            if (!warming)
            {
                if (record.kind == support::MoveRecord::Kind::Placed)
                {
                    totals.add(record, runner.last_attack());
                }
                else
                {
                    totals.add(record, 0);
                    totals.add_death();
                }
            }
            if (warming && moves_done + 1 == opt.warmup_moves)
            {
                t_total0 = std::chrono::steady_clock::now();
                timer_started = true;
            }
            ++moves_done;
        }
        std::fclose(moves_file);
        if (!timer_started)
        {
            t_total0 = std::chrono::steady_clock::now();
        }
        if (!valid)
        {
            return fail("record: invalid run: " + invalid_reason);
        }
        if (record_failed)
        {
            return fail("record: recorder error: " + record_error);
        }
        double total_sec =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - t_total0)
                .count();
        if (total_sec <= 0)
        {
            total_sec = 1e-9;
        }

        {
            fmt::Writer w(prefix + ".inputs.bin");
            if (!w.ok())
            {
                return fail("record: cannot open inputs output");
            }
            w.put(fmt::inputs_magic, 8);
            w.put_value(fmt::format_version);
            std::uint32_t wc =
                static_cast<std::uint32_t>(tetris::Board::occupancy_t::word_count());
            w.put_value(wc);
            std::uint64_t n = distinct.size();
            w.put_value(n);
            for (auto const &in : distinct)
            {
                for (auto word : in.words)
                {
                    w.put_value(word);
                }
                w.put_value(static_cast<std::uint8_t>(in.piece));
                w.put_value(in.source);
                w.put_value(static_cast<std::uint8_t>(in.overflow ? 1 : 0));
                w.put_value(static_cast<std::uint8_t>(0));
                w.put_value(in.multiplicity);
                std::uint32_t nc = static_cast<std::uint32_t>(in.candidates.size());
                w.put_value(nc);
                for (auto const &c : in.candidates)
                {
                    w.put_value(c.packed);
                    w.put_value(c.arrival);
                    w.put_value(c.apply_ok);
                    w.put_value(c.spin);
                    w.put_value(c.clear_count);
                    w.put_value(c.lockout);
                    w.put_value(c.survivor);
                    w.put_value(static_cast<std::uint8_t>(0));
                    w.put_value(static_cast<std::uint8_t>(0));
                    w.put_value(c.result_hash40);
                }
            }
        }

        std::uint64_t n_ood = 0;
        for (auto const &in : distinct)
        {
            if (!fmt::rows40_47_empty(rows_from_words(in.words)))
            {
                ++n_ood;
            }
        }
        bool roundtrip_ok = false;
        if (!distinct.empty())
        {
            auto rows = rows_from_words(distinct[0].words);
            tetris::Board board = tetris::Board::from_rows(rows);
            roundtrip_ok = true;
            for (int i = 0; i < tetris::Board::occupancy_t::word_count(); ++i)
            {
                if (board.occupancy().logical_word(i) != distinct[0].words[i])
                {
                    roundtrip_ok = false;
                }
            }
        }
        {
            FILE *f = std::fopen((prefix + ".run_totals.tsv").c_str(), "w");
            if (f == nullptr)
            {
                return fail("record: cannot open totals output");
            }
            auto kv = [&](char const *k, long long v) {
                std::println(f, "{}\t{}", k, v);
            };
            kv("seed", opt.seed);
            kv("moves", static_cast<long long>(opt.moves));
            kv("maxdepth", static_cast<long long>(opt.maxdepth));
            kv("iters", static_cast<long long>(opt.iters));
            kv("budget_ms", static_cast<long long>(budget_ms));
            kv("unique_candidates", totals.unique_candidates);
            kv("policy_transitions", totals.policy_transitions);
            kv("enum_calls", totals.enum_calls);
            kv("raw_landings", totals.raw_landings);
            kv("rule_transitions", totals.rule_transitions);
            kv("parents", totals.parents);
            kv("widening_iters", totals.widening_iters);
            kv("materialized_nodes", totals.materialized_nodes);
            kv("transposition_merges", totals.transposition_merges);
            kv("texhaust_moves", totals.texhaust_moves);
            kv("recorder_calls", static_cast<long long>(calls));
            kv("recorder_sum_nV", static_cast<long long>(sum_nV));
            kv("distinct_inputs", static_cast<long long>(distinct.size()));
            kv("distinct_ood", static_cast<long long>(n_ood));
            kv("recorder_overflow", static_cast<long long>(n_overflow));
            kv("selfcheck_uniques",
                sum_nV == static_cast<std::uint64_t>(totals.unique_candidates) ? 1 : 0);
            kv("selfcheck_calls",
                calls == static_cast<std::uint64_t>(totals.enum_calls) ? 1 : 0);
            kv("roundtrip_ok", roundtrip_ok || distinct.empty() ? 1 : 0);
            std::fclose(f);
        }

        support::V3Row row = support::build_v3_row(totals, opt, total_sec,
            0, static_cast<std::int64_t>(engine.retained_bytes()),
            static_cast<std::int64_t>(engine.arena_reserved_bytes()),
            static_cast<std::int64_t>(engine.idmap_reserved_bytes()), opt.telemetry);
        std::println("{}", support::format_v3(row));
        if (sum_nV != static_cast<std::uint64_t>(totals.unique_candidates)
            || calls != static_cast<std::uint64_t>(totals.enum_calls)
            || (!distinct.empty() && !roundtrip_ok))
        {
            return fail("record: recorder self-check failed");
        }
        return 0;
    }

    bool read_inputs_bin(std::string const &path, std::uint32_t &word_count,
        std::vector<fmt::ValueInput> &out)
    {
        fmt::Reader r(path);
        if (!r.ok())
        {
            return false;
        }
        char magic[8];
        std::uint32_t version = 0;
        std::uint64_t n = 0;
        if (!r.get(magic, 8) || std::memcmp(magic, fmt::inputs_magic, 8) != 0
            || !r.get_value(version) || version != fmt::format_version
            || !r.get_value(word_count) || word_count == 0 || word_count > 16
            || !r.get_value(n) || n > fmt::distinct_cap)
        {
            return false;
        }
        out.reserve(static_cast<std::size_t>(n));
        for (std::uint64_t i = 0; i < n; ++i)
        {
            fmt::ValueInput in;
            in.words.resize(word_count);
            for (std::uint32_t k = 0; k < word_count; ++k)
            {
                if (!r.get_value(in.words[k]))
                {
                    return false;
                }
            }
            std::uint8_t piece = 0;
            std::uint8_t overflow = 0;
            std::uint8_t reserved = 0;
            std::uint32_t nc = 0;
            if (!r.get_value(piece) || !r.get_value(in.source)
                || !r.get_value(overflow) || !r.get_value(reserved)
                || !r.get_value(in.multiplicity) || !r.get_value(nc)
                || nc > 100000)
            {
                return false;
            }
            in.piece = static_cast<char>(piece);
            in.overflow = overflow != 0;
            in.candidates.reserve(nc);
            for (std::uint32_t j = 0; j < nc; ++j)
            {
                fmt::ValueCandidate c;
                std::uint8_t z1 = 0;
                std::uint8_t z2 = 0;
                if (!r.get_value(c.packed) || !r.get_value(c.arrival)
                    || !r.get_value(c.apply_ok) || !r.get_value(c.spin)
                    || !r.get_value(c.clear_count) || !r.get_value(c.lockout)
                    || !r.get_value(c.survivor) || !r.get_value(z1)
                    || !r.get_value(z2) || !r.get_value(c.result_hash40))
                {
                    return false;
                }
                in.candidates.push_back(c);
            }
            out.push_back(std::move(in));
        }
        return true;
    }

    oracle::Engine make_legacy_engine()
    {
        oracle::Engine engine;
        if (!engine.prepare(10, 40))
        {
            std::println(stderr, "candidate_partition: engine.prepare(10, 40) failed");
            std::exit(1);
        }
        engine.memory_limit(256ull << 20);
        engine.search_config()->allow_rotate_move = false;
        engine.search_config()->allow_180 = true;
        engine.search_config()->allow_d = true;
        engine.search_config()->allow_D = true;
        engine.search_config()->allow_LR = true;
        engine.search_config()->allow_nont_d = false;
        engine.search_config()->is_20g = false;
        engine.search_config()->last_rotate = false;
        return engine;
    }

    void process_search_results(search_tspin::Search &search, oracle::Engine &engine,
        m_tetris::TetrisMap const &map, auto const *results,
        std::vector<fmt::LegacyPoint> &out)
    {
        for (auto const &land : *results)
        {
            fmt::LegacyPoint p;
            p.x = land.node->status.x;
            p.y = land.node->status.y;
            p.r = land.node->status.r;
            p.spin_class = land.type == search_tspin::Search::TSpinType::TSpin
                ? 1
                : (land.type == search_tspin::Search::TSpinType::TSpinMini ? 2 : 0);
            p.last_rotate = land.is_last_rotate ? 1 : 0;
            p.status_bits = land.node->status.status;
            m_tetris::TetrisMap copy = map;
            std::size_t clear = land.node->attach(engine.context().get(), copy);
            p.clear_count = static_cast<std::uint16_t>(clear);
            auto spin = search.classify(map, land.node, land.is_last_rotate != 0, clear);
            p.legacy_spin = spin == search_tspin::Search::TSpinType::TSpin
                ? 1
                : (spin == search_tspin::Search::TSpinType::TSpinMini ? 2 : 0);
            int rel = land.node->row - 20;
            if (rel < -128)
            {
                rel = -128;
            }
            if (rel > 127)
            {
                rel = 127;
            }
            p.node_row_rel20 = static_cast<std::int8_t>(rel);
            std::uint64_t h = 1469598103934665603ull;
            for (int y = 0; y < 40; ++y)
            {
                h ^= static_cast<std::uint64_t>(copy.row[y] & 0x3ff);
                h *= 1099511628211ull;
            }
            p.result_hash40 = h;
            out.push_back(p);
        }
    }

    int run_drive(Args const &args)
    {
        std::string const inputs_path = args.get("--inputs", "");
        std::string const legacy_path = args.get("--legacy-out", "");
        if (inputs_path.empty() || legacy_path.empty())
        {
            return fail("drive: --inputs and --legacy-out are required");
        }
        std::uint32_t word_count = 0;
        std::vector<fmt::ValueInput> inputs;
        if (!read_inputs_bin(inputs_path, word_count, inputs))
        {
            return fail("drive: cannot read inputs file");
        }
        oracle::Engine engine = make_legacy_engine();
        search_tspin::Search search;
        search.init(engine.context().get(), engine.search_config());

        fmt::Writer w(legacy_path);
        if (!w.ok())
        {
            return fail("drive: cannot open legacy output");
        }
        w.put(fmt::legacy_magic, 8);
        w.put_value(fmt::format_version);
        std::uint64_t n_driven = 0;
        std::uint64_t n_ood = 0;
        std::uint64_t n_divergent = 0;
        auto put_point = [&](fmt::LegacyPoint const &p) {
            w.put_value(p.x);
            w.put_value(p.y);
            w.put_value(p.r);
            w.put_value(p.spin_class);
            w.put_value(p.last_rotate);
            w.put_value(p.status_bits);
            w.put_value(p.clear_count);
            w.put_value(p.legacy_spin);
            w.put_value(p.node_row_rel20);
            w.put_value(p.result_hash40);
        };
        for (std::size_t i = 0; i < inputs.size(); ++i)
        {
            auto const &in = inputs[i];
            auto rows = rows_from_words(in.words);
            if (!fmt::rows40_47_empty(rows))
            {
                ++n_ood;
                continue;
            }
            m_tetris::TetrisMap map(10, 40);
            for (int y = 0; y < 40; ++y)
            {
                map.row[y] = rows[static_cast<std::size_t>(y)];
            }
            rebuild_metadata(map);
            tetris::Board board = tetris::Board::from_rows(rows);
            m_tetris::TetrisNode const *gen = engine.context()->generate(in.piece);
            m_tetris::TetrisNode const *sugg =
                engine.spawn_node(in.piece, 0, false, map);
            bool divergent = sugg->status.x != gen->status.x
                || sugg->status.y != gen->status.y || sugg->status.r != gen->status.r;
            n_divergent += divergent ? 1 : 0;
            auto const *results = search.search(map, gen, 1);
            std::vector<fmt::LegacyPoint> points;
            process_search_results(search, engine, map, results, points);
            std::vector<fmt::LegacyPoint> spawn_points;
            if (divergent)
            {
                auto const *results2 = search.search(map, sugg, 1);
                process_search_results(search, engine, map, results2, spawn_points);
            }
            w.put_value(fmt::hash_rows40(rows));
            w.put_value(static_cast<std::uint8_t>(in.piece));
            w.put_value(gen->status.x);
            w.put_value(gen->status.y);
            w.put_value(static_cast<std::uint8_t>(gen->status.r));
            w.put_value(sugg->status.x);
            w.put_value(sugg->status.y);
            w.put_value(static_cast<std::uint8_t>(sugg->status.r));
            w.put_value(static_cast<std::uint8_t>(
                fits_pose(engine, board, in.piece, gen->status.x, gen->status.y,
                    gen->status.r)
                    ? 1
                    : 0));
            w.put_value(static_cast<std::uint8_t>(
                fits_pose(engine, board, in.piece, sugg->status.x, sugg->status.y,
                    sugg->status.r)
                    ? 1
                    : 0));
            w.put_value(static_cast<std::uint8_t>(divergent ? 1 : 0));
            std::uint32_t nr = static_cast<std::uint32_t>(points.size());
            w.put_value(nr);
            for (auto const &p : points)
            {
                put_point(p);
            }
            if (divergent)
            {
                std::uint32_t ns = static_cast<std::uint32_t>(spawn_points.size());
                w.put_value(ns);
                for (auto const &p : spawn_points)
                {
                    put_point(p);
                }
            }
            ++n_driven;
            if ((i + 1) % 2000 == 0)
            {
                std::println(stderr, "drive: {}/{} inputs (driven {}, ood {}, divergent {})",
                    i + 1, inputs.size(), n_driven, n_ood, n_divergent);
            }
        }
        std::println(stderr, "drive: done inputs={} driven={} ood_skipped={} divergent={}",
            inputs.size(), n_driven, n_ood, n_divergent);
        return 0;
    }

    bool read_legacy_bin(std::string const &path, std::vector<fmt::LegacyInput> &out)
    {
        fmt::Reader r(path);
        if (!r.ok())
        {
            return false;
        }
        char magic[8];
        std::uint32_t version = 0;
        if (!r.get(magic, 8) || std::memcmp(magic, fmt::legacy_magic, 8) != 0
            || !r.get_value(version) || version != fmt::format_version)
        {
            return false;
        }
        auto get_point = [&](fmt::LegacyPoint &p) {
            std::int8_t rel = 0;
            return r.get_value(p.x) && r.get_value(p.y) && r.get_value(p.r)
                && r.get_value(p.spin_class) && r.get_value(p.last_rotate)
                && r.get_value(p.status_bits) && r.get_value(p.clear_count)
                && r.get_value(p.legacy_spin) && r.get_value(rel)
                && r.get_value(p.result_hash40)
                && (p.node_row_rel20 = rel, true);
        };
        while (true)
        {
            std::uint64_t h40 = 0;
            if (!r.get_value(h40))
            {
                return r.clean_eof();
            }
            fmt::LegacyInput in;
            in.hash40 = h40;
            std::uint8_t piece = 0;
            std::int8_t gx = 0;
            std::int8_t gy = 0;
            std::uint8_t gr = 0;
            std::int8_t sx = 0;
            std::int8_t sy = 0;
            std::uint8_t sr = 0;
            std::uint8_t gen_fits = 0;
            std::uint8_t spawn_fits = 0;
            std::uint8_t spawn_driven = 0;
            std::uint32_t nr = 0;
            if (!r.get_value(piece) || !r.get_value(gx) || !r.get_value(gy)
                || !r.get_value(gr) || !r.get_value(sx) || !r.get_value(sy)
                || !r.get_value(sr) || !r.get_value(gen_fits)
                || !r.get_value(spawn_fits) || !r.get_value(spawn_driven)
                || !r.get_value(nr) || nr > 100000)
            {
                return false;
            }
            in.piece = static_cast<char>(piece);
            in.gen_x = gx;
            in.gen_y = gy;
            in.gen_r = gr;
            in.spawn_x = sx;
            in.spawn_y = sy;
            in.spawn_r = sr;
            in.gen_fits = gen_fits != 0;
            in.spawn_fits = spawn_fits != 0;
            in.spawn_driven = spawn_driven != 0;
            in.points.reserve(nr);
            for (std::uint32_t j = 0; j < nr; ++j)
            {
                fmt::LegacyPoint p;
                if (!get_point(p))
                {
                    return false;
                }
                in.points.push_back(p);
            }
            if (in.spawn_driven)
            {
                std::uint32_t ns = 0;
                if (!r.get_value(ns) || ns > 100000)
                {
                    return false;
                }
                in.spawn_points.reserve(ns);
                for (std::uint32_t j = 0; j < ns; ++j)
                {
                    fmt::LegacyPoint p;
                    if (!get_point(p))
                    {
                        return false;
                    }
                    in.spawn_points.push_back(p);
                }
            }
            out.push_back(std::move(in));
        }
    }

    std::string hex16(std::uint64_t v)
    {
        char buf[17];
        std::snprintf(buf, sizeof buf, "%016llx", (unsigned long long)v);
        return buf;
    }

    int run_classify(Args const &args)
    {
        std::string const inputs_path = args.get("--inputs", "");
        std::string const legacy_path = args.get("--legacy", "");
        std::string const prefix = args.get("--out", "");
        if (inputs_path.empty() || legacy_path.empty() || prefix.empty())
        {
            return fail("classify: --inputs, --legacy and --out are required");
        }
        std::uint32_t word_count = 0;
        std::vector<fmt::ValueInput> inputs;
        if (!read_inputs_bin(inputs_path, word_count, inputs))
        {
            return fail("classify: cannot read inputs file");
        }
        std::vector<fmt::LegacyInput> legacy;
        if (!read_legacy_bin(legacy_path, legacy))
        {
            return fail("classify: cannot read legacy file");
        }
        std::map<std::pair<std::uint64_t, char>, std::size_t> legacy_index;
        for (std::size_t i = 0; i < legacy.size(); ++i)
        {
            legacy_index[{legacy[i].hash40, legacy[i].piece}] = i;
        }
        FILE *fu = std::fopen((prefix + ".uniques.csv").c_str(), "w");
        FILE *ft = std::fopen((prefix + ".trans.csv").c_str(), "w");
        FILE *fd = std::fopen((prefix + ".defects.tsv").c_str(), "w");
        if (fu == nullptr || ft == nullptr || fd == nullptr)
        {
            return fail("classify: cannot open output files");
        }
        std::println(fu, "input_id,hash40,piece,source,mult,nV,nL_norm,nL_sem,nRaw,"
                         "delta,a,b_counted,b_spin,b_opaque,b_collapsed,c1,c2v,c2l,c2div,"
                         "dV,dL,dV_om,nL_spawn_sem,delta_spawn,ood,c3,missing,ok");
        std::println(ft, "input_id,hash40,piece,source,nCV,nCL,deltaC,a_t,b_t,"
                         "c1_t_V,c1_t_L,dVt,dLt,ood,ok");
        std::println(fd, "input_id\tleg\tkind\tkhash\tpacked_or_pose\tdetail");
        oracle::Engine engine = make_legacy_engine();

        long t_delta = 0;
        long t_a = 0;
        long t_b = 0;
        long t_bspin = 0;
        long t_bopaque = 0;
        long t_bcoll = 0;
        long t_c1 = 0;
        long t_c2v = 0;
        long t_c2l = 0;
        long t_dV = 0;
        long t_dL = 0;
        long t_om = 0;
        long t_nV = 0;
        long t_nLsem = 0;
        long t_nLnorm = 0;
        long t_nRaw = 0;
        long t_dC = 0;
        long t_nCV = 0;
        long t_nCL = 0;
        long t_at = 0;
        long t_bt = 0;
        long t_c1t = 0;
        long t_dVt = 0;
        long t_dLt = 0;
        long n_S = 0;
        long n_ood = 0;
        long ood_nV = 0;
        long n_missing = 0;
        long n_overflow = 0;
        long n_divergent = 0;
        bool all_ok = true;

        for (std::size_t id = 0; id < inputs.size(); ++id)
        {
            auto const &in = inputs[id];
            auto rows = rows_from_words(in.words);
            std::uint64_t const h40 = fmt::hash_rows40(rows);
            long const nV_cands = static_cast<long>(in.candidates.size());
            if (!fmt::rows40_47_empty(rows))
            {
                ++n_ood;
                ood_nV += nV_cands;
                std::println(fu, "{},{},{},{},{},{},0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,-1,0,1,{},0,1",
                    id, hex16(h40), in.piece, (int)in.source, in.multiplicity,
                    nV_cands, nV_cands);
                long nCV_ood = 0;
                for (auto const &c : in.candidates)
                {
                    nCV_ood += c.survivor ? 1 : 0;
                }
                std::println(ft, "{},{},{},{},{},0,{},0,0,0,0,0,0,1,1", id,
                    hex16(h40), in.piece, (int)in.source, nCV_ood, nCV_ood);
                continue;
            }
            n_overflow += in.overflow ? 1 : 0;
            auto lit = legacy_index.find({h40, in.piece});
            if (lit == legacy_index.end())
            {
                ++n_missing;
                all_ok = false;
                std::println(fu, "{},{},{},{},{},{},0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,-1,0,0,0,1,0",
                    id, hex16(h40), in.piece, (int)in.source, in.multiplicity,
                    nV_cands);
                std::println(ft, "{},{},{},{},0,0,0,0,0,0,0,0,0,0,0", id,
                    hex16(h40), in.piece, (int)in.source);
                continue;
            }
            auto const &L = legacy[lit->second];
            n_divergent += L.spawn_driven ? 1 : 0;
            auto piece = tetris::try_from_char(in.piece);
            if (!piece.has_value())
            {
                return fail("classify: bad piece char in inputs");
            }
            tetris::Board board48 = tetris::Board::from_rows(rows);

            std::vector<cls::ValueCand> vc;
            vc.reserve(in.candidates.size());
            std::map<std::uint64_t, std::vector<std::uint32_t>> kh_packed;
            bool cells_fail = false;
            for (auto const &c : in.candidates)
            {
                tetris::Placement pl = unpack_placement(c.packed);
                auto cells = tetris::toj::cells(*piece, pl);
                if (!cells.has_value())
                {
                    cells_fail = true;
                    break;
                }
                candfmt::Cells sorted = *cells;
                std::sort(sorted.begin(), sorted.end());
                std::uint8_t arr = (*piece == tetris::Piece::T) ? c.arrival : 0;
                std::uint64_t kh = khash_of(candfmt::occupancy_hash(sorted), arr);
                cls::ValueCand v;
                v.khash = kh;
                v.apply_ok = c.apply_ok != 0;
                v.spin = c.spin;
                v.clear_count = c.clear_count;
                v.lockout = c.lockout != 0;
                v.survivor = c.survivor != 0;
                v.res40 = c.result_hash40;
                v.packed = c.packed;
                v.arrival = arr;
                vc.push_back(v);
                kh_packed[kh].push_back(c.packed);
            }
            if (cells_fail)
            {
                return fail("classify: kernel placement without cells");
            }
            std::vector<cls::LegacyPoint> lp;
            lp.reserve(L.points.size());
            for (auto const &p : L.points)
            {
                auto key = legacy_cmp::normalize_land_point(in.piece, p.x, p.y,
                    p.r, p.spin_class, p.last_rotate != 0, p.status_bits);
                std::uint8_t arr =
                    (in.piece == 'T' && p.last_rotate) ? 1 : 0;
                cls::LegacyPoint q;
                q.has_k = key.matched;
                q.khash = key.matched ? khash_of(key.cells_hash, arr) : 0;
                q.matched = key.matched;
                q.channel = key.channel;
                q.opaque = key.opaque;
                q.clear_count = p.clear_count;
                q.spin = p.legacy_spin;
                q.lockout = p.node_row_rel20 >= 0;
                q.res40 = p.result_hash40;
                lp.push_back(q);
            }
            bool const can_spawn = tetris::toj::can_spawn(board48, *piece);

            std::set<std::uint64_t> vset;
            std::set<std::uint64_t> lset;
            for (auto const &v : vc)
            {
                vset.insert(v.khash);
            }
            for (auto const &q : lp)
            {
                if (q.has_k)
                {
                    lset.insert(q.khash);
                }
            }
            bool need_oracle = false;
            for (auto k : vset)
            {
                if (lset.count(k) == 0)
                {
                    need_oracle = true;
                }
            }
            if (need_oracle)
            {
                m_tetris::TetrisMap map(10, 40);
                for (int y = 0; y < 40; ++y)
                {
                    map.row[y] = rows[static_cast<std::size_t>(y)];
                }
                rebuild_metadata(map);
                oracle::LegacyReplay replay{engine, map, board48};
                replay.run(in.piece);
                std::map<std::uint64_t, std::pair<bool, bool>> verdict;
                for (auto const &v : vc)
                {
                    if (lset.count(v.khash) != 0)
                    {
                        continue;
                    }
                    auto it = verdict.find(v.khash);
                    if (it != verdict.end())
                    {
                        continue;
                    }
                    tetris::Placement pl = unpack_placement(v.packed);
                    tetris::ArrivalClass arr = v.arrival != 0
                        ? tetris::ArrivalClass::TerminalRotation
                        : tetris::ArrivalClass::Normal;
                    tetris::Candidate cand{pl, arr};
                    bool s = oracle::scalar_reachable(rows, *piece, cand);
                    bool r = oracle::command_reachable(replay, in.piece, *piece,
                        oracle::cells_key(*piece, pl), arr);
                    verdict[v.khash] = {s, r};
                }
                for (auto &v : vc)
                {
                    auto it = verdict.find(v.khash);
                    if (it != verdict.end())
                    {
                        v.scalar_ok = it->second.first;
                        v.replay_ok = it->second.second;
                    }
                }
            }

            cls::UniquesRow row = cls::classify_uniques(vc, lp, can_spawn);

            std::vector<cls::TransChild> tvc;
            std::vector<cls::TransChild> tlc;
            for (auto const &v : vc)
            {
                if (!v.survivor)
                {
                    continue;
                }
                int kclass = 1;
                auto it = row.v_kclass.find(v.khash);
                if (it != row.v_kclass.end())
                {
                    kclass = it->second == 'a' ? 0 : (it->second == 's' ? 2 : 1);
                }
                tvc.push_back({v.res40, v.clear_count, v.spin, v.lockout,
                    v.khash, kclass});
            }
            for (auto const &q : lp)
            {
                std::uint8_t sem = q.spin == 1 ? 2 : (q.spin == 2 ? 1 : 0);
                int kclass = 3;
                if (q.has_k)
                {
                    auto it = row.l_kclass.find(q.khash);
                    kclass = (it != row.l_kclass.end() && it->second == 's') ? 2
                                                                            : 3;
                }
                tlc.push_back(
                    {q.res40, q.clear_count, sem, q.lockout, q.khash, kclass});
            }
            cls::TransRow trow = cls::classify_trans(tvc, tlc);

            long nL_spawn_sem = -1;
            long delta_spawn = 0;
            if (L.spawn_driven)
            {
                std::set<std::uint64_t> sset;
                for (auto const &p : L.spawn_points)
                {
                    auto key = legacy_cmp::normalize_land_point(in.piece, p.x,
                        p.y, p.r, p.spin_class, p.last_rotate != 0,
                        p.status_bits);
                    if (!key.matched)
                    {
                        continue;
                    }
                    std::uint8_t arr =
                        (in.piece == 'T' && p.last_rotate) ? 1 : 0;
                    sset.insert(khash_of(key.cells_hash, arr));
                }
                nL_spawn_sem = static_cast<long>(sset.size());
                delta_spawn = nV_cands - nL_spawn_sem;
            }
            bool ok_row = row.ok_S && row.ok_N && row.ok_R && row.ok_maps
                && trow.ok_T && trow.ok_c1;
            all_ok = all_ok && ok_row;
            std::println(fu, "{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},0,0,0,{}",
                id, hex16(h40), in.piece, (int)in.source, in.multiplicity,
                row.nV, row.nL_norm, row.nL_sem, row.nRaw, row.nV - row.nL_sem,
                row.a, row.b_counted, row.b_spin_split, row.b_opaque,
                row.b_collapsed, row.c1, row.c2v, row.c2l,
                L.spawn_driven ? 1 : 0, row.dV, row.dL, row.dV_om,
                nL_spawn_sem, delta_spawn, ok_row ? 1 : 0);
            std::println(ft, "{},{},{},{},{},{},{},{},{},{},{},{},{},0,{}", id,
                hex16(h40), in.piece, (int)in.source, trow.nCV, trow.nCL,
                trow.nCV - trow.nCL, trow.a_t, trow.b_t, trow.c1_t_V,
                trow.c1_t_L, trow.dVt, trow.dLt,
                (trow.ok_T && trow.ok_c1) ? 1 : 0);
            for (auto k : row.dV_keys)
            {
                std::string packs;
                for (auto pk : kh_packed[k])
                {
                    packs += std::to_string(pk) + ",";
                }
                std::println(fd, "{}\tU\tdV\t{}\t{}\tcan_spawn={}", id,
                    hex16(k), packs, can_spawn ? 1 : 0);
            }
            for (auto k : row.dL_keys)
            {
                std::println(fd, "{}\tU\tdL\t{}\t{}\tcan_spawn={}", id,
                    hex16(k), "", can_spawn ? 1 : 0);
            }
            for (auto k : row.om_keys)
            {
                std::println(fd, "{}\tU\toutcome_mm\t{}\t{}\t-", id, hex16(k),
                    "");
            }
            ++n_S;
            t_delta += row.nV - row.nL_sem;
            t_a += row.a;
            t_b += row.b_counted;
            t_bspin += row.b_spin_split;
            t_bopaque += row.b_opaque;
            t_bcoll += row.b_collapsed;
            t_c1 += row.c1;
            t_c2v += row.c2v;
            t_c2l += row.c2l;
            t_dV += row.dV;
            t_dL += row.dL;
            t_om += row.dV_om;
            t_nV += row.nV;
            t_nLsem += row.nL_sem;
            t_nLnorm += row.nL_norm;
            t_nRaw += row.nRaw;
            t_dC += trow.nCV - trow.nCL;
            t_nCV += trow.nCV;
            t_nCL += trow.nCL;
            t_at += trow.a_t;
            t_bt += trow.b_t;
            t_c1t += trow.c1_t_V;
            t_dVt += trow.dVt;
            t_dLt += trow.dLt;
            all_ok = all_ok && (trow.c1_t_V == trow.c1_t_L);
            if ((id + 1) % 20000 == 0)
            {
                std::println(stderr, "classify: {}/{} inputs", id + 1,
                    inputs.size());
            }
        }
        std::fclose(fu);
        std::fclose(ft);
        std::fclose(fd);

        bool exact_S = (t_nV - t_nLsem == t_a + t_c2v - t_c2l + t_dV - t_dL);
        bool exact_N = (t_nLnorm - t_nLsem == t_b)
            && (t_b == t_bspin + t_bopaque);
        bool exact_R = (t_nRaw - t_nLnorm == t_bcoll);
        bool exact_T = (t_dC == t_at - t_bt + t_dVt - t_dLt);
        all_ok = all_ok && exact_S && exact_N && exact_R && exact_T;
        long d_total = t_dV + t_dL + t_om + t_dVt + t_dLt;
        {
            FILE *f = std::fopen((prefix + ".class_table.tsv").c_str(), "w");
            if (f != nullptr)
            {
                std::println(f, "leg\tmetric\tvalue");
                auto kv = [&](char const *leg, char const *k, long long v) {
                    std::println(f, "{}\t{}\t{}", leg, k, v);
                };
                kv("uniques", "S_inputs", n_S);
                kv("uniques", "nV", t_nV);
                kv("uniques", "nL_sem", t_nLsem);
                kv("uniques", "nL_norm", t_nLnorm);
                kv("uniques", "nRaw", t_nRaw);
                kv("uniques", "delta", t_delta);
                kv("uniques", "a", t_a);
                kv("uniques", "b_counted", t_b);
                kv("uniques", "b_spin_split", t_bspin);
                kv("uniques", "b_opaque", t_bopaque);
                kv("uniques", "b_collapsed", t_bcoll);
                kv("uniques", "c1", t_c1);
                kv("uniques", "c2v", t_c2v);
                kv("uniques", "c2l", t_c2l);
                kv("uniques", "dV", t_dV);
                kv("uniques", "dL", t_dL);
                kv("uniques", "dV_om", t_om);
                kv("uniques", "exact_S", exact_S ? 1 : 0);
                kv("uniques", "exact_N", exact_N ? 1 : 0);
                kv("uniques", "exact_R", exact_R ? 1 : 0);
                kv("trans", "nCV", t_nCV);
                kv("trans", "nCL", t_nCL);
                kv("trans", "deltaC", t_dC);
                kv("trans", "a_t", t_at);
                kv("trans", "b_t", t_bt);
                kv("trans", "c1_t", t_c1t);
                kv("trans", "dVt", t_dVt);
                kv("trans", "dLt", t_dLt);
                kv("trans", "exact_T", exact_T ? 1 : 0);
                kv("volume", "ood_inputs", n_ood);
                kv("volume", "ood_nV", ood_nV);
                kv("volume", "missing_legacy", n_missing);
                kv("volume", "overflow_inputs", n_overflow);
                kv("volume", "divergent_inputs", n_divergent);
                kv("volume", "distinct_inputs", (long long)inputs.size());
                std::fclose(f);
            }
        }
        std::println("FINAL uniques: S={} delta={} a={} b_counted={} "
                     "(spin {} opaque {}) b_collapsed={} c1={} c2v={} c2l={} "
                     "dV={} dL={} dV_om={} exact_S={} exact_N={} exact_R={}",
            n_S, t_delta, t_a, t_b, t_bspin, t_bopaque, t_bcoll, t_c1, t_c2v,
            t_c2l, t_dV, t_dL, t_om, exact_S ? 1 : 0, exact_N ? 1 : 0,
            exact_R ? 1 : 0);
        std::println("FINAL trans: dC={} a_t={} b_t={} c1_t={} dVt={} dLt={} "
                     "exact_T={}",
            t_dC, t_at, t_bt, t_c1t, t_dVt, t_dLt, exact_T ? 1 : 0);
        std::println("FINAL volume: ood_inputs={} ood_nV={} missing={} "
                     "overflow={} divergent={}",
            n_ood, ood_nV, n_missing, n_overflow, n_divergent);
        cls::Verdict verdict{all_ok, d_total, n_missing, n_overflow};
        int code = cls::exit_code_for(verdict);
        std::println("FINAL exit={}", code);
        return code;
    }

    cls::ValueCand mkV(std::uint64_t kh, bool apply_ok = true,
        std::uint8_t spin = 0, std::uint8_t clear = 0, bool lock = false,
        bool surv = true, std::uint64_t res = 0xa0, std::uint32_t packed = 7,
        std::uint8_t arrival = 0, bool scalar = true, bool replay = true)
    {
        cls::ValueCand v;
        v.khash = kh;
        v.apply_ok = apply_ok;
        v.spin = spin;
        v.clear_count = clear;
        v.lockout = lock;
        v.survivor = surv;
        v.res40 = res;
        v.packed = packed;
        v.arrival = arrival;
        v.scalar_ok = scalar;
        v.replay_ok = replay;
        return v;
    }

    cls::LegacyPoint mkL(bool has_k, std::uint64_t kh, bool matched,
        std::uint64_t channel, std::uint32_t opaque, std::uint8_t clear = 0,
        std::uint8_t spin = 0, bool lock = false, std::uint64_t res = 0xa0)
    {
        cls::LegacyPoint p;
        p.has_k = has_k;
        p.khash = kh;
        p.matched = matched;
        p.channel = channel;
        p.opaque = opaque;
        p.clear_count = clear;
        p.spin = spin;
        p.lockout = lock;
        p.res40 = res;
        return p;
    }

    int run_selftest()
    {
        long checks = 0;
        long failures = 0;
        auto check = [&](bool ok, char const *name) {
            ++checks;
            if (!ok)
            {
                ++failures;
                std::println(stderr, "SELFTEST FAIL: {}", name);
            }
            else
            {
                std::println("SELFTEST pass: {}", name);
            }
        };
        std::uint64_t const kh_I = khash_of(
            cells_hash_of(tetris::Piece::I, tetris::Placement::unchecked(3, 0, 0)),
            0);
        {
            auto row = cls::classify_uniques({mkV(kh_I)},
                {mkL(true, kh_I, true, 0, 11), mkL(true, kh_I, true, 0, 22)},
                true);
            check(row.nRaw == 2 && row.nL_sem == 1 && row.nL_norm == 1
                    && row.b_collapsed == 1 && row.b_counted == 0
                    && row.nV - row.nL_sem == 0 && row.a == 0 && row.dV == 0
                    && row.dL == 0 && row.ok_S && row.ok_N && row.ok_R
                    && row.ok_maps,
                "b_collapsed I-r0/r2 dual landing");
        }
        {
            std::uint64_t const K = 0x1001;
            auto row = cls::classify_uniques({mkV(K)},
                {mkL(true, K, true, 2, 31), mkL(true, K, true, 5, 32)}, true);
            check(row.nL_sem == 1 && row.nL_norm == 2 && row.b_counted == 1
                    && row.b_spin_split == 1 && row.b_opaque == 0
                    && row.b_collapsed == 0 && row.ok_S && row.ok_N && row.ok_R,
                "b_counted spin-split pair");
        }
        {
            auto o1 = legacy_cmp::normalize_land_point('O', 3, 1, 1, 0, false,
                0xbeefu);
            check(o1.matched, "O-r1 normalizes matched (not opaque)");
            std::uint64_t const K = 0x1002;
            auto row = cls::classify_uniques({mkV(K)},
                {mkL(true, K, true, 0, 41), mkL(false, 0, false, 0, 0x9abc)},
                true);
            check(row.nL_norm == 2 && row.nL_sem == 1 && row.b_counted == 1
                    && row.b_opaque == 1 && row.b_spin_split == 0
                    && row.ok_N,
                "b_counted opaque point");
        }
        {
            std::uint64_t const K = 0x1003;
            auto row = cls::classify_uniques({mkV(K, true, 0, 0, false)},
                {mkL(true, K, true, 0, 51, 0, 0, true)}, true);
            check(row.c1 == 1 && row.dV_om == 0 && row.nV - row.nL_sem == 0
                    && row.ok_S,
                "c1 anchor-vs-mino disagreement");
        }
        {
            auto row = cls::classify_uniques({},
                {mkL(true, 0x2001, true, 0, 61), mkL(true, 0x2002, true, 0, 62)},
                false);
            check(row.c2l == 2 && row.dL == 0 && row.nV - row.nL_sem == -2
                    && row.ok_S,
                "c2 blocked-spawn legacy side");
            auto row2 =
                cls::classify_uniques({mkV(0x2003)}, {}, false);
            check(row2.c2v == 1 && row2.dV == 0
                    && row2.nV - row2.nL_sem == 1 && row2.ok_S,
                "c2 blocked-spawn value side");
        }
        {
            std::array<std::uint16_t, 48> rows{};
            check(fmt::rows40_47_empty(rows), "c3 empty rows in-domain");
            rows[45] = 1;
            check(!fmt::rows40_47_empty(rows), "c3 high row out-of-domain");
            rows[45] = 0;
            std::uint64_t h1 = fmt::hash_rows40(rows);
            std::uint64_t h2 = fmt::hash_rows40(rows);
            rows[3] = 7;
            check(h1 == h2 && fmt::hash_rows40(rows) != h1,
                "c3 board-hash40 stable and sensitive");
        }
        {
            auto row = cls::classify_uniques(
                {mkV(0x3001, true, 0, 0, false, true, 0xa0, 9, 0, true, false)},
                {}, true);
            check(row.dV == 1 && row.ok_S, "dV replay-failure counted");
            check(cls::exit_code_for({true, 1, 0, 0}) == 1,
                "dV replay-failure exits nonzero");
        }
        {
            std::uint64_t const K = 0x3002;
            auto row = cls::classify_uniques({mkV(K, true, 0, 1)},
                {mkL(true, K, true, 0, 71, 2)}, true);
            check(row.dV_om == 1, "dV clear-mismatch counted");
            check(cls::exit_code_for({true, 1, 0, 0}) == 1,
                "dV clear-mismatch exits nonzero");
        }
        {
            std::uint64_t const K1 = 0x4001;
            std::uint64_t const K2 = 0x4002;
            std::uint64_t const K3 = 0x4003;
            std::uint64_t const K4 = 0x4004;
            auto row = cls::classify_uniques(
                {mkV(K1), mkV(K2), mkV(K3),
                    mkV(K4, true, 0, 0, false, true, 0xd0, 13)},
                {mkL(true, K3, true, 0, 81), mkL(true, K3, true, 0, 82),
                    mkL(true, K4, true, 0, 83, 0, 0, true),
                    mkL(true, K4, true, 4, 84, 0, 0, true)},
                true);
            check(row.nV == 4 && row.nL_sem == 2 && row.nRaw == 4
                    && row.nL_norm == 3 && row.a == 2 && row.b_counted == 1
                    && row.b_spin_split == 1 && row.b_collapsed == 1
                    && row.c1 == 1 && row.dV == 0 && row.dL == 0
                    && row.dV_om == 0 && row.ok_S && row.ok_N && row.ok_R
                    && row.ok_maps,
                "mixed exact-accounting fixture");
        }
        {
            cls::TransChild A{1, 0, 0, false, 0x5001, 0};
            cls::TransChild B{2, 0, 0, false, 0x5002, 0};
            cls::TransChild C{3, 0, 0, false, 0x5003, 2};
            cls::TransChild D{4, 0, 0, false, 0x5004, 2};
            cls::TransChild Df{4, 0, 0, true, 0x5004, 2};
            auto trow = cls::classify_trans({A, B, C, D}, {A, B, B, C, Df});
            check(trow.nCV == 4 && trow.nCL == 5 && trow.a_t == 0
                    && trow.b_t == 1 && trow.c1_t_V == 1 && trow.c1_t_L == 1
                    && trow.dVt == 0 && trow.dLt == 0 && trow.ok_T
                    && trow.ok_c1,
                "mixed trans fixture");
            cls::TransChild X{9, 0, 0, false, 0x6001, 1};
            cls::TransChild Y{8, 0, 0, false, 0x6002, 3};
            auto trow2 = cls::classify_trans({X}, {Y});
            check(trow2.dVt == 1 && trow2.dLt == 1 && trow2.ok_T
                    && cls::exit_code_for({true, 2, 0, 0}) == 1,
                "trans defects exit nonzero");
        }
        check(cls::exit_code_for({true, 0, 0, 0}) == 0, "clean verdict exits 0");
        check(cls::exit_code_for({false, 0, 0, 0}) == 1,
            "failed accounting exits nonzero");
        check(cls::exit_code_for({true, 0, 1, 0}) == 1,
            "missing legacy exits nonzero");
        check(cls::exit_code_for({true, 0, 0, 2}) == 1,
            "overflow exits nonzero");
        std::println("SELFTEST {} checks, {} failures", checks, failures);
        return failures == 0 ? 0 : 1;
    }
} // namespace

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        std::println(stderr,
            "usage: candidate_partition (record|drive|classify|selftest) [options]");
        return 1;
    }
    std::string const mode = argv[1];
    Args const args = parse_args(argc, argv);
    if (mode == "record")
    {
        return run_record(args);
    }
    if (mode == "drive")
    {
        return run_drive(args);
    }
    if (mode == "classify")
    {
        return run_classify(args);
    }
    if (mode == "selftest")
    {
        return run_selftest();
    }
    return fail("unknown mode (record|drive|classify|selftest)");
}
