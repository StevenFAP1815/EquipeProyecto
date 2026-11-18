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
#include <delivery/gpu/DeviceInfo.hpp>
#include <delivery/gpu/FloydWarshall.hpp>
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
#include <memory>
#include <optional>
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
    bool cpuOnly = false;
};

bool gpuCompiledIn() {
#ifdef DELIVERY_USE_CUDA
    return true;
#else
    return false;
#endif
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
    if (args.benchOnce) {
        options.repetitions = 1;
        options.warmup = false;
    }
    bench::BenchRunner runner(csv, options);
    for (const bench::BenchCase& benchCase : bench::allBenchCases(cfg)) {
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
        std::cerr << "Usage: delivery_sim <config.json> [--log <path>] [--headless] [--cpu-only]\n"
                     "       delivery_sim <config.json> --headless --bench <results.csv>"
                     " [--bench-once] [--bench-only <application>]\n";
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
        dispatcher.useTravelTable(buildTravelTable(*network, gpuWorker, useGpu));
#ifdef DELIVERY_HAS_VIEWER
        std::optional<viz::Renderer> renderer;
        if (!args->headless) {
            const std::filesystem::path imagePath =
                args->configPath.parent_path() / cfg.map.imagePath;
            renderer.emplace(cfg, imagePath, fleet, book, log, clock, stop);
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
        gpuWorker.stop();
        if (tRenderer.joinable()) {
            tRenderer.join();
        }
        fleet.join();

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
