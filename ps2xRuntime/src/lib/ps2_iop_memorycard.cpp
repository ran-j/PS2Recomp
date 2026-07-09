#include "runtime/ps2_iop_memorycard.h"

#include "Kernel/Stubs/MemoryCard.h"
#include "ps2_runtime.h"
#include "runtime/ps2_iop.h"
#include "runtime/ps2_memory.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <iostream>

namespace
{
    constexpr int32_t kMcResultSucceed = 0;
    constexpr int32_t kMcResultNoFormat = -2;
    constexpr int32_t kMcResultNoEntry = -4;
    constexpr int32_t kMcResultDeniedPermit = -5;

    constexpr int32_t kMcTypePs2 = 2;
    constexpr int32_t kMcFormatted = 1;
    constexpr int32_t kMcUnformatted = 0;
    constexpr int32_t kMcFreeClusters = 0x2000;

    constexpr uint32_t kMcservVersion = 0x0205u;
    constexpr uint32_t kMcmanVersion = 0x0206u;
    constexpr uint32_t kMcFileCreateDir = 0x0040u;

    constexpr uint32_t kNewCmdInit = 0xFEu;
    constexpr uint32_t kNewCmdGetInfo = 0x01u;
    constexpr uint32_t kNewCmdOpen = 0x02u;
    constexpr uint32_t kNewCmdClose = 0x03u;
    constexpr uint32_t kNewCmdSeek = 0x04u;
    constexpr uint32_t kNewCmdRead = 0x05u;
    constexpr uint32_t kNewCmdWrite = 0x06u;
    constexpr uint32_t kNewCmdFlush = 0x0Au;
    constexpr uint32_t kNewCmdChDir = 0x0Cu;
    constexpr uint32_t kNewCmdGetDir = 0x0Du;
    constexpr uint32_t kNewCmdSetInfo = 0x0Eu;
    constexpr uint32_t kNewCmdDelete = 0x0Fu;
    constexpr uint32_t kNewCmdFormat = 0x10u;
    constexpr uint32_t kNewCmdUnformat = 0x11u;
    constexpr uint32_t kNewCmdGetEnt = 0x12u;
    constexpr uint32_t kNewCmdChgPrio = 0x14u;

    constexpr uint32_t kOldCmdInit = 0x70u;
    constexpr uint32_t kOldCmdOpen = 0x71u;
    constexpr uint32_t kOldCmdClose = 0x72u;
    constexpr uint32_t kOldCmdRead = 0x73u;
    constexpr uint32_t kOldCmdWrite = 0x74u;
    constexpr uint32_t kOldCmdSeek = 0x75u;
    constexpr uint32_t kOldCmdGetDir = 0x76u;
    constexpr uint32_t kOldCmdFormat = 0x77u;
    constexpr uint32_t kOldCmdGetInfo = 0x78u;
    constexpr uint32_t kOldCmdDelete = 0x79u;
    constexpr uint32_t kOldCmdFlush = 0x7Au;
    constexpr uint32_t kOldCmdChDir = 0x7Bu;
    constexpr uint32_t kOldCmdSetInfo = 0x7Cu;
    constexpr uint32_t kOldCmdUnformat = 0x80u;

    enum class McRpcOp
    {
        Init,
        GetInfo,
        Open,
        Close,
        Seek,
        Read,
        Write,
        Flush,
        ChDir,
        GetDir,
        SetInfo,
        Delete,
        Format,
        Unformat,
        GetEnt,
        ChangePriority,
        Unknown,
    };

    enum class McRpcFlavor
    {
        OldMcserv,
        NewXmcserv,
    };

#pragma pack(push, 1)
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

    struct McNameParam
    {
        int32_t port;
        int32_t slot;
        int32_t flags;
        int32_t maxent;
        uint32_t ptr;
        char name[1024];
    };
#pragma pack(pop)

    static_assert(sizeof(McDescParam) == 48u, "mcDescParam_t size mismatch");
    static_assert(sizeof(McNameParam) == 1044u, "libmc name param size mismatch");

    uint32_t g_unknownRpcLogCount = 0u;

    bool isMemoryCardSid(uint32_t sid)
    {
        return sid == IOP_SID_MCSERV || sid == IOP_SID_MCSERV_DEV9;
    }

