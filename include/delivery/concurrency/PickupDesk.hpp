#pragma once

#include <delivery/concurrency/ShutdownToken.hpp>

#include <condition_variable>
#include <cstdint>
#include <mutex>

namespace delivery::concurrency {
class PickupDesk {
public:
    explicit PickupDesk(int slots);

    PickupDesk(const PickupDesk&) = delete;
    PickupDesk& operator=(const PickupDesk&) = delete;

    bool enter(ShutdownToken& stop);

    void leave();

    void wakeAll();

    int slots() const noexcept { return slots_; }
    int busySlots() const;

private:
    const int slots_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    int busy_ = 0;
    std::uint64_t nextTicket_ = 0;
    std::uint64_t nowServing_ = 0;
};
}
