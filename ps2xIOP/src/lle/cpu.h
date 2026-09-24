#pragma once

#include <cstdint>

namespace ps2x::iop::lle
{
    class Bus;

    // One R3000A register context. Every IOP thread and the interrupt
    // context own one; the scheduler swaps them in and out of the core.
    struct CpuContext
    {
        uint32_t gpr[32]{};
        uint32_t hi = 0;
        uint32_t lo = 0;
        uint32_t pc = 0;
        uint32_t branchTarget = 0;
        bool branchPending = false;
        // R3000A loads land one instruction late.
        uint8_t loadRegister = 0;
        uint32_t loadValue = 0;
    };

    enum class StopReason : uint8_t
    {
        Budget,  // cycle budget used up
        Syscall, // a SYSCALL trapped; the code is in syscallCode()
        Fault,   // an instruction the IOP sound modules never use
    };

    class Cpu
    {
    public:
        explicit Cpu(Bus &bus) noexcept : m_bus(bus) {}

        // Run until the budget is spent or a trap needs the kernel. Returns
        // with pc already past the trapping instruction.
        StopReason run(CpuContext &context, uint64_t &cycles, uint64_t budget);

        uint32_t syscallCode() const noexcept { return m_syscallCode; }
        uint32_t faultPc() const noexcept { return m_faultPc; }
        uint32_t cop0Status() const noexcept { return m_status; }
        void setCop0Status(uint32_t status) noexcept { m_status = status; }

    private:
        Bus &m_bus;
        uint32_t m_syscallCode = 0;
        uint32_t m_faultPc = 0;
        uint32_t m_status = 0;
    };
}
