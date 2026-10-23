#include "iop_spu2.h"

#include <algorithm>
#include <cstring>

namespace ps2x::iop::detail
{
    namespace
    {
        constexpr uint32_t kAddressMask = IopSpu2::RamHalfwords - 1u;
        constexpr uint32_t kCoreStride = 0x400u;
        constexpr uint32_t kVoiceParamEnd = 0x180u;
        constexpr uint32_t kVoiceAddressBase = 0x1C0u;
        constexpr uint32_t kVoiceAddressEnd = kVoiceAddressBase + 24u * 0xCu;
        constexpr uint32_t kMasterBase = 0x760u;
        constexpr uint32_t kMasterStride = 0x28u;
        constexpr uint32_t kSpdifIrqInfo = 0x7C2u;

        constexpr int32_t kAdpcmPos[5] = {0, 60, 115, 98, 122};
        constexpr int32_t kAdpcmNeg[5] = {0, 0, -52, -55, -60};

        int32_t clamp16(int32_t value)
        {
            return std::clamp(value, -32768, 32767);
        }

        uint32_t setLow16(uint32_t current, uint16_t value)
        {
            return (current & 0xFFFF0000u) | value;
        }

        uint32_t setHigh8(uint32_t current, uint16_t value)
        {
            return (current & 0xFFFFu) | (static_cast<uint32_t>(value & 0xFFu) << 16u);
        }

        // SPU2 addresses are 20-bit halfword addresses split into a high word (bits 16-19) and a low word.
        uint32_t setAddressHigh(uint32_t current, uint16_t value)
        {
            return ((static_cast<uint32_t>(value & 0xFu) << 16u) | (current & 0xFFFFu)) & kAddressMask;
        }

        uint32_t setAddressLow(uint32_t current, uint16_t value)
        {
            return ((current & 0xF0000u) | value) & kAddressMask;
        }
    }

    void IopSpu2::Envelope::reset(uint8_t newRate, bool newDecreasing, bool newExponential)
    {
        rate = newRate;
        decreasing = newDecreasing;
        exponential = newExponential;
        counter = 0u;
        increment = 0x8000u;
        const int32_t baseStep = 7 - static_cast<int32_t>(rate & 3u);
        step = decreasing ? ~baseStep : baseStep;
        if (rate < 44u)
            step <<= (11 - (rate >> 2u));
        else if (rate >= 48u)
            increment >>= ((rate >> 2u) - 11u);
    }

    int32_t IopSpu2::Envelope::tick(int32_t level)
    {
        uint32_t thisIncrement = increment;
        int32_t thisStep = step;
        if (exponential)
        {
            if (decreasing)
            {
                thisStep = (thisStep * level) >> 15;
            }
            else if (level >= 0x6000)
            {
                if (rate < 40u)
                {
                    thisStep >>= 2;
                }
                else if (rate >= 44u)
                {
                    thisIncrement >>= 2u;
                }
                else
                {
                    thisStep >>= 1;
                    thisIncrement >>= 1u;
                }
            }
        }
        counter += thisIncrement;
        if ((counter & 0x8000u) == 0u)
            return level;
        counter = 0u;
        return std::clamp(level + thisStep, 0, 0x7FFF);
    }

    void IopSpu2::Volume::write(uint16_t value)
    {
        reg = value;
        if ((value & 0x8000u) == 0u)
        {
            current = static_cast<int16_t>(static_cast<uint16_t>(value << 1u));
            return;
        }
        sweep.reset(static_cast<uint8_t>(value & 0x7Fu), (value & 0x2000u) != 0u, (value & 0x4000u) != 0u);
    }

    void IopSpu2::Volume::tick()
    {
        if ((reg & 0x8000u) == 0u)
            return;
        const bool negative = current < 0;
        int32_t magnitude = negative ? -current : current;
        magnitude = sweep.tick(std::min(magnitude, 0x7FFF));
        current = ((reg & 0x1000u) != 0u) != negative ? -magnitude : magnitude;
    }

    IopSpu2::IopSpu2()
        : m_ram(RamHalfwords)
    {
        reset();
    }

