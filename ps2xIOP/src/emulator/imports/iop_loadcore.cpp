#include "iop_loadcore.h"

#include "../core/iop_cpu.h"
#include "iop_imports.h"
#include "../core/iop_memory.h"

namespace ps2x::iop::detail
{
    IopLoadcore::IopLoadcore(IopMemory &memory, IopImportRegistry &imports) noexcept
        : m_memory(memory), m_imports(imports)
    {
    }

    bool IopLoadcore::dispatchImport(uint16_t ordinal, IopCpuState &cpu)
    {
        const uint32_t a0 = cpu.gpr[4];
        const auto setV0 = [&](uint32_t value)
        {
            cpu.gpr[2] = value;
        };

        switch (ordinal)
        {
        case 3:
        case 4:
        case 5:
        case 8:
        case 9:
        case 12:
        case 13:
        case 14:
        case 15:
        case 16:
        case 17:
        case 20:
        case 21:
            setV0(0u);
            return true;
        case 6:
        case 10:
            setV0(m_imports.registerExportTable(a0) ? 0u : 0xFFFFFFFFu);
            return true;
        case 7:
            setV0(m_imports.releaseExportTable(a0) ? 0u : 0xFFFFFFFFu);
            return true;
        case 11: // QueryLibraryEntryTable returns the function array, not the export header.
        {
            const uint32_t address = IopMemory::physicalAddress(a0);
            if (a0 == 0u || address > IopMemory::RamSize - 20u)
            {
                setV0(0u);
                return true;
            }
            const uint32_t table = m_imports.findTable(m_memory.readString(address + 12u, 8u), m_memory.read16(address + 8u));
            setV0(table != 0u ? table + 20u : 0u);
            return true;
        }
        case 27: // SetRebootTimeLibraryHandlingMode
            setV0(static_cast<uint32_t>(m_imports.setRebootTimeLibraryHandlingMode(a0, cpu.gpr[5])));
            return true;
        default:
            return false;
        }
    }
}
