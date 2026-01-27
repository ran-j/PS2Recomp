#pragma once

#include <string_view>
#include "ps2_call_list.h"

namespace ps2_runtime_calls
{
    inline constexpr std::string_view kSyscallNames[] = {
#define PS2_SYSCALL_NAME(name) std::string_view{#name},
        PS2_SYSCALL_LIST(PS2_SYSCALL_NAME)
#undef PS2_SYSCALL_NAME
    };

    inline constexpr std::string_view kStubNames[] = {
#define PS2_STUB_NAME(name) std::string_view{#name},
        PS2_STUB_LIST(PS2_STUB_NAME)
#undef PS2_STUB_NAME
    };

    inline bool isSyscallName(std::string_view name)
    {
        for (auto entry : kSyscallNames)
        {
            if (entry == name)
            {
                return true;
            }
        }
        return false;
    }

    inline bool isStubName(std::string_view name)
    {
        for (auto entry : kStubNames)
        {
            if (entry == name)
            {
                return true;
            }
        }
        return false;
    }
}
