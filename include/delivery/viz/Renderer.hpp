#pragma once

#include <delivery/concurrency/OrderBook.hpp>
#include <delivery/concurrency/ShutdownToken.hpp>
#include <delivery/infra/Config.hpp>
#include <delivery/sim/Fleet.hpp>
#include <delivery/sim/SimClock.hpp>

#include <opencv2/core.hpp>

#include <atomic>
#include <cstdint>
#include <filesystem>

namespace delivery::viz {
class Renderer {
public:
    Renderer(const infra::Config& config,
             const std::filesystem::path& imagePath,
             const sim::Fleet& fleet,
             const concurrency::OrderBook& book,
             const std::atomic<std::uint64_t>& createdOrders,
             sim::SimClock& clock,
             concurrency::ShutdownToken& stop);

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    void run();

private:
    cv::Point toScreen(double lat, double lon) const;
    void drawBackground();
    void drawCouriers(cv::Mat& frame) const;
    void drawPanel(cv::Mat& frame) const;
    bool windowClosed() const;

    const infra::Config& config_;
    const sim::Fleet& fleet_;
    const concurrency::OrderBook& book_;
    const std::atomic<std::uint64_t>& createdOrders_;
    sim::SimClock& clock_;
    concurrency::ShutdownToken& stop_;

    cv::Mat background_;
};
}
