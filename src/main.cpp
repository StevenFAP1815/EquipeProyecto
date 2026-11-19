#include <delivery/bench/Applications.hpp>
#include <delivery/bench/BenchRunner.hpp>
#include <delivery/bench/Stopwatch.hpp>
#include <delivery/concurrency/ChangeSignal.hpp>
#include <delivery/concurrency/GpuWorker.hpp>
#include <delivery/concurrency/OrderBook.hpp>
#include <delivery/concurrency/ShutdownToken.hpp>
#include <delivery/core/CsrGraph.hpp>
#include <delivery/core/StreetNetwork.hpp>
#include <delivery/core/TravelTable.hpp>
#include <delivery/gpu/CoverageField.hpp>
#include <delivery/gpu/DeliveryHistogram.hpp>
#include <delivery/gpu/DemandHeatmap.hpp>
#include <delivery/gpu/DeviceInfo.hpp>
#include <delivery/gpu/EtaBatch.hpp>
#include <delivery/gpu/FloydWarshall.hpp>
#include <delivery/gpu/GroupByKey.hpp>
#include <delivery/gpu/RadixSort.hpp>
#include <delivery/gpu/SegmentMerge.hpp>
#include <delivery/infra/ConfigLoader.hpp>
#include <delivery/infra/EventLog.hpp>
#include <delivery/sim/Dispatcher.hpp>
#include <delivery/sim/Fleet.hpp>
#include <delivery/sim/OrderGenerator.hpp>
#include <delivery/sim/SimClock.hpp>
#ifdef DELIVERY_HAS_VIEWER
#include <delivery/viz/Renderer.hpp>
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <thread>

