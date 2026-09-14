#include "tournament_rating.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <map>
#include <numeric>
#include <utility>

namespace tournament_rating
{
    namespace
    {
        double logistic(double x)
        {
            if (x >= 0.0)
            {
                return 1.0 / (1.0 + std::exp(-x));
            }
            double const e = std::exp(x);
            return e / (1.0 + e);
        }

        double log_one_plus_exp(double x)
        {
            if (x > 0.0)
            {
                return x + std::log1p(std::exp(-x));
            }
            return std::log1p(std::exp(x));
        }

        std::uint64_t splitmix64_next(std::uint64_t &state)
        {
            state += 0x9E3779B97F4A7C15ULL;
            std::uint64_t z = state;
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
            return z ^ (z >> 31);
        }

        bool valid_score(double score)
        {
            return score == 0.0 || score == 0.5 || score == 1.0;
        }

        std::vector<CandidateId> sorted_unique_ids(std::vector<GameRecord> const &records)
        {
            std::vector<CandidateId> ids;
            ids.reserve(2 * records.size());
            for (GameRecord const &record : records)
            {
                ids.push_back(record.candidate_a);
                ids.push_back(record.candidate_b);
            }
            std::sort(ids.begin(), ids.end());
            ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
            return ids;
        }

        struct PairAggregates
        {
            std::vector<CandidateId> ids;
            std::int64_t total_games = 0;
            std::vector<std::int64_t> games;
            std::vector<std::int64_t> wins;
            std::vector<std::int64_t> draws;
            std::vector<std::int64_t> losses;
            std::vector<int> pair_lo;
            std::vector<int> pair_hi;
            std::vector<std::int64_t> pair_games;
            std::vector<std::int64_t> pair_wins_lo;
            std::vector<std::int64_t> pair_draws;
        };

        struct CanonicalGame
        {
            int lo = 0;
            int hi = 0;
            int code = 0;
        };

        bool canonical_before(CanonicalGame const &a, CanonicalGame const &b)
        {
            if (a.lo != b.lo)
            {
                return a.lo < b.lo;
            }
            if (a.hi != b.hi)
            {
                return a.hi < b.hi;
            }
            return a.code < b.code;
        }

        struct BuiltData
        {
            bool ok = false;
            std::string error;
            PairAggregates aggregates;
            std::vector<std::vector<CandidateId>> components;
        };

