#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ps2x::iop::lle
{
    // One stub from an IRX import table: `jr $ra; addiu $zero, $zero, ordinal`.
    struct IrxImport
    {
        std::string library;
        uint16_t ordinal = 0;
        uint32_t stubAddress = 0; // after relocation
    };

    struct IrxImage
    {
        std::string name;
        uint16_t version = 0;
        uint32_t entry = 0;     // relative to the load address
        uint32_t gp = 0;        // relative to the load address
        uint32_t textSize = 0;
        uint32_t dataSize = 0;
        uint32_t bssSize = 0;
        std::vector<uint8_t> image; // text and data, not yet relocated
        struct Relocation
        {
            uint32_t offset;
            uint8_t type;
        };
        std::vector<Relocation> relocations;
    };

    // Parse an IRX (an ELF of type 0xFF80 with an .iopmod section).
    bool parseIrx(const std::vector<uint8_t> &file, IrxImage &out, std::string &error);

    // Copy the image to memory at base, apply relocations and zero BSS.
    // Returns the import stubs found in the relocated text.
    bool relocateIrx(const IrxImage &irx, uint8_t *ram, uint32_t ramSize, uint32_t base,
                     std::vector<IrxImport> &imports, std::string &error);
}
