#include "iop_timrman.h"

#include "../core/iop_cpu.h"
#include "../services/iop_rpc.h"

#include <algorithm>
#include <array>
#include <limits>

namespace ps2x::iop::detail
{
    namespace
    {
        constexpr int32_t kNoTimer = -150;
        constexpr int32_t kIllegalTimerId = -151;
        constexpr int32_t kIllegalSource = -152;
        constexpr int32_t kIllegalPrescale = -153;
        constexpr int32_t kTimerBusy = -154;
        constexpr int32_t kTimerNotConfigured = -155;
        constexpr int32_t kTimerNotRunning = -156;
        constexpr int32_t kIllegalMode = -405;
        constexpr uint64_t kIopClockHz = 36'864'000ull;
        constexpr uint64_t kPixelClockHz = 13'500'000ull;
        constexpr uint64_t kHlineClockHz = 15'734ull;

        constexpr std::array<size_t, 6> kAllocationOrder{2u, 5u, 4u, 3u, 0u, 1u};
        constexpr std::array<uint32_t, 6> kAddresses{
            0xBF801100u,
            0xBF801110u,
            0xBF801120u,
            0xBF801480u,
            0xBF801490u,
            0xBF8014A0u,
        };
        constexpr std::array<uint8_t, 6> kSources{0x0Bu, 0x0Du, 0x01u, 0x05u, 0x01u, 0x01u};
        constexpr std::array<uint8_t, 6> kWidths{16u, 16u, 16u, 32u, 32u, 32u};
        constexpr std::array<uint16_t, 6> kMaxPrescales{1u, 1u, 8u, 1u, 256u, 256u};
        constexpr std::array<uint8_t, 6> kIrqs{4u, 5u, 6u, 14u, 15u, 16u};

        uint32_t errorValue(int32_t error) noexcept
        {
            return static_cast<uint32_t>(error);
        }
    }

    void IopTimrman::reset() noexcept
    {
        for (size_t i = 0u; i < m_timers.size(); ++i)
        {
            m_timers[i] = {};
            m_timers[i].address = kAddresses[i];
            m_timers[i].sources = kSources[i];
            m_timers[i].width = kWidths[i];
            m_timers[i].maxPrescale = kMaxPrescales[i];
            m_timers[i].irq = kIrqs[i];
        }
        m_holdMode = 0u;
        m_servicing = false;
    }

    uint32_t IopTimrman::timerId(size_t index) noexcept
    {
        return (static_cast<uint32_t>(index + 1u) << 28u) | (kAddresses[index] >> 4u);
    }

    IopTimrman::Timer *IopTimrman::timerFromId(uint32_t id) noexcept
    {
        const uint32_t encoded = id >> 28u;
        if (encoded == 0u || encoded > m_timers.size())
            return nullptr;
        Timer &timer = m_timers[encoded - 1u];
        return timer.users != 0u && (id & 0x0FFFFFFFu) == (timer.address >> 4u)
                   ? &timer
                   : nullptr;
    }

    const IopTimrman::Timer *IopTimrman::timerFromId(uint32_t id) const noexcept
    {
        return const_cast<IopTimrman *>(this)->timerFromId(id);
    }

    uint64_t IopTimrman::ticksToCycles(const Timer &timer, uint64_t ticks) noexcept
    {
        const uint64_t prescale = std::max<uint64_t>(timer.prescale, 1u);
        const uint64_t sourceHz = timer.source == 2u
                                      ? kPixelClockHz
                                      : (timer.source == 4u ? kHlineClockHz : kIopClockHz);
        if (ticks == 0u)
            ticks = timer.width == 16u ? (1ull << 16u) : (1ull << 32u);
        const unsigned long long scaled = ticks * prescale;
        if (sourceHz == kIopClockHz)
            return std::max<uint64_t>(scaled, 1u);
        const uint64_t whole = (scaled / sourceHz) * kIopClockHz;
        const uint64_t remainder = scaled % sourceHz;
        return std::max<uint64_t>(1u, whole + (remainder * kIopClockHz + sourceHz - 1u) / sourceHz);
    }

