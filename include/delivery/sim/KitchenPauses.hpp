#pragma once

#include <delivery/concurrency/ShutdownToken.hpp>
#include <delivery/infra/Config.hpp>
#include <delivery/infra/EventLog.hpp>
#include <delivery/sim/Fleet.hpp>
#include <delivery/sim/SimClock.hpp>

namespace delivery::sim {
class KitchenPauses {
public:
    KitchenPauses(const infra::Config& config,
                  Fleet& fleet,
                  SimClock& clock,
                  concurrency::ShutdownToken& stop,
                  infra::EventLog& log);

    KitchenPauses(const KitchenPauses&) = delete;
    KitchenPauses& operator=(const KitchenPauses&) = delete;

    void run();

private:
    const infra::Config& config_;
    Fleet& fleet_;
    SimClock& clock_;
    concurrency::ShutdownToken& stop_;
    infra::EventLog& log_;
};
}
