#include "ps2_runtime.h"
#include <iostream>
#include <fstream>
#include <algorithm>
#include <cstring>

#define ELF_MAGIC 0x464C457F // "\x7FELF" in little endian
#define ET_EXEC 2            // Executable file

#define EM_MIPS 8 // MIPS architecture

struct ElfHeader
{
    uint32_t magic;
    uint8_t elf_class;
    uint8_t endianness;
    uint8_t version;
    uint8_t os_abi;
    uint8_t abi_version;
    uint8_t padding[7];
    uint16_t type;
    uint16_t machine;
    uint32_t version2;
    uint32_t entry;
    uint32_t phoff;
    uint32_t shoff;
    uint32_t flags;
    uint16_t ehsize;
    uint16_t phentsize;
    uint16_t phnum;
    uint16_t shentsize;
    uint16_t shnum;
    uint16_t shstrndx;
};

struct ProgramHeader
{
    uint32_t type;
    uint32_t offset;
    uint32_t vaddr;
    uint32_t paddr;
    uint32_t filesz;
    uint32_t memsz;
    uint32_t flags;
    uint32_t align;
};

#define PT_LOAD 1 // Loadable segment

PS2Runtime::PS2Runtime()
{
    std::memset(&m_cpuContext, 0, sizeof(m_cpuContext));

    // R0 is always zero in MIPS
    m_cpuContext.r[0] = _mm_set1_epi32(0);

    // Stack pointer (SP) and global pointer (GP) will be set by the loaded ELF

    m_functionTable.clear();

    m_loadedModules.clear();
}

PS2Runtime::~PS2Runtime()
{
    m_loadedModules.clear();

    m_functionTable.clear();
}

bool PS2Runtime::initialize()
{
    if (!m_memory.initialize())
    {
        std::cerr << "Failed to initialize PS2 memory" << std::endl;
        return false;
    }

    return true;
}

bool PS2Runtime::loadELF(const std::string &elfPath)
{
    std::ifstream file(elfPath, std::ios::binary);
    if (!file)
    {
        std::cerr << "Failed to open ELF file: " << elfPath << std::endl;
        return false;
    }

    ElfHeader header;
    file.read(reinterpret_cast<char *>(&header), sizeof(header));

    if (header.magic != ELF_MAGIC)
    {
        std::cerr << "Invalid ELF magic number" << std::endl;
        return false;
    }

    if (header.machine != EM_MIPS || header.type != ET_EXEC)
    {
        std::cerr << "Not a MIPS executable ELF file" << std::endl;
        return false;
    }

    m_cpuContext.pc = header.entry;

    for (uint16_t i = 0; i < header.phnum; i++)
    {
        ProgramHeader ph;
        file.seekg(header.phoff + i * header.phentsize);
        file.read(reinterpret_cast<char *>(&ph), sizeof(ph));

        if (ph.type == PT_LOAD && ph.filesz > 0)
        {
            std::cout << "Loading segment: 0x" << std::hex << ph.vaddr
                      << " - 0x" << (ph.vaddr + ph.memsz)
                      << " (size: 0x" << ph.memsz << ")" << std::dec << std::endl;

            // Allocate temporary buffer for the segment
            std::vector<uint8_t> buffer(ph.filesz);

            // Read segment data
            file.seekg(ph.offset);
            file.read(reinterpret_cast<char *>(buffer.data()), ph.filesz);

            // Copy to memory
            uint32_t physAddr = m_memory.translateAddress(ph.vaddr);
            uint8_t *dest = m_memory.getRDRAM() + physAddr;
            std::memcpy(dest, buffer.data(), ph.filesz);

            if (ph.memsz > ph.filesz)
            {
                std::memset(dest + ph.filesz, 0, ph.memsz - ph.filesz);
            }
        }
    }

    LoadedModule module;
    module.name = elfPath.substr(elfPath.find_last_of("/\\") + 1);
    module.baseAddress = 0x00100000; // Typical base address for PS2 executables
    module.size = 0;                 // Would need to calculate from segments
    module.active = true;

    m_loadedModules.push_back(module);

    std::cout << "ELF file loaded successfully. Entry point: 0x" << std::hex << m_cpuContext.pc << std::dec << std::endl;
    return true;
}

void PS2Runtime::registerFunction(uint32_t address, RecompiledFunction func)
{
    m_functionTable[address] = func;
}

