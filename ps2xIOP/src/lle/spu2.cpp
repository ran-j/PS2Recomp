#include "spu2.h"

#include <algorithm>
#include <cstring>

namespace ps2x::iop::lle
{
    namespace
    {
        constexpr uint32_t kAddressMask = Spu2::kRamWords - 1u;
        constexpr int32_t kFilters[16][2] = {
            {0, 0}, {60, 0}, {115, -52}, {98, -55}, {122, -60},
        };

        inline int32_t clamp16(int32_t value) { return std::clamp(value, -0x8000, 0x7FFF); }
        inline int32_t scale(int32_t value, int32_t volume) { return (value * volume) >> 15; }

        // Core register offsets, relative to the core's 0x400 byte window.
        enum : uint32_t
        {
            kPitchModL = 0x180, kPitchModH = 0x182, kNoiseL = 0x184, kNoiseH = 0x186,
            kDryLL = 0x188, kDryLH = 0x18A, kWetLL = 0x18C, kWetLH = 0x18E,
            kDryRL = 0x190, kDryRH = 0x192, kWetRL = 0x194, kWetRH = 0x196,
            kMmix = 0x198, kAttr = 0x19A, kIrqAH = 0x19C, kIrqAL = 0x19E,
            kKeyOnL = 0x1A0, kKeyOnH = 0x1A2, kKeyOffL = 0x1A4, kKeyOffH = 0x1A6,
            kTsaH = 0x1A8, kTsaL = 0x1AA, kData = 0x1AC, kAdmas = 0x1B0,
            kVoiceAddresses = 0x1C0, kEsaH = 0x2E0, kEsaL = 0x2E2,
            kReverbAddresses = 0x2E4, kEea = 0x33C, kEndxL = 0x340, kEndxH = 0x342, kStatx = 0x344,
        };
        // Per-core volume block: 0x760 for core 0, 0x788 for core 1. AVOL is
        // the external input (core 0's output, on core 1), BVOL the core's
        // own sound data input fed by AutoDMA.
        enum : uint32_t
        {
            kMvolL = 0x00, kMvolR = 0x02, kEvolL = 0x04, kEvolR = 0x06, kAvolL = 0x08, kAvolR = 0x0A,
            kBvolL = 0x0C, kBvolR = 0x0E, kMvolXL = 0x10, kMvolXR = 0x12, kReverbCoefficients = 0x14,
        };
        constexpr uint32_t kIrqInfo = 0x7C2;

        // Reverb register indices into Core::reverb: addresses then coefficients.
        enum
        {
            kApf1Size, kApf2Size, kLSameDst, kRSameDst, kLComb1, kRComb1, kLComb2, kRComb2,
            kLSameSrc, kRSameSrc, kLDiffDst, kRDiffDst, kLComb3, kRComb3, kLComb4, kRComb4,
            kLDiffSrc, kRDiffSrc, kLApf1, kRApf1, kLApf2, kRApf2,
            kIir, kComb1, kComb2, kComb3, kComb4, kWall, kApf1, kApf2, kInL, kInR,
        };
    }

    void Spu2::Volume::set(uint16_t value)
    {
        reg = value;
        if ((value & 0x8000u) == 0u)
        {
            // Fixed volume: a signed 15-bit value, doubled.
            level = static_cast<int16_t>(static_cast<uint16_t>(value << 1));
            counter = 0;
        }
    }

    void Spu2::Volume::update()
    {
        if ((reg & 0x8000u) == 0u)
            return;
        // Sweep: bit 14 exponential, bit 13 decrease, bits 6-0 rate.
        const bool exponential = (reg & 0x4000u) != 0u;
        const bool decrease = (reg & 0x2000u) != 0u;
        const int32_t rate = reg & 0x7F;
        const int32_t shift = rate >> 2;
        int32_t step = decrease ? -8 + (rate & 3) : 7 - (rate & 3);
        const int32_t cycles = 1 << std::max(0, shift - 11);
        step <<= std::max(0, 11 - shift);
        int32_t magnitude = std::abs(level);
        if (exponential && decrease)
            step = (step * magnitude) >> 15;
        if (++counter < cycles * (exponential && !decrease && magnitude > 0x6000 ? 4 : 1))
            return;
        counter = 0;
        magnitude = std::clamp(magnitude + step, 0, 0x7FFF);
        level = (reg & 0x1000u) != 0u ? -magnitude : magnitude;
    }

