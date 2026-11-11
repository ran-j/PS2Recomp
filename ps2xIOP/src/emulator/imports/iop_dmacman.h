#pragma once

#include <cstdint>
#include <optional>

namespace ps2x::iop::detail
{
    struct IopCpuState;
    class IopMemory;

    // dmacman only programs DMA controller registers. Modules such as sio2man start their
    // transfers through it, so the writes must land in IopMemory where the channels run.
    class IopDmacman
    {
    public:
        explicit IopDmacman(IopMemory &memory) noexcept;

        [[nodiscard]] bool dispatchImport(uint16_t ordinal, IopCpuState &cpu);

        // MADR address of a channel; BCR, CHCR and TADR follow at +4, +8 and +0xC.
        [[nodiscard]] static std::optional<uint32_t> channelBase(uint32_t channel) noexcept;

    private:
        [[nodiscard]] uint32_t readChannel(uint32_t channel, uint32_t offset) const;
        void writeChannel(uint32_t channel, uint32_t offset, uint32_t value);

        IopMemory &m_memory;
    };
}
