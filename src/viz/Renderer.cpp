#include <delivery/viz/Renderer.hpp>

#include <opencv2/highgui.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <map>
#include <mutex>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace delivery::viz {
namespace {
const char* const kWindowName = "Equipetrol Delivery";
constexpr int kMaxWindowWidth = 900;
constexpr int kMaxWindowHeight = 600;
constexpr int kFrameMs = 33;
constexpr int kEscKey = 27;

const cv::Scalar kStreetColor(200, 120, 40);
const cv::Scalar kRestaurantColor(40, 40, 200);
const cv::Scalar kTextColor(20, 20, 20);
const cv::Scalar kPanelColor(255, 255, 255);
const cv::Scalar kDeliveryColor(0, 140, 0);

cv::Scalar colorFor(sim::CourierState state) {
    switch (state) {
        case sim::CourierState::Idle:               return {130, 130, 130};
        case sim::CourierState::MovingToRestaurant: return {220, 120, 0};
        case sim::CourierState::PickingUp:          return {0, 165, 255};
        case sim::CourierState::Delivering:         return {0, 170, 0};
        case sim::CourierState::BrokenDown:         return {0, 0, 230};
    }
    return {0, 0, 0};
}

void drawLabel(cv::Mat& frame, const std::string& text, cv::Point origin) {
    cv::putText(frame, text, origin, cv::FONT_HERSHEY_SIMPLEX, 0.5,
                kTextColor, 1, cv::LINE_AA);
}
}

Renderer::Renderer(const infra::Config& config,
                   const std::filesystem::path& imagePath,
                   const sim::Fleet& fleet,
                   const concurrency::OrderBook& book,
                   const infra::EventLog& log,
                   sim::SimClock& clock,
                   concurrency::ShutdownToken& stop)
    : config_(config),
      fleet_(fleet),
      book_(book),
      log_(log),
      clock_(clock),
      stop_(stop) {
    const cv::Mat original = cv::imread(imagePath.string());
    if (original.empty()) {
        throw std::runtime_error("cannot load map image: " + imagePath.string());
    }

    const double scale = std::min(
        {1.0,
         static_cast<double>(kMaxWindowWidth) / original.cols,
         static_cast<double>(kMaxWindowHeight) / original.rows});
    cv::resize(original, background_, cv::Size(), scale, scale, cv::INTER_AREA);

    drawBackground();
}

cv::Point Renderer::toScreen(double lat, double lon) const {
    const core::Pixel p =
        config_.map.bounds.toPixel({lat, lon}, background_.cols, background_.rows);
    return {p.x, p.y};
}

void Renderer::drawBackground() {
    for (const core::Street& street : config_.streets) {
        const core::Node& from = config_.nodes[config_.nodeIndex.at(street.from)];
        const core::Node& to = config_.nodes[config_.nodeIndex.at(street.to)];
        cv::line(background_, toScreen(from.lat, from.lon), toScreen(to.lat, to.lon),
                 kStreetColor, 2, cv::LINE_AA);
    }

    std::map<core::NodeId, int> labelsAtNode;
    for (const core::Restaurant& restaurant : config_.restaurants) {
        const core::Node& node = config_.nodes[config_.nodeIndex.at(restaurant.node)];
        const cv::Point p = toScreen(node.lat, node.lon);
        cv::rectangle(background_, p - cv::Point(6, 6), p + cv::Point(6, 6),
                      kRestaurantColor, cv::FILLED);
        const int previousLabels = labelsAtNode[restaurant.node]++;
        drawLabel(background_, restaurant.name, p + cv::Point(9, 5 + 16 * previousLabels));
    }

    const std::string attribution = config_.map.attribution;
    const cv::Point corner(background_.cols - 290, background_.rows - 10);
    cv::rectangle(background_, corner + cv::Point(-6, -18),
                  cv::Point(background_.cols, background_.rows), kPanelColor, cv::FILLED);
    drawLabel(background_, attribution, corner);
}

