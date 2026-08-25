#pragma once

#include "chash.h"


template<class key_t, class unique_t, class hasher_t, class key_equal_t, class allocator_t>
struct chash_set_config_t
{
    using key_type = key_t;
    using mapped_type = key_t;
    using value_type = key_t;
    using hasher = hasher_t;
    using key_equal = key_equal_t;
    using allocator_type = allocator_t;
    using offset_type = std::uintptr_t;
    using hash_value_type = std::invoke_result_t<hasher, key_type>;
    using unique_type = unique_t;
    static float grow_proportion(std::size_t)
    {
        return 2;
    }
    template<class in_type> static key_type const &get_key(in_type &&value)
    {
        return value;
    }
};
template<class key_t, class hasher_t = std::hash<key_t>, class key_equal_t = std::equal_to<key_t>, class allocator_t = std::allocator<key_t>>
using chash_set = contiguous_hash<chash_set_config_t<key_t, std::true_type, hasher_t, key_equal_t, allocator_t>>;
template<class key_t, class hasher_t = std::hash<key_t>, class key_equal_t = std::equal_to<key_t>, class allocator_t = std::allocator<key_t>>
using chash_multiset = contiguous_hash<chash_set_config_t<key_t, std::false_type, hasher_t, key_equal_t, allocator_t>>;