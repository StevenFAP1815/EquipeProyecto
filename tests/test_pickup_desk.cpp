#include <catch2/catch_test_macros.hpp>

#include <delivery/concurrency/PickupDesk.hpp>
#include <delivery/concurrency/ShutdownToken.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <future>
#include <mutex>
#include <thread>
#include <vector>

using namespace delivery;
using namespace std::chrono_literals;

TEST_CASE("PickupDesk: nunca hay mas repartidores recogiendo que cupos",
          "[pickup_desk][concurrent]") {
    concurrency::PickupDesk desk(2);
    concurrency::ShutdownToken stop;
    std::mutex counterMutex;
    int inside = 0;
    int maxInside = 0;
    std::atomic<int> refused{0};

    std::vector<std::thread> couriers;
    for (int i = 0; i < 8; ++i) {
        couriers.emplace_back([&] {
            if (!desk.enter(stop)) {
                ++refused;
                return;
            }
            {
                std::lock_guard<std::mutex> lock(counterMutex);
                ++inside;
                maxInside = std::max(maxInside, inside);
            }
            std::this_thread::sleep_for(5ms);
            {
                std::lock_guard<std::mutex> lock(counterMutex);
                --inside;
            }
            desk.leave();
        });
    }
    for (auto& t : couriers) {
        t.join();
    }

    REQUIRE(refused == 0);
    REQUIRE(maxInside <= 2);
    REQUIRE(desk.busySlots() == 0);
}

TEST_CASE("PickupDesk: atiende en orden de llegada", "[pickup_desk][concurrent]") {
    concurrency::PickupDesk desk(1);
    concurrency::ShutdownToken stop;
    std::mutex orderMutex;
    std::vector<int> served;

    REQUIRE(desk.enter(stop));

    std::vector<std::thread> couriers;
    for (int i = 0; i < 5; ++i) {
        couriers.emplace_back([&, i] {
            if (!desk.enter(stop)) {
                return;
            }
            {
                std::lock_guard<std::mutex> lock(orderMutex);
                served.push_back(i);
            }
            desk.leave();
        });
        std::this_thread::sleep_for(20ms);
    }

    desk.leave();
    for (auto& t : couriers) {
        t.join();
    }

    REQUIRE(served == std::vector<int>{0, 1, 2, 3, 4});
}

TEST_CASE("PickupDesk: quien espera en la fila despierta al pedir el apagado",
          "[pickup_desk][shutdown][concurrent]") {
    concurrency::PickupDesk desk(1);
    concurrency::ShutdownToken stop;
    REQUIRE(desk.enter(stop));

    auto waiter = std::async(std::launch::async, [&] { return desk.enter(stop); });
    std::this_thread::sleep_for(20ms);
    stop.requestStop();
    desk.wakeAll();

    REQUIRE(waiter.wait_for(2s) == std::future_status::ready);
    REQUIRE_FALSE(waiter.get());
}