void Renderer::drawCouriers(cv::Mat& frame) const {
    std::shared_lock<std::shared_mutex> snapshot(fleet_.transferMutex());
    const std::vector<sim::Courier*> active = fleet_.activeCouriers();
    for (const sim::Courier* courier : active) {
        for (const core::NodeId& delivery : courier->deliveryNodesInBag()) {
            const core::Node& node = config_.nodes[config_.nodeIndex.at(delivery)];
            const cv::Point d = toScreen(node.lat, node.lon);
            cv::drawMarker(frame, d, kDeliveryColor, cv::MARKER_TILTED_CROSS, 14, 3);
        }
    }

    for (const sim::Courier* courier : active) {
        const core::LatLon pos = courier->position();
        const cv::Point p = toScreen(pos.lat, pos.lon);
        cv::circle(frame, p, 7, colorFor(courier->state()), cv::FILLED, cv::LINE_AA);
        cv::circle(frame, p, 7, cv::Scalar(0, 0, 0), 1, cv::LINE_AA);
    }
}

void Renderer::drawPanel(cv::Mat& frame) const {
    cv::rectangle(frame, cv::Point(0, 0), cv::Point(250, 338), kPanelColor, cv::FILLED);

    const infra::Summary s = log_.summary();
    const std::int64_t simSeconds = clock_.nowMs() / 1000;
    drawLabel(frame, "Tiempo simulado: " + std::to_string(simSeconds) + " s", {10, 22});
    drawLabel(frame, "Creados: " + std::to_string(s.created) +
                     "   Pendientes: " + std::to_string(s.pending), {10, 44});
    drawLabel(frame, "Entregados: " + std::to_string(s.delivered), {10, 66});
    drawLabel(frame, "Prom. entrega: " + std::to_string(s.avgDeliveryMs / 1000) + " s", {10, 88});
    drawLabel(frame, "En el libro: " + std::to_string(book_.size()), {10, 110});
    drawLabel(frame, "Rechazados: " + std::to_string(s.rejected), {10, 132});
    drawLabel(frame, "  queueFull: " + std::to_string(s.queueFull), {10, 154});
    drawLabel(frame, "  unreachable: " + std::to_string(s.unreachable), {10, 176});
    drawLabel(frame, "  noCourier: " + std::to_string(s.noCourier), {10, 198});
    drawLabel(frame, "Averias: " + std::to_string(s.breakdowns) +
                     "   Reasignados: " + std::to_string(s.reassigned), {10, 220});

    const sim::CourierState states[] = {
        sim::CourierState::Idle, sim::CourierState::MovingToRestaurant,
        sim::CourierState::PickingUp, sim::CourierState::Delivering,
        sim::CourierState::BrokenDown};
    int y = 246;
    for (sim::CourierState state : states) {
        cv::circle(frame, {18, y - 5}, 6, colorFor(state), cv::FILLED, cv::LINE_AA);
        drawLabel(frame, sim::toString(state), {32, y});
        y += 20;
    }
}

bool Renderer::windowClosed() const {
    return cv::getWindowProperty(kWindowName, cv::WND_PROP_VISIBLE) < 1.0;
}

void Renderer::run() {
    cv::namedWindow(kWindowName, cv::WINDOW_AUTOSIZE | cv::WINDOW_GUI_NORMAL);
    bool firstFrame = true;

    while (!stop_.stopRequested()) {
        cv::Mat frame = background_.clone();
        drawCouriers(frame);
        drawPanel(frame);
        cv::imshow(kWindowName, frame);

        const int key = cv::waitKey(kFrameMs);
        if (firstFrame) {
            cv::moveWindow(kWindowName, 0, 0);
            firstFrame = false;
        }
        if (key == kEscKey || windowClosed()) {
            stop_.requestStop();
            clock_.wakeAll();
        }
    }

    cv::destroyAllWindows();
}
}