    McRpcOp decodeRpcOp(uint32_t rpcNum, McRpcFlavor &flavor)
    {
        switch (rpcNum)
        {
        case kNewCmdInit:
            flavor = McRpcFlavor::NewXmcserv;
            return McRpcOp::Init;
        case kNewCmdGetInfo:
            flavor = McRpcFlavor::NewXmcserv;
            return McRpcOp::GetInfo;
        case kNewCmdOpen:
            flavor = McRpcFlavor::NewXmcserv;
            return McRpcOp::Open;
        case kNewCmdClose:
            flavor = McRpcFlavor::NewXmcserv;
            return McRpcOp::Close;
        case kNewCmdSeek:
            flavor = McRpcFlavor::NewXmcserv;
            return McRpcOp::Seek;
        case kNewCmdRead:
            flavor = McRpcFlavor::NewXmcserv;
            return McRpcOp::Read;
        case kNewCmdWrite:
            flavor = McRpcFlavor::NewXmcserv;
            return McRpcOp::Write;
        case kNewCmdFlush:
            flavor = McRpcFlavor::NewXmcserv;
            return McRpcOp::Flush;
        case kNewCmdChDir:
            flavor = McRpcFlavor::NewXmcserv;
            return McRpcOp::ChDir;
        case kNewCmdGetDir:
            flavor = McRpcFlavor::NewXmcserv;
            return McRpcOp::GetDir;
        case kNewCmdSetInfo:
            flavor = McRpcFlavor::NewXmcserv;
            return McRpcOp::SetInfo;
        case kNewCmdDelete:
            flavor = McRpcFlavor::NewXmcserv;
            return McRpcOp::Delete;
        case kNewCmdFormat:
            flavor = McRpcFlavor::NewXmcserv;
            return McRpcOp::Format;
        case kNewCmdUnformat:
            flavor = McRpcFlavor::NewXmcserv;
            return McRpcOp::Unformat;
        case kNewCmdGetEnt:
            flavor = McRpcFlavor::NewXmcserv;
            return McRpcOp::GetEnt;
        case kNewCmdChgPrio:
            flavor = McRpcFlavor::NewXmcserv;
            return McRpcOp::ChangePriority;

        case kOldCmdInit:
            flavor = McRpcFlavor::OldMcserv;
            return McRpcOp::Init;
        case kOldCmdGetInfo:
            flavor = McRpcFlavor::OldMcserv;
            return McRpcOp::GetInfo;
        case kOldCmdOpen:
            flavor = McRpcFlavor::OldMcserv;
            return McRpcOp::Open;
        case kOldCmdClose:
            flavor = McRpcFlavor::OldMcserv;
            return McRpcOp::Close;
        case kOldCmdSeek:
            flavor = McRpcFlavor::OldMcserv;
            return McRpcOp::Seek;
        case kOldCmdRead:
            flavor = McRpcFlavor::OldMcserv;
            return McRpcOp::Read;
        case kOldCmdWrite:
            flavor = McRpcFlavor::OldMcserv;
            return McRpcOp::Write;
        case kOldCmdFlush:
            flavor = McRpcFlavor::OldMcserv;
            return McRpcOp::Flush;
        case kOldCmdChDir:
            flavor = McRpcFlavor::OldMcserv;
            return McRpcOp::ChDir;
        case kOldCmdGetDir:
            flavor = McRpcFlavor::OldMcserv;
            return McRpcOp::GetDir;
        case kOldCmdSetInfo:
            flavor = McRpcFlavor::OldMcserv;
            return McRpcOp::SetInfo;
        case kOldCmdDelete:
            flavor = McRpcFlavor::OldMcserv;
            return McRpcOp::Delete;
        case kOldCmdFormat:
            flavor = McRpcFlavor::OldMcserv;
            return McRpcOp::Format;
        case kOldCmdUnformat:
            flavor = McRpcFlavor::OldMcserv;
            return McRpcOp::Unformat;
        default:
            return McRpcOp::Unknown;
        }
    }

    void setRegU32(R5900Context &ctx, int reg, uint32_t value)
    {
        ctx.r[reg] = _mm_set_epi64x(0, static_cast<int64_t>(static_cast<int32_t>(value)));
    }

    template <typename T>
    bool readGuestStruct(uint8_t *rdram, uint32_t addr, T &out)
    {
        const uint8_t *ptr = getConstMemPtr(rdram, addr);
        if (!ptr)
        {
            return false;
        }

        std::memcpy(&out, ptr, sizeof(out));
        return true;
    }

    template <typename T>
    bool writeGuestStruct(uint8_t *rdram, uint32_t addr, const T &value)
    {
        uint8_t *ptr = getMemPtr(rdram, addr);
        if (!ptr)
        {
            return false;
        }

        std::memcpy(ptr, &value, sizeof(value));
        return true;
    }

