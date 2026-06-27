#include "MiniTest.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "ps2_syscalls.h"
#include "ps2_stubs.h"

#include <chrono>
#include <array>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <thread>
#include <vector>

// g_currentThreadId is an `inline thread_local int` defined in the kernel's
// internal State.h (ps2xRuntime/.../Kernel/Syscalls/Helpers/State.h, default 1).
// Only sub-case H of the semaphore-return-value test reaches into it: the worker
// thread sets its own guest tid so ReleaseWaitThread(tid) can target the exact
// ThreadInfo that the worker's WaitSema put into THS_WAIT.
//
// ODR-safety: this declaration MUST stay byte-for-byte type-compatible with that
// definition (`thread_local int`, same name, no namespace). It is an `extern`
// declaration of an existing inline thread_local, NOT a second definition, so the
// linker binds to the runtime's instance. If the runtime ever changes the type or
// moves it into a namespace, update this line in lockstep or the build will break.
extern thread_local int g_currentThreadId;

using namespace ps2_syscalls;

namespace
{
    constexpr uint32_t K_PARAM_ADDR = 0x1000u;
    constexpr uint32_t K_STATUS_ADDR = 0x1400u;

    constexpr int KE_OK = 0;
    constexpr int KE_ERROR = -1;
    constexpr int KE_ILLEGAL_THID = -406;
    constexpr int KE_UNKNOWN_THID = -407;
    constexpr int KE_UNKNOWN_SEMID = -408;
    constexpr int KE_DORMANT = -413;
    constexpr int KE_SEMA_ZERO = -419;
    constexpr int KE_SEMA_OVF = -420;
    constexpr int KE_WAIT_DELETE = -425;
    constexpr int KE_RELEASE_WAIT = -418;
    constexpr uint32_t K_SEMA_WAIT_READY_ADDR = 0x1900u;

    constexpr int THS_WAIT = 0x04;
    constexpr int THS_SUSPEND = 0x08;
    constexpr int THS_WAITSUSPEND = 0x0C;
    constexpr int THS_DORMANT = 0x10;
    constexpr uint32_t TSW_SEMA = 2u;
    constexpr uint32_t TSW_EVENT = 3u;

    constexpr uint32_t K_EVENT_WAIT_READY_ADDR = 0x1800u;
    constexpr uint32_t K_EVENT_WAIT_GATE_ADDR = 0x1804u;
    constexpr uint32_t K_TERMINATE_SEMA_WAIT_READY_ADDR = 0x1810u;

    struct EeThreadStatus
    {
        int32_t status;
        uint32_t func;
        uint32_t stack;
        int32_t stack_size;
        uint32_t gp_reg;
        int32_t initial_priority;
        int32_t current_priority;
        uint32_t attr;
        uint32_t option;
        uint32_t waitType;
        uint32_t waitId;
        uint32_t wakeupCount;
    };

    struct EeSemaStatus
    {
        int32_t count;
        int32_t max_count;
        int32_t init_count;
        int32_t wait_threads;
        uint32_t attr;
        uint32_t option;
    };

    static_assert(sizeof(EeThreadStatus) == 0x30u, "Unexpected ee_thread_status_t size.");
    static_assert(sizeof(EeSemaStatus) == 0x18u, "Unexpected ee_sema_t size.");

    void setRegU32(R5900Context &ctx, int reg, uint32_t value)
    {
        SET_GPR_U32(&ctx, reg, value);
    }

    int32_t getRegS32(const R5900Context &ctx, int reg)
    {
        return static_cast<int32_t>(::getRegU32(&ctx, reg));
    }

    void writeGuestU32(uint8_t *rdram, uint32_t addr, uint32_t value)
    {
        std::memcpy(rdram + addr, &value, sizeof(value));
    }

    void writeGuestWords(uint8_t *rdram, uint32_t addr, const uint32_t *words, size_t count)
    {
        for (size_t i = 0; i < count; ++i)
        {
            writeGuestU32(rdram, addr + static_cast<uint32_t>(i * sizeof(uint32_t)), words[i]);
        }
    }

    uint32_t readGuestU32(const uint8_t *rdram, uint32_t addr)
    {
        uint32_t value = 0;
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

    bool callSyscall(uint32_t syscallNumber, uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        return dispatchNumericSyscall(syscallNumber, rdram, ctx, runtime);
    }

    void overrideReturnHandler(uint8_t *, R5900Context *ctx, PS2Runtime *)
    {
        setReturnU32(ctx, ::getRegU32(ctx, 4) + ::getRegU32(ctx, 5));
        ctx->pc = ::getRegU32(ctx, 31);
    }

    void overrideBrokenHandler(uint8_t *, R5900Context *ctx, PS2Runtime *)
    {
        setReturnU32(ctx, 0xDEADBEEFu);
        ctx->pc = 0x12345678u;
    }

    void overrideRecursiveFindAddressHandler(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        runtime->handleSyscall(rdram, ctx, 0x83u);
        ctx->pc = ::getRegU32(ctx, 31);
    }

    void overrideKsegCompareHandler(uint8_t *, R5900Context *ctx, PS2Runtime *)
    {
        auto getLowU64 = [](const R5900Context *cpu, int reg) -> uint64_t
        {
            return (reg == 0) ? 0u : static_cast<uint64_t>(_mm_extract_epi64(cpu->r[reg], 0));
        };
        auto setLowS32 = [](R5900Context *cpu, int reg, uint32_t value)
        {
            SET_GPR_S32(cpu, reg, value);
        };
        auto setLowU64 = [](R5900Context *cpu, int reg, uint64_t value)
        {
            SET_GPR_U64(cpu, reg, value);
        };

        const uint32_t nextA0 = static_cast<uint32_t>(::getRegU32(ctx, 4) + 4u);
        setLowS32(ctx, 4, nextA0);
        setLowU64(ctx, 2, (getLowU64(ctx, 4) < getLowU64(ctx, 5)) ? 1u : 0u);
        if (getLowU64(ctx, 2) == 0u)
        {
            ctx->r[4] = _mm_setzero_si128();
        }
        setLowU64(ctx, 2, getLowU64(ctx, 4));
        ctx->pc = ::getRegU32(ctx, 31);
    }

    constexpr uint64_t K_EXPECTED_UPPER64 = 0x1122334455667788ull;

    void overridePreserveUpper64Handler(uint8_t *, R5900Context *ctx, PS2Runtime *)
    {
        const uint64_t hi = static_cast<uint64_t>(_mm_extract_epi64(ctx->r[4], 1));
        const uint64_t low = static_cast<uint64_t>(_mm_extract_epi64(ctx->r[4], 0));
        const uint64_t expectedLow = static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(0x80000000u)));
        setReturnU32(ctx, (hi == K_EXPECTED_UPPER64 && low == expectedLow) ? 1u : 0u);
        ctx->pc = ::getRegU32(ctx, 31);
    }

    void waitEventAfterSuspendHandler(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (!rdram || !ctx)
        {
            return;
        }

        writeGuestU32(rdram, K_EVENT_WAIT_READY_ADDR, 1u);
        while (readGuestU32(rdram, K_EVENT_WAIT_GATE_ADDR) == 0u)
        {
            if (runtime && runtime->isStopRequested())
            {
                ctx->pc = 0u;
                return;
            }
            std::this_thread::yield();
        }

        const uint32_t eid = ::getRegU32(ctx, 4);
        setRegU32(*ctx, 4, eid);
        setRegU32(*ctx, 5, 0x4u);
        setRegU32(*ctx, 6, 1u);
        setRegU32(*ctx, 7, 0u);
        WaitEventFlag(rdram, ctx, runtime);
        ctx->pc = 0u;
    }

    void waitSemaUntilTerminatedHandler(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (!rdram || !ctx)
        {
            return;
        }

        writeGuestU32(rdram, K_TERMINATE_SEMA_WAIT_READY_ADDR, 1u);
        WaitSema(rdram, ctx, runtime);
        ctx->pc = 0u;
    }

    void alarmNoopHandler(uint8_t *, R5900Context *ctx, PS2Runtime *)
    {
        ctx->pc = 0u;
    }

    struct TestEnv
    {
        std::vector<uint8_t> rdram;
        R5900Context ctx{};
        PS2Runtime runtime;

        TestEnv() : rdram(PS2_RAM_SIZE, 0)
        {
            std::memset(&ctx, 0, sizeof(ctx));
        }
    };
}

