#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace ps2x::iop::lle
{
    // What the SPU2 needs from the IOP around it.
    class Spu2Host
    {
    public:
        virtual ~Spu2Host() = default;
        // AutoDMA wants its next 1 KiB block (512 bytes left, 512 bytes right).
        // Returns false while the core's DMA channel has nothing queued.
        virtual bool fetchAutoDma(int core, uint16_t *block) = 0;
        // The SPU2 interrupt line went up.
        virtual void raiseSpuInterrupt() = 0;
    };

    // The two SPU2 cores, their 48 voices and 2 MiB of sound memory, mixed
    // one 48 kHz sample at a time.
    class Spu2
    {
    public:
        static constexpr uint32_t kRamWords = 1u << 20;

        explicit Spu2(Spu2Host &host);

        void reset();
        uint16_t read(uint32_t offset);
        void write(uint32_t offset, uint16_t value);

        // Produce one output sample pair.
        void tick(int16_t &left, int16_t &right);

        // DMA transfers between IOP memory and sound memory at the core's TSA.
        void dmaWrite(int core, const uint16_t *data, uint32_t words);
        void dmaRead(int core, uint16_t *data, uint32_t words);
        bool autoDmaEnabled(int core) const;
        // A DMA channel was started or stopped for this core.
        void dmaStarted(int core);

        uint16_t *ram() { return m_ram.data(); }

    private:
        struct Envelope
        {
            int32_t level = 0;
            int32_t counter = 0;
            uint8_t phase = 0; // 0 off, 1 attack, 2 decay, 3 sustain, 4 release
        };

        struct Volume
        {
            uint16_t reg = 0;
            int32_t level = 0; // current value, -0x8000..0x7FFF
            int32_t counter = 0;
            void set(uint16_t value);
            void update();
        };

        struct Voice
        {
            Volume left, right;
            uint16_t pitch = 0;
            uint16_t adsr1 = 0, adsr2 = 0;
            Envelope envelope;
            uint32_t startAddress = 0;
            uint32_t loopAddress = 0;
            uint32_t nextAddress = 0; // address of the block being played
            bool loopAddressSet = false;
            uint32_t counter = 0;     // 12-bit fraction plus sample index
            std::array<int16_t, 28> decoded{};
            int32_t history[2]{};
            std::array<int16_t, 4> window{}; // last four samples for interpolation
            uint8_t sampleIndex = 28;         // 28 forces a block decode
            uint8_t blockFlags = 0;
            int32_t output = 0;               // post-envelope sample, for modulation
        };

        struct Core
        {
            std::array<Voice, 24> voices;
            uint32_t pitchMod = 0, noise = 0, dryL = 0, dryR = 0, wetL = 0, wetR = 0;
            uint16_t mmix = 0, attr = 0, admas = 0, statx = 0x80;
            uint32_t irqAddress = 0, transferAddress = 0, endx = 0;
            uint32_t effectStart = 0, effectEnd = 0;
            Volume masterL, masterR;
            int16_t effectL = 0, effectR = 0, inputL = 0, inputR = 0, externalL = 0, externalR = 0;
            uint32_t inputPos = 0;
            bool inputPrimed = false;
            int32_t noiseLevel = 0;
            uint32_t noiseCounter = 0;
            std::array<uint32_t, 22> reverbAddress{};
            std::array<int16_t, 10> reverbCoefficient{};
            uint32_t reverbPos = 0;
            int32_t reverbOutL = 0, reverbOutR = 0;
            bool reverbPhase = false;
        };

        uint16_t &raw(uint32_t offset) { return m_regs[(offset >> 1) & 0x3FFu]; }
        void writeCore(int core, uint32_t offset, uint16_t value);
        uint16_t readCore(int core, uint32_t offset);
        void keyOn(int core, uint32_t mask);
        void keyOff(int core, uint32_t mask);
        void checkIrq(uint32_t address);
        void decodeBlock(int core, Voice &voice);
        int32_t voiceSample(int core, int index);
        void stepEnvelope(Voice &voice);
        void stepNoise(Core &core);
        void readInput(int core);
        void mixReverb(int core, int32_t inL, int32_t inR, int32_t &outL, int32_t &outR);

        Spu2Host &m_host;
        std::vector<uint16_t> m_ram;
        std::array<uint16_t, 0x400> m_regs{};
        std::array<Core, 2> m_cores{};
        uint16_t m_irqInfo = 0;
        uint32_t m_outputPos = 0;
    };
}
