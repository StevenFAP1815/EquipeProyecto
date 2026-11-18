#pragma once

#include <delivery/gpu/EtaFormula.hpp>
#include <delivery/gpu/KernelTiming.hpp>

#include <cstdint>
#include <vector>

namespace delivery::gpu {
struct EtaOrders {
    std::vector<int> restaurant;
    std::vector<int> delivery;
    std::vector<long long> readyAtMs;

    int size() const { return static_cast<int>(restaurant.size()); }
};

struct EtaCouriers {
    std::vector<int> node;
    std::vector<int> stops;
    std::vector<long long> latestReadyAtMs;
    std::vector<int> cmPerSecond;

    int size() const { return static_cast<int>(node.size()); }
    void add(int courierNode, const std::vector<int>& bagStops, long long latestReadyAt,
             int speedCmPerSecond);
    std::vector<int> stopsSoA() const;
};

std::vector<long long> etaMatrixCpu(const std::vector<std::int32_t>& table, int n,
                                    const EtaOrders& orders, const EtaCouriers& couriers,
                                    long long nowMs, unsigned threads);

class EtaEngine {
public:
    EtaEngine(int n, const std::vector<std::int32_t>& table);
    ~EtaEngine();

    EtaEngine(const EtaEngine&) = delete;
    EtaEngine& operator=(const EtaEngine&) = delete;

    std::vector<long long> etaMatrixSoA(const EtaOrders& orders, const EtaCouriers& couriers,
                                        long long nowMs, KernelTiming* timing = nullptr);
    std::vector<long long> etaMatrixAoS(const EtaOrders& orders, const EtaCouriers& couriers,
                                        long long nowMs, KernelTiming* timing = nullptr);

private:
    int n_;
    std::int32_t* deviceTable_ = nullptr;
};
}
