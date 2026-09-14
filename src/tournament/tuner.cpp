#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <print>
#include <random>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "ai_zzz.h"
#include "param.h"
#include "tuner_match.h"
#include "tournament/bracket.h"
#include "tournament/bytes.h"
#include "tournament/checkpoint.h"
#include "tournament/cmaes.h"
#include "tournament/ordinal.h"
#include "tournament/promotion.h"
#include "tournament/rating.h"
#include "tournament/runner.h"
#include "tournament/scheduler.h"
#include "tuning/domain.h"
#include "tuning/engine_match.h"
#include "tuning/match.h"
#include "tuning/toj_adapter.h"

namespace tournament_tuner
{
    using TojBackend = tuning::EngineMatchBackend<tuning_toj::TojAdapter>;
    using Runner = tournament_runner::TournamentRunner<TojBackend>;

    struct TunerConfig
    {
        int generations = 10;
        std::size_t iters_per_move = 50;
        std::uint64_t root_seed = 555;
        int threads = 0;
        int max_rounds = 3600;
        int pairs = 32;
        double threshold = 0.5;
        std::string data_file = "tournament_data.bin";
        std::string incumbent_file = "tournament_incumbent.bin";
        std::string current_file = "tournament_current.bin";
    };

    int workers_for(int threads_arg)
    {
        if (threads_arg > 0)
        {
            return threads_arg;
        }
        unsigned hw = std::thread::hardware_concurrency();
        if (hw > 1)
        {
            return static_cast<int>(hw - 1);
        }
        return 1;
    }

    int roster_size_for(int workers)
    {
        return std::max(2, workers * 2);
    }

    int lambda_for(int roster)
    {
        return std::max(2, roster - 1);
    }

    std::uint64_t cma_seed_for(std::uint64_t root)
    {
        std::uint64_t mixed = tuning::mix64(root ^ 0xC0A5EED123456789ULL);
        return (mixed % 2147483647ULL) + 1ULL;
    }

    std::uint64_t generation_seed_for(std::uint64_t root, std::uint64_t generation)
    {
        return tuning::mix64(root ^ tuning::mix64(generation + 0x9E3779B97F4A7C15ULL));
    }

    std::uint64_t promotion_seed_for(std::uint64_t root, std::uint64_t generation)
    {
        std::uint64_t seed = tuning::mix64(root ^ 0xB207071C651E98DFULL ^ tuning::mix64(generation + 1));
        return seed == 0 ? 1 : seed;
    }

    bool read_theta_bin(std::string const &path, std::vector<double> &out)
    {
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        if (!input.good())
        {
            return false;
        }
        std::streamoff size = input.tellg();
        std::size_t want = tuning_toj::TojAdapter::param_count() * sizeof(double);
        if (size != static_cast<std::streamoff>(want))
        {
            return false;
        }
        input.seekg(0, std::ios::beg);
        out.resize(tuning_toj::TojAdapter::param_count());
        input.read(reinterpret_cast<char *>(out.data()), static_cast<std::streamsize>(want));
        if (!input.good())
        {
            out.clear();
            return false;
        }
        for (double value : out)
        {
            if (!std::isfinite(value))
            {
                out.clear();
                return false;
            }
        }
        return true;
    }

    bool write_theta_bin(std::string const &path, std::vector<double> const &theta)
    {
        return durable::write_doubles(path, theta.data(), theta.size());
    }

    std::vector<double> default_theta_vec()
    {
        std::vector<double> theta(tuning_toj::TojAdapter::param_count());
        for (std::size_t i = 0; i < theta.size(); ++i)
        {
            theta[i] = ai_zzz::TOJ::kProductionDefaultTheta[i];
        }
        return theta;
    }

    std::vector<double> scales_vec()
    {
        auto schema = tuning_toj::TojAdapter::schema();
        return std::vector<double>(schema.scales.begin(), schema.scales.end());
    }

    tournament_checkpoint::Identity make_identity()
    {
        tournament_checkpoint::Identity identity;
        identity.schema_version = 1;
        identity.adapter_id = std::string(tuning_toj::TojAdapter::kAdapterId);
        identity.schema_hash = tuning_toj::TojAdapter::schema_hash();
        return identity;
    }

    nlohmann::json theta_to_json(std::vector<double> const &theta)
    {
        nlohmann::json array = nlohmann::json::array();
        for (double value : theta)
        {
            array.push_back(value);
        }
        return array;
    }

