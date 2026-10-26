#include "MiniTest.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "ps2_iop_transport.h"
#include "ps2_syscalls.h"
#include "ps2_stubs.h"
#include "runtime/ee_scheduler.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

using namespace ps2_syscalls;

namespace ps2_stubs
{
    void resetSifState();
    void sceSifAddCmdHandler(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
}

namespace
{
    constexpr int KE_OK = 0;
    constexpr int KE_SEMA_ZERO = -419;

    constexpr uint32_t K_SIF_RPC_MODE_NOWAIT = 0x01u;
    constexpr uint32_t K_STACK_ADDR = 0x00100000u;
    constexpr uint32_t IOP_SID_MCSERV = 0x80000400u;
    constexpr uint32_t IOP_SID_LIBSD = 0x80000701u;

    #pragma pack(push, 1)
    struct SifRpcHeader
    {
        uint32_t pkt_addr;
        uint32_t rpc_id;
        int32_t sema_id;
        uint32_t mode;
    };

    struct SifRpcClientData
    {
        SifRpcHeader hdr;
        uint32_t command;
        uint32_t buf;
        uint32_t cbuf;
        uint32_t end_function;
        uint32_t end_param;
        uint32_t server;
    };

    struct SifRpcServerData
    {
        int32_t sid;
        uint32_t func;
        uint32_t buf;
        int32_t size;
        uint32_t cfunc;
        uint32_t cbuf;
        int32_t size2;
        uint32_t client;
        uint32_t pkt_addr;
        int32_t rpc_number;
        uint32_t recvbuf;
        int32_t rsize;
        int32_t rmode;
        int32_t rid;
        uint32_t link;
        uint32_t next;
        uint32_t base;
    };

    struct SifRpcDataQueue
    {
        int32_t thread_id;
        int32_t active;
        uint32_t link;
        uint32_t start;
        uint32_t end;
        uint32_t next;
    };

    struct McDescParam
    {
        int32_t fd;
        int32_t port;
        int32_t slot;
        int32_t size;
        int32_t offset;
        int32_t origin;
        uint32_t buffer;
        uint32_t param;
        uint8_t data[16];
    };
    #pragma pack(pop)

    static_assert(sizeof(SifRpcHeader) == 0x10u, "Unexpected SifRpcHeader size.");
    static_assert(sizeof(SifRpcClientData) == 0x28u, "Unexpected SifRpcClientData size.");
    static_assert(sizeof(SifRpcServerData) == 0x44u, "Unexpected SifRpcServerData size.");
    static_assert(sizeof(SifRpcDataQueue) == 0x18u, "Unexpected SifRpcDataQueue size.");
    static_assert(sizeof(McDescParam) == 0x30u, "Unexpected McDescParam size.");

    struct TestEnv
    {
        std::vector<uint8_t> rdram;
        R5900Context ctx{};
        PS2Runtime runtime;

        TestEnv() : rdram(PS2_RAM_SIZE, 0)
        {
            ps2_stubs::resetSifState();
            std::memset(&ctx, 0, sizeof(ctx));
        }
    };

    ps2x::iop::RpcResult callIop(TestEnv &env,
                                 uint32_t sid,
                                 uint32_t function,
                                 uint32_t sendAddress,
                                 uint32_t sendSize,
                                 uint32_t receiveAddress,
                                 uint32_t receiveSize)
    {
        ps2x::iop::RpcRequest request{};
        request.sid = sid;
        request.function = function;
        request.send = {sendAddress, sendSize};
        request.receive = {receiveAddress, receiveSize};
        return PS2IopTransport::handleRpc(
            &env.runtime, env.rdram.data(), &env.ctx, std::move(request));
    }

    void setRegU32(R5900Context &ctx, int reg, uint32_t value)
    {
        ctx.r[reg] = _mm_set_epi64x(0, static_cast<int64_t>(value));
    }

    int32_t getRegS32(const R5900Context &ctx, int reg)
    {
        return static_cast<int32_t>(::getRegU32(&ctx, reg));
    }

    uint32_t getRegU32Result(const R5900Context &ctx, int reg)
    {
        return ::getRegU32(&ctx, reg);
    }

    void writeGuestU32(uint8_t *rdram, uint32_t addr, uint32_t value)
    {
        std::memcpy(rdram + addr, &value, sizeof(value));
    }

    // Capture area + fake cid-1 completion handler used by the loader
    // queue test below.
    constexpr uint32_t K_CID1REPLY_WAIT_PC = 0x00160700u;
    constexpr uint32_t K_CID1REPLY_RESUME_PC = 0x00160710u;
    constexpr uint32_t K_CID1REPLY_HANDLER_PC = 0x00160720u;
    constexpr uint32_t K_CID1REPLY_REPLY_CAPTURE = 0x00026800u;
    int g_cid1ReplySemaphoreId = 0;
    uint32_t g_cid1ReplyReplyHandle = 0;
    uint32_t g_cid1ReplyReplyStatus = 0;
    uint32_t g_cid1ReplyHandlerArg = 0;
    uint32_t g_cid1ReplyHandlerRuns = 0;

