#include "tournament_bracket.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <numeric>
#include <print>
#include <string>
#include <vector>

namespace
{
    int checks = 0;
    int failures = 0;

    void check(bool condition, std::string const &name)
    {
        ++checks;
        if (condition)
        {
            std::println("PASS: {}", name);
        }
        else
        {
            ++failures;
            std::println("FAIL: {}", name);
        }
    }

    using Bracket = tournament_bracket::Bracket;
    using SeriesView = tournament_bracket::SeriesView;
    using CandidateId = tournament_bracket::CandidateId;
    using GameWinner = tournament_bracket::GameWinner;
    using OutcomeFn = std::function<GameWinner(SeriesView const &, int)>;

    constexpr int kStageCount = 6;
    constexpr int kKindCount = 4;

    std::vector<CandidateId> iota_seeds(int n)
    {
        std::vector<CandidateId> seeds(static_cast<size_t>(n));
        std::iota(seeds.begin(), seeds.end(), CandidateId{0});
        return seeds;
    }

    int next_power_of_two(int value)
    {
        int p = 1;
        while (p < value)
        {
            p <<= 1;
        }
        return p;
    }

    std::vector<int> balanced_seed_positions(int slots)
    {
        std::vector<int> order{0};
        while (static_cast<int>(order.size()) < slots)
        {
            int const width = static_cast<int>(order.size()) * 2;
            std::vector<int> next;
            next.reserve(static_cast<size_t>(width));
            for (int seed : order)
            {
                next.push_back(seed);
                next.push_back(width - 1 - seed);
            }
            order = std::move(next);
        }
        return order;
    }

    int seed_index(std::vector<CandidateId> const &seeds, CandidateId candidate)
    {
        return static_cast<int>(std::find(seeds.begin(), seeds.end(), candidate) - seeds.begin());
    }

    OutcomeFn seed_bias(std::vector<CandidateId> const &seeds, bool higher_wins)
    {
        return [&seeds, higher_wins](SeriesView const &v, int)
        {
            int const a = seed_index(seeds, v.side_a);
            int const b = seed_index(seeds, v.side_b);
            bool const a_wins = higher_wins ? a < b : a > b;
            return a_wins ? GameWinner::SideA : GameWinner::SideB;
        };
    }

    OutcomeFn hash_outcome(std::uint64_t salt)
    {
        return [salt](SeriesView const &v, int game_index)
        {
            std::uint64_t h = salt ^ 0x9E3779B97F4A7C15ULL;
            auto mix = [&h](std::uint64_t x)
            {
                h ^= x + 0x9E3779B97F4A7C15ULL + (h << 6) + (h >> 2);
            };
            mix(static_cast<std::uint64_t>(v.id));
            mix(static_cast<std::uint64_t>(static_cast<int>(v.kind)));
            mix(v.side_a);
            mix(v.side_b);
            mix(static_cast<std::uint64_t>(game_index));
            if (h % 11 == 0)
            {
                return GameWinner::Draw;
            }
            return (h & 1) == 0 ? GameWinner::SideA : GameWinner::SideB;
        };
    }

    OutcomeFn gf_script(GameWinner grand_final, GameWinner reset, OutcomeFn fallback)
    {
        return [grand_final, reset, fallback](SeriesView const &v, int game_index)
        {
            if (v.kind == tournament_bracket::NodeKind::GrandFinal)
            {
                return grand_final;
            }
            if (v.kind == tournament_bracket::NodeKind::GrandFinalReset)
            {
                return reset;
            }
            return fallback(v, game_index);
        };
    }

    bool drive_forward(Bracket &bracket, OutcomeFn const &outcome, std::string const &ctx)
    {
        long guard = 0;
        while (!bracket.complete())
        {
            std::vector<int> const ready = bracket.ready_series();
            if (ready.empty())
            {
                check(false, ctx + ": stalled without a ready series");
                return false;
            }
            for (int id : ready)
            {
                while (bracket.series(id).status == tournament_bracket::SeriesStatus::Ready)
                {
                    SeriesView const v = bracket.series(id);
                    auto const st = bracket.report_game(id, v.games_played, outcome(v, v.games_played));
                    if (st != tournament_bracket::ReportStatus::Accepted)
                    {
                        check(false, ctx + ": report rejected by the bracket");
                        return false;
                    }
                    if (++guard > 8000000L)
                    {
                        check(false, ctx + ": game guard tripped");
                        return false;
                    }
                }
            }
        }
        return true;
    }

    bool drive_reverse(Bracket &bracket, OutcomeFn const &outcome, std::string const &ctx)
    {
        long guard = 0;
        while (!bracket.complete())
        {
            std::vector<int> const ready = bracket.ready_series();
            if (ready.empty())
            {
                check(false, ctx + ": stalled without a ready series");
                return false;
            }
            for (auto it = ready.rbegin(); it != ready.rend(); ++it)
            {
                SeriesView const v = bracket.series(*it);
                auto const st = bracket.report_game(*it, v.games_played, outcome(v, v.games_played));
                if (st != tournament_bracket::ReportStatus::Accepted)
                {
                    check(false, ctx + ": report rejected by the bracket");
                    return false;
                }
                if (++guard > 8000000L)
                {
                    check(false, ctx + ": game guard tripped");
                    return false;
                }
            }
        }
        return true;
    }

