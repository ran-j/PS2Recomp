#include "iop_format.h"

#include "../core/iop_memory.h"

#include <algorithm>
#include <cstdio>
#include <string_view>

namespace ps2x::iop::detail
{
    std::string formatIopString(const IopMemory &memory,
                                const std::string &format,
                                const std::function<uint32_t()> &nextArg)
    {
        std::string out;
        out.reserve(format.size() + 32u);
        for (size_t i = 0; i < format.size(); ++i)
        {
            const char c = format[i];
            if (c != '%')
            {
                out.push_back(c);
                continue;
            }

            // Rebuild the conversion spec for the host snprintf, dropping length modifiers since
            // every guest argument is a 32-bit word.
            std::string spec = "%";
            size_t j = i + 1u;
            while (j < format.size() && std::string_view("-+ #0").find(format[j]) != std::string_view::npos)
                spec.push_back(format[j++]);
            const auto readCount = [&]()
            {
                if (j < format.size() && format[j] == '*')
                {
                    spec += std::to_string(static_cast<int32_t>(nextArg()));
                    ++j;
                    return;
                }
                while (j < format.size() && format[j] >= '0' && format[j] <= '9')
                    spec.push_back(format[j++]);
            };
            readCount();
            if (j < format.size() && format[j] == '.')
            {
                spec.push_back(format[j++]);
                readCount();
            }
            while (j < format.size() && std::string_view("hlLqjzt").find(format[j]) != std::string_view::npos)
                ++j;
            if (j >= format.size())
            {
                out.append(format, i, std::string::npos);
                break;
            }

            const char conv = format[j];
            char buffer[128];
            int written = 0;
            switch (conv)
            {
            case '%':
                out.push_back('%');
                break;
            case 'd':
            case 'i':
                spec.push_back('d');
                written = std::snprintf(buffer, sizeof(buffer), spec.c_str(), static_cast<int>(static_cast<int32_t>(nextArg())));
                break;
            case 'u':
            case 'x':
            case 'X':
            case 'o':
            case 'c':
                spec.push_back(conv);
                written = std::snprintf(buffer, sizeof(buffer), spec.c_str(), static_cast<unsigned>(nextArg()));
                break;
            case 'p':
                spec.push_back('x');
                written = std::snprintf(buffer, sizeof(buffer), spec.c_str(), static_cast<unsigned>(nextArg()));
                break;
            case 's':
            {
                const uint32_t address = nextArg();
                const std::string text = address ? memory.readString(address, 1024u) : std::string("(null)");
                spec.push_back('s');
                const int needed = std::snprintf(nullptr, 0, spec.c_str(), text.c_str());
                if (needed > 0)
                {
                    std::string formatted(static_cast<size_t>(needed) + 1u, '\0');
                    std::snprintf(formatted.data(), formatted.size(), spec.c_str(), text.c_str());
                    formatted.resize(static_cast<size_t>(needed));
                    out += formatted;
                }
                break;
            }
            default:
                // Unknown conversion: keep it verbatim so the log still shows what was intended.
                out.append(format, i, j - i + 1u);
                break;
            }
            if (written > 0)
                out.append(buffer, static_cast<size_t>(std::min<int>(written, static_cast<int>(sizeof(buffer)) - 1)));
            i = j;
        }
        return out;
    }
}
