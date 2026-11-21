#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace delivery::viz {
struct FrameSample {
    double intervalMs = 0.0;
    double drawMs = 0.0;
    std::int64_t simMs = 0;
    double lockMs = 0.0;
    double couriersMs = 0.0;
};

struct FrameSummary {
    std::size_t frames = 0;
    double seconds = 0.0;
    double meanFps = 0.0;
    double p50IntervalMs = 0.0;
    double p95IntervalMs = 0.0;
    double maxIntervalMs = 0.0;
    double p50DrawMs = 0.0;
    double p95DrawMs = 0.0;
};

FrameSummary summarizeFrames(const std::vector<FrameSample>& samples);
std::string describeFrames(const FrameSummary& summary);
void writeFrameLog(const std::filesystem::path& path, const std::vector<FrameSample>& samples);
}
