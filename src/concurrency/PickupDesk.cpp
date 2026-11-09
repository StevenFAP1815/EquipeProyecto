#include <delivery/concurrency/PickupDesk.hpp>

namespace delivery::concurrency {
PickupDesk::PickupDesk(int slots) : slots_(slots) {}

bool PickupDesk::enter(ShutdownToken& stop) {
    std::unique_lock<std::mutex> lock(mutex_);
    const std::uint64_t myTicket = nextTicket_++;

    cv_.wait(lock, [&] {
        return stop.stopRequested() || (myTicket == nowServing_ && busy_ < slots_);
    });
    if (stop.stopRequested()) {
        return false;
    }

    ++nowServing_;
    ++busy_;
    lock.unlock();
    cv_.notify_all();
    return true;
}

void PickupDesk::leave() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        --busy_;
    }
    cv_.notify_all();
}

void PickupDesk::wakeAll() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
    }
    cv_.notify_all();
}

int PickupDesk::busySlots() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return busy_;
}
}
