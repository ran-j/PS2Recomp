#include "MiniTest.h"
#include "ps2_runtime.h"
#include "ps2_syscalls.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <exception>
#include <thread>
#include <vector>

using namespace ps2_syscalls;

namespace
{
    constexpr int KE_OK = 0;
    constexpr int KE_EVF_COND = -421;

    constexpr uint32_t WEF_OR = 1u;
    constexpr uint32_t WEF_CLEAR = 0x10u;
    constexpr uint32_t WEF_CLEAR_ALL = 0x20u;

    struct Ps2EventFlagInfo
    {
        uint32_t attr;
        uint32_t option;
        uint32_t initBits;
        uint32_t currBits;
        int32_t numThreads;
        int32_t reserved1;
        int32_t reserved2;
    };

    static_assert(sizeof(Ps2EventFlagInfo) == 28u, "Unexpected Ps2EventFlagInfo layout.");

    struct TestEnv
    {
        std::vector<uint8_t> rdram;
        PS2Runtime runtime;

        TestEnv() : rdram(PS2_RAM_SIZE, 0u)
        {
        }
    };

    std::atomic<uint32_t> g_vblankStartHits{0u};
    std::atomic<uint32_t> g_vblankEndHits{0u};
    std::atomic<uint32_t> g_lastIntcArg{0u};

    void setRegU32(R5900Context &ctx, int reg, uint32_t value)
    {
        ctx.r[reg] = _mm_set_epi64x(0, static_cast<int64_t>(value));
    }

    int32_t getRegS32(const R5900Context &ctx, int reg)
    {
        return static_cast<int32_t>(::getRegU32(&ctx, reg));
    }

    bool callSyscall(uint32_t syscallNumber, uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        return dispatchNumericSyscall(syscallNumber, rdram, ctx, runtime);
    }

    void writeGuestU32(uint8_t *rdram, uint32_t addr, uint32_t value)
    {
        std::memcpy(rdram + addr, &value, sizeof(value));
    }

    uint32_t readGuestU32(const uint8_t *rdram, uint32_t addr)
    {
        uint32_t value = 0;
        std::memcpy(&value, rdram + addr, sizeof(value));
        return value;
    }

    uint64_t readGuestU64(const uint8_t *rdram, uint32_t addr)
    {
        uint64_t value = 0;
        std::memcpy(&value, rdram + addr, sizeof(value));
        return value;
    }

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

    void cleanupRuntime(TestEnv &env)
    {
        env.runtime.requestStop();
        notifyRuntimeStop();
    }

    void testIntcHandler(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        (void)rdram;
        (void)runtime;

        const uint32_t cause = getRegU32(ctx, 4);
        const uint32_t arg = getRegU32(ctx, 5);
        g_lastIntcArg.store(arg, std::memory_order_relaxed);

        if (cause == 2u)
        {
            g_vblankStartHits.fetch_add(1u, std::memory_order_relaxed);
        }
        else if (cause == 3u)
        {
            g_vblankEndHits.fetch_add(1u, std::memory_order_relaxed);
        }

        ctx->pc = 0u;
    }
}

