#pragma once

#include <delivery/concurrency/ChangeSignal.hpp>
#include <delivery/concurrency/ShutdownToken.hpp>
#include <delivery/core/StreetNetwork.hpp>
#include <delivery/infra/Config.hpp>
#include <delivery/sim/Courier.hpp>
#include <delivery/sim/SimClock.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

namespace delivery::sim {
class Fleet {
public:
    Fleet(const infra::Config& cfg,
          std::shared_ptr<const core::StreetNetwork> network,
          SimClock& clock,
          concurrency::ShutdownToken& stop,
          concurrency::ChangeSignal& freedSignal);

    ~Fleet();

    Fleet(const Fleet&) = delete;
    Fleet& operator=(const Fleet&) = delete;

    void start();

    void join();

    const std::vector<std::unique_ptr<Courier>>& couriers() const noexcept {
        return couriers_;
    }

    std::size_t size() const noexcept { return couriers_.size(); }

    Courier* findCourier(const core::CourierId& id) noexcept;

    std::uint64_t deliveredCount() const;
    std::uint64_t failedCount() const;
    std::uint64_t ordersInProgress() const;

private:
    std::vector<std::unique_ptr<Courier>> couriers_;
    std::vector<std::thread> threads_;
};
}