        BuiltData build_data(std::vector<GameRecord> const &records)
        {
            BuiltData out;
            if (records.empty())
            {
                out.error = "no game records provided";
                return out;
            }
            for (size_t i = 0; i < records.size(); ++i)
            {
                GameRecord const &record = records[i];
                if (record.candidate_a == record.candidate_b)
                {
                    out.error = "record " + std::to_string(i) + " has candidate "
                        + std::to_string(record.candidate_a) + " on both sides";
                    return out;
                }
                if (!valid_score(record.score_for_a))
                {
                    out.error = "record " + std::to_string(i) + " has score_for_a "
                        + std::to_string(record.score_for_a) + " which is not 0, 0.5, or 1";
                    return out;
                }
            }
            out.aggregates.ids = sorted_unique_ids(records);
            int const n = static_cast<int>(out.aggregates.ids.size());
            std::map<CandidateId, int> dense;
            for (int i = 0; i < n; ++i)
            {
                dense.emplace(out.aggregates.ids[static_cast<size_t>(i)], i);
            }
            std::vector<CanonicalGame> games;
            games.reserve(records.size());
            for (GameRecord const &record : records)
            {
                int const a = dense.at(record.candidate_a);
                int const b = dense.at(record.candidate_b);
                CanonicalGame game;
                if (a < b)
                {
                    game.lo = a;
                    game.hi = b;
                    game.code = record.score_for_a == 1.0 ? 2 : (record.score_for_a == 0.5 ? 1 : 0);
                }
                else
                {
                    game.lo = b;
                    game.hi = a;
                    game.code = record.score_for_a == 0.0 ? 2 : (record.score_for_a == 0.5 ? 1 : 0);
                }
                games.push_back(game);
            }
            std::sort(games.begin(), games.end(), canonical_before);
            PairAggregates &agg = out.aggregates;
            agg.games.assign(static_cast<size_t>(n), 0);
            agg.wins.assign(static_cast<size_t>(n), 0);
            agg.draws.assign(static_cast<size_t>(n), 0);
            agg.losses.assign(static_cast<size_t>(n), 0);
            size_t i = 0;
            while (i < games.size())
            {
                size_t j = i;
                std::int64_t wins_lo = 0;
                std::int64_t draw_count = 0;
                while (j < games.size() && games[j].lo == games[i].lo && games[j].hi == games[i].hi)
                {
                    if (games[j].code == 2)
                    {
                        ++wins_lo;
                    }
                    else if (games[j].code == 1)
                    {
                        ++draw_count;
                    }
                    ++j;
                }
                std::int64_t const pair_total = static_cast<std::int64_t>(j - i);
                std::int64_t const wins_hi = pair_total - wins_lo - draw_count;
                int const lo = games[i].lo;
                int const hi = games[i].hi;
                agg.pair_lo.push_back(lo);
                agg.pair_hi.push_back(hi);
                agg.pair_games.push_back(pair_total);
                agg.pair_wins_lo.push_back(wins_lo);
                agg.pair_draws.push_back(draw_count);
                agg.total_games += pair_total;
                agg.games[static_cast<size_t>(lo)] += pair_total;
                agg.games[static_cast<size_t>(hi)] += pair_total;
                agg.wins[static_cast<size_t>(lo)] += wins_lo;
                agg.draws[static_cast<size_t>(lo)] += draw_count;
                agg.losses[static_cast<size_t>(lo)] += wins_hi;
                agg.wins[static_cast<size_t>(hi)] += wins_hi;
                agg.draws[static_cast<size_t>(hi)] += draw_count;
                agg.losses[static_cast<size_t>(hi)] += wins_lo;
                i = j;
            }
            std::vector<std::vector<int>> adjacency(static_cast<size_t>(n));
            for (size_t p = 0; p < agg.pair_lo.size(); ++p)
            {
                adjacency[static_cast<size_t>(agg.pair_lo[p])].push_back(agg.pair_hi[p]);
                adjacency[static_cast<size_t>(agg.pair_hi[p])].push_back(agg.pair_lo[p]);
            }
            std::vector<bool> visited(static_cast<size_t>(n), false);
            std::vector<int> stack;
            for (int start = 0; start < n; ++start)
            {
                if (visited[static_cast<size_t>(start)])
                {
                    continue;
                }
                std::vector<CandidateId> component;
                visited[static_cast<size_t>(start)] = true;
                stack.push_back(start);
                while (!stack.empty())
                {
                    int const node = stack.back();
                    stack.pop_back();
                    component.push_back(out.aggregates.ids[static_cast<size_t>(node)]);
                    for (int neighbor : adjacency[static_cast<size_t>(node)])
                    {
                        if (!visited[static_cast<size_t>(neighbor)])
                        {
                            visited[static_cast<size_t>(neighbor)] = true;
                            stack.push_back(neighbor);
                        }
                    }
                }
                std::sort(component.begin(), component.end());
                out.components.push_back(std::move(component));
            }
            out.ok = true;
            return out;
        }

        bool options_valid(Options const &options, std::string &error)
        {
            if (!std::isfinite(options.l2_lambda) || options.l2_lambda <= 0.0)
            {
                error = "l2_lambda must be finite and positive";
                return false;
            }
            if (!std::isfinite(options.gradient_tolerance) || options.gradient_tolerance <= 0.0)
            {
                error = "gradient_tolerance must be finite and positive";
                return false;
            }
            if (options.max_newton_iterations < 1)
            {
                error = "max_newton_iterations must be at least 1";
                return false;
            }
            if (!std::isfinite(options.cg_relative_tolerance) || options.cg_relative_tolerance <= 0.0
                || options.cg_relative_tolerance >= 1.0)
            {
                error = "cg_relative_tolerance must be finite and inside (0, 1)";
                return false;
            }
            if (options.cg_max_iterations < 1)
            {
                error = "cg_max_iterations must be at least 1";
                return false;
            }
            return true;
        }

