#pragma once

#include <string>
#include <string_view>

namespace ps2x::iop
{
    enum class Ps2PathDevice
    {
        Invalid,
        Host,
        Cdrom,
        MemoryCard0,
        Rom0,
        NativeHost,
    };

    struct ParsedPs2Path
    {
        Ps2PathDevice device = Ps2PathDevice::Invalid;
        std::string deviceName;
        std::string path;

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return device != Ps2PathDevice::Invalid;
        }
    };

    [[nodiscard]] ParsedPs2Path parsePs2Path(std::string_view path);

    // Returns a lower-case module/file leaf without an optional .irx suffix.
    [[nodiscard]] std::string ps2PathLeafKey(const ParsedPs2Path &path);
    [[nodiscard]] std::string ps2PathLeafKey(std::string_view path);
}
