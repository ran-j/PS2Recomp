#pragma once
#include <cstdint>
#include <memory>
#include <functional>
#include "vu_state.h"

class PS2Memory;
struct R5900Context;
namespace ps2vu
{
    class Unit;
}

namespace ps2vu
{
    class Runtime
    {
    public:
        Runtime(PS2Memory &memory, std::function<void(uint32_t)> advanceClock);
        ~Runtime();
        Unit &unit(bool vu1) noexcept;
        void start0(R5900Context &context, uint32_t pc);
        void startVif0(R5900Context &context, uint32_t pc, uint32_t itop, bool resume);
        void start1(uint32_t pc, uint32_t top, uint32_t itop, bool resume);
        void advance(uint32_t cycles, R5900Context &context) noexcept;
        void beforeAccess(R5900Context &context, uint32_t instruction);
        void afterAccess(R5900Context &context, uint32_t instruction);
        void flush1(bool gif);
        void flush0(R5900Context &context);
        bool failed() const noexcept;
        void reportFailure() const;

    private:
        static bool submitGif(void *, const uint8_t *, uint32_t) noexcept;
        static bool readShared(void *, uint64_t, uint32_t, uint32_t *) noexcept;
        static bool writeShared(void *, uint64_t, uint32_t, const uint32_t *, uint8_t) noexcept;
        void advanceUnit(bool vu1, uint64_t cycle) noexcept;
        void run(bool vu1, uint32_t cycles);
        void wait(bool vu1);
        PS2Memory &m_memory;
        std::function<void(uint32_t)> m_advanceClock;
        uint64_t m_cycle = 0;
        R5900Context *m_accessContext = nullptr;
        bool m_contextDirty = false;
        std::unique_ptr<Unit> m_units[2];
    };

    void importContext(const R5900Context &context, VU1State &state) noexcept;
    void exportContext(const VU1State &state, R5900Context &context) noexcept;
}
