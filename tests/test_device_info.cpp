#include <delivery/gpu/DeviceInfo.hpp>

#include <catch2/catch_test_macros.hpp>

using delivery::gpu::describe;
using delivery::gpu::queryDevice;

TEST_CASE("DeviceInfo: el backend de cómputo se identifica y expone un dispositivo") {
    const auto info = queryDevice();
    REQUIRE_FALSE(info.backend.empty());
    REQUIRE_FALSE(info.name.empty());
    REQUIRE(info.multiprocessors > 0);
    REQUIRE(info.warpSize == 32);
    REQUIRE(info.maxThreadsPerBlock >= 1024);
}

TEST_CASE("DeviceInfo: la descripción nombra el backend y el dispositivo") {
    const auto info = queryDevice();
    const std::string text = describe(info);
    REQUIRE(text.find(info.backend) != std::string::npos);
    REQUIRE(text.find(info.name) != std::string::npos);
}
