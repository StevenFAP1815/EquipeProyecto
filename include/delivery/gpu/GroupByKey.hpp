#pragma once

#include <delivery/gpu/KernelTiming.hpp>

#include <vector>

namespace delivery::gpu {
constexpr int kGroupBlock = 256;
constexpr int kScanBlock = 512;
constexpr int kMaxGroupKeys = 1024;

struct Grouping {
    std::vector<int> order;
    std::vector<int> offsets;

    bool operator==(const Grouping& other) const {
        return order == other.order && offsets == other.offsets;
    }
};

Grouping groupByKeyCpu(const std::vector<int>& keys, int keyCount, unsigned threads);
Grouping groupByKey(const std::vector<int>& keys, int keyCount, KernelTiming* timing = nullptr);
}
