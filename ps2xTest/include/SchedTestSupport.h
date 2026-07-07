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

#include <atomic>
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

    // Atomic 32-bit read from guest RAM. Lives here (rather than duplicated
    // per test file) because the poll helpers below need it, and any test
    // that only ever reads a control word can use it directly too.
    inline uint32_t rdramRead32(const std::vector<uint8_t> &rdram, uint32_t addr)
    {
        return std::atomic_ref<uint32_t>(
                   *reinterpret_cast<uint32_t *>(const_cast<uint8_t *>(rdram.data()) + addr))
            .load();
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

    // Waits for the guest executor to fully quiesce (every dispatched thread
    // has exited and g_activeThreads has settled back to <= 0). This is the
    // teardown/drain poll almost every scheduler workload test ends on; a
    // thin wrap of waitUntil so call sites collapse to one line without
    // changing the wait semantics.
    inline bool drainedWithin(std::chrono::milliseconds timeout)
    {
        return waitUntil([]
        {
            return g_activeThreads.load(std::memory_order_acquire) <= 0;
        }, timeout);
    }

    // Waits for a guest-RAM control word to reach an exact value, or a floor
    // (AtLeast) — the recurring "poll a flag/counter a fiber writes" shape.
    // Also thin wraps of waitUntil; no new timing behavior.
    inline bool waitForWord(const std::vector<uint8_t> &rdram, uint32_t addr,
                             uint32_t want, std::chrono::milliseconds timeout)
    {
        return waitUntil([&]
        {
            return rdramRead32(rdram, addr) == want;
        }, timeout);
    }

    inline bool waitForWordAtLeast(const std::vector<uint8_t> &rdram, uint32_t addr,
                                    uint32_t want, std::chrono::milliseconds timeout)
    {
        return waitUntil([&]
        {
            return rdramRead32(rdram, addr) >= want;
        }, timeout);
    }

    // Hands out a fresh, disjoint worker-stack base of `size` bytes on every
    // call, so scheduler tests never need to hand-pick non-overlapping
    // addresses to avoid a sibling fiber's stack in the same test. Bumped
    // monotonically for the life of the process; each returned range is
    // exactly [base, base + size), so distinct calls can never overlap
    // regardless of call order.
    inline uint32_t nextWorkerStackBase(uint32_t size)
    {
        static std::atomic<uint32_t> next{0x00510000u};
        return next.fetch_add(size, std::memory_order_relaxed);
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

    // RAII pair of scheduler semaphores: constructs both up front (exposed
    // via .a()/.b()) and deletes both on scope exit on every path — normal
    // fall-through, an assertion failure, or an early return — with no
    // hand-written guard/delete boilerplate at the call site. `create` /
    // `destroy` are passed in (rather than named directly here) because each
    // test file owns its own thin createSchedSema/deleteSchedSema wrappers
    // around the CreateSema/DeleteSema syscalls: file-local test scaffolding,
    // not part of the shared harness.
    struct SemaPair
    {
        using CreateFn = int32_t (*)(uint8_t *, PS2Runtime *, int, int);
        using DeleteFn = void (*)(uint8_t *, PS2Runtime *, int32_t);

        uint8_t *rdram;
        PS2Runtime *runtime;
        DeleteFn destroy;
        int32_t sidA;
        int32_t sidB;

        SemaPair(uint8_t *rdram_, PS2Runtime *runtime_, CreateFn create, DeleteFn destroy_,
                 int initA, int maxA, int initB, int maxB)
            : rdram(rdram_), runtime(runtime_), destroy(destroy_),
              sidA(create(rdram_, runtime_, initA, maxA)),
              sidB(create(rdram_, runtime_, initB, maxB))
        {
        }

        int32_t a() const { return sidA; }
        int32_t b() const { return sidB; }

        ~SemaPair()
        {
            destroy(rdram, runtime, sidA);
            destroy(rdram, runtime, sidB);
        }
    };

    // RAII non-fiber host worker that parks in async_guest_begin() for the
    // guest token (the interrupt-worker shape: g_currentThreadId == -1, so it
    // borrows the token rather than holding a fiber's own). Replaces the
    // hand-rolled atomic<bool> pair + std::thread duplicated across the
    // token-handoff regression cases: a caller just needs to know when the
    // worker won the token and when it finished; the destructor joins
    // unconditionally so callers never have to remember to.
    struct ParkedHostWorker
    {
        std::atomic<bool> acquired{false}, done{false};
        std::thread th;
        ParkedHostWorker() : th([this]
        {
            g_currentThreadId = -1;
            ps2sched::async_guest_begin();
            acquired.store(true, std::memory_order_release);
            ps2sched::async_guest_end();
            done.store(true, std::memory_order_release);
        })
        {
        }
        bool wonTokenWithin(std::chrono::milliseconds t)
        {
            return waitUntil([&] { return acquired.load(std::memory_order_acquire); }, t);
        }
        bool finishedWithin(std::chrono::milliseconds t)
        {
            return waitUntil([&] { return done.load(std::memory_order_acquire); }, t);
        }
        ~ParkedHostWorker()
        {
            if (th.joinable())
            {
                th.join();
            }
        }
    };
}