        struct Problem
        {
            int n = 0;
            double lambda = 0.0;
            std::vector<int> lo;
            std::vector<int> hi;
            std::vector<double> pair_games;
            std::vector<double> pair_score_lo;
            std::vector<double> pair_weight;
        };

        Problem make_problem(PairAggregates const &agg, double lambda)
        {
            Problem problem;
            problem.n = static_cast<int>(agg.ids.size());
            problem.lambda = lambda;
            problem.lo = agg.pair_lo;
            problem.hi = agg.pair_hi;
            problem.pair_games.resize(agg.pair_games.size());
            problem.pair_score_lo.resize(agg.pair_games.size());
            for (size_t k = 0; k < agg.pair_games.size(); ++k)
            {
                problem.pair_games[k] = static_cast<double>(agg.pair_games[k]);
                problem.pair_score_lo[k] = static_cast<double>(agg.pair_wins_lo[k])
                    + 0.5 * static_cast<double>(agg.pair_draws[k]);
            }
            problem.pair_weight.assign(agg.pair_games.size(), 0.0);
            return problem;
        }

        double data_log_likelihood(Problem const &problem, std::vector<double> const &r)
        {
            double total = 0.0;
            for (size_t k = 0; k < problem.lo.size(); ++k)
            {
                double const x = r[static_cast<size_t>(problem.lo[k])]
                    - r[static_cast<size_t>(problem.hi[k])];
                total += problem.pair_score_lo[k] * x
                    - problem.pair_games[k] * log_one_plus_exp(x);
            }
            return total;
        }

        double ridge_penalty(Problem const &problem, std::vector<double> const &r)
        {
            double total = 0.0;
            for (double value : r)
            {
                total += value * value;
            }
            return 0.5 * problem.lambda * total;
        }

        double objective(Problem const &problem, std::vector<double> const &r)
        {
            return data_log_likelihood(problem, r) - ridge_penalty(problem, r);
        }

        void update_weights(Problem &problem, std::vector<double> const &r)
        {
            for (size_t k = 0; k < problem.lo.size(); ++k)
            {
                double const x = r[static_cast<size_t>(problem.lo[k])]
                    - r[static_cast<size_t>(problem.hi[k])];
                double const p = logistic(x);
                problem.pair_weight[k] = problem.pair_games[k] * p * (1.0 - p);
            }
        }

        void apply_operator(Problem const &problem, std::vector<double> const &x,
                            std::vector<double> &out)
        {
            out.assign(static_cast<size_t>(problem.n), 0.0);
            for (int i = 0; i < problem.n; ++i)
            {
                out[static_cast<size_t>(i)] = problem.lambda * x[static_cast<size_t>(i)];
            }
            for (size_t k = 0; k < problem.lo.size(); ++k)
            {
                double const d = problem.pair_weight[k]
                    * (x[static_cast<size_t>(problem.lo[k])] - x[static_cast<size_t>(problem.hi[k])]);
                out[static_cast<size_t>(problem.lo[k])] += d;
                out[static_cast<size_t>(problem.hi[k])] -= d;
            }
        }

