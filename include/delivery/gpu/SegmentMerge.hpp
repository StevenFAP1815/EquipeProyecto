#pragma once

#include <delivery/gpu/KernelTiming.hpp>

#include <vector>

namespace delivery::gpu {
constexpr int kMergeBlock = 256;
constexpr int kMergeTile = 8;

struct Timeline {
    std::vector<long long> t;
    std::vector<int> source;

    bool operator==(const Timeline& other) const {
        return t == other.t && source == other.source;
    }
};

Timeline mergeSegmentsReference(const Timeline& segments, const std::vector<int>& offsets);
Timeline mergeSegmentsCpu(const Timeline& segments, const std::vector<int>& offsets,
                          unsigned threads);
Timeline mergeSegments(const Timeline& segments, const std::vector<int>& offsets,
                       KernelTiming* timing = nullptr);
}
