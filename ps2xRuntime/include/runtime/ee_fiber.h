#pragma once

#include <cstddef>
#include <cstdint>

// A stack a guest thread can be suspended on and resumed into.
//
// The EE scheduler used to transfer control by throwing EeDispatcherTransfer,
// which unwinds the guest's whole C++ call chain and then rebuilds it on the
// way back. That cost ~10us per switch against the ~540ns a 60fps movie frame
// can afford, because DQ8's movie thread yields ~31,000 times per frame.
// Suspending the stack instead makes a switch a register save and a stack
// pointer swap.
//
// x86-64 gets a hand-written switch; everything else falls back to ucontext,
// which is correct but pays a sigprocmask syscall per switch. setjmp/longjmp
// across stacks is deliberately not used: glibc's _FORTIFY_SOURCE turns it
// into an abort ("longjmp causes uninitialized stack frame").
class EeFiber
{
public:
    using EntryFn = void (*)(void *user);

    EeFiber() = default;
    ~EeFiber();

    EeFiber(const EeFiber &) = delete;
    EeFiber &operator=(const EeFiber &) = delete;

    // Allocates the stack and arms `entry`; entry must never return.
    bool create(EntryFn entry, void *user, size_t stackBytes);
    [[nodiscard]] bool valid() const noexcept { return m_stack != nullptr; }
    void destroy();

    // Called from the scheduler: run this fiber until it switches back.
    void resume();
    // Called from inside the fiber: hand control back to whoever resumed it.
    void suspend();

    [[nodiscard]] static bool usingFastSwitch() noexcept;

private:
    void *m_stack = nullptr;      // lowest address of the allocation
    size_t m_stackBytes = 0u;
    void *m_fiberSp = nullptr;    // suspended fiber stack pointer
    void *m_returnSp = nullptr;   // stack pointer of whoever called resume()
    void *m_platform = nullptr;   // ucontext pair on the fallback path
};
