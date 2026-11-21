#include <delivery/gpu/RouteArrivals.hpp>

#include <delivery/gpu/EtaFormula.hpp>
#include <delivery/gpu/EventTimer.hpp>
#include <delivery/gpu/cuda_compat.h>

#include <algorithm>
#include <stdexcept>
#include <thread>

namespace delivery::gpu {
namespace {
void validate(const std::vector<int>& legMs, const std::vector<unsigned char>& routeStart,
              const std::vector<int>& deadlineMs) {
    if (routeStart.size() != legMs.size() || deadlineMs.size() != legMs.size()) {
        throw std::invalid_argument("route legs, starts and deadlines must have the same size");
    }
    if (!legMs.empty() && routeStart.front() == 0) {
        throw std::invalid_argument("the first leg must start a route");
    }
    for (int leg : legMs) {
        if (leg < 0) {
            throw std::invalid_argument("route leg times must not be negative");
        }
    }
}

__global__ void segmentedScanBlocks(const int* legMs, const unsigned char* routeStart, int n,
                                    int* arrival, unsigned char* startSeen, int* blockValue,
                                    int* blockStarted) {
    __shared__ int value[kRouteScanBlock];
    __shared__ int started[kRouteScanBlock];
    const int t = static_cast<int>(threadIdx.x);
    const int i = static_cast<int>(blockIdx.x) * kRouteScanBlock + t;
    value[t] = i < n ? legMs[i] : 0;
    started[t] = i < n ? routeStart[i] : 0;
    __syncthreads();

    for (int offset = 1; offset < kRouteScanBlock; offset *= 2) {
        int previousValue = 0;
        int previousStarted = 0;
        if (t >= offset) {
            previousValue = value[t - offset];
            previousStarted = started[t - offset];
        }
        __syncthreads();
        if (t >= offset) {
            if (started[t] == 0) {
                value[t] += previousValue;
            }
            started[t] |= previousStarted;
        }
        __syncthreads();
    }

    if (i < n) {
        arrival[i] = value[t];
        startSeen[i] = static_cast<unsigned char>(started[t]);
    }
    if (t == kRouteScanBlock - 1) {
        blockValue[blockIdx.x] = value[t];
        blockStarted[blockIdx.x] = started[t];
    }
}

__global__ void scanBlockCarries(const int* blockValue, const int* blockStarted, int blocks,
                                 int* carryIn) {
    __shared__ int value[kRouteCarryBlock];
    __shared__ int started[kRouteCarryBlock];
    __shared__ int runningValue;
    const int t = static_cast<int>(threadIdx.x);
    if (t == 0) {
        runningValue = 0;
    }
    __syncthreads();

    for (int chunk = 0; chunk < blocks; chunk += kRouteCarryBlock) {
        const int b = chunk + t;
        value[t] = b < blocks ? blockValue[b] : 0;
        started[t] = b < blocks ? blockStarted[b] : 0;
        __syncthreads();
        for (int offset = 1; offset < kRouteCarryBlock; offset *= 2) {
            int previousValue = 0;
            int previousStarted = 0;
            if (t >= offset) {
                previousValue = value[t - offset];
                previousStarted = started[t - offset];
            }
            __syncthreads();
            if (t >= offset) {
                if (started[t] == 0) {
                    value[t] += previousValue;
                }
                started[t] |= previousStarted;
            }
            __syncthreads();
        }

        int exclusiveValue = runningValue;
        if (t > 0) {
            exclusiveValue = started[t - 1] != 0 ? value[t - 1] : runningValue + value[t - 1];
        }
        if (b < blocks) {
            carryIn[b] = exclusiveValue;
        }
        __syncthreads();
        if (t == kRouteCarryBlock - 1) {
            runningValue = started[t] != 0 ? value[t] : runningValue + value[t];
        }
        __syncthreads();
    }
}

__global__ void applyCarriesAndCountLate(int* arrival, const unsigned char* startSeen,
                                         const int* carryIn, const int* deadlineMs, int n,
                                         int* late) {
    __shared__ int blockLate;
    const int t = static_cast<int>(threadIdx.x);
    const int i = static_cast<int>(blockIdx.x) * kRouteScanBlock + t;
    if (t == 0) {
        blockLate = 0;
    }
    __syncthreads();
    if (i < n) {
        const int value = arrival[i] + (startSeen[i] != 0 ? 0 : carryIn[blockIdx.x]);
        arrival[i] = value;
        if (deadlineMs[i] != kNoDeadline && value > deadlineMs[i]) {
            atomicAdd(&blockLate, 1);
        }
    }
    __syncthreads();
    if (t == 0 && blockLate != 0) {
        atomicAdd(late, blockLate);
    }
}
}

RouteArrivals routeArrivalsCpu(const std::vector<int>& legMs, const std::vector<unsigned char>& routeStart,
                               const std::vector<int>& deadlineMs, unsigned threads) {
    validate(legMs, routeStart, deadlineMs);
    const std::size_t n = legMs.size();
    RouteArrivals result;
    result.arrivalMs.assign(n, 0);
    if (n == 0) {
        return result;
    }

    const unsigned workers = std::max(1u, std::min<unsigned>(threads, static_cast<unsigned>(n)));
    std::vector<std::size_t> bounds(workers + 1, n);
    bounds[0] = 0;
    for (unsigned w = 1; w < workers; ++w) {
        std::size_t start = std::max(bounds[w - 1], n * w / workers);
        while (start < n && routeStart[start] == 0) {
            ++start;
        }
        bounds[w] = start;
    }

    std::vector<int> lateCounts(workers, 0);
    auto work = [&](unsigned w) {
        int running = 0;
        for (std::size_t i = bounds[w]; i < bounds[w + 1]; ++i) {
            running = routeStart[i] != 0 ? legMs[i] : running + legMs[i];
            result.arrivalMs[i] = running;
            if (deadlineMs[i] != kNoDeadline && running > deadlineMs[i]) {
                ++lateCounts[w];
            }
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
    for (int count : lateCounts) {
        result.late += count;
    }
    return result;
}

RouteArrivals routeArrivals(const std::vector<int>& legMs, const std::vector<unsigned char>& routeStart,
                            const std::vector<int>& deadlineMs, KernelTiming* timing) {
    validate(legMs, routeStart, deadlineMs);
    const int n = static_cast<int>(legMs.size());
    RouteArrivals result;
    result.arrivalMs.assign(legMs.size(), 0);
    if (n == 0) {
        return result;
    }
    const int blocks = (n + kRouteScanBlock - 1) / kRouteScanBlock;
    const std::size_t intBytes = legMs.size() * sizeof(int);

    int* dLeg = nullptr;
    unsigned char* dStart = nullptr;
    int* dDeadline = nullptr;
    int* dArrival = nullptr;
    unsigned char* dSeen = nullptr;
    int* dBlockValue = nullptr;
    int* dBlockStarted = nullptr;
    int* dCarry = nullptr;
    int* dLate = nullptr;
    CHECK(cudaMalloc(&dLeg, intBytes));
    CHECK(cudaMalloc(&dStart, legMs.size()));
    CHECK(cudaMalloc(&dDeadline, intBytes));
    CHECK(cudaMalloc(&dArrival, intBytes));
    CHECK(cudaMalloc(&dSeen, legMs.size()));
    CHECK(cudaMalloc(&dBlockValue, blocks * sizeof(int)));
    CHECK(cudaMalloc(&dBlockStarted, blocks * sizeof(int)));
    CHECK(cudaMalloc(&dCarry, blocks * sizeof(int)));
    CHECK(cudaMalloc(&dLate, sizeof(int)));

    EventTimer copyTimer;
    EventTimer kernelTimer;
    copyTimer.start();
    CHECK(cudaMemcpy(dLeg, legMs.data(), intBytes, cudaMemcpyHostToDevice));
    CHECK(cudaMemcpy(dStart, routeStart.data(), legMs.size(), cudaMemcpyHostToDevice));
    CHECK(cudaMemcpy(dDeadline, deadlineMs.data(), intBytes, cudaMemcpyHostToDevice));
    CHECK(cudaMemset(dLate, 0, sizeof(int)));
    double copyMs = copyTimer.stopMs();

    kernelTimer.start();
    launch(segmentedScanBlocks, dim3(blocks), dim3(kRouteScanBlock),
           static_cast<const int*>(dLeg), static_cast<const unsigned char*>(dStart), n, dArrival,
           dSeen, dBlockValue, dBlockStarted);
    launch(scanBlockCarries, dim3(1), dim3(kRouteCarryBlock),
           static_cast<const int*>(dBlockValue), static_cast<const int*>(dBlockStarted), blocks,
           dCarry);
    launch(applyCarriesAndCountLate, dim3(blocks), dim3(kRouteScanBlock), dArrival,
           static_cast<const unsigned char*>(dSeen), static_cast<const int*>(dCarry),
           static_cast<const int*>(dDeadline), n, dLate);
    CHECK(cudaGetLastError());
    const double kernelMs = kernelTimer.stopMs();

    copyTimer.start();
    CHECK(cudaMemcpy(result.arrivalMs.data(), dArrival, intBytes, cudaMemcpyDeviceToHost));
    CHECK(cudaMemcpy(&result.late, dLate, sizeof(int), cudaMemcpyDeviceToHost));
    copyMs += copyTimer.stopMs();
    CHECK(cudaFree(dLeg));
    CHECK(cudaFree(dStart));
    CHECK(cudaFree(dDeadline));
    CHECK(cudaFree(dArrival));
    CHECK(cudaFree(dSeen));
    CHECK(cudaFree(dBlockValue));
    CHECK(cudaFree(dBlockStarted));
    CHECK(cudaFree(dCarry));
    CHECK(cudaFree(dLate));
    if (timing != nullptr) {
        timing->kernelMs = kernelMs;
        timing->copyMs = copyMs;
    }
    return result;
}
}