    void IopSpu2::reset()
    {
        std::fill(m_ram.begin(), m_ram.end(), uint16_t{0});
        m_regs.fill(0u);
        m_cores = {};
        // Some sound drivers never write the master and input volumes; on hardware the BIOS
        // leaves them at full scale before the game boots.
        for (Core &core : m_cores)
        {
            core.mvolL.write(0x3FFFu);
            core.mvolR.write(0x3FFFu);
            core.avolL = core.avolR = 0x7FFF;
            core.bvolL = core.bvolR = 0x7FFF;
        }
        m_irqPending = false;
    }

    bool IopSpu2::takeIrq() noexcept
    {
        const bool pending = m_irqPending;
        m_irqPending = false;
        return pending;
    }

    void IopSpu2::checkIrq(uint32_t begin, uint32_t count)
    {
        for (int c = 0; c < 2; ++c)
        {
            Core &core = m_cores[c];
            if ((core.attr & 0x40u) == 0u)
                continue;
            if (((core.irqa - begin) & kAddressMask) < count)
            {
                m_regs[kSpdifIrqInfo / 2u] |= static_cast<uint16_t>(4u << c);
                m_irqPending = true;
            }
        }
    }

    uint16_t IopSpu2::read16(uint32_t offset) const
    {
        offset &= (RegisterBytes - 1u) & ~1u;
        if (offset < 2u * kCoreStride)
        {
            const int coreIndex = static_cast<int>(offset / kCoreStride);
            const uint32_t local = offset % kCoreStride;
            const Core &core = m_cores[coreIndex];
            if (local < kVoiceParamEnd)
                return readVoiceParam(core.voices[local >> 4u], (local & 0xFu) >> 1u);
            if (local >= kVoiceAddressBase && local < kVoiceAddressEnd)
            {
                const uint32_t rel = local - kVoiceAddressBase;
                return readVoiceAddress(core.voices[rel / 0xCu], (rel % 0xCu) >> 1u);
            }
            uint16_t value = 0u;
            if (readCoreRegister(coreIndex, local, value))
                return value;
        }
        else if (offset >= kMasterBase && offset < kMasterBase + 2u * kMasterStride)
        {
            const Core &core = m_cores[(offset - kMasterBase) / kMasterStride];
            switch ((offset - kMasterBase) % kMasterStride)
            {
            case 0x10u:
                return static_cast<uint16_t>(core.mvolL.current);
            case 0x12u:
                return static_cast<uint16_t>(core.mvolR.current);
            default:
                break;
            }
        }
        return m_regs[offset / 2u];
    }

    void IopSpu2::write16(uint32_t offset, uint16_t value)
    {
        offset &= (RegisterBytes - 1u) & ~1u;
        m_regs[offset / 2u] = value;
        if (offset < 2u * kCoreStride)
        {
            const int coreIndex = static_cast<int>(offset / kCoreStride);
            const uint32_t local = offset % kCoreStride;
            Core &core = m_cores[coreIndex];
            if (local < kVoiceParamEnd)
            {
                writeVoiceParam(core, core.voices[local >> 4u], (local & 0xFu) >> 1u, value);
                return;
            }
            if (local >= kVoiceAddressBase && local < kVoiceAddressEnd)
            {
                const uint32_t rel = local - kVoiceAddressBase;
                writeVoiceAddress(core.voices[rel / 0xCu], (rel % 0xCu) >> 1u, value);
                return;
            }
            writeCoreRegister(coreIndex, local, value);
            return;
        }
        if (offset >= kMasterBase && offset < kMasterBase + 2u * kMasterStride)
        {
            Core &core = m_cores[(offset - kMasterBase) / kMasterStride];
            switch ((offset - kMasterBase) % kMasterStride)
            {
            case 0x0u:
                core.mvolL.write(value);
                break;
            case 0x2u:
                core.mvolR.write(value);
                break;
            case 0x4u:
                core.evolL = static_cast<int16_t>(value);
                break;
            case 0x6u:
                core.evolR = static_cast<int16_t>(value);
                break;
            case 0x8u:
                core.avolL = static_cast<int16_t>(value);
                break;
            case 0xAu:
                core.avolR = static_cast<int16_t>(value);
                break;
            case 0xCu:
                core.bvolL = static_cast<int16_t>(value);
                break;
            case 0xEu:
                core.bvolR = static_cast<int16_t>(value);
                break;
            default:
                break;
            }
            return;
        }
        if (offset == kSpdifIrqInfo)
            m_regs[offset / 2u] = 0u;
    }

