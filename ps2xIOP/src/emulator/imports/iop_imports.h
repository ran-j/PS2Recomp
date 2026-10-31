#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ps2x::iop::detail
{
    class IopMemory;

    struct IopImportCall
    {
        std::string library;
        uint16_t ordinal = 0;
        uint16_t version = 0;
    };

    class IopImportRegistry
    {
    public:
        explicit IopImportRegistry(IopMemory &memory) noexcept;

        void reset();
        [[nodiscard]] std::optional<IopImportCall> decode(uint32_t pc) const;
        [[nodiscard]] bool registerExportTable(uint32_t address);
        [[nodiscard]] bool releaseExportTable(uint32_t address);
        [[nodiscard]] uint32_t findTable(std::string_view library, std::optional<uint16_t> version = std::nullopt) const;
        [[nodiscard]] uint32_t resolve(std::string_view library, uint16_t ordinal, std::optional<uint16_t> version = std::nullopt) const;
        [[nodiscard]] int32_t setRebootTimeLibraryHandlingMode(uint32_t address, uint32_t mode);
        void eraseRange(uint32_t base, uint32_t size);
        void forgetDecodedStubs() noexcept;

    private:
        struct ExportLibrary
        {
            uint32_t tableAddress = 0;
            uint16_t version = 0;
            std::string name;
            std::vector<uint32_t> functions;
        };

        struct DecodedStub
        {
            uint32_t table = 0;
            uint64_t name = 0;
            IopImportCall call;
        };

        [[nodiscard]] const ExportLibrary *findLibrary(std::string_view name, std::optional<uint16_t> version) const;
        [[nodiscard]] std::optional<IopImportCall> scanImportTable(uint32_t physicalPc, uint32_t delay) const;
        [[nodiscard]] uint64_t readLibraryName(uint32_t table) const;

        IopMemory &m_memory;
        std::map<uint32_t, ExportLibrary> m_libraries;
        // Import stubs are hit on every HLE call; re-scanning backwards for the table header each time dominated the IOP.
        mutable std::unordered_map<uint32_t, DecodedStub> m_decodedStubs;
    };
}