        bool cg_solve(Problem const &problem, std::vector<double> const &b, std::vector<double> &x,
                      Options const &options)
        {
            size_t const n = static_cast<size_t>(problem.n);
            x.assign(n, 0.0);
            std::vector<double> diag(n, problem.lambda);
            for (size_t k = 0; k < problem.lo.size(); ++k)
            {
                diag[static_cast<size_t>(problem.lo[k])] += problem.pair_weight[k];
                diag[static_cast<size_t>(problem.hi[k])] += problem.pair_weight[k];
            }
            double const b_norm = std::sqrt(std::inner_product(b.begin(), b.end(), b.begin(), 0.0));
            if (b_norm == 0.0)
            {
                return true;
            }
            std::vector<double> r = b;
            std::vector<double> z(n);
            for (size_t i = 0; i < n; ++i)
            {
                z[i] = r[i] / diag[i];
            }
            std::vector<double> direction = z;
            std::vector<double> product;
            double rz = std::inner_product(r.begin(), r.end(), z.begin(), 0.0);
            for (int iter = 0; iter < options.cg_max_iterations; ++iter)
            {
                apply_operator(problem, direction, product);
                double dz = 0.0;
                for (size_t i = 0; i < n; ++i)
                {
                    dz += direction[i] * product[i];
                }
                if (!(dz > 0.0))
                {
                    return false;
                }
                double const alpha = rz / dz;
                for (size_t i = 0; i < n; ++i)
                {
                    x[i] += alpha * direction[i];
                    r[i] -= alpha * product[i];
                }
                double const r_norm = std::sqrt(std::inner_product(r.begin(), r.end(), r.begin(), 0.0));
                if (r_norm <= options.cg_relative_tolerance * b_norm)
                {
                    return true;
                }
                for (size_t i = 0; i < n; ++i)
                {
                    z[i] = r[i] / diag[i];
                }
                double const rz_new = std::inner_product(r.begin(), r.end(), z.begin(), 0.0);
                double const beta = rz_new / rz;
                for (size_t i = 0; i < n; ++i)
                {
                    direction[i] = z[i] + beta * direction[i];
                }
                rz = rz_new;
            }
            double const r_norm = std::sqrt(std::inner_product(r.begin(), r.end(), r.begin(), 0.0));
            return r_norm <= options.cg_relative_tolerance * b_norm;
        }

        void data_gradient(Problem const &problem, std::vector<double> const &r,
                           std::vector<double> &grad)
        {
            grad.assign(static_cast<size_t>(problem.n), 0.0);
            for (size_t k = 0; k < problem.lo.size(); ++k)
            {
                double const x = r[static_cast<size_t>(problem.lo[k])]
                    - r[static_cast<size_t>(problem.hi[k])];
                double const g = problem.pair_score_lo[k] - problem.pair_games[k] * logistic(x);
                grad[static_cast<size_t>(problem.lo[k])] += g;
                grad[static_cast<size_t>(problem.hi[k])] -= g;
            }
        }

        struct NewtonFit
        {
            bool ok = false;
            std::string error;
            std::vector<double> ratings;
            int iterations = 0;
            double max_abs_gradient = 0.0;
        };

        NewtonFit newton_fit(Problem &problem, Options const &options)
        {
            NewtonFit out;
            size_t const n = static_cast<size_t>(problem.n);
            std::vector<double> r(n, 0.0);
            std::vector<double> grad;
            std::vector<double> delta;
            std::vector<double> trial(n);
            auto full_gradient = [&]()
            {
                data_gradient(problem, r, grad);
                for (size_t i = 0; i < n; ++i)
                {
                    grad[i] -= problem.lambda * r[i];
                }
                double max_grad = 0.0;
                for (double g : grad)
                {
                    max_grad = std::max(max_grad, std::fabs(g));
                }
                return max_grad;
            };
            for (int iter = 0; iter < options.max_newton_iterations; ++iter)
            {
                double const max_grad = full_gradient();
                if (max_grad < options.gradient_tolerance)
                {
                    out.ok = true;
                    out.ratings = r;
                    out.iterations = iter;
                    out.max_abs_gradient = max_grad;
                    return out;
                }
                update_weights(problem, r);
                if (!cg_solve(problem, grad, delta, options))
                {
                    out.error = "conjugate gradient solve failed at newton iteration "
                        + std::to_string(iter);
                    return out;
                }
                double const dir = std::inner_product(grad.begin(), grad.end(), delta.begin(), 0.0);
                if (!(dir > 0.0))
                {
                    out.error = "newton step is not an ascent direction at newton iteration "
                        + std::to_string(iter);
                    return out;
                }
                double const f_current = objective(problem, r);
                double t = 1.0;
                bool accepted = false;
                for (int backtrack = 0; backtrack < 60; ++backtrack)
                {
                    for (size_t i = 0; i < n; ++i)
                    {
                        trial[i] = r[i] + t * delta[i];
                    }
                    if (objective(problem, trial) >= f_current + 1e-4 * t * dir)
                    {
                        accepted = true;
                        break;
                    }
                    t *= 0.5;
                }
                if (!accepted)
                {
                    out.error = "line search failed at newton iteration " + std::to_string(iter);
                    return out;
                }
                r = trial;
            }
            double const max_grad = full_gradient();
            if (max_grad < options.gradient_tolerance)
            {
                out.ok = true;
                out.ratings = r;
                out.iterations = options.max_newton_iterations;
                out.max_abs_gradient = max_grad;
                return out;
            }
            out.error = "did not converge within " + std::to_string(options.max_newton_iterations)
                + " newton iterations (max |gradient| = " + std::to_string(max_grad) + ")";
            return out;
        }

