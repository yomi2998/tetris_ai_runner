#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "tournament/rating.h"

namespace tournament_ordinal
{
    struct Result
    {
        bool ok = false;
        std::string error;
        std::vector<double> fitness;
        std::vector<tournament_rating::CandidateId> order;
    };

    inline Result build(tournament_rating::FitResult const &rating,
                        std::vector<tournament_rating::CandidateId> const &sample_ids)
    {
        Result result;
        if (!rating.ok)
        {
            result.error = "cannot rank samples from a failed rating fit";
            return result;
        }
        if (sample_ids.size() < 2)
        {
            result.error = "at least two optimizer samples are required";
            return result;
        }
        std::unordered_set<tournament_rating::CandidateId> unique_ids;
        unique_ids.reserve(sample_ids.size());
        for (auto id : sample_ids)
        {
            if (!unique_ids.insert(id).second)
            {
                result.error = "optimizer sample ids must be unique";
                return result;
            }
        }
        std::unordered_map<tournament_rating::CandidateId, tournament_rating::Rating const *> by_id;
        by_id.reserve(rating.ratings.size());
        for (auto const &entry : rating.ratings)
        {
            if (!std::isfinite(entry.expected_rank) || !std::isfinite(entry.rating)
                || !by_id.emplace(entry.candidate, &entry).second)
            {
                result.error = "rating entries must be unique and finite";
                return result;
            }
        }
        result.order = sample_ids;
        for (auto id : result.order)
        {
            if (!by_id.contains(id))
            {
                result.error = "rating fit is missing optimizer sample " + std::to_string(id);
                result.order.clear();
                return result;
            }
        }
        std::sort(result.order.begin(), result.order.end(), [&by_id](auto a, auto b)
        {
            auto const &left = *by_id.at(a);
            auto const &right = *by_id.at(b);
            if (left.expected_rank != right.expected_rank)
            {
                return left.expected_rank < right.expected_rank;
            }
            if (left.rating != right.rating)
            {
                return left.rating > right.rating;
            }
            return left.candidate < right.candidate;
        });
        std::unordered_map<tournament_rating::CandidateId, std::size_t> rank_by_id;
        rank_by_id.reserve(result.order.size());
        for (std::size_t rank = 0; rank < result.order.size(); ++rank)
        {
            rank_by_id.emplace(result.order[rank], rank);
        }
        result.fitness.resize(sample_ids.size());
        for (std::size_t i = 0; i < sample_ids.size(); ++i)
        {
            result.fitness[i] = static_cast<double>(rank_by_id.at(sample_ids[i]));
        }
        result.ok = true;
        return result;
    }
}