    Spu2::Spu2(Spu2Host &host) : m_host(host), m_ram(kRamWords, 0u)
    {
        reset();
    }

    void Spu2::reset()
    {
        std::fill(m_ram.begin(), m_ram.end(), 0u);
        m_regs.fill(0u);
        m_cores = {};
        m_irqInfo = 0;
        m_outputPos = 0;
    }

    void Spu2::checkIrq(uint32_t address)
    {
        for (int core = 0; core < 2; ++core)
        {
            Core &c = m_cores[core];
            if ((c.attr & 0x40u) != 0u && c.irqAddress == (address & kAddressMask) &&
                (m_irqInfo & (4u << core)) == 0u)
            {
                m_irqInfo |= static_cast<uint16_t>(4u << core);
                m_host.raiseSpuInterrupt();
            }
        }
    }

    void Spu2::keyOn(int core, uint32_t mask)
    {
        Core &c = m_cores[core];
        c.endx &= ~mask;
        for (int index = 0; index < 24; ++index)
        {
            if ((mask & (1u << index)) == 0u)
                continue;
            Voice &v = c.voices[index];
            v.nextAddress = v.startAddress;
            v.loopAddressSet = false;
            v.counter = 0;
            v.sampleIndex = 28;
            v.history[0] = v.history[1] = 0;
            v.window = {};
            v.blockFlags = 0;
            v.envelope = {0, 0, 1};
        }
    }

    void Spu2::keyOff(int core, uint32_t mask)
    {
        for (int index = 0; index < 24; ++index)
        {
            Voice &v = m_cores[core].voices[index];
            if ((mask & (1u << index)) != 0u && v.envelope.phase != 0u)
            {
                v.envelope.phase = 4;
                v.envelope.counter = 0;
            }
        }
    }

    void Spu2::decodeBlock(int core, Voice &v)
    {
        const uint32_t base = v.nextAddress & kAddressMask & ~7u;
        for (uint32_t offset = 0; offset < 8u; ++offset)
            checkIrq(base + offset);
        const uint16_t header = m_ram[base];
        v.blockFlags = static_cast<uint8_t>(header >> 8);
        if ((v.blockFlags & 4u) != 0u && !v.loopAddressSet)
            v.loopAddress = base;
        int32_t shift = header & 0xF;
        if (shift > 12)
            shift = 9;
        const int32_t *filter = kFilters[(header >> 4) & 0xF];
        for (uint32_t index = 0; index < 28u; ++index)
        {
            const uint16_t word = m_ram[(base + 1u + index / 4u) & kAddressMask];
            const int32_t nibble = static_cast<int16_t>(static_cast<uint16_t>(((word >> ((index & 3u) * 4u)) & 0xFu) << 12));
            const int32_t sample = clamp16((nibble >> shift) +
                                           ((v.history[0] * filter[0] + v.history[1] * filter[1] + 32) >> 6));
            v.history[1] = v.history[0];
            v.history[0] = sample;
            v.decoded[index] = static_cast<int16_t>(sample);
        }
        (void)core;
    }

    int32_t Spu2::voiceSample(int core, int index)
    {
        Core &c = m_cores[core];
        Voice &v = c.voices[index];

        int32_t pitch = v.pitch;
        if (index > 0 && (c.pitchMod & (1u << index)) != 0u)
            pitch = std::clamp((pitch * (0x8000 + c.voices[index - 1].output)) >> 15, 0, 0x3FFF);
        v.counter += static_cast<uint32_t>(std::min(pitch, 0x3FFF));
        while (v.counter >= 0x1000u)
        {
            v.counter -= 0x1000u;
            if (v.sampleIndex >= 28u)
            {
                if (v.sampleIndex != 29u)
                    decodeBlock(core, v);
                v.sampleIndex = 0;
            }
            v.window[0] = v.window[1];
            v.window[1] = v.window[2];
            v.window[2] = v.window[3];
            v.window[3] = v.decoded[v.sampleIndex++];
            if (v.sampleIndex == 28u)
            {
                // The block's flags apply once its last sample has been read.
                if ((v.blockFlags & 1u) != 0u)
                {
                    c.endx |= 1u << index;
                    v.nextAddress = v.loopAddress;
                    if ((v.blockFlags & 2u) == 0u)
                    {
                        v.envelope.phase = 0;
                        v.envelope.level = 0;
                    }
                }
                else
                    v.nextAddress = (v.nextAddress + 8u) & kAddressMask;
            }
        }

        // Four-point Catmull-Rom between the last two samples.
        const int32_t t = static_cast<int32_t>(v.counter);
        const int32_t p0 = v.window[0], p1 = v.window[1], p2 = v.window[2], p3 = v.window[3];
        const int64_t a = -p0 + 3 * p1 - 3 * p2 + p3;
        const int64_t b = 2 * p0 - 5 * p1 + 4 * p2 - p3;
        const int64_t d = -p0 + p2;
        const int64_t value = ((((a * t >> 12) + b) * t >> 12) + d) * t >> 13;
        return clamp16(static_cast<int32_t>(value) + p1);
    }

