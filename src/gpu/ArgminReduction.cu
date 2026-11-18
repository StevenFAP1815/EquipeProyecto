#include <delivery/gpu/ArgminReduction.hpp>

#include <delivery/gpu/EtaFormula.hpp>
#include <delivery/gpu/EventTimer.hpp>
#include <delivery/gpu/cuda_compat.h>

#include <algorithm>
#include <stdexcept>
#include <thread>

namespace delivery::gpu {
namespace {
DELIVERY_HD inline bool isBetter(long long eta, int index, long long bestEta, int bestIndex) {
    if (index < 0) {
        return false;
    }
    if (bestIndex < 0) {
        return true;
    }
    return eta < bestEta || (eta == bestEta && index < bestIndex);
}

__global__ void rowArgminKernel(const long long* etas, int cols, BestCourier* out) {
    __shared__ long long bestEta[kArgminBlock];
    __shared__ int bestIndex[kArgminBlock];
    const int t = static_cast<int>(threadIdx.x);
    const long long* row = etas + static_cast<long long>(blockIdx.x) * cols;

    long long myEta = kNoEta;
    int myIndex = -1;
    for (int c = t; c < cols; c += kArgminBlock) {
        const long long eta = row[c];
        if (eta < myEta) {
            myEta = eta;
            myIndex = c;
        }
    }
    bestEta[t] = myEta;
    bestIndex[t] = myIndex;
    __syncthreads();

    for (int stride = kArgminBlock / 2; stride > 0; stride /= 2) {
        if (t < stride &&
            isBetter(bestEta[t + stride], bestIndex[t + stride], bestEta[t], bestIndex[t])) {
            bestEta[t] = bestEta[t + stride];
            bestIndex[t] = bestIndex[t + stride];
        }
        __syncthreads();
    }
    if (t == 0) {
        out[blockIdx.x] = BestCourier{bestIndex[0], bestEta[0]};
    }
}
}

std::vector<BestCourier> rowArgminCpu(const std::vector<long long>& etas, int rows, int cols,
                                      unsigned threads) {
    if (etas.size() != static_cast<std::size_t>(rows) * static_cast<std::size_t>(cols)) {
        throw std::invalid_argument("ETA matrix size does not match rows*cols");
    }
    std::vector<BestCourier> best(static_cast<std::size_t>(rows));
    const unsigned workers = std::max(1u, std::min<unsigned>(threads, std::max(1, rows)));
    auto work = [&](unsigned w) {
        for (int r = static_cast<int>(w); r < rows; r += static_cast<int>(workers)) {
            BestCourier b;
            const long long* row = etas.data() + static_cast<std::size_t>(r) * cols;
            for (int c = 0; c < cols; ++c) {
                if (row[c] < b.etaMs) {
                    b = BestCourier{c, row[c]};
                }
            }
            best[static_cast<std::size_t>(r)] = b;
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
    return best;
}

void launchRowArgmin(const long long* deviceEtas, int rows, int cols, BestCourier* deviceOut) {
    if (rows == 0) {
        return;
    }
    launch(rowArgminKernel, dim3(rows), dim3(kArgminBlock), deviceEtas, cols, deviceOut);
    CHECK(cudaGetLastError());
}

std::vector<BestCourier> rowArgmin(const std::vector<long long>& etas, int rows, int cols,
                                   KernelTiming* timing) {
    if (etas.size() != static_cast<std::size_t>(rows) * static_cast<std::size_t>(cols)) {
        throw std::invalid_argument("ETA matrix size does not match rows*cols");
    }
    std::vector<BestCourier> best(static_cast<std::size_t>(rows));
    if (rows == 0) {
        return best;
    }
    long long* dEtas = nullptr;
    BestCourier* dBest = nullptr;
    CHECK(cudaMalloc(&dEtas, std::max<std::size_t>(1, etas.size()) * sizeof(long long)));
    CHECK(cudaMalloc(&dBest, best.size() * sizeof(BestCourier)));

    EventTimer copyTimer;
    EventTimer kernelTimer;
    copyTimer.start();
    if (!etas.empty()) {
        CHECK(cudaMemcpy(dEtas, etas.data(), etas.size() * sizeof(long long),
                         cudaMemcpyHostToDevice));
    }
    double copyMs = copyTimer.stopMs();

    kernelTimer.start();
    launchRowArgmin(dEtas, rows, cols, dBest);
    const double kernelMs = kernelTimer.stopMs();

    copyTimer.start();
    CHECK(cudaMemcpy(best.data(), dBest, best.size() * sizeof(BestCourier),
                     cudaMemcpyDeviceToHost));
    copyMs += copyTimer.stopMs();

    CHECK(cudaFree(dEtas));
    CHECK(cudaFree(dBest));
    if (timing != nullptr) {
        timing->kernelMs = kernelMs;
        timing->copyMs = copyMs;
    }
    return best;
}
}
