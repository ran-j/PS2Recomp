#include "runtime/vu/vu_runtime.h"
#include "runtime/vu/vu_unit.h"
#include "runtime/vu/vu_decoder.h"
#include "ps2_runtime.h"
#include <algorithm>
#include <bit>
#include <cstring>
#include <stdexcept>
#include <string>

namespace ps2vu
{
    Runtime::Runtime(PS2Memory &memory, std::function<void(uint32_t)> advanceClock)
        : m_memory(memory), m_advanceClock(std::move(advanceClock))
    {
        MemoryServices services;
        services.context = this;
        services.readShared = readShared;
        services.writeShared = writeShared;
        services.submitGif = submitGif;
        for (uint32_t index = 0; index < 2; ++index)
        {
            const bool vu1 = index != 0;
            services.data = vu1 ? memory.getVU1Data() : memory.getVU0Data();
            m_units[index] = std::make_unique<Unit>(vu1, std::span(vu1 ? memory.getVU1Code() : memory.getVU0Code(), vu1 ? PS2_VU1_CODE_SIZE : PS2_VU0_CODE_SIZE), services);
        }
    }

    Runtime::~Runtime() = default;
    Unit &Runtime::unit(bool vu1) noexcept { return *m_units[vu1]; }

    bool Runtime::failed() const noexcept
    {
        return m_units[0]->status() == UnitStatus::Fault || m_units[1]->status() == UnitStatus::Fault;
    }

    void Runtime::reportFailure() const
    {
        for (uint32_t index = 0; index < 2; ++index)
            if (m_units[index]->status() == UnitStatus::Fault)
            {
                const auto &failure = m_units[index]->failure();
                throw std::runtime_error("VU" + std::to_string(index) + " execution failed at PC " +
                                         std::to_string(failure.pc) + ": " + failure.reason.data());
            }
    }

    void Runtime::run(bool vu1, uint32_t cycles)
    {
        auto &target = unit(vu1);
        while (cycles)
        {
            if (vu1 && unit(false).status() == UnitStatus::Running)
            {
                m_advanceClock(1);
                reportFailure();
                --cycles;
                if (target.status() != UnitStatus::Running)
                    return;
                continue;
            }

            const auto exit = target.advance(cycles);
            if (!vu1)
                m_contextDirty = true;

            if (exit.cycles)
                m_advanceClock(static_cast<uint32_t>(exit.cycles));

            reportFailure();
            cycles -= static_cast<uint32_t>(exit.cycles);
            if (exit.reason != ExitReason::Event || !exit.cycles)
                return;
        }
    }

    void Runtime::wait(bool vu1)
    {
        while (unit(vu1).status() == UnitStatus::Running)
            run(vu1, 64);
        reportFailure();
    }

    void Runtime::start0(R5900Context &context, uint32_t pc)
    {
        m_accessContext = &context;
        const bool running = unit(false).status() == UnitStatus::Running;
        wait(false);
        if (running)
            exportContext(unit(false).state(), context);

        importContext(context, unit(false).state());
        if (!unit(false).start(pc, 0, context.vu0_itop, StartSource::Ee))
            reportFailure();

        exportContext(unit(false).state(), context);
        m_accessContext = nullptr;
    }

    void Runtime::startVif0(R5900Context &context, uint32_t pc, uint32_t itop, bool resume)
    {
        m_accessContext = &context;
        const bool running = unit(false).status() == UnitStatus::Running;

        wait(false);

        if (running)
            exportContext(unit(false).state(), context);

        importContext(context, unit(false).state());
        const bool started = resume ? unit(false).resume(0, itop) : unit(false).start(pc, 0, itop);
        m_accessContext = nullptr;
        if (!started)
        {
            reportFailure();
            throw std::runtime_error("VIF startup requested while VU0 is stopped");
        }
        exportContext(unit(false).state(), context);
    }

    void Runtime::start1(uint32_t pc, uint32_t top, uint32_t itop, bool resume)
    {
        wait(true);
        const bool started = resume ? unit(true).resume(top, itop) : unit(true).start(pc, top, itop);
        if (!started)
        {
            reportFailure();
            throw std::runtime_error("VIF startup requested while VU1 is stopped");
        }
    }

    void Runtime::flush0(R5900Context &context)
    {
        m_accessContext = &context;
        wait(false);
        m_accessContext = nullptr;
    }

    void Runtime::flush1(bool gif)
    {
        wait(true);
        if (gif)
            while (unit(true).transferring())
                run(true, 2);
    }