    uint16_t IopSpu2::readVoiceParam(const Voice &voice, uint32_t reg) const
    {
        switch (reg)
        {
        case 0:
            return voice.volL.reg;
        case 1:
            return voice.volR.reg;
        case 2:
            return voice.pitch;
        case 3:
            return voice.adsr1;
        case 4:
            return voice.adsr2;
        case 5:
            return static_cast<uint16_t>(voice.envLevel);
        case 6:
            return static_cast<uint16_t>(voice.volL.current);
        case 7:
            return static_cast<uint16_t>(voice.volR.current);
        default:
            return 0u;
        }
    }

    void IopSpu2::writeVoiceParam(Core &, Voice &voice, uint32_t reg, uint16_t value)
    {
        switch (reg)
        {
        case 0:
            voice.volL.write(value);
            break;
        case 1:
            voice.volR.write(value);
            break;
        case 2:
            voice.pitch = value;
            break;
        case 3:
            voice.adsr1 = value;
            break;
        case 4:
            voice.adsr2 = value;
            break;
        case 5:
            voice.envLevel = std::min<int32_t>(value, 0x7FFF);
            break;
        default:
            break;
        }
    }

    uint16_t IopSpu2::readVoiceAddress(const Voice &voice, uint32_t reg) const
    {
        uint32_t address = 0u;
        switch (reg >> 1u)
        {
        case 0:
            address = voice.ssa;
            break;
        case 1:
            address = voice.lsax;
            break;
        default:
            // NAX points at the halfword holding the sample being played, past the block header.
            address = voice.nax;
            if (voice.phase != Phase::Off)
                address = (address + 1u + std::min<uint32_t>(voice.counter >> 12u, 27u) / 4u) & kAddressMask;
            break;
        }
        return (reg & 1u) == 0u ? static_cast<uint16_t>(address >> 16u) : static_cast<uint16_t>(address);
    }

    void IopSpu2::writeVoiceAddress(Voice &voice, uint32_t reg, uint16_t value)
    {
        uint32_t *target = nullptr;
        switch (reg >> 1u)
        {
        case 0:
            target = &voice.ssa;
            break;
        case 1:
            target = &voice.lsax;
            break;
        default:
            target = &voice.nax;
            voice.blockLoaded = false;
            break;
        }
        *target = (reg & 1u) == 0u ? setAddressHigh(*target, value) : setAddressLow(*target, value);
    }

    bool IopSpu2::readCoreRegister(int coreIndex, uint32_t local, uint16_t &value) const
    {
        const Core &core = m_cores[coreIndex];
        switch (local)
        {
        case 0x19Cu:
            value = static_cast<uint16_t>(core.irqa >> 16u);
            return true;
        case 0x19Eu:
            value = static_cast<uint16_t>(core.irqa);
            return true;
        case 0x1A8u:
            value = static_cast<uint16_t>(core.tsa >> 16u);
            return true;
        case 0x1AAu:
            value = static_cast<uint16_t>(core.tsa);
            return true;
        case 0x340u:
            value = static_cast<uint16_t>(core.endx);
            return true;
        case 0x342u:
            value = static_cast<uint16_t>(core.endx >> 16u);
            return true;
        case 0x344u:
            value = core.statx;
            return true;
        default:
            return false;
        }
    }

