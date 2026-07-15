#include "MiniTest.h"
#include "ps2_runtime.h"
#include "ps2_iop_transport.h"
#include "ps2_syscalls.h"
#include "ps2_stubs.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <stdexcept>
#include <vector>

using namespace ps2_syscalls;

namespace ps2_stubs
{
    void resetSifState();
}

namespace
{
    constexpr int KE_OK = 0;
    constexpr int KE_SEMA_ZERO = -419;

    constexpr uint32_t K_SIF_RPC_MODE_NOWAIT = 0x01u;
    constexpr uint32_t K_STACK_ADDR = 0x00100000u;
    constexpr uint32_t IOP_SID_SNDDRV_COMMAND = 0x00000000u;
    constexpr uint32_t IOP_SID_SNDDRV_STATE = 0x00000001u;
    constexpr uint32_t IOP_SID_LOTR_CLFILE = 0x0000FF01u;
    constexpr uint32_t IOP_SID_LOTR_SOUND = 0x00012345u;
    constexpr uint32_t IOP_SID_MCSERV = 0x80000400u;
    constexpr uint32_t IOP_SID_LIBSD = 0x80000701u;
    constexpr uint32_t IOP_SID_FATAL_FRAME_SDRDRV = 0x19740512u;
    constexpr uint32_t IOP_RPC_SNDDRV_SUBMIT = 0x00000000u;
    constexpr uint32_t IOP_RPC_SNDDRV_GET_STATUS_ADDR = 0x00000012u;
    constexpr uint32_t IOP_RPC_SNDDRV_GET_ADDR_TABLE = 0x00000013u;

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

    void configureProfile(TestEnv &env, std::string_view elfName)
    {
        std::string error;
        const bool configured = PS2IopTransport::configureForTesting(
            &env.runtime, {std::string(elfName), 0u, 0u}, &error);
        if (!configured)
        {
            throw std::runtime_error("failed to configure test IOP profile: " + error);
        }
    }

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

    std::atomic<uint32_t> g_lotrSoundCallbackHits{0u};
    std::atomic<uint32_t> g_recvxSoundCallbackHits{0u};
    std::atomic<uint32_t> g_dtxDispatcherHits{0u};
    std::atomic<uint32_t> g_dtxDispatcherRpcNum{0u};
    std::atomic<uint32_t> g_dtxDispatcherSendBuf{0u};
    std::atomic<uint32_t> g_dtxDispatcherSendSize{0u};

    constexpr uint32_t K_DTX_DISPATCH_RESULT_ADDR = 0x0002D800u;
    constexpr uint32_t K_DTX_DISPATCH_RESULT_MARKER = 0xD15CA7C1u;

    void lotrSoundEndCallbackShouldNotRun(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        (void)rdram;
        (void)runtime;
        ++g_lotrSoundCallbackHits;
        ctx->pc = ::getRegU32(ctx, 31);
    }

    void recvxSoundEndCallbackShouldNotRun(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        (void)rdram;
        (void)runtime;
        ++g_recvxSoundCallbackHits;
        ctx->pc = ::getRegU32(ctx, 31);
    }

