#include <catch2/catch_test_macros.hpp>

#include <delivery/infra/EventLog.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

using namespace delivery;

namespace {
core::Order makeOrder(const std::string& id, std::int64_t createdAtMs) {
    core::Order o;
    o.id = core::OrderId(id);
    o.restaurant = core::RestaurantId("r0");
    o.createdAtMs = createdAtMs;
    return o;
}
}

TEST_CASE("EventLog: con muchos hilos escribiendo, cada linea es un JSON completo",
          "[event_log][concurrent]") {
    const auto path = std::filesystem::temp_directory_path() / "equipetrol_event_log_test.jsonl";
    constexpr int kThreads = 8;
    constexpr int kEventsPerThread = 500;
    {
        infra::EventLog log(path);
        std::vector<std::thread> writers;
        for (int w = 0; w < kThreads; ++w) {
            writers.emplace_back([&log, w] {
                for (int i = 0; i < kEventsPerThread; ++i) {
                    const std::string id = "o" + std::to_string(w) + "_" + std::to_string(i);
                    log.orderCreated(i, makeOrder(id, i));
                    log.orderAssigned(i, core::OrderId(id), core::CourierId("c" + std::to_string(w)));
                }
            });
        }
        for (auto& t : writers) {
            t.join();
        }
        log.simulationStopping(kEventsPerThread);
    }

    std::ifstream in(path);
    std::string line;
    int lines = 0;
    bool allValid = true;
    while (std::getline(in, line)) {
        ++lines;
        const auto j = nlohmann::json::parse(line, nullptr, false);
        if (j.is_discarded() || !j.contains("t") || !j.contains("event")) {
            allValid = false;
        }
    }
    in.close();
    std::filesystem::remove(path);

    REQUIRE(allValid);
    REQUIRE(lines == kThreads * kEventsPerThread * 2 + 1);
}

TEST_CASE("EventLog: el resumen coincide con los eventos registrados", "[event_log]") {
    infra::EventLog log;
    log.registerCouriers({core::CourierId("c0"), core::CourierId("c1")});

    log.orderCreated(0, makeOrder("o1", 0));
    log.orderCreated(1000, makeOrder("o2", 1000));
    log.orderCreated(2000, makeOrder("o3", 2000));
    log.orderCreated(3000, makeOrder("o4", 3000));

    log.orderAssigned(100, core::OrderId("o1"), core::CourierId("c0"));
    log.orderDelivered(60'000, core::OrderId("o1"), core::CourierId("c0"));
    log.orderAssigned(1100, core::OrderId("o2"), core::CourierId("c0"));
    log.courierBreakdown(5000, core::CourierId("c0"));
    log.orderReassigned(5000, core::OrderId("o2"), core::CourierId("c0"), core::CourierId("c1"));
    log.orderDelivered(121'000, core::OrderId("o2"), core::CourierId("c1"));
    log.orderRejected(2000, core::OrderId("o3"), core::RejectReason::QueueFull);

    const infra::Summary s = log.summary();
    REQUIRE(s.created == 4);
    REQUIRE(s.delivered == 2);
    REQUIRE(s.rejected == 1);
    REQUIRE(s.queueFull == 1);
    REQUIRE(s.pending == 1);
    REQUIRE(s.created == s.delivered + s.rejected + s.pending);
    REQUIRE(s.breakdowns == 1);
    REQUIRE(s.reassigned == 1);
    REQUIRE(s.avgDeliveryMs == 90'000);
    REQUIRE(s.maxDeliveryMs == 120'000);
    REQUIRE(s.deliveredPerCourier.at("c0") == 1);
    REQUIRE(s.deliveredPerCourier.at("c1") == 1);
}

TEST_CASE("EventLog: el resumen se imprime como un objeto JSON con las claves del brief",
          "[event_log]") {
    infra::EventLog log;
    log.registerCouriers({core::CourierId("c0")});
    const auto j = nlohmann::json::parse(infra::toJsonLine(log.summary()));

    for (const char* key : {"created", "delivered", "rejected", "pending", "reassigned",
                            "breakdowns", "avgDeliveryMs", "maxDeliveryMs",
                            "deliveredPerCourier"}) {
        REQUIRE(j.contains(key));
    }
    REQUIRE(j["deliveredPerCourier"]["c0"] == 0);
}
