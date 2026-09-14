#pragma once

#include <chrono>
#include <condition_variable>
#include <mutex>

namespace ee_host_pacing {
using Clock = std::chrono::steady_clock;

struct VBlankBoundary {
    Clock::time_point host;
    Clock::duration lateness;
    bool rebased;
};

inline VBlankBoundary vblankBoundary(Clock::time_point scheduled, Clock::time_point now,
                                     Clock::duration fieldPeriod) {
    const auto lateness = now > scheduled ? now - scheduled : Clock::duration::zero();
    const bool rebased = lateness > fieldPeriod;
    // Keep one field of catch-up credit: a slow two-field game frame should
    // not incur a fresh host wait between both already-earned guest VBlanks.
    return {rebased ? now - fieldPeriod : scheduled, lateness, rebased};
}

// Internal deterministic-clock seam. Production keeps this null; tests install
// hooks only on their executor thread, without changing scheduler object layout.
struct ClockHooks {
    void *context;
    Clock::time_point (*now)(void *);
    void (*waitUntil)(void *, Clock::time_point);
};
inline thread_local const ClockHooks *clockHooks = nullptr;

inline Clock::time_point now() {
    return clockHooks ? clockHooks->now(clockHooks->context) : Clock::now();
}

template <typename Predicate>
bool waitUntil(std::condition_variable &cv, std::unique_lock<std::mutex> &lock,
               Clock::time_point deadline, Predicate predicate) {
    if (!clockHooks)
        return cv.wait_until(lock, deadline, predicate);
    if (predicate())
        return true;
    clockHooks->waitUntil(clockHooks->context, deadline);
    return predicate();
}
}
