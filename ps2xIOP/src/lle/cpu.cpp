#include "cpu.h"
#include "bus.h"

namespace ps2x::iop::lle
{
    namespace
    {
        inline int32_t signExtend16(uint32_t value) { return static_cast<int16_t>(value & 0xFFFFu); }
    }

    StopReason Cpu::run(CpuContext &c, uint64_t &cycles, uint64_t budget)
    {
        uint32_t *const r = c.gpr;
        while (cycles < budget)
        {
            const uint32_t pc = c.pc;
            const uint32_t op = m_bus.fetch(pc);
            ++cycles;

            // Leaving a delay slot takes the branch; otherwise fall through.
            uint32_t next = pc + 4u;
            if (c.branchPending)
            {
                next = c.branchTarget;
                c.branchPending = false;
            }

            // A load issued by the previous instruction lands after this one
            // reads its operands.
            const uint8_t delayedRegister = c.loadRegister;
            const uint32_t delayedValue = c.loadValue;
            c.loadRegister = 0;

            const uint32_t rs = (op >> 21) & 31u;
            const uint32_t rt = (op >> 16) & 31u;
            const uint32_t rd = (op >> 11) & 31u;
            const uint32_t imm = op & 0xFFFFu;
            const int32_t simm = signExtend16(op);
            uint32_t writeRegister = 0;
            uint32_t writeValue = 0;
            const auto set = [&](uint32_t reg, uint32_t value) {
                writeRegister = reg;
                writeValue = value;
            };
            const auto branch = [&](bool taken, uint32_t target) {
                if (taken)
                {
                    c.branchPending = true;
                    c.branchTarget = target;
                }
            };
            const auto load = [&](uint32_t reg, uint32_t value) {
                if (reg != 0u)
                {
                    c.loadRegister = static_cast<uint8_t>(reg);
                    c.loadValue = value;
                }
            };
            const uint32_t address = r[rs] + static_cast<uint32_t>(simm);

            switch (op >> 26)
            {
            case 0x00:
                switch (op & 63u)
                {
                case 0x00: set(rd, r[rt] << ((op >> 6) & 31u)); break;                       // SLL
                case 0x02: set(rd, r[rt] >> ((op >> 6) & 31u)); break;                       // SRL
                case 0x03: set(rd, static_cast<uint32_t>(static_cast<int32_t>(r[rt]) >> ((op >> 6) & 31u))); break; // SRA
                case 0x04: set(rd, r[rt] << (r[rs] & 31u)); break;                           // SLLV
                case 0x06: set(rd, r[rt] >> (r[rs] & 31u)); break;                           // SRLV
                case 0x07: set(rd, static_cast<uint32_t>(static_cast<int32_t>(r[rt]) >> (r[rs] & 31u))); break; // SRAV
                case 0x08: branch(true, r[rs]); break;                                       // JR
                case 0x09:                                                                   // JALR
                    set(rd, pc + 8u);
                    branch(true, r[rs]);
                    break;
                case 0x0C:                                                                   // SYSCALL
                    m_syscallCode = (op >> 6) & 0xFFFFFu;
                    c.pc = next;
                    if (delayedRegister != 0u)
                        r[delayedRegister] = delayedValue;
                    return StopReason::Syscall;
                case 0x0D: break;                                                            // BREAK
                case 0x10: set(rd, c.hi); break;                                             // MFHI
                case 0x11: c.hi = r[rs]; break;                                              // MTHI
                case 0x12: set(rd, c.lo); break;                                             // MFLO
                case 0x13: c.lo = r[rs]; break;                                              // MTLO
                case 0x18:                                                                   // MULT
                {
                    const int64_t product = static_cast<int64_t>(static_cast<int32_t>(r[rs])) *
                                            static_cast<int32_t>(r[rt]);
                    c.lo = static_cast<uint32_t>(product);
                    c.hi = static_cast<uint32_t>(static_cast<uint64_t>(product) >> 32);
                    break;
                }
                case 0x19:                                                                   // MULTU
                {
                    const uint64_t product = static_cast<uint64_t>(r[rs]) * r[rt];
                    c.lo = static_cast<uint32_t>(product);
                    c.hi = static_cast<uint32_t>(product >> 32);
                    break;
                }
                case 0x1A:                                                                   // DIV
                {
                    const int32_t n = static_cast<int32_t>(r[rs]), d = static_cast<int32_t>(r[rt]);
                    if (d == 0)
                    {
                        c.lo = n >= 0 ? 0xFFFFFFFFu : 1u;
                        c.hi = static_cast<uint32_t>(n);
                    }
                    else if (n == INT32_MIN && d == -1)
                    {
                        c.lo = static_cast<uint32_t>(INT32_MIN);
                        c.hi = 0;
                    }
                    else
                    {
                        c.lo = static_cast<uint32_t>(n / d);
                        c.hi = static_cast<uint32_t>(n % d);
                    }
                    break;
                }
                case 0x1B:                                                                   // DIVU
                    if (r[rt] == 0u)
                    {
                        c.lo = 0xFFFFFFFFu;
                        c.hi = r[rs];
                    }
                    else
                    {
                        c.lo = r[rs] / r[rt];
                        c.hi = r[rs] % r[rt];
                    }
                    break;
                // Overflow traps are never relied on by IOP drivers; wrap like ADDU.
                case 0x20: case 0x21: set(rd, r[rs] + r[rt]); break;                         // ADD, ADDU
                case 0x22: case 0x23: set(rd, r[rs] - r[rt]); break;                         // SUB, SUBU
                case 0x24: set(rd, r[rs] & r[rt]); break;                                    // AND
                case 0x25: set(rd, r[rs] | r[rt]); break;                                    // OR
                case 0x26: set(rd, r[rs] ^ r[rt]); break;                                    // XOR
                case 0x27: set(rd, ~(r[rs] | r[rt])); break;                                 // NOR
                case 0x2A: set(rd, static_cast<int32_t>(r[rs]) < static_cast<int32_t>(r[rt]) ? 1u : 0u); break; // SLT
                case 0x2B: set(rd, r[rs] < r[rt] ? 1u : 0u); break;                          // SLTU
                default:
                    m_faultPc = pc;
                    return StopReason::Fault;
                }
                break;
            case 0x01:                                                                       // REGIMM
            {
                const bool less = static_cast<int32_t>(r[rs]) < 0;
                const uint32_t target = pc + 4u + (static_cast<uint32_t>(simm) << 2);
                const bool link = (rt & 0x1Eu) == 0x10u;
                if (link)
                    set(31, pc + 8u);
                branch((rt & 1u) ? !less : less, target);                                    // BLTZ, BGEZ(AL)
                break;
            }
            case 0x02: branch(true, (pc & 0xF0000000u) | ((op & 0x03FFFFFFu) << 2)); break;  // J
            case 0x03:                                                                       // JAL
                set(31, pc + 8u);
                branch(true, (pc & 0xF0000000u) | ((op & 0x03FFFFFFu) << 2));
                break;
            case 0x04: branch(r[rs] == r[rt], pc + 4u + (static_cast<uint32_t>(simm) << 2)); break;  // BEQ
            case 0x05: branch(r[rs] != r[rt], pc + 4u + (static_cast<uint32_t>(simm) << 2)); break;  // BNE
            case 0x06: branch(static_cast<int32_t>(r[rs]) <= 0, pc + 4u + (static_cast<uint32_t>(simm) << 2)); break; // BLEZ
            case 0x07: branch(static_cast<int32_t>(r[rs]) > 0, pc + 4u + (static_cast<uint32_t>(simm) << 2)); break;  // BGTZ
            case 0x08: case 0x09: set(rt, r[rs] + static_cast<uint32_t>(simm)); break;       // ADDI, ADDIU
            case 0x0A: set(rt, static_cast<int32_t>(r[rs]) < simm ? 1u : 0u); break;         // SLTI
            case 0x0B: set(rt, r[rs] < static_cast<uint32_t>(simm) ? 1u : 0u); break;        // SLTIU
            case 0x0C: set(rt, r[rs] & imm); break;                                          // ANDI
            case 0x0D: set(rt, r[rs] | imm); break;                                          // ORI
            case 0x0E: set(rt, r[rs] ^ imm); break;                                          // XORI
            case 0x0F: set(rt, imm << 16); break;                                            // LUI
            case 0x10:                                                                       // COP0
                if (rs == 0x00u)
                    load(rt, rd == 12u ? m_status : 0u);                                     // MFC0
                else if (rs == 0x04u && rd == 12u)
                    m_status = r[rt];                                                        // MTC0 SR
                else if (rs == 0x10u && (op & 63u) == 0x10u)
                    m_status = (m_status & ~0xFu) | ((m_status >> 2) & 0xFu);                // RFE
                break;
            case 0x20: load(rt, static_cast<uint32_t>(static_cast<int8_t>(m_bus.read8(address)))); break;   // LB
            case 0x21: load(rt, static_cast<uint32_t>(static_cast<int16_t>(m_bus.read16(address)))); break; // LH
            case 0x22:                                                                       // LWL
            {
                const uint32_t word = m_bus.read32(address & ~3u);
                const uint32_t shift = (address & 3u) * 8u;
                const uint32_t current = delayedRegister == rt ? delayedValue : r[rt];
                const uint32_t mask = shift == 24u ? 0u : 0x00FFFFFFu >> shift;
                load(rt, (current & mask) | (word << (24u - shift)));
                break;
            }
            case 0x23: load(rt, m_bus.read32(address)); break;                               // LW
            case 0x24: load(rt, m_bus.read8(address)); break;                                // LBU
            case 0x25: load(rt, m_bus.read16(address)); break;                               // LHU
            case 0x26:                                                                       // LWR
            {
                const uint32_t word = m_bus.read32(address & ~3u);
                const uint32_t shift = (address & 3u) * 8u;
                const uint32_t current = delayedRegister == rt ? delayedValue : r[rt];
                const uint32_t mask = shift == 0u ? 0u : 0xFFFFFF00u << (24u - shift);
                load(rt, (current & mask) | (word >> shift));
                break;
            }
            case 0x28: m_bus.write8(address, static_cast<uint8_t>(r[rt])); break;            // SB
            case 0x29: m_bus.write16(address, static_cast<uint16_t>(r[rt])); break;          // SH
            case 0x2A:                                                                       // SWL
            {
                const uint32_t aligned = address & ~3u;
                const uint32_t shift = (address & 3u) * 8u;
                const uint32_t word = m_bus.read32(aligned);
                const uint32_t mask = shift == 24u ? 0u : 0xFFFFFF00u << shift;
                m_bus.write32(aligned, (word & mask) | (r[rt] >> (24u - shift)));
                break;
            }
            case 0x2B: m_bus.write32(address, r[rt]); break;                                 // SW
            case 0x2E:                                                                       // SWR
            {
                const uint32_t aligned = address & ~3u;
                const uint32_t shift = (address & 3u) * 8u;
                const uint32_t word = m_bus.read32(aligned);
                const uint32_t mask = shift == 0u ? 0u : 0x00FFFFFFu >> (24u - shift);
                m_bus.write32(aligned, (word & mask) | (r[rt] << shift));
                break;
            }
            case 0x2F: case 0x33: break;                                                     // CACHE, PREF
            default:
                m_faultPc = pc;
                return StopReason::Fault;
            }

            // An ALU result written in a load delay slot wins over the load.
            if (delayedRegister != 0u)
                r[delayedRegister] = delayedValue;
            if (writeRegister != 0u)
            {
                r[writeRegister] = writeValue;
                if (c.loadRegister == writeRegister)
                    c.loadRegister = 0;
            }
            r[0] = 0;
            c.pc = next;
        }
        return StopReason::Budget;
    }
}
