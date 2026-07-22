#ifndef PS2_DIAG_H
#define PS2_DIAG_H

// Opt-in runtime diagnostics gate + small helpers shared by every probe added
// under the "runtime observability" effort.
//
// IMPORTANT: this header must be included ONLY by runtime .cpp files (or by
// headers that are themselves only ever pulled in by runtime .cpp files, and
// never by anything on the recompiled-unit include graph). Recompiled guest
// code must never see this header.

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace ps2_diag
{
namespace detail
{
    // -1 = not yet resolved, 0 = disabled, 1 = enabled.
    inline std::atomic<int> &cachedEnabledState()
    {
        static std::atomic<int> state{-1};
        return state;
    }
}

// Returns true when the global diagnostics gate (PS2X_DIAG env var) is on.
// The result of the first call is cached in a function-local atomic, so
// every subsequent call costs a single relaxed atomic load when the gate is
// off (the common case).
inline bool enabled()
{
    std::atomic<int> &state = detail::cachedEnabledState();
    const int cached = state.load(std::memory_order_relaxed);
    if (cached >= 0)
    {
        return cached != 0;
    }

    const char *value = std::getenv("PS2X_DIAG");
    bool isEnabled = false;
    if (value != nullptr && value[0] != '\0')
    {
        if (std::strcmp(value, "1") == 0 || std::strcmp(value, "true") == 0)
        {
            isEnabled = true;
        }
        else
        {
            char *end = nullptr;
            const long parsed = std::strtol(value, &end, 10);
            if (end != value && parsed != 0)
            {
                isEnabled = true;
            }
        }
    }

    state.store(isEnabled ? 1 : 0, std::memory_order_relaxed);
    return isEnabled;
}

// Forces the cached gate state on/off. Used at boot when PS2X_WATCH arms
// a watch (so [watch] output is produced even if PS2X_DIAG is unset), and
// by tests.
inline void set_enabled(bool value)
{
    detail::cachedEnabledState().store(value ? 1 : 0, std::memory_order_relaxed);
}

// Test-only override for the cached gate state.
inline void set_enabled_for_test(bool value)
{
    set_enabled(value);
}

// Parses an integer environment variable, returning `def` when unset or
// unparsable.
inline int env_int(const char *name, int def)
{
    const char *value = std::getenv(name);
    if (value == nullptr || value[0] == '\0')
    {
        return def;
    }

    char *end = nullptr;
    const long parsed = std::strtol(value, &end, 10);
    if (end == value)
    {
        return def;
    }

    return static_cast<int>(parsed);
}

// "First-N verbose, then every-Nth" throttle shared by every bounded probe.
inline bool should_log(uint64_t count, uint64_t firstN, uint64_t everyNth)
{
    return count < firstN || (everyNth != 0 && (count % everyNth) == 0);
}

} // namespace ps2_diag

#endif // PS2_DIAG_H