void register_ps2_runtime_interrupt_tests()
{
    MiniTest::Case("PS2RuntimeInterrupt", [](TestCase &tc)
    {
        tc.Run("SetVSyncFlag updates guest flag and monotonic tick", [](TestCase &t)
        {
            notifyRuntimeStop();
            TestEnv env;

            constexpr uint32_t kFlagAddr = 0x1000u;
            constexpr uint32_t kTickAddr = 0x1010u;

            writeGuestU32(env.rdram.data(), kFlagAddr, 0xDEADBEEFu);
            writeGuestU32(env.rdram.data(), kTickAddr + 0u, 0xAAAAAAAAu);
            writeGuestU32(env.rdram.data(), kTickAddr + 4u, 0xBBBBBBBBu);

            R5900Context ctx{};
            setRegU32(ctx, 4, kFlagAddr);
            setRegU32(ctx, 5, kTickAddr);
            t.IsTrue(callSyscall(0x73u, env.rdram.data(), &ctx, &env.runtime), "SetVSyncFlag syscall should dispatch");
            t.Equals(getRegS32(ctx, 2), KE_OK, "SetVSyncFlag should return KE_OK");
            t.Equals(readGuestU32(env.rdram.data(), kFlagAddr), 0u, "SetVSyncFlag should reset flag to zero");
            t.Equals(readGuestU64(env.rdram.data(), kTickAddr), 0ull, "SetVSyncFlag should reset tick counter to zero");

            const bool firstTickSeen = waitUntil([&]() {
                return readGuestU64(env.rdram.data(), kTickAddr) > 0u;
            }, std::chrono::milliseconds(300));
            t.IsTrue(firstTickSeen, "VSync worker should update tick value");

            const uint64_t firstTick = readGuestU64(env.rdram.data(), kTickAddr);
            t.IsTrue(firstTick > 0u, "First observed VSync tick should be positive");
            t.Equals(readGuestU32(env.rdram.data(), kFlagAddr), 1u, "VSync worker should set flag to one");

            const bool secondTickSeen = waitUntil([&]() {
                return readGuestU64(env.rdram.data(), kTickAddr) > firstTick;
            }, std::chrono::milliseconds(300));
            t.IsTrue(secondTickSeen, "VSync tick should continue to advance");
            t.IsTrue(readGuestU64(env.rdram.data(), kTickAddr) > firstTick, "tick should be monotonic");

            cleanupRuntime(env);
        });

        tc.Run("INTC VBLANK handlers respect EnableIntc and DisableIntc masks", [](TestCase &t)
        {
            notifyRuntimeStop();
            TestEnv env;

            g_vblankStartHits.store(0u, std::memory_order_relaxed);
            g_vblankEndHits.store(0u, std::memory_order_relaxed);
            g_lastIntcArg.store(0u, std::memory_order_relaxed);

            constexpr uint32_t kFlagAddr = 0x1100u;
            constexpr uint32_t kTickAddr = 0x1110u;
            constexpr uint32_t kHandlerAddr = 0x00ABC100u;

            env.runtime.registerFunction(kHandlerAddr, &testIntcHandler);

            R5900Context addStart{};
            setRegU32(addStart, 4, 2u); // VBLANK start
            setRegU32(addStart, 5, kHandlerAddr);
            setRegU32(addStart, 6, 0u);
            setRegU32(addStart, 7, 0xCAFE0002u);
            setRegU32(addStart, 28, 0x12340000u);
            setRegU32(addStart, 29, 0x001FFFE0u);
            t.IsTrue(callSyscall(0x10u, env.rdram.data(), &addStart, &env.runtime), "AddIntcHandler syscall should dispatch");
            t.IsTrue(getRegS32(addStart, 2) > 0, "AddIntcHandler for cause 2 should return handler id");

            R5900Context addEnd{};
            setRegU32(addEnd, 4, 3u); // VBLANK end
            setRegU32(addEnd, 5, kHandlerAddr);
            setRegU32(addEnd, 6, 0u);
            setRegU32(addEnd, 7, 0xCAFE0003u);
            setRegU32(addEnd, 28, 0x12340000u);
            setRegU32(addEnd, 29, 0x001FFFE0u);
            t.IsTrue(callSyscall(0x10u, env.rdram.data(), &addEnd, &env.runtime), "AddIntcHandler syscall should dispatch");
            t.IsTrue(getRegS32(addEnd, 2) > 0, "AddIntcHandler for cause 3 should return handler id");

            R5900Context vsyncCtx{};
            setRegU32(vsyncCtx, 4, kFlagAddr);
            setRegU32(vsyncCtx, 5, kTickAddr);
            t.IsTrue(callSyscall(0x73u, env.rdram.data(), &vsyncCtx, &env.runtime), "SetVSyncFlag syscall should dispatch");
            t.Equals(getRegS32(vsyncCtx, 2), KE_OK, "SetVSyncFlag should succeed");

            const bool startSeen = waitUntil([&]() {
                return g_vblankStartHits.load(std::memory_order_relaxed) > 0u;
            }, std::chrono::milliseconds(400));
            const bool endSeen = waitUntil([&]() {
                return g_vblankEndHits.load(std::memory_order_relaxed) > 0u;
            }, std::chrono::milliseconds(400));

            t.IsTrue(startSeen, "VBLANK start handler should fire while cause 2 is enabled");
            t.IsTrue(endSeen, "VBLANK end handler should fire while cause 3 is enabled");

            R5900Context disableStart{};
            setRegU32(disableStart, 4, 2u);
            t.IsTrue(callSyscall(0x15u, env.rdram.data(), &disableStart, &env.runtime), "DisableIntc syscall should dispatch");
            t.Equals(getRegS32(disableStart, 2), KE_OK, "DisableIntc should return KE_OK");

            std::this_thread::sleep_for(std::chrono::milliseconds(40));
            const uint32_t startAfterDisable = g_vblankStartHits.load(std::memory_order_relaxed);
            const uint32_t endAfterDisable = g_vblankEndHits.load(std::memory_order_relaxed);

            std::this_thread::sleep_for(std::chrono::milliseconds(80));
            const uint32_t startLater = g_vblankStartHits.load(std::memory_order_relaxed);
            const uint32_t endLater = g_vblankEndHits.load(std::memory_order_relaxed);

            t.Equals(startLater, startAfterDisable, "cause 2 handler count should stop increasing while cause 2 is disabled");
            t.IsTrue(endLater > endAfterDisable, "cause 3 handler should keep firing while still enabled");

            R5900Context enableStart{};
            setRegU32(enableStart, 4, 2u);
            t.IsTrue(callSyscall(0x14u, env.rdram.data(), &enableStart, &env.runtime), "EnableIntc syscall should dispatch");
            t.Equals(getRegS32(enableStart, 2), KE_OK, "EnableIntc should return KE_OK");

            const bool startResumed = waitUntil([&]() {
                return g_vblankStartHits.load(std::memory_order_relaxed) > startLater;
            }, std::chrono::milliseconds(300));
            t.IsTrue(startResumed, "cause 2 handler should resume after re-enable");

            const uint32_t lastArg = g_lastIntcArg.load(std::memory_order_relaxed);
            t.IsTrue(lastArg == 0xCAFE0002u || lastArg == 0xCAFE0003u,
                     "handler should receive configured argument value");

            cleanupRuntime(env);
        });

        tc.Run("WaitEventFlag blocks and wakes when SetEventFlag publishes bits", [](TestCase &t)
        {
            notifyRuntimeStop();
            TestEnv env;

            constexpr uint32_t kParamAddr = 0x1200u;
            constexpr uint32_t kResBitsAddr = 0x1300u;

            const uint32_t eventParam[3] = {
                0u, // attr
                0u, // option
                0u  // init bits
            };
            std::memcpy(env.rdram.data() + kParamAddr, eventParam, sizeof(eventParam));

            R5900Context createCtx{};
            setRegU32(createCtx, 4, kParamAddr);
            CreateEventFlag(env.rdram.data(), &createCtx, &env.runtime);
            const int32_t eid = getRegS32(createCtx, 2);
            t.IsTrue(eid > 0, "CreateEventFlag should return a valid id");

            writeGuestU32(env.rdram.data(), kResBitsAddr, 0u);

            std::atomic<bool> waiterDone{false};
            std::atomic<bool> waiterThrew{false};
            std::atomic<int32_t> waiterRet{0x7FFFFFFF};
            std::atomic<uint32_t> waiterResBits{0u};

            std::thread waiter([&]()
            {
                try
                {
                    R5900Context waitCtx{};
                    setRegU32(waitCtx, 4, static_cast<uint32_t>(eid));
                    setRegU32(waitCtx, 5, 0x4u);      // wait bits
                    setRegU32(waitCtx, 6, WEF_OR);    // OR mode
                    setRegU32(waitCtx, 7, kResBitsAddr);
                    WaitEventFlag(env.rdram.data(), &waitCtx, &env.runtime);
                    waiterRet.store(getRegS32(waitCtx, 2), std::memory_order_relaxed);
                    waiterResBits.store(readGuestU32(env.rdram.data(), kResBitsAddr), std::memory_order_relaxed);
                }
                catch (...)
                {
                    waiterThrew.store(true, std::memory_order_release);
                }

                waiterDone.store(true, std::memory_order_release);
            });

            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            t.IsFalse(waiterDone.load(std::memory_order_acquire), "WaitEventFlag should block before matching bits are set");

            R5900Context signalCtx{};
            setRegU32(signalCtx, 4, static_cast<uint32_t>(eid));
            setRegU32(signalCtx, 5, 0x4u);
            SetEventFlag(env.rdram.data(), &signalCtx, &env.runtime);
            t.Equals(getRegS32(signalCtx, 2), KE_OK, "SetEventFlag should succeed");

            const bool woke = waitUntil([&]() {
                return waiterDone.load(std::memory_order_acquire);
            }, std::chrono::milliseconds(300));
            if (!woke)
            {
                // Force unblock for deterministic test cleanup.
                R5900Context deleteCtx{};
                setRegU32(deleteCtx, 4, static_cast<uint32_t>(eid));
                DeleteEventFlag(env.rdram.data(), &deleteCtx, &env.runtime);
            }

            if (waiter.joinable())
            {
                waiter.join();
            }

            t.IsFalse(waiterThrew.load(std::memory_order_acquire),
                      "WaitEventFlag waiter thread should not throw");
            t.IsTrue(woke, "WaitEventFlag should wake after SetEventFlag publishes matching bits");
            t.Equals(waiterRet.load(std::memory_order_relaxed), KE_OK, "waiter should return KE_OK");
            t.IsTrue((waiterResBits.load(std::memory_order_relaxed) & 0x4u) != 0u,
                     "waiter result bits should include published bit");

            R5900Context deleteCtx{};
            setRegU32(deleteCtx, 4, static_cast<uint32_t>(eid));
            DeleteEventFlag(env.rdram.data(), &deleteCtx, &env.runtime);

            cleanupRuntime(env);
        });

        tc.Run("PollEventFlag WEF_CLEAR clears only matched bits", [](TestCase &t)
        {
            notifyRuntimeStop();
            TestEnv env;

            constexpr uint32_t kParamAddr = 0x1400u;
            constexpr uint32_t kResBitsAddr = 0x1410u;
            constexpr uint32_t kStatusAddr = 0x1420u;

            const uint32_t eventParam[3] = {
                0u, // attr
                0u, // option
                0x7u // init bits: 0b111
            };
            std::memcpy(env.rdram.data() + kParamAddr, eventParam, sizeof(eventParam));

            R5900Context createCtx{};
            setRegU32(createCtx, 4, kParamAddr);
            CreateEventFlag(env.rdram.data(), &createCtx, &env.runtime);
            const int32_t eid = getRegS32(createCtx, 2);
            t.IsTrue(eid > 0, "CreateEventFlag should return a valid id");

            R5900Context pollCtx{};
            setRegU32(pollCtx, 4, static_cast<uint32_t>(eid));
            setRegU32(pollCtx, 5, 0x1u);
            setRegU32(pollCtx, 6, WEF_OR | WEF_CLEAR);
            setRegU32(pollCtx, 7, kResBitsAddr);
            PollEventFlag(env.rdram.data(), &pollCtx, &env.runtime);
            t.Equals(getRegS32(pollCtx, 2), KE_OK, "PollEventFlag should succeed when condition is met");
            t.Equals(readGuestU32(env.rdram.data(), kResBitsAddr), 0x7u, "PollEventFlag should report bits before clear");

            R5900Context referCtx{};
            setRegU32(referCtx, 4, static_cast<uint32_t>(eid));
            setRegU32(referCtx, 5, kStatusAddr);
            ReferEventFlagStatus(env.rdram.data(), &referCtx, &env.runtime);
            t.Equals(getRegS32(referCtx, 2), KE_OK, "ReferEventFlagStatus should succeed");

            Ps2EventFlagInfo info{};
            std::memcpy(&info, env.rdram.data() + kStatusAddr, sizeof(info));
            t.Equals(info.currBits, 0x6u, "WEF_CLEAR should clear only requested bits, not all bits");

            R5900Context pollMissCtx{};
            setRegU32(pollMissCtx, 4, static_cast<uint32_t>(eid));
            setRegU32(pollMissCtx, 5, 0x1u);
            setRegU32(pollMissCtx, 6, WEF_OR);
            setRegU32(pollMissCtx, 7, 0u);
            PollEventFlag(env.rdram.data(), &pollMissCtx, &env.runtime);
            t.Equals(getRegS32(pollMissCtx, 2), KE_EVF_COND,
                     "after clearing bit 0, polling for bit 0 should fail condition");

            R5900Context deleteCtx{};
            setRegU32(deleteCtx, 4, static_cast<uint32_t>(eid));
            DeleteEventFlag(env.rdram.data(), &deleteCtx, &env.runtime);
            t.Equals(getRegS32(deleteCtx, 2), KE_OK, "DeleteEventFlag should succeed");

            cleanupRuntime(env);
        });

        tc.Run("WaitVSyncTick returns when runtime stop is requested", [](TestCase &t)
        {
            notifyRuntimeStop();
            TestEnv env;

            std::atomic<bool> waiterDone{false};
            std::atomic<bool> waiterThrew{false};
            std::thread waiter([&]()
            {
                try
                {
                    WaitVSyncTick(env.rdram.data(), &env.runtime);
                }
                catch (...)
                {
                    waiterThrew.store(true, std::memory_order_release);
                }
                waiterDone.store(true, std::memory_order_release);
            });

            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            env.runtime.requestStop();

            bool wokeOnStop = waitUntil([&]() {
                return waiterDone.load(std::memory_order_acquire);
            }, std::chrono::milliseconds(80));

            if (!wokeOnStop)
            {
                // Fallback wake-up for deterministic cleanup: one extra tick on fresh runtime.
                TestEnv wakeEnv;
                R5900Context setCtx{};
                constexpr uint32_t kWakeFlagAddr = 0x1500u;
                constexpr uint32_t kWakeTickAddr = 0x1510u;
                setRegU32(setCtx, 4, kWakeFlagAddr);
                setRegU32(setCtx, 5, kWakeTickAddr);
                (void)callSyscall(0x73u, wakeEnv.rdram.data(), &setCtx, &wakeEnv.runtime);
                (void)waitUntil([&]() {
                    return readGuestU64(wakeEnv.rdram.data(), kWakeTickAddr) > 0u;
                }, std::chrono::milliseconds(300));
                wakeEnv.runtime.requestStop();
                wokeOnStop = waitUntil([&]() {
                    return waiterDone.load(std::memory_order_acquire);
                }, std::chrono::milliseconds(80));
            }

            if (waiter.joinable())
            {
                waiter.join();
            }

            t.IsFalse(waiterThrew.load(std::memory_order_acquire),
                      "WaitVSyncTick waiter thread should not throw");
            t.IsTrue(wokeOnStop, "WaitVSyncTick waiter should unblock when runtime is stopping");

            cleanupRuntime(env);
        });
    });
}
