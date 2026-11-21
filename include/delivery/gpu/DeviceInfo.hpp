#pragma once

#include <cstddef>
#include <string>

namespace delivery::gpu {
struct DeviceInfo {
    std::string backend;
    std::string name;
    int multiprocessors = 0;
    int maxThreadsPerBlock = 0;
    int maxThreadsPerMultiprocessor = 0;
    int warpSize = 0;
    std::size_t sharedMemPerBlock = 0;
    std::size_t totalGlobalMem = 0;
};

DeviceInfo queryDevice();
std::string describe(const DeviceInfo& info);
}
