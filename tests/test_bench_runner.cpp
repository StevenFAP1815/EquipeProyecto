#include <delivery/bench/BenchRunner.hpp>
#include <delivery/gpu/EventTimer.hpp>

#include <catch2/catch_test_macros.hpp>

#include <sstream>
#include <string>
#include <vector>

using namespace delivery::bench;

namespace {
std::vector<std::string> lines(const std::string& text) {
    std::vector<std::string> result;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        result.push_back(line);
    }
    return result;
}

BenchCase twoVersionCase(std::vector<std::string>& calls, bool secondCorrect) {
    BenchCase c;
    c.application = "demo";
    c.sizes = {"small", "large"};
    c.prepare = [&calls, secondCorrect](std::size_t) {
        return std::vector<Version>{
            {"cpu_1", [&calls] { calls.push_back("cpu_1"); return Measurement{1.0, true}; }},
            {"gpu_e2e", [&calls, secondCorrect] {
                 calls.push_back("gpu_e2e");
                 return Measurement{0.5, secondCorrect};
             }},
        };
    };
    return c;
}
}

TEST_CASE("BenchRunner: escribe la cabecera y una fila por repetición, versión y tamaño") {
    std::ostringstream csv;
    std::vector<std::string> calls;
    BenchOptions options;
    options.repetitions = 3;
    BenchRunner runner(csv, options);
    runner.run(twoVersionCase(calls, true));

    const auto rows = lines(csv.str());
    REQUIRE(rows.front() == "application,size,version,repetition,ms,correct");
    REQUIRE(rows.size() == 1 + 2 * 3 * 2);
    REQUIRE(runner.rowsWritten() == 12);
    REQUIRE(runner.failures() == 0);
    REQUIRE(rows[1] == "demo,small,cpu_1,0,1.0000,yes");
}

TEST_CASE("BenchRunner: el calentamiento no se registra y las versiones se intercalan") {
    std::ostringstream csv;
    std::vector<std::string> calls;
    BenchOptions options;
    options.repetitions = 2;
    BenchRunner runner(csv, options);
    runner.run(twoVersionCase(calls, true));

    const std::vector<std::string> perSize = {"cpu_1", "gpu_e2e", "cpu_1", "gpu_e2e",
                                              "cpu_1", "gpu_e2e"};
    REQUIRE(calls.size() == 2 * perSize.size());
    for (std::size_t i = 0; i < calls.size(); ++i) {
        REQUIRE(calls[i] == perSize[i % perSize.size()]);
    }
    REQUIRE(runner.rowsWritten() == 8);
}

TEST_CASE("BenchRunner: los resultados incorrectos se cuentan y se marcan") {
    std::ostringstream csv;
    std::vector<std::string> calls;
    BenchOptions options;
    options.repetitions = 1;
    options.warmup = false;
    BenchRunner runner(csv, options);
    runner.run(twoVersionCase(calls, false));

    REQUIRE(runner.failures() == 2);
    REQUIRE(csv.str().find("demo,large,gpu_e2e,0,0.5000,no") != std::string::npos);
}

TEST_CASE("BenchRunner: --bench-only omite las demás aplicaciones") {
    std::ostringstream csv;
    std::vector<std::string> calls;
    BenchOptions options;
    options.only = "other";
    BenchRunner runner(csv, options);
    runner.run(twoVersionCase(calls, true));

    REQUIRE(calls.empty());
    REQUIRE(runner.rowsWritten() == 0);
}

TEST_CASE("BenchRunner: el barrido de hilos duplica hasta el máximo") {
    REQUIRE(threadSweep(8) == std::vector<unsigned>{1, 2, 4, 8});
    REQUIRE(threadSweep(12) == std::vector<unsigned>{1, 2, 4, 8, 12});
    REQUIRE(threadSweep(1) == std::vector<unsigned>{1});
}

TEST_CASE("EventTimer: mide un intervalo no negativo") {
    delivery::gpu::EventTimer timer;
    timer.start();
    delivery::gpu::synchronizeDevice();
    REQUIRE(timer.stopMs() >= 0.0);
}
