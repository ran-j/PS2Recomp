
#include "ps2_stubs.h"


// Stub implementations for standard library functions
namespace ps2_stubs
{
    // Memory operations
    void memcpy(uint8_t *rdram, R5900Context *ctx)
    {
        uint32_t dst = ctx->r[4].m128i_u32[0];  // a0 = destination
        uint32_t src = ctx->r[5].m128i_u32[0];  // a1 = source
        uint32_t size = ctx->r[6].m128i_u32[0]; // a2 = size

        uint32_t physDst = dst & 0x1FFFFFFF;
        uint32_t physSrc = src & 0x1FFFFFFF;

        std::memcpy(rdram + physDst, rdram + physSrc, size);

        ctx->r[2] = _mm_set1_epi32(dst);
    }

    void memset(uint8_t *rdram, R5900Context *ctx)
    {
        uint32_t dst = ctx->r[4].m128i_u32[0];          // a0 = destination
        uint32_t value = ctx->r[5].m128i_u32[0] & 0xFF; // a1 = fill value (byte)
        uint32_t size = ctx->r[6].m128i_u32[0];         // a2 = size

        uint32_t physDst = dst & 0x1FFFFFFF;

        std::memset(rdram + physDst, value, size);

        ctx->r[2] = _mm_set1_epi32(dst);
    }

    void memmove(uint8_t *rdram, R5900Context *ctx)
    {
        uint32_t dst = ctx->r[4].m128i_u32[0];  // a0 = destination
        uint32_t src = ctx->r[5].m128i_u32[0];  // a1 = source
        uint32_t size = ctx->r[6].m128i_u32[0]; // a2 = size

        uint32_t physDst = dst & 0x1FFFFFFF;
        uint32_t physSrc = src & 0x1FFFFFFF;

        std::memmove(rdram + physDst, rdram + physSrc, size);

        ctx->r[2] = _mm_set1_epi32(dst);
    }

    void memcmp(uint8_t *rdram, R5900Context *ctx)
    {
        uint32_t ptr1 = ctx->r[4].m128i_u32[0]; // a0 = first buffer
        uint32_t ptr2 = ctx->r[5].m128i_u32[0]; // a1 = second buffer
        uint32_t size = ctx->r[6].m128i_u32[0]; // a2 = size

        uint32_t phys1 = ptr1 & 0x1FFFFFFF;
        uint32_t phys2 = ptr2 & 0x1FFFFFFF;

        int result = std::memcmp(rdram + phys1, rdram + phys2, size);

        ctx->r[2] = _mm_set1_epi32(result);
    }

    // String operations
    void strcpy(uint8_t *rdram, R5900Context *ctx)
    {
        uint32_t dst = ctx->r[4].m128i_u32[0]; // a0 = destination
        uint32_t src = ctx->r[5].m128i_u32[0]; // a1 = source

        uint32_t physDst = dst & 0x1FFFFFFF;
        uint32_t physSrc = src & 0x1FFFFFFF;

        char *destStr = reinterpret_cast<char *>(rdram + physDst);
        const char *srcStr = reinterpret_cast<const char *>(rdram + physSrc);

        std::strcpy(destStr, srcStr);

        ctx->r[2] = _mm_set1_epi32(dst);
    }

    void strncpy(uint8_t *rdram, R5900Context *ctx)
    {
        uint32_t dst = ctx->r[4].m128i_u32[0]; // a0 = destination
        uint32_t src = ctx->r[5].m128i_u32[0]; // a1 = source
        uint32_t n = ctx->r[6].m128i_u32[0];   // a2 = max length

        uint32_t physDst = dst & 0x1FFFFFFF;
        uint32_t physSrc = src & 0x1FFFFFFF;

        char *destStr = reinterpret_cast<char *>(rdram + physDst);
        const char *srcStr = reinterpret_cast<const char *>(rdram + physSrc);

        std::strncpy(destStr, srcStr, n);

        ctx->r[2] = _mm_set1_epi32(dst);
    }

    void strlen(uint8_t *rdram, R5900Context *ctx)
    {
        uint32_t str = ctx->r[4].m128i_u32[0]; // a0 = string

        uint32_t physStr = str & 0x1FFFFFFF;
        const char *strPtr = reinterpret_cast<const char *>(rdram + physStr);

        size_t length = std::strlen(strPtr);

        ctx->r[2] = _mm_set1_epi32(static_cast<uint32_t>(length));
    }

