#include <delivery/viz/FrameStats.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace delivery::viz {
namespace {
double percentile(std::vector<double> values, double fraction) {
    std::sort(values.begin(), values.end());
    const auto rank = static_cast<std::size_t>(std::ceil(fraction * static_cast<double>(values.size())));
    return values[std::max<std::size_t>(rank, 1) - 1];
}
}

FrameSummary summarizeFrames(const std::vector<FrameSample>& samples) {
    FrameSummary summary;
    if (samples.empty()) {
        return summary;
    }
    std::vector<double> intervals;
    std::vector<double> draws;
    for (const FrameSample& s : samples) {
        intervals.push_back(s.intervalMs);
        draws.push_back(s.drawMs);
        summary.seconds += s.intervalMs / 1000.0;
    }
    summary.frames = samples.size();
    summary.meanFps = summary.seconds > 0.0 ? static_cast<double>(summary.frames) / summary.seconds : 0.0;
    summary.p50IntervalMs = percentile(intervals, 0.50);
    summary.p95IntervalMs = percentile(intervals, 0.95);
    summary.maxIntervalMs = *std::max_element(intervals.begin(), intervals.end());
    summary.p50DrawMs = percentile(draws, 0.50);
    summary.p95DrawMs = percentile(draws, 0.95);
    return summary;
}

std::string describeFrames(const FrameSummary& summary) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(1) << "Window: " << summary.frames << " frames in "
        << summary.seconds << " s, " << summary.meanFps << " FPS; frame interval p50 "
        << summary.p50IntervalMs << " ms, p95 " << summary.p95IntervalMs << " ms, max "
        << summary.maxIntervalMs << " ms; drawing p50 " << summary.p50DrawMs << " ms, p95 "
        << summary.p95DrawMs << " ms";
    return out.str();
}

void writeFrameLog(const std::filesystem::path& path, const std::vector<FrameSample>& samples) {
    std::ofstream out(path);
    if (!out) {
        throw std::runtime_error("cannot write frame log: " + path.string());
    }
    out << "frame,interval_ms,draw_ms,sim_ms\n" << std::fixed << std::setprecision(3);
    for (std::size_t i = 0; i < samples.size(); ++i) {
        out << i << ',' << samples[i].intervalMs << ',' << samples[i].drawMs << ','
            << samples[i].simMs << '\n';
    }
}
}
