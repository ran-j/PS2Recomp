#include "MiniTest.h"
#include "ps2_runtime.h"
#include "ps2_syscalls.h"
#include "Stubs/DMA.h"
#include "Syscalls/Interrupt.h"
#include "runtime/ps2_gs_gpu.h"

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
    std::atomic<uint32_t> g_dmacSendHits{0u};
    std::atomic<uint32_t> g_dmacSendLastCause{0u};
    std::atomic<uint32_t> g_dmacSendLastChcr{0u};
    std::atomic<uint32_t> g_pendingIntcHits{0u};
    std::atomic<uint32_t> g_pendingIntcLastCause{0xFFFFFFFFu};

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

    void writeGuestU64(uint8_t *rdram, uint32_t addr, uint64_t value)
    {
        std::memcpy(rdram + addr, &value, sizeof(value));
    }

    uint64_t makeDmaTag(uint16_t qwc, uint8_t id, uint32_t addr, bool irq = false)
    {
        return static_cast<uint64_t>(qwc) |
               (static_cast<uint64_t>(id & 0x7u) << 28) |
               (irq ? (1ull << 31) : 0ull) |
               (static_cast<uint64_t>(addr & 0x7FFFFFFFu) << 32);
    }

    void writeDmaTag(uint8_t *rdram, uint32_t tagAddr, uint64_t tagLo)
    {
        std::memset(rdram + tagAddr, 0, 16);
        std::memcpy(rdram + tagAddr, &tagLo, sizeof(tagLo));
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
        // Defensive cleanup: reset the global INTC handler tables, enable masks,
        // and pending latch so each test starts from a known INTC state.
        resetInterruptHandlerState();
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

    void testDmacSendHandler(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        (void)rdram;

        const uint32_t cause = getRegU32(ctx, 4);
        g_dmacSendHits.fetch_add(1u, std::memory_order_relaxed);
        g_dmacSendLastCause.store(cause, std::memory_order_relaxed);

        uint32_t channelBase = 0u;
        if (cause == 0u)
        {
            channelBase = 0x10008000u;
        }
        else if (cause == 1u)
        {
            channelBase = 0x10009000u;
        }
        else if (cause == 2u)
        {
            channelBase = 0x1000A000u;
        }

        if (runtime && channelBase != 0u)
        {
            g_dmacSendLastChcr.store(runtime->memory().readIORegister(channelBase + 0x00u), std::memory_order_relaxed);
        }

        ctx->pc = 0u;
    }

    void testPendingIntcHandler(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        (void)rdram;
        (void)runtime;

        const uint32_t cause = getRegU32(ctx, 4);
        g_pendingIntcHits.fetch_add(1u, std::memory_order_relaxed);
        g_pendingIntcLastCause.store(cause, std::memory_order_relaxed);

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

        tc.Run("VSync worker updates GS CSR FIELD bit for MMIO polling loops", [](TestCase &t)
        {
            notifyRuntimeStop();
            TestEnv env;
            t.IsTrue(env.runtime.memory().initialize(), "runtime memory initialize should succeed");

            constexpr uint32_t kFlagAddr = 0x1080u;
            constexpr uint32_t kTickAddr = 0x1090u;
            constexpr uint64_t kGsCsrFieldMask = 0x2000ull;

            env.runtime.memory().gs().csr = 0x3ull;

            R5900Context ctx{};
            setRegU32(ctx, 4, kFlagAddr);
            setRegU32(ctx, 5, kTickAddr);
            t.IsTrue(callSyscall(0x73u, env.rdram.data(), &ctx, &env.runtime), "SetVSyncFlag syscall should dispatch");

            const uint64_t initialField = env.runtime.memory().gs().csr & kGsCsrFieldMask;
            const bool firstFieldFlip = waitUntil([&]() {
                return (env.runtime.memory().gs().csr & kGsCsrFieldMask) != initialField;
            }, std::chrono::milliseconds(300));
            t.IsTrue(firstFieldFlip, "VSync worker should toggle GS CSR FIELD for direct CSR polling");
            t.Equals(env.runtime.memory().gs().csr & 0x3ull, 0x3ull, "VSync FIELD update should preserve CSR status bits");

            const uint64_t fieldAfterFirstFlip = env.runtime.memory().gs().csr & kGsCsrFieldMask;
            const bool secondFieldFlip = waitUntil([&]() {
                return (env.runtime.memory().gs().csr & kGsCsrFieldMask) != fieldAfterFirstFlip;
            }, std::chrono::milliseconds(300));
            t.IsTrue(secondFieldFlip, "VSync worker should keep alternating GS CSR FIELD");

            cleanupRuntime(env);
        });

        // Regression test for the GS CSR data race: a two-writer word-level
        // lost-update guard. Pre-fix, every CSR update was a plain (non-atomic)
        // 64-bit load-modify-store of the WHOLE word, so two threads that own
        // logically disjoint bits could still clobber each other: thread A's
        // read-modify-write of the word can overwrite thread B's bit with the
        // stale value A loaded before B's update landed.
        //
        // Two racer threads with disjoint bit ownership run concurrently:
        //   - racer A owns SIGNAL (bit 0): sets it via the GIF register path
        //     (GS_REG_SIGNAL) then W1C-clears ONLY bit 0 via the MMIO write path;
        //   - racer B owns FINISH (bit 1): same protocol with GS_REG_FINISH and
        //     a W1C write of only bit 1.
        // Each racer checks only its own bit after each half-op. With the fix
        // (std::atomic CSR, every update a single atomic RMW) each racer is the
        // sole writer of its bit, so its bit deterministically reflects its own
        // last operation: zero anomalies are possible. Pre-fix, the racers'
        // whole-word W1C RMWs constantly interleave and lose each other's
        // set/clear, lighting up the anomaly counters.
        //
        // Why racer-vs-racer instead of racer-vs-vsync: the vsync worker (which
        // motivated the fix) writes CSR only once per ~16.7ms tick, a window far
        // too narrow to hit deterministically in a bounded test. The corrupting
        // mechanism -- a non-atomic whole-word RMW clobbering a concurrently
        // written disjoint bit -- is identical, so guarding it with two
        // high-frequency writers also guards the vsync FIELD interleaving. The
        // real vsync worker still runs throughout (started via the same
        // SetVSyncFlag syscall production uses) and its FIELD (bit 13) toggling
        // is asserted when at least two ticks were observed.
        tc.Run("Disjoint-bit GS CSR writers (SIGNAL vs FINISH vs vsync FIELD) never lose word-level updates", [](TestCase &t)
        {
            notifyRuntimeStop();
            TestEnv env;
            t.IsTrue(env.runtime.memory().initialize(), "runtime memory initialize should succeed");

            constexpr uint32_t kFlagAddr = 0x1180u;
            constexpr uint32_t kTickAddr = 0x1190u;
            constexpr uint64_t kGsCsrFieldMask = 0x2000ull;
            constexpr uint32_t kCsrAddr = PS2_GS_PRIV_REG_BASE + 0x1000u;
            constexpr uint32_t kIterations = 80000u;

            GS gs;
            gs.init(env.runtime.memory().getGSVRAM(), static_cast<uint32_t>(PS2_GS_VRAM_SIZE),
                    &env.runtime.memory().gs());

            // Drive the real vsync worker via the same syscall path production
            // code uses; it runs on its own thread and toggles CSR.FIELD once
            // per tick via updateGsCsrFieldForVSync.
            R5900Context ctx{};
            setRegU32(ctx, 4, kFlagAddr);
            setRegU32(ctx, 5, kTickAddr);
            t.IsTrue(callSyscall(0x73u, env.rdram.data(), &ctx, &env.runtime), "SetVSyncFlag syscall should dispatch");
            const uint64_t tickBefore = GetCurrentVSyncTick();

            std::atomic<uint32_t> setAnomaliesA{0u}, clearAnomaliesA{0u};
            std::atomic<uint32_t> setAnomaliesB{0u}, clearAnomaliesB{0u};
            std::atomic<uint32_t> racersDone{0u};

            // ownBit: the single CSR status bit this racer exclusively owns.
            // Each iteration: raise the bit via the GIF register-write path,
            // verify it reads back set, W1C-clear only that bit via the guest
            // MMIO path, verify it reads back clear. The other racer and the
            // vsync worker never touch this bit, so under atomic RMWs both
            // checks are exact -- any anomaly is a lost word-level update.
            auto racerBody = [&](uint8_t gifReg, uint64_t gifValue, uint64_t ownBit,
                                 std::atomic<uint32_t> &setAnomalies, std::atomic<uint32_t> &clearAnomalies) {
                for (uint32_t i = 0; i < kIterations; ++i)
                {
                    gs.writeRegister(gifReg, gifValue);
                    if ((env.runtime.memory().gs().csr.load() & ownBit) == 0ull)
                    {
                        setAnomalies.fetch_add(1u, std::memory_order_relaxed);
                    }

                    env.runtime.memory().write64(kCsrAddr, ownBit);
                    if ((env.runtime.memory().gs().csr.load() & ownBit) != 0ull)
                    {
                        clearAnomalies.fetch_add(1u, std::memory_order_relaxed);
                    }
                }
                racersDone.fetch_add(1u, std::memory_order_relaxed);
            };

            const uint64_t signalValue = (0xFFFFFFFFull << 32) | 0x11223344ull;
            std::thread racerA(racerBody, GS_REG_SIGNAL, signalValue, 0x1ull,
                               std::ref(setAnomaliesA), std::ref(clearAnomaliesA));
            std::thread racerB(racerBody, GS_REG_FINISH, 0ull, 0x2ull,
                               std::ref(setAnomaliesB), std::ref(clearAnomaliesB));

            // While the racers hammer bits 0..1, watch for CSR.FIELD (bit 13)
            // flips from the vsync worker. Polling ends when both racers finish,
            // so this adds no fixed wall-clock cost.
            const uint64_t initialField = env.runtime.memory().gs().csr.load() & kGsCsrFieldMask;
            bool fieldFlipped = false;
            while (racersDone.load(std::memory_order_relaxed) < 2u)
            {
                if ((env.runtime.memory().gs().csr.load() & kGsCsrFieldMask) != initialField)
                {
                    fieldFlipped = true;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }

            racerA.join();
            racerB.join();
            const uint64_t ticksElapsed = GetCurrentVSyncTick() - tickBefore;

            t.Equals(setAnomaliesA.load(), 0u, "racer A: SIGNAL set must never be lost to a concurrent whole-word CSR RMW");
            t.Equals(clearAnomaliesA.load(), 0u, "racer A: SIGNAL W1C-clear must never be lost to a concurrent whole-word CSR RMW");
            t.Equals(setAnomaliesB.load(), 0u, "racer B: FINISH set must never be lost to a concurrent whole-word CSR RMW");
            t.Equals(clearAnomaliesB.load(), 0u, "racer B: FINISH W1C-clear must never be lost to a concurrent whole-word CSR RMW");
            t.Equals(env.runtime.memory().gs().csr.load() & 0x3ull, 0x0ull,
                     "final CSR status bits must match both racers' ledgers (last op on each bit was a clear)");
            if (ticksElapsed >= 2u)
            {
                t.IsTrue(fieldFlipped, "VSync worker should toggle GS CSR FIELD while the racers run");
            }

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

        tc.Run("sceDmaSend dispatches completed VIF1 DMAC handler with latched END tag", [](TestCase &t)
        {
            notifyRuntimeStop();
            TestEnv env;
            t.IsTrue(env.runtime.memory().initialize(), "runtime memory initialize should succeed");

            constexpr uint32_t kHandlerAddr = 0x00ABD100u;
            constexpr uint32_t kVif1Ch = 0x10009000u;
            constexpr uint32_t kTag0 = 0x00028000u;
            constexpr uint32_t kTag1 = kTag0 + 0x20u;

            uint8_t *rdram = env.runtime.memory().getRDRAM();
            writeDmaTag(rdram, kTag0, makeDmaTag(1u, 1u, 0u, false)); // CNT
            writeGuestU64(rdram, kTag0 + 0x10u, 0u);
            writeGuestU64(rdram, kTag0 + 0x18u, 0u);
            writeDmaTag(rdram, kTag1, makeDmaTag(0u, 7u, 0u, false)); // END

            g_dmacSendHits.store(0u, std::memory_order_relaxed);
            g_dmacSendLastCause.store(0u, std::memory_order_relaxed);
            g_dmacSendLastChcr.store(0u, std::memory_order_relaxed);
            env.runtime.registerFunction(kHandlerAddr, &testDmacSendHandler);

            R5900Context addCtx{};
            setRegU32(addCtx, 4, 1u);
            setRegU32(addCtx, 5, kHandlerAddr);
            setRegU32(addCtx, 6, 0u);
            setRegU32(addCtx, 7, 0u);
            ps2_syscalls::AddDmacHandler(rdram, &addCtx, &env.runtime);
            t.IsTrue(getRegS32(addCtx, 2) > 0, "AddDmacHandler should register VIF1 handler");

            R5900Context enableCtx{};
            setRegU32(enableCtx, 4, 1u);
            ps2_syscalls::EnableDmac(rdram, &enableCtx, &env.runtime);
            t.Equals(getRegS32(enableCtx, 2), KE_OK, "EnableDmac should enable VIF1 cause");

            R5900Context sendCtx{};
            setRegU32(sendCtx, 4, kVif1Ch);
            setRegU32(sendCtx, 5, kTag0);
            ps2_stubs::sceDmaSend(rdram, &sendCtx, &env.runtime);

            t.Equals(getRegS32(sendCtx, 2), 0, "sceDmaSend should succeed");
            t.Equals(g_dmacSendHits.load(std::memory_order_relaxed), 1u, "sceDmaSend should dispatch the VIF1 DMAC handler");
            t.Equals(g_dmacSendLastCause.load(std::memory_order_relaxed), 1u, "DMAC handler should observe VIF1 cause");
            t.Equals(g_dmacSendLastChcr.load(std::memory_order_relaxed) & 0x100u, 0u, "handler should see VIF1 STR cleared");
            t.Equals(g_dmacSendLastChcr.load(std::memory_order_relaxed) & 0x70000000u, 0x70000000u, "handler should see the latched END tag id");

            cleanupRuntime(env);
        });

        tc.Run("MMIO VIF1 chain completion dispatches DMAC handler after CHCR store", [](TestCase &t)
        {
            notifyRuntimeStop();
            TestEnv env;
            t.IsTrue(env.runtime.memory().initialize(), "runtime memory initialize should succeed");

            constexpr uint32_t kHandlerAddr = 0x00ABD180u;
            constexpr uint32_t kVif1Ch = 0x10009000u;
            constexpr uint32_t kTag0 = 0x00028200u;
            constexpr uint32_t kTag1 = kTag0 + 0x20u;

            uint8_t *rdram = env.runtime.memory().getRDRAM();
            writeDmaTag(rdram, kTag0, makeDmaTag(1u, 1u, 0u, false)); // CNT
            writeGuestU64(rdram, kTag0 + 0x10u, 0u);
            writeGuestU64(rdram, kTag0 + 0x18u, 0u);
            writeDmaTag(rdram, kTag1, makeDmaTag(0u, 7u, 0u, false)); // END

            g_dmacSendHits.store(0u, std::memory_order_relaxed);
            g_dmacSendLastCause.store(0u, std::memory_order_relaxed);
            g_dmacSendLastChcr.store(0u, std::memory_order_relaxed);
            env.runtime.registerFunction(kHandlerAddr, &testDmacSendHandler);

            R5900Context addCtx{};
            setRegU32(addCtx, 4, 1u);
            setRegU32(addCtx, 5, kHandlerAddr);
            setRegU32(addCtx, 6, 0u);
            setRegU32(addCtx, 7, 0u);
            ps2_syscalls::AddDmacHandler(rdram, &addCtx, &env.runtime);
            t.IsTrue(getRegS32(addCtx, 2) > 0, "AddDmacHandler should register VIF1 handler");

            R5900Context enableCtx{};
            setRegU32(enableCtx, 4, 1u);
            ps2_syscalls::EnableDmac(rdram, &enableCtx, &env.runtime);
            t.Equals(getRegS32(enableCtx, 2), KE_OK, "EnableDmac should enable VIF1 cause");

            R5900Context storeCtx{};
            env.runtime.Store32(rdram, &storeCtx, kVif1Ch + 0x30u, kTag0);
            env.runtime.Store32(rdram, &storeCtx, kVif1Ch + 0x00u, 0x185u);

            t.Equals(g_dmacSendHits.load(std::memory_order_relaxed), 1u, "CHCR store should dispatch the VIF1 DMAC handler");
            t.Equals(g_dmacSendLastCause.load(std::memory_order_relaxed), 1u, "DMAC handler should observe VIF1 cause");
            t.Equals(g_dmacSendLastChcr.load(std::memory_order_relaxed) & 0x100u, 0u, "handler should see VIF1 STR cleared");
            t.Equals(g_dmacSendLastChcr.load(std::memory_order_relaxed) & 0x70000000u, 0x70000000u, "handler should see the latched END tag id");

            cleanupRuntime(env);
        });

        tc.Run("native GIF DMA MMIO kick dispatches completed DMAC handler", [](TestCase &t)
        {
            notifyRuntimeStop();
            TestEnv env;
            t.IsTrue(env.runtime.memory().initialize(), "runtime memory initialize should succeed");

            constexpr uint32_t kHandlerAddr = 0x00ABD1C0u;
            constexpr uint32_t kDStat = 0x1000E010u;
            constexpr uint32_t kDPcr = 0x1000E020u;
            constexpr uint32_t kTag0 = 0x00028400u;

            uint8_t *rdram = env.runtime.memory().getRDRAM();
            writeDmaTag(rdram, kTag0, makeDmaTag(1u, 7u, 0u, false)); // END
            writeGuestU64(rdram, kTag0 + 0x10u, 0x1122334455667788ull);
            writeGuestU64(rdram, kTag0 + 0x18u, 0x99AABBCCDDEEFF00ull);

            g_dmacSendHits.store(0u, std::memory_order_relaxed);
            g_dmacSendLastCause.store(0u, std::memory_order_relaxed);
            g_dmacSendLastChcr.store(0u, std::memory_order_relaxed);
            env.runtime.registerFunction(kHandlerAddr, &testDmacSendHandler);

            R5900Context addCtx{};
            setRegU32(addCtx, 4, 2u);
            setRegU32(addCtx, 5, kHandlerAddr);
            setRegU32(addCtx, 6, 0u);
            setRegU32(addCtx, 7, 0u);
            ps2_syscalls::AddDmacHandler(rdram, &addCtx, &env.runtime);
            t.IsTrue(getRegS32(addCtx, 2) > 0, "AddDmacHandler should register GIF handler");

            R5900Context enableCtx{};
            setRegU32(enableCtx, 4, 2u);
            ps2_syscalls::EnableDmac(rdram, &enableCtx, &env.runtime);
            t.Equals(getRegS32(enableCtx, 2), KE_OK, "EnableDmac should enable GIF cause");

            R5900Context kickCtx{};
            env.runtime.kickGifDmaChainFromMMIO(rdram, &kickCtx, 4u, 4u, kTag0, 0x105u);

            t.Equals(env.runtime.memory().readIORegister(kDPcr), 4u, "native GIF kick should preserve D_PCR write");
            t.IsTrue((env.runtime.memory().readIORegister(kDStat) & (1u << 2)) != 0u,
                     "native GIF kick should raise D_STAT GIF completion status");
            t.Equals(g_dmacSendHits.load(std::memory_order_relaxed), 1u,
                     "native GIF kick should dispatch the GIF DMAC handler");
            t.Equals(g_dmacSendLastCause.load(std::memory_order_relaxed), 2u,
                     "DMAC handler should observe GIF cause");
            t.Equals(g_dmacSendLastChcr.load(std::memory_order_relaxed) & 0x100u, 0u,
                     "handler should see GIF STR cleared");
            t.Equals(g_dmacSendLastChcr.load(std::memory_order_relaxed) & 0x70000000u, 0x70000000u,
                     "handler should see the latched END tag id");

            cleanupRuntime(env);
        });

        tc.Run("negative interrupt-safe EE syscall ids dispatch", [](TestCase &t)
        {
            notifyRuntimeStop();
            TestEnv env;

            constexpr uint32_t kEventParamAddr = 0x1200u;
            constexpr uint32_t kStatusAddr = 0x1210u;

            const uint32_t eventParam[3] = {
                0u,
                0u,
                0u
            };
            std::memcpy(env.rdram.data() + kEventParamAddr, eventParam, sizeof(eventParam));

            R5900Context createCtx{};
            setRegU32(createCtx, 4, kEventParamAddr);
            CreateEventFlag(env.rdram.data(), &createCtx, &env.runtime);
            const int32_t eid = getRegS32(createCtx, 2);
            t.IsTrue(eid > 0, "CreateEventFlag should return a valid event id");

            R5900Context disableIntcCtx{};
            setRegU32(disableIntcCtx, 4, 2u);
            t.IsTrue(callSyscall(static_cast<uint32_t>(-0x1B), env.rdram.data(), &disableIntcCtx, &env.runtime),
                     "negative iDisableIntc syscall id should dispatch");
            t.Equals(getRegS32(disableIntcCtx, 2), KE_OK, "negative iDisableIntc should return KE_OK");

            R5900Context enableIntcCtx{};
            setRegU32(enableIntcCtx, 4, 2u);
            t.IsTrue(callSyscall(static_cast<uint32_t>(-0x1A), env.rdram.data(), &enableIntcCtx, &env.runtime),
                     "negative iEnableIntc syscall id should dispatch");
            t.Equals(getRegS32(enableIntcCtx, 2), KE_OK, "negative iEnableIntc should return KE_OK");

            R5900Context disableDmacCtx{};
            setRegU32(disableDmacCtx, 4, 5u);
            t.IsTrue(callSyscall(static_cast<uint32_t>(-0x1D), env.rdram.data(), &disableDmacCtx, &env.runtime),
                     "negative iDisableDmac syscall id should dispatch");
            t.Equals(getRegS32(disableDmacCtx, 2), KE_OK, "negative iDisableDmac should return KE_OK");

            R5900Context enableDmacCtx{};
            setRegU32(enableDmacCtx, 4, 5u);
            t.IsTrue(callSyscall(static_cast<uint32_t>(-0x1C), env.rdram.data(), &enableDmacCtx, &env.runtime),
                     "negative iEnableDmac syscall id should dispatch");
            t.Equals(getRegS32(enableDmacCtx, 2), KE_OK, "negative iEnableDmac should return KE_OK");

            R5900Context setEventFlagCtx{};
            setRegU32(setEventFlagCtx, 4, static_cast<uint32_t>(eid));
            setRegU32(setEventFlagCtx, 5, 0x6u);
            t.IsTrue(callSyscall(static_cast<uint32_t>(-0x53), env.rdram.data(), &setEventFlagCtx, &env.runtime),
                     "negative iSetEventFlag syscall id should dispatch");
            t.Equals(getRegS32(setEventFlagCtx, 2), KE_OK, "negative iSetEventFlag should return KE_OK");

            R5900Context referCtx{};
            setRegU32(referCtx, 4, static_cast<uint32_t>(eid));
            setRegU32(referCtx, 5, kStatusAddr);
            ReferEventFlagStatus(env.rdram.data(), &referCtx, &env.runtime);
            t.Equals(getRegS32(referCtx, 2), KE_OK, "ReferEventFlagStatus should succeed after iSetEventFlag");
            t.Equals(readGuestU32(env.rdram.data(), kStatusAddr + 12u), 0x6u,
                     "negative iSetEventFlag should publish the requested bits");

            R5900Context deleteCtx{};
            setRegU32(deleteCtx, 4, static_cast<uint32_t>(eid));
            DeleteEventFlag(env.rdram.data(), &deleteCtx, &env.runtime);

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

        tc.Run("raisePendingIntc delivers to a registered handler on the next drain tick", [](TestCase &t)
        {
            notifyRuntimeStop();
            TestEnv env;
            stopInterruptWorker();
            interrupt_state::g_pending_intc_causes.store(0u);

            constexpr uint32_t kCause = 5u;
            constexpr uint32_t kHandlerAddr = 0x00ABE100u;

            g_pendingIntcHits.store(0u, std::memory_order_relaxed);
            g_pendingIntcLastCause.store(0xFFFFFFFFu, std::memory_order_relaxed);

            env.runtime.registerFunction(kHandlerAddr, &testPendingIntcHandler);

            R5900Context addCtx{};
            setRegU32(addCtx, 4, kCause);
            setRegU32(addCtx, 5, kHandlerAddr);
            setRegU32(addCtx, 6, 0u);
            setRegU32(addCtx, 7, 0u);
            t.IsTrue(callSyscall(0x10u, env.rdram.data(), &addCtx, &env.runtime), "AddIntcHandler syscall should dispatch");
            t.IsTrue(getRegS32(addCtx, 2) > 0, "AddIntcHandler for cause 5 should return handler id");

            R5900Context enableCtx{};
            setRegU32(enableCtx, 4, kCause);
            t.IsTrue(callSyscall(0x14u, env.rdram.data(), &enableCtx, &env.runtime), "EnableIntc syscall should dispatch");

            // AddIntcHandler auto-starts the interrupt worker thread; stop it so
            // this test thread is the only drainer (avoids a double-dispatch race
            // between the worker's own per-tick drain and the manual drain below).
            stopInterruptWorker();

            raisePendingIntc(kCause);
            t.Equals(interrupt_state::g_pending_intc_causes.load() & (1u << kCause), (1u << kCause),
                     "pending bit 5 should be set after raise");

            drainPendingIntc(env.rdram.data(), &env.runtime);

            t.Equals(g_pendingIntcHits.load(std::memory_order_relaxed), 1u, "handler should run exactly once");
            t.Equals(g_pendingIntcLastCause.load(std::memory_order_relaxed), kCause,
                     "handler should observe cause 5 in $a0");
            t.Equals(interrupt_state::g_pending_intc_causes.load() & (1u << kCause), 0u,
                     "pending bit 5 should be cleared after delivery");

            cleanupRuntime(env);
        });

        tc.Run("undelivered pending cause survives the age window then drops", [](TestCase &t)
        {
            notifyRuntimeStop();
            TestEnv env;
            stopInterruptWorker();
            interrupt_state::g_pending_intc_causes.store(0u);

            // No cause-5 handler is registered in this test, and cleanupRuntime
            // resets the process-global INTC handler table between tests, so the
            // handler table starts empty here by construction (order-independent).
            // Every drain therefore finds no registered+enabled handler for
            // cause 5 and is a no-op until the age-out threshold fires.
            constexpr uint32_t kCause = 5u;

            raisePendingIntc(kCause);
            t.Equals(interrupt_state::g_pending_intc_causes.load() & (1u << kCause), (1u << kCause),
                     "pending bit should be set after raise");

            for (uint32_t i = 0; i < interrupt_state::kPendingIntcMaxAgeTicks; ++i)
            {
                drainPendingIntc(env.rdram.data(), &env.runtime);
            }
            t.Equals(interrupt_state::g_pending_intc_causes.load() & (1u << kCause), (1u << kCause),
                     "pending bit should survive exactly kPendingIntcMaxAgeTicks undelivered drain ticks");

            drainPendingIntc(env.rdram.data(), &env.runtime);
            t.Equals(interrupt_state::g_pending_intc_causes.load() & (1u << kCause), 0u,
                     "pending bit should age out and drop on the 121st undelivered drain");

            raisePendingIntc(kCause);
            t.Equals(interrupt_state::g_pending_intc_causes.load() & (1u << kCause), (1u << kCause),
                     "re-raise after drop should set the pending bit again");

            drainPendingIntc(env.rdram.data(), &env.runtime);
            t.Equals(interrupt_state::g_pending_intc_causes.load() & (1u << kCause), (1u << kCause),
                     "pending bit should survive a single drain after re-raise since its age restarted");

            cleanupRuntime(env);
        });

        tc.Run("pending cause persists across the raise-vs-registration race", [](TestCase &t)
        {
            notifyRuntimeStop();
            TestEnv env;
            stopInterruptWorker();
            interrupt_state::g_pending_intc_causes.store(0u);

            constexpr uint32_t kCause = 5u;
            constexpr uint32_t kHandlerAddr = 0x00ABE200u;

            g_pendingIntcHits.store(0u, std::memory_order_relaxed);
            g_pendingIntcLastCause.store(0xFFFFFFFFu, std::memory_order_relaxed);

            raisePendingIntc(kCause);
            t.Equals(interrupt_state::g_pending_intc_causes.load() & (1u << kCause), (1u << kCause),
                     "pending bit should be set after raise");

            drainPendingIntc(env.rdram.data(), &env.runtime);
            t.Equals(interrupt_state::g_pending_intc_causes.load() & (1u << kCause), (1u << kCause),
                     "pending bit should survive a drain while no handler is registered yet");

            env.runtime.registerFunction(kHandlerAddr, &testPendingIntcHandler);

            R5900Context addCtx{};
            setRegU32(addCtx, 4, kCause);
            setRegU32(addCtx, 5, kHandlerAddr);
            setRegU32(addCtx, 6, 0u);
            setRegU32(addCtx, 7, 0u);
            t.IsTrue(callSyscall(0x10u, env.rdram.data(), &addCtx, &env.runtime), "AddIntcHandler syscall should dispatch");
            t.IsTrue(getRegS32(addCtx, 2) > 0, "AddIntcHandler for cause 5 should return handler id");

            R5900Context enableCtx{};
            setRegU32(enableCtx, 4, kCause);
            t.IsTrue(callSyscall(0x14u, env.rdram.data(), &enableCtx, &env.runtime), "EnableIntc syscall should dispatch");

            // AddIntcHandler auto-starts the worker thread; it may legitimately
            // deliver the still-pending cause on its own tick before we stop it.
            // Either that path or the manual drain below is correct -- the
            // level-triggered bit guarantees exactly one delivery either way.
            stopInterruptWorker();

            drainPendingIntc(env.rdram.data(), &env.runtime);

            t.Equals(g_pendingIntcHits.load(std::memory_order_relaxed), 1u,
                     "handler should run exactly once total, delivered by the worker or the manual drain");
            t.Equals(g_pendingIntcLastCause.load(std::memory_order_relaxed), kCause,
                     "delivered handler should observe cause 5 in $a0");
            t.Equals(interrupt_state::g_pending_intc_causes.load() & (1u << kCause), 0u,
                     "pending bit should be cleared after delivery");

            cleanupRuntime(env);
        });

        tc.Run("vblank causes are excluded from the pending latch", [](TestCase &t)
        {
            notifyRuntimeStop();
            TestEnv env;
            stopInterruptWorker();
            interrupt_state::g_pending_intc_causes.store(0u);

            raisePendingIntc(2u);  // VBLANK start
            raisePendingIntc(3u);  // VBLANK end
            raisePendingIntc(32u); // out of range

            t.Equals(interrupt_state::g_pending_intc_causes.load(), 0u,
                     "vblank causes and out-of-range causes must never set the pending latch");

            cleanupRuntime(env);
        });
    });
}
