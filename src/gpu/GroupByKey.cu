#include <delivery/gpu/GroupByKey.hpp>

#include <delivery/gpu/EventTimer.hpp>
#include <delivery/gpu/cuda_compat.h>

#include <algorithm>
#include <stdexcept>
#include <thread>

namespace delivery::gpu {
namespace {
__global__ void countKeysPerBlock(const int* keys, int n, int keyCount, int blocks, int* matrix) {
    __shared__ int local[kMaxGroupKeys];
    for (int k = static_cast<int>(threadIdx.x); k < keyCount; k += kGroupBlock) {
        local[k] = 0;
    }
    __syncthreads();
    const int i = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
    if (i < n) {
        atomicAdd(&local[keys[i]], 1);
    }
    __syncthreads();
    for (int k = static_cast<int>(threadIdx.x); k < keyCount; k += kGroupBlock) {
        matrix[k * blocks + static_cast<int>(blockIdx.x)] = local[k];
    }
}

__global__ void exclusiveScanChunks(int* data, int m, int* chunkSums) {
    __shared__ int inclusive[kScanBlock];
    const int t = static_cast<int>(threadIdx.x);
    const int i = static_cast<int>(blockIdx.x) * kScanBlock + t;
    const int value = i < m ? data[i] : 0;

    inclusive[t] = value;
    __syncthreads();
    for (int stride = 1; stride < kScanBlock; stride *= 2) {
        const int left = t >= stride ? inclusive[t - stride] : 0;
        __syncthreads();
        inclusive[t] += left;
        __syncthreads();
    }
    if (i < m) {
        data[i] = inclusive[t] - value;
    }
    if (t == kScanBlock - 1 && chunkSums != nullptr) {
        chunkSums[blockIdx.x] = inclusive[t];
    }
}

__global__ void addChunkOffsets(int* data, int m, const int* chunkOffsets) {
    const int i = static_cast<int>(blockIdx.x) * kScanBlock + static_cast<int>(threadIdx.x);
    if (i < m) {
        data[i] += chunkOffsets[blockIdx.x];
    }
}

__global__ void scatterByKey(const int* keys, int n, int blocks, const int* startOfKeyInBlock,
                             int* order) {
    __shared__ int blockKeys[kGroupBlock];
    const int t = static_cast<int>(threadIdx.x);
    const int i = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
    blockKeys[t] = i < n ? keys[i] : -1;
    __syncthreads();
    if (i < n) {
        const int key = blockKeys[t];
        int rank = 0;
        for (int j = 0; j < t; ++j) {
            if (blockKeys[j] == key) {
                ++rank;
            }
        }
        order[startOfKeyInBlock[key * blocks + static_cast<int>(blockIdx.x)] + rank] = i;
    }
}

__global__ void extractKeyOffsets(const int* scanned, int blocks, int keyCount, int n,
                                  int* offsets) {
    const int k = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
    if (k < keyCount) {
        offsets[k] = scanned[k * blocks];
    }
    if (k == keyCount) {
        offsets[k] = n;
    }
}

void validateKeys(const std::vector<int>& keys, int keyCount) {
    if (keyCount <= 0 || keyCount > kMaxGroupKeys) {
        throw std::invalid_argument("key count out of range");
    }
    for (int k : keys) {
        if (k < 0 || k >= keyCount) {
            throw std::invalid_argument("key out of range");
        }
    }
}
}

Grouping groupByKeyCpu(const std::vector<int>& keys, int keyCount, unsigned threads) {
    validateKeys(keys, keyCount);
    const std::size_t n = keys.size();
    const unsigned workers = std::max(1u, std::min<unsigned>(threads, static_cast<unsigned>(
                                                                          std::max<std::size_t>(1, n))));
    const std::size_t chunk = (n + workers - 1) / workers;
    std::vector<std::vector<int>> counts(workers, std::vector<int>(static_cast<std::size_t>(keyCount), 0));

    auto runAll = [workers](auto&& job) {
        if (workers == 1) {
            job(0u);
            return;
        }
        std::vector<std::thread> pool;
        for (unsigned w = 0; w < workers; ++w) {
            pool.emplace_back(job, w);
        }
        for (std::thread& t : pool) {
            t.join();
        }
    };

    runAll([&](unsigned w) {
        const std::size_t end = std::min(n, (w + 1) * chunk);
        for (std::size_t i = w * chunk; i < end; ++i) {
            ++counts[w][static_cast<std::size_t>(keys[i])];
        }
    });

    Grouping g;
    g.offsets.assign(static_cast<std::size_t>(keyCount) + 1, 0);
    std::vector<std::vector<int>> start(workers, std::vector<int>(static_cast<std::size_t>(keyCount), 0));
    int running = 0;
    for (int k = 0; k < keyCount; ++k) {
        g.offsets[static_cast<std::size_t>(k)] = running;
        for (unsigned w = 0; w < workers; ++w) {
            start[w][static_cast<std::size_t>(k)] = running;
            running += counts[w][static_cast<std::size_t>(k)];
        }
    }
    g.offsets[static_cast<std::size_t>(keyCount)] = running;

    g.order.assign(n, 0);
    runAll([&](unsigned w) {
        std::vector<int>& next = start[w];
        const std::size_t end = std::min(n, (w + 1) * chunk);
        for (std::size_t i = w * chunk; i < end; ++i) {
            g.order[static_cast<std::size_t>(next[static_cast<std::size_t>(keys[i])]++)] =
                static_cast<int>(i);
        }
    });
    return g;
}

Grouping groupByKey(const std::vector<int>& keys, int keyCount, KernelTiming* timing) {
    validateKeys(keys, keyCount);
    const int n = static_cast<int>(keys.size());
    Grouping g;
    g.order.assign(keys.size(), 0);
    g.offsets.assign(static_cast<std::size_t>(keyCount) + 1, 0);
    if (n == 0) {
        return g;
    }
    const int blocks = (n + kGroupBlock - 1) / kGroupBlock;
    const int m = keyCount * blocks;
    const int chunks = (m + kScanBlock - 1) / kScanBlock;
    if (chunks > kScanBlock) {
        throw std::invalid_argument("too many keys x blocks for a two-level scan");
    }

    int* dKeys = nullptr;
    int* dMatrix = nullptr;
    int* dChunkSums = nullptr;
    int* dOrder = nullptr;
    int* dOffsets = nullptr;
    CHECK(cudaMalloc(&dKeys, n * sizeof(int)));
    CHECK(cudaMalloc(&dMatrix, m * sizeof(int)));
    CHECK(cudaMalloc(&dChunkSums, kScanBlock * sizeof(int)));
    CHECK(cudaMalloc(&dOrder, n * sizeof(int)));
    CHECK(cudaMalloc(&dOffsets, (keyCount + 1) * sizeof(int)));

    EventTimer copyTimer;
    EventTimer kernelTimer;
    copyTimer.start();
    CHECK(cudaMemcpy(dKeys, keys.data(), n * sizeof(int), cudaMemcpyHostToDevice));
    double copyMs = copyTimer.stopMs();

    kernelTimer.start();
    launch(countKeysPerBlock, dim3(blocks), dim3(kGroupBlock), static_cast<const int*>(dKeys), n,
           keyCount, blocks, dMatrix);
    launch(exclusiveScanChunks, dim3(chunks), dim3(kScanBlock), dMatrix, m, dChunkSums);
    launch(exclusiveScanChunks, dim3(1), dim3(kScanBlock), dChunkSums, chunks,
           static_cast<int*>(nullptr));
    launch(addChunkOffsets, dim3(chunks), dim3(kScanBlock), dMatrix, m,
           static_cast<const int*>(dChunkSums));
    launch(scatterByKey, dim3(blocks), dim3(kGroupBlock), static_cast<const int*>(dKeys), n,
           blocks, static_cast<const int*>(dMatrix), dOrder);
    launch(extractKeyOffsets, dim3((keyCount + kGroupBlock) / kGroupBlock), dim3(kGroupBlock),
           static_cast<const int*>(dMatrix), blocks, keyCount, n, dOffsets);
    CHECK(cudaGetLastError());
    const double kernelMs = kernelTimer.stopMs();

    copyTimer.start();
    CHECK(cudaMemcpy(g.order.data(), dOrder, n * sizeof(int), cudaMemcpyDeviceToHost));
    CHECK(cudaMemcpy(g.offsets.data(), dOffsets, (keyCount + 1) * sizeof(int),
                     cudaMemcpyDeviceToHost));
    copyMs += copyTimer.stopMs();

    CHECK(cudaFree(dKeys));
    CHECK(cudaFree(dMatrix));
    CHECK(cudaFree(dChunkSums));
    CHECK(cudaFree(dOrder));
    CHECK(cudaFree(dOffsets));
    if (timing != nullptr) {
        timing->kernelMs = kernelMs;
        timing->copyMs = copyMs;
    }
    return g;
}
}
