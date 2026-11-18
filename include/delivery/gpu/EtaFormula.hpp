#pragma once

#include <cstdint>

#if defined(__CUDACC__)
#define DELIVERY_HD __host__ __device__
#else
#define DELIVERY_HD
#endif

namespace delivery::gpu {
constexpr int kMaxBagStops = 4;
constexpr long long kNoEta = 0x7fffffffffffffffLL;
constexpr std::int32_t kTableInfinity = 0x3fffffff;

DELIVERY_HD inline long long travelMs(long long centimeters, int cmPerSecond) {
    return centimeters * 1000 / cmPerSecond;
}

DELIVERY_HD inline long long maxOf(long long a, long long b) {
    return a > b ? a : b;
}

DELIVERY_HD inline long long etaMs(const std::int32_t* table, int n, int courierNode,
                                   const int* stops, int stopStride, long long latestReadyAtMs,
                                   int cmPerSecond, int restaurant, int delivery,
                                   long long readyAtMs, long long nowMs) {
    const std::int32_t toRestaurant =
        table[static_cast<long long>(courierNode) * n + restaurant];
    if (toRestaurant >= kTableInfinity) {
        return kNoEta;
    }
    long long afterPickup = 0;
    int at = restaurant;
    for (int k = 0; k < kMaxBagStops; ++k) {
        const int stop = stops[k * stopStride];
        if (stop < 0) {
            break;
        }
        const std::int32_t leg = table[static_cast<long long>(at) * n + stop];
        if (leg >= kTableInfinity) {
            return kNoEta;
        }
        afterPickup += leg;
        at = stop;
    }
    const std::int32_t last = table[static_cast<long long>(at) * n + delivery];
    if (last >= kTableInfinity) {
        return kNoEta;
    }
    afterPickup += last;

    const long long arrival = nowMs + travelMs(toRestaurant, cmPerSecond);
    const long long pickup = maxOf(maxOf(arrival, readyAtMs), latestReadyAtMs);
    return pickup + travelMs(afterPickup, cmPerSecond) - nowMs;
}
}