    bool json_to_theta(nlohmann::json const &json, std::vector<double> &theta)
    {
        if (!json.is_array())
        {
            return false;
        }
        if (json.size() != tuning_toj::TojAdapter::param_count())
        {
            return false;
        }
        theta.resize(json.size());
        for (std::size_t i = 0; i < json.size(); ++i)
        {
            if (!json[i].is_number())
            {
                return false;
            }
            double value = json[i].get<double>();
            if (!std::isfinite(value))
            {
                return false;
            }
            theta[i] = value;
        }
        return true;
    }

    void print_usage(char const *program)
    {
        std::println("Usage:");
        std::println("  {} [generations] [iters_per_move] [seed] [threads] [max_rounds] [pairs] [data_file]", program);
        std::println("  {} selfcheck", program);
        std::println("  {} smoke", program);
        std::println("  {} help", program);
        std::println("");
        std::println("Defaults: generations 10, iters_per_move 50, seed 555, threads 0 (auto), max_rounds 3600, pairs 32, data tournament_data.bin");
        std::println("  {} view [seed] [iters] [max_rounds]", program);
        std::println("View defaults: seed 0 (time-based, printed for replay), iters 50, max_rounds 3600");
        std::println("Threading: workers = threads if given else hardware_threads - 1, roster = 2 * workers, lambda = roster - 1");
        std::println("Search: iteration budgets only, no time budgets");
        std::println("Checkpoint: tournament_data.bin with .bak fallback, resume by generation");
        std::println("Outputs: tournament_incumbent.bin anchor, tournament_current.bin latest champion");
        std::println("Existing tuner files are not touched");
    }

    int run_selfcheck()
    {
        int failures = 0;
        auto check = [&](bool ok, std::string const &name)
        {
            std::println("{}: {}", ok ? "PASS" : "FAIL", name);
            if (!ok)
            {
                ++failures;
            }
        };
        check(workers_for(0) >= 1, "workers_for auto is at least one");
        check(workers_for(4) == 4, "workers_for respects explicit threads");
        check(roster_size_for(15) == 30, "roster is twice workers");
        check(lambda_for(30) == 29, "lambda is roster minus one");
        check(lambda_for(2) == 2, "lambda clamps to at least two");
        check(cma_seed_for(555) >= 1 && cma_seed_for(555) <= 2147483647ULL, "cma seed is in library range");
        check(generation_seed_for(555, 0) != generation_seed_for(555, 1), "generation seeds differ");
        check(promotion_seed_for(555, 0) != 0, "promotion seed is nonzero");
        std::vector<std::uint8_t> raw = {0, 1, 15, 16, 127, 128, 254, 255};
        std::string encoded = tournament_bytes::encode_hex(raw);
        auto decoded = tournament_bytes::decode_hex(encoded);
        check(decoded.has_value() && *decoded == raw, "hex round trip");
        check(!tournament_bytes::decode_hex("0").has_value(), "odd hex rejected");
        tournament_rating::FitResult failed;
        failed.ok = false;
        check(!tournament_ordinal::build(failed, {10, 20}).ok, "ordinal rejects failed rating");
        std::vector<tournament_rating::GameRecord> records;
        std::uint64_t block = 1;
        auto add_games = [&](std::uint64_t a, std::uint64_t b, int a_wins, int b_wins)
        {
            for (int i = 0; i < a_wins; ++i)
            {
                records.push_back({a, b, 1.0, block++});
            }
            for (int i = 0; i < b_wins; ++i)
            {
                records.push_back({a, b, 0.0, block++});
            }
        };
        add_games(11, 22, 20, 5);
        add_games(11, 33, 20, 5);
        add_games(22, 33, 15, 10);
        auto fit = tournament_rating::fit(records);
        check(fit.ok, "rating fits synthetic round robin");
        if (fit.ok)
        {
            check(fit.ratings.size() == 3, "rating covers all candidates");
            auto ordinal = tournament_ordinal::build(fit, {11, 22, 33});
            check(ordinal.ok, "ordinal builds for optimizer samples");
            if (ordinal.ok)
            {
                bool distinct = ordinal.fitness[0] != ordinal.fitness[1]
                    && ordinal.fitness[0] != ordinal.fitness[2]
                    && ordinal.fitness[1] != ordinal.fitness[2];
                check(distinct, "ordinal fitness is distinct");
            }
        }
        tournament_cmaes::Configuration cma_config;
        cma_config.dimension = 2;
        cma_config.mean = {0.0, 0.0};
        cma_config.coordinate_scales = {1.0, 1.0};
        cma_config.lambda = 4;
        cma_config.seed = 42;
        try
        {
            tournament_cmaes::Optimizer optimizer(cma_config);
            auto const &samples = optimizer.ask();
            check(samples.size() == 8, "cma ask returns lambda by dimension");
            optimizer.tell({0.0, 1.0, 2.0, 3.0});
            auto blob = optimizer.save_state();
            check(!blob.empty(), "cma save produces a blob");
            tournament_cmaes::Optimizer resumed(cma_config);
            resumed.load_state(blob);
            check(resumed.generation() == optimizer.generation(), "cma resume preserves generation");
        }
        catch (std::exception const &error)
        {
            check(false, std::string("cma ask tell save load: ") + error.what());
        }
        std::filesystem::path temp = std::filesystem::temp_directory_path() / "tournament_tuner_selfcheck.bin";
        std::string temp_path = temp.string();
        std::filesystem::remove(temp);
        std::filesystem::remove(temp.string() + ".bak");
        tournament_checkpoint::Envelope envelope;
        envelope.identity = make_identity();
        envelope.generation = 7;
        envelope.root_seed = 555;
        envelope.payload = {{"marker", 123}};
        std::string save_error;
        check(tournament_checkpoint::save(temp_path, envelope, save_error), "checkpoint save succeeds");
        auto loaded = tournament_checkpoint::load_with_backup(temp_path, make_identity());
        check(loaded.status == tournament_checkpoint::LoadStatus::Ok, "checkpoint reloads");
        check(loaded.envelope.generation == 7, "checkpoint generation survives");
        std::filesystem::remove(temp);
        std::filesystem::remove(temp.string() + ".bak");
        if (failures == 0)
        {
            std::println("ALL TOURNAMENT TUNER SELFCHECKS PASSED");
            return 0;
        }
        std::println("{} TOURNAMENT TUNER SELFCHECK(S) FAILED", failures);
        return 1;
    }

