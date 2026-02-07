#include "MiniTest.h"

#ifdef PS2X_HAS_RUNTIME

#include "ps2_runtime.h"
#include "ps2_syscalls.h"
#include <cstring>
#include <chrono>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

extern const char *translateFioMode(int ps2Flags);

namespace
{
    constexpr uint32_t EVENT_WAIT_OR = 0x01;
    constexpr uint32_t EVENT_WAIT_CLEAR = 0x10;
    constexpr size_t TEST_RAM_SIZE = 1u << 20;

    void setArgU32(R5900Context &ctx, int reg, uint32_t value)
    {
        ctx.r[reg] = _mm_set_epi32(0, 0, 0, static_cast<int32_t>(value));
    }

    int32_t getReturnS32(const R5900Context &ctx)
    {
        return static_cast<int32_t>(getRegU32(&ctx, 2));
    }

    bool waitForAtomicTrue(const std::atomic<bool> &flag, const std::chrono::milliseconds timeout)
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (!flag.load(std::memory_order_acquire))
        {
            if (std::chrono::steady_clock::now() >= deadline)
            {
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return true;
    }

    std::string uniqueTestName(const char *prefix)
    {
        static std::atomic<uint64_t> counter{0};
        static const uint64_t sessionSeed = static_cast<uint64_t>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count());
        const uint64_t value = counter.fetch_add(1, std::memory_order_relaxed);
        return std::string(prefix) + "_" + std::to_string(sessionSeed) + "_" + std::to_string(value);
    }
}

