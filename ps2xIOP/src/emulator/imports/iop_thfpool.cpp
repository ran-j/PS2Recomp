#include "iop_thfpool.h"

#include "../core/iop_cpu.h"
#include "../core/iop_memory.h"

namespace ps2x::iop::detail
{
    IopThfpool::IopThfpool(IopMemory &memory) noexcept
        : m_memory(memory)
    {
    }

    void IopThfpool::reset()
    {
        m_pools.clear();
        m_nextId = 1;
    }

    bool IopThfpool::dispatchImport(uint16_t ordinal, IopCpuState &cpu)
    {
        const uint32_t a0 = cpu.gpr[4];
        const uint32_t a1 = cpu.gpr[5];
        const auto setV0 = [&](uint32_t value)
        {
            cpu.gpr[2] = value;
        };

        switch (ordinal)
        {
        case 4: // CreateFpool(FpoolParam *param) -> fpid
        {
            // FpoolParam layout (PS2SDK thfpool): attr, option, blockSize, blockCount.
            if (a0 == 0u)
            {
                setV0(0xFFFFFFFFu);
                return true;
            }
            const uint32_t blockSize = m_memory.read32(a0 + 8u);
            const uint32_t blockCount = m_memory.read32(a0 + 12u);
            if (blockSize == 0u || blockCount == 0u || blockCount > 4096u)
            {
                setV0(0xFFFFFFFFu);
                return true;
            }
            const uint32_t stride = (blockSize + 15u) & ~15u;
            const uint64_t total = static_cast<uint64_t>(stride) * blockCount;
            if (total > IopMemory::RamSize)
            {
                setV0(0xFFFFFFFFu);
                return true;
            }
            const uint32_t base = m_memory.allocate(static_cast<uint32_t>(total), 16u);
            if (base == 0u)
            {
                setV0(0xFFFFFFFFu);
                return true;
            }
            Pool pool;
            pool.base = base;
            pool.stride = stride;
            pool.blockCount = blockCount;
            pool.used.assign(blockCount, false);
            const int32_t id = m_nextId++;
            if (m_nextId <= 0)
                m_nextId = 1;
            m_pools[id] = std::move(pool);
            setV0(static_cast<uint32_t>(id));
            return true;
        }
        case 5: // DeleteFpool(fpid) -> 0 / error
        {
            const auto it = m_pools.find(static_cast<int32_t>(a0));
            if (it == m_pools.end())
            {
                setV0(0xFFFFFFFFu);
                return true;
            }
            (void)m_memory.freeAllocation(it->second.base);
            m_pools.erase(it);
            setV0(0u);
            return true;
        }
        case 6: // AllocateFpl(fpid) -> block pointer (0 while the pool is exhausted)
        {
            const auto it = m_pools.find(static_cast<int32_t>(a0));
            if (it == m_pools.end())
            {
                setV0(0u);
                return true;
            }
            Pool &pool = it->second;
            for (uint32_t i = 0; i < pool.blockCount; ++i)
            {
                if (!pool.used[i])
                {
                    pool.used[i] = true;
                    setV0(pool.base + i * pool.stride);
                    return true;
                }
            }
            // Hardware blocks the caller until a block is freed; without a
            // blocking wait state the contract is a null block, the same
            // value a pool with no waiter handling would hand out.
            setV0(0u);
            return true;
        }
        case 9: // FreeFpool(fpid, ptr) -> 0 / error
        {
            const auto it = m_pools.find(static_cast<int32_t>(a0));
            if (it == m_pools.end() || a1 < it->second.base)
            {
                setV0(0xFFFFFFFFu);
                return true;
            }
            Pool &pool = it->second;
            const uint32_t offset = a1 - pool.base;
            if (offset % pool.stride != 0u || offset / pool.stride >= pool.blockCount)
            {
                setV0(0xFFFFFFFFu);
                return true;
            }
            pool.used[offset / pool.stride] = false;
            setV0(0u);
            return true;
        }
        default:
            return false;
        }
    }
}