    int run_view(std::uint64_t seed, std::size_t iters, int max_rounds)
    {
        if (seed == 0)
        {
            seed = static_cast<std::uint64_t>(std::time(nullptr));
        }
        if (iters == 0 || max_rounds <= 0)
        {
            std::println(stderr, "view needs iters > 0 and max_rounds > 0");
            return 1;
        }
        std::size_t const dimension = tuning_toj::TojAdapter::param_count();
        std::vector<double> scales = scales_vec();
        std::vector<double> base = default_theta_vec();
        std::mt19937_64 rng(seed);
        auto sample_candidate = [&]()
        {
            std::uint64_t draw = rng();
            std::normal_distribution<double> local(0.0, 1.0);
            std::mt19937_64 local_rng(draw);
            std::vector<double> theta(dimension);
            for (std::size_t i = 0; i < dimension; ++i)
            {
                theta[i] = base[i] + local(local_rng) * scales[i] * 0.25;
            }
            return theta;
        };
        std::vector<double> theta_a = sample_candidate();
        std::vector<double> theta_b = sample_candidate();
        std::uint64_t seed_a = tuning::mix64(seed ^ 0xA24BAED4963EE407ULL);
        std::uint64_t seed_b = tuning::mix64(seed ^ 0x9FB21C651E98DF25ULL);
        tuner_match::Scenario scenario_a = tuner_match::make_scenario(seed_a, static_cast<std::size_t>(max_rounds), static_cast<std::size_t>(tuner_match::next_length));
        tuner_match::Scenario scenario_b = tuner_match::make_scenario(seed_b, static_cast<std::size_t>(max_rounds), static_cast<std::size_t>(tuner_match::next_length));
        tuner_match::BotInstance b1;
        tuner_match::BotInstance b2;
        b1.scenario = &scenario_a;
        b2.scenario = &scenario_b;
        b1.search_budget = m_tetris::SearchBudget::by_iterations(iters);
        b2.search_budget = m_tetris::SearchBudget::by_iterations(iters);
        b1.init(theta_a.data());
        b2.init(theta_b.data());
        std::println("view seed {} iters {} max_rounds {}", seed, iters, max_rounds);
        tuner_match::MatchResult result = tuner_match::play_match(b1, b2, max_rounds,
            [&]() { tuner_match::render_view(b1, b2, "A", "B"); }, nullptr);
        tuner_match::render_view(b1, b2, "A", "B");
        std::string winner = result.winner > 0 ? "A" : (result.winner < 0 ? "B" : "draw");
        std::println("view done winner {} rounds {} capped {} app {:.2f} {:.2f} apl {:.2f} {:.2f}",
            winner, result.rounds, result.capped ? 1 : 0, result.app1, result.app2, result.apl1, result.apl2);
        return 0;
    }

