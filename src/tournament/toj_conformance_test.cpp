#include <cstddef>
#include <cstdint>
#include <memory>
#include <print>
#include <string>
#include <vector>

#include "tournament/toj_conformance.h"

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

    void test_fingerprint_determinism()
    {
        auto const context_a = tuning_toj::TojAdapter::make_shared_context();
        auto const context_b = tuning_toj::TojAdapter::make_shared_context();
        if (!context_a || !context_b)
        {
            check(false, "conformance: contexts prepare");
            return;
        }
        std::uint64_t const first = tournament_identity::toj_conformance_fingerprint(context_a);
        std::uint64_t const second = tournament_identity::toj_conformance_fingerprint(context_b);
        std::uint64_t const repeat = tournament_identity::toj_conformance_fingerprint(context_a);
        check(first != 0, "conformance: fingerprint is nonzero");
        check(first == second, "conformance: independent contexts agree");
        check(first == repeat, "conformance: repeated computation is stable");
    }

    void test_eval_fingerprint_sensitivity()
    {
        auto const context = tuning_toj::TojAdapter::make_shared_context();
        if (!context)
        {
            check(false, "conformance: context prepares for sensitivity test");
            return;
        }
        tuning::ParamSchema const schema = tuning_toj::TojAdapter::schema();
        std::vector<double> const theta_a(schema.defaults.begin(), schema.defaults.end());
        std::vector<double> theta_b(theta_a.size());
        for (std::size_t i = 0; i < theta_a.size(); ++i)
        {
            theta_b[i] = theta_a[i] * 1.001;
        }
        std::uint64_t const hash_a = tournament_identity::detail::toj_eval_fingerprint(context, theta_a);
        std::uint64_t const hash_b = tournament_identity::detail::toj_eval_fingerprint(context, theta_b);
        check(hash_a != 0, "conformance: eval fingerprint is nonzero");
        check(hash_a != hash_b, "conformance: eval fingerprint responds to theta");
    }
}

int main()
{
    test_fingerprint_determinism();
    test_eval_fingerprint_sensitivity();
    std::println("{} conformance test failure(s)", failures);
    return failures == 0 ? 0 : 1;
}
