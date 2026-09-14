#pragma once

#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include "tetris_core.h"

namespace tuning
{
    using CandidateId = std::uint64_t;

    struct Candidate
    {
        CandidateId id = 0;
        std::vector<double> theta;
    };

    enum class Spin : std::uint8_t
    {
        None,
        Mini,
        Full,
    };

    struct MoveRequest
    {
        m_tetris::TetrisMap* map = nullptr;
        char current = ' ';
        char hold = ' ';
        bool hold_free = true;
        char const* next = nullptr;
        std::size_t next_length = 0;
        int last_clear = 0;
        int combo = 0;
        bool b2b = false;
        int under_attack = 0;
    };

    struct MoveOutcome
    {
        bool dead = false;
        bool change_hold = false;
        int clear = 0;
        Spin spin = Spin::None;
    };

    struct ParamSchema
    {
        std::string_view adapter_id;
        std::span<char const* const> names;
        std::span<double const> scales;
        std::span<double const> defaults;

        std::size_t size() const
        {
            return names.size();
        }
    };

    inline constexpr std::uint64_t kFnvOffsetBasis = 0xCBF29CE484222325ULL;
    inline constexpr std::uint64_t kFnvPrime = 0x100000001B3ULL;

    inline constexpr std::uint64_t fnv1a_byte(std::uint64_t hash, unsigned char value)
    {
        return (hash ^ value) * kFnvPrime;
    }

    inline constexpr std::uint64_t fnv1a_bytes(std::uint64_t hash, char const* data, std::size_t count)
    {
        for (std::size_t i = 0; i < count; ++i)
        {
            hash = fnv1a_byte(hash, static_cast<unsigned char>(data[i]));
        }
        return hash;
    }

    inline constexpr std::uint64_t fnv1a_u64(std::uint64_t hash, std::uint64_t value)
    {
        for (int i = 0; i < 8; ++i)
        {
            hash = fnv1a_byte(hash, static_cast<unsigned char>((value >> (8 * i)) & 0xFFu));
        }
        return hash;
    }

    inline constexpr std::uint64_t fnv1a_double(std::uint64_t hash, double value)
    {
        return fnv1a_u64(hash, std::bit_cast<std::uint64_t>(value));
    }

    inline std::uint64_t schema_hash(ParamSchema const& schema)
    {
        std::uint64_t hash = kFnvOffsetBasis;
        hash = fnv1a_bytes(hash, schema.adapter_id.data(), schema.adapter_id.size());
        hash = fnv1a_byte(hash, 0);
        hash = fnv1a_u64(hash, static_cast<std::uint64_t>(schema.names.size()));
        for (char const* name : schema.names)
        {
            std::size_t const length = name != nullptr ? std::strlen(name) : std::size_t{0};
            hash = fnv1a_bytes(hash, name, length);
            hash = fnv1a_byte(hash, 0);
        }
        for (double value : schema.scales)
        {
            hash = fnv1a_double(hash, value);
        }
        for (double value : schema.defaults)
        {
            hash = fnv1a_double(hash, value);
        }
        return hash;
    }

    inline bool schema_consistent(ParamSchema const& schema)
    {
        if (schema.adapter_id.empty() || schema.names.empty())
        {
            return false;
        }
        if (schema.scales.size() != schema.names.size() || schema.defaults.size() != schema.names.size())
        {
            return false;
        }
        for (char const* name : schema.names)
        {
            if (name == nullptr || name[0] == '\0')
            {
                return false;
            }
        }
        for (double scale : schema.scales)
        {
            if (!std::isfinite(scale) || scale <= 0.0)
            {
                return false;
            }
        }
        for (double value : schema.defaults)
        {
            if (!std::isfinite(value))
            {
                return false;
            }
        }
        return true;
    }

    inline bool theta_finite(double const* theta, std::size_t count)
    {
        if (theta == nullptr)
        {
            return count == 0;
        }
        for (std::size_t i = 0; i < count; ++i)
        {
            if (!std::isfinite(theta[i]))
            {
                return false;
            }
        }
        return true;
    }

    inline bool validate_theta(ParamSchema const& schema, double const* theta, std::size_t count)
    {
        return count == schema.size() && theta_finite(theta, count);
    }

    inline bool validate_theta(ParamSchema const& schema, std::vector<double> const& theta)
    {
        return validate_theta(schema, theta.data(), theta.size());
    }

    template<class I>
    concept EngineInstance = requires(I& instance, MoveRequest const& request, double const* theta, std::size_t count, m_tetris::SearchBudget budget)
    {
        { instance.apply_theta(theta, count) } -> std::convertible_to<bool>;
        { instance.run_move(request, budget) } -> std::same_as<MoveOutcome>;
        { instance.context() } -> std::convertible_to<m_tetris::TetrisContext*>;
    };

    template<class A>
    concept EngineAdapter = requires(std::shared_ptr<m_tetris::TetrisContext> const& context)
    {
        typename A::engine_type;
        typename A::instance_type;
        requires EngineInstance<typename A::instance_type>;
        { A::schema() } -> std::convertible_to<ParamSchema>;
        { A::param_count() } -> std::convertible_to<std::size_t>;
        { A::next_length() } -> std::convertible_to<std::size_t>;
        { A::combo_table() } -> std::convertible_to<std::span<int const>>;
        { A::memory_limit_bytes() } -> std::convertible_to<std::uint64_t>;
        { A::make_shared_context() } -> std::convertible_to<std::shared_ptr<m_tetris::TetrisContext>>;
        { A::make_instance(context) } -> std::convertible_to<typename A::instance_type>;
    };
}
