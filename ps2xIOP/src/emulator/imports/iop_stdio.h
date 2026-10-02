#pragma once

#include <cstdint>

namespace ps2x::iop
{
    class IopHost;
}

namespace ps2x::iop::detail
{
    struct IopCpuState;
    class IopMemory;

    class IopStdio
    {
    public:
        IopStdio(IopHost &host, IopMemory &memory) noexcept;

        [[nodiscard]] bool dispatchImport(uint16_t ordinal, IopCpuState &cpu);

    private:
        IopHost &m_host;
        IopMemory &m_memory;
    };
}
