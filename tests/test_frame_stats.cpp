#include <delivery/viz/FrameStats.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

using delivery::viz::FrameSample;
using delivery::viz::summarizeFrames;

TEST_CASE("Cuadros: sin muestras el resumen queda en cero") {
    const auto summary = summarizeFrames({});
    REQUIRE(summary.frames == 0);
    REQUIRE(summary.meanFps == 0.0);
}

TEST_CASE("Cuadros: FPS medio, percentiles y máximo de los intervalos") {
    std::vector<FrameSample> samples;
    for (int i = 1; i <= 20; ++i) {
        samples.push_back({static_cast<double>(i * 5), static_cast<double>(i), i * 100});
    }
    const auto summary = summarizeFrames(samples);
    REQUIRE(summary.frames == 20);
    REQUIRE(summary.seconds == Catch::Approx(1.05));
    REQUIRE(summary.meanFps == Catch::Approx(20.0 / 1.05));
    REQUIRE(summary.p50IntervalMs == 50.0);
    REQUIRE(summary.p95IntervalMs == 95.0);
    REQUIRE(summary.maxIntervalMs == 100.0);
    REQUIRE(summary.p50DrawMs == 10.0);
    REQUIRE(summary.p95DrawMs == 19.0);
}

TEST_CASE("Cuadros: el registro CSV tiene cabecera y una fila por cuadro") {
    const auto path = std::filesystem::temp_directory_path() / "frame_stats_test.csv";
    delivery::viz::writeFrameLog(path, {{33.5, 2.0, 1000}, {34.0, 2.5, 2000}});
    std::ifstream in(path);
    std::string header;
    std::string first;
    std::string second;
    std::getline(in, header);
    std::getline(in, first);
    std::getline(in, second);
    REQUIRE(header == "frame,interval_ms,draw_ms,sim_ms");
    REQUIRE(first == "0,33.500,2.000,1000");
    REQUIRE(second == "1,34.000,2.500,2000");
    in.close();
    std::filesystem::remove(path);
}
