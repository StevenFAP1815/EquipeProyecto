#pragma once

#include <delivery/concurrency/GpuWorker.hpp>
#include <delivery/concurrency/OrderBook.hpp>
#include <delivery/concurrency/ShutdownToken.hpp>
#include <delivery/infra/Config.hpp>
#include <delivery/infra/EventLog.hpp>
#include <delivery/sim/Fleet.hpp>
#include <delivery/sim/SimClock.hpp>

#include <opencv2/core.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <future>
#include <optional>
#include <vector>

namespace delivery::viz {
class Renderer {
public:
    Renderer(const infra::Config& config,
             const std::filesystem::path& imagePath,
             const sim::Fleet& fleet,
             const concurrency::OrderBook& book,
             const infra::EventLog& log,
             sim::SimClock& clock,
             concurrency::ShutdownToken& stop,
             std::filesystem::path frameLogPath = {},
             concurrency::GpuWorker* gpuWorker = nullptr,
             bool showHeatmap = false,
             std::filesystem::path screenshotPath = {});

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    void run();

private:
    cv::Point toScreen(double lat, double lon) const;
    void drawBackground();
    void drawCouriers(cv::Mat& frame) const;
    void drawPanel(cv::Mat& frame) const;
    bool windowClosed() const;
    std::vector<int> recentDemand() const;
    void updateHeatmap();
    void setHeatmap(const std::vector<int>& smoothed);
    void drawHeatmap(cv::Mat& frame) const;

    const infra::Config& config_;
    const sim::Fleet& fleet_;
    const concurrency::OrderBook& book_;
    const infra::EventLog& log_;
    sim::SimClock& clock_;
    concurrency::ShutdownToken& stop_;
    std::filesystem::path frameLogPath_;
    concurrency::GpuWorker* gpuWorker_;
    std::filesystem::path screenshotPath_;

    cv::Mat background_;
    bool showHeatmap_ = false;
    cv::Mat heatColor_;
    cv::Mat heatMask_;
    std::optional<std::future<std::vector<int>>> pendingHeat_;
    std::chrono::steady_clock::time_point lastHeatRequest_;
};
}
