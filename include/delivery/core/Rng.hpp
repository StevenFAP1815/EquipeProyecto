#pragma once

#include <cstdint>

namespace delivery::core {
inline std::uint64_t deriveSeed(std::uint64_t masterSeed,
                                std::uint64_t streamId) noexcept {
    std::uint64_t z = masterSeed + 0x9e3779b97f4a7c15ULL * (streamId + 1);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}
}
