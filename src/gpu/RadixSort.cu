#include <delivery/gpu/RadixSort.hpp>

#include <delivery/gpu/EventTimer.hpp>
#include <delivery/gpu/cuda_compat.h>

#include <algorithm>
#include <sstream>
#include <thread>
#include <utility>

namespace delivery::gpu {
namespace {
__global__ void countZerosPerBlock(const unsigned* keys, int n, int bit, int* blockZeros) {
    __shared__ int zeros;
    if (threadIdx.x == 0) {
        zeros = 0;
    }
    __syncthreads();
    const int i = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
    if (i < n && ((keys[i] >> bit) & 1u) == 0u) {
        atomicAdd(&zeros, 1);
    }
    __syncthreads();
    if (threadIdx.x == 0) {
        blockZeros[blockIdx.x] = zeros;
    }
}

__global__ void scanBlockZeros(int* blockZeros, int blocks, int* totalZeros) {
    if (threadIdx.x == 0 && blockIdx.x == 0) {
        int running = 0;
        for (int b = 0; b < blocks; ++b) {
            const int count = blockZeros[b];
            blockZeros[b] = running;
            running += count;
        }
        *totalZeros = running;
    }
}

__global__ void splitByBit(const unsigned* in, unsigned* out, int n, int bit,
                           const int* zerosBeforeBlock, const int* totalZeros) {
    __shared__ int zerosUpTo[kRadixBlock];
    const int t = static_cast<int>(threadIdx.x);
    const int i = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
    const unsigned key = i < n ? in[i] : 0u;
    const int isZero = (i < n && ((key >> bit) & 1u) == 0u) ? 1 : 0;

    zerosUpTo[t] = isZero;
    __syncthreads();
    for (int stride = 1; stride < kRadixBlock; stride *= 2) {
        const int left = t >= stride ? zerosUpTo[t - stride] : 0;
        __syncthreads();
        zerosUpTo[t] += left;
        __syncthreads();
    }
    if (i < n) {
        const int zerosBefore = zerosBeforeBlock[blockIdx.x] + zerosUpTo[t] - isZero;
        const int destination = isZero ? zerosBefore : *totalZeros + (i - zerosBefore);
        out[destination] = key;
    }
}

int significantBits(const std::vector<unsigned>& keys) {
    unsigned largest = 0;
    for (unsigned k : keys) {
        largest = std::max(largest, k);
    }
    int bits = 0;
    while (bits < 32 && (largest >> bits) != 0u) {
        ++bits;
    }
    return bits;
}
}

std::vector<unsigned> toSortKeys(const std::vector<long long>& durationsMs) {
    std::vector<unsigned> keys;
    keys.reserve(durationsMs.size());
    for (long long d : durationsMs) {
        keys.push_back(d <= 0 ? 0u : d >= 0xffffffffLL ? 0xffffffffu : static_cast<unsigned>(d));
    }
    return keys;
}

std::vector<unsigned> sortKeysCpu(std::vector<unsigned> keys, unsigned threads) {
    const std::size_t n = keys.size();
    const unsigned workers = std::max(1u, std::min<unsigned>(threads, static_cast<unsigned>(
                                                                          std::max<std::size_t>(1, n))));
    if (workers == 1) {
        std::sort(keys.begin(), keys.end());
        return keys;
    }
    const std::size_t chunk = (n + workers - 1) / workers;
    std::vector<std::thread> pool;
    for (unsigned w = 0; w < workers; ++w) {
        const std::size_t begin = std::min(n, w * chunk);
        const std::size_t end = std::min(n, begin + chunk);
        pool.emplace_back([&keys, begin, end] {
            std::sort(keys.begin() + static_cast<std::ptrdiff_t>(begin),
                      keys.begin() + static_cast<std::ptrdiff_t>(end));
        });
    }
    for (std::thread& t : pool) {
        t.join();
    }
    for (std::size_t width = chunk; width < n; width *= 2) {
        for (std::size_t begin = 0; begin + width < n; begin += 2 * width) {
            const std::size_t middle = begin + width;
            const std::size_t end = std::min(n, begin + 2 * width);
            std::inplace_merge(keys.begin() + static_cast<std::ptrdiff_t>(begin),
                               keys.begin() + static_cast<std::ptrdiff_t>(middle),
                               keys.begin() + static_cast<std::ptrdiff_t>(end));
        }
    }
    return keys;
}

std::vector<unsigned> radixSort(const std::vector<unsigned>& keys, KernelTiming* timing) {
    const int n = static_cast<int>(keys.size());
    std::vector<unsigned> sorted(keys.size());
    if (n == 0) {
        return sorted;
    }
    const int blocks = (n + kRadixBlock - 1) / kRadixBlock;
    const int bits = significantBits(keys);

    unsigned* dA = nullptr;
    unsigned* dB = nullptr;
    int* dBlockZeros = nullptr;
    int* dTotalZeros = nullptr;
    CHECK(cudaMalloc(&dA, n * sizeof(unsigned)));
    CHECK(cudaMalloc(&dB, n * sizeof(unsigned)));
    CHECK(cudaMalloc(&dBlockZeros, blocks * sizeof(int)));
    CHECK(cudaMalloc(&dTotalZeros, sizeof(int)));

    EventTimer copyTimer;
    EventTimer kernelTimer;
    copyTimer.start();
    CHECK(cudaMemcpy(dA, keys.data(), n * sizeof(unsigned), cudaMemcpyHostToDevice));
    double copyMs = copyTimer.stopMs();

    kernelTimer.start();
    for (int bit = 0; bit < bits; ++bit) {
        launch(countZerosPerBlock, dim3(blocks), dim3(kRadixBlock),
               static_cast<const unsigned*>(dA), n, bit, dBlockZeros);
        launch(scanBlockZeros, dim3(1), dim3(1), dBlockZeros, blocks, dTotalZeros);
        launch(splitByBit, dim3(blocks), dim3(kRadixBlock), static_cast<const unsigned*>(dA), dB,
               n, bit, static_cast<const int*>(dBlockZeros), static_cast<const int*>(dTotalZeros));
        std::swap(dA, dB);
    }
    CHECK(cudaGetLastError());
    const double kernelMs = kernelTimer.stopMs();

    copyTimer.start();
    CHECK(cudaMemcpy(sorted.data(), dA, n * sizeof(unsigned), cudaMemcpyDeviceToHost));
    copyMs += copyTimer.stopMs();

    CHECK(cudaFree(dA));
    CHECK(cudaFree(dB));
    CHECK(cudaFree(dBlockZeros));
    CHECK(cudaFree(dTotalZeros));
    if (timing != nullptr) {
        timing->kernelMs = kernelMs;
        timing->copyMs = copyMs;
    }
    return sorted;
}

Percentiles percentilesOfSorted(const std::vector<unsigned>& sorted) {
    Percentiles p;
    if (sorted.empty()) {
        return p;
    }
    const std::size_t n = sorted.size();
    auto nearestRank = [&sorted, n](std::size_t percent) {
        const std::size_t rank = (percent * n + 99) / 100;
        return sorted[std::max<std::size_t>(rank, 1) - 1];
    };
    p.p50 = nearestRank(50);
    p.p90 = nearestRank(90);
    p.p99 = nearestRank(99);
    return p;
}

std::string describePercentiles(const Percentiles& p, std::size_t count) {
    std::ostringstream out;
    out << "Delivery-time percentiles (" << count << " deliveries): p50=" << p.p50
        << " ms, p90=" << p.p90 << " ms, p99=" << p.p99 << " ms";
    return out.str();
}
}
