#include "iop_spu2.h"

#include <algorithm>
#include <cstring>

namespace ps2x::iop::detail
{
    namespace
    {
        constexpr uint32_t kRamMask = IopSpu2::kRamWords - 1u;

        // Core-relative register offsets (bytes).
        constexpr uint32_t kRegPmon = 0x180u;
        constexpr uint32_t kRegVmixL = 0x188u;
        constexpr uint32_t kRegVmixR = 0x190u;
        constexpr uint32_t kRegMmix = 0x198u;
        constexpr uint32_t kRegAttr = 0x19Au;
        constexpr uint32_t kRegIrqaHi = 0x19Cu;
        constexpr uint32_t kRegIrqaLo = 0x19Eu;
        constexpr uint32_t kRegKonLo = 0x1A0u;
        constexpr uint32_t kRegKonHi = 0x1A2u;
        constexpr uint32_t kRegKoffLo = 0x1A4u;
        constexpr uint32_t kRegKoffHi = 0x1A6u;
        constexpr uint32_t kRegTsaHi = 0x1A8u;
        constexpr uint32_t kRegTsaLo = 0x1AAu;
        constexpr uint32_t kRegStd = 0x1ACu;
        constexpr uint32_t kRegAdmas = 0x1B0u;
        constexpr uint32_t kRegVoiceAddr = 0x1C0u;
        constexpr uint32_t kRegVoiceAddrEnd = kRegVoiceAddr + IopSpu2::kVoicesPerCore * 12u;
        constexpr uint32_t kRegEndxLo = 0x340u;
        constexpr uint32_t kRegEndxHi = 0x342u;
        constexpr uint32_t kRegStatx = 0x344u;

        // Global block at 0x760: per-core volumes 0x28 bytes apart, then SPDIF/IRQ info.
        constexpr uint32_t kGlobalBase = 0x760u;
        constexpr uint32_t kCoreVolumeStride = 0x28u;
        constexpr uint32_t kRegMvolL = 0x00u;
        constexpr uint32_t kRegMvolR = 0x02u;
        constexpr uint32_t kRegAvolL = 0x08u;
        constexpr uint32_t kRegAvolR = 0x0Au;
        constexpr uint32_t kRegBvolL = 0x0Cu;
        constexpr uint32_t kRegBvolR = 0x0Eu;
        constexpr uint32_t kRegIrqInfo = 0x7C2u;

        constexpr uint16_t kAttrIrqEnable = 0x0040u;
        constexpr uint16_t kAttrCoreEnable = 0x8000u;
        constexpr uint16_t kStatxDmaReady = 0x0080u;

        constexpr uint8_t kFlagLoopEnd = 0x01u;
        constexpr uint8_t kFlagLoopRepeat = 0x02u;
        constexpr uint8_t kFlagLoopStart = 0x04u;

        constexpr int32_t kFilterPositive[5] = {0, 60, 115, 98, 122};
        constexpr int32_t kFilterNegative[5] = {0, 0, -52, -55, -60};

        int32_t clamp16(int32_t value)
        {
            return std::clamp(value, -0x8000, 0x7FFF);
        }

        // Fixed volumes are 15-bit signed halves; sweeps are approximated by where they settle.
        int32_t decodeVolume(uint16_t value)
        {
            if ((value & 0x8000u) == 0u)
                return static_cast<int16_t>(static_cast<uint16_t>(value << 1u));
            if ((value & 0x2000u) != 0u)
                return 0;
            return (value & 0x1000u) != 0u ? -0x7FFF : 0x7FFF;
        }

        uint32_t combineAddress(uint16_t hi, uint16_t lo)
        {
            return ((static_cast<uint32_t>(hi) << 16u) | lo) & kRamMask;
        }
    }

    void decodeAdpcmBlock(const uint16_t *block, int16_t *out, AdpcmHistory &history)
    {
        const uint8_t header = static_cast<uint8_t>(block[0] & 0xFFu);
        uint32_t shift = header & 0x0Fu;
        if (shift > 12u)
            shift = 9u; // reserved shifts behave like 9 on hardware
        const uint32_t filter = std::min<uint32_t>((header >> 4u) & 0x07u, 4u);
        const int32_t positive = kFilterPositive[filter];
        const int32_t negative = kFilterNegative[filter];

        for (uint32_t i = 0; i < 28u; ++i)
        {
            const uint16_t word = block[1u + i / 4u];
            const uint16_t nibble = static_cast<uint16_t>((word >> ((i & 3u) * 4u)) & 0x0Fu);
            int32_t sample = static_cast<int16_t>(static_cast<uint16_t>(nibble << 12u)) >> shift;
            sample += (history.prev1 * positive + history.prev2 * negative + 32) >> 6;
            sample = clamp16(sample);
            history.prev2 = history.prev1;
            history.prev1 = sample;
            out[i] = static_cast<int16_t>(sample);
        }
    }

