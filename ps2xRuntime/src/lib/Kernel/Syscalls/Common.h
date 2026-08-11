#include "ps2_syscalls.h"
#include "ps2_log.h"
#include "ps2_runtime.h"
#include "runtime/ee_scheduler.h"
#include "ps2_runtime_macros.h"
#include "ps2_stubs.h"
#include <iostream>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <fstream>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <thread>
#include <atomic>
#include <filesystem>
#include <chrono>
#include <ctime>
#include <memory>
#include <string>

#ifndef _WIN32
#include <unistd.h>
#include <sys/stat.h>
#endif
#include <ThreadNaming.h>

std::string translatePs2Path(const char *ps2Path);

#include "Helpers/Path.h"
#include "Helpers/State.h"
#include "Helpers/Loader.h"
#include "Helpers/Runtime.h"

inline bool resolveEeGuestRange(uint32_t address, size_t size, uint32_t &offset, bool &scratch)
{
    scratch = ps2IsScratchpadAddress(address);
    if (scratch)
    {
        offset = ps2ScratchpadOffset(address);
        return size <= PS2_SCRATCHPAD_SIZE && offset <= PS2_SCRATCHPAD_SIZE - size;
    }

    if (address < PS2_RAM_SIZE)
    {
        offset = address;
    }
    else if ((address >= 0x20000000u && address < 0x40000000u) ||
             (address >= 0x80000000u && address < 0xC0000000u))
    {
        offset = address & 0x1FFFFFFFu;
    }
    else
    {
        return false;
    }
    return size <= PS2_RAM_SIZE && offset <= PS2_RAM_SIZE - size;
}

template <typename T>
inline const T *getEeGuestStruct(const uint8_t *rdram, uint32_t address)
{
    uint32_t offset = 0;
    bool scratch = false;
    if (!rdram || (address & (alignof(T) - 1u)) != 0u ||
        !resolveEeGuestRange(address, sizeof(T), offset, scratch))
    {
        return nullptr;
    }
    if (scratch)
    {
        const uint8_t *base = ps2GetScratchpadHostPtr();
        return base ? reinterpret_cast<const T *>(base + offset) : nullptr;
    }
    return reinterpret_cast<const T *>(rdram + offset);
}

template <typename T>
inline T *getEeGuestStruct(uint8_t *rdram, uint32_t address)
{
    return const_cast<T *>(getEeGuestStruct<T>(static_cast<const uint8_t *>(rdram), address));
}
