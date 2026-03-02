#include "MiniTest.h"
#include "ps2_runtime.h"
#include "ps2_syscalls.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

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

    constexpr int THS_DORMANT = 0x10;

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
        ctx.r[reg] = _mm_set_epi64x(0, static_cast<int64_t>(value));
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

    bool callSyscall(uint32_t syscallNumber, uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        return dispatchNumericSyscall(syscallNumber, rdram, ctx, runtime);
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
            t.Equals(getRegS32(env.ctx, 2), KE_OK, "PollSema should consume one available token");

            setRegU32(env.ctx, 4, static_cast<uint32_t>(sid));
            PollSema(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), KE_SEMA_ZERO, "PollSema should fail when count is zero");

            setRegU32(env.ctx, 4, static_cast<uint32_t>(sid));
            SignalSema(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), KE_OK, "SignalSema should increment count when below max");

            setRegU32(env.ctx, 4, static_cast<uint32_t>(sid));
            SignalSema(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), KE_OK, "SignalSema should allow increment up to max");

            setRegU32(env.ctx, 4, static_cast<uint32_t>(sid));
            SignalSema(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), KE_SEMA_OVF, "SignalSema should report overflow at max_count");

            setRegU32(env.ctx, 4, static_cast<uint32_t>(sid));
            DeleteSema(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), KE_OK, "DeleteSema should succeed for existing semaphore");

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
            t.Equals(getRegS32(env.ctx, 2), KE_OK, "DeleteSema should clean up legacy-decoded semaphore");
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
            const uint32_t heapEndBefore = static_cast<uint32_t>(getRegS32(env.ctx, 2));
            t.Equals(heapEndBefore, heapBase, "EndOfHeap should start at heap base before allocation");

            const uint32_t alignedAlloc = env.runtime.guestMalloc(0x20u, 64u);
            t.IsTrue(alignedAlloc != 0u, "guestMalloc should allocate inside configured heap");
            t.Equals(alignedAlloc & 0x3Fu, 0u, "guestMalloc should honor 64-byte alignment");

            t.IsTrue(callSyscall(0x3Eu, env.rdram.data(), &env.ctx, &env.runtime), "EndOfHeap syscall should dispatch");
            const uint32_t heapEndAfter = static_cast<uint32_t>(getRegS32(env.ctx, 2));
            t.IsTrue(heapEndAfter >= alignedAlloc + 0x20u, "EndOfHeap should advance after allocation");

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
    });
}
