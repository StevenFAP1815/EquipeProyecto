#pragma once

#include <delivery/gpu/KernelTiming.hpp>

#include <string>
#include <vector>

namespace delivery::gpu {
constexpr int kHistogramBins = 128;
constexpr long long kHistogramBinMs = 60000;

std::vector<unsigned> deliveryHistogramCpu(const std::vector<long long>& durationsMs,
                                           unsigned threads);
std::vector<unsigned> deliveryHistogram(const std::vector<long long>& durationsMs,
                                        KernelTiming* timing = nullptr);
std::string describeHistogram(const std::vector<unsigned>& bins);
}
