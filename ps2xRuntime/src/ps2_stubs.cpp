#include "ps2_stubs.h"
#include "ps2_runtime.h"
#include <iostream>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <unordered_map>
#include <filesystem>

namespace ps2_stubs
{

    void ps2_stubs::malloc(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void ps2_stubs::free(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void ps2_stubs::calloc(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void ps2_stubs::realloc(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void ps2_stubs::memcpy(uint8_t *rdram, R5900Context *ctx)
    {
        uint32_t destAddr = getRegU32(ctx, 4); // $a0
        uint32_t srcAddr = getRegU32(ctx, 5);  // $a1
        size_t size = getRegU32(ctx, 6);       // $a2

        uint8_t *hostDest = getMemPtr(rdram, destAddr);
        const uint8_t *hostSrc = getConstMemPtr(rdram, srcAddr);

        if (hostDest && hostSrc)
        {
            ::memcpy(hostDest, hostSrc, size);
        }
        else
        {
            std::cerr << "memcpy error: Attempted copy involving non-RDRAM address (or invalid RDRAM address)."
                      << " Dest: 0x" << std::hex << destAddr << " (host ptr valid: " << (hostDest != nullptr) << ")"
                      << ", Src: 0x" << srcAddr << " (host ptr valid: " << (hostSrc != nullptr) << ")" << std::dec
                      << ", Size: " << size << std::endl;
        }

        ctx->r[2] = ctx->r[4]; // Return dest pointer ($v0 = $a0)
    }

    void ps2_stubs::memset(uint8_t *rdram, R5900Context *ctx)
    {
        uint32_t destAddr = getRegU32(ctx, 4);       // $a0
        int value = (int)(getRegU32(ctx, 5) & 0xFF); // $a1 (char value)
        uint32_t size = getRegU32(ctx, 6);           // $a2

        uint8_t *hostDest = getMemPtr(rdram, destAddr);

        if (hostDest)
        {
            ::memset(hostDest, value, size);
        }
        else
        {
            std::cerr << "memset error: Invalid address provided." << std::endl;
        }

        ctx->r[2] = ctx->r[4]; // Return dest pointer ($v0 = $a0)
    }

    void ps2_stubs::memmove(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void ps2_stubs::memcmp(uint8_t *rdram, R5900Context *ctx)
    {
        uint32_t ptr1Addr = getRegU32(ctx, 4); // $a0
        uint32_t ptr2Addr = getRegU32(ctx, 5); // $a1
        uint32_t size = getRegU32(ctx, 6);     // $a2

        const uint8_t *hostPtr1 = getConstMemPtr(rdram, ptr1Addr);
        const uint8_t *hostPtr2 = getConstMemPtr(rdram, ptr2Addr);
        int result = 0; // Default if pointers are bad

        if (hostPtr1 && hostPtr2)
        {
            result = ::memcmp(hostPtr1, hostPtr2, size);
        }
        else
        {
            std::cerr << "memcmp error: Invalid address provided." << std::endl;
            result = 1;
        }
        setReturnS32(ctx, result);
    }

    void ps2_stubs::strcpy(uint8_t *rdram, R5900Context *ctx)
    {
        uint32_t destAddr = getRegU32(ctx, 4); // $a0
        uint32_t srcAddr = getRegU32(ctx, 5);  // $a1

        char *hostDest = reinterpret_cast<char *>(getMemPtr(rdram, destAddr));
        const char *hostSrc = reinterpret_cast<const char *>(getConstMemPtr(rdram, srcAddr));

        if (hostDest && hostSrc)
        {
            ::strcpy(hostDest, hostSrc);
        }
        else
        {
            std::cerr << "strcpy error: Invalid address provided." << std::endl;
        }

        ctx->r[2] = ctx->r[4]; // Return dest pointer ($v0 = $a0)
    }

    void ps2_stubs::strncpy(uint8_t *rdram, R5900Context *ctx)
    {
        uint32_t destAddr = getRegU32(ctx, 4); // $a0
        uint32_t srcAddr = getRegU32(ctx, 5);  // $a1
        uint32_t size = getRegU32(ctx, 6);     // $a2

        char *hostDest = reinterpret_cast<char *>(getMemPtr(rdram, destAddr));
        const char *hostSrc = reinterpret_cast<const char *>(getConstMemPtr(rdram, srcAddr));

        if (hostDest && hostSrc)
        {
            ::strncpy(hostDest, hostSrc, size);
            // Null termination if possible
            if (size > 0)
                hostDest[size - 1] = '\0';
        }
        else
        {
            std::cerr << "strncpy error: Invalid address provided." << std::endl;
        }
        ctx->r[2] = ctx->r[4]; // Return dest pointer ($v0 = $a0)
    }

    void ps2_stubs::strlen(uint8_t *rdram, R5900Context *ctx)
    {
        uint32_t strAddr = getRegU32(ctx, 4); // $a0
        const char *hostStr = reinterpret_cast<const char *>(getConstMemPtr(rdram, strAddr));
        size_t len = 0;

        if (hostStr)
        {
            len = ::strlen(hostStr);
        }
        else
        {
            std::cerr << "strlen error: Invalid address provided." << std::endl;
        }
        setReturnU32(ctx, (uint32_t)len);
    }

    void ps2_stubs::strcmp(uint8_t *rdram, R5900Context *ctx)
    {
        uint32_t str1Addr = getRegU32(ctx, 4); // $a0
        uint32_t str2Addr = getRegU32(ctx, 5); // $a1

        const char *hostStr1 = reinterpret_cast<const char *>(getConstMemPtr(rdram, str1Addr));
        const char *hostStr2 = reinterpret_cast<const char *>(getConstMemPtr(rdram, str2Addr));
        int result = 0; // Default if pointers bad

        if (hostStr1 && hostStr2)
        {
            result = ::strcmp(hostStr1, hostStr2);
        }
        else
        {
            std::cerr << "strcmp error: Invalid address provided." << std::endl;
            result = 1; // Indicate difference on error
        }
        setReturnS32(ctx, result);
    }

    void ps2_stubs::strncmp(uint8_t *rdram, R5900Context *ctx)
    {
        uint32_t str1Addr = getRegU32(ctx, 4); // $a0
        uint32_t str2Addr = getRegU32(ctx, 5); // $a1
        uint32_t size = getRegU32(ctx, 6);     // $a2

        const char *hostStr1 = reinterpret_cast<const char *>(getConstMemPtr(rdram, str1Addr));
        const char *hostStr2 = reinterpret_cast<const char *>(getConstMemPtr(rdram, str2Addr));
        int result = 0; // Default if pointers bad

        if (hostStr1 && hostStr2)
        {
            result = ::strncmp(hostStr1, hostStr2, size);
        }
        else
        {
            std::cerr << "strncmp error: Invalid address provided." << std::endl;
            result = 1;
        }
        setReturnS32(ctx, result);
    }

    void ps2_stubs::strcat(uint8_t *rdram, R5900Context *ctx)
    {
        uint32_t destAddr = getRegU32(ctx, 4); // $a0
        uint32_t srcAddr = getRegU32(ctx, 5);  // $a1

        char *hostDest = reinterpret_cast<char *>(getMemPtr(rdram, destAddr));
        const char *hostSrc = reinterpret_cast<const char *>(getConstMemPtr(rdram, srcAddr));

        if (hostDest && hostSrc)
        {
            ::strcat(hostDest, hostSrc);
        }
        else
        {
            std::cerr << "strcat error: Invalid address provided." << std::endl;
        }

        ctx->r[2] = ctx->r[4]; // Return dest pointer ($v0 = $a0)
    }

    void ps2_stubs::strncat(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void ps2_stubs::strchr(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void ps2_stubs::strrchr(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void ps2_stubs::strstr(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void ps2_stubs::printf(uint8_t *rdram, R5900Context *ctx)
    {
        uint32_t format_addr = getRegU32(ctx, 4); // $a0

        // We can't easily handle variable arguments in this simple implementation,
        // Check if the address is within RDRAM

        if ((format_addr & PS2_RAM_MASK) == 0)
        {
            const char *format = (const char *)(rdram + (format_addr & 0x1FFFFFF));
            ::printf("PS2 printf: %s\n", format);
        }
        else
        {
            std::cerr << "strcat error: Invalid address provided." << std::endl;
        }

        setReturnS32(ctx, (int32_t)1); // TODO fix this later
    }

    void ps2_stubs::sprintf(uint8_t *rdram, R5900Context *ctx)
    {
        uint32_t str_addr = getRegU32(ctx, 4);    // $a0
        uint32_t format_addr = getRegU32(ctx, 5); // $a1

        // We can't easily handle variable arguments in this simple implementation,
        // so we'll just copy the format string to the destination without substituting any arguments

        if ((str_addr & PS2_RAM_MASK) == 0 && (format_addr & PS2_RAM_MASK) == 0)
        {
            char *str = (char *)(rdram + (str_addr & 0x1FFFFFF));
            const char *format = (const char *)(rdram + (format_addr & 0x1FFFFFF));
            ::strcpy(str, format);
        }
        else
        {
            std::cerr << "strcat error: Invalid address provided." << std::endl;
        }

        setReturnS32(ctx, 0);
    }

    void ps2_stubs::snprintf(uint8_t *rdram, R5900Context *ctx)
    {
        uint32_t str_addr = getRegU32(ctx, 4);    // $a0
        size_t size = getRegU32(ctx, 5);          // $a1
        uint32_t format_addr = getRegU32(ctx, 6); // $a2

        // We can't easily handle variable arguments in this simple implementation,
        // so we'll just copy the format string to the destination without substituting any arguments

        // Check if the addresses are within RDRAM
        if ((str_addr & PS2_RAM_MASK) == 0 && (format_addr & PS2_RAM_MASK) == 0)
        {
            // Addresses are within RDRAM, use direct access
            char *str = (char *)(rdram + (str_addr & 0x1FFFFFF));
            const char *format = (const char *)(rdram + (format_addr & 0x1FFFFFF));
            ::strncpy(str, format, size);
            str[size - 1] = '\0'; // Ensure null termination
        }
        else
        {
            std::cerr << "strcat error: Invalid address provided." << std::endl;
        }

        setReturnS32(ctx, 0);
    }

    void ps2_stubs::puts(uint8_t *rdram, R5900Context *ctx)
    {
        uint32_t strAddr = getRegU32(ctx, 4); // $a0
        const char *hostStr = reinterpret_cast<const char *>(getConstMemPtr(rdram, strAddr));

        int result = -1;
        if (hostStr)
        {
            result = std::puts(hostStr);
        }
        else
        {
            std::cerr << "puts error: Invalid address provided." << std::endl;
        }

        setReturnS32(ctx, result >= 0 ? 0 : -1); // Return 0 on success, -1 on error like PS2 libs might
    }

    void ps2_stubs::fopen(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void ps2_stubs::fclose(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void ps2_stubs::fread(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void ps2_stubs::fwrite(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void ps2_stubs::fprintf(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void ps2_stubs::fseek(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void ps2_stubs::ftell(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void ps2_stubs::fflush(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void ps2_stubs::sqrt(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void ps2_stubs::sin(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void ps2_stubs::cos(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void ps2_stubs::tan(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void ps2_stubs::atan2(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void ps2_stubs::pow(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void ps2_stubs::exp(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void ps2_stubs::log(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void ps2_stubs::log10(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void ps2_stubs::ceil(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void ps2_stubs::floor(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void ps2_stubs::fabs(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void ps2_stubs::TODO(uint8_t *rdram, R5900Context *ctx)
    {
        uint32_t stub_num = getRegU32(ctx, 2); // $v0 often holds stub num before call
        std::cerr << "Warning: Unimplemented stub called. PC=0x" << std::hex << ctx->pc
                  << " stub # (approx): 0x" << stub_num << std::dec << std::endl;
        setReturnS32(ctx, -1); // Return error
    }
}