#include "iop_dmacman.h"

#include "../core/iop_cpu.h"
#include "../core/iop_memory.h"

namespace ps2x::iop::detail
{
    namespace
    {
        constexpr uint32_t kMadr = 0x0u;
        constexpr uint32_t kBcr = 0x4u;
        constexpr uint32_t kChcr = 0x8u;
        constexpr uint32_t kTadr = 0xCu;

        constexpr uint32_t kDpcr = 0x1F8010F0u;
        constexpr uint32_t kDicr = 0x1F8010F4u;
        constexpr uint32_t kDpcr2 = 0x1F801570u;
        constexpr uint32_t kDicr2 = 0x1F801574u;
        constexpr uint32_t kDpcr3 = 0x1F8015F0u;
        constexpr uint32_t kReg157C = 0x1F80157Cu;
        constexpr uint32_t kReg1578 = 0x1F801578u;

        constexpr uint32_t kChannelCount = 13u;
        constexpr uint32_t kChannelOtc = 6u;
        constexpr uint32_t kChcrStart = 0x01000000u;

        // Each channel owns a nibble of DPCR (channels 0-6) or DPCR2 (7-12): priority in bits 0-2, enable in bit 3.
        struct ControlSlot
        {
            uint32_t address = 0u;
            uint32_t shift = 0u;
        };

        std::optional<ControlSlot> controlSlot(uint32_t channel) noexcept
        {
            if (channel < 7u)
                return ControlSlot{kDpcr, channel * 4u};
            if (channel < kChannelCount)
                return ControlSlot{kDpcr2, (channel - 7u) * 4u};
            return std::nullopt;
        }
    }

    IopDmacman::IopDmacman(IopMemory &memory) noexcept
        : m_memory(memory)
    {
    }

    std::optional<uint32_t> IopDmacman::channelBase(uint32_t channel) noexcept
    {
        if (channel < 7u)
            return 0x1F801080u + channel * 0x10u;
        if (channel < kChannelCount)
            return 0x1F801500u + (channel - 7u) * 0x10u;
        return std::nullopt;
    }

    uint32_t IopDmacman::readChannel(uint32_t channel, uint32_t offset) const
    {
        const auto base = channelBase(channel);
        return base ? m_memory.read32(*base + offset) : 0u;
    }

    void IopDmacman::writeChannel(uint32_t channel, uint32_t offset, uint32_t value)
    {
        if (const auto base = channelBase(channel))
            m_memory.write32(*base + offset, value);
    }

    bool IopDmacman::dispatchImport(uint16_t ordinal, IopCpuState &cpu)
    {
        const uint32_t a0 = cpu.gpr[4];
        const uint32_t a1 = cpu.gpr[5];
        const auto setV0 = [&](uint32_t value)
        {
            cpu.gpr[2] = value;
        };

        switch (ordinal)
        {
        case 4: // dmac_ch_set_madr
        case 6: // dmac_ch_set_bcr
        case 8: // dmac_ch_set_chcr
        case 10: // dmac_ch_set_tadr
            writeChannel(a0, (ordinal - 4u) * 2u, a1);
            setV0(0u);
            return true;
        case 5: // dmac_ch_get_madr
        case 7: // dmac_ch_get_bcr
        case 9: // dmac_ch_get_chcr
        case 11: // dmac_ch_get_tadr
            setV0(readChannel(a0, (ordinal - 5u) * 2u));
            return true;
        case 14: // dmac_set_dpcr
        case 15:
        case 16: // dmac_set_dpcr2
        case 17:
        case 18: // dmac_set_dpcr3
        case 19:
        case 20: // dmac_set_dicr
        case 21:
        case 22: // dmac_set_dicr2
        case 23:
        case 24: // dmac_set_BF80157C
        case 25:
        case 26: // dmac_set_BF801578
        case 27:
        {
            static constexpr uint32_t registers[] = {kDpcr, kDpcr2, kDpcr3, kDicr, kDicr2, kReg157C, kReg1578};
            const uint32_t address = registers[(ordinal - 14u) / 2u];
            if ((ordinal & 1u) == 0u)
            {
                m_memory.write32(address, a0);
                setV0(0u);
            }
            else
            {
                setV0(m_memory.read32(address));
            }
            return true;
        }
        case 28: // sceSetSliceDMA(channel, address, blockWords, blockCount, direction)
        {
            if (a0 >= kChannelCount || a0 == kChannelOtc)
            {
                setV0(0u);
                return true;
            }
            const uint32_t direction = m_memory.read32(cpu.gpr[29] + 16u);
            writeChannel(a0, kMadr, a1 & 0xFFFFFFu);
            writeChannel(a0, kBcr, (cpu.gpr[6] & 0xFFFFu) | (cpu.gpr[7] << 16u));
            writeChannel(a0, kChcr, (direction & 1u) | 0x200u | (direction == 0u ? 0x40000000u : 0u));
            setV0(1u);
            return true;
        }
        case 32: // sceStartDMA
            writeChannel(a0, kChcr, readChannel(a0, kChcr) | kChcrStart);
            setV0(0u);
            return true;
        case 33: // sceSetDMAPriority(channel, priority)
        case 34: // sceEnableDMAChannel
        case 35: // sceDisableDMAChannel
        {
            if (const auto slot = controlSlot(a0))
            {
                uint32_t value = m_memory.read32(slot->address);
                if (ordinal == 33u)
                    value = (value & ~(7u << slot->shift)) | ((a1 & 7u) << slot->shift);
                else if (ordinal == 34u)
                    value |= 8u << slot->shift;
                else
                    value &= ~(8u << slot->shift);
                m_memory.write32(slot->address, value);
            }
            setV0(0u);
            return true;
        }
        default:
            // SIF channel setup (29-31) is owned by the runtime's SIF transport, not these registers.
            setV0(0u);
            return true;
        }
    }
}
