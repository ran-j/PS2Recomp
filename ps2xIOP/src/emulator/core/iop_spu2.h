#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace ps2x::iop::detail
{
    // SPU2 sound processor: two cores of 24 ADPCM voices each, 2 MB of sound RAM, the
    // register file at 0x1F900000 and DMA channels 4/7. Produces 48 kHz stereo output.
    // Reverb, noise and the AutoDMA (PCM block streaming) input are not emulated yet.
    class IopSpu2
    {
    public:
        static constexpr uint32_t RamHalfwords = 1u << 20u;
        static constexpr uint32_t RegisterBytes = 0x800u;
        static constexpr uint32_t SampleRate = 48000u;
        static constexpr uint32_t IopCyclesPerSample = 768u; // 36.864 MHz / 48 kHz

        IopSpu2();

        void reset();

        // `offset` is the byte offset from 0x1F900000.
        [[nodiscard]] uint16_t read16(uint32_t offset) const;
        void write16(uint32_t offset, uint16_t value);

        void dmaWrite(int core, const uint8_t *source, uint32_t bytes);
        void dmaRead(int core, uint8_t *destination, uint32_t bytes);

        // Mixes `frames` stereo samples into `out` (interleaved L/R).
        void generate(int16_t *out, uint32_t frames);

        // True once after a voice, DMA or data-port access hit an enabled IRQA.
        [[nodiscard]] bool takeIrq() noexcept;

    private:
        enum class Phase : uint8_t
        {
            Off,
            Attack,
            Decay,
            Sustain,
            Release,
        };

        struct Envelope
        {
            uint32_t counter = 0u;
            uint32_t increment = 0x8000u;
            int32_t step = 0;
            uint8_t rate = 0u;
            bool decreasing = false;
            bool exponential = false;

            void reset(uint8_t rate, bool decreasing, bool exponential);
            [[nodiscard]] int32_t tick(int32_t level);
        };

        struct Volume
        {
            uint16_t reg = 0u;
            int32_t current = 0;
            Envelope sweep;

            void write(uint16_t value);
            void tick();
        };

        struct Voice
        {
            Volume volL;
            Volume volR;
            uint16_t pitch = 0u;
            uint16_t adsr1 = 0u;
            uint16_t adsr2 = 0u;
            uint32_t ssa = 0u;
            uint32_t lsax = 0u;
            uint32_t nax = 0u;

            Phase phase = Phase::Off;
            int32_t envLevel = 0;
            Envelope env;

            uint32_t counter = 0u;
            bool blockLoaded = false;
            uint8_t blockFlags = 0u;
            int32_t adpcm1 = 0;
            int32_t adpcm2 = 0;
            // [0..2] = last samples of the previous block, [3..30] = current block.
            std::array<int16_t, 31> samples{};
            int32_t lastOutput = 0;
        };

        struct Core
        {
            std::array<Voice, 24> voices;
            uint32_t pmon = 0u;
            uint32_t non = 0u;
            uint32_t vmixl = 0u;
            uint32_t vmixr = 0u;
            uint32_t vmixel = 0u;
            uint32_t vmixer = 0u;
            uint16_t mmix = 0u;
            uint16_t attr = 0u;
            uint32_t irqa = 0u;
            uint32_t tsa = 0u;
            uint32_t endx = 0u;
            uint16_t statx = 0u;
            uint16_t admas = 0u;
            Volume mvolL;
            Volume mvolR;
            int16_t avolL = 0;
            int16_t avolR = 0;
            int16_t bvolL = 0;
            int16_t bvolR = 0;
            int16_t evolL = 0;
            int16_t evolR = 0;
        };

        void writeVoiceParam(Core &core, Voice &voice, uint32_t reg, uint16_t value);
        [[nodiscard]] uint16_t readVoiceParam(const Voice &voice, uint32_t reg) const;
        void writeVoiceAddress(Voice &voice, uint32_t reg, uint16_t value);
        [[nodiscard]] uint16_t readVoiceAddress(const Voice &voice, uint32_t reg) const;
        void writeCoreRegister(int coreIndex, uint32_t offset, uint16_t value);
        [[nodiscard]] bool readCoreRegister(int coreIndex, uint32_t offset, uint16_t &value) const;

        void keyOn(Core &core, uint32_t mask);
        void keyOff(Core &core, uint32_t mask);
        void loadBlock(Core &core, Voice &voice);
        [[nodiscard]] int32_t sampleVoice(Core &core, int voiceIndex, int32_t previousOutput);
        void tickEnvelope(Voice &voice);
        void checkIrq(uint32_t begin, uint32_t count);

        std::vector<uint16_t> m_ram;
        std::array<uint16_t, RegisterBytes / 2u> m_regs{};
        std::array<Core, 2> m_cores{};
        bool m_irqPending = false;
    };
}