    bool drive_round_robin(Bracket &bracket, OutcomeFn const &outcome, std::string const &ctx)
    {
        long guard = 0;
        size_t cursor = 0;
        while (!bracket.complete())
        {
            std::vector<int> const ready = bracket.ready_series();
            if (ready.empty())
            {
                check(false, ctx + ": stalled without a ready series");
                return false;
            }
            int const id = ready[cursor % ready.size()];
            SeriesView const v = bracket.series(id);
            auto const st = bracket.report_game(id, v.games_played, outcome(v, v.games_played));
            if (st != tournament_bracket::ReportStatus::Accepted)
            {
                check(false, ctx + ": report rejected by the bracket");
                return false;
            }
            ++cursor;
            if (++guard > 8000000L)
            {
                check(false, ctx + ": game guard tripped");
                return false;
            }
        }
        return true;
    }

    void check_final(Bracket const &bracket, std::vector<CandidateId> const &seeds, std::string const &ctx)
    {
        check(bracket.complete(), ctx + ": tournament complete");
        CandidateId const champ = bracket.champion();
        check(champ != tournament_bracket::kNoCandidate
                  && std::find(seeds.begin(), seeds.end(), champ) != seeds.end(),
              ctx + ": champion is an entrant");

        std::vector<CandidateId> const order = bracket.standings();
        check(order.size() == seeds.size(), ctx + ": standings cover every entrant");
        std::vector<CandidateId> sorted_order = order;
        std::vector<CandidateId> sorted_seeds = seeds;
        std::sort(sorted_order.begin(), sorted_order.end());
        std::sort(sorted_seeds.begin(), sorted_seeds.end());
        check(sorted_order == sorted_seeds, ctx + ": standings are a permutation of entrants");
        check(!order.empty() && order.front() == champ, ctx + ": champion stands first");
        check(bracket.create_status() == tournament_bracket::CreateStatus::Ok, ctx + ": creation status ok");

        std::string loss_problem;
        long long total_losses = 0;
        for (CandidateId c : seeds)
        {
            int const l = bracket.losses(c);
            total_losses += l;
            if (c == champ)
            {
                if (l > 1)
                {
                    loss_problem = "champion has " + std::to_string(l) + " losses";
                }
            }
            else if (l != 2)
            {
                loss_problem = "candidate " + std::to_string(c) + " has " + std::to_string(l) + " losses";
            }
        }
        check(loss_problem.empty(), ctx + ": exactly two losses per nonchampion" + (loss_problem.empty() ? std::string() : " (" + loss_problem + ")"));

        int complete_series = 0;
        int dormant_count = 0;
        int dormant_id = -1;
        bool unresolved = false;
        bool shape_problem = false;
        for (int id = 0; id < bracket.series_count(); ++id)
        {
            SeriesView const v = bracket.series(id);
            switch (v.status)
            {
            case tournament_bracket::SeriesStatus::Complete:
                ++complete_series;
                shape_problem = shape_problem || v.winner == tournament_bracket::kNoCandidate
                    || v.loser == tournament_bracket::kNoCandidate;
                break;
            case tournament_bracket::SeriesStatus::Pending:
            case tournament_bracket::SeriesStatus::Ready:
                unresolved = true;
                break;
            case tournament_bracket::SeriesStatus::Walkover:
                shape_problem = shape_problem || v.winner == tournament_bracket::kNoCandidate
                    || v.loser != tournament_bracket::kNoCandidate || v.games_played != 0
                    || v.side_a != tournament_bracket::kNoCandidate
                    || v.side_b != tournament_bracket::kNoCandidate;
                break;
            case tournament_bracket::SeriesStatus::Void:
                shape_problem = shape_problem || v.winner != tournament_bracket::kNoCandidate
                    || v.loser != tournament_bracket::kNoCandidate || v.games_played != 0;
                break;
            case tournament_bracket::SeriesStatus::Dormant:
                ++dormant_count;
                dormant_id = v.id;
                break;
            }
        }
        check(!unresolved, ctx + ": no unresolved series remain");
        check(!shape_problem, ctx + ": terminal series shapes are consistent");
        check(dormant_count == 0 || (dormant_count == 1 && dormant_id == bracket.series_count() - 1),
              ctx + ": only the reset series may rest dormant");
        check(static_cast<long long>(complete_series) == total_losses,
              ctx + ": one loss awarded per completed series");
    }

