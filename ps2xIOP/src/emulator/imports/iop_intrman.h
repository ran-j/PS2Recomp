#pragma once

#include <cstdint>
#include <map>

namespace ps2x::iop::detail
{
    struct IopCpuState;
    class IopGuestExecutor;
    class IopMemory;

    class IopIntrman
    {
    public:
        explicit IopIntrman(IopMemory &memory) noexcept;

        void reset();
        [[nodiscard]] bool dispatchImport(uint16_t ordinal, IopCpuState &cpu, IopGuestExecutor &executor);
        [[nodiscard]] bool dispatchInterrupt(int irq, IopGuestExecutor &executor) const;

    private:
        struct Handler
        {
            uint32_t function = 0u;
            uint32_t argument = 0u;
            uint32_t gp = 0u;
        };

        IopMemory &m_memory;
        std::map<int, Handler> m_handlers;
        std::map<int, bool> m_enabled;
    };
}