    int run_smoke()
    {
        std::filesystem::path temp_dir = std::filesystem::temp_directory_path() / "tournament_tuner_smoke";
        std::error_code error;
        std::filesystem::create_directories(temp_dir, error);
        std::string data_file = (temp_dir / "smoke_data.bin").string();
        std::string incumbent_file = (temp_dir / "smoke_incumbent.bin").string();
        std::string current_file = (temp_dir / "smoke_current.bin").string();
        std::filesystem::remove(data_file);
        std::filesystem::remove(data_file + ".bak");
        std::vector<double> incumbent = default_theta_vec();
        if (!write_theta_bin(incumbent_file, incumbent))
        {
            std::println(stderr, "smoke: cannot write incumbent file");
            return 1;
        }
        int const workers = 2;
        int const roster = 4;
        int const lambda = 3;
        int const dimension = static_cast<int>(tuning_toj::TojAdapter::param_count());
        tournament_cmaes::Configuration cma_config;
        cma_config.dimension = dimension;
        cma_config.mean = incumbent;
        cma_config.coordinate_scales = scales_vec();
        cma_config.lambda = lambda;
        cma_config.seed = 424242 % 2147483647ULL + 1;
        tournament_cmaes::Optimizer optimizer(cma_config);
        auto const &flat = optimizer.ask();
        std::vector<tournament_runner::RosterEntry> roster_entries;
        roster_entries.reserve(static_cast<std::size_t>(roster));
        roster_entries.push_back({1, incumbent});
        for (int i = 0; i < lambda; ++i)
        {
            std::vector<double> theta(dimension);
            for (int j = 0; j < dimension; ++j)
            {
                theta[static_cast<std::size_t>(j)] = flat[static_cast<std::size_t>(i) * static_cast<std::size_t>(dimension) + static_cast<std::size_t>(j)];
            }
            roster_entries.push_back({static_cast<std::uint64_t>(i + 2), theta});
        }
        auto shared_context = tuning_toj::TojAdapter::make_shared_context();
        if (!shared_context)
        {
            std::println(stderr, "smoke: cannot prepare shared context");
            return 1;
        }
        TojBackend backend(shared_context);
        tuning::RunConfig run_config;
        run_config.threads = workers;
        run_config.iterations_per_move = 5;
        run_config.max_rounds = 100;
        tournament_runner::RunLimits limits;
        limits.wave_limit = 0;
        limits.max_games = 20000;
        limits.max_draws = 10000;
        Runner runner(backend, roster_entries, 987654321ULL, run_config, limits);
        auto result = runner.run();
        if (result.error.code != tournament_runner::ErrorCode::None)
        {
            std::println(stderr, "smoke: tournament failed: {}", result.error.detail);
            return 1;
        }
        if (!result.complete)
        {
            std::println(stderr, "smoke: tournament did not complete");
            return 1;
        }
        std::vector<tournament_rating::GameRecord> rating_records;
        rating_records.reserve(runner.ledger().size());
        for (auto const &record : runner.ledger())
        {
            double score = 0.5;
            if (record.winner == tournament_bracket::GameWinner::SideA)
            {
                score = 1.0;
            }
            else if (record.winner == tournament_bracket::GameWinner::SideB)
            {
                score = 0.0;
            }
            rating_records.push_back({record.seat.side_a, record.seat.side_b, score,
                tournament_runner::series_scenario_tag(record.series_id, record.game_index)});
        }
        auto fit = tournament_rating::fit(rating_records);
        if (!fit.ok)
        {
            std::println(stderr, "smoke: rating failed: {}", fit.error);
            return 1;
        }
        std::vector<std::uint64_t> sample_ids;
        for (std::size_t i = 1; i < roster_entries.size(); ++i)
        {
            sample_ids.push_back(roster_entries[i].id);
        }
        auto ordinal = tournament_ordinal::build(fit, sample_ids);
        if (!ordinal.ok)
        {
            std::println(stderr, "smoke: ordinal failed: {}", ordinal.error);
            return 1;
        }
        try
        {
            optimizer.tell(ordinal.fitness);
        }
        catch (std::exception const &error)
        {
            std::println(stderr, "smoke: cma tell failed: {}", error.what());
            return 1;
        }
        tournament_checkpoint::Envelope envelope;
        envelope.identity = make_identity();
        envelope.generation = 1;
        envelope.root_seed = 987654321ULL;
        envelope.payload = {{"smoke", true}, {"games", result.stats.games}};
        std::string save_error;
        if (!tournament_checkpoint::save(data_file, envelope, save_error))
        {
            std::println(stderr, "smoke: checkpoint save failed: {}", save_error);
            return 1;
        }
        auto loaded = tournament_checkpoint::load_with_backup(data_file, make_identity());
        if (loaded.status != tournament_checkpoint::LoadStatus::Ok)
        {
            std::println(stderr, "smoke: checkpoint reload failed");
            return 1;
        }
        std::println("SMOKE OK games={} draws={} waves={} champion={} checksum={}",
            result.stats.games, result.stats.draws, result.stats.waves,
            result.champion, runner.checksum());
        std::filesystem::remove(data_file);
        std::filesystem::remove(data_file + ".bak");
        std::filesystem::remove(incumbent_file);
        std::filesystem::remove(current_file);
        return 0;
    }

