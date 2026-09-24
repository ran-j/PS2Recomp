#include "irx.h"

#include <cstring>

namespace ps2x::iop::lle
{
    namespace
    {
        constexpr uint16_t kIrxType = 0xFF80u;
        constexpr uint32_t kIopModSegment = 0x70000080u;
        constexpr uint32_t kLoadSegment = 1u;
        constexpr uint32_t kRelocationSection = 9u;
        constexpr uint32_t kImportMagic = 0x41E00000u;

        template <class T>
        bool readAt(const std::vector<uint8_t> &data, size_t offset, T &value)
        {
            if (offset > data.size() || data.size() - offset < sizeof(T))
                return false;
            std::memcpy(&value, data.data() + offset, sizeof(T));
            return true;
        }

        uint32_t load32(const uint8_t *ram, uint32_t offset)
        {
            uint32_t value;
            std::memcpy(&value, ram + offset, sizeof(value));
            return value;
        }

        void store32(uint8_t *ram, uint32_t offset, uint32_t value)
        {
            std::memcpy(ram + offset, &value, sizeof(value));
        }
    }

    bool parseIrx(const std::vector<uint8_t> &file, IrxImage &out, std::string &error)
    {
        uint16_t type = 0, phentsize = 0, phnum = 0, shentsize = 0, shnum = 0;
        uint32_t phoff = 0, shoff = 0;
        if (file.size() < 0x34u || std::memcmp(file.data(), "\x7F" "ELF", 4) != 0 ||
            !readAt(file, 0x10, type) || type != kIrxType || !readAt(file, 0x1C, phoff) ||
            !readAt(file, 0x20, shoff) || !readAt(file, 0x2A, phentsize) || !readAt(file, 0x2C, phnum) ||
            !readAt(file, 0x2E, shentsize) || !readAt(file, 0x30, shnum))
        {
            error = "not an IRX module";
            return false;
        }

        uint32_t loadOffset = 0, loadFileSize = 0, loadMemorySize = 0;
        bool haveModule = false, haveLoad = false;
        for (uint32_t index = 0; index < phnum; ++index)
        {
            const size_t header = phoff + static_cast<size_t>(index) * phentsize;
            uint32_t segmentType = 0, offset = 0, fileSize = 0, memorySize = 0;
            if (!readAt(file, header, segmentType) || !readAt(file, header + 4, offset) ||
                !readAt(file, header + 16, fileSize) || !readAt(file, header + 20, memorySize))
                break;
            if (segmentType == kIopModSegment)
            {
                uint32_t fields[6];
                for (uint32_t field = 0; field < 6u; ++field)
                    if (!readAt(file, offset + field * 4u, fields[field]))
                        break;
                readAt(file, offset + 24u, out.version);
                out.entry = fields[1];
                out.gp = fields[2];
                out.textSize = fields[3];
                out.dataSize = fields[4];
                out.bssSize = fields[5];
                for (size_t at = offset + 26u; at < file.size() && at < offset + fileSize && file[at] != 0u; ++at)
                    out.name.push_back(static_cast<char>(file[at]));
                haveModule = true;
            }
            else if (segmentType == kLoadSegment)
            {
                loadOffset = offset;
                loadFileSize = fileSize;
                loadMemorySize = memorySize;
                haveLoad = true;
            }
        }
        if (!haveModule || !haveLoad || loadOffset > file.size() || file.size() - loadOffset < loadFileSize ||
            loadMemorySize < loadFileSize)
        {
            error = "IRX is missing its .iopmod or load segment";
            return false;
        }
        out.image.assign(file.begin() + loadOffset, file.begin() + loadOffset + loadFileSize);
        out.bssSize = loadMemorySize - loadFileSize;

        for (uint32_t index = 0; index < shnum; ++index)
        {
            const size_t header = shoff + static_cast<size_t>(index) * shentsize;
            uint32_t sectionType = 0, offset = 0, size = 0;
            if (!readAt(file, header + 4, sectionType) || !readAt(file, header + 16, offset) ||
                !readAt(file, header + 20, size))
                break;
            if (sectionType != kRelocationSection)
                continue;
            for (uint32_t entry = 0; entry + 8u <= size; entry += 8u)
            {
                uint32_t target = 0, info = 0;
                if (!readAt(file, offset + entry, target) || !readAt(file, offset + entry + 4u, info))
                {
                    error = "truncated relocation table";
                    return false;
                }
                out.relocations.push_back({target, static_cast<uint8_t>(info & 0xFFu)});
            }
        }
        return true;
    }