    IopSpu2::IopSpu2(std::span<uint8_t> iopRam)
        : m_iopRam(iopRam), m_ram(kRamWords)
    {
        reset();
    }

    void IopSpu2::reset()
    {
        std::fill(m_ram.begin(), m_ram.end(), uint16_t{0});
        m_cores = {};
        m_globals = {};
        m_irqInfo = 0u;
        m_admaFrame = 0u;
        m_events = 0u;
    }

    uint32_t IopSpu2::takeEvents() noexcept
    {
        const uint32_t events = m_events;
        m_events = 0u;
        return events;
    }

    int32_t IopSpu2::globalVolume(uint32_t reg) const
    {
        return decodeVolume(m_globals[reg / 2u]);
    }

    int32_t IopSpu2::plainVolume(uint32_t reg) const
    {
        return static_cast<int16_t>(m_globals[reg / 2u]);
    }

    uint16_t IopSpu2::read16(uint32_t phys)
    {
        const uint32_t offset = (phys - kBase) & ~1u;
        if (offset < kGlobalBase)
            return readCore(offset >> 10u, offset & 0x3FFu);
        if (offset == kRegIrqInfo)
        {
            // libsd's handler reads which core fired; the bits clear once read.
            const uint16_t info = m_irqInfo;
            m_irqInfo = 0u;
            return info;
        }
        return m_globals[(offset - kGlobalBase) / 2u];
    }

    void IopSpu2::write16(uint32_t phys, uint16_t value)
    {
        const uint32_t offset = (phys - kBase) & ~1u;
        if (offset < kGlobalBase)
        {
            writeCore(offset >> 10u, offset & 0x3FFu, value);
            return;
        }
        m_globals[(offset - kGlobalBase) / 2u] = value;
    }

    uint16_t IopSpu2::readCore(uint32_t coreIndex, uint32_t reg)
    {
        Core &core = m_cores[coreIndex];
        if (reg < kRegPmon)
        {
            const Voice &voice = core.voices[reg >> 4u];
            switch (reg & 0x0Fu)
            {
            case 0x0A:
                return static_cast<uint16_t>(voice.envelope);
            case 0x0C:
                return static_cast<uint16_t>(voice.volumeLeft >> 1);
            case 0x0E:
                return static_cast<uint16_t>(voice.volumeRight >> 1);
            default:
                return core.regs[reg / 2u];
            }
        }
        if (reg >= kRegVoiceAddr && reg < kRegVoiceAddrEnd)
        {
            const Voice &voice = core.voices[(reg - kRegVoiceAddr) / 12u];
            uint32_t address = 0u;
            switch ((reg - kRegVoiceAddr) % 12u)
            {
            case 0:
            case 2:
                address = voice.startAddress;
                break;
            case 4:
            case 6:
                address = voice.loopAddress;
                break;
            default:
                address = voice.blockAddress;
                break;
            }
            return ((reg - kRegVoiceAddr) % 4u) == 0u ? static_cast<uint16_t>(address >> 16u)
                                                     : static_cast<uint16_t>(address);
        }
        switch (reg)
        {
        case kRegAttr:
            return core.attr;
        case kRegIrqaHi:
            return static_cast<uint16_t>(core.irqAddress >> 16u);
        case kRegIrqaLo:
            return static_cast<uint16_t>(core.irqAddress);
        case kRegTsaHi:
            return static_cast<uint16_t>(core.transferAddress >> 16u);
        case kRegTsaLo:
            return static_cast<uint16_t>(core.transferAddress);
        case kRegStd:
        {
            const uint16_t value = m_ram[core.transferAddress];
            core.transferAddress = (core.transferAddress + 1u) & kRamMask;
            return value;
        }
        case kRegAdmas:
            return core.admas;
        case kRegEndxLo:
            return static_cast<uint16_t>(core.endFlags);
        case kRegEndxHi:
            return static_cast<uint16_t>(core.endFlags >> 16u);
        case kRegStatx:
            return core.statx;
        default:
            return core.regs[reg / 2u];
        }
    }

