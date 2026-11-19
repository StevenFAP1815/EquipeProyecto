#include <delivery/gpu/SegmentMerge.hpp>

#include <delivery/gpu/EtaFormula.hpp>
#include <delivery/gpu/EventTimer.hpp>
#include <delivery/gpu/cuda_compat.h>

#include <algorithm>
#include <numeric>
#include <stdexcept>
#include <thread>
#include <utility>

namespace delivery::gpu {
namespace {
DELIVERY_HD inline int coRank(int k, const long long* a, int m, const long long* b, int n) {
    int i = k < m ? k : m;
    int j = k - i;
    int iLow = k - n > 0 ? k - n : 0;
    int jLow = k - m > 0 ? k - m : 0;
    while (true) {
        if (i > 0 && j < n && a[i - 1] > b[j]) {
            const int delta = (i - iLow + 1) / 2;
            jLow = j;
            j += delta;
            i -= delta;
        } else if (j > 0 && i < m && b[j - 1] >= a[i]) {
            const int delta = (j - jLow + 1) / 2;
            iLow = i;
            i += delta;
            j -= delta;
        } else {
            return i;
        }
    }
}

DELIVERY_HD inline int pairContaining(const int* offsets, int segments, int k) {
    int low = 0;
    int high = (segments + 1) / 2 - 1;
    while (low < high) {
        const int mid = (low + high + 1) / 2;
        if (offsets[2 * mid] <= k) {
            low = mid;
        } else {
            high = mid - 1;
        }
    }
    return low;
}

__global__ void mergePairsRound(const long long* inT, const int* inSource, const int* offsets,
                                int segments, int total, long long* outT, int* outSource) {
    const int first = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x) * kMergeTile;
    const int last = first + kMergeTile < total ? first + kMergeTile : total;
    int k = first;
    while (k < last) {
        const int p = pairContaining(offsets, segments, k);
        const int aBegin = offsets[2 * p];
        const int aEnd = offsets[2 * p + 1 <= segments ? 2 * p + 1 : segments];
        const int bEnd = offsets[2 * p + 2 <= segments ? 2 * p + 2 : segments];
        const int m = aEnd - aBegin;
        const int n = bEnd - aEnd;
        const int localStart = k - aBegin;
        const int localStop = (last < bEnd ? last : bEnd) - aBegin;
        int i = coRank(localStart, inT + aBegin, m, inT + aEnd, n);
        int j = localStart - i;
        for (int out = localStart; out < localStop; ++out) {
            const bool takeA = j >= n || (i < m && inT[aBegin + i] <= inT[aEnd + j]);
            const int from = takeA ? aBegin + i : aEnd + j;
            outT[aBegin + out] = inT[from];
            outSource[aBegin + out] = inSource[from];
            if (takeA) {
                ++i;
            } else {
                ++j;
            }
        }
        k = aBegin + localStop;
    }
}

std::vector<int> nextOffsets(const std::vector<int>& offsets) {
    const int segments = static_cast<int>(offsets.size()) - 1;
    std::vector<int> next;
    for (int s = 0; s <= segments; s += 2) {
        next.push_back(offsets[static_cast<std::size_t>(s)]);
    }
    if (next.back() != offsets.back()) {
        next.push_back(offsets.back());
    }
    return next;
}

void validate(const Timeline& segments, const std::vector<int>& offsets) {
    if (segments.t.size() != segments.source.size() || offsets.empty() || offsets.front() != 0 ||
        offsets.back() != static_cast<int>(segments.t.size()) ||
        !std::is_sorted(offsets.begin(), offsets.end())) {
        throw std::invalid_argument("inconsistent merge segments");
    }
}
}

Timeline mergeSegmentsReference(const Timeline& segments, const std::vector<int>& offsets) {
    validate(segments, offsets);
    std::vector<int> index(segments.t.size());
    std::iota(index.begin(), index.end(), 0);
    std::stable_sort(index.begin(), index.end(), [&segments](int a, int b) {
        return segments.t[static_cast<std::size_t>(a)] < segments.t[static_cast<std::size_t>(b)];
    });
    Timeline merged;
    for (int i : index) {
        merged.t.push_back(segments.t[static_cast<std::size_t>(i)]);
        merged.source.push_back(segments.source[static_cast<std::size_t>(i)]);
    }
    return merged;
}