    bool relocateIrx(const IrxImage &irx, uint8_t *ram, uint32_t ramSize, uint32_t base,
                     std::vector<IrxImport> &imports, std::string &error)
    {
        const uint32_t total = static_cast<uint32_t>(irx.image.size()) + irx.bssSize;
        if (base >= ramSize || ramSize - base < total)
        {
            error = "IRX does not fit in IOP memory";
            return false;
        }
        uint8_t *image = ram + base;
        std::memcpy(image, irx.image.data(), irx.image.size());
        std::memset(image + irx.image.size(), 0, irx.bssSize);

        // HI16 entries wait for the LO16 that completes their address.
        std::vector<uint32_t> pendingHigh;
        for (const auto &relocation : irx.relocations)
        {
            if (relocation.offset + 4u > irx.image.size())
            {
                error = "relocation outside the module";
                return false;
            }
            const uint32_t word = load32(image, relocation.offset);
            switch (relocation.type)
            {
            case 2: // R_MIPS_32
                store32(image, relocation.offset, word + base);
                break;
            case 4: // R_MIPS_26
            {
                const uint32_t target = ((word & 0x03FFFFFFu) << 2) + base;
                store32(image, relocation.offset, (word & 0xFC000000u) | ((target >> 2) & 0x03FFFFFFu));
                break;
            }
            case 5: // R_MIPS_HI16
                pendingHigh.push_back(relocation.offset);
                break;
            case 6: // R_MIPS_LO16
            {
                const int32_t low = static_cast<int16_t>(word & 0xFFFFu);
                for (const uint32_t highOffset : pendingHigh)
                {
                    const uint32_t high = load32(image, highOffset);
                    const uint32_t value = ((high & 0xFFFFu) << 16) + static_cast<uint32_t>(low) + base;
                    const uint32_t adjusted = ((value >> 16) + ((value & 0x8000u) ? 1u : 0u)) & 0xFFFFu;
                    store32(image, highOffset, (high & 0xFFFF0000u) | adjusted);
                }
                pendingHigh.clear();
                store32(image, relocation.offset, (word & 0xFFFF0000u) | ((static_cast<uint32_t>(low) + base) & 0xFFFFu));
                break;
            }
            case 7: // R_MIPS_GPREL16: gp moves with the module
                break;
            default:
                error = "unsupported relocation type " + std::to_string(relocation.type);
                return false;
            }
        }

        for (uint32_t offset = 0; offset + 20u <= irx.textSize && offset + 20u <= irx.image.size(); offset += 4u)
        {
            if (load32(image, offset) != kImportMagic)
                continue;
            std::string library;
            for (uint32_t index = 0; index < 8u && image[offset + 12u + index] != 0u; ++index)
                library.push_back(static_cast<char>(image[offset + 12u + index]));
            uint32_t stub = offset + 20u;
            for (; stub + 8u <= irx.textSize; stub += 8u)
            {
                const uint32_t jump = load32(image, stub), slot = load32(image, stub + 4u);
                if (jump != 0x03E00008u || (slot & 0xFFFF0000u) != 0x24000000u)
                    break;
                imports.push_back({library, static_cast<uint16_t>(slot & 0xFFFFu), base + stub});
            }
            offset = stub - 4u;
        }
        return true;
    }
}
