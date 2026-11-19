#include <delivery/gpu/DemandHeatmap.hpp>

#include <delivery/gpu/EventTimer.hpp>
#include <delivery/gpu/cuda_compat.h>

#include <algorithm>
#include <stdexcept>
#include <thread>

namespace delivery::gpu {
namespace {
constexpr int kHeatInputTile = kHeatTile + 2 * kHeatRadius;

__constant__ int heatFilter[kHeatWidth * kHeatWidth];

__global__ void smoothTiled(const int* counts, int width, int height, int* smoothed) {
    __shared__ int tile[kHeatInputTile][kHeatInputTile];
    const int tx = static_cast<int>(threadIdx.x);
    const int ty = static_cast<int>(threadIdx.y);
    const int originX = static_cast<int>(blockIdx.x) * kHeatTile - kHeatRadius;
    const int originY = static_cast<int>(blockIdx.y) * kHeatTile - kHeatRadius;

    for (int dy = ty; dy < kHeatInputTile; dy += kHeatTile) {
        for (int dx = tx; dx < kHeatInputTile; dx += kHeatTile) {
            const int x = originX + dx;
            const int y = originY + dy;
            const bool inside = x >= 0 && x < width && y >= 0 && y < height;
            tile[dy][dx] = inside ? counts[y * width + x] : 0;
        }
    }
    __syncthreads();

    const int outX = originX + kHeatRadius + tx;
    const int outY = originY + kHeatRadius + ty;
    if (outX < width && outY < height) {
        int sum = 0;
        for (int i = 0; i < kHeatWidth; ++i) {
            for (int j = 0; j < kHeatWidth; ++j) {
                sum += heatFilter[i * kHeatWidth + j] * tile[ty + i][tx + j];
            }
        }
        smoothed[outY * width + outX] = sum;
    }
}

void validate(const std::vector<int>& counts, int width, int height) {
    if (width <= 0 || height <= 0 ||
        counts.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height)) {
        throw std::invalid_argument("demand grid size does not match width*height");
    }
}
}

std::vector<int> heatFilterWeights() {
    const int binomial[kHeatWidth] = {1, 4, 6, 4, 1};
    std::vector<int> weights;
    for (int i = 0; i < kHeatWidth; ++i) {
        for (int j = 0; j < kHeatWidth; ++j) {
            weights.push_back(binomial[i] * binomial[j]);
        }
    }
    return weights;
}

std::vector<int> smoothDemandCpu(const std::vector<int>& counts, int width, int height,
                                 unsigned threads) {
    validate(counts, width, height);
    const std::vector<int> weights = heatFilterWeights();
    std::vector<int> smoothed(counts.size(), 0);
    const unsigned workers = std::max(1u, std::min<unsigned>(threads, static_cast<unsigned>(height)));
    auto work = [&](unsigned w) {
        for (int y = static_cast<int>(w); y < height; y += static_cast<int>(workers)) {
            for (int x = 0; x < width; ++x) {
                int sum = 0;
                for (int i = 0; i < kHeatWidth; ++i) {
                    for (int j = 0; j < kHeatWidth; ++j) {
                        const int sx = x + j - kHeatRadius;
                        const int sy = y + i - kHeatRadius;
                        if (sx >= 0 && sx < width && sy >= 0 && sy < height) {
                            sum += weights[static_cast<std::size_t>(i * kHeatWidth + j)] *
                                   counts[static_cast<std::size_t>(sy) * width + sx];
                        }
                    }
                }
                smoothed[static_cast<std::size_t>(y) * width + x] = sum;
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
    return smoothed;
}

std::vector<int> smoothDemand(const std::vector<int>& counts, int width, int height,
                              KernelTiming* timing) {
    validate(counts, width, height);
    const std::size_t bytes = counts.size() * sizeof(int);
    const std::vector<int> weights = heatFilterWeights();
    std::vector<int> smoothed(counts.size(), 0);

    int* dCounts = nullptr;
    int* dSmoothed = nullptr;
    CHECK(cudaMalloc(&dCounts, bytes));
    CHECK(cudaMalloc(&dSmoothed, bytes));

    EventTimer copyTimer;
    EventTimer kernelTimer;
    copyTimer.start();
    CHECK(cudaMemcpyToSymbol(heatFilter, weights.data(), weights.size() * sizeof(int)));
    CHECK(cudaMemcpy(dCounts, counts.data(), bytes, cudaMemcpyHostToDevice));
    double copyMs = copyTimer.stopMs();

    const dim3 block(kHeatTile, kHeatTile);
    const dim3 grid((width + kHeatTile - 1) / kHeatTile, (height + kHeatTile - 1) / kHeatTile);
    kernelTimer.start();
    launch(smoothTiled, grid, block, static_cast<const int*>(dCounts), width, height, dSmoothed);
    CHECK(cudaGetLastError());
    const double kernelMs = kernelTimer.stopMs();

    copyTimer.start();
    CHECK(cudaMemcpy(smoothed.data(), dSmoothed, bytes, cudaMemcpyDeviceToHost));
    copyMs += copyTimer.stopMs();
    CHECK(cudaFree(dCounts));
    CHECK(cudaFree(dSmoothed));
    if (timing != nullptr) {
        timing->kernelMs = kernelMs;
        timing->copyMs = copyMs;
    }
    return smoothed;
}
}
