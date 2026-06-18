#include "runtime/ps2_iop_cpu.h"
#include "runtime/ps2_memory.h"

#include <cstring>
#include <iostream>

IopCpu::IopCpu(PS2Memory *mem) : m_mem(mem)
{
    reset();
}

void IopCpu::reset()
{
    std::memset(m_gpr, 0, sizeof(m_gpr));
    std::memset(m_cop0, 0, sizeof(m_cop0));
    m_hi = m_lo = 0;
    m_pc = 0;
    m_halted = false;
    m_haltPcSet = false;
    m_haltPc = 0;
    m_instrCount = 0;
    m_ioWarnCount = 0;
}

// ---- IOP bus -----------------------------------------------------------------
// IOP RAM is 2 MB at physical 0x00000000-0x001FFFFF (mirrored through KSEG).
// Anything else routes to the IO hooks (SIF/dev regs), else returns 0 / is
// dropped with a bounded warning.

uint8_t IopCpu::busRead8(uint32_t addr)
{
    const uint32_t phys = addr & 0x1FFFFFFFu;
    if (phys < PS2Memory::IOP_RAM_SIZE)
        return m_mem->iopRead8(addr);
    uint32_t out = 0;
    if (m_ioRead && m_ioRead(phys, out))
        return static_cast<uint8_t>(out);
    return 0;
}

uint16_t IopCpu::busRead16(uint32_t addr)
{
    const uint32_t phys = addr & 0x1FFFFFFFu;
    if (phys < PS2Memory::IOP_RAM_SIZE)
        return m_mem->iopRead16(addr);
    uint32_t out = 0;
    if (m_ioRead && m_ioRead(phys, out))
        return static_cast<uint16_t>(out);
    return 0;
}

uint32_t IopCpu::busRead32(uint32_t addr)
{
    const uint32_t phys = addr & 0x1FFFFFFFu;
    if (phys < PS2Memory::IOP_RAM_SIZE)
        return m_mem->iopRead32(addr);
    uint32_t out = 0;
    if (m_ioRead && m_ioRead(phys, out))
        return out;
    if (m_ioWarnCount < 32)
    {
        std::cerr << "[iop:cpu] unhandled read32 @0x" << std::hex << addr
                  << " (pc=0x" << m_pc << ")" << std::dec << std::endl;
        ++m_ioWarnCount;
    }
    return 0;
}

void IopCpu::busWrite8(uint32_t addr, uint8_t v)
{
    const uint32_t phys = addr & 0x1FFFFFFFu;
    if (phys < PS2Memory::IOP_RAM_SIZE)
    {
        m_mem->iopWrite8(addr, v);
        return;
    }
    if (m_ioWrite && m_ioWrite(phys, v))
        return;
}

void IopCpu::busWrite16(uint32_t addr, uint16_t v)
{
    const uint32_t phys = addr & 0x1FFFFFFFu;
    if (phys < PS2Memory::IOP_RAM_SIZE)
    {
        m_mem->iopWrite16(addr, v);
        return;
    }
    if (m_ioWrite && m_ioWrite(phys, v))
        return;
}

void IopCpu::busWrite32(uint32_t addr, uint32_t v)
{
    const uint32_t phys = addr & 0x1FFFFFFFu;
    if (phys < PS2Memory::IOP_RAM_SIZE)
    {
        m_mem->iopWrite32(addr, v);
        return;
    }
    if (m_ioWrite && m_ioWrite(phys, v))
        return;
    if (m_ioWarnCount < 32)
    {
        std::cerr << "[iop:cpu] unhandled write32 @0x" << std::hex << addr
                  << " =0x" << v << " (pc=0x" << m_pc << ")" << std::dec << std::endl;
        ++m_ioWarnCount;
    }
}

// ---- execution ---------------------------------------------------------------

void IopCpu::doBranch(uint32_t target)
{
    // R3000 has a single architectural delay slot: execute the next instruction,
    // then transfer control. (Matches PCSX2's doBranch -> execI -> set pc.)
    execOne();
    m_pc = target;
}

void IopCpu::trap(uint32_t excode)
{
    if (m_trap)
    {
        m_trap(*this, excode);
        return;
    }
    // No kernel HLE yet: stop rather than run wild.
    m_halted = true;
}

