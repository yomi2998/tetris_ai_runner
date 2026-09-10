#include "legacy_host_diag_common.h"

#include <algorithm>
#include <chrono>
#include <memory>
#include <optional>
#include <print>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

namespace
{
    using namespace legacy_diag;

    struct ArmOptions
    {
        std::string mode = "replay";
        std::string arm = "L1";
        std::string inputs_path;
        std::string param_path;
        std::size_t iters = 1000;
        std::size_t moves = 200;
        std::size_t warmup_moves = 20;
        std::uint32_t seed = 1;
        std::size_t maxdepth = 6;
        std::size_t max_inputs = 0;
    };

    ArmOptions parse_args(int argc, char **argv)
    {
        ArmOptions opt;
        for (int i = 1; i < argc; ++i)
        {
            std::string a = argv[i];
            auto next = [&](char const *name) -> std::string {
                if (i + 1 >= argc)
                {
                    std::println(stderr, "missing value for {}", name);
                    std::exit(2);
                }
                return argv[++i];
            };
            if (a == "--mode") opt.mode = next("mode");
            else if (a == "--arm") opt.arm = next("arm");
            else if (a == "--inputs") opt.inputs_path = next("inputs");
            else if (a == "--param-file") opt.param_path = next("param");
            else if (a == "--iters") opt.iters = std::strtoull(next("iters").c_str(), nullptr, 10);
            else if (a == "--moves") opt.moves = std::strtoull(next("moves").c_str(), nullptr, 10);
            else if (a == "--warmup-moves") opt.warmup_moves = std::strtoull(next("warmup").c_str(), nullptr, 10);
            else if (a == "--seed") opt.seed = std::strtoul(next("seed").c_str(), nullptr, 10);
            else if (a == "--maxdepth") opt.maxdepth = std::strtoull(next("depth").c_str(), nullptr, 10);
            else if (a == "--max-inputs") opt.max_inputs = std::strtoull(next("max").c_str(), nullptr, 10);
            else
            {
                std::println(stderr, "unknown option {}", a);
                std::exit(2);
            }
        }
        return opt;
    }

    bool read_param_file(std::string const &path, ai_zzz::TOJ::Param &param)
    {
        double values[29];
        FILE *file = std::fopen(path.c_str(), "rb");
        if (file == nullptr)
        {
            return false;
        }
        std::size_t const count = std::fread(values, sizeof(double), 29, file);
        std::fclose(file);
        if (count != 29)
        {
            return false;
        }
        param = {values[0], values[1], values[2], values[3], values[4], values[5],
            values[6], values[7], values[8], values[9], values[10], values[11],
            values[12], values[13], values[14], values[15], values[16], values[17],
            values[18], values[19], values[20], values[21], values[22], values[23],
            values[24], values[25], values[26], values[27], values[28]};
        return true;
    }

    int const combo_table[] = {0, 0, 0, 1, 1, 2, 2, 3, 3, 4};

    struct DiagSearch : search_tspin::Search
    {
        static inline std::string mode = "L0";
        static inline legacy_diag::Counts *counts = nullptr;
        static inline AdapterSearch adapter;
        static inline std::vector<value_Candidate> last_candidates;
        static inline int init_calls = 0;

        void init(m_tetris::TetrisContext const *context, Config const *config)
        {
            ++init_calls;
            search_tspin::Search::init(context, config);
            adapter.init(context, config);
        }

        static void configure(std::string const &arm, legacy_diag::Counts *c)
        {
            mode = arm;
            counts = c;
            adapter.counts = c;
        }

        std::vector<TetrisNodeWithTSpinType> const *search(
            m_tetris::TetrisMap const &map, m_tetris::TetrisNode const *node, std::size_t depth)
        {
            if (mode == "L0")
            {
                auto const *results = search_tspin::Search::search(map, node, depth);
                if (counts != nullptr)
                {
                    ++counts->searches;
                    counts->candidates += results != nullptr ? results->size() : 0u;
                }
                return results;
            }
            auto const *result = adapter.search(map, node, depth);
            last_candidates = adapter.last_candidates;
            return result;
        }