    void IopSpu2::writeCore(uint32_t coreIndex, uint32_t reg, uint16_t value)
    {
        Core &core = m_cores[coreIndex];
        core.regs[reg / 2u] = value;
        if (reg < kRegPmon)
        {
            Voice &voice = core.voices[reg >> 4u];
            switch (reg & 0x0Fu)
            {
            case 0x00:
                voice.volumeLeftReg = value;
                voice.volumeLeft = decodeVolume(value);
                break;
            case 0x02:
                voice.volumeRightReg = value;
                voice.volumeRight = decodeVolume(value);
                break;
            case 0x04:
                voice.pitch = std::min<uint16_t>(value, 0x3FFFu);
                break;
            case 0x06:
                voice.adsr1 = value;
                break;
            case 0x08:
                voice.adsr2 = value;
                break;
            case 0x0A:
                voice.envelope = value & 0x7FFF;
                break;
            default:
                break;
            }
            return;
        }
        if (reg >= kRegVoiceAddr && reg < kRegVoiceAddrEnd)
        {
            Voice &voice = core.voices[(reg - kRegVoiceAddr) / 12u];
            const uint32_t field = (reg - kRegVoiceAddr) % 12u;
            const bool high = (field % 4u) == 0u;
            const auto merge = [&](uint32_t current)
            {
                return high ? combineAddress(value, static_cast<uint16_t>(current))
                            : combineAddress(static_cast<uint16_t>(current >> 16u), value);
            };
            if (field < 4u)
            {
                voice.startAddress = merge(voice.startAddress);
            }
            else if (field < 8u)
            {
                // A loop address set by the driver wins over loop-start flags in the sample data.
                voice.loopAddress = merge(voice.loopAddress);
                voice.customLoop = true;
            }
            else
            {
                voice.nextAddress = merge(voice.nextAddress);
                voice.blockAddress = voice.nextAddress;
                voice.fresh = true;
            }
            return;
        }

        switch (reg)
        {
        case kRegAttr:
            core.attr = value;
            // libsd resets a core by clearing ATTR and then waits for STATX to read idle.
            if ((value & kAttrCoreEnable) == 0u)
                core.statx = 0u;
            if ((value & kAttrIrqEnable) == 0u)
                m_irqInfo = static_cast<uint16_t>(m_irqInfo & ~(4u << coreIndex));
            break;
        case kRegIrqaHi:
            core.irqAddress = combineAddress(value, static_cast<uint16_t>(core.irqAddress));
            break;
        case kRegIrqaLo:
            core.irqAddress = combineAddress(static_cast<uint16_t>(core.irqAddress >> 16u), value);
            break;
        case kRegKonLo:
            keyOn(core, value);
            break;
        case kRegKonHi:
            keyOn(core, static_cast<uint32_t>(value & 0xFFu) << 16u);
            break;
        case kRegKoffLo:
            keyOff(core, value);
            break;
        case kRegKoffHi:
            keyOff(core, static_cast<uint32_t>(value & 0xFFu) << 16u);
            break;
        case kRegTsaHi:
            core.transferAddress = combineAddress(value, static_cast<uint16_t>(core.transferAddress));
            break;
        case kRegTsaLo:
            core.transferAddress = combineAddress(static_cast<uint16_t>(core.transferAddress >> 16u), value);
            break;
        case kRegStd:
            m_ram[core.transferAddress] = value;
            checkIrqAll(core.transferAddress, 1u);
            core.transferAddress = (core.transferAddress + 1u) & kRamMask;
            break;
        case kRegAdmas:
            core.admas = value;
            break;
        case kRegEndxLo:
        case kRegEndxHi:
            core.endFlags = 0u;
            break;
        default:
            break;
        }
    }

