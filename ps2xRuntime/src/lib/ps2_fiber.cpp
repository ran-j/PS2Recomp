// Backend dispatch: PLATFORM_VITA -> SceFiber; PS2X_FIBER_PTHREAD -> pthread semaphore;
// _WIN32 -> Win32 Fibers; else -> POSIX ucontext
#include "ps2_fiber.h"
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <utility>

// Abort if a fiber context switch is attempted off the guest executor thread.
// Used by the backends (ucontext, SceFiber, Win32 Fibers) where exactly one
// thread ever switches between fiber contexts; doing it from another thread
// is undefined behaviour (ucontext_t) or a documented single-thread-use
// requirement (SceFiber, Win32 Fibers). The pthread backend runs each fiber
// on its own OS thread instead of a shared executor thread, so it does not
// use this guard.
static inline void ps2fiber_require_executor_thread(const char* who)
{
    if (!ps2fiber_on_executor_thread())
    {
        std::fprintf(stderr, "FATAL: %s called off the guest executor thread\n", who);
        std::abort();
    }
}

// The fiber currently running on this thread, or nullptr. PS2Fiber* is the
// same opaque pointer type across all four backends (only the struct it
// points to differs per backend), so the TLS slot and its accessor are
// defined once here instead of once per backend.
static thread_local PS2Fiber* tls_current_fiber_ptr = nullptr;

PS2Fiber* ps2fiber_current()
{
    return tls_current_fiber_ptr;
}

#if !defined(PS2X_FIBER_PTHREAD)
// ucontext, SceFiber, and Win32 Fibers backends: no separate OS thread to
// outlive the PS2Fiber, so the executor relies on FiberContext::state ==
// Finished instead of polling this function. (The pthread backend below has
// a real per-fiber OS thread that can already be dead when polled, so it
// defines its own ps2fiber_finished under its branch.)
bool ps2fiber_finished(PS2Fiber* /*f*/)
{
    return false;
}
#endif

#if !defined(PLATFORM_VITA) && !defined(_WIN32)
// ============================================================================
// Shared guard-paged stack allocator — ucontext and pthread backends only.
// Both are POSIX mmap/mprotect based; Vita and Win32 use their own
// platform-native stack allocation and are excluded here.
// ============================================================================
#include <sys/mman.h>
#include <unistd.h> // sysconf(_SC_PAGESIZE)

// Owns an mmap'd stack with a single low guard page (PROT_NONE) below the
// usable region, so a stack overflow faults instead of silently corrupting
// adjacent memory. Move-only; unmaps on destruction.
struct GuardedStack
{
    uint8_t* base   = nullptr; // mmap base (guard page first)
    size_t   total  = 0;       // total mapping incl. guard page
    size_t   usable = 0;       // usable stack size (page-rounded)

    uint8_t* stack() const { return base + total - usable; } // region above the low guard page

