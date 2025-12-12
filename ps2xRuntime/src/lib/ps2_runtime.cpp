#include "ps2_runtime.h"
#include "ps2_syscalls.h"

// PS2 EE Kernel Syscall Names /* DEBUG_INJECT */
static const char* getSyscallName(int num) { /* DEBUG_INJECT */
    switch(num) { /* DEBUG_INJECT */
        case 0x02: return "SetGsCrt"; /* DEBUG_INJECT */
        case 0x04: return "Exit"; /* DEBUG_INJECT */
        case 0x06: return "LoadExecPS2"; /* DEBUG_INJECT */
        case 0x07: return "ExecPS2"; /* DEBUG_INJECT */
        case 0x10: return "AddIntcHandler"; /* DEBUG_INJECT */
        case 0x11: return "RemoveIntcHandler"; /* DEBUG_INJECT */
        case 0x12: return "AddDmacHandler"; /* DEBUG_INJECT */
        case 0x13: return "RemoveDmacHandler"; /* DEBUG_INJECT */
        case 0x14: return "_EnableIntc"; /* DEBUG_INJECT */
        case 0x15: return "_DisableIntc"; /* DEBUG_INJECT */
        case 0x16: return "_EnableDmac"; /* DEBUG_INJECT */
        case 0x17: return "_DisableDmac"; /* DEBUG_INJECT */
        case 0x20: return "CreateThread"; /* DEBUG_INJECT */
        case 0x21: return "DeleteThread"; /* DEBUG_INJECT */
        case 0x22: return "StartThread"; /* DEBUG_INJECT */
        case 0x23: return "ExitThread"; /* DEBUG_INJECT */
        case 0x24: return "ExitDeleteThread"; /* DEBUG_INJECT */
        case 0x25: return "TerminateThread"; /* DEBUG_INJECT */
        case 0x29: return "ChangeThreadPriority"; /* DEBUG_INJECT */
        case 0x2a: return "iChangeThreadPriority"; /* DEBUG_INJECT */
        case 0x2b: return "RotateThreadReadyQueue"; /* DEBUG_INJECT */
        case 0x2f: return "GetThreadId"; /* DEBUG_INJECT */
        case 0x30: return "ReferThreadStatus"; /* DEBUG_INJECT */
        case 0x32: return "SleepThread"; /* DEBUG_INJECT */
        case 0x33: return "WakeupThread"; /* DEBUG_INJECT */
        case 0x34: return "iWakeupThread"; /* DEBUG_INJECT */
        case 0x37: return "SuspendThread"; /* DEBUG_INJECT */
        case 0x39: return "ResumeThread"; /* DEBUG_INJECT */
        case 0x3c: return "SetupThread"; /* DEBUG_INJECT */
        case 0x3d: return "SetupHeap"; /* DEBUG_INJECT */
        case 0x3e: return "EndOfHeap"; /* DEBUG_INJECT */
        case 0x40: return "CreateSema"; /* DEBUG_INJECT */
        case 0x41: return "DeleteSema"; /* DEBUG_INJECT */
        case 0x42: return "SignalSema"; /* DEBUG_INJECT */
        case 0x43: return "iSignalSema"; /* DEBUG_INJECT */
        case 0x44: return "WaitSema"; /* DEBUG_INJECT */
        case 0x45: return "PollSema"; /* DEBUG_INJECT */
        case 0x47: return "ReferSemaStatus"; /* DEBUG_INJECT */
        case 0x48: return "iReferSemaStatus"; /* DEBUG_INJECT */
        case 0x64: return "FlushCache"; /* DEBUG_INJECT */
        case 0x68: return "FlushCache (alt)"; /* DEBUG_INJECT */
        case 0x70: return "GsGetIMR"; /* DEBUG_INJECT */
        case 0x71: return "GsPutIMR"; /* DEBUG_INJECT */
        case 0x73: return "SetVSyncFlag"; /* DEBUG_INJECT */
        case 0x74: return "SetSyscall"; /* DEBUG_INJECT */
        case 0x76: return "SifDmaStat"; /* DEBUG_INJECT */
        case 0x77: return "SifSetDma"; /* DEBUG_INJECT */
        case 0x78: return "SifSetDChain"; /* DEBUG_INJECT */
        case 0x79: return "SifSetReg"; /* DEBUG_INJECT */
        case 0x7a: return "SifGetReg"; /* DEBUG_INJECT */
        case 0x7c: return "Deci2Call"; /* DEBUG_INJECT */
        case 0x7e: return "MachineType"; /* DEBUG_INJECT */
        case 0x7f: return "GetMemorySize"; /* DEBUG_INJECT */
        default: return "Unknown"; /* DEBUG_INJECT */
    } /* DEBUG_INJECT */
} /* DEBUG_INJECT */
#include <iostream>
#include <fstream>
#include <algorithm>
#include <cstring>
#include <atomic>
#include <thread>
#include "raylib.h"

