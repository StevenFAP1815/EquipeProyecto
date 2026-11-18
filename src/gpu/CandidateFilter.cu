#include <delivery/gpu/CandidateFilter.hpp>

#include <delivery/gpu/EventTimer.hpp>
#include <delivery/gpu/cuda_compat.h>

namespace delivery::gpu {
namespace {
__device__ inline bool insideRadius(const std::int32_t* x, const std::int32_t* y,
                                    const std::uint8_t* available, int i, std::int32_t cx,
                                    std::int32_t cy, long long r2) {
    if (available[i] == 0) {
        return false;
    }
    const long long dx = static_cast<long long>(x[i]) - cx;
    const long long dy = static_cast<long long>(y[i]) - cy;
    return dx * dx + dy * dy <= r2;
}

__global__ void countInsidePerBlock(const std::int32_t* x, const std::int32_t* y,
                                    const std::uint8_t* available, int n, std::int32_t cx,
                                    std::int32_t cy, long long r2, int* blockCounts) {
    __shared__ int count;
    if (threadIdx.x == 0) {
        count = 0;
    }
    __syncthreads();
    const int i = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
    if (i < n && insideRadius(x, y, available, i, cx, cy, r2)) {
        atomicAdd(&count, 1);
    }
    __syncthreads();
    if (threadIdx.x == 0) {
        blockCounts[blockIdx.x] = count;
    }
}

__global__ void exclusiveScanBlockCounts(int* blockCounts, int blocks, int* total) {
    if (threadIdx.x == 0 && blockIdx.x == 0) {
        int running = 0;
        for (int b = 0; b < blocks; ++b) {
            const int count = blockCounts[b];
            blockCounts[b] = running;
            running += count;
        }
        *total = running;
    }
}

__global__ void scatterInside(const std::int32_t* x, const std::int32_t* y,
                              const std::uint8_t* available, int n, std::int32_t cx,
                              std::int32_t cy, long long r2, const int* blockOffsets, int* out) {
    __shared__ int position[kFilterBlock];
    const int t = static_cast<int>(threadIdx.x);
    const int i = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
    const int keep = (i < n && insideRadius(x, y, available, i, cx, cy, r2)) ? 1 : 0;

    position[t] = keep;
    __syncthreads();
    for (int stride = 1; stride < kFilterBlock; stride *= 2) {
        const int left = t >= stride ? position[t - stride] : 0;
        __syncthreads();
        position[t] += left;
        __syncthreads();
    }
    if (keep) {
        out[blockOffsets[blockIdx.x] + position[t] - 1] = i;
    }
}
}

std::vector<int> filterWithinRadius(const std::vector<std::int32_t>& xDm,
                                    const std::vector<std::int32_t>& yDm,
                                    const std::vector<std::uint8_t>& available,
                                    std::int32_t centerXDm, std::int32_t centerYDm,
                                    std::int32_t radiusDm, KernelTiming* timing) {
    const int n = static_cast<int>(xDm.size());
    if (n == 0) {
        return {};
    }
    const int blocks = (n + kFilterBlock - 1) / kFilterBlock;
    const long long r2 = static_cast<long long>(radiusDm) * radiusDm;

    std::int32_t* dX = nullptr;
    std::int32_t* dY = nullptr;
    std::uint8_t* dAvailable = nullptr;
    int* dBlockCounts = nullptr;
    int* dTotal = nullptr;
    int* dOut = nullptr;
    CHECK(cudaMalloc(&dX, n * sizeof(std::int32_t)));
    CHECK(cudaMalloc(&dY, n * sizeof(std::int32_t)));
    CHECK(cudaMalloc(&dAvailable, n * sizeof(std::uint8_t)));
    CHECK(cudaMalloc(&dBlockCounts, blocks * sizeof(int)));
    CHECK(cudaMalloc(&dTotal, sizeof(int)));
    CHECK(cudaMalloc(&dOut, n * sizeof(int)));

    EventTimer copyTimer;
    EventTimer kernelTimer;
    copyTimer.start();
    CHECK(cudaMemcpy(dX, xDm.data(), n * sizeof(std::int32_t), cudaMemcpyHostToDevice));
    CHECK(cudaMemcpy(dY, yDm.data(), n * sizeof(std::int32_t), cudaMemcpyHostToDevice));
    CHECK(cudaMemcpy(dAvailable, available.data(), n * sizeof(std::uint8_t),
                     cudaMemcpyHostToDevice));
    double copyMs = copyTimer.stopMs();

    kernelTimer.start();
    launch(countInsidePerBlock, dim3(blocks), dim3(kFilterBlock), dX, dY, dAvailable, n,
           centerXDm, centerYDm, r2, dBlockCounts);
    launch(exclusiveScanBlockCounts, dim3(1), dim3(1), dBlockCounts, blocks, dTotal);
    launch(scatterInside, dim3(blocks), dim3(kFilterBlock), dX, dY, dAvailable, n, centerXDm,
           centerYDm, r2, dBlockCounts, dOut);
    CHECK(cudaGetLastError());
    const double kernelMs = kernelTimer.stopMs();

    copyTimer.start();
    int total = 0;
    CHECK(cudaMemcpy(&total, dTotal, sizeof(int), cudaMemcpyDeviceToHost));
    std::vector<int> result(static_cast<std::size_t>(total));
    if (total > 0) {
        CHECK(cudaMemcpy(result.data(), dOut, total * sizeof(int), cudaMemcpyDeviceToHost));
    }
    copyMs += copyTimer.stopMs();

    CHECK(cudaFree(dX));
    CHECK(cudaFree(dY));
    CHECK(cudaFree(dAvailable));
    CHECK(cudaFree(dBlockCounts));
    CHECK(cudaFree(dTotal));
    CHECK(cudaFree(dOut));

    if (timing != nullptr) {
        timing->kernelMs = kernelMs;
        timing->copyMs = copyMs;
    }
    return result;
}
}
