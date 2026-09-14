#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

#include "ai_zzz.h"
#include "rule_toj.h"
#include "search_tspin.h"
#include "tetris_core.h"
#include "tuner_toj.h"
#include "tuning_domain.h"

namespace tuning_toj
{
    static_assert(tuner_toj::Tuner::NUM_PARAMS == ai_zzz::TOJ::NUM_PARAMS,
                  "tuner_toj and ai_zzz::TOJ parameter counts must agree");

    class TojInstance;

    struct TojAdapter
    {
        using engine_type = m_tetris::TetrisEngine<rule_toj::TetrisRule, ai_zzz::TOJ, search_tspin::Search>;
        using instance_type = TojInstance;

        static constexpr std::string_view kAdapterId = "ai_zzz::TOJ";

        static constexpr std::size_t param_count()
        {
            return ai_zzz::TOJ::NUM_PARAMS;
        }

        static constexpr std::size_t next_length()
        {
            return static_cast<std::size_t>(tuner_toj::Tuner::NEXT_LENGTH);
        }

        static std::span<int const> combo_table()
        {
            return std::span<int const>(tuner_toj::Tuner::combo_table,
                                        static_cast<std::size_t>(tuner_toj::Tuner::combo_table_max));
        }

        static constexpr std::uint64_t memory_limit_bytes()
        {
            return 256ull << 20;
        }

        static tuning::ParamSchema schema();
        static std::uint64_t schema_hash();
        static std::shared_ptr<m_tetris::TetrisContext> make_shared_context();
        static TojInstance make_instance(std::shared_ptr<m_tetris::TetrisContext> const& context);
    };

    class TojInstance
    {
    public:
        using Engine = TojAdapter::engine_type;

        explicit TojInstance(std::shared_ptr<m_tetris::TetrisContext> const& context)
        {
            if (context == nullptr)
            {
                return;
            }
            engine_.emplace(context);
            engine_->prepare(10, 40);
            engine_->memory_limit(TojAdapter::memory_limit_bytes());
            engine_->search_config()->allow_rotate_move = false;
            engine_->search_config()->allow_180 = true;
            engine_->search_config()->allow_d = true;
            engine_->search_config()->is_20g = false;
            engine_->search_config()->last_rotate = false;
            engine_->ai_config()->table = TojAdapter::combo_table().data();
            engine_->ai_config()->table_max = static_cast<int>(TojAdapter::combo_table().size());
        }

        bool valid() const
        {
            return engine_.has_value();
        }

        bool apply_theta(double const* theta, std::size_t count)
        {
            if (!engine_ || !tuning::validate_theta(TojAdapter::schema(), theta, count))
            {
                return false;
            }
            ai_zzz::TOJ::theta_to_param(theta, engine_->ai_config()->param);
            return true;
        }

        tuning::MoveOutcome run_move(tuning::MoveRequest const& request, m_tetris::SearchBudget budget)
        {
            tuning::MoveOutcome outcome;
            if (!engine_)
            {
                outcome.dead = true;
                return outcome;
            }
            if (request.map == nullptr || (request.next_length > 0 && request.next == nullptr))
            {
                outcome.dead = true;
                return outcome;
            }
            m_tetris::TetrisMap const& map = *request.map;
            engine_->ai_config()->safe = engine_->ai()->get_safe(map, request.current);
            engine_->status()->death = 0;
            engine_->status()->combo = request.combo;
            engine_->status()->under_attack = request.under_attack;
            engine_->status()->map_rise = 0;
            engine_->status()->combo_debt = 0;
            engine_->status()->just_attacked = 0;
            engine_->status()->since_attack = 0;
            engine_->status()->b2b = request.b2b;
            engine_->status()->acc_value = 0;
            engine_->status()->like = 0;
            engine_->status()->value = 0;
            ai_zzz::TOJ::Status::init_t_value(map, engine_->status()->t2_value, engine_->status()->t3_value);

            bool const is_hold_piece = request.hold != ' ' && request.current == request.hold;
            auto const result = engine_->run_hold(map, engine_->spawn_node(request.current, request.last_clear, is_hold_piece, map),
                                                  request.hold, request.hold_free, request.next, request.next_length, budget);
            if (result.target == nullptr || result.target->row >= 20)
            {
                outcome.dead = true;
                return outcome;
            }
            outcome.change_hold = result.change_hold;
            outcome.clear = static_cast<int>(result.target->attach(engine_->context().get(), *request.map));
            outcome.spin = normalize_spin(result.target.type);
            return outcome;
        }

        m_tetris::TetrisContext* context()
        {
            return engine_ ? engine_->context().get() : nullptr;
        }

    private:
        static tuning::Spin normalize_spin(search_tspin::Search::TSpinType type)
        {
            switch (type)
            {
            case search_tspin::Search::TSpinType::TSpinMini:
                return tuning::Spin::Mini;
            case search_tspin::Search::TSpinType::TSpin:
                return tuning::Spin::Full;
            default:
                return tuning::Spin::None;
            }
        }

        std::optional<Engine> engine_;
    };

    inline tuning::ParamSchema TojAdapter::schema()
    {
        return tuning::ParamSchema{
            .adapter_id = kAdapterId,
            .names = std::span<char const* const>(tuner_toj::Tuner::param_names, tuner_toj::Tuner::NUM_PARAMS),
            .scales = std::span<double const>(tuner_toj::Tuner::param_scale, tuner_toj::Tuner::NUM_PARAMS),
            .defaults = std::span<double const>(ai_zzz::TOJ::kProductionDefaultTheta, ai_zzz::TOJ::NUM_PARAMS),
        };
    }

    inline std::uint64_t TojAdapter::schema_hash()
    {
        return tuning::schema_hash(schema());
    }

    inline std::shared_ptr<m_tetris::TetrisContext> TojAdapter::make_shared_context()
    {
        engine_type engine;
        if (!engine.prepare(10, 40))
        {
            return {};
        }
        return engine.context();
    }

    inline TojInstance TojAdapter::make_instance(std::shared_ptr<m_tetris::TetrisContext> const& context)
    {
        return TojInstance(context);
    }

    static_assert(tuning::EngineAdapter<TojAdapter>);
}
