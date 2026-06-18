#include "runtime/ps2_iop_kernel.h"
#include "runtime/ps2_iop_cpu.h"
#include "runtime/ps2_memory.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>

namespace
{
    constexpr uint32_t MODULE_BASE = 0x00010000u;       // IRX load base (leave low 64K for "kernel")
    constexpr uint32_t PARK_SENTINEL = 0x00FFF000u;
    constexpr uint32_t MODSTART_STACKTOP = 0x001F0000u; // module_start stack (grows down)
    constexpr uint32_t HEAP_BASE = 0x00050000u;         // sysmem bump heap (after the modules)
    constexpr uint32_t HEAP_LIMIT = 0x001E0000u;        // ~1.6 MB heap; clear of the stack
    constexpr uint32_t RUN_CAP = 1000000u;
}

IopKernel::IopKernel(PS2Memory *mem)
    : m_mem(mem), m_cpu(std::make_unique<IopCpu>(mem)), m_loader(mem),
      m_heapTop(HEAP_BASE), m_curGp(0), m_nextTid(1), m_parked(false), m_logBudget(200)
{
    m_loader.reset(MODULE_BASE);
    m_cpu->setImportHook([this](IopCpu &c, uint32_t addr) { return onImport(c, addr); });
    // IO hook: IOP hardware registers (SPU2/SSBUS/DMA) — accept silently for now.
    m_cpu->setIoHooks(
        [](uint32_t, uint32_t &out) { out = 0; return true; },
        [](uint32_t, uint32_t) { return true; });
}

IopKernel::~IopKernel() = default;

uint32_t IopKernel::sysAlloc(uint32_t size, uint32_t align)
{
    if (align == 0)
        align = 4;
    uint32_t a = (m_heapTop + (align - 1u)) & ~(align - 1u);
    const uint32_t end = a + ((size + 0xFu) & ~0xFu);
    if (end > HEAP_LIMIT)
    {
        std::cerr << "[iop:kernel] sysAlloc OOM size=0x" << std::hex << size << std::dec << std::endl;
        return 0;
    }
    m_heapTop = end;
    return a;
}

// Which loaded module owns this IOP address (for gp switching on a cross-module call).
static uint32_t gpForAddr(const std::vector<IopModule> &mods, uint32_t addr)
{
    for (const auto &m : mods)
        if (addr >= m.base && addr < m.base + m.imageSize)
            return m.gp;
    return 0;
}

bool IopKernel::onImport(IopCpu &c, uint32_t stubAddr)
{
    auto it = m_stubs.find(stubAddr);
    if (it == m_stubs.end())
        return false;
    const IopImportStub &s = it->second;

    // Cross-module call: the imported library is provided by a loaded module.
    auto ex = m_exports.find(s.lib);
    if (ex != m_exports.end() && s.index < ex->second.size() && ex->second[s.index] != 0)
    {
        const uint32_t target = ex->second[s.index];
        c.setPC(target);
        const uint32_t gp = gpForAddr(m_modules, target);
        if (gp)
        {
            c.setGpr(28, gp); // $gp for the callee's module
            m_curGp = gp;
        }
        return true; // run the real function; it returns via the caller's $ra
    }

    // Otherwise it's a kernel import -> HLE.
    return kernelHle(c, s.lib, s.index);
}

void IopKernel::runToHalt(uint32_t entry, uint32_t a0, uint32_t a1, uint32_t gp,
                          const char *what)
{
    const IopCpu::State saved = m_cpu->saveState();
    const bool savedParked = m_parked;
    const uint32_t savedGp = m_curGp;

    // Fresh frame for this routine.
    for (int i = 1; i < 32; ++i)
        m_cpu->setGpr(i, 0);
    m_cpu->setGpr(29, MODSTART_STACKTOP); // $sp (module_start); threads override below
    m_cpu->setGpr(28, gp);                // $gp
    m_cpu->setGpr(4, a0);
    m_cpu->setGpr(5, a1);
    m_cpu->setGpr(31, PARK_SENTINEL);     // $ra
    m_cpu->setPC(entry);
    m_cpu->setHaltPc(PARK_SENTINEL);
    m_curGp = gp;
    m_parked = false;

    const uint32_t n = m_cpu->run(RUN_CAP);
    if (m_logBudget)
    {
        std::cerr << "[iop:kernel] ran " << what << " entry=0x" << std::hex << entry
                  << " stopPc=0x" << m_cpu->pc() << std::dec
                  << " instrs=" << n
                  << (m_parked ? " (parked)" : (m_cpu->pc() == PARK_SENTINEL ? " (returned)" : " (capped)"))
                  << std::endl;
        --m_logBudget;
    }

    m_cpu->restoreState(saved);
    m_parked = savedParked;
    m_curGp = savedGp;
}

// ---- kernel HLE -------------------------------------------------------------

