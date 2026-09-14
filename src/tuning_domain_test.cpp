#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "tetris_core.h"
#include "tuning_domain.h"
#ifndef TUNING_DOMAIN_TEST_SKIP_TOJ
#include "tuning_toj_adapter.h"
#endif

namespace
{
    int failures = 0;

    void check(bool ok, std::string_view name)
    {
        std::println("{} {}", ok ? "PASS" : "FAIL", name);
        if (!ok)
        {
            ++failures;
        }
    }

    struct FakeInstance
    {
        std::vector<double> applied;
        int moves = 0;

        bool apply_theta(double const* theta, std::size_t count)
        {
            if (count != 3)
            {
                return false;
            }
            for (std::size_t i = 0; i < count; ++i)
            {
                if (!std::isfinite(theta[i]))
                {
                    return false;
                }
            }
            applied.assign(theta, theta + count);
            return true;
        }

        tuning::MoveOutcome run_move(tuning::MoveRequest const& request, m_tetris::SearchBudget budget)
        {
            (void)budget;
            ++moves;
            tuning::MoveOutcome outcome;
            outcome.dead = request.under_attack > 100;
            outcome.clear = outcome.dead ? 0 : 1;
            outcome.spin = tuning::Spin::None;
            return outcome;
        }

        m_tetris::TetrisContext* context()
        {
            return nullptr;
        }
    };

    struct FakeAdapter
    {
        using engine_type = int;
        using instance_type = FakeInstance;

        static tuning::ParamSchema schema()
        {
            static char const* const names[] = { "alpha", "beta", "gamma" };
            static double const scales[] = { 1.0, 2.0, 4.0 };
            static double const defaults[] = { 0.5, -1.25, 8.0 };
            return tuning::ParamSchema{
                .adapter_id = "fake3",
                .names = names,
                .scales = scales,
                .defaults = defaults,
            };
        }

        static constexpr std::size_t param_count()
        {
            return 3;
        }

        static constexpr std::size_t next_length()
        {
            return 2;
        }

        static std::span<int const> combo_table()
        {
            static int const table[] = { 0, 1, 2 };
            return table;
        }

        static constexpr std::uint64_t memory_limit_bytes()
        {
            return 0;
        }

        static std::shared_ptr<m_tetris::TetrisContext> make_shared_context()
        {
            return nullptr;
        }

        static FakeInstance make_instance(std::shared_ptr<m_tetris::TetrisContext> const&)
        {
            return FakeInstance{};
        }
    };

    static_assert(tuning::EngineInstance<FakeInstance>);
    static_assert(tuning::EngineAdapter<FakeAdapter>);
#ifndef TUNING_DOMAIN_TEST_SKIP_TOJ
    static_assert(tuning::EngineAdapter<tuning_toj::TojAdapter>);
#endif

    template<tuning::EngineAdapter A>
    bool drive_moves(double const* theta, std::size_t count, int moves_requested)
    {
        auto context = A::make_shared_context();
        auto instance = A::make_instance(context);
        if (!instance.apply_theta(theta, count))
        {
            return false;
        }
        std::string const queue = "IJOLOTSZIJOLOTSZ";
        m_tetris::TetrisMap map(10, 40);
        int last_clear = 0;
        std::size_t position = 0;
        for (int m = 0; m < moves_requested; ++m)
        {
            if (position + A::next_length() >= queue.size())
            {
                return false;
            }
            tuning::MoveRequest request;
            request.map = &map;
            request.current = queue[position];
            request.hold = ' ';
            request.hold_free = true;
            request.next = queue.data() + position + 1;
            request.next_length = A::next_length();
            request.last_clear = last_clear;
            tuning::MoveOutcome const outcome = instance.run_move(request, m_tetris::SearchBudget::by_iterations(1000));
            if (outcome.dead || outcome.clear < 0 || outcome.clear > 4)
            {
                return false;
            }
            last_clear = outcome.clear;
            ++position;
        }
        return true;
    }

