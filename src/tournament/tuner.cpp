#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <print>
#include <random>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "ai_zzz.h"
#include "param.h"
#include "tuner_match.h"
#include "tournament/audit.h"
#include "tournament/ban_file.h"
#include "tournament/bracket.h"
#include "tournament/bytes.h"
#include "tournament/checkpoint.h"
#include "tournament/cmaes.h"
#include "tournament/config_file.h"
#include "tournament/engine_identity.h"
#include "tournament/journal.h"
#if defined(TUNER_HAS_REMOTE)
#include "tournament/net_transport.h"
#endif
#include "tournament/ordinal.h"
#include "tournament/promotion.h"
#include "tournament/provenance.h"
#include "tournament/rating.h"
#include "tournament/registry.h"
#include "tournament/remote_backend.h"
#include "tournament/repair.h"
#include "tournament/runner.h"
#include "tournament/runtime_backend.h"
#include "tournament/scheduler.h"
#include "tournament/transport.h"
#include "tuning/domain.h"
#include "tuning/engine_match.h"
#include "tuning/match.h"
#include "tuning/toj_adapter.h"

namespace tournament_tuner
{
    using TojBackend = tuning::EngineMatchBackend<tuning_toj::TojAdapter>;
    using LocalRunner = tournament_runner::TournamentRunner<TojBackend>;
    using RuntimeRunner = tournament_runner::TournamentRunner<tournament_runtime::RuntimeBackend>;

    struct TunerConfig
    {
        int generations = 10;
        std::size_t iters_per_move = 50;
        std::uint64_t root_seed = 555;
        int threads = 0;
        int max_rounds = 3600;
        int pairs = 32;
        bool fresh_zero = false;
        double threshold = 0.5;
        std::string data_file = "tournament_data.bin";
        std::string incumbent_file = "tournament_incumbent.bin";
        std::string current_file = "tournament_current.bin";
        int remote_port = 0;
        std::string remote_address = "0.0.0.0";
        std::string remote_certificate = "tournament_host.cert";
        std::string remote_key = "tournament_host.key";
        std::string devices_file;
        double audit_rate = 0.25;
        std::string journal_file = "tournament_journal.bin";
        std::string ban_file = "tournament_bans.txt";
        int wait_clients_ms = 120000;
        std::uint64_t lease_ms = 30000;
        int games_per_assignment = 4;
        int series_cap = 2;
        int roster = 0;
    };

    int thread_budget_for(int threads_arg)
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

    int game_workers_for(int budget)
    {
        return std::max(1, budget / 2);
    }

