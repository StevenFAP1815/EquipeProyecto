#pragma once

namespace delivery::gpu {
struct KernelTiming {
    double kernelMs = 0.0;
    double copyMs = 0.0;
};
}
