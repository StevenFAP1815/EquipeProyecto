#include <delivery/concurrency/GpuWorker.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <future>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>
#include <vector>

using delivery::concurrency::GpuWorker;
using delivery::concurrency::GpuWorkerStopped;
using namespace std::chrono_literals;

TEST_CASE("GpuWorker: devuelve el resultado del encargo por un future") {
    GpuWorker worker;
    auto answer = worker.submit([] { return 6 * 7; });
    REQUIRE(answer.get() == 42);
}

TEST_CASE("GpuWorker: todos los encargos corren en un único hilo distinto del llamador") {
    GpuWorker worker;
    std::vector<std::future<std::thread::id>> ids;
    for (int i = 0; i < 20; ++i) {
        ids.push_back(worker.submit([] { return std::this_thread::get_id(); }));
    }
    std::set<std::thread::id> seen;
    for (auto& id : ids) {
        seen.insert(id.get());
    }
    REQUIRE(seen.size() == 1);
    REQUIRE(*seen.begin() == worker.workerThreadId());
    REQUIRE(*seen.begin() != std::this_thread::get_id());
}

TEST_CASE("GpuWorker: atiende los encargos en orden de llegada") {
    GpuWorker worker;
    std::vector<int> order;
    std::vector<std::future<void>> done;
    for (int i = 0; i < 50; ++i) {
        done.push_back(worker.submit([&order, i] { order.push_back(i); }));
    }
    for (auto& d : done) {
        d.get();
    }
    REQUIRE(order.size() == 50);
    for (int i = 0; i < 50; ++i) {
        REQUIRE(order[static_cast<std::size_t>(i)] == i);
    }
}

TEST_CASE("GpuWorker: varios hilos pueden enviar encargos a la vez sin perder ninguno") {
    GpuWorker worker;
    constexpr int kProducers = 8;
    constexpr int kJobsEach = 200;
    std::vector<std::thread> producers;
    std::mutex resultsMutex;
    long long total = 0;
    for (int p = 0; p < kProducers; ++p) {
        producers.emplace_back([&worker, &resultsMutex, &total] {
            std::vector<std::future<int>> futures;
            for (int j = 0; j < kJobsEach; ++j) {
                futures.push_back(worker.submit([j] { return j; }));
            }
            long long local = 0;
            for (auto& f : futures) {
                local += f.get();
            }
            std::lock_guard<std::mutex> lock(resultsMutex);
            total += local;
        });
    }
    for (auto& t : producers) {
        t.join();
    }
    REQUIRE(total == kProducers * (kJobsEach - 1) * kJobsEach / 2);
    REQUIRE(worker.completedJobs() == kProducers * kJobsEach);
}

TEST_CASE("GpuWorker: una excepción del encargo llega a quien espera el future") {
    GpuWorker worker;
    auto failing = worker.submit([]() -> int { throw std::runtime_error("kernel failed"); });
    REQUIRE_THROWS_AS(failing.get(), std::runtime_error);
    REQUIRE(worker.submit([] { return 1; }).get() == 1);
}

TEST_CASE("GpuWorker: quien espera puede rendirse con un plazo mientras el encargo sigue") {
    GpuWorker worker;
    std::promise<void> release;
    std::shared_future<void> gate = release.get_future().share();
    auto slow = worker.submit([gate] {
        gate.wait();
        return 5;
    });
    REQUIRE(slow.wait_for(20ms) == std::future_status::timeout);
    release.set_value();
    REQUIRE(slow.get() == 5);
}

TEST_CASE("GpuWorker: al detenerse descarta los pendientes y rechaza encargos nuevos") {
    GpuWorker worker;
    std::promise<void> release;
    std::shared_future<void> gate = release.get_future().share();
    std::promise<void> started;
    auto running = worker.submit([gate, &started] {
        started.set_value();
        gate.wait();
        return 1;
    });
    started.get_future().wait();
    auto queued = worker.submit([] { return 2; });
    REQUIRE(worker.pendingJobs() == 1);

    std::thread stopper([&worker] { worker.stop(); });
    REQUIRE_THROWS_AS(queued.get(), std::future_error);
    release.set_value();
    stopper.join();

    REQUIRE(running.get() == 1);
    REQUIRE_THROWS_AS(worker.submit([] { return 3; }), GpuWorkerStopped);
}
