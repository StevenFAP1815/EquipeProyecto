#include <delivery/bench/Applications.hpp>

#include <delivery/bench/Stopwatch.hpp>
#include <delivery/core/CandidateFilter.hpp>
#include <delivery/core/CsrGraph.hpp>
#include <delivery/core/TravelTable.hpp>
#include <delivery/gpu/CandidateFilter.hpp>
#include <delivery/gpu/DeliveryHistogram.hpp>
#include <delivery/gpu/EtaBatch.hpp>
#include <delivery/gpu/FloydWarshall.hpp>
#include <delivery/gpu/GroupByKey.hpp>
#include <delivery/gpu/RadixSort.hpp>
#include <delivery/gpu/SegmentMerge.hpp>

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

struct EtaWorkload {
    std::shared_ptr<const core::TravelTable> table;
    gpu::EtaOrders orders;
    gpu::EtaCouriers couriers;
};

std::shared_ptr<const core::TravelTable> benchCityTable() {
    static const auto table = std::make_shared<const core::TravelTable>(
        core::allPairsDijkstra(core::CsrGraph::syntheticGrid(64, kBenchSeed), hardwareThreads()));
    return table;
}

EtaWorkload randomEtaWorkload(int orders, int couriers, std::uint64_t seed) {
    EtaWorkload w;
    w.table = benchCityTable();
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<int> node(0, w.table->size() - 1);
    std::uniform_int_distribution<long long> readyAt(0, 240000);
    std::uniform_int_distribution<int> bagSize(0, 3);
    for (int o = 0; o < orders; ++o) {
        w.orders.restaurant.push_back(node(rng));
        w.orders.delivery.push_back(node(rng));
        w.orders.readyAtMs.push_back(readyAt(rng));
    }
    for (int c = 0; c < couriers; ++c) {
        std::vector<int> stops;
        const int bag = bagSize(rng);
        for (int k = 0; k < bag; ++k) {
            stops.push_back(node(rng));
        }
        w.couriers.add(node(rng), stops, readyAt(rng), 833);
    }
    return w;
}

BenchCase etaBatchCase() {
    static const std::vector<std::pair<int, int>> shapes = {
        {10, 50}, {50, 200}, {100, 500}, {200, 1000}, {500, 2000}, {1000, 5000}};
    BenchCase c;
    c.application = "eta_batch";
    for (const auto& [orders, couriers] : shapes) {
        c.sizes.push_back(std::to_string(orders) + "x" + std::to_string(couriers));
    }
    c.prepare = [](std::size_t sizeIndex) {
        const auto [rows, cols] = shapes[sizeIndex];
        auto work = std::make_shared<EtaWorkload>(randomEtaWorkload(rows, cols, kBenchSeed));
        auto reference = std::make_shared<std::vector<long long>>(gpu::etaMatrixCpu(
            work->table->data(), work->table->size(), work->orders, work->couriers, 0, 1));

        std::vector<Version> versions;
        for (unsigned threads : threadSweep(hardwareThreads())) {
            versions.push_back({"cpu_t" + std::to_string(threads), [work, reference, threads] {
                                    Stopwatch watch;
                                    const auto etas = gpu::etaMatrixCpu(
                                        work->table->data(), work->table->size(), work->orders,
                                        work->couriers, 0, threads);
                                    const double ms = watch.elapsedMs();
                                    return Measurement{ms, etas == *reference};
                                }});
        }
#ifdef DELIVERY_USE_CUDA
        auto engine = std::make_shared<gpu::EtaEngine>(work->table->size(), work->table->data());
        versions.push_back({"gpu_kernel_aos", [work, reference, engine] {
                                gpu::KernelTiming timing;
                                const auto etas =
                                    engine->etaMatrixAoS(work->orders, work->couriers, 0, &timing);
                                return Measurement{timing.kernelMs, etas == *reference};
                            }});
        versions.push_back({"gpu_kernel", [work, reference, engine] {
                                gpu::KernelTiming timing;
                                const auto etas =
                                    engine->etaMatrixSoA(work->orders, work->couriers, 0, &timing);
                                return Measurement{timing.kernelMs, etas == *reference};
                            }});
        versions.push_back({"gpu_e2e", [work, reference, engine] {
                                Stopwatch watch;
                                const auto etas =
                                    engine->etaMatrixSoA(work->orders, work->couriers, 0);
                                const double ms = watch.elapsedMs();
                                return Measurement{ms, etas == *reference};
                            }});
        auto bestReference = std::make_shared<std::vector<gpu::BestCourier>>(gpu::rowArgminCpu(
            *reference, work->orders.size(), work->couriers.size(), 1));
        versions.push_back({"gpu_e2e_best", [work, bestReference, engine] {
                                Stopwatch watch;
                                const auto best =
                                    engine->bestPerOrder(work->orders, work->couriers, 0);
                                const double ms = watch.elapsedMs();
                                return Measurement{ms, best == *bestReference};
                            }});
#endif
        return versions;
    };
    return c;
}