    void recvxDtxDispatcher(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        (void)runtime;
        ++g_dtxDispatcherHits;
        g_dtxDispatcherRpcNum = ::getRegU32(ctx, 4);
        g_dtxDispatcherSendBuf = ::getRegU32(ctx, 5);
        g_dtxDispatcherSendSize = ::getRegU32(ctx, 6);

        std::memcpy(rdram + K_DTX_DISPATCH_RESULT_ADDR,
                    &K_DTX_DISPATCH_RESULT_MARKER,
                    sizeof(K_DTX_DISPATCH_RESULT_MARKER));
        ctx->r[2] = _mm_set_epi64x(0, static_cast<int64_t>(K_DTX_DISPATCH_RESULT_ADDR));
        ctx->pc = ::getRegU32(ctx, 31);
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

    void writeFile(const std::filesystem::path &path, const std::vector<uint8_t> &data)
    {
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size()));
    }

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

        tc.Run("Fatal Frame SDRDRV RPC initializes header and loads body archives", [](TestCase &t)
        {
            TestEnv env;
            configureProfile(env, "SLUS_203.88");
            ScopedTempDir temp("fatal_frame_sdrdrv");

            std::vector<uint8_t> header(64u, 0);
            const char headerPayload[] = "img-header";
            std::memcpy(header.data(), headerPayload, sizeof(headerPayload) - 1u);
            writeFile(temp.path / "img_hd.bin", header);

            constexpr uint32_t kSectorSize = 2048u;
            std::vector<uint8_t> body(kSectorSize * 2u, 0);
            const char bodyPayload[] = "archive-data";
            std::memcpy(body.data() + kSectorSize, bodyPayload, sizeof(bodyPayload) - 1u);
            writeFile(temp.path / "img_bd.bin", body);

            const PS2Runtime::IoPaths oldPaths = PS2Runtime::getIoPaths();
            PS2Runtime::IoPaths ioPaths;
            ioPaths.elfDirectory = temp.path;
            ioPaths.hostRoot = temp.path;
            ioPaths.cdRoot = temp.path;
            ioPaths.mcRoot = temp.path / "mc0";
            PS2Runtime::setIoPaths(ioPaths);

            constexpr uint32_t kSendAddr = 0x00030000u;
            constexpr uint32_t kRecvAddr = 0x00031000u;
            constexpr uint32_t kDstAddr = 0x00032000u;
            constexpr uint32_t kImgHeaderAddr = 0x012F0000u;
            constexpr uint32_t kLoadId = 3u;

            std::array<uint32_t, 8> commands{};
            commands[0] = 0x0Eu;
            commands[2] = 1u; // lbn
            commands[3] = sizeof(bodyPayload) - 1u;
            commands[4] = kDstAddr;
            commands[6] = kLoadId;
            std::memcpy(env.rdram.data() + kSendAddr, commands.data(), commands.size() * sizeof(uint32_t));
            std::memset(env.rdram.data() + kRecvAddr, 0xCC, 0x180u);

            const ps2x::iop::RpcResult initResult =
                callIop(env, IOP_SID_FATAL_FRAME_SDRDRV, 0u,
                        0u, 0u, kRecvAddr, 0x180u);
            t.IsTrue(initResult.handled, "Fatal Frame SDRDRV init RPC should be handled");
            t.Equals(std::memcmp(env.rdram.data() + kImgHeaderAddr, "img-header", 10), 0,
                     "SDRDRV init RPC should load img_hd.bin into the arrangement table");

            const ps2x::iop::RpcResult result =
                callIop(env, IOP_SID_FATAL_FRAME_SDRDRV, 1u,
                        kSendAddr,
                        static_cast<uint32_t>(commands.size() * sizeof(uint32_t)),
                        kRecvAddr, 0x180u);

            PS2Runtime::setIoPaths(oldPaths);

            t.IsTrue(result.handled, "Fatal Frame SDRDRV SID should be handled");
            t.Equals(result.resultAddress, kRecvAddr, "SDRDRV RPC should return recv buffer");
            t.IsFalse(result.signalNowaitCompletion, "SDRDRV RPC should not request special nowait signaling");
            t.Equals(std::memcmp(env.rdram.data() + kDstAddr, "archive-data", 12), 0,
                     "SDRDRV load command should copy bytes from img_bd.bin by LBN");
            t.Equals(env.rdram[kRecvAddr + 0x6Cu + (kLoadId * 8u)], static_cast<uint8_t>(0),
                     "SDRDRV load status should report completed");
        });

        tc.Run("MCSERV RPC init and get info report a formatted PS2 card", [](TestCase &t)
        {
            TestEnv env;
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

        tc.Run("DBCMAN version RPC returns the 3.20 compatibility response", [](TestCase &t)
        {
            TestEnv env;

            constexpr uint32_t kDbcManSid = 0x80001300u;
            constexpr uint32_t kCheckVersionRpc = 0x80001363u;
            constexpr uint32_t kRecvAddr = 0x00035A00u;
            constexpr uint32_t kDbcManVersion = 0x0320u;

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
                         "DBCMAN should repeat version 3.20 across the response words");
            }
        });

        tc.Run("LIBSD RPC routes through the IOP audio service", [](TestCase &t)
        {
            TestEnv env;

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

        tc.Run("LotR ClFile RPC opens reads and reports EOF", [](TestCase &t)
        {
            TestEnv env;
            configureProfile(env, "SLUS_205.78");
            ScopedTempDir temp("lotr_clfile_rpc");

            const std::vector<uint8_t> payload = {'a', 'b', 'c'};
            const std::vector<uint8_t> loadPayload = {'x', 'y', 'z', '!'};
            writeFile(temp.path / "boot.cfg", payload);
            writeFile(temp.path / "load.bin", loadPayload);

            const PS2Runtime::IoPaths oldPaths = PS2Runtime::getIoPaths();
            PS2Runtime::IoPaths ioPaths;
            ioPaths.elfDirectory = temp.path;
            ioPaths.hostRoot = temp.path;
            ioPaths.cdRoot = temp.path;
            ioPaths.mcRoot = temp.path / "mc0";
            PS2Runtime::setIoPaths(ioPaths);

            constexpr uint32_t kSendAddr = 0x00036000u;
            constexpr uint32_t kRecvAddr = 0x00037000u;
            constexpr uint32_t kDstAddr = 0x00038000u;

            auto callClFileRpc = [&](uint32_t rpcNum, uint32_t sendSize) {
                const ps2x::iop::RpcResult result =
                    callIop(env, IOP_SID_LOTR_CLFILE, rpcNum,
                            kSendAddr, sendSize, kRecvAddr, 0x40u);
                t.IsTrue(result.handled, "LotR ClFile SID should be handled");
                t.Equals(result.resultAddress, kRecvAddr, "ClFile RPC should return recv buffer");
                t.IsFalse(result.signalNowaitCompletion, "ClFile RPC should not request special nowait signaling");
            };

            std::memset(env.rdram.data() + kSendAddr, 0, 0x100u);
            std::memcpy(env.rdram.data() + kSendAddr, "boot.cfg", 9u);
            std::memset(env.rdram.data() + kRecvAddr, 0xAA, 0x40u);
            callClFileRpc(0x08u, 0x100u);

            t.Equals(readGuestStruct<uint32_t>(env.rdram.data(), kRecvAddr + 0u), 0u,
                     "open should report success status");
            const uint32_t handle = readGuestStruct<uint32_t>(env.rdram.data(), kRecvAddr + 4u);
            t.IsTrue(handle != 0u, "open should return a non-zero remote file handle");

            std::memset(env.rdram.data() + kSendAddr, 0, 0x100u);
            std::memcpy(env.rdram.data() + kSendAddr, "missing.cfg", 12u);
            std::memset(env.rdram.data() + kRecvAddr, 0xAA, 0x40u);
            callClFileRpc(0x08u, 0x100u);
            t.Equals(readGuestStruct<uint32_t>(env.rdram.data(), kRecvAddr + 4u), 0u,
                     "missing file open should not manufacture a handle from path bytes");

            constexpr uint32_t kLoadDstAddr = 0x00039000u;
            std::memset(env.rdram.data() + kSendAddr, 0, 0x110u);
            std::memcpy(env.rdram.data() + kSendAddr, "load.bin", 9u);
            writeGuestU32(env.rdram.data(), kSendAddr + 0x100u, static_cast<uint32_t>(loadPayload.size()));
            writeGuestU32(env.rdram.data(), kSendAddr + 0x104u, kLoadDstAddr);
            std::memset(env.rdram.data() + kLoadDstAddr, 0xCC, 0x20u);
            callClFileRpc(0x01u, 0x110u);

            t.Equals(readGuestStruct<uint32_t>(env.rdram.data(), kRecvAddr + 0u), 5u,
                     "direct load should report a queued load result");
            const uint32_t loadHandle = readGuestStruct<uint32_t>(env.rdram.data(), kRecvAddr + 4u);
            t.IsTrue(loadHandle >= 3u, "direct load should return a status handle usable by getStatus");
            t.Equals(std::memcmp(env.rdram.data() + kLoadDstAddr, loadPayload.data(), loadPayload.size()), 0,
                     "direct load should copy file bytes into the requested guest destination");

            writeGuestU32(env.rdram.data(), kSendAddr, loadHandle);
            callClFileRpc(0x05u, 0u);
            writeGuestU32(env.rdram.data(), kSendAddr, loadHandle);
            callClFileRpc(0x03u, sizeof(uint32_t));
            t.Equals(readGuestStruct<uint32_t>(env.rdram.data(), kRecvAddr + 0u), 7u,
                     "direct load getStatus should report completed");

            writeGuestU32(env.rdram.data(), kSendAddr, loadHandle);
            callClFileRpc(0x06u, sizeof(uint32_t));
            t.Equals(readGuestStruct<uint32_t>(env.rdram.data(), kRecvAddr + 4u),
                     static_cast<uint32_t>(loadPayload.size()),
                     "direct load getSize should report the loaded host file size");

            writeGuestU32(env.rdram.data(), kSendAddr, loadHandle);
            callClFileRpc(0x09u, sizeof(uint32_t));

            if (handle != 0u)
            {
                std::array<uint32_t, 4> readPacket = {handle, 2u, kDstAddr, 0u};
                writeGuestStruct(env.rdram.data(), kSendAddr, readPacket);
                std::memset(env.rdram.data() + kDstAddr, 0, 8u);
                callClFileRpc(0x0Au, static_cast<uint32_t>(readPacket.size() * sizeof(uint32_t)));
                t.Equals(readGuestStruct<uint32_t>(env.rdram.data(), kRecvAddr + 4u), 2u,
                         "first read should report actual byte count");
                t.Equals(std::memcmp(env.rdram.data() + kDstAddr, "ab", 2), 0,
                         "first read should copy file bytes into guest destination");

                writeGuestStruct(env.rdram.data(), kSendAddr, readPacket);
                std::memset(env.rdram.data() + kDstAddr, 0, 8u);
                callClFileRpc(0x0Au, static_cast<uint32_t>(readPacket.size() * sizeof(uint32_t)));
                t.Equals(readGuestStruct<uint32_t>(env.rdram.data(), kRecvAddr + 4u), 1u,
                         "short read should report remaining byte count");
                t.Equals(std::memcmp(env.rdram.data() + kDstAddr, "c", 1), 0,
                         "short read should copy remaining file byte");

                writeGuestStruct(env.rdram.data(), kSendAddr, readPacket);
                std::memset(env.rdram.data() + kDstAddr, 0xCC, 8u);
                callClFileRpc(0x0Au, static_cast<uint32_t>(readPacket.size() * sizeof(uint32_t)));
                t.Equals(readGuestStruct<uint32_t>(env.rdram.data(), kRecvAddr + 4u), 0u,
                         "EOF read should report zero bytes");

                writeGuestU32(env.rdram.data(), kSendAddr, handle);
                callClFileRpc(0x09u, sizeof(uint32_t));
            }

            PS2Runtime::setIoPaths(oldPaths);
        });

        tc.Run("LotR sound RPC completes HLE callback without invoking guest loop", [](TestCase &t)
        {
            TestEnv env;
            configureProfile(env, "SLUS_205.78");

            constexpr uint32_t kClientAddr = 0x00039000u;
            constexpr uint32_t kSemaParamAddr = 0x00039100u;
            constexpr uint32_t kSendAddr = 0x0003A000u;
            constexpr uint32_t kRecvAddr = 0x0003C000u;
            constexpr uint32_t kEndFunc = 0x001FFD70u;

            env.runtime.registerFunction(kEndFunc, lotrSoundEndCallbackShouldNotRun);
            g_lotrSoundCallbackHits = 0u;

            SifInitRpc(env.rdram.data(), &env.ctx, &env.runtime);

            const uint32_t semaParam[6] = {0u, 1u, 0u, 0u, 0u, 0u};
            std::memcpy(env.rdram.data() + kSemaParamAddr, semaParam, sizeof(semaParam));
            setRegU32(env.ctx, 4, kSemaParamAddr);
            CreateSema(env.rdram.data(), &env.ctx, &env.runtime);
            const int32_t semaId = getRegS32(env.ctx, 2);
            t.IsTrue(semaId > 0, "CreateSema should return a positive semaphore id");

            setRegU32(env.ctx, 4, kClientAddr);
            setRegU32(env.ctx, 5, IOP_SID_LOTR_SOUND);
            setRegU32(env.ctx, 6, 0u);
            SifBindRpc(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), KE_OK, "SifBindRpc should succeed for LotR sound SID");

            writeGuestU32(env.rdram.data(), kSendAddr, 3u);
            std::memset(env.rdram.data() + kRecvAddr, 0xAA, 0x2000u);
            setRegU32(env.ctx, 4, kClientAddr);
            setRegU32(env.ctx, 5, 0u);
            setRegU32(env.ctx, 6, K_SIF_RPC_MODE_NOWAIT);
            setRegU32(env.ctx, 7, kSendAddr);
            setRegU32(env.ctx, 8, 0x2000u);
            setRegU32(env.ctx, 9, kRecvAddr);
            setRegU32(env.ctx, 10, 0x2000u);
            setRegU32(env.ctx, 11, kEndFunc);
            setRegU32(env.ctx, 29, K_STACK_ADDR);
            writeGuestU32(env.rdram.data(), K_STACK_ADDR + 0x00u, static_cast<uint32_t>(semaId));
            SifCallRpc(env.rdram.data(), &env.ctx, &env.runtime);

            t.Equals(getRegS32(env.ctx, 2), KE_OK, "SifCallRpc should succeed for LotR sound RPC");
            t.Equals(g_lotrSoundCallbackHits.load(), 0u,
                     "HLE-completed LotR sound callback should not invoke the guest callback");
            t.Equals(readGuestStruct<uint32_t>(env.rdram.data(), kRecvAddr + 0u), 0u,
                     "LotR sound response should report no active stream records");
            t.IsTrue(readGuestStruct<uint32_t>(env.rdram.data(), kRecvAddr + 4u) != 0u,
                     "LotR sound response should advance the IOP update counter");

            setRegU32(env.ctx, 4, static_cast<uint32_t>(semaId));
            PollSema(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), semaId, "LotR sound callback completion should signal the sema");
        });

        tc.Run("RECVX sound callbacks complete in HLE and only clear busy on designated callbacks", [](TestCase &t)
        {
            TestEnv env;
            configureProfile(env, "slus_201.84");

            constexpr uint32_t kClientAddr = 0x0003D000u;
            constexpr uint32_t kSemaParamAddr = 0x0003D100u;
            constexpr uint32_t kCompletionOnlyCallback = 0x002EAC20u;
            constexpr uint32_t kClearBusyCallback = 0x002EAC30u;
            constexpr uint32_t kBusyFlagAddr = 0x01E212C8u;

            env.runtime.registerFunction(kCompletionOnlyCallback, recvxSoundEndCallbackShouldNotRun);
            env.runtime.registerFunction(kClearBusyCallback, recvxSoundEndCallbackShouldNotRun);
            g_recvxSoundCallbackHits = 0u;

            SifInitRpc(env.rdram.data(), &env.ctx, &env.runtime);

            const uint32_t semaParam[6] = {0u, 1u, 0u, 0u, 0u, 0u};
            std::memcpy(env.rdram.data() + kSemaParamAddr, semaParam, sizeof(semaParam));
            setRegU32(env.ctx, 4, kSemaParamAddr);
            CreateSema(env.rdram.data(), &env.ctx, &env.runtime);
            const int32_t semaId = getRegS32(env.ctx, 2);
            t.IsTrue(semaId > 0, "CreateSema should return a positive semaphore id");

            setRegU32(env.ctx, 4, kClientAddr);
            setRegU32(env.ctx, 5, IOP_SID_SNDDRV_COMMAND);
            setRegU32(env.ctx, 6, 0u);
            SifBindRpc(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), KE_OK, "SifBindRpc should succeed for RECVX snddrv command SID");

            auto callSoundDriver = [&](uint32_t endFunc) {
                setRegU32(env.ctx, 4, kClientAddr);
                setRegU32(env.ctx, 5, IOP_RPC_SNDDRV_SUBMIT);
                setRegU32(env.ctx, 6, K_SIF_RPC_MODE_NOWAIT);
                setRegU32(env.ctx, 7, 0u);
                setRegU32(env.ctx, 8, 0u);
                setRegU32(env.ctx, 9, 0u);
                setRegU32(env.ctx, 10, 0u);
                setRegU32(env.ctx, 11, endFunc);
                setRegU32(env.ctx, 29, K_STACK_ADDR);
                writeGuestU32(env.rdram.data(), K_STACK_ADDR + 0x00u, static_cast<uint32_t>(semaId));

                SifCallRpc(env.rdram.data(), &env.ctx, &env.runtime);
                t.Equals(getRegS32(env.ctx, 2), KE_OK, "RECVX snddrv submission should complete");
            };

            constexpr uint32_t kBusyBeforeCompletion = 0x11111111u;
            writeGuestU32(env.rdram.data(), kBusyFlagAddr, kBusyBeforeCompletion);
            callSoundDriver(kCompletionOnlyCallback);

            t.Equals(g_recvxSoundCallbackHits.load(), 0u,
                     "recognized RECVX completion callback should not execute guest code");
            t.Equals(readGuestStruct<uint32_t>(env.rdram.data(), kBusyFlagAddr), kBusyBeforeCompletion,
                     "completion-only callback should preserve the RECVX busy flag");
            setRegU32(env.ctx, 4, static_cast<uint32_t>(semaId));
            PollSema(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), semaId, "completion-only callback should signal its semaphore");

            writeGuestU32(env.rdram.data(), kBusyFlagAddr, 0x22222222u);
            callSoundDriver(kClearBusyCallback);

            t.Equals(g_recvxSoundCallbackHits.load(), 0u,
                     "recognized RECVX clear-busy callback should not execute guest code");
            t.Equals(readGuestStruct<uint32_t>(env.rdram.data(), kBusyFlagAddr), 0u,
                     "designated RECVX callback should clear the guest busy flag");
            setRegU32(env.ctx, 4, static_cast<uint32_t>(semaId));
            PollSema(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), semaId, "clear-busy callback should signal its semaphore");

            setRegU32(env.ctx, 4, kClientAddr);
            SifCheckStatRpc(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), 0, "RECVX snddrv client should no longer be RPC-busy");
        });

        tc.Run("bind before register creates placeholder then remaps", [](TestCase &t)
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
            t.IsTrue(clientBeforeRegister.server != 0u, "bind should allocate placeholder server when sid is missing");
            t.IsTrue(clientBeforeRegister.server >= 0x01F10000u && clientBeforeRegister.server < 0x01F20000u,
                     "placeholder server should come from rpc server pool");
            t.Equals(clientBeforeRegister.buf, 0u, "placeholder server starts with empty buf");
            t.Equals(clientBeforeRegister.cbuf, 0u, "placeholder server starts with empty cbuf");

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
            t.IsTrue(clientAfterRegister.server != clientBeforeRegister.server, "client server pointer should switch from placeholder to real server");

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

        tc.Run("snddrv state RPC returns stable buffers and signals sema", [](TestCase &t)
        {
            TestEnv env;
            configureProfile(env, "slus_201.84");

            constexpr uint32_t kClientAddr = 0x00028000u;
            constexpr uint32_t kSemaParamAddr = 0x00028100u;
            constexpr uint32_t kRecvAddr = 0x00028200u;
            constexpr uint32_t kSid = IOP_SID_SNDDRV_STATE;

            SifInitRpc(env.rdram.data(), &env.ctx, &env.runtime);

            const uint32_t semaParam[6] = {
                0u, // count (unused by runtime decode)
                1u, // max_count
                0u, // init_count
                0u, // wait_threads
                0u, // attr
                0u  // option
            };
            std::memcpy(env.rdram.data() + kSemaParamAddr, semaParam, sizeof(semaParam));

            setRegU32(env.ctx, 4, kSemaParamAddr);
            CreateSema(env.rdram.data(), &env.ctx, &env.runtime);
            const int32_t semaId = getRegS32(env.ctx, 2);
            t.IsTrue(semaId > 0, "CreateSema should return a positive semaphore id");

            setRegU32(env.ctx, 4, kClientAddr);
            setRegU32(env.ctx, 5, kSid);
            setRegU32(env.ctx, 6, 0u);
            SifBindRpc(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), KE_OK, "SifBindRpc should succeed for snddrv state sid");

            setRegU32(env.ctx, 4, static_cast<uint32_t>(semaId));
            PollSema(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), KE_SEMA_ZERO, "semaphore should start at zero before nowait rpc");

            std::memset(env.rdram.data() + kRecvAddr, 0, 16u);
            setRegU32(env.ctx, 4, kClientAddr);
            setRegU32(env.ctx, 5, IOP_RPC_SNDDRV_GET_STATUS_ADDR);
            setRegU32(env.ctx, 6, K_SIF_RPC_MODE_NOWAIT);
            setRegU32(env.ctx, 7, 0u);
            setRegU32(env.ctx, 8, 0u);
            setRegU32(env.ctx, 9, kRecvAddr);
            setRegU32(env.ctx, 10, 16u);
            setRegU32(env.ctx, 11, 0u);
            setRegU32(env.ctx, 29, K_STACK_ADDR);
            writeGuestU32(env.rdram.data(), K_STACK_ADDR + 0x00u, static_cast<uint32_t>(semaId));
            SifCallRpc(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), KE_OK, "SifCallRpc(sound status) should succeed");
            const uint32_t statusAddr = readGuestStruct<uint32_t>(env.rdram.data(), kRecvAddr);
            t.IsTrue(statusAddr != 0u, "rpc 0x12 should return a nonzero sound-status pointer");

            setRegU32(env.ctx, 4, static_cast<uint32_t>(semaId));
            PollSema(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), semaId, "nowait rpc should signal completion sema");

            std::memset(env.rdram.data() + kRecvAddr, 0, 16u);
            setRegU32(env.ctx, 4, kClientAddr);
            setRegU32(env.ctx, 5, IOP_RPC_SNDDRV_GET_ADDR_TABLE);
            setRegU32(env.ctx, 6, K_SIF_RPC_MODE_NOWAIT);
            setRegU32(env.ctx, 7, 0u);
            setRegU32(env.ctx, 8, 0u);
            setRegU32(env.ctx, 9, kRecvAddr);
            setRegU32(env.ctx, 10, 16u);
            setRegU32(env.ctx, 11, 0u);
            writeGuestU32(env.rdram.data(), K_STACK_ADDR + 0x00u, static_cast<uint32_t>(semaId));
            SifCallRpc(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), KE_OK, "SifCallRpc(sound addr table) should succeed");
            const uint32_t addrTableAddr = readGuestStruct<uint32_t>(env.rdram.data(), kRecvAddr);
            t.IsTrue(addrTableAddr != 0u, "rpc 0x13 should return a nonzero address-table pointer");
            t.IsTrue(addrTableAddr != statusAddr, "sound-status and address-table pointers should be distinct");

            setRegU32(env.ctx, 4, static_cast<uint32_t>(semaId));
            PollSema(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), semaId, "each nowait rpc should signal completion sema");

            std::memset(env.rdram.data() + kRecvAddr, 0, 16u);
            setRegU32(env.ctx, 4, kClientAddr);
            setRegU32(env.ctx, 5, IOP_RPC_SNDDRV_GET_STATUS_ADDR);
            setRegU32(env.ctx, 6, K_SIF_RPC_MODE_NOWAIT);
            setRegU32(env.ctx, 7, 0u);
            setRegU32(env.ctx, 8, 0u);
            setRegU32(env.ctx, 9, kRecvAddr);
            setRegU32(env.ctx, 10, 16u);
            setRegU32(env.ctx, 11, 0u);
            writeGuestU32(env.rdram.data(), K_STACK_ADDR + 0x00u, static_cast<uint32_t>(semaId));
            SifCallRpc(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(readGuestStruct<uint32_t>(env.rdram.data(), kRecvAddr), statusAddr,
                     "sound-status pointer should remain stable across repeated rpc 0x12 calls");
        });

        tc.Run("snddrv state RPC returns low guest sound-driver addresses", [](TestCase &t)
        {
            TestEnv env;
            configureProfile(env, "slus_201.84");

            constexpr uint32_t kClientAddr = 0x00028300u;
            constexpr uint32_t kSemaParamAddr = 0x00028400u;
            constexpr uint32_t kRecvAddr = 0x00028500u;
            constexpr uint32_t kSid = IOP_SID_SNDDRV_STATE;

            SifInitRpc(env.rdram.data(), &env.ctx, &env.runtime);

            const uint32_t semaParam[6] = {0u, 1u, 0u, 0u, 0u, 0u};
            std::memcpy(env.rdram.data() + kSemaParamAddr, semaParam, sizeof(semaParam));

            setRegU32(env.ctx, 4, kSemaParamAddr);
            CreateSema(env.rdram.data(), &env.ctx, &env.runtime);
            const int32_t semaId = getRegS32(env.ctx, 2);
            t.IsTrue(semaId > 0, "CreateSema should return a positive semaphore id");

            setRegU32(env.ctx, 4, kClientAddr);
            setRegU32(env.ctx, 5, kSid);
            setRegU32(env.ctx, 6, 0u);
            SifBindRpc(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), KE_OK, "SifBindRpc should succeed for snddrv state sid");

            SifRpcClientData client = readGuestStruct<SifRpcClientData>(env.rdram.data(), kClientAddr);
            client.hdr.sema_id = semaId;
            writeGuestStruct(env.rdram.data(), kClientAddr, client);

            setRegU32(env.ctx, 4, kClientAddr);
            setRegU32(env.ctx, 5, IOP_RPC_SNDDRV_GET_STATUS_ADDR);
            setRegU32(env.ctx, 6, K_SIF_RPC_MODE_NOWAIT);
            setRegU32(env.ctx, 7, 0u);
            setRegU32(env.ctx, 8, 0u);
            setRegU32(env.ctx, 9, kRecvAddr);
            setRegU32(env.ctx, 10, 16u);
            setRegU32(env.ctx, 11, 0u);
            setRegU32(env.ctx, 29, K_STACK_ADDR);
            writeGuestU32(env.rdram.data(), K_STACK_ADDR + 0x00u, static_cast<uint32_t>(semaId));
            SifCallRpc(env.rdram.data(), &env.ctx, &env.runtime);
            const uint32_t statusAddr = readGuestStruct<uint32_t>(env.rdram.data(), kRecvAddr);
            t.IsTrue(statusAddr > 0u && statusAddr < 0x00200000u,
                     "rpc 0x12 should return a low guest address like an IOP pointer");

            setRegU32(env.ctx, 4, kClientAddr);
            setRegU32(env.ctx, 5, IOP_RPC_SNDDRV_GET_ADDR_TABLE);
            setRegU32(env.ctx, 6, K_SIF_RPC_MODE_NOWAIT);
            setRegU32(env.ctx, 7, 0u);
            setRegU32(env.ctx, 8, 0u);
            setRegU32(env.ctx, 9, kRecvAddr);
            setRegU32(env.ctx, 10, 16u);
            setRegU32(env.ctx, 11, 0u);
            SifCallRpc(env.rdram.data(), &env.ctx, &env.runtime);
            const uint32_t addrTableAddr = readGuestStruct<uint32_t>(env.rdram.data(), kRecvAddr);
            t.IsTrue(addrTableAddr > 0u && addrTableAddr < 0x00200000u,
                     "rpc 0x13 should return a low guest address like an IOP pointer");

            const uint32_t hdBaseAddr = readGuestStruct<uint32_t>(env.rdram.data(), addrTableAddr + 0u);
            const uint32_t sqBaseAddr = readGuestStruct<uint32_t>(env.rdram.data(), addrTableAddr + 4u);
            const uint32_t dataBaseAddr = readGuestStruct<uint32_t>(env.rdram.data(), addrTableAddr + 8u);
            t.IsTrue(hdBaseAddr > 0u && hdBaseAddr < 0x00200000u,
                     "sound-driver hd base should stay in low guest address space");
            t.IsTrue(sqBaseAddr > hdBaseAddr && sqBaseAddr < 0x00200000u,
                     "sound-driver sq base should be a later low guest address");
            t.IsTrue(dataBaseAddr > sqBaseAddr && dataBaseAddr < 0x00200000u,
                     "sound-driver data base should be a later low guest address");
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

        tc.Run("DTX URPC uses the guest dispatcher only when its function-table slot is registered", [](TestCase &t)
        {
            TestEnv env;
            configureProfile(env, "slus_201.84");

            constexpr uint32_t kClientAddr = 0x0002C000u;
            constexpr uint32_t kDtxSid = 0x7D000000u;
            constexpr uint32_t kSendAddr = 0x0002C100u;
            constexpr uint32_t kRecvAddr = 0x0002C200u;
            constexpr uint32_t kUrpcCommand = 7u;
            constexpr uint32_t kRpcNum = 0x400u | kUrpcCommand;
            constexpr uint32_t kFnTableSlot = 0x0033FED0u + (kUrpcCommand * sizeof(uint32_t));
            constexpr uint32_t kObjTableSlot = 0x0033FFD0u + (kUrpcCommand * sizeof(uint32_t));
            constexpr uint32_t kWrongFnTableSlot = 0x0034FED0u + (kUrpcCommand * sizeof(uint32_t));
            constexpr uint32_t kWrongObjTableSlot = 0x0034FFD0u + (kUrpcCommand * sizeof(uint32_t));
            constexpr uint32_t kDispatcherAddr = 0x002FABC0u;
            constexpr uint32_t kRegisteredHandlerAddr = 0x002FADE0u;
            constexpr uint32_t kRegisteredObjectAddr = 0x01F18100u;
            constexpr uint32_t kFallbackValue = 0x13579BDFu;

            g_dtxDispatcherHits = 0u;
            g_dtxDispatcherRpcNum = 0u;
            g_dtxDispatcherSendBuf = 0u;
            g_dtxDispatcherSendSize = 0u;
            env.runtime.registerFunction(kDispatcherAddr, recvxDtxDispatcher);

            SifInitRpc(env.rdram.data(), &env.ctx, &env.runtime);

            setRegU32(env.ctx, 4, kClientAddr);
            setRegU32(env.ctx, 5, kDtxSid);
            setRegU32(env.ctx, 6, 0u);
            SifBindRpc(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), KE_OK, "SifBindRpc should succeed for DTX dispatcher test");

            writeGuestU32(env.rdram.data(), kSendAddr + 0u, kFallbackValue);
            writeGuestU32(env.rdram.data(), kSendAddr + 4u, 0x2468ACE0u);

            auto callUrpc = [&]() {
                setRegU32(env.ctx, 4, kClientAddr);
                setRegU32(env.ctx, 5, kRpcNum);
                setRegU32(env.ctx, 6, 0u);
                setRegU32(env.ctx, 7, kSendAddr);
                setRegU32(env.ctx, 8, 8u);
                setRegU32(env.ctx, 9, kRecvAddr);
                setRegU32(env.ctx, 10, sizeof(uint32_t));
                setRegU32(env.ctx, 11, 0u);
                setRegU32(env.ctx, 29, K_STACK_ADDR);
                writeGuestU32(env.rdram.data(), K_STACK_ADDR + 0x00u, 0u);

                SifCallRpc(env.rdram.data(), &env.ctx, &env.runtime);
                t.Equals(getRegS32(env.ctx, 2), KE_OK, "DTX URPC should complete");
            };

            writeGuestU32(env.rdram.data(), kFnTableSlot, 0u);
            writeGuestU32(env.rdram.data(), kObjTableSlot, kRegisteredObjectAddr);
            writeGuestU32(env.rdram.data(), kWrongFnTableSlot, kRegisteredHandlerAddr);
            writeGuestU32(env.rdram.data(), kWrongObjTableSlot, kRegisteredObjectAddr);
            writeGuestU32(env.rdram.data(), kRecvAddr, 0u);
            callUrpc();

            t.Equals(g_dtxDispatcherHits.load(), 0u,
                     "empty DTX function-table slot should use fallback emulation");
            t.Equals(readGuestStruct<uint32_t>(env.rdram.data(), kRecvAddr), kFallbackValue,
                     "fallback DTX emulation should return the first send word for an unknown command");

            writeGuestU32(env.rdram.data(), kFnTableSlot, kRegisteredHandlerAddr);
            writeGuestU32(env.rdram.data(), kRecvAddr, 0u);
            callUrpc();

            t.Equals(g_dtxDispatcherHits.load(), 1u,
                     "registered DTX function-table slot should enter the guest dispatcher");
            t.Equals(g_dtxDispatcherRpcNum.load(), kRpcNum,
                     "DTX dispatcher should receive the full URPC number");
            t.Equals(g_dtxDispatcherSendBuf.load(), kSendAddr,
                     "DTX dispatcher should receive the send-buffer address");
            t.Equals(g_dtxDispatcherSendSize.load(), 8u,
                     "DTX dispatcher should receive the send-buffer size");
            t.Equals(readGuestStruct<uint32_t>(env.rdram.data(), kRecvAddr), K_DTX_DISPATCH_RESULT_MARKER,
                     "SifCallRpc should copy the dispatcher result into the receive buffer");
        });

        tc.Run("SifCallRpc prefers stack ABI for DTX URPC when both packs look plausible", [](TestCase &t)
        {
            TestEnv env;
            configureProfile(env, "slus_201.84");

            constexpr uint32_t kClientAddr = 0x0002B000u;
            constexpr uint32_t kDtxSid = 0x7D000000u;
            constexpr uint32_t kSendAddr = 0x0002B100u;
            constexpr uint32_t kRecvStackAddr = 0x0002B200u;
            constexpr uint32_t kRecvRegAddr = 0x0002B300u;

            SifInitRpc(env.rdram.data(), &env.ctx, &env.runtime);

            setRegU32(env.ctx, 4, kClientAddr);
            setRegU32(env.ctx, 5, kDtxSid);
            setRegU32(env.ctx, 6, 0u);
            SifBindRpc(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), KE_OK, "SifBindRpc should succeed for DTX sid");

            writeGuestU32(env.rdram.data(), kSendAddr + 0x00u, 1u);        // mode
            writeGuestU32(env.rdram.data(), kSendAddr + 0x04u, 0x1E21440u); // wk addr
            writeGuestU32(env.rdram.data(), kSendAddr + 0x08u, 0x100u);      // wk size
            writeGuestU32(env.rdram.data(), kRecvStackAddr, 0u);
            writeGuestU32(env.rdram.data(), kRecvRegAddr, 0u);

            setRegU32(env.ctx, 29, K_STACK_ADDR);
            writeGuestU32(env.rdram.data(), K_STACK_ADDR + 0x10u, 12u);
            writeGuestU32(env.rdram.data(), K_STACK_ADDR + 0x14u, kRecvStackAddr);
            writeGuestU32(env.rdram.data(), K_STACK_ADDR + 0x18u, 4u);
            writeGuestU32(env.rdram.data(), K_STACK_ADDR + 0x1Cu, 0u);
            writeGuestU32(env.rdram.data(), K_STACK_ADDR + 0x20u, 0u);
            writeGuestU32(env.rdram.data(), K_STACK_ADDR + 0x00u, 0u);

            setRegU32(env.ctx, 4, kClientAddr);
            setRegU32(env.ctx, 5, 0x422u); // DTX URPC command 34 (SJUNI create)
            setRegU32(env.ctx, 6, 0u);
            setRegU32(env.ctx, 7, kSendAddr);
            // Plausible but intentionally wrong register-side packed args.
            setRegU32(env.ctx, 8, 4u);
            setRegU32(env.ctx, 9, kRecvRegAddr);
            setRegU32(env.ctx, 10, 12u);
            setRegU32(env.ctx, 11, 0u);

            SifCallRpc(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), KE_OK, "SifCallRpc should succeed for DTX URPC");

            const uint32_t stackHandle = readGuestStruct<uint32_t>(env.rdram.data(), kRecvStackAddr);
            const uint32_t regHandle = readGuestStruct<uint32_t>(env.rdram.data(), kRecvRegAddr);
            t.IsTrue(stackHandle != 0u, "DTX handle should be written to stack-selected recv buffer");
            t.Equals(regHandle, 0u, "register recv buffer should remain untouched when stack ABI is preferred");
        });
    });
}
