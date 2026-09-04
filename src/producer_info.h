#pragma once

#include <print>

namespace producer_info
{

inline void print(char const *name)
{
#ifdef PRODUCER_DETAIL
    std::println("PRODUCER {} {}", name, PRODUCER_DETAIL);
#else
    std::println("PRODUCER {} detail-not-configured", name);
#endif
}

} // namespace producer_info