    void expected_stage_counts(int slots, int &early, int &top8, int &wf, int &lf)
    {
        early = 0;
        top8 = 0;
        wf = 0;
        lf = 0;
        if (slots == 2)
        {
            wf = 1;
        }
        else if (slots == 4)
        {
            top8 = 3;
            wf = 1;
            lf = 1;
        }
        else if (slots == 8)
        {
            early = 4;
            top8 = 7;
            wf = 1;
            lf = 1;
        }
        else if (slots == 16)
        {
            early = 20;
            top8 = 7;
            wf = 1;
            lf = 1;
        }
        else if (slots == 32)
        {
            early = 52;
            top8 = 7;
            wf = 1;
            lf = 1;
        }
    }

    int expected_void_series(int entrants)
    {
        int const slots = next_power_of_two(entrants);
        if (slots < 8)
        {
            return 0;
        }
        std::vector<int> const positions = balanced_seed_positions(slots);
        auto bye_present = [&](int match)
        {
            return positions[static_cast<size_t>(2 * match)] >= entrants
                || positions[static_cast<size_t>(2 * match + 1)] >= entrants;
        };
        int voids = 0;
        for (int m = 0; m < slots / 4; ++m)
        {
            if (bye_present(m) && bye_present(slots / 2 - 1 - m))
            {
                ++voids;
            }
        }
        return voids;
    }

    void check_structure(int n)
    {
        std::vector<CandidateId> const seeds = iota_seeds(n);
        Bracket const bracket = Bracket::create(seeds);
        std::string const ctx = "structure n=" + std::to_string(n);
        check(bracket.valid(), ctx + ": valid");
        if (!bracket.valid())
        {
            return;
        }
        int const slots = bracket.slot_count();
        check(slots == next_power_of_two(n), ctx + ": slots are the next power of two");
        check(bracket.entrant_count() == n, ctx + ": entrant count");
        check(bracket.bye_count() == slots - n, ctx + ": bye count");
        check(bracket.series_count() == 2 * slots - 1, ctx + ": series node count");

        std::vector<int> stage_counts(static_cast<size_t>(kStageCount), 0);
        std::vector<int> kind_counts(static_cast<size_t>(kKindCount), 0);
        bool formats_ok = true;
        bool ids_ok = true;
        for (int id = 0; id < bracket.series_count(); ++id)
        {
            SeriesView const v = bracket.series(id);
            ids_ok = ids_ok && v.id == id;
            stage_counts[static_cast<size_t>(static_cast<int>(v.stage))] += 1;
            kind_counts[static_cast<size_t>(static_cast<int>(v.kind))] += 1;
            tournament_bracket::SeriesFormat expected{1, 7};
            switch (v.stage)
            {
            case tournament_bracket::Stage::Early:
                expected = {1, 7};
                break;
            case tournament_bracket::Stage::Top8:
                expected = {1, 11};
                break;
            case tournament_bracket::Stage::WinnersFinal:
            case tournament_bracket::Stage::LosersFinal:
                expected = {2, 11};
                break;
            case tournament_bracket::Stage::GrandFinal:
            case tournament_bracket::Stage::GrandFinalReset:
                expected = {3, 11};
                break;
            }
            formats_ok = formats_ok && v.format.sets_to_win == expected.sets_to_win
                && v.format.first_to == expected.first_to;
        }
        check(ids_ok, ctx + ": series ids match their index");
        check(formats_ok, ctx + ": stage formats are FT7, FT11, BO3 FT11, BO5 FT11");

        int early = 0;
        int top8 = 0;
        int wf = 0;
        int lf = 0;
        expected_stage_counts(slots, early, top8, wf, lf);
        check(stage_counts[0] == early && stage_counts[1] == top8 && stage_counts[2] == wf
                  && stage_counts[3] == lf && stage_counts[4] == 1 && stage_counts[5] == 1,
              ctx + ": stage counts");

        check(kind_counts[0] == slots - 1, ctx + ": winners node count");
        check(kind_counts[1] == (slots >= 4 ? slots - 2 : 0), ctx + ": losers node count");
        check(kind_counts[2] == 1 && kind_counts[3] == 1, ctx + ": grand final and reset nodes");

        std::vector<int> const ready = bracket.ready_series();
        bool ready_sorted = std::is_sorted(ready.begin(), ready.end());
        check(ready_sorted, ctx + ": ready list is sorted");
    }

    void check_byes(int n)
    {
        std::vector<CandidateId> const seeds = iota_seeds(n);
        Bracket const bracket = Bracket::create(seeds);
        std::string const ctx = "byes n=" + std::to_string(n);
        std::vector<CandidateId> walkover_winners;
        int void_count = 0;
        for (int id = 0; id < bracket.series_count(); ++id)
        {
            SeriesView const v = bracket.series(id);
            if (v.status == tournament_bracket::SeriesStatus::Walkover)
            {
                walkover_winners.push_back(v.winner);
            }
            if (v.status == tournament_bracket::SeriesStatus::Void)
            {
                ++void_count;
            }
        }
        std::sort(walkover_winners.begin(), walkover_winners.end());
        std::vector<CandidateId> expected(seeds.begin(), seeds.begin() + bracket.bye_count());
        std::sort(expected.begin(), expected.end());
        check(walkover_winners == expected, ctx + ": bye receivers advanced without playing");
        check(void_count == expected_void_series(n), ctx + ": void lower bracket nodes");
    }

