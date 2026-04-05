#include "Common.h"
#include "Compatibility.h"
#include "ps2_log.h"

namespace ps2_stubs
{
    void calloc_r(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t count = getRegU32(ctx, 5); // $a1
        const uint32_t size = getRegU32(ctx, 6);  // $a2
        const uint32_t guestAddr = runtime ? runtime->guestCalloc(count, size) : 0u;
        setReturnU32(ctx, guestAddr);
    }

    void ret0(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnU32(ctx, 0u);
        ctx->pc = getRegU32(ctx, 31);
    }

    void ret1(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnU32(ctx, 1u);
        ctx->pc = getRegU32(ctx, 31);
    }

    void reta0(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnU32(ctx, getRegU32(ctx, 4));
        ctx->pc = getRegU32(ctx, 31);
    }

    void free_r(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t guestAddr = getRegU32(ctx, 5); // $a1
        if (runtime && guestAddr != 0u)
        {
            runtime->guestFree(guestAddr);
        }
    }

    void malloc_r(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t size = getRegU32(ctx, 5); // $a1
        const uint32_t guestAddr = runtime ? runtime->guestMalloc(size) : 0u;
        setReturnU32(ctx, guestAddr);
    }

    void malloc_trim_r(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 0);
    }

    void mbtowc_r(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t wcAddr = getRegU32(ctx, 5);                 // $a1
        const uint32_t strAddr = getRegU32(ctx, 6);                // $a2
        const int32_t n = static_cast<int32_t>(getRegU32(ctx, 7)); // $a3
        if (n <= 0 || strAddr == 0u)
        {
            setReturnS32(ctx, 0);
            return;
        }

        const uint8_t *src = getConstMemPtr(rdram, strAddr);
        if (!src)
        {
            setReturnS32(ctx, -1);
            return;
        }

        const uint8_t ch = *src;
        if (wcAddr != 0u)
        {
            if (uint8_t *dst = getMemPtr(rdram, wcAddr))
            {
                const uint32_t out = static_cast<uint32_t>(ch);
                std::memcpy(dst, &out, sizeof(out));
            }
        }
        setReturnS32(ctx, (ch == 0u) ? 0 : 1);
    }

    void printf_r(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t format_addr = getRegU32(ctx, 5); // $a1
        const std::string formatOwned = readPs2CStringBounded(rdram, runtime, format_addr, 1024);
        int ret = -1;

        if (format_addr != 0)
        {
            std::string rendered = formatPs2StringWithArgs(rdram, ctx, runtime, formatOwned.c_str(), 2);
            if (rendered.size() > 2048)
            {
                rendered.resize(2048);
            }
            PS2_IF_AGRESSIVE_LOGS({
                const std::string logLine = sanitizeForLog(rendered);
                uint32_t count = 0;
                {
                    std::lock_guard<std::mutex> lock(g_printfLogMutex);
                    count = ++g_printfLogCount;
                }
                if (count <= kMaxPrintfLogs)
                {
                    RUNTIME_LOG("PS2 printf: " << logLine);
                    RUNTIME_LOG(std::flush);
                }
                else if (count == kMaxPrintfLogs + 1)
                {
                    std::cerr << "PS2 printf logging suppressed after " << kMaxPrintfLogs << " lines" << std::endl;
                }
            });
            ret = static_cast<int>(rendered.size());
        }
        else
        {
            std::cerr << "printf_r error: Invalid format string address provided: 0x" << std::hex << format_addr << std::dec << std::endl;
        }

        setReturnS32(ctx, ret);
    }
}