    int run_tournament(TunerConfig const &cli)
    {
        int const workers = workers_for(cli.threads);
        int const roster_size = roster_size_for(workers);
        int const lambda = lambda_for(roster_size);
        int const dimension = static_cast<int>(tuning_toj::TojAdapter::param_count());
        tuning::RunConfig run_config;
        run_config.threads = workers;
        run_config.iterations_per_move = cli.iters_per_move;
        run_config.max_rounds = cli.max_rounds;
        if (!tuning::valid_run_config(run_config))
        {
            std::println(stderr, "invalid run configuration");
            return 1;
        }
        tournament_runner::RunLimits limits;
        limits.wave_limit = 0;
        limits.max_games = 20000;
        limits.max_draws = 10000;
        tournament_checkpoint::Identity identity = make_identity();
        std::uint64_t root_seed = cli.root_seed == 0 ? 555 : cli.root_seed;
        std::uint64_t start_generation = 0;
        std::vector<double> incumbent = default_theta_vec();
        tournament_cmaes::Configuration cma_config;
        cma_config.dimension = dimension;
        cma_config.mean = incumbent;
        cma_config.coordinate_scales = scales_vec();
        cma_config.lambda = lambda;
        cma_config.seed = cma_seed_for(root_seed);
        std::vector<std::uint8_t> cma_blob;
        bool have_checkpoint = false;
        auto loaded = tournament_checkpoint::load_with_backup(cli.data_file, identity);
        if (loaded.status == tournament_checkpoint::LoadStatus::Ok)
        {
            try
            {
                nlohmann::json const &payload = loaded.envelope.payload;
                std::uint64_t saved_root = loaded.envelope.root_seed;
                std::uint64_t saved_generation = loaded.envelope.generation;
                int saved_dimension = payload.at("dimension").get<int>();
                int saved_lambda = payload.at("lambda").get<int>();
                std::size_t saved_iters = payload.at("iters_per_move").get<std::size_t>();
                int saved_rounds = payload.at("max_rounds").get<int>();
                std::vector<double> saved_incumbent;
                std::vector<double> saved_mean;
                std::vector<double> saved_scales;
                if (!json_to_theta(payload.at("incumbent"), saved_incumbent))
                {
                    std::println(stderr, "checkpoint has an invalid incumbent vector");
                    return 1;
                }
                if (!json_to_theta(payload.at("cma_mean"), saved_mean))
                {
                    std::println(stderr, "checkpoint has an invalid cma mean vector");
                    return 1;
                }
                saved_scales.resize(payload.at("cma_scales").size());
                for (std::size_t i = 0; i < saved_scales.size(); ++i)
                {
                    saved_scales[i] = payload.at("cma_scales")[i].get<double>();
                }
                auto blob_hex = payload.at("cma_blob_hex").get<std::string>();
                auto blob = tournament_bytes::decode_hex(blob_hex);
                if (!blob.has_value())
                {
                    std::println(stderr, "checkpoint has an invalid cma blob");
                    return 1;
                }
                if (saved_dimension != dimension || saved_lambda != lambda
                    || saved_iters != cli.iters_per_move || saved_rounds != cli.max_rounds)
                {
                    std::println(stderr, "checkpoint configuration mismatch, delete {} for a fresh run", cli.data_file);
                    return 1;
                }
                root_seed = saved_root;
                start_generation = saved_generation;
                incumbent = saved_incumbent;
                cma_config.mean = saved_mean;
                cma_config.coordinate_scales = saved_scales;
                cma_config.seed = payload.at("cma_seed").get<std::uint64_t>();
                cma_blob = std::move(*blob);
                have_checkpoint = true;
                std::println("Resumed from generation {} root_seed {}", start_generation, root_seed);
            }
            catch (std::exception const &error)
            {
                std::println(stderr, "checkpoint payload is malformed: {}", error.what());
                return 1;
            }
        }
        else if (loaded.status != tournament_checkpoint::LoadStatus::Missing)
        {
            std::println(stderr, "checkpoint load failed: {}", loaded.detail);
            return 1;
        }
        if (static_cast<int>(start_generation) >= cli.generations)
        {
            std::println("Nothing to do, start generation {} >= generations {}", start_generation, cli.generations);
            return 0;
        }
        if (!have_checkpoint)
        {
            std::vector<double> file_theta;
            if (read_theta_bin(cli.incumbent_file, file_theta))
            {
                incumbent = file_theta;
                cma_config.mean = file_theta;
                std::println("Loaded incumbent from {}", cli.incumbent_file);
            }
            else
            {
                if (!write_theta_bin(cli.incumbent_file, incumbent))
                {
                    std::println(stderr, "cannot write {}", cli.incumbent_file);
                    return 1;
                }
                std::println("Fresh incumbent from production defaults");
            }
        }
        std::unique_ptr<tournament_cmaes::Optimizer> optimizer;
        try
        {
            optimizer = std::make_unique<tournament_cmaes::Optimizer>(cma_config);
            if (have_checkpoint)
            {
                optimizer->load_state(cma_blob);
            }
        }
        catch (std::exception const &error)
        {
            std::println(stderr, "cma initialization failed: {}", error.what());
            return 1;
        }
        auto shared_context = tuning_toj::TojAdapter::make_shared_context();
        if (!shared_context)
        {
            std::println(stderr, "cannot prepare shared TOJ context");
            return 1;
        }
        for (std::uint64_t generation = start_generation; generation < static_cast<std::uint64_t>(cli.generations); ++generation)
        {
            std::vector<double> const &flat = optimizer->ask();
            if (flat.size() != static_cast<std::size_t>(lambda) * static_cast<std::size_t>(dimension))
            {
                std::println(stderr, "cma ask returned an unexpected population size");
                return 1;
            }
            std::vector<tournament_runner::RosterEntry> roster_entries;
            roster_entries.reserve(static_cast<std::size_t>(lambda) + 1);
            roster_entries.push_back({1, incumbent});
            std::vector<std::uint64_t> sample_ids;
            sample_ids.reserve(static_cast<std::size_t>(lambda));
            for (int i = 0; i < lambda; ++i)
            {
                std::vector<double> theta(static_cast<std::size_t>(dimension));
                for (int j = 0; j < dimension; ++j)
                {
                    theta[static_cast<std::size_t>(j)] = flat[static_cast<std::size_t>(i) * static_cast<std::size_t>(dimension) + static_cast<std::size_t>(j)];
                }
                std::uint64_t id = static_cast<std::uint64_t>(i + 2);
                roster_entries.push_back({id, theta});
                sample_ids.push_back(id);
            }
            TojBackend backend(shared_context);
            std::uint64_t generation_seed = generation_seed_for(root_seed, generation);
            Runner runner(backend, roster_entries, generation_seed, run_config, limits);
            auto run_result = runner.run();
            if (run_result.error.code != tournament_runner::ErrorCode::None)
            {
                std::println(stderr, "generation {} tournament failed: {}", generation, run_result.error.detail);
                return 1;
            }
            if (!run_result.complete)
            {
                std::println(stderr, "generation {} tournament did not complete", generation);
                return 1;
            }
            std::vector<tournament_rating::GameRecord> rating_records;
            rating_records.reserve(runner.ledger().size());
            for (auto const &record : runner.ledger())
            {
                double score = 0.5;
                if (record.winner == tournament_bracket::GameWinner::SideA)
                {
                    score = 1.0;
                }
                else if (record.winner == tournament_bracket::GameWinner::SideB)
                {
                    score = 0.0;
                }
                rating_records.push_back({record.seat.side_a, record.seat.side_b, score,
                    tournament_runner::series_scenario_tag(record.series_id, record.game_index)});
            }
            auto fit = tournament_rating::fit(rating_records);
            if (!fit.ok)
            {
                std::println(stderr, "generation {} rating failed: {}", generation, fit.error);
                return 1;
            }
            auto ordinal = tournament_ordinal::build(fit, sample_ids);
            if (!ordinal.ok)
            {
                std::println(stderr, "generation {} ordinal failed: {}", generation, ordinal.error);
                return 1;
            }
            try
            {
                optimizer->tell(ordinal.fitness);
            }
            catch (std::exception const &error)
            {
                std::println(stderr, "generation {} cma tell failed: {}", generation, error.what());
                return 1;
            }
            std::uint64_t champion_id = runner.champion();
            std::vector<double> champion_theta;
            for (auto const &entry : roster_entries)
            {
                if (entry.id == champion_id)
                {
                    champion_theta = entry.theta;
                }
            }
            if (champion_theta.empty())
            {
                std::println(stderr, "generation {} champion is missing from roster", generation);
                return 1;
            }
            if (!write_theta_bin(cli.current_file, champion_theta))
            {
                std::println(stderr, "cannot write {}", cli.current_file);
                return 1;
            }
            bool promoted = false;
            double promotion_mean = 0.0;
            double promotion_lb = 0.0;
            if (champion_id != 1)
            {
                tournament_promotion::Options promotion_options;
                promotion_options.pairs = cli.pairs;
                promotion_options.promotion_threshold = cli.threshold;
                promotion_options.seed = promotion_seed_for(root_seed, generation);
                promotion_options.first_game_id = 0xE000000000000000ULL + generation * 1000000ULL;
                auto promotion = tournament_promotion::evaluate(backend, champion_theta, incumbent, run_config, promotion_options);
                if (!promotion.ok)
                {
                    std::println(stderr, "generation {} promotion failed: {}", generation, promotion.error);
                    return 1;
                }
                promotion_mean = promotion.mean_score;
                promotion_lb = promotion.lower_bound;
                promoted = promotion.promoted;
                if (promoted)
                {
                    incumbent = champion_theta;
                    if (!write_theta_bin(cli.incumbent_file, incumbent))
                    {
                        std::println(stderr, "cannot write {}", cli.incumbent_file);
                        return 1;
                    }
                }
            }
            std::vector<std::uint8_t> blob = optimizer->save_state();
            std::string blob_hex = tournament_bytes::encode_hex(blob);
            tournament_checkpoint::Envelope envelope;
            envelope.identity = identity;
            envelope.generation = generation + 1;
            envelope.root_seed = root_seed;
            nlohmann::json payload;
            payload["dimension"] = dimension;
            payload["lambda"] = lambda;
            payload["iters_per_move"] = run_config.iterations_per_move;
            payload["max_rounds"] = run_config.max_rounds;
            payload["pairs"] = cli.pairs;
            payload["threshold"] = cli.threshold;
            payload["cma_seed"] = optimizer->seed();
            payload["cma_mean"] = theta_to_json(cma_config.mean);
            payload["cma_scales"] = theta_to_json(cma_config.coordinate_scales);
            payload["cma_blob_hex"] = blob_hex;
            payload["cma_sigma"] = optimizer->sigma();
            payload["incumbent"] = theta_to_json(incumbent);
            payload["champion"] = champion_id;
            payload["champion_theta"] = theta_to_json(champion_theta);
            payload["games"] = run_result.stats.games;
            payload["draws"] = run_result.stats.draws;
            payload["waves"] = run_result.stats.waves;
            payload["checksum"] = runner.checksum();
            payload["promoted"] = promoted;
            payload["promotion_mean"] = promotion_mean;
            payload["promotion_lb"] = promotion_lb;
            nlohmann::json standings = nlohmann::json::array();
            for (auto id : runner.standings())
            {
                standings.push_back(id);
            }
            payload["standings"] = standings;
            nlohmann::json ratings_json = nlohmann::json::array();
            for (auto const &entry : fit.ratings)
            {
                nlohmann::json item;
                item["candidate"] = entry.candidate;
                item["rating"] = entry.rating;
                item["std_error"] = entry.std_error;
                item["expected_rank"] = entry.expected_rank;
                item["games"] = entry.games;
                item["wins"] = entry.wins;
                item["draws"] = entry.draws;
                item["losses"] = entry.losses;
                ratings_json.push_back(item);
            }
            payload["ratings"] = ratings_json;
            envelope.payload = payload;
            std::string save_error;
            if (!tournament_checkpoint::save(cli.data_file, envelope, save_error))
            {
                std::println(stderr, "cannot save checkpoint: {}", save_error);
                return 1;
            }
            std::println("gen {} games {} draws {} waves {} champion {} checksum {} sigma {:.4f} promoted {} mean {:.3f} lb {:.3f}",
                generation, run_result.stats.games, run_result.stats.draws, run_result.stats.waves,
                champion_id, runner.checksum(), optimizer->sigma(), promoted ? 1 : 0, promotion_mean, promotion_lb);
            std::size_t show = std::min<std::size_t>(5, fit.ratings.size());
            for (std::size_t i = 0; i < show; ++i)
            {
                auto const &entry = fit.ratings[i];
                std::println("  rank {} id {} rating {:.4f} se {:.4f} exprank {:.2f} games {} w {} d {} l {}",
                    i + 1, entry.candidate, entry.rating, entry.std_error, entry.expected_rank,
                    entry.games, entry.wins, entry.draws, entry.losses);
            }
        }
        return 0;
    }
}