    void check_initial_ready(int n, int expected_ready)
    {
        Bracket const bracket = Bracket::create(iota_seeds(n));
        std::vector<int> const ready = bracket.ready_series();
        check(static_cast<int>(ready.size()) == expected_ready,
              "initial ready n=" + std::to_string(n) + " expected " + std::to_string(expected_ready));
    }

    void check_construction_identity(int n)
    {
        std::vector<CandidateId> const seeds = iota_seeds(n);
        Bracket const a = Bracket::create(seeds);
        Bracket const b = Bracket::create(seeds);
        bool same = a.series_count() == b.series_count();
        for (int id = 0; id < a.series_count() && same; ++id)
        {
            SeriesView const va = a.series(id);
            SeriesView const vb = b.series(id);
            same = va.status == vb.status && va.kind == vb.kind && va.stage == vb.stage
                && va.side_a == vb.side_a && va.side_b == vb.side_b && va.winner == vb.winner
                && va.loser == vb.loser;
        }
        check(same, "construction identity n=" + std::to_string(n));
    }

    void run_seed_bias_sim(int n, bool higher_wins)
    {
        std::vector<CandidateId> const seeds = iota_seeds(n);
        Bracket bracket = Bracket::create(seeds);
        std::string const ctx = (higher_wins ? "sim higher seed n=" : "sim lower seed n=") + std::to_string(n);
        if (!drive_forward(bracket, seed_bias(seeds, higher_wins), ctx))
        {
            return;
        }
        check_final(bracket, seeds, ctx);
        CandidateId const expected = higher_wins ? seeds.front() : seeds.back();
        check(bracket.champion() == expected, ctx + ": dominant seed is champion");
        check(bracket.series(bracket.series_count() - 1).status == tournament_bracket::SeriesStatus::Dormant,
              ctx + ": no reset was triggered");
    }

    void run_hash_sim(int n)
    {
        std::vector<CandidateId> const seeds = iota_seeds(n);
        Bracket bracket = Bracket::create(seeds);
        std::string const ctx = "sim hash n=" + std::to_string(n);
        auto const base = hash_outcome(7);
        int draws = 0;
        OutcomeFn counting = [&base, &draws](SeriesView const &v, int game_index)
        {
            GameWinner const w = base(v, game_index);
            if (w == GameWinner::Draw)
            {
                ++draws;
            }
            return w;
        };
        if (!drive_forward(bracket, counting, ctx))
        {
            return;
        }
        check_final(bracket, seeds, ctx);
        if (n >= 8)
        {
            check(draws > 0, ctx + ": scripted draws were exercised");
        }
    }

    void run_gf_path(int n, GameWinner gf_side, GameWinner reset_side, std::string const &label)
    {
        std::vector<CandidateId> const seeds = iota_seeds(n);
        Bracket bracket = Bracket::create(seeds);
        std::string const ctx = "gf path n=" + std::to_string(n) + " " + label;
        if (!drive_forward(bracket, gf_script(gf_side, reset_side, seed_bias(seeds, true)), ctx))
        {
            return;
        }
        check(bracket.complete(), ctx + ": tournament complete");
        int const gf_id = bracket.series_count() - 2;
        int const reset_id = bracket.series_count() - 1;
        SeriesView const gf = bracket.series(gf_id);
        SeriesView const reset = bracket.series(reset_id);
        check(gf.side_a != tournament_bracket::kNoCandidate && gf.side_b != tournament_bracket::kNoCandidate,
              ctx + ": grand final seating known");
        if (gf_side == GameWinner::SideA)
        {
            check(reset.status == tournament_bracket::SeriesStatus::Dormant, ctx + ": no reset series");
            check(bracket.champion() == gf.side_a, ctx + ": winners finalist is champion");
            check(bracket.losses(gf.side_a) == 0, ctx + ": champion is undefeated");
            check(bracket.losses(gf.side_b) == 2, ctx + ": losers finalist has two losses");
            std::vector<CandidateId> const order = bracket.standings();
            check(order.size() >= 2 && order[0] == gf.side_a && order[1] == gf.side_b,
                  ctx + ": top two standings order");
        }
        else if (reset_side == GameWinner::SideB)
        {
            check(reset.status == tournament_bracket::SeriesStatus::Complete, ctx + ": reset series played");
            check(bracket.champion() == gf.side_b, ctx + ": losers finalist wins the reset");
            check(bracket.losses(gf.side_b) == 1, ctx + ": champion carries one loss");
            check(bracket.losses(gf.side_a) == 2, ctx + ": winners finalist has two losses");
            std::vector<CandidateId> const order = bracket.standings();
            check(order.size() >= 2 && order[0] == gf.side_b && order[1] == gf.side_a,
                  ctx + ": top two standings order");
        }
        else
        {
            check(reset.status == tournament_bracket::SeriesStatus::Complete, ctx + ": reset series played");
            check(bracket.champion() == gf.side_a, ctx + ": winners finalist wins the reset");
            check(bracket.losses(gf.side_a) == 1, ctx + ": champion carries one loss");
            check(bracket.losses(gf.side_b) == 2, ctx + ": losers finalist has two losses");
            std::vector<CandidateId> const order = bracket.standings();
            check(order.size() >= 2 && order[0] == gf.side_a && order[1] == gf.side_b,
                  ctx + ": top two standings order");
        }
    }