    uint64_t IopTimrman::elapsedTicks(const Timer &timer, uint64_t currentCycle) noexcept
    {
        if (!timer.running || currentCycle <= timer.counterBaseCycle)
            return 0u;
        const uint64_t elapsed = currentCycle - timer.counterBaseCycle;
        const uint64_t sourceHz = timer.source == 2u
                                      ? kPixelClockHz
                                      : (timer.source == 4u ? kHlineClockHz : kIopClockHz);
        return (elapsed * sourceHz) / (kIopClockHz * std::max<uint64_t>(timer.prescale, 1u));
    }

    uint32_t IopTimrman::counterValue(const Timer &timer, uint64_t currentCycle) noexcept
    {
        const uint64_t value = static_cast<uint64_t>(timer.counterBase) + elapsedTicks(timer, currentCycle);
        return timer.width == 16u ? static_cast<uint32_t>(value & 0xFFFFu) : static_cast<uint32_t>(value);
    }

    void IopTimrman::schedule(Timer &timer, uint64_t currentCycle) noexcept
    {
        timer.compareCycle = UINT64_MAX;
        timer.overflowCycle = UINT64_MAX;
        if (!timer.running)
            return;

        const uint64_t current = counterValue(timer, currentCycle);
        timer.counterBase = static_cast<uint32_t>(current);
        timer.counterBaseCycle = currentCycle;

        if (timer.compareCallback.function != 0u)
        {
            const uint64_t modulus = timer.width == 16u ? (1ull << 16u) : (1ull << 32u);
            const uint64_t compare = timer.width == 16u ? (timer.compare & 0xFFFFu) : timer.compare;
            uint64_t delta = (compare + modulus - current) % modulus;
            if (delta == 0u)
                delta = modulus;
            timer.compareCycle = currentCycle + ticksToCycles(timer, delta);
        }

        if (timer.overflowCallback.function != 0u)
        {
            const uint64_t modulus = timer.width == 16u ? (1ull << 16u) : (1ull << 32u);
            uint64_t delta = modulus - current;
            if (delta == 0u)
                delta = modulus;
            timer.overflowCycle = currentCycle + ticksToCycles(timer, delta);
        }
    }

    void IopTimrman::stop(Timer &timer, uint64_t currentCycle) noexcept
    {
        timer.counterBase = counterValue(timer, currentCycle);
        timer.counterBaseCycle = currentCycle;
        timer.running = false;
        timer.liveMode = 0u;
        timer.compareCycle = UINT64_MAX;
        timer.overflowCycle = UINT64_MAX;
    }

