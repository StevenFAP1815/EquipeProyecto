#include <delivery/concurrency/ChangeSignal.hpp>
#include <delivery/concurrency/OrderBook.hpp>
#include <delivery/concurrency/ShutdownToken.hpp>
#include <delivery/core/StreetNetwork.hpp>
#include <delivery/infra/ConfigLoader.hpp>
#include <delivery/infra/EventLog.hpp>
#include <delivery/sim/Dispatcher.hpp>
#include <delivery/sim/Fleet.hpp>
#include <delivery/sim/OrderGenerator.hpp>
#include <delivery/sim/SimClock.hpp>
#include <delivery/viz/Renderer.hpp>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <thread>

namespace {
struct Arguments {
    std::filesystem::path configPath;
    std::filesystem::path logPath = "events.log";
};

std::optional<Arguments> parseArguments(int argc, char** argv) {
    if (argc != 2 && argc != 4) {
        return std::nullopt;
    }
    Arguments args;
    args.configPath = argv[1];
    if (argc == 4) {
        if (std::string(argv[2]) != "--log") {
            return std::nullopt;
        }
        args.logPath = argv[3];
    }
    return args;
}
}

int main(int argc, char** argv) {
    using namespace delivery;

    const std::optional<Arguments> args = parseArguments(argc, argv);
    if (!args) {
        std::cerr << "Usage: delivery_sim <config.json> [--log <path>]\n";
        return 1;
    }

    try {
        const infra::Config cfg = infra::loadConfigFromFile(args->configPath);
        const std::filesystem::path imagePath =
            args->configPath.parent_path() / cfg.map.imagePath;
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
        viz::Renderer renderer(cfg, imagePath, fleet, book, log, clock, stop);

        fleet.start();
        std::thread tProducer([&generator] {
            generator.run();
        });
        std::thread tDispatcher([&dispatcher] {
            dispatcher.run();
        });
        std::thread tRenderer([&renderer] {
            renderer.run();
        });

        std::cout << "Simulation has started..." << std::endl;

        if (cfg.simulation.durationS > 0) {
            const std::int64_t durationMs =
                static_cast<std::int64_t>(cfg.simulation.durationS) * 1000;
            clock.waitUntilMs(durationMs, stop);
        } else {
            stop.waitForStop();
        }

        stop.requestStop();
        log.simulationStopping(clock.nowMs());
        clock.wakeAll();
        book.wakeAll();
        freedSignal.wakeAll();

        tProducer.join();
        tDispatcher.join();
        tRenderer.join();
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