    void Spu2::stepEnvelope(Voice &v)
    {
        Envelope &e = v.envelope;
        int32_t rate = 0;
        bool decrease = false, exponential = false;
        switch (e.phase)
        {
        case 1:
            rate = (v.adsr1 >> 8) & 0x7F;
            exponential = (v.adsr1 & 0x8000u) != 0u;
            break;
        case 2:
            rate = ((v.adsr1 >> 4) & 0xF) << 2;
            decrease = exponential = true;
            break;
        case 3:
            rate = (v.adsr2 >> 6) & 0x7F;
            decrease = (v.adsr2 & 0x4000u) != 0u;
            exponential = (v.adsr2 & 0x8000u) != 0u;
            break;
        case 4:
            rate = (v.adsr2 & 0x1F) << 2;
            decrease = true;
            exponential = (v.adsr2 & 0x20u) != 0u;
            break;
        default:
            return;
        }
        const int32_t shift = rate >> 2;
        int32_t step = decrease ? -8 + (rate & 3) : 7 - (rate & 3);
        int32_t cycles = 1 << std::max(0, shift - 11);
        step <<= std::max(0, 11 - shift);
        if (exponential && !decrease && e.level > 0x6000)
            cycles *= 4;
        if (exponential && decrease)
            step = (step * e.level) >> 15;
        if (++e.counter >= cycles)
        {
            e.counter = 0;
            e.level = std::clamp(e.level + step, 0, 0x7FFF);
        }
        switch (e.phase)
        {
        case 1:
            if (e.level >= 0x7FFF)
                e.phase = 2;
            break;
        case 2:
            if (e.level <= ((v.adsr1 & 0xF) + 1) * 0x800)
                e.phase = 3;
            break;
        case 4:
            if (e.level == 0)
                e.phase = 0;
            break;
        default:
            break;
        }
    }

    void Spu2::stepNoise(Core &c)
    {
        // The PS1-style noise generator, clocked by ATTR bits 8-13.
        const uint32_t clock = (c.attr >> 8) & 0x3Fu;
        const uint32_t shift = clock >> 2;
        const uint32_t step = 4u + (clock & 3u);
        c.noiseCounter += step << (15u - std::min(shift, 15u));
        while (c.noiseCounter >= 0x20000u)
        {
            c.noiseCounter -= 0x20000u;
            const uint32_t bits = static_cast<uint32_t>(c.noiseLevel);
            const uint32_t feedback = ((bits >> 15) ^ (bits >> 12) ^ (bits >> 11) ^ (bits >> 10) ^ 1u) & 1u;
            c.noiseLevel = static_cast<int16_t>(static_cast<uint16_t>((bits << 1) | feedback));
        }
    }

