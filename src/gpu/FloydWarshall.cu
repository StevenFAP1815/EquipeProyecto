#include <delivery/gpu/FloydWarshall.hpp>

#include <delivery/gpu/EventTimer.hpp>
#include <delivery/gpu/cuda_compat.h>

#include <stdexcept>

namespace delivery::gpu {
namespace {
__device__ inline int tileIndex(int tileRow, int tileCol, int npad) {
    return (tileRow * kFwTile + static_cast<int>(threadIdx.y)) * npad + tileCol * kFwTile +
           static_cast<int>(threadIdx.x);
}

__global__ void fwDependentPhase(std::int32_t* d, int npad, int k) {
    __shared__ std::int32_t pivot[kFwTile][kFwTile];
    const int tx = static_cast<int>(threadIdx.x);
    const int ty = static_cast<int>(threadIdx.y);
    const int idx = tileIndex(k, k, npad);

    pivot[ty][tx] = d[idx];
    __syncthreads();
    for (int t = 0; t < kFwTile; ++t) {
        const std::int32_t candidate = pivot[ty][t] + pivot[t][tx];
        __syncthreads();
        if (candidate < pivot[ty][tx]) {
            pivot[ty][tx] = candidate;
        }
        __syncthreads();
    }
    d[idx] = pivot[ty][tx];
}

__global__ void fwPivotRowColumnPhase(std::int32_t* d, int npad, int k) {
    __shared__ std::int32_t pivot[kFwTile][kFwTile];
    __shared__ std::int32_t own[kFwTile][kFwTile];
    const int tile = static_cast<int>(blockIdx.x);
    if (tile == k) {
        return;
    }
    const bool pivotRow = blockIdx.y == 0;
    const int tx = static_cast<int>(threadIdx.x);
    const int ty = static_cast<int>(threadIdx.y);
    const int idx = pivotRow ? tileIndex(k, tile, npad) : tileIndex(tile, k, npad);

    pivot[ty][tx] = d[tileIndex(k, k, npad)];
    own[ty][tx] = d[idx];
    __syncthreads();
    for (int t = 0; t < kFwTile; ++t) {
        const std::int32_t candidate =
            pivotRow ? pivot[ty][t] + own[t][tx] : own[ty][t] + pivot[t][tx];
        __syncthreads();
        if (candidate < own[ty][tx]) {
            own[ty][tx] = candidate;
        }
        __syncthreads();
    }
    d[idx] = own[ty][tx];
}

__global__ void fwIndependentPhase(std::int32_t* d, int npad, int k) {
    __shared__ std::int32_t rowTile[kFwTile][kFwTile];
    __shared__ std::int32_t columnTile[kFwTile][kFwTile];
    const int tileRow = static_cast<int>(blockIdx.y);
    const int tileCol = static_cast<int>(blockIdx.x);
    if (tileRow == k || tileCol == k) {
        return;
    }
    const int tx = static_cast<int>(threadIdx.x);
    const int ty = static_cast<int>(threadIdx.y);

    rowTile[ty][tx] = d[tileIndex(tileRow, k, npad)];
    columnTile[ty][tx] = d[tileIndex(k, tileCol, npad)];
    __syncthreads();

    const int idx = tileIndex(tileRow, tileCol, npad);
    std::int32_t best = d[idx];
    for (int t = 0; t < kFwTile; ++t) {
        const std::int32_t candidate = rowTile[ty][t] + columnTile[t][tx];
        if (candidate < best) {
            best = candidate;
        }
    }
    d[idx] = best;
}
}

std::vector<std::int32_t> floydWarshallTiled(int n, const std::vector<std::int32_t>& adjacency,
                                             KernelTiming* timing) {
    if (adjacency.size() != static_cast<std::size_t>(n) * static_cast<std::size_t>(n)) {
        throw std::invalid_argument("adjacency size does not match n*n");
    }
    const int tiles = (n + kFwTile - 1) / kFwTile;
    const int npad = tiles * kFwTile;
    const std::size_t padded = static_cast<std::size_t>(npad) * static_cast<std::size_t>(npad);

    std::vector<std::int32_t> host(padded, kFwInfinity);
    for (int i = 0; i < npad; ++i) {
        host[static_cast<std::size_t>(i) * npad + i] = 0;
    }
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) {
            host[static_cast<std::size_t>(i) * npad + j] =
                adjacency[static_cast<std::size_t>(i) * n + j];
        }
    }

    EventTimer copyTimer;
    EventTimer kernelTimer;
    std::int32_t* device = nullptr;
    CHECK(cudaMalloc(&device, padded * sizeof(std::int32_t)));

    copyTimer.start();
    CHECK(cudaMemcpy(device, host.data(), padded * sizeof(std::int32_t), cudaMemcpyHostToDevice));
    double copyMs = copyTimer.stopMs();

    const dim3 block(kFwTile, kFwTile);
    kernelTimer.start();
    for (int k = 0; k < tiles; ++k) {
        launch(fwDependentPhase, dim3(1), block, device, npad, k);
        launch(fwPivotRowColumnPhase, dim3(tiles, 2), block, device, npad, k);
        launch(fwIndependentPhase, dim3(tiles, tiles), block, device, npad, k);
    }
    CHECK(cudaGetLastError());
    const double kernelMs = kernelTimer.stopMs();

    copyTimer.start();
    CHECK(cudaMemcpy(host.data(), device, padded * sizeof(std::int32_t), cudaMemcpyDeviceToHost));
    copyMs += copyTimer.stopMs();
    CHECK(cudaFree(device));

    if (timing != nullptr) {
        timing->kernelMs = kernelMs;
        timing->copyMs = copyMs;
    }

    std::vector<std::int32_t> result(static_cast<std::size_t>(n) * static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) {
            result[static_cast<std::size_t>(i) * n + j] = host[static_cast<std::size_t>(i) * npad + j];
        }
    }
    return result;
}
}
