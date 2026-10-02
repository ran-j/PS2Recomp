#pragma once

#include <array>
#include <cstdint>
#include <optional>

namespace ps2x::iop::detail
{
    class IopMemory;

    struct IopCpuState
    {
        std::array<uint32_t, 32> gpr{};
        uint32_t hi = 0;
        uint32_t lo = 0;
        uint32_t pc = 0;
        std::array<uint32_t, 32> cop0{};
        uint32_t pendingLoadReg = 0;
        uint32_t pendingLoadValue = 0;
        bool pendingLoad = false;
        bool branchPending = false;
        uint32_t branchTarget = 0;
        bool stopped = false;
        bool yielded = false;
        bool exception = false;
    };

    class IopCpuCore
    {
    public:
        explicit IopCpuCore(IopMemory &memory) noexcept;

        [[nodiscard]] bool executeInstruction(IopCpuState &cpu);
        void raiseException(IopCpuState &cpu, uint32_t code, uint32_t faultPc, bool delaySlot, std::optional<uint32_t> badAddress = std::nullopt) const;

    private:
        static void writeRegister(IopCpuState &cpu, uint32_t reg, uint32_t value, uint32_t &writtenReg);
        static void scheduleLoad(uint32_t reg, uint32_t value, bool &scheduled, uint32_t &scheduledReg, uint32_t &scheduledValue);

        IopMemory &m_memory;
    };
}
