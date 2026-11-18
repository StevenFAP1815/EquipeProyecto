#include <delivery/gpu/EtaBatch.hpp>

#include <delivery/gpu/EventTimer.hpp>
#include <delivery/gpu/cuda_compat.h>

#include <algorithm>
#include <stdexcept>
#include <thread>

namespace delivery::gpu {
namespace {
constexpr int kEtaBlock = 256;

struct CourierRecord {
    int node;
    int stops[kMaxBagStops];
    int cmPerSecond;
    long long latestReadyAtMs;
};

__global__ void etaKernelSoA(const std::int32_t* table, int n, const int* restaurant,
                             const int* delivery, const long long* readyAtMs, const int* node,
                             const int* stops, const long long* latestReadyAtMs,
                             const int* cmPerSecond, int couriers, long long nowMs,
                             long long* out) {
    const int c = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
    const int o = static_cast<int>(blockIdx.y);
    if (c >= couriers) {
        return;
    }
    out[static_cast<long long>(o) * couriers + c] =
        etaMs(table, n, node[c], stops + c, couriers, latestReadyAtMs[c], cmPerSecond[c],
              restaurant[o], delivery[o], readyAtMs[o], nowMs);
}

__global__ void etaKernelAoS(const std::int32_t* table, int n, const int* restaurant,
                             const int* delivery, const long long* readyAtMs,
                             const CourierRecord* records, int couriers, long long nowMs,
                             long long* out) {
    const int c = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
    const int o = static_cast<int>(blockIdx.y);
    if (c >= couriers) {
        return;
    }
    out[static_cast<long long>(o) * couriers + c] =
        etaMs(table, n, records[c].node, records[c].stops, 1, records[c].latestReadyAtMs,
              records[c].cmPerSecond, restaurant[o], delivery[o], readyAtMs[o], nowMs);
}

template <class T>
T* upload(const std::vector<T>& host) {
    T* device = nullptr;
    CHECK(cudaMalloc(&device, std::max<std::size_t>(1, host.size()) * sizeof(T)));
    if (!host.empty()) {
        CHECK(cudaMemcpy(device, host.data(), host.size() * sizeof(T), cudaMemcpyHostToDevice));
    }
    return device;
}

void validate(const EtaOrders& orders, const EtaCouriers& couriers) {
    if (orders.delivery.size() != orders.restaurant.size() ||
        orders.readyAtMs.size() != orders.restaurant.size() ||
        couriers.stops.size() != couriers.node.size() * kMaxBagStops ||
        couriers.latestReadyAtMs.size() != couriers.node.size() ||
        couriers.cmPerSecond.size() != couriers.node.size()) {
        throw std::invalid_argument("inconsistent ETA batch arrays");
    }
}
}

void EtaCouriers::add(int courierNode, const std::vector<int>& bagStops, long long latestReadyAt,
                      int speedCmPerSecond) {
    if (bagStops.size() > static_cast<std::size_t>(kMaxBagStops)) {
        throw std::invalid_argument("bag has more stops than the ETA batch supports");
    }
    node.push_back(courierNode);
    for (int k = 0; k < kMaxBagStops; ++k) {
        stops.push_back(k < static_cast<int>(bagStops.size()) ? bagStops[static_cast<std::size_t>(k)]
                                                             : -1);
    }
    latestReadyAtMs.push_back(latestReadyAt);
    cmPerSecond.push_back(speedCmPerSecond);
}

std::vector<int> EtaCouriers::stopsSoA() const {
    const int count = size();
    std::vector<int> soa(stops.size());
    for (int c = 0; c < count; ++c) {
        for (int k = 0; k < kMaxBagStops; ++k) {
            soa[static_cast<std::size_t>(k) * count + c] =
                stops[static_cast<std::size_t>(c) * kMaxBagStops + k];
        }
    }
    return soa;
}

std::vector<long long> etaMatrixCpu(const std::vector<std::int32_t>& table, int n,
                                    const EtaOrders& orders, const EtaCouriers& couriers,
                                    long long nowMs, unsigned threads) {
    validate(orders, couriers);
    const int rows = orders.size();
    const int cols = couriers.size();
    std::vector<long long> out(static_cast<std::size_t>(rows) * cols);
    const unsigned workers = std::max(1u, std::min<unsigned>(threads, std::max(1, rows)));

    auto work = [&](unsigned w) {
        for (int o = static_cast<int>(w); o < rows; o += static_cast<int>(workers)) {
            for (int c = 0; c < cols; ++c) {
                out[static_cast<std::size_t>(o) * cols + c] =
                    etaMs(table.data(), n, couriers.node[static_cast<std::size_t>(c)],
                          couriers.stops.data() + static_cast<std::size_t>(c) * kMaxBagStops, 1,
                          couriers.latestReadyAtMs[static_cast<std::size_t>(c)],
                          couriers.cmPerSecond[static_cast<std::size_t>(c)],
                          orders.restaurant[static_cast<std::size_t>(o)],
                          orders.delivery[static_cast<std::size_t>(o)],
                          orders.readyAtMs[static_cast<std::size_t>(o)], nowMs);
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
    return out;
}

EtaEngine::EtaEngine(int n, const std::vector<std::int32_t>& table) : n_(n) {
    if (table.size() != static_cast<std::size_t>(n) * static_cast<std::size_t>(n)) {
        throw std::invalid_argument("travel table size does not match n*n");
    }
    deviceTable_ = upload(table);
}

EtaEngine::~EtaEngine() {
    cudaFree(deviceTable_);
}

std::vector<long long> EtaEngine::etaMatrixSoA(const EtaOrders& orders,
                                               const EtaCouriers& couriers, long long nowMs,
                                               KernelTiming* timing) {
    validate(orders, couriers);
    const int rows = orders.size();
    const int cols = couriers.size();
    std::vector<long long> out(static_cast<std::size_t>(rows) * cols);
    if (rows == 0 || cols == 0) {
        return out;
    }

    EventTimer copyTimer;
    EventTimer kernelTimer;
    copyTimer.start();
    int* dRestaurant = upload(orders.restaurant);
    int* dDelivery = upload(orders.delivery);
    long long* dReady = upload(orders.readyAtMs);
    int* dNode = upload(couriers.node);
    int* dStops = upload(couriers.stopsSoA());
    long long* dLatest = upload(couriers.latestReadyAtMs);
    int* dSpeed = upload(couriers.cmPerSecond);
    long long* dOut = nullptr;
    CHECK(cudaMalloc(&dOut, out.size() * sizeof(long long)));
    double copyMs = copyTimer.stopMs();

    const dim3 grid((cols + kEtaBlock - 1) / kEtaBlock, rows);
    kernelTimer.start();
    launch(etaKernelSoA, grid, dim3(kEtaBlock), static_cast<const std::int32_t*>(deviceTable_),
           n_, static_cast<const int*>(dRestaurant), static_cast<const int*>(dDelivery),
           static_cast<const long long*>(dReady), static_cast<const int*>(dNode),
           static_cast<const int*>(dStops), static_cast<const long long*>(dLatest),
           static_cast<const int*>(dSpeed), cols, nowMs, dOut);
    CHECK(cudaGetLastError());
    const double kernelMs = kernelTimer.stopMs();

    copyTimer.start();
    CHECK(cudaMemcpy(out.data(), dOut, out.size() * sizeof(long long), cudaMemcpyDeviceToHost));
    copyMs += copyTimer.stopMs();

    CHECK(cudaFree(dRestaurant));
    CHECK(cudaFree(dDelivery));
    CHECK(cudaFree(dReady));
    CHECK(cudaFree(dNode));
    CHECK(cudaFree(dStops));
    CHECK(cudaFree(dLatest));
    CHECK(cudaFree(dSpeed));
    CHECK(cudaFree(dOut));
    if (timing != nullptr) {
        timing->kernelMs = kernelMs;
        timing->copyMs = copyMs;
    }
    return out;
}

std::vector<long long> EtaEngine::etaMatrixAoS(const EtaOrders& orders,
                                               const EtaCouriers& couriers, long long nowMs,
                                               KernelTiming* timing) {
    validate(orders, couriers);
    const int rows = orders.size();
    const int cols = couriers.size();
    std::vector<long long> out(static_cast<std::size_t>(rows) * cols);
    if (rows == 0 || cols == 0) {
        return out;
    }
    std::vector<CourierRecord> records(static_cast<std::size_t>(cols));
    for (int c = 0; c < cols; ++c) {
        CourierRecord& r = records[static_cast<std::size_t>(c)];
        r.node = couriers.node[static_cast<std::size_t>(c)];
        for (int k = 0; k < kMaxBagStops; ++k) {
            r.stops[k] = couriers.stops[static_cast<std::size_t>(c) * kMaxBagStops + k];
        }
        r.cmPerSecond = couriers.cmPerSecond[static_cast<std::size_t>(c)];
        r.latestReadyAtMs = couriers.latestReadyAtMs[static_cast<std::size_t>(c)];
    }

    EventTimer copyTimer;
    EventTimer kernelTimer;
    copyTimer.start();
    int* dRestaurant = upload(orders.restaurant);
    int* dDelivery = upload(orders.delivery);
    long long* dReady = upload(orders.readyAtMs);
    CourierRecord* dRecords = upload(records);
    long long* dOut = nullptr;
    CHECK(cudaMalloc(&dOut, out.size() * sizeof(long long)));
    double copyMs = copyTimer.stopMs();

    const dim3 grid((cols + kEtaBlock - 1) / kEtaBlock, rows);
    kernelTimer.start();
    launch(etaKernelAoS, grid, dim3(kEtaBlock), static_cast<const std::int32_t*>(deviceTable_),
           n_, static_cast<const int*>(dRestaurant), static_cast<const int*>(dDelivery),
           static_cast<const long long*>(dReady), static_cast<const CourierRecord*>(dRecords),
           cols, nowMs, dOut);
    CHECK(cudaGetLastError());
    const double kernelMs = kernelTimer.stopMs();

    copyTimer.start();
    CHECK(cudaMemcpy(out.data(), dOut, out.size() * sizeof(long long), cudaMemcpyDeviceToHost));
    copyMs += copyTimer.stopMs();

    CHECK(cudaFree(dRestaurant));
    CHECK(cudaFree(dDelivery));
    CHECK(cudaFree(dReady));
    CHECK(cudaFree(dRecords));
    CHECK(cudaFree(dOut));
    if (timing != nullptr) {
        timing->kernelMs = kernelMs;
        timing->copyMs = copyMs;
    }
    return out;
}
}
