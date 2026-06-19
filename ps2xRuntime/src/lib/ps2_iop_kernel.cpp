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
      m_heapTop(HEAP_BASE), m_curGp(0), m_nextTid(1), m_parked(false), m_curThread(-1),
      m_logBudget(200)
{
    std::memset(m_iopSreg, 0, sizeof(m_iopSreg));
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

bool IopKernel::serviceRpc(uint32_t sid, uint32_t rpcNum,
                           const uint8_t *sendData, uint32_t sendSize,
                           uint8_t *recvData, uint32_t recvSize)
{
    const RpcServer *srv = nullptr;
    for (const auto &s : m_rpcServers)
        if (s.sid == sid)
        {
            srv = &s;
            break;
        }
    if (!srv || !srv->func)
        return false;

    // Place the EE send data into the server's IOP receive buffer (or a scratch alloc).
    uint32_t iopBuf = srv->buf;
    if (!iopBuf)
        iopBuf = sysAlloc(sendSize < 0x400u ? 0x400u : sendSize);
    if (sendData)
        for (uint32_t i = 0; i < sendSize; ++i)
            m_mem->iopWrite8(iopBuf + i, sendData[i]);

    const uint32_t gp = gpForAddr(m_modules, srv->func);

    const IopCpu::State saved = m_cpu->saveState();
    const bool savedParked = m_parked;
    const uint32_t savedGp = m_curGp;
    const int savedCur = m_curThread;

    for (int i = 1; i < 32; ++i)
        m_cpu->setGpr(i, 0);
    m_cpu->setGpr(29, MODSTART_STACKTOP);
    m_cpu->setGpr(28, gp);
    m_cpu->setGpr(4, rpcNum);   // fno
    m_cpu->setGpr(5, iopBuf);   // buf (holds the EE send data)
    m_cpu->setGpr(6, sendSize); // size
    m_cpu->setGpr(31, PARK_SENTINEL);
    m_cpu->setPC(srv->func);
    m_cpu->setHaltPc(PARK_SENTINEL);
    m_curGp = gp;
    m_curThread = -1;
    m_parked = false;
    m_inRpc = true;

    const uint32_t n = m_cpu->run(RUN_CAP);
    m_inRpc = false;
    const uint32_t resultPtr = m_cpu->gpr(2);
    const bool parked = m_parked;

    std::cerr << "[iop:kernel] serviceRpc sid=0x" << std::hex << sid << " rpc=0x" << rpcNum
              << " buf=0x" << iopBuf << " -> v0=0x" << resultPtr << std::dec
              << " instrs=" << n
              << (parked ? " (parked)" : (m_cpu->pc() == PARK_SENTINEL ? " (returned)" : " (capped)"))
              << std::endl;

    if (resultPtr && recvData)
        for (uint32_t i = 0; i < recvSize; ++i)
            recvData[i] = m_mem->iopRead8(resultPtr + i);

    m_cpu->restoreState(saved);
    m_parked = savedParked;
    m_curGp = savedGp;
    m_curThread = savedCur;

    // Cooperative scheduler: let any deferred streaming worker + producer make
    // background progress between EE RPC calls so the async load completes.
    runDeferredThreads();
    return true;
}

uint32_t IopKernel::cdRead(uint32_t lsn, uint32_t nsec, uint32_t iopBuf)
{
    if (!m_cdImageTried)
    {
        m_cdImageTried = true;
        if (const char *path = std::getenv("PS2_CD_IMAGE"); path && *path)
        {
            auto f = std::make_unique<std::ifstream>(path, std::ios::binary);
            if (f->is_open())
            {
                m_cdImage = std::move(f);
                std::cerr << "[iop:cdvd] disc image: " << path << std::endl;
            }
            else
                std::cerr << "[iop:cdvd] FAILED to open PS2_CD_IMAGE=" << path << std::endl;
        }
        else
            std::cerr << "[iop:cdvd] PS2_CD_IMAGE not set; sceCdRead returns empty" << std::endl;
    }
    if (!m_cdImage)
        return 0;

    const uint64_t off = static_cast<uint64_t>(lsn) * 2048ull;
    const uint32_t bytes = nsec * 2048u;
    std::vector<uint8_t> sec(bytes);
    m_cdImage->clear();
    m_cdImage->seekg(static_cast<std::streamoff>(off));
    m_cdImage->read(reinterpret_cast<char *>(sec.data()), bytes);
    const uint32_t got = static_cast<uint32_t>(m_cdImage->gcount());
    for (uint32_t i = 0; i < got; ++i)
        m_mem->iopWrite8(iopBuf + i, sec[i]);

    static uint32_t cdLog = 0;
    if (cdLog < 24)
    {
        std::cerr << "[iop:cdvd] sceCdRead lsn=0x" << std::hex << lsn << " nsec=" << std::dec << nsec
                  << " -> iopBuf=0x" << std::hex << iopBuf << " got=" << std::dec << got
                  << std::hex << " [" << (got>=4?*reinterpret_cast<uint32_t*>(sec.data()):0u) << "]"
                  << std::dec << std::endl;
        ++cdLog;
    }
    return got;
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
            int ti = -1;
            for (size_t k = 0; k < m_threads.size(); ++k)
                if (m_threads[k].tid == tid)
                {
                    ti = static_cast<int>(k);
                    break;
                }
            if (ti >= 0)
            {
                // Async thread model (PS2_IOP_DEFERTHREAD): when a thread is
                // StartThread'd from *inside* an EE->IOP RPC dispatch, register it
                // as started but do NOT run it inline. The default inline model runs
                // the new thread to completion/park immediately, which deadlocks an
                // RPC whose handler spawns a streaming/worker thread that then
                // busy-waits on a producer that cannot run concurrently. Deferring
                // lets the dispatcher finish and return the load handle (async on
                // real HW); the worker is advanced later by runDeferredThreads().
                // Only deferred during RPC (m_inRpc) so module_start, which legitimately
                // needs its init threads to run, is unaffected.
                static const bool s_defer = std::getenv("PS2_IOP_DEFERTHREAD") != nullptr;
                if (s_defer && m_inRpc)
                {
                    m_threads[ti].started = true;
                    m_threads[ti].startArg = arg;
                    m_threads[ti].live = false; // run fresh from entry on first slice
                    ret(0);
                    return true;
                }
                // Run the thread on its own stack until it returns or parks.
                const IopCpu::State saved = m_cpu->saveState();
                const uint32_t savedGp = m_curGp;
                const int savedCur = m_curThread;
                for (int i = 1; i < 32; ++i)
                    m_cpu->setGpr(i, 0);
                m_cpu->setGpr(29, m_threads[ti].stackTop);
                m_cpu->setGpr(28, m_threads[ti].gp);
                m_cpu->setGpr(4, arg);
                m_cpu->setGpr(31, PARK_SENTINEL);
                m_cpu->setPC(m_threads[ti].entry);
                m_cpu->setHaltPc(PARK_SENTINEL);
                m_curGp = m_threads[ti].gp;
                m_curThread = ti;
                m_parked = false;
                const uint32_t n = m_cpu->run(RUN_CAP);
                const bool parked = m_parked;
                if (parked)
                {
                    m_threads[ti].parked = true;
                    m_threads[ti].savedState = m_cpu->saveState();
                }
                m_threads[ti].started = true;
                if (m_logBudget)
                {
                    std::cerr << "[iop:kernel] thread tid=" << tid << " entry=0x" << std::hex
                              << m_threads[ti].entry << " stopPc=0x" << m_cpu->pc() << std::dec
                              << " instrs=" << n;
                    if (parked)
                        std::cerr << " (parked, waitSreg=0x" << std::hex << m_threads[ti].waitSreg << std::dec << ")";
                    else
                        std::cerr << " (returned/capped)";
                    std::cerr << std::endl;
                    --m_logBudget;
                }
                m_cpu->restoreState(saved);
                m_curGp = savedGp;
                m_curThread = savedCur;
                m_parked = false;
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
    if (lib == "stdio")
    {
        // printf/Kprintf family (idx 4..7): print the format string so module
        // diagnostics (e.g. why an RPC request is rejected) are visible. Varargs
        // are not expanded; %-specifiers are left literal.
        if (idx >= 4 && idx <= 8)
        {
            std::string s;
            for (uint32_t k = 0; k < 256u; ++k)
            {
                const uint8_t ch = memb->iopRead8(R(4) + k);
                if (!ch) break;
                s += static_cast<char>(ch);
            }
            std::cerr << "[iop:stdio] " << s;
            if (s.find('%') != std::string::npos)
                std::cerr << "   [args a1=0x" << std::hex << R(5) << " a2=0x" << R(6)
                          << " a3=0x" << R(7) << std::dec << "]";
            std::cerr << std::endl;
        }
        ret(0);
        return true;
    }
    if (lib == "cdvdman")
    {
        // Disc reads for an IOP file/streaming driver. Standard cdvdman export order:
        // 4 sceCdInit, 6 sceCdRead(lsn,sectors,buf,mode), 8 sceCdGetError, 11 sceCdSync,
        // 12 sceCdGetDiskType. Reads come from the disc image (env PS2_CD_IMAGE).
        switch (idx)
        {
        case 6: // sceCdRead(lsn, sectors, buf, mode) -> 1 on success
            cdRead(R(4), R(5), R(6));
            ret(1);
            return true;
        case 8:  ret(0);    return true; // sceCdGetError -> 0 (no error)
        case 11: ret(0);    return true; // sceCdSync(mode) -> 0 (idle/complete)
        case 12: ret(0x14); return true; // sceCdGetDiskType -> SCECdPS2DVD
        default: ret(1);    return true; // sceCdInit/Standby/Seek/Stop/... -> ok
        }
    }
    if (lib == "sifman")
    {
        ret((idx == 7) ? 1u : 0u); // sceSifSetDma -> fake transfer id
        return true;
    }
    if (lib == "sifcmd")
    {
        switch (idx)
        {
        case 6: // sceSifGetSreg(idx) -- poll an IOP soft register
        {
            const uint16_t r = static_cast<uint16_t>(R(4) & 31u);
            if (m_iopSreg[r] != 0u)
            {
                ret(m_iopSreg[r]);
                return true;
            }
            // Not set yet: park this thread polling sreg r; it re-polls on resume.
            if (m_curThread >= 0)
            {
                m_threads[m_curThread].waitSreg = r;
                m_parked = true;
                c.requestHalt();
                return true; // leave PC at the stub so GetSreg re-runs on resume
            }
            ret(0);
            return true;
        }
        case 7: // sceSifSetSreg(idx, val)
            m_iopSreg[R(4) & 31u] = R(5);
            ret(0);
            return true;
        case 12: // sceSifSendCmd(cid, packet, size, ...)
        case 13: // isceSifSendCmd
        {
            if (R(4) == 0x80000001u) // SET_SREG back to the EE
            {
                const uint32_t pkt = R(5);
                const uint16_t sidx = static_cast<uint16_t>(memb->iopRead32(pkt + 0x10u) & 31u);
                const uint32_t sval = memb->iopRead32(pkt + 0x14u);
                if (m_logBudget)
                {
                    std::cerr << "[iop:kernel] IOP SET_SREG -> EE sreg[" << sidx << "]=0x"
                              << std::hex << sval << std::dec << std::endl;
                    --m_logBudget;
                }
                if (m_eeSregWriter)
                    m_eeSregWriter(sidx, sval);
            }
            ret(1);
            return true;
        }
        case 17: // sceSifRegisterRpc(sd, sid, func, buf, cfunc, cbuf, qd)
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
        case 20: // sceSifGetNextRequest
        case 22: // sceSifRpcLoop -> server blocks here
            m_parked = true;
            c.requestHalt();
            return true;
        default:
            ret(0);
            return true;
        }
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

void IopKernel::resumeThread(size_t ti)
{
    if (ti >= m_threads.size() || !m_threads[ti].parked)
        return;

    const IopCpu::State saved = m_cpu->saveState();
    const uint32_t savedGp = m_curGp;
    const int savedCur = m_curThread;

    m_cpu->restoreState(m_threads[ti].savedState);
    m_cpu->setHaltPc(PARK_SENTINEL);
    m_curGp = m_threads[ti].gp;
    m_curThread = static_cast<int>(ti);
    m_threads[ti].parked = false;
    m_threads[ti].waitSreg = 0xFFFFu;
    m_parked = false;

    const uint32_t n = m_cpu->run(RUN_CAP);
    const bool parked = m_parked;
    if (parked)
    {
        m_threads[ti].parked = true;
        m_threads[ti].savedState = m_cpu->saveState();
    }
    if (m_logBudget)
    {
        std::cerr << "[iop:kernel] resumed tid=" << m_threads[ti].tid
                  << " stopPc=0x" << std::hex << m_cpu->pc() << std::dec
                  << " instrs=" << n
                  << (parked ? " (parked again)" : " (returned/capped)") << std::endl;
        --m_logBudget;
    }

    m_cpu->restoreState(saved);
    m_curGp = savedGp;
    m_curThread = savedCur;
    m_parked = false;
}

uint32_t IopKernel::runThreadSlice(size_t ti, uint32_t slice)
{
    if (ti >= m_threads.size())
        return 0;
    Thread &t = m_threads[ti];
    if (t.finished)
        return 0;

    const IopCpu::State saved = m_cpu->saveState();
    const uint32_t savedGp = m_curGp;
    const int savedCur = m_curThread;
    const bool savedParked = m_parked;

    if (t.live)
    {
        m_cpu->restoreState(t.savedState);
    }
    else
    {
        for (int i = 1; i < 32; ++i)
            m_cpu->setGpr(i, 0);
        m_cpu->setGpr(29, t.stackTop ? t.stackTop : MODSTART_STACKTOP);
        m_cpu->setGpr(28, t.gp);
        m_cpu->setGpr(4, t.startArg);
        m_cpu->setGpr(31, PARK_SENTINEL);
        m_cpu->setPC(t.entry);
    }
    m_cpu->setHaltPc(PARK_SENTINEL);
    m_curGp = t.gp;
    m_curThread = static_cast<int>(ti);
    m_parked = false;

    const uint32_t n = m_cpu->run(slice);
    if (m_cpu->pc() == PARK_SENTINEL)
    {
        t.finished = true;
        t.live = false;
        t.parked = false;
    }
    else
    {
        t.savedState = m_cpu->saveState();
        t.live = true;
        t.parked = m_parked;
    }

    m_cpu->restoreState(saved);
    m_curGp = savedGp;
    m_curThread = savedCur;
    m_parked = savedParked;
    return n;
}

void IopKernel::runDeferredThreads()
{
    static const bool s_coop = std::getenv("PS2_IOP_COOP") != nullptr;
    if (!s_coop)
        return;
    static const uint32_t slice = []
    {
        const char *e = std::getenv("PS2_IOP_COOP_SLICE");
        return e ? static_cast<uint32_t>(std::strtoul(e, nullptr, 0)) : 300000u;
    }();
    static const int rounds = []
    {
        const char *e = std::getenv("PS2_IOP_COOP_ROUNDS");
        return e ? std::atoi(e) : 8;
    }();

    for (int r = 0; r < rounds; ++r)
    {
        bool anyLive = false;
        for (size_t k = 0; k < m_threads.size(); ++k)
        {
            const Thread &t = m_threads[k];
            if (t.finished)
                continue;
            if (!t.started && !t.parked && !t.live)
                continue;
            runThreadSlice(k, slice);
            if (!m_threads[k].finished)
                anyLive = true;
        }
        if (!anyLive)
            break;
    }
}

bool IopKernel::setIopSregAndResume(uint16_t idx, uint32_t val)
{
    if (idx < 32)
        m_iopSreg[idx] = val;
    bool woke = false;
    // Resume any thread parked on this sreg (or on an unspecified one).
    for (size_t k = 0; k < m_threads.size(); ++k)
    {
        if (m_threads[k].parked && (m_threads[k].waitSreg == idx || m_threads[k].waitSreg == 0xFFFFu))
        {
            resumeThread(k);
            woke = true;
        }
    }
    return woke;
}

bool IopKernel::hasParkedThread() const
{
    for (const auto &t : m_threads)
        if (t.parked)
            return true;
    return false;
}

bool iopKernelTest(PS2Memory *mem)
{
    const char *paths = std::getenv("PS2_IOP_KERNEL_TEST");
    if (!paths || !*paths)
        return false;
    if (!mem || !mem->getIOPRAM())
        return false;

    IopKernel kernel(mem);
    // When a loaded IOP module SET_SREGs back to the EE, log it (this is what a
    // live run writes into the EE-side _sif_sreg mirror).
    kernel.setEeSregWriter([](uint16_t idx, uint32_t val) {
        std::cerr << "[iop:kernel] (-> EE mirror) sreg[" << idx << "]=0x" << std::hex << val
                  << std::dec << std::endl;
    });

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

    // Simulate the EE completing its half of the audio handshake: set the IOP
    // soft-register the AUDIO worker thread is polling, and resume it.
    if (kernel.hasParkedThread())
    {
        std::cerr << "[iop:kernel] simulating EE -> IOP: SET_SREG iop[0x1f]=1, resuming parked thread(s)"
                  << std::endl;
        kernel.setIopSregAndResume(0x1Fu, 1u);
    }

    std::cerr << "[iop:kernel] done. RPC servers registered: " << kernel.rpcServers().size();
    for (const auto &s : kernel.rpcServers())
        std::cerr << " sid=0x" << std::hex << s.sid << std::dec;
    std::cerr << std::endl;

    // Marco 5b probe: send a synthetic GoW asset-load request (rpc=0x68 "SMPD") to
    // server 0x123456 and observe what the server function does (which cdvdman/ioman
    // calls it makes). Gated on env PS2_IOP_RPC_PROBE=1.
    if (std::getenv("PS2_IOP_RPC_PROBE"))
    {
        for (const auto &s : kernel.rpcServers())
        {
            if (s.sid != 0x123456u)
                continue;
            uint8_t recv[12] = {0};
            auto probe = [&](uint32_t fno, uint32_t *pkt, uint32_t words) {
                std::vector<uint8_t> send(words * 4u);
                std::memcpy(send.data(), pkt, words * 4u);
                std::cerr << "[iop:kernel] RPC_PROBE rpc=0x" << std::hex << fno << std::dec << std::endl;
                kernel.serviceRpc(0x123456u, fno, send.data(), words * 4u, recv, sizeof(recv));
                std::cerr << "[iop:kernel] RPC_PROBE recv=[" << std::hex
                          << *reinterpret_cast<uint32_t *>(recv) << ","
                          << *reinterpret_cast<uint32_t *>(recv + 4) << ","
                          << *reinterpret_cast<uint32_t *>(recv + 8) << "]" << std::dec << std::endl;
            };
            // 1) rpc=0x0 init (workbuf), 2) rpc=0x68 "SMPD" load.
            uint32_t init[2] = {0x0030a1c0u, 0u};
            probe(0x0u, init, 2);
            uint32_t ld[10] = {0x534D5044u, 1u, 0x1cu, 0u, 0x004533c0u, 0x20u, 0x00453440u, 0x280u, 0u, 0u};
            probe(0x68u, ld, 10);
            break;
        }
    }

    if (any)
        std::memset(mem->getIOPRAM() + 0x00010000u, 0, 0x001D0000u);
    return any;
}
