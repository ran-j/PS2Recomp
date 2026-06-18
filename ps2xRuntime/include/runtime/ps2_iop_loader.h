#ifndef PS2_IOP_LOADER_H
#define PS2_IOP_LOADER_H

#include <cstdint>
#include <string>
#include <vector>

class PS2Memory;

// One IRX import stub: the IOP-RAM address the module jal's to, and the
// (library, function index) it represents. Used to dispatch kernel imports.
struct IopImportStub
{
    uint32_t addr = 0;     // IOP-RAM address of the stub (jal target)
    std::string lib;       // imported library name (e.g. "thbase")
    uint16_t index = 0;    // function index within that library
};

// Result of loading one IRX module into IOP RAM.
struct IopModule
{
    bool valid = false;
    std::string name;             // module name from .iopmod (or hint)
    uint32_t base = 0;            // IOP-RAM load address (64 KiB aligned)
    uint32_t entry = 0;          // base + e_entry  == module_start
    uint32_t gp = 0;             // base + gp_value
    uint32_t textSize = 0;
    uint32_t dataSize = 0;
    uint32_t bssSize = 0;
    uint32_t imageSize = 0;      // bytes reserved in IOP RAM
    uint32_t relocCount = 0;
    std::vector<std::string> imports; // library names this module imports
    std::vector<std::string> exports; // library names this module exports
    std::vector<IopImportStub> stubs; // per-function import stubs
};

// Loads ET_SCE_IOPRELEXEC (relocatable MIPS) IRX images into the IOP's 2 MB RAM
// via a simple bump allocator, applying .rel.* relocations against the chosen
// 64 KiB-aligned load base (which makes HI16/LO16 relocation carry-free).
class IopModuleLoader
{
public:
    explicit IopModuleLoader(PS2Memory *mem);

    void reset(uint32_t moduleBase = 0x00100000u);

    IopModule loadFile(const std::string &hostPath);
    IopModule loadImage(const uint8_t *data, size_t size, const std::string &nameHint);

    uint32_t allocTop() const { return m_allocTop; }

private:
    PS2Memory *m_mem;
    uint32_t m_allocTop;
};

// Env-driven self-test: loads each IRX path in PS2_IOP_LOAD_TEST (comma list)
// and logs its parsed layout. Returns true if at least one loaded.
bool iopLoaderSelfTest(PS2Memory *mem);

// Env-driven test (PS2_IOP_RUN_TEST=<irx path>): load the IRX, then execute its
// module_start on the R3000A core, dispatching kernel imports to a logging HLE
// stub. Logs the import-call trace and where execution stops. Returns true if it
// ran. This maps the IOP kernel HLE surface needed to finish Marco 4.
bool iopRunModuleTest(PS2Memory *mem);

#endif // PS2_IOP_LOADER_H