struct EtaMatrix {
    int rows = 0;
    int cols = 0;
    std::vector<long long> etas;
};

EtaMatrix randomEtaMatrix(int rows, int cols, std::uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<long long> eta(60000, 60999);
    std::bernoulli_distribution unreachable(0.05);
    EtaMatrix m{rows, cols, {}};
    m.etas.resize(static_cast<std::size_t>(rows) * cols);
    for (long long& value : m.etas) {
        value = unreachable(rng) ? gpu::kNoEta : eta(rng);
    }
    return m;
}

BenchCase dispatchArgminCase() {
    static const std::vector<std::pair<int, int>> shapes = {
        {10, 500}, {50, 2000}, {200, 5000}, {1000, 5000}, {1000, 20000}};
    BenchCase c;
    c.application = "dispatch_argmin";
    for (const auto& [rows, cols] : shapes) {
        c.sizes.push_back(std::to_string(rows) + "x" + std::to_string(cols));
    }
    c.prepare = [](std::size_t sizeIndex) {
        const auto [rows, cols] = shapes[sizeIndex];
        auto m = std::make_shared<EtaMatrix>(randomEtaMatrix(rows, cols, kBenchSeed));
        auto reference = std::make_shared<std::vector<gpu::BestCourier>>(
            gpu::rowArgminCpu(m->etas, m->rows, m->cols, 1));

        std::vector<Version> versions;
        for (unsigned threads : threadSweep(hardwareThreads())) {
            versions.push_back({"cpu_t" + std::to_string(threads), [m, reference, threads] {
                                    Stopwatch watch;
                                    const auto best =
                                        gpu::rowArgminCpu(m->etas, m->rows, m->cols, threads);
                                    const double ms = watch.elapsedMs();
                                    return Measurement{ms, best == *reference};
                                }});
        }
#ifdef DELIVERY_USE_CUDA
        versions.push_back({"gpu_kernel", [m, reference] {
                                gpu::KernelTiming timing;
                                const auto best = gpu::rowArgmin(m->etas, m->rows, m->cols, &timing);
                                return Measurement{timing.kernelMs, best == *reference};
                            }});
        versions.push_back({"gpu_e2e", [m, reference] {
                                Stopwatch watch;
                                const auto best = gpu::rowArgmin(m->etas, m->rows, m->cols);
                                const double ms = watch.elapsedMs();
                                return Measurement{ms, best == *reference};
                            }});
#endif
        return versions;
    };
    return c;
}

std::vector<long long> randomDeliveryDurations(int count, std::uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::gamma_distribution<double> minutes(4.0, 5.0);
    std::vector<long long> durations;
    durations.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        durations.push_back(static_cast<long long>(minutes(rng) * 60000.0));
    }
    return durations;
}

