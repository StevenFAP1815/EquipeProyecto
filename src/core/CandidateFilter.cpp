#include <delivery/core/CandidateFilter.hpp>

#include <algorithm>
#include <thread>

namespace delivery::core {
namespace {
bool inside(std::int64_t x, std::int64_t y, std::int64_t cx, std::int64_t cy, std::int64_t r2) {
    const std::int64_t dx = x - cx;
    const std::int64_t dy = y - cy;
    return dx * dx + dy * dy <= r2;
}
}

std::vector<int> filterWithinRadiusCpu(const std::vector<std::int32_t>& xDm,
                                       const std::vector<std::int32_t>& yDm,
                                       const std::vector<std::uint8_t>& available,
                                       std::int32_t centerXDm, std::int32_t centerYDm,
                                       std::int32_t radiusDm, unsigned threads) {
    const std::size_t n = xDm.size();
    const std::int64_t r2 = static_cast<std::int64_t>(radiusDm) * radiusDm;
    const unsigned workers = std::max(1u, std::min<unsigned>(threads, static_cast<unsigned>(n)));
    const std::size_t chunk = (n + workers - 1) / std::max(1u, workers);

    std::vector<std::vector<int>> parts(workers);
    auto work = [&](unsigned w) {
        const std::size_t begin = w * chunk;
        const std::size_t end = std::min(n, begin + chunk);
        for (std::size_t i = begin; i < end; ++i) {
            if (available[i] != 0 && inside(xDm[i], yDm[i], centerXDm, centerYDm, r2)) {
                parts[w].push_back(static_cast<int>(i));
            }
        }
    };

    if (workers <= 1) {
        if (n > 0) {
            work(0);
        }
    } else {
        std::vector<std::thread> pool;
        for (unsigned w = 0; w < workers; ++w) {
            pool.emplace_back(work, w);
        }
        for (std::thread& t : pool) {
            t.join();
        }
    }

    std::vector<int> result;
    for (const auto& part : parts) {
        result.insert(result.end(), part.begin(), part.end());
    }
    return result;
}
}
