#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace ps2x::iop::detail
{
    class IopMemory;

    // Expands a guest printf-style format. IOP code is 32-bit integer only, so every argument is
    // one word fetched in order through `nextArg` (registers first, then the caller's stack).
    [[nodiscard]] std::string formatIopString(const IopMemory &memory,
                                              const std::string &format,
                                              const std::function<uint32_t()> &nextArg);
}