    void check_top8_boundary(int n)
    {
        std::vector<CandidateId> const seeds = iota_seeds(n);
        Bracket bracket = Bracket::create(seeds);
        std::string const ctx = "top8 boundary n=" + std::to_string(n);
        auto const outcome = seed_bias(seeds, true);
        long guard = 0;
        for (;;)
        {
            bool played = false;
            bool early_pending = false;
            for (int id = 0; id < bracket.series_count(); ++id)
            {
                SeriesView v = bracket.series(id);
                if (v.stage != tournament_bracket::Stage::Early)
                {
                    continue;
                }
                if (v.status == tournament_bracket::SeriesStatus::Ready)
                {
                    while (bracket.series(id).status == tournament_bracket::SeriesStatus::Ready)
                    {
                        v = bracket.series(id);
                        bracket.report_game(id, v.games_played, outcome(v, v.games_played));
                        if (++guard > 8000000L)
                        {
                            check(false, ctx + ": game guard tripped");
                            return;
                        }
                    }
                    played = true;
                }
                else if (v.status == tournament_bracket::SeriesStatus::Pending)
                {
                    early_pending = true;
                }
            }
            if (!played)
            {
                check(!early_pending, ctx + ": early series all resolved");
                break;
            }
        }
        check(!bracket.complete(), ctx + ": tournament continues past the boundary");
        std::vector<CandidateId> alive;
        for (CandidateId c : seeds)
        {
            if (bracket.losses(c) < 2)
            {
                alive.push_back(c);
            }
        }
        check(static_cast<int>(alive.size()) == 8, ctx + ": exactly eight candidates remain");
        std::vector<int> const ready = bracket.ready_series();
        check(!ready.empty(), ctx + ": top8 series are ready");
        check(std::is_sorted(ready.begin(), ready.end()), ctx + ": ready list is sorted");
        bool all_top8 = true;
        std::vector<CandidateId> upper;
        std::vector<CandidateId> lower;
        for (int id : ready)
        {
            SeriesView const v = bracket.series(id);
            all_top8 = all_top8 && v.stage == tournament_bracket::Stage::Top8;
            std::vector<CandidateId> &target = v.kind == tournament_bracket::NodeKind::Winners ? upper : lower;
            target.push_back(v.side_a);
            target.push_back(v.side_b);
        }
        check(all_top8, ctx + ": every ready series is a top8 series");
        check(static_cast<int>(upper.size()) == 4 && static_cast<int>(lower.size()) == 4,
              ctx + ": four upper and four lower pairings ready");
        std::sort(upper.begin(), upper.end());
        std::sort(lower.begin(), lower.end());
        bool const distinct = std::adjacent_find(upper.begin(), upper.end()) == upper.end()
            && std::adjacent_find(lower.begin(), lower.end()) == lower.end()
            && upper.front() != lower.front();
        std::vector<CandidateId> merged = upper;
        merged.insert(merged.end(), lower.begin(), lower.end());
        std::sort(merged.begin(), merged.end());
        check(distinct && std::adjacent_find(merged.begin(), merged.end()) == merged.end()
                  && merged == alive,
              ctx + ": the remaining eight are split four and four");
    }

    void check_order_independence(int n)
    {
        std::vector<CandidateId> const seeds = iota_seeds(n);
        std::string const ctx = "order independence n=" + std::to_string(n);
        auto const outcome = hash_outcome(7);
        Bracket forward = Bracket::create(seeds);
        Bracket reverse = Bracket::create(seeds);
        Bracket roundrobin = Bracket::create(seeds);
        if (!drive_forward(forward, outcome, ctx))
        {
            return;
        }
        if (!drive_reverse(reverse, outcome, ctx))
        {
            return;
        }
        if (!drive_round_robin(roundrobin, outcome, ctx))
        {
            return;
        }
        bool same = forward.champion() == reverse.champion()
            && reverse.champion() == roundrobin.champion()
            && forward.standings() == reverse.standings()
            && reverse.standings() == roundrobin.standings();
        for (int id = 0; id < forward.series_count() && same; ++id)
        {
            SeriesView const va = forward.series(id);
            SeriesView const vb = reverse.series(id);
            SeriesView const vc = roundrobin.series(id);
            same = va.status == vb.status && vb.status == vc.status
                && va.winner == vb.winner && vb.winner == vc.winner
                && va.loser == vb.loser && vb.loser == vc.loser
                && va.games_played == vb.games_played && vb.games_played == vc.games_played
                && va.sets_a == vb.sets_a && vb.sets_a == vc.sets_a
                && va.sets_b == vb.sets_b && vb.sets_b == vc.sets_b;
        }
        check(same, ctx + ": identical bracket state across scheduling orders");
        check(forward.replay_entries() == reverse.replay_entries()
                  && reverse.replay_entries() == roundrobin.replay_entries(),
              ctx + ": identical replay log across scheduling orders");
    }