void IopCpu::execOne()
{
    const uint32_t code = busRead32(m_pc);
    const uint32_t curpc = m_pc;
    m_pc += 4;
    ++m_instrCount;

    const uint32_t op = code >> 26;
    const uint32_t rs = (code >> 21) & 31u;
    const uint32_t rt = (code >> 16) & 31u;
    const uint32_t rd = (code >> 11) & 31u;
    const uint32_t sa = (code >> 6) & 31u;
    const uint32_t funct = code & 0x3Fu;
    const uint16_t imm = static_cast<uint16_t>(code);
    const int32_t simm = static_cast<int16_t>(imm);
    const uint32_t branchTarget = curpc + 4u + (static_cast<uint32_t>(simm) << 2);

    auto R = [&](uint32_t i) -> uint32_t { return m_gpr[i]; };
    auto W = [&](uint32_t i, uint32_t v) { if (i) m_gpr[i] = v; };

    switch (op)
    {
    case 0x00: // SPECIAL
        switch (funct)
        {
        case 0x00: W(rd, R(rt) << sa); break;                                   // SLL
        case 0x02: W(rd, R(rt) >> sa); break;                                   // SRL
        case 0x03: W(rd, static_cast<uint32_t>(static_cast<int32_t>(R(rt)) >> sa)); break; // SRA
        case 0x04: W(rd, R(rt) << (R(rs) & 31u)); break;                        // SLLV
        case 0x06: W(rd, R(rt) >> (R(rs) & 31u)); break;                        // SRLV
        case 0x07: W(rd, static_cast<uint32_t>(static_cast<int32_t>(R(rt)) >> (R(rs) & 31u))); break; // SRAV
        case 0x08: doBranch(R(rs)); break;                                      // JR
        case 0x09: { const uint32_t t = R(rs); if (rd) W(rd, m_pc + 4u); doBranch(t); break; } // JALR
        case 0x0C: trap(0x20); break;                                           // SYSCALL
        case 0x0D: trap(0x24); break;                                           // BREAK
        case 0x10: W(rd, m_hi); break;                                          // MFHI
        case 0x11: m_hi = R(rs); break;                                         // MTHI
        case 0x12: W(rd, m_lo); break;                                          // MFLO
        case 0x13: m_lo = R(rs); break;                                         // MTLO
        case 0x18: { const int64_t r = static_cast<int64_t>(static_cast<int32_t>(R(rs))) * static_cast<int64_t>(static_cast<int32_t>(R(rt))); m_lo = static_cast<uint32_t>(r); m_hi = static_cast<uint32_t>(r >> 32); break; } // MULT
        case 0x19: { const uint64_t r = static_cast<uint64_t>(R(rs)) * static_cast<uint64_t>(R(rt)); m_lo = static_cast<uint32_t>(r); m_hi = static_cast<uint32_t>(r >> 32); break; } // MULTU
        case 0x1A: // DIV
        {
            const int32_t a = static_cast<int32_t>(R(rs));
            const int32_t b = static_cast<int32_t>(R(rt));
            if (b == 0) { m_lo = (a < 0) ? 1u : 0xFFFFFFFFu; m_hi = static_cast<uint32_t>(a); }
            else if (static_cast<uint32_t>(a) == 0x80000000u && b == -1) { m_lo = 0x80000000u; m_hi = 0; }
            else { m_lo = static_cast<uint32_t>(a / b); m_hi = static_cast<uint32_t>(a % b); }
            break;
        }
        case 0x1B: // DIVU
        {
            const uint32_t a = R(rs), b = R(rt);
            if (b == 0) { m_lo = 0xFFFFFFFFu; m_hi = a; }
            else { m_lo = a / b; m_hi = a % b; }
            break;
        }
        case 0x20: W(rd, R(rs) + R(rt)); break;                                 // ADD (no overflow trap)
        case 0x21: W(rd, R(rs) + R(rt)); break;                                 // ADDU
        case 0x22: W(rd, R(rs) - R(rt)); break;                                 // SUB
        case 0x23: W(rd, R(rs) - R(rt)); break;                                 // SUBU
        case 0x24: W(rd, R(rs) & R(rt)); break;                                 // AND
        case 0x25: W(rd, R(rs) | R(rt)); break;                                 // OR
        case 0x26: W(rd, R(rs) ^ R(rt)); break;                                 // XOR
        case 0x27: W(rd, ~(R(rs) | R(rt))); break;                              // NOR
        case 0x2A: W(rd, (static_cast<int32_t>(R(rs)) < static_cast<int32_t>(R(rt))) ? 1u : 0u); break; // SLT
        case 0x2B: W(rd, (R(rs) < R(rt)) ? 1u : 0u); break;                     // SLTU
        default: break;
        }
        break;

    case 0x01: // REGIMM (BCOND)
    {
        const bool lt = static_cast<int32_t>(R(rs)) < 0;
        const bool ge = static_cast<int32_t>(R(rs)) >= 0;
        switch (rt)
        {
        case 0x00: if (lt) doBranch(branchTarget); break;                       // BLTZ
        case 0x01: if (ge) doBranch(branchTarget); break;                       // BGEZ
        case 0x10: W(31, m_pc + 4u); if (lt) doBranch(branchTarget); break;     // BLTZAL
        case 0x11: W(31, m_pc + 4u); if (ge) doBranch(branchTarget); break;     // BGEZAL
        default: break;
        }
        break;
    }

    case 0x02: doBranch((m_pc & 0xF0000000u) | ((code & 0x03FFFFFFu) << 2)); break;                 // J
    case 0x03: W(31, m_pc + 4u); doBranch((m_pc & 0xF0000000u) | ((code & 0x03FFFFFFu) << 2)); break; // JAL
    case 0x04: if (R(rs) == R(rt)) doBranch(branchTarget); break;               // BEQ
    case 0x05: if (R(rs) != R(rt)) doBranch(branchTarget); break;               // BNE
    case 0x06: if (static_cast<int32_t>(R(rs)) <= 0) doBranch(branchTarget); break; // BLEZ
    case 0x07: if (static_cast<int32_t>(R(rs)) > 0) doBranch(branchTarget); break;  // BGTZ

    case 0x08: W(rt, R(rs) + static_cast<uint32_t>(simm)); break;               // ADDI (no overflow trap)
    case 0x09: W(rt, R(rs) + static_cast<uint32_t>(simm)); break;               // ADDIU
    case 0x0A: W(rt, (static_cast<int32_t>(R(rs)) < simm) ? 1u : 0u); break;    // SLTI
    case 0x0B: W(rt, (R(rs) < static_cast<uint32_t>(simm)) ? 1u : 0u); break;   // SLTIU
    case 0x0C: W(rt, R(rs) & imm); break;                                       // ANDI
    case 0x0D: W(rt, R(rs) | imm); break;                                       // ORI
    case 0x0E: W(rt, R(rs) ^ imm); break;                                       // XORI
    case 0x0F: W(rt, static_cast<uint32_t>(imm) << 16); break;                  // LUI

    case 0x10: // COP0
        switch (rs)
        {
        case 0x00: W(rt, m_cop0[rd]); break;                                    // MFC0
        case 0x02: W(rt, m_cop0[rd]); break;                                    // CFC0
        case 0x04: m_cop0[rd] = R(rt); break;                                   // MTC0
        case 0x06: m_cop0[rd] = R(rt); break;                                   // CTC0
        case 0x10: m_cop0[12] = (m_cop0[12] & 0xFFFFFFF0u) | ((m_cop0[12] & 0x3Cu) >> 2); break; // RFE
        default: break;
        }
        break;

    // ---- loads ----
    case 0x20: W(rt, static_cast<uint32_t>(static_cast<int8_t>(busRead8(R(rs) + simm)))); break;   // LB
    case 0x21: W(rt, static_cast<uint32_t>(static_cast<int16_t>(busRead16(R(rs) + simm)))); break; // LH
    case 0x22: // LWL
    {
        const uint32_t a = R(rs) + simm;
        const uint32_t sh = (a & 3u) << 3;
        const uint32_t mem = busRead32(a & ~3u);
        W(rt, (R(rt) & (0x00FFFFFFu >> sh)) | (mem << (24 - sh)));
        break;
    }
    case 0x23: W(rt, busRead32(R(rs) + simm)); break;                           // LW
    case 0x24: W(rt, busRead8(R(rs) + simm)); break;                            // LBU
    case 0x25: W(rt, busRead16(R(rs) + simm)); break;                           // LHU
    case 0x26: // LWR
    {
        const uint32_t a = R(rs) + simm;
        const uint32_t sh = (a & 3u) << 3;
        const uint32_t mem = busRead32(a & ~3u);
        W(rt, (R(rt) & (0xFFFFFF00u << (24 - sh))) | (mem >> sh));
        break;
    }

    // ---- stores ----
    case 0x28: busWrite8(R(rs) + simm, static_cast<uint8_t>(R(rt))); break;     // SB
    case 0x29: busWrite16(R(rs) + simm, static_cast<uint16_t>(R(rt))); break;   // SH
    case 0x2A: // SWL
    {
        const uint32_t a = R(rs) + simm;
        const uint32_t sh = (a & 3u) << 3;
        const uint32_t mem = busRead32(a & ~3u);
        busWrite32(a & ~3u, (R(rt) >> (24 - sh)) | (mem & (0xFFFFFF00u << sh)));
        break;
    }
    case 0x2B: busWrite32(R(rs) + simm, R(rt)); break;                          // SW
    case 0x2E: // SWR
    {
        const uint32_t a = R(rs) + simm;
        const uint32_t sh = (a & 3u) << 3;
        const uint32_t mem = busRead32(a & ~3u);
        busWrite32(a & ~3u, (R(rt) << sh) | (mem & (0x00FFFFFFu >> (24 - sh))));
        break;
    }

    default:
        // COP2/GTE and anything else: unimplemented, treated as nop.
        break;
    }

    m_gpr[0] = 0; // $zero stays hard-wired to 0
}

