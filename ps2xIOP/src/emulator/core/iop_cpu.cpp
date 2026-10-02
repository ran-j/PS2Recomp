#include "iop_cpu.h"
#include "iop_memory.h"

#include <limits>

namespace ps2x::iop::detail
{
    IopCpuCore::IopCpuCore(IopMemory &memory) noexcept
        : m_memory(memory)
    {
    }

    void IopCpuCore::writeRegister(IopCpuState &cpu, uint32_t reg, uint32_t value, uint32_t &writtenReg)
    {
        if (reg == 0u)
            return;
        cpu.gpr[reg] = value;
        writtenReg = reg;
    }

    void IopCpuCore::scheduleLoad(uint32_t reg, uint32_t value, bool &scheduled, uint32_t &scheduledReg, uint32_t &scheduledValue)
    {
        if (reg == 0u)
            return;
        scheduled = true;
        scheduledReg = reg;
        scheduledValue = value;
    }

    void IopCpuCore::raiseException(IopCpuState &cpu, uint32_t code, uint32_t faultPc, bool delaySlot, std::optional<uint32_t> badAddress) const
    {
        uint32_t cause = cpu.cop0[13] & ~0x7Cu;
        cause |= (code & 0x1Fu) << 2u;
        if (delaySlot)
        {
            cause |= 0x80000000u;
            cpu.cop0[14] = faultPc - 4u;
        }
        else
        {
            cause &= ~0x80000000u;
            cpu.cop0[14] = faultPc;
        }
        cpu.cop0[13] = cause;
        if (badAddress)
            cpu.cop0[8] = *badAddress;
        const uint32_t status = cpu.cop0[12];
        cpu.cop0[12] = (status & ~0x3Fu) | ((status << 2u) & 0x3Fu);
        cpu.pc = (status & (1u << 22u)) ? 0xBFC00180u : 0x80000080u;
        cpu.branchPending = false;
        cpu.pendingLoad = false;
        cpu.exception = true;
    }