    void IopSpu2::keyOn(Core &core, uint32_t mask)
    {
        for (uint32_t i = 0; i < kVoicesPerCore; ++i)
        {
            if ((mask & (1u << i)) == 0u)
                continue;
            Voice &voice = core.voices[i];
            voice.phase = Phase::Attack;
            voice.envelope = 0;
            voice.envelopeCounter = 0u;
            voice.nextAddress = voice.startAddress;
            voice.blockAddress = voice.startAddress;
            voice.customLoop = false;
            voice.fresh = true;
            voice.blockFlags = 0u;
            voice.pitchCounter = 0u;
            voice.sampleIndex = 28u;
            voice.history = {};
            voice.previous = 0;
            voice.current = 0;
            core.endFlags &= ~(1u << i);
        }
    }

    void IopSpu2::keyOff(Core &core, uint32_t mask)
    {
        for (uint32_t i = 0; i < kVoicesPerCore; ++i)
        {
            if ((mask & (1u << i)) != 0u && core.voices[i].phase != Phase::Off)
            {
                core.voices[i].phase = Phase::Release;
                core.voices[i].envelopeCounter = 0u;
            }
        }
    }

    void IopSpu2::checkIrq(uint32_t coreIndex, uint32_t start, uint32_t count)
    {
        Core &core = m_cores[coreIndex];
        if ((core.attr & kAttrIrqEnable) == 0u || (m_irqInfo & (4u << coreIndex)) != 0u)
            return;
        const uint32_t distance = (core.irqAddress - start) & kRamMask;
        if (distance >= count)
            return;
        m_irqInfo = static_cast<uint16_t>(m_irqInfo | (4u << coreIndex));
        m_events |= EventIrq;
    }

    void IopSpu2::checkIrqAll(uint32_t start, uint32_t count)
    {
        // Either core's IRQ address matches accesses made on behalf of both cores.
        checkIrq(0u, start, count);
        checkIrq(1u, start, count);
    }

    void IopSpu2::loadBlock(uint32_t coreIndex, Voice &voice, uint32_t voiceIndex)
    {
        Core &core = m_cores[coreIndex];
        if (!voice.fresh)
        {
            if ((voice.blockFlags & kFlagLoopEnd) != 0u)
            {
                core.endFlags |= 1u << voiceIndex;
                voice.nextAddress = voice.loopAddress;
                if ((voice.blockFlags & kFlagLoopRepeat) == 0u)
                {
                    // End without repeat: the voice is silenced at once, not released.
                    voice.phase = Phase::Off;
                    voice.envelope = 0;
                }
            }
            else
            {
                voice.nextAddress = (voice.blockAddress + 8u) & kRamMask;
            }
        }
        voice.fresh = false;
        voice.blockAddress = voice.nextAddress & kRamMask & ~7u;

        uint16_t block[8];
        for (uint32_t i = 0; i < 8u; ++i)
            block[i] = m_ram[(voice.blockAddress + i) & kRamMask];
        voice.blockFlags = static_cast<uint8_t>(block[0] >> 8u);
        if ((voice.blockFlags & kFlagLoopStart) != 0u && !voice.customLoop)
            voice.loopAddress = voice.blockAddress;
        checkIrqAll(voice.blockAddress, 8u);
        decodeAdpcmBlock(block, voice.samples.data(), voice.history);
        voice.sampleIndex = 0u;
    }

    int32_t IopSpu2::nextSample(uint32_t coreIndex, Voice &voice, uint32_t voiceIndex)
    {
        if (voice.sampleIndex >= 28u)
            loadBlock(coreIndex, voice, voiceIndex);
        return voice.samples[voice.sampleIndex++];
    }