    void Runtime::advance(uint32_t cycles, R5900Context &context) noexcept
    {
        auto &activeContext = m_accessContext ? *m_accessContext : context;
        m_cycle += cycles;

        auto &vu0 = unit(false);
        const bool updateContext = vu0.pending() || m_contextDirty;
        if (updateContext && !m_contextDirty && vu0.status() != UnitStatus::Running)
            importContext(activeContext, vu0.state());

        advanceUnit(false, m_cycle);
        advanceUnit(true, m_cycle);

        if (updateContext)
            exportContext(vu0.state(), activeContext);

        m_contextDirty = false;
        activeContext.vu0_vpu_stat = (activeContext.vu0_vpu_stat & ~0x0701u) |
                                     (vu0.status() == UnitStatus::Running ? 1u : 0u) |
                                     (unit(true).status() == UnitStatus::Running ? 0x100u : 0u) |
                                     (unit(true).state().stoppedByD ? 0x200u : 0u) | (unit(true).state().stoppedByT ? 0x400u : 0u);
    }

    void Runtime::advanceUnit(bool vu1, uint64_t cycle) noexcept
    {
        auto &target = unit(vu1);
        if (target.state().cycles >= cycle)
            return;

        if (!target.pending() && !target.transferring())
        {
            if (target.status() != UnitStatus::Fault)
                target.state().cycles = cycle;
            return;
        }

        while (target.state().cycles < cycle)
        {
            const auto remaining = static_cast<uint32_t>(std::min<uint64_t>(cycle - target.state().cycles, UINT32_MAX));
            const auto exit = target.advance(remaining);
            if (!exit.cycles || exit.reason == ExitReason::Fault)
                break;
        }
    }

    void Runtime::beforeAccess(R5900Context &context, uint32_t instruction)
    {
        const auto opcode = instruction >> 26;
        if (opcode == 0x36 || opcode == 0x3e)
        {
            const auto target = (instruction >> 16) & 31;
            if (unit(false).pending() || m_contextDirty)
                context.vu0_vf[target] = _mm_loadu_ps(unit(false).state().vf[target]);
            reportFailure();
            return;
        }

        const auto format = (instruction >> 21) & 31;
        const auto reg = (instruction >> 11) & 31;
        auto &vu0 = unit(false);
        if (!vu0.pending() && !m_contextDirty)
        {
            if (format == 5 || format == 6)
                vu0.state().mBit = false;
            reportFailure();
            return;
        }

        m_accessContext = &context;
        if (vu0.status() != UnitStatus::Running)
            importContext(context, vu0.state());

        const bool interlocked = (instruction & 1) != 0;
        const bool writeTransfer = format == 5 || format == 6;
        if (interlocked && writeTransfer)
        {
            while (vu0.status() == UnitStatus::Running && !vu0.state().mBit)
                run(false, 1);
        }
        else if (format >= 16 || interlocked)
            wait(false);

        if (writeTransfer)
            vu0.state().mBit = false;

        const auto &ready = vu0.readiness();
        uint32_t delay = 0;
        if ((format == 1 || format == 5) && interlocked)
            for (uint32_t lane = 0; lane < 4; ++lane)
                delay = std::max(delay, uint32_t(ready.registers[reg * 4 + lane]));
        else if ((format == 2 || format == 6) && reg < 16 && interlocked)
            delay = ready.registers[kViReadyBase + reg];
        else if (format >= 16)
        {
            const uint32_t op = instruction & 63;
            if (op != 0x38 && op != 0x39)
            {
                const auto special = ((instruction >> 4) & 0x7c) | (instruction & 3);
                const bool lower = op >= 0x30 && (op < 0x3c || special >= 0x30);
                const auto decoded = decodeInstructionPair(lower ? 0x80000000u | (instruction & 0x1ffffff) : 0x8000033cu,
                                                           lower ? 0x2ffu : instruction & 0x1ffffff, false);
                for (uint32_t index = 0; index < decoded.readDependencyCount; ++index)
                    delay = std::max(delay, uint32_t(ready.registers[decoded.readDependencies[index]]));
                for (const auto &usage : {decoded.upperUsage, decoded.lowerUsage})
                {
                    if (usage.vfWrite.reg)
                        for (uint32_t lane = 0; lane < 4; ++lane)
                            delay = std::max(delay, uint32_t(ready.registers[usage.vfWrite.reg * 4 + lane]));
                    for (uint32_t reg = 1; reg < 16; ++reg)
                        if (usage.viWrite & (1u << reg))
                            delay = std::max(delay, uint32_t(ready.registers[kViReadyBase + reg]));
                }
                if (decoded.lowerUsage.waitQ || decoded.lowerUsage.pipeline == PipelineFdiv)
                    delay = std::max(delay, uint32_t(ready.q));
            }
        }

        if (delay)
            run(false, delay);

        exportContext(vu0.state(), context);
        m_accessContext = nullptr;
    }

