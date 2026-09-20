#pragma once

#include "ps2x/iop/iop_host.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <span>

namespace ps2x::iop::detail
{
    [[nodiscard]] inline bool writeRpcWords(IopHost &host, GuestBuffer receive, std::span<const uint32_t> words)
    {
        const size_t count = std::min<size_t>(receive.size / sizeof(uint32_t), words.size());
        const size_t bytes = count * sizeof(uint32_t);
        if (receive.address == 0u || bytes == 0u)
            return false;
        if (bytes - 1u > std::numeric_limits<uint32_t>::max() - receive.address)
            return false;
        return host.writeGuest(receive.address, words.data(), bytes);
    }
}
