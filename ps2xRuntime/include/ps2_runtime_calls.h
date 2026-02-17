#pragma once

#include <algorithm>
#include <cstddef>
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

    namespace detail
    {
        template <std::size_t N>
        inline std::string_view findExact(
            std::string_view name,
            const std::string_view (&entries)[N])
        {
            const auto it = std::ranges::find(entries, name);
            return (it == std::end(entries)) ? std::string_view{} : *it;
        }

        template <std::size_t N>
        inline std::string_view resolveNameWithOptionalLeadingUnderscoreAlias(
            std::string_view name,
            const std::string_view (&entries)[N])
        {
            if (name.empty())
            {
                return {};
            }

            if (const std::string_view exact = findExact(name, entries); !exact.empty())
            {
                return exact;
            }

            if (name.starts_with('_'))
            {
                return findExact(name.substr(1), entries);
            }

            for (auto entry : entries)
            {
                if (entry.size() == name.size() + 1 &&
                    entry.starts_with('_') &&
                    entry.substr(1) == name)
                {
                    return entry;
                }
            }

            return {};
        }
    }

    inline std::string_view resolveSyscallName(std::string_view name)
    {
        return detail::resolveNameWithOptionalLeadingUnderscoreAlias(name, kSyscallNames);
    }

    inline std::string_view resolveStubName(std::string_view name)
    {
        return detail::resolveNameWithOptionalLeadingUnderscoreAlias(name, kStubNames);
    }

    inline bool isSyscallName(std::string_view name)
    {
        return !resolveSyscallName(name).empty();
    }

    inline bool isStubName(std::string_view name)
    {
        return !resolveStubName(name).empty();
    }
}
