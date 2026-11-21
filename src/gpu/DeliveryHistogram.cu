#include <delivery/gpu/DeliveryHistogram.hpp>

#include <delivery/gpu/EtaFormula.hpp>
#include <delivery/gpu/EventTimer.hpp>
#include <delivery/gpu/cuda_compat.h>

#include <algorithm>
#include <sstream>
#include <thread>

namespace delivery::gpu {
namespace {
constexpr int kHistogramBlock = 256;
constexpr int kHistogramGrid = 160;

DELIVERY_HD inline int binOf(long long durationMs) {
    if (durationMs < 0) {
        return 0;
    }
    const long long bin = durationMs / kHistogramBinMs;
    return bin >= kHistogramBins ? kHistogramBins - 1 : static_cast<int>(bin);
}

__global__ void privatizedHistogram(const long long* durations, int n, unsigned* bins) {
    __shared__ unsigned local[kHistogramBins];
    for (int b = static_cast<int>(threadIdx.x); b < kHistogramBins; b += kHistogramBlock) {
        local[b] = 0;
    }
    __syncthreads();
    const int stride = static_cast<int>(gridDim.x * blockDim.x);
    for (int i = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x); i < n; i += stride) {
        atomicAdd(&local[binOf(durations[i])], 1u);
    }
    __syncthreads();
    for (int b = static_cast<int>(threadIdx.x); b < kHistogramBins; b += kHistogramBlock) {
        if (local[b] != 0) {
            atomicAdd(&bins[b], local[b]);
        }
    }
}
}

std::vector<unsigned> deliveryHistogramCpu(const std::vector<long long>& durationsMs,
                                           unsigned threads) {
    const std::size_t n = durationsMs.size();
    const unsigned workers = std::max(1u, std::min<unsigned>(threads, static_cast<unsigned>(
                                                                          std::max<std::size_t>(1, n))));
    std::vector<std::vector<unsigned>> partial(workers, std::vector<unsigned>(kHistogramBins, 0));
    const std::size_t chunk = (n + workers - 1) / workers;
    auto work = [&](unsigned w) {
        const std::size_t begin = w * chunk;
        const std::size_t end = std::min(n, begin + chunk);
        for (std::size_t i = begin; i < end; ++i) {
            ++partial[w][static_cast<std::size_t>(binOf(durationsMs[i]))];
        }
    };
    if (workers == 1) {
        work(0);
    } else {
        std::vector<std::thread> pool;
        for (unsigned w = 0; w < workers; ++w) {
            pool.emplace_back(work, w);
        }
        for (std::thread& t : pool) {
            t.join();
        }
    }
    std::vector<unsigned> bins(kHistogramBins, 0);
    for (const auto& p : partial) {
        for (int b = 0; b < kHistogramBins; ++b) {
            bins[static_cast<std::size_t>(b)] += p[static_cast<std::size_t>(b)];
        }
    }
    return bins;
}

std::vector<unsigned> deliveryHistogram(const std::vector<long long>& durationsMs,
                                        KernelTiming* timing) {
    std::vector<unsigned> bins(kHistogramBins, 0);
    const int n = static_cast<int>(durationsMs.size());
    long long* dDurations = nullptr;
    unsigned* dBins = nullptr;
    CHECK(cudaMalloc(&dDurations, std::max(1, n) * sizeof(long long)));
    CHECK(cudaMalloc(&dBins, kHistogramBins * sizeof(unsigned)));

    EventTimer copyTimer;
    EventTimer kernelTimer;
    copyTimer.start();
    if (n > 0) {
        CHECK(cudaMemcpy(dDurations, durationsMs.data(), n * sizeof(long long),
                         cudaMemcpyHostToDevice));
    }
    CHECK(cudaMemset(dBins, 0, kHistogramBins * sizeof(unsigned)));
    double copyMs = copyTimer.stopMs();

    const int blocks = std::max(1, std::min(kHistogramGrid, (n + kHistogramBlock - 1) / kHistogramBlock));
    kernelTimer.start();
    launch(privatizedHistogram, dim3(blocks), dim3(kHistogramBlock),
           static_cast<const long long*>(dDurations), n, dBins);
    CHECK(cudaGetLastError());
    const double kernelMs = kernelTimer.stopMs();

    copyTimer.start();
    CHECK(cudaMemcpy(bins.data(), dBins, kHistogramBins * sizeof(unsigned),
                     cudaMemcpyDeviceToHost));
    copyMs += copyTimer.stopMs();
    CHECK(cudaFree(dDurations));
    CHECK(cudaFree(dBins));
    if (timing != nullptr) {
        timing->kernelMs = kernelMs;
        timing->copyMs = copyMs;
    }
    return bins;
}

std::string describeHistogram(const std::vector<unsigned>& bins) {
    std::ostringstream out;
    unsigned long long total = 0;
    for (unsigned count : bins) {
        total += count;
    }
    out << "Delivery-time histogram (1-minute bins, " << total << " deliveries):";
    for (std::size_t b = 0; b < bins.size(); ++b) {
        if (bins[b] != 0) {
            out << ' ' << b << (b + 1 == bins.size() ? "+" : "") << "m=" << bins[b];
        }
    }
    return out.str();
}
}