    void zeroGuest(uint8_t *rdram, uint32_t addr, uint32_t size)
    {
        if (!addr || size == 0u)
        {
            return;
        }

        if (uint8_t *ptr = getMemPtr(rdram, addr))
        {
            std::memset(ptr, 0, size);
        }
    }

    void writeGuestS32(uint8_t *rdram, uint32_t addr, int32_t value)
    {
        (void)writeGuestStruct(rdram, addr, value);
    }

    void writeResult(uint8_t *rdram, uint32_t recvBufAddr, uint32_t recvSize, int32_t result)
    {
        if (recvBufAddr == 0u || recvSize < sizeof(int32_t))
        {
            return;
        }

        writeGuestS32(rdram, recvBufAddr, result);
        if (recvSize > sizeof(int32_t))
        {
            zeroGuest(rdram,
                      recvBufAddr + static_cast<uint32_t>(sizeof(int32_t)),
                      recvSize - static_cast<uint32_t>(sizeof(int32_t)));
        }
    }

    int32_t lastMemoryCardResult()
    {
        return ps2_stubs::getMemoryCardDebugSnapshot().lastResult;
    }

    int32_t callMcStub(uint8_t *rdram,
                       PS2Runtime *runtime,
                       void (*fn)(uint8_t *, R5900Context *, PS2Runtime *),
                       uint32_t a0 = 0u,
                       uint32_t a1 = 0u,
                       uint32_t a2 = 0u,
                       uint32_t a3 = 0u)
    {
        R5900Context ctx{};
        setRegU32(ctx, 4, a0);
        setRegU32(ctx, 5, a1);
        setRegU32(ctx, 6, a2);
        setRegU32(ctx, 7, a3);
        fn(rdram, &ctx, runtime);
        return lastMemoryCardResult();
    }

    int32_t handleInit(uint8_t *rdram, PS2Runtime *runtime, uint32_t recvBufAddr, uint32_t recvSize)
    {
        R5900Context ctx{};
        ps2_stubs::sceMcInit(rdram, &ctx, runtime);

        if (recvBufAddr != 0u && recvSize >= sizeof(int32_t))
        {
            writeGuestS32(rdram, recvBufAddr + 0u, kMcResultSucceed);
            if (recvSize >= 12u)
            {
                (void)writeGuestStruct(rdram, recvBufAddr + 4u, kMcservVersion);
                (void)writeGuestStruct(rdram, recvBufAddr + 8u, kMcmanVersion);
            }
            else if (recvSize > sizeof(int32_t))
            {
                zeroGuest(rdram,
                          recvBufAddr + static_cast<uint32_t>(sizeof(int32_t)),
                          recvSize - static_cast<uint32_t>(sizeof(int32_t)));
            }
            if (recvSize > 12u)
            {
                zeroGuest(rdram, recvBufAddr + 12u, recvSize - 12u);
            }
        }

        return kMcResultSucceed;
    }

    bool getPortState(int32_t port, bool &formatted)
    {
        const ps2_stubs::MemoryCardDebugSnapshot snapshot = ps2_stubs::getMemoryCardDebugSnapshot();
        for (const ps2_stubs::MemoryCardDebugPort &row : snapshot.ports)
        {
            if (row.port == port)
            {
                formatted = row.formatted;
                return true;
            }
        }
        return false;
    }

    int32_t handleGetInfo(uint8_t *rdram,
                          const McDescParam &desc,
                          McRpcFlavor flavor)
    {
        int32_t cardType = 0;
        int32_t freeBlocks = 0;
        int32_t format = kMcUnformatted;
        int32_t result = kMcResultNoEntry;

        bool formatted = false;
        if (desc.slot == 0 && getPortState(desc.port, formatted))
        {
            cardType = kMcTypePs2;
            freeBlocks = formatted ? kMcFreeClusters : 0;
            format = formatted ? kMcFormatted : kMcUnformatted;
            result = formatted ? kMcResultSucceed : kMcResultNoFormat;
        }

        if (desc.param != 0u)
        {
            if (flavor == McRpcFlavor::NewXmcserv)
            {
                zeroGuest(rdram, desc.param, 192u);
                writeGuestS32(rdram, desc.param + 0u, cardType);
                writeGuestS32(rdram, desc.param + 4u, freeBlocks);
                writeGuestS32(rdram, desc.param + 144u, format);
            }
            else
            {
                zeroGuest(rdram, desc.param, 64u);
                writeGuestS32(rdram, desc.param + 0u, cardType);
                writeGuestS32(rdram, desc.param + 4u, freeBlocks);
            }
        }

        return result;
    }