    void IopSpu2::tickEnvelope(Voice &voice)
    {
        const int32_t sustainLevel = static_cast<int32_t>(((voice.adsr1 & 0x0Fu) + 1u) * 0x800u);
        if (voice.phase == Phase::Decay && voice.envelope <= sustainLevel)
        {
            voice.phase = Phase::Sustain;
            voice.envelopeCounter = 0u;
        }

        uint32_t rate = 0u;
        bool decrease = false;
        bool exponential = false;
        switch (voice.phase)
        {
        case Phase::Attack:
            rate = (voice.adsr1 >> 8u) & 0x7Fu;
            exponential = (voice.adsr1 & 0x8000u) != 0u;
            break;
        case Phase::Decay:
            rate = ((voice.adsr1 >> 4u) & 0x0Fu) << 2u;
            decrease = true;
            exponential = true;
            break;
        case Phase::Sustain:
            rate = (voice.adsr2 >> 6u) & 0x7Fu;
            decrease = (voice.adsr2 & 0x4000u) != 0u;
            exponential = (voice.adsr2 & 0x8000u) != 0u;
            break;
        case Phase::Release:
            rate = (voice.adsr2 & 0x1Fu) << 2u;
            decrease = true;
            exponential = (voice.adsr2 & 0x0020u) != 0u;
            break;
        case Phase::Off:
            return;
        }

        // Envelope timing as documented for the PS1 SPU, which the SPU2 voices keep.
        const int32_t shift = static_cast<int32_t>(rate >> 2u);
        const int32_t stepBits = static_cast<int32_t>(rate & 3u);
        int32_t step = decrease ? (-8 + stepBits) : (7 - stepBits);
        uint32_t cycles = 1u << std::min(std::max(0, shift - 11), 30);
        step = static_cast<int32_t>(static_cast<uint32_t>(step) << std::max(0, 11 - shift));
        if (exponential && !decrease && voice.envelope > 0x6000)
            cycles *= 4u;
        if (exponential && decrease)
            step = (step * voice.envelope) >> 15;

        if (++voice.envelopeCounter < cycles)
            return;
        voice.envelopeCounter = 0u;
        voice.envelope = std::clamp(voice.envelope + step, 0, 0x7FFF);

        switch (voice.phase)
        {
        case Phase::Attack:
            if (voice.envelope >= 0x7FFF)
            {
                voice.phase = Phase::Decay;
                voice.envelopeCounter = 0u;
            }
            break;
        case Phase::Decay:
            if (voice.envelope <= sustainLevel)
            {
                voice.phase = Phase::Sustain;
                voice.envelopeCounter = 0u;
            }
            break;
        case Phase::Release:
            if (voice.envelope == 0)
                voice.phase = Phase::Off;
            break;
        default:
            break;
        }
    }

    bool IopSpu2::startDma(uint32_t coreIndex, uint32_t madr, uint32_t bytes, bool toSpu)
    {
        Core &core = m_cores[coreIndex & 1u];
        madr &= 0x1FFFFFu;
        if (toSpu && (core.admas & (1u << coreIndex)) != 0u)
        {
            core.adma = {true, madr, bytes};
            return false;
        }

        const uint32_t available = madr < m_iopRam.size() ? static_cast<uint32_t>(m_iopRam.size()) - madr : 0u;
        const uint32_t words = std::min(bytes, available) / 2u;
        uint8_t *iop = m_iopRam.data() + madr;
        for (uint32_t i = 0; i < words; ++i)
        {
            const uint32_t address = (core.transferAddress + i) & kRamMask;
            if (toSpu)
                std::memcpy(&m_ram[address], iop + i * 2u, 2u);
            else
                std::memcpy(iop + i * 2u, &m_ram[address], 2u);
        }
        checkIrqAll(core.transferAddress, words);
        core.transferAddress = (core.transferAddress + words) & kRamMask;
        // libsd's DMA handler spins until the core reports the transfer drained.
        core.statx |= kStatxDmaReady;
        return true;
    }

    void IopSpu2::pullAdmaBlock(uint32_t coreIndex)
    {
        Core &core = m_cores[coreIndex];
        core.inputLeft.fill(0);
        core.inputRight.fill(0);
        if ((core.admas & (1u << coreIndex)) == 0u || !core.adma.active)
            return;

        // Each block is 256 left samples followed by 256 right samples.
        constexpr uint32_t kBlockBytes = kAdmaBlockFrames * 4u;
        const uint32_t bytes = std::min(core.adma.remaining, kBlockBytes);
        const uint32_t address = core.adma.address;
        const auto copy = [&](uint32_t offset, std::array<int16_t, kAdmaBlockFrames> &target)
        {
            const uint32_t length = std::min<uint32_t>(bytes > offset ? bytes - offset : 0u, kBlockBytes / 2u);
            if (address + offset + length <= m_iopRam.size())
                std::memcpy(target.data(), m_iopRam.data() + address + offset, length);
        };
        copy(0u, core.inputLeft);
        copy(kBlockBytes / 2u, core.inputRight);

        // Mirror the block into the core's input area so IRQ addresses there fire like on hardware.
        const uint32_t base = 0x2000u + coreIndex * 0x400u + core.admaHalf * 0x100u;
        std::memcpy(&m_ram[base], core.inputLeft.data(), kBlockBytes / 2u);
        std::memcpy(&m_ram[base + 0x200u], core.inputRight.data(), kBlockBytes / 2u);
        checkIrqAll(base, 0x100u);
        checkIrqAll(base + 0x200u, 0x100u);
        core.admaHalf ^= 1u;

        core.adma.address += bytes;
        core.adma.remaining -= bytes;
        if (core.adma.remaining == 0u)
        {
            core.adma.active = false;
            core.statx |= kStatxDmaReady;
            m_events |= coreIndex == 0u ? EventDmaDone0 : EventDmaDone1;
        }
    }