    void strcmp(uint8_t *rdram, R5900Context *ctx)
    {
        uint32_t str1 = ctx->r[4].m128i_u32[0]; // a0 = first string
        uint32_t str2 = ctx->r[5].m128i_u32[0]; // a1 = second string

        uint32_t phys1 = str1 & 0x1FFFFFFF;
        uint32_t phys2 = str2 & 0x1FFFFFFF;

        const char *str1Ptr = reinterpret_cast<const char *>(rdram + phys1);
        const char *str2Ptr = reinterpret_cast<const char *>(rdram + phys2);

        int result = std::strcmp(str1Ptr, str2Ptr);

        ctx->r[2] = _mm_set1_epi32(result);
    }

    void strncmp(uint8_t *rdram, R5900Context *ctx)
    {
        uint32_t str1 = ctx->r[4].m128i_u32[0]; // a0 = first string
        uint32_t str2 = ctx->r[5].m128i_u32[0]; // a1 = second string
        uint32_t n = ctx->r[6].m128i_u32[0];    // a2 = max length

        uint32_t phys1 = str1 & 0x1FFFFFFF;
        uint32_t phys2 = str2 & 0x1FFFFFFF;

        const char *str1Ptr = reinterpret_cast<const char *>(rdram + phys1);
        const char *str2Ptr = reinterpret_cast<const char *>(rdram + phys2);

        int result = std::strncmp(str1Ptr, str2Ptr, n);

        ctx->r[2] = _mm_set1_epi32(result);
    }

    // I/O operations
    void printf(uint8_t *rdram, R5900Context *ctx)
    {
        uint32_t fmtAddr = ctx->r[4].m128i_u32[0]; // a0 = format string

        uint32_t physAddr = fmtAddr & 0x1FFFFFFF;
        const char *fmt = reinterpret_cast<const char *>(rdram + physAddr);

        // Simple implementation - just print the format string TODO  we would parse the format and handle arguments
        std::cout << "printf: " << fmt << std::endl;

        ctx->r[2] = _mm_set1_epi32(1);
    }

    void sprintf(uint8_t *rdram, R5900Context *ctx)
    {
        uint32_t destAddr = ctx->r[4].m128i_u32[0]; // a0 = destination buffer
        uint32_t fmtAddr = ctx->r[5].m128i_u32[0];  // a1 = format string

        uint32_t physDest = destAddr & 0x1FFFFFFF;
        uint32_t physFmt = fmtAddr & 0x1FFFFFFF;

        char *destStr = reinterpret_cast<char *>(rdram + physDest);
        const char *fmt = reinterpret_cast<const char *>(rdram + physFmt);

        std::strcpy(destStr, fmt);

        size_t len = std::strlen(fmt);
        ctx->r[2] = _mm_set1_epi32(static_cast<uint32_t>(len));
    }

    void puts(uint8_t *rdram, R5900Context *ctx)
    {
        uint32_t strAddr = ctx->r[4].m128i_u32[0]; // a0 = string

        uint32_t physAddr = strAddr & 0x1FFFFFFF;
        const char *str = reinterpret_cast<const char *>(rdram + physAddr);

        std::cout << str << std::endl;

        // Return success (1) in v0
        ctx->r[2] = _mm_set1_epi32(1);
    }

    // Memory allocation TODO need a proper PS2 heap implementation
    void malloc(uint8_t *rdram, R5900Context *ctx)
    {
        uint32_t size = ctx->r[4].m128i_u32[0]; // a0 = size

        // Simple implementation - allocate from top of memory

        static uint32_t heapTop = 0x01000000; // Example heap start

        uint32_t allocAddr = heapTop;
        heapTop += ((size + 15) & ~15); // Align to 16 bytes

        ctx->r[2] = _mm_set1_epi32(allocAddr);
    }

    void free(uint8_t *rdram, R5900Context *ctx)
    {
        uint32_t ptr = ctx->r[4].m128i_u32[0]; // a0 = pointer

        // Simple implementation - do nothing
    }

    void calloc(uint8_t *rdram, R5900Context *ctx)
    {
        uint32_t nmemb = ctx->r[4].m128i_u32[0]; // a0 = number of elements
        uint32_t size = ctx->r[5].m128i_u32[0];  // a1 = size of each element

        uint32_t totalSize = nmemb * size;

        // Call malloc
        ctx->r[4] = _mm_set1_epi32(totalSize);
        malloc(rdram, ctx);

        // Zero the memory
        uint32_t allocAddr = ctx->r[2].m128i_u32[0];
        uint32_t physAddr = allocAddr & 0x1FFFFFFF;
        std::memset(rdram + physAddr, 0, totalSize);

        // Return value already in v0 from malloc
    }