    void cid1ReplyWaitMain(uint8_t *, R5900Context *ctx, PS2Runtime *runtime)
    {
        EeScheduler &scheduler = runtime->eeScheduler();
        g_cid1ReplySemaphoreId = scheduler.createSemaphore(0, 1, 0u, 0u);
        ctx->pc = K_CID1REPLY_RESUME_PC;
        scheduler.waitSemaphore(g_cid1ReplySemaphoreId);
    }

    void cid1ReplyResumeMain(uint8_t *, R5900Context *ctx, PS2Runtime *runtime)
    {
        ctx->pc = 0u;
        runtime->requestStop();
    }

    void cid1ReplyFakeCompletionHandler(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t packetAddress = ::getRegU32(ctx, 4);
        g_cid1ReplyHandlerArg = ::getRegU32(ctx, 5);
        if (packetAddress != 0u)
        {
            std::memcpy(&g_cid1ReplyReplyHandle, rdram + packetAddress + 0x0Cu, sizeof(uint32_t));
            std::memcpy(&g_cid1ReplyReplyStatus, rdram + packetAddress + 0x10u, sizeof(uint32_t));
        }
        writeGuestU32(rdram, K_CID1REPLY_REPLY_CAPTURE, g_cid1ReplyReplyHandle);
        ++g_cid1ReplyHandlerRuns;
        ctx->pc = 0u;
        runtime->eeScheduler().signalSemaphore(g_cid1ReplySemaphoreId, true);
    }

    // Runs the scheduler on a worker thread with a hard watchdog so a missing
    // completion fails the test instead of hanging the suite.
    bool cid1ReplyRunSchedulerWithWatchdog(TestEnv &env)
    {
        std::atomic<bool> finished{false};
        std::thread runner([&env, &finished]()
                           {
                               env.runtime.eeScheduler().run();
                               finished.store(true, std::memory_order_release);
                           });
        for (int i = 0; i < 300 && !finished.load(std::memory_order_acquire); ++i)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if (!finished.load(std::memory_order_acquire))
        {
            env.runtime.requestStop();
        }
        runner.join();
        return finished.load(std::memory_order_acquire);
    }

    struct ScopedTempDir
    {
        std::filesystem::path path;

        explicit ScopedTempDir(const std::string &name)
        {
            path = std::filesystem::temp_directory_path() /
                   ("ps2recomp_" + name + "_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
            std::filesystem::remove_all(path);
            std::filesystem::create_directories(path);
        }

        ~ScopedTempDir()
        {
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
        }
    };

    template <typename T>
    void writeGuestStruct(uint8_t *rdram, uint32_t addr, const T &value)
    {
        std::memcpy(rdram + addr, &value, sizeof(value));
    }