    void Runtime::afterAccess(R5900Context &context, uint32_t instruction)
    {
        const auto opcode = instruction >> 26;
        if (opcode == 0x36)
        {
            const auto target = (instruction >> 16) & 31;
            if (target)
                _mm_storeu_ps(unit(false).state().vf[target], context.vu0_vf[target]);
            else
                context.vu0_vf[0] = _mm_set_ps(1, 0, 0, 0);
            return;
        }

        if (opcode == 0x3e)
            return;
        if (unit(false).pending())
            importContext(context, unit(false).state());

        const auto format = (instruction >> 21) & 31;
        const auto reg = (instruction >> 11) & 31;
        if (format == 6 && reg == 28)
        {
            const auto value = static_cast<uint32_t>(_mm_cvtsi128_si32(context.r[(instruction >> 16) & 31]));
            for (uint32_t index = 0; index < 2; ++index)
            {
                auto &target = *m_units[index];
                const auto bits = value >> (index * 8);
                if (bits & 2)
                    target.reset();
                else if (bits & 1)
                    target.forceBreak();
                target.state().dBitEnabled = (bits & 4) != 0;
                target.state().tBitEnabled = (bits & 8) != 0;
                if (bits & 2)
                    target.state().cycles = m_cycle;
            }
            if (value & 2)
                exportContext(unit(false).state(), context);
        }

        if (format == 6 && reg == 31)
        {
            m_accessContext = &context;
            wait(true);
            const auto &state = unit(true).state();
            unit(true).start((context.vu0_cmsar1 & 0x7ff) * 8, state.top, state.itop, StartSource::Ee);
            m_accessContext = nullptr;
        }
    }

    bool Runtime::submitGif(void *context, const uint8_t *data, uint32_t size) noexcept
    {
        try
        {
            static_cast<Runtime *>(context)->m_memory.submitGifPacket(GifPathId::Path1, data, size);
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    bool Runtime::readShared(void *context, uint64_t cycle, uint32_t offset, uint32_t *words) noexcept
    {
        auto &runtime = *static_cast<Runtime *>(context);
        runtime.advanceUnit(true, cycle);
        if (runtime.failed())
            return false;

        auto &state = runtime.unit(true).state();
        const auto reg = (offset & 0x3ff) >> 4;
        std::fill_n(words, 4, 0u);

        if (reg < 32)
            std::memcpy(words, state.vf[reg], 16);
        else if (reg < 48)
            words[0] = static_cast<uint16_t>(state.vi[reg - 32]);
        else
            switch (reg - 32)
            {
            case 16:
                words[0] = state.status;
                break;
            case 17:
                words[0] = state.mac;
                break;
            case 18:
                words[0] = state.clip;
                break;
            case 20:
                words[0] = state.r;
                break;
            case 21:
                words[0] = std::bit_cast<uint32_t>(state.i);
                break;
            case 22:
                words[0] = std::bit_cast<uint32_t>(state.q);
                break;
            case 23:
                words[0] = std::bit_cast<uint32_t>(state.p);
                break;
            case 26:
                words[0] = state.pc / 8;
                break;
            default:
                break;
            }
        return true;
    }

    bool Runtime::writeShared(void *context, uint64_t cycle, uint32_t offset, const uint32_t *words, uint8_t lanes) noexcept
    {
        auto &runtime = *static_cast<Runtime *>(context);
        runtime.advanceUnit(true, cycle);
        if (runtime.failed())
            return false;

        auto &target = runtime.unit(true);
        auto &state = target.state();
        const auto reg = (offset & 0x3ff) >> 4;
        if (reg && reg < 32)
            for (uint32_t lane = 0; lane < 4; ++lane)
                if (lanes & (8u >> lane))
                    state.vf[reg][lane] = std::bit_cast<float>(words[lane]);

        if (!(lanes & 8))
            return true;

        if (reg > 32 && reg < 48)
            state.vi[reg - 32] = static_cast<int16_t>(words[0]);
        else
            switch (reg - 32)
            {
            case 16:
                state.status = (state.status & 0x3f) | (words[0] & 0xfc0);
                break;
            case 18:
                state.clip = words[0] & 0xffffff;
                break;
            case 20:
                state.r = 0x3f800000 | (words[0] & 0x7fffff);
                break;
            case 21:
                state.i = std::bit_cast<float>(words[0]);
                break;
            case 22:
                state.q = std::bit_cast<float>(words[0]);
                break;
            case 31:
                return target.start((words[0] & 0x7ff) * 8, state.top, state.itop, StartSource::Ee);
            default:
                break;
            }
        return true;
    }
}
