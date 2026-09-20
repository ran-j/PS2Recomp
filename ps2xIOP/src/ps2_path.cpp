#include "ps2x/iop/ps2_path.h"

#include <algorithm>
#include <cctype>

namespace ps2x::iop
{
    namespace
    {
        std::string lowerAscii(std::string_view value)
        {
            std::string result(value);
            std::transform(result.begin(), result.end(), result.begin(), [](unsigned char ch)
                           { return static_cast<char>(std::tolower(ch)); });
            return result;
        }

        void normalizeSuffix(std::string &suffix)
        {
            std::replace(suffix.begin(), suffix.end(), '\\', '/');
            while (!suffix.empty() && suffix.front() == '/')
                suffix.erase(suffix.begin());

            const size_t semicolon = suffix.rfind(';');
            if (semicolon == std::string::npos || semicolon + 1u == suffix.size())
                return;

            const bool numeric = std::all_of(suffix.begin() + static_cast<std::ptrdiff_t>(semicolon + 1u),
                                             suffix.end(),
                                             [](unsigned char ch)
                                             { return std::isdigit(ch) != 0; });
            if (numeric)
                suffix.erase(semicolon);
        }
    }

    ParsedPs2Path parsePs2Path(std::string_view value)
    {
        ParsedPs2Path result;
        if (value.empty())
            return result;

        const std::string lower = lowerAscii(value);
        size_t prefixLength = 0u;
        if (lower.rfind("host0:", 0u) == 0u)
        {
            result.device = Ps2PathDevice::Host;
            result.deviceName = "host0";
            prefixLength = 6u;
        }
        else if (lower.rfind("host:", 0u) == 0u)
        {
            result.device = Ps2PathDevice::Host;
            result.deviceName = "host";
            prefixLength = 5u;
        }
        else if (lower.rfind("cdrom0:", 0u) == 0u)
        {
            result.device = Ps2PathDevice::Cdrom;
            result.deviceName = "cdrom0";
            prefixLength = 7u;
        }
        else if (lower.rfind("cdrom:", 0u) == 0u)
        {
            result.device = Ps2PathDevice::Cdrom;
            result.deviceName = "cdrom";
            prefixLength = 6u;
        }
        else if (lower.rfind("mc0:", 0u) == 0u)
        {
            result.device = Ps2PathDevice::MemoryCard0;
            result.deviceName = "mc0";
            prefixLength = 4u;
        }
        else if (lower.rfind("rom0:", 0u) == 0u)
        {
            result.device = Ps2PathDevice::Rom0;
            result.deviceName = "rom0";
            prefixLength = 5u;
        }
        else if (value.size() > 2u && std::isalpha(static_cast<unsigned char>(value[0])) &&
                 value[1] == ':' && (value[2] == '/' || value[2] == '\\'))
        {
            result.device = Ps2PathDevice::NativeHost;
            result.deviceName = "native";
        }
        else if (value.find(':') != std::string_view::npos)
        {
            // TODO maybe log an error here, but don't fail the parse. This is a non-standard device name.
            return result;
        }
        else
        {
            result.device = Ps2PathDevice::Cdrom;
            result.deviceName = "cdrom0";
        }

        result.path.assign(value.substr(prefixLength));
        if (result.device != Ps2PathDevice::NativeHost)
            normalizeSuffix(result.path);
        return result;
    }

    std::string ps2PathLeafKey(const ParsedPs2Path &parsed)
    {
        if (!parsed)
            return {};
        std::string path = parsed.path;
        std::replace(path.begin(), path.end(), '\\', '/');
        const size_t slash = path.find_last_of('/');
        if (slash != std::string::npos)
            path.erase(0u, slash + 1u);
        path = lowerAscii(path);
        if (path.size() > 4u && path.ends_with(".irx"))
            path.resize(path.size() - 4u);
        return path;
    }

    std::string ps2PathLeafKey(std::string_view path)
    {
        return ps2PathLeafKey(parsePs2Path(path));
    }
}
