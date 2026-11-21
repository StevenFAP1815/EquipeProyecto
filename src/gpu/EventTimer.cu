#include <delivery/gpu/EventTimer.hpp>

#include <delivery/gpu/cuda_compat.h>

namespace delivery::gpu {
#ifdef __CUDACC__
struct EventTimer::Impl {
    cudaEvent_t begin{};
    cudaEvent_t end{};
};

EventTimer::EventTimer() : impl_(std::make_unique<Impl>()) {
    CHECK(cudaEventCreate(&impl_->begin));
    CHECK(cudaEventCreate(&impl_->end));
}

EventTimer::~EventTimer() {
    cudaEventDestroy(impl_->begin);
    cudaEventDestroy(impl_->end);
}

void EventTimer::start() {
    CHECK(cudaEventRecord(impl_->begin));
}

double EventTimer::stopMs() {
    CHECK(cudaEventRecord(impl_->end));
    CHECK(cudaEventSynchronize(impl_->end));
    float ms = 0.0f;
    CHECK(cudaEventElapsedTime(&ms, impl_->begin, impl_->end));
    return ms;
}
#else
struct EventTimer::Impl {
    std::chrono::steady_clock::time_point begin;
};

EventTimer::EventTimer() : impl_(std::make_unique<Impl>()) {}

EventTimer::~EventTimer() = default;

void EventTimer::start() {
    impl_->begin = std::chrono::steady_clock::now();
}

double EventTimer::stopMs() {
    const auto elapsed = std::chrono::steady_clock::now() - impl_->begin;
    return std::chrono::duration<double, std::milli>(elapsed).count();
}
#endif

void synchronizeDevice() {
    CHECK(cudaDeviceSynchronize());
}
}
