#include "tournament/ordinal.h"

#include <limits>
#include <print>
#include <string>
#include <vector>

namespace
{
    int failures = 0;

    void check(bool condition, std::string const &name)
    {
        std::println("{}: {}", condition ? "PASS" : "FAIL", name);
        if (!condition)
        {
            ++failures;
        }
    }

    tournament_rating::FitResult fit()
    {
        tournament_rating::FitResult result;
        result.ok = true;
        tournament_rating::Rating anchor;
        anchor.candidate = 1;
        anchor.rating = 0.5;
        anchor.expected_rank = 1.0;
        tournament_rating::Rating a;
        a.candidate = 10;
        a.rating = 0.2;
        a.expected_rank = 2.5;
        tournament_rating::Rating b;
        b.candidate = 20;
        b.rating = 0.2;
        b.expected_rank = 2.5;
        tournament_rating::Rating c;
        c.candidate = 30;
        c.rating = -0.1;
        c.expected_rank = 4.0;
        result.ratings = {anchor, a, b, c};
        return result;
    }

    void test_alignment_and_ties()
    {
        auto const result = tournament_ordinal::build(fit(), {30, 20, 10});
        check(result.ok, "ordinal build succeeds");
        check(result.order == std::vector<std::uint64_t>{10, 20, 30},
              "ties use candidate id deterministically");
        check(result.fitness == std::vector<double>{2.0, 1.0, 0.0},
              "fitness stays aligned with CMA sample order");
        bool distinct = result.fitness.size() == 3
            && result.fitness[0] != result.fitness[1]
            && result.fitness[0] != result.fitness[2]
            && result.fitness[1] != result.fitness[2];
        check(distinct, "ordinal fitness is always distinct");
    }

    void test_rating_breaks_rank_tie()
    {
        auto data = fit();
        data.ratings[2].rating = 0.3;
        auto const result = tournament_ordinal::build(data, {10, 20, 30});
        check(result.ok && result.order == std::vector<std::uint64_t>{20, 10, 30},
              "rating breaks expected-rank ties before id");
    }

    void test_rejections()
    {
        auto failed = fit();
        failed.ok = false;
        check(!tournament_ordinal::build(failed, {10, 20}).ok,
              "failed rating fit is rejected");
        check(!tournament_ordinal::build(fit(), {10}).ok,
              "single optimizer sample is rejected");
        check(!tournament_ordinal::build(fit(), {10, 10}).ok,
              "duplicate sample ids are rejected");
        check(!tournament_ordinal::build(fit(), {10, 99}).ok,
              "missing sample rating is rejected");
        auto duplicate = fit();
        duplicate.ratings.push_back(duplicate.ratings.back());
        check(!tournament_ordinal::build(duplicate, {10, 20}).ok,
              "duplicate rating entries are rejected");
        auto nonfinite = fit();
        nonfinite.ratings[1].expected_rank = std::numeric_limits<double>::quiet_NaN();
        check(!tournament_ordinal::build(nonfinite, {10, 20}).ok,
              "non-finite rating entries are rejected");
    }
}

int main()
{
    test_alignment_and_ties();
    test_rating_breaks_rank_tie();
    test_rejections();
    std::println("{} ordinal test failure(s)", failures);
    return failures == 0 ? 0 : 1;
}
