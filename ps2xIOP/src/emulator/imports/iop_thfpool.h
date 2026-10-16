#pragma once

#include "../core/iop_cpu.h"
#include "../core/iop_memory.h"

#include <cstdint>
#include <map>
#include <vector>

namespace ps2x::iop::detail
{
    // thfpool (IOP kernel fixed-size memory pools), imported only by
    // first-party streaming modules (XAVSTRM.IRX uses ordinals 4, 5, 6, 9).
    // Pools are backed by IopMemory::allocate; AllocateFpl returns the next
    // free block, or 0 while the pool is exhausted (a blocking wait needs an
    // IOP scheduler wait state, which this emulator does not model yet).
    class IopThfpool
    {
    public:
        IopThfpool(IopMemory &memory) noexcept;

        void reset();

        // Returns false when the ordinal is not a thfpool import.
        bool dispatchImport(uint16_t ordinal, IopCpuState &cpu);

    private:
        struct Pool
        {
            uint32_t base = 0;
            uint32_t stride = 0;
            uint32_t blockCount = 0;
            std::vector<bool> used;
        };

        IopMemory &m_memory;
        std::map<int32_t, Pool> m_pools;
        int32_t m_nextId = 1;
    };
}
