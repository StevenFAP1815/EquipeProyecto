#include <delivery/bench/Applications.hpp>

#include <delivery/bench/Stopwatch.hpp>
#include <delivery/core/CandidateFilter.hpp>
#include <delivery/core/CsrGraph.hpp>
#include <delivery/core/TravelTable.hpp>
#include <delivery/gpu/CandidateFilter.hpp>
#include <delivery/gpu/FloydWarshall.hpp>

#include <memory>
#include <random>
#include <string>
#include <thread>

namespace delivery::bench {
namespace {
constexpr std::uint64_t kBenchSeed = 2026;

unsigned hardwareThreads() {
    const unsigned hw = std::thread::hardware_concurrency();
    return hw == 0 ? 1 : hw;
}

BenchCase travelTableCase() {
    static const std::vector<int> sides = {6, 12, 24, 32, 48, 64};
    BenchCase c;
    c.application = "travel_table";
    for (int side : sides) {
        c.sizes.push_back(std::to_string(side * side));
    }
    c.prepare = [](std::size_t sizeIndex) {
        auto graph = std::make_shared<core::CsrGraph>(
            core::CsrGraph::syntheticGrid(sides[sizeIndex], kBenchSeed));
        auto reference = std::make_shared<std::vector<std::int32_t>>(
            core::allPairsDijkstra(*graph, 1).data());

        std::vector<Version> versions;
        for (unsigned threads : threadSweep(hardwareThreads())) {
            versions.push_back({"cpu_t" + std::to_string(threads), [graph, reference, threads] {
                                    Stopwatch watch;
                                    const auto table = core::allPairsDijkstra(*graph, threads);
                                    const double ms = watch.elapsedMs();
                                    return Measurement{ms, table.data() == *reference};
                                }});
        }
#ifdef DELIVERY_USE_CUDA
        versions.push_back({"gpu_kernel", [graph, reference] {
                                const auto adjacency = core::denseAdjacency(*graph);
                                gpu::KernelTiming timing;
                                const auto table =
                                    gpu::floydWarshallTiled(graph->n, adjacency, &timing);
                                return Measurement{timing.kernelMs, table == *reference};
                            }});
        versions.push_back({"gpu_e2e", [graph, reference] {
                                Stopwatch watch;
                                const auto adjacency = core::denseAdjacency(*graph);
                                const auto table = gpu::floydWarshallTiled(graph->n, adjacency);
                                const double ms = watch.elapsedMs();
                                return Measurement{ms, table == *reference};
                            }});
#endif
        return versions;
    };
    return c;
}

struct FleetPositions {
    std::vector<std::int32_t> xDm;
    std::vector<std::int32_t> yDm;
    std::vector<std::uint8_t> available;
};

FleetPositions randomFleet(int couriers, std::uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<std::int32_t> coordinate(-50000, 50000);
    std::bernoulli_distribution free(0.7);
    FleetPositions fleet;
    for (int i = 0; i < couriers; ++i) {
        fleet.xDm.push_back(coordinate(rng));
        fleet.yDm.push_back(coordinate(rng));
        fleet.available.push_back(free(rng) ? 1 : 0);
    }
    return fleet;
}

BenchCase candidateFilterCase() {
    static const std::vector<int> fleetSizes = {1000, 4000, 16000, 64000, 256000, 1000000};
    constexpr std::int32_t kRadiusDm = 20000;
    BenchCase c;
    c.application = "candidate_filter";
    for (int size : fleetSizes) {
        c.sizes.push_back(std::to_string(size));
    }
    c.prepare = [](std::size_t sizeIndex) {
        auto fleet = std::make_shared<FleetPositions>(randomFleet(fleetSizes[sizeIndex], kBenchSeed));
        auto reference = std::make_shared<std::vector<int>>(core::filterWithinRadiusCpu(
            fleet->xDm, fleet->yDm, fleet->available, 0, 0, kRadiusDm, 1));

        std::vector<Version> versions;
        for (unsigned threads : threadSweep(hardwareThreads())) {
            versions.push_back({"cpu_t" + std::to_string(threads), [fleet, reference, threads] {
                                    Stopwatch watch;
                                    const auto inside = core::filterWithinRadiusCpu(
                                        fleet->xDm, fleet->yDm, fleet->available, 0, 0, kRadiusDm,
                                        threads);
                                    const double ms = watch.elapsedMs();
                                    return Measurement{ms, inside == *reference};
                                }});
        }
#ifdef DELIVERY_USE_CUDA
        versions.push_back({"gpu_kernel", [fleet, reference] {
                                gpu::KernelTiming timing;
                                const auto inside = gpu::filterWithinRadius(
                                    fleet->xDm, fleet->yDm, fleet->available, 0, 0, kRadiusDm,
                                    &timing);
                                return Measurement{timing.kernelMs, inside == *reference};
                            }});
        versions.push_back({"gpu_e2e", [fleet, reference] {
                                Stopwatch watch;
                                const auto inside = gpu::filterWithinRadius(
                                    fleet->xDm, fleet->yDm, fleet->available, 0, 0, kRadiusDm);
                                const double ms = watch.elapsedMs();
                                return Measurement{ms, inside == *reference};
                            }});
#endif
        return versions;
    };
    return c;
}
}

std::vector<BenchCase> allBenchCases(const infra::Config&) {
    std::vector<BenchCase> cases;
    cases.push_back(travelTableCase());
    cases.push_back(candidateFilterCase());
    return cases;
}
}
