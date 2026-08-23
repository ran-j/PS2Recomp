#include "runtime/ee_fiber.h"

#include <cstdlib>
#include <cstring>
#include <new>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/mman.h>
#define EE_FIBER_HAVE_MMAP 1
#else
#define EE_FIBER_HAVE_MMAP 0
#endif

namespace
{
    // Whole mapping including one guard page at the low end.
    void *mmapStack(size_t bytes)
    {
#if EE_FIBER_HAVE_MMAP
        void *base = mmap(nullptr, bytes, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (base == MAP_FAILED)
        {
            return nullptr;
        }
        if (mprotect(base, 4096u, PROT_NONE) != 0)
        {
            munmap(base, bytes);
            return nullptr;
        }
        return base;
#else
        return std::aligned_alloc(64u, bytes);
#endif
    }

    void munmapStack(void *base, size_t bytes)
    {
#if EE_FIBER_HAVE_MMAP
        if (base != nullptr)
        {
            munmap(base, bytes);
        }
#else
        (void)bytes;
        std::free(base);
#endif
    }
} // namespace

#if defined(__x86_64__) && (defined(__linux__) || defined(__unix__) || defined(__APPLE__))
#define EE_FIBER_FAST_X86_64 1
#else
#define EE_FIBER_FAST_X86_64 0
#include <ucontext.h>
#endif

namespace
{
#if EE_FIBER_FAST_X86_64
    // void eeFiberSwitch(void **saveSp, void *targetSp)
    //
    // Saves the callee-saved registers and the FP control words on the current
    // stack, records the resulting stack pointer through saveSp, switches to
    // targetSp and restores the same set from there. The final `ret` lands on
    // whatever return address that stack was suspended at -- for a fresh fiber,
    // the trampoline create() planted.
    extern "C" void eeFiberSwitch(void **saveSp, void *targetSp);
    __asm__(
        ".text\n"
        ".globl eeFiberSwitch\n"
        ".hidden eeFiberSwitch\n"
        ".type eeFiberSwitch,@function\n"
        ".align 16\n"
        "eeFiberSwitch:\n"
        "    pushq %rbp\n"
        "    pushq %rbx\n"
        "    pushq %r12\n"
        "    pushq %r13\n"
        "    pushq %r14\n"
        "    pushq %r15\n"
        "    subq  $8, %rsp\n"
        "    stmxcsr (%rsp)\n"
        "    fnstcw 4(%rsp)\n"
        "    movq %rsp, (%rdi)\n"
        "    movq %rsi, %rsp\n"
        "    ldmxcsr (%rsp)\n"
        "    fldcw 4(%rsp)\n"
        "    addq $8, %rsp\n"
        "    popq %r15\n"
        "    popq %r14\n"
        "    popq %r13\n"
        "    popq %r12\n"
        "    popq %rbx\n"
        "    popq %rbp\n"
        "    ret\n"
        ".size eeFiberSwitch,.-eeFiberSwitch\n");

    struct FiberBootstrap
    {
        EeFiber::EntryFn entry;
        void *user;
    };

    // Reached by the first `ret` out of eeFiberSwitch. %rbx carries the
    // bootstrap because create() planted it in the saved-register slot.
    extern "C" void eeFiberTrampoline();
    __asm__(
        ".text\n"
        ".globl eeFiberTrampoline\n"
        ".hidden eeFiberTrampoline\n"
        ".type eeFiberTrampoline,@function\n"
        ".align 16\n"
        "eeFiberTrampoline:\n"
        // Entered by `ret`, so rsp%16 == 8 as at any function entry. SysV wants
        // rsp%16 == 0 immediately before a call; without this the callee's
        // 16-byte SSE spills fault.
        "    subq $8, %rsp\n"
        "    movq %rbx, %rdi\n"
        "    call eeFiberEnter\n"
        "    hlt\n" // eeFiberEnter never returns
        ".size eeFiberTrampoline,.-eeFiberTrampoline\n");

    extern "C" void eeFiberEnter(void *bootstrap)
    {
        auto *boot = static_cast<FiberBootstrap *>(bootstrap);
        boot->entry(boot->user);
        std::abort(); // entry must never return
    }
#else
    struct FiberPlatform
    {
        ucontext_t fiber{};
        ucontext_t caller{};
        EeFiber::EntryFn entry = nullptr;
        void *user = nullptr;
    };

    FiberPlatform *g_startingFiber = nullptr;

