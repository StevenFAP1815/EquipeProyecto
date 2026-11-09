#include <delivery/concurrency/OrderBook.hpp>
#include <delivery/concurrency/ShutdownToken.hpp>
#include <delivery/core/Order.hpp>
#include <delivery/core/StreetNetwork.hpp>
#include <delivery/infra/ConfigLoader.hpp>
#include <delivery/sim/Fleet.hpp>
#include <delivery/sim/OrderGenerator.hpp>
#include <delivery/sim/SimClock.hpp>

#include <atomic>
#include <cstdint>
#include <iostream>
#include <thread>

namespace {
using namespace delivery;

void consumerLoop(concurrency::OrderBook& book,
                  concurrency::ShutdownToken& stop,
                  std::atomic<std::uint64_t>& consumedCount) {
    while (true) {
        auto maybeOrder = book.waitAndPop(stop);
        if (!maybeOrder) {
            return;
        }
        consumedCount.fetch_add(1, std::memory_order_relaxed);
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
        const infra::Config cfg = infra::loadConfigFromFile(argv[1]);
        const auto network = core::StreetNetwork::build(cfg);

        concurrency::ShutdownToken stop;
        concurrency::OrderBook book(static_cast<std::size_t>(cfg.orders.maxPending));
        std::atomic<std::uint64_t> orderIdCounter{0};
        std::atomic<std::uint64_t> consumedCount{0};

        sim::SimClock clock(cfg.simulation.timeScale);
        sim::OrderGenerator generator(cfg, book, stop, clock, orderIdCounter);
        sim::Fleet fleet(cfg, network, clock, stop);

        fleet.start();
        std::thread tProducer([&generator] {
            generator.run();
        });
        std::thread tConsumer([&book, &stop, &consumedCount] {
            consumerLoop(book, stop, consumedCount);
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

        tProducer.join();
        tConsumer.join();
        fleet.join();

        const std::uint64_t created  = orderIdCounter.load(std::memory_order_relaxed);
        const std::uint64_t consumed = consumedCount.load(std::memory_order_relaxed);
        const std::uint64_t rejected = book.rejectedCount();
        const std::uint64_t pending  = book.size();

        std::cout << "{"
                  << "\"created\": "   << created  << ", "
                  << "\"consumed\": "  << consumed << ", "
                  << "\"rejected\": "  << rejected << ", "
                  << "\"pending\": "   << pending
                  << "}\n";

        return 0;
    } catch (const infra::ConfigError& e) {
        std::cerr << "Configuration error: " << e.what() << "\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Unexpected error: " << e.what() << "\n";
        return 2;
    }
}