    void IopSpu2::writeCoreRegister(int coreIndex, uint32_t local, uint16_t value)
    {
        Core &core = m_cores[coreIndex];
        switch (local)
        {
        case 0x180u:
            core.pmon = setLow16(core.pmon, value);
            break;
        case 0x182u:
            core.pmon = setHigh8(core.pmon, value);
            break;
        case 0x184u:
            core.non = setLow16(core.non, value);
            break;
        case 0x186u:
            core.non = setHigh8(core.non, value);
            break;
        case 0x188u:
            core.vmixl = setLow16(core.vmixl, value);
            break;
        case 0x18Au:
            core.vmixl = setHigh8(core.vmixl, value);
            break;
        case 0x18Cu:
            core.vmixel = setLow16(core.vmixel, value);
            break;
        case 0x18Eu:
            core.vmixel = setHigh8(core.vmixel, value);
            break;
        case 0x190u:
            core.vmixr = setLow16(core.vmixr, value);
            break;
        case 0x192u:
            core.vmixr = setHigh8(core.vmixr, value);
            break;
        case 0x194u:
            core.vmixer = setLow16(core.vmixer, value);
            break;
        case 0x196u:
            core.vmixer = setHigh8(core.vmixer, value);
            break;
        case 0x198u:
            core.mmix = value;
            break;
        case 0x19Au:
            core.attr = value;
            break;
        case 0x19Cu:
            core.irqa = setAddressHigh(core.irqa, value);
            break;
        case 0x19Eu:
            core.irqa = setAddressLow(core.irqa, value);
            break;
        case 0x1A0u:
            keyOn(core, value);
            break;
        case 0x1A2u:
            keyOn(core, static_cast<uint32_t>(value & 0xFFu) << 16u);
            break;
        case 0x1A4u:
            keyOff(core, value);
            break;
        case 0x1A6u:
            keyOff(core, static_cast<uint32_t>(value & 0xFFu) << 16u);
            break;
        case 0x1A8u:
            core.tsa = setAddressHigh(core.tsa, value);
            break;
        case 0x1AAu:
            core.tsa = setAddressLow(core.tsa, value);
            break;
        case 0x1ACu:
            checkIrq(core.tsa, 1u);
            m_ram[core.tsa] = value;
            core.tsa = (core.tsa + 1u) & kAddressMask;
            break;
        case 0x1B0u:
            core.admas = value;
            break;
        case 0x340u:
            core.endx &= 0xFF0000u;
            break;
        case 0x342u:
            core.endx &= 0xFFFFu;
            break;
        default:
            break;
        }
    }

    void IopSpu2::keyOn(Core &core, uint32_t mask)
    {
        for (int v = 0; v < 24; ++v)
        {
            if ((mask & (1u << v)) == 0u)
                continue;
            Voice &voice = core.voices[v];
            voice.nax = voice.ssa;
            voice.counter = 0u;
            voice.blockLoaded = false;
            voice.adpcm1 = 0;
            voice.adpcm2 = 0;
            voice.samples.fill(0);
            voice.envLevel = 0;
            voice.phase = Phase::Attack;
            voice.env.reset(static_cast<uint8_t>((voice.adsr1 >> 8u) & 0x7Fu), false, (voice.adsr1 & 0x8000u) != 0u);
            core.endx &= ~(1u << v);
        }
    }

    void IopSpu2::keyOff(Core &core, uint32_t mask)
    {
        for (int v = 0; v < 24; ++v)
        {
            if ((mask & (1u << v)) == 0u)
                continue;
            Voice &voice = core.voices[v];
            if (voice.phase == Phase::Off || voice.phase == Phase::Release)
                continue;
            voice.phase = Phase::Release;
            voice.env.reset(static_cast<uint8_t>((voice.adsr2 & 0x1Fu) << 2u), true, (voice.adsr2 & 0x20u) != 0u);
        }
    }