    int32_t handleNameOpen(uint8_t *rdram, PS2Runtime *runtime, uint32_t sendBufAddr, const McNameParam &name)
    {
        const uint32_t nameAddr = sendBufAddr + static_cast<uint32_t>(offsetof(McNameParam, name));
        if ((static_cast<uint32_t>(name.flags) & kMcFileCreateDir) != 0u)
        {
            return callMcStub(rdram,
                              runtime,
                              ps2_stubs::sceMcMkdir,
                              static_cast<uint32_t>(name.port),
                              static_cast<uint32_t>(name.slot),
                              nameAddr);
        }

        return callMcStub(rdram,
                          runtime,
                          ps2_stubs::sceMcOpen,
                          static_cast<uint32_t>(name.port),
                          static_cast<uint32_t>(name.slot),
                          nameAddr,
                          static_cast<uint32_t>(name.flags));
    }

    int32_t handleNameCommand(uint8_t *rdram,
                              PS2Runtime *runtime,
                              McRpcOp op,
                              uint32_t sendBufAddr,
                              const McNameParam &name)
    {
        const uint32_t nameAddr = sendBufAddr + static_cast<uint32_t>(offsetof(McNameParam, name));
        switch (op)
        {
        case McRpcOp::ChDir:
            return callMcStub(rdram,
                              runtime,
                              ps2_stubs::sceMcChdir,
                              static_cast<uint32_t>(name.port),
                              static_cast<uint32_t>(name.slot),
                              nameAddr,
                              name.ptr);
        case McRpcOp::SetInfo:
            return callMcStub(rdram,
                              runtime,
                              ps2_stubs::sceMcSetFileInfo,
                              static_cast<uint32_t>(name.port),
                              static_cast<uint32_t>(name.slot),
                              nameAddr);
        case McRpcOp::Delete:
            return callMcStub(rdram,
                              runtime,
                              ps2_stubs::sceMcDelete,
                              static_cast<uint32_t>(name.port),
                              static_cast<uint32_t>(name.slot),
                              nameAddr);
        case McRpcOp::GetDir:
            // The real service DMA-writes directory entries to name.ptr. Returning
            // zero entries keeps callers progressing without inventing save data.
            return kMcResultSucceed;
        case McRpcOp::GetEnt:
            return 1024;
        default:
            return kMcResultDeniedPermit;
        }
    }

    int32_t handleDescCommand(uint8_t *rdram,
                              PS2Runtime *runtime,
                              McRpcOp op,
                              const McDescParam &desc,
                              McRpcFlavor flavor)
    {
        switch (op)
        {
        case McRpcOp::GetInfo:
            return handleGetInfo(rdram, desc, flavor);
        case McRpcOp::Close:
            return callMcStub(rdram, runtime, ps2_stubs::sceMcClose, static_cast<uint32_t>(desc.fd));
        case McRpcOp::Seek:
            return callMcStub(rdram,
                              runtime,
                              ps2_stubs::sceMcSeek,
                              static_cast<uint32_t>(desc.fd),
                              static_cast<uint32_t>(desc.offset),
                              static_cast<uint32_t>(desc.origin));
        case McRpcOp::Read:
            if (desc.param != 0u)
            {
                zeroGuest(rdram, desc.param, flavor == McRpcFlavor::NewXmcserv ? 192u : 64u);
            }
            return callMcStub(rdram,
                              runtime,
                              ps2_stubs::sceMcRead,
                              static_cast<uint32_t>(desc.fd),
                              desc.buffer,
                              static_cast<uint32_t>(std::max(desc.size, 0)));
        case McRpcOp::Write:
            return callMcStub(rdram,
                              runtime,
                              ps2_stubs::sceMcWrite,
                              static_cast<uint32_t>(desc.fd),
                              desc.buffer,
                              static_cast<uint32_t>(std::max(desc.size, 0)));
        case McRpcOp::Flush:
            return callMcStub(rdram, runtime, ps2_stubs::sceMcFlush, static_cast<uint32_t>(desc.fd));
        case McRpcOp::Format:
            return callMcStub(rdram,
                              runtime,
                              ps2_stubs::sceMcFormat,
                              static_cast<uint32_t>(desc.port),
                              static_cast<uint32_t>(desc.slot));
        case McRpcOp::Unformat:
            return callMcStub(rdram,
                              runtime,
                              ps2_stubs::sceMcUnformat,
                              static_cast<uint32_t>(desc.port),
                              static_cast<uint32_t>(desc.slot));
        case McRpcOp::ChangePriority:
            return kMcResultSucceed;
        default:
            return kMcResultDeniedPermit;
        }
    }

