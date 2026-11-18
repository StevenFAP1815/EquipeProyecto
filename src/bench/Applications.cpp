#include <delivery/bench/Applications.hpp>

#include <delivery/bench/Stopwatch.hpp>
#include <delivery/core/CsrGraph.hpp>
#include <delivery/core/TravelTable.hpp>
#include <delivery/gpu/FloydWarshall.hpp>

#include <memory>
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
}

std::vector<BenchCase> allBenchCases(const infra::Config&) {
    std::vector<BenchCase> cases;
    cases.push_back(travelTableCase());
    return cases;
}
}
