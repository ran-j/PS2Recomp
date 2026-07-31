#include "MiniTest.h"
#include "ps2_runtime.h"
#include "ps2_iop_transport.h"
#include "ps2_syscalls.h"
#include "ps2_stubs.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ps2_stubs
{
    void resetSifState();
}

namespace
{
    constexpr int KE_OK = 0;

    struct TestEnv
    {
        std::vector<uint8_t> rdram;
        R5900Context ctx{};
        PS2Runtime runtime;

        TestEnv() : rdram(PS2_RAM_SIZE, 0u)
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

    #pragma pack(push, 1)
    struct Ps2SifDmaTransfer
    {
        uint32_t src;
        uint32_t dest;
        int32_t size;
        int32_t attr;
    };

    struct SifRpcHeader
    {
        uint32_t pkt_addr;
        uint32_t rpc_id;
        int32_t sema_id;
        uint32_t mode;
    };

    struct SifRpcReceiveData
    {
        SifRpcHeader hdr;
        uint32_t src;
        uint32_t dest;
        int32_t size;
    };
    #pragma pack(pop)

    static_assert(sizeof(Ps2SifDmaTransfer) == 16u, "Unexpected Ps2SifDmaTransfer size.");
    static_assert(sizeof(SifRpcReceiveData) == 28u, "Unexpected SifRpcReceiveData size.");

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

    uint32_t readGuestU32(const uint8_t *rdram, uint32_t addr)
    {
        uint32_t value = 0;
        std::memcpy(&value, rdram + addr, sizeof(value));
        return value;
    }

    void writeGuestS16(uint8_t *rdram, uint32_t addr, int16_t value)
    {
        std::memcpy(rdram + addr, &value, sizeof(value));
    }

    int16_t readGuestS16(const uint8_t *rdram, uint32_t addr)
    {
        int16_t value = 0;
        std::memcpy(&value, rdram + addr, sizeof(value));
        return value;
    }

    uint32_t g_dmacHandlerWriteAddr = 0u;
    uint32_t g_dmacHandlerValue = 0u;
    uint32_t g_dmacHandlerLastCause = 0u;
    uint32_t g_dmacHandlerLastArg = 0u;

    void testDmacHandler(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        (void)runtime;
        g_dmacHandlerLastCause = ::getRegU32(ctx, 4);
        g_dmacHandlerLastArg = ::getRegU32(ctx, 5);
        if (g_dmacHandlerWriteAddr != 0u)
        {
            writeGuestU32(rdram, g_dmacHandlerWriteAddr, g_dmacHandlerValue);
        }
        ctx->pc = 0u;
    }
}

void register_ps2_sif_dma_tests()
{
    MiniTest::Case("PS2SifDma", [](TestCase &tc)
    {
        tc.Run("sceSifSetDma copies payload and sceSifDmaStat reports complete", [](TestCase &t)
        {
            TestEnv env;

            constexpr uint32_t kDescAddr = 0x00020000u;
            constexpr uint32_t kSrcAddr = 0x00020100u;
            constexpr uint32_t kDstAddr = 0x00020200u;

            std::array<uint8_t, 16> payload{};
            for (size_t i = 0; i < payload.size(); ++i)
            {
                payload[i] = static_cast<uint8_t>(0x30u + i);
            }
            std::memcpy(env.rdram.data() + kSrcAddr, payload.data(), payload.size());
            std::memset(env.rdram.data() + kDstAddr, 0, payload.size());

            const Ps2SifDmaTransfer desc{
                kSrcAddr,
                kDstAddr,
                static_cast<int32_t>(payload.size()),
                0};
            std::memcpy(env.rdram.data() + kDescAddr, &desc, sizeof(desc));

            setRegU32(env.ctx, 4, kDescAddr);
            setRegU32(env.ctx, 5, 1u);
            ps2_stubs::sceSifSetDma(env.rdram.data(), &env.ctx, &env.runtime);
            const int32_t dmaId = getRegS32(env.ctx, 2);
            t.IsTrue(dmaId > 0, "sceSifSetDma should return a positive transfer id on success");

            t.IsTrue(std::memcmp(env.rdram.data() + kDstAddr, payload.data(), payload.size()) == 0,
                     "sceSifSetDma should copy transfer payload to destination");

            setRegU32(env.ctx, 4, static_cast<uint32_t>(dmaId));
            ps2_stubs::sceSifDmaStat(env.rdram.data(), &env.ctx, &env.runtime);
            t.IsTrue(getRegS32(env.ctx, 2) < 0, "sceSifDmaStat should be negative when transfer is complete");
        });

        tc.Run("sceSifSetDma routes raw LOADFILE bind and calls through the IOP subsystem", [](TestCase &t)
        {
            TestEnv env;
            configureProfile(env, "unmatched.elf");

            constexpr uint32_t kDescriptorAddress = 0x00025000u;
            constexpr uint32_t kPacketAddress = 0x00025100u;
            constexpr uint32_t kPayloadAddress = 0x00025200u;
            constexpr uint32_t kReceivePointerAddress = 0x00025400u;
            constexpr uint32_t kInboundAddress = 0x00025500u;
            constexpr uint32_t kClientAddress = 0x00025600u;
            constexpr uint32_t kResultAddress = 0x00025700u;
            constexpr uint32_t kSid = 0x80000006u;
            constexpr uint32_t kBindCommand = 0x80000009u;
            constexpr uint32_t kCallCommand = 0x8000000Au;
            constexpr uint32_t kEndCommand = 0x80000008u;
            constexpr uint32_t kSifSubAddressRegister = 0x80000001u;

            writeGuestU32(env.rdram.data(), kReceivePointerAddress, kInboundAddress);
            setRegU32(env.ctx, 4, kSifSubAddressRegister);
            setRegU32(env.ctx, 5, kReceivePointerAddress);
            ps2_stubs::sceSifSetReg(env.rdram.data(), &env.ctx, &env.runtime);

            const auto writePacketHeader = [&](uint32_t command, uint32_t rpcId)
            {
                std::memset(env.rdram.data() + kPacketAddress, 0, 0x40u);
                writeGuestU32(env.rdram.data(), kPacketAddress, 0x40u);
                writeGuestU32(env.rdram.data(), kPacketAddress + 0x08u, command);
                writeGuestU32(env.rdram.data(), kPacketAddress + 0x10u, 0xA5000000u | rpcId);
                writeGuestU32(env.rdram.data(), kPacketAddress + 0x14u, 0x20310000u + rpcId * 0x40u);
                writeGuestU32(env.rdram.data(), kPacketAddress + 0x18u, rpcId);
                writeGuestU32(env.rdram.data(), kPacketAddress + 0x1Cu, kClientAddress);
            };
            const auto invokeDma = [&](uint32_t descriptorCount)
            {
                setRegU32(env.ctx, 4, kDescriptorAddress);
                setRegU32(env.ctx, 5, descriptorCount);
                ps2_stubs::sceSifSetDma(env.rdram.data(), &env.ctx, &env.runtime);
                t.IsTrue(getRegS32(env.ctx, 2) > 0,
                         "raw SIF RPC DMA should return a completed transfer ID");
            };

            writePacketHeader(kBindCommand, 1u);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x20u, kSid);
            const Ps2SifDmaTransfer bindDescriptor{
                kPacketAddress, 0u, 0x40, 0x44};
            std::memcpy(env.rdram.data() + kDescriptorAddress,
                        &bindDescriptor,
                        sizeof(bindDescriptor));
            invokeDma(1u);

            t.Equals(readGuestU32(env.rdram.data(), kInboundAddress + 0x08u),
                     kEndCommand,
                     "a supported raw RPC_BIND should emit RPC_END");
            t.Equals(readGuestU32(env.rdram.data(), kInboundAddress + 0x20u),
                     kBindCommand,
                     "the bind completion should identify RPC_BIND");
            t.IsTrue(readGuestU32(env.rdram.data(), kInboundAddress + 0x24u) != 0u,
                     "a supported raw RPC_BIND should return a server token");

            std::memset(env.rdram.data() + kResultAddress, 0, 8u);
            writePacketHeader(kCallCommand, 2u);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x20u, 0xFFu);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x24u, 0u);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x28u, kResultAddress);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x2Cu, 4u);
            const Ps2SifDmaTransfer versionDescriptor{
                kPacketAddress, 0u, 0x40, 0x44};
            std::memcpy(env.rdram.data() + kDescriptorAddress,
                        &versionDescriptor,
                        sizeof(versionDescriptor));
            invokeDma(1u);

            t.Equals(readGuestU32(env.rdram.data(), kResultAddress),
                     0x30333432u,
                     "raw LOADFILE GET_VERSION should return the compatible version");
            t.Equals(readGuestU32(env.rdram.data(), kInboundAddress + 0x20u),
                     kCallCommand,
                     "the version completion should identify RPC_CALL");

