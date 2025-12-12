#include "ps2_runtime.h"
#include <iostream>
#include <string>

// Example of how to use the PS2 runtime with recompiled code

// Stub implementation for PS2 syscalls
void syscall(uint8_t *rdram, R5900Context *ctx)
{
    uint32_t syscallNum = M128I_U32(ctx->r[4], 0);
    std::cout << "Syscall " << syscallNum << " called" << std::endl;

    switch (syscallNum)
    {
    case 0x01: // Exit program
        std::cout << "Program requested exit with code: " << M128I_U32(ctx->r[5], 0) << std::endl;
        break;

    case 0x3C: // PutChar - print a character to stdout
        std::cout << (char)M128I_U32(ctx->r[5], 0);
        break;

    case 0x3D: // PutString - print a string to stdout
    {
        uint32_t strAddr = M128I_U32(ctx->r[5], 0);
        if (strAddr == 0)
        {
            std::cout << "(null)";
        }
        else
        {
            uint32_t physAddr = strAddr & 0x1FFFFFFF;
            const char *str = reinterpret_cast<const char *>(rdram + physAddr);
            std::cout << str;
        }
    }
    break;

    default:
        std::cout << "Unhandled syscall: " << syscallNum << std::endl;
        break;
    }
}

// Example implementation of FlushCache
void FlushCache(uint8_t *rdram, R5900Context *ctx)
{
    uint32_t cacheType = M128I_U32(ctx->r[4], 0);
    std::cout << "FlushCache called with type: " << cacheType << std::endl;
}

// Example implementation of a recompiled function
void recompiled_main(uint8_t *rdram, R5900Context *ctx)
{
    std::cout << "Running recompiled main function" << std::endl;

    // Example of memory access
    uint32_t addr = 0x100000; // Some address in memory
    uint32_t physAddr = addr & 0x1FFFFFFF;
    uint32_t value = *reinterpret_cast<uint32_t *>(rdram + physAddr);
    std::cout << "Value at 0x" << std::hex << addr << " = 0x" << value << std::dec << std::endl;

    // Example of register manipulation
    ctx->r[2] = _mm_set1_epi32(0x12345678); // Set register v0
    ctx->r[4] = _mm_set1_epi32(0x3D);       // Set register a0 for syscall (PutString)
    ctx->r[5] = _mm_set1_epi32(0x10000);    // Set register a1 with string address

    // Call a "syscall" function
    syscall(rdram, ctx);

    // Example of returning a value
    ctx->r[2] = _mm_set1_epi32(0); // Return 0 (success)
}

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        std::cout << "Usage: " << argv[0] << " <elf_file>" << std::endl;
        return 1;
    }

    std::string elfPath = argv[1];

    PS2Runtime runtime;
    if (!runtime.initialize())
    {
        std::cerr << "Failed to initialize PS2 runtime" << std::endl;
        return 1;
    }

    // Register built-in functions
    runtime.registerFunction(0x00000001, syscall);
    runtime.registerFunction(0x00000002, FlushCache);
    runtime.registerFunction(0x00100000, recompiled_main); // Example address for main

    // Load the ELF file
    if (!runtime.loadELF(elfPath))
    {
        std::cerr << "Failed to load ELF file: " << elfPath << std::endl;
        return 1;
    }

    // Run the program
    runtime.run();

    return 0;
}