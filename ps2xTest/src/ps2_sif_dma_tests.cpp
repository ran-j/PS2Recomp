#include "MiniTest.h"
#include "ps2_runtime.h"
#include "ps2_syscalls.h"
#include "ps2_stubs.h"

#include <array>
#include <cstdint>
#include <cstring>
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
            ps2_syscalls::resetSoundDriverRpcState();
            ps2_syscalls::clearSoundDriverCompatLayout();
            std::memset(&ctx, 0, sizeof(ctx));
        }
    };

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

        tc.Run("sceSifSetDma lets active PS2RNA playback drain emulated SJRMT data", [](TestCase &t)
        {
            TestEnv env;

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

            constexpr uint32_t kRdAddr = 0x00023300u;
            constexpr uint32_t kDstAddr = 0x00023400u;
            constexpr uint32_t kSize = 0x42u;
            constexpr uint32_t kPrimarySeCheckAddr = 0x01E0EF10u;
            constexpr uint32_t kPrimaryMidiCheckAddr = 0x01E0EF20u;
            constexpr uint32_t kMidiSumOffset = 0x1Eu;
            constexpr uint32_t kSeSumOffset = 0x26u;
            constexpr uint32_t kBank = 1u;

            PS2SoundDriverCompatLayout compat{};
            compat.primarySeCheckAddr = kPrimarySeCheckAddr;
            compat.primaryMidiCheckAddr = kPrimaryMidiCheckAddr;
            ps2_syscalls::setSoundDriverCompatLayout(compat);

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

            constexpr uint32_t kRdAddr = 0x00023700u;
            constexpr uint32_t kDstAddr = 0x00023800u;
            constexpr uint32_t kSize = 0x42u;
            constexpr uint32_t kPrimarySeCheckAddr = 0x01E0EF10u;
            constexpr uint32_t kPrimaryMidiCheckAddr = 0x01E0EF20u;
            constexpr uint32_t kMidiSumOffset = 0x1Eu;
            constexpr uint32_t kSeSumOffset = 0x26u;
            constexpr uint32_t kLiveBank = 0u;
            constexpr uint32_t kPendingBank = 1u;

            PS2SoundDriverCompatLayout compat{};
            compat.primarySeCheckAddr = kPrimarySeCheckAddr;
            compat.primaryMidiCheckAddr = kPrimaryMidiCheckAddr;
            ps2_syscalls::setSoundDriverCompatLayout(compat);

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
    });
}