    void realloc(uint8_t *rdram, R5900Context *ctx)
    {
        uint32_t ptr = ctx->r[4].m128i_u32[0];  // a0 = pointer
        uint32_t size = ctx->r[5].m128i_u32[0]; // a1 = new size

        // Simple implementation - allocate new block and copy

        if (ptr == 0)
        {
            ctx->r[4] = _mm_set1_epi32(size);
            malloc(rdram, ctx);
            return;
        }

        // Allocate new block
        uint32_t oldPtr = ptr;
        ctx->r[4] = _mm_set1_epi32(size);
        malloc(rdram, ctx);
        uint32_t newPtr = ctx->r[2].m128i_u32[0];

        // Copy data from old to new
        uint32_t physOld = oldPtr & 0x1FFFFFFF;
        uint32_t physNew = newPtr & 0x1FFFFFFF;
        std::memcpy(rdram + physNew, rdram + physOld, size);

        // Return value already in v0
    }

    // Math functions
    void sqrt(uint8_t *rdram, R5900Context *ctx)
    {
        // Assuming the argument is in the first FPU register (f12)
        float arg = ctx->f[12];
        float result = std::sqrt(arg);

        // Return result in f0
        ctx->f[0] = result;
    }

    void sin(uint8_t *rdram, R5900Context *ctx)
    {
        // Assuming the argument is in the first FPU register (f12)
        float arg = ctx->f[12];
        float result = std::sin(arg);

        // Return result in f0
        ctx->f[0] = result;
    }

    void cos(uint8_t *rdram, R5900Context *ctx)
    {
        // Assuming the argument is in the first FPU register (f12)
        float arg = ctx->f[12];
        float result = std::cos(arg);

        // Return result in f0
        ctx->f[0] = result;
    }

    void atan2(uint8_t *rdram, R5900Context *ctx)
    {
        // Assuming the arguments are in f12 and f13
        float y = ctx->f[12];
        float x = ctx->f[13];
        float result = std::atan2(y, x);

        // Return result in f0
        ctx->f[0] = result;
    }
}

// PS2 system call implementation stubs
namespace ps2_syscalls
{
    void FlushCache(uint8_t *rdram, R5900Context *ctx)
    {
        uint32_t operation = ctx->r[4].m128i_u32[0]; // a0 = operation

        std::cout << "FlushCache called with operation: " << operation << std::endl;

        // Operation values:
        // 0 = Instruction cache
        // 1 = Data cache
        // 2 = Both
    }

    void ExitThread(uint8_t *rdram, R5900Context *ctx)
    {
        uint32_t exitCode = ctx->r[4].m128i_u32[0]; // a0 = exit code

        std::cout << "ExitThread called with exit code: " << exitCode << std::endl;

        // This would terminate the current thread
    }

    void SleepThread(uint8_t *rdram, R5900Context *ctx)
    {
        std::cout << "SleepThread called" << std::endl;

        // This would suspend the current thread
    }

    void CreateThread(uint8_t *rdram, R5900Context *ctx)
    {
        uint32_t threadParam = ctx->r[4].m128i_u32[0]; // a0 = thread parameter

        // We need to extract all thread creation parameters
        std::cout << "CreateThread called with parameter: " << std::hex << threadParam << std::dec << std::endl;

        // Return a dummy thread ID
        ctx->r[2] = _mm_set1_epi32(1);
    }

    void StartThread(uint8_t *rdram, R5900Context *ctx)
    {
        uint32_t threadId = ctx->r[4].m128i_u32[0]; // a0 = thread ID
        uint32_t priority = ctx->r[5].m128i_u32[0]; // a1 = priority

        std::cout << "StartThread called with ID: " << threadId << ", priority: " << priority << std::endl;

        // Return success (0)
        ctx->r[2] = _mm_set1_epi32(0);
    }

    void SifInitRpc(uint8_t *rdram, R5900Context *ctx)
    {
        uint32_t mode = ctx->r[4].m128i_u32[0]; // a0 = mode

        std::cout << "SifInitRpc called with mode: " << mode << std::endl;

        // Return success (0)
        ctx->r[2] = _mm_set1_epi32(0);
    }

    void SifBindRpc(uint8_t *rdram, R5900Context *ctx)
    {
        uint32_t clientPtr = ctx->r[4].m128i_u32[0]; // a0 = client pointer
        uint32_t rpcId = ctx->r[5].m128i_u32[0];     // a1 = RPC ID
        uint32_t mode = ctx->r[6].m128i_u32[0];      // a2 = mode

        std::cout << "SifBindRpc called with client: " << std::hex << clientPtr
                  << ", RPC ID: " << rpcId << ", mode: " << mode << std::dec << std::endl;

        // Return success (0)
        ctx->r[2] = _mm_set1_epi32(0);
    }