void register_ps2_runtime_kernel_tests()
{
    MiniTest::Case("PS2RuntimeKernel", [](TestCase &tc)
    {
        tc.Run("thread create/refer/delete follows EE status layout", [](TestCase &t)
        {
            TestEnv env;

            const uint32_t threadParam[7] = {
                0x00000002u, // attr
                0x00200000u, // entry
                0x00300000u, // stack
                0x00000800u, // stack size
                0x00120000u, // gp
                5u,          // initial priority
                0xABCD0001u  // option
            };

            writeGuestWords(env.rdram.data(), K_PARAM_ADDR, threadParam, std::size(threadParam));
            setRegU32(env.ctx, 4, K_PARAM_ADDR);
            CreateThread(env.rdram.data(), &env.ctx, &env.runtime);

            const int32_t tid = getRegS32(env.ctx, 2);
            t.IsTrue(tid >= 2, "CreateThread should return a valid non-main thread id");

            setRegU32(env.ctx, 4, static_cast<uint32_t>(tid));
            setRegU32(env.ctx, 5, K_STATUS_ADDR);
            ReferThreadStatus(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), KE_OK, "ReferThreadStatus should succeed for created thread");

            EeThreadStatus status{};
            std::memcpy(&status, env.rdram.data() + K_STATUS_ADDR, sizeof(status));
            t.Equals(status.status, THS_DORMANT, "new thread should be dormant before StartThread");
            t.Equals(status.func, threadParam[1], "status.func should match entry");
            t.Equals(status.stack, threadParam[2], "status.stack should match configured stack");
            t.Equals(status.stack_size, static_cast<int32_t>(threadParam[3]), "status.stack_size should match thread param");
            t.Equals(status.gp_reg, threadParam[4], "status.gp_reg should match configured gp");
            t.Equals(status.initial_priority, 5, "status.initial_priority should match thread param");
            t.Equals(status.current_priority, 5, "status.current_priority should start at initial priority");
            t.Equals(status.attr, threadParam[0], "status.attr should match thread param");
            t.Equals(status.option, threadParam[6], "status.option should match thread param");

            setRegU32(env.ctx, 4, static_cast<uint32_t>(tid));
            DeleteThread(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), KE_OK, "DeleteThread should succeed for dormant thread");

            setRegU32(env.ctx, 4, static_cast<uint32_t>(tid));
            setRegU32(env.ctx, 5, K_STATUS_ADDR);
            ReferThreadStatus(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), KE_UNKNOWN_THID, "deleted thread id should no longer be referable");
        });

        tc.Run("start thread validates target and entry registration", [](TestCase &t)
        {
            TestEnv env;

            const uint32_t threadParam[7] = {
                0u,
                0x00250000u, // entry not registered in runtime
                0x00300000u,
                0x00000400u,
                0x00110000u,
                8u,
                0u
            };

            writeGuestWords(env.rdram.data(), K_PARAM_ADDR, threadParam, std::size(threadParam));
            setRegU32(env.ctx, 4, K_PARAM_ADDR);
            CreateThread(env.rdram.data(), &env.ctx, &env.runtime);
            const int32_t tid = getRegS32(env.ctx, 2);
            t.IsTrue(tid >= 2, "CreateThread should return an id before StartThread check");

            setRegU32(env.ctx, 4, static_cast<uint32_t>(tid));
            setRegU32(env.ctx, 5, 0x12345678u);
            StartThread(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), KE_ERROR, "StartThread should fail when entry is not registered");

            setRegU32(env.ctx, 4, static_cast<uint32_t>(tid));
            setRegU32(env.ctx, 5, K_STATUS_ADDR);
            ReferThreadStatus(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), KE_OK, "ReferThreadStatus should still succeed after failed StartThread");

            EeThreadStatus status{};
            std::memcpy(&status, env.rdram.data() + K_STATUS_ADDR, sizeof(status));
            t.Equals(status.status, THS_DORMANT, "thread should remain dormant when StartThread fails early");

            setRegU32(env.ctx, 4, static_cast<uint32_t>(tid));
            DeleteThread(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), KE_OK, "DeleteThread should clean up failed-start thread");
        });

        tc.Run("thread id and wakeup guard rails match kernel-style errors", [](TestCase &t)
        {
            TestEnv env;

            GetThreadId(env.rdram.data(), &env.ctx, &env.runtime);
            const int32_t selfTid = getRegS32(env.ctx, 2);
            t.IsTrue(selfTid > 0, "GetThreadId should return a positive thread id");

            setRegU32(env.ctx, 4, 0u);
            WakeupThread(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), KE_ILLEGAL_THID, "WakeupThread(TH_SELF/0) should be illegal");

            setRegU32(env.ctx, 4, static_cast<uint32_t>(selfTid));
            WakeupThread(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), KE_ILLEGAL_THID, "WakeupThread(self) should be illegal");

            setRegU32(env.ctx, 4, 0u);
            iCancelWakeupThread(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), KE_ILLEGAL_THID, "iCancelWakeupThread(0) should be illegal");

            setRegU32(env.ctx, 4, 0u);
            CancelWakeupThread(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), KE_OK, "CancelWakeupThread(TH_SELF) should return previous count (0)");
        });

        tc.Run("semaphore EE layout covers poll, signal overflow, and status", [](TestCase &t)
        {
            TestEnv env;

            const uint32_t semaParam[6] = {
                0u,          // count (unused by runtime decode)
                2u,          // max_count
                1u,          // init_count
                0u,          // wait_threads
                0x11u,       // attr
                0x00202020u  // option
            };

            writeGuestWords(env.rdram.data(), K_PARAM_ADDR, semaParam, std::size(semaParam));
            setRegU32(env.ctx, 4, K_PARAM_ADDR);
            CreateSema(env.rdram.data(), &env.ctx, &env.runtime);
            const int32_t sid = getRegS32(env.ctx, 2);
            t.IsTrue(sid > 0, "CreateSema should return positive semaphore id");

            setRegU32(env.ctx, 4, static_cast<uint32_t>(sid));
            setRegU32(env.ctx, 5, K_STATUS_ADDR);
            ReferSemaStatus(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), KE_OK, "ReferSemaStatus should succeed for valid semaphore");

            EeSemaStatus semaStatus{};
            std::memcpy(&semaStatus, env.rdram.data() + K_STATUS_ADDR, sizeof(semaStatus));
            t.Equals(semaStatus.count, 1, "initial semaphore count should match init_count");
            t.Equals(semaStatus.max_count, 2, "max_count should match CreateSema params");
            t.Equals(semaStatus.init_count, 1, "init_count should be preserved");
            t.Equals(semaStatus.attr, semaParam[4], "attr should be preserved");
            t.Equals(semaStatus.option, semaParam[5], "option should be preserved");

            setRegU32(env.ctx, 4, static_cast<uint32_t>(sid));
            PollSema(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), sid, "PollSema should return sid when consuming one available token");

            setRegU32(env.ctx, 4, static_cast<uint32_t>(sid));
            PollSema(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), KE_SEMA_ZERO, "PollSema should fail when count is zero");

            setRegU32(env.ctx, 4, static_cast<uint32_t>(sid));
            SignalSema(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), sid, "SignalSema should return sid when incrementing count below max");

            setRegU32(env.ctx, 4, static_cast<uint32_t>(sid));
            SignalSema(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), sid, "SignalSema should return sid when incrementing up to max");

            setRegU32(env.ctx, 4, static_cast<uint32_t>(sid));
            SignalSema(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), KE_SEMA_OVF, "SignalSema should report overflow at max_count");

            setRegU32(env.ctx, 4, static_cast<uint32_t>(sid));
            DeleteSema(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), sid, "DeleteSema should return sid for existing semaphore");

            setRegU32(env.ctx, 4, static_cast<uint32_t>(sid));
            PollSema(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), KE_UNKNOWN_SEMID, "deleted semaphore id should be rejected");
        });

        tc.Run("semaphore legacy layout decode remains supported", [](TestCase &t)
        {
            TestEnv env;

            const uint32_t legacyParam[6] = {
                0x7u,        // attr
                0x1234u,     // legacy option / ee max_count
                3u,          // init
                4u,          // max
                0u,          // ee attr (ignored if legacy selected)
                0x1FFFFFFFu  // ee option (invalid guest pointer to bias decode toward legacy)
            };
            writeGuestWords(env.rdram.data(), K_PARAM_ADDR, legacyParam, std::size(legacyParam));

            setRegU32(env.ctx, 4, K_PARAM_ADDR);
            CreateSema(env.rdram.data(), &env.ctx, &env.runtime);
            const int32_t sid = getRegS32(env.ctx, 2);
            t.IsTrue(sid > 0, "CreateSema should still accept legacy-style parameter blocks");

            setRegU32(env.ctx, 4, static_cast<uint32_t>(sid));
            setRegU32(env.ctx, 5, K_STATUS_ADDR);
            ReferSemaStatus(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), KE_OK, "ReferSemaStatus should succeed for legacy-decoded semaphore");

            EeSemaStatus semaStatus{};
            std::memcpy(&semaStatus, env.rdram.data() + K_STATUS_ADDR, sizeof(semaStatus));
            t.Equals(semaStatus.count, 3, "legacy init_count should map to runtime count");
            t.Equals(semaStatus.max_count, 4, "legacy max_count should map to runtime max");
            t.Equals(semaStatus.attr, 0x7u, "legacy attr should be preserved");
            t.Equals(semaStatus.option, 0x1234u, "legacy option should be preserved");

            setRegU32(env.ctx, 4, static_cast<uint32_t>(sid));
            DeleteSema(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), sid, "DeleteSema should return sid for legacy-decoded semaphore");
        });

        tc.Run("semaphore syscalls return sid on success (EE BIOS convention)", [](TestCase &t)
        {
            // Sub-case A: CreateSema returns positive id (regression guard)
            {
                TestEnv env;
                const uint32_t semaParam[6] = { 0u, 2u, 1u, 0u, 0x11u, 0u };
                writeGuestWords(env.rdram.data(), K_PARAM_ADDR, semaParam, std::size(semaParam));
                setRegU32(env.ctx, 4, K_PARAM_ADDR);
                CreateSema(env.rdram.data(), &env.ctx, &env.runtime);
                const int32_t sid = getRegS32(env.ctx, 2);
                t.IsTrue(sid > 0, "CreateSema should return positive semaphore id");
            }

            // Sub-case B: PollSema success returns sid
            {
                TestEnv env;
                const uint32_t semaParam[6] = { 0u, 2u, 1u, 0u, 0u, 0u };
                writeGuestWords(env.rdram.data(), K_PARAM_ADDR, semaParam, std::size(semaParam));
                setRegU32(env.ctx, 4, K_PARAM_ADDR);
                CreateSema(env.rdram.data(), &env.ctx, &env.runtime);
                const int32_t sid = getRegS32(env.ctx, 2);
                t.IsTrue(sid > 0, "CreateSema should return positive id for PollSema test");

                setRegU32(env.ctx, 4, static_cast<uint32_t>(sid));
                PollSema(env.rdram.data(), &env.ctx, &env.runtime);
                t.Equals(getRegS32(env.ctx, 2), sid, "PollSema success should return sid");

                setRegU32(env.ctx, 4, static_cast<uint32_t>(sid));
                PollSema(env.rdram.data(), &env.ctx, &env.runtime);
                t.Equals(getRegS32(env.ctx, 2), KE_SEMA_ZERO, "PollSema should return KE_SEMA_ZERO when count exhausted");
            }

            // Sub-case C: SignalSema success returns sid + overflow returns KE_SEMA_OVF
            {
                TestEnv env;
                const uint32_t semaParam[6] = { 0u, 1u, 0u, 0u, 0u, 0u };
                writeGuestWords(env.rdram.data(), K_PARAM_ADDR, semaParam, std::size(semaParam));
                setRegU32(env.ctx, 4, K_PARAM_ADDR);
                CreateSema(env.rdram.data(), &env.ctx, &env.runtime);
                const int32_t sid = getRegS32(env.ctx, 2);
                t.IsTrue(sid > 0, "CreateSema should return positive id for SignalSema test");

                setRegU32(env.ctx, 4, static_cast<uint32_t>(sid));
                SignalSema(env.rdram.data(), &env.ctx, &env.runtime);
                t.Equals(getRegS32(env.ctx, 2), sid, "SignalSema success should return sid (count 0->1)");

                setRegU32(env.ctx, 4, static_cast<uint32_t>(sid));
                SignalSema(env.rdram.data(), &env.ctx, &env.runtime);
                t.Equals(getRegS32(env.ctx, 2), KE_SEMA_OVF, "SignalSema should return KE_SEMA_OVF when count at max=1");
            }

            // Sub-case D: WaitSema success returns sid AND decrements count
            {
                TestEnv env;
                const uint32_t semaParam[6] = { 0u, 2u, 1u, 0u, 0u, 0u };
                writeGuestWords(env.rdram.data(), K_PARAM_ADDR, semaParam, std::size(semaParam));
                setRegU32(env.ctx, 4, K_PARAM_ADDR);
                CreateSema(env.rdram.data(), &env.ctx, &env.runtime);
                const int32_t sid = getRegS32(env.ctx, 2);
                t.IsTrue(sid > 0, "CreateSema should return positive id for WaitSema test");

                setRegU32(env.ctx, 4, static_cast<uint32_t>(sid));
                WaitSema(env.rdram.data(), &env.ctx, &env.runtime);
                t.Equals(getRegS32(env.ctx, 2), sid, "WaitSema success should return sid");

                R5900Context statusCtx{};
                setRegU32(statusCtx, 4, static_cast<uint32_t>(sid));
                setRegU32(statusCtx, 5, K_STATUS_ADDR);
                ReferSemaStatus(env.rdram.data(), &statusCtx, &env.runtime);
                EeSemaStatus semaStatus{};
                std::memcpy(&semaStatus, env.rdram.data() + K_STATUS_ADDR, sizeof(semaStatus));
                t.Equals(semaStatus.count, 0, "WaitSema should decrement count to 0");
            }

            // Sub-case E: WaitSema delete-while-waiting returns KE_WAIT_DELETE
            {
                TestEnv env;
                const uint32_t semaParam[6] = { 0u, 1u, 0u, 0u, 0u, 0u };
                writeGuestWords(env.rdram.data(), K_PARAM_ADDR, semaParam, std::size(semaParam));
                setRegU32(env.ctx, 4, K_PARAM_ADDR);
                CreateSema(env.rdram.data(), &env.ctx, &env.runtime);
                const int32_t sid = getRegS32(env.ctx, 2);
                t.IsTrue(sid > 0, "CreateSema should return positive id for delete-while-waiting test");

                int32_t workerRet = 0;
                writeGuestU32(env.rdram.data(), K_SEMA_WAIT_READY_ADDR, 0u);

                std::thread worker([&]()
                {
                    R5900Context wctx{};
                    setRegU32(wctx, 4, static_cast<uint32_t>(sid));
                    writeGuestU32(env.rdram.data(), K_SEMA_WAIT_READY_ADDR, 1u);
                    WaitSema(env.rdram.data(), &wctx, &env.runtime);
                    workerRet = getRegS32(wctx, 2);
                });

                // Wait until the waiter has incremented waiter count (count=0, so it must block)
                const bool waiterBlocking = waitUntil([&]()
                {
                    R5900Context statusCtx{};
                    setRegU32(statusCtx, 4, static_cast<uint32_t>(sid));
                    setRegU32(statusCtx, 5, K_STATUS_ADDR);
                    ReferSemaStatus(env.rdram.data(), &statusCtx, &env.runtime);
                    EeSemaStatus st{};
                    std::memcpy(&st, env.rdram.data() + K_STATUS_ADDR, sizeof(st));
                    return st.wait_threads >= 1;
                }, std::chrono::milliseconds(500));
                t.IsTrue(waiterBlocking, "worker thread should be blocking on WaitSema");

                // Delete the semaphore while worker is waiting
                setRegU32(env.ctx, 4, static_cast<uint32_t>(sid));
                DeleteSema(env.rdram.data(), &env.ctx, &env.runtime);
                t.Equals(getRegS32(env.ctx, 2), sid, "DeleteSema should return sid while thread is waiting");

                worker.join();
                t.Equals(workerRet, KE_WAIT_DELETE, "WaitSema should return KE_WAIT_DELETE when semaphore is deleted");
            }

            // Sub-case F: DeleteSema success returns sid
            {
                TestEnv env;
                const uint32_t semaParam[6] = { 0u, 1u, 0u, 0u, 0u, 0u };
                writeGuestWords(env.rdram.data(), K_PARAM_ADDR, semaParam, std::size(semaParam));
                setRegU32(env.ctx, 4, K_PARAM_ADDR);
                CreateSema(env.rdram.data(), &env.ctx, &env.runtime);
                const int32_t sid = getRegS32(env.ctx, 2);
                t.IsTrue(sid > 0, "CreateSema should return positive id for DeleteSema test");

                setRegU32(env.ctx, 4, static_cast<uint32_t>(sid));
                DeleteSema(env.rdram.data(), &env.ctx, &env.runtime);
                t.Equals(getRegS32(env.ctx, 2), sid, "DeleteSema success should return sid");
            }

            // Sub-case G: Invalid sid returns KE_UNKNOWN_SEMID for all four syscalls
            {
                TestEnv env;
                constexpr uint32_t kBadSid = 0x7FFFu;

                setRegU32(env.ctx, 4, kBadSid);
                PollSema(env.rdram.data(), &env.ctx, &env.runtime);
                t.Equals(getRegS32(env.ctx, 2), KE_UNKNOWN_SEMID, "PollSema should return KE_UNKNOWN_SEMID for invalid sid");

                setRegU32(env.ctx, 4, kBadSid);
                SignalSema(env.rdram.data(), &env.ctx, &env.runtime);
                t.Equals(getRegS32(env.ctx, 2), KE_UNKNOWN_SEMID, "SignalSema should return KE_UNKNOWN_SEMID for invalid sid");

                setRegU32(env.ctx, 4, kBadSid);
                WaitSema(env.rdram.data(), &env.ctx, &env.runtime);
                t.Equals(getRegS32(env.ctx, 2), KE_UNKNOWN_SEMID, "WaitSema should return KE_UNKNOWN_SEMID for invalid sid");

                setRegU32(env.ctx, 4, kBadSid);
                DeleteSema(env.rdram.data(), &env.ctx, &env.runtime);
                t.Equals(getRegS32(env.ctx, 2), KE_UNKNOWN_SEMID, "DeleteSema should return KE_UNKNOWN_SEMID for invalid sid");
            }

            // Sub-case H: WaitSema force-released via ReleaseWaitThread returns KE_RELEASE_WAIT
            // and the ret >= 0 guard must NOT consume a token (count stays 0, not -1).
            {
                // Use a tid the sequential allocator (range 2..0xFF) will never produce, so the
                // worker's WaitSema creates a fresh ThreadInfo that ReleaseWaitThread can target.
                // Prior tests leave stale entries at low tids (2, 3, ...), which would make
                // ReleaseWaitThread find a non-waiting ThreadInfo and return KE_NOT_WAIT.
                constexpr int kWorkerTid = 0x7FFE;
                TestEnv env;
                const uint32_t semaParam[6] = {0u, 2u, 0u, 0u, 0u, 0u};
                writeGuestWords(env.rdram.data(), K_PARAM_ADDR, semaParam, 6);
                setRegU32(env.ctx, 4, K_PARAM_ADDR);
                CreateSema(env.rdram.data(), &env.ctx, &env.runtime);
                const int sid = getRegS32(env.ctx, 2);
                t.IsTrue(sid > 0, "sub-case H: CreateSema must return positive sid");

                writeGuestU32(env.rdram.data(), K_SEMA_WAIT_READY_ADDR, 0u);
                int32_t workerRet = 0;

                std::thread worker([&]() {
                    g_currentThreadId = kWorkerTid;
                    R5900Context wctx{};
                    setRegU32(wctx, 4, static_cast<uint32_t>(sid));
                    writeGuestU32(env.rdram.data(), K_SEMA_WAIT_READY_ADDR, 1u);
                    WaitSema(env.rdram.data(), &wctx, &env.runtime);
                    workerRet = getRegS32(wctx, 2);
                });

                // Wait until the worker is confirmed blocking in WaitSema.
                const bool waiterBlocking = waitUntil([&]() {
                    R5900Context statusCtx{};
                    setRegU32(statusCtx, 4, static_cast<uint32_t>(sid));
                    setRegU32(statusCtx, 5, K_STATUS_ADDR);
                    ReferSemaStatus(env.rdram.data(), &statusCtx, &env.runtime);
                    EeSemaStatus st{};
                    std::memcpy(&st, env.rdram.data() + K_STATUS_ADDR, sizeof(st));
                    return st.wait_threads >= 1;
                }, std::chrono::milliseconds(500));
                t.IsTrue(waiterBlocking, "sub-case H: worker must be blocking in WaitSema before force-release");

                // Force-release the worker via ReleaseWaitThread.
                setRegU32(env.ctx, 4, static_cast<uint32_t>(kWorkerTid));
                ReleaseWaitThread(env.rdram.data(), &env.ctx, &env.runtime);
                t.Equals(getRegS32(env.ctx, 2), KE_OK,
                         "sub-case H: ReleaseWaitThread must succeed");

                worker.join();
                t.Equals(workerRet, KE_RELEASE_WAIT,
                         "sub-case H: WaitSema force-released must return KE_RELEASE_WAIT, not sid");

                // Assert the count was NOT decremented (core guard check: ret < 0 skips decrement).
                {
                    R5900Context statusCtx{};
                    setRegU32(statusCtx, 4, static_cast<uint32_t>(sid));
                    setRegU32(statusCtx, 5, K_STATUS_ADDR);
                    ReferSemaStatus(env.rdram.data(), &statusCtx, &env.runtime);
                    EeSemaStatus st{};
                    std::memcpy(&st, env.rdram.data() + K_STATUS_ADDR, sizeof(st));
                    t.Equals(st.count, 0, "sub-case H: force-released WaitSema must NOT consume a token (count must stay 0, not -1)");
                }

                // Prove token accounting is intact: signal once, poll twice (one token, clean).
                setRegU32(env.ctx, 4, static_cast<uint32_t>(sid));
                SignalSema(env.rdram.data(), &env.ctx, &env.runtime);
                t.Equals(getRegS32(env.ctx, 2), sid,
                         "sub-case H: SignalSema after force-release must return sid (count 0->1)");

                setRegU32(env.ctx, 4, static_cast<uint32_t>(sid));
                PollSema(env.rdram.data(), &env.ctx, &env.runtime);
                t.Equals(getRegS32(env.ctx, 2), sid,
                         "sub-case H: PollSema must consume the one token after force-release");

                setRegU32(env.ctx, 4, static_cast<uint32_t>(sid));
                PollSema(env.rdram.data(), &env.ctx, &env.runtime);
                t.Equals(getRegS32(env.ctx, 2), KE_SEMA_ZERO,
                         "sub-case H: count must be exactly 0 after single token consumed (not -1)");
            }

            // Sub-case I: blocking WaitSema woken by SignalSema returns sid (the DQ8 scenario).
            // init=0 forces the worker to block; SignalSema uses cv.notify_one() (not
            // ReleaseWaitThread), so the worker needs no g_currentThreadId identity.
            {
                TestEnv env;
                const uint32_t semaParam[6] = {0u, 1u, 0u, 0u, 0u, 0u};
                writeGuestWords(env.rdram.data(), K_PARAM_ADDR, semaParam, 6);
                setRegU32(env.ctx, 4, K_PARAM_ADDR);
                CreateSema(env.rdram.data(), &env.ctx, &env.runtime);
                const int sid = getRegS32(env.ctx, 2);
                t.IsTrue(sid > 0, "sub-case I: CreateSema must return positive sid");

                writeGuestU32(env.rdram.data(), K_SEMA_WAIT_READY_ADDR, 0u);
                int32_t workerRet = 0;

                std::thread worker([&]() {
                    R5900Context wctx{};
                    setRegU32(wctx, 4, static_cast<uint32_t>(sid));
                    writeGuestU32(env.rdram.data(), K_SEMA_WAIT_READY_ADDR, 1u);
                    WaitSema(env.rdram.data(), &wctx, &env.runtime);
                    workerRet = getRegS32(wctx, 2);
                });

                // Confirm the worker is actually blocking (count==0 forces a block).
                const bool waiterBlocking = waitUntil([&]() {
                    R5900Context statusCtx{};
                    setRegU32(statusCtx, 4, static_cast<uint32_t>(sid));
                    setRegU32(statusCtx, 5, K_STATUS_ADDR);
                    ReferSemaStatus(env.rdram.data(), &statusCtx, &env.runtime);
                    EeSemaStatus st{};
                    std::memcpy(&st, env.rdram.data() + K_STATUS_ADDR, sizeof(st));
                    return st.wait_threads >= 1;
                }, std::chrono::milliseconds(500));
                t.IsTrue(waiterBlocking, "sub-case I: worker must be blocking in WaitSema before signal");

                // Wake the worker; success path must return sid, not KE_OK.
                setRegU32(env.ctx, 4, static_cast<uint32_t>(sid));
                SignalSema(env.rdram.data(), &env.ctx, &env.runtime);
                t.Equals(getRegS32(env.ctx, 2), sid,
                         "sub-case I: SignalSema that wakes a waiter must return sid (count 0->1)");

                worker.join();
                t.Equals(workerRet, sid,
                         "sub-case I: blocking WaitSema woken by signal must return sid, not KE_OK");

                // Signal incremented to 1, the woken wait consumed it back to 0.
                {
                    R5900Context statusCtx{};
                    setRegU32(statusCtx, 4, static_cast<uint32_t>(sid));
                    setRegU32(statusCtx, 5, K_STATUS_ADDR);
                    ReferSemaStatus(env.rdram.data(), &statusCtx, &env.runtime);
                    EeSemaStatus st{};
                    std::memcpy(&st, env.rdram.data() + K_STATUS_ADDR, sizeof(st));
                    t.Equals(st.count, 0,
                             "sub-case I: woken WaitSema must consume the signaled token (count back to 0)");
                }
            }

            // Reset all global sema/thread state so no entries (e.g. the 0x7FFE ThreadInfo
            // from sub-case H) leak into subsequent test cases.
            notifyRuntimeStop();
        });

        tc.Run("WaitEventFlag preserves waitsuspend state when a suspended thread blocks", [](TestCase &t)
        {
            TestEnv env;

            constexpr uint32_t kEventParamAddr = 0x1600u;
            constexpr uint32_t kWaitThreadEntry = 0x00260000u;

            const uint32_t eventParam[3] = {
                0u,
                0u,
                0u
            };
            std::memcpy(env.rdram.data() + kEventParamAddr, eventParam, sizeof(eventParam));
            writeGuestU32(env.rdram.data(), K_EVENT_WAIT_READY_ADDR, 0u);
            writeGuestU32(env.rdram.data(), K_EVENT_WAIT_GATE_ADDR, 0u);

            R5900Context createEventCtx{};
            setRegU32(createEventCtx, 4, kEventParamAddr);
            CreateEventFlag(env.rdram.data(), &createEventCtx, &env.runtime);
            const int32_t eid = getRegS32(createEventCtx, 2);
            t.IsTrue(eid > 0, "CreateEventFlag should return a valid event id");

            env.runtime.registerFunction(kWaitThreadEntry, &waitEventAfterSuspendHandler);

            const uint32_t threadParam[7] = {
                0u,
                kWaitThreadEntry,
                0x00310000u,
                0x00000800u,
                0x00120000u,
                6u,
                0u
            };

            writeGuestWords(env.rdram.data(), K_PARAM_ADDR, threadParam, std::size(threadParam));
            setRegU32(env.ctx, 4, K_PARAM_ADDR);
            CreateThread(env.rdram.data(), &env.ctx, &env.runtime);
            const int32_t tid = getRegS32(env.ctx, 2);
            t.IsTrue(tid >= 2, "CreateThread should return a valid worker thread id");

            setRegU32(env.ctx, 4, static_cast<uint32_t>(tid));
            setRegU32(env.ctx, 5, static_cast<uint32_t>(eid));
            StartThread(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), KE_OK, "StartThread should launch the event waiter");

            const bool ready = waitUntil([&]()
            {
                return readGuestU32(env.rdram.data(), K_EVENT_WAIT_READY_ADDR) == 1u;
            }, std::chrono::milliseconds(200));
            t.IsTrue(ready, "waiter thread should reach the suspend gate before blocking");

            setRegU32(env.ctx, 4, static_cast<uint32_t>(tid));
            SuspendThread(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), KE_OK, "SuspendThread should succeed for the running waiter");

            writeGuestU32(env.rdram.data(), K_EVENT_WAIT_GATE_ADDR, 1u);

            const bool waiting = waitUntil([&]()
            {
                R5900Context statusCtx{};
                setRegU32(statusCtx, 4, static_cast<uint32_t>(tid));
                setRegU32(statusCtx, 5, K_STATUS_ADDR);
                ReferThreadStatus(env.rdram.data(), &statusCtx, &env.runtime);
                if (getRegS32(statusCtx, 2) != KE_OK)
                {
                    return false;
                }

                EeThreadStatus status{};
                std::memcpy(&status, env.rdram.data() + K_STATUS_ADDR, sizeof(status));
                return status.waitType == TSW_EVENT;
            }, std::chrono::milliseconds(200));
            t.IsTrue(waiting, "waiter thread should block on the event flag");

            EeThreadStatus waitingStatus{};
            std::memcpy(&waitingStatus, env.rdram.data() + K_STATUS_ADDR, sizeof(waitingStatus));
            t.Equals(waitingStatus.status, THS_WAITSUSPEND,
                     "event-flag wait should report THS_WAITSUSPEND when the thread is already suspended");

            R5900Context signalCtx{};
            setRegU32(signalCtx, 4, static_cast<uint32_t>(eid));
            setRegU32(signalCtx, 5, 0x4u);
            SetEventFlag(env.rdram.data(), &signalCtx, &env.runtime);
            t.Equals(getRegS32(signalCtx, 2), KE_OK, "SetEventFlag should wake the waiting thread");

            const bool suspended = waitUntil([&]()
            {
                R5900Context statusCtx{};
                setRegU32(statusCtx, 4, static_cast<uint32_t>(tid));
                setRegU32(statusCtx, 5, K_STATUS_ADDR);
                ReferThreadStatus(env.rdram.data(), &statusCtx, &env.runtime);
                if (getRegS32(statusCtx, 2) != KE_OK)
                {
                    return false;
                }

                EeThreadStatus status{};
                std::memcpy(&status, env.rdram.data() + K_STATUS_ADDR, sizeof(status));
                return status.status == THS_SUSPEND && status.waitType == 0u;
            }, std::chrono::milliseconds(200));
            t.IsTrue(suspended, "after wake, a still-suspended waiter should move to THS_SUSPEND");

            setRegU32(env.ctx, 4, static_cast<uint32_t>(tid));
            ResumeThread(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), KE_OK, "ResumeThread should release the waiter after the event is set");

            const bool dormant = waitUntil([&]()
            {
                R5900Context statusCtx{};
                setRegU32(statusCtx, 4, static_cast<uint32_t>(tid));
                setRegU32(statusCtx, 5, K_STATUS_ADDR);
                ReferThreadStatus(env.rdram.data(), &statusCtx, &env.runtime);
                if (getRegS32(statusCtx, 2) != KE_OK)
                {
                    return false;
                }

                EeThreadStatus status{};
                std::memcpy(&status, env.rdram.data() + K_STATUS_ADDR, sizeof(status));
                return status.status == THS_DORMANT;
            }, std::chrono::milliseconds(200));
            t.IsTrue(dormant, "waiter thread should return to dormant after the event is signaled and resumed");

            setRegU32(env.ctx, 4, static_cast<uint32_t>(eid));
            DeleteEventFlag(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), KE_OK, "DeleteEventFlag should clean up the test event flag");

            setRegU32(env.ctx, 4, static_cast<uint32_t>(tid));
            DeleteThread(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), KE_OK, "DeleteThread should clean up the waiter thread");
        });

        tc.Run("TerminateThread unwinds semaphore wait as a normal thread exit", [](TestCase &t)
        {
            TestEnv env;

            constexpr uint32_t kWaitThreadEntry = 0x00261000u;
            const uint32_t semaParam[6] = {
                0u,
                1u,
                0u,
                0u,
                0u,
                0u
            };
            writeGuestWords(env.rdram.data(), K_PARAM_ADDR, semaParam, std::size(semaParam));

            R5900Context createSemaCtx{};
            setRegU32(createSemaCtx, 4, K_PARAM_ADDR);
            CreateSema(env.rdram.data(), &createSemaCtx, &env.runtime);
            const int32_t sid = getRegS32(createSemaCtx, 2);
            t.IsTrue(sid > 0, "CreateSema should create a zero-count semaphore");

            env.runtime.registerFunction(kWaitThreadEntry, &waitSemaUntilTerminatedHandler);

            const uint32_t threadParam[7] = {
                0u,
                kWaitThreadEntry,
                0x00312000u,
                0x00000800u,
                0x00120000u,
                6u,
                0u
            };

            writeGuestU32(env.rdram.data(), K_TERMINATE_SEMA_WAIT_READY_ADDR, 0u);
            writeGuestWords(env.rdram.data(), K_PARAM_ADDR, threadParam, std::size(threadParam));
            setRegU32(env.ctx, 4, K_PARAM_ADDR);
            CreateThread(env.rdram.data(), &env.ctx, &env.runtime);
            const int32_t tid = getRegS32(env.ctx, 2);
            t.IsTrue(tid >= 2, "CreateThread should return a valid semaphore waiter thread id");

            std::ostringstream capturedErr;
            std::streambuf *oldErr = std::cerr.rdbuf(capturedErr.rdbuf());

            setRegU32(env.ctx, 4, static_cast<uint32_t>(tid));
            setRegU32(env.ctx, 5, static_cast<uint32_t>(sid));
            StartThread(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), KE_OK, "StartThread should launch the semaphore waiter");

            const bool waiting = waitUntil([&]()
            {
                if (readGuestU32(env.rdram.data(), K_TERMINATE_SEMA_WAIT_READY_ADDR) != 1u)
                {
                    return false;
                }

                R5900Context statusCtx{};
                setRegU32(statusCtx, 4, static_cast<uint32_t>(tid));
                setRegU32(statusCtx, 5, K_STATUS_ADDR);
                ReferThreadStatus(env.rdram.data(), &statusCtx, &env.runtime);
                if (getRegS32(statusCtx, 2) != KE_OK)
                {
                    return false;
                }

                EeThreadStatus status{};
                std::memcpy(&status, env.rdram.data() + K_STATUS_ADDR, sizeof(status));
                return status.status == THS_WAIT && status.waitType == TSW_SEMA;
            }, std::chrono::milliseconds(200));
            t.IsTrue(waiting, "worker should block inside WaitSema before termination");

            R5900Context terminateCtx{};
            setRegU32(terminateCtx, 4, static_cast<uint32_t>(tid));
            TerminateThread(env.rdram.data(), &terminateCtx, &env.runtime);
            t.Equals(getRegS32(terminateCtx, 2), KE_OK, "TerminateThread should join the semaphore waiter");

            std::cerr.rdbuf(oldErr);
            const std::string errText = capturedErr.str();
            t.IsTrue(errText.find("PS2 Thread Exit") == std::string::npos,
                     "thread-exit exceptions from Sync.cpp should be caught as normal exits");

            R5900Context dormantCtx{};
            setRegU32(dormantCtx, 4, static_cast<uint32_t>(tid));
            setRegU32(dormantCtx, 5, K_STATUS_ADDR);
            ReferThreadStatus(env.rdram.data(), &dormantCtx, &env.runtime);
            t.Equals(getRegS32(dormantCtx, 2), KE_OK, "terminated waiter should still have readable status");

            EeThreadStatus dormantStatus{};
            std::memcpy(&dormantStatus, env.rdram.data() + K_STATUS_ADDR, sizeof(dormantStatus));
            t.Equals(dormantStatus.status, THS_DORMANT, "terminated waiter should become dormant");

            setRegU32(env.ctx, 4, static_cast<uint32_t>(tid));
            DeleteThread(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), KE_OK, "DeleteThread should clean up the terminated waiter");

            setRegU32(env.ctx, 4, static_cast<uint32_t>(sid));
            DeleteSema(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), sid, "DeleteSema should return sid while cleaning up the waiter semaphore");
        });

        tc.Run("setup heap and allocator primitives track end-of-heap", [](TestCase &t)
        {
            TestEnv env;

            setRegU32(env.ctx, 4, 0x00180010u);
            setRegU32(env.ctx, 5, 0x00001000u);
            t.IsTrue(callSyscall(0x3Du, env.rdram.data(), &env.ctx, &env.runtime), "SetupHeap syscall should dispatch");
            const uint32_t heapBase = static_cast<uint32_t>(getRegS32(env.ctx, 2));
            t.Equals(heapBase, 0x00180010u, "SetupHeap should return configured base");

            t.IsTrue(callSyscall(0x3Eu, env.rdram.data(), &env.ctx, &env.runtime), "EndOfHeap syscall should dispatch");
            const uint32_t heapLimit = static_cast<uint32_t>(getRegS32(env.ctx, 2));
            t.Equals(heapLimit, 0x00181010u, "EndOfHeap should report the upper limit of the configured heap");

            const uint32_t alignedAlloc = env.runtime.guestMalloc(0x20u, 64u);
            t.IsTrue(alignedAlloc != 0u, "guestMalloc should allocate inside configured heap");
            t.Equals(alignedAlloc & 0x3Fu, 0u, "guestMalloc should honor 64-byte alignment");

            env.runtime.guestFree(alignedAlloc);

            const uint32_t a = env.runtime.guestMalloc(0x100u, 16u);
            const uint32_t b = env.runtime.guestMalloc(0x100u, 16u);
            t.IsTrue(a != 0u && b != 0u, "guestMalloc should provide two adjacent blocks in this heap window");
            env.runtime.guestFree(b);

            const uint32_t grown = env.runtime.guestRealloc(a, 0x180u, 16u);
            t.Equals(grown, a, "guestRealloc should grow in place when adjacent free space is available");

            env.runtime.guestFree(grown);
            const uint32_t reused = env.runtime.guestMalloc(0x80u, 16u);
            t.Equals(reused, heapBase, "guestFree should make the head block reusable");
        });

        tc.Run("memalign stubs allocate aligned guest memory", [](TestCase &t)
        {
            TestEnv env;

            env.runtime.configureGuestHeap(0x00180010u, 0x00182010u);

            setRegU32(env.ctx, 4, 128u);
            setRegU32(env.ctx, 5, 0x40u);
            ps2_stubs::memalign(env.rdram.data(), &env.ctx, &env.runtime);
            const uint32_t direct = ::getRegU32(&env.ctx, 2);
            t.IsTrue(direct != 0u, "memalign should return a guest address");
            t.Equals(direct & 0x7Fu, 0u, "memalign should honor 128-byte alignment");

            setRegU32(env.ctx, 5, 64u);
            setRegU32(env.ctx, 6, 0x40u);
            ps2_stubs::memalign_r(env.rdram.data(), &env.ctx, &env.runtime);
            const uint32_t reent = ::getRegU32(&env.ctx, 2);
            t.IsTrue(reent != 0u, "_memalign_r should return a guest address");
            t.Equals(reent & 0x3Fu, 0u, "_memalign_r should honor 64-byte alignment");
            t.IsTrue(reent != direct, "_memalign_r should allocate a distinct block");
        });

        tc.Run("allocator compatibility stubs use the runtime guest heap", [](TestCase &t)
        {
            TestEnv env;

            env.runtime.configureGuestHeap(0x00180010u, 0x00183010u);

            setRegU32(env.ctx, 5, 0x20u);
            ps2_stubs::malloc_r(env.rdram.data(), &env.ctx, &env.runtime);
            const uint32_t initial = ::getRegU32(&env.ctx, 2);
            t.IsTrue(initial != 0u, "_malloc_r should allocate guest memory");

            writeGuestU32(env.rdram.data(), initial, 0xAABBCCDDu);

            setRegU32(env.ctx, 5, initial);
            setRegU32(env.ctx, 6, 0x80u);
            ps2_stubs::realloc_r(env.rdram.data(), &env.ctx, &env.runtime);
            const uint32_t grown = ::getRegU32(&env.ctx, 2);
            t.IsTrue(grown != 0u, "_realloc_r should return a guest block");
            t.Equals(readGuestU32(env.rdram.data(), grown), 0xAABBCCDDu,
                     "_realloc_r should preserve existing guest bytes");

            setRegU32(env.ctx, 5, grown);
            ps2_stubs::free_r(env.rdram.data(), &env.ctx, &env.runtime);

            setRegU32(env.ctx, 5, 0x100u);
            ps2_stubs::malloc_extend_top(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(::getRegU32(&env.ctx, 2), 0u,
                     "malloc_extend_top should be a safe runtime-owned heap no-op");

            ps2_stubs::__malloc_lock(env.rdram.data(), &env.ctx, &env.runtime);
            ps2_stubs::__malloc_unlock(env.rdram.data(), &env.ctx, &env.runtime);
        });

        tc.Run("libc helper stubs cover memclr and libgcc div", [](TestCase &t)
        {
            TestEnv env;

            constexpr uint32_t kBuf = 0x5000u;
            std::memset(env.rdram.data() + kBuf, 0xCD, 16u);
            setRegU32(env.ctx, 4, kBuf);
            setRegU32(env.ctx, 5, 12u);
            ps2_stubs::memclr(env.rdram.data(), &env.ctx, &env.runtime);
            for (uint32_t i = 0; i < 12u; ++i)
            {
                t.Equals(env.rdram[kBuf + i], static_cast<uint8_t>(0),
                         "memclr should zero the requested byte range");
            }
            t.Equals(env.rdram[kBuf + 12u], static_cast<uint8_t>(0xCD),
                     "memclr should not write past the requested byte range");

            SET_GPR_S64(&env.ctx, 4, -9);
            SET_GPR_S64(&env.ctx, 5, 2);
            ps2_stubs::__divdi3(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), -4, "__divdi3 should divide signed 64-bit values");
        });

        tc.Run("ReleaseAlarm aliases CancelAlarm and cache toggles succeed", [](TestCase &t)
        {
            TestEnv env;

            constexpr uint32_t kAlarmHandlerAddr = 0x00270000u;
            env.runtime.registerFunction(kAlarmHandlerAddr, &alarmNoopHandler);

            setRegU32(env.ctx, 4, 0xFFFFu);
            setRegU32(env.ctx, 5, kAlarmHandlerAddr);
            setRegU32(env.ctx, 6, 0u);
            SetAlarm(env.rdram.data(), &env.ctx, &env.runtime);
            const int32_t alarmId = getRegS32(env.ctx, 2);
            t.IsTrue(alarmId > 0, "SetAlarm should create a cancellable alarm");

            setRegU32(env.ctx, 4, static_cast<uint32_t>(alarmId));
            ReleaseAlarm(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), KE_OK, "ReleaseAlarm should cancel active alarms");

            setRegU32(env.ctx, 4, static_cast<uint32_t>(alarmId));
            CancelAlarm(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), KE_ERROR,
                     "CancelAlarm should report missing alarms after ReleaseAlarm consumes them");

            EnableCache(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), KE_OK, "EnableCache should succeed as a no-op");

            DisableCache(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), KE_OK, "DisableCache should succeed as a no-op");
        });

        tc.Run("setup heap and thread invalid ids use documented kernel errors", [](TestCase &t)
        {
            TestEnv env;

            setRegU32(env.ctx, 4, 0u);
            CreateThread(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), KE_ERROR, "CreateThread with null param should fail");

            setRegU32(env.ctx, 4, 0u);
            DeleteThread(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), KE_ILLEGAL_THID, "DeleteThread(0) should be KE_ILLEGAL_THID");

            setRegU32(env.ctx, 4, 0x7FFFu);
            StartThread(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), KE_UNKNOWN_THID, "StartThread should reject unknown thread ids");

            setRegU32(env.ctx, 4, 0x7FFFu);
            WakeupThread(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), KE_UNKNOWN_THID, "WakeupThread should reject unknown thread ids");

            setRegU32(env.ctx, 4, 0x7FFFu);
            PollSema(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), KE_UNKNOWN_SEMID, "PollSema should reject unknown semaphore ids");

            setRegU32(env.ctx, 4, 0xFFFFFFFFu);
            t.IsTrue(callSyscall(0x3Du, env.rdram.data(), &env.ctx, &env.runtime), "SetupHeap syscall should dispatch");
            const uint32_t clampedBase = static_cast<uint32_t>(getRegS32(env.ctx, 2));
            t.IsTrue(clampedBase < PS2_RAM_SIZE, "SetupHeap should normalize out-of-range base into guest RAM");

            t.IsTrue(callSyscall(0x3Eu, env.rdram.data(), &env.ctx, &env.runtime), "EndOfHeap syscall should dispatch");
            const uint32_t heapEnd = static_cast<uint32_t>(getRegS32(env.ctx, 2));
            t.IsTrue(heapEnd >= clampedBase, "EndOfHeap should be at or above normalized heap base");

            setRegU32(env.ctx, 4, 1u);
            setRegU32(env.ctx, 5, 0u);
            setRegU32(env.ctx, 6, 0u);
            setRegU32(env.ctx, 29, 0x0010FFF0u);
            t.IsTrue(callSyscall(0x3Cu, env.rdram.data(), &env.ctx, &env.runtime), "SetupThread syscall should dispatch");
            const uint32_t setupSp = static_cast<uint32_t>(getRegS32(env.ctx, 2));
            t.Equals(setupSp & 0xFu, 0u, "SetupThread should always return a 16-byte aligned stack pointer");
        });

        tc.Run("OSD config2 syscalls round-trip extended config", [](TestCase &t)
        {
            TestEnv env;
            constexpr uint32_t kConfig2Addr = 0x00005000u;
            constexpr uint32_t kConfig2OutAddr = 0x00005010u;
            constexpr uint32_t kConfig1OutAddr = 0x00005020u;
            constexpr uint32_t kInitialConfig1 =
                (1u << 0) |  // SPDIF disabled
                (1u << 4) |  // non-Japanese language flag
                (1u << 13) | // OSD2
                (1u << 16);  // English
            constexpr uint32_t kConfig2Raw =
                0xABu |        // format
                (0xB0u << 8) | // daylightSaving=1, timeFormat=1, dateFormat=2
                (2u << 16) |   // extended OSD version
                (10u << 24);   // traditional Chinese

            writeGuestU32(env.rdram.data(), K_PARAM_ADDR, kInitialConfig1);
            setRegU32(env.ctx, 4, K_PARAM_ADDR);
            t.IsTrue(callSyscall(0x4Au, env.rdram.data(), &env.ctx, &env.runtime),
                     "SetOsdConfigParam syscall should dispatch");
            t.Equals(getRegS32(env.ctx, 2), KE_OK, "SetOsdConfigParam should seed base OSD state");

            writeGuestU32(env.rdram.data(), kConfig2Addr, kConfig2Raw);
            setRegU32(env.ctx, 4, kConfig2Addr);
            setRegU32(env.ctx, 5, 4u);
            setRegU32(env.ctx, 6, 0u);
            t.IsTrue(callSyscall(0x6Eu, env.rdram.data(), &env.ctx, &env.runtime),
                     "SetOsdConfigParam2 syscall should dispatch");
            t.Equals(getRegS32(env.ctx, 2), KE_OK, "SetOsdConfigParam2 should succeed");

            writeGuestU32(env.rdram.data(), kConfig2OutAddr, 0xFFFFFFFFu);
            setRegU32(env.ctx, 4, kConfig2OutAddr);
            setRegU32(env.ctx, 5, 4u);
            setRegU32(env.ctx, 6, 0u);
            t.IsTrue(callSyscall(0x6Fu, env.rdram.data(), &env.ctx, &env.runtime),
                     "GetOsdConfigParam2 syscall should dispatch");
            t.Equals(getRegS32(env.ctx, 2), KE_OK, "GetOsdConfigParam2 should succeed");
            const uint32_t readConfig2 = readGuestU32(env.rdram.data(), kConfig2OutAddr);
            t.Equals(readConfig2, kConfig2Raw, "GetOsdConfigParam2 should round-trip the sanitized Config2Param bytes");
            t.Equals((readConfig2 >> 12) & 1u, 1u, "Config2 daylightSaving should live at bit 12 for libosd callers");

            setRegU32(env.ctx, 4, kConfig1OutAddr);
            t.IsTrue(callSyscall(0x4Bu, env.rdram.data(), &env.ctx, &env.runtime),
                     "GetOsdConfigParam syscall should dispatch after Config2 update");
            const uint32_t readConfig1 = readGuestU32(env.rdram.data(), kConfig1OutAddr);
            t.Equals((readConfig1 >> 13) & 0x7u, 2u, "SetOsdConfigParam2 should sync ConfigParam.version");
            t.Equals((readConfig1 >> 16) & 0x1Fu, 10u, "SetOsdConfigParam2 should sync ConfigParam.language");
        });

        tc.Run("numeric syscall 0x83 finds matching table entry", [](TestCase &t)
        {
            TestEnv env;
            constexpr uint32_t kTableBase = 0x00002000u;
            constexpr uint32_t kValues[] = {
                0x11111111u,
                0x11223344u,
                0x55555555u,
                0x89ABCDEFu
            };

            writeGuestWords(env.rdram.data(), kTableBase, kValues, std::size(kValues));
            setRegU32(env.ctx, 4, kTableBase);
            setRegU32(env.ctx, 5, kTableBase + static_cast<uint32_t>(sizeof(kValues)));
            setRegU32(env.ctx, 6, 0x11223344u);

            t.IsTrue(callSyscall(0x83u, env.rdram.data(), &env.ctx, &env.runtime),
                     "syscall 0x83 should dispatch");
            t.Equals(static_cast<uint32_t>(getRegS32(env.ctx, 2)),
                     kTableBase + 4u,
                     "FindAddress should return address of first matching word");
        });

        tc.Run("numeric syscall 0x83 supports KSEG aliases", [](TestCase &t)
        {
            TestEnv env;
            constexpr uint32_t kTableBasePhys = 0x00003000u;
            constexpr uint32_t kTableBaseKseg = 0x80003000u;
            constexpr uint32_t kValues[] = {
                0x00123456u,
                0x8000AAAAu
            };

            writeGuestWords(env.rdram.data(), kTableBasePhys, kValues, std::size(kValues));
            setRegU32(env.ctx, 4, kTableBaseKseg);
            setRegU32(env.ctx, 5, kTableBaseKseg + static_cast<uint32_t>(sizeof(kValues)));
            setRegU32(env.ctx, 6, 0x80123456u); // Alias of first table value

            t.IsTrue(callSyscall(0x83u, env.rdram.data(), &env.ctx, &env.runtime),
                     "syscall 0x83 should dispatch");
            t.Equals(static_cast<uint32_t>(getRegS32(env.ctx, 2)),
                     kTableBaseKseg,
                     "FindAddress should match KSEG aliases and preserve guest segment in return value");
        });

        tc.Run("numeric syscall 0x83 returns 0 when entry is absent", [](TestCase &t)
        {
            TestEnv env;
            constexpr uint32_t kTableBase = 0x00004000u;
            constexpr uint32_t kValues[] = {
                0x00000001u,
                0x00000002u,
                0x00000003u
            };

            writeGuestWords(env.rdram.data(), kTableBase, kValues, std::size(kValues));
            setRegU32(env.ctx, 4, kTableBase);
            setRegU32(env.ctx, 5, kTableBase + static_cast<uint32_t>(sizeof(kValues)));
            setRegU32(env.ctx, 6, 0xDEADBEEFu);

            t.IsTrue(callSyscall(0x83u, env.rdram.data(), &env.ctx, &env.runtime),
                     "syscall 0x83 should dispatch");
            t.Equals(static_cast<uint32_t>(getRegS32(env.ctx, 2)),
                     0u,
                     "FindAddress should return 0 when no matching word exists");
        });

        tc.Run("SetSyscall mirrors guest kernel table entries into low memory", [](TestCase &t)
        {
            notifyRuntimeStop();
            TestEnv env;
            initializeGuestKernelState(env.rdram.data());

            constexpr uint32_t kGuestSyscallTableGuestBase = 0x80011F80u;
            constexpr uint32_t kSyscallIndex = 0x83u;
            constexpr uint32_t kHandler = 0x00383548u;
            constexpr uint32_t kExpectedGuestAddr = kGuestSyscallTableGuestBase + (kSyscallIndex * 4u);
            constexpr uint32_t kExpectedPhysAddr = kExpectedGuestAddr & 0x1FFFFFFFu;

            setRegU32(env.ctx, 4, kSyscallIndex);
            setRegU32(env.ctx, 5, kHandler);
            t.IsTrue(callSyscall(0x74u, env.rdram.data(), &env.ctx, &env.runtime),
                     "SetSyscall syscall should dispatch");

            uint32_t mirrored = 0u;
            std::memcpy(&mirrored, env.rdram.data() + kExpectedPhysAddr, sizeof(mirrored));
            t.Equals(mirrored,
                     kHandler,
                     "SetSyscall should mirror handler pointers into the guest kernel syscall table");

            setRegU32(env.ctx, 4, 0x80000000u);
            setRegU32(env.ctx, 5, 0x80080000u);
            setRegU32(env.ctx, 6, kHandler);
            t.IsTrue(callSyscall(0x83u, env.rdram.data(), &env.ctx, &env.runtime),
                     "FindAddress syscall should dispatch");
            t.Equals(static_cast<uint32_t>(getRegS32(env.ctx, 2)),
                     kExpectedGuestAddr,
                     "FindAddress should discover mirrored SetSyscall entries in low guest memory");

            notifyRuntimeStop();
        });

        tc.Run("SetSyscall honors signed kernel-table offsets", [](TestCase &t)
        {
            notifyRuntimeStop();
            TestEnv env;
            initializeGuestKernelState(env.rdram.data());

            constexpr uint32_t kPatchIndex = 0xFFFFC402u;
            constexpr uint32_t kHandler = 0xDEADBEEFu;
            constexpr uint32_t kExpectedGuestAddr = 0x80002F88u;
            constexpr uint32_t kExpectedPhysAddr = kExpectedGuestAddr & 0x1FFFFFFFu;

            setRegU32(env.ctx, 4, kPatchIndex);
            setRegU32(env.ctx, 5, kHandler);
            t.IsTrue(callSyscall(0x74u, env.rdram.data(), &env.ctx, &env.runtime),
                     "SetSyscall syscall should dispatch for signed offsets");

            uint32_t mirrored = 0u;
            std::memcpy(&mirrored, env.rdram.data() + kExpectedPhysAddr, sizeof(mirrored));
            t.Equals(mirrored,
                     kHandler,
                     "SetSyscall should treat the syscall index as a signed offset from the kernel table base");

            notifyRuntimeStop();
        });

        tc.Run("guest kernel syscall mirror resets between runs", [](TestCase &t)
        {
            notifyRuntimeStop();
            TestEnv env;
            initializeGuestKernelState(env.rdram.data());

            constexpr uint32_t kGuestSyscallTableGuestBase = 0x80011F80u;
            constexpr uint32_t kGuestSyscallTableProbeBase = 0x000002F0u;
            constexpr uint32_t kSyscallIndex = 0x5Au;
            constexpr uint32_t kHandler = 0x00383510u;
            constexpr uint32_t kEntryPhysAddr = (kGuestSyscallTableGuestBase + (kSyscallIndex * 4u)) & 0x1FFFFFFFu;

            setRegU32(env.ctx, 4, kSyscallIndex);
            setRegU32(env.ctx, 5, kHandler);
            t.IsTrue(callSyscall(0x74u, env.rdram.data(), &env.ctx, &env.runtime),
                     "SetSyscall syscall should dispatch");

            notifyRuntimeStop();
            initializeGuestKernelState(env.rdram.data());

            uint32_t mirrored = 1u;
            std::memcpy(&mirrored, env.rdram.data() + kEntryPhysAddr, sizeof(mirrored));
            t.Equals(mirrored,
                     0u,
                     "Initializing guest kernel state should clear stale mirrored syscall entries");

            uint32_t probeHi = 0u;
            uint32_t probeLo = 0u;
            std::memcpy(&probeHi, env.rdram.data() + kGuestSyscallTableProbeBase + 0u, sizeof(probeHi));
            std::memcpy(&probeLo, env.rdram.data() + kGuestSyscallTableProbeBase + 8u, sizeof(probeLo));
            t.Equals(probeHi,
                     kGuestSyscallTableGuestBase >> 16,
                     "Guest kernel initialization should seed the syscall table probe high word");
            t.Equals(probeLo,
                     kGuestSyscallTableGuestBase & 0xFFFFu,
                     "Guest kernel initialization should seed the syscall table probe low word");
        });

        tc.Run("SetSyscall override dispatches guest handlers that return through the sentinel", [](TestCase &t)
        {
            notifyRuntimeStop();
            TestEnv env;
            constexpr uint32_t kSyscallIndex = 0x91u;
            constexpr uint32_t kHandler = 0x00200000u;

            env.runtime.registerFunction(kHandler, overrideReturnHandler);
            setRegU32(env.ctx, 4, kSyscallIndex);
            setRegU32(env.ctx, 5, kHandler);
            t.IsTrue(callSyscall(0x74u, env.rdram.data(), &env.ctx, &env.runtime),
                     "SetSyscall syscall should dispatch");

            setRegU32(env.ctx, 4, 7u);
            setRegU32(env.ctx, 5, 5u);
            t.IsTrue(callSyscall(kSyscallIndex, env.rdram.data(), &env.ctx, &env.runtime),
                     "Overridden syscall should dispatch through guest handler");
            t.Equals(static_cast<uint32_t>(getRegS32(env.ctx, 2)),
                     12u,
                     "Successful override dispatch should propagate guest handler return value");

            notifyRuntimeStop();
        });

        tc.Run("SetSyscall override preserves KSEG argument sign extension", [](TestCase &t)
        {
            notifyRuntimeStop();
            TestEnv env;
            constexpr uint32_t kSyscallIndex = 0x92u;
            constexpr uint32_t kHandler = 0x00200030u;

            env.runtime.registerFunction(kHandler, overrideKsegCompareHandler);
            setRegU32(env.ctx, 4, kSyscallIndex);
            setRegU32(env.ctx, 5, kHandler);
            t.IsTrue(callSyscall(0x74u, env.rdram.data(), &env.ctx, &env.runtime),
                     "SetSyscall syscall should dispatch");

            setRegU32(env.ctx, 4, 0x80000000u);
            setRegU32(env.ctx, 5, 0x80080000u);
            t.IsTrue(callSyscall(kSyscallIndex, env.rdram.data(), &env.ctx, &env.runtime),
                     "Override syscall should invoke the guest handler");
            t.Equals(static_cast<uint32_t>(getRegS32(env.ctx, 2)),
                     0x80000004u,
                     "Override invocation should preserve KSEG ordering after 32-bit guest writes");

            notifyRuntimeStop();
        });

        tc.Run("SetSyscall override preserves upper 64 bits when writing 32-bit args", [](TestCase &t)
        {
            notifyRuntimeStop();
            TestEnv env;
            constexpr uint32_t kSyscallIndex = 0x93u;
            constexpr uint32_t kHandler = 0x00200040u;

            env.runtime.registerFunction(kHandler, overridePreserveUpper64Handler);
            setRegU32(env.ctx, 4, kSyscallIndex);
            setRegU32(env.ctx, 5, kHandler);
            t.IsTrue(callSyscall(0x74u, env.rdram.data(), &env.ctx, &env.runtime),
                     "SetSyscall syscall should dispatch");

            env.ctx.r[4] = _mm_set_epi64x(static_cast<int64_t>(K_EXPECTED_UPPER64),
                                          static_cast<int64_t>(static_cast<int32_t>(0x80000000u)));
            t.IsTrue(callSyscall(kSyscallIndex, env.rdram.data(), &env.ctx, &env.runtime),
                     "Override syscall should invoke the guest handler");
            t.Equals(static_cast<uint32_t>(getRegS32(env.ctx, 2)),
                     1u,
                     "Override invocation should preserve the upper 64 bits of 128-bit GPRs when setting 32-bit args");

            notifyRuntimeStop();
        });

        tc.Run("broken syscall overrides fall back to builtin handlers", [](TestCase &t)
        {
            notifyRuntimeStop();
            TestEnv env;
            constexpr uint32_t kHandler = 0x00200010u;
            constexpr uint32_t kTableBase = 0x00002000u;
            constexpr uint32_t kValues[] = {
                0x11111111u,
                0x11223344u,
                0x55555555u
            };

            env.runtime.registerFunction(kHandler, overrideBrokenHandler);
            setRegU32(env.ctx, 4, 0x83u);
            setRegU32(env.ctx, 5, kHandler);
            t.IsTrue(callSyscall(0x74u, env.rdram.data(), &env.ctx, &env.runtime),
                     "SetSyscall syscall should dispatch");

            writeGuestWords(env.rdram.data(), kTableBase, kValues, std::size(kValues));
            setRegU32(env.ctx, 4, kTableBase);
            setRegU32(env.ctx, 5, kTableBase + static_cast<uint32_t>(sizeof(kValues)));
            setRegU32(env.ctx, 6, 0x11223344u);
            t.IsTrue(callSyscall(0x83u, env.rdram.data(), &env.ctx, &env.runtime),
                     "Builtin syscall should still dispatch when override exits abnormally");
            t.Equals(static_cast<uint32_t>(getRegS32(env.ctx, 2)),
                     kTableBase + 4u,
                     "Abnormal override exits should fall back to the builtin syscall implementation");

            notifyRuntimeStop();
        });

        tc.Run("reentrant syscall overrides fall back to builtin handlers", [](TestCase &t)
        {
            notifyRuntimeStop();
            TestEnv env;
            constexpr uint32_t kHandler = 0x00200020u;
            constexpr uint32_t kTableBase = 0x00003000u;
            constexpr uint32_t kValues[] = {
                0xCAFEBABEu,
                0x11223344u,
                0x55667788u
            };

            env.runtime.registerFunction(kHandler, overrideRecursiveFindAddressHandler);
            setRegU32(env.ctx, 4, 0x83u);
            setRegU32(env.ctx, 5, kHandler);
            t.IsTrue(callSyscall(0x74u, env.rdram.data(), &env.ctx, &env.runtime),
                     "SetSyscall syscall should dispatch");

            writeGuestWords(env.rdram.data(), kTableBase, kValues, std::size(kValues));
            setRegU32(env.ctx, 4, kTableBase);
            setRegU32(env.ctx, 5, kTableBase + static_cast<uint32_t>(sizeof(kValues)));
            setRegU32(env.ctx, 6, 0x11223344u);
            t.IsTrue(callSyscall(0x83u, env.rdram.data(), &env.ctx, &env.runtime),
                     "Reentrant override should resolve through builtin fallback");
            t.Equals(static_cast<uint32_t>(getRegS32(env.ctx, 2)),
                     kTableBase + 4u,
                     "Reentrant override dispatch should use builtin syscall implementation");

            notifyRuntimeStop();
        });

        tc.Run("Copy syscall (0x5A) performs a memory copy", [](TestCase &t)
        {
            TestEnv env;
            constexpr uint32_t kDestAddr = 0x00005000u;
            constexpr uint32_t kSrcAddr = 0x00006000u;
            constexpr uint32_t kSize = 16u;
            constexpr uint32_t kValues[] = {
                0x11223344u,
                0x55667788u,
                0x99AABBCCu,
                0xDDEEFF00u
            };

            writeGuestWords(env.rdram.data(), kSrcAddr, kValues, std::size(kValues));
            
            setRegU32(env.ctx, 4, kDestAddr);
            setRegU32(env.ctx, 5, kSrcAddr);
            setRegU32(env.ctx, 6, kSize);

            t.IsTrue(callSyscall(0x5Au, env.rdram.data(), &env.ctx, &env.runtime),
                     "Copy syscall should dispatch");
            
            for (size_t i = 0; i < std::size(kValues); ++i)
            {
                uint32_t destVal = readGuestU32(env.rdram.data(), kDestAddr + static_cast<uint32_t>(i * sizeof(uint32_t)));
                t.Equals(destVal, kValues[i], "Copy should correctly transfer bytes");
            }
        });

        tc.Run("GetEntryAddress syscall (0x5B) returns handler from guest table", [](TestCase &t)
        {
            notifyRuntimeStop();
            TestEnv env;
            initializeGuestKernelState(env.rdram.data());

            constexpr uint32_t kGuestSyscallTableGuestBase = 0x80011F80u;
            constexpr uint32_t kSyscallIndex = 0x5Au;
            constexpr uint32_t kExpectedHandler = 0x00383548u;
            constexpr uint32_t kEntryPhysAddr = (kGuestSyscallTableGuestBase + (kSyscallIndex * 4u)) & 0x1FFFFFFFu;

            writeGuestU32(env.rdram.data(), kEntryPhysAddr, kExpectedHandler);

            setRegU32(env.ctx, 4, kSyscallIndex);

            t.IsTrue(callSyscall(0x5Bu, env.rdram.data(), &env.ctx, &env.runtime),
                     "GetEntryAddress syscall should dispatch");
            
            t.Equals(static_cast<uint32_t>(getRegS32(env.ctx, 2)),
                     kExpectedHandler,
                     "GetEntryAddress should read and return the handler address from the table");

            notifyRuntimeStop();
        });
    });
}