bool IopKernel::kernelHle(IopCpu &c, const std::string &lib, uint16_t idx)
{
    auto R = [&](int i) { return c.gpr(i); };
    auto ret = [&](uint32_t v) { c.setGpr(2, v); c.setPC(c.gpr(31)); };
    auto memb = m_mem;

    {
        // Log each distinct (lib,index) once to keep the trace readable.
        const std::string key = lib + "[" + std::to_string(idx) + "]";
        if (m_hleLogged.insert(key).second)
        {
            std::cerr << "[iop:kernel] hle " << key
                      << " a0=0x" << std::hex << R(4) << " a1=0x" << R(5) << " a2=0x" << R(6)
                      << std::dec << std::endl;
        }
    }

    if (lib == "intrman")
    {
        // suspend/resume return previous state; everything else returns ok.
        ret((idx == 17 || idx == 18 || idx == 19 || idx == 20) ? 1u : 0u);
        return true;
    }
    if (lib == "loadcore")
    {
        if (idx == 6) // RegisterLibraryEntries(a0 = export table)
        {
            const uint32_t tbl = R(4);
            char name[9] = {0};
            for (int k = 0; k < 8; ++k)
                name[k] = static_cast<char>(memb->iopRead8(tbl + 0x0Cu + k));
            for (int k = 0; k < 8; ++k)
                if (name[k] == ' ')
                    name[k] = 0;
            std::string lname(name);
            std::vector<uint32_t> funcs;
            for (uint32_t e = 0;; ++e)
            {
                const uint32_t fp = memb->iopRead32(tbl + 0x14u + e * 4u);
                if (fp == 0)
                    break;
                funcs.push_back(fp);
                if (e > 1024)
                    break;
            }
            if (!lname.empty())
            {
                m_exports[lname] = funcs;
                if (m_logBudget)
                {
                    std::cerr << "[iop:kernel] RegisterLibraryEntries '" << lname << "' funcs="
                              << funcs.size() << std::endl;
                    --m_logBudget;
                }
            }
            ret(0);
            return true;
        }
        ret(0);
        return true;
    }
    if (lib == "sysmem")
    {
        if (idx == 4) // AllocSysMemory(mode, size, addr)
        {
            ret(sysAlloc(R(5)));
            return true;
        }
        if (idx == 6 || idx == 7 || idx == 8) // QueryMemSize / free sizes
        {
            ret(HEAP_LIMIT - m_heapTop);
            return true;
        }
        ret(0);
        return true;
    }
    if (lib == "sysclib")
    {
        const uint32_t a0 = R(4), a1 = R(5), a2 = R(6);
        switch (idx)
        {
        case 4: ret(0); return true;                 // setjmp -> 0 (normal path)
        case 12:                                     // memcpy(dst,src,n)
        case 13:                                     // memmove(dst,src,n)
            for (uint32_t i = 0; i < a2; ++i)
                memb->iopWrite8(a0 + i, memb->iopRead8(a1 + i));
            ret(a0);
            return true;
        case 14:                                     // memset(dst,c,n)
            for (uint32_t i = 0; i < a2; ++i)
                memb->iopWrite8(a0 + i, static_cast<uint8_t>(a1));
            ret(a0);
            return true;
        case 16:                                     // bcopy(src,dst,n)
            for (uint32_t i = 0; i < a2; ++i)
                memb->iopWrite8(a1 + i, memb->iopRead8(a0 + i));
            ret(0);
            return true;
        case 17:                                     // bzero(dst,n)
            for (uint32_t i = 0; i < a1; ++i)
                memb->iopWrite8(a0 + i, 0);
            ret(0);
            return true;
        case 27:                                     // strlen
        {
            uint32_t n = 0;
            while (memb->iopRead8(a0 + n) && n < 0x10000u)
                ++n;
            ret(n);
            return true;
        }
        case 23:                                     // strcpy(dst,src)
        {
            uint32_t i = 0;
            for (;; ++i)
            {
                const uint8_t ch = memb->iopRead8(a1 + i);
                memb->iopWrite8(a0 + i, ch);
                if (!ch)
                    break;
                if (i > 0x10000u)
                    break;
            }
            ret(a0);
            return true;
        }
        default: ret(0); return true;
        }
    }
    if (lib == "thbase")
    {
        if (idx == 4) // CreateThread(ThreadParam*) -> tid
        {
            const uint32_t tp = R(4);
            const uint32_t entry = memb->iopRead32(tp + 0x08u);
            uint32_t stackSize = memb->iopRead32(tp + 0x0Cu);
            if (stackSize < 0x800u || stackSize > 0x20000u)
                stackSize = 0x4000u;
            const uint32_t stackBase = sysAlloc(stackSize, 0x100u);
            Thread t;
            t.tid = m_nextTid++;
            t.entry = entry;
            t.stackTop = stackBase ? (stackBase + stackSize) : MODSTART_STACKTOP;
            t.gp = gpForAddr(m_modules, entry);
            m_threads.push_back(t);
            ret(t.tid);
            return true;
        }
        if (idx == 6 || idx == 7) // StartThread(tid, arg) / StartThreadArgs
        {
            const uint32_t tid = R(4);
            const uint32_t arg = R(5);
            Thread *th = nullptr;
            for (auto &t : m_threads)
                if (t.tid == tid)
                    th = &t;
            if (th)
            {
                // Run the thread on its own stack until it returns or parks.
                const IopCpu::State saved = m_cpu->saveState();
                for (int i = 1; i < 32; ++i)
                    m_cpu->setGpr(i, 0);
                m_cpu->setGpr(29, th->stackTop);
                m_cpu->setGpr(28, th->gp);
                m_cpu->setGpr(4, arg);
                m_cpu->setGpr(31, PARK_SENTINEL);
                m_cpu->setPC(th->entry);
                m_cpu->setHaltPc(PARK_SENTINEL);
                const uint32_t savedGp = m_curGp;
                m_curGp = th->gp;
                m_parked = false;
                const uint32_t n = m_cpu->run(RUN_CAP);
                if (m_logBudget)
                {
                    std::cerr << "[iop:kernel] thread tid=" << tid << " entry=0x" << std::hex
                              << th->entry << " stopPc=0x" << m_cpu->pc() << std::dec
                              << " instrs=" << n << (m_parked ? " (parked)" : " (returned/capped)")
                              << std::endl;
                    --m_logBudget;
                }
                m_cpu->restoreState(saved);
                m_curGp = savedGp;
                m_parked = false;
                th->started = true;
            }
            ret(0);
            return true;
        }
        if (idx == 20) { ret(1); return true; }       // GetThreadId
        if (idx == 24) { m_parked = true; c.requestHalt(); return true; } // SleepThread (park)
        ret(0); // DelayThread, Wakeup, etc.
        return true;
    }
    if (lib == "thsemap" || lib == "thevent")
    {
        // CreateSema/CreateEventFlag (index 4) return a positive id; waits return ok.
        ret((idx == 4) ? m_nextTid++ : 0u);
        return true;
    }
    if (lib == "sifman")
    {
        ret((idx == 7) ? 1u : 0u); // sceSifSetDma -> fake transfer id
        return true;
    }
    if (lib == "sifcmd")
    {
        if (idx == 17) // sceSifRegisterRpc(sd, sid, func, buf, cfunc, cbuf, qd)
        {
            RpcServer srv;
            srv.sid = R(5);
            srv.func = R(6);
            srv.buf = R(7);
            m_rpcServers.push_back(srv);
            if (m_logBudget)
            {
                std::cerr << "[iop:kernel] RegisterRpc sid=0x" << std::hex << srv.sid
                          << " func=0x" << srv.func << std::dec << std::endl;
                --m_logBudget;
            }
            ret(R(4));
            return true;
        }
        if (idx == 20 || idx == 22) // GetNextRequest / RpcLoop -> server blocks here
        {
            m_parked = true;
            c.requestHalt();
            return true;
        }
        ret(0);
        return true;
    }

    // Unknown kernel library/function: log once and return 0.
    if (m_logBudget)
    {
        std::cerr << "[iop:kernel] unhandled " << lib << "[" << idx << "] -> 0" << std::endl;
        --m_logBudget;
    }
    ret(0);
    return true;
}