        bool laplace_std_errors(Problem &problem, std::vector<CandidateId> const &ids,
                                std::vector<double> const &r, Options const &options,
                                std::vector<double> &errors, std::string &error)
        {
            update_weights(problem, r);
            size_t const n = static_cast<size_t>(problem.n);
            double const common_mode = 1.0 / (problem.lambda * static_cast<double>(n));
            errors.assign(n, 0.0);
            std::vector<double> unit(n, 0.0);
            std::vector<double> solution;
            for (size_t i = 0; i < n; ++i)
            {
                std::fill(unit.begin(), unit.end(), 0.0);
                unit[i] = 1.0;
                if (!cg_solve(problem, unit, solution, options) || solution.size() != n)
                {
                    error = "standard error solve for candidate " + std::to_string(ids[i])
                        + " did not converge";
                    return false;
                }
                double const centered_variance = solution[i] - common_mode;
                if (!std::isfinite(centered_variance) || centered_variance <= 0.0)
                {
                    error = "standard error solve for candidate " + std::to_string(ids[i])
                        + " produced a non-finite or non-positive centered variance";
                    return false;
                }
                errors[i] = std::sqrt(centered_variance);
            }
            return true;
        }

        std::vector<double> dense_average_ranks(std::vector<double> const &values)
        {
            size_t const n = values.size();
            std::vector<size_t> order(n);
            std::iota(order.begin(), order.end(), size_t{0});
            std::sort(order.begin(), order.end(), [&values](size_t a, size_t b)
            {
                if (values[a] != values[b])
                {
                    return values[a] > values[b];
                }
                return a < b;
            });
            std::vector<double> ranks(n, 0.0);
            size_t i = 0;
            while (i < n)
            {
                size_t j = i;
                while (j < n && values[order[j]] == values[order[i]])
                {
                    ++j;
                }
                double const average = 0.5 * static_cast<double>(i + j + 1);
                for (size_t k = i; k < j; ++k)
                {
                    ranks[order[k]] = average;
                }
                i = j;
            }
            return ranks;
        }

        double percentile_of_sorted(std::vector<double> const &sorted, double q)
        {
            if (sorted.empty())
            {
                return std::numeric_limits<double>::quiet_NaN();
            }
            if (sorted.size() == 1)
            {
                return sorted[0];
            }
            double const position = q * static_cast<double>(sorted.size() - 1);
            size_t const lo = static_cast<size_t>(position);
            size_t const hi = std::min(lo + 1, sorted.size() - 1);
            double const frac = position - static_cast<double>(lo);
            return sorted[lo] * (1.0 - frac) + sorted[hi] * frac;
        }