    // Allocate `want` bytes of usable stack (rounded up to a page) plus one
    // low guard page below it. Returns false on failure; *out is untouched.
    static bool make(size_t want, GuardedStack& out)
    {
        long pageQuery = sysconf(_SC_PAGESIZE);
        const size_t page = (pageQuery > 0) ? static_cast<size_t>(pageQuery) : 4096u;
        size_t usable = (want + page - 1) & ~(page - 1);
        size_t total  = usable + page; // 1 guard page at low address

        void* base = mmap(nullptr, total, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (base == MAP_FAILED)
        {
            std::fprintf(stderr, "[ps2fiber] mmap failed\n");
            return false;
        }
        if (mprotect(base, page, PROT_NONE) != 0)
        {
            munmap(base, total);
            std::fprintf(stderr, "[ps2fiber] mprotect guard failed\n");
            return false;
        }

        out.base   = static_cast<uint8_t*>(base);
        out.total  = total;
        out.usable = usable;
        return true;
    }

    GuardedStack() = default;
    GuardedStack(GuardedStack&& o) noexcept { *this = std::move(o); }
    GuardedStack& operator=(GuardedStack&& o) noexcept
    {
        if (this != &o)
        {
            if (base) munmap(base, total);
            base = o.base; total = o.total; usable = o.usable;
            o.base = nullptr; o.total = 0; o.usable = 0;
        }
        return *this;
    }
    GuardedStack(const GuardedStack&) = delete;
    GuardedStack& operator=(const GuardedStack&) = delete;
    ~GuardedStack() { if (base) munmap(base, total); }
};
#endif // !PLATFORM_VITA && !_WIN32

#if !defined(PLATFORM_VITA) && !defined(PS2X_FIBER_PTHREAD) && !defined(_WIN32)
// ============================================================================
// POSIX path — ucontext_t
// ============================================================================
#include <ucontext.h>
#include <cerrno>
#include <cstring>

static_assert(sizeof(void*) == 8, "ucontext pointer split requires exactly 64-bit pointers (LP64)");
static_assert(sizeof(unsigned int) == 4, "makecontext int args must be 32 bits");

// Per-guest-executor-thread state. This is thread_local but in practice only
// ever touched by the single g_guest_thread.
static thread_local ucontext_t tls_guest_main_ctx;

struct PS2Fiber
{
    ucontext_t   ctx;
    GuardedStack stack;
    void       (*fn)(void*) = nullptr;
    void*        arg = nullptr;
};

// makecontext can only pass int-sized arguments portably. Split the 64-bit
// PS2Fiber* into two 32-bit halves and reassemble on entry. fn/arg are stored
// in the struct, so we only need to recover the PS2Fiber* here.
static void ps2fiber_trampoline(unsigned int self_hi, unsigned int self_lo)
{
    uintptr_t raw = (static_cast<uintptr_t>(self_hi) << 32) | static_cast<uintptr_t>(self_lo);
    PS2Fiber* self = reinterpret_cast<PS2Fiber*>(raw);
    self->fn(self->arg);
    // fn returned. Switch back to the guest executor main context. The executor
    // observes fiber state == Finished and frees it. We must NOT return from
    // this function (there is no uc_link); swap out unconditionally.
    swapcontext(&self->ctx, &tls_guest_main_ctx);
    std::abort();
}

PS2Fiber* ps2fiber_alloc(void (*fn)(void*), void* arg, size_t stack_bytes)
{
    if (stack_bytes == 0) { std::fprintf(stderr, "ps2fiber_alloc: zero stack size\n"); return nullptr; }

    PS2Fiber* f = new (std::nothrow) PS2Fiber();
    if (!f) return nullptr;

    if (!GuardedStack::make(stack_bytes, f->stack))
    {
        delete f;
        return nullptr;
    }
    f->fn  = fn;
    f->arg = arg;

    if (getcontext(&f->ctx) != 0)
    {
        std::fprintf(stderr, "[ps2fiber] getcontext failed: %s\n", std::strerror(errno));
        delete f; // GuardedStack destructor unmaps the stack
        return nullptr;
    }
    f->ctx.uc_stack.ss_sp   = f->stack.stack();
    f->ctx.uc_stack.ss_size = f->stack.usable;
    f->ctx.uc_link          = nullptr; // trampoline never returns via link

    uintptr_t raw = reinterpret_cast<uintptr_t>(f);
    makecontext(&f->ctx, reinterpret_cast<void(*)()>(ps2fiber_trampoline), 2,
                static_cast<unsigned int>(raw >> 32),
                static_cast<unsigned int>(raw & 0xFFFFFFFFu));
    return f;
}

void ps2fiber_free(PS2Fiber* f)
{
    if (!f) return;
    delete f; // GuardedStack destructor unmaps the stack
}

void ps2fiber_resume(PS2Fiber* f)
{
    ps2fiber_require_executor_thread("ps2fiber_resume");
    PS2Fiber* prev = tls_current_fiber_ptr;
    tls_current_fiber_ptr = f;
    if (prev == nullptr)
    {
        // Resuming from the guest executor main context.
        swapcontext(&tls_guest_main_ctx, &f->ctx);
    }
    else
    {
        // Unreachable in the N=1 design: the executor only ever resumes a fiber
        // from the main context (prev == nullptr). A non-null prev would mean a
        // fiber resumed another fiber directly, which the scheduler never does.
        std::fprintf(stderr, "FATAL: nested ps2fiber_resume (prev != nullptr)\n");
        std::abort();
    }
    tls_current_fiber_ptr = prev;
}

void ps2fiber_yield()
{
    ps2fiber_require_executor_thread("ps2fiber_yield");
    PS2Fiber* self = tls_current_fiber_ptr;
    if (!self)
    {
        std::fprintf(stderr, "FATAL: ps2fiber_yield with no current fiber\n");
        std::abort();
    }
    tls_current_fiber_ptr = nullptr;
    swapcontext(&self->ctx, &tls_guest_main_ctx);
    tls_current_fiber_ptr = self;
}

#elif defined(PLATFORM_VITA) && !defined(PS2X_FIBER_PTHREAD)
// ============================================================================
// SceFiber backend — Vita (PLATFORM_VITA and not PS2X_FIBER_PTHREAD)
// ============================================================================
#include <psp2/fiber.h>
#include <psp2/kernel/sysmem.h> // sceKernelAllocMemBlock/GetMemBlockBase/FreeMemBlock
#include "ps2_scheduler.h" // extern thread_local int g_currentThreadId

struct PS2Fiber
{
    SceFiber  fiber;              // SDK control block (value, not pointer)
    SceUID    memblock = -1;      // sceKernelAllocMemBlock uid backing ctx (< 0 == none)
    void*     ctx      = nullptr; // usable context buffer = memblock base
    size_t    ctxSize  = 0;
    void    (*fn)(void*) = nullptr;
    void*     arg        = nullptr;
    int       tid        = -1;
};

static void ps2fiber_entry(SceUInt32 /*argOnInitialize*/, SceUInt32 /*argOnRunTo*/)
{
    // ps2fiber_resume already sets tls_current_fiber_ptr = f immediately
    // before sceFiberRun on this same thread, so it's already set here.
    PS2Fiber* self = tls_current_fiber_ptr;
    g_currentThreadId = self->tid;
    self->fn(self->arg);
    // fn returned — hand control back to the thread. The entry must not return.
    sceFiberReturnToThread(0, nullptr);
    std::abort(); // unreachable
}

PS2Fiber* ps2fiber_alloc(void (*fn)(void*), void* arg, size_t stack_bytes)
{
    if (stack_bytes == 0) { std::fprintf(stderr, "ps2fiber_alloc: zero stack size\n"); return nullptr; }
    // sceKernelAllocMemBlock requires the size to be a multiple of 4 KiB for
    // SCE_KERNEL_MEMBLOCK_TYPE_USER_RW (documented VitaSDK behaviour; there is
    // no vitasdk-exposed named constant for the alignment itself). Round up.
    const size_t page = 4096u;
    size_t usable = (stack_bytes + page - 1) & ~(page - 1);

    // NOTE: unlike mmap+mprotect on POSIX, there is no VitaSDK equivalent of a
    // guard page for a plain USER_RW memblock (no per-page protection API for
    // a sub-range of a block), so a Vita fiber stack has NO guard page. A
    // stack overflow here will silently corrupt adjacent memory rather than
    // fault — this is an accepted platform limitation, not an oversight.
    SceUID memblock = sceKernelAllocMemBlock("ps2fiber_stack", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW,
                                              static_cast<SceSize>(usable), nullptr);
    if (memblock < 0)
    {
        std::fprintf(stderr, "[ps2fiber] sceKernelAllocMemBlock failed: 0x%08x\n", memblock);
        return nullptr;
    }
    void* base = nullptr;
    int rcBase = sceKernelGetMemBlockBase(memblock, &base);
    if (rcBase != SCE_OK || !base)
    {
        sceKernelFreeMemBlock(memblock);
        std::fprintf(stderr, "[ps2fiber] sceKernelGetMemBlockBase failed: 0x%08x\n", rcBase);
        return nullptr;
    }

    PS2Fiber* f = new (std::nothrow) PS2Fiber();
    if (!f) { sceKernelFreeMemBlock(memblock); return nullptr; }

    f->memblock = memblock;
    f->ctx      = base;
    f->ctxSize  = usable;
    f->fn  = fn;
    f->arg = arg;

    // vitasdk only declares _sceFiberInitializeImpl (confirmed against
    // vita-headers db/360/SceFiber.yml — there is no sceFiberInitialize NID),
    // so call the Impl entry point directly. It takes a non-const char* name.
    int rc = _sceFiberInitializeImpl(&f->fiber, const_cast<char*>("ps2fiber"), ps2fiber_entry, 0,
                                      f->ctx, static_cast<SceSize>(f->ctxSize), nullptr);
    if (rc != 0)
    {
        sceKernelFreeMemBlock(memblock);
        delete f;
        std::fprintf(stderr, "[ps2fiber] _sceFiberInitializeImpl failed: 0x%08x\n", rc);
        return nullptr;
    }
    return f;
}

void ps2fiber_set_tid(PS2Fiber* f, int tid)
{
    // No-op stub for SceFiber: guest_executor_main already publishes
    // g_currentThreadId on the executor thread (N=1 — no cross-thread TLS hazard).
    // The tid is stored so ps2fiber_entry can publish it when the fiber first runs.
    if (f) f->tid = tid;
}

void ps2fiber_resume(PS2Fiber* f)
{
    ps2fiber_require_executor_thread("ps2fiber_resume");
    PS2Fiber* prev = tls_current_fiber_ptr;
    tls_current_fiber_ptr = f;
    SceInt32 rc = sceFiberRun(&f->fiber, 0, nullptr);
    if (rc != SCE_OK)
    {
        std::fprintf(stderr, "[ps2fiber] sceFiberRun failed: 0x%08x\n", rc);
        std::abort();
    }
    tls_current_fiber_ptr = prev;
}

void ps2fiber_yield()
{
    ps2fiber_require_executor_thread("ps2fiber_yield");
    PS2Fiber* self = tls_current_fiber_ptr;
    if (!self) { std::fprintf(stderr, "FATAL: ps2fiber_yield with no current fiber\n"); std::abort(); }
    tls_current_fiber_ptr = nullptr;
    SceInt32 rcRet = sceFiberReturnToThread(0, nullptr);
    if (rcRet != SCE_OK)
    {
        std::fprintf(stderr, "[ps2fiber] sceFiberReturnToThread failed: 0x%08x\n", rcRet);
        std::abort();
    }
    tls_current_fiber_ptr = self;
}

void ps2fiber_free(PS2Fiber* f)
{
    if (!f) return;
    // sceFiberInitialize always succeeds before ps2fiber_alloc returns, so
    // every allocated fiber must be finalized — even one that was never started.
    SceInt32 rcFin = sceFiberFinalize(&f->fiber);
    if (rcFin != SCE_OK)
    {
        std::fprintf(stderr, "[ps2fiber] sceFiberFinalize failed: 0x%08x\n", rcFin);
        std::abort();
    }
    if (f->memblock >= 0) sceKernelFreeMemBlock(f->memblock);
    delete f;
}

#elif defined(PS2X_FIBER_PTHREAD)
// ============================================================================
// pthread backend — Linux TSan testing (PS2X_FIBER_PTHREAD)
// ============================================================================
#if defined(PLATFORM_VITA)
#error "Both PLATFORM_VITA and PS2X_FIBER_PTHREAD defined — use Vita toolchain without PS2X_FIBER_PTHREAD"
#endif
#include <pthread.h>
#include <semaphore.h>
#include "ps2_scheduler.h" // extern thread_local int g_currentThreadId

struct PS2Fiber
{
    pthread_t thread;
    sem_t     resume_sem; // posted by ps2fiber_resume; waited by fiber thread
    sem_t     yield_sem;  // posted by ps2fiber_yield/finish; waited by resume
    void    (*fn)(void*) = nullptr;
    void*     arg        = nullptr;
    bool      started    = false; // pthread created
    std::atomic<bool> finished{false}; // fn returned; written on the fiber thread and read on the executor, so must be atomic
    std::atomic<bool> ever_resumed{false}; // set by ps2fiber_resume; distinguishes "never resumed" (free is legal) from "parked mid-run" (free is a caller bug)
    std::atomic<bool> abandoned{false}; // set by ps2fiber_free when freeing a never-resumed fiber; tells the worker to exit without running fn
    GuardedStack stack; // guard-paged stack backing `thread`; unmapped after pthread_join
    // Guest thread id for this fiber. Set by the scheduler via
    // ps2fiber_set_tid() after alloc; published onto the fiber's own pthread in
    // ps2fiber_thread_main so blocking syscalls see the right g_currentThreadId.
    // Defaults to -1 until the scheduler assigns it.
    int       tid        = -1;
};

static void* ps2fiber_thread_main(void* raw)
{
    PS2Fiber* self = reinterpret_cast<PS2Fiber*>(raw);
    sem_wait(&self->resume_sem); // wait for first resume
    if (self->abandoned.load(std::memory_order_acquire))
    {
        // ps2fiber_free abandoned this fiber before the scheduler ever resumed
        // it (legal per ps2_fiber.h's contract: free is allowed "before it was
        // ever resumed"). Exit immediately without running fn or touching
        // guest TLS; ps2fiber_free is parked in pthread_join waiting for us.
        self->finished.store(true, std::memory_order_release);
        return nullptr;
    }
    // Publish guest identity ON THE FIBER'S OWN THREAD. On Vita guest code
    // runs here, not on the executor thread, so blocking syscalls must read the
    // fiber's tid / current-fiber pointer from this thread's TLS.
    tls_current_fiber_ptr = self;
    g_currentThreadId = self->tid;
    self->fn(self->arg);
    self->finished.store(true, std::memory_order_release);
    g_currentThreadId = -1;
    tls_current_fiber_ptr = nullptr;
    sem_post(&self->yield_sem); // hand control back to executor; thread ends
    return nullptr;
}

PS2Fiber* ps2fiber_alloc(void (*fn)(void*), void* arg, size_t stack_bytes)
{
    PS2Fiber* f = new (std::nothrow) PS2Fiber();
    if (!f) return nullptr;
    f->fn  = fn;
    f->arg = arg;

    // Allocate our own stack with an explicit guard page at the low address so
    // a stack overflow faults (SIGSEGV) rather than silently corrupting memory.
    size_t want = stack_bytes ? stack_bytes : (512 * 1024); // smaller on Vita
    if (!GuardedStack::make(want, f->stack))
    {
        delete f;
        return nullptr;
    }

    sem_init(&f->resume_sem, 0, 0);
    sem_init(&f->yield_sem,  0, 0);

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    // Belt-and-suspenders: ask pthread to set its own guard too (may be a no-op
    // on some platforms, but the mmap guard in GuardedStack::make is the
    // authoritative one).
    long pg_long = sysconf(_SC_PAGESIZE);
    size_t page   = (pg_long > 0) ? static_cast<size_t>(pg_long) : 4096u;
    pthread_attr_setguardsize(&attr, page);
    // Hand our pre-allocated, guard-paged stack to pthread.
    pthread_attr_setstack(&attr, f->stack.stack(), f->stack.usable);
    int rc = pthread_create(&f->thread, &attr, ps2fiber_thread_main, f);
    pthread_attr_destroy(&attr);
    if (rc != 0)
    {
        sem_destroy(&f->resume_sem);
        sem_destroy(&f->yield_sem);
        delete f; // GuardedStack destructor unmaps the stack
        return nullptr;
    }
    f->started = true;
    return f;
}

void ps2fiber_set_tid(PS2Fiber* f, int tid)
{
    if (f) f->tid = tid;
}

void ps2fiber_resume(PS2Fiber* f)
{
    f->ever_resumed.store(true, std::memory_order_release);
    sem_post(&f->resume_sem); // wake fiber thread
    sem_wait(&f->yield_sem);  // block until it yields or finishes
}

void ps2fiber_yield()
{
    PS2Fiber* self = tls_current_fiber_ptr;
    if (!self) { std::abort(); }
    sem_post(&self->yield_sem);  // hand back to executor
    sem_wait(&self->resume_sem); // wait for next resume
}

bool ps2fiber_finished(PS2Fiber* f)
{
    // ps2fiber_thread_main sets finished=true just before the pthread exits.
    // The executor uses this to avoid resuming a dead fiber thread.
    return f && f->finished.load(std::memory_order_acquire);
}

void ps2fiber_free(PS2Fiber* f)
{
    if (!f) return;
    if (f->started && !f->finished.load(std::memory_order_acquire))
    {
        if (!f->ever_resumed.load(std::memory_order_acquire))
        {
            // Never resumed: legal per ps2_fiber.h's contract ("before it was
            // ever resumed"). The worker thread is parked in its first
            // sem_wait(resume_sem); wake it with 'abandoned' set so it exits
            // immediately instead of running fn / publishing guest TLS.
            f->abandoned.store(true, std::memory_order_release);
            sem_post(&f->resume_sem);
        }
        else
        {
            // Resumed at least once and currently parked mid-run (yielded, not
            // finished): a genuine caller bug — the scheduler must only free
            // fibers that are Finished (or were never started).
            std::fprintf(stderr, "FATAL: ps2fiber_free on unfinished fiber\n");
            std::abort();
        }
    }
    if (f->started) pthread_join(f->thread, nullptr); // joinable: no leak
    sem_destroy(&f->resume_sem);
    sem_destroy(&f->yield_sem);
    // GuardedStack destructor (in ~PS2Fiber via delete below) unmaps the
    // guard-paged stack — only now that the thread is fully joined (exited).
    delete f;
}

#elif defined(_WIN32)
// ============================================================================
// Win32 Fibers backend — Windows (_WIN32, not PLATFORM_VITA/PS2X_FIBER_PTHREAD)
// ============================================================================
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

// Per-guest-executor-thread state. This is thread_local but in practice only
// ever touched by the single g_guest_thread.
static thread_local LPVOID tls_guest_main_fiber = nullptr; // executor's own fiber (the "main context")

struct PS2Fiber
{
    LPVOID  fiber = nullptr; // CreateFiberEx handle
    void  (*fn)(void*) = nullptr;
    void*   arg = nullptr;
};

// Lazily turn the executor thread itself into a fiber so there is a "main"
// fiber to SwitchToFiber back to. ConvertThreadToFiberEx fails with
// ERROR_ALREADY_FIBER when the thread was already converted (e.g. by host
// code), so detect that case via IsThreadAFiber/GetCurrentFiber instead of
// calling it twice.
//
// FIBER_FLAG_FLOAT_SWITCH matters here and in CreateFiberEx: without it,
// x87/SSE floating-point state is NOT saved across fiber switches, and guest
// code relies heavily on FPU/SSE state (COP1, VU macro mode).
static LPVOID ps2fiber_main_fiber()
{
    if (tls_guest_main_fiber) return tls_guest_main_fiber;
    if (IsThreadAFiber())
    {
        tls_guest_main_fiber = GetCurrentFiber();
        return tls_guest_main_fiber;
    }
    LPVOID mainFiber = ConvertThreadToFiberEx(nullptr, FIBER_FLAG_FLOAT_SWITCH);
    if (!mainFiber)
    {
        std::fprintf(stderr, "FATAL: ConvertThreadToFiberEx failed: %lu\n",
                     static_cast<unsigned long>(GetLastError()));
        std::abort();
    }
    tls_guest_main_fiber = mainFiber;
    return mainFiber;
}

static void CALLBACK ps2fiber_trampoline(void* raw)
{
    PS2Fiber* self = static_cast<PS2Fiber*>(raw);
    self->fn(self->arg);
    // fn returned. Switch back to the guest executor main fiber. The executor
    // observes fiber state == Finished and frees it. We must NOT return from
    // this start routine: a Win32 fiber start routine that returns calls
    // ExitThread, silently killing the entire executor thread.
    SwitchToFiber(tls_guest_main_fiber);
    std::fprintf(stderr, "FATAL: Win32 fiber resumed after its fn returned\n");
    std::abort();
}

PS2Fiber* ps2fiber_alloc(void (*fn)(void*), void* arg, size_t stack_bytes)
{
    if (stack_bytes == 0) { std::fprintf(stderr, "ps2fiber_alloc: zero stack size\n"); return nullptr; }
    PS2Fiber* f = new (std::nothrow) PS2Fiber();
    if (!f) return nullptr;
    f->fn  = fn;
    f->arg = arg;

    // Reserve the caller's full stack_bytes but commit only 64 KiB up front:
    // Windows grows the committed region on demand through its guard-page
    // mechanism, so a large reserve costs address space, not RAM, and 64 KiB
    // comfortably covers the trampoline + scheduler frames before guest code
    // pushes deeper. (If stack_bytes is smaller than the commit, the system
    // rounds the reserve up to cover it.) Unlike the ucontext backend's
    // explicit mprotect(PROT_NONE) page, Windows fiber stacks get an
    // OS-managed guard page natively, so overflow raises
    // STATUS_STACK_OVERFLOW instead of silently corrupting adjacent memory.
    const SIZE_T commit  = 64 * 1024;
    const SIZE_T reserve = stack_bytes;
    f->fiber = CreateFiberEx(commit, reserve, FIBER_FLAG_FLOAT_SWITCH, ps2fiber_trampoline, f);
    if (!f->fiber)
    {
        std::fprintf(stderr, "[ps2fiber] CreateFiberEx failed: %lu\n",
                     static_cast<unsigned long>(GetLastError()));
        delete f;
        return nullptr;
    }
    return f;
}

void ps2fiber_free(PS2Fiber* f)
{
    if (!f) return;
    if (f->fiber)
    {
        // DeleteFiber is safe both for a Finished fiber (parked on the main
        // fiber after its fn returned) and for one that was never resumed.
        // It must NOT run for the currently executing fiber — DeleteFiber on
        // the running fiber calls ExitThread. The ps2_fiber.h contract already
        // forbids freeing a running fiber; enforce it anyway.
        if (IsThreadAFiber() && GetCurrentFiber() == f->fiber)
        {
            std::fprintf(stderr, "FATAL: ps2fiber_free called on the running fiber\n");
            std::abort();
        }
        DeleteFiber(f->fiber);
    }
    delete f;
}

void ps2fiber_resume(PS2Fiber* f)
{
    ps2fiber_require_executor_thread("ps2fiber_resume");
    ps2fiber_main_fiber(); // lazily convert the executor thread on first use
    PS2Fiber* prev = tls_current_fiber_ptr;
    tls_current_fiber_ptr = f;
    if (prev == nullptr)
    {
        // Resuming from the guest executor main fiber.
        SwitchToFiber(f->fiber);
    }
    else
    {
        // Unreachable in the N=1 design: the executor only ever resumes a fiber
        // from the main context (prev == nullptr). A non-null prev would mean a
        // fiber resumed another fiber directly, which the scheduler never does.
        std::fprintf(stderr, "FATAL: nested ps2fiber_resume (prev != nullptr)\n");
        std::abort();
    }
    tls_current_fiber_ptr = prev;
}

void ps2fiber_yield()
{
    ps2fiber_require_executor_thread("ps2fiber_yield");
    PS2Fiber* self = tls_current_fiber_ptr;
    if (!self)
    {
        std::fprintf(stderr, "FATAL: ps2fiber_yield with no current fiber\n");
        std::abort();
    }
    tls_current_fiber_ptr = nullptr;
    SwitchToFiber(tls_guest_main_fiber);
    tls_current_fiber_ptr = self;
}

#else
#error "Unknown fiber backend: define PLATFORM_VITA (SceFiber), PS2X_FIBER_PTHREAD (pthread), build for _WIN32 (Win32 Fibers), or use the default POSIX ucontext backend"
#endif // fiber backend dispatch