    void check_invalid_inputs()
    {
        using tournament_bracket::CreateStatus;
        Bracket const empty = Bracket::create({});
        check(!empty.valid() && empty.series_count() == 0 && !empty.complete()
                  && empty.create_status() == CreateStatus::EmptyRoster,
              "invalid input: empty roster rejected");

        Bracket const single = Bracket::create({42});
        check(!single.valid() && single.create_status() == CreateStatus::SingleEntrant,
              "invalid input: single entrant rejected");

        Bracket const duplicate = Bracket::create({7, 7, 8});
        check(!duplicate.valid() && duplicate.create_status() == CreateStatus::DuplicateEntrant,
              "invalid input: duplicate entrants rejected");

        Bracket const reserved = Bracket::create(std::vector<CandidateId>{1, tournament_bracket::kNoCandidate});
        check(!reserved.valid() && reserved.series_count() == 0 && !reserved.complete()
                  && reserved.create_status() == CreateStatus::ReservedEntrant,
              "invalid input: reserved candidate id rejected as an entrant");

        Bracket const reserved_only = Bracket::create(std::vector<CandidateId>{tournament_bracket::kNoCandidate, tournament_bracket::kNoCandidate});
        check(!reserved_only.valid() && reserved_only.create_status() == CreateStatus::ReservedEntrant,
              "invalid input: a roster of only reserved ids is rejected");

        std::vector<CandidateId> oversized(static_cast<size_t>(tournament_bracket::kMaxEntrants) + 1);
        std::iota(oversized.begin(), oversized.end(), CandidateId{0});
        Bracket const too_many = Bracket::create(oversized);
        check(!too_many.valid() && too_many.series_count() == 0
                  && too_many.create_status() == CreateStatus::TooManyEntrants,
              "invalid input: roster beyond the documented cap rejected");

        Bracket const accepted = Bracket::create(std::vector<CandidateId>{3, 4});
        check(accepted.valid() && accepted.create_status() == CreateStatus::Ok,
              "invalid input: accepted roster reports ok");
    }

    void check_invalid_winner_rejection()
    {
        using tournament_bracket::ReportStatus;
        Bracket bracket = Bracket::create(std::vector<CandidateId>{10, 20});
        std::string const ctx = "winner validation";
        auto const garbage_high = static_cast<GameWinner>(77);
        auto const garbage_negative = static_cast<GameWinner>(-1);
        auto const garbage_gap = static_cast<GameWinner>(3);
        check(bracket.report_game(0, 0, garbage_high) == ReportStatus::InvalidWinner,
              ctx + ": casted value above the domain rejected");
        check(bracket.report_game(0, 0, garbage_negative) == ReportStatus::InvalidWinner,
              ctx + ": negative casted value rejected");
        check(bracket.report_game(0, 0, garbage_gap) == ReportStatus::InvalidWinner,
              ctx + ": unused enum slot rejected");
        SeriesView const untouched = bracket.series(0);
        check(untouched.status == tournament_bracket::SeriesStatus::Ready && untouched.games_played == 0
                  && untouched.games_a == 0 && untouched.games_b == 0,
              ctx + ": rejected winner left no trace");
        check(bracket.report_game(0, 0, GameWinner::Draw) == ReportStatus::Accepted,
              ctx + ": valid draw accepted after rejections");
        check(bracket.report_game(0, 1, garbage_high) == ReportStatus::InvalidWinner,
              ctx + ": rejection on a later index");
        check(bracket.series(0).games_played == 1, ctx + ": accepted game count unchanged by rejections");
        check(bracket.report_game(99, 0, garbage_high) == ReportStatus::UnknownSeries,
              ctx + ": unknown series outranks winner validation");
        check(bracket.report_game(0, 1, GameWinner::Draw) == ReportStatus::Accepted,
              ctx + ": draw at the next index accepted after rejections");
        check(bracket.report_game(0, 2, GameWinner::Draw) == ReportStatus::Accepted,
              ctx + ": draw at the following index accepted");
    }

