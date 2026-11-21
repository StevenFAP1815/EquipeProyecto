#pragma once

#include <delivery/gpu/KernelTiming.hpp>

#include <vector>

namespace delivery::gpu {
constexpr int kResponseTile = 16;
constexpr int kResponseUnreachable = 1 << 30;
constexpr int kResponseBinSeconds = 15;
constexpr int kResponseBins = 128;

std::vector<int> responseMapCpu(const std::vector<int>& initial, const std::vector<int>& cellCost,
                                int width, int height, int sweeps, unsigned threads);
std::vector<int> responseMap(const std::vector<int>& initial, const std::vector<int>& cellCost,
                             int width, int height, int sweeps, KernelTiming* timing = nullptr);
int worstServedCell(const std::vector<int>& times, const std::vector<bool>& counted);

std::vector<unsigned> binResponseTimes(const std::vector<int>& times,
                                       const std::vector<unsigned char>& counted);
std::vector<unsigned> responseHistogramCpu(const std::vector<int>& initial,
                                           const std::vector<int>& cellCost,
                                           const std::vector<unsigned char>& counted, int width,
                                           int height, int sweeps, unsigned threads);
std::vector<unsigned> responseHistogram(const std::vector<int>& initial,
                                        const std::vector<int>& cellCost,
                                        const std::vector<unsigned char>& counted, int width,
                                        int height, int sweeps, KernelTiming* timing = nullptr);
}