    void fiberTrampoline()
    {
        FiberPlatform *self = g_startingFiber;
        self->entry(self->user);
        std::abort();
    }
#endif
} // namespace

bool EeFiber::usingFastSwitch() noexcept
{
    return EE_FIBER_FAST_X86_64 != 0;
}

EeFiber::~EeFiber()
{
    destroy();
}

void EeFiber::destroy()
{
#if !EE_FIBER_FAST_X86_64
    delete static_cast<FiberPlatform *>(m_platform);
#else
    std::free(m_platform);
#endif
    m_platform = nullptr;
    munmapStack(m_stack, m_stackBytes);
    m_stack = nullptr;
    m_stackBytes = 0u;
    m_fiberSp = nullptr;
    m_returnSp = nullptr;
}

bool EeFiber::create(EntryFn entry, void *user, size_t stackBytes)
{
    destroy();
    if (entry == nullptr || stackBytes < 64u * 1024u)
    {
        return false;
    }
    // Guest call chains nest in C++ through dispatchGuestBranch, so a fiber
    // stack can run deep. Map it with a PROT_NONE guard page at the low end:
    // an overflow then faults on the guard instead of quietly writing into
    // whatever the allocator put underneath.
    const size_t pageSize = 4096u;
    const size_t mapped = ((stackBytes + pageSize - 1u) / pageSize) * pageSize + pageSize;
    void *base = mmapStack(mapped);
    if (base == nullptr)
    {
        return false;
    }
    void *stack = static_cast<char *>(base) + pageSize;
    m_stack = base;
    m_stackBytes = mapped;
    stackBytes = mapped - pageSize;

#if EE_FIBER_FAST_X86_64
    auto *boot = static_cast<FiberBootstrap *>(std::malloc(sizeof(FiberBootstrap)));
    if (boot == nullptr)
    {
        destroy();
        return false;
    }
    boot->entry = entry;
    boot->user = user;
    m_platform = boot;

    // Build the frame eeFiberSwitch will pop: FP words, r15..rbx, rbp, then the
    // return address it rets to. rbx carries the bootstrap into the trampoline.
    auto *top = reinterpret_cast<uintptr_t *>(static_cast<char *>(stack) + stackBytes);
    top = reinterpret_cast<uintptr_t *>(reinterpret_cast<uintptr_t>(top) & ~static_cast<uintptr_t>(15));
    // SysV wants rsp % 16 == 8 on entry, i.e. the return-address slot 16-aligned.
    *--top = 0u;                                              // alignment padding
    *--top = reinterpret_cast<uintptr_t>(&eeFiberTrampoline); // ret target
    *--top = 0u;                                              // rbp
    *--top = reinterpret_cast<uintptr_t>(boot);               // rbx
    *--top = 0u;                                              // r12
    *--top = 0u;                                              // r13
    *--top = 0u;                                              // r14
    *--top = 0u;                                              // r15
    // mxcsr (low 32) + x87 control word (next 16), matching the switch's layout.
    uint32_t mxcsr = 0x1F80u;
    uint16_t fcw = 0x037Fu;
    __asm__ __volatile__("stmxcsr %0" : "=m"(mxcsr));
    __asm__ __volatile__("fnstcw %0" : "=m"(fcw));
    --top;
    std::memcpy(top, &mxcsr, sizeof(mxcsr));
    std::memcpy(reinterpret_cast<char *>(top) + 4, &fcw, sizeof(fcw));
    m_fiberSp = top;
#else
    auto *platform = new (std::nothrow) FiberPlatform();
    if (platform == nullptr)
    {
        destroy();
        return false;
    }
    platform->entry = entry;
    platform->user = user;
    if (getcontext(&platform->fiber) != 0)
    {
        delete platform;
        destroy();
        return false;
    }
    platform->fiber.uc_stack.ss_sp = stack;
    platform->fiber.uc_stack.ss_size = stackBytes;
    platform->fiber.uc_link = nullptr;
    makecontext(&platform->fiber, fiberTrampoline, 0);
    m_platform = platform;
#endif
    return true;
}

void EeFiber::resume()
{
#if EE_FIBER_FAST_X86_64
    eeFiberSwitch(&m_returnSp, m_fiberSp);
#else
    auto *platform = static_cast<FiberPlatform *>(m_platform);
    g_startingFiber = platform;
    swapcontext(&platform->caller, &platform->fiber);
#endif
}

void EeFiber::suspend()
{
#if EE_FIBER_FAST_X86_64
    eeFiberSwitch(&m_fiberSp, m_returnSp);
#else
    auto *platform = static_cast<FiberPlatform *>(m_platform);
    swapcontext(&platform->fiber, &platform->caller);
#endif
}