    void SifCallRpc(uint8_t *rdram, R5900Context *ctx)
    {
        uint32_t clientPtr = ctx->r[4].m128i_u32[0]; // a0 = client pointer
        uint32_t command = ctx->r[5].m128i_u32[0];   // a1 = command
        uint32_t mode = ctx->r[6].m128i_u32[0];      // a2 = mode

        std::cout << "SifCallRpc called with client: " << std::hex << clientPtr
                  << ", command: " << command << ", mode: " << mode << std::dec << std::endl;

        // Return success (0)
        ctx->r[2] = _mm_set1_epi32(0);
    }

    void SetGsCrt(uint8_t *rdram, R5900Context *ctx)
    {
        uint32_t interlaced = ctx->r[4].m128i_u32[0]; // a0 = interlaced
        uint32_t mode = ctx->r[5].m128i_u32[0];       // a1 = mode (NTSC/PAL)
        uint32_t field = ctx->r[6].m128i_u32[0];      // a2 = field

        std::cout << "SetGsCrt called with interlaced: " << interlaced
                  << ", mode: " << mode << ", field: " << field << std::endl;

        // No return value
    }
}

// MMI operation implementations
namespace ps2_mmi
{
    void PADDW(R5900Context *ctx, int rd, int rs, int rt)
    {
        ctx->r[rd] = _mm_add_epi32(ctx->r[rs], ctx->r[rt]);
    }

    void PSUBW(R5900Context *ctx, int rd, int rs, int rt)
    {
        ctx->r[rd] = _mm_sub_epi32(ctx->r[rs], ctx->r[rt]);
    }

    void PCGTW(R5900Context *ctx, int rd, int rs, int rt)
    {
        ctx->r[rd] = _mm_cmpgt_epi32(ctx->r[rs], ctx->r[rt]);
    }

    void PMAXW(R5900Context *ctx, int rd, int rs, int rt)
    {
        // we could use _mm_max_epi32 but is SSE4.1
        __m128i mask = _mm_cmpgt_epi32(ctx->r[rs], ctx->r[rt]);
        ctx->r[rd] = _mm_or_si128(
            _mm_and_si128(mask, ctx->r[rs]),
            _mm_andnot_si128(mask, ctx->r[rt]));
    }

    void PADDH(R5900Context *ctx, int rd, int rs, int rt)
    {
        ctx->r[rd] = _mm_add_epi16(ctx->r[rs], ctx->r[rt]);
    }

    void PSUBH(R5900Context *ctx, int rd, int rs, int rt)
    {
        ctx->r[rd] = _mm_sub_epi16(ctx->r[rs], ctx->r[rt]);
    }

    void PCGTH(R5900Context *ctx, int rd, int rs, int rt)
    {
        ctx->r[rd] = _mm_cmpgt_epi16(ctx->r[rs], ctx->r[rt]);
    }

    void PMAXH(R5900Context *ctx, int rd, int rs, int rt)
    {
        ctx->r[rd] = _mm_max_epi16(ctx->r[rs], ctx->r[rt]);
    }

    void PADDB(R5900Context *ctx, int rd, int rs, int rt)
    {
        ctx->r[rd] = _mm_add_epi8(ctx->r[rs], ctx->r[rt]);
    }

    void PSUBB(R5900Context *ctx, int rd, int rs, int rt)
    {
        ctx->r[rd] = _mm_sub_epi8(ctx->r[rs], ctx->r[rt]);
    }

    void PCGTB(R5900Context *ctx, int rd, int rs, int rt)
    {
        ctx->r[rd] = _mm_cmpgt_epi8(ctx->r[rs], ctx->r[rt]);
    }

    void PEXTLW(R5900Context *ctx, int rd, int rs, int rt)
    {
        ctx->r[rd] = _mm_unpacklo_epi32(ctx->r[rt], ctx->r[rs]);
    }

    void PEXTUW(R5900Context *ctx, int rd, int rs, int rt)
    {
        ctx->r[rd] = _mm_unpackhi_epi32(ctx->r[rt], ctx->r[rs]);
    }

    void PEXTLH(R5900Context *ctx, int rd, int rs, int rt)
    {
        ctx->r[rd] = _mm_unpacklo_epi16(ctx->r[rt], ctx->r[rs]);
    }

    void PEXTUH(R5900Context *ctx, int rd, int rs, int rt)
    {
        ctx->r[rd] = _mm_unpackhi_epi16(ctx->r[rt], ctx->r[rs]);
    }

