#ifndef PS2_DISPATCH_HISTORY_H
#define PS2_DISPATCH_HISTORY_H
#include <array>
#include <cstdint>

// Fixed-size ring of recently dispatched guest PCs, used only for diagnostic
// "trace=..." output. Owned per-fiber by FiberContext (fresh per fiber, gone at
// teardown); host workers / non-fiber callers use a per-OS-thread fallback.
struct DispatchHistory
{
    std::array<uint32_t, 64> pcs{};
    uint32_t next    = 0u;
    bool     wrapped = false;
};
#endif // PS2_DISPATCH_HISTORY_H
