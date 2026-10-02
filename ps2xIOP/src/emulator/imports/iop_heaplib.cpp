#include "iop_heaplib.h"

#include "../core/iop_cpu.h"
#include "../core/iop_memory.h"

namespace ps2x::iop::detail
{
    IopHeaplib::IopHeaplib(IopMemory &memory) noexcept
        : m_memory(memory)
    {
    }

    bool IopHeaplib::dispatchImport(uint16_t ordinal, IopCpuState &cpu)
    {
        const uint32_t a0 = cpu.gpr[4];
        const uint32_t a1 = cpu.gpr[5];
        const auto setV0 = [&](uint32_t value)
        {
            cpu.gpr[2] = value;
        };

        switch (ordinal)
        {
        case 4: // CreateHeap
            setV0(m_memory.allocate(16u, 16u));
            return true;
        case 5: // DeleteHeap
            if (a0 != 0u)
                (void)m_memory.freeAllocation(a0);
            setV0(0u);
            return true;
        case 6:
            setV0(m_memory.allocate(a1, 16u));
            return true;
        case 7:
            setV0(m_memory.freeAllocation(a1) ? 0u : 0xFFFFFFFFu);
            return true;
        case 8:
            setV0(m_memory.maxFreeMemory());
            return true;
        case 11:
            setV0(0u);
            return true;
        case 15:
            if (const auto block = m_memory.allocationContaining(a0))
                setV0(block->size);
            else
                setV0(0xFFFFFFFFu);
            return true;
        default:
            return false;
        }
    }
}