BenchCase deliveryHistogramCase() {
    static const std::vector<int> counts = {10000, 100000, 1000000, 4000000, 16000000};
    BenchCase c;
    c.application = "delivery_histogram";
    for (int count : counts) {
        c.sizes.push_back(std::to_string(count));
    }
    c.prepare = [](std::size_t sizeIndex) {
        auto durations = std::make_shared<std::vector<long long>>(
            randomDeliveryDurations(counts[sizeIndex], kBenchSeed));
        auto reference = std::make_shared<std::vector<unsigned>>(
            gpu::deliveryHistogramCpu(*durations, 1));

        std::vector<Version> versions;
        for (unsigned threads : threadSweep(hardwareThreads())) {
            versions.push_back({"cpu_t" + std::to_string(threads), [durations, reference, threads] {
                                    Stopwatch watch;
                                    const auto bins = gpu::deliveryHistogramCpu(*durations, threads);
                                    const double ms = watch.elapsedMs();
                                    return Measurement{ms, bins == *reference};
                                }});
        }
#ifdef DELIVERY_USE_CUDA
        versions.push_back({"gpu_kernel", [durations, reference] {
                                gpu::KernelTiming timing;
                                const auto bins = gpu::deliveryHistogram(*durations, &timing);
                                return Measurement{timing.kernelMs, bins == *reference};
                            }});
        versions.push_back({"gpu_e2e", [durations, reference] {
                                Stopwatch watch;
                                const auto bins = gpu::deliveryHistogram(*durations);
                                const double ms = watch.elapsedMs();
                                return Measurement{ms, bins == *reference};
                            }});
#endif
        return versions;
    };
    return c;
}

BenchCase deliveryPercentilesCase() {
    static const std::vector<int> counts = {10000, 100000, 1000000, 4000000};
    BenchCase c;
    c.application = "delivery_percentiles";
    for (int count : counts) {
        c.sizes.push_back(std::to_string(count));
    }
    c.prepare = [](std::size_t sizeIndex) {
        auto keys = std::make_shared<std::vector<unsigned>>(
            gpu::toSortKeys(randomDeliveryDurations(counts[sizeIndex], kBenchSeed)));
        auto sortedReference = std::make_shared<std::vector<unsigned>>(gpu::sortKeysCpu(*keys, 1));
        const gpu::Percentiles reference = gpu::percentilesOfSorted(*sortedReference);

        std::vector<Version> versions;
        for (unsigned threads : threadSweep(hardwareThreads())) {
            versions.push_back({"cpu_t" + std::to_string(threads),
                                [keys, sortedReference, reference, threads] {
                                    Stopwatch watch;
                                    const auto sorted = gpu::sortKeysCpu(*keys, threads);
                                    const auto p = gpu::percentilesOfSorted(sorted);
                                    const double ms = watch.elapsedMs();
                                    return Measurement{ms, p == reference && sorted == *sortedReference};
                                }});
        }
#ifdef DELIVERY_USE_CUDA
        versions.push_back({"gpu_kernel", [keys, sortedReference, reference] {
                                gpu::KernelTiming timing;
                                const auto sorted = gpu::radixSort(*keys, &timing);
                                const auto p = gpu::percentilesOfSorted(sorted);
                                return Measurement{timing.kernelMs,
                                                   p == reference && sorted == *sortedReference};
                            }});
        versions.push_back({"gpu_e2e", [keys, sortedReference, reference] {
                                Stopwatch watch;
                                const auto sorted = gpu::radixSort(*keys);
                                const auto p = gpu::percentilesOfSorted(sorted);
                                const double ms = watch.elapsedMs();
                                return Measurement{ms, p == reference && sorted == *sortedReference};
                            }});
#endif
        return versions;
    };
    return c;
}

BenchCase ordersByRestaurantCase() {
    static const std::vector<int> counts = {10000, 100000, 300000, 1000000};
    constexpr int kRestaurants = 60;
    BenchCase c;
    c.application = "orders_by_restaurant";
    for (int count : counts) {
        c.sizes.push_back(std::to_string(count));
    }
    c.prepare = [](std::size_t sizeIndex) {
        auto keys = std::make_shared<std::vector<int>>();
        std::mt19937_64 rng(kBenchSeed);
        std::geometric_distribution<int> popularity(0.05);
        for (int i = 0; i < counts[sizeIndex]; ++i) {
            keys->push_back(popularity(rng) % kRestaurants);
        }
        auto reference =
            std::make_shared<gpu::Grouping>(gpu::groupByKeyCpu(*keys, kRestaurants, 1));

        std::vector<Version> versions;
        for (unsigned threads : threadSweep(hardwareThreads())) {
            versions.push_back({"cpu_t" + std::to_string(threads), [keys, reference, threads] {
                                    Stopwatch watch;
                                    const auto g = gpu::groupByKeyCpu(*keys, kRestaurants, threads);
                                    const double ms = watch.elapsedMs();
                                    return Measurement{ms, g == *reference};
                                }});
        }
#ifdef DELIVERY_USE_CUDA
        versions.push_back({"gpu_kernel", [keys, reference] {
                                gpu::KernelTiming timing;
                                const auto g = gpu::groupByKey(*keys, kRestaurants, &timing);
                                return Measurement{timing.kernelMs, g == *reference};
                            }});
        versions.push_back({"gpu_e2e", [keys, reference] {
                                Stopwatch watch;
                                const auto g = gpu::groupByKey(*keys, kRestaurants);
                                const double ms = watch.elapsedMs();
                                return Measurement{ms, g == *reference};
                            }});
#endif
        return versions;
    };
    return c;
}