    void IopSpu2::generate(uint32_t frames, std::vector<int16_t> &out)
    {
        out.reserve(out.size() + frames * 2u);
        for (uint32_t frame = 0; frame < frames; ++frame)
        {
            if (m_admaFrame == 0u)
            {
                pullAdmaBlock(0u);
                pullAdmaBlock(1u);
            }

            int32_t external[2] = {0, 0};
            int32_t result[2] = {0, 0};
            for (uint32_t coreIndex = 0; coreIndex < 2u; ++coreIndex)
            {
                Core &core = m_cores[coreIndex];
                const uint32_t vmixL = core.regs[kRegVmixL / 2u] | (static_cast<uint32_t>(core.regs[kRegVmixL / 2u + 1u]) << 16u);
                const uint32_t vmixR = core.regs[kRegVmixR / 2u] | (static_cast<uint32_t>(core.regs[kRegVmixR / 2u + 1u]) << 16u);
                int32_t dryL = 0;
                int32_t dryR = 0;
                for (uint32_t i = 0; i < kVoicesPerCore; ++i)
                {
                    Voice &voice = core.voices[i];
                    if (voice.phase == Phase::Off)
                        continue;

                    voice.pitchCounter += voice.pitch;
                    while (voice.pitchCounter >= 0x1000u && voice.phase != Phase::Off)
                    {
                        voice.pitchCounter -= 0x1000u;
                        voice.previous = voice.current;
                        voice.current = nextSample(coreIndex, voice, i);
                    }
                    if (voice.phase == Phase::Off)
                        continue;

                    const int32_t fraction = static_cast<int32_t>(voice.pitchCounter);
                    const int32_t sample = voice.previous + (((voice.current - voice.previous) * fraction) >> 12);
                    const int32_t value = (sample * voice.envelope) >> 15;
                    tickEnvelope(voice);
                    if ((vmixL & (1u << i)) != 0u)
                        dryL += (value * voice.volumeLeft) >> 15;
                    if ((vmixR & (1u << i)) != 0u)
                        dryR += (value * voice.volumeRight) >> 15;
                }

                const uint32_t volumes = kCoreVolumeStride * coreIndex;
                const uint16_t mmix = core.regs[kRegMmix / 2u];
                const int32_t inputL = (core.inputLeft[m_admaFrame] * plainVolume(volumes + kRegBvolL)) >> 15;
                const int32_t inputR = (core.inputRight[m_admaFrame] * plainVolume(volumes + kRegBvolR)) >> 15;
                const int32_t extL = (external[0] * plainVolume(volumes + kRegAvolL)) >> 15;
                const int32_t extR = (external[1] * plainVolume(volumes + kRegAvolR)) >> 15;

                int32_t left = 0;
                int32_t right = 0;
                if (mmix & 0x0800u)
                    left += clamp16(dryL);
                if (mmix & 0x0400u)
                    right += clamp16(dryR);
                if (mmix & 0x0080u)
                    left += inputL;
                if (mmix & 0x0040u)
                    right += inputR;
                if (mmix & 0x0008u)
                    left += extL;
                if (mmix & 0x0004u)
                    right += extR;

                // Core 0's output feeds core 1's external input; core 1's output is the DAC.
                const int32_t masteredL = clamp16((clamp16(left) * globalVolume(volumes + kRegMvolL)) >> 15);
                const int32_t masteredR = clamp16((clamp16(right) * globalVolume(volumes + kRegMvolR)) >> 15);
                external[0] = masteredL;
                external[1] = masteredR;
                result[0] = masteredL;
                result[1] = masteredR;
            }

            out.push_back(static_cast<int16_t>(result[0]));
            out.push_back(static_cast<int16_t>(result[1]));
            m_admaFrame = (m_admaFrame + 1u) % kAdmaBlockFrames;
        }
    }
}