    void Spu2::readInput(int core)
    {
        Core &c = m_cores[core];
        const uint32_t base = 0x2000u + (static_cast<uint32_t>(core) << 10);
        checkIrq(base + c.inputPos);
        checkIrq(base + 0x200u + c.inputPos);
        if ((c.admas & (1u << core)) == 0u)
        {
            c.inputL = c.inputR = 0;
            return;
        }
        if (!c.inputPrimed)
        {
            // Once AutoDMA is on, the first transfer fills both halves before
            // anything plays; until it starts the input stays silent.
            uint16_t block[512];
            for (uint32_t half = 0; half < 2u; ++half)
            {
                if (!m_host.fetchAutoDma(core, block))
                    break;
                std::memcpy(&m_ram[base + half * 0x100u], block, 0x200u);
                std::memcpy(&m_ram[base + 0x200u + half * 0x100u], block + 256, 0x200u);
                c.inputPrimed = true;
            }
            if (!c.inputPrimed)
            {
                c.inputL = c.inputR = 0;
                return;
            }
            c.inputPos = 0;
        }
        c.inputL = static_cast<int16_t>(m_ram[base + c.inputPos]);
        c.inputR = static_cast<int16_t>(m_ram[base + 0x200u + c.inputPos]);
        c.inputPos = (c.inputPos + 1u) & 0x1FFu;
        if ((c.inputPos & 0xFFu) == 0u)
        {
            // The half just finished playing takes the next block.
            const uint32_t half = c.inputPos == 0u ? 0x100u : 0u;
            uint16_t block[512];
            if (m_host.fetchAutoDma(core, block))
            {
                std::memcpy(&m_ram[base + half], block, 0x200u);
                std::memcpy(&m_ram[base + 0x200u + half], block + 256, 0x200u);
            }
        }
    }

    void Spu2::mixReverb(int core, int32_t inL, int32_t inR, int32_t &outL, int32_t &outR)
    {
        Core &c = m_cores[core];
        // The reverb runs at half rate on the average of each pair of inputs.
        c.reverbPhase = !c.reverbPhase;
        if (c.reverbPhase || (c.attr & 0x80u) == 0u || c.effectEnd <= c.effectStart)
        {
            outL = c.reverbOutL;
            outR = c.reverbOutR;
            if ((c.attr & 0x80u) == 0u)
                outL = outR = 0;
            return;
        }
        const uint32_t size = c.effectEnd - c.effectStart + 1u;
        const auto address = [&](uint32_t offset) {
            return c.effectStart + (c.reverbPos + offset) % size;
        };
        const auto readAt = [&](uint32_t offset) {
            return static_cast<int32_t>(static_cast<int16_t>(m_ram[address(offset) & kAddressMask]));
        };
        const auto writeAt = [&](uint32_t offset, int32_t value) {
            m_ram[address(offset) & kAddressMask] = static_cast<uint16_t>(clamp16(value));
        };
        const auto reg = [&](int index) { return c.reverbAddress[index]; };
        const auto coef = [&](int index) { return static_cast<int32_t>(c.reverbCoefficient[index - kIir]); };

        const int32_t lin = scale(inL, coef(kInL));
        const int32_t rin = scale(inR, coef(kInR));
        const int32_t iir = coef(kIir), wall = coef(kWall);
        const auto reflect = [&](int dst, int src, int32_t input) {
            const int32_t previous = readAt(reg(dst) - 1u);
            writeAt(reg(dst), scale(input + scale(readAt(reg(src)), wall) - previous, iir) + previous);
        };
        reflect(kLSameDst, kLSameSrc, lin);
        reflect(kRSameDst, kRSameSrc, rin);
        reflect(kLDiffDst, kRDiffSrc, lin);
        reflect(kRDiffDst, kLDiffSrc, rin);

        int32_t l = scale(readAt(reg(kLComb1)), coef(kComb1)) + scale(readAt(reg(kLComb2)), coef(kComb2)) +
                    scale(readAt(reg(kLComb3)), coef(kComb3)) + scale(readAt(reg(kLComb4)), coef(kComb4));
        int32_t r = scale(readAt(reg(kRComb1)), coef(kComb1)) + scale(readAt(reg(kRComb2)), coef(kComb2)) +
                    scale(readAt(reg(kRComb3)), coef(kComb3)) + scale(readAt(reg(kRComb4)), coef(kComb4));
        const auto allPass = [&](int32_t value, int dst, int size, int coefficient) {
            const int32_t delayed = readAt(reg(dst) - reg(size));
            value = clamp16(value - scale(delayed, coef(coefficient)));
            writeAt(reg(dst), value);
            return clamp16(scale(value, coef(coefficient)) + delayed);
        };
        l = allPass(clamp16(l), kLApf1, kApf1Size, kApf1);
        r = allPass(clamp16(r), kRApf1, kApf1Size, kApf1);
        l = allPass(l, kLApf2, kApf2Size, kApf2);
        r = allPass(r, kRApf2, kApf2Size, kApf2);
        c.reverbOutL = outL = l;
        c.reverbOutR = outR = r;
        c.reverbPos = (c.reverbPos + 1u) % size;
    }