    void IopSpu2::loadBlock(Core &, Voice &voice)
    {
        const uint32_t base = voice.nax & ~7u;
        checkIrq(base, 8u);
        const uint16_t header = m_ram[base];
        voice.blockFlags = static_cast<uint8_t>(header >> 8u);
        if ((voice.blockFlags & 4u) != 0u)
            voice.lsax = base;

        uint32_t shift = header & 0xFu;
        if (shift > 12u)
            shift = 9u;
        const uint32_t filter = std::min<uint32_t>((header >> 4u) & 7u, 4u);
        for (uint32_t i = 0; i < 28u; ++i)
        {
            const uint16_t word = m_ram[(base + 1u + i / 4u) & kAddressMask];
            const uint16_t nibble = static_cast<uint16_t>((word >> ((i & 3u) * 4u)) & 0xFu);
            int32_t sample = static_cast<int16_t>(static_cast<uint16_t>(nibble << 12u)) >> shift;
            sample += (voice.adpcm1 * kAdpcmPos[filter] + voice.adpcm2 * kAdpcmNeg[filter] + 32) >> 6;
            sample = clamp16(sample);
            voice.adpcm2 = voice.adpcm1;
            voice.adpcm1 = sample;
            voice.samples[3u + i] = static_cast<int16_t>(sample);
        }
        voice.blockLoaded = true;
    }

    void IopSpu2::tickEnvelope(Voice &voice)
    {
        switch (voice.phase)
        {
        case Phase::Off:
            return;
        case Phase::Attack:
            voice.envLevel = voice.env.tick(voice.envLevel);
            if (voice.envLevel >= 0x7FFF)
            {
                voice.phase = Phase::Decay;
                voice.env.reset(static_cast<uint8_t>(((voice.adsr1 >> 4u) & 0xFu) << 2u), true, true);
            }
            return;
        case Phase::Decay:
        {
            voice.envLevel = voice.env.tick(voice.envLevel);
            const int32_t sustainLevel = std::min<int32_t>(((voice.adsr1 & 0xFu) + 1u) * 0x800u, 0x7FFF);
            if (voice.envLevel <= sustainLevel)
            {
                voice.phase = Phase::Sustain;
                voice.env.reset(static_cast<uint8_t>((voice.adsr2 >> 6u) & 0x7Fu),
                                (voice.adsr2 & 0x4000u) != 0u,
                                (voice.adsr2 & 0x8000u) != 0u);
            }
            return;
        }
        case Phase::Sustain:
            voice.envLevel = voice.env.tick(voice.envLevel);
            return;
        case Phase::Release:
            voice.envLevel = voice.env.tick(voice.envLevel);
            if (voice.envLevel <= 0)
            {
                voice.envLevel = 0;
                voice.phase = Phase::Off;
            }
            return;
        }
    }

    int32_t IopSpu2::sampleVoice(Core &core, int voiceIndex, int32_t previousOutput)
    {
        Voice &voice = core.voices[voiceIndex];
        if (voice.phase == Phase::Off)
        {
            voice.lastOutput = 0;
            return 0;
        }
        if (!voice.blockLoaded)
            loadBlock(core, voice);

        // 4-point Hermite interpolation between samples[i+1] and samples[i+2].
        const uint32_t index = voice.counter >> 12u;
        const int32_t frac = static_cast<int32_t>(voice.counter & 0xFFFu);
        const int32_t p0 = voice.samples[index];
        const int32_t p1 = voice.samples[index + 1u];
        const int32_t p2 = voice.samples[index + 2u];
        const int32_t p3 = voice.samples[index + 3u];
        const float t = static_cast<float>(frac) / 4096.0f;
        const float c1 = 0.5f * static_cast<float>(p2 - p0);
        const float c2 = static_cast<float>(p0) - 2.5f * static_cast<float>(p1) + 2.0f * static_cast<float>(p2) - 0.5f * static_cast<float>(p3);
        const float c3 = 0.5f * static_cast<float>(p3 - p0) + 1.5f * static_cast<float>(p1 - p2);
        const int32_t sample = clamp16(static_cast<int32_t>(((c3 * t + c2) * t + c1) * t + static_cast<float>(p1)));

        const int32_t output = (sample * voice.envLevel) >> 15;
        voice.lastOutput = output;

        uint32_t step = voice.pitch;
        if (voiceIndex > 0 && (core.pmon & (1u << voiceIndex)) != 0u)
            step = static_cast<uint32_t>(std::max<int32_t>(0, (static_cast<int32_t>(step) * (0x8000 + previousOutput)) >> 15));
        step = std::min<uint32_t>(step, 0x3FFFu);

        voice.counter += step;
        if ((voice.counter >> 12u) >= 28u)
        {
            voice.counter -= 28u << 12u;
            voice.samples[0] = voice.samples[28];
            voice.samples[1] = voice.samples[29];
            voice.samples[2] = voice.samples[30];
            voice.blockLoaded = false;
            if ((voice.blockFlags & 1u) != 0u)
            {
                core.endx |= 1u << voiceIndex;
                voice.nax = voice.lsax;
                if ((voice.blockFlags & 2u) == 0u)
                {
                    voice.phase = Phase::Off;
                    voice.envLevel = 0;
                }
            }
            else
            {
                voice.nax = ((voice.nax & ~7u) + 8u) & kAddressMask;
            }
        }

        tickEnvelope(voice);
        voice.volL.tick();
        voice.volR.tick();
        return output;
    }