struct SegmentedLog {
    gpu::Timeline segments;
    std::vector<int> offsets;
};

SegmentedLog randomCourierLogs(int events, int couriers, std::uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<int> owner(0, couriers - 1);
    std::uniform_int_distribution<long long> gap(1000, 600000);
    std::vector<int> perCourier(static_cast<std::size_t>(couriers), 0);
    for (int e = 0; e < events; ++e) {
        ++perCourier[static_cast<std::size_t>(owner(rng))];
    }
    SegmentedLog log;
    log.offsets.push_back(0);
    for (int c = 0; c < couriers; ++c) {
        long long t = 0;
        for (int k = 0; k < perCourier[static_cast<std::size_t>(c)]; ++k) {
            t += gap(rng) / 1000 * 1000;
            log.segments.t.push_back(t);
            log.segments.source.push_back(c);
        }
        log.offsets.push_back(static_cast<int>(log.segments.t.size()));
    }
    return log;
}

BenchCase courierLogMergeCase() {
    static const std::vector<int> counts = {10000, 100000, 1000000, 4000000};
    constexpr int kCouriers = 3000;
    BenchCase c;
    c.application = "courier_log_merge";
    for (int count : counts) {
        c.sizes.push_back(std::to_string(count));
    }
    c.prepare = [](std::size_t sizeIndex) {
        auto log = std::make_shared<SegmentedLog>(
            randomCourierLogs(counts[sizeIndex], kCouriers, kBenchSeed));
        auto reference = std::make_shared<gpu::Timeline>(
            gpu::mergeSegmentsReference(log->segments, log->offsets));

        std::vector<Version> versions;
        for (unsigned threads : threadSweep(hardwareThreads())) {
            versions.push_back({"cpu_t" + std::to_string(threads), [log, reference, threads] {
                                    Stopwatch watch;
                                    const auto merged =
                                        gpu::mergeSegmentsCpu(log->segments, log->offsets, threads);
                                    const double ms = watch.elapsedMs();
                                    return Measurement{ms, merged == *reference};
                                }});
        }
#ifdef DELIVERY_USE_CUDA
        versions.push_back({"gpu_kernel", [log, reference] {
                                gpu::KernelTiming timing;
                                const auto merged =
                                    gpu::mergeSegments(log->segments, log->offsets, &timing);
                                return Measurement{timing.kernelMs, merged == *reference};
                            }});
        versions.push_back({"gpu_e2e", [log, reference] {
                                Stopwatch watch;
                                const auto merged = gpu::mergeSegments(log->segments, log->offsets);
                                const double ms = watch.elapsedMs();
                                return Measurement{ms, merged == *reference};
                            }});
#endif
        return versions;
    };
    return c;
}

std::vector<BenchCase> allBenchCases(const infra::Config&) {
    std::vector<BenchCase> cases;
    cases.push_back(travelTableCase());
    cases.push_back(candidateFilterCase());
    cases.push_back(etaBatchCase());
    cases.push_back(dispatchArgminCase());
    cases.push_back(deliveryHistogramCase());
    cases.push_back(deliveryPercentilesCase());
    cases.push_back(ordersByRestaurantCase());
    cases.push_back(courierLogMergeCase());
    return cases;
}
}
