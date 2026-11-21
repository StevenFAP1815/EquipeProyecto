#pragma once

#include <delivery/gpu/KernelTiming.hpp>

#include <vector>

namespace delivery::gpu {
constexpr int kRouteScanBlock = 512;
constexpr int kRouteCarryBlock = 256;
constexpr int kNoDeadline = 0x7fffffff;

struct RouteArrivals {
    std::vector<int> arrivalMs;
    int late = 0;

    bool operator==(const RouteArrivals& other) const {
        return late == other.late && arrivalMs == other.arrivalMs;
    }
};

RouteArrivals routeArrivalsCpu(const std::vector<int>& legMs, const std::vector<unsigned char>& routeStart,
                               const std::vector<int>& deadlineMs, unsigned threads);
RouteArrivals routeArrivals(const std::vector<int>& legMs, const std::vector<unsigned char>& routeStart,
                            const std::vector<int>& deadlineMs, KernelTiming* timing = nullptr);
}
