#pragma once

#include <delivery/gpu/KernelTiming.hpp>

#include <string>
#include <vector>

namespace delivery::gpu {
constexpr int kRadixBlock = 256;

struct Percentiles {
    unsigned p50 = 0;
    unsigned p90 = 0;
    unsigned p99 = 0;

    bool operator==(const Percentiles& other) const {
        return p50 == other.p50 && p90 == other.p90 && p99 == other.p99;
    }
};

std::vector<unsigned> toSortKeys(const std::vector<long long>& durationsMs);
std::vector<unsigned> sortKeysCpu(std::vector<unsigned> keys, unsigned threads);
std::vector<unsigned> radixSort(const std::vector<unsigned>& keys,
                                KernelTiming* timing = nullptr);
Percentiles percentilesOfSorted(const std::vector<unsigned>& sorted);
std::string describePercentiles(const Percentiles& p, std::size_t count);
}
