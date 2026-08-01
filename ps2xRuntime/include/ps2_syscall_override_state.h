#ifndef PS2_SYSCALL_OVERRIDE_STATE_H
#define PS2_SYSCALL_OVERRIDE_STATE_H
#include <cstdint>
#include <vector>

// Stack of syscall numbers whose overrides are currently executing on ONE guest
// context. dispatchSyscallOverride pushes a number before invoking its guest
// handler and pops it after; if the number is already present the handler is NOT
// re-entered (it falls through to the builtin) — this is what lets an override
// handler safely re-issue the very syscall it overrides without recursing.
//
// Owned per-fiber by FiberContext under the N=1 executor: a fiber parked mid-
// override keeps its own active set, so it never marks the syscall active for a
// different fiber, and every push/pop stays LIFO-balanced within one fiber's
// call chain. Host workers and direct non-fiber callers use a per-OS-thread
// fallback (each runs its override chain to completion without yielding).
// Mirrors the DispatchHistory ownership model (ps2_dispatch_history.h).
struct SyscallOverrideStack
{
    std::vector<uint32_t> active;
};
#endif // PS2_SYSCALL_OVERRIDE_STATE_H