        FitResult assemble(PairAggregates const &agg, NewtonFit const &solution, Options const &options)
        {
            FitResult out;
            Problem problem = make_problem(agg, options.l2_lambda);
            std::vector<double> errors;
            std::string error;
            if (!laplace_std_errors(problem, agg.ids, solution.ratings, options, errors, error))
            {
                out.error = error;
                return out;
            }
            out.ok = true;
            size_t const n = agg.ids.size();
            double const mean = std::accumulate(solution.ratings.begin(), solution.ratings.end(), 0.0)
                / static_cast<double>(n);
            std::vector<Rating> entries(n);
            for (size_t i = 0; i < n; ++i)
            {
                Rating &entry = entries[i];
                entry.candidate = agg.ids[i];
                entry.rating = solution.ratings[i] - mean;
                entry.std_error = errors[i];
                entry.rating_lower = entry.rating;
                entry.rating_upper = entry.rating;
                entry.games = agg.games[i];
                entry.wins = agg.wins[i];
                entry.draws = agg.draws[i];
                entry.losses = agg.losses[i];
            }
            std::sort(entries.begin(), entries.end(), [](Rating const &a, Rating const &b)
            {
                if (a.rating != b.rating)
                {
                    return a.rating > b.rating;
                }
                return a.candidate < b.candidate;
            });
            std::vector<double> values(n);
            for (size_t i = 0; i < n; ++i)
            {
                values[i] = entries[i].rating;
            }
            std::vector<double> const ranks = dense_average_ranks(values);
            for (size_t i = 0; i < n; ++i)
            {
                entries[i].expected_rank = ranks[i];
            }
            out.ratings = std::move(entries);
            out.diagnostics.candidates = static_cast<std::int64_t>(n);
            out.diagnostics.games = agg.total_games;
            out.diagnostics.pairs = static_cast<std::int64_t>(agg.pair_lo.size());
            out.diagnostics.log_likelihood = data_log_likelihood(problem, solution.ratings);
            out.diagnostics.ridge_penalty = ridge_penalty(problem, solution.ratings);
            out.diagnostics.newton_iterations = solution.iterations;
            out.diagnostics.max_abs_gradient = solution.max_abs_gradient;
            return out;
        }

        bool bootstrap_options_valid(BootstrapOptions const &bootstrap, std::string &error)
        {
            if (bootstrap.samples < 1)
            {
                error = "bootstrap samples must be at least 1";
                return false;
            }
            if (bootstrap.max_attempts < bootstrap.samples)
            {
                error = "bootstrap max_attempts must be at least the requested samples";
                return false;
            }
            if (!std::isfinite(bootstrap.lower_probability)
                || !std::isfinite(bootstrap.upper_probability)
                || bootstrap.lower_probability < 0.0 || bootstrap.upper_probability > 1.0
                || !(bootstrap.lower_probability < bootstrap.upper_probability))
            {
                error = "bootstrap probabilities must satisfy 0 <= lower < upper <= 1";
                return false;
            }
            return true;
        }
    }

    FitResult fit(std::vector<GameRecord> const &records, Options const &options)
    {
        FitResult out;
        std::string error;
        if (!options_valid(options, error))
        {
            out.error = error;
            return out;
        }
        BuiltData built = build_data(records);
        if (!built.ok)
        {
            out.error = built.error;
            out.components = std::move(built.components);
            return out;
        }
        if (built.components.size() > 1)
        {
            std::string sizes;
            for (size_t i = 0; i < built.components.size(); ++i)
            {
                if (!sizes.empty())
                {
                    sizes += ", ";
                }
                sizes += std::to_string(built.components[i].size());
            }
            out.error = "candidate graph is disconnected: "
                + std::to_string(built.components.size()) + " components with sizes " + sizes;
            out.components = std::move(built.components);
            return out;
        }
        Problem problem = make_problem(built.aggregates, options.l2_lambda);
        NewtonFit solution = newton_fit(problem, options);
        if (!solution.ok)
        {
            out.error = solution.error;
            return out;
        }
        return assemble(built.aggregates, solution, options);
    }