PS2Runtime::RecompiledFunction PS2Runtime::lookupFunction(uint32_t address)
{
    auto it = m_functionTable.find(address);
    if (it != m_functionTable.end())
    {
        return it->second;
    }

    std::cerr << "Warning: Function at address 0x" << std::hex << address << std::dec << " not found" << std::endl;

    static RecompiledFunction defaultFunction = [](uint8_t *rdram, R5900Context *ctx, PS2Runtime* runtime)
    {
        std::cerr << "Error: Called unimplemented function at address 0x" << std::hex << ctx->pc << std::dec << std::endl;
    };

    return defaultFunction;
}

void PS2Runtime::SignalException(R5900Context *ctx, PS2Exception exception)
{
    if (exception == EXCEPTION_INTEGER_OVERFLOW)
    {
        // PS2 behavior: jump to exception handler
        HandleIntegerOverflow(ctx);
    }
}

void PS2Runtime::executeVU0Microprogram(uint8_t *rdram, R5900Context *ctx, uint32_t address)
{
    std::cout << "VU0 microprogram call to address 0x" << std::hex << address
              << " - not implemented" << std::dec << std::endl;

    // mayve implement like this or a vu0_interpreter
    // Placeholder for VU0 microprogram execution
    // auto microprog = findCompiledMicroprogram(address);
    // if (microprog) microprog(rdram, ctx);
}

void PS2Runtime::vu0StartMicroProgram(uint8_t *rdram, R5900Context *ctx, uint32_t address)
{
    std::cout << "VU0 microprogram call to address 0x" << std::hex << address
              << " - not implemented" << std::dec << std::endl;
}

void PS2Runtime::handleSyscall(uint8_t *rdram, R5900Context *ctx)
{
    std::cout << "Syscall encountered at PC: 0x" << std::hex << ctx->pc << std::dec << std::endl; 
}

void PS2Runtime::handleBreak(uint8_t *rdram, R5900Context *ctx)
{
    std::cout << "Break encountered at PC: 0x" << std::hex << ctx->pc << std::dec << std::endl;
}

void PS2Runtime::handleTrap(uint8_t *rdram, R5900Context *ctx)
{
    std::cout << "Trap encountered at PC: 0x" << std::hex << ctx->pc << std::dec << std::endl;
}

void PS2Runtime::handleTLBR(uint8_t *rdram, R5900Context *ctx)
{
    std::cout << "TLBR (TLB Read) at PC: 0x" << std::hex << ctx->pc << std::dec << std::endl;
}

void PS2Runtime::handleTLBWI(uint8_t *rdram, R5900Context *ctx)
{
    std::cout << "TLBWI (TLB Write Indexed) at PC: 0x" << std::hex << ctx->pc << std::dec << std::endl;
}

void PS2Runtime::handleTLBWR(uint8_t *rdram, R5900Context *ctx)
{
    std::cout << "TLBWR (TLB Write Random) at PC: 0x" << std::hex << ctx->pc << std::dec << std::endl;
}

void PS2Runtime::handleTLBP(uint8_t *rdram, R5900Context *ctx)
{
    std::cout << "TLBP (TLB Probe) at PC: 0x" << std::hex << ctx->pc << std::dec << std::endl;
}

void PS2Runtime::clearLLBit(R5900Context *ctx)
{
    ctx->cop0_status &= ~0x00000002; // LL bit is bit 1 in the status register
    std::cout << "LL bit cleared at PC: 0x" << std::hex << ctx->pc << std::dec << std::endl;
}

void PS2Runtime::HandleIntegerOverflow(R5900Context *ctx)
{
    std::cerr << "Integer overflow exception at PC: 0x" << std::hex << ctx->pc << std::dec << std::endl;

    // Set the EPC (Exception Program Counter) to the current PC
    m_cpuContext.cop0_epc = ctx->pc;

    // Set the cause register to indicate an integer overflow
    m_cpuContext.cop0_cause |= (EXCEPTION_INTEGER_OVERFLOW << 2);

    // Jump to the exception handler (usually at 0x80000000)
    m_cpuContext.pc = 0x80000000; // Default PS2 exception handler address
}

void PS2Runtime::run()
{
    RecompiledFunction entryPoint = lookupFunction(m_cpuContext.pc);

    m_cpuContext.r[4] = _mm_set1_epi32(0);           // A0 = 0 (argc)
    m_cpuContext.r[5] = _mm_set1_epi32(0);           // A1 = 0 (argv)
    m_cpuContext.r[29] = _mm_set1_epi32(0x02000000); // SP = top of RAM

    std::cout << "Starting execution at address 0x" << std::hex << m_cpuContext.pc << std::dec << std::endl;

    try
    {
        // Call the entry point function
        entryPoint(m_memory.getRDRAM(), &m_cpuContext, this);

        std::cout << "Program execution completed successfully" << std::endl;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error during program execution: " << e.what() << std::endl;
    }
}