    bool opUsesNameParam(McRpcOp op)
    {
        return op == McRpcOp::Open ||
               op == McRpcOp::ChDir ||
               op == McRpcOp::GetDir ||
               op == McRpcOp::SetInfo ||
               op == McRpcOp::Delete ||
               op == McRpcOp::GetEnt;
    }

    void logUnknownRpc(uint32_t sid, uint32_t rpcNum, uint32_t sendBufAddr, uint32_t sendSize)
    {
        if (g_unknownRpcLogCount >= 32u)
        {
            return;
        }

        std::cerr << "[MCSERV:stub]"
                  << " sid=0x" << std::hex << sid
                  << " rpc=0x" << rpcNum
                  << " send=0x" << sendBufAddr
                  << " sendSize=0x" << sendSize
                  << std::dec << std::endl;
        ++g_unknownRpcLogCount;
    }
}

namespace ps2_iop_memorycard
{
    void reset()
    {
        g_unknownRpcLogCount = 0u;
    }

    bool handleMemoryCardRpc(uint8_t *rdram,
                             PS2Runtime *runtime,
                             uint32_t sid,
                             uint32_t rpcNum,
                             uint32_t sendBufAddr,
                             uint32_t sendSize,
                             uint32_t recvBufAddr,
                             uint32_t recvSize,
                             uint32_t &resultPtr,
                             bool &signalNowaitCompletion)
    {
        if (!isMemoryCardSid(sid))
        {
            return false;
        }

        resultPtr = recvBufAddr;
        signalNowaitCompletion = false;

        if (!rdram)
        {
            return true;
        }

        McRpcFlavor flavor = McRpcFlavor::NewXmcserv;
        const McRpcOp op = decodeRpcOp(rpcNum, flavor);
        int32_t result = kMcResultDeniedPermit;

        if (op == McRpcOp::Init)
        {
            result = handleInit(rdram, runtime, recvBufAddr, recvSize);
            return true;
        }

        if (op == McRpcOp::Unknown)
        {
            logUnknownRpc(sid, rpcNum, sendBufAddr, sendSize);
            writeResult(rdram, recvBufAddr, recvSize, result);
            return true;
        }

        if (opUsesNameParam(op))
        {
            McNameParam name{};
            if (sendBufAddr != 0u && sendSize >= offsetof(McNameParam, name) &&
                readGuestStruct(rdram, sendBufAddr, name))
            {
                if (op == McRpcOp::Open)
                {
                    result = handleNameOpen(rdram, runtime, sendBufAddr, name);
                }
                else
                {
                    result = handleNameCommand(rdram, runtime, op, sendBufAddr, name);
                }
            }
        }
        else
        {
            McDescParam desc{};
            if (sendBufAddr != 0u && sendSize >= sizeof(McDescParam) &&
                readGuestStruct(rdram, sendBufAddr, desc))
            {
                if (op == McRpcOp::Write &&
                    desc.origin > 0 &&
                    desc.origin <= static_cast<int32_t>(sizeof(desc.data)))
                {
                    const uint32_t inlineDataAddr =
                        sendBufAddr + static_cast<uint32_t>(offsetof(McDescParam, data));
                    const int32_t prefix = callMcStub(rdram,
                                                      runtime,
                                                      ps2_stubs::sceMcWrite,
                                                      static_cast<uint32_t>(desc.fd),
                                                      inlineDataAddr,
                                                      static_cast<uint32_t>(desc.origin));
                    if (prefix < 0)
                    {
                        result = prefix;
                    }
                    else
                    {
                        const int32_t body = callMcStub(rdram,
                                                        runtime,
                                                        ps2_stubs::sceMcWrite,
                                                        static_cast<uint32_t>(desc.fd),
                                                        desc.buffer,
                                                        static_cast<uint32_t>(std::max(desc.size, 0)));
                        result = body < 0 ? body : prefix + body;
                    }
                }
                else
                {
                    result = handleDescCommand(rdram, runtime, op, desc, flavor);
                }
            }
        }

        writeResult(rdram, recvBufAddr, recvSize, result);
        return true;
    }
}
