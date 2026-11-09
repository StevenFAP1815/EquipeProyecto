#include <delivery/concurrency/ChangeSignal.hpp>
#include <delivery/concurrency/OrderBook.hpp>
#include <delivery/concurrency/ShutdownToken.hpp>
#include <delivery/core/Order.hpp>
#include <delivery/core/StreetNetwork.hpp>
#include <delivery/infra/ConfigLoader.hpp>
#include <delivery/sim/Fleet.hpp>
#include <delivery/sim/OrderGenerator.hpp>
#include <delivery/sim/SimClock.hpp>
#include <delivery/viz/Renderer.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <thread>

namespace {
using namespace delivery;

bool assignToFirstFreeCourier(sim::Fleet& fleet, core::Order& order) {
    for (const auto& courier : fleet.couriers()) {
        if (courier->tryAssign(order)) {
            return true;
        }
    }
    return false;
}

void provisionalDispatchLoop(concurrency::OrderBook& book,
                             sim::Fleet& fleet,
                             concurrency::ChangeSignal& freedSignal,
                             concurrency::ShutdownToken& stop,
                             std::atomic<std::uint64_t>& pendingAtStop) {
    while (auto order = book.waitAndPop(stop)) {
        while (true) {
            if (stop.stopRequested()) {
                pendingAtStop.fetch_add(1);
                break;
            }
            const std::uint64_t seen = freedSignal.version();
            if (assignToFirstFreeCourier(fleet, *order)) {
                break;
            }
            freedSignal.waitForChange(
                seen, std::chrono::steady_clock::now() + std::chrono::hours(24), stop);
        }
    }
}
}

int main(int argc, char** argv) {
    using namespace delivery;

    if (argc < 2) {
        std::cerr << "Usage: delivery_sim <config.json> [--log <path>]\n";
        return 1;
    }

    try {
        const std::filesystem::path configPath(argv[1]);
        const infra::Config cfg = infra::loadConfigFromFile(configPath);
        const std::filesystem::path imagePath =
            configPath.parent_path() / cfg.map.imagePath;
        const auto network = core::StreetNetwork::build(cfg.nodes, cfg.streets);

        concurrency::ShutdownToken stop;
        concurrency::OrderBook book(static_cast<std::size_t>(cfg.orders.maxPending));
        concurrency::ChangeSignal freedSignal;
        std::atomic<std::uint64_t> orderIdCounter{0};
        std::atomic<std::uint64_t> pendingAtStop{0};

        sim::SimClock clock(cfg.simulation.timeScale);
        sim::OrderGenerator generator(cfg, book, stop, clock, orderIdCounter);
        sim::Fleet fleet(cfg, network, clock, stop, freedSignal);
        viz::Renderer renderer(cfg, imagePath, fleet, book, orderIdCounter, clock, stop);

        fleet.start();
        std::thread tProducer([&generator] {
            generator.run();
        });
        std::thread tDispatcher([&book, &fleet, &freedSignal, &stop, &pendingAtStop] {
            provisionalDispatchLoop(book, fleet, freedSignal, stop, pendingAtStop);
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
        clock.wakeAll();
        book.wakeAll();
        freedSignal.wakeAll();

        tProducer.join();
        tDispatcher.join();
        tRenderer.join();
        fleet.join();

        const std::uint64_t created   = orderIdCounter.load();
        const std::uint64_t delivered = fleet.deliveredCount();
        const std::uint64_t rejected  = book.rejectedCount() + fleet.failedCount();
        const std::uint64_t pending   =
            book.size() + pendingAtStop.load() + fleet.ordersInProgress();

        std::cout << "{"
                  << "\"created\": "   << created   << ", "
                  << "\"delivered\": " << delivered << ", "
                  << "\"rejected\": "  << rejected  << ", "
                  << "\"pending\": "   << pending
                  << "}\n";

        return 0;
    } catch (const infra::ConfigError& e) {
        std::cerr << "Configuration error: " << e.what() << "\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 2;
    }
}