    FitResult fit_with_bootstrap(std::vector<GameRecord> const &records, Options const &options,
                                 BootstrapOptions const &bootstrap)
    {
        std::string error;
        if (!bootstrap_options_valid(bootstrap, error))
        {
            FitResult out;
            out.error = error;
            return out;
        }
        FitResult out = fit(records, options);
        if (!out.ok)
        {
            return out;
        }
        std::map<std::uint64_t, std::vector<size_t>> block_map;
        for (size_t i = 0; i < records.size(); ++i)
        {
            block_map[records[i].correlation_block].push_back(i);
        }
        std::vector<std::vector<size_t>> blocks;
        blocks.reserve(block_map.size());
        for (auto const &entry : block_map)
        {
            blocks.push_back(entry.second);
        }
        std::vector<CandidateId> const base_ids = sorted_unique_ids(records);
        size_t const n = base_ids.size();
        std::vector<size_t> entry_of_dense(n, 0);
        for (size_t k = 0; k < out.ratings.size(); ++k)
        {
            auto const pos = std::lower_bound(base_ids.begin(), base_ids.end(),
                                              out.ratings[k].candidate);
            entry_of_dense[static_cast<size_t>(pos - base_ids.begin())] = k;
        }
        std::vector<std::vector<double>> rating_samples(n);
        std::vector<std::vector<double>> rank_samples(n);
        std::uint64_t state = bootstrap.seed;
        int attempted = 0;
        int valid_samples = 0;
        while (valid_samples < bootstrap.samples && attempted < bootstrap.max_attempts)
        {
            ++attempted;
            std::vector<GameRecord> resample;
            resample.reserve(records.size());
            for (size_t k = 0; k < blocks.size(); ++k)
            {
                size_t const pick = static_cast<size_t>(splitmix64_next(state) % blocks.size());
                for (size_t index : blocks[pick])
                {
                    resample.push_back(records[index]);
                }
            }
            BuiltData built = build_data(resample);
            if (!built.ok || built.components.size() != 1 || built.aggregates.ids != base_ids)
            {
                continue;
            }
            Problem problem = make_problem(built.aggregates, options.l2_lambda);
            NewtonFit solution = newton_fit(problem, options);
            if (!solution.ok)
            {
                continue;
            }
            double const mean = std::accumulate(solution.ratings.begin(), solution.ratings.end(), 0.0)
                / static_cast<double>(n);
            std::vector<double> centered(n);
            for (size_t i = 0; i < n; ++i)
            {
                centered[i] = solution.ratings[i] - mean;
            }
            std::vector<double> const ranks = dense_average_ranks(centered);
            for (size_t i = 0; i < n; ++i)
            {
                rating_samples[i].push_back(centered[i]);
                rank_samples[i].push_back(ranks[i]);
            }
            ++valid_samples;
        }
        out.diagnostics.bootstrap_requested = bootstrap.samples;
        out.diagnostics.bootstrap_attempted = attempted;
        out.diagnostics.bootstrap_valid = valid_samples;
        if (valid_samples < bootstrap.samples)
        {
            FitResult failed;
            failed.error = "bootstrap collected " + std::to_string(valid_samples) + " of "
                + std::to_string(bootstrap.samples) + " requested resamples after "
                + std::to_string(attempted) + " attempts";
            failed.diagnostics = out.diagnostics;
            return failed;
        }
        for (size_t i = 0; i < n; ++i)
        {
            Rating &entry = out.ratings[entry_of_dense[i]];
            std::vector<double> sorted_samples = rating_samples[i];
            std::sort(sorted_samples.begin(), sorted_samples.end());
            entry.rating_lower = percentile_of_sorted(sorted_samples, bootstrap.lower_probability);
            entry.rating_upper = percentile_of_sorted(sorted_samples, bootstrap.upper_probability);
            double const rank_mean = std::accumulate(rank_samples[i].begin(), rank_samples[i].end(), 0.0)
                / static_cast<double>(valid_samples);
            double rank_square_sum = 0.0;
            for (double rank : rank_samples[i])
            {
                rank_square_sum += rank * rank;
            }
            double const rank_variance = std::max(0.0,
                rank_square_sum / static_cast<double>(valid_samples) - rank_mean * rank_mean);
            entry.expected_rank = rank_mean;
            entry.rank_stddev = std::sqrt(rank_variance);
        }
        return out;
    }

    std::vector<CandidateId> optimizer_order(FitResult const &result)
    {
        std::vector<CandidateId> order;
        if (!result.ok)
        {
            return order;
        }
        order.reserve(result.ratings.size());
        for (Rating const &entry : result.ratings)
        {
            order.push_back(entry.candidate);
        }
        return order;
    }
}