    bool IopTimrman::dispatchImport(uint16_t ordinal, IopCpuState &cpu, uint64_t currentCycle)
    {
        const uint32_t a0 = cpu.gpr[4];
        const uint32_t a1 = cpu.gpr[5];
        const uint32_t a2 = cpu.gpr[6];
        const uint32_t a3 = cpu.gpr[7];
        const auto setV0 = [&](uint32_t value) { cpu.gpr[2] = value; };

        switch (ordinal)
        {
        case 3: // GetTimersTable
            setV0(0u);
            return true;
        case 4: // AllocHardTimer
            for (const size_t index : kAllocationOrder)
            {
                Timer &timer = m_timers[index];
                if (timer.users != 0u || (timer.sources & a0) == 0u || timer.width != a1 || timer.maxPrescale < a2)
                    continue;
                timer.users = 1u;
                timer.source = a0;
                timer.prescale = std::max(a2, 1u);
                timer.counterBaseCycle = currentCycle;
                setV0(timerId(index));
                return true;
            }
            setV0(errorValue(kNoTimer));
            return true;
        case 5: // ReferHardTimer
            for (size_t index = 0u; index < m_timers.size(); ++index)
            {
                Timer &timer = m_timers[index];
                if (timer.users == 0u || timer.liveMode == 0u || (timer.sources & a0) == 0u ||
                    timer.width != a1 || (timer.liveMode & a3) != a2)
                    continue;
                ++timer.users;
                setV0(timerId(index));
                return true;
            }
            setV0(errorValue(kNoTimer));
            return true;
        case 6: // FreeHardTimer
        {
            Timer *timer = timerFromId(a0);
            if (!timer)
            {
                setV0(errorValue(kIllegalTimerId));
                return true;
            }
            if (--timer->users == 0u)
            {
                const uint32_t address = timer->address;
                const uint8_t sources = timer->sources;
                const uint8_t width = timer->width;
                const uint16_t maxPrescale = timer->maxPrescale;
                const uint8_t irq = timer->irq;
                *timer = {};
                timer->address = address;
                timer->sources = sources;
                timer->width = width;
                timer->maxPrescale = maxPrescale;
                timer->irq = irq;
            }
            setV0(0u);
            return true;
        }
        case 7: // SetTimerMode
        {
            Timer *timer = timerFromId(a0);
            if (!timer)
            {
                setV0(errorValue(kIllegalTimerId));
                return true;
            }
            if (a1 == 0u)
                stop(*timer, currentCycle);
            else
            {
                timer->liveMode = a1;
                timer->running = true;
                timer->counterBaseCycle = currentCycle;
                schedule(*timer, currentCycle);
            }
            setV0(0u);
            return true;
        }
        case 8: // GetTimerStatus
        case 17: // GetTimerMode
        {
            const Timer *timer = timerFromId(a0);
            setV0(timer ? timer->liveMode : errorValue(kIllegalTimerId));
            return true;
        }
        case 9: // SetTimerCounter
        {
            Timer *timer = timerFromId(a0);
            if (!timer)
            {
                setV0(errorValue(kIllegalTimerId));
                return true;
            }
            timer->counterBase = timer->width == 16u ? (a1 & 0xFFFFu) : a1;
            timer->counterBaseCycle = currentCycle;
            schedule(*timer, currentCycle);
            setV0(0u);
            return true;
        }
        case 10: // GetTimerCounter
        {
            const Timer *timer = timerFromId(a0);
            setV0(timer ? counterValue(*timer, currentCycle) : errorValue(kIllegalTimerId));
            return true;
        }
        case 11: // SetTimerCompare
        {
            Timer *timer = timerFromId(a0);
            if (!timer)
            {
                setV0(errorValue(kIllegalTimerId));
                return true;
            }
            timer->compare = timer->width == 16u ? (a1 & 0xFFFFu) : a1;
            schedule(*timer, currentCycle);
            setV0(0u);
            return true;
        }
        case 12: // GetTimerCompare
        {
            const Timer *timer = timerFromId(a0);
            setV0(timer ? timer->compare : errorValue(kIllegalTimerId));
            return true;
        }
        case 13: // SetHoldMode
            m_holdMode = (m_holdMode & ~(0xFu << ((a0 & 7u) * 4u))) | ((a1 & 0xFu) << ((a0 & 7u) * 4u));
            setV0(0u);
            return true;
        case 14: // GetHoldMode
            setV0((m_holdMode >> ((a0 & 7u) * 4u)) & 0xFu);
            return true;
        case 15: // GetHoldReg
            setV0(0u);
            return true;
        case 16: // GetHardTimerIntrCode
        {
            const Timer *timer = timerFromId(a0);
            setV0(timer ? timer->irq : errorValue(kIllegalTimerId));
            return true;
        }
        case 18: // GetTimerReadFunc
            // Returning a host-side register reader as a guest function is not meaningful.
            setV0(0u);
            return true;
        case 20: // SetTimerHandler
        case 21: // SetOverflowHandler
        {
            Timer *timer = timerFromId(a0);
            if (!timer)
            {
                setV0(errorValue(kIllegalTimerId));
                return true;
            }
            if (timer->running)
            {
                setV0(errorValue(kTimerNotRunning));
                return true;
            }
            if (ordinal == 20u)
            {
                timer->compare = timer->width == 16u ? (a1 & 0xFFFFu) : a1;
                timer->compareCallback = {a2, a3, cpu.gpr[28]};
            }
            else
            {
                timer->overflowCallback = {a1, a2, cpu.gpr[28]};
            }
            setV0(0u);
            return true;
        }
        case 22: // SetupHardTimer
        {
            Timer *timer = timerFromId(a0);
            if (!timer)
            {
                setV0(errorValue(kIllegalTimerId));
                return true;
            }
            if (timer->running)
            {
                setV0(errorValue(kTimerBusy));
                return true;
            }
            if ((a2 != 0u && a2 != 1u && a2 != 3u && a2 != 5u && a2 != 7u))
            {
                setV0(errorValue(kIllegalMode));
                return true;
            }
            if ((timer->sources & a1) == 0u)
            {
                setV0(errorValue(kIllegalSource));
                return true;
            }
            if (a3 == 0u || a3 > timer->maxPrescale)
            {
                setV0(errorValue(kIllegalPrescale));
                return true;
            }
            timer->source = a1;
            timer->setupMode = a2;
            timer->prescale = a3;
            timer->configured = true;
            setV0(0u);
            return true;
        }
        case 23: // StartHardTimer
        {
            Timer *timer = timerFromId(a0);
            if (!timer)
            {
                setV0(errorValue(kIllegalTimerId));
                return true;
            }
            if (timer->running)
            {
                setV0(errorValue(kTimerBusy));
                return true;
            }
            if (!timer->configured)
            {
                setV0(errorValue(kTimerNotConfigured));
                return true;
            }
            timer->counterBase = 0u;
            timer->counterBaseCycle = currentCycle;
            timer->liveMode = 0x80000000u | timer->setupMode;
            timer->running = true;
            schedule(*timer, currentCycle);
            setV0(0u);
            return true;
        }
        case 24: // StopHardTimer
        {
            Timer *timer = timerFromId(a0);
            if (!timer)
            {
                setV0(errorValue(kIllegalTimerId));
                return true;
            }
            if (!timer->running)
            {
                setV0(errorValue(kTimerNotRunning));
                return true;
            }
            stop(*timer, currentCycle);
            setV0(0u);
            return true;
        }
        default:
            return false;
        }
    }