int main(int argc, char *argv[])
{
    std::setbuf(stdout, nullptr);
    std::setbuf(stderr, nullptr);
    std::string program = argc > 0 ? argv[0] : "tournament_tuner";
    if (argc > 1)
    {
        std::string first = argv[1];
        if (first == "help" || first == "--help" || first == "-h")
        {
            tournament_tuner::print_usage(program.c_str());
            return 0;
        }
        if (first == "selfcheck")
        {
            return tournament_tuner::run_selfcheck();
        }
        if (first == "smoke")
        {
            return tournament_tuner::run_smoke();
        }
        if (first == "view")
        {
            std::uint64_t seed = 0;
            std::size_t iters = 50;
            int max_rounds = 3600;
            try
            {
                if (argc > 2)
                {
                    seed = static_cast<std::uint64_t>(std::stoull(argv[2]));
                }
                if (argc > 3)
                {
                    iters = static_cast<std::size_t>(std::stoul(argv[3]));
                }
                if (argc > 4)
                {
                    max_rounds = std::stoi(argv[4]);
                }
            }
            catch (std::exception const &error)
            {
                std::println(stderr, "invalid view arguments: {}", error.what());
                tournament_tuner::print_usage(program.c_str());
                return 1;
            }
            return tournament_tuner::run_view(seed, iters, max_rounds);
        }
    }
    tournament_tuner::TunerConfig config;
    try
    {
        if (argc > 1)
        {
            config.generations = std::stoi(argv[1]);
        }
        if (argc > 2)
        {
            config.iters_per_move = static_cast<std::size_t>(std::stoul(argv[2]));
        }
        if (argc > 3)
        {
            config.root_seed = static_cast<std::uint64_t>(std::stoull(argv[3]));
        }
        if (argc > 4)
        {
            config.threads = std::stoi(argv[4]);
        }
        if (argc > 5)
        {
            config.max_rounds = std::stoi(argv[5]);
        }
        if (argc > 6)
        {
            config.pairs = std::stoi(argv[6]);
        }
        if (argc > 7)
        {
            config.data_file = argv[7];
        }
    }
    catch (std::exception const &error)
    {
        std::println(stderr, "invalid arguments: {}", error.what());
        tournament_tuner::print_usage(program.c_str());
        return 1;
    }
    if (config.generations <= 0 || config.iters_per_move == 0
        || config.threads < 0 || config.max_rounds <= 0 || config.pairs < 2)
    {
        std::println(stderr, "invalid tournament configuration");
        tournament_tuner::print_usage(program.c_str());
        return 1;
    }
    if (!std::isfinite(config.threshold) || config.threshold < 0.0 || config.threshold > 1.0)
    {
        std::println(stderr, "invalid promotion threshold");
        return 1;
    }
    return tournament_tuner::run_tournament(config);
}