IopModule IopKernel::loadAndStart(const std::string &hostPath)
{
    IopModule mod = m_loader.loadFile(hostPath);
    if (!mod.valid)
    {
        std::cerr << "[iop:kernel] load failed: " << hostPath << std::endl;
        return mod;
    }
    for (const auto &s : mod.stubs)
        m_stubs[s.addr] = s;
    if (m_loader.allocTop() > m_heapTop)
        m_heapTop = (m_loader.allocTop() + 0xFFFu) & ~0xFFFu;
    if (m_heapTop < HEAP_BASE)
        m_heapTop = HEAP_BASE;
    m_modules.push_back(mod);

    runToHalt(mod.entry, 0, 0, mod.gp, mod.name.c_str());
    return mod;
}

bool iopKernelTest(PS2Memory *mem)
{
    const char *paths = std::getenv("PS2_IOP_KERNEL_TEST");
    if (!paths || !*paths)
        return false;
    if (!mem || !mem->getIOPRAM())
        return false;

    IopKernel kernel(mem);
    std::stringstream ss(paths);
    std::string path;
    bool any = false;
    while (std::getline(ss, path, ','))
    {
        if (path.empty())
            continue;
        IopModule m = kernel.loadAndStart(path);
        if (m.valid)
            any = true;
    }
    std::cerr << "[iop:kernel] done. RPC servers registered: " << kernel.rpcServers().size();
    for (const auto &s : kernel.rpcServers())
        std::cerr << " sid=0x" << std::hex << s.sid << std::dec;
    std::cerr << std::endl;

    if (any)
        std::memset(mem->getIOPRAM() + 0x00010000u, 0, 0x001D0000u);
    return any;
}
