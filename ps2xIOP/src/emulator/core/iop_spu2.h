#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace ps2x::iop::detail
{
    struct AdpcmHistory
    {
        int32_t prev1 = 0;
        int32_t prev2 = 0;
    };

    // PS-ADPCM block: 8 little-endian halfwords; the first holds shift/filter (low byte) and loop flags
    // (high byte), the other seven pack 28 4-bit samples.
    void decodeAdpcmBlock(const uint16_t *block, int16_t *out, AdpcmHistory &history);

    // SPU2 (0x1F900000-0x1F9007FF): two cores of 24 ADPCM voices, 2 MB sound RAM, AutoDMA PCM input
    // and DMA channels 4/7. The disc's own libsd drives it, so voices, streams and IRQs behave as the
    // drivers expect. Reverb is not emulated: the effect return is left out of the mix.
    class IopSpu2
    {
    public:
        static constexpr uint32_t kBase = 0x1F900000u;
        static constexpr uint32_t kEnd = 0x1F900800u;
        static constexpr uint32_t kRamWords = 0x100000u; // 2 MB of 16-bit words
        static constexpr uint32_t kCyclesPerSample = 768u; // 36.864 MHz IOP clock / 48 kHz
        static constexpr uint32_t kSampleRate = 48000u;
        static constexpr uint32_t kVoicesPerCore = 24u;
        static constexpr uint32_t kAdmaBlockFrames = 256u;
        static constexpr int kIrq = 9;

        enum Event : uint32_t
        {
            EventIrq = 1u,
            EventDmaDone0 = 2u,
            EventDmaDone1 = 4u,
        };

        explicit IopSpu2(std::span<uint8_t> iopRam);

        void reset();
        [[nodiscard]] static bool contains(uint32_t phys) noexcept { return phys >= kBase && phys < kEnd; }
        [[nodiscard]] uint16_t read16(uint32_t phys);
        void write16(uint32_t phys, uint16_t value);

        // Starts DMA channel 4 (core 0) or 7 (core 1). Returns false for AutoDMA, which drains at the
        // playback rate and reports completion through takeEvents().
        bool startDma(uint32_t core, uint32_t madr, uint32_t bytes, bool toSpu);
        void stopAutoDma(uint32_t core) noexcept { m_cores[core & 1u].adma.active = false; }
        [[nodiscard]] bool autoDmaActive(uint32_t core) const noexcept { return m_cores[core & 1u].adma.active; }
        [[nodiscard]] uint32_t autoDmaAddress(uint32_t core) const noexcept { return m_cores[core & 1u].adma.address; }

        // Frames until an AutoDMA core pulls its next 256-frame block; DMA completions only happen there.
        [[nodiscard]] uint32_t framesUntilBlock() const noexcept { return kAdmaBlockFrames - m_admaFrame; }
        // Appends `frames` interleaved stereo samples.
        void generate(uint32_t frames, std::vector<int16_t> &out);
        [[nodiscard]] uint32_t takeEvents() noexcept;

        [[nodiscard]] std::span<uint16_t> ram() noexcept { return m_ram; }

    private:
        enum class Phase : uint8_t
        {
            Off,
            Attack,
            Decay,
            Sustain,
            Release,
        };

        struct Voice
        {
            uint16_t volumeLeftReg = 0u;
            uint16_t volumeRightReg = 0u;
            int32_t volumeLeft = 0;
            int32_t volumeRight = 0;
            uint16_t pitch = 0u;
            uint16_t adsr1 = 0u;
            uint16_t adsr2 = 0u;
            int32_t envelope = 0;
            uint32_t envelopeCounter = 0u;
            Phase phase = Phase::Off;

            uint32_t startAddress = 0u;
            uint32_t loopAddress = 0u;
            uint32_t nextAddress = 0u;
            uint32_t blockAddress = 0u;
            bool customLoop = false;
            bool fresh = true;
            uint8_t blockFlags = 0u;

            uint32_t pitchCounter = 0u;
            uint32_t sampleIndex = 28u;
            std::array<int16_t, 28> samples{};
            AdpcmHistory history;
            int32_t previous = 0;
            int32_t current = 0;
        };

        struct AutoDma
        {
            bool active = false;
            uint32_t address = 0u;
            uint32_t remaining = 0u;
        };

        struct Core
        {
            std::array<Voice, kVoicesPerCore> voices{};
            std::array<uint16_t, 0x200> regs{};
            uint32_t irqAddress = 0u;
            uint32_t transferAddress = 0u;
            uint32_t endFlags = 0u;
            uint16_t attr = 0u;
            uint16_t admas = 0u;
            uint16_t statx = 0u;
            AutoDma adma;
            std::array<int16_t, kAdmaBlockFrames> inputLeft{};
            std::array<int16_t, kAdmaBlockFrames> inputRight{};
            uint32_t admaHalf = 0u;
        };

        [[nodiscard]] uint16_t readCore(uint32_t core, uint32_t reg);
        void writeCore(uint32_t core, uint32_t reg, uint16_t value);
        void keyOn(Core &core, uint32_t mask);
        void keyOff(Core &core, uint32_t mask);
        void checkIrq(uint32_t core, uint32_t start, uint32_t count);
        void checkIrqAll(uint32_t start, uint32_t count);
        void loadBlock(uint32_t coreIndex, Voice &voice, uint32_t voiceIndex);
        int32_t nextSample(uint32_t coreIndex, Voice &voice, uint32_t voiceIndex);
        static void tickEnvelope(Voice &voice);
        void pullAdmaBlock(uint32_t coreIndex);
        // MVOL uses the voice volume format (15-bit, optional sweep); AVOL/BVOL are plain signed 16-bit.
        [[nodiscard]] int32_t globalVolume(uint32_t reg) const;
        [[nodiscard]] int32_t plainVolume(uint32_t reg) const;

        std::span<uint8_t> m_iopRam;
        std::vector<uint16_t> m_ram;
        std::array<Core, 2> m_cores{};
        std::array<uint16_t, 0x50> m_globals{}; // 0x760-0x7FF
        uint16_t m_irqInfo = 0u;
        uint32_t m_admaFrame = 0u;
        uint32_t m_events = 0u;
    };
}