void register_runtime_syscall_tests()
{
    MiniTest::Case("RuntimeSyscalls", [](TestCase &tc)
                   {
        tc.Run("event flags create poll set clear and delete", [](TestCase &t) {
            std::vector<uint8_t> rdram(TEST_RAM_SIZE, 0);
            R5900Context ctx{};

            constexpr uint32_t paramAddr = 0x1000;
            constexpr uint32_t resultAddr = 0x1100;

            auto *params = reinterpret_cast<uint32_t *>(rdram.data() + paramAddr);
            params[0] = 0;    // attr
            params[1] = 0;    // option
            params[2] = 0x04; // init pattern

            setArgU32(ctx, 4, paramAddr);
            ps2_syscalls::CreateEventFlag(rdram.data(), &ctx, nullptr);
            const int32_t flagId = getReturnS32(ctx);
            t.IsTrue(flagId > 0, "CreateEventFlag should return a positive id");

            setArgU32(ctx, 4, static_cast<uint32_t>(flagId));
            setArgU32(ctx, 5, 0x04);
            setArgU32(ctx, 6, 0x00); // AND
            setArgU32(ctx, 7, resultAddr);
            ps2_syscalls::PollEventFlag(rdram.data(), &ctx, nullptr);
            t.Equals(getReturnS32(ctx), 0, "PollEventFlag should match initial bits");

            auto *pollResult = reinterpret_cast<uint32_t *>(rdram.data() + resultAddr);
            t.Equals(*pollResult, 0x04u, "result should capture current pattern");

            setArgU32(ctx, 4, static_cast<uint32_t>(flagId));
            setArgU32(ctx, 5, 0x00); // clear all with pattern &= 0
            ps2_syscalls::ClearEventFlag(rdram.data(), &ctx, nullptr);
            t.Equals(getReturnS32(ctx), 0, "ClearEventFlag should succeed");

            setArgU32(ctx, 4, static_cast<uint32_t>(flagId));
            setArgU32(ctx, 5, 0x04);
            setArgU32(ctx, 6, 0x00);
            setArgU32(ctx, 7, resultAddr);
            ps2_syscalls::PollEventFlag(rdram.data(), &ctx, nullptr);
            t.Equals(getReturnS32(ctx), -1, "PollEventFlag should fail when requested bits are not set");

            setArgU32(ctx, 4, static_cast<uint32_t>(flagId));
            setArgU32(ctx, 5, 0x06);
            ps2_syscalls::SetEventFlag(rdram.data(), &ctx, nullptr);
            t.Equals(getReturnS32(ctx), 0, "SetEventFlag should succeed");

            setArgU32(ctx, 4, static_cast<uint32_t>(flagId));
            setArgU32(ctx, 5, 0x02);
            setArgU32(ctx, 6, EVENT_WAIT_OR);
            setArgU32(ctx, 7, resultAddr);
            ps2_syscalls::PollEventFlag(rdram.data(), &ctx, nullptr);
            t.Equals(getReturnS32(ctx), 0, "PollEventFlag OR mode should succeed when any requested bit is set");

            setArgU32(ctx, 4, static_cast<uint32_t>(flagId));
            ps2_syscalls::DeleteEventFlag(rdram.data(), &ctx, nullptr);
            t.Equals(getReturnS32(ctx), 0, "DeleteEventFlag should succeed");

            setArgU32(ctx, 4, static_cast<uint32_t>(flagId));
            setArgU32(ctx, 5, 0x02);
            setArgU32(ctx, 6, EVENT_WAIT_OR);
            setArgU32(ctx, 7, resultAddr);
            ps2_syscalls::PollEventFlag(rdram.data(), &ctx, nullptr);
            t.Equals(getReturnS32(ctx), -1, "PollEventFlag should fail after deletion");
        });

        tc.Run("event flags wait supports clear mode", [](TestCase &t) {
            std::vector<uint8_t> rdram(TEST_RAM_SIZE, 0);
            R5900Context ctx{};

            constexpr uint32_t paramAddr = 0x1200;
            constexpr uint32_t resultAddr = 0x1210;

            auto *params = reinterpret_cast<uint32_t *>(rdram.data() + paramAddr);
            params[0] = 0;
            params[1] = 0;
            params[2] = 0x08; // already signaled to avoid blocking in unit tests

            setArgU32(ctx, 4, paramAddr);
            ps2_syscalls::CreateEventFlag(rdram.data(), &ctx, nullptr);
            const int32_t flagId = getReturnS32(ctx);
            t.IsTrue(flagId > 0, "CreateEventFlag should return a valid id");

            setArgU32(ctx, 4, static_cast<uint32_t>(flagId));
            setArgU32(ctx, 5, 0x08);
            setArgU32(ctx, 6, EVENT_WAIT_OR | EVENT_WAIT_CLEAR);
            setArgU32(ctx, 7, resultAddr);
            ps2_syscalls::WaitEventFlag(rdram.data(), &ctx, nullptr);
            t.Equals(getReturnS32(ctx), 0, "WaitEventFlag should succeed");

            auto *waitResult = reinterpret_cast<uint32_t *>(rdram.data() + resultAddr);
            t.Equals(*waitResult, 0x08u, "wait result should report the matched pattern");

            setArgU32(ctx, 4, static_cast<uint32_t>(flagId));
            setArgU32(ctx, 5, 0x08);
            setArgU32(ctx, 6, EVENT_WAIT_OR);
            setArgU32(ctx, 7, resultAddr);
            ps2_syscalls::PollEventFlag(rdram.data(), &ctx, nullptr);
            t.Equals(getReturnS32(ctx), -1, "clear mode should consume the matched bits");

            setArgU32(ctx, 4, static_cast<uint32_t>(flagId));
            ps2_syscalls::DeleteEventFlag(rdram.data(), &ctx, nullptr);
            t.Equals(getReturnS32(ctx), 0, "DeleteEventFlag should succeed");
        });

        tc.Run("WaitEventFlag wakes with error after deletion", [](TestCase &t) {
            auto rdram = std::make_shared<std::vector<uint8_t>>(TEST_RAM_SIZE, 0);
            R5900Context mainCtx{};

            constexpr uint32_t paramAddr = 0x1260;
            auto *params = reinterpret_cast<uint32_t *>(rdram->data() + paramAddr);
            params[0] = 0;
            params[1] = 0;
            params[2] = 0; // no bits set so waiter blocks

            setArgU32(mainCtx, 4, paramAddr);
            ps2_syscalls::CreateEventFlag(rdram->data(), &mainCtx, nullptr);
            const int32_t flagId = getReturnS32(mainCtx);
            t.IsTrue(flagId > 0, "CreateEventFlag should return a valid id");

            auto waiterCtx = std::make_shared<R5900Context>();
            auto waiterStarted = std::make_shared<std::atomic<bool>>(false);
            auto waiterDone = std::make_shared<std::atomic<bool>>(false);
            auto waiterReturn = std::make_shared<std::atomic<int32_t>>(0x7FFFFFFF);

            std::thread waiter([rdram, waiterCtx, flagId, waiterStarted, waiterDone, waiterReturn]() {
                setArgU32(*waiterCtx, 4, static_cast<uint32_t>(flagId));
                setArgU32(*waiterCtx, 5, 0x01);
                setArgU32(*waiterCtx, 6, EVENT_WAIT_OR);
                setArgU32(*waiterCtx, 7, 0);
                waiterStarted->store(true, std::memory_order_release);
                ps2_syscalls::WaitEventFlag(rdram->data(), waiterCtx.get(), nullptr);
                waiterReturn->store(getReturnS32(*waiterCtx), std::memory_order_release);
                waiterDone->store(true, std::memory_order_release);
            });

            t.IsTrue(waitForAtomicTrue(*waiterStarted, std::chrono::milliseconds(200)),
                     "waiter thread should start promptly");
            std::this_thread::sleep_for(std::chrono::milliseconds(10));

            setArgU32(mainCtx, 4, static_cast<uint32_t>(flagId));
            ps2_syscalls::DeleteEventFlag(rdram->data(), &mainCtx, nullptr);
            t.Equals(getReturnS32(mainCtx), 0, "DeleteEventFlag should succeed");

            if (!waitForAtomicTrue(*waiterDone, std::chrono::milliseconds(500)))
            {
                t.Fail("WaitEventFlag waiter did not wake after DeleteEventFlag");
                waiter.detach();
                return;
            }

            waiter.join();
            t.Equals(waiterReturn->load(std::memory_order_acquire), -1,
                     "WaitEventFlag should return -1 when woken by deletion");
        });

        tc.Run("event flag operations fail after deletion", [](TestCase &t) {
            std::vector<uint8_t> rdram(TEST_RAM_SIZE, 0);
            R5900Context ctx{};

            constexpr uint32_t paramAddr = 0x1220;
            constexpr uint32_t resultAddr = 0x1230;
            constexpr uint32_t statusAddr = 0x1240;

            auto *params = reinterpret_cast<uint32_t *>(rdram.data() + paramAddr);
            params[0] = 0;
            params[1] = 0;
            params[2] = 0;

            setArgU32(ctx, 4, paramAddr);
            ps2_syscalls::CreateEventFlag(rdram.data(), &ctx, nullptr);
            const int32_t flagId = getReturnS32(ctx);
            t.IsTrue(flagId > 0, "CreateEventFlag should return a valid id");

            setArgU32(ctx, 4, static_cast<uint32_t>(flagId));
            ps2_syscalls::DeleteEventFlag(rdram.data(), &ctx, nullptr);
            t.Equals(getReturnS32(ctx), 0, "DeleteEventFlag should succeed");

            setArgU32(ctx, 4, static_cast<uint32_t>(flagId));
            setArgU32(ctx, 5, 0x01);
            ps2_syscalls::SetEventFlag(rdram.data(), &ctx, nullptr);
            t.Equals(getReturnS32(ctx), -1, "SetEventFlag should fail for deleted flag");

            setArgU32(ctx, 4, static_cast<uint32_t>(flagId));
            setArgU32(ctx, 5, 0x00);
            ps2_syscalls::ClearEventFlag(rdram.data(), &ctx, nullptr);
            t.Equals(getReturnS32(ctx), -1, "ClearEventFlag should fail for deleted flag");

            setArgU32(ctx, 4, static_cast<uint32_t>(flagId));
            setArgU32(ctx, 5, 0x01);
            setArgU32(ctx, 6, EVENT_WAIT_OR);
            setArgU32(ctx, 7, resultAddr);
            ps2_syscalls::PollEventFlag(rdram.data(), &ctx, nullptr);
            t.Equals(getReturnS32(ctx), -1, "PollEventFlag should fail for deleted flag");

            setArgU32(ctx, 4, static_cast<uint32_t>(flagId));
            setArgU32(ctx, 5, statusAddr);
            ps2_syscalls::ReferEventFlagStatus(rdram.data(), &ctx, nullptr);
            t.Equals(getReturnS32(ctx), -1, "ReferEventFlagStatus should fail for deleted flag");
        });

        tc.Run("DeleteEventFlag returns -1 for unknown or already deleted id", [](TestCase &t) {
            std::vector<uint8_t> rdram(TEST_RAM_SIZE, 0);
            R5900Context ctx{};

            constexpr uint32_t paramAddr = 0x1280;
            auto *params = reinterpret_cast<uint32_t *>(rdram.data() + paramAddr);
            params[0] = 0;
            params[1] = 0;
            params[2] = 0;

            setArgU32(ctx, 4, 0x7FFF1234u);
            ps2_syscalls::DeleteEventFlag(rdram.data(), &ctx, nullptr);
            t.Equals(getReturnS32(ctx), -1, "DeleteEventFlag should fail for unknown id");

            setArgU32(ctx, 4, paramAddr);
            ps2_syscalls::CreateEventFlag(rdram.data(), &ctx, nullptr);
            const int32_t flagId = getReturnS32(ctx);
            t.IsTrue(flagId > 0, "CreateEventFlag should return a valid id");

            setArgU32(ctx, 4, static_cast<uint32_t>(flagId));
            ps2_syscalls::DeleteEventFlag(rdram.data(), &ctx, nullptr);
            t.Equals(getReturnS32(ctx), 0, "DeleteEventFlag should succeed on first delete");

            setArgU32(ctx, 4, static_cast<uint32_t>(flagId));
            ps2_syscalls::DeleteEventFlag(rdram.data(), &ctx, nullptr);
            t.Equals(getReturnS32(ctx), -1, "DeleteEventFlag should fail when deleting an already deleted id");
        });


        tc.Run("semaphore poll signal and status report", [](TestCase &t) {
            std::vector<uint8_t> rdram(TEST_RAM_SIZE, 0);
            R5900Context ctx{};

            constexpr uint32_t paramAddr = 0x2000;
            constexpr uint32_t statusAddr = 0x2100;

            auto *params = reinterpret_cast<uint32_t *>(rdram.data() + paramAddr);
            params[0] = 0x12; // attr
            params[1] = 0x34; // option
            params[2] = 1;    // init count
            params[3] = 3;    // max count

            setArgU32(ctx, 4, paramAddr);
            ps2_syscalls::CreateSema(rdram.data(), &ctx, nullptr);
            const int32_t semaId = getReturnS32(ctx);
            t.IsTrue(semaId > 0, "CreateSema should return a positive id");

            setArgU32(ctx, 4, static_cast<uint32_t>(semaId));
            ps2_syscalls::PollSema(rdram.data(), &ctx, nullptr);
            t.Equals(getReturnS32(ctx), 0, "first PollSema should consume the initial token");

            setArgU32(ctx, 4, static_cast<uint32_t>(semaId));
            ps2_syscalls::PollSema(rdram.data(), &ctx, nullptr);
            t.Equals(getReturnS32(ctx), -1, "second PollSema should fail when count is zero");

            setArgU32(ctx, 4, static_cast<uint32_t>(semaId));
            ps2_syscalls::SignalSema(rdram.data(), &ctx, nullptr);
            t.Equals(getReturnS32(ctx), 0, "SignalSema should increment semaphore count");

            setArgU32(ctx, 4, static_cast<uint32_t>(semaId));
            setArgU32(ctx, 5, statusAddr);
            ps2_syscalls::ReferSemaStatus(rdram.data(), &ctx, nullptr);
            t.Equals(getReturnS32(ctx), 0, "ReferSemaStatus should succeed");

            auto *status = reinterpret_cast<uint32_t *>(rdram.data() + statusAddr);
            t.Equals(status[0], 0x12u, "status attr should match create params");
            t.Equals(status[1], 0x34u, "status option should match create params");
            t.Equals(status[2], 1u, "status init count should match create params");
            t.Equals(status[3], 3u, "status max count should match create params");
            t.Equals(status[4], 1u, "status current count should reflect signaling");
            t.Equals(status[5], 0u, "status waiter count should be zero in this test");

            setArgU32(ctx, 4, static_cast<uint32_t>(semaId));
            ps2_syscalls::DeleteSema(rdram.data(), &ctx, nullptr);
            t.Equals(getReturnS32(ctx), 0, "DeleteSema should succeed");
        });

        tc.Run("WaitSema wakes with error after deletion", [](TestCase &t) {
            auto rdram = std::make_shared<std::vector<uint8_t>>(TEST_RAM_SIZE, 0);
            R5900Context mainCtx{};

            constexpr uint32_t paramAddr = 0x2240;
            auto *params = reinterpret_cast<uint32_t *>(rdram->data() + paramAddr);
            params[0] = 0;
            params[1] = 0;
            params[2] = 0; // init count so waiter blocks
            params[3] = 1;

            setArgU32(mainCtx, 4, paramAddr);
            ps2_syscalls::CreateSema(rdram->data(), &mainCtx, nullptr);
            const int32_t semaId = getReturnS32(mainCtx);
            t.IsTrue(semaId > 0, "CreateSema should return a valid id");

            auto waiterCtx = std::make_shared<R5900Context>();
            auto waiterStarted = std::make_shared<std::atomic<bool>>(false);
            auto waiterDone = std::make_shared<std::atomic<bool>>(false);
            auto waiterReturn = std::make_shared<std::atomic<int32_t>>(0x7FFFFFFF);

            std::thread waiter([rdram, waiterCtx, semaId, waiterStarted, waiterDone, waiterReturn]() {
                setArgU32(*waiterCtx, 4, static_cast<uint32_t>(semaId));
                waiterStarted->store(true, std::memory_order_release);
                ps2_syscalls::WaitSema(rdram->data(), waiterCtx.get(), nullptr);
                waiterReturn->store(getReturnS32(*waiterCtx), std::memory_order_release);
                waiterDone->store(true, std::memory_order_release);
            });

            t.IsTrue(waitForAtomicTrue(*waiterStarted, std::chrono::milliseconds(200)),
                     "waiter thread should start promptly");
            std::this_thread::sleep_for(std::chrono::milliseconds(10));

            setArgU32(mainCtx, 4, static_cast<uint32_t>(semaId));
            ps2_syscalls::DeleteSema(rdram->data(), &mainCtx, nullptr);
            t.Equals(getReturnS32(mainCtx), 0, "DeleteSema should succeed");

            if (!waitForAtomicTrue(*waiterDone, std::chrono::milliseconds(500)))
            {
                t.Fail("WaitSema waiter did not wake after DeleteSema");
                waiter.detach();
                return;
            }

            waiter.join();
            t.Equals(waiterReturn->load(std::memory_order_acquire), -1,
                     "WaitSema should return -1 when woken by deletion");
        });

        tc.Run("semaphore operations fail after deletion", [](TestCase &t) {
            std::vector<uint8_t> rdram(TEST_RAM_SIZE, 0);
            R5900Context ctx{};

            constexpr uint32_t paramAddr = 0x2200;
            auto *params = reinterpret_cast<uint32_t *>(rdram.data() + paramAddr);
            params[0] = 0;
            params[1] = 0;
            params[2] = 0;
            params[3] = 1;

            setArgU32(ctx, 4, paramAddr);
            ps2_syscalls::CreateSema(rdram.data(), &ctx, nullptr);
            const int32_t semaId = getReturnS32(ctx);
            t.IsTrue(semaId > 0, "CreateSema should return a positive id");

            setArgU32(ctx, 4, static_cast<uint32_t>(semaId));
            ps2_syscalls::DeleteSema(rdram.data(), &ctx, nullptr);
            t.Equals(getReturnS32(ctx), 0, "DeleteSema should succeed");

            setArgU32(ctx, 4, static_cast<uint32_t>(semaId));
            ps2_syscalls::PollSema(rdram.data(), &ctx, nullptr);
            t.Equals(getReturnS32(ctx), -1, "PollSema should fail for deleted semaphore");

            setArgU32(ctx, 4, static_cast<uint32_t>(semaId));
            ps2_syscalls::SignalSema(rdram.data(), &ctx, nullptr);
            t.Equals(getReturnS32(ctx), -1, "SignalSema should fail for deleted semaphore");
        });

        tc.Run("DeleteSema returns -1 for unknown or already deleted id", [](TestCase &t) {
            std::vector<uint8_t> rdram(TEST_RAM_SIZE, 0);
            R5900Context ctx{};

            constexpr uint32_t paramAddr = 0x2260;
            auto *params = reinterpret_cast<uint32_t *>(rdram.data() + paramAddr);
            params[0] = 0;
            params[1] = 0;
            params[2] = 0;
            params[3] = 1;

            setArgU32(ctx, 4, 0x7FFF5678u);
            ps2_syscalls::DeleteSema(rdram.data(), &ctx, nullptr);
            t.Equals(getReturnS32(ctx), -1, "DeleteSema should fail for unknown id");

            setArgU32(ctx, 4, paramAddr);
            ps2_syscalls::CreateSema(rdram.data(), &ctx, nullptr);
            const int32_t semaId = getReturnS32(ctx);
            t.IsTrue(semaId > 0, "CreateSema should return a valid id");

            setArgU32(ctx, 4, static_cast<uint32_t>(semaId));
            ps2_syscalls::DeleteSema(rdram.data(), &ctx, nullptr);
            t.Equals(getReturnS32(ctx), 0, "DeleteSema should succeed on first delete");

            setArgU32(ctx, 4, static_cast<uint32_t>(semaId));
            ps2_syscalls::DeleteSema(rdram.data(), &ctx, nullptr);
            t.Equals(getReturnS32(ctx), -1, "DeleteSema should fail when deleting an already deleted id");
        });

        tc.Run("thread status reflects created and updated thread fields", [](TestCase &t) {
            std::vector<uint8_t> rdram(TEST_RAM_SIZE, 0);
            R5900Context ctx{};

            constexpr uint32_t paramAddr = 0x3000;
            constexpr uint32_t statusAddr = 0x3100;

            auto *params = reinterpret_cast<uint32_t *>(rdram.data() + paramAddr);
            params[0] = 0xA1;      // attr
            params[1] = 0x123456;  // entry
            params[2] = 0x200000;  // stack
            params[3] = 0x4000;    // stackSize
            params[4] = 9;         // priority
            params[5] = 0x334455;  // gp
            params[6] = 0xB2;      // option

            setArgU32(ctx, 4, paramAddr);
            ps2_syscalls::CreateThread(rdram.data(), &ctx, nullptr);
            const int32_t threadId = getReturnS32(ctx);
            t.IsTrue(threadId > 0, "CreateThread should return a positive id");

            setArgU32(ctx, 4, static_cast<uint32_t>(threadId));
            setArgU32(ctx, 5, 33);
            ps2_syscalls::ChangeThreadPriority(rdram.data(), &ctx, nullptr);
            t.Equals(getReturnS32(ctx), 0, "ChangeThreadPriority should succeed");

            setArgU32(ctx, 4, static_cast<uint32_t>(threadId));
            setArgU32(ctx, 5, statusAddr);
            ps2_syscalls::ReferThreadStatus(rdram.data(), &ctx, nullptr);
            t.Equals(getReturnS32(ctx), 0, "ReferThreadStatus should succeed for a valid thread");

            auto *status = reinterpret_cast<uint32_t *>(rdram.data() + statusAddr);
            t.Equals(status[0], 0xA1u, "thread status attr should match create params");
            t.Equals(status[1], 0xB2u, "thread status option should match create params");
            t.Equals(status[2], 0u, "thread should not be marked started before StartThread");
            t.Equals(status[3], 0x123456u, "thread entry should match create params");
            t.Equals(status[4], 0x200000u, "thread stack should match create params");
            t.Equals(status[5], 0x4000u, "thread stack size should match create params");
            t.Equals(status[6], 0x334455u, "thread gp should match create params");
            t.Equals(status[7], 33u, "thread priority should reflect ChangeThreadPriority");

            setArgU32(ctx, 4, static_cast<uint32_t>(threadId));
            ps2_syscalls::DeleteThread(rdram.data(), &ctx, nullptr);
            t.Equals(getReturnS32(ctx), 0, "DeleteThread should succeed");

            setArgU32(ctx, 4, static_cast<uint32_t>(threadId));
            setArgU32(ctx, 5, statusAddr);
            ps2_syscalls::ReferThreadStatus(rdram.data(), &ctx, nullptr);
            t.Equals(getReturnS32(ctx), -1, "ReferThreadStatus should fail for a deleted thread");
        });

        tc.Run("StartThread with null runtime fails and leaves thread stopped", [](TestCase &t) {
            std::vector<uint8_t> rdram(TEST_RAM_SIZE, 0);
            R5900Context ctx{};

            constexpr uint32_t paramAddr = 0x3200;
            constexpr uint32_t statusAddr = 0x3300;

            auto *params = reinterpret_cast<uint32_t *>(rdram.data() + paramAddr);
            params[0] = 0;
            params[1] = 0xDEADBEEF;
            params[2] = 0x1000;
            params[3] = 0x200;
            params[4] = 16;
            params[5] = 0;
            params[6] = 0;

            setArgU32(ctx, 4, paramAddr);
            ps2_syscalls::CreateThread(rdram.data(), &ctx, nullptr);
            const int32_t threadId = getReturnS32(ctx);
            t.IsTrue(threadId > 0, "CreateThread should return a valid id");

            setArgU32(ctx, 4, static_cast<uint32_t>(threadId));
            setArgU32(ctx, 5, 0x42);
            ps2_syscalls::StartThread(rdram.data(), &ctx, nullptr);
            t.Equals(getReturnS32(ctx), -1, "StartThread should fail when runtime is null");

            setArgU32(ctx, 4, static_cast<uint32_t>(threadId));
            setArgU32(ctx, 5, statusAddr);
            ps2_syscalls::ReferThreadStatus(rdram.data(), &ctx, nullptr);
            t.Equals(getReturnS32(ctx), 0, "ReferThreadStatus should still succeed");

            auto *status = reinterpret_cast<uint32_t *>(rdram.data() + statusAddr);
            t.Equals(status[2], 0u, "thread should remain stopped after failed StartThread");
            t.Equals(status[8], 0u, "thread arg should remain unchanged after failed StartThread");

            setArgU32(ctx, 4, static_cast<uint32_t>(threadId));
            ps2_syscalls::DeleteThread(rdram.data(), &ctx, nullptr);
            t.Equals(getReturnS32(ctx), 0, "DeleteThread should succeed");
        });

        tc.Run("GS IMR syscalls round-trip runtime state", [](TestCase &t) {
            std::vector<uint8_t> rdram(TEST_RAM_SIZE, 0);
            R5900Context ctx{};
            PS2Runtime runtime;

            const uint64_t expectedImr = 0x1122334455667788ULL;
            setArgU32(ctx, 4, static_cast<uint32_t>(expectedImr));
            setArgU32(ctx, 5, static_cast<uint32_t>(expectedImr >> 32));
            ps2_syscalls::GsPutIMR(rdram.data(), &ctx, &runtime);
            t.Equals(getReturnS32(ctx), 0, "GsPutIMR should succeed");

            ps2_syscalls::GsGetIMR(rdram.data(), &ctx, &runtime);
            const uint64_t gotImr = static_cast<uint64_t>(getRegU32(&ctx, 2)) |
                                    (static_cast<uint64_t>(getRegU32(&ctx, 3)) << 32);
            t.Equals(gotImr, expectedImr, "GsGetIMR should return the value set by GsPutIMR");
        });

        tc.Run("OSD config syscalls round-trip stored value", [](TestCase &t) {
            std::vector<uint8_t> rdram(TEST_RAM_SIZE, 0);
            R5900Context ctx{};

            constexpr uint32_t paramAddr = 0x3A00;
            auto *param = reinterpret_cast<uint32_t *>(rdram.data() + paramAddr);
            *param = 0xCAFEBABE;

            setArgU32(ctx, 4, paramAddr);
            ps2_syscalls::SetOsdConfigParam(rdram.data(), &ctx, nullptr);
            t.Equals(getReturnS32(ctx), 0, "SetOsdConfigParam should succeed");

            *param = 0;
            setArgU32(ctx, 4, paramAddr);
            ps2_syscalls::GetOsdConfigParam(rdram.data(), &ctx, nullptr);
            t.Equals(getReturnS32(ctx), 0, "GetOsdConfigParam should succeed");
            t.Equals(*param, 0xCAFEBABEu, "GetOsdConfigParam should return value set by SetOsdConfigParam");
        });

        tc.Run("fio mode translation honors PS2 access bits", [](TestCase &t) {
            t.Equals(std::string(translateFioMode(PS2_FIO_O_RDONLY)), std::string("rb"),
                     "O_RDONLY should map to rb");
            t.Equals(std::string(translateFioMode(PS2_FIO_O_WRONLY | PS2_FIO_O_APPEND)), std::string("ab"),
                     "O_WRONLY|O_APPEND should map to ab");
            t.Equals(std::string(translateFioMode(PS2_FIO_O_RDWR)), std::string("r+b"),
                     "O_RDWR should map to r+b");
            t.Equals(std::string(translateFioMode(PS2_FIO_O_RDWR | PS2_FIO_O_APPEND)), std::string("a+b"),
                     "O_RDWR|O_APPEND should map to a+b");
            t.Equals(std::string(translateFioMode(PS2_FIO_O_WRONLY | PS2_FIO_O_CREAT)), std::string("r+b"),
                     "O_WRONLY|O_CREAT should map to r+b without forced truncation");
            t.Equals(std::string(translateFioMode(PS2_FIO_O_WRONLY | PS2_FIO_O_TRUNC)), std::string("wb"),
                     "O_WRONLY|O_TRUNC should map to wb");
            t.Equals(std::string(translateFioMode(PS2_FIO_O_RDWR | PS2_FIO_O_TRUNC)), std::string("w+b"),
                     "O_RDWR|O_TRUNC should map to w+b");
            t.Equals(std::string(translateFioMode(PS2_FIO_O_WRONLY | PS2_FIO_O_CREAT | PS2_FIO_O_TRUNC)), std::string("wb"),
                     "O_WRONLY|O_CREAT|O_TRUNC should map to wb");
        });

        tc.Run("fioOpen translates host and bare paths", [](TestCase &t) {
            namespace fs = std::filesystem;

            std::vector<uint8_t> rdram(TEST_RAM_SIZE, 0);
            R5900Context ctx{};

            const fs::path hostBase = fs::current_path() / "host_fs";
            fs::create_directories(hostBase);

            const std::string hostPrefixedName = uniqueTestName("runtime_host_prefix") + ".bin";
            const std::string bareName = uniqueTestName("runtime_bare_path") + ".bin";
            const std::string hostPrefixedPs2Path = "host:" + hostPrefixedName;
            const fs::path hostPrefixedFile = hostBase / hostPrefixedName;
            const fs::path bareFile = hostBase / bareName;

            {
                std::ofstream out(hostPrefixedFile, std::ios::binary);
                out << "prefix";
            }
            {
                std::ofstream out(bareFile, std::ios::binary);
                out << "bare";
            }

            constexpr uint32_t pathAddr = 0x3200;

            std::memset(rdram.data() + pathAddr, 0, 256);
            std::strcpy(reinterpret_cast<char *>(rdram.data() + pathAddr), hostPrefixedPs2Path.c_str());
            setArgU32(ctx, 4, pathAddr);
            setArgU32(ctx, 5, PS2_FIO_O_RDONLY);
            setArgU32(ctx, 6, 0);
            ps2_syscalls::fioOpen(rdram.data(), &ctx, nullptr);
            const int32_t fdHost = getReturnS32(ctx);
            t.IsTrue(fdHost > 0, "fioOpen should resolve host: paths");

            setArgU32(ctx, 4, static_cast<uint32_t>(fdHost));
            ps2_syscalls::fioClose(rdram.data(), &ctx, nullptr);
            t.Equals(getReturnS32(ctx), 0, "fioClose should close host-prefixed file");

            std::memset(rdram.data() + pathAddr, 0, 256);
            std::strcpy(reinterpret_cast<char *>(rdram.data() + pathAddr), bareName.c_str());
            setArgU32(ctx, 4, pathAddr);
            setArgU32(ctx, 5, PS2_FIO_O_RDONLY);
            setArgU32(ctx, 6, 0);
            ps2_syscalls::fioOpen(rdram.data(), &ctx, nullptr);
            const int32_t fdBare = getReturnS32(ctx);
            t.IsTrue(fdBare > 0, "fioOpen should resolve unprefixed paths to host_fs");

            setArgU32(ctx, 4, static_cast<uint32_t>(fdBare));
            ps2_syscalls::fioClose(rdram.data(), &ctx, nullptr);
            t.Equals(getReturnS32(ctx), 0, "fioClose should close bare-path file");

            std::memset(rdram.data() + pathAddr, 0, 256);
            std::strcpy(reinterpret_cast<char *>(rdram.data() + pathAddr), hostPrefixedPs2Path.c_str());
            setArgU32(ctx, 4, pathAddr);
            setArgU32(ctx, 5, PS2_FIO_O_WRONLY | PS2_FIO_O_CREAT | PS2_FIO_O_EXCL);
            setArgU32(ctx, 6, 0);
            ps2_syscalls::fioOpen(rdram.data(), &ctx, nullptr);
            t.Equals(getReturnS32(ctx), -1, "fioOpen should reject O_EXCL when file already exists");

            std::error_code ec;
            fs::remove(hostPrefixedFile, ec);
            fs::remove(bareFile, ec);
        });

        tc.Run("fioOpen supports cdrom and mc0 prefixes and blocks traversal", [](TestCase &t) {
            namespace fs = std::filesystem;

            std::vector<uint8_t> rdram(TEST_RAM_SIZE, 0);
            R5900Context ctx{};
            constexpr uint32_t pathAddr = 0x3550;

            const fs::path runtimeBase = fs::current_path();
            const fs::path cdBase = runtimeBase / "cd_fs";
            const fs::path mc0Base = runtimeBase / "mc0_fs";
            const std::string cdFileName = uniqueTestName("runtime_cd_prefix") + ".bin";
            const std::string mcFileName = uniqueTestName("runtime_mc_prefix") + ".bin";
            const std::string escapedName = uniqueTestName("runtime_cd_escape") + ".bin";
            const fs::path cdFile = cdBase / cdFileName;
            const fs::path mcFile = mc0Base / mcFileName;
            const fs::path escaped = runtimeBase / escapedName;
            fs::create_directories(cdBase);
            fs::create_directories(mc0Base);

            {
                std::ofstream out(cdFile, std::ios::binary | std::ios::trunc);
                out << "cd";
            }
            {
                std::ofstream out(mcFile, std::ios::binary | std::ios::trunc);
                out << "mc";
            }

            std::error_code ec;
            fs::remove(escaped, ec);

            auto setPath = [&](const char *path) {
                std::memset(rdram.data() + pathAddr, 0, 256);
                std::strcpy(reinterpret_cast<char *>(rdram.data() + pathAddr), path);
                setArgU32(ctx, 4, pathAddr);
            };

            setPath((std::string("cdrom0:") + cdFileName).c_str());
            setArgU32(ctx, 5, PS2_FIO_O_RDONLY);
            setArgU32(ctx, 6, 0);
            ps2_syscalls::fioOpen(rdram.data(), &ctx, nullptr);
            const int32_t cdFd = getReturnS32(ctx);
            t.IsTrue(cdFd > 0, "fioOpen should resolve cdrom0: prefix paths");

            setArgU32(ctx, 4, static_cast<uint32_t>(cdFd));
            ps2_syscalls::fioClose(rdram.data(), &ctx, nullptr);
            t.Equals(getReturnS32(ctx), 0, "fioClose should close cdrom0 file");

            setPath((std::string("mc0:") + mcFileName).c_str());
            setArgU32(ctx, 5, PS2_FIO_O_RDONLY);
            setArgU32(ctx, 6, 0);
            ps2_syscalls::fioOpen(rdram.data(), &ctx, nullptr);
            const int32_t mcFd = getReturnS32(ctx);
            t.IsTrue(mcFd > 0, "fioOpen should resolve mc0: prefix paths");

            setArgU32(ctx, 4, static_cast<uint32_t>(mcFd));
            ps2_syscalls::fioClose(rdram.data(), &ctx, nullptr);
            t.Equals(getReturnS32(ctx), 0, "fioClose should close mc0 file");

            setPath((std::string("cdrom:../") + escapedName).c_str());
            setArgU32(ctx, 5, PS2_FIO_O_WRONLY | PS2_FIO_O_CREAT | PS2_FIO_O_TRUNC);
            setArgU32(ctx, 6, 0);
            ps2_syscalls::fioOpen(rdram.data(), &ctx, nullptr);
            t.Equals(getReturnS32(ctx), -1, "fioOpen should reject cdrom traversal outside cd_fs root");
            t.IsTrue(!fs::exists(escaped), "cdrom traversal target should not be created outside cd_fs");

            fs::remove(cdFile, ec);
            fs::remove(mcFile, ec);
            fs::remove(escaped, ec);
        });

        tc.Run("fio directory and remove syscalls work on host mappings", [](TestCase &t) {
            namespace fs = std::filesystem;

            std::vector<uint8_t> rdram(TEST_RAM_SIZE, 0);
            R5900Context ctx{};
            constexpr uint32_t pathAddr = 0x3600;

            auto setPath = [&](const char *path) {
                std::memset(rdram.data() + pathAddr, 0, 256);
                std::strcpy(reinterpret_cast<char *>(rdram.data() + pathAddr), path);
                setArgU32(ctx, 4, pathAddr);
            };

            const fs::path hostBase = fs::current_path() / "host_fs";
            const std::string dirName = uniqueTestName("runtime_fs_dir");
            const fs::path dirPath = hostBase / dirName;
            const fs::path filePath = dirPath / "runtime_file.bin";
            const std::string hostDirPath = "host:" + dirName;
            const std::string hostFilePath = hostDirPath + "/runtime_file.bin";

            std::error_code ec;
            fs::remove(filePath, ec);
            fs::remove_all(dirPath, ec);

            setPath(hostDirPath.c_str());
            ps2_syscalls::fioMkdir(rdram.data(), &ctx, nullptr);
            t.Equals(getReturnS32(ctx), 0, "fioMkdir should create directory");

            setPath(hostDirPath.c_str());
            ps2_syscalls::fioChdir(rdram.data(), &ctx, nullptr);
            t.Equals(getReturnS32(ctx), 0, "fioChdir should succeed for existing mapped directory");

            {
                std::ofstream out(filePath, std::ios::binary);
                out << "data";
            }

            setPath(hostFilePath.c_str());
            ps2_syscalls::fioRemove(rdram.data(), &ctx, nullptr);
            t.Equals(getReturnS32(ctx), 0, "fioRemove should delete mapped file");

            setPath(hostDirPath.c_str());
            ps2_syscalls::fioRmdir(rdram.data(), &ctx, nullptr);
            t.Equals(getReturnS32(ctx), 0, "fioRmdir should delete empty mapped directory");

            setPath("host:");
            ps2_syscalls::fioChdir(rdram.data(), &ctx, nullptr);
            t.Equals(getReturnS32(ctx), 0, "fioChdir should reset emulated cwd back to host root");

            setPath(hostDirPath.c_str());
            ps2_syscalls::fioChdir(rdram.data(), &ctx, nullptr);
            t.Equals(getReturnS32(ctx), -1, "fioChdir should fail for deleted directory");

            fs::remove(filePath, ec);
            fs::remove_all(dirPath, ec);
        });

        tc.Run("fioRemove rejects directories and fioRmdir rejects files", [](TestCase &t) {
            namespace fs = std::filesystem;

            std::vector<uint8_t> rdram(TEST_RAM_SIZE, 0);
            R5900Context ctx{};
            constexpr uint32_t pathAddr = 0x3610;

            auto setPath = [&](const char *path) {
                std::memset(rdram.data() + pathAddr, 0, 256);
                std::strcpy(reinterpret_cast<char *>(rdram.data() + pathAddr), path);
                setArgU32(ctx, 4, pathAddr);
            };

            const fs::path hostBase = fs::current_path() / "host_fs";
            const std::string dirName = uniqueTestName("runtime_remove_dir");
            const std::string fileName = uniqueTestName("runtime_remove_file") + ".bin";
            const std::string hostDirPath = "host:" + dirName;
            const std::string hostFilePath = "host:" + fileName;
            const fs::path dirPath = hostBase / dirName;
            const fs::path filePath = hostBase / fileName;

            std::error_code ec;
            fs::remove(filePath, ec);
            fs::remove_all(dirPath, ec);
            fs::create_directories(dirPath, ec);
            {
                std::ofstream out(filePath, std::ios::binary | std::ios::trunc);
                out << "file";
            }

            setPath(hostDirPath.c_str());
            ps2_syscalls::fioRemove(rdram.data(), &ctx, nullptr);
            t.Equals(getReturnS32(ctx), -1, "fioRemove should fail when target is a directory");
            t.IsTrue(fs::is_directory(dirPath), "directory target should remain after failed fioRemove");

            setPath(hostFilePath.c_str());
            ps2_syscalls::fioRmdir(rdram.data(), &ctx, nullptr);
            t.Equals(getReturnS32(ctx), -1, "fioRmdir should fail when target is a regular file");
            t.IsTrue(fs::exists(filePath), "file target should remain after failed fioRmdir");

            fs::remove(filePath, ec);
            fs::remove_all(dirPath, ec);
        });

        tc.Run("fioChdir updates relative path resolution for bare paths", [](TestCase &t) {
            namespace fs = std::filesystem;

            std::vector<uint8_t> rdram(TEST_RAM_SIZE, 0);
            R5900Context ctx{};
            constexpr uint32_t pathAddr = 0x3650;

            auto setPath = [&](const char *path) {
                std::memset(rdram.data() + pathAddr, 0, 256);
                std::strcpy(reinterpret_cast<char *>(rdram.data() + pathAddr), path);
                setArgU32(ctx, 4, pathAddr);
            };

            const fs::path hostBase = fs::current_path() / "host_fs";
            const std::string cwdDirName = uniqueTestName("runtime_cwd_rel");
            const std::string relFileName = uniqueTestName("relative_target") + ".bin";
            const fs::path cwdDir = hostBase / cwdDirName;
            const fs::path cwdFile = cwdDir / relFileName;
            const std::string hostCwdPath = "host:" + cwdDirName;

            std::error_code ec;
            fs::create_directories(cwdDir, ec);
            {
                std::ofstream out(cwdFile, std::ios::binary | std::ios::trunc);
                out << "ok";
            }

            setPath(hostCwdPath.c_str());
            ps2_syscalls::fioChdir(rdram.data(), &ctx, nullptr);
            t.Equals(getReturnS32(ctx), 0, "fioChdir should set emulated cwd for host paths");

            setPath(relFileName.c_str());
            setArgU32(ctx, 5, PS2_FIO_O_RDONLY);
            setArgU32(ctx, 6, 0);
            ps2_syscalls::fioOpen(rdram.data(), &ctx, nullptr);
            const int32_t fd = getReturnS32(ctx);
            t.IsTrue(fd > 0, "fioOpen on bare path should resolve relative to emulated cwd");

            setArgU32(ctx, 4, static_cast<uint32_t>(fd));
            ps2_syscalls::fioClose(rdram.data(), &ctx, nullptr);
            t.Equals(getReturnS32(ctx), 0, "fioClose should close bare-path fd opened from emulated cwd");

            setPath("host:");
            ps2_syscalls::fioChdir(rdram.data(), &ctx, nullptr);
            t.Equals(getReturnS32(ctx), 0, "fioChdir should reset emulated cwd to host root");

            fs::remove(cwdFile, ec);
            fs::remove(cwdDir, ec);
        });

        tc.Run("fioOpen falls back to host root when emulated cwd is deleted", [](TestCase &t) {
            namespace fs = std::filesystem;

            std::vector<uint8_t> rdram(TEST_RAM_SIZE, 0);
            R5900Context ctx{};
            constexpr uint32_t pathAddr = 0x3660;

            auto setPath = [&](const char *path) {
                std::memset(rdram.data() + pathAddr, 0, 256);
                std::strcpy(reinterpret_cast<char *>(rdram.data() + pathAddr), path);
                setArgU32(ctx, 4, pathAddr);
            };

            const fs::path hostBase = fs::current_path() / "host_fs";
            const std::string cwdDirName = uniqueTestName("runtime_deleted_cwd");
            const std::string rootFileName = uniqueTestName("runtime_root_fallback") + ".bin";
            const fs::path cwdDir = hostBase / cwdDirName;
            const fs::path rootFile = hostBase / rootFileName;
            const std::string hostCwdPath = "host:" + cwdDirName;

            std::error_code ec;
            fs::create_directories(cwdDir, ec);
            {
                std::ofstream out(rootFile, std::ios::binary | std::ios::trunc);
                out << "fallback";
            }

            setPath(hostCwdPath.c_str());
            ps2_syscalls::fioChdir(rdram.data(), &ctx, nullptr);
            t.Equals(getReturnS32(ctx), 0, "fioChdir should succeed for existing directory");

            fs::remove_all(cwdDir, ec);

            setPath(rootFileName.c_str());
            setArgU32(ctx, 5, PS2_FIO_O_RDONLY);
            setArgU32(ctx, 6, 0);
            ps2_syscalls::fioOpen(rdram.data(), &ctx, nullptr);
            const int32_t fd = getReturnS32(ctx);
            t.IsTrue(fd > 0, "fioOpen should resolve relative bare path from host root when cwd was removed");

            if (fd > 0)
            {
                setArgU32(ctx, 4, static_cast<uint32_t>(fd));
                ps2_syscalls::fioClose(rdram.data(), &ctx, nullptr);
                t.Equals(getReturnS32(ctx), 0, "fioClose should close fallback-opened descriptor");
            }

            setPath("host:");
            ps2_syscalls::fioChdir(rdram.data(), &ctx, nullptr);
            t.Equals(getReturnS32(ctx), 0, "fioChdir should keep host root usable after cwd fallback");

            fs::remove(rootFile, ec);
            fs::remove_all(cwdDir, ec);
        });

        tc.Run("fioOpen falls back to host root when emulated cwd becomes a file", [](TestCase &t) {
            namespace fs = std::filesystem;

            std::vector<uint8_t> rdram(TEST_RAM_SIZE, 0);
            R5900Context ctx{};
            constexpr uint32_t pathAddr = 0x3670;

            auto setPath = [&](const char *path) {
                std::memset(rdram.data() + pathAddr, 0, 256);
                std::strcpy(reinterpret_cast<char *>(rdram.data() + pathAddr), path);
                setArgU32(ctx, 4, pathAddr);
            };

            const fs::path hostBase = fs::current_path() / "host_fs";
            const std::string cwdName = uniqueTestName("runtime_file_cwd");
            const std::string rootFileName = uniqueTestName("runtime_root_file_fallback") + ".bin";
            const fs::path cwdPath = hostBase / cwdName;
            const fs::path rootFile = hostBase / rootFileName;
            const std::string hostCwdPath = "host:" + cwdName;

            std::error_code ec;
            fs::create_directories(cwdPath, ec);
            {
                std::ofstream out(rootFile, std::ios::binary | std::ios::trunc);
                out << "fallback";
            }

            setPath(hostCwdPath.c_str());
            ps2_syscalls::fioChdir(rdram.data(), &ctx, nullptr);
            t.Equals(getReturnS32(ctx), 0, "fioChdir should succeed for directory before replacement");

            fs::remove_all(cwdPath, ec);
            {
                std::ofstream out(cwdPath, std::ios::binary | std::ios::trunc);
                out << "not-a-directory";
            }

            setPath(rootFileName.c_str());
            setArgU32(ctx, 5, PS2_FIO_O_RDONLY);
            setArgU32(ctx, 6, 0);
            ps2_syscalls::fioOpen(rdram.data(), &ctx, nullptr);
            const int32_t fd = getReturnS32(ctx);
            t.IsTrue(fd > 0, "fioOpen should resolve relative bare path from host root when cwd is a file");

            if (fd > 0)
            {
                setArgU32(ctx, 4, static_cast<uint32_t>(fd));
                ps2_syscalls::fioClose(rdram.data(), &ctx, nullptr);
                t.Equals(getReturnS32(ctx), 0, "fioClose should close descriptor opened via file-cwd fallback");
            }

            setPath("host:");
            ps2_syscalls::fioChdir(rdram.data(), &ctx, nullptr);
            t.Equals(getReturnS32(ctx), 0, "fioChdir should keep host root usable after file-cwd fallback");

            fs::remove(rootFile, ec);
            fs::remove(cwdPath, ec);
            fs::remove_all(cwdPath, ec);
        });

        tc.Run("fioOpen create without trunc preserves existing data and creates missing file", [](TestCase &t) {
            namespace fs = std::filesystem;

            std::vector<uint8_t> rdram(TEST_RAM_SIZE, 0);
            R5900Context ctx{};
            constexpr uint32_t pathAddr = 0x3700;

            const fs::path hostBase = fs::current_path() / "host_fs";
            fs::create_directories(hostBase);

            const std::string existingName = uniqueTestName("runtime_create_keep") + ".bin";
            const std::string newName = uniqueTestName("runtime_create_new") + ".bin";
            const std::string existingPs2Path = "host:" + existingName;
            const std::string newPs2Path = "host:" + newName;
            const fs::path existingFile = hostBase / existingName;
            const fs::path newFile = hostBase / newName;

            {
                std::ofstream out(existingFile, std::ios::binary | std::ios::trunc);
                out << "keep";
            }
            std::error_code ec;
            fs::remove(newFile, ec);

            auto setPath = [&](const char *path) {
                std::memset(rdram.data() + pathAddr, 0, 256);
                std::strcpy(reinterpret_cast<char *>(rdram.data() + pathAddr), path);
                setArgU32(ctx, 4, pathAddr);
            };

            setPath(existingPs2Path.c_str());
            setArgU32(ctx, 5, PS2_FIO_O_WRONLY | PS2_FIO_O_CREAT);
            setArgU32(ctx, 6, 0);
            ps2_syscalls::fioOpen(rdram.data(), &ctx, nullptr);
            const int32_t fdExisting = getReturnS32(ctx);
            t.IsTrue(fdExisting > 0, "fioOpen should open existing file with O_CREAT without truncating");

            setArgU32(ctx, 4, static_cast<uint32_t>(fdExisting));
            ps2_syscalls::fioClose(rdram.data(), &ctx, nullptr);
            t.Equals(getReturnS32(ctx), 0, "fioClose should close existing file");
            t.Equals(fs::file_size(existingFile), static_cast<uintmax_t>(4), "existing file size should be preserved");

            setPath(newPs2Path.c_str());
            setArgU32(ctx, 5, PS2_FIO_O_WRONLY | PS2_FIO_O_CREAT);
            setArgU32(ctx, 6, 0);
            ps2_syscalls::fioOpen(rdram.data(), &ctx, nullptr);
            const int32_t fdNew = getReturnS32(ctx);
            t.IsTrue(fdNew > 0, "fioOpen should create a missing file with O_CREAT");

            setArgU32(ctx, 4, static_cast<uint32_t>(fdNew));
            ps2_syscalls::fioClose(rdram.data(), &ctx, nullptr);
            t.Equals(getReturnS32(ctx), 0, "fioClose should close newly created file");
            t.IsTrue(fs::exists(newFile), "new file should exist after O_CREAT open");

            fs::remove(existingFile, ec);
            fs::remove(newFile, ec);
        });

        tc.Run("fioOpen truncates existing files and rejects truncation on missing file without create", [](TestCase &t) {
            namespace fs = std::filesystem;

            std::vector<uint8_t> rdram(TEST_RAM_SIZE, 0);
            R5900Context ctx{};
            constexpr uint32_t pathAddr = 0x3D00;

            const fs::path hostBase = fs::current_path() / "host_fs";
            fs::create_directories(hostBase);
            const std::string truncName = uniqueTestName("runtime_trunc_existing") + ".bin";
            const std::string missingName = uniqueTestName("runtime_trunc_missing") + ".bin";
            const std::string truncPs2Path = "host:" + truncName;
            const std::string missingPs2Path = "host:" + missingName;
            const fs::path truncFile = hostBase / truncName;
            const fs::path missingFile = hostBase / missingName;

            std::error_code ec;
            fs::remove(missingFile, ec);

            auto setPath = [&](const char *path) {
                std::memset(rdram.data() + pathAddr, 0, 256);
                std::strcpy(reinterpret_cast<char *>(rdram.data() + pathAddr), path);
                setArgU32(ctx, 4, pathAddr);
            };

            {
                std::ofstream out(truncFile, std::ios::binary | std::ios::trunc);
                out << "abcdef";
            }

            setPath(truncPs2Path.c_str());
            setArgU32(ctx, 5, PS2_FIO_O_WRONLY | PS2_FIO_O_TRUNC);
            setArgU32(ctx, 6, 0);
            ps2_syscalls::fioOpen(rdram.data(), &ctx, nullptr);
            const int32_t truncWrFd = getReturnS32(ctx);
            t.IsTrue(truncWrFd > 0, "fioOpen should truncate existing file for O_WRONLY|O_TRUNC");

            setArgU32(ctx, 4, static_cast<uint32_t>(truncWrFd));
            ps2_syscalls::fioClose(rdram.data(), &ctx, nullptr);
            t.Equals(getReturnS32(ctx), 0, "fioClose should close O_WRONLY|O_TRUNC fd");
            t.Equals(fs::file_size(truncFile), static_cast<uintmax_t>(0),
                     "O_WRONLY|O_TRUNC should truncate existing file to zero bytes");

            {
                std::ofstream out(truncFile, std::ios::binary | std::ios::trunc);
                out << "xyz";
            }

            setPath(truncPs2Path.c_str());
            setArgU32(ctx, 5, PS2_FIO_O_RDWR | PS2_FIO_O_TRUNC);
            setArgU32(ctx, 6, 0);
            ps2_syscalls::fioOpen(rdram.data(), &ctx, nullptr);
            const int32_t truncRdwrFd = getReturnS32(ctx);
            t.IsTrue(truncRdwrFd > 0, "fioOpen should truncate existing file for O_RDWR|O_TRUNC");

            setArgU32(ctx, 4, static_cast<uint32_t>(truncRdwrFd));
            ps2_syscalls::fioClose(rdram.data(), &ctx, nullptr);
            t.Equals(getReturnS32(ctx), 0, "fioClose should close O_RDWR|O_TRUNC fd");
            t.Equals(fs::file_size(truncFile), static_cast<uintmax_t>(0),
                     "O_RDWR|O_TRUNC should truncate existing file to zero bytes");

            setPath(missingPs2Path.c_str());
            setArgU32(ctx, 5, PS2_FIO_O_WRONLY | PS2_FIO_O_TRUNC);
            setArgU32(ctx, 6, 0);
            ps2_syscalls::fioOpen(rdram.data(), &ctx, nullptr);
            t.Equals(getReturnS32(ctx), -1,
                     "fioOpen should fail O_TRUNC without O_CREAT when target does not exist");
            t.IsTrue(!fs::exists(missingFile), "missing O_TRUNC target should remain absent");

            fs::remove(truncFile, ec);
            fs::remove(missingFile, ec);
        });

        tc.Run("fioOpen append mode keeps writes at file end", [](TestCase &t) {
            namespace fs = std::filesystem;

            std::vector<uint8_t> rdram(TEST_RAM_SIZE, 0);
            R5900Context ctx{};
            constexpr uint32_t pathAddr = 0x3E00;
            constexpr uint32_t writeBufAddr = 0x3F00;

            const fs::path hostBase = fs::current_path() / "host_fs";
            fs::create_directories(hostBase);
            const std::string appendName = uniqueTestName("runtime_append_mode") + ".bin";
            const std::string appendPs2Path = "host:" + appendName;
            const fs::path appendFile = hostBase / appendName;

            {
                std::ofstream out(appendFile, std::ios::binary | std::ios::trunc);
                out << "base";
            }

            std::memset(rdram.data() + pathAddr, 0, 256);
            std::strcpy(reinterpret_cast<char *>(rdram.data() + pathAddr), appendPs2Path.c_str());
            setArgU32(ctx, 4, pathAddr);
            setArgU32(ctx, 5, PS2_FIO_O_RDWR | PS2_FIO_O_APPEND);
            setArgU32(ctx, 6, 0);
            ps2_syscalls::fioOpen(rdram.data(), &ctx, nullptr);
            const int32_t fd = getReturnS32(ctx);
            t.IsTrue(fd > 0, "fioOpen should open existing file with O_RDWR|O_APPEND");

            std::memcpy(rdram.data() + writeBufAddr, "X", 1);
            setArgU32(ctx, 4, static_cast<uint32_t>(fd));
            setArgU32(ctx, 5, writeBufAddr);
            setArgU32(ctx, 6, 1);
            ps2_syscalls::fioWrite(rdram.data(), &ctx, nullptr);
            t.Equals(getReturnS32(ctx), 1, "fioWrite should report one appended byte");

            setArgU32(ctx, 4, static_cast<uint32_t>(fd));
            ps2_syscalls::fioClose(rdram.data(), &ctx, nullptr);
            t.Equals(getReturnS32(ctx), 0, "fioClose should close append-mode fd");

            std::ifstream in(appendFile, std::ios::binary);
            std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            t.Equals(content, std::string("baseX"), "append-mode writes should be placed at end of file");

            std::error_code ec;
            fs::remove(appendFile, ec);
        });

        tc.Run("fioLseek supports cur/end seeks and rejects invalid range or whence", [](TestCase &t) {
            namespace fs = std::filesystem;

            std::vector<uint8_t> rdram(TEST_RAM_SIZE, 0);
            R5900Context ctx{};
            constexpr uint32_t pathAddr = 0x3E80;

            const fs::path hostBase = fs::current_path() / "host_fs";
            fs::create_directories(hostBase);
            const std::string seekName = uniqueTestName("runtime_lseek") + ".bin";
            const std::string seekPs2Path = "host:" + seekName;
            const fs::path seekFile = hostBase / seekName;
            {
                std::ofstream out(seekFile, std::ios::binary | std::ios::trunc);
                out << "0123456789";
            }

            std::memset(rdram.data() + pathAddr, 0, 256);
            std::strcpy(reinterpret_cast<char *>(rdram.data() + pathAddr), seekPs2Path.c_str());
            setArgU32(ctx, 4, pathAddr);
            setArgU32(ctx, 5, PS2_FIO_O_RDWR);
            setArgU32(ctx, 6, 0);
            ps2_syscalls::fioOpen(rdram.data(), &ctx, nullptr);
            const int32_t fd = getReturnS32(ctx);
            t.IsTrue(fd > 0, "fioOpen should open lseek test file");

            auto doSeek = [&](int32_t off, uint32_t whence) {
                setArgU32(ctx, 4, static_cast<uint32_t>(fd));
                setArgU32(ctx, 5, static_cast<uint32_t>(off));
                setArgU32(ctx, 6, whence);
                ps2_syscalls::fioLseek(rdram.data(), &ctx, nullptr);
                return getReturnS32(ctx);
            };

            t.Equals(doSeek(5, PS2_FIO_SEEK_SET), 5, "SEEK_SET should move to absolute offset");
            t.Equals(doSeek(-2, PS2_FIO_SEEK_CUR), 3, "SEEK_CUR should support signed negative offsets");
            t.Equals(doSeek(-1, PS2_FIO_SEEK_END), 9, "SEEK_END should seek relative to file end");
            t.Equals(doSeek(-1, PS2_FIO_SEEK_SET), -1, "SEEK_SET should reject negative target offset");
            t.Equals(doSeek(0, 99), -1, "invalid whence should fail");

            const int32_t maxS32 = std::numeric_limits<int32_t>::max();
            t.Equals(doSeek(maxS32, PS2_FIO_SEEK_SET), maxS32,
                     "SEEK_SET should allow the maximum signed 32-bit offset");
            t.Equals(doSeek(1, PS2_FIO_SEEK_CUR), -1,
                     "SEEK_CUR should fail when resulting position exceeds signed 32-bit range");
            t.Equals(doSeek(0, PS2_FIO_SEEK_CUR), maxS32,
                     "failed overflow seek should not alter file position");

            setArgU32(ctx, 4, static_cast<uint32_t>(fd));
            ps2_syscalls::fioClose(rdram.data(), &ctx, nullptr);
            t.Equals(getReturnS32(ctx), 0, "fioClose should close lseek test file");

            std::error_code ec;
            fs::remove(seekFile, ec);
        });

        tc.Run("fioOpen rejects path traversal outside host root", [](TestCase &t) {
            namespace fs = std::filesystem;

            std::vector<uint8_t> rdram(TEST_RAM_SIZE, 0);
            R5900Context ctx{};
            constexpr uint32_t pathAddr = 0x3C00;

            const fs::path runtimeBase = fs::current_path();
            const fs::path hostBase = runtimeBase / "host_fs";
            const std::string cwdDirName = uniqueTestName("runtime_escape_cwd");
            const std::string escapedPrefixedName = uniqueTestName("runtime_escape_prefixed") + ".bin";
            const std::string escapedBareName = uniqueTestName("runtime_escape_bare") + ".bin";
            const fs::path cwdDir = hostBase / cwdDirName;
            const fs::path escapedPrefixed = runtimeBase / escapedPrefixedName;
            const fs::path escapedBare = runtimeBase / escapedBareName;
            fs::create_directories(cwdDir);

            std::error_code ec;
            fs::remove(escapedPrefixed, ec);
            fs::remove(escapedBare, ec);

            auto setPath = [&](const char *path) {
                std::memset(rdram.data() + pathAddr, 0, 256);
                std::strcpy(reinterpret_cast<char *>(rdram.data() + pathAddr), path);
                setArgU32(ctx, 4, pathAddr);
            };

            setPath((std::string("host:../") + escapedPrefixedName).c_str());
            setArgU32(ctx, 5, PS2_FIO_O_WRONLY | PS2_FIO_O_CREAT | PS2_FIO_O_TRUNC);
            setArgU32(ctx, 6, 0);
            ps2_syscalls::fioOpen(rdram.data(), &ctx, nullptr);
            t.Equals(getReturnS32(ctx), -1, "fioOpen should reject host: paths that escape host root");

            setPath((std::string("host:") + cwdDirName).c_str());
            ps2_syscalls::fioChdir(rdram.data(), &ctx, nullptr);
            t.Equals(getReturnS32(ctx), 0, "fioChdir should succeed for host subdir used in traversal test");

            setPath((std::string("../../") + escapedBareName).c_str());
            setArgU32(ctx, 5, PS2_FIO_O_WRONLY | PS2_FIO_O_CREAT | PS2_FIO_O_TRUNC);
            setArgU32(ctx, 6, 0);
            ps2_syscalls::fioOpen(rdram.data(), &ctx, nullptr);
            t.Equals(getReturnS32(ctx), -1, "fioOpen should reject bare paths that escape emulated host cwd root");

            setPath("host:");
            ps2_syscalls::fioChdir(rdram.data(), &ctx, nullptr);
            t.Equals(getReturnS32(ctx), 0, "fioChdir should restore emulated cwd to host root");

            t.IsTrue(!fs::exists(escapedPrefixed), "prefixed traversal target should not be created");
            t.IsTrue(!fs::exists(escapedBare), "bare traversal target should not be created");

            fs::remove_all(cwdDir, ec);
            fs::remove(escapedPrefixed, ec);
            fs::remove(escapedBare, ec);
        });

        tc.Run("fioGetstat reports file size", [](TestCase &t) {
            namespace fs = std::filesystem;

            std::vector<uint8_t> rdram(TEST_RAM_SIZE, 0);
            R5900Context ctx{};

            constexpr uint32_t pathAddr = 0x3800;
            constexpr uint32_t statAddr = 0x3900;

            const fs::path hostBase = fs::current_path() / "host_fs";
            fs::create_directories(hostBase);
            const std::string statFileName = uniqueTestName("runtime_stat_file") + ".bin";
            const std::string statPs2Path = "host:" + statFileName;
            const fs::path statFile = hostBase / statFileName;

            {
                std::ofstream out(statFile, std::ios::binary | std::ios::trunc);
                out << "1234567";
            }

            std::memset(rdram.data() + pathAddr, 0, 256);
            std::strcpy(reinterpret_cast<char *>(rdram.data() + pathAddr), statPs2Path.c_str());
            setArgU32(ctx, 4, pathAddr);
            setArgU32(ctx, 5, statAddr);
            ps2_syscalls::fioGetstat(rdram.data(), &ctx, nullptr);
            t.Equals(getReturnS32(ctx), 0, "fioGetstat should succeed for existing file");

            auto *fields = reinterpret_cast<uint32_t *>(rdram.data() + statAddr);
            t.Equals(fields[2], 7u, "fioGetstat lower size field should match file size");
            t.Equals(fields[9], 0u, "fioGetstat high size field should be zero for small files");

            std::error_code ec;
            fs::remove(statFile, ec);
        });

        tc.Run("fioGetstat reports directory mode and fails for missing path", [](TestCase &t) {
            namespace fs = std::filesystem;

            std::vector<uint8_t> rdram(TEST_RAM_SIZE, 0);
            R5900Context ctx{};

            constexpr uint32_t pathAddr = 0x3A00;
            constexpr uint32_t statAddr = 0x3B00;

            const fs::path hostBase = fs::current_path() / "host_fs";
            const std::string dirName = uniqueTestName("runtime_stat_dir");
            const std::string missingName = uniqueTestName("runtime_stat_missing");
            const std::string dirPs2Path = "host:" + dirName;
            const std::string missingPs2Path = "host:" + missingName;
            const fs::path dirPath = hostBase / dirName;
            fs::create_directories(dirPath);

            std::memset(rdram.data() + statAddr, 0xCD, 64);
            std::memset(rdram.data() + pathAddr, 0, 256);
            std::strcpy(reinterpret_cast<char *>(rdram.data() + pathAddr), dirPs2Path.c_str());
            setArgU32(ctx, 4, pathAddr);
            setArgU32(ctx, 5, statAddr);
            ps2_syscalls::fioGetstat(rdram.data(), &ctx, nullptr);
            t.Equals(getReturnS32(ctx), 0, "fioGetstat should succeed for existing directory");

            auto *fields = reinterpret_cast<uint32_t *>(rdram.data() + statAddr);
            t.Equals(fields[0], 0x4000u, "fioGetstat mode should identify directory entries");
            t.Equals(fields[2], 0u, "fioGetstat lower size field should be zero for directories");
            t.Equals(fields[9], 0u, "fioGetstat high size field should be zero for directories");

            std::memset(rdram.data() + statAddr, 0xAB, 64);
            std::memset(rdram.data() + pathAddr, 0, 256);
            std::strcpy(reinterpret_cast<char *>(rdram.data() + pathAddr), missingPs2Path.c_str());
            setArgU32(ctx, 4, pathAddr);
            setArgU32(ctx, 5, statAddr);
            ps2_syscalls::fioGetstat(rdram.data(), &ctx, nullptr);
            t.Equals(getReturnS32(ctx), -1, "fioGetstat should fail for missing paths");

            std::error_code ec;
            fs::remove_all(dirPath, ec);
        });

        tc.Run("memory alignment exceptions report hexadecimal addresses", [](TestCase &t) {
            PS2Memory memory;
            t.IsTrue(memory.initialize(), "PS2Memory initialization should succeed");

            try
            {
                memory.read32(0x123);
                t.Fail("read32 on unaligned address should throw");
            }
            catch (const std::runtime_error &err)
            {
                const std::string message = err.what();
                t.IsTrue(message.find("0x123") != std::string::npos,
                         "exception message should preserve hexadecimal address formatting");
            }
        });
    });
}

#else

void register_runtime_syscall_tests()
{
}

#endif
