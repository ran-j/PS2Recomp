// ---------------------------------------------------------------------------
// Shared scaffolding for scheduler-shaped tests (ps2_runtime_expansion_tests.cpp
// and ps2_scheduler_workload_regression_tests.cpp): the RAII fixture that owns the
// runtime + guest RAM for a single test, and the register-access / wait-poll
// helpers every scheduler test needs regardless of which file it lives in.
// ---------------------------------------------------------------------------
#pragma once

#include "ps2_runtime.h"
#include "ps2_scheduler.h"
#include "ps2_syscalls.h"
#include "runtime/ps2_memory.h"

#include <chrono>
#include <cstdint>
#include <thread>
#include <vector>

namespace ps2x_test
{
    // Binds a MIPS-ABI argument register to a 32-bit value.
    inline void setRegU32(R5900Context &ctx, int reg, uint32_t value)
    {
        ctx.r[reg] = _mm_set_epi64x(0, static_cast<int64_t>(value));
    }

    // Reads back a return-value register as a signed 32-bit result.
    inline int32_t getRegS32(const R5900Context &ctx, int reg)
    {
        return static_cast<int32_t>(::getRegU32(&ctx, reg));
    }

    // Polls `pred` at 1ms intervals until it is true or `timeout` elapses,
    // always giving `pred` one final check at the deadline.
    template <typename Predicate>
    bool waitUntil(Predicate pred, std::chrono::milliseconds timeout)
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (pred())
            {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return pred();
    }

    // Owns the runtime + guest RAM a scheduler test dispatches fibers against.
    // Construction clears residual thread/sema/handler state from a previous
    // test (notifyRuntimeStop) and brings up a fresh scheduler epoch
    // (scheduler_init, which also heals any g_activeThreads drift left by a
    // prior epoch's shutdown races); destruction tears the scheduler down and
    // requests the runtime stop, in that order, so a test body only needs to
    // add its own test-specific drains (signaling semas it created, joining
    // threads it spawned) before falling out of scope.
    struct SchedFixture
    {
        PS2Runtime runtime;
        std::vector<uint8_t> rdram = std::vector<uint8_t>(PS2_RAM_SIZE, 0u);

        SchedFixture()
        {
            ps2_syscalls::notifyRuntimeStop();
            ps2sched::scheduler_init();
        }

        ~SchedFixture()
        {
            ps2sched::scheduler_shutdown();
            runtime.requestStop();
        }
    };
}