#define ELF_MAGIC 0x464C457F
#define ET_EXEC 2
#define EM_MIPS 8

struct ElfHeader {
    uint32_t magic;
    uint8_t elf_class, endianness, version, os_abi, abi_version;
    uint8_t padding[7];
    uint16_t type, machine;
    uint32_t version2, entry, phoff, shoff, flags;
    uint16_t ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
};

struct ProgramHeader {
    uint32_t type, offset, vaddr, paddr, filesz, memsz, flags, align;
};

#define PT_LOAD 1

static constexpr int FB_WIDTH = 640;
static constexpr int FB_HEIGHT = 448;
static constexpr uint32_t DEFAULT_FB_ADDR = 0x00100000;

static void UploadFrame(Texture2D &tex, PS2Runtime *rt) {
    uint8_t *src = rt->memory().getRDRAM() + (DEFAULT_FB_ADDR & 0x1FFFFFFF);
    UpdateTexture(tex, src);
}

PS2Runtime::PS2Runtime() {
    std::memset(&m_cpuContext, 0, sizeof(m_cpuContext));
    m_cpuContext.r[0] = _mm_set1_epi32(0);
    m_functionTable.clear();
    m_loadedModules.clear();
}

PS2Runtime::~PS2Runtime() { m_loadedModules.clear(); m_functionTable.clear(); }

bool PS2Runtime::initialize(const char *title) {
    if (!m_memory.initialize()) return false;
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(FB_WIDTH, FB_HEIGHT, title);
    SetTargetFPS(60);
    return true;
}

bool PS2Runtime::loadELF(const std::string &elfPath) {
    std::ifstream file(elfPath, std::ios::binary);
    if (!file) return false;
    ElfHeader header;
    file.read(reinterpret_cast<char *>(&header), sizeof(header));
    if (header.magic != ELF_MAGIC || header.machine != EM_MIPS || header.type != ET_EXEC) return false;
    m_cpuContext.pc = header.entry;
    for (uint16_t i = 0; i < header.phnum; i++) {
        ProgramHeader ph;
        file.seekg(header.phoff + i * header.phentsize);
        file.read(reinterpret_cast<char *>(&ph), sizeof(ph));
        if (ph.type == PT_LOAD && ph.filesz > 0) {
            std::cout << "Loading segment: 0x" << std::hex << ph.vaddr << std::dec << std::endl;
            std::vector<uint8_t> buffer(ph.filesz);
            file.seekg(ph.offset);
            file.read(reinterpret_cast<char *>(buffer.data()), ph.filesz);
            uint32_t physAddr = m_memory.translateAddress(ph.vaddr);
            uint8_t *dest = (ph.vaddr >= PS2_SCRATCHPAD_BASE && ph.vaddr < PS2_SCRATCHPAD_BASE + PS2_SCRATCHPAD_SIZE) ? m_memory.getScratchpad() + physAddr : m_memory.getRDRAM() + physAddr;
            std::memcpy(dest, buffer.data(), ph.filesz);
            if (ph.memsz > ph.filesz) std::memset(dest + ph.filesz, 0, ph.memsz - ph.filesz);
            if (ph.flags & 0x1) m_memory.registerCodeRegion(ph.vaddr, ph.vaddr + ph.memsz);
        }
    }
    std::cout << "ELF loaded. Entry: 0x" << std::hex << m_cpuContext.pc << std::dec << std::endl;
    return true;
}

