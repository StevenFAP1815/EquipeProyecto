#include <delivery/gpu/ResponseMap.hpp>

#include <delivery/gpu/EtaFormula.hpp>
#include <delivery/gpu/EventTimer.hpp>
#include <delivery/gpu/cuda_compat.h>

#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

namespace delivery::gpu {
namespace {
DELIVERY_HD inline int relax(int self, int north, int south, int east, int west, int cost) {
    int best = north < south ? north : south;
    best = best < east ? best : east;
    best = best < west ? best : west;
    if (best >= kResponseUnreachable) {
        return self;
    }
    const int through = best + cost;
    return through < self ? through : self;
}

__global__ void minPlusSweepTiled(const int* in, const int* cost, int width, int height, int* out) {
    __shared__ int tile[kResponseTile + 2][kResponseTile + 2];
    const int tx = static_cast<int>(threadIdx.x);
    const int ty = static_cast<int>(threadIdx.y);
    const int originX = static_cast<int>(blockIdx.x) * kResponseTile - 1;
    const int originY = static_cast<int>(blockIdx.y) * kResponseTile - 1;

    for (int dy = ty; dy < kResponseTile + 2; dy += kResponseTile) {
        for (int dx = tx; dx < kResponseTile + 2; dx += kResponseTile) {
            const int x = originX + dx;
            const int y = originY + dy;
            const bool inside = x >= 0 && x < width && y >= 0 && y < height;
            tile[dy][dx] = inside ? in[y * width + x] : kResponseUnreachable;
        }
    }
    __syncthreads();

    const int x = originX + 1 + tx;
    const int y = originY + 1 + ty;
    if (x < width && y < height) {
        const int idx = y * width + x;
        out[idx] = relax(tile[ty + 1][tx + 1], tile[ty][tx + 1], tile[ty + 2][tx + 1],
                         tile[ty + 1][tx + 2], tile[ty + 1][tx], cost[idx]);
    }
}

void validate(const std::vector<int>& initial, const std::vector<int>& cellCost, int width,
              int height, int sweeps) {
    const std::size_t cells = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    if (width <= 0 || height <= 0 || sweeps < 0 || initial.size() != cells ||
        cellCost.size() != cells) {
        throw std::invalid_argument("response map size does not match width*height");
    }
    for (int c : cellCost) {
        if (c <= 0) {
            throw std::invalid_argument("response map cell cost must be positive");
        }
    }
}
}

std::vector<int> responseMapCpu(const std::vector<int>& initial, const std::vector<int>& cellCost,
                                int width, int height, int sweeps, unsigned threads) {
    validate(initial, cellCost, width, height, sweeps);
    std::vector<int> buffers[2] = {initial, initial};
    const unsigned workers = std::max(1u, std::min<unsigned>(threads, static_cast<unsigned>(height)));
    const int rowsPerWorker = (height + static_cast<int>(workers) - 1) / static_cast<int>(workers);

    std::mutex mutex;
    std::condition_variable allArrived;
    unsigned arrived = 0;
    unsigned generation = 0;
    auto waitForEveryWorker = [&] {
        std::unique_lock<std::mutex> lock(mutex);
        const unsigned myGeneration = generation;
        if (++arrived == workers) {
            arrived = 0;
            ++generation;
            allArrived.notify_all();
        } else {
            allArrived.wait(lock, [&] { return generation != myGeneration; });
        }
    };

    auto work = [&](unsigned w) {
        const int firstRow = static_cast<int>(w) * rowsPerWorker;
        const int lastRow = std::min(height, firstRow + rowsPerWorker);
        for (int sweep = 0; sweep < sweeps; ++sweep) {
            const std::vector<int>& current = buffers[sweep % 2];
            std::vector<int>& next = buffers[(sweep + 1) % 2];
            for (int y = firstRow; y < lastRow; ++y) {
                for (int x = 0; x < width; ++x) {
                    const std::size_t idx = static_cast<std::size_t>(y) * width + x;
                    const int north = y > 0 ? current[idx - width] : kResponseUnreachable;
                    const int south = y + 1 < height ? current[idx + width] : kResponseUnreachable;
                    const int east = x + 1 < width ? current[idx + 1] : kResponseUnreachable;
                    const int west = x > 0 ? current[idx - 1] : kResponseUnreachable;
                    next[idx] = relax(current[idx], north, south, east, west, cellCost[idx]);
                }
            }
            if (workers > 1) {
                waitForEveryWorker();
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
    return buffers[sweeps % 2];
}

namespace {
struct DeviceMap {
    int* cost = nullptr;
    int* times = nullptr;
    int* scratch = nullptr;
    double copyMs = 0.0;
    double kernelMs = 0.0;
};

DeviceMap sweepOnDevice(const std::vector<int>& initial, const std::vector<int>& cellCost,
                        int width, int height, int sweeps) {
    const std::size_t bytes = initial.size() * sizeof(int);
    DeviceMap map;
    CHECK(cudaMalloc(&map.cost, bytes));
    CHECK(cudaMalloc(&map.times, bytes));
    CHECK(cudaMalloc(&map.scratch, bytes));

    EventTimer copyTimer;
    EventTimer kernelTimer;
    copyTimer.start();
    CHECK(cudaMemcpy(map.cost, cellCost.data(), bytes, cudaMemcpyHostToDevice));
    CHECK(cudaMemcpy(map.times, initial.data(), bytes, cudaMemcpyHostToDevice));
    map.copyMs = copyTimer.stopMs();

    const dim3 block(kResponseTile, kResponseTile);
    const dim3 grid((width + kResponseTile - 1) / kResponseTile,
                    (height + kResponseTile - 1) / kResponseTile);
    kernelTimer.start();
    for (int sweep = 0; sweep < sweeps; ++sweep) {
        launch(minPlusSweepTiled, grid, block, static_cast<const int*>(map.times),
               static_cast<const int*>(map.cost), width, height, map.scratch);
        std::swap(map.times, map.scratch);
    }
    CHECK(cudaGetLastError());
    map.kernelMs = kernelTimer.stopMs();
    return map;
}

void release(DeviceMap& map) {
    CHECK(cudaFree(map.cost));
    CHECK(cudaFree(map.times));
    CHECK(cudaFree(map.scratch));
}

DELIVERY_HD inline int responseBin(int seconds) {
    if (seconds >= kResponseUnreachable) {
        return kResponseBins;
    }
    const int bin = seconds / kResponseBinSeconds;
    return bin >= kResponseBins ? kResponseBins - 1 : bin;
}

constexpr int kResponseHistogramBlock = 256;
constexpr int kResponseHistogramGrid = 160;

__global__ void privatizedResponseHistogram(const int* times, const unsigned char* counted, int n,
                                            unsigned* bins) {
    __shared__ unsigned local[kResponseBins + 1];
    for (int b = static_cast<int>(threadIdx.x); b <= kResponseBins; b += kResponseHistogramBlock) {
        local[b] = 0;
    }
    __syncthreads();
    const int stride = static_cast<int>(gridDim.x * blockDim.x);
    for (int i = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x); i < n; i += stride) {
        if (counted[i] != 0) {
            atomicAdd(&local[responseBin(times[i])], 1u);
        }
    }
    __syncthreads();
    for (int b = static_cast<int>(threadIdx.x); b <= kResponseBins; b += kResponseHistogramBlock) {
        if (local[b] != 0) {
            atomicAdd(&bins[b], local[b]);
        }
    }
}

void validateMask(const std::vector<unsigned char>& counted, std::size_t cells) {
    if (counted.size() != cells) {
        throw std::invalid_argument("response histogram mask does not match width*height");
    }
}
}

std::vector<int> responseMap(const std::vector<int>& initial, const std::vector<int>& cellCost,
                             int width, int height, int sweeps, KernelTiming* timing) {
    validate(initial, cellCost, width, height, sweeps);
    std::vector<int> times(initial.size(), 0);
    DeviceMap map = sweepOnDevice(initial, cellCost, width, height, sweeps);
    const double kernelMs = map.kernelMs;
    double copyMs = map.copyMs;

    EventTimer copyTimer;
    copyTimer.start();
    CHECK(cudaMemcpy(times.data(), map.times, times.size() * sizeof(int), cudaMemcpyDeviceToHost));
    copyMs += copyTimer.stopMs();
    release(map);
    if (timing != nullptr) {
        timing->kernelMs = kernelMs;
        timing->copyMs = copyMs;
    }
    return times;
}

int worstServedCell(const std::vector<int>& times, const std::vector<bool>& counted) {
    int worst = -1;
    for (std::size_t i = 0; i < times.size() && i < counted.size(); ++i) {
        if (!counted[i] || times[i] >= kResponseUnreachable) {
            continue;
        }
        if (worst < 0 || times[i] > times[static_cast<std::size_t>(worst)]) {
            worst = static_cast<int>(i);
        }
    }
    return worst;
}

std::vector<unsigned> binResponseTimes(const std::vector<int>& times,
                                       const std::vector<unsigned char>& counted) {
    validateMask(counted, times.size());
    std::vector<unsigned> bins(kResponseBins + 1, 0);
    for (std::size_t i = 0; i < times.size(); ++i) {
        if (counted[i] != 0) {
            ++bins[static_cast<std::size_t>(responseBin(times[i]))];
        }
    }
    return bins;
}

std::vector<unsigned> responseHistogramCpu(const std::vector<int>& initial,
                                           const std::vector<int>& cellCost,
                                           const std::vector<unsigned char>& counted, int width,
                                           int height, int sweeps, unsigned threads) {
    validateMask(counted, initial.size());
    return binResponseTimes(responseMapCpu(initial, cellCost, width, height, sweeps, threads),
                            counted);
}

std::vector<unsigned> responseHistogram(const std::vector<int>& initial,
                                        const std::vector<int>& cellCost,
                                        const std::vector<unsigned char>& counted, int width,
                                        int height, int sweeps, KernelTiming* timing) {
    validate(initial, cellCost, width, height, sweeps);
    validateMask(counted, initial.size());
    const int n = static_cast<int>(initial.size());
    std::vector<unsigned> bins(kResponseBins + 1, 0);

    DeviceMap map = sweepOnDevice(initial, cellCost, width, height, sweeps);
    unsigned char* dCounted = nullptr;
    unsigned* dBins = nullptr;
    CHECK(cudaMalloc(&dCounted, initial.size()));
    CHECK(cudaMalloc(&dBins, bins.size() * sizeof(unsigned)));

    EventTimer copyTimer;
    EventTimer kernelTimer;
    copyTimer.start();
    CHECK(cudaMemcpy(dCounted, counted.data(), counted.size(), cudaMemcpyHostToDevice));
    CHECK(cudaMemset(dBins, 0, bins.size() * sizeof(unsigned)));
    double copyMs = map.copyMs + copyTimer.stopMs();

    const int blocks = std::max(
        1, std::min(kResponseHistogramGrid, (n + kResponseHistogramBlock - 1) / kResponseHistogramBlock));
    kernelTimer.start();
    launch(privatizedResponseHistogram, dim3(blocks), dim3(kResponseHistogramBlock),
           static_cast<const int*>(map.times), static_cast<const unsigned char*>(dCounted), n, dBins);
    CHECK(cudaGetLastError());
    const double kernelMs = map.kernelMs + kernelTimer.stopMs();

    copyTimer.start();
    CHECK(cudaMemcpy(bins.data(), dBins, bins.size() * sizeof(unsigned), cudaMemcpyDeviceToHost));
    copyMs += copyTimer.stopMs();
    CHECK(cudaFree(dCounted));
    CHECK(cudaFree(dBins));
    release(map);
    if (timing != nullptr) {
        timing->kernelMs = kernelMs;
        timing->copyMs = copyMs;
    }
    return bins;
}
}
