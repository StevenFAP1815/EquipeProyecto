#pragma once

#include <chrono>

namespace delivery::bench {
class Stopwatch {
public:
    Stopwatch() : start_(std::chrono::steady_clock::now()) {}

    double elapsedMs() const {
        const auto elapsed = std::chrono::steady_clock::now() - start_;
        return std::chrono::duration<double, std::milli>(elapsed).count();
    }

private:
    std::chrono::steady_clock::time_point start_;
};
}