namespace {
constexpr auto kSignalCheckInterval = std::chrono::milliseconds(100);

std::atomic<int> receivedSignal{0};
static_assert(std::atomic<int>::is_always_lock_free,
              "the signal handler may only touch lock-free atomics");

void onSignal(int signalNumber) {
    receivedSignal.store(signalNumber);
}

void waitForEndOfSimulation(const delivery::infra::Config& cfg,
                            delivery::sim::SimClock& clock,
                            delivery::concurrency::ShutdownToken& stop) {
    const bool hasDuration = cfg.simulation.durationS > 0;
    const std::int64_t endMs = static_cast<std::int64_t>(cfg.simulation.durationS) * 1000;

    while (!stop.stopRequested() && receivedSignal.load() == 0) {
        auto deadline = std::chrono::steady_clock::now() + kSignalCheckInterval;
        if (hasDuration) {
            if (clock.nowMs() >= endMs) {
                return;
            }
            deadline = std::min(deadline, clock.realTimeAt(endMs));
        }
        stop.waitForStopUntil(deadline);
    }
}

struct Arguments {
    std::filesystem::path configPath;
    std::filesystem::path logPath = "events.log";
    bool headless = false;
    std::filesystem::path benchPath;
    bool benchOnce = false;
    std::string benchOnly;
    std::string benchSize;
    bool cpuOnly = false;
    std::filesystem::path frameLogPath;
    delivery::sim::GpuDispatch gpuDispatch = delivery::sim::GpuDispatch::Measured;
};

bool gpuCompiledIn() {
#ifdef DELIVERY_USE_CUDA
    return true;
#else
    return false;
#endif
}

unsigned cpuThreads() {
    return std::max(1u, std::thread::hardware_concurrency());
}

std::string describeGroups(const delivery::infra::Config& cfg,
                           const delivery::gpu::Grouping& groups,
                           const std::vector<long long>& durations) {
    std::ostringstream out;
    out << "Deliveries grouped by restaurant (" << cfg.restaurants.size() << " restaurants):";
    int busiest = -1;
    int slowest = -1;
    int busiestCount = 0;
    double slowestAverage = -1.0;
    for (std::size_t r = 0; r + 1 < groups.offsets.size(); ++r) {
        const int begin = groups.offsets[r];
        const int end = groups.offsets[r + 1];
        if (end == begin) {
            continue;
        }
        long long total = 0;
        for (int p = begin; p < end; ++p) {
            total += durations[static_cast<std::size_t>(groups.order[static_cast<std::size_t>(p)])];
        }
        const double average = static_cast<double>(total) / (end - begin);
        if (end - begin > busiestCount) {
            busiestCount = end - begin;
            busiest = static_cast<int>(r);
        }
        if (average > slowestAverage) {
            slowestAverage = average;
            slowest = static_cast<int>(r);
        }
    }
    if (busiest < 0) {
        out << " none";
        return out.str();
    }
    out << " busiest " << cfg.restaurants[static_cast<std::size_t>(busiest)].id.str() << " ("
        << busiestCount << " deliveries), slowest average "
        << cfg.restaurants[static_cast<std::size_t>(slowest)].id.str() << " ("
        << static_cast<long long>(slowestAverage) << " ms)";
    return out.str();
}

delivery::gpu::Timeline timelineByCourier(const std::vector<delivery::infra::DeliveryStamp>& stamps,
                                          std::vector<int>& offsets) {
    std::map<std::string, int> courierIndex;
    std::vector<std::vector<long long>> perCourier;
    for (const auto& stamp : stamps) {
        const auto [it, inserted] =
            courierIndex.emplace(stamp.courier, static_cast<int>(perCourier.size()));
        if (inserted) {
            perCourier.emplace_back();
        }
        perCourier[static_cast<std::size_t>(it->second)].push_back(stamp.t);
    }
    delivery::gpu::Timeline segments;
    offsets.assign(1, 0);
    for (std::size_t c = 0; c < perCourier.size(); ++c) {
        for (long long t : perCourier[c]) {
            segments.t.push_back(t);
            segments.source.push_back(static_cast<int>(c));
        }
        offsets.push_back(static_cast<int>(segments.t.size()));
    }
    return segments;
}

std::string describeTimeline(const delivery::gpu::Timeline& merged, int couriers) {
    constexpr long long kWindowMs = 10 * 60 * 1000;
    std::size_t bestCount = 0;
    long long bestStart = 0;
    std::size_t begin = 0;
    for (std::size_t end = 0; end < merged.t.size(); ++end) {
        while (merged.t[end] - merged.t[begin] >= kWindowMs) {
            ++begin;
        }
        if (end - begin + 1 > bestCount) {
            bestCount = end - begin + 1;
            bestStart = merged.t[begin];
        }
    }
    std::ostringstream out;
    out << "Merged delivery timeline (" << merged.t.size() << " deliveries from " << couriers
        << " couriers, ordered=" << (std::is_sorted(merged.t.begin(), merged.t.end()) ? "yes" : "no")
        << "): busiest 10-minute window starts at " << bestStart << " ms with " << bestCount
        << " deliveries";
    return out.str();
}

void reportDemandHeatmap(const delivery::infra::Config& cfg,
                         const delivery::core::StreetNetwork& network,
                         const delivery::infra::EventLog& log,
                         delivery::concurrency::GpuWorker& worker, bool useGpu) {
    using namespace delivery;
    constexpr int kCells = 64;
    const auto& bounds = cfg.map.bounds;
    std::vector<int> counts(static_cast<std::size_t>(kCells) * kCells, 0);
    int orders = 0;
    for (const std::string& node : log.createdDeliveryNodes()) {
        const auto position = network.nodeLatLon(core::NodeId(node));
        if (!position) {
            continue;
        }
        const double fx = (position->lon - bounds.west()) / (bounds.east() - bounds.west());
        const double fy = (bounds.north() - position->lat) / (bounds.north() - bounds.south());
        const int cx = std::clamp(static_cast<int>(fx * kCells), 0, kCells - 1);
        const int cy = std::clamp(static_cast<int>(fy * kCells), 0, kCells - 1);
        ++counts[static_cast<std::size_t>(cy) * kCells + cx];
        ++orders;
    }

    std::vector<int> smoothed;
    if (useGpu) {
        smoothed = worker.submit([&counts] {
            return gpu::smoothDemand(counts, kCells, kCells);
        }).get();
    } else {
        smoothed = gpu::smoothDemandCpu(counts, kCells, kCells, cpuThreads());
    }

    const auto hottest = std::max_element(smoothed.begin(), smoothed.end());
    const auto index = static_cast<int>(hottest - smoothed.begin());
    const int cx = index % kCells;
    const int cy = index / kCells;
    const double lat = bounds.north() - (cy + 0.5) / kCells * (bounds.north() - bounds.south());
    const double lon = bounds.west() + (cx + 0.5) / kCells * (bounds.east() - bounds.west());
    std::cerr << "Demand heatmap (" << kCells << "x" << kCells << " cells, " << gpu::kHeatWidth
              << "x" << gpu::kHeatWidth << " Gaussian, " << orders << " orders): hottest cell ("
              << cx << "," << cy << ") near " << lat << "," << lon << " with smoothed demand "
              << static_cast<double>(*hottest) / gpu::kHeatWeightSum << std::endl;
}

void reportCoverage(const delivery::infra::Config& cfg, const delivery::sim::Fleet& fleet,
                    delivery::concurrency::GpuWorker& worker, bool useGpu) {
    using namespace delivery;
    constexpr int kCells = 64;
    constexpr int kSweeps = 200;
    const auto& bounds = cfg.map.bounds;
    std::vector<float> density(static_cast<std::size_t>(kCells) * kCells, 0.0f);
    for (const auto& courier : fleet.couriers()) {
        const core::LatLon position = courier->position();
        const double fx = (position.lon - bounds.west()) / (bounds.east() - bounds.west());
        const double fy = (bounds.north() - position.lat) / (bounds.north() - bounds.south());
        const int cx = std::clamp(static_cast<int>(fx * kCells), 0, kCells - 1);
        const int cy = std::clamp(static_cast<int>(fy * kCells), 0, kCells - 1);
        density[static_cast<std::size_t>(cy) * kCells + cx] += 1.0f;
    }

    std::vector<float> field;
    if (useGpu) {
        field = worker.submit([&density] {
            return gpu::coverageField(density, kCells, kCells, kSweeps);
        }).get();
    } else {
        field = gpu::coverageFieldCpu(density, kCells, kCells, kSweeps, cpuThreads());
    }
    const int least = gpu::leastCoveredInteriorCell(field, kCells, kCells);
    if (least < 0) {
        return;
    }
    const int cx = least % kCells;
    const int cy = least / kCells;
    const double lat = bounds.north() - (cy + 0.5) / kCells * (bounds.north() - bounds.south());
    const double lon = bounds.west() + (cx + 0.5) / kCells * (bounds.east() - bounds.west());
    std::cerr << "Coverage field (" << kCells << "x" << kCells << " cells, " << kSweeps
              << " Jacobi sweeps, " << fleet.couriers().size()
              << " couriers): least covered cell (" << cx << "," << cy << ") near " << lat << ","
              << lon << " with coverage " << field[static_cast<std::size_t>(least)]
              << ", where idle couriers should wait" << std::endl;
}

void reportDeliveryStatistics(const delivery::infra::Config& cfg,
                              const delivery::infra::EventLog& log,
                              delivery::concurrency::GpuWorker& worker, bool useGpu) {
    using namespace delivery;
    const auto raw = log.deliveryDurationsMs();
    const std::vector<long long> durations(raw.begin(), raw.end());

    std::vector<unsigned> histogram;
    if (useGpu) {
        histogram = worker.submit([&durations] { return gpu::deliveryHistogram(durations); }).get();
    } else {
        histogram = gpu::deliveryHistogramCpu(durations, cpuThreads());
    }
    std::cerr << gpu::describeHistogram(histogram) << std::endl;

    const std::vector<unsigned> keys = gpu::toSortKeys(durations);
    std::vector<unsigned> sorted;
    if (useGpu) {
        sorted = worker.submit([&keys] { return gpu::radixSort(keys); }).get();
    } else {
        sorted = gpu::sortKeysCpu(keys, cpuThreads());
    }
    std::cerr << gpu::describePercentiles(gpu::percentilesOfSorted(sorted), sorted.size())
              << std::endl;

    const std::vector<std::string> restaurants = log.deliveryRestaurants();
    std::vector<int> restaurantKeys;
    std::vector<long long> keyedDurations;
    for (std::size_t i = 0; i < restaurants.size() && i < durations.size(); ++i) {
        const auto found = cfg.restaurantIndex.find(core::RestaurantId(restaurants[i]));
        if (found != cfg.restaurantIndex.end()) {
            restaurantKeys.push_back(static_cast<int>(found->second));
            keyedDurations.push_back(durations[i]);
        }
    }
    const int restaurantCount = static_cast<int>(cfg.restaurants.size());
    if (restaurantCount == 0 || restaurantCount > gpu::kMaxGroupKeys) {
        return;
    }
    gpu::Grouping groups;
    if (useGpu) {
        groups = worker.submit([&restaurantKeys, restaurantCount] {
            return gpu::groupByKey(restaurantKeys, restaurantCount);
        }).get();
    } else {
        groups = gpu::groupByKeyCpu(restaurantKeys, restaurantCount, cpuThreads());
    }
    std::cerr << describeGroups(cfg, groups, keyedDurations) << std::endl;

    std::vector<int> offsets;
    const gpu::Timeline perCourier = timelineByCourier(log.deliveryStamps(), offsets);
    gpu::Timeline merged;
    if (useGpu) {
        merged = worker.submit([&perCourier, &offsets] {
            return gpu::mergeSegments(perCourier, offsets);
        }).get();
    } else {
        merged = gpu::mergeSegmentsCpu(perCourier, offsets, cpuThreads());
    }
    std::cerr << describeTimeline(merged, static_cast<int>(offsets.size()) - 1) << std::endl;
}

std::shared_ptr<const delivery::core::TravelTable> buildTravelTable(
    const delivery::core::StreetNetwork& network, delivery::concurrency::GpuWorker& worker,
    bool useGpu) {
    using namespace delivery;
    const core::CsrGraph graph = core::CsrGraph::fromNetwork(network);
    const bench::Stopwatch watch;
    std::shared_ptr<const core::TravelTable> table;
    if (useGpu) {
        std::vector<std::int32_t> centimeters = worker.submit([&graph] {
            return gpu::floydWarshallTiled(graph.n, core::denseAdjacency(graph));
        }).get();
        table = std::make_shared<const core::TravelTable>(graph.n, std::move(centimeters));
    } else {
        const unsigned threads = std::max(1u, std::thread::hardware_concurrency());
        table = std::make_shared<const core::TravelTable>(core::allPairsDijkstra(graph, threads));
    }
    std::cerr << "Travel table: " << graph.n << " nodes on "
              << (useGpu ? "GPU (tiled Floyd-Warshall)" : "CPU (parallel Dijkstra)") << " in "
              << watch.elapsedMs() << " ms" << std::endl;
    return table;
}

int runBenchmarks(const delivery::infra::Config& cfg, const Arguments& args) {
    using namespace delivery;
    std::ofstream csv(args.benchPath);
    if (!csv) {
        std::cerr << "Cannot write " << args.benchPath << "\n";
        return 1;
    }
    bench::BenchOptions options;
    options.only = args.benchOnly;
    options.onlySize = args.benchSize;
    if (args.benchOnce) {
        options.repetitions = 1;
        options.warmup = false;
    }
    bench::BenchRunner runner(csv, options);
    for (const bench::BenchCase& benchCase : bench::allBenchCases(cfg)) {
        if (!args.benchOnly.empty() && args.benchOnly != benchCase.application) {
            continue;
        }
        std::cerr << "bench: " << benchCase.application << std::endl;
        runner.run(benchCase);
    }
    std::cout << "bench: " << runner.rowsWritten() << " rows, " << runner.failures()
              << " incorrect, written to " << args.benchPath.string() << std::endl;
    return runner.failures() == 0 ? 0 : 3;
}

std::optional<Arguments> parseArguments(int argc, char** argv) {
    if (argc < 2) {
        return std::nullopt;
    }
    Arguments args;
    args.configPath = argv[1];
    for (int i = 2; i < argc; ++i) {
        const std::string option = argv[i];
        if (option == "--log" && i + 1 < argc) {
            args.logPath = argv[++i];
        } else if (option == "--headless") {
            args.headless = true;
        } else if (option == "--bench" && i + 1 < argc) {
            args.benchPath = argv[++i];
        } else if (option == "--cpu-only") {
            args.cpuOnly = true;
        } else if (option == "--bench-once") {
            args.benchOnce = true;
        } else if (option == "--bench-only" && i + 1 < argc) {
            args.benchOnly = argv[++i];
        } else if (option == "--bench-size" && i + 1 < argc) {
            args.benchSize = argv[++i];
        } else if (option == "--frame-log" && i + 1 < argc) {
            args.frameLogPath = argv[++i];
        } else if (option == "--gpu-dispatch" && i + 1 < argc) {
            const std::string policy = argv[++i];
            if (policy == "always") {
                args.gpuDispatch = delivery::sim::GpuDispatch::Always;
            } else if (policy != "measured") {
                return std::nullopt;
            }
        } else {
            return std::nullopt;
        }
    }
#ifndef DELIVERY_HAS_VIEWER
    args.headless = true;
#endif
    return args;
}
}