    void IopTimrman::serviceDue(uint64_t currentCycle, IopGuestExecutor &executor)
    {
        if (m_servicing)
            return;
        m_servicing = true;
        struct ServiceGuard
        {
            bool &flag;
            ~ServiceGuard() { flag = false; }
        } guard{m_servicing};

        for (Timer &timer : m_timers)
        {
            if (!timer.running)
                continue;

            const bool compareDue = timer.compareCycle <= currentCycle;
            const bool overflowDue = timer.overflowCycle <= currentCycle;
            if (!compareDue && !overflowDue)
                continue;

            const Callback callback = compareDue ? timer.compareCallback : timer.overflowCallback;
            timer.compareCycle = UINT64_MAX;
            timer.overflowCycle = UINT64_MAX;
            const uint32_t result = callback.function != 0u
                                        ? executor.executeGuestFunctionWithBudget(callback.function,
                                                                                  callback.common,
                                                                                  0u,
                                                                                  0u,
                                                                                  0u,
                                                                                  callback.gp,
                                                                                  100000u)
                                        : 0u;
            if (!timer.running)
                continue;
            if (result == 0u)
            {
                stop(timer, currentCycle);
                continue;
            }
            if (compareDue)
                timer.compare = timer.width == 16u ? (result & 0xFFFFu) : result;
            timer.counterBase = 0u;
            timer.counterBaseCycle = currentCycle;
            schedule(timer, currentCycle);
        }

    }

    uint64_t IopTimrman::nextEventCycle(uint64_t fallback) const noexcept
    {
        uint64_t next = fallback;
        for (const Timer &timer : m_timers)
        {
            next = std::min(next, timer.compareCycle);
            next = std::min(next, timer.overflowCycle);
        }
        return next;
    }
}
