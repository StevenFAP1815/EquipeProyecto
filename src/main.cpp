#include <delivery/concurrency/OrderBook.hpp>
#include <delivery/concurrency/ShutdownToken.hpp>
#include <delivery/core/Order.hpp>
#include <delivery/infra/ConfigLoader.hpp>
#include <delivery/sim/OrderGenerator.hpp>

#include <atomic>
#include <chrono>
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

        concurrency::ShutdownToken stop;
        concurrency::OrderBook book(static_cast<std::size_t>(cfg.orders.maxPending));
        std::atomic<std::uint64_t> orderIdCounter{0};
        std::atomic<std::uint64_t> consumedCount{0};

        sim::OrderGenerator generator(cfg, book, stop, orderIdCounter);

        std::thread tProducer([&generator] {
            generator.run();
        });
        std::thread tConsumer([&book, &stop, &consumedCount] {
            consumerLoop(book, stop, consumedCount);
        });

        if (cfg.simulation.durationS > 0) {
            const double realSeconds =
                static_cast<double>(cfg.simulation.durationS) /
                cfg.simulation.timeScale;
            std::this_thread::sleep_for(
                std::chrono::milliseconds(
                    static_cast<std::int64_t>(realSeconds * 1000.0)));
        } else {
            stop.waitForStop();
        }

        stop.requestStop();

        book.wakeAll();

        tProducer.join();
        tConsumer.join();

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
