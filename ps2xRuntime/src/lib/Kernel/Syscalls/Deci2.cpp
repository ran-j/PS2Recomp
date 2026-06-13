#include "System.h"
#include "Common.h"
#include "ps2_runtime.h"

namespace
{
    struct Deci2Session
    {
        uint16_t protocol = 0;
        uint32_t opt = 0;
        uint32_t handler = 0;
        uint32_t userArea = 0;
        bool locked = false;
    };

    std::mutex g_deci2Mutex;
    std::unordered_map<int32_t, Deci2Session> g_deci2Sessions;
    int32_t g_nextDeci2Socket = 1;
    std::atomic<uint32_t> g_deci2LogCount{0};

    static bool readDeci2U32(uint8_t *rdram, uint32_t guestAddr, uint32_t &value)
    {
        value = 0;

        const uint8_t *ptr = getConstMemPtr(rdram, guestAddr);
        if (!ptr)
        {
            return false;
        }

        std::memcpy(&value, ptr, sizeof(value));
        return true;
    }

    static void readDeci2Args(uint8_t *rdram, uint32_t argsAddr, uint32_t args[4])
    {
        for (uint32_t argIndex = 0; argIndex < 4; argIndex++)
        {
            uint32_t value = 0;
            readDeci2U32(rdram, argsAddr + (argIndex * sizeof(uint32_t)), value);
            args[argIndex] = value;
        }
    }

    static std::string sanitizeDeci2Text(std::string text)
    {
        for (char &ch : text)
        {
            const unsigned char value = static_cast<unsigned char>(ch);
            if (ch == '\r')
            {
                ch = '\n';
                continue;
            }

            if (ch != '\n' && ch != '\t' && !std::isprint(value))
            {
                ch = '.';
            }
        }

        return text;
    }

    static void logDeci2Text(const char *prefix, const std::string &text)
    {
        constexpr uint32_t kMaxDeci2TextLogs = 256u;
        const uint32_t logIndex = g_deci2LogCount.fetch_add(1u, std::memory_order_relaxed);
        if (logIndex >= kMaxDeci2TextLogs)
        {
            return;
        }

        const std::string safeText = sanitizeDeci2Text(text);
        std::cerr << prefix << safeText;
        if (safeText.empty() || safeText.back() != '\n')
        {
            std::cerr << std::endl;
        }
    }

    static int32_t allocateDeci2Socket(uint16_t protocol, uint32_t opt, uint32_t handler, uint32_t userArea)
    {
        std::lock_guard<std::mutex> lock(g_deci2Mutex);

        if (g_nextDeci2Socket <= 0)
        {
            g_nextDeci2Socket = 1;
        }

        const int32_t socket = g_nextDeci2Socket;
        Deci2Session session;
        session.protocol = protocol;
        session.opt = opt;
        session.handler = handler;
        session.userArea = userArea;
        session.locked = false;

        g_deci2Sessions[socket] = session;
        return socket;
    }

    static bool updateDeci2LockState(int32_t socket, bool locked)
    {
        std::lock_guard<std::mutex> lock(g_deci2Mutex);

        auto sessionIt = g_deci2Sessions.find(socket);
        if (sessionIt == g_deci2Sessions.end())
        {
            return false;
        }

        sessionIt->second.locked = locked;
        return true;
    }

    static bool closeDeci2Socket(int32_t socket)
    {
        std::lock_guard<std::mutex> lock(g_deci2Mutex);
        return g_deci2Sessions.erase(socket) != 0;
    }
}

namespace ps2_syscalls
{
    void Deci2Call(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
#if defined(_DEBUG) || defined(RUNTIME_DECI2CALL)
        const int32_t code = static_cast<int32_t>(getRegU32(ctx, 4));
        const uint32_t argsAddr = getRegU32(ctx, 5);

        uint32_t args[4] = {};
        readDeci2Args(rdram, argsAddr, args);

        switch (code)
        {
        case 1: // sceDeci2Open(protocol, opt, handler)
        {
            const uint16_t protocol = static_cast<uint16_t>(args[0] & 0xFFFFu);
            const uint32_t opt = args[1];
            const uint32_t handler = args[2];
            const uint32_t userArea = args[3];

            const int32_t socket = allocateDeci2Socket(protocol, opt, handler, userArea);
            setReturnS32(ctx, socket);
            return;
        }
        case 2: // sceDeci2Close(socket)
        {
            const int32_t socket = static_cast<int32_t>(args[0]);
            setReturnS32(ctx, closeDeci2Socket(socket) ? KE_OK : KE_ERROR);
            return;
        }
        // WE dont need to do thouses
        case 3:  // sceDeci2ReqSend(socket, dest)
        case 4:  // sceDeci2Poll(socket)
        case -7: // sceDeci2ExReqSend(socket, dest)
        {
            setReturnS32(ctx, KE_OK);
            return;
        }
        case -5: // sceDeci2ExRecv(socket, buf, len)
        {
            // No host debugger channel is attached, so report "no bytes available".
            setReturnS32(ctx, 0);
            return;
        }
        case -6: // sceDeci2ExSend(socket, buf, len)
        {
            const uint32_t bufferAddr = args[1];
            const uint32_t length = args[2] & 0xFFFFu;

            if (bufferAddr != 0u && length != 0u)
            {
                const uint32_t clampedLength = std::min<uint32_t>(length, 512u);
                std::string text;
                text.reserve(clampedLength);
                for (uint32_t byteIndex = 0; byteIndex < clampedLength; byteIndex++)
                {
                    text.push_back(static_cast<char>(rdram[(bufferAddr + byteIndex) & PS2_RAM_MASK]));
                }
                logDeci2Text("[Deci2Call:send] ", text);
            }

            // Treat send as successful and return the amount accepted.
            setReturnS32(ctx, static_cast<int32_t>(length));
            return;
        }
        case -8: // sceDeci2ExLock(socket)
        {
            const int32_t socket = static_cast<int32_t>(args[0]);
            setReturnS32(ctx, updateDeci2LockState(socket, true) ? KE_OK : KE_ERROR);
            return;
        }
        case -9: // sceDeci2ExUnLock(socket)
        {
            const int32_t socket = static_cast<int32_t>(args[0]);
            setReturnS32(ctx, updateDeci2LockState(socket, false) ? KE_OK : KE_ERROR);
            return;
        }
        case 16: // kputs(char *s)
        {
            constexpr size_t kMaxDeci2KputsBytes = 4096u;
            const uint32_t textAddr = args[0];
            const std::string text = readGuestCStringBounded(rdram, textAddr, kMaxDeci2KputsBytes);

            logDeci2Text("[Deci2Call:kputs] ", text);
            setReturnS32(ctx, static_cast<int32_t>(text.size()));
            return;
        }
        default:
        {
            static std::atomic<uint32_t> s_unknownDeci2Logs{0u};
            constexpr uint32_t kMaxUnknownDeci2Logs = 64u;
            const uint32_t logIndex = s_unknownDeci2Logs.fetch_add(1u, std::memory_order_relaxed);
            if (logIndex < kMaxUnknownDeci2Logs)
            {
                std::cerr << "[Deci2Call:unknown]"
                          << " code=" << code
                          << " args=0x" << std::hex << argsAddr
                          << " a0=0x" << args[0]
                          << " a1=0x" << args[1]
                          << " a2=0x" << args[2]
                          << " a3=0x" << args[3]
                          << " pc=0x" << ctx->pc
                          << std::dec << std::endl;
            }

            setReturnS32(ctx, KE_OK);
            return;
        }
        }
#endif
    }
}
