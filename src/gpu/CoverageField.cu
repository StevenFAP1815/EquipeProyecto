#include <delivery/gpu/CoverageField.hpp>

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
DELIVERY_HD inline float jacobiUpdate(float north, float south, float east, float west,
                                      float source) {
    return ((((north + south) + east) + west) + source) * 0.25f;
}

DELIVERY_HD inline bool isBoundary(int x, int y, int width, int height) {
    return x == 0 || y == 0 || x == width - 1 || y == height - 1;
}

__global__ void jacobiSweepTiled(const float* in, const float* source, int width, int height,
                                 float* out) {
    __shared__ float tile[kStencilTile + 2][kStencilTile + 2];
    const int tx = static_cast<int>(threadIdx.x);
    const int ty = static_cast<int>(threadIdx.y);
    const int originX = static_cast<int>(blockIdx.x) * kStencilTile - 1;
    const int originY = static_cast<int>(blockIdx.y) * kStencilTile - 1;

    for (int dy = ty; dy < kStencilTile + 2; dy += kStencilTile) {
        for (int dx = tx; dx < kStencilTile + 2; dx += kStencilTile) {
            const int x = originX + dx;
            const int y = originY + dy;
            const bool inside = x >= 0 && x < width && y >= 0 && y < height;
            tile[dy][dx] = inside ? in[y * width + x] : 0.0f;
        }
    }
    __syncthreads();

    const int x = originX + 1 + tx;
    const int y = originY + 1 + ty;
    if (x < width && y < height) {
        const int idx = y * width + x;
        if (isBoundary(x, y, width, height)) {
            out[idx] = tile[ty + 1][tx + 1];
        } else {
            out[idx] = jacobiUpdate(tile[ty][tx + 1], tile[ty + 2][tx + 1], tile[ty + 1][tx + 2],
                                    tile[ty + 1][tx], source[idx]);
        }
    }
}

void validate(const std::vector<float>& density, int width, int height, int sweeps) {
    if (width <= 0 || height <= 0 || sweeps < 0 ||
        density.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height)) {
        throw std::invalid_argument("coverage grid size does not match width*height");
    }
}
}

std::vector<float> coverageFieldCpu(const std::vector<float>& density, int width, int height,
                                    int sweeps, unsigned threads) {
    validate(density, width, height, sweeps);
    std::vector<float> buffers[2] = {std::vector<float>(density.size(), 0.0f),
                                     std::vector<float>(density.size(), 0.0f)};
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
            const std::vector<float>& current = buffers[sweep % 2];
            std::vector<float>& next = buffers[(sweep + 1) % 2];
            for (int y = firstRow; y < lastRow; ++y) {
                for (int x = 0; x < width; ++x) {
                    const std::size_t idx = static_cast<std::size_t>(y) * width + x;
                    if (isBoundary(x, y, width, height)) {
                        next[idx] = current[idx];
                    } else {
                        next[idx] = jacobiUpdate(current[idx - width], current[idx + width],
                                                 current[idx + 1], current[idx - 1], density[idx]);
                    }
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

std::vector<float> coverageField(const std::vector<float>& density, int width, int height,
                                 int sweeps, KernelTiming* timing) {
    validate(density, width, height, sweeps);
    const std::size_t bytes = density.size() * sizeof(float);
    std::vector<float> field(density.size(), 0.0f);

    float* dSource = nullptr;
    float* dA = nullptr;
    float* dB = nullptr;
    CHECK(cudaMalloc(&dSource, bytes));
    CHECK(cudaMalloc(&dA, bytes));
    CHECK(cudaMalloc(&dB, bytes));

    EventTimer copyTimer;
    EventTimer kernelTimer;
    copyTimer.start();
    CHECK(cudaMemcpy(dSource, density.data(), bytes, cudaMemcpyHostToDevice));
    CHECK(cudaMemset(dA, 0, bytes));
    double copyMs = copyTimer.stopMs();

    const dim3 block(kStencilTile, kStencilTile);
    const dim3 grid((width + kStencilTile - 1) / kStencilTile,
                    (height + kStencilTile - 1) / kStencilTile);
    kernelTimer.start();
    for (int sweep = 0; sweep < sweeps; ++sweep) {
        launch(jacobiSweepTiled, grid, block, static_cast<const float*>(dA),
               static_cast<const float*>(dSource), width, height, dB);
        std::swap(dA, dB);
    }
    CHECK(cudaGetLastError());
    const double kernelMs = kernelTimer.stopMs();

    copyTimer.start();
    CHECK(cudaMemcpy(field.data(), dA, bytes, cudaMemcpyDeviceToHost));
    copyMs += copyTimer.stopMs();
    CHECK(cudaFree(dSource));
    CHECK(cudaFree(dA));
    CHECK(cudaFree(dB));
    if (timing != nullptr) {
        timing->kernelMs = kernelMs;
        timing->copyMs = copyMs;
    }
    return field;
}

int leastCoveredInteriorCell(const std::vector<float>& field, int width, int height) {
    int best = -1;
    for (int y = 1; y + 1 < height; ++y) {
        for (int x = 1; x + 1 < width; ++x) {
            const int idx = y * width + x;
            if (best < 0 || field[static_cast<std::size_t>(idx)] <
                                field[static_cast<std::size_t>(best)]) {
                best = idx;
            }
        }
    }
    return best;
}
}
