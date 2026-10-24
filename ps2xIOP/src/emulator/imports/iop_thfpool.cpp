#include "iop_thfpool.h"

#include "../core/iop_cpu.h"
#include "../core/iop_kernel.h"
#include "../core/iop_memory.h"

namespace ps2x::iop::detail
{
    namespace
    {
        constexpr uint32_t kFaThPri = 0x001u; // waiters are served by priority, not FIFO
        constexpr uint32_t kKeWaitDelete = static_cast<uint32_t>(-425);
    }

    IopThfpool::IopThfpool(IopMemory &memory, IopKernel &kernel) noexcept
        : m_memory(memory), m_kernel(kernel)
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
            pool.attr = m_memory.read32(a0 + 0u);
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
        case 5: // DeleteFpool(fpid) -> 0 / error; waiting threads get KE_WAIT_DELETE
        {
            const auto it = m_pools.find(static_cast<int32_t>(a0));
            if (it == m_pools.end())
            {
                setV0(0xFFFFFFFFu);
                return true;
            }
            while (IopThread *waiter = m_kernel.fixedPoolWaiter(it->first, false))
                m_kernel.releaseWaiter(*waiter, kKeWaitDelete);
            (void)m_memory.freeAllocation(it->second.base);
            m_pools.erase(it);
            setV0(0u);
            return true;
        }
        case 6: // AllocateFpl(fpid) -> block pointer; waits while the pool is empty
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
            // The calling thread waits for FreeFpl to hand it a block (its
            // $v0). Interrupt handlers and host calls cannot wait: null block.
            if (!m_kernel.waitCurrentOnFixedPool(it->first, cpu))
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
            const uint32_t index = offset / pool.stride;
            if (offset % pool.stride != 0u || index >= pool.blockCount)
            {
                setV0(0xFFFFFFFFu);
                return true;
            }
            // A waiting thread takes the block over; it stays allocated.
            if (IopThread *waiter = m_kernel.fixedPoolWaiter(it->first, (pool.attr & kFaThPri) != 0u))
                m_kernel.releaseWaiter(*waiter, a1);
            else
                pool.used[index] = false;
            setV0(0u);
            return true;
        }
        default:
            return false;
        }
    }
}