int main(int argc, char** argv) {
    using namespace delivery;

    const std::optional<Arguments> args = parseArguments(argc, argv);
    if (!args) {
        std::cerr << "Usage: delivery_sim <config.json> [--log <path>] [--headless] [--cpu-only]"
                     " [--frame-log <frames.csv>] [--gpu-dispatch measured|always]\n"
                     "       delivery_sim <config.json> --headless --bench <results.csv>"
                     " [--bench-once] [--bench-only <application>] [--bench-size <size>]\n";
        return 1;
    }

    try {
        const infra::Config cfg = infra::loadConfigFromFile(args->configPath);
        if (!args->benchPath.empty()) {
            std::cerr << gpu::describe(gpu::queryDevice()) << std::endl;
            return runBenchmarks(cfg, *args);
        }
        concurrency::GpuWorker gpuWorker;
        std::cerr << gpu::describe(gpuWorker.submit([] { return gpu::queryDevice(); }).get())
                  << std::endl;
        const auto network = core::StreetNetwork::build(cfg.nodes, cfg.streets);

        infra::EventLog log(args->logPath);
        concurrency::ShutdownToken stop;
        concurrency::OrderBook book(static_cast<std::size_t>(cfg.orders.maxPending));
        concurrency::ChangeSignal freedSignal;
        std::atomic<std::uint64_t> orderIdCounter{0};

        sim::SimClock clock(cfg.simulation.timeScale);
        sim::OrderGenerator generator(cfg, book, stop, clock, orderIdCounter, log);
        sim::Fleet fleet(cfg, network, clock, stop, freedSignal, log);
        sim::Dispatcher dispatcher(cfg, network, book, fleet, clock, freedSignal, stop, log);
        const bool useGpu = gpuCompiledIn() && !args->cpuOnly;
        const auto travelTable = buildTravelTable(*network, gpuWorker, useGpu);
        dispatcher.useTravelTable(travelTable);
        std::unique_ptr<gpu::EtaEngine> etaEngine;
        if (useGpu) {
            dispatcher.useGpu(gpuWorker, args->gpuDispatch);
            etaEngine = gpuWorker.submit([&travelTable] {
                return std::make_unique<gpu::EtaEngine>(travelTable->size(), travelTable->data());
            }).get();
            dispatcher.useEtaEngine(etaEngine.get());
        }
#ifdef DELIVERY_HAS_VIEWER
        std::optional<viz::Renderer> renderer;
        if (!args->headless) {
            const std::filesystem::path imagePath =
                args->configPath.parent_path() / cfg.map.imagePath;
            renderer.emplace(cfg, imagePath, fleet, book, log, clock, stop, args->frameLogPath);
        }
#endif

        std::signal(SIGINT, onSignal);
        std::signal(SIGTERM, onSignal);

        fleet.start();
        std::thread tProducer([&generator] {
            generator.run();
        });
        std::thread tDispatcher([&dispatcher] {
            dispatcher.run();
        });
        std::thread tRenderer;
#ifdef DELIVERY_HAS_VIEWER
        if (renderer) {
            tRenderer = std::thread([&renderer] {
                renderer->run();
            });
        }
#endif

        std::cout << "Simulation has started..." << std::endl;

        waitForEndOfSimulation(cfg, clock, stop);

        stop.requestStop();
        log.simulationStopping(clock.nowMs());
        clock.wakeAll();
        book.wakeAll();
        freedSignal.wakeAll();

        tProducer.join();
        tDispatcher.join();
        if (etaEngine) {
            gpuWorker.submit([&etaEngine] { etaEngine.reset(); }).get();
        }
        if (tRenderer.joinable()) {
            tRenderer.join();
        }
        fleet.join();

        reportDeliveryStatistics(cfg, log, gpuWorker, useGpu);
        reportDemandHeatmap(cfg, *network, log, gpuWorker, useGpu);
        reportCoverage(cfg, fleet, gpuWorker, useGpu);
        gpuWorker.stop();

        const infra::Summary summary = log.summary();
        const std::uint64_t pendingInSystem =
            book.size() + dispatcher.pendingAtStopCount() + fleet.ordersInProgress();
        if (summary.pending != pendingInSystem) {
            std::cerr << "Warning: log reports " << summary.pending
                      << " pending orders but " << pendingInSystem
                      << " are still in the system\n";
        }

        std::cout << infra::toJsonLine(summary) << std::endl;
        return 0;
    } catch (const infra::ConfigError& e) {
        std::cerr << "Configuration error: " << e.what() << "\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 2;
    }
}