    void Spu2::tick(int16_t &left, int16_t &right)
    {
        int32_t external[2] = {0, 0};
        int32_t out[2] = {0, 0};
        for (int core = 0; core < 2; ++core)
        {
            Core &c = m_cores[core];
            stepNoise(c);
            int32_t dry[2] = {0, 0}, wet[2] = {0, 0};
            for (int index = 0; index < 24; ++index)
            {
                Voice &v = c.voices[index];
                v.left.update();
                v.right.update();
                if (v.envelope.phase == 0u)
                {
                    v.output = 0;
                    continue;
                }
                int32_t sample = (c.noise & (1u << index)) != 0u ? c.noiseLevel : voiceSample(core, index);
                stepEnvelope(v);
                sample = scale(sample, v.envelope.level);
                v.output = sample;
                const int32_t l = scale(sample, v.left.level);
                const int32_t r = scale(sample, v.right.level);
                const uint32_t bit = 1u << index;
                if ((c.dryL & bit) != 0u) dry[0] += l;
                if ((c.dryR & bit) != 0u) dry[1] += r;
                if ((c.wetL & bit) != 0u) wet[0] += l;
                if ((c.wetR & bit) != 0u) wet[1] += r;
            }
            dry[0] = clamp16(dry[0]);
            dry[1] = clamp16(dry[1]);
            wet[0] = clamp16(wet[0]);
            wet[1] = clamp16(wet[1]);

            readInput(core);
            const uint32_t volumes = 0x760u + static_cast<uint32_t>(core) * 0x28u;
            const auto vol = [&](uint32_t offset) { return static_cast<int32_t>(static_cast<int16_t>(raw(volumes + offset))); };
            const int32_t input[2] = {scale(c.inputL, vol(kBvolL)), scale(c.inputR, vol(kBvolR))};
            const int32_t ext[2] = {core == 1 ? scale(external[0], vol(kAvolL)) : 0,
                                    core == 1 ? scale(external[1], vol(kAvolR)) : 0};

            // MMIX gates, from bit 11 down: voice dry L/R, voice wet L/R,
            // input dry L/R, input wet L/R, external dry L/R, external wet L/R.
            const uint16_t gates = core == 0 ? c.mmix & 0xFF0u : c.mmix;
            const auto gate = [&](uint32_t bit, int32_t value) { return (gates & bit) != 0u ? value : 0; };
            int32_t mixL = gate(0x800u, dry[0]) + gate(0x080u, input[0]) + gate(0x008u, ext[0]);
            int32_t mixR = gate(0x400u, dry[1]) + gate(0x040u, input[1]) + gate(0x004u, ext[1]);
            const int32_t sendL = clamp16(gate(0x200u, wet[0]) + gate(0x020u, input[0]) + gate(0x002u, ext[0]));
            const int32_t sendR = clamp16(gate(0x100u, wet[1]) + gate(0x010u, input[1]) + gate(0x001u, ext[1]));
            int32_t reverbL = 0, reverbR = 0;
            mixReverb(core, sendL, sendR, reverbL, reverbR);
            mixL += scale(reverbL, vol(kEvolL));
            mixR += scale(reverbR, vol(kEvolR));

            c.masterL.update();
            c.masterR.update();
            out[0] = clamp16(scale(clamp16(mixL), c.masterL.level));
            out[1] = clamp16(scale(clamp16(mixR), c.masterR.level));
            if (core == 0)
            {
                external[0] = out[0];
                external[1] = out[1];
                // Core 0's output area, which core 1 and captures read back.
                m_ram[0x800u + m_outputPos] = static_cast<uint16_t>(out[0]);
                m_ram[0xA00u + m_outputPos] = static_cast<uint16_t>(out[1]);
            }
        }
        m_outputPos = (m_outputPos + 1u) & 0x1FFu;
        left = static_cast<int16_t>(out[0]);
        right = static_cast<int16_t>(out[1]);
    }

    void Spu2::dmaWrite(int core, const uint16_t *data, uint32_t words)
    {
        Core &c = m_cores[core];
        for (uint32_t index = 0; index < words; ++index)
        {
            checkIrq(c.transferAddress);
            m_ram[c.transferAddress] = data[index];
            c.transferAddress = (c.transferAddress + 1u) & kAddressMask;
        }
        // Bit 7 reports a finished transfer; LIBSD's DMA handler waits for it.
        c.statx |= 0x80u;
    }