void PS2Runtime::registerFunction(uint32_t address, RecompiledFunction func) { m_functionTable[address] = func; }
PS2Runtime::RecompiledFunction PS2Runtime::lookupFunction(uint32_t address) {
    auto it = m_functionTable.find(address);
    return (it != m_functionTable.end()) ? it->second : nullptr;
}
void PS2Runtime::SignalException(R5900Context *ctx, PS2Exception exception) { if (exception == EXCEPTION_INTEGER_OVERFLOW) HandleIntegerOverflow(ctx); }
void PS2Runtime::executeVU0Microprogram(uint8_t *rdram, R5900Context *ctx, uint32_t address) {}
void PS2Runtime::vu0StartMicroProgram(uint8_t *rdram, R5900Context *ctx, uint32_t address) {}
void PS2Runtime::handleSyscall(uint8_t *rdram, R5900Context *ctx) {
    int syscall_num = M128I_U32(ctx->r[3], 0); /* DEBUG_INJECT */
    std::cout << "=== SYSCALL " << syscall_num << " (" << getSyscallName(syscall_num) << ") ===" << std::endl; /* DEBUG_INJECT */
    std::cout << "  PC: 0x" << std::hex << ctx->pc << std::dec << std::endl; /* DEBUG_INJECT */
    std::cout << "  RA: 0x" << std::hex << M128I_U32(ctx->r[31], 0) << std::dec << std::endl; /* DEBUG_INJECT */
    std::cout << "  A0: 0x" << std::hex << M128I_U32(ctx->r[4], 0) << std::dec << std::endl; /* DEBUG_INJECT */
    std::cout << "  A1: 0x" << std::hex << M128I_U32(ctx->r[5], 0) << std::dec << std::endl; /* DEBUG_INJECT */
    std::cout << "  A2: 0x" << std::hex << M128I_U32(ctx->r[6], 0) << std::dec << std::endl; /* DEBUG_INJECT */
    std::cout << "  A3: 0x" << std::hex << M128I_U32(ctx->r[7], 0) << std::dec << std::endl; /* DEBUG_INJECT */

    // Dispatch syscalls
    switch (syscall_num) {
        case 0x02: ps2_syscalls::GsSetCrt(rdram, ctx, this); break;
        case 0x04: std::cout << "Exit syscall - stopping" << std::endl; exit(0); break;
        case 0x5a: // GetSyscallHandler (RFU090) - returns handler for syscall $a0
        case 0x5b: // GetSyscallHandler variant
            {
                // Returns the current handler address for the requested syscall
                // We return a BIOS address (0x80070000 + syscall_num * 8) as placeholder
                // The game uses this to copy BIOS handlers to its own table
                uint32_t requested_syscall = M128I_U32(ctx->r[4], 0);
                // Return a unique BIOS-like address for each syscall
                // These will be recognized as BIOS handlers later
                uint32_t handler_addr = 0x80070000 + (requested_syscall * 8);
                std::cout << "  -> GetSyscallHandler: syscall 0x" << std::hex << requested_syscall
                          << " -> handler 0x" << handler_addr << std::dec << std::endl;
                setReturnU32(ctx, handler_addr);
            }
            break;
        case 0x14: ps2_syscalls::EnableIntc(rdram, ctx, this); break;
        case 0x15: ps2_syscalls::DisableIntc(rdram, ctx, this); break;
        case 0x16: ps2_syscalls::EnableDmac(rdram, ctx, this); break;
        case 0x17: ps2_syscalls::DisableDmac(rdram, ctx, this); break;
        case 0x20: ps2_syscalls::CreateThread(rdram, ctx, this); break;
        case 0x21: ps2_syscalls::DeleteThread(rdram, ctx, this); break;
        case 0x22: ps2_syscalls::StartThread(rdram, ctx, this); break;
        case 0x23: ps2_syscalls::ExitThread(rdram, ctx, this); break;
        case 0x2f: ps2_syscalls::GetThreadId(rdram, ctx, this); break;
        case 0x32: ps2_syscalls::SleepThread(rdram, ctx, this); break;
        case 0x33: ps2_syscalls::WakeupThread(rdram, ctx, this); break;
        case 0x3c: // SetupThread / InitMainThread (RFU060)
            {
                // InitMainThread(uint32 gp, void* stack, int stack_size, char* args, int root)
                // Returns: stack pointer of the thread
                // If stack == -1, SP = end_of_RDRAM - stack_size
                // Else, SP = stack + stack_size
                uint32_t gp = M128I_U32(ctx->r[4], 0);
                int32_t stack = (int32_t)M128I_U32(ctx->r[5], 0);
                int32_t stack_size = (int32_t)M128I_U32(ctx->r[6], 0);
                uint32_t args = M128I_U32(ctx->r[7], 0);
                uint32_t root = M128I_U32(ctx->r[8], 0);

                uint32_t stack_ptr;
                if (stack == -1) {
                    // Use end of RDRAM minus stack size
                    stack_ptr = 0x02000000 - stack_size;
                } else {
                    // Use provided stack address plus size
                    stack_ptr = (uint32_t)stack + stack_size;
                }

                std::cout << "  -> SetupThread: gp=0x" << std::hex << gp
                          << " stack=0x" << stack
                          << " stack_size=0x" << stack_size
                          << " -> SP=0x" << stack_ptr << std::dec << std::endl;

                // Also set GP register
                ctx->r[28] = _mm_set_epi32(0, 0, 0, gp);

                setReturnU32(ctx, stack_ptr);
            }
            break;
        case 0x3d: // SetupHeap
            {
                // SetupHeap(void *heap_start, int heap_size)
                // Returns heap end address or 0 on success
                uint32_t heap_start = M128I_U32(ctx->r[4], 0);
                int32_t heap_size = (int32_t)M128I_U32(ctx->r[5], 0);
                uint32_t heap_end = (heap_size == -1) ? 0x02000000 : heap_start + heap_size;
                std::cout << "  -> SetupHeap: start=0x" << std::hex << heap_start
                          << " size=" << std::dec << heap_size
                          << " end=0x" << std::hex << heap_end << std::dec << std::endl;
                setReturnU32(ctx, heap_end);
            }
            break;
        case 0x3e: // EndOfHeap
            {
                std::cout << "  -> EndOfHeap: Returning 0x02000000" << std::endl;
                setReturnU32(ctx, 0x02000000);
            }
            break;
        case 0x40: ps2_syscalls::CreateSema(rdram, ctx, this); break;
        case 0x41: ps2_syscalls::DeleteSema(rdram, ctx, this); break;
        case 0x42: ps2_syscalls::SignalSema(rdram, ctx, this); break;
        case 0x43: ps2_syscalls::iSignalSema(rdram, ctx, this); break;
        case 0x44: ps2_syscalls::WaitSema(rdram, ctx, this); break;
        case 0x45: ps2_syscalls::PollSema(rdram, ctx, this); break;
        case 0x64: ps2_syscalls::FlushCache(rdram, ctx, this); break;
        case 0x70: ps2_syscalls::GsGetIMR(rdram, ctx, this); break;
        case 0x71: ps2_syscalls::GsPutIMR(rdram, ctx, this); break;
        case 0x74: // SetSyscall - register custom syscall handler
            {
                // SetSyscall(int syscall_num, void* handler)
                uint32_t syscall_to_set = M128I_U32(ctx->r[4], 0);
                uint32_t handler_addr = M128I_U32(ctx->r[5], 0);
                std::cout << "  -> SetSyscall: Registering syscall 0x" << std::hex
                          << syscall_to_set << " -> handler at 0x" << handler_addr
                          << std::dec << std::endl;
                // Store the custom handler
                m_customSyscalls[syscall_to_set] = handler_addr;
                // Return success (0)
                setReturnS32(ctx, 0);
            }
            break;
        default:
            // Check for custom syscall handler
            if (m_customSyscalls.find(syscall_num) != m_customSyscalls.end()) {
                uint32_t handler = m_customSyscalls[syscall_num];
                // Check if handler is in recompiled code range (not BIOS/kernel)
                // KSEG0 is 0x80000000-0x9FFFFFFF, KSEG1 is 0xA0000000-0xBFFFFFFF
                if (handler >= 0x80000000) {
                    std::cout << "  -> Custom syscall 0x" << std::hex << syscall_num
                              << " -> BIOS handler 0x" << handler
                              << " (stubbing)" << std::dec << std::endl;
                    // This is a BIOS syscall handler, stub it
                    setReturnS32(ctx, 0);
                } else if (lookupFunction(handler) != nullptr) {
                    std::cout << "  -> Custom syscall 0x" << std::hex << syscall_num
                              << " -> dispatching to 0x" << handler << std::dec << std::endl;
                    // Set PC to handler, run loop will dispatch
                    ctx->pc = handler;
                } else {
                    std::cout << "  -> Custom syscall 0x" << std::hex << syscall_num
                              << " -> handler 0x" << handler
                              << " not found (stubbing)" << std::dec << std::endl;
                    setReturnS32(ctx, 0);
                }
            } else {
                std::cerr << "  -> Unhandled syscall " << syscall_num << std::endl;
                ps2_syscalls::TODO(rdram, ctx, this);
            }
            break;
    }
}
void PS2Runtime::handleBreak(uint8_t *rdram, R5900Context *ctx) {}
void PS2Runtime::handleTrap(uint8_t *rdram, R5900Context *ctx) {}
void PS2Runtime::handleTLBR(uint8_t *rdram, R5900Context *ctx) {}
void PS2Runtime::handleTLBWI(uint8_t *rdram, R5900Context *ctx) {}
void PS2Runtime::handleTLBWR(uint8_t *rdram, R5900Context *ctx) {}
void PS2Runtime::handleTLBP(uint8_t *rdram, R5900Context *ctx) {}
void PS2Runtime::clearLLBit(R5900Context *ctx) { ctx->cop0_status &= ~0x00000002; }
void PS2Runtime::HandleIntegerOverflow(R5900Context *ctx) { m_cpuContext.cop0_epc = ctx->pc; m_cpuContext.cop0_cause |= (EXCEPTION_INTEGER_OVERFLOW << 2); m_cpuContext.pc = 0x80000000; }