Timeline mergeSegmentsCpu(const Timeline& segments, const std::vector<int>& offsets,
                          unsigned threads) {
    validate(segments, offsets);
    std::vector<std::pair<long long, int>> current(segments.t.size());
    for (std::size_t i = 0; i < current.size(); ++i) {
        current[i] = {segments.t[i], segments.source[i]};
    }
    std::vector<std::pair<long long, int>> next(current.size());
    std::vector<int> bounds = offsets;
    const auto byTime = [](const auto& a, const auto& b) { return a.first < b.first; };

    while (bounds.size() > 2) {
        const int pairs = (static_cast<int>(bounds.size()) - 1 + 1) / 2;
        const unsigned workers = std::max(1u, std::min<unsigned>(threads, static_cast<unsigned>(pairs)));
        auto work = [&](unsigned w) {
            for (int p = static_cast<int>(w); p < pairs; p += static_cast<int>(workers)) {
                const auto a = static_cast<std::size_t>(bounds[static_cast<std::size_t>(2 * p)]);
                const auto mid = static_cast<std::size_t>(
                    bounds[std::min<std::size_t>(static_cast<std::size_t>(2 * p + 1), bounds.size() - 1)]);
                const auto end = static_cast<std::size_t>(
                    bounds[std::min<std::size_t>(static_cast<std::size_t>(2 * p + 2), bounds.size() - 1)]);
                std::merge(current.begin() + static_cast<std::ptrdiff_t>(a),
                           current.begin() + static_cast<std::ptrdiff_t>(mid),
                           current.begin() + static_cast<std::ptrdiff_t>(mid),
                           current.begin() + static_cast<std::ptrdiff_t>(end),
                           next.begin() + static_cast<std::ptrdiff_t>(a), byTime);
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
        current.swap(next);
        bounds = nextOffsets(bounds);
    }

    Timeline merged;
    merged.t.reserve(current.size());
    merged.source.reserve(current.size());
    for (const auto& [t, source] : current) {
        merged.t.push_back(t);
        merged.source.push_back(source);
    }
    return merged;
}

Timeline mergeSegments(const Timeline& segments, const std::vector<int>& offsets,
                       KernelTiming* timing) {
    validate(segments, offsets);
    const int total = static_cast<int>(segments.t.size());
    Timeline merged;
    merged.t.resize(segments.t.size());
    merged.source.resize(segments.source.size());
    if (total == 0) {
        return merged;
    }
    const int segmentsCount = static_cast<int>(offsets.size()) - 1;

    long long* dT[2] = {nullptr, nullptr};
    int* dSource[2] = {nullptr, nullptr};
    int* dOffsets = nullptr;
    for (int b = 0; b < 2; ++b) {
        CHECK(cudaMalloc(&dT[b], total * sizeof(long long)));
        CHECK(cudaMalloc(&dSource[b], total * sizeof(int)));
    }
    CHECK(cudaMalloc(&dOffsets, (segmentsCount + 1) * sizeof(int)));

    EventTimer copyTimer;
    EventTimer kernelTimer;
    copyTimer.start();
    CHECK(cudaMemcpy(dT[0], segments.t.data(), total * sizeof(long long), cudaMemcpyHostToDevice));
    CHECK(cudaMemcpy(dSource[0], segments.source.data(), total * sizeof(int),
                     cudaMemcpyHostToDevice));
    double copyMs = copyTimer.stopMs();

    const int threadsNeeded = (total + kMergeTile - 1) / kMergeTile;
    const int blocks = (threadsNeeded + kMergeBlock - 1) / kMergeBlock;
    std::vector<int> bounds = offsets;
    int current = 0;
    double kernelMs = 0.0;
    while (bounds.size() > 2) {
        const int count = static_cast<int>(bounds.size()) - 1;
        copyTimer.start();
        CHECK(cudaMemcpy(dOffsets, bounds.data(), bounds.size() * sizeof(int),
                         cudaMemcpyHostToDevice));
        copyMs += copyTimer.stopMs();

        kernelTimer.start();
        launch(mergePairsRound, dim3(blocks), dim3(kMergeBlock),
               static_cast<const long long*>(dT[current]),
               static_cast<const int*>(dSource[current]), static_cast<const int*>(dOffsets), count,
               total, dT[1 - current], dSource[1 - current]);
        CHECK(cudaGetLastError());
        kernelMs += kernelTimer.stopMs();
        current = 1 - current;
        bounds = nextOffsets(bounds);
    }

    copyTimer.start();
    CHECK(cudaMemcpy(merged.t.data(), dT[current], total * sizeof(long long),
                     cudaMemcpyDeviceToHost));
    CHECK(cudaMemcpy(merged.source.data(), dSource[current], total * sizeof(int),
                     cudaMemcpyDeviceToHost));
    copyMs += copyTimer.stopMs();

    for (int b = 0; b < 2; ++b) {
        CHECK(cudaFree(dT[b]));
        CHECK(cudaFree(dSource[b]));
    }
    CHECK(cudaFree(dOffsets));
    if (timing != nullptr) {
        timing->kernelMs = kernelMs;
        timing->copyMs = copyMs;
    }
    return merged;
}
}