    template <typename T>
    T readGuestStruct(const uint8_t *rdram, uint32_t addr)
    {
        T value{};
        std::memcpy(&value, rdram + addr, sizeof(value));
        return value;
    }
}

void register_ps2_sif_rpc_tests()
{
    MiniTest::Case("PS2SifRpc", [](TestCase &tc)
    {
        tc.Run("SifInitRpc does not reset the running IOP", [](TestCase &t)
        {
            TestEnv env;

            env.runtime.eeScheduler().accountCycles(80u);
            const uint64_t cyclesBeforeInit = env.runtime.iopDebugSnapshot().emulatorCycles;
            t.IsTrue(cyclesBeforeInit != 0u, "IOP cycle counter should advance before RPC initialization");

            SifInitRpc(env.rdram.data(), &env.ctx, &env.runtime);

            t.Equals(env.runtime.iopDebugSnapshot().emulatorCycles, cyclesBeforeInit,
                     "SifInitRpc must not reboot or reset the IOP");
        });

        tc.Run("SifLoadModule validates ROM modules and activates their HLE service", [](TestCase &t)
        {
            TestEnv env;
            constexpr uint32_t kPathAddress = 0x00021000u;

            const auto load = [&](std::string_view path)
            {
                std::memcpy(env.rdram.data() + kPathAddress, path.data(), path.size());
                env.rdram[kPathAddress + path.size()] = 0u;
                setRegU32(env.ctx, 4, kPathAddress);
                setRegU32(env.ctx, 5, 0u);
                setRegU32(env.ctx, 6, 0u);
                SifLoadModule(env.rdram.data(), &env.ctx, &env.runtime);
                return getRegS32(env.ctx, 2);
            };

            t.Equals(load("rom0:NOT_A_REAL_MODULE"), -1,
                     "SifLoadModule must reject unknown ROM modules instead of fabricating success");

            const int32_t libsdId = load("rom0:LIBSD");
            t.IsTrue(libsdId > 0, "registered no-BIOS ROM module should receive a real managed ID");

            const auto snapshot = env.runtime.iopDebugSnapshot();
            bool libsdActive = false;
            for (const auto &service : snapshot.services)
            {
                if (service.name == "libsd")
                {
                    libsdActive = service.active;
                    break;
                }
            }
            t.IsTrue(libsdActive, "loading LIBSD should activate its HLE RPC route");
        });

        tc.Run("emulated RPC bind waits for a registered IOP server", [](TestCase &t)
        {
            TestEnv env;

            constexpr uint32_t kClientAddr = 0x00021F00u;
            constexpr uint32_t kUnregisteredSid = 0x13572468u;

            SifInitRpc(env.rdram.data(), &env.ctx, &env.runtime);
            setRegU32(env.ctx, 4, kClientAddr);
            setRegU32(env.ctx, 5, kUnregisteredSid);
            setRegU32(env.ctx, 6, 0u);
            SifBindRpc(env.rdram.data(), &env.ctx, &env.runtime);

            t.Equals(getRegS32(env.ctx, 2), KE_OK, "SifBindRpc transport should complete");
            const SifRpcClientData client = readGuestStruct<SifRpcClientData>(env.rdram.data(), kClientAddr);
            t.Equals(client.server, 0u,
                     "client server pointer must stay null until the emulated IRX registers its SID");
        });

        tc.Run("register bind call updates descriptors and payload", [](TestCase &t)
        {
            TestEnv env;

            constexpr uint32_t kQdAddr = 0x00022000u;
            constexpr uint32_t kSdAddr = 0x00022100u;
            constexpr uint32_t kClientAddr = 0x00022200u;
            constexpr uint32_t kServerBufAddr = 0x00022300u;
            constexpr uint32_t kClientCbufAddr = 0x00022400u;
            constexpr uint32_t kSendAddr = 0x00022500u;
            constexpr uint32_t kRecvAddr = 0x00022600u;
            constexpr uint32_t kSid = 0x20000111u;

            SifInitRpc(env.rdram.data(), &env.ctx, &env.runtime);

            setRegU32(env.ctx, 4, kQdAddr);
            setRegU32(env.ctx, 5, 0x33u);
            SifSetRpcQueue(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), KE_OK, "SifSetRpcQueue should succeed");

            setRegU32(env.ctx, 29, K_STACK_ADDR);
            writeGuestU32(env.rdram.data(), K_STACK_ADDR + 0x10u, 0x9000u);          // cfunc
            writeGuestU32(env.rdram.data(), K_STACK_ADDR + 0x14u, kClientCbufAddr);  // cbuf
            writeGuestU32(env.rdram.data(), K_STACK_ADDR + 0x18u, kQdAddr);          // qd

            setRegU32(env.ctx, 4, kSdAddr);
            setRegU32(env.ctx, 5, kSid);
            setRegU32(env.ctx, 6, 0u); // no server callback
            setRegU32(env.ctx, 7, kServerBufAddr);
            SifRegisterRpc(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), KE_OK, "SifRegisterRpc should succeed");

            const SifRpcDataQueue qdAfterRegister = readGuestStruct<SifRpcDataQueue>(env.rdram.data(), kQdAddr);
            const SifRpcServerData sdAfterRegister = readGuestStruct<SifRpcServerData>(env.rdram.data(), kSdAddr);
            t.Equals(qdAfterRegister.link, kSdAddr, "queue link should point at registered server");
            t.Equals(static_cast<uint32_t>(sdAfterRegister.sid), kSid, "server sid should match registered sid");
            t.Equals(sdAfterRegister.buf, kServerBufAddr, "server buf should match register arg");
            t.Equals(sdAfterRegister.cbuf, kClientCbufAddr, "server cbuf should match stack arg");
            t.Equals(sdAfterRegister.base, kQdAddr, "server base should point to queue");

            setRegU32(env.ctx, 4, kClientAddr);
            setRegU32(env.ctx, 5, kSid);
            setRegU32(env.ctx, 6, 0u);
            SifBindRpc(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), KE_OK, "SifBindRpc should succeed");

            const SifRpcClientData clientAfterBind = readGuestStruct<SifRpcClientData>(env.rdram.data(), kClientAddr);
            t.Equals(clientAfterBind.server, kSdAddr, "client should bind to registered server");
            t.Equals(clientAfterBind.buf, kServerBufAddr, "client buf should mirror server buf");
            t.Equals(clientAfterBind.cbuf, kClientCbufAddr, "client cbuf should mirror server cbuf");

            std::array<uint8_t, 16> payload{};
            for (size_t i = 0; i < payload.size(); ++i)
            {
                payload[i] = static_cast<uint8_t>(0x50u + i);
            }
            std::memcpy(env.rdram.data() + kSendAddr, payload.data(), payload.size());
            std::memset(env.rdram.data() + kServerBufAddr, 0, payload.size());
            std::memset(env.rdram.data() + kRecvAddr, 0, payload.size());

            setRegU32(env.ctx, 4, kClientAddr);
            setRegU32(env.ctx, 5, 0x55u);
            setRegU32(env.ctx, 6, 0u);
            setRegU32(env.ctx, 7, kSendAddr);
            setRegU32(env.ctx, 8, static_cast<uint32_t>(payload.size()));
            setRegU32(env.ctx, 9, kRecvAddr);
            setRegU32(env.ctx, 10, static_cast<uint32_t>(payload.size()));
            setRegU32(env.ctx, 11, 0u);
            setRegU32(env.ctx, 29, K_STACK_ADDR);
            writeGuestU32(env.rdram.data(), K_STACK_ADDR + 0x00u, 0u); // endParam

            SifCallRpc(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), KE_OK, "SifCallRpc should succeed");

            const SifRpcServerData sdAfterCall = readGuestStruct<SifRpcServerData>(env.rdram.data(), kSdAddr);
            t.Equals(sdAfterCall.client, kClientAddr, "server should record caller client pointer");
            t.Equals(static_cast<uint32_t>(sdAfterCall.rpc_number), 0x55u, "server rpc_number should match request");
            t.Equals(static_cast<uint32_t>(sdAfterCall.size), static_cast<uint32_t>(payload.size()), "server size should match sendSize");
            t.Equals(sdAfterCall.recvbuf, kRecvAddr, "server recvbuf should match request recv pointer");
            t.Equals(static_cast<uint32_t>(sdAfterCall.rsize), static_cast<uint32_t>(payload.size()), "server rsize should match recvSize");
            t.Equals(static_cast<uint32_t>(sdAfterCall.rmode), 1u, "blocking call should set rmode to 1");

            t.IsTrue(std::memcmp(env.rdram.data() + kServerBufAddr, payload.data(), payload.size()) == 0,
                     "send payload should be copied into server buffer");
            t.IsTrue(std::memcmp(env.rdram.data() + kRecvAddr, payload.data(), payload.size()) == 0,
                     "unhandled RPC should copy payload into recv buffer");

            setRegU32(env.ctx, 4, kClientAddr);
            SifCheckStatRpc(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), 0, "SifCheckStatRpc should report not busy after synchronous completion");
        });

        tc.Run("MCSERV RPC init and get info report a formatted PS2 card", [](TestCase &t)
        {
            TestEnv env;
            const auto mcservModule = env.runtime.loadIopModule("rom0:MCSERV");
            t.IsTrue(mcservModule.moduleId > 0, "MCSERV test should load its IOP module first");
            ScopedTempDir temp("mcserv_rpc");

            const PS2Runtime::IoPaths oldPaths = PS2Runtime::getIoPaths();
            PS2Runtime::IoPaths ioPaths;
            ioPaths.elfDirectory = temp.path;
            ioPaths.hostRoot = temp.path;
            ioPaths.cdRoot = temp.path;
            ioPaths.mcRoot = temp.path / "mc0";
            PS2Runtime::setIoPaths(ioPaths);

            constexpr uint32_t kSendAddr = 0x00034000u;
            constexpr uint32_t kRecvAddr = 0x00035000u;
            constexpr uint32_t kEndParamAddr = 0x00036000u;

            const ps2x::iop::RpcResult initResult =
                callIop(env, IOP_SID_MCSERV, 0xFEu,
                        kSendAddr, sizeof(McDescParam), kRecvAddr, 12u);
            t.IsTrue(initResult.handled, "MCSERV init RPC should be handled");
            t.Equals(initResult.resultAddress, kRecvAddr, "MCSERV init should return recv buffer");
            t.IsFalse(initResult.signalNowaitCompletion, "MCSERV init should not request special nowait signaling");
            t.Equals(readGuestStruct<int32_t>(env.rdram.data(), kRecvAddr + 0u), 0,
                     "MCSERV init result should succeed");
            t.IsTrue(readGuestStruct<uint32_t>(env.rdram.data(), kRecvAddr + 4u) >= 0x205u,
                     "MCSERV init should expose a supported mcserv version");
            t.IsTrue(readGuestStruct<uint32_t>(env.rdram.data(), kRecvAddr + 8u) >= 0x206u,
                     "MCSERV init should expose a supported mcman version");

            McDescParam getInfo{};
            getInfo.port = 0;
            getInfo.slot = 0;
            getInfo.size = 1;   // formatted requested by XMCSERV libmc
            getInfo.offset = 1; // free clusters requested by XMCSERV libmc
            getInfo.origin = 1; // type requested by XMCSERV libmc
            getInfo.param = kEndParamAddr;
            writeGuestStruct(env.rdram.data(), kSendAddr, getInfo);
            std::memset(env.rdram.data() + kRecvAddr, 0xCC, 12u);
            std::memset(env.rdram.data() + kEndParamAddr, 0xCC, 192u);

            const ps2x::iop::RpcResult getInfoResult =
                callIop(env, IOP_SID_MCSERV, 0x01u,
                        kSendAddr, sizeof(McDescParam), kRecvAddr, 4u);

            PS2Runtime::setIoPaths(oldPaths);

            t.IsTrue(getInfoResult.handled, "MCSERV get info RPC should be handled");
            t.Equals(readGuestStruct<int32_t>(env.rdram.data(), kRecvAddr), 0,
                     "get info result should succeed");
            t.Equals(readGuestStruct<int32_t>(env.rdram.data(), kEndParamAddr + 0u), 2,
                     "get info should report PS2 card type");
            t.Equals(readGuestStruct<int32_t>(env.rdram.data(), kEndParamAddr + 4u), 0x2000,
                     "get info should report free clusters");
            t.Equals(readGuestStruct<int32_t>(env.rdram.data(), kEndParamAddr + 144u), 1,
                     "get info should report formatted card");
        });

        tc.Run("DBCMAN version RPC returns the 3.10 compatibility response", [](TestCase &t)
        {
            TestEnv env;
            const auto dbcmanModule = env.runtime.loadIopModule("rom0:DBCMAN");
            t.IsTrue(dbcmanModule.moduleId > 0, "DBCMAN test should load its IOP module first");

            constexpr uint32_t kDbcManSid = 0x80001300u;
            constexpr uint32_t kCheckVersionRpc = 0x80001363u;
            constexpr uint32_t kRecvAddr = 0x00035A00u;
            constexpr uint32_t kDbcManVersion = 0x0310u;

            std::memset(env.rdram.data() + kRecvAddr, 0xCC, 16u);
            const ps2x::iop::RpcResult result =
                callIop(env, kDbcManSid, kCheckVersionRpc,
                        0u, 0u, kRecvAddr, 16u);

            t.IsTrue(result.handled, "DBCMAN check-version RPC should be handled");
            t.Equals(result.resultAddress, kRecvAddr, "DBCMAN check-version RPC should return the receive buffer");
            t.IsFalse(result.signalNowaitCompletion, "DBCMAN check-version RPC should not request special nowait signaling");
            for (uint32_t index = 0u; index < 4u; ++index)
            {
                t.Equals(readGuestStruct<uint32_t>(env.rdram.data(), kRecvAddr + (index * 4u)),
                         kDbcManVersion,
                         "DBCMAN should repeat version 3.10 across the response words");
            }
        });

        tc.Run("LIBSD RPC routes through the IOP audio service", [](TestCase &t)
        {
            TestEnv env;
            const auto libsdModule = env.runtime.loadIopModule("rom0:LIBSD");
            t.IsTrue(libsdModule.moduleId > 0, "LIBSD test should load its IOP module first");

            constexpr uint32_t kSetVoiceRpc = 0x8010u;
            constexpr uint32_t kSendAddr = 0x00035B00u;
            constexpr uint32_t kRecvAddr = 0x00035C00u;

            std::array<uint32_t, 5> command{};
            command[0] = 3u;          // voice index
            command[2] = 0x1000u;    // neutral pitch
            command[3] = 0x00120000u; // plausible sample address
            writeGuestStruct(env.rdram.data(), kSendAddr, command);
            std::memset(env.rdram.data() + kRecvAddr, 0xA5, 16u);
            const ps2x::iop::RpcResult result =
                callIop(env, IOP_SID_LIBSD, kSetVoiceRpc,
                        kSendAddr, static_cast<uint32_t>(sizeof(command)),
                        kRecvAddr, 16u);

            t.IsTrue(result.handled, "LIBSD SID should be handled by the IOP audio service");
            t.Equals(result.resultAddress, kRecvAddr, "LIBSD should return the audio backend receive buffer");
            t.IsFalse(result.signalNowaitCompletion, "LIBSD should not request special nowait signaling");
            for (uint32_t index = 0u; index < 16u; ++index)
            {
                t.Equals(env.rdram[kRecvAddr + index], static_cast<uint8_t>(0xA5),
                         "LIBSD should preserve the backend-owned response buffer");
            }
        });

        tc.Run("hybrid bind before register waits then remaps", [](TestCase &t)
        {
            TestEnv env;

            constexpr uint32_t kQdAddr = 0x00024000u;
            constexpr uint32_t kSdAddr = 0x00024100u;
            constexpr uint32_t kClientAddr = 0x00024200u;
            constexpr uint32_t kServerBufAddr = 0x00024300u;
            constexpr uint32_t kServerCbufAddr = 0x00024400u;
            constexpr uint32_t kSid = 0x20000122u;

            SifInitRpc(env.rdram.data(), &env.ctx, &env.runtime);

            setRegU32(env.ctx, 4, kClientAddr);
            setRegU32(env.ctx, 5, kSid);
            setRegU32(env.ctx, 6, 0u);
            SifBindRpc(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), KE_OK, "initial bind without registered server should still succeed");

            const SifRpcClientData clientBeforeRegister = readGuestStruct<SifRpcClientData>(env.rdram.data(), kClientAddr);
            t.Equals(clientBeforeRegister.server, 0u, "bind must wait until a hybrid backend owns the SID");
            t.Equals(clientBeforeRegister.buf, 0u, "unbound client starts with empty buf");
            t.Equals(clientBeforeRegister.cbuf, 0u, "unbound client starts with empty cbuf");

            setRegU32(env.ctx, 4, kQdAddr);
            setRegU32(env.ctx, 5, 0x44u);
            SifSetRpcQueue(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), KE_OK, "SifSetRpcQueue should succeed");

            setRegU32(env.ctx, 29, K_STACK_ADDR);
            writeGuestU32(env.rdram.data(), K_STACK_ADDR + 0x10u, 0u);
            writeGuestU32(env.rdram.data(), K_STACK_ADDR + 0x14u, kServerCbufAddr);
            writeGuestU32(env.rdram.data(), K_STACK_ADDR + 0x18u, kQdAddr);

            setRegU32(env.ctx, 4, kSdAddr);
            setRegU32(env.ctx, 5, kSid);
            setRegU32(env.ctx, 6, 0u);
            setRegU32(env.ctx, 7, kServerBufAddr);
            SifRegisterRpc(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), KE_OK, "SifRegisterRpc should succeed");

            const SifRpcClientData clientAfterRegister = readGuestStruct<SifRpcClientData>(env.rdram.data(), kClientAddr);
            t.Equals(clientAfterRegister.server, kSdAddr, "register should remap pre-bound clients to concrete server descriptor");
            t.Equals(clientAfterRegister.buf, kServerBufAddr, "register should update client buf from server descriptor");
            t.Equals(clientAfterRegister.cbuf, kServerCbufAddr, "register should update client cbuf from server descriptor");
            t.IsTrue(clientAfterRegister.server != clientBeforeRegister.server, "client server pointer should switch from unbound to real server");

            setRegU32(env.ctx, 4, kSdAddr);
            setRegU32(env.ctx, 5, kQdAddr);
            SifRemoveRpc(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegU32Result(env.ctx, 2), kSdAddr, "SifRemoveRpc should return removed server pointer");

            const SifRpcDataQueue qdAfterRemove = readGuestStruct<SifRpcDataQueue>(env.rdram.data(), kQdAddr);
            const SifRpcServerData sdAfterRemove = readGuestStruct<SifRpcServerData>(env.rdram.data(), kSdAddr);
            t.Equals(qdAfterRemove.link, 0u, "queue link should detach removed server");
            t.Equals(sdAfterRemove.link, 0u, "removed server link should be cleared");
        });

        tc.Run("SifSetRpcQueue remove roundtrip is stable", [](TestCase &t)
        {
            TestEnv env;

            constexpr uint32_t kQdAddr = 0x00026000u;

            SifInitRpc(env.rdram.data(), &env.ctx, &env.runtime);

            setRegU32(env.ctx, 4, kQdAddr);
            setRegU32(env.ctx, 5, 0x55u);
            SifSetRpcQueue(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), KE_OK, "SifSetRpcQueue should succeed");

            const SifRpcDataQueue qd = readGuestStruct<SifRpcDataQueue>(env.rdram.data(), kQdAddr);
            t.Equals(static_cast<uint32_t>(qd.thread_id), 0x55u, "queue thread id should match argument");

            setRegU32(env.ctx, 4, kQdAddr);
            SifRemoveRpcQueue(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegU32Result(env.ctx, 2), kQdAddr, "SifRemoveRpcQueue should return removed queue pointer");

            setRegU32(env.ctx, 4, kQdAddr);
            SifRemoveRpcQueue(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegU32Result(env.ctx, 2), 0u, "removing the same queue twice should return 0");
        });

        tc.Run("SifCallRpc falls back to stack ABI when register pack is implausible", [](TestCase &t)
        {
            TestEnv env;

            constexpr uint32_t kQdAddr = 0x0002A000u;
            constexpr uint32_t kSdAddr = 0x0002A100u;
            constexpr uint32_t kClientAddr = 0x0002A200u;
            constexpr uint32_t kServerBufAddr = 0x0002A300u;
            constexpr uint32_t kSendAddr = 0x0002A400u;
            constexpr uint32_t kRecvAddr = 0x0002A500u;
            constexpr uint32_t kSid = 0x20000133u;

            SifInitRpc(env.rdram.data(), &env.ctx, &env.runtime);

            setRegU32(env.ctx, 4, kQdAddr);
            setRegU32(env.ctx, 5, 0x66u);
            SifSetRpcQueue(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), KE_OK, "SifSetRpcQueue should succeed");

            setRegU32(env.ctx, 29, K_STACK_ADDR);
            writeGuestU32(env.rdram.data(), K_STACK_ADDR + 0x10u, 0u);
            writeGuestU32(env.rdram.data(), K_STACK_ADDR + 0x14u, 0u);
            writeGuestU32(env.rdram.data(), K_STACK_ADDR + 0x18u, kQdAddr);

            setRegU32(env.ctx, 4, kSdAddr);
            setRegU32(env.ctx, 5, kSid);
            setRegU32(env.ctx, 6, 0u);
            setRegU32(env.ctx, 7, kServerBufAddr);
            SifRegisterRpc(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), KE_OK, "SifRegisterRpc should succeed");

            setRegU32(env.ctx, 4, kClientAddr);
            setRegU32(env.ctx, 5, kSid);
            setRegU32(env.ctx, 6, 0u);
            SifBindRpc(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), KE_OK, "SifBindRpc should succeed");

            std::array<uint8_t, 12> payload{};
            for (size_t i = 0; i < payload.size(); ++i)
            {
                payload[i] = static_cast<uint8_t>(0xA0u + i);
            }
            std::memcpy(env.rdram.data() + kSendAddr, payload.data(), payload.size());
            std::memset(env.rdram.data() + kRecvAddr, 0, payload.size());

            setRegU32(env.ctx, 29, K_STACK_ADDR);
            writeGuestU32(env.rdram.data(), K_STACK_ADDR + 0x10u, static_cast<uint32_t>(payload.size()));
            writeGuestU32(env.rdram.data(), K_STACK_ADDR + 0x14u, kRecvAddr);
            writeGuestU32(env.rdram.data(), K_STACK_ADDR + 0x18u, static_cast<uint32_t>(payload.size()));
            writeGuestU32(env.rdram.data(), K_STACK_ADDR + 0x1Cu, 0u);
            writeGuestU32(env.rdram.data(), K_STACK_ADDR + 0x20u, 0u);
            writeGuestU32(env.rdram.data(), K_STACK_ADDR + 0x00u, 0u);

            setRegU32(env.ctx, 4, kClientAddr);
            setRegU32(env.ctx, 5, 0x99u);
            setRegU32(env.ctx, 6, 0u);
            setRegU32(env.ctx, 7, kSendAddr);
            setRegU32(env.ctx, 8, 0x03000000u); // implausible size (> 0x02000000 threshold)
            setRegU32(env.ctx, 9, 0x00000004u); // implausible guest pointer
            setRegU32(env.ctx, 10, 0x03000001u);
            setRegU32(env.ctx, 11, 0u);

            SifCallRpc(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), KE_OK, "SifCallRpc should succeed with stack ABI fallback");

            const SifRpcServerData sdAfterCall = readGuestStruct<SifRpcServerData>(env.rdram.data(), kSdAddr);
            t.Equals(static_cast<uint32_t>(sdAfterCall.size), static_cast<uint32_t>(payload.size()),
                     "stack ABI sendSize should be selected when register ABI is implausible");
            t.Equals(sdAfterCall.recvbuf, kRecvAddr, "stack ABI recvBuf should be selected");
            t.Equals(static_cast<uint32_t>(sdAfterCall.rsize), static_cast<uint32_t>(payload.size()),
                     "stack ABI recvSize should be selected");

            t.IsTrue(std::memcmp(env.rdram.data() + kRecvAddr, payload.data(), payload.size()) == 0,
                     "recv payload should match stack-selected transfer size");
        });

        // The glue's unhandled rpc
        // 0xFF version probe must answer the "2800" tag the EE side accepts
        // (sub_49F940), not zeros.
        tc.Run("MOH Vanguard glue: version probe answers 2800", [](TestCase &t)
        {
            TestEnv env;

            constexpr uint32_t kGlueClientAddr = 0x65FDC0u;
            constexpr uint32_t kGlueRecvAddr = 0x00026000u;
            setRegU32(env.ctx, 4, kGlueClientAddr);
            setRegU32(env.ctx, 5, 0xFFu);
            setRegU32(env.ctx, 6, 0u);
            setRegU32(env.ctx, 7, 0u);
            setRegU32(env.ctx, 8, 0u);
            setRegU32(env.ctx, 9, kGlueRecvAddr);
            setRegU32(env.ctx, 10, 4u);
            setRegU32(env.ctx, 11, 0u);
            setRegU32(env.ctx, 29, K_STACK_ADDR);
            writeGuestU32(env.rdram.data(), K_STACK_ADDR, 0u);
            SifCallRpc(env.rdram.data(), &env.ctx, &env.runtime);

            const uint8_t expectedTag[4] = {'2', '8', '0', '0'};
            t.IsTrue(std::memcmp(env.rdram.data() + kGlueRecvAddr, expectedTag, 4) == 0,
                     "the glue version probe must answer the accepted tag");

            // A different unhandled rpc keeps the zero fallback.
            TestEnv otherEnv;
            setRegU32(otherEnv.ctx, 4, kGlueClientAddr);
            setRegU32(otherEnv.ctx, 5, 0x09u);
            setRegU32(otherEnv.ctx, 6, 0u);
            setRegU32(otherEnv.ctx, 7, 0u);
            setRegU32(otherEnv.ctx, 8, 0u);
            setRegU32(otherEnv.ctx, 9, kGlueRecvAddr);
            setRegU32(otherEnv.ctx, 10, 4u);
            setRegU32(otherEnv.ctx, 11, 0u);
            setRegU32(otherEnv.ctx, 29, K_STACK_ADDR);
            writeGuestU32(otherEnv.rdram.data(), K_STACK_ADDR, 0u);
            SifCallRpc(otherEnv.rdram.data(), &otherEnv.ctx, &otherEnv.runtime);

            const std::array<uint8_t, 4> zeros{};
            t.IsTrue(std::memcmp(otherEnv.rdram.data() + kGlueRecvAddr, zeros.data(), 4) == 0,
                     "other unhandled rpcs still fall back to zeros");
        });

        // The loader queue contract: the consumer thread
        // (0x36E578) posts a 48-byte request with sceSifSendCmd(cid=0) and the
        // game completes the request record only from its registered cid=1
        // SIF-cmd handler (sub_0036C1A8, registered at 0x36CD50 with
        // sceSifAddCmdHandler). With no IOP module serving cid=0 the runtime
        // must synthesize that reply: [+0xC] = request handle (the request
        // packet's +0x14 seq word), [+0x10] = 2 (the "complete the bucket
        // record" branch of the handler). This is what unlocks the LOADING
        // poll 0x360620 -> 0x36A3F8 -> 0x36E3E8.
        tc.Run("MOH Vanguard loader queue: SifSendCmd(0) synthesizes the cid-1 completion reply", [](TestCase &t)
        {
            TestEnv env;
            constexpr uint32_t kPktAddr = 0x656660u;         // the game's request-packet block
            constexpr uint32_t kRequestHandle = 0x00000001u; // seq 1, bucket slot = handle>>24 = 0
            constexpr uint32_t kHandlerArg = 0x64D780u;      // the argument sub_0036C7E0 registers

            // The request packet exactly as thread 6 builds it: the record's
            // seq word lands at +0x14.
            writeGuestU32(env.rdram.data(), kPktAddr + 0x14u, kRequestHandle);

            // Register the completion handler the way the game does.
            env.runtime.registerFunction(K_CID1REPLY_WAIT_PC, cid1ReplyWaitMain);
            env.runtime.registerFunction(K_CID1REPLY_RESUME_PC, cid1ReplyResumeMain);
            env.runtime.registerFunction(K_CID1REPLY_HANDLER_PC, cid1ReplyFakeCompletionHandler);
            setRegU32(env.ctx, 4, 1u);
            setRegU32(env.ctx, 5, K_CID1REPLY_HANDLER_PC);
            setRegU32(env.ctx, 6, kHandlerArg);
            ps2_stubs::sceSifAddCmdHandler(env.rdram.data(), &env.ctx, &env.runtime);

            // Reset the scheduler first: reset() clears the pending
            // invocation queue, so the request must be posted after it.
            g_cid1ReplySemaphoreId = 0;
            g_cid1ReplyReplyHandle = 0u;
            g_cid1ReplyReplyStatus = 0u;
            g_cid1ReplyHandlerArg = 0u;
            g_cid1ReplyHandlerRuns = 0u;
            R5900Context mainContext{};
            mainContext.pc = K_CID1REPLY_WAIT_PC;
            env.runtime.eeScheduler().reset(env.rdram.data(), mainContext);

            // Post the request through the game's SDK surface.
            setRegU32(env.ctx, 4, 0u);        // cid 0
            setRegU32(env.ctx, 5, kPktAddr);  // packet
            setRegU32(env.ctx, 6, 0x30u);     // size
            setRegU32(env.ctx, 7, 0u);        // no optional source
            setRegU32(env.ctx, 29, K_STACK_ADDR);
            writeGuestU32(env.rdram.data(), K_STACK_ADDR + 0x10u, 0u);
            writeGuestU32(env.rdram.data(), K_STACK_ADDR + 0x14u, 0u);
            sceSifSendCmd(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), 1, "SifSendCmd should still report success");

            // Let the scheduler deliver the queued completion invocation:
            // main blocks on a semaphore the fake handler signals.
            const bool finished = cid1ReplyRunSchedulerWithWatchdog(env);
            t.IsTrue(finished, "the scheduler must unblock: the synthesized completion must reach the handler");

            t.Equals(g_cid1ReplyHandlerRuns, 1u, "the cid-1 completion handler should run exactly once");
            t.Equals(g_cid1ReplyHandlerArg, kHandlerArg, "the completion handler should receive its registered argument");
            t.Equals(g_cid1ReplyReplyHandle, kRequestHandle, "the reply must carry the request handle at +0xC");
            t.Equals(g_cid1ReplyReplyStatus, 2u, "the reply status at +0x10 must be 2 (complete the bucket record)");
        });

        // The reply direction (cid=1) is the game's own completion traffic:
        // the runtime must never synthesize a reply for it (no loops).
        tc.Run("MOH Vanguard loader queue: cid-1 sends are not echoed back", [](TestCase &t)
        {
            TestEnv env;
            constexpr uint32_t kPktAddr = 0x656660u;
            writeGuestU32(env.rdram.data(), kPktAddr + 0x14u, 0x00000001u);

            env.runtime.registerFunction(K_CID1REPLY_WAIT_PC, cid1ReplyWaitMain);
            env.runtime.registerFunction(K_CID1REPLY_RESUME_PC, cid1ReplyResumeMain);
            env.runtime.registerFunction(K_CID1REPLY_HANDLER_PC, cid1ReplyFakeCompletionHandler);
            setRegU32(env.ctx, 4, 1u);
            setRegU32(env.ctx, 5, K_CID1REPLY_HANDLER_PC);
            setRegU32(env.ctx, 6, 0u);
            ps2_stubs::sceSifAddCmdHandler(env.rdram.data(), &env.ctx, &env.runtime);

            g_cid1ReplySemaphoreId = 0;
            g_cid1ReplyHandlerRuns = 0u;
            g_cid1ReplyReplyHandle = 0u;
            g_cid1ReplyReplyStatus = 0u;
            R5900Context mainContext{};
            mainContext.pc = K_CID1REPLY_WAIT_PC;
            env.runtime.eeScheduler().reset(env.rdram.data(), mainContext);

            setRegU32(env.ctx, 4, 1u); // cid 1 (reply direction)
            setRegU32(env.ctx, 5, kPktAddr);
            setRegU32(env.ctx, 6, 0x30u);
            setRegU32(env.ctx, 7, 0u);
            setRegU32(env.ctx, 29, K_STACK_ADDR);
            writeGuestU32(env.rdram.data(), K_STACK_ADDR + 0x10u, 0u);
            writeGuestU32(env.rdram.data(), K_STACK_ADDR + 0x14u, 0u);
            sceSifSendCmd(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), 1, "SifSendCmd should report success");

            const bool finished = cid1ReplyRunSchedulerWithWatchdog(env);
            t.IsTrue(finished, "the scheduler run must end (main stays blocked, nothing should wake it)");

            t.Equals(g_cid1ReplyHandlerRuns, 0u, "no synthetic reply may be queued for cid-1 sends");
        });

    });
}