    int roster_size_for(int pso_count)
    {
        return std::max(2, pso_count * 2);
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

    nlohmann::json ledger_to_json(std::vector<tournament_runner::GameRecord> const &ledger)
    {
        nlohmann::json array = nlohmann::json::array();
        for (auto const &record : ledger)
        {
            nlohmann::json seat;
            seat["side_a"] = record.seat.side_a;
            seat["side_b"] = record.seat.side_b;
            seat["side_a_is_player_one"] = record.seat.side_a_is_player_one;
            nlohmann::json item;
            item["game_id"] = record.game_id;
            item["series_id"] = record.series_id;
            item["game_index"] = record.game_index;
            item["seat"] = seat;
            item["seed_player_one"] = record.seed_player_one;
            item["seed_player_two"] = record.seed_player_two;
            item["winner"] = static_cast<int>(record.winner);
            item["reason"] = static_cast<int>(record.reason);
            item["rounds"] = record.rounds;
            array.push_back(item);
        }
        return array;
    }

    bool json_get_u64(nlohmann::json const &json, std::uint64_t &out)
    {
        if (json.is_number_unsigned())
        {
            out = json.get<std::uint64_t>();
            return true;
        }
        if (json.is_number_integer())
        {
            std::int64_t value = json.get<std::int64_t>();
            if (value >= 0)
            {
                out = static_cast<std::uint64_t>(value);
                return true;
            }
        }
        return false;
    }

    bool json_get_int(nlohmann::json const &json, int &out)
    {
        if (!json.is_number_integer())
        {
            return false;
        }
        std::int64_t value = json.get<std::int64_t>();
        if (value < 0 || value > std::numeric_limits<int>::max())
        {
            return false;
        }
        out = static_cast<int>(value);
        return true;
    }

    bool json_to_ledger(nlohmann::json const &json, std::vector<tournament_runner::GameRecord> &ledger)
    {
        if (!json.is_array())
        {
            return false;
        }
        std::vector<tournament_runner::GameRecord> records;
        records.reserve(json.size());
        for (auto const &item : json)
        {
            if (!item.is_object() || !item.contains("seat") || !item.at("seat").is_object())
            {
                return false;
            }
            tournament_runner::GameRecord record;
            nlohmann::json const &seat = item.at("seat");
            if (!json_get_u64(item.at("game_id"), record.game_id)
                || !json_get_int(item.at("series_id"), record.series_id)
                || !json_get_int(item.at("game_index"), record.game_index)
                || !json_get_u64(seat.at("side_a"), record.seat.side_a)
                || !json_get_u64(seat.at("side_b"), record.seat.side_b)
                || !seat.at("side_a_is_player_one").is_boolean()
                || !json_get_u64(item.at("seed_player_one"), record.seed_player_one)
                || !json_get_u64(item.at("seed_player_two"), record.seed_player_two))
            {
                return false;
            }
            record.seat.side_a_is_player_one = seat.at("side_a_is_player_one").get<bool>();
            int winner = -1;
            int reason = -1;
            int rounds = -1;
            if (!json_get_int(item.at("winner"), winner) || winner < 0 || winner > 2
                || !json_get_int(item.at("reason"), reason) || reason < 0 || reason > 8
                || !json_get_int(item.at("rounds"), rounds))
            {
                return false;
            }
            record.winner = static_cast<tournament_bracket::GameWinner>(winner);
            record.reason = static_cast<tuning::WinReason>(reason);
            record.rounds = rounds;
            records.push_back(record);
        }
        ledger = std::move(records);
        return true;
    }

    nlohmann::json roster_to_json(std::vector<tournament_runner::RosterEntry> const &roster)
    {
        nlohmann::json ids = nlohmann::json::array();
        nlohmann::json thetas = nlohmann::json::array();
        for (auto const &entry : roster)
        {
            ids.push_back(entry.id);
            thetas.push_back(theta_to_json(entry.theta));
        }
        nlohmann::json json;
        json["ids"] = ids;
        json["thetas"] = thetas;
        return json;
    }

    bool json_to_roster(nlohmann::json const &json, std::vector<tournament_runner::RosterEntry> &roster)
    {
        if (!json.is_object() || !json.contains("ids") || !json.contains("thetas"))
        {
            return false;
        }
        nlohmann::json const &ids = json.at("ids");
        nlohmann::json const &thetas = json.at("thetas");
        if (!ids.is_array() || !thetas.is_array() || ids.size() != thetas.size() || ids.empty())
        {
            return false;
        }
        std::vector<tournament_runner::RosterEntry> entries;
        entries.reserve(ids.size());
        for (std::size_t i = 0; i < ids.size(); ++i)
        {
            std::uint64_t id = 0;
            std::vector<double> theta;
            if (!json_get_u64(ids[i], id) || !json_to_theta(thetas[i], theta))
            {
                return false;
            }
            entries.push_back({id, theta});
        }
        roster = std::move(entries);
        return true;
    }

    bool save_progress(std::string const &data_file, tournament_checkpoint::Identity const &identity,
                       std::uint64_t generation, std::uint64_t root_seed, TunerConfig const &cli,
                       tournament_cmaes::Configuration const &cma_config, std::string const &blob_hex,
                       std::vector<double> const &incumbent,
                       std::vector<tournament_runner::RosterEntry> const &roster,
                       std::vector<tournament_runner::GameRecord> const &ledger, std::int64_t waves,
                       std::string &error)
    {
        tournament_checkpoint::Envelope envelope;
        envelope.identity = identity;
        envelope.generation = generation;
        envelope.root_seed = root_seed;
        nlohmann::json payload;
        payload["stage"] = "progress";
        payload["dimension"] = cma_config.dimension;
        payload["lambda"] = cma_config.lambda;
        payload["iters_per_move"] = cli.iters_per_move;
        payload["max_rounds"] = cli.max_rounds;
        payload["pairs"] = cli.pairs;
        payload["threshold"] = cli.threshold;
        payload["cma_seed"] = cma_config.seed;
        payload["cma_mean"] = theta_to_json(cma_config.mean);
        payload["cma_scales"] = theta_to_json(cma_config.coordinate_scales);
        payload["cma_blob_hex"] = blob_hex;
        payload["incumbent"] = theta_to_json(incumbent);
        payload["roster"] = roster_to_json(roster);
        payload["ledger"] = ledger_to_json(ledger);
        payload["waves"] = waves;
        envelope.payload = payload;
        return tournament_checkpoint::save(data_file, envelope, error);
    }

    bool apply_config_file(TunerConfig &config, nlohmann::json const &values, std::string &error)
    {
        std::vector<std::string> const allowed{
            "generations", "iters_per_move", "seed", "threads", "max_rounds", "pairs", "data_file",
            "incumbent_file", "current_file", "fresh_zero", "threshold", "remote_port",
            "remote_address", "remote_cert", "remote_key", "devices_file", "audit_rate",
            "journal_file", "ban_file", "wait_clients_ms", "lease_ms", "games_per_assignment",
            "series_cap", "roster",
        };
        if (!tournament_config::reject_unknown_keys(values, allowed, error))
        {
            return false;
        }
        if (!tournament_config::get_int(values, "generations", config.generations, error))
        {
            return false;
        }
        if (values.contains("iters_per_move"))
        {
            std::uint64_t value = 0;
            if (!tournament_config::get_u64(values, "iters_per_move", value, error))
            {
                return false;
            }
            config.iters_per_move = static_cast<std::size_t>(value);
        }
        if (!tournament_config::get_u64(values, "seed", config.root_seed, error))
        {
            return false;
        }
        if (!tournament_config::get_int(values, "threads", config.threads, error))
        {
            return false;
        }
        if (!tournament_config::get_int(values, "max_rounds", config.max_rounds, error))
        {
            return false;
        }
        if (!tournament_config::get_int(values, "pairs", config.pairs, error))
        {
            return false;
        }
        if (!tournament_config::get_string(values, "data_file", config.data_file, error))
        {
            return false;
        }
        if (!tournament_config::get_string(values, "incumbent_file", config.incumbent_file, error))
        {
            return false;
        }
        if (!tournament_config::get_string(values, "current_file", config.current_file, error))
        {
            return false;
        }
        if (!tournament_config::get_bool(values, "fresh_zero", config.fresh_zero, error))
        {
            return false;
        }
        if (!tournament_config::get_double(values, "threshold", config.threshold, error))
        {
            return false;
        }
        if (!tournament_config::get_int(values, "remote_port", config.remote_port, error))
        {
            return false;
        }
        if (!tournament_config::get_string(values, "remote_address", config.remote_address, error))
        {
            return false;
        }
        if (!tournament_config::get_string(values, "remote_cert", config.remote_certificate, error))
        {
            return false;
        }
        if (!tournament_config::get_string(values, "remote_key", config.remote_key, error))
        {
            return false;
        }
        if (!tournament_config::get_string(values, "devices_file", config.devices_file, error))
        {
            return false;
        }
        if (!tournament_config::get_double(values, "audit_rate", config.audit_rate, error))
        {
            return false;
        }
        if (!tournament_config::get_string(values, "journal_file", config.journal_file, error))
        {
            return false;
        }
        if (!tournament_config::get_string(values, "ban_file", config.ban_file, error))
        {
            return false;
        }
        if (!tournament_config::get_int(values, "wait_clients_ms", config.wait_clients_ms, error))
        {
            return false;
        }
        if (!tournament_config::get_u64(values, "lease_ms", config.lease_ms, error))
        {
            return false;
        }
        if (!tournament_config::get_int(values, "games_per_assignment", config.games_per_assignment,
                                        error))
        {
            return false;
        }
        if (!tournament_config::get_int(values, "series_cap", config.series_cap, error))
        {
            return false;
        }
        if (!tournament_config::get_int(values, "roster", config.roster, error))
        {
            return false;
        }
        return true;
    }

    void print_usage(char const *program)
    {
        std::println("Usage:");
        std::println("  {} [generations] [iters_per_move] [seed] [threads] [max_rounds] [pairs] [data_file]", program);
        std::println("  {} selfcheck", program);
        std::println("  {} smoke", program);
        std::println("  {} status [data_file]", program);
        std::println("  {} help", program);
        std::println("");
        std::println("Defaults: generations 10, iters_per_move 50, seed 555, threads 0 (auto), max_rounds 3600, pairs 32, data tournament_data.bin");
        std::println("  {} view [seed] [iters] [max_rounds]", program);
        std::println("View defaults: seed 0 (time-based, printed for replay), iters 50, max_rounds 3600");
        std::println("Threading: budget = threads if given else hardware_threads - 1, game workers = budget / 2, helpers = game workers, roster = 2 * budget (pso candidate count), lambda = roster - 1");
        std::println("Roster: in remote mode an unset roster defaults to 16 candidates instead of the host thread count, because games run on the clients; set roster in the config file to scale the search with the worker pool");
        std::println("Config: every parameter above can live in a json file; tournament_tuner.json in the working directory is read automatically, --config P reads another file, command line values override the file");
        std::println("Config keys: generations, iters_per_move, seed, threads, max_rounds, pairs, data_file, incumbent_file, current_file, fresh_zero, threshold, remote_port, remote_address, remote_cert, remote_key, devices_file, audit_rate, journal_file, ban_file, wait_clients_ms, lease_ms, games_per_assignment, series_cap, roster");
        std::println("Flags: --fresh-zero starts a fresh run from zero weights instead of the incumbent file (ignored when a checkpoint exists)");
        std::println("Flags: --remote-port N distributes matches to remote_client devices over TLS (certificate and key are generated on first use, clients verify the printed fingerprint)");
        std::println("Flags: --remote-cert P and --remote-key P override the certificate paths, --audit-rate R sets the audit sample rate (default 0.25, remote mode only)");
        std::println("Flags: --journal-file P sets the provenance journal path (default tournament_journal.bin, remote mode only), --wait-clients-ms N bounds the initial client wait (default 120000)");
        std::println("Flags: --lease-ms N sets the remote result lease (default 30000, remote mode only), --games-per-assignment N batches that many games per remote flight (default 4)");
        std::println("Flags: --series-cap N caps one device's accepted games per series (default 2, raise it for small device pools so late bracket series can spread)");
        std::println("Flags: --ban-file P persists banned device keys across restarts (default tournament_bans.txt, empty string disables)");
        std::println("Flags: --devices-file P restricts remote enrollment to the device ids and keys listed in P (default open enrollment)");
        std::println("Search: iteration budgets only, no time budgets");
        std::println("Checkpoint: tournament_data.bin with .bak fallback, resume by generation");
        std::println("During runs: type view and Enter for one live game per wave, empty line to stop, bracket for live standings");
        std::println("Outputs: tournament_incumbent.bin anchor, tournament_current.bin latest champion");
        std::println("Existing tuner files are not touched");
    }

    int run_status(std::string const &data_file)
    {
        auto loaded = tournament_checkpoint::load_with_backup(data_file, make_identity());
        if (loaded.status == tournament_checkpoint::LoadStatus::Missing)
        {
            std::println("no checkpoint at {}", data_file);
            return 1;
        }
        if (loaded.status != tournament_checkpoint::LoadStatus::Ok)
        {
            std::println(stderr, "checkpoint load failed: {}", loaded.detail);
            return 1;
        }
        nlohmann::json const &payload = loaded.envelope.payload;
        try
        {
            std::string stage = "complete";
            if (payload.contains("stage") && payload.at("stage").is_string())
            {
                stage = payload.at("stage").get<std::string>();
            }
            if (stage == "progress")
            {
                std::println("data {} backup {} generation {} in progress root_seed {}",
                    data_file, loaded.backup_used ? 1 : 0,
                    loaded.envelope.generation, loaded.envelope.root_seed);
                std::println("ledger games {} roster {} waves {}",
                    payload.at("ledger").size(), payload.at("roster").at("ids").size(),
                    payload.at("waves").get<std::int64_t>());
                return 0;
            }
            std::println("data {} backup {} generation {} root_seed {}",
                data_file, loaded.backup_used ? 1 : 0,
                loaded.envelope.generation, loaded.envelope.root_seed);
            std::println("config dimension {} lambda {} iters {} rounds {} pairs {} cma_seed {} sigma {:.4f}",
                payload.at("dimension").get<int>(), payload.at("lambda").get<int>(),
                payload.at("iters_per_move").get<std::size_t>(), payload.at("max_rounds").get<int>(),
                payload.at("pairs").get<int>(), payload.at("cma_seed").get<std::uint64_t>(),
                payload.at("cma_sigma").get<double>());
            std::println("last champion {} games {} draws {} waves {} checksum {} promoted {} mean {:.3f} lb {:.3f}",
                payload.at("champion").get<std::uint64_t>(),
                payload.at("games").get<std::int64_t>(), payload.at("draws").get<std::int64_t>(),
                payload.at("waves").get<int>(), payload.at("checksum").get<std::uint64_t>(),
                payload.at("promoted").get<bool>() ? 1 : 0,
                payload.at("promotion_mean").get<double>(), payload.at("promotion_lb").get<double>());
            std::string order;
            for (auto const &id : payload.at("standings"))
            {
                if (!order.empty())
                {
                    order += " ";
                }
                order += std::to_string(id.get<std::uint64_t>());
            }
            std::println("standings {}", order);
            int rank = 0;
            for (auto const &entry : payload.at("ratings"))
            {
                std::println("  rank {} id {} rating {:.4f} se {:.4f} exprank {:.2f} games {} w {} d {} l {}",
                    ++rank, entry.at("candidate").get<std::uint64_t>(),
                    entry.at("rating").get<double>(), entry.at("std_error").get<double>(),
                    entry.at("expected_rank").get<double>(), entry.at("games").get<std::int64_t>(),
                    entry.at("wins").get<std::int64_t>(), entry.at("draws").get<std::int64_t>(),
                    entry.at("losses").get<std::int64_t>());
            }
        }
        catch (std::exception const &error)
        {
            std::println(stderr, "checkpoint payload is malformed: {}", error.what());
            return 1;
        }
        return 0;
    }

    std::string bracket_id_name(tournament_bracket::CandidateId id)
    {
        if (id == tournament_bracket::kNoCandidate)
        {
            return "-";
        }
        return std::to_string(id);
    }

    void print_bracket_group(tournament_bracket::Bracket const &bracket,
                             tournament_bracket::NodeKind kind, char const *title)
    {
        int last_round = -1;
        for (int id = 0; id < bracket.series_count(); ++id)
        {
            auto view = bracket.series(id);
            if (view.kind != kind || view.status == tournament_bracket::SeriesStatus::Void)
            {
                continue;
            }
            if (view.round != last_round)
            {
                std::println("{} round {}", title, view.round);
                last_round = view.round;
            }
            if (view.status == tournament_bracket::SeriesStatus::Complete)
            {
                std::println("  S{} {} v {} {}-{} -> {}",
                    view.id, bracket_id_name(view.side_a), bracket_id_name(view.side_b),
                    view.sets_a, view.sets_b, bracket_id_name(view.winner));
            }
            else if (view.status == tournament_bracket::SeriesStatus::Walkover)
            {
                std::println("  S{} bye -> {}", view.id, bracket_id_name(view.winner));
            }
            else if (view.status == tournament_bracket::SeriesStatus::Ready)
            {
                std::println("  S{} {} v {} sets {}-{} FT{} games {}-{} LIVE",
                    view.id, bracket_id_name(view.side_a), bracket_id_name(view.side_b),
                    view.sets_a, view.sets_b, view.format.first_to, view.games_a, view.games_b);
            }
            else if (view.status == tournament_bracket::SeriesStatus::Dormant)
            {
                std::println("  S{} dormant", view.id);
            }
            else
            {
                std::println("  S{} {} v {} waiting",
                    view.id, bracket_id_name(view.side_a), bracket_id_name(view.side_b));
            }
        }
    }

    template<class RunnerT>
    void print_bracket(RunnerT const &runner)
    {
        auto const &bracket = runner.bracket();
        print_bracket_group(bracket, tournament_bracket::NodeKind::Winners, "WINNERS");
        print_bracket_group(bracket, tournament_bracket::NodeKind::Losers, "LOSERS");
        print_bracket_group(bracket, tournament_bracket::NodeKind::GrandFinal, "GRAND FINAL");
        print_bracket_group(bracket, tournament_bracket::NodeKind::GrandFinalReset, "GRAND FINAL RESET");
        if (bracket.complete())
        {
            std::println("Champion: {}", bracket_id_name(bracket.champion()));
        }
        else
        {
            std::println("Champion: undecided");
        }
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
        check(thread_budget_for(0) >= 1, "thread budget auto is at least one");
        check(thread_budget_for(4) == 4, "thread budget respects explicit threads");
        check(game_workers_for(15) == 7, "game workers split the budget with helper threads");
        check(game_workers_for(1) == 1, "game workers clamp to at least one");
        check(roster_size_for(15) == 30, "roster follows the pso candidate count of twice the budget");
        check(roster_size_for(1) == 2, "roster clamps to at least two");
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
        LocalRunner runner(backend, roster_entries, 987654321ULL, run_config, limits);
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
        int const budget = thread_budget_for(cli.threads);
        int const game_workers = game_workers_for(budget);
        int roster_size = roster_size_for(budget);
        char const *roster_source = "pso 2x";
        if (cli.remote_port > 0 && cli.roster == 0)
        {
            roster_size = 16;
            roster_source = "remote default";
        }
        if (cli.roster > 0)
        {
            roster_size = cli.roster;
            roster_source = "configured";
        }
        int const lambda = lambda_for(roster_size);
        int const dimension = static_cast<int>(tuning_toj::TojAdapter::param_count());
        tuning::RunConfig run_config;
        run_config.threads = game_workers;
        std::println("thread budget {} game workers {} helpers {} roster {} ({}) lambda {}",
            budget, game_workers, game_workers, roster_size, roster_source, lambda);
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
        bool resume_progress = false;
        std::vector<tournament_runner::RosterEntry> resume_roster;
        std::vector<tournament_runner::GameRecord> resume_ledger;
        std::int64_t resume_waves = 0;
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
                std::string stage = "complete";
                if (payload.contains("stage"))
                {
                    if (!payload.at("stage").is_string())
                    {
                        std::println(stderr, "checkpoint has an invalid stage");
                        return 1;
                    }
                    stage = payload.at("stage").get<std::string>();
                    if (stage != "complete" && stage != "progress")
                    {
                        std::println(stderr, "checkpoint has an unknown stage");
                        return 1;
                    }
                }
                if (saved_dimension != dimension || saved_lambda != lambda
                    || saved_iters != cli.iters_per_move || saved_rounds != cli.max_rounds)
                {
                    std::println(stderr, "checkpoint configuration mismatch, delete {} for a fresh run", cli.data_file);
                    return 1;
                }
                if (stage == "progress")
                {
                    if (!json_to_roster(payload.at("roster"), resume_roster))
                    {
                        std::println(stderr, "checkpoint has an invalid roster");
                        return 1;
                    }
                    if (!json_to_ledger(payload.at("ledger"), resume_ledger))
                    {
                        std::println(stderr, "checkpoint has an invalid ledger");
                        return 1;
                    }
                    if (!payload.at("waves").is_number_integer()
                        || payload.at("waves").get<std::int64_t>() < 0)
                    {
                        std::println(stderr, "checkpoint has an invalid wave count");
                        return 1;
                    }
                    resume_waves = payload.at("waves").get<std::int64_t>();
                    resume_progress = true;
                }
                root_seed = saved_root;
                start_generation = saved_generation;
                incumbent = saved_incumbent;
                cma_config.mean = saved_mean;
                cma_config.coordinate_scales = saved_scales;
                cma_config.seed = payload.at("cma_seed").get<std::uint64_t>();
                cma_blob = std::move(*blob);
                have_checkpoint = true;
                if (resume_progress)
                {
                    std::println("Resumed generation {} at {} ledger games root_seed {}",
                        start_generation, resume_ledger.size(), root_seed);
                }
                else
                {
                    std::println("Resumed from generation {} root_seed {}", start_generation, root_seed);
                }
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
            if (cli.fresh_zero)
            {
                incumbent.assign(static_cast<std::size_t>(dimension), 0.0);
                cma_config.mean = incumbent;
                if (!write_theta_bin(cli.incumbent_file, incumbent))
                {
                    std::println(stderr, "cannot write {}", cli.incumbent_file);
                    return 1;
                }
                std::println("Fresh incumbent from zero weights (--fresh-zero)");
            }
            else if (read_theta_bin(cli.incumbent_file, file_theta))
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
        else if (cli.fresh_zero)
        {
            std::println("note: a valid checkpoint exists, --fresh-zero only affects runs without one; delete {} for a fresh start", cli.data_file);
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
        std::shared_ptr<tournament_registry::DeviceRegistry> device_registry;
        tournament_ban::BanFile ban_store(cli.ban_file);
#if defined(TUNER_HAS_REMOTE)
        std::shared_ptr<tournament_net::HostTransport> net_transport;
        std::shared_ptr<tournament_remote::DeviceTiming> remote_timing;
        if (cli.remote_port > 0)
        {
            if (!std::filesystem::exists(cli.remote_certificate)
                || !std::filesystem::exists(cli.remote_key))
            {
                std::string cert_error;
                if (!tournament_net::generate_self_signed_host_cert(cli.remote_certificate,
                                                                   cli.remote_key, cert_error))
                {
                    std::println(stderr, "cannot generate host certificate: {}", cert_error);
                    return 1;
                }
                std::println("generated {} and {}", cli.remote_certificate, cli.remote_key);
            }
            std::optional<std::string> const fingerprint
                = tournament_net::certificate_fingerprint(cli.remote_certificate);
            if (!fingerprint.has_value())
            {
                std::println(stderr, "cannot read certificate {}", cli.remote_certificate);
                return 1;
            }
            device_registry = std::make_shared<tournament_registry::DeviceRegistry>();
            std::uint64_t engine_fingerprint = 0;
            {
                TojBackend probe_engine(shared_context);
                auto probe_run = [&probe_engine](std::vector<tuning::BatchGame> const &games,
                                                 tuning::RunConfig const &probe_config)
                {
                    return probe_engine.run_games(games, probe_config);
                };
                engine_fingerprint = tournament_identity::adapter_engine_fingerprint<tuning_toj::TojAdapter>(
                    probe_run);
            }
            tournament_net::NetConfig net_config;
            net_config.listen_address = cli.remote_address;
            net_config.port = static_cast<std::uint16_t>(cli.remote_port);
            net_config.certificate_path = cli.remote_certificate;
            net_config.private_key_path = cli.remote_key;
            net_config.expected_adapter_id = std::string(tuning_toj::TojAdapter::schema().adapter_id);
            net_config.expected_schema_hash = tuning::schema_hash(tuning_toj::TojAdapter::schema());
            net_config.devices_file = cli.devices_file;
            net_config.expected_engine_fingerprint = engine_fingerprint;
            net_config.log = [](std::string const &message)
            {
                std::println("remote: {}", message);
            };
            net_transport = std::make_shared<tournament_net::HostTransport>(device_registry, net_config);
            std::string start_error;
            if (!net_transport->start(start_error))
            {
                std::println(stderr, "cannot start host transport: {}", start_error);
                return 1;
            }
            std::uint16_t const port = net_transport->listening_port();
            std::println("remote: listening on {}:{} fingerprint {}", cli.remote_address,
                         static_cast<int>(port), *fingerprint);
            std::println("remote: engine fingerprint {:016x}", engine_fingerprint);
            remote_timing = std::make_shared<tournament_remote::DeviceTiming>();
            {
                TojBackend calibrate_engine(shared_context);
                tuning::ParamSchema const &schema = tuning_toj::TojAdapter::schema();
                tuning::BatchGame probe;
                probe.id = tournament_runner::game_id_for(1, 0);
                probe.theta_a.assign(schema.defaults.begin(), schema.defaults.end());
                probe.theta_b.assign(schema.defaults.begin(), schema.defaults.end());
                probe.seed_a = tuning::derive_game_seed(0xC0FFEEULL, probe.id, 0);
                probe.seed_b = tuning::derive_game_seed(0xC0FFEEULL, probe.id, 1);
                auto const began = std::chrono::steady_clock::now();
                calibrate_engine.run_games({probe}, run_config);
                auto const elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - began).count();
                remote_timing->seed_ms_per_game(static_cast<double>(elapsed));
                std::println("remote: calibrated {} ms per game, lease {} ms, {} games per assignment",
                             elapsed, cli.lease_ms, cli.games_per_assignment);
            }
            if (!cli.devices_file.empty())
            {
                std::println("remote: enrollment restricted to {}", cli.devices_file);
            }
            if (!cli.ban_file.empty())
            {
                std::vector<tournament_ban::BanRecord> ban_records;
                std::string ban_error;
                if (!ban_store.load(ban_records, ban_error))
                {
                    std::println(stderr, "cannot load ban file: {}", ban_error);
                    return 1;
                }
                for (tournament_ban::BanRecord const &record : ban_records)
                {
                    device_registry->ban_key(record.public_key);
                }
                std::println("remote: {} banned device key(s) loaded from {}",
                             ban_records.size(), cli.ban_file);
            }
            std::println("remote: start clients with remote_client --host <host> --port {} --device-id N --key-file K --fingerprint {}",
                         static_cast<int>(port), *fingerprint);
            for (int waited = 0; device_registry->active_devices().empty();)
            {
                if (waited >= cli.wait_clients_ms)
                {
                    std::println(stderr, "remote: no clients enrolled within {} ms", cli.wait_clients_ms);
                    return 1;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
                waited += 250;
            }
            std::println("remote: {} client(s) enrolled", device_registry->active_devices().size());
        }
#endif
        struct LiveBracket
        {
            std::mutex mutex;
            RuntimeRunner const *runner = nullptr;
        };
        auto view_state = std::make_shared<tuning::EngineViewState>();
        auto live = std::make_shared<LiveBracket>();
        std::thread stdin_thread([view_state, live]()
        {
            std::string line;
            while (std::getline(std::cin, line))
            {
                if (line == "view")
                {
                    view_state->enabled.store(true, std::memory_order_relaxed);
                    std::print("\033[2J");
                    std::println("view enabled, one live game renders from the next round");
                }
                else if (line.empty())
                {
                    view_state->enabled.store(false, std::memory_order_relaxed);
                    std::println("view disabled");
                }
                else if (line == "bracket" || line == "status")
                {
                    std::lock_guard<std::mutex> live_lock(live->mutex);
                    if (live->runner == nullptr)
                    {
                        std::println("no live tournament right now");
                        continue;
                    }
                    std::lock_guard<std::mutex> view_lock(view_state->mutex);
                    print_bracket(*live->runner);
                }
            }
        });
        stdin_thread.detach();
        std::println("Type view and Enter to watch one live game per wave, empty line to stop, bracket for live standings");
        for (std::uint64_t generation = start_generation; generation < static_cast<std::uint64_t>(cli.generations); ++generation)
        {
            std::vector<std::uint8_t> pre_tell = optimizer->save_state();
            std::string pre_tell_hex = tournament_bytes::encode_hex(pre_tell);
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
            std::vector<tournament_runner::GameRecord> prior;
            std::int64_t prior_waves = 0;
            if (resume_progress && generation == start_generation)
            {
                bool roster_ok = resume_roster.size() == roster_entries.size();
                for (std::size_t i = 0; roster_ok && i < roster_entries.size(); ++i)
                {
                    roster_ok = resume_roster[i].id == roster_entries[i].id
                        && resume_roster[i].theta == roster_entries[i].theta;
                }
                if (!roster_ok)
                {
                    std::println(stderr, "checkpoint roster does not match optimizer samples");
                    return 1;
                }
                prior = resume_ledger;
                prior_waves = resume_waves;
                resume_progress = false;
                std::println("gen {} continuing at {} ledger games", generation, prior.size());
            }
            std::shared_ptr<tournament_provenance::ProvenanceLedger> provenance;
            auto local_engine = [shared_context, view_state](std::vector<tuning::BatchGame> const &games,
                                                             tuning::RunConfig const &engine_config)
            {
                TojBackend local(shared_context);
                local.set_view_state(view_state);
                return local.run_games(games, engine_config);
            };
            tournament_runtime::RuntimeBackend backend(tuning_toj::TojAdapter::schema(), local_engine);
            tournament_journal::ProvenanceJournal journal(cli.journal_file);
            std::unordered_set<std::uint64_t> journaled_ids;
#if defined(TUNER_HAS_REMOTE)
            std::optional<tournament_remote::RemoteBackend> remote_backend;
            if (net_transport)
            {
                provenance = std::make_shared<tournament_provenance::ProvenanceLedger>();
                tournament_remote::RemoteConfig remote_config;
                remote_config.games_per_assignment = cli.games_per_assignment;
                remote_config.lease_ms = cli.lease_ms;
                remote_config.per_series_device_cap = cli.series_cap;
                remote_config.nonce_seed = generation_seed_for(root_seed, generation);
                remote_config.timing = remote_timing;
                remote_config.log = [](std::string const &message)
                {
                    std::println("remote: {}", message);
                };
                remote_config.audit_rate = cli.audit_rate;
                remote_config.auditor = [shared_context](std::vector<tuning::BatchGame> const &games,
                                                         tuning::RunConfig const &audit_config)
                {
                    TojBackend local(shared_context);
                    return local.run_games(games, audit_config);
                };
                auto liar_failures
                    = std::make_shared<std::unordered_map<std::uint64_t, std::uint64_t>>();
                remote_config.on_liar
                    = [device_registry, &ban_store, &cli, generation, liar_failures](std::uint64_t device)
                {
                    device_registry->blacklist(device);
                    tournament_wire::PublicKey const *banned_key = device_registry->public_key(device);
                    if (banned_key != nullptr && !cli.ban_file.empty())
                    {
                        device_registry->ban_key(*banned_key);
                        tournament_ban::BanRecord ban_record;
                        ban_record.device = device;
                        ban_record.public_key = *banned_key;
                        ban_record.generation = generation;
                        ban_record.failed_verdicts = ++(*liar_failures)[device];
                        ban_record.caught_at_ms = tournament_transport::SystemClock().now_ms();
                        std::string ban_error;
                        if (!ban_store.append(ban_record, ban_error))
                        {
                            std::println(stderr, "gen {} ban file append failed: {}", generation,
                                         ban_error);
                        }
                    }
                    std::println("remote: device {} caught lying by the streaming audit and banned",
                                 device);
                };
                remote_backend.emplace(tuning_toj::TojAdapter::schema(), net_transport, device_registry,
                                       provenance, std::make_shared<tournament_transport::SystemClock>(),
                                       remote_config);
                backend = tournament_runtime::RuntimeBackend(
                    tuning_toj::TojAdapter::schema(),
                    [engine = *remote_backend](std::vector<tuning::BatchGame> const &games,
                                               tuning::RunConfig const &engine_config)
                    {
                        return engine.run_games(games, engine_config);
                    });
                if (prior.empty())
                {
                    std::string clear_error;
                    if (!journal.clear(clear_error))
                    {
                        std::println(stderr, "gen {} cannot clear journal: {}", generation, clear_error);
                        return 1;
                    }
                }
                else
                {
                    std::string load_error;
                    if (!journal.load(*provenance, load_error))
                    {
                        std::println("gen {} journal note: {}", generation, load_error);
                    }
                    for (auto const *entry : provenance->entries())
                    {
                        journaled_ids.insert(entry->game_id);
                    }
                }
            }
#endif
            std::uint64_t generation_seed = generation_seed_for(root_seed, generation);
            RuntimeRunner runner(backend, roster_entries, generation_seed, run_config, limits, std::move(prior));
            {
                std::lock_guard<std::mutex> live_lock(live->mutex);
                live->runner = &runner;
            }
            if (!runner.ok())
            {
                std::println(stderr, "gen {} ledger replay failed: {}", generation, runner.error().detail);
                return 1;
            }
            tournament_runner::RunResult run_result;
            for (;;)
            {
                tournament_runner::RunResult step = runner.run_next_wave();
                if (step.error.code != tournament_runner::ErrorCode::None)
                {
                    run_result = step;
                    break;
                }
                std::int64_t ledger_games = static_cast<std::int64_t>(runner.ledger().size());
                std::int64_t ledger_draws = 0;
                for (auto const &record : runner.ledger())
                {
                    if (record.winner == tournament_bracket::GameWinner::Draw)
                    {
                        ++ledger_draws;
                    }
                }
                std::int64_t ledger_waves = prior_waves + runner.total_waves();
                std::string active;
                for (int id : runner.bracket().ready_series())
                {
                    auto view = runner.bracket().series(id);
                    if (!active.empty())
                    {
                        active += " ";
                    }
                    active += "S" + std::to_string(id) + " " + std::to_string(view.side_a)
                        + "v" + std::to_string(view.side_b) + " " + std::to_string(view.sets_a)
                        + "-" + std::to_string(view.sets_b) + " " + std::to_string(view.games_a)
                        + "-" + std::to_string(view.games_b);
                }
                std::println("gen {} wave {} games {} draws {} ready {} {}",
                    generation, ledger_waves, ledger_games, ledger_draws,
                    runner.bracket().ready_series().size(), active);
                if (provenance)
                {
                    for (auto const *entry : provenance->entries())
                    {
                        if (journaled_ids.insert(entry->game_id).second)
                        {
                            std::string append_error;
                            if (!journal.append(*entry, append_error))
                            {
                                std::println(stderr, "gen {} journal append failed: {}",
                                             generation, append_error);
                            }
                        }
                    }
                }
                if (!step.complete)
                {
                    std::string save_error;
                    if (!save_progress(cli.data_file, identity, generation, root_seed, cli,
                                       cma_config, pre_tell_hex, incumbent, roster_entries,
                                       runner.ledger(), ledger_waves, save_error))
                    {
                        std::println(stderr, "cannot save wave checkpoint: {}", save_error);
                        return 1;
                    }
                    continue;
                }
                run_result.complete = true;
                run_result.champion = step.champion;
                run_result.stats.games = ledger_games;
                run_result.stats.draws = ledger_draws;
                run_result.stats.waves = ledger_waves;
                break;
            }
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
            auto engine_rerun = [shared_context](std::vector<tuning::BatchGame> const &games,
                                                 tuning::RunConfig const &rerun_config)
            {
                TojBackend local(shared_context);
                return local.run_games(games, rerun_config);
            };
            std::unordered_set<std::uint64_t> audited_ids;
            std::optional<LocalRunner> audited_runner;
            if (provenance && !provenance->empty())
            {
#if defined(TUNER_HAS_REMOTE)
                if (remote_backend)
                {
                    for (std::uint64_t game_id : remote_backend->audited_game_ids())
                    {
                        audited_ids.insert(game_id);
                    }
                }
#endif
                std::vector<std::uint64_t> failed_devices;
                for (auto const *entry : provenance->entries())
                {
                    if (device_registry->blacklisted(entry->device)
                        && std::find(failed_devices.begin(), failed_devices.end(), entry->device)
                            == failed_devices.end())
                    {
                        failed_devices.push_back(entry->device);
                    }
                }
                std::sort(failed_devices.begin(), failed_devices.end());
                std::println("gen {} streaming audit: {} games audited at acceptance, {} device(s) with games to void",
                             generation, audited_ids.size(), failed_devices.size());
                std::vector<tournament_runner::GameRecord> authoritative_ledger = runner.ledger();
                if (!failed_devices.empty())
                {
                    std::vector<std::uint64_t> voided;
                    for (std::uint64_t device : failed_devices)
                    {
                        for (std::uint64_t game : provenance->games_of_device(device))
                        {
                            voided.push_back(game);
                        }
                    }
                    tournament_repair::RepairRequest repair_request;
                    repair_request.roster = roster_entries;
                    repair_request.generation_seed = generation_seed;
                    repair_request.config = run_config;
                    repair_request.ledger = runner.ledger();
                    repair_request.voided_game_ids = voided;
                    repair_request.re_run = engine_rerun;
                    tournament_repair::RepairResult const repaired
                        = tournament_repair::repair_ledger(std::move(repair_request));
                    if (!repaired.ok)
                    {
                        std::println(stderr, "gen {} repair failed: {}", generation, repaired.error);
                        return 1;
                    }
                    std::println("gen {} repair: voided {} re-ran {} dropped {} diverged {}",
                                 generation, repaired.voided_games, repaired.re_run_games,
                                 repaired.dropped_games, repaired.diverged_games);
                    authoritative_ledger = std::move(repaired.repaired_ledger);
                }
                audited_runner.emplace(TojBackend(shared_context), roster_entries, generation_seed,
                                       run_config, limits, std::move(authoritative_ledger));
                if (!audited_runner->ok())
                {
                    std::println(stderr, "gen {} audited ledger replay failed: {}", generation,
                                 audited_runner->error().detail);
                    return 1;
                }
                tournament_runner::RunResult const audited_result = audited_runner->run();
                if (audited_result.error.code != tournament_runner::ErrorCode::None
                    || !audited_result.complete)
                {
                    std::println(stderr, "gen {} audited tournament failed: {}", generation,
                                 audited_result.error.detail.empty()
                                     ? std::string("did not complete")
                                     : audited_result.error.detail);
                    return 1;
                }
                run_result.complete = audited_result.complete;
                run_result.champion = audited_result.champion;
                run_result.stats.games = static_cast<std::int64_t>(audited_runner->ledger().size());
                run_result.stats.draws = audited_runner->total_draws();
                {
                    std::lock_guard<std::mutex> live_lock(live->mutex);
                    live->runner = nullptr;
                }
            }
            std::vector<tournament_runner::GameRecord> const &authoritative_ledger_ref
                = audited_runner.has_value() ? audited_runner->ledger() : runner.ledger();
            std::vector<tournament_rating::GameRecord> rating_records;
            rating_records.reserve(authoritative_ledger_ref.size());
            for (auto const &record : authoritative_ledger_ref)
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
            std::uint64_t const champion_id = audited_runner.has_value() ? audited_runner->champion()
                                                                         : runner.champion();
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
                if (promoted && provenance && !provenance->empty())
                {
                    for (auto const *entry : provenance->entries())
                    {
                        if (journaled_ids.insert(entry->game_id).second)
                        {
                            std::string append_error;
                            if (!journal.append(*entry, append_error))
                            {
                                std::println(stderr, "gen {} journal append failed: {}",
                                             generation, append_error);
                            }
                        }
                    }
                    std::vector<std::uint64_t> forced;
                    for (auto const *entry : provenance->entries())
                    {
                        if (audited_ids.insert(entry->game_id).second)
                        {
                            forced.push_back(entry->game_id);
                        }
                    }
                    std::vector<tournament_audit::AuditTarget> const promotion_targets
                        = tournament_audit::select_targets(*provenance, 0.0, forced, generation_seed);
                    tournament_audit::AuditReport const promotion_report
                        = tournament_audit::audit_records(*provenance, promotion_targets, run_config,
                                                          engine_rerun);
                    std::vector<std::uint64_t> const promotion_failed = promotion_report.failed_devices();
                    if (!promotion_failed.empty())
                    {
                        tournament_transport::SystemClock promotion_ban_clock;
                        for (std::uint64_t device : promotion_failed)
                        {
                            device_registry->blacklist(device);
                            device_registry->record_audit(device, false);
                            tournament_wire::PublicKey const *banned_key
                                = device_registry->public_key(device);
                            if (banned_key != nullptr && !cli.ban_file.empty())
                            {
                                device_registry->ban_key(*banned_key);
                                tournament_ban::BanRecord ban_record;
                                ban_record.device = device;
                                ban_record.public_key = *banned_key;
                                ban_record.generation = generation;
                                ban_record.failed_verdicts = 0;
                                ban_record.caught_at_ms = promotion_ban_clock.now_ms();
                                std::string ban_error;
                                if (!ban_store.append(ban_record, ban_error))
                                {
                                    std::println(stderr, "gen {} ban file append failed: {}",
                                                 generation, ban_error);
                                }
                            }
                        }
                        std::println(stderr,
                                     "gen {} promotion audit failed for {} device(s); promotion rejected",
                                     generation, promotion_failed.size());
                        promoted = false;
                    }
                }
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
            payload["stage"] = "complete";
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
            payload["checksum"] = audited_runner.has_value() ? audited_runner->checksum()
                                                             : runner.checksum();
            payload["promoted"] = promoted;
            payload["promotion_mean"] = promotion_mean;
            payload["promotion_lb"] = promotion_lb;
            nlohmann::json standings = nlohmann::json::array();
            for (auto id : audited_runner.has_value() ? audited_runner->standings() : runner.standings())
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
                champion_id,
                audited_runner.has_value() ? audited_runner->checksum() : runner.checksum(),
                optimizer->sigma(), promoted ? 1 : 0, promotion_mean, promotion_lb);
            std::size_t show = std::min<std::size_t>(5, fit.ratings.size());
            for (std::size_t i = 0; i < show; ++i)
            {
                auto const &entry = fit.ratings[i];
                std::println("  rank {} id {} rating {:.4f} se {:.4f} exprank {:.2f} games {} w {} d {} l {}",
                    i + 1, entry.candidate, entry.rating, entry.std_error, entry.expected_rank,
                    entry.games, entry.wins, entry.draws, entry.losses);
            }
            {
                std::lock_guard<std::mutex> live_lock(live->mutex);
                live->runner = nullptr;
            }
        }
#if defined(TUNER_HAS_REMOTE)
        if (net_transport)
        {
            net_transport->stop();
        }
#endif
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
        if (first == "status")
        {
            std::string config_path = "tournament_tuner.json";
            for (int i = 2; i < argc; ++i)
            {
                if (std::strcmp(argv[i], "--config") == 0 && i + 1 < argc)
                {
                    config_path = argv[++i];
                }
            }
            std::string data_file;
            bool have_data_file = false;
            std::error_code config_probe;
            if (std::filesystem::exists(config_path, config_probe))
            {
                std::string config_error;
                std::optional<tournament_config::ConfigFile> const loaded
                    = tournament_config::load(config_path, config_error);
                if (loaded.has_value())
                {
                    if (tournament_config::get_string(loaded->values, "data_file", data_file,
                                                      config_error))
                    {
                        have_data_file = !data_file.empty();
                    }
                }
            }
            for (int i = 2; i < argc; ++i)
            {
                if (std::strcmp(argv[i], "--config") == 0 && i + 1 < argc)
                {
                    ++i;
                    continue;
                }
                data_file = argv[i];
                have_data_file = true;
                break;
            }
            if (!have_data_file)
            {
                data_file = "tournament_data.bin";
            }
            return tournament_tuner::run_status(data_file);
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
    std::vector<char const *> positional;
    positional.push_back(argv[0]);
    bool flag_error = false;
    std::string config_path = "tournament_tuner.json";
    bool config_explicit = false;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--config") == 0)
        {
            if (i + 1 >= argc)
            {
                std::println(stderr, "missing value for --config");
                tournament_tuner::print_usage(program.c_str());
                return 1;
            }
            config_path = argv[++i];
            config_explicit = true;
        }
    }
    std::error_code config_probe;
    bool const config_present = std::filesystem::exists(config_path, config_probe);
    if (config_present || config_explicit)
    {
        std::string config_error;
        std::optional<tournament_config::ConfigFile> const loaded
            = tournament_config::load(config_path, config_error);
        if (!loaded.has_value()
            || !tournament_tuner::apply_config_file(config, loaded->values, config_error))
        {
            std::println(stderr, "config: {}", config_error);
            return 1;
        }
        std::println("config: loaded {}", config_path);
    }
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--config") == 0)
        {
            if (i + 1 < argc)
            {
                ++i;
            }
            continue;
        }
        if (std::strcmp(argv[i], "--fresh-zero") == 0)
        {
            config.fresh_zero = true;
        }
        else if (std::strcmp(argv[i], "--remote-port") == 0 && i + 1 < argc)
        {
            try
            {
                config.remote_port = std::stoi(argv[++i]);
            }
            catch (std::exception const &)
            {
                flag_error = true;
            }
        }
        else if (std::strcmp(argv[i], "--remote-cert") == 0 && i + 1 < argc)
        {
            config.remote_certificate = argv[++i];
        }
        else if (std::strcmp(argv[i], "--remote-key") == 0 && i + 1 < argc)
        {
            config.remote_key = argv[++i];
        }
        else if (std::strcmp(argv[i], "--devices-file") == 0 && i + 1 < argc)
        {
            config.devices_file = argv[++i];
        }
        else if (std::strcmp(argv[i], "--audit-rate") == 0 && i + 1 < argc)
        {
            try
            {
                config.audit_rate = std::stod(argv[++i]);
            }
            catch (std::exception const &)
            {
                flag_error = true;
            }
        }
        else if (std::strcmp(argv[i], "--journal-file") == 0 && i + 1 < argc)
        {
            config.journal_file = argv[++i];
        }
        else if (std::strcmp(argv[i], "--ban-file") == 0 && i + 1 < argc)
        {
            config.ban_file = argv[++i];
        }
        else if (std::strcmp(argv[i], "--wait-clients-ms") == 0 && i + 1 < argc)
        {
            try
            {
                config.wait_clients_ms = std::stoi(argv[++i]);
            }
            catch (std::exception const &)
            {
                flag_error = true;
            }
        }
        else if (std::strcmp(argv[i], "--lease-ms") == 0 && i + 1 < argc)
        {
            try
            {
                std::uint64_t const value = std::stoull(argv[++i]);
                if (value == 0)
                {
                    flag_error = true;
                }
                else
                {
                    config.lease_ms = value;
                }
            }
            catch (std::exception const &)
            {
                flag_error = true;
            }
        }
        else if (std::strcmp(argv[i], "--games-per-assignment") == 0 && i + 1 < argc)
        {
            try
            {
                int const value = std::stoi(argv[++i]);
                if (value <= 0)
                {
                    flag_error = true;
                }
                else
                {
                    config.games_per_assignment = value;
                }
            }
            catch (std::exception const &)
            {
                flag_error = true;
            }
        }
        else if (std::strcmp(argv[i], "--series-cap") == 0 && i + 1 < argc)
        {
            try
            {
                int const value = std::stoi(argv[++i]);
                if (value <= 0)
                {
                    flag_error = true;
                }
                else
                {
                    config.series_cap = value;
                }
            }
            catch (std::exception const &)
            {
                flag_error = true;
            }
        }
        else
        {
            positional.push_back(argv[i]);
        }
    }
    if (flag_error)
    {
        tournament_tuner::print_usage(program.c_str());
        return 1;
    }
    int const argn = static_cast<int>(positional.size());
    try
    {
        if (argn > 1)
        {
            config.generations = std::stoi(positional[1]);
        }
        if (argn > 2)
        {
            config.iters_per_move = static_cast<std::size_t>(std::stoul(positional[2]));
        }
        if (argn > 3)
        {
            config.root_seed = static_cast<std::uint64_t>(std::stoull(positional[3]));
        }
        if (argn > 4)
        {
            config.threads = std::stoi(positional[4]);
        }
        if (argn > 5)
        {
            config.max_rounds = std::stoi(positional[5]);
        }
        if (argn > 6)
        {
            config.pairs = std::stoi(positional[6]);
        }
        if (argn > 7)
        {
            config.data_file = positional[7];
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
    if (config.remote_port < 0 || config.remote_port > 65535)
    {
        std::println(stderr, "invalid remote port");
        return 1;
    }
    if (!std::isfinite(config.audit_rate) || config.audit_rate < 0.0 || config.audit_rate > 1.0)
    {
        std::println(stderr, "invalid audit rate");
        return 1;
    }
    if (config.wait_clients_ms <= 0)
    {
        std::println(stderr, "invalid client wait");
        return 1;
    }
    if (config.lease_ms == 0)
    {
        std::println(stderr, "invalid lease");
        return 1;
    }
    if (config.games_per_assignment <= 0)
    {
        std::println(stderr, "invalid games per assignment");
        return 1;
    }
    if (config.series_cap <= 0)
    {
        std::println(stderr, "invalid series cap");
        return 1;
    }
    if (config.roster < 0 || config.roster == 1)
    {
        std::println(stderr, "roster must be 0 (automatic) or at least 2");
        return 1;
    }
#if !defined(TUNER_HAS_REMOTE)
    if (config.remote_port > 0)
    {
        std::println(stderr, "remote support requires a build with OpenSSL");
        return 1;
    }
#endif
    return tournament_tuner::run_tournament(config);
}
