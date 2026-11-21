#include <delivery/gpu/DeviceInfo.hpp>

#include <delivery/gpu/cuda_compat.h>

#include <sstream>

namespace delivery::gpu {
DeviceInfo queryDevice() {
    DeviceInfo info;
    info.backend = CUDA_BACKEND;

    int count = 0;
    if (cudaGetDeviceCount(&count) != cudaSuccess || count == 0) {
        return info;
    }
    cudaDeviceProp props{};
    if (cudaGetDeviceProperties(&props, 0) != cudaSuccess) {
        return info;
    }
    info.name = props.name;
    info.multiprocessors = props.multiProcessorCount;
    info.maxThreadsPerBlock = props.maxThreadsPerBlock;
    info.maxThreadsPerMultiprocessor = props.maxThreadsPerMultiProcessor;
    info.warpSize = props.warpSize;
    info.sharedMemPerBlock = props.sharedMemPerBlock;
    info.totalGlobalMem = props.totalGlobalMem;
    return info;
}

std::string describe(const DeviceInfo& info) {
    std::ostringstream out;
    out << "Compute backend: " << info.backend;
    if (!info.name.empty()) {
        out << " | " << info.name << " | " << info.multiprocessors << " SMs | "
            << info.maxThreadsPerMultiprocessor << " threads/SM | warp " << info.warpSize;
    } else {
        out << " | no device";
    }
    return out.str();
}
}