void PS2Runtime::run() {
    m_cpuContext.r[4] = _mm_set1_epi32(0);
    m_cpuContext.r[5] = _mm_set1_epi32(0);
    m_cpuContext.r[29] = _mm_set1_epi32(0x02000000);
    std::cout << "Starting at 0x" << std::hex << m_cpuContext.pc << std::dec << std::endl;
    Image blank = GenImageColor(FB_WIDTH, FB_HEIGHT, BLANK);
    Texture2D frameTex = LoadTextureFromImage(blank);
    UnloadImage(blank);
    std::atomic<bool> running{true};
    std::atomic<bool> shouldExit{false};
    std::thread gameThread([&]() {
        try {
            uint32_t lastPc = 0;
            uint64_t calls = 0;
            while (!shouldExit) {
                uint32_t pc = m_cpuContext.pc;
                RecompiledFunction func = lookupFunction(pc);
                if (!func) {
                    std::cerr << "No func at 0x" << std::hex << pc << std::dec << std::endl;
                    std::cerr << "  RA=0x" << std::hex << M128I_U32(m_cpuContext.r[31], 0) << std::dec << std::endl; /* DEBUG_INJECT */
                    m_cpuContext.dump(); /* DEBUG_INJECT */
                    break;
                }
                lastPc = pc; calls++;
                if (calls < 50 || calls > 95) { /* DEBUG_INJECT */
                    std::cout << "=== Call #" << calls << " ===" << std::endl; /* DEBUG_INJECT */
                    std::cout << "  PC: 0x" << std::hex << pc << std::dec << std::endl; /* DEBUG_INJECT */
                    std::cout << "  RA: 0x" << std::hex << M128I_U32(m_cpuContext.r[31], 0) << std::dec << std::endl; /* DEBUG_INJECT */
                    std::cout << "  SP: 0x" << std::hex << M128I_U32(m_cpuContext.r[29], 0) << std::dec << std::endl; /* DEBUG_INJECT */
                    std::cout << "  A0: 0x" << std::hex << M128I_U32(m_cpuContext.r[4], 0) << std::dec << std::endl; /* DEBUG_INJECT */
                    std::cout << "  A1: 0x" << std::hex << M128I_U32(m_cpuContext.r[5], 0) << std::dec << std::endl; /* DEBUG_INJECT */
                    std::cout << "  V0: 0x" << std::hex << M128I_U32(m_cpuContext.r[2], 0) << std::dec << std::endl; /* DEBUG_INJECT */
                }
                func(m_memory.getRDRAM(), &m_cpuContext, this);
            }
            std::cout << "Total calls: " << calls << std::endl;
        } catch (const std::exception &e) { std::cerr << "Error: " << e.what() << std::endl; }
        running = false;
    });
    while (running && !WindowShouldClose()) {
        UploadFrame(frameTex, this);
        BeginDrawing();
        ClearBackground(BLACK);
        DrawTexture(frameTex, 0, 0, WHITE);
        EndDrawing();
    }
    shouldExit = true;
    if (gameThread.joinable()) gameThread.join();
    UnloadTexture(frameTex);
    CloseWindow();
}