    void Spu2::dmaRead(int core, uint16_t *data, uint32_t words)
    {
        Core &c = m_cores[core];
        for (uint32_t index = 0; index < words; ++index)
        {
            checkIrq(c.transferAddress);
            data[index] = m_ram[c.transferAddress];
            c.transferAddress = (c.transferAddress + 1u) & kAddressMask;
        }
        c.statx |= 0x80u;
    }

    bool Spu2::autoDmaEnabled(int core) const
    {
        return (m_cores[core].admas & (1u << core)) != 0u;
    }

    void Spu2::dmaStarted(int core)
    {
        // Drivers restart the DMA for each half of their buffer while AutoDMA
        // keeps playing, so a restart must not touch the input position.
        Core &c = m_cores[core];
        c.statx &= ~0x80u;
        if (autoDmaEnabled(core))
            c.statx |= 0x80u;
    }

    uint16_t Spu2::readCore(int core, uint32_t offset)
    {
        Core &c = m_cores[core];
        if (offset < 0x180u)
        {
            const Voice &v = c.voices[offset >> 4];
            switch (offset & 0xFu)
            {
            case 0xA: return static_cast<uint16_t>(v.envelope.level);
            case 0xC: return static_cast<uint16_t>(v.left.level);
            case 0xE: return static_cast<uint16_t>(v.right.level);
            default: break;
            }
        }
        else if (offset >= kVoiceAddresses && offset < kVoiceAddresses + 24u * 0xCu)
        {
            const uint32_t relative = offset - kVoiceAddresses;
            const Voice &v = c.voices[relative / 0xCu];
            if (relative % 0xCu == 8u)
                return static_cast<uint16_t>(v.nextAddress >> 16);
            if (relative % 0xCu == 10u)
                return static_cast<uint16_t>(v.nextAddress);
        }
        switch (offset)
        {
        case kEndxL: return static_cast<uint16_t>(c.endx);
        case kEndxH: return static_cast<uint16_t>(c.endx >> 16);
        case kStatx: return c.statx;
        case kAdmas: return c.admas;
        case kTsaH: return static_cast<uint16_t>(c.transferAddress >> 16);
        case kTsaL: return static_cast<uint16_t>(c.transferAddress);
        case kAttr: return c.attr;
        default: break;
        }
        return raw(static_cast<uint32_t>(core) * 0x400u + offset);
    }

    uint16_t Spu2::read(uint32_t offset)
    {
        offset &= 0x7FEu;
        if (offset == kIrqInfo)
            return m_irqInfo;
        if (offset >= 0x760u && offset < 0x7B0u)
        {
            const int core = offset >= 0x788u ? 1 : 0;
            const uint32_t relative = offset - 0x760u - static_cast<uint32_t>(core) * 0x28u;
            if (relative == kMvolXL)
                return static_cast<uint16_t>(m_cores[core].masterL.level);
            if (relative == kMvolXR)
                return static_cast<uint16_t>(m_cores[core].masterR.level);
            return raw(offset);
        }
        if (offset < 0x760u)
            return readCore(offset >= 0x400u ? 1 : 0, offset & 0x3FFu);
        return raw(offset);
    }