        std::vector<char> make_path(m_tetris::TetrisNode const *node,
            TetrisNodeWithTSpinType const &land, m_tetris::TetrisMap const &map)
        {
            if (mode == "L0")
            {
                return search_tspin::Search::make_path(node, land, map);
            }
            return adapter.make_path(node, land, map);
        }
    };

    using DiagEngine = m_tetris::TetrisEngine<rule_toj::TetrisRule, ai_zzz::TOJ, DiagSearch>;

    std::uint64_t steady_stamp()
    {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch())
                .count());
    }

    std::uint64_t digest_candidates(std::vector<value_Candidate> const &candidates)
    {
        std::uint64_t h = 1469598103934665603ull;
        for (auto const &c : candidates)
        {
            h = fnv_mix(h, digest_one_candidate(c));
        }
        return h;
    }
}

int main(int argc, char **argv)
{
    ArmOptions opt = parse_args(argc, argv);
    bool const arm_is_legacy = opt.arm == "L0" || opt.arm == "L1" || opt.arm == "L2";
    if (!arm_is_legacy && opt.arm != "V")
    {
        std::println(stderr, "unknown arm {}", opt.arm);
        return 2;
    }
    if (opt.mode != "replay" && opt.mode != "selfplay")
    {
        std::println(stderr, "unknown mode {}", opt.mode);
        return 2;
    }
    if (opt.mode == "replay" && opt.inputs_path.empty())
    {
        std::println(stderr, "--inputs required in replay mode");
        return 2;
    }

    legacy_host_diag::audit_enabled() = arm_is_legacy;
    legacy_host_diag::exact_only() = opt.arm == "L2";
    auto &audit = legacy_host_diag::EvalAudit<ai_zzz::TOJ::Result>::instance();
    audit.reset();

    DiagEngine legacy_engine;
    legacy_diag::Counts counts{};
    if (arm_is_legacy)
    {
        if (!legacy_engine.prepare(10, 40))
        {
            std::println(stderr, "legacy prepare failed");
            return 2;
        }
        legacy_engine.memory_limit(256ull << 20);
        legacy_engine.search_config()->allow_rotate_move = false;
        legacy_engine.search_config()->allow_180 = true;
        legacy_engine.search_config()->allow_d = true;
        legacy_engine.search_config()->is_20g = false;
        legacy_engine.search_config()->last_rotate = false;
        legacy_engine.ai_config()->table = combo_table;
        legacy_engine.ai_config()->table_max = 10;
        if (!opt.param_path.empty()
            && !read_param_file(opt.param_path, legacy_engine.ai_config()->param))
        {
            std::println(stderr, "failed to read parameter file");
            return 2;
        }
        DiagSearch::configure(opt.arm, &counts);
    }
    else
    {
        DiagSearch::configure("L0", nullptr);
    }

    if (opt.mode == "selfplay")
    {
        if (!arm_is_legacy)
        {
            std::println(stderr, "selfplay mode supports legacy arms only in this driver");
            return 2;
        }
        m_tetris::TetrisMap map(10, 40);
        std::mt19937 rng(opt.seed);
        std::vector<char> next;
        char hold = ' ';
        int combo = 0;
        int b2b = 0;
        std::uint64_t run_ns = 0;
        std::uint64_t apply_ns = 0;
        std::uint64_t path_ns = 0;
        std::uint64_t total_dead = 0;
        std::uint64_t total_path_ok = 0;
        std::uint64_t total_searches = 0;
        std::uint64_t total_candidates = 0;
        std::uint64_t total_path_fail = 0;
        std::uint64_t total_path_relabels = 0;
        std::uint64_t cand_digest = 1469598103934665603ull;
        std::uint64_t done = 0;
        std::uint64_t const total_moves = opt.warmup_moves + opt.moves;
        std::uint64_t t_all0 = steady_stamp();
        while (done < total_moves)
        {
            if (!next.empty())
            {
                next.erase(next.begin());
            }
            while (next.size() <= opt.maxdepth)
            {
                for (std::size_t i = 0; i < legacy_engine.context()->type_max(); ++i)
                {
                    next.push_back(legacy_engine.context()->convert(i));
                }
                std::shuffle(next.end() - static_cast<std::ptrdiff_t>(legacy_engine.context()->type_max()),
                    next.end(), rng);
            }
            legacy_engine.ai_config()->safe = legacy_engine.ai()->get_safe(map, next.front());
            legacy_engine.status()->death = 0;
            legacy_engine.status()->combo = combo;
            legacy_engine.status()->under_attack = 0;
            legacy_engine.status()->map_rise = 0;
            legacy_engine.status()->b2b = b2b ? 1 : 0;
            legacy_engine.status()->acc_value = 0;
            legacy_engine.status()->like = 0;
            legacy_engine.status()->value = 0;
            ai_zzz::TOJ::Status::init_t_value(map, legacy_engine.status()->t2_value,
                legacy_engine.status()->t3_value);
            char current = next.front();
            audit.reset();
            counts = legacy_diag::Counts{};
            m_tetris::TetrisNode const *gen = legacy_engine.context()->generate(current);
            std::uint64_t t0 = steady_stamp();
            auto result = legacy_engine.run_hold(map, gen, hold, true, next.data() + 1,
                opt.maxdepth, m_tetris::SearchBudget::by_iterations(opt.iters));
            std::uint64_t t1 = steady_stamp();
            run_ns += t1 - t0;
            bool dead = result.target == nullptr || result.target->row >= 20;
            if (dead)
            {
                ++total_dead;
                map = m_tetris::TetrisMap(10, 40);
                hold = ' ';
                combo = 0;
                b2b = 0;
            }
            else
            {
                if (result.change_hold)
                {
                    if (hold == ' ')
                    {
                        next.erase(next.begin());
                    }
                    hold = current;
                }
                std::uint64_t t2 = steady_stamp();
                result.target->attach(legacy_engine.context().get(), map);
                std::uint64_t t3 = steady_stamp();
                apply_ns += t3 - t2;
                auto path = legacy_engine.make_path(gen, result.target, map);
                std::uint64_t t4 = steady_stamp();
                path_ns += t4 - t3;
                if (path.empty())
                {
                    ++counts.path_failures;
                }
                else
                {
                    ++total_path_ok;
                }
                std::uint64_t h = 1469598103934665603ull;
                for (auto const &c : DiagSearch::last_candidates)
                {
                    h = fnv_mix(h, digest_one_candidate(c));
                }
                cand_digest = fnv_mix(cand_digest, h);
            }
            total_searches += counts.searches;
            total_candidates += counts.candidates;
            total_path_fail += counts.path_failures;
            total_path_relabels += counts.path_relabels;
            ++done;
        }
        std::uint64_t t_all1 = steady_stamp();
        std::uint64_t hwm = 0;
        std::uint64_t rss = 0;
        read_vmhwm_vmrss(hwm, rss);
        std::println(stdout,
            "DIAG_V1 mode=selfplay arm={} total_s={:.6f} setup_ms=0 run_ms={:.3f} apply_ms={:.3f} "
            "path_ms={:.3f} inputs={} ood=0 skipped=0 dead={} path_ok={} path_fail={} "
            "total_searches={} total_candidates={} total_path_relabels={} "
            "searches=0 candidates=0 "
            "audit_requests={} audit_legacy_hits={} audit_collisions={} audit_exact_hits={} "
            "audit_fresh={} audit_evictions={} audit_reserved={} "
            "vmhwm={} vmrss={} cand_digest={:016x} sel_digest=0",
            opt.arm, static_cast<double>(t_all1 - t_all0) / 1e9,
            static_cast<double>(run_ns) / 1e6, static_cast<double>(apply_ns) / 1e6,
            static_cast<double>(path_ns) / 1e6, done, total_dead, total_path_ok,
            total_path_fail, total_searches, total_candidates, total_path_relabels,
            audit.requests, audit.legacy_hits, audit.collisions, audit.exact_hits,
            audit.fresh_evals, audit.evictions, audit.reserved_bytes(), hwm, rss,
            cand_digest);
        return 0;
    }

    ReplayStream stream(opt.inputs_path);
    if (!stream.ok())
    {
        std::println(stderr, "cannot open inputs");
        return 2;
    }
    std::uint32_t word_count = 0;
    std::uint64_t n_inputs = 0;
    if (!stream.header(word_count, n_inputs))
    {
        std::println(stderr, "bad inputs header");
        return 2;
    }

    std::unique_ptr<tetris_engine::Engine> value_holder;
    if (opt.arm == "V")
    {
        tetris_engine::EngineConfig config;
        config.telemetry_enabled = true;
        config.timers_enabled = false;
        value_holder = std::make_unique<tetris_engine::Engine>();
        if (!value_holder->init(config))
        {
            std::println(stderr, "value engine init failed");
            return 2;
        }
    }

    toj_policy::Config policy_config{};
    toj_policy::Policy seed_policy;
    seed_policy.init(&policy_config);

    std::uint64_t inputs = 0;
    std::uint64_t ood = 0;
    std::uint64_t skipped = 0;
    std::uint64_t dead = 0;
    std::uint64_t path_ok = 0;
    std::uint64_t path_fail_total = 0;
    std::uint64_t total_searches = 0;
    std::uint64_t total_candidates = 0;
    std::uint64_t total_path_relabels = 0;
    std::uint64_t setup_ns = 0;
    std::uint64_t run_ns = 0;
    std::uint64_t apply_ns = 0;
    std::uint64_t path_ns = 0;
    std::uint64_t cand_digest = 1469598103934665603ull;
    std::uint64_t sel_digest = 1469598103934665603ull;
    std::uint64_t value_evals = 0;
    std::uint64_t value_materialized = 0;
    std::uint64_t value_selected = 0;
    std::uint64_t not_in_graph_inputs = 0;

    ReplayInput in;
    std::vector<partition_fmt::ValueCandidate> recorded;
    bool first_parent = true;
    while (stream.next(in, recorded))
    {
        if (opt.max_inputs != 0 && inputs >= opt.max_inputs)
        {
            break;
        }
        ++inputs;
        auto rows = rows_from_words(in.words);
        if (!map_rows_fit(rows))
        {
            ++ood;
            continue;
        }
        auto piece_opt = tetris::try_from_char(in.piece);
        if (!piece_opt.has_value())
        {
            ++skipped;
            continue;
        }
        value_Piece piece = *piece_opt;
        audit.reset();
        counts = legacy_diag::Counts{};

        std::uint64_t t_setup0 = steady_stamp();
        m_tetris::TetrisMap map(10, 40);
        for (int y = 0; y < 40; ++y)
        {
            map.row[static_cast<std::size_t>(y)] = rows[static_cast<std::size_t>(y)];
        }
        rebuild_metadata(map);
        value_Board board = board_from_words(in.words);
        toj_policy::State root_state;
        policy_config.safe = seed_policy.safe_margin(board, piece);
        toj_policy::Evaluation seed_eval = seed_policy.evaluate(board);
        root_state.t2_value = seed_eval.t2_value;
        root_state.t3_value = seed_eval.t3_value;
        std::uint64_t t_setup1 = steady_stamp();
        setup_ns += t_setup1 - t_setup0;

        std::uint64_t t_run0 = steady_stamp();
        if (arm_is_legacy)
        {
            if (!first_parent)
            {
                legacy_engine.update();
            }
            auto spawn_status = tetris::toj::ExternalPoseTransform::to_legacy(piece,
                value_Placement::unchecked(4, 20, 0));
            m_tetris::TetrisNode const *gen = spawn_status.has_value()
                ? legacy_engine.context()->get(m_tetris::TetrisBlockStatus{in.piece,
                      static_cast<std::int8_t>((*spawn_status)[0]),
                      static_cast<std::int8_t>((*spawn_status)[1]),
                      static_cast<std::uint8_t>((*spawn_status)[2])})
                : nullptr;
            if (gen == nullptr)
            {
                gen = legacy_engine.context()->generate(in.piece);
                ++not_in_graph_inputs;
            }
            std::vector<char> next(opt.maxdepth + 1, in.piece);
            auto result = legacy_engine.run_hold(map, gen, ' ', true, next.data(),
                next.size(), m_tetris::SearchBudget::by_iterations(opt.iters));
            std::uint64_t t_run1 = steady_stamp();
            run_ns += t_run1 - t_run0;
            if (result.target != nullptr && result.target->row < 20)
            {
                std::uint64_t t_apply0 = steady_stamp();
                result.target->attach(legacy_engine.context().get(), map);
                std::uint64_t t_apply1 = steady_stamp();
                apply_ns += t_apply1 - t_apply0;
                std::uint64_t t_path0 = steady_stamp();
                auto path = legacy_engine.make_path(gen, result.target, map);
                std::uint64_t t_path1 = steady_stamp();
                path_ns += t_path1 - t_path0;
                if (path.empty())
                {
                    ++counts.path_failures;
                }
                else
                {
                    ++path_ok;
                }
                sel_digest = fnv_mix(sel_digest,
                    static_cast<std::uint64_t>(static_cast<std::uint8_t>(result.target.node->status.x)));
                sel_digest = fnv_mix(sel_digest,
                    static_cast<std::uint64_t>(static_cast<std::uint8_t>(result.target.node->status.y)));
                sel_digest = fnv_mix(sel_digest, result.target.node->status.r);
            }
            else
            {
                ++dead;
            }
            cand_digest = fnv_mix(cand_digest, digest_candidates(DiagSearch::last_candidates));
            total_searches += counts.searches;
            total_candidates += counts.candidates;
            total_path_relabels += counts.path_relabels;
            first_parent = false;
        }
        else
        {
            tetris_engine::Queue queue;
            for (std::size_t i = 0; i <= opt.maxdepth; ++i)
            {
                queue.pieces.push_back(piece);
                queue.boundary.push_back(false);
            }
            tetris_engine::HoldState hold;
            tetris_engine::NodeId root = value_holder->set_root(board, root_state,
                std::move(queue), hold);
            if (root == tetris_engine::no_node)
            {
                ++skipped;
                continue;
            }
            auto children = value_holder->expand(root);
            std::uint64_t child_digest = 1469598103934665603ull;
            for (auto const &child : children)
            {
                child_digest = fnv_mix(child_digest, digest_one_candidate(child.candidate));
            }
            cand_digest = fnv_mix(cand_digest, child_digest);
            value_holder->run(tetris_engine::SearchBudget::by_iterations(opt.iters));
            auto selection = value_holder->select_best();
            std::uint64_t t_run1 = steady_stamp();
            run_ns += t_run1 - t_run0;
            if (selection.has_value())
            {
                auto const *node = value_holder->node(selection->root_child);
                if (node != nullptr)
                {
                    ++value_selected;
                    sel_digest = fnv_mix(sel_digest, digest_one_candidate(node->incoming));
                }
            }
            auto stats = value_holder->search_stats();
            value_evals += stats.eval_computed;
            value_materialized += stats.materialized_nodes;
        }
    }

    std::uint64_t hwm = 0;
    std::uint64_t rss = 0;
    read_vmhwm_vmrss(hwm, rss);
    std::println(stdout,
        "DIAG_V1 mode=replay arm={} total_inputs={} inputs={} ood={} skipped={} dead={} "
        "setup_ms={:.3f} run_ms={:.3f} apply_ms={:.3f} path_ms={:.3f} "
        "total_searches={} total_candidates={} total_path_relabels={} "
        "path_ok={} path_fail={} not_in_graph_inputs={} "
        "value_evals={} value_materialized={} value_selected={} "
        "audit_requests={} audit_legacy_hits={} audit_collisions={} audit_exact_hits={} "
        "audit_fresh={} audit_evictions={} audit_reserved={} "
        "vmhwm={} vmrss={} cand_digest={:016x} sel_digest={:016x}",
        opt.arm, n_inputs, inputs, ood, skipped, dead,
        static_cast<double>(setup_ns) / 1e6, static_cast<double>(run_ns) / 1e6,
        static_cast<double>(apply_ns) / 1e6, static_cast<double>(path_ns) / 1e6,
        total_searches, total_candidates, total_path_relabels,
        path_ok, path_fail_total, not_in_graph_inputs,
        value_evals, value_materialized, value_selected,
        audit.requests, audit.legacy_hits, audit.collisions, audit.exact_hits,
        audit.fresh_evals, audit.evictions, audit.reserved_bytes(), hwm, rss,
        cand_digest, sel_digest);
    return 0;
}