    void PEXTLB(R5900Context *ctx, int rd, int rs, int rt)
    {
        ctx->r[rd] = _mm_unpacklo_epi8(ctx->r[rt], ctx->r[rs]);
    }

    void PEXTUB(R5900Context *ctx, int rd, int rs, int rt)
    {
        ctx->r[rd] = _mm_unpackhi_epi8(ctx->r[rt], ctx->r[rs]);
    }

    void PMADDW(R5900Context *ctx, int rd, int rs, int rt)
    {
        // We need to compute (rs * rt) + (HI:LO) and store in both rd and HI:LO
        // This is a simplification as the actual MMI operation is more complex

        // Just do vector multiplication
        __m128i product = _mm_mullo_epi32(ctx->r[rs], ctx->r[rt]);

        // Add the current HI:LO values
        // This is a simplified version
        __m128i hiLo = _mm_set_epi32(0, ctx->hi, 0, ctx->lo);
        __m128i result = _mm_add_epi32(product, hiLo);

        // Set result and update HI:LO
        ctx->r[rd] = result;
        ctx->lo = _mm_extract_epi32(result, 0);
        ctx->hi = _mm_extract_epi32(result, 1);
    }

    void PMSUBW(R5900Context *ctx, int rd, int rs, int rt)
    {
        // Similar to PMADDW but subtracts
        __m128i product = _mm_mullo_epi32(ctx->r[rs], ctx->r[rt]);

        __m128i hiLo = _mm_set_epi32(0, ctx->hi, 0, ctx->lo);
        __m128i result = _mm_sub_epi32(hiLo, product);

        ctx->r[rd] = result;
        ctx->lo = _mm_extract_epi32(result, 0);
        ctx->hi = _mm_extract_epi32(result, 1);
    }

    void PAND(R5900Context *ctx, int rd, int rs, int rt)
    {
        ctx->r[rd] = _mm_and_si128(ctx->r[rs], ctx->r[rt]);
    }

    void POR(R5900Context *ctx, int rd, int rs, int rt)
    {
        ctx->r[rd] = _mm_or_si128(ctx->r[rs], ctx->r[rt]);
    }

    void PXOR(R5900Context *ctx, int rd, int rs, int rt)
    {
        ctx->r[rd] = _mm_xor_si128(ctx->r[rs], ctx->r[rt]);
    }

    void PNOR(R5900Context *ctx, int rd, int rs, int rt)
    {
        __m128i orResult = _mm_or_si128(ctx->r[rs], ctx->r[rt]);
        ctx->r[rd] = _mm_xor_si128(orResult, _mm_set1_epi32(0xFFFFFFFF));
    }
}

// Hardware register handlers
namespace ps2_hardware
{
    void handleDmacReg(uint32_t address, uint32_t value)
    {
        uint32_t channel = (address >> 8) & 0xF;
        uint32_t reg = address & 0xFF;

        std::cout << "DMA channel " << channel << " register " << std::hex << reg
                  << " written with value " << value << std::dec << std::endl;

        // Check if this is the start bit in the channel control register
        if (reg == 0 && (value & 0x100))
        {
            std::cout << "Starting DMA transfer on channel " << channel << std::endl;

            // Initiate DMA transfer here
        }
    }

    // GS register handlers
    void handleGsReg(uint32_t address, uint32_t value)
    {
        std::cout << "GS register " << std::hex << address << " written with value "
                  << value << std::dec << std::endl;

        // Update GS state and possibly trigger rendering
    }

    // VIF register handlers
    void handleVif0Reg(uint32_t address, uint32_t value)
    {
        uint32_t reg = address & 0xFF;

        std::cout << "VIF0 register " << std::hex << reg << " written with value "
                  << value << std::dec << std::endl;

        // Handle VIF0 operations
    }

    void handleVif1Reg(uint32_t address, uint32_t value)
    {
        uint32_t reg = address & 0xFF;

        std::cout << "VIF1 register " << std::hex << reg << " written with value "
                  << value << std::dec << std::endl;

        // Handle VIF1 operations
    }

    void handleTimerReg(uint32_t address, uint32_t value)
    {
        uint32_t timer = (address >> 4) & 0x3;
        uint32_t reg = address & 0xF;

        std::cout << "Timer " << timer << " register " << std::hex << reg << " written with value "
                  << value << std::dec << std::endl;

        // Update timer state
    }

    void handleSPU2Reg(uint32_t address, uint32_t value)
    {
        std::cout << "SPU2 register " << std::hex << address << " written with value "
                  << value << std::dec << std::endl;

        // Update audio state
    }
}
#endif // PS2_STUBS_H