    void run_fake_tests()
    {
        auto const schema = FakeAdapter::schema();
        check(tuning::schema_consistent(schema), "fake_schema_consistent");
        check(schema.size() == FakeAdapter::param_count(), "fake_schema_size_matches_param_count");

        tuning::ParamSchema empty_name = schema;
        static char const* const empty_names[] = { "", "beta", "gamma" };
        empty_name.names = empty_names;
        check(!tuning::schema_consistent(empty_name), "schema_consistent_rejects_empty_name");

        double const good[] = { 1.0, -2.0, 3.5 };
        check(tuning::validate_theta(schema, good, 3), "fake_schema_accepts_finite_theta");
        check(!tuning::validate_theta(schema, good, 2), "fake_schema_rejects_short_theta");
        double const dirty[] = { 1.0, std::nan(""), 3.5 };
        check(!tuning::validate_theta(schema, dirty, 3), "fake_schema_rejects_nonfinite_theta");
        check(!tuning::validate_theta(schema, nullptr, 3), "fake_schema_rejects_null_theta");

        std::uint64_t const h1 = tuning::schema_hash(schema);
        std::uint64_t const h2 = tuning::schema_hash(FakeAdapter::schema());
        check(h1 == h2 && h1 != 0, "schema_hash_stable_and_nonzero");

        tuning::ParamSchema renamed = schema;
        renamed.adapter_id = "fake3_renamed";
        check(tuning::schema_hash(renamed) != h1, "schema_hash_covers_adapter_id");

        tuning::ParamSchema rescaled = schema;
        static double const alt_scales[] = { 1.0, 2.0, 4.5 };
        rescaled.scales = alt_scales;
        check(tuning::schema_hash(rescaled) != h1, "schema_hash_covers_scales");

        tuning::ParamSchema zero_scale = schema;
        static double const zero_scales[] = { 0.0, 2.0, 4.0 };
        zero_scale.scales = zero_scales;
        check(!tuning::schema_consistent(zero_scale), "schema_consistent_rejects_zero_scale");

        tuning::ParamSchema negative_scale = schema;
        static double const negative_scales[] = { 1.0, -2.0, 4.0 };
        negative_scale.scales = negative_scales;
        check(!tuning::schema_consistent(negative_scale), "schema_consistent_rejects_negative_scale");

        tuning::ParamSchema nan_scale = schema;
        static double const nan_scales[] = { 1.0, std::nan(""), 4.0 };
        nan_scale.scales = nan_scales;
        check(!tuning::schema_consistent(nan_scale), "schema_consistent_rejects_nonfinite_scale");

        tuning::ParamSchema inf_scale = schema;
        static double const inf_scales[] = { 1.0, 2.0, std::numeric_limits<double>::infinity() };
        inf_scale.scales = inf_scales;
        check(!tuning::schema_consistent(inf_scale), "schema_consistent_rejects_infinite_scale");

        tuning::ParamSchema nan_default = schema;
        static double const nan_defaults[] = { 0.5, std::nan(""), 8.0 };
        nan_default.defaults = nan_defaults;
        check(!tuning::schema_consistent(nan_default), "schema_consistent_rejects_nonfinite_default");

        tuning::ParamSchema inf_default = schema;
        static double const inf_defaults[] = { 0.5, -1.25, std::numeric_limits<double>::infinity() };
        inf_default.defaults = inf_defaults;
        check(!tuning::schema_consistent(inf_default), "schema_consistent_rejects_infinite_default");

        check(drive_moves<FakeAdapter>(good, 3, 3), "fake_adapter_drives_three_generic_moves");

        auto instance = FakeAdapter::make_instance(FakeAdapter::make_shared_context());
        check(instance.apply_theta(good, 3), "fake_instance_applies_valid_theta");
        check(instance.applied.size() == 3 && instance.applied[1] == -2.0, "fake_instance_stores_theta");
        check(!instance.apply_theta(good, 2), "fake_instance_rejects_short_theta");
        check(!instance.apply_theta(dirty, 3), "fake_instance_rejects_nonfinite_theta");

        tuning::MoveRequest request;
        request.under_attack = 200;
        auto const outcome = instance.run_move(request, m_tetris::SearchBudget::by_iterations(1));
        check(outcome.dead && outcome.clear == 0, "fake_instance_reports_dead_move");
        check(instance.moves == 1, "fake_instance_counts_moves");
    }

#ifndef TUNING_DOMAIN_TEST_SKIP_TOJ
    void run_toj_tests()
    {
        auto const schema = tuning_toj::TojAdapter::schema();
        check(tuning::schema_consistent(schema), "toj_schema_consistent");
        check(schema.size() == tuning_toj::TojAdapter::param_count(), "toj_schema_size_matches_param_count");
        check(tuning::validate_theta(schema, schema.defaults.data(), schema.defaults.size()), "toj_production_defaults_validate");

        std::vector<double> dirty(schema.defaults.begin(), schema.defaults.end());
        dirty[0] = std::nan("");
        check(!tuning::validate_theta(schema, dirty.data(), dirty.size()), "toj_schema_rejects_nonfinite_theta");
        check(!tuning::validate_theta(schema, schema.defaults.data(), schema.defaults.size() - 1), "toj_schema_rejects_short_theta");
        check(tuning::schema_hash(schema) != tuning::schema_hash(FakeAdapter::schema()), "toj_and_fake_schema_hashes_differ");

        check(drive_moves<tuning_toj::TojAdapter>(schema.defaults.data(), schema.defaults.size(), 4), "toj_adapter_drives_four_generic_moves");

        auto context = tuning_toj::TojAdapter::make_shared_context();
        check(context != nullptr, "toj_adapter_builds_shared_context");
        auto instance = tuning_toj::TojAdapter::make_instance(context);
        check(instance.valid(), "toj_instance_valid_with_shared_context");
        check(instance.apply_theta(schema.defaults.data(), schema.defaults.size()), "toj_instance_applies_defaults");
        check(!instance.apply_theta(schema.defaults.data(), schema.defaults.size() - 1), "toj_instance_rejects_short_theta");

        auto null_instance = tuning_toj::TojAdapter::make_instance(std::shared_ptr<m_tetris::TetrisContext>{});
        check(!null_instance.valid(), "toj_instance_reports_missing_context");
        check(null_instance.context() == nullptr, "toj_instance_missing_context_has_null_engine");
        check(!null_instance.apply_theta(schema.defaults.data(), schema.defaults.size()), "toj_instance_rejects_theta_without_context");
        m_tetris::TetrisMap dead_map(10, 40);
        tuning::MoveRequest dead_request;
        dead_request.map = &dead_map;
        check(null_instance.run_move(dead_request, m_tetris::SearchBudget::by_iterations(1)).dead, "toj_instance_dead_move_without_context");

        tuning::MoveRequest null_map_request;
        check(instance.run_move(null_map_request, m_tetris::SearchBudget::by_iterations(1)).dead, "toj_instance_dead_move_with_null_map");

        m_tetris::TetrisMap smoke_map(10, 40);
        tuning::MoveRequest null_next_request;
        null_next_request.map = &smoke_map;
        null_next_request.current = 'I';
        null_next_request.next = nullptr;
        null_next_request.next_length = tuning_toj::TojAdapter::next_length();
        check(instance.run_move(null_next_request, m_tetris::SearchBudget::by_iterations(1)).dead, "toj_instance_dead_move_with_null_next");

        char const queue[] = "IJOLOTS";
        tuning::MoveRequest valid_request;
        valid_request.map = &smoke_map;
        valid_request.current = queue[0];
        valid_request.next = queue + 1;
        valid_request.next_length = tuning_toj::TojAdapter::next_length();
        check(!instance.run_move(valid_request, m_tetris::SearchBudget::by_iterations(256)).dead, "toj_instance_plays_after_invalid_requests");

        std::vector<double> nan_theta(schema.defaults.begin(), schema.defaults.end());
        nan_theta[7] = std::nan("");
        check(!instance.apply_theta(nan_theta.data(), nan_theta.size()), "toj_instance_rejects_nonfinite_theta");

        std::vector<double> bad_dim(46, 1.0);
        check(!instance.apply_theta(bad_dim.data(), bad_dim.size()), "toj_instance_rejects_wrong_count");
    }
#endif
}

int main()
{
    run_fake_tests();
#ifndef TUNING_DOMAIN_TEST_SKIP_TOJ
    run_toj_tests();
#endif
    if (failures == 0)
    {
        std::println("ALL TUNING DOMAIN CHECKS PASSED");
        return 0;
    }
    std::println("{} TUNING DOMAIN CHECK(S) FAILED", failures);
    return 1;
}