    void check_replay_roundtrip(int n, std::uint64_t salt, bool use_hash)
    {
        using tournament_bracket::ReportStatus;
        std::vector<CandidateId> const seeds = iota_seeds(n);
        Bracket bracket = Bracket::create(seeds);
        std::string const ctx = "replay n=" + std::to_string(n);
        OutcomeFn outcome = seed_bias(seeds, true);
        if (use_hash)
        {
            outcome = hash_outcome(salt);
        }
        if (!drive_forward(bracket, outcome, ctx))
        {
            return;
        }
        std::vector<tournament_bracket::ReplayEntry> const entries = bracket.replay_entries();
        bool const sorted = std::is_sorted(entries.begin(), entries.end(),
                                           [](tournament_bracket::ReplayEntry const &a, tournament_bracket::ReplayEntry const &b)
                                           {
                                               return a.series_id != b.series_id ? a.series_id < b.series_id
                                                                                 : a.game_index < b.game_index;
                                           });
        check(sorted, ctx + ": replay log is in canonical order");
        long long played = 0;
        for (int id = 0; id < bracket.series_count(); ++id)
        {
            played += bracket.series(id).games_played;
        }
        check(static_cast<long long>(entries.size()) == played, ctx + ": replay log covers every game");
        std::vector<std::uint8_t> const bytes = tournament_bracket::encode_replay_log(entries);
        std::optional<std::vector<tournament_bracket::ReplayEntry>> const decoded = tournament_bracket::decode_replay_log(bytes);
        check(decoded.has_value() && *decoded == entries, ctx + ": replay log round trips through bytes");
        if (!decoded.has_value())
        {
            return;
        }
        Bracket reborn = Bracket::create(seeds);
        check(tournament_bracket::apply_replay(reborn, *decoded) == ReportStatus::Accepted,
              ctx + ": replay applies cleanly");
        bool same = reborn.complete() && reborn.champion() == bracket.champion()
            && reborn.standings() == bracket.standings();
        for (int id = 0; id < bracket.series_count() && same; ++id)
        {
            SeriesView const a = bracket.series(id);
            SeriesView const b = reborn.series(id);
            same = a.status == b.status && a.side_a == b.side_a && a.side_b == b.side_b
                && a.winner == b.winner && a.loser == b.loser
                && a.games_played == b.games_played && a.sets_a == b.sets_a && a.sets_b == b.sets_b;
        }
        check(same, ctx + ": replayed bracket matches the original");
    }

    void check_replay_decode_validation()
    {
        auto const empty_log = tournament_bracket::decode_replay_log({});
        check(empty_log.has_value() && empty_log->empty(), "replay decode: empty log is valid");
        check(!tournament_bracket::decode_replay_log({0x00}).has_value(),
              "replay decode: truncated entry rejected");
        check(!tournament_bracket::decode_replay_log({0x00, 0x00, 0x03}).has_value(),
              "replay decode: unknown winner byte rejected");
        check(!tournament_bracket::decode_replay_log({0xFF}).has_value(),
              "replay decode: dangling continuation byte rejected");
        check(!tournament_bracket::decode_replay_log({0x00, 0x00}).has_value(),
              "replay decode: missing winner byte rejected");
    }

    void check_replay_foreign_application()
    {
        std::vector<CandidateId> const eight = iota_seeds(8);
        Bracket source = Bracket::create(eight);
        if (!drive_forward(source, hash_outcome(11), "replay foreign"))
        {
            return;
        }
        std::vector<tournament_bracket::ReplayEntry> const entries = source.replay_entries();
        Bracket two = Bracket::create(std::vector<CandidateId>{1, 2});
        check(tournament_bracket::apply_replay(two, entries) != tournament_bracket::ReportStatus::Accepted,
              "replay apply: foreign log is rejected by a mismatched bracket");
    }

    void check_report_semantics()
    {
        Bracket bracket = Bracket::create(std::vector<CandidateId>{10, 20});
        std::string const ctx = "report";
        check(bracket.report_game(99, 0, GameWinner::SideA) == tournament_bracket::ReportStatus::UnknownSeries,
              ctx + ": unknown series rejected");
        check(bracket.report_game(0, 1, GameWinner::SideA) == tournament_bracket::ReportStatus::GameIndexMismatch,
              ctx + ": skipped game index rejected");
        check(bracket.report_game(0, 0, GameWinner::SideA) == tournament_bracket::ReportStatus::Accepted,
              ctx + ": first game accepted");
        check(bracket.report_game(0, 0, GameWinner::SideB) == tournament_bracket::ReportStatus::GameIndexMismatch,
              ctx + ": duplicate game index rejected");
        check(bracket.report_game(0, 5, GameWinner::SideB) == tournament_bracket::ReportStatus::GameIndexMismatch,
              ctx + ": game index gap rejected");

        Bracket three = Bracket::create(iota_seeds(3));
        int walkover_id = -1;
        for (int id = 0; id < three.series_count(); ++id)
        {
            if (three.series(id).status == tournament_bracket::SeriesStatus::Walkover)
            {
                walkover_id = id;
            }
        }
        check(walkover_id >= 0, ctx + ": walkover exists for a three entrant bracket");
        check(three.report_game(walkover_id, 0, GameWinner::SideA) == tournament_bracket::ReportStatus::NotReady,
              ctx + ": walkover series is not playable");
    }