uint32_t IopCpu::run(uint32_t maxInstr)
{
    m_halted = false;
    const uint32_t start = m_instrCount;
    while ((m_instrCount - start) < maxInstr)
    {
        if (m_haltPcSet && m_pc == m_haltPc)
        {
            m_halted = true;
            break;
        }
        if (m_importHook && m_importHook(*this, m_pc))
        {
            // The hook serviced an IRX import stub (set $v0, set PC to $ra).
            ++m_instrCount;
            if (m_halted)
                break;
            continue;
        }
        execOne();
        if (m_halted)
            break;
    }
    return m_instrCount - start;
}

// ---- self-test ---------------------------------------------------------------

bool IopCpu::selfTest()
{
    if (!m_mem || !m_mem->getIOPRAM())
    {
        std::cerr << "[iop:cpu:selftest] FAIL: no IOP RAM" << std::endl;
        return false;
    }

    // Hand-assembled R3000 program: r2 = sum(1..10) = 55, store to mem[0x100],
    // then jr $ra (returns to the halt sentinel).
    static const uint32_t prog[] = {
        0x24020000u, // 0x00 addiu $v0,$zero,0
        0x24030001u, // 0x04 addiu $v1,$zero,1
        0x00431021u, // 0x08 loop: addu $v0,$v0,$v1
        0x24630001u, // 0x0C addiu $v1,$v1,1
        0x2861000Bu, // 0x10 slti  $at,$v1,11
        0x1420FFFCu, // 0x14 bne   $at,$zero,loop
        0x00000000u, // 0x18 nop (delay slot)
        0xAC020100u, // 0x1C sw    $v0,0x100($zero)
        0x03E00008u, // 0x20 jr    $ra
        0x00000000u, // 0x24 nop (delay slot)
    };

    const uint32_t base = 0x1000u;
    const uint32_t resultAddr = 0x0100u;
    const uint32_t sentinel = 0x00FFF000u;

    reset();
    for (uint32_t i = 0; i < sizeof(prog) / sizeof(prog[0]); ++i)
        m_mem->iopWrite32(base + i * 4u, prog[i]);
    m_mem->iopWrite32(resultAddr, 0u);

    setGpr(31, sentinel); // $ra
    setHaltPc(sentinel);
    setPC(base);

    const uint32_t retired = run(10000u);
    const uint32_t result = m_mem->iopRead32(resultAddr);
    const bool ok = halted() && (result == 55u);

    std::cerr << "[iop:cpu:selftest] instrs=" << retired
              << " sum(1..10)=" << result << " (expect 55)"
              << " haltedAtSentinel=" << (halted() ? "yes" : "no")
              << " => " << (ok ? "PASS" : "FAIL") << std::endl;

    // Clean up the scratch we wrote so the guest starts from zeroed IOP RAM.
    m_mem->iopWrite32(resultAddr, 0u);
    for (uint32_t i = 0; i < sizeof(prog) / sizeof(prog[0]); ++i)
        m_mem->iopWrite32(base + i * 4u, 0u);
    reset();

    return ok;
}
