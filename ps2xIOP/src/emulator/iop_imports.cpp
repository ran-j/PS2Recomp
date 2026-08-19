#include "iop_imports.h"

#include "iop_memory.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace ps2x::iop::detail
{
    namespace
    {
        constexpr uint32_t kImportMagic = 0x41E00000u;
        constexpr uint32_t kExportMagic = 0x41C00000u;

        bool equalsIgnoreCase(std::string_view lhs, std::string_view rhs)
        {
            if (lhs.size() != rhs.size())
                return false;
            for (size_t i = 0; i < lhs.size(); ++i)
            {
                if (std::tolower(static_cast<unsigned char>(lhs[i])) !=
                    std::tolower(static_cast<unsigned char>(rhs[i])))
                    return false;
            }
            return true;
        }

        std::string trimLibraryName(const char *name)
        {
            size_t length = 0u;
            while (length < 8u && name[length] != '\0')
                ++length;
            return std::string(name, length);
        }
    }

    IopImportRegistry::IopImportRegistry(IopMemory &memory) noexcept
        : m_memory(memory)
    {
    }

    void IopImportRegistry::reset()
    {
        m_libraries.clear();
    }

    std::optional<IopImportCall> IopImportRegistry::decode(uint32_t pc) const
    {
        if (m_memory.read32(pc) != 0x03E00008u)
            return std::nullopt;
        const uint32_t delay = m_memory.read32(pc + 4u);
        if ((delay & 0xFFFF0000u) != 0x24000000u)
            return std::nullopt;

        const uint32_t physicalPc = IopMemory::physicalAddress(pc);
        const uint32_t searchBegin = physicalPc > 0x10000u ? physicalPc - 0x10000u : 0u;
        for (uint32_t candidate = physicalPc & ~3u; candidate >= searchBegin + 20u; candidate -= 4u)
        {
            const uint32_t table = candidate - 20u;
            if (m_memory.read32(table) != kImportMagic)
            {
                if (candidate == searchBegin + 20u)
                    break;
                continue;
            }

            char name[9]{};
            for (uint32_t i = 0; i < 8u; ++i)
                name[i] = static_cast<char>(m_memory.read8(table + 12u + i));
            const uint32_t stubs = table + 20u;
            if (physicalPc < stubs || ((physicalPc - stubs) & 7u) != 0u)
                continue;

            bool valid = false;
            for (uint32_t stub = stubs;
                 stub + 7u < IopMemory::RamSize && stub <= physicalPc;
                 stub += 8u)
            {
                const uint32_t first = m_memory.read32(stub);
                const uint32_t second = m_memory.read32(stub + 4u);
                if (first == 0u && second == 0u)
                    break;
                if (stub == physicalPc)
                {
                    valid = true;
                    break;
                }
            }
            if (valid)
            {
                return IopImportCall{
                    trimLibraryName(name),
                    static_cast<uint16_t>(delay & 0xFFFFu),
                };
            }
        }
        return std::nullopt;
    }

    bool IopImportRegistry::registerExportTable(uint32_t address)
    {
        const uint32_t physical = IopMemory::physicalAddress(address);
        if (physical + 20u > IopMemory::RamSize ||
            m_memory.read32(physical) != kExportMagic)
            return false;

        char name[9]{};
        for (uint32_t i = 0; i < 8u; ++i)
            name[i] = static_cast<char>(m_memory.read8(physical + 12u + i));

        ExportLibrary library;
        library.tableAddress = physical;
        library.version = m_memory.read16(physical + 8u);
        library.name = trimLibraryName(name);
        for (uint32_t cursor = physical + 20u; cursor + 3u < IopMemory::RamSize; cursor += 4u)
        {
            const uint32_t function = m_memory.read32(cursor);
            if (function == 0u)
                break;
            library.functions.push_back(function);
            if (library.functions.size() > 1024u)
                return false;
        }
        m_libraries[physical] = std::move(library);
        return true;
    }

    bool IopImportRegistry::releaseExportTable(uint32_t address)
    {
        return m_libraries.erase(IopMemory::physicalAddress(address)) != 0u;
    }

    uint32_t IopImportRegistry::findTable(std::string_view library) const
    {
        const auto found = std::find_if(m_libraries.begin(), m_libraries.end(), [&](const auto &entry)
                                        { return equalsIgnoreCase(entry.second.name, library); });
        return found != m_libraries.end() ? found->second.tableAddress : 0u;
    }

    uint32_t IopImportRegistry::resolve(std::string_view library, uint16_t ordinal) const
    {
        const auto found = std::find_if(m_libraries.begin(), m_libraries.end(), [&](const auto &entry)
                                        { return equalsIgnoreCase(entry.second.name, library); });
        if (found == m_libraries.end() || ordinal >= found->second.functions.size())
            return 0u;
        return found->second.functions[ordinal];
    }

    void IopImportRegistry::eraseRange(uint32_t base, uint32_t size)
    {
        for (auto library = m_libraries.begin(); library != m_libraries.end();)
        {
            if (library->first >= base && library->first < base + size)
                library = m_libraries.erase(library);
            else
                ++library;
        }
    }
}
