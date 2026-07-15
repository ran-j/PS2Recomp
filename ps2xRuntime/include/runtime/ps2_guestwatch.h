#ifndef PS2_GUESTWATCH_H
#define PS2_GUESTWATCH_H

// Poll-based guest-memory transition watcher + writer-PC capture.
//
// IMPORTANT: this header must be included ONLY by ps2_runtime.cpp. The tiny
// forward declarations for g_writeWatchActive / onGuestWrite that live in
// ps2_runtime.h (so the write-hook stays cheap on the recompiled-unit include
// graph) must stay in sync with the declarations below.

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "ps2_log.h"
#include "runtime/ps2_diag.h"
#include "runtime/ps2_memory.h"

namespace ps2_watch
{
struct Watch
{
    uint32_t addr = 0;
    uint32_t byteWidth = 4;
    std::string label;

    uint64_t prevValue = 0;
    bool hasPrevValue = false;
    uint64_t changeCount = 0;

    uint32_t writerPc = 0;
    bool hasWriterPc = false;
};

inline std::mutex &registryMutex()
{
    static std::mutex m;
    return m;
}

inline std::vector<Watch> &registry()
{
    static std::vector<Watch> watches;
    return watches;
}

// Mirrors registry().size(); lets pollWatches() bail out before touching
// rdram or taking the mutex when nothing is registered.
inline std::atomic<size_t> &watchCountRef()
{
    static std::atomic<size_t> count{0};
    return count;
}

// Declared here (and forward-declared minimally in ps2_runtime.h); defined
// out-of-line in ps2_runtime.cpp so ps2_runtime.h doesn't need this header.
extern std::atomic<bool> g_writeWatchActive;
void onGuestWrite(uint32_t addr, uint32_t size, uint64_t lo, uint64_t hi, uint32_t pc) noexcept;

inline void addWatch(uint32_t addr, uint32_t byteWidth, const char *label)
{
    Watch w;
    w.addr = addr;
    w.byteWidth = (byteWidth == 0u) ? 4u : byteWidth;
    w.label = (label != nullptr) ? label : "";

    {
        std::lock_guard<std::mutex> lock(registryMutex());
        registry().push_back(std::move(w));
        watchCountRef().store(registry().size(), std::memory_order_relaxed);
    }

    g_writeWatchActive.store(true, std::memory_order_relaxed);
}

// Parses a PS2X_WATCH spec and registers one watch per entry. Format:
//   PS2X_WATCH=ADDR[:SIZE][:LABEL][,ADDR[:SIZE][:LABEL]...]
// ADDR is hex (0x-prefixed) or decimal; SIZE defaults to 4; LABEL is an
// optional free-form tag (no ':' or ','). Returns the number of watches
// registered. `spec` may be null/empty (returns 0).
inline size_t armWatchesFromEnv(const char *spec)
{
    if (spec == nullptr || spec[0] == '\0')
    {
        return 0;
    }

    size_t armed = 0;
    std::string s(spec);
    size_t pos = 0;
    while (pos <= s.size())
    {
        const size_t comma = s.find(',', pos);
        const size_t end = (comma == std::string::npos) ? s.size() : comma;
        std::string entry = s.substr(pos, end - pos);
        pos = (comma == std::string::npos) ? (s.size() + 1) : (comma + 1);

        if (entry.empty())
        {
            continue;
        }

        // Split entry into up to 3 fields on ':'
        std::string addrField;
        std::string sizeField;
        std::string labelField;
        const size_t c1 = entry.find(':');
        if (c1 == std::string::npos)
        {
            addrField = entry;
        }
        else
        {
            addrField = entry.substr(0, c1);
            const size_t c2 = entry.find(':', c1 + 1);
            if (c2 == std::string::npos)
            {
                sizeField = entry.substr(c1 + 1);
            }
            else
            {
                sizeField = entry.substr(c1 + 1, c2 - (c1 + 1));
                labelField = entry.substr(c2 + 1);
            }
        }

        if (addrField.empty())
        {
            continue;
        }

        char *addrEnd = nullptr;
        const unsigned long addr = std::strtoul(addrField.c_str(), &addrEnd, 0);
        if (addrEnd == addrField.c_str())
        {
            continue; // unparsable address, skip
        }

        uint32_t byteWidth = 4u;
        if (!sizeField.empty())
        {
            char *sizeEnd = nullptr;
            const unsigned long parsedSize = std::strtoul(sizeField.c_str(), &sizeEnd, 0);
            if (sizeEnd != sizeField.c_str() && parsedSize != 0ul)
            {
                byteWidth = static_cast<uint32_t>(parsedSize);
            }
        }

        addWatch(static_cast<uint32_t>(addr), byteWidth,
                 labelField.empty() ? "watch" : labelField.c_str());
        ++armed;
    }

    return armed;
}

inline void clearWatches()
{
    std::lock_guard<std::mutex> lock(registryMutex());
    registry().clear();
    watchCountRef().store(0, std::memory_order_relaxed);
    g_writeWatchActive.store(false, std::memory_order_relaxed);
}

inline uint64_t readLittleEndian(const uint8_t *rdram, uint32_t addr, uint32_t byteWidth)
{
    uint64_t value = 0;
    const uint32_t clampedWidth = (byteWidth > 8u) ? 8u : byteWidth;
    for (uint32_t i = 0; i < clampedWidth; ++i)
    {
        const uint32_t byteAddr = (addr + i) & PS2_RAM_MASK;
        value |= static_cast<uint64_t>(rdram[byteAddr]) << (8u * i);
    }
    return value;
}

// Polls every registered watch for a value transition. Zero overhead when no
// watches are registered (bails before touching rdram or the gate/mutex).
inline void pollWatches(const uint8_t *rdram)
{
    if (watchCountRef().load(std::memory_order_relaxed) == 0u)
    {
        return;
    }
    if (!ps2_diag::enabled())
    {
        return;
    }
    if (rdram == nullptr)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(registryMutex());
    for (Watch &w : registry())
    {
        const uint64_t current = readLittleEndian(rdram, w.addr, w.byteWidth);

        if (!w.hasPrevValue)
        {
            w.prevValue = current;
            w.hasPrevValue = true;
            continue;
        }

        if (current != w.prevValue)
        {
            const uint64_t c = w.changeCount;
            if (ps2_diag::should_log(c, 16, 600))
            {
                std::string pcSuffix;
                if (w.hasWriterPc)
                {
                    std::ostringstream pcStream;
                    pcStream << " pc=0x" << std::hex << w.writerPc;
                    pcSuffix = pcStream.str();
                }

                PS2X_DIAG_LOG("[watch] " << w.label
                                       << " 0x" << std::hex << w.addr
                                       << ": 0x" << w.prevValue
                                       << " -> 0x" << current
                                       << std::dec
                                       << " (change #" << (c + 1) << ")"
                                       << pcSuffix);
            }
            w.changeCount = c + 1;
            w.prevValue = current;
            w.hasWriterPc = false;
        }
    }
}

} // namespace ps2_watch

#endif // PS2_GUESTWATCH_H