    bool IopCpuCore::executeInstruction(IopCpuState &cpu)
    {
        const uint32_t pc = cpu.pc;
        const uint32_t instruction = m_memory.read32(pc);
        const bool wasDelaySlot = cpu.branchPending;
        const uint32_t priorBranchTarget = cpu.branchTarget;

        cpu.branchPending = false;
        cpu.exception = false;
        cpu.yielded = false;

        const uint32_t opcode = instruction >> 26u;
        const uint32_t rs = (instruction >> 21u) & 31u;
        const uint32_t rt = (instruction >> 16u) & 31u;
        const uint32_t rd = (instruction >> 11u) & 31u;
        const uint32_t sa = (instruction >> 6u) & 31u;
        const uint32_t funct = instruction & 63u;
        const uint32_t imm = instruction & 0xFFFFu;
        const int32_t simm = static_cast<int16_t>(imm);
        const uint32_t nextPc = pc + 4u;

        uint32_t writtenReg = 0u;
        bool scheduledLoad = false;
        uint32_t scheduledReg = 0u;
        uint32_t scheduledValue = 0u;
        bool newBranch = false;
        uint32_t newBranchTarget = 0u;

        auto branch = [&](bool condition)
        {
            if (condition)
            {
                newBranch = true;
                newBranchTarget = nextPc + (static_cast<uint32_t>(simm) << 2u);
            }
        };
        auto write = [&](uint32_t reg, uint32_t value)
        {
            writeRegister(cpu, reg, value, writtenReg);
        };
        auto load = [&](uint32_t reg, uint32_t value)
        {
            scheduleLoad(reg, value, scheduledLoad, scheduledReg, scheduledValue);
        };
        auto overflowAdd = [&](int32_t lhs, int32_t rhs, uint32_t reg)
        {
            const int64_t result = static_cast<int64_t>(lhs) + rhs;
            if (result > std::numeric_limits<int32_t>::max() || result < std::numeric_limits<int32_t>::min())
                raiseException(cpu, 12u, pc, wasDelaySlot);
            else
                write(reg, static_cast<uint32_t>(static_cast<int32_t>(result)));
        };
        auto overflowSub = [&](int32_t lhs, int32_t rhs, uint32_t reg)
        {
            const int64_t result = static_cast<int64_t>(lhs) - rhs;
            if (result > std::numeric_limits<int32_t>::max() || result < std::numeric_limits<int32_t>::min())
                raiseException(cpu, 12u, pc, wasDelaySlot);
            else
                write(reg, static_cast<uint32_t>(static_cast<int32_t>(result)));
        };

        // TODO kill this magic number and make it a constant somewhere
        switch (opcode)
        {
        case 0x00:
            switch (funct)
            {
            case 0x00:
                write(rd, cpu.gpr[rt] << sa);
                break;
            case 0x02:
                write(rd, cpu.gpr[rt] >> sa);
                break;
            case 0x03:
                write(rd, static_cast<uint32_t>(static_cast<int32_t>(cpu.gpr[rt]) >> sa));
                break;
            case 0x04:
                write(rd, cpu.gpr[rt] << (cpu.gpr[rs] & 31u));
                break;
            case 0x06:
                write(rd, cpu.gpr[rt] >> (cpu.gpr[rs] & 31u));
                break;
            case 0x07:
                write(rd, static_cast<uint32_t>(static_cast<int32_t>(cpu.gpr[rt]) >> (cpu.gpr[rs] & 31u)));
                break;
            case 0x08:
                newBranch = true;
                newBranchTarget = cpu.gpr[rs];
                break;
            case 0x09:
                write(rd ? rd : 31u, pc + 8u);
                newBranch = true;
                newBranchTarget = cpu.gpr[rs];
                break;
            case 0x0C:
                raiseException(cpu, 8u, pc, wasDelaySlot);
                break;
            case 0x0D:
                raiseException(cpu, 9u, pc, wasDelaySlot);
                break;
            case 0x10:
                write(rd, cpu.hi);
                break;
            case 0x11:
                cpu.hi = cpu.gpr[rs];
                break;
            case 0x12:
                write(rd, cpu.lo);
                break;
            case 0x13:
                cpu.lo = cpu.gpr[rs];
                break;
            case 0x18:
            {
                const int64_t result = static_cast<int64_t>(static_cast<int32_t>(cpu.gpr[rs])) * static_cast<int64_t>(static_cast<int32_t>(cpu.gpr[rt]));
                cpu.lo = static_cast<uint32_t>(result);
                cpu.hi = static_cast<uint32_t>(static_cast<uint64_t>(result) >> 32u);
                break;
            }
            case 0x19:
            {
                const uint64_t result = static_cast<uint64_t>(cpu.gpr[rs]) * cpu.gpr[rt];
                cpu.lo = static_cast<uint32_t>(result);
                cpu.hi = static_cast<uint32_t>(result >> 32u);
                break;
            }
            case 0x1A:
            {
                const int32_t lhs = static_cast<int32_t>(cpu.gpr[rs]);
                const int32_t rhs = static_cast<int32_t>(cpu.gpr[rt]);
                if (rhs == 0)
                {
                    cpu.lo = lhs >= 0 ? 0xFFFFFFFFu : 1u;
                    cpu.hi = static_cast<uint32_t>(lhs);
                }
                else if (lhs == std::numeric_limits<int32_t>::min() && rhs == -1)
                {
                    cpu.lo = static_cast<uint32_t>(lhs);
                    cpu.hi = 0u;
                }
                else
                {
                    cpu.lo = static_cast<uint32_t>(lhs / rhs);
                    cpu.hi = static_cast<uint32_t>(lhs % rhs);
                }
                break;
            }
            case 0x1B:
                if (cpu.gpr[rt] == 0u)
                {
                    cpu.lo = 0xFFFFFFFFu;
                    cpu.hi = cpu.gpr[rs];
                }
                else
                {
                    cpu.lo = cpu.gpr[rs] / cpu.gpr[rt];
                    cpu.hi = cpu.gpr[rs] % cpu.gpr[rt];
                }
                break;
            case 0x20:
                overflowAdd(static_cast<int32_t>(cpu.gpr[rs]), static_cast<int32_t>(cpu.gpr[rt]), rd);
                break;
            case 0x21:
                write(rd, cpu.gpr[rs] + cpu.gpr[rt]);
                break;
            case 0x22:
                overflowSub(static_cast<int32_t>(cpu.gpr[rs]), static_cast<int32_t>(cpu.gpr[rt]), rd);
                break;
            case 0x23:
                write(rd, cpu.gpr[rs] - cpu.gpr[rt]);
                break;
            case 0x24:
                write(rd, cpu.gpr[rs] & cpu.gpr[rt]);
                break;
            case 0x25:
                write(rd, cpu.gpr[rs] | cpu.gpr[rt]);
                break;
            case 0x26:
                write(rd, cpu.gpr[rs] ^ cpu.gpr[rt]);
                break;
            case 0x27:
                write(rd, ~(cpu.gpr[rs] | cpu.gpr[rt]));
                break;
            case 0x2A:
                write(rd, static_cast<int32_t>(cpu.gpr[rs]) < static_cast<int32_t>(cpu.gpr[rt]) ? 1u : 0u);
                break;
            case 0x2B:
                write(rd, cpu.gpr[rs] < cpu.gpr[rt] ? 1u : 0u);
                break;
            default:
                raiseException(cpu, 10u, pc, wasDelaySlot);
                break;
            }
            break;
        case 0x01:
            switch (rt)
            {
            case 0x00:
                branch(static_cast<int32_t>(cpu.gpr[rs]) < 0);
                break;
            case 0x01:
                branch(static_cast<int32_t>(cpu.gpr[rs]) >= 0);
                break;
            case 0x10:
                write(31u, pc + 8u);
                branch(static_cast<int32_t>(cpu.gpr[rs]) < 0);
                break;
            case 0x11:
                write(31u, pc + 8u);
                branch(static_cast<int32_t>(cpu.gpr[rs]) >= 0);
                break;
            default:
                raiseException(cpu, 10u, pc, wasDelaySlot);
                break;
            }
            break;
        case 0x02:
            newBranch = true;
            newBranchTarget = (nextPc & 0xF0000000u) | ((instruction & 0x03FFFFFFu) << 2u);
            break;
        case 0x03:
            write(31u, pc + 8u);
            newBranch = true;
            newBranchTarget = (nextPc & 0xF0000000u) | ((instruction & 0x03FFFFFFu) << 2u);
            break;
        case 0x04:
            branch(cpu.gpr[rs] == cpu.gpr[rt]);
            break;
        case 0x05:
            branch(cpu.gpr[rs] != cpu.gpr[rt]);
            break;
        case 0x06:
            branch(static_cast<int32_t>(cpu.gpr[rs]) <= 0);
            break;
        case 0x07:
            branch(static_cast<int32_t>(cpu.gpr[rs]) > 0);
            break;
        case 0x08:
            overflowAdd(static_cast<int32_t>(cpu.gpr[rs]), simm, rt);
            break;
        case 0x09:
            write(rt, cpu.gpr[rs] + static_cast<uint32_t>(simm));
            break;
        case 0x0A:
            write(rt, static_cast<int32_t>(cpu.gpr[rs]) < simm ? 1u : 0u);
            break;
        case 0x0B:
            write(rt, cpu.gpr[rs] < static_cast<uint32_t>(simm) ? 1u : 0u);
            break;
        case 0x0C:
            write(rt, cpu.gpr[rs] & imm);
            break;
        case 0x0D:
            write(rt, cpu.gpr[rs] | imm);
            break;
        case 0x0E:
            write(rt, cpu.gpr[rs] ^ imm);
            break;
        case 0x0F:
            write(rt, imm << 16u);
            break;
        case 0x10:
        {
            const uint32_t copRs = rs;
            if (copRs == 0x00)
                load(rt, cpu.cop0[rd]);
            else if (copRs == 0x04)
                cpu.cop0[rd] = cpu.gpr[rt];
            else if (copRs == 0x10 && funct == 0x10)
            {
                const uint32_t status = cpu.cop0[12];
                cpu.cop0[12] = (status & ~0x0Fu) | ((status >> 2u) & 0x0Fu);
            }
            else
                raiseException(cpu, 10u, pc, wasDelaySlot);
            break;
        }
        case 0x20:
        case 0x24:
        {
            const uint32_t address = cpu.gpr[rs] + static_cast<uint32_t>(simm);
            const uint8_t value = m_memory.read8(address);
            load(rt, opcode == 0x20
                         ? static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(value)))
                         : value);
            break;
        }
        case 0x21:
        case 0x25:
        {
            const uint32_t address = cpu.gpr[rs] + static_cast<uint32_t>(simm);
            if (address & 1u)
            {
                raiseException(cpu, 4u, pc, wasDelaySlot, address);
                break;
            }
            const uint16_t value = m_memory.read16(address);
            load(rt, opcode == 0x21 ? static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(value))) : value);
            break;
        }
        case 0x22:
        {
            const uint32_t address = cpu.gpr[rs] + static_cast<uint32_t>(simm);
            const uint32_t memory = m_memory.read32(address & ~3u);
            const uint32_t old = cpu.gpr[rt];
            static constexpr uint32_t masks[4] = {0x00FFFFFFu, 0x0000FFFFu, 0x000000FFu, 0x00000000u};
            static constexpr uint32_t shifts[4] = {24u, 16u, 8u, 0u};
            load(rt, (old & masks[address & 3u]) | (memory << shifts[address & 3u]));
            break;
        }
        case 0x23:
        {
            const uint32_t address = cpu.gpr[rs] + static_cast<uint32_t>(simm);
            if (address & 3u)
            {
                raiseException(cpu, 4u, pc, wasDelaySlot, address);
                break;
            }
            load(rt, m_memory.read32(address));
            break;
        }
        case 0x26:
        {
            const uint32_t address = cpu.gpr[rs] + static_cast<uint32_t>(simm);
            const uint32_t memory = m_memory.read32(address & ~3u);
            const uint32_t old = cpu.gpr[rt];
            static constexpr uint32_t masks[4] = {0x00000000u, 0xFF000000u, 0xFFFF0000u, 0xFFFFFF00u};
            static constexpr uint32_t shifts[4] = {0u, 8u, 16u, 24u};
            load(rt, (old & masks[address & 3u]) | (memory >> shifts[address & 3u]));
            break;
        }
        case 0x28:
            m_memory.write8(cpu.gpr[rs] + static_cast<uint32_t>(simm), static_cast<uint8_t>(cpu.gpr[rt]));
            break;
        case 0x29:
        {
            const uint32_t address = cpu.gpr[rs] + static_cast<uint32_t>(simm);
            if (address & 1u)
            {
                raiseException(cpu, 5u, pc, wasDelaySlot, address);
                break;
            }
            m_memory.write16(address, static_cast<uint16_t>(cpu.gpr[rt]));
            break;
        }
        case 0x2A:
        {
            const uint32_t address = cpu.gpr[rs] + static_cast<uint32_t>(simm);
            const uint32_t aligned = address & ~3u;
            const uint32_t old = m_memory.read32(aligned);
            const uint32_t value = cpu.gpr[rt];
            uint32_t result = old;
            switch (address & 3u)
            {
            case 0u:
                result = (old & 0xFFFFFF00u) | (value >> 24u);
                break;
            case 1u:
                result = (old & 0xFFFF0000u) | (value >> 16u);
                break;
            case 2u:
                result = (old & 0xFF000000u) | (value >> 8u);
                break;
            case 3u:
                result = value;
                break;
            }
            m_memory.write32(aligned, result);
            break;
        }
        case 0x2B:
        {
            const uint32_t address = cpu.gpr[rs] + static_cast<uint32_t>(simm);
            if (address & 3u)
            {
                raiseException(cpu, 5u, pc, wasDelaySlot, address);
                break;
            }
            m_memory.write32(address, cpu.gpr[rt]);
            break;
        }
        case 0x2E:
        {
            const uint32_t address = cpu.gpr[rs] + static_cast<uint32_t>(simm);
            const uint32_t aligned = address & ~3u;
            const uint32_t old = m_memory.read32(aligned);
            const uint32_t value = cpu.gpr[rt];
            uint32_t result = old;
            switch (address & 3u)
            {
            case 0u:
                result = value;
                break;
            case 1u:
                result = (old & 0x000000FFu) | (value << 8u);
                break;
            case 2u:
                result = (old & 0x0000FFFFu) | (value << 16u);
                break;
            case 3u:
                result = (old & 0x00FFFFFFu) | (value << 24u);
                break;
            }
            m_memory.write32(aligned, result);
            break;
        }
        default:
            raiseException(cpu, 10u, pc, wasDelaySlot);
            break;
        }

        cpu.gpr[0] = 0u;
        if (cpu.exception)
            return !cpu.stopped;

        if (cpu.pendingLoad)
        {
            if (cpu.pendingLoadReg != 0u && cpu.pendingLoadReg != writtenReg)
                cpu.gpr[cpu.pendingLoadReg] = cpu.pendingLoadValue;
            cpu.pendingLoad = false;
        }
        if (scheduledLoad)
        {
            cpu.pendingLoad = true;
            cpu.pendingLoadReg = scheduledReg;
            cpu.pendingLoadValue = scheduledValue;
        }
        cpu.gpr[0] = 0u;

        if (wasDelaySlot)
        {
            cpu.pc = priorBranchTarget;
            cpu.branchPending = false;
        }
        else
        {
            cpu.pc = nextPc;
            cpu.branchPending = newBranch;
            cpu.branchTarget = newBranchTarget;
        }
        return !cpu.stopped;
    }
}