            std::memset(env.rdram.data() + kPayloadAddress, 0, 0x200u);
            constexpr std::string_view kModulePath = "cdrom0:\\IOP\\SIO2MAN.IRX;1";
            std::memcpy(env.rdram.data() + kPayloadAddress + 8u,
                        kModulePath.data(),
                        kModulePath.size() + 1u);
            std::memset(env.rdram.data() + kResultAddress, 0, 8u);
            writePacketHeader(kCallCommand, 3u);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x20u, 0u);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x24u, 0x200u);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x28u, kResultAddress);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x2Cu, 8u);
            const std::array<Ps2SifDmaTransfer, 2> loadDescriptors{{
                {kPayloadAddress, 0u, 0x200, 0},
                {kPacketAddress, 0u, 0x40, 0x44},
            }};
            std::memcpy(env.rdram.data() + kDescriptorAddress,
                        loadDescriptors.data(),
                        sizeof(loadDescriptors));
            invokeDma(2u);

            t.Equals(readGuestU32(env.rdram.data(), kResultAddress),
                     1u,
                     "raw LOADFILE MOD_LOAD should return the first module ID");
            t.Equals(readGuestU32(env.rdram.data(), kResultAddress + 4u),
                     0u,
                     "raw LOADFILE MOD_LOAD should report successful startup");
            t.Equals(readGuestU32(env.rdram.data(), kInboundAddress + 0x18u),
                     3u,
                     "the module-load completion should preserve the raw RPC ID");

            // Duelists reaches this core service after its IOP module loader and
            // uses raw RPC sequence 28 for the observed readiness poll.
            constexpr uint32_t kDiskReadySid = 0x8000059Au;
            constexpr uint32_t kDiskReadyClientAddress = 0x00025800u;
            constexpr uint32_t kDiskReadyModeAddress = 0x00025900u;
            constexpr uint32_t kDiskReadyComplete = 2u;
            writePacketHeader(kBindCommand, 27u);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x1Cu, kDiskReadyClientAddress);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x20u, kDiskReadySid);
            std::memcpy(env.rdram.data() + kDescriptorAddress,
                        &bindDescriptor,
                        sizeof(bindDescriptor));
            invokeDma(1u);

            t.Equals(readGuestU32(env.rdram.data(), kInboundAddress + 0x20u),
                     kBindCommand,
                     "the Disk Ready bind completion should identify RPC_BIND");
            t.IsTrue(readGuestU32(env.rdram.data(), kInboundAddress + 0x24u) != 0u,
                     "the Disk Ready raw RPC_BIND should return a server token");

            writeGuestU32(env.rdram.data(), kDiskReadyModeAddress, 0u);
            writeGuestU32(env.rdram.data(), kResultAddress, 0xFFFFFFFFu);
            writePacketHeader(kCallCommand, 28u);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x1Cu, kDiskReadyClientAddress);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x20u, 0u);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x24u, 4u);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x28u, kResultAddress);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x2Cu, 4u);
            const std::array<Ps2SifDmaTransfer, 2> diskReadyDescriptors{{
                {kDiskReadyModeAddress, 0u, 4, 0},
                {kPacketAddress, 0u, 0x40, 0x44},
            }};
            std::memcpy(env.rdram.data() + kDescriptorAddress,
                        diskReadyDescriptors.data(),
                        sizeof(diskReadyDescriptors));
            invokeDma(2u);

            t.Equals(readGuestU32(env.rdram.data(), kResultAddress),
                     kDiskReadyComplete,
                     "raw CD/DVD Disk Ready should report the standard complete status");
            t.Equals(readGuestU32(env.rdram.data(), kInboundAddress + 0x18u),
                     28u,
                     "the Disk Ready completion should preserve raw RPC sequence 28");
            t.Equals(readGuestU32(env.rdram.data(), kInboundAddress + 0x20u),
                     kCallCommand,
                     "the Disk Ready completion should identify RPC_CALL");

            // The next Duelists boot boundary binds Search File with raw RPC
            // sequence 19, then issues the standard legacy 0x124-byte request.
            constexpr uint32_t kSearchFileSid = 0x80000597u;
            constexpr uint32_t kSearchFileClientAddress = 0x003158C0u;
            constexpr uint32_t kSearchFilePacketAddress = 0x00025A00u;
            constexpr uint32_t kSearchFileResultAddress = 0x00025C00u;
            constexpr uint32_t kSearchFileEntryAddress = 0x00025D00u;
            constexpr uint32_t kSearchFilePacketSize = 0x124u;
            constexpr std::string_view kMissingBootPath = "cdrom0:\\SYSTEM.CNF;1";

            writePacketHeader(kBindCommand, 19u);
            writeGuestU32(env.rdram.data(),
                          kPacketAddress + 0x1Cu,
                          kSearchFileClientAddress);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x20u, kSearchFileSid);
            std::memcpy(env.rdram.data() + kDescriptorAddress,
                        &bindDescriptor,
                        sizeof(bindDescriptor));
            invokeDma(1u);

            t.Equals(readGuestU32(env.rdram.data(), kInboundAddress + 0x18u),
                     19u,
                     "the Search File bind completion should preserve Duelists sequence 19");
            t.Equals(readGuestU32(env.rdram.data(), kInboundAddress + 0x20u),
                     kBindCommand,
                     "the Search File bind completion should identify RPC_BIND");
            t.IsTrue(readGuestU32(env.rdram.data(), kInboundAddress + 0x24u) != 0u,
                     "the Search File raw RPC_BIND should return a server token");

            std::memset(env.rdram.data() + kSearchFilePacketAddress,
                        0,
                        kSearchFilePacketSize);
            std::memcpy(env.rdram.data() + kSearchFilePacketAddress + 0x20u,
                        kMissingBootPath.data(),
                        kMissingBootPath.size() + 1u);
            writeGuestU32(env.rdram.data(),
                          kSearchFilePacketAddress + 0x120u,
                          kSearchFileEntryAddress);
            writeGuestU32(env.rdram.data(), kSearchFileResultAddress, 0xFFFFFFFFu);

            writePacketHeader(kCallCommand, 20u);
            writeGuestU32(env.rdram.data(),
                          kPacketAddress + 0x1Cu,
                          kSearchFileClientAddress);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x20u, 0u);
            writeGuestU32(env.rdram.data(),
                          kPacketAddress + 0x24u,
                          kSearchFilePacketSize);
            writeGuestU32(env.rdram.data(),
                          kPacketAddress + 0x28u,
                          kSearchFileResultAddress);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x2Cu, 4u);
            const std::array<Ps2SifDmaTransfer, 2> searchFileDescriptors{{
                {kSearchFilePacketAddress, 0u,
                 static_cast<int32_t>(kSearchFilePacketSize), 0},
                {kPacketAddress, 0u, 0x40, 0x44},
            }};
            std::memcpy(env.rdram.data() + kDescriptorAddress,
                        searchFileDescriptors.data(),
                        sizeof(searchFileDescriptors));
            invokeDma(2u);

            t.Equals(readGuestU32(env.rdram.data(), kSearchFileResultAddress),
                     0u,
                     "raw Search File should complete with not-found when no disc root is configured");
            t.Equals(readGuestU32(env.rdram.data(), kInboundAddress + 0x18u),
                     20u,
                     "the Search File call completion should preserve its raw RPC sequence");
            t.Equals(readGuestU32(env.rdram.data(), kInboundAddress + 0x20u),
                     kCallCommand,
                     "the Search File call completion should identify RPC_CALL");

            // Duelists next binds MCSERV at raw RPC sequence 0xd5 and starts
            // it with the standard 48-byte function-0xfe request.
            constexpr uint32_t kMcservSid = 0x80000400u;
            constexpr uint32_t kMcservClientAddress = 0x00315C40u;
            constexpr uint32_t kMcservPayloadAddress = 0x00025E00u;
            constexpr uint32_t kMcservResultAddress = 0x00025F00u;
            constexpr uint32_t kMcservBindRpc = 0xD5u;
            constexpr uint32_t kMcservCallRpc = 0xD6u;
            constexpr uint32_t kMcservInitFunction = 0xFEu;
            constexpr uint32_t kMcservPayloadSize = 48u;
            constexpr uint32_t kMcservResultSize = 12u;

            writePacketHeader(kBindCommand, kMcservBindRpc);
            writeGuestU32(env.rdram.data(),
                          kPacketAddress + 0x1Cu,
                          kMcservClientAddress);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x20u, kMcservSid);
            std::memcpy(env.rdram.data() + kDescriptorAddress,
                        &bindDescriptor,
                        sizeof(bindDescriptor));
            invokeDma(1u);

            t.Equals(readGuestU32(env.rdram.data(), kInboundAddress + 0x08u),
                     kEndCommand,
                     "the MCSERV bind should emit RPC_END");
            t.Equals(readGuestU32(env.rdram.data(), kInboundAddress + 0x18u),
                     kMcservBindRpc,
                     "the MCSERV bind completion should preserve Duelists sequence 0xd5");
            t.Equals(readGuestU32(env.rdram.data(), kInboundAddress + 0x20u),
                     kBindCommand,
                     "the MCSERV bind completion should identify RPC_BIND");
            t.IsTrue(readGuestU32(env.rdram.data(), kInboundAddress + 0x24u) != 0u,
                     "the MCSERV raw RPC_BIND should return a server token");

            std::memset(env.rdram.data() + kMcservPayloadAddress,
                        0,
                        kMcservPayloadSize);
            std::memset(env.rdram.data() + kMcservResultAddress,
                        0xFF,
                        kMcservResultSize);
            writePacketHeader(kCallCommand, kMcservCallRpc);
            writeGuestU32(env.rdram.data(),
                          kPacketAddress + 0x1Cu,
                          kMcservClientAddress);
            writeGuestU32(env.rdram.data(),
                          kPacketAddress + 0x20u,
                          kMcservInitFunction);
            writeGuestU32(env.rdram.data(),
                          kPacketAddress + 0x24u,
                          kMcservPayloadSize);
            writeGuestU32(env.rdram.data(),
                          kPacketAddress + 0x28u,
                          kMcservResultAddress);
            writeGuestU32(env.rdram.data(),
                          kPacketAddress + 0x2Cu,
                          kMcservResultSize);
            const std::array<Ps2SifDmaTransfer, 2> mcservInitDescriptors{{
                {kMcservPayloadAddress, 0u,
                 static_cast<int32_t>(kMcservPayloadSize), 0},
                {kPacketAddress, 0u, 0x40, 0x44},
            }};
            std::memcpy(env.rdram.data() + kDescriptorAddress,
                        mcservInitDescriptors.data(),
                        sizeof(mcservInitDescriptors));
            invokeDma(2u);

            t.Equals(readGuestU32(env.rdram.data(), kMcservResultAddress),
                     0u,
                     "raw MCSERV init should report success");
            t.Equals(readGuestU32(env.rdram.data(), kMcservResultAddress + 4u),
                     0x020Au,
                     "raw MCSERV init should report MCSERV version 0x20A");
            t.Equals(readGuestU32(env.rdram.data(), kMcservResultAddress + 8u),
                     0x020Eu,
                     "raw MCSERV init should report MCMAN version 0x20E");
            t.Equals(readGuestU32(env.rdram.data(), kInboundAddress + 0x08u),
                     kEndCommand,
                     "the MCSERV init completion should emit RPC_END");
            t.Equals(readGuestU32(env.rdram.data(), kInboundAddress + 0x18u),
                     kMcservCallRpc,
                     "the MCSERV init completion should preserve its raw RPC sequence");
            t.Equals(readGuestU32(env.rdram.data(), kInboundAddress + 0x20u),
                     kCallCommand,
                     "the MCSERV init completion should identify RPC_CALL");
        });

        tc.Run("isceSifSetDma and isceSifSetDChain alias the SIF DMA helpers", [](TestCase &t)
        {
            TestEnv env;

            constexpr uint32_t kDescAddr = 0x00020240u;
            constexpr uint32_t kSrcAddr = 0x00020340u;
            constexpr uint32_t kDstAddr = 0x00020440u;

            std::array<uint8_t, 12> payload{};
            for (size_t i = 0; i < payload.size(); ++i)
            {
                payload[i] = static_cast<uint8_t>(0x50u + i);
            }
            std::memcpy(env.rdram.data() + kSrcAddr, payload.data(), payload.size());
            std::memset(env.rdram.data() + kDstAddr, 0, payload.size());

            const Ps2SifDmaTransfer desc{
                kSrcAddr,
                kDstAddr,
                static_cast<int32_t>(payload.size()),
                0};
            std::memcpy(env.rdram.data() + kDescAddr, &desc, sizeof(desc));

            setRegU32(env.ctx, 4, kDescAddr);
            setRegU32(env.ctx, 5, 1u);
            ps2_stubs::isceSifSetDma(env.rdram.data(), &env.ctx, &env.runtime);
            t.IsTrue(getRegS32(env.ctx, 2) > 0, "isceSifSetDma should report a successful transfer id");
            t.IsTrue(std::memcmp(env.rdram.data() + kDstAddr, payload.data(), payload.size()) == 0,
                     "isceSifSetDma should copy transfer payload like sceSifSetDma");

            ps2_stubs::isceSifSetDChain(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), 0, "isceSifSetDChain should mirror sceSifSetDChain");
        });

        tc.Run("sceSifSetDma dispatches enabled DMAC handlers for cause 5", [](TestCase &t)
        {
            TestEnv env;

            constexpr uint32_t kDescAddr = 0x00020300u;
            constexpr uint32_t kSrcAddr = 0x00020400u;
            constexpr uint32_t kDstAddr = 0x00020500u;
            constexpr uint32_t kHandlerAddr = 0x00100000u;
            constexpr uint32_t kHandlerWriteAddr = 0x00020600u;
            constexpr uint32_t kHandlerArg = 0x12345678u;

            g_dmacHandlerWriteAddr = kHandlerWriteAddr;
            g_dmacHandlerValue = 0xCAFEBABEu;
            g_dmacHandlerLastCause = 0u;
            g_dmacHandlerLastArg = 0u;
            env.runtime.registerFunction(kHandlerAddr, &testDmacHandler);

            setRegU32(env.ctx, 4, 5u);
            setRegU32(env.ctx, 5, kHandlerAddr);
            setRegU32(env.ctx, 6, 0u);
            setRegU32(env.ctx, 7, kHandlerArg);
            ps2_syscalls::AddDmacHandler(env.rdram.data(), &env.ctx, &env.runtime);
            const int32_t handlerId = getRegS32(env.ctx, 2);
            t.IsTrue(handlerId > 0, "AddDmacHandler should register a handler");

            setRegU32(env.ctx, 4, 5u);
            ps2_syscalls::EnableDmac(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), 0, "EnableDmac should succeed");

            std::array<uint8_t, 16> payload{};
            for (size_t i = 0; i < payload.size(); ++i)
            {
                payload[i] = static_cast<uint8_t>(0x40u + i);
            }
            std::memcpy(env.rdram.data() + kSrcAddr, payload.data(), payload.size());

            const Ps2SifDmaTransfer desc{
                kSrcAddr,
                kDstAddr,
                static_cast<int32_t>(payload.size()),
                0};
            std::memcpy(env.rdram.data() + kDescAddr, &desc, sizeof(desc));

            setRegU32(env.ctx, 4, kDescAddr);
            setRegU32(env.ctx, 5, 1u);
            ps2_stubs::sceSifSetDma(env.rdram.data(), &env.ctx, &env.runtime);

            t.IsTrue(getRegS32(env.ctx, 2) > 0, "sceSifSetDma should still report success");
            t.Equals(readGuestU32(env.rdram.data(), kHandlerWriteAddr), g_dmacHandlerValue,
                     "sceSifSetDma should invoke registered DMAC handlers");
            t.Equals(g_dmacHandlerLastCause, 5u, "DMAC handler should observe cause 5");
            t.Equals(g_dmacHandlerLastArg, kHandlerArg, "DMAC handler should receive registered argument");
        });

        tc.Run("sceSifSetDma acknowledges DTX work-buffer transfers by advancing the EE footer ticket", [](TestCase &t)
        {
            TestEnv env;
            configureProfile(env, "slus_201.84");

            constexpr uint32_t kClientAddr = 0x0002D000u;
            constexpr uint32_t kDtxSid = 0x7D000000u;
            constexpr uint32_t kSendAddr = 0x0002D100u;
            constexpr uint32_t kRecvAddr = 0x0002D200u;
            constexpr uint32_t kDescAddr = 0x0002D300u;
            constexpr uint32_t kEeWorkAddr = 0x0002D400u;
            constexpr uint32_t kIopWorkAddr = 0x0002D800u;
            constexpr uint32_t kDtxId = 3u;
            constexpr uint32_t kWorkLen = 0x100u;
            constexpr uint32_t kFooterTicketAddr = kEeWorkAddr + kWorkLen - sizeof(uint32_t);

            ps2_syscalls::SifInitRpc(env.rdram.data(), &env.ctx, &env.runtime);

            setRegU32(env.ctx, 4, kClientAddr);
            setRegU32(env.ctx, 5, kDtxSid);
            setRegU32(env.ctx, 6, 0u);
            ps2_syscalls::SifBindRpc(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), KE_OK, "SifBindRpc should succeed for the DTX sid");

            writeGuestU32(env.rdram.data(), kSendAddr + 0x00u, kDtxId);
            writeGuestU32(env.rdram.data(), kSendAddr + 0x04u, kEeWorkAddr);
            writeGuestU32(env.rdram.data(), kSendAddr + 0x08u, kIopWorkAddr);
            writeGuestU32(env.rdram.data(), kSendAddr + 0x0Cu, kWorkLen);
            writeGuestU32(env.rdram.data(), kRecvAddr + 0x00u, 0u);

            setRegU32(env.ctx, 4, kClientAddr);
            setRegU32(env.ctx, 5, 2u);
            setRegU32(env.ctx, 6, 0u);
            setRegU32(env.ctx, 7, kSendAddr);
            setRegU32(env.ctx, 8, 16u);
            setRegU32(env.ctx, 9, kRecvAddr);
            setRegU32(env.ctx, 10, 4u);
            setRegU32(env.ctx, 11, 0u);
            ps2_syscalls::SifCallRpc(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), KE_OK, "SifCallRpc should create the DTX transport");
            t.IsTrue(readGuestU32(env.rdram.data(), kRecvAddr) != 0u, "DTX create should return a remote handle");

            std::memset(env.rdram.data() + kEeWorkAddr, 0x44, kWorkLen);
            std::memset(env.rdram.data() + kIopWorkAddr, 0x00, kWorkLen);
            writeGuestU32(env.rdram.data(), kFooterTicketAddr, 1u);

            const Ps2SifDmaTransfer desc{
                kEeWorkAddr,
                kIopWorkAddr,
                static_cast<int32_t>(kWorkLen),
                0};
            std::memcpy(env.rdram.data() + kDescAddr, &desc, sizeof(desc));

            setRegU32(env.ctx, 4, kDescAddr);
            setRegU32(env.ctx, 5, 1u);
            ps2_stubs::sceSifSetDma(env.rdram.data(), &env.ctx, &env.runtime);
            t.IsTrue(getRegS32(env.ctx, 2) > 0, "sceSifSetDma should succeed for the DTX transfer");

            t.Equals(readGuestU32(env.rdram.data(), kFooterTicketAddr), 2u,
                     "sceSifSetDma should advance the EE footer ticket so DTX clears wait_flag");
        });

        tc.Run("sceSifSetDma applies SJX DTX payloads into the emulated SJRMT data ring", [](TestCase &t)
        {
            TestEnv env;
            configureProfile(env, "slus_201.84");

            constexpr uint32_t kClientAddr = 0x0002E000u;
            constexpr uint32_t kDtxSid = 0x7D000000u;
            constexpr uint32_t kRecvAddr = 0x0002E100u;
            constexpr uint32_t kSendAddr = 0x0002E200u;
            constexpr uint32_t kDescAddr = 0x0002E300u;
            constexpr uint32_t kEeWorkAddr = 0x0002E400u;
            constexpr uint32_t kIopWorkAddr = 0x0002E800u;
            constexpr uint32_t kRingAddr = 0x0002EC00u;
            constexpr uint32_t kChunkDataAddr = 0x0002ED00u;
            constexpr uint32_t kWorkLen = 0x100u;
            constexpr uint32_t kChunkLen = 8u;

            ps2_syscalls::SifInitRpc(env.rdram.data(), &env.ctx, &env.runtime);

            setRegU32(env.ctx, 4, kClientAddr);
            setRegU32(env.ctx, 5, kDtxSid);
            setRegU32(env.ctx, 6, 0u);
            ps2_syscalls::SifBindRpc(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), KE_OK, "SifBindRpc should bind the DTX sid");

            writeGuestU32(env.rdram.data(), kSendAddr + 0x00u, 1u);
            writeGuestU32(env.rdram.data(), kSendAddr + 0x04u, kRingAddr);
            writeGuestU32(env.rdram.data(), kSendAddr + 0x08u, kWorkLen);
            setRegU32(env.ctx, 4, kClientAddr);
            setRegU32(env.ctx, 5, 0x422u);
            setRegU32(env.ctx, 6, 0u);
            setRegU32(env.ctx, 7, kSendAddr);
            setRegU32(env.ctx, 8, 12u);
            setRegU32(env.ctx, 9, kRecvAddr);
            setRegU32(env.ctx, 10, 4u);
            setRegU32(env.ctx, 11, 0u);
            ps2_syscalls::SifCallRpc(env.rdram.data(), &env.ctx, &env.runtime);
            const uint32_t sjrmtHandle = readGuestU32(env.rdram.data(), kRecvAddr);
            t.IsTrue(sjrmtHandle != 0u, "SJRMT_UNI_CREATE should return a handle");

            writeGuestU32(env.rdram.data(), kSendAddr + 0x00u, 0u);
            writeGuestU32(env.rdram.data(), kSendAddr + 0x04u, sjrmtHandle);
            writeGuestU32(env.rdram.data(), kSendAddr + 0x08u, 1u);
            writeGuestU32(env.rdram.data(), kSendAddr + 0x0Cu, 0x12345678u);
            setRegU32(env.ctx, 4, kClientAddr);
            setRegU32(env.ctx, 5, 0x400u);
            setRegU32(env.ctx, 6, 0u);
            setRegU32(env.ctx, 7, kSendAddr);
            setRegU32(env.ctx, 8, 16u);
            setRegU32(env.ctx, 9, kRecvAddr);
            setRegU32(env.ctx, 10, 4u);
            setRegU32(env.ctx, 11, 0u);
            ps2_syscalls::SifCallRpc(env.rdram.data(), &env.ctx, &env.runtime);
            const uint32_t sjxHandle = readGuestU32(env.rdram.data(), kRecvAddr);
            t.IsTrue(sjxHandle != 0u, "SJX_CREATE should return a handle");

            writeGuestU32(env.rdram.data(), kSendAddr + 0x00u, 0u);
            writeGuestU32(env.rdram.data(), kSendAddr + 0x04u, kEeWorkAddr);
            writeGuestU32(env.rdram.data(), kSendAddr + 0x08u, kIopWorkAddr);
            writeGuestU32(env.rdram.data(), kSendAddr + 0x0Cu, kWorkLen);
            setRegU32(env.ctx, 4, kClientAddr);
            setRegU32(env.ctx, 5, 2u);
            setRegU32(env.ctx, 6, 0u);
            setRegU32(env.ctx, 7, kSendAddr);
            setRegU32(env.ctx, 8, 16u);
            setRegU32(env.ctx, 9, kRecvAddr);
            setRegU32(env.ctx, 10, 4u);
            setRegU32(env.ctx, 11, 0u);
            ps2_syscalls::SifCallRpc(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), KE_OK, "DTX create should succeed");

            std::memset(env.rdram.data() + kEeWorkAddr, 0, kWorkLen);
            std::memset(env.rdram.data() + kIopWorkAddr, 0, kWorkLen);
            std::memset(env.rdram.data() + kRingAddr, 0, kWorkLen);
            for (uint32_t i = 0; i < kChunkLen; ++i)
            {
                env.rdram[kChunkDataAddr + i] = static_cast<uint8_t>(0xA0u + i);
            }

            writeGuestU32(env.rdram.data(), kEeWorkAddr + 0x00u, 1u);
            env.rdram[kEeWorkAddr + 0x10u] = 0u;
            env.rdram[kEeWorkAddr + 0x11u] = 1u;
            std::memcpy(env.rdram.data() + kEeWorkAddr + 0x12u, "\0\0", 2u);
            writeGuestU32(env.rdram.data(), kEeWorkAddr + 0x14u, sjxHandle);
            writeGuestU32(env.rdram.data(), kEeWorkAddr + 0x18u, kChunkDataAddr);
            writeGuestU32(env.rdram.data(), kEeWorkAddr + 0x1Cu, kChunkLen);
            writeGuestU32(env.rdram.data(), kEeWorkAddr + kWorkLen - sizeof(uint32_t), 1u);

            const Ps2SifDmaTransfer desc{
                kEeWorkAddr,
                kIopWorkAddr,
                static_cast<int32_t>(kWorkLen),
                0};
            std::memcpy(env.rdram.data() + kDescAddr, &desc, sizeof(desc));

            setRegU32(env.ctx, 4, kDescAddr);
            setRegU32(env.ctx, 5, 1u);
            ps2_stubs::sceSifSetDma(env.rdram.data(), &env.ctx, &env.runtime);
            t.IsTrue(getRegS32(env.ctx, 2) > 0, "sceSifSetDma should succeed for the SJX transport");
            t.Equals(env.rdram[kEeWorkAddr + 0x11u], static_cast<uint8_t>(0u),
                     "SJX DMA ack should rewrite the response line to room so EE recycles the chunk");
            t.Equals(readGuestU32(env.rdram.data(), kEeWorkAddr + kWorkLen - sizeof(uint32_t)), 2u,
                     "SJX DMA ack should still advance the EE footer ticket");

            writeGuestU32(env.rdram.data(), kSendAddr + 0x00u, sjrmtHandle);
            writeGuestU32(env.rdram.data(), kSendAddr + 0x04u, 1u);
            setRegU32(env.ctx, 4, kClientAddr);
            setRegU32(env.ctx, 5, 0x429u);
            setRegU32(env.ctx, 6, 0u);
            setRegU32(env.ctx, 7, kSendAddr);
            setRegU32(env.ctx, 8, 8u);
            setRegU32(env.ctx, 9, kRecvAddr);
            setRegU32(env.ctx, 10, 4u);
            setRegU32(env.ctx, 11, 0u);
            ps2_syscalls::SifCallRpc(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(readGuestU32(env.rdram.data(), kRecvAddr), kChunkLen,
                     "SJX DMA should make SJRMT report available data");
            t.IsTrue(std::memcmp(env.rdram.data() + kRingAddr, env.rdram.data() + kChunkDataAddr, kChunkLen) == 0,
                     "SJX DMA should copy the chunk payload into the emulated SJRMT ring");
        });

        tc.Run("sceSifSetDma recognizes SJX DTX payloads from rotated EE work buffers", [](TestCase &t)
        {
            TestEnv env;
            configureProfile(env, "slus_201.84");

            constexpr uint32_t kClientAddr = 0x00031000u;
            constexpr uint32_t kDtxSid = 0x7D000000u;
            constexpr uint32_t kRecvAddr = 0x00031100u;
            constexpr uint32_t kSendAddr = 0x00031200u;
            constexpr uint32_t kDescAddr = 0x00031300u;
            constexpr uint32_t kRegisteredEeWorkAddr = 0x00031400u;
            constexpr uint32_t kRegisteredIopWorkAddr = 0x00031800u;
            constexpr uint32_t kAltEeWorkAddr = 0x00031C00u;
            constexpr uint32_t kAltIopWorkAddr = 0x00032000u;
            constexpr uint32_t kRingAddr = 0x00032400u;
            constexpr uint32_t kChunkDataAddr = 0x00032500u;
            constexpr uint32_t kRegisteredWorkLen = 0x100u;
            constexpr uint32_t kAltWorkLen = 0x180u;
            constexpr uint32_t kChunkLen = 12u;

            ps2_syscalls::SifInitRpc(env.rdram.data(), &env.ctx, &env.runtime);

            setRegU32(env.ctx, 4, kClientAddr);
            setRegU32(env.ctx, 5, kDtxSid);
            setRegU32(env.ctx, 6, 0u);
            ps2_syscalls::SifBindRpc(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), KE_OK, "SifBindRpc should bind the DTX sid");

            writeGuestU32(env.rdram.data(), kSendAddr + 0x00u, 1u);
            writeGuestU32(env.rdram.data(), kSendAddr + 0x04u, kRingAddr);
            writeGuestU32(env.rdram.data(), kSendAddr + 0x08u, kRegisteredWorkLen);
            setRegU32(env.ctx, 4, kClientAddr);
            setRegU32(env.ctx, 5, 0x422u);
            setRegU32(env.ctx, 6, 0u);
            setRegU32(env.ctx, 7, kSendAddr);
            setRegU32(env.ctx, 8, 12u);
            setRegU32(env.ctx, 9, kRecvAddr);
            setRegU32(env.ctx, 10, 4u);
            setRegU32(env.ctx, 11, 0u);
            ps2_syscalls::SifCallRpc(env.rdram.data(), &env.ctx, &env.runtime);
            const uint32_t sjrmtHandle = readGuestU32(env.rdram.data(), kRecvAddr);
            t.IsTrue(sjrmtHandle != 0u, "SJRMT_UNI_CREATE should return a handle");

            writeGuestU32(env.rdram.data(), kSendAddr + 0x00u, 0u);
            writeGuestU32(env.rdram.data(), kSendAddr + 0x04u, sjrmtHandle);
            writeGuestU32(env.rdram.data(), kSendAddr + 0x08u, 1u);
            writeGuestU32(env.rdram.data(), kSendAddr + 0x0Cu, 0x87654321u);
            setRegU32(env.ctx, 4, kClientAddr);
            setRegU32(env.ctx, 5, 0x400u);
            setRegU32(env.ctx, 6, 0u);
            setRegU32(env.ctx, 7, kSendAddr);
            setRegU32(env.ctx, 8, 16u);
            setRegU32(env.ctx, 9, kRecvAddr);
            setRegU32(env.ctx, 10, 4u);
            setRegU32(env.ctx, 11, 0u);
            ps2_syscalls::SifCallRpc(env.rdram.data(), &env.ctx, &env.runtime);
            const uint32_t sjxHandle = readGuestU32(env.rdram.data(), kRecvAddr);
            t.IsTrue(sjxHandle != 0u, "SJX_CREATE should return a handle");

            writeGuestU32(env.rdram.data(), kSendAddr + 0x00u, 0u);
            writeGuestU32(env.rdram.data(), kSendAddr + 0x04u, kRegisteredEeWorkAddr);
            writeGuestU32(env.rdram.data(), kSendAddr + 0x08u, kRegisteredIopWorkAddr);
            writeGuestU32(env.rdram.data(), kSendAddr + 0x0Cu, kRegisteredWorkLen);
            setRegU32(env.ctx, 4, kClientAddr);
            setRegU32(env.ctx, 5, 2u);
            setRegU32(env.ctx, 6, 0u);
            setRegU32(env.ctx, 7, kSendAddr);
            setRegU32(env.ctx, 8, 16u);
            setRegU32(env.ctx, 9, kRecvAddr);
            setRegU32(env.ctx, 10, 4u);
            setRegU32(env.ctx, 11, 0u);
            ps2_syscalls::SifCallRpc(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), KE_OK, "DTX create should succeed");

            std::memset(env.rdram.data() + kRegisteredEeWorkAddr, 0, kRegisteredWorkLen);
            std::memset(env.rdram.data() + kRegisteredIopWorkAddr, 0, kRegisteredWorkLen);
            std::memset(env.rdram.data() + kAltEeWorkAddr, 0, kAltWorkLen);
            std::memset(env.rdram.data() + kAltIopWorkAddr, 0, kAltWorkLen);
            std::memset(env.rdram.data() + kRingAddr, 0, kRegisteredWorkLen);
            for (uint32_t i = 0; i < kChunkLen; ++i)
            {
                env.rdram[kChunkDataAddr + i] = static_cast<uint8_t>(0xC0u + i);
            }

            writeGuestU32(env.rdram.data(), kAltEeWorkAddr + 0x00u, 1u);
            env.rdram[kAltEeWorkAddr + 0x10u] = 0u;
            env.rdram[kAltEeWorkAddr + 0x11u] = 1u;
            std::memcpy(env.rdram.data() + kAltEeWorkAddr + 0x12u, "\0\0", 2u);
            writeGuestU32(env.rdram.data(), kAltEeWorkAddr + 0x14u, sjxHandle);
            writeGuestU32(env.rdram.data(), kAltEeWorkAddr + 0x18u, kChunkDataAddr);
            writeGuestU32(env.rdram.data(), kAltEeWorkAddr + 0x1Cu, kChunkLen);
            writeGuestU32(env.rdram.data(), kAltEeWorkAddr + kAltWorkLen - sizeof(uint32_t), 9u);

            const Ps2SifDmaTransfer desc{
                kAltEeWorkAddr,
                kAltIopWorkAddr,
                static_cast<int32_t>(kAltWorkLen),
                0};
            std::memcpy(env.rdram.data() + kDescAddr, &desc, sizeof(desc));

            setRegU32(env.ctx, 4, kDescAddr);
            setRegU32(env.ctx, 5, 1u);
            ps2_stubs::sceSifSetDma(env.rdram.data(), &env.ctx, &env.runtime);
            t.IsTrue(getRegS32(env.ctx, 2) > 0, "sceSifSetDma should succeed for the rotated SJX transport");
            t.Equals(env.rdram[kAltEeWorkAddr + 0x11u], static_cast<uint8_t>(0u),
                     "rotated SJX DMA ack should rewrite the response line to room");
            t.Equals(readGuestU32(env.rdram.data(), kAltEeWorkAddr + kAltWorkLen - sizeof(uint32_t)), 10u,
                     "rotated SJX DMA ack should advance the alternate EE footer ticket");

            writeGuestU32(env.rdram.data(), kSendAddr + 0x00u, sjrmtHandle);
            writeGuestU32(env.rdram.data(), kSendAddr + 0x04u, 1u);
            setRegU32(env.ctx, 4, kClientAddr);
            setRegU32(env.ctx, 5, 0x429u);
            setRegU32(env.ctx, 6, 0u);
            setRegU32(env.ctx, 7, kSendAddr);
            setRegU32(env.ctx, 8, 8u);
            setRegU32(env.ctx, 9, kRecvAddr);
            setRegU32(env.ctx, 10, 4u);
            setRegU32(env.ctx, 11, 0u);
            ps2_syscalls::SifCallRpc(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(readGuestU32(env.rdram.data(), kRecvAddr), kChunkLen,
                     "rotated SJX DMA should make SJRMT report available data");
            t.IsTrue(std::memcmp(env.rdram.data() + kRingAddr, env.rdram.data() + kChunkDataAddr, kChunkLen) == 0,
                     "rotated SJX DMA should copy the chunk payload into the emulated SJRMT ring");
        });

        tc.Run("sceSifSetDma lets active PS2RNA playback drain emulated SJRMT data", [](TestCase &t)
        {
            TestEnv env;
            configureProfile(env, "slus_201.84");

            constexpr uint32_t kClientAddr = 0x0002F000u;
            constexpr uint32_t kDtxSid = 0x7D000000u;
            constexpr uint32_t kRecvAddr = 0x0002F100u;
            constexpr uint32_t kSendAddr = 0x0002F200u;
            constexpr uint32_t kDesc0Addr = 0x0002F300u;
            constexpr uint32_t kDesc1Addr = 0x0002F320u;
            constexpr uint32_t kEeWork0Addr = 0x0002F400u;
            constexpr uint32_t kIopWork0Addr = 0x0002F800u;
            constexpr uint32_t kEeWork1Addr = 0x0002FC00u;
            constexpr uint32_t kIopWork1Addr = 0x00030000u;
            constexpr uint32_t kRingAddr = 0x00030400u;
            constexpr uint32_t kChunkDataAddr = 0x00030500u;
            constexpr uint32_t kWorkLen = 0x100u;
            constexpr uint32_t kChunkLen = 8u;

            ps2_syscalls::SifInitRpc(env.rdram.data(), &env.ctx, &env.runtime);

            setRegU32(env.ctx, 4, kClientAddr);
            setRegU32(env.ctx, 5, kDtxSid);
            setRegU32(env.ctx, 6, 0u);
            ps2_syscalls::SifBindRpc(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), KE_OK, "SifBindRpc should bind the DTX sid");

            writeGuestU32(env.rdram.data(), kSendAddr + 0x00u, 1u);
            writeGuestU32(env.rdram.data(), kSendAddr + 0x04u, kRingAddr);
            writeGuestU32(env.rdram.data(), kSendAddr + 0x08u, kWorkLen);
            setRegU32(env.ctx, 4, kClientAddr);
            setRegU32(env.ctx, 5, 0x422u);
            setRegU32(env.ctx, 6, 0u);
            setRegU32(env.ctx, 7, kSendAddr);
            setRegU32(env.ctx, 8, 12u);
            setRegU32(env.ctx, 9, kRecvAddr);
            setRegU32(env.ctx, 10, 4u);
            setRegU32(env.ctx, 11, 0u);
            ps2_syscalls::SifCallRpc(env.rdram.data(), &env.ctx, &env.runtime);
            const uint32_t sjrmtHandle = readGuestU32(env.rdram.data(), kRecvAddr);
            t.IsTrue(sjrmtHandle != 0u, "SJRMT_UNI_CREATE should return a handle");

            writeGuestU32(env.rdram.data(), kSendAddr + 0x00u, 0u);
            writeGuestU32(env.rdram.data(), kSendAddr + 0x04u, sjrmtHandle);
            writeGuestU32(env.rdram.data(), kSendAddr + 0x08u, 1u);
            writeGuestU32(env.rdram.data(), kSendAddr + 0x0Cu, 0xCAFEBABEu);
            setRegU32(env.ctx, 4, kClientAddr);
            setRegU32(env.ctx, 5, 0x400u);
            setRegU32(env.ctx, 6, 0u);
            setRegU32(env.ctx, 7, kSendAddr);
            setRegU32(env.ctx, 8, 16u);
            setRegU32(env.ctx, 9, kRecvAddr);
            setRegU32(env.ctx, 10, 4u);
            setRegU32(env.ctx, 11, 0u);
            ps2_syscalls::SifCallRpc(env.rdram.data(), &env.ctx, &env.runtime);
            const uint32_t sjxHandle = readGuestU32(env.rdram.data(), kRecvAddr);
            t.IsTrue(sjxHandle != 0u, "SJX_CREATE should return a handle");

            writeGuestU32(env.rdram.data(), kSendAddr + 0x00u, 1u);
            writeGuestU32(env.rdram.data(), kSendAddr + 0x04u, 0u);
            writeGuestU32(env.rdram.data(), kSendAddr + 0x08u, sjrmtHandle);
            writeGuestU32(env.rdram.data(), kSendAddr + 0x0Cu, 0u);
            setRegU32(env.ctx, 4, kClientAddr);
            setRegU32(env.ctx, 5, 0x408u);
            setRegU32(env.ctx, 6, 0u);
            setRegU32(env.ctx, 7, kSendAddr);
            setRegU32(env.ctx, 8, 16u);
            setRegU32(env.ctx, 9, kRecvAddr);
            setRegU32(env.ctx, 10, 4u);
            setRegU32(env.ctx, 11, 0u);
            ps2_syscalls::SifCallRpc(env.rdram.data(), &env.ctx, &env.runtime);
            const uint32_t ps2RnaHandle = readGuestU32(env.rdram.data(), kRecvAddr);
            t.IsTrue(ps2RnaHandle != 0u, "PS2RNA_CREATE should return a handle");

            writeGuestU32(env.rdram.data(), kSendAddr + 0x00u, 0u);
            writeGuestU32(env.rdram.data(), kSendAddr + 0x04u, kEeWork0Addr);
            writeGuestU32(env.rdram.data(), kSendAddr + 0x08u, kIopWork0Addr);
            writeGuestU32(env.rdram.data(), kSendAddr + 0x0Cu, kWorkLen);
            setRegU32(env.ctx, 4, kClientAddr);
            setRegU32(env.ctx, 5, 2u);
            setRegU32(env.ctx, 6, 0u);
            setRegU32(env.ctx, 7, kSendAddr);
            setRegU32(env.ctx, 8, 16u);
            setRegU32(env.ctx, 9, kRecvAddr);
            setRegU32(env.ctx, 10, 4u);
            setRegU32(env.ctx, 11, 0u);
            ps2_syscalls::SifCallRpc(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), KE_OK, "DTX create should succeed for SJX transport");

            writeGuestU32(env.rdram.data(), kSendAddr + 0x00u, 1u);
            writeGuestU32(env.rdram.data(), kSendAddr + 0x04u, kEeWork1Addr);
            writeGuestU32(env.rdram.data(), kSendAddr + 0x08u, kIopWork1Addr);
            writeGuestU32(env.rdram.data(), kSendAddr + 0x0Cu, kWorkLen);
            setRegU32(env.ctx, 4, kClientAddr);
            setRegU32(env.ctx, 5, 2u);
            setRegU32(env.ctx, 6, 0u);
            setRegU32(env.ctx, 7, kSendAddr);
            setRegU32(env.ctx, 8, 16u);
            setRegU32(env.ctx, 9, kRecvAddr);
            setRegU32(env.ctx, 10, 4u);
            setRegU32(env.ctx, 11, 0u);
            ps2_syscalls::SifCallRpc(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), KE_OK, "DTX create should succeed for PS2RNA transport");

            std::memset(env.rdram.data() + kEeWork0Addr, 0, kWorkLen);
            std::memset(env.rdram.data() + kIopWork0Addr, 0, kWorkLen);
            std::memset(env.rdram.data() + kEeWork1Addr, 0, kWorkLen);
            std::memset(env.rdram.data() + kIopWork1Addr, 0, kWorkLen);
            std::memset(env.rdram.data() + kRingAddr, 0, kWorkLen);
            for (uint32_t i = 0; i < kChunkLen; ++i)
            {
                env.rdram[kChunkDataAddr + i] = static_cast<uint8_t>(0xB0u + i);
            }

            writeGuestU32(env.rdram.data(), kEeWork1Addr + 0x00u, 1u);
            writeGuestU32(env.rdram.data(), kEeWork1Addr + 0x10u, 2u);
            writeGuestU32(env.rdram.data(), kEeWork1Addr + 0x14u, ps2RnaHandle);
            writeGuestU32(env.rdram.data(), kEeWork1Addr + 0x18u, 1u);
            writeGuestU32(env.rdram.data(), kEeWork1Addr + 0x1Cu, 0u);
            writeGuestU32(env.rdram.data(), kEeWork1Addr + kWorkLen - sizeof(uint32_t), 1u);

            const Ps2SifDmaTransfer desc1{
                kEeWork1Addr,
                kIopWork1Addr,
                static_cast<int32_t>(kWorkLen),
                0};
            std::memcpy(env.rdram.data() + kDesc1Addr, &desc1, sizeof(desc1));

            setRegU32(env.ctx, 4, kDesc1Addr);
            setRegU32(env.ctx, 5, 1u);
            ps2_stubs::sceSifSetDma(env.rdram.data(), &env.ctx, &env.runtime);
            t.IsTrue(getRegS32(env.ctx, 2) > 0, "sceSifSetDma should succeed for the PS2RNA control transport");
            t.Equals(readGuestU32(env.rdram.data(), kEeWork1Addr + kWorkLen - sizeof(uint32_t)), 2u,
                     "PS2RNA control DMA should advance the EE footer ticket");

            writeGuestU32(env.rdram.data(), kEeWork0Addr + 0x00u, 1u);
            env.rdram[kEeWork0Addr + 0x10u] = 0u;
            env.rdram[kEeWork0Addr + 0x11u] = 1u;
            std::memcpy(env.rdram.data() + kEeWork0Addr + 0x12u, "\0\0", 2u);
            writeGuestU32(env.rdram.data(), kEeWork0Addr + 0x14u, sjxHandle);
            writeGuestU32(env.rdram.data(), kEeWork0Addr + 0x18u, kChunkDataAddr);
            writeGuestU32(env.rdram.data(), kEeWork0Addr + 0x1Cu, kChunkLen);
            writeGuestU32(env.rdram.data(), kEeWork0Addr + kWorkLen - sizeof(uint32_t), 1u);

            const Ps2SifDmaTransfer desc0{
                kEeWork0Addr,
                kIopWork0Addr,
                static_cast<int32_t>(kWorkLen),
                0};
            std::memcpy(env.rdram.data() + kDesc0Addr, &desc0, sizeof(desc0));

            setRegU32(env.ctx, 4, kDesc0Addr);
            setRegU32(env.ctx, 5, 1u);
            ps2_stubs::sceSifSetDma(env.rdram.data(), &env.ctx, &env.runtime);
            t.IsTrue(getRegS32(env.ctx, 2) > 0, "sceSifSetDma should succeed for the SJX transport");
            t.Equals(env.rdram[kEeWork0Addr + 0x11u], static_cast<uint8_t>(0u),
                     "SJX DMA ack should still rewrite the response line to room");

            writeGuestU32(env.rdram.data(), kSendAddr + 0x00u, sjrmtHandle);
            writeGuestU32(env.rdram.data(), kSendAddr + 0x04u, 1u);
            setRegU32(env.ctx, 4, kClientAddr);
            setRegU32(env.ctx, 5, 0x429u);
            setRegU32(env.ctx, 6, 0u);
            setRegU32(env.ctx, 7, kSendAddr);
            setRegU32(env.ctx, 8, 8u);
            setRegU32(env.ctx, 9, kRecvAddr);
            setRegU32(env.ctx, 10, 4u);
            setRegU32(env.ctx, 11, 0u);
            ps2_syscalls::SifCallRpc(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(readGuestU32(env.rdram.data(), kRecvAddr), 0u,
                     "active PS2RNA playback should drain remote SJRMT data instead of leaving it queued forever");

            writeGuestU32(env.rdram.data(), kSendAddr + 0x00u, sjrmtHandle);
            writeGuestU32(env.rdram.data(), kSendAddr + 0x04u, 0u);
            setRegU32(env.ctx, 4, kClientAddr);
            setRegU32(env.ctx, 5, 0x429u);
            setRegU32(env.ctx, 6, 0u);
            setRegU32(env.ctx, 7, kSendAddr);
            setRegU32(env.ctx, 8, 8u);
            setRegU32(env.ctx, 9, kRecvAddr);
            setRegU32(env.ctx, 10, 4u);
            setRegU32(env.ctx, 11, 0u);
            ps2_syscalls::SifCallRpc(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(readGuestU32(env.rdram.data(), kRecvAddr), kWorkLen,
                     "drained PS2RNA playback should return remote SJRMT room to full capacity");
        });

        tc.Run("resetSifState seeds boot-ready SIF registers", [](TestCase &t)
        {
            TestEnv env;

            auto getReg = [&](uint32_t reg) -> uint32_t
            {
                setRegU32(env.ctx, 4, reg);
                ps2_stubs::sceSifGetReg(env.rdram.data(), &env.ctx, &env.runtime);
                return ::getRegU32(&env.ctx, 2);
            };

            t.Equals(getReg(0x4u), 0x00020000u, "SIF boot status register should expose ready bit by default");
            t.Equals(getReg(0x80000000u), 0u, "SIF main-address register should default to zero");
            t.Equals(getReg(0x80000001u), 0u, "SIF sub-address register should default to zero");
            t.Equals(getReg(0x80000002u), 0u, "SIF mscom register should default to zero");
        });

        tc.Run("sceSifExitCmd restores default boot-ready SIF registers", [](TestCase &t)
        {
            TestEnv env;

            setRegU32(env.ctx, 4, 0x4u);
            setRegU32(env.ctx, 5, 0x12340000u);
            ps2_stubs::sceSifSetReg(env.rdram.data(), &env.ctx, &env.runtime);

            setRegU32(env.ctx, 4, 0x80000002u);
            setRegU32(env.ctx, 5, 0x89ABCDEFu);
            ps2_stubs::sceSifSetReg(env.rdram.data(), &env.ctx, &env.runtime);

            ps2_stubs::sceSifExitCmd(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), 0, "sceSifExitCmd should succeed");

            auto getReg = [&](uint32_t reg) -> uint32_t
            {
                setRegU32(env.ctx, 4, reg);
                ps2_stubs::sceSifGetReg(env.rdram.data(), &env.ctx, &env.runtime);
                return ::getRegU32(&env.ctx, 2);
            };

            t.Equals(getReg(0x4u), 0x00020000u, "sceSifExitCmd should restore the boot-ready status bit");
            t.Equals(getReg(0x80000002u), 0u, "sceSifExitCmd should clear transient mscom state");
        });

        tc.Run("sceSifSetDma rejects invalid descriptors without partial writes", [](TestCase &t)
        {
            TestEnv env;

            constexpr uint32_t kDescAddr = 0x00021000u;
            constexpr uint32_t kSrcA = 0x00021100u;
            constexpr uint32_t kDstA = 0x00021200u;
            constexpr uint32_t kSrcB = 0x00021300u;
            constexpr uint32_t kInvalidDstB = 0xE0000100u; // unsupported guest segment

            std::array<uint8_t, 8> payloadA{};
            for (size_t i = 0; i < payloadA.size(); ++i)
            {
                payloadA[i] = static_cast<uint8_t>(0x70u + i);
            }
            std::array<uint8_t, 8> payloadB{};
            for (size_t i = 0; i < payloadB.size(); ++i)
            {
                payloadB[i] = static_cast<uint8_t>(0x90u + i);
            }

            std::memcpy(env.rdram.data() + kSrcA, payloadA.data(), payloadA.size());
            std::memcpy(env.rdram.data() + kSrcB, payloadB.data(), payloadB.size());
            std::memset(env.rdram.data() + kDstA, 0x5Au, payloadA.size());

            const Ps2SifDmaTransfer descs[2] = {
                {kSrcA, kDstA, static_cast<int32_t>(payloadA.size()), 0},
                {kSrcB, kInvalidDstB, static_cast<int32_t>(payloadB.size()), 0}};
            std::memcpy(env.rdram.data() + kDescAddr, descs, sizeof(descs));

            setRegU32(env.ctx, 4, kDescAddr);
            setRegU32(env.ctx, 5, 2u);
            ps2_stubs::sceSifSetDma(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), 0, "sceSifSetDma should fail when any descriptor is invalid");

            const std::array<uint8_t, 8> expectedUnchanged{
                0x5A, 0x5A, 0x5A, 0x5A, 0x5A, 0x5A, 0x5A, 0x5A};
            t.IsTrue(std::memcmp(env.rdram.data() + kDstA, expectedUnchanged.data(), expectedUnchanged.size()) == 0,
                     "failed multi-descriptor sceSifSetDma should not partially write earlier descriptors");
        });

        tc.Run("sceSifSetDma enforces descriptor count limit", [](TestCase &t)
        {
            TestEnv env;
            constexpr uint32_t kDescAddr = 0x00022000u;

            setRegU32(env.ctx, 4, kDescAddr);
            setRegU32(env.ctx, 5, 33u);
            ps2_stubs::sceSifSetDma(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), 0, "sceSifSetDma should reject count > 32");
        });

        tc.Run("sceSifGetOtherData copies payload and writes receive metadata", [](TestCase &t)
        {
            TestEnv env;

            constexpr uint32_t kRdAddr = 0x00023000u;
            constexpr uint32_t kSrcAddr = 0x00023100u;
            constexpr uint32_t kDstAddr = 0x00023200u;
            constexpr uint32_t kSize = 20u;

            std::array<uint8_t, kSize> payload{};
            for (size_t i = 0; i < payload.size(); ++i)
            {
                payload[i] = static_cast<uint8_t>((i * 7u) & 0xFFu);
            }
            std::memcpy(env.rdram.data() + kSrcAddr, payload.data(), payload.size());
            std::memset(env.rdram.data() + kDstAddr, 0, payload.size());
            std::memset(env.rdram.data() + kRdAddr, 0, sizeof(SifRpcReceiveData));

            setRegU32(env.ctx, 4, kRdAddr);
            setRegU32(env.ctx, 5, kSrcAddr);
            setRegU32(env.ctx, 6, kDstAddr);
            setRegU32(env.ctx, 7, kSize);
            ps2_stubs::sceSifGetOtherData(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), 0, "sceSifGetOtherData should succeed for valid transfer");

            t.IsTrue(std::memcmp(env.rdram.data() + kDstAddr, payload.data(), payload.size()) == 0,
                     "sceSifGetOtherData should copy payload");

            const SifRpcReceiveData rd = *reinterpret_cast<const SifRpcReceiveData *>(env.rdram.data() + kRdAddr);
            t.Equals(rd.src, kSrcAddr, "receive metadata src should be populated");
            t.Equals(rd.dest, kDstAddr, "receive metadata dest should be populated");
            t.Equals(static_cast<uint32_t>(rd.size), kSize, "receive metadata size should be populated");
        });

        tc.Run("sceSifGetOtherData preserves live sound-status sums when compat backfill is enabled", [](TestCase &t)
        {
            TestEnv env;
            configureProfile(env, "slus_201.84");

            constexpr uint32_t kRdAddr = 0x00023300u;
            constexpr uint32_t kDstAddr = 0x00023400u;
            constexpr uint32_t kSize = 0x42u;
            constexpr uint32_t kPrimarySeCheckAddr = 0x01E0EF10u;
            constexpr uint32_t kPrimaryMidiCheckAddr = 0x01E0EF20u;
            constexpr uint32_t kMidiSumOffset = 0x1Eu;
            constexpr uint32_t kSeSumOffset = 0x26u;
            constexpr uint32_t kBank = 1u;

            constexpr uint32_t kClientAddr = 0x00023500u;
            constexpr uint32_t kRecvAddr = 0x00023600u;
            constexpr uint32_t kSid = 1u;

            ps2_syscalls::SifInitRpc(env.rdram.data(), &env.ctx, &env.runtime);
            setRegU32(env.ctx, 4, kClientAddr);
            setRegU32(env.ctx, 5, kSid);
            setRegU32(env.ctx, 6, 0u);
            ps2_syscalls::SifBindRpc(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), KE_OK, "SifBindRpc should succeed for sound-driver sid");

            setRegU32(env.ctx, 4, kClientAddr);
            setRegU32(env.ctx, 5, 0x12u);
            setRegU32(env.ctx, 6, 0u);
            setRegU32(env.ctx, 7, 0u);
            setRegU32(env.ctx, 8, 0u);
            setRegU32(env.ctx, 9, kRecvAddr);
            setRegU32(env.ctx, 10, 4u);
            setRegU32(env.ctx, 11, 0u);
            ps2_syscalls::SifCallRpc(env.rdram.data(), &env.ctx, &env.runtime);
            const uint32_t kSrcAddr = readGuestU32(env.rdram.data(), kRecvAddr);

            std::memset(env.rdram.data() + kDstAddr, 0, kSize);
            std::memset(env.rdram.data() + kRdAddr, 0, sizeof(SifRpcReceiveData));

            writeGuestS16(env.rdram.data(), kSrcAddr + kSeSumOffset + (kBank * 2u), static_cast<int16_t>(0x1357));
            writeGuestS16(env.rdram.data(), kSrcAddr + kMidiSumOffset + (kBank * 2u), static_cast<int16_t>(0x2468));

            writeGuestS16(env.rdram.data(), kPrimarySeCheckAddr + (kBank * 2u), static_cast<int16_t>(0x7B7B));
            writeGuestS16(env.rdram.data(), kPrimaryMidiCheckAddr + (kBank * 2u), static_cast<int16_t>(0x6A6A));

            setRegU32(env.ctx, 4, kRdAddr);
            setRegU32(env.ctx, 5, kSrcAddr);
            setRegU32(env.ctx, 6, kDstAddr);
            setRegU32(env.ctx, 7, kSize);
            ps2_stubs::sceSifGetOtherData(env.rdram.data(), &env.ctx, &env.runtime);

            t.Equals(getRegS32(env.ctx, 2), 0,
                     "sceSifGetOtherData should succeed for sound-status transfer");
            t.Equals(readGuestS16(env.rdram.data(), kDstAddr + kSeSumOffset + (kBank * 2u)),
                     static_cast<int16_t>(0x1357),
                     "live se_sum for the active bank should not be clobbered by compat check arrays");
            t.Equals(readGuestS16(env.rdram.data(), kDstAddr + kMidiSumOffset + (kBank * 2u)),
                     static_cast<int16_t>(0x2468),
                     "live midi_sum for the active bank should not be clobbered by compat check arrays");
        });

        tc.Run("sceSifGetOtherData backfills zero sound-status sums for later banks", [](TestCase &t)
        {
            TestEnv env;
            configureProfile(env, "slus_201.84");

            constexpr uint32_t kRdAddr = 0x00023700u;
            constexpr uint32_t kDstAddr = 0x00023800u;
            constexpr uint32_t kSize = 0x42u;
            constexpr uint32_t kPrimarySeCheckAddr = 0x01E0EF10u;
            constexpr uint32_t kPrimaryMidiCheckAddr = 0x01E0EF20u;
            constexpr uint32_t kMidiSumOffset = 0x1Eu;
            constexpr uint32_t kSeSumOffset = 0x26u;
            constexpr uint32_t kLiveBank = 0u;
            constexpr uint32_t kPendingBank = 1u;

            constexpr uint32_t kClientAddr = 0x00023900u;
            constexpr uint32_t kRecvAddr = 0x00023A00u;

            ps2_syscalls::SifInitRpc(env.rdram.data(), &env.ctx, &env.runtime);
            setRegU32(env.ctx, 4, kClientAddr);
            setRegU32(env.ctx, 5, 1u);
            setRegU32(env.ctx, 6, 0u);
            ps2_syscalls::SifBindRpc(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), KE_OK, "SifBindRpc should succeed for sound-driver sid");

            setRegU32(env.ctx, 4, kClientAddr);
            setRegU32(env.ctx, 5, 0x12u);
            setRegU32(env.ctx, 6, 0u);
            setRegU32(env.ctx, 7, 0u);
            setRegU32(env.ctx, 8, 0u);
            setRegU32(env.ctx, 9, kRecvAddr);
            setRegU32(env.ctx, 10, 4u);
            setRegU32(env.ctx, 11, 0u);
            ps2_syscalls::SifCallRpc(env.rdram.data(), &env.ctx, &env.runtime);
            const uint32_t kSrcAddr = readGuestU32(env.rdram.data(), kRecvAddr);

            std::memset(env.rdram.data() + kDstAddr, 0, kSize);
            std::memset(env.rdram.data() + kRdAddr, 0, sizeof(SifRpcReceiveData));

            writeGuestS16(env.rdram.data(), kSrcAddr + kSeSumOffset + (kLiveBank * 2u), static_cast<int16_t>(0x1111));
            writeGuestS16(env.rdram.data(), kSrcAddr + kMidiSumOffset + (kLiveBank * 2u), static_cast<int16_t>(0x2222));

            writeGuestS16(env.rdram.data(), kPrimarySeCheckAddr + (kPendingBank * 2u), static_cast<int16_t>(0x3333));
            writeGuestS16(env.rdram.data(), kPrimaryMidiCheckAddr + (kPendingBank * 2u), static_cast<int16_t>(0x4444));

            setRegU32(env.ctx, 4, kRdAddr);
            setRegU32(env.ctx, 5, kSrcAddr);
            setRegU32(env.ctx, 6, kDstAddr);
            setRegU32(env.ctx, 7, kSize);
            ps2_stubs::sceSifGetOtherData(env.rdram.data(), &env.ctx, &env.runtime);

            t.Equals(getRegS32(env.ctx, 2), 0,
                     "sceSifGetOtherData should succeed for later-bank sound-status transfer");
            t.Equals(readGuestS16(env.rdram.data(), kDstAddr + kSeSumOffset + (kLiveBank * 2u)),
                     static_cast<int16_t>(0x1111),
                     "existing live se_sum values should remain intact");
            t.Equals(readGuestS16(env.rdram.data(), kDstAddr + kMidiSumOffset + (kLiveBank * 2u)),
                     static_cast<int16_t>(0x2222),
                     "existing live midi_sum values should remain intact");
            t.Equals(readGuestS16(env.rdram.data(), kDstAddr + kSeSumOffset + (kPendingBank * 2u)),
                     static_cast<int16_t>(0x3333),
                     "zero se_sum slots should backfill from compat tables for later banks");
            t.Equals(readGuestS16(env.rdram.data(), kDstAddr + kMidiSumOffset + (kPendingBank * 2u)),
                     static_cast<int16_t>(0x4444),
                     "zero midi_sum slots should backfill from compat tables for later banks");
        });

        tc.Run("sceSifGetOtherData rejects unsupported guest segments", [](TestCase &t)
        {
            TestEnv env;

            constexpr uint32_t kRdAddr = 0x00024000u;
            constexpr uint32_t kDstAddr = 0x00024100u;
            constexpr uint32_t kInvalidSrcAddr = 0xE0000200u;
            constexpr uint32_t kSize = 16u;

            std::memset(env.rdram.data() + kDstAddr, 0xA5, kSize);
            writeGuestU32(env.rdram.data(), kRdAddr + 0x10u, 0x11111111u);
            writeGuestU32(env.rdram.data(), kRdAddr + 0x14u, 0x22222222u);
            writeGuestU32(env.rdram.data(), kRdAddr + 0x18u, 0x33333333u);

            setRegU32(env.ctx, 4, kRdAddr);
            setRegU32(env.ctx, 5, kInvalidSrcAddr);
            setRegU32(env.ctx, 6, kDstAddr);
            setRegU32(env.ctx, 7, kSize);
            ps2_stubs::sceSifGetOtherData(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegS32(env.ctx, 2), -1, "sceSifGetOtherData should fail for unsupported source segment");

            std::array<uint8_t, kSize> expected{};
            expected.fill(0xA5u);
            t.IsTrue(std::memcmp(env.rdram.data() + kDstAddr, expected.data(), expected.size()) == 0,
                     "failed sceSifGetOtherData should not modify destination");
            t.Equals(readGuestU32(env.rdram.data(), kRdAddr + 0x10u), 0x11111111u,
                     "failed sceSifGetOtherData should not overwrite rd metadata");
        });

        tc.Run("raw PADMAN bind, version, init, and open complete through SIF DMA", [](TestCase &t)
        {
            TestEnv env;
            configureProfile(env, "SLUS_205.15");

            constexpr uint32_t kDescriptorAddress = 0x00026000u;
            constexpr uint32_t kPacketAddress = 0x00026100u;
            constexpr uint32_t kReceivePointerAddress = 0x00026200u;
            constexpr uint32_t kInboundAddress = 0x00026300u;
            constexpr uint32_t kRpcBufferAddress = 0x00026400u;
            constexpr uint32_t kPadAreaAddress = 0x00026600u;
            constexpr uint32_t kPadmanClientAddress = 0x00315980u;
            constexpr uint32_t kPadmanExtClientAddress = 0x003159A8u;
            constexpr uint32_t kPadmanSid = 0x80000100u;
            constexpr uint32_t kPadmanExtSid = 0x80000101u;
            constexpr uint32_t kBindCommand = 0x80000009u;
            constexpr uint32_t kCallCommand = 0x8000000Au;
            constexpr uint32_t kSifSubAddressRegister = 0x80000001u;

            writeGuestU32(env.rdram.data(), kReceivePointerAddress, kInboundAddress);
            setRegU32(env.ctx, 4, kSifSubAddressRegister);
            setRegU32(env.ctx, 5, kReceivePointerAddress);
            ps2_stubs::sceSifSetReg(env.rdram.data(), &env.ctx, &env.runtime);

            const auto bind = [&](uint32_t sid, uint32_t clientAddress, uint32_t sequence)
            {
                std::memset(env.rdram.data() + kPacketAddress, 0, 0x40u);
                writeGuestU32(env.rdram.data(), kPacketAddress, 0x40u);
                writeGuestU32(env.rdram.data(), kPacketAddress + 0x08u, kBindCommand);
                writeGuestU32(env.rdram.data(), kPacketAddress + 0x10u, 0xA5000000u | sequence);
                writeGuestU32(env.rdram.data(), kPacketAddress + 0x14u, 0x20310000u + sequence * 0x40u);
                writeGuestU32(env.rdram.data(), kPacketAddress + 0x18u, sequence);
                writeGuestU32(env.rdram.data(), kPacketAddress + 0x1Cu, clientAddress);
                writeGuestU32(env.rdram.data(), kPacketAddress + 0x20u, sid);

                const Ps2SifDmaTransfer descriptor{
                    kPacketAddress, 0u, 0x40, 0x44};
                std::memcpy(env.rdram.data() + kDescriptorAddress,
                            &descriptor,
                            sizeof(descriptor));

                setRegU32(env.ctx, 4, kDescriptorAddress);
                setRegU32(env.ctx, 5, 1u);
                ps2_stubs::sceSifSetDma(env.rdram.data(), &env.ctx, &env.runtime);
                t.IsTrue(getRegS32(env.ctx, 2) > 0,
                         "raw PADMAN bind should complete through SIF DMA");
                t.Equals(readGuestU32(env.rdram.data(), kInboundAddress + 0x18u),
                         sequence,
                         "PADMAN bind completion should preserve the RPC sequence");
                t.Equals(readGuestU32(env.rdram.data(), kInboundAddress + 0x20u),
                         kBindCommand,
                         "PADMAN bind completion should identify RPC_BIND");
                return readGuestU32(env.rdram.data(), kInboundAddress + 0x24u);
            };

            const uint32_t padmanServer =
                bind(kPadmanSid, kPadmanClientAddress, 0x30u);
            const uint32_t padmanExtServer =
                bind(kPadmanExtSid, kPadmanExtClientAddress, 0x31u);

            t.IsTrue(padmanServer != 0u,
                     "SID 0x80000100 should return a valid server token");
            t.IsTrue(padmanExtServer != 0u,
                     "SID 0x80000101 should return a valid server token");
            t.IsTrue(padmanServer != padmanExtServer,
                     "the two PADMAN SIDs should route to distinct server tokens");

            const auto call = [&](uint32_t command,
                                  uint32_t sequence,
                                  uint32_t port = 0u,
                                  uint32_t slot = 0u,
                                  uint32_t padAreaAddress = 0u)
            {
                std::memset(env.rdram.data() + kRpcBufferAddress, 0, 0x80u);
                writeGuestU32(env.rdram.data(), kRpcBufferAddress, command);
                writeGuestU32(env.rdram.data(), kRpcBufferAddress + 0x04u, port);
                writeGuestU32(env.rdram.data(), kRpcBufferAddress + 0x08u, slot);
                writeGuestU32(env.rdram.data(), kRpcBufferAddress + 0x10u, padAreaAddress);

                std::memset(env.rdram.data() + kPacketAddress, 0, 0x40u);
                writeGuestU32(env.rdram.data(), kPacketAddress, 0x40u);
                writeGuestU32(env.rdram.data(), kPacketAddress + 0x08u, kCallCommand);
                writeGuestU32(env.rdram.data(), kPacketAddress + 0x10u, 0xA5000000u | sequence);
                writeGuestU32(env.rdram.data(), kPacketAddress + 0x14u, 0x20310000u + sequence * 0x40u);
                writeGuestU32(env.rdram.data(), kPacketAddress + 0x18u, sequence);
                writeGuestU32(env.rdram.data(), kPacketAddress + 0x1Cu, kPadmanClientAddress);
                writeGuestU32(env.rdram.data(), kPacketAddress + 0x20u, 1u);
                writeGuestU32(env.rdram.data(), kPacketAddress + 0x24u, 0x80u);
                writeGuestU32(env.rdram.data(), kPacketAddress + 0x28u, kRpcBufferAddress);
                writeGuestU32(env.rdram.data(), kPacketAddress + 0x2Cu, 0x80u);

                const std::array<Ps2SifDmaTransfer, 2> descriptors{{
                    {kRpcBufferAddress, 0u, 0x80, 0},
                    {kPacketAddress, 0u, 0x40, 0x44},
                }};
                std::memcpy(env.rdram.data() + kDescriptorAddress,
                            descriptors.data(),
                            sizeof(descriptors));

                setRegU32(env.ctx, 4, kDescriptorAddress);
                setRegU32(env.ctx, 5, 2u);
                ps2_stubs::sceSifSetDma(env.rdram.data(), &env.ctx, &env.runtime);
                t.IsTrue(getRegS32(env.ctx, 2) > 0,
                         "raw PADMAN call should complete through SIF DMA");
                t.Equals(readGuestU32(env.rdram.data(), kInboundAddress + 0x18u),
                         sequence,
                         "PADMAN call completion should preserve the RPC sequence");
                t.Equals(readGuestU32(env.rdram.data(), kInboundAddress + 0x20u),
                         kCallCommand,
                         "PADMAN call completion should identify RPC_CALL");
                return readGuestU32(env.rdram.data(), kRpcBufferAddress + 0x0Cu);
            };

            t.Equals(call(0x12u, 0x32u),
                     0x00000422u,
                     "raw PADMAN GET_MODVER should report module version 4.22 at +0x0c");
            t.Equals(call(0x10u, 0x33u),
                     1u,
                     "raw PADMAN INIT should report success at +0x0c");

            std::memset(env.rdram.data() + kPadAreaAddress, 0xA5, 0x100u);
            t.Equals(call(0x01u, 0x34u, 0u, 0u, kPadAreaAddress),
                     1u,
                     "raw PADMAN OPEN should report success at +0x0c");
            t.Equals(readGuestU32(env.rdram.data(), kRpcBufferAddress + 0x14u),
                     kPadAreaAddress,
                     "raw PADMAN OPEN should echo the guest pad-area token at +0x14");

            for (uint32_t bufferOffset : {0u, 0x80u})
            {
                t.Equals(static_cast<uint32_t>(
                             env.rdram[kPadAreaAddress + bufferOffset + 0x01u]),
                         0x41u,
                         "OPEN should initialize each pad buffer in digital mode");
                t.Equals(static_cast<uint32_t>(
                             env.rdram[kPadAreaAddress + bufferOffset + 0x70u]),
                         6u,
                         "OPEN should initialize each pad buffer to PAD_STATE_STABLE");
                t.Equals(static_cast<uint32_t>(
                             env.rdram[kPadAreaAddress + bufferOffset + 0x71u]),
                         0u,
                         "OPEN should initialize each pad buffer to PAD_RSTAT_COMPLETE");
                t.Equals(static_cast<uint32_t>(
                             env.rdram[kPadAreaAddress + bufferOffset + 0x02u]),
                         0xFFu,
                         "OPEN should initialize the first neutral active-low button byte");
                t.Equals(static_cast<uint32_t>(
                             env.rdram[kPadAreaAddress + bufferOffset + 0x03u]),
                         0xFFu,
                         "OPEN should initialize the second neutral active-low button byte");
                for (uint32_t axisOffset = 0x04u; axisOffset <= 0x07u; ++axisOffset)
                {
                    t.Equals(static_cast<uint32_t>(
                                 env.rdram[kPadAreaAddress + bufferOffset + axisOffset]),
                             0x80u,
                             "OPEN should center every neutral analog axis");
                }
                t.Equals(readGuestU32(env.rdram.data(),
                                      kPadAreaAddress + bufferOffset + 0x58u),
                         bufferOffset / 0x80u,
                         "OPEN should seed the two pad buffers with distinct frames");
                t.Equals(readGuestU32(env.rdram.data(),
                                      kPadAreaAddress + bufferOffset + 0x60u),
                         8u,
                         "OPEN should expose an eight-byte neutral digital report");
                t.Equals(static_cast<uint32_t>(
                             env.rdram[kPadAreaAddress + bufferOffset + 0x72u]),
                         1u,
                         "OPEN should activate the read task in each pad buffer");
            }
        });

        tc.Run("raw Duelists MCSERV init returns its required version tuple", [](TestCase &t)
        {
            TestEnv env;
            configureProfile(env, "SLUS_205.15");

            constexpr uint32_t kDescriptorAddress = 0x00026400u;
            constexpr uint32_t kPacketAddress = 0x00026500u;
            constexpr uint32_t kSendAddress = 0x00026600u;
            constexpr uint32_t kReceiveAddress = 0x00026700u;
            constexpr uint32_t kReceivePointerAddress = 0x00026800u;
            constexpr uint32_t kInboundAddress = 0x00026900u;
            constexpr uint32_t kClientAddress = 0x00315C40u;
            constexpr uint32_t kSid = 0x80000400u;
            constexpr uint32_t kBindCommand = 0x80000009u;
            constexpr uint32_t kCallCommand = 0x8000000Au;
            constexpr uint32_t kSifSubAddressRegister = 0x80000001u;
            constexpr uint32_t kSequence = 0xD6u;
            constexpr uint32_t kGuard = 0xA5A55A5Au;

            writeGuestU32(env.rdram.data(), kReceivePointerAddress, kInboundAddress);
            setRegU32(env.ctx, 4, kSifSubAddressRegister);
            setRegU32(env.ctx, 5, kReceivePointerAddress);
            ps2_stubs::sceSifSetReg(env.rdram.data(), &env.ctx, &env.runtime);

            const auto invokeDma = [&](uint32_t count)
            {
                setRegU32(env.ctx, 4, kDescriptorAddress);
                setRegU32(env.ctx, 5, count);
                ps2_stubs::sceSifSetDma(env.rdram.data(), &env.ctx, &env.runtime);
                t.IsTrue(getRegS32(env.ctx, 2) > 0,
                         "raw Duelists MCSERV DMA should complete");
            };

            std::memset(env.rdram.data() + kPacketAddress, 0, 0x40u);
            writeGuestU32(env.rdram.data(), kPacketAddress, 0x40u);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x08u, kBindCommand);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x10u, 0xA50000D5u);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x14u, 0x20313540u);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x18u, 0xD5u);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x1Cu, kClientAddress);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x20u, kSid);
            const Ps2SifDmaTransfer bindDescriptor{
                kPacketAddress, 0u, 0x40, 0x44};
            std::memcpy(env.rdram.data() + kDescriptorAddress,
                        &bindDescriptor,
                        sizeof(bindDescriptor));
            invokeDma(1u);
            t.IsTrue(readGuestU32(env.rdram.data(), kInboundAddress + 0x24u) != 0u,
                     "Duelists MCSERV bind should return a server token");

            std::memset(env.rdram.data() + kSendAddress, 0, 0x30u);
            writeGuestU32(env.rdram.data(), kReceiveAddress - 4u, kGuard);
            std::memset(env.rdram.data() + kReceiveAddress, 0xCC, 0x0Cu);
            writeGuestU32(env.rdram.data(), kReceiveAddress + 0x0Cu, kGuard);

            std::memset(env.rdram.data() + kPacketAddress, 0, 0x40u);
            writeGuestU32(env.rdram.data(), kPacketAddress, 0x40u);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x08u, kCallCommand);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x10u,
                          0xA5000000u | kSequence);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x14u,
                          0x20310000u + kSequence * 0x40u);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x18u, kSequence);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x1Cu, kClientAddress);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x20u, 0xFEu);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x24u, 0x30u);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x28u, kReceiveAddress);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x2Cu, 0x0Cu);
            const std::array<Ps2SifDmaTransfer, 2> callDescriptors{{
                {kSendAddress, 0u, 0x30, 0},
                {kPacketAddress, 0u, 0x40, 0x44},
            }};
            std::memcpy(env.rdram.data() + kDescriptorAddress,
                        callDescriptors.data(),
                        sizeof(callDescriptors));
            invokeDma(2u);

            t.Equals(readGuestU32(env.rdram.data(), kReceiveAddress), 0u,
                     "Duelists MCSERV init should report success");
            t.Equals(readGuestU32(env.rdram.data(), kReceiveAddress + 4u), 0x020Au,
                     "Duelists MCSERV init should report version 2.10");
            t.Equals(readGuestU32(env.rdram.data(), kReceiveAddress + 8u), 0x020Eu,
                     "Duelists MCSERV init should report MCMAN version 2.14");
            t.Equals(readGuestU32(env.rdram.data(), kReceiveAddress - 4u), kGuard,
                     "Duelists MCSERV init should preserve memory before its response");
            t.Equals(readGuestU32(env.rdram.data(), kReceiveAddress + 0x0Cu), kGuard,
                     "Duelists MCSERV init should preserve memory after its response");
            t.Equals(readGuestU32(env.rdram.data(), kInboundAddress + 0x18u),
                     kSequence,
                     "Duelists MCSERV completion should preserve its RPC sequence");
            t.Equals(readGuestU32(env.rdram.data(), kInboundAddress + 0x20u),
                     kCallCommand,
                     "Duelists MCSERV completion should identify RPC_CALL");
        });

        tc.Run("raw Duelists typed calls preserve their observed SIF framing", [](TestCase &t)
        {
            TestEnv env;
            configureProfile(env, "SLUS_205.15");

            constexpr uint32_t kDescriptorAddress = 0x00026800u;
            constexpr uint32_t kPacketAddress = 0x00026900u;
            constexpr uint32_t kSendAddress = 0x00026A00u;
            constexpr uint32_t kReceiveAddress = 0x00026B10u;
            constexpr uint32_t kReceivePointerAddress = 0x00026C00u;
            constexpr uint32_t kInboundAddress = 0x00026D00u;
            constexpr uint32_t kClientAddress = 0x0040001Cu;
            constexpr uint32_t kSid = 0x05730601u;
            constexpr uint32_t kBindCommand = 0x80000009u;
            constexpr uint32_t kCallCommand = 0x8000000Au;
            constexpr uint32_t kSifSubAddressRegister = 0x80000001u;
            constexpr uint32_t kSequence = 0x35u;
            constexpr uint32_t kGuard = 0xA5A55A5Au;

            writeGuestU32(env.rdram.data(), kReceivePointerAddress, kInboundAddress);
            setRegU32(env.ctx, 4, kSifSubAddressRegister);
            setRegU32(env.ctx, 5, kReceivePointerAddress);
            ps2_stubs::sceSifSetReg(env.rdram.data(), &env.ctx, &env.runtime);

            const auto invokeDma = [&](uint32_t count)
            {
                setRegU32(env.ctx, 4, kDescriptorAddress);
                setRegU32(env.ctx, 5, count);
                ps2_stubs::sceSifSetDma(env.rdram.data(), &env.ctx, &env.runtime);
                t.IsTrue(getRegS32(env.ctx, 2) > 0,
                         "raw Duelists RPC DMA should complete");
            };

            std::memset(env.rdram.data() + kPacketAddress, 0, 0x40u);
            writeGuestU32(env.rdram.data(), kPacketAddress, 0x40u);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x08u, kBindCommand);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x10u, 0xA5000034u);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x14u, 0x20310D00u);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x18u, 0x34u);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x1Cu, kClientAddress);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x20u, kSid);
            const Ps2SifDmaTransfer bindDescriptor{
                kPacketAddress, 0u, 0x40, 0x44};
            std::memcpy(env.rdram.data() + kDescriptorAddress,
                        &bindDescriptor,
                        sizeof(bindDescriptor));
            invokeDma(1u);

            t.IsTrue(readGuestU32(env.rdram.data(), kInboundAddress + 0x24u) != 0u,
                     "raw Duelists SID bind should return a server token");

            for (uint32_t offset = 0u; offset < 0x40u; offset += 4u)
            {
                writeGuestU32(env.rdram.data(),
                              kSendAddress + offset,
                              0x11110000u + offset);
            }
            writeGuestU32(env.rdram.data(), kSendAddress, kSendAddress);
            writeGuestU32(env.rdram.data(), kReceiveAddress - 4u, kGuard);
            std::memset(env.rdram.data() + kReceiveAddress, 0xCC, 0x10u);
            writeGuestU32(env.rdram.data(), kReceiveAddress + 0x10u, kGuard);

            std::memset(env.rdram.data() + kPacketAddress, 0, 0x40u);
            writeGuestU32(env.rdram.data(), kPacketAddress, 0x40u);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x08u, kCallCommand);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x10u, 0xA5000000u | kSequence);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x14u, 0x20310000u + kSequence * 0x40u);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x18u, kSequence);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x1Cu, kClientAddress);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x20u, 0xF005u);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x24u, 0x40u);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x28u, kReceiveAddress);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x2Cu, 0x10u);
            const std::array<Ps2SifDmaTransfer, 2> callDescriptors{{
                {kSendAddress, 0u, 0x40, 0},
                {kPacketAddress, 0u, 0x40, 0x44},
            }};
            std::memcpy(env.rdram.data() + kDescriptorAddress,
                        callDescriptors.data(),
                        sizeof(callDescriptors));
            invokeDma(2u);

            for (uint32_t offset = 0u; offset < 0x10u; offset += 4u)
            {
                t.Equals(readGuestU32(env.rdram.data(), kReceiveAddress + offset),
                         0u,
                         "Duelists F005 should zero its exact 0x10-byte response");
            }
            t.Equals(readGuestU32(env.rdram.data(), kReceiveAddress - 4u),
                     kGuard,
                     "Duelists F005 should preserve memory before the response");
            t.Equals(readGuestU32(env.rdram.data(), kReceiveAddress + 0x10u),
                     kGuard,
                     "Duelists F005 should preserve memory after the response");
            t.Equals(readGuestU32(env.rdram.data(), kInboundAddress + 0x18u),
                     kSequence,
                     "Duelists F005 completion should preserve the RPC sequence");
            t.Equals(readGuestU32(env.rdram.data(), kInboundAddress + 0x20u),
                     kCallCommand,
                     "Duelists F005 completion should identify RPC_CALL");

            constexpr uint32_t kF002Sequence = 0x36u;
            constexpr uint32_t kF002Token = 0x000C1800u;
            writeGuestU32(env.rdram.data(), kSendAddress + 0x04u, kF002Token);
            writeGuestU32(env.rdram.data(), kReceiveAddress - 4u, kGuard);
            std::memset(env.rdram.data() + kReceiveAddress, 0xCC, 0x10u);
            writeGuestU32(env.rdram.data(), kReceiveAddress + 0x10u, kGuard);

            writeGuestU32(env.rdram.data(), kPacketAddress + 0x10u,
                          0xA5000000u | kF002Sequence);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x14u,
                          0x20310000u + kF002Sequence * 0x40u);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x18u, kF002Sequence);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x20u, 0xF002u);
            std::memcpy(env.rdram.data() + kDescriptorAddress,
                        callDescriptors.data(),
                        sizeof(callDescriptors));
            invokeDma(2u);

            t.Equals(readGuestU32(env.rdram.data(), kReceiveAddress),
                     kF002Token,
                     "Duelists F002 should echo the requested token in response word zero");
            for (uint32_t offset = 4u; offset < 0x10u; offset += 4u)
            {
                t.Equals(readGuestU32(env.rdram.data(), kReceiveAddress + offset),
                         0u,
                         "Duelists F002 should zero the remaining response words");
            }
            t.Equals(readGuestU32(env.rdram.data(), kReceiveAddress - 4u),
                     kGuard,
                     "Duelists F002 should preserve memory before the response");
            t.Equals(readGuestU32(env.rdram.data(), kReceiveAddress + 0x10u),
                     kGuard,
                     "Duelists F002 should preserve memory after the response");
            t.Equals(readGuestU32(env.rdram.data(), kInboundAddress + 0x18u),
                     kF002Sequence,
                     "Duelists F002 completion should preserve the RPC sequence");
            t.Equals(readGuestU32(env.rdram.data(), kInboundAddress + 0x20u),
                     kCallCommand,
                     "Duelists F002 completion should identify RPC_CALL");

            const std::array<std::pair<uint32_t, uint32_t>, 2> registrations{{
                {0u, 0x00005010u},
                {11u, 0x00107810u},
            }};
            constexpr uint32_t kRegistrationSelfToken = 0x00400080u;
            writeGuestU32(env.rdram.data(), kSendAddress, kRegistrationSelfToken);
            for (uint32_t registrationIndex = 0u;
                 registrationIndex < registrations.size();
                 ++registrationIndex)
            {
                const auto [index, pointer] = registrations[registrationIndex];
                const uint32_t sequence = 0x37u + registrationIndex;
                writeGuestU32(env.rdram.data(), kSendAddress + 0x04u, index);
                writeGuestU32(env.rdram.data(), kSendAddress + 0x08u, pointer);
                writeGuestU32(env.rdram.data(), kReceiveAddress - 4u, kGuard);
                std::memset(env.rdram.data() + kReceiveAddress, 0xCC, 0x10u);
                writeGuestU32(env.rdram.data(), kReceiveAddress + 0x10u, kGuard);

                writeGuestU32(env.rdram.data(), kPacketAddress + 0x10u,
                              0xA5000000u | sequence);
                writeGuestU32(env.rdram.data(), kPacketAddress + 0x14u,
                              0x20310000u + sequence * 0x40u);
                writeGuestU32(env.rdram.data(), kPacketAddress + 0x18u, sequence);
                writeGuestU32(env.rdram.data(), kPacketAddress + 0x20u, 0x5F10u);
                std::memcpy(env.rdram.data() + kDescriptorAddress,
                            callDescriptors.data(),
                            sizeof(callDescriptors));
                invokeDma(2u);

                for (uint32_t offset = 0u; offset < 0x10u; offset += 4u)
                {
                    t.Equals(readGuestU32(env.rdram.data(), kReceiveAddress + offset),
                             0u,
                             "Duelists 5F10 should zero its exact response");
                }
                t.Equals(readGuestU32(env.rdram.data(), kReceiveAddress - 4u),
                         kGuard,
                         "Duelists 5F10 should preserve memory before the response");
                t.Equals(readGuestU32(env.rdram.data(), kReceiveAddress + 0x10u),
                         kGuard,
                         "Duelists 5F10 should preserve memory after the response");
                t.Equals(readGuestU32(env.rdram.data(), kInboundAddress + 0x18u),
                         sequence,
                         "Duelists 5F10 completion should preserve the RPC sequence");
                t.Equals(readGuestU32(env.rdram.data(), kInboundAddress + 0x20u),
                         kCallCommand,
                         "Duelists 5F10 completion should identify RPC_CALL");
            }

            constexpr uint32_t k5F12Sequence = 0x39u;
            writeGuestU32(env.rdram.data(), kSendAddress + 0x00u, kRegistrationSelfToken);
            writeGuestU32(env.rdram.data(), kSendAddress + 0x04u, 0x00150000u);
            writeGuestU32(env.rdram.data(), kSendAddress + 0x08u, 0x00038000u);
            writeGuestU32(env.rdram.data(), kReceiveAddress - 4u, kGuard);
            std::memset(env.rdram.data() + kReceiveAddress, 0xCC, 0x10u);
            writeGuestU32(env.rdram.data(), kReceiveAddress + 0x10u, kGuard);

            writeGuestU32(env.rdram.data(), kPacketAddress + 0x10u,
                          0xA5000000u | k5F12Sequence);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x14u,
                          0x20310000u + k5F12Sequence * 0x40u);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x18u, k5F12Sequence);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x20u, 0x5F12u);
            std::memcpy(env.rdram.data() + kDescriptorAddress,
                        callDescriptors.data(),
                        sizeof(callDescriptors));
            invokeDma(2u);

            for (uint32_t offset = 0u; offset < 0x10u; offset += 4u)
            {
                t.Equals(readGuestU32(env.rdram.data(), kReceiveAddress + offset),
                         0u,
                         "Duelists 5F12 should zero its exact response");
            }
            t.Equals(readGuestU32(env.rdram.data(), kReceiveAddress - 4u),
                     kGuard,
                     "Duelists 5F12 should preserve memory before the response");
            t.Equals(readGuestU32(env.rdram.data(), kReceiveAddress + 0x10u),
                     kGuard,
                     "Duelists 5F12 should preserve memory after the response");
            t.Equals(readGuestU32(env.rdram.data(), kInboundAddress + 0x18u),
                     k5F12Sequence,
                     "Duelists 5F12 completion should preserve the RPC sequence");
            t.Equals(readGuestU32(env.rdram.data(), kInboundAddress + 0x20u),
                     kCallCommand,
                     "Duelists 5F12 completion should identify RPC_CALL");

            constexpr uint32_t k5000Sequence = 0x3Au;
            writeGuestU32(env.rdram.data(), kSendAddress, kRegistrationSelfToken);
            writeGuestU32(env.rdram.data(), kReceiveAddress - 4u, kGuard);
            std::memset(env.rdram.data() + kReceiveAddress, 0xCC, 0x10u);
            writeGuestU32(env.rdram.data(), kReceiveAddress + 0x10u, kGuard);

            writeGuestU32(env.rdram.data(), kPacketAddress + 0x10u,
                          0xA5000000u | k5000Sequence);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x14u,
                          0x20310000u + k5000Sequence * 0x40u);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x18u, k5000Sequence);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x20u, 0x5000u);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x30u, 1u);
            std::memcpy(env.rdram.data() + kDescriptorAddress,
                        callDescriptors.data(),
                        sizeof(callDescriptors));
            invokeDma(2u);

            for (uint32_t offset = 0u; offset < 0x10u; offset += 4u)
            {
                t.Equals(readGuestU32(env.rdram.data(), kReceiveAddress + offset),
                         0u,
                         "Duelists 5000 should zero its exact response");
            }
            t.Equals(readGuestU32(env.rdram.data(), kReceiveAddress - 4u),
                     kGuard,
                     "Duelists 5000 should preserve memory before the response");
            t.Equals(readGuestU32(env.rdram.data(), kReceiveAddress + 0x10u),
                     kGuard,
                     "Duelists 5000 should preserve memory after the response");
            t.Equals(readGuestU32(env.rdram.data(), kInboundAddress + 0x18u),
                     k5000Sequence,
                     "Duelists 5000 completion should preserve the RPC sequence");
            t.Equals(readGuestU32(env.rdram.data(), kInboundAddress + 0x20u),
                     kCallCommand,
                     "Duelists 5000 completion should identify RPC_CALL");

            constexpr uint32_t k5005Sequence = 0x3Bu;
            writeGuestU32(env.rdram.data(), kSendAddress + 0x00u, kRegistrationSelfToken);
            writeGuestU32(env.rdram.data(), kSendAddress + 0x04u, 0x80u);
            writeGuestU32(env.rdram.data(), kSendAddress + 0x08u, 0x80u);
            writeGuestU32(env.rdram.data(), kSendAddress + 0x0Cu, 0x40u);
            writeGuestU32(env.rdram.data(), kReceiveAddress - 4u, kGuard);
            std::memset(env.rdram.data() + kReceiveAddress, 0xCC, 0x10u);
            writeGuestU32(env.rdram.data(), kReceiveAddress + 0x10u, kGuard);

            writeGuestU32(env.rdram.data(), kPacketAddress + 0x10u,
                          0xA5000000u | k5005Sequence);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x14u,
                          0x20310000u + k5005Sequence * 0x40u);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x18u, k5005Sequence);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x20u, 0x5005u);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x30u, 1u);
            std::memcpy(env.rdram.data() + kDescriptorAddress,
                        callDescriptors.data(),
                        sizeof(callDescriptors));
            invokeDma(2u);

            for (uint32_t offset = 0u; offset < 0x10u; offset += 4u)
            {
                t.Equals(readGuestU32(env.rdram.data(), kReceiveAddress + offset),
                         0u,
                         "Duelists 5005 should zero its exact response");
            }
            t.Equals(readGuestU32(env.rdram.data(), kReceiveAddress - 4u),
                     kGuard,
                     "Duelists 5005 should preserve memory before the response");
            t.Equals(readGuestU32(env.rdram.data(), kReceiveAddress + 0x10u),
                     kGuard,
                     "Duelists 5005 should preserve memory after the response");
            t.Equals(readGuestU32(env.rdram.data(), kInboundAddress + 0x18u),
                     k5005Sequence,
                     "Duelists 5005 completion should preserve the RPC sequence");
            t.Equals(readGuestU32(env.rdram.data(), kInboundAddress + 0x20u),
                     kCallCommand,
                     "Duelists 5005 completion should identify RPC_CALL");

            constexpr uint32_t kF003Sequence = 0x3Cu;
            writeGuestU32(env.rdram.data(), kSendAddress + 0x00u, kRegistrationSelfToken);
            writeGuestU32(env.rdram.data(), kSendAddress + 0x04u, 0u);
            writeGuestU32(env.rdram.data(), kReceiveAddress - 4u, kGuard);
            std::memset(env.rdram.data() + kReceiveAddress, 0xCC, 0x10u);
            writeGuestU32(env.rdram.data(), kReceiveAddress + 0x10u, kGuard);

            writeGuestU32(env.rdram.data(), kPacketAddress + 0x10u,
                          0xA5000000u | kF003Sequence);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x14u,
                          0x20310000u + kF003Sequence * 0x40u);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x18u, kF003Sequence);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x20u, 0xF003u);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x30u, 1u);
            std::memcpy(env.rdram.data() + kDescriptorAddress,
                        callDescriptors.data(),
                        sizeof(callDescriptors));
            invokeDma(2u);

            for (uint32_t offset = 0u; offset < 0x10u; offset += 4u)
            {
                t.Equals(readGuestU32(env.rdram.data(), kReceiveAddress + offset),
                         0u,
                         "Duelists F003 should zero its exact response");
            }
            t.Equals(readGuestU32(env.rdram.data(), kReceiveAddress - 4u),
                     kGuard,
                     "Duelists F003 should preserve memory before the response");
            t.Equals(readGuestU32(env.rdram.data(), kReceiveAddress + 0x10u),
                     kGuard,
                     "Duelists F003 should preserve memory after the response");
            t.Equals(readGuestU32(env.rdram.data(), kInboundAddress + 0x18u),
                     kF003Sequence,
                     "Duelists F003 completion should preserve the RPC sequence");
            t.Equals(readGuestU32(env.rdram.data(), kInboundAddress + 0x20u),
                     kCallCommand,
                     "Duelists F003 completion should identify RPC_CALL");

            constexpr uint32_t k5003Sequence = 0x3Du;
            writeGuestU32(env.rdram.data(), kSendAddress + 0x00u,
                          kRegistrationSelfToken);
            writeGuestU32(env.rdram.data(), kSendAddress + 0x04u, 0x00000100u);
            writeGuestU32(env.rdram.data(), kSendAddress + 0x08u, 0xCCCCCCCCu);
            writeGuestU32(env.rdram.data(), kSendAddress + 0x0Cu, 0xDDDDDDDDu);
            writeGuestU32(env.rdram.data(), kReceiveAddress - 4u, kGuard);
            std::memset(env.rdram.data() + kReceiveAddress, 0xCC, 0x10u);
            writeGuestU32(env.rdram.data(), kReceiveAddress + 0x10u, kGuard);

            writeGuestU32(env.rdram.data(), kPacketAddress + 0x10u,
                          0xA5000000u | k5003Sequence);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x14u,
                          0x20310000u + k5003Sequence * 0x40u);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x18u, k5003Sequence);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x20u, 0x5003u);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x30u, 1u);
            std::memcpy(env.rdram.data() + kDescriptorAddress,
                        callDescriptors.data(),
                        sizeof(callDescriptors));
            invokeDma(2u);

            for (uint32_t offset = 0u; offset < 0x10u; offset += 4u)
            {
                t.Equals(readGuestU32(env.rdram.data(), kReceiveAddress + offset),
                         0u,
                         "Duelists 5003 should return its idle audio status");
            }
            t.Equals(readGuestU32(env.rdram.data(), kReceiveAddress - 4u),
                     kGuard,
                     "Duelists 5003 should preserve memory before the response");
            t.Equals(readGuestU32(env.rdram.data(), kReceiveAddress + 0x10u),
                     kGuard,
                     "Duelists 5003 should preserve memory after the response");
            t.Equals(readGuestU32(env.rdram.data(), kInboundAddress + 0x18u),
                     k5003Sequence,
                     "Duelists 5003 completion should preserve the RPC sequence");
            t.Equals(readGuestU32(env.rdram.data(), kInboundAddress + 0x20u),
                     kCallCommand,
                     "Duelists 5003 completion should identify RPC_CALL");

            constexpr uint32_t k5004Sequence = 0x3Eu;
            writeGuestU32(env.rdram.data(), kSendAddress + 0x00u,
                          kRegistrationSelfToken);
            writeGuestU32(env.rdram.data(), kSendAddress + 0x04u, 0x00007FFFu);
            writeGuestU32(env.rdram.data(), kSendAddress + 0x08u, 0x20311040u);
            writeGuestU32(env.rdram.data(), kSendAddress + 0x0Cu, 0x00000040u);
            writeGuestU32(env.rdram.data(), kReceiveAddress - 4u, kGuard);
            std::memset(env.rdram.data() + kReceiveAddress, 0xCC, 0x10u);
            writeGuestU32(env.rdram.data(), kReceiveAddress + 0x10u, kGuard);

            writeGuestU32(env.rdram.data(), kPacketAddress + 0x10u,
                          0xA5000000u | k5004Sequence);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x14u,
                          0x20310000u + k5004Sequence * 0x40u);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x18u, k5004Sequence);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x20u, 0x5004u);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x30u, 1u);
            std::memcpy(env.rdram.data() + kDescriptorAddress,
                        callDescriptors.data(),
                        sizeof(callDescriptors));
            invokeDma(2u);

            for (uint32_t offset = 0u; offset < 0x10u; offset += 4u)
            {
                t.Equals(readGuestU32(env.rdram.data(), kReceiveAddress + offset),
                         0u,
                         "Duelists 5004 should return its idle audio status");
            }
            t.Equals(readGuestU32(env.rdram.data(), kReceiveAddress - 4u),
                     kGuard,
                     "Duelists 5004 should preserve memory before the response");
            t.Equals(readGuestU32(env.rdram.data(), kReceiveAddress + 0x10u),
                     kGuard,
                     "Duelists 5004 should preserve memory after the response");
            t.Equals(readGuestU32(env.rdram.data(), kInboundAddress + 0x18u),
                     k5004Sequence,
                     "Duelists 5004 completion should preserve the RPC sequence");
            t.Equals(readGuestU32(env.rdram.data(), kInboundAddress + 0x20u),
                     kCallCommand,
                     "Duelists 5004 completion should identify RPC_CALL");

            constexpr uint32_t kF006Sequence = 0x3Fu;
            writeGuestU32(env.rdram.data(), kSendAddress + 0x00u,
                          kRegistrationSelfToken);
            writeGuestU32(env.rdram.data(), kSendAddress + 0x04u, 0x00000001u);
            writeGuestU32(env.rdram.data(), kSendAddress + 0x08u, 0x20311040u);
            writeGuestU32(env.rdram.data(), kSendAddress + 0x0Cu, 0x00000040u);
            writeGuestU32(env.rdram.data(), kReceiveAddress - 4u, kGuard);
            std::memset(env.rdram.data() + kReceiveAddress, 0xCC, 0x10u);
            writeGuestU32(env.rdram.data(), kReceiveAddress + 0x10u, kGuard);

            writeGuestU32(env.rdram.data(), kPacketAddress + 0x10u,
                          0xA5000000u | kF006Sequence);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x14u,
                          0x20310000u + kF006Sequence * 0x40u);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x18u, kF006Sequence);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x20u, 0xF006u);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x30u, 1u);
            std::memcpy(env.rdram.data() + kDescriptorAddress,
                        callDescriptors.data(),
                        sizeof(callDescriptors));
            invokeDma(2u);

            for (uint32_t offset = 0u; offset < 0x10u; offset += 4u)
            {
                t.Equals(readGuestU32(env.rdram.data(), kReceiveAddress + offset),
                         0u,
                         "Duelists F006 should return its idle audio status");
            }
            t.Equals(readGuestU32(env.rdram.data(), kReceiveAddress - 4u),
                     kGuard,
                     "Duelists F006 should preserve memory before the response");
            t.Equals(readGuestU32(env.rdram.data(), kReceiveAddress + 0x10u),
                     kGuard,
                     "Duelists F006 should preserve memory after the response");
            t.Equals(readGuestU32(env.rdram.data(), kInboundAddress + 0x18u),
                     kF006Sequence,
                     "Duelists F006 completion should preserve the RPC sequence");
            t.Equals(readGuestU32(env.rdram.data(), kInboundAddress + 0x20u),
                     kCallCommand,
                     "Duelists F006 completion should identify RPC_CALL");

            constexpr uint32_t k5002Sequence = 0x40u;
            writeGuestU32(env.rdram.data(), kSendAddress + 0x00u,
                          kRegistrationSelfToken);
            writeGuestU32(env.rdram.data(), kSendAddress + 0x04u, 0x00000002u);
            writeGuestU32(env.rdram.data(), kSendAddress + 0x08u, 0x00001000u);
            writeGuestU32(env.rdram.data(), kSendAddress + 0x0Cu, 0xFFFFF000u);
            writeGuestU32(env.rdram.data(), kReceiveAddress - 4u, kGuard);
            std::memset(env.rdram.data() + kReceiveAddress, 0xCC, 0x10u);
            writeGuestU32(env.rdram.data(), kReceiveAddress + 0x10u, kGuard);

            writeGuestU32(env.rdram.data(), kPacketAddress + 0x10u,
                          0xA5000000u | k5002Sequence);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x14u,
                          0x20310000u + k5002Sequence * 0x40u);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x18u, k5002Sequence);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x20u, 0x5002u);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x30u, 1u);
            std::memcpy(env.rdram.data() + kDescriptorAddress,
                        callDescriptors.data(),
                        sizeof(callDescriptors));
            invokeDma(2u);

            for (uint32_t offset = 0u; offset < 0x10u; offset += 4u)
            {
                t.Equals(readGuestU32(env.rdram.data(), kReceiveAddress + offset),
                         0u,
                         "Duelists 5002 should return its idle audio status");
            }
            t.Equals(readGuestU32(env.rdram.data(), kReceiveAddress - 4u),
                     kGuard,
                     "Duelists 5002 should preserve memory before the response");
            t.Equals(readGuestU32(env.rdram.data(), kReceiveAddress + 0x10u),
                     kGuard,
                     "Duelists 5002 should preserve memory after the response");
            t.Equals(readGuestU32(env.rdram.data(), kInboundAddress + 0x18u),
                     k5002Sequence,
                     "Duelists 5002 completion should preserve the RPC sequence");
            t.Equals(readGuestU32(env.rdram.data(), kInboundAddress + 0x20u),
                     kCallCommand,
                     "Duelists 5002 completion should identify RPC_CALL");

            constexpr uint32_t k5202Sequence = 0x41u;
            writeGuestU32(env.rdram.data(), kSendAddress + 0x00u,
                          kRegistrationSelfToken);
            writeGuestU32(env.rdram.data(), kSendAddress + 0x04u, 0xCCCCCCCCu);
            writeGuestU32(env.rdram.data(), kSendAddress + 0x08u, 0xDDDDDDDDu);
            writeGuestU32(env.rdram.data(), kSendAddress + 0x0Cu, 0xEEEEEEEEu);
            writeGuestU32(env.rdram.data(), kReceiveAddress - 4u, kGuard);
            std::memset(env.rdram.data() + kReceiveAddress, 0xCC, 0x10u);
            writeGuestU32(env.rdram.data(), kReceiveAddress + 0x10u, kGuard);

            writeGuestU32(env.rdram.data(), kPacketAddress + 0x10u,
                          0xA5000000u | k5202Sequence);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x14u,
                          0x20310000u + k5202Sequence * 0x40u);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x18u, k5202Sequence);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x20u, 0x5202u);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x30u, 1u);
            std::memcpy(env.rdram.data() + kDescriptorAddress,
                        callDescriptors.data(),
                        sizeof(callDescriptors));
            invokeDma(2u);

            for (uint32_t offset = 0u; offset < 0x10u; offset += 4u)
            {
                t.Equals(readGuestU32(env.rdram.data(), kReceiveAddress + offset),
                         0u,
                         "Duelists 5202 should return its idle audio status");
            }
            t.Equals(readGuestU32(env.rdram.data(), kReceiveAddress - 4u),
                     kGuard,
                     "Duelists 5202 should preserve memory before the response");
            t.Equals(readGuestU32(env.rdram.data(), kReceiveAddress + 0x10u),
                     kGuard,
                     "Duelists 5202 should preserve memory after the response");
            t.Equals(readGuestU32(env.rdram.data(), kInboundAddress + 0x18u),
                     k5202Sequence,
                     "Duelists 5202 completion should preserve the RPC sequence");
            t.Equals(readGuestU32(env.rdram.data(), kInboundAddress + 0x20u),
                     kCallCommand,
                     "Duelists 5202 completion should identify RPC_CALL");
        });

        tc.Run("raw FILEIO core service binds at the observed Duelists boundary", [](TestCase &t)
        {
            TestEnv env;
            configureProfile(env, "SLUS_205.15");

            constexpr uint32_t kDescriptorAddress = 0x00026E00u;
            constexpr uint32_t kPacketAddress = 0x00026F00u;
            constexpr uint32_t kReceivePointerAddress = 0x00027000u;
            constexpr uint32_t kInboundAddress = 0x00027100u;
            constexpr uint32_t kFileIoClientAddress = 0x00313B80u;
            constexpr uint32_t kFileIoSid = 0x80000001u;
            constexpr uint32_t kBindCommand = 0x80000009u;
            constexpr uint32_t kBindSequence = 0xEDu;
            constexpr uint32_t kSifSubAddressRegister = 0x80000001u;

            writeGuestU32(env.rdram.data(), kReceivePointerAddress, kInboundAddress);
            setRegU32(env.ctx, 4, kSifSubAddressRegister);
            setRegU32(env.ctx, 5, kReceivePointerAddress);
            ps2_stubs::sceSifSetReg(env.rdram.data(), &env.ctx, &env.runtime);

            std::memset(env.rdram.data() + kPacketAddress, 0, 0x40u);
            writeGuestU32(env.rdram.data(), kPacketAddress, 0x40u);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x08u, kBindCommand);
            writeGuestU32(env.rdram.data(),
                          kPacketAddress + 0x10u,
                          0xA5000000u | kBindSequence);
            writeGuestU32(env.rdram.data(),
                          kPacketAddress + 0x14u,
                          0x20310000u + kBindSequence * 0x40u);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x18u, kBindSequence);
            writeGuestU32(env.rdram.data(),
                          kPacketAddress + 0x1Cu,
                          kFileIoClientAddress);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x20u, kFileIoSid);

            const Ps2SifDmaTransfer descriptor{
                kPacketAddress, 0u, 0x40, 0x44};
            std::memcpy(env.rdram.data() + kDescriptorAddress,
                        &descriptor,
                        sizeof(descriptor));

            setRegU32(env.ctx, 4, kDescriptorAddress);
            setRegU32(env.ctx, 5, 1u);
            ps2_stubs::sceSifSetDma(env.rdram.data(), &env.ctx, &env.runtime);
            t.IsTrue(getRegS32(env.ctx, 2) > 0,
                     "raw FILEIO bind should complete through SIF DMA");
            t.Equals(readGuestU32(env.rdram.data(), kInboundAddress + 0x18u),
                     kBindSequence,
                     "FILEIO bind completion should preserve Duelists sequence 0xed");
            t.Equals(readGuestU32(env.rdram.data(), kInboundAddress + 0x20u),
                     kBindCommand,
                     "FILEIO bind completion should identify RPC_BIND");
            t.IsTrue(readGuestU32(env.rdram.data(), kInboundAddress + 0x24u) != 0u,
                     "FILEIO bind should return a nonzero server token");

            constexpr uint32_t kCallCommand = 0x8000000Au;
            constexpr uint32_t kInitFunction = 0xFFu;
            constexpr uint32_t kInitSequence = 0xEEu;
            constexpr uint32_t kInitSendAddress = 0x00312880u;
            constexpr uint32_t kInitReceiveAddress = 0x00313500u;
            constexpr uint32_t kInitToken = 0x00313540u;
            constexpr uint32_t kInitSentinel = 0xA5A55A5Au;

            writeGuestU32(env.rdram.data(), kInitSendAddress, kInitToken);
            writeGuestU32(env.rdram.data(), kInitReceiveAddress, kInitSentinel);

            std::memset(env.rdram.data() + kPacketAddress, 0, 0x40u);
            writeGuestU32(env.rdram.data(), kPacketAddress, 0x40u);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x08u, kCallCommand);
            writeGuestU32(env.rdram.data(),
                          kPacketAddress + 0x10u,
                          0xA5000000u | kInitSequence);
            writeGuestU32(env.rdram.data(),
                          kPacketAddress + 0x14u,
                          0x20310000u + kInitSequence * 0x40u);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x18u, kInitSequence);
            writeGuestU32(env.rdram.data(),
                          kPacketAddress + 0x1Cu,
                          kFileIoClientAddress);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x20u, kInitFunction);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x24u, 4u);
            writeGuestU32(env.rdram.data(),
                          kPacketAddress + 0x28u,
                          kInitReceiveAddress);
            writeGuestU32(env.rdram.data(), kPacketAddress + 0x2Cu, 4u);

            const std::array<Ps2SifDmaTransfer, 2> callDescriptors{{
                {kInitSendAddress, 0u, 4, 0},
                {kPacketAddress, 0u, 0x40, 0x44},
            }};
            std::memcpy(env.rdram.data() + kDescriptorAddress,
                        callDescriptors.data(),
                        sizeof(callDescriptors));

            setRegU32(env.ctx, 4, kDescriptorAddress);
            setRegU32(env.ctx, 5, 2u);
            ps2_stubs::sceSifSetDma(env.rdram.data(), &env.ctx, &env.runtime);
            t.IsTrue(getRegS32(env.ctx, 2) > 0,
                     "raw FILEIO init should complete through SIF DMA");
            t.Equals(readGuestU32(env.rdram.data(), kInitReceiveAddress),
                     0u,
                     "FILEIO init should return zero through its exact four-byte response");
            t.Equals(readGuestU32(env.rdram.data(), kInboundAddress + 0x18u),
                     kInitSequence,
                     "FILEIO init completion should preserve Duelists sequence 0xee");
            t.Equals(readGuestU32(env.rdram.data(), kInboundAddress + 0x20u),
                     kCallCommand,
                     "FILEIO init completion should identify RPC_CALL");
        });
    });
}
