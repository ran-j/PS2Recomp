#ifndef PS2_IOP_KERNEL_H
#define PS2_IOP_KERNEL_H

#include "runtime/ps2_iop_loader.h"
#include "runtime/ps2_iop_cpu.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class PS2Memory;

// High-level-emulated IOP kernel: owns a persistent R3000A core + IOP RAM and
// HLEs the kernel libraries (loadcore/intrman/sysmem/sysclib/thbase/sifman/
// sifcmd...) so real driver IRX modules (LIBSD, AUDIO, ...) can be loaded and
// their module_start + threads executed. Cross-module imports (e.g. AUDIO ->
// libsd) resolve to the real loaded code via the export registry that
// RegisterLibraryEntries populates.
class IopKernel
{
public:
    explicit IopKernel(PS2Memory *mem);
    ~IopKernel();

    IopCpu &cpu() { return *m_cpu; }

    // Load an IRX from a host path and run its module_start. Returns the loaded
    // module record (valid==false on failure).
    IopModule loadAndStart(const std::string &hostPath);

    // RPC server registered by a loaded module (sceSifRegisterRpc).
    struct RpcServer
    {
        uint32_t sid = 0;
        uint32_t func = 0;   // IOP address of the server function
        uint32_t buf = 0;
        uint32_t cfunc = 0;
        uint32_t cbuf = 0;
    };
    const std::vector<RpcServer> &rpcServers() const { return m_rpcServers; }

    // ---- EE <-> IOP SREG bridge (Step 3b) ----
    // The EE sets an IOP soft-register (its SET_SREG to the IOP) and any IOP
    // thread parked polling that sreg is resumed. Returns true if a thread woke.
    bool setIopSregAndResume(uint16_t idx, uint32_t val);
    uint32_t iopSreg(uint16_t idx) const { return (idx < 32) ? m_iopSreg[idx] : 0u; }
    bool hasParkedThread() const;

    // Callback invoked when a loaded IOP module sends SET_SREG back to the EE
    // (sceSifSendCmd cid 0x80000001). The runtime uses this to write the EE-side
    // _sif_sreg[] mirror so EE poll loops progress.
    using EeSregWriteFn = std::function<void(uint16_t idx, uint32_t val)>;
    void setEeSregWriter(EeSregWriteFn fn) { m_eeSregWriter = std::move(fn); }

private:
    bool onImport(IopCpu &c, uint32_t stubAddr);
    bool kernelHle(IopCpu &c, const std::string &lib, uint16_t idx);
    void runToHalt(uint32_t entry, uint32_t a0, uint32_t a1, uint32_t gp, const char *what);
    void resumeThread(size_t threadIdx);

    uint32_t sysAlloc(uint32_t size, uint32_t align = 0x100u);

    PS2Memory *m_mem;
    std::unique_ptr<IopCpu> m_cpu;
    IopModuleLoader m_loader;
    std::vector<IopModule> m_modules;

    // stub address -> (library, index)
    std::unordered_map<uint32_t, IopImportStub> m_stubs;
    // library name -> [function index -> IOP address]
    std::unordered_map<std::string, std::vector<uint32_t>> m_exports;

    // sysmem bump allocator (within IOP RAM, above the loaded modules)
    uint32_t m_heapTop;

    // gp of the module currently executing (for cross-module gp switching)
    uint32_t m_curGp;

    // cooperative thread model
    struct Thread
    {
        uint32_t tid = 0;
        uint32_t entry = 0;
        uint32_t stackTop = 0;
        uint32_t gp = 0;
        bool started = false;
        bool parked = false;        // blocked, savedState valid
        uint16_t waitSreg = 0xFFFFu; // IOP sreg index it's polling (or 0xFFFF)
        IopCpu::State savedState{};  // context to resume from
    };
    std::vector<Thread> m_threads;
    uint32_t m_nextTid;
    bool m_parked;       // set by blocking HLE (RpcLoop/SleepThread/GetSreg)
    int m_curThread;     // index in m_threads of the running thread, or -1

    uint32_t m_iopSreg[32]; // IOP-side soft registers
    EeSregWriteFn m_eeSregWriter;

    std::vector<RpcServer> m_rpcServers;

    uint32_t m_logBudget;
    std::unordered_set<std::string> m_hleLogged; // dedupe per-(lib,idx) HLE logging
};

// Env-driven test (PS2_IOP_KERNEL_TEST=<irx>[,<irx>...]): load+start each IRX
// through the HLE kernel and log the result (threads, RPC servers). Returns true
// if it ran.
bool iopKernelTest(PS2Memory *mem);

#endif // PS2_IOP_KERNEL_H
