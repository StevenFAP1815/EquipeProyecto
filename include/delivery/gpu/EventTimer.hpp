#pragma once

#include <memory>

namespace delivery::gpu {
class EventTimer {
public:
    EventTimer();
    ~EventTimer();

    EventTimer(const EventTimer&) = delete;
    EventTimer& operator=(const EventTimer&) = delete;

    void start();
    double stopMs();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

void synchronizeDevice();
}