    void Spu2::writeCore(int core, uint32_t offset, uint16_t value)
    {
        Core &c = m_cores[core];
        const auto high = [](uint32_t &target, uint16_t v) { target = (target & 0xFFFFu) | ((v & 0xFu) << 16); };
        const auto low = [](uint32_t &target, uint16_t v) { target = (target & 0xF0000u) | v; };
        const auto maskLow = [](uint32_t &target, uint16_t v) { target = (target & 0xFF0000u) | v; };
        const auto maskHigh = [](uint32_t &target, uint16_t v) { target = (target & 0xFFFFu) | ((v & 0xFFu) << 16); };
        if (offset < 0x180u)
        {
            Voice &v = c.voices[offset >> 4];
            switch (offset & 0xFu)
            {
            case 0x0: v.left.set(value); break;
            case 0x2: v.right.set(value); break;
            case 0x4: v.pitch = value; break;
            case 0x6: v.adsr1 = value; break;
            case 0x8: v.adsr2 = value; break;
            default: break;
            }
            return;
        }
        if (offset >= kVoiceAddresses && offset < kVoiceAddresses + 24u * 0xCu)
        {
            const uint32_t relative = offset - kVoiceAddresses;
            Voice &v = c.voices[relative / 0xCu];
            switch (relative % 0xCu)
            {
            case 0: high(v.startAddress, value); break;
            case 2: low(v.startAddress, value); break;
            case 4: high(v.loopAddress, value); v.loopAddressSet = true; break;
            case 6: low(v.loopAddress, value); v.loopAddressSet = true; break;
            case 8: high(v.nextAddress, value); break;
            case 10: low(v.nextAddress, value); break;
            default: break;
            }
            return;
        }
        if (offset >= kReverbAddresses && offset < kEea)
        {
            // Twenty-bit offsets into the effect area, high word first.
            uint32_t &address = c.reverbAddress[(offset - kReverbAddresses) >> 2];
            if ((offset & 2u) == 0u)
                high(address, value);
            else
                low(address, value);
            return;
        }
        switch (offset)
        {
        case kPitchModL: maskLow(c.pitchMod, value); break;
        case kPitchModH: maskHigh(c.pitchMod, value); break;
        case kNoiseL: maskLow(c.noise, value); break;
        case kNoiseH: maskHigh(c.noise, value); break;
        case kDryLL: maskLow(c.dryL, value); break;
        case kDryLH: maskHigh(c.dryL, value); break;
        case kWetLL: maskLow(c.wetL, value); break;
        case kWetLH: maskHigh(c.wetL, value); break;
        case kDryRL: maskLow(c.dryR, value); break;
        case kDryRH: maskHigh(c.dryR, value); break;
        case kWetRL: maskLow(c.wetR, value); break;
        case kWetRH: maskHigh(c.wetR, value); break;
        case kMmix: c.mmix = value; break;
        case kAttr:
        {
            const bool irqWasEnabled = (c.attr & 0x40u) != 0u;
            c.attr = value;
            // Clearing IRQ enable acknowledges the core's pending interrupt.
            if (irqWasEnabled && (value & 0x40u) == 0u)
                m_irqInfo &= static_cast<uint16_t>(~(4u << core));
            if ((value & 0x30u) == 0u)
                c.statx &= ~0x80u;
            break;
        }
        case kIrqAH: high(c.irqAddress, value); break;
        case kIrqAL: low(c.irqAddress, value); break;
        case kKeyOnL: keyOn(core, value); break;
        case kKeyOnH: keyOn(core, static_cast<uint32_t>(value & 0xFFu) << 16); break;
        case kKeyOffL: keyOff(core, value); break;
        case kKeyOffH: keyOff(core, static_cast<uint32_t>(value & 0xFFu) << 16); break;
        case kTsaH: high(c.transferAddress, value); break;
        case kTsaL: low(c.transferAddress, value); break;
        case kData:
            checkIrq(c.transferAddress);
            m_ram[c.transferAddress] = value;
            c.transferAddress = (c.transferAddress + 1u) & kAddressMask;
            break;
        case kAdmas:
            c.admas = value;
            if ((value & 3u) == 0u)
                c.inputPrimed = false;
            break;
        case kEsaH: high(c.effectStart, value); c.reverbPos = 0; break;
        case kEsaL: low(c.effectStart, value); c.reverbPos = 0; break;
        case kEea: c.effectEnd = (static_cast<uint32_t>(value & 0xFu) << 16) | 0xFFFFu; break;
        case kEndxL: case kEndxH: break;
        default: break;
        }
    }

    void Spu2::write(uint32_t offset, uint16_t value)
    {
        offset &= 0x7FEu;
        raw(offset) = value;
        if (offset == kIrqInfo)
            return;
        if (offset >= 0x760u && offset < 0x7B0u)
        {
            const int core = offset >= 0x788u ? 1 : 0;
            const uint32_t relative = offset - 0x760u - static_cast<uint32_t>(core) * 0x28u;
            Core &c = m_cores[core];
            if (relative == kMvolL)
                c.masterL.set(value);
            else if (relative == kMvolR)
                c.masterR.set(value);
            else if (relative >= kReverbCoefficients && relative < kReverbCoefficients + 20u)
                c.reverbCoefficient[(relative - kReverbCoefficients) / 2u] = static_cast<int16_t>(value);
            return;
        }
        if (offset < 0x760u)
            writeCore(offset >= 0x400u ? 1 : 0, offset & 0x3FFu, value);
    }
}