    void check_draw_handling()
    {
        Bracket bracket = Bracket::create(std::vector<CandidateId>{10, 20});
        std::string const ctx = "draws";
        auto rep = [&](int gi, GameWinner w, std::string const &name)
        {
            check(bracket.report_game(0, gi, w) == tournament_bracket::ReportStatus::Accepted,
                  ctx + ": " + name);
        };
        rep(0, GameWinner::Draw, "draw one");
        rep(1, GameWinner::Draw, "draw two");
        SeriesView v = bracket.series(0);
        check(v.status == tournament_bracket::SeriesStatus::Ready && v.games_played == 2
                  && v.games_a == 0 && v.games_b == 0,
              ctx + ": draws award no game wins");
        for (int gi = 2; gi < 12; ++gi)
        {
            rep(gi, GameWinner::SideA, "set one climb");
        }
        v = bracket.series(0);
        check(v.games_a == 10 && v.games_b == 0 && v.games_played == 12, ctx + ": set one at ten");
        rep(12, GameWinner::Draw, "match point draw");
        v = bracket.series(0);
        check(v.games_a == 10 && v.status == tournament_bracket::SeriesStatus::Ready,
              ctx + ": match point draw does not close the set");
        rep(13, GameWinner::SideA, "set one closer");
        v = bracket.series(0);
        check(v.sets_a == 1 && v.sets_b == 0 && v.games_played == 14 && v.games_a == 0 && v.games_b == 0,
              ctx + ": set one closed at eleven game wins");
        for (int gi = 14; gi < 25; ++gi)
        {
            rep(gi, GameWinner::SideB, "set two climb");
        }
        v = bracket.series(0);
        check(v.sets_a == 1 && v.sets_b == 1 && v.games_played == 25, ctx + ": set two closed");
        for (int gi = 25; gi < 36; ++gi)
        {
            rep(gi, GameWinner::SideA, "set three climb");
        }
        v = bracket.series(0);
        check(v.status == tournament_bracket::SeriesStatus::Complete && v.winner == 10
                  && v.loser == 20 && v.sets_a == 2 && v.sets_b == 1,
              ctx + ": winners final decided");
        check(bracket.series(1).status == tournament_bracket::SeriesStatus::Ready,
              ctx + ": grand final ready");
        check(bracket.series(1).side_a == 10 && bracket.series(1).side_b == 20,
              ctx + ": grand final seating is deterministic");
        check(bracket.series(2).status == tournament_bracket::SeriesStatus::Dormant,
              ctx + ": reset dormant before the grand final");
        while (bracket.series(1).status == tournament_bracket::SeriesStatus::Ready)
        {
            SeriesView const g = bracket.series(1);
            check(bracket.report_game(1, g.games_played, GameWinner::SideA) == tournament_bracket::ReportStatus::Accepted,
                  ctx + ": grand final game");
        }
        check(bracket.complete() && bracket.champion() == 10, ctx + ": champion decided without reset");
        check(bracket.losses(10) == 0 && bracket.losses(20) == 2, ctx + ": loss totals after the final");
        check(bracket.series(2).status == tournament_bracket::SeriesStatus::Dormant,
              ctx + ": reset never activated");
    }
}

int main()
{
    check_invalid_inputs();
    check_invalid_winner_rejection();
    check_report_semantics();
    check_draw_handling();
    check_replay_decode_validation();
    check_replay_foreign_application();
    check_replay_roundtrip(2, 0, false);
    for (int n : {8, 30})
    {
        check_replay_roundtrip(n, 7, true);
    }
    for (int n : {2, 3, 8, 30, 31, 32})
    {
        check_structure(n);
        check_byes(n);
        check_construction_identity(n);
        run_seed_bias_sim(n, true);
        run_seed_bias_sim(n, false);
        run_hash_sim(n);
    }
    check_initial_ready(2, 1);
    check_initial_ready(3, 1);
    check_initial_ready(5, 2);
    check_initial_ready(8, 4);
    check_initial_ready(30, 14);
    check_initial_ready(31, 15);
    check_initial_ready(32, 16);
    check_byes(5);
    {
        std::vector<CandidateId> const seeds = iota_seeds(5);
        Bracket bracket = Bracket::create(seeds);
        std::string const ctx = "sim higher seed n=5";
        if (drive_forward(bracket, seed_bias(seeds, true), ctx))
        {
            check_final(bracket, seeds, ctx);
            check(bracket.champion() == seeds.front(), ctx + ": dominant seed is champion");
        }
    }
    for (int n : {2, 8})
    {
        run_gf_path(n, GameWinner::SideA, GameWinner::SideA, "winners finalist sweeps");
        run_gf_path(n, GameWinner::SideB, GameWinner::SideB, "losers finalist wins reset");
        run_gf_path(n, GameWinner::SideB, GameWinner::SideA, "winners finalist wins reset");
    }
    for (int n : {8, 30, 32})
    {
        check_top8_boundary(n);
    }
    for (int n : {8, 32})
    {
        check_order_independence(n);
    }

    std::println("");
    if (failures == 0)
    {
        std::println("ALL {} CHECKS PASSED", checks);
        return 0;
    }
    std::println("{} OF {} CHECKS FAILED", failures, checks);
    return 1;
}
