#ifndef PS2_IOP_CPU_H
#define PS2_IOP_CPU_H

#include <cstdint>
#include <functional>

class PS2Memory;

// Minimal R3000A (MIPS-I) interpreter for the PS2 IOP. Executes IOP code out of
// the IOP's 2 MB RAM (through PS2Memory's IOP-RAM accessors). It implements the
// full MIPS-I integer ISA + COP0 basics; it deliberately omits COP2/GTE (the PS1
// geometry unit, unused by the audio/cdvd/pad IRX modules we need).
//
// syscall/break trap out to an optional handler -- that is where the IOP kernel
// HLE (Marco 4) will service module calls. A "halt PC" sentinel lets callers run
// an IOP routine and stop when it returns.
class IopCpu
{
public:
    explicit IopCpu(PS2Memory *mem);

    void reset();

    void setPC(uint32_t pc) { m_pc = pc; }
    uint32_t pc() const { return m_pc; }
    uint32_t gpr(int i) const { return m_gpr[i & 31]; }
    void setGpr(int i, uint32_t v) { if (i & 31) m_gpr[i & 31] = v; }
    uint32_t hi() const { return m_hi; }
    uint32_t lo() const { return m_lo; }

    // Run up to maxInstr instructions, stopping early on: the halt-PC sentinel,
    // requestHalt(), or a trap with no handler. Returns instructions retired.
    uint32_t run(uint32_t maxInstr);

    bool halted() const { return m_halted; }
    void requestHalt() { m_halted = true; }
    void setHaltPc(uint32_t addr) { m_haltPc = addr; m_haltPcSet = true; }

    // syscall (excode 0x20) / break (0x24) trap. The handler may inspect/modify
    // registers and set the PC (e.g. to $ra) before returning.
    using TrapFn = std::function<void(IopCpu &, uint32_t excode)>;
    void setTrapHandler(TrapFn fn) { m_trap = std::move(fn); }

    // Hardware-register hooks for IOP addresses outside RAM (SIF, dev9, timers,
    // ...). Return true if the access was handled. Filled in by Marco 4.
    using IoReadFn = std::function<bool(uint32_t addr, uint32_t &out)>;
    using IoWriteFn = std::function<bool(uint32_t addr, uint32_t val)>;
    void setIoHooks(IoReadFn r, IoWriteFn w) { m_ioRead = std::move(r); m_ioWrite = std::move(w); }

    // Import-stub hook: called with the current PC before each instruction. If
    // the PC is an IRX import stub, the hook services the imported function as
    // an HLE call (sets $v0, sets PC to $ra) and returns true so the CPU skips
    // the stub body. This is how kernel imports (loadcore/thbase/sifcmd/...) are
    // resolved without an IOP BIOS.
    using ImportHook = std::function<bool(IopCpu &, uint32_t addr)>;
    void setImportHook(ImportHook h) { m_importHook = std::move(h); }

    // IOP bus access (RAM + IO hook). Public so the loader/HLE can poke IOP RAM.
    uint8_t busRead8(uint32_t addr);
    uint16_t busRead16(uint32_t addr);
    uint32_t busRead32(uint32_t addr);
    void busWrite8(uint32_t addr, uint8_t v);
    void busWrite16(uint32_t addr, uint16_t v);
    void busWrite32(uint32_t addr, uint32_t v);

    // Foundation self-test: runs a hand-assembled sum(1..10) program in IOP RAM
    // and checks the result. Returns true on PASS (logs a one-line summary).
    bool selfTest();

private:
    void execOne();
    void doBranch(uint32_t target);
    void trap(uint32_t excode);

    PS2Memory *m_mem = nullptr;
    uint32_t m_gpr[32] = {};
    uint32_t m_hi = 0;
    uint32_t m_lo = 0;
    uint32_t m_pc = 0;
    uint32_t m_cop0[32] = {};
    bool m_halted = false;
    bool m_haltPcSet = false;
    uint32_t m_haltPc = 0;
    uint32_t m_instrCount = 0;
    uint32_t m_ioWarnCount = 0;
    TrapFn m_trap;
    IoReadFn m_ioRead;
    IoWriteFn m_ioWrite;
    ImportHook m_importHook;
};

#endif // PS2_IOP_CPU_H