    void IopSpu2::generate(int16_t *out, uint32_t frames)
    {
        for (uint32_t frame = 0; frame < frames; ++frame)
        {
            int32_t coreOutL[2] = {0, 0};
            int32_t coreOutR[2] = {0, 0};
            for (int c = 0; c < 2; ++c)
            {
                Core &core = m_cores[c];
                int32_t dryL = 0;
                int32_t dryR = 0;
                int32_t previous = 0;
                for (int v = 0; v < 24; ++v)
                {
                    const int32_t output = sampleVoice(core, v, previous);
                    previous = output;
                    if (output == 0)
                        continue;
                    const Voice &voice = core.voices[v];
                    if ((core.vmixl & (1u << v)) != 0u)
                        dryL += (output * voice.volL.current) >> 15;
                    if ((core.vmixr & (1u << v)) != 0u)
                        dryR += (output * voice.volR.current) >> 15;
                }

                // MMIX: bit set routes the source to the dry output
                // (0x800/0x400 voices L/R, 0x80/0x40 memory input, 0x8/0x4 external = core 0).
                int32_t mixL = (core.mmix & 0x800u) != 0u ? dryL : 0;
                int32_t mixR = (core.mmix & 0x400u) != 0u ? dryR : 0;
                if (c == 1)
                {
                    if ((core.mmix & 0x8u) != 0u)
                        mixL += (coreOutL[0] * core.bvolL) >> 15;
                    if ((core.mmix & 0x4u) != 0u)
                        mixR += (coreOutR[0] * core.bvolR) >> 15;
                }
                core.mvolL.tick();
                core.mvolR.tick();
                coreOutL[c] = clamp16((clamp16(mixL) * core.mvolL.current) >> 15);
                coreOutR[c] = clamp16((clamp16(mixR) * core.mvolR.current) >> 15);
            }
            out[frame * 2u] = static_cast<int16_t>(coreOutL[1]);
            out[frame * 2u + 1u] = static_cast<int16_t>(coreOutR[1]);
        }
    }

    void IopSpu2::dmaWrite(int coreIndex, const uint8_t *source, uint32_t bytes)
    {
        Core &core = m_cores[coreIndex & 1];
        const uint32_t halfwords = bytes / 2u;
        checkIrq(core.tsa, halfwords);
        for (uint32_t i = 0; i < halfwords; ++i)
        {
            uint16_t value;
            std::memcpy(&value, source + i * 2u, sizeof(value));
            m_ram[core.tsa] = value;
            core.tsa = (core.tsa + 1u) & kAddressMask;
        }
        core.statx |= 0x80u;
    }

    void IopSpu2::dmaRead(int coreIndex, uint8_t *destination, uint32_t bytes)
    {
        Core &core = m_cores[coreIndex & 1];
        const uint32_t halfwords = bytes / 2u;
        checkIrq(core.tsa, halfwords);
        for (uint32_t i = 0; i < halfwords; ++i)
        {
            std::memcpy(destination + i * 2u, &m_ram[core.tsa], sizeof(uint16_t));
            core.tsa = (core.tsa + 1u) & kAddressMask;
        }
        core.statx |= 0x80u;
    }
}

