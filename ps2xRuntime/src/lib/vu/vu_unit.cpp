#include "runtime/vu/vu_unit.h"
#include <algorithm>
#include <cstdio>
#include <exception>

namespace ps2vu
{
    Unit::Unit(bool vu1, std::span<const uint8_t> code, MemoryServices services)
        : m_executor(vu1 ? VUExecutor::Unit::VU1 : VUExecutor::Unit::VU0), m_services(services),
          m_code(code), m_dataSize(vu1 ? 16384 : 4096)
    {
        m_executor.m_services = &m_services;
        m_executor.m_activeVuData = services.data;
        m_executor.m_activeVuDataSize = m_dataSize;
    }

    void Unit::reset() noexcept
    {
        m_executor.reset();
        m_failure = {};
        m_status = UnitStatus::Ready;
    }

    void Unit::fail(const char *reason) noexcept
    {
        if (m_status == UnitStatus::Fault)
            return;
        m_failure.pc = state().pc;
        std::snprintf(m_failure.reason.data(), m_failure.reason.size(), "%s", reason);
        m_status = UnitStatus::Fault;
    }

    bool Unit::start(uint32_t pc, uint32_t top, uint32_t itop, StartSource source) noexcept
    {
        if (m_status == UnitStatus::Fault || m_status == UnitStatus::Running)
            return false;
        if (m_status == UnitStatus::Stopped && source == StartSource::Vif)
            return false;
        if ((pc & 7) || pc >= m_code.size())
        {
            state().pc = pc;
            fail("Invalid VU entry address");
            return false;
        }
        state().pc = pc;
        begin(top, itop);
        return true;
    }

    bool Unit::resume(uint32_t top, uint32_t itop) noexcept
    {
        if (m_status != UnitStatus::Ready)
            return false;
        begin(top, itop);
        return true;
    }

    void Unit::forceBreak() noexcept
    {
        if (m_status != UnitStatus::Fault)
            m_status = UnitStatus::Stopped;
    }

    void Unit::begin(uint32_t top, uint32_t itop) noexcept
    {
        auto &s = state();
        s.top = top;
        s.itop = itop;
        s.ebit = false;
        s.mBit = false;
        s.haltAfterDelaySlot = false;
        s.stoppedByD = false;
        s.stoppedByT = false;
        s.branchPending = false;
        s.branchDelay = 0;
        m_executor.m_pendingHaltD = false;
        m_executor.m_pendingHaltT = false;
        m_executor.m_viBranchBackupValid = false;
        m_executor.m_programEnded = false;
        m_executor.m_stopRequested = false;
        m_status = UnitStatus::Running;
    }

    Readiness Unit::readiness() const noexcept
    {
        Readiness result;
        const auto cycle = state().cycles;
        for (size_t index = 0; index < result.registers.size(); ++index)
            if (m_executor.m_registerReady[index] > cycle)
                result.registers[index] = static_cast<uint32_t>(m_executor.m_registerReady[index] - cycle);

        if (m_executor.m_fdiv.valid && m_executor.m_fdiv.readyCycle > cycle)
            result.q = static_cast<uint32_t>(m_executor.m_fdiv.readyCycle - cycle);

        return result;
    }

    ExecutionExit Unit::advance(uint32_t cycles) noexcept
    {
        if (m_status == UnitStatus::Fault)
            return {ExitReason::Fault, state().pc, 0};
        const auto initial = state().cycles;
        m_executor.m_cycle = initial;
        try
        {
            if (m_status == UnitStatus::Running)
            {
                m_executor.run(m_code.data(), static_cast<uint32_t>(m_code.size()), m_services.data, m_dataSize, nullptr, nullptr, cycles, false);
                if (m_executor.m_stopRequested)
                    fail("VU instruction execution failed");
                else if (m_executor.m_programEnded)
                    m_status = state().stoppedByD || state().stoppedByT ? UnitStatus::Stopped : UnitStatus::Ready;
            }
            else
                m_executor.advanceTo(initial + cycles);
        }
        catch (const std::exception &error)
        {
            fail(error.what());
        }
        catch (...)
        {
            fail("Unknown VU execution failure");
        }
        return {m_status == UnitStatus::Fault ? ExitReason::Fault : m_executor.m_programEnded ? ExitReason::Halt
                                                                                              : ExitReason::Budget,
                state().pc, state().cycles - initial};
    }
}
