#include "runtime/ps2_iop_loader.h"
#include "runtime/ps2_memory.h"
#include "runtime/ps2_iop_cpu.h"

#include <cstring>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <unordered_map>
#include <map>

namespace
{
    // ---- ELF32 (little-endian) ----
    constexpr uint16_t ET_SCE_IOPRELEXEC = 0xFF80u;
    constexpr uint16_t EM_MIPS = 8u;
    constexpr uint32_t PT_LOAD = 1u;
    constexpr uint32_t PT_SCE_IOPMOD = 0x70000080u;
    constexpr uint32_t SHT_REL = 9u;

    // MIPS relocation types
    constexpr uint8_t R_MIPS_32 = 2u;
    constexpr uint8_t R_MIPS_26 = 4u;
    constexpr uint8_t R_MIPS_HI16 = 5u;
    constexpr uint8_t R_MIPS_LO16 = 6u;

    constexpr uint32_t IMPORT_MAGIC = 0x41E00000u;
    constexpr uint32_t EXPORT_MAGIC = 0x41C00000u;

    uint16_t rd16(const uint8_t *p) { uint16_t v; std::memcpy(&v, p, 2); return v; }
    uint32_t rd32(const uint8_t *p) { uint32_t v; std::memcpy(&v, p, 4); return v; }
}

IopModuleLoader::IopModuleLoader(PS2Memory *mem) : m_mem(mem), m_allocTop(0x00100000u) {}

void IopModuleLoader::reset(uint32_t moduleBase)
{
    m_allocTop = moduleBase & ~0xFFFFu; // 64 KiB aligned
}

IopModule IopModuleLoader::loadFile(const std::string &hostPath)
{
    IopModule mod;
    std::ifstream f(hostPath, std::ios::binary | std::ios::ate);
    if (!f)
    {
        std::cerr << "[iop:loader] cannot open " << hostPath << std::endl;
        return mod;
    }
    const std::streamsize n = f.tellg();
    f.seekg(0);
    std::vector<uint8_t> buf(static_cast<size_t>(n));
    if (n <= 0 || !f.read(reinterpret_cast<char *>(buf.data()), n))
    {
        std::cerr << "[iop:loader] read failed for " << hostPath << std::endl;
        return mod;
    }
    // name hint = file stem
    std::string hint = hostPath;
    const size_t slash = hint.find_last_of("/\\");
    if (slash != std::string::npos)
        hint = hint.substr(slash + 1);
    return loadImage(buf.data(), buf.size(), hint);
}

IopModule IopModuleLoader::loadImage(const uint8_t *data, size_t size, const std::string &nameHint)
{
    IopModule mod;
    mod.name = nameHint;

    if (!m_mem || !m_mem->getIOPRAM() || size < 0x34)
    {
        std::cerr << "[iop:loader] bad args / no IOP RAM" << std::endl;
        return mod;
    }
    if (!(data[0] == 0x7F && data[1] == 'E' && data[2] == 'L' && data[3] == 'F'))
    {
        std::cerr << "[iop:loader] not an ELF: " << nameHint << std::endl;
        return mod;
    }

    const uint16_t e_type = rd16(data + 0x10);
    const uint16_t e_machine = rd16(data + 0x12);
    const uint32_t e_entry = rd32(data + 0x18);
    const uint32_t e_phoff = rd32(data + 0x1C);
    const uint32_t e_shoff = rd32(data + 0x20);
    const uint16_t e_phentsize = rd16(data + 0x2A);
    const uint16_t e_phnum = rd16(data + 0x2C);
    const uint16_t e_shentsize = rd16(data + 0x2E);
    const uint16_t e_shnum = rd16(data + 0x30);

    if (e_machine != EM_MIPS)
    {
        std::cerr << "[iop:loader] not MIPS (machine=" << e_machine << ")" << std::endl;
        return mod;
    }
    if (e_type != ET_SCE_IOPRELEXEC && e_type != 1u /*ET_REL*/)
    {
        std::cerr << "[iop:loader] unexpected e_type=0x" << std::hex << e_type << std::dec
                  << " (" << nameHint << ")" << std::endl;
        // continue anyway: many IRX are 0xFF80
    }

    // ---- find the single PT_LOAD (image extent) and PT_SCE_IOPMOD ----
    uint32_t loadOff = 0, loadFilesz = 0, loadMemsz = 0;
    bool haveLoad = false;
    uint32_t iopmodOff = 0;
    bool haveIopmod = false;
    for (uint16_t i = 0; i < e_phnum; ++i)
    {
        const uint32_t ph = e_phoff + static_cast<uint32_t>(i) * e_phentsize;
        if (ph + 0x20 > size)
            break;
        const uint32_t p_type = rd32(data + ph + 0x00);
        const uint32_t p_offset = rd32(data + ph + 0x04);
        const uint32_t p_filesz = rd32(data + ph + 0x10);
        const uint32_t p_memsz = rd32(data + ph + 0x14);
        if (p_type == PT_LOAD && !haveLoad)
        {
            loadOff = p_offset;
            loadFilesz = p_filesz;
            loadMemsz = p_memsz;
            haveLoad = true;
        }
        else if (p_type == PT_SCE_IOPMOD)
        {
            iopmodOff = p_offset;
            haveIopmod = true;
        }
    }
    if (!haveLoad)
    {
        std::cerr << "[iop:loader] no PT_LOAD in " << nameHint << std::endl;
        return mod;
    }
    if (loadMemsz < loadFilesz)
        loadMemsz = loadFilesz;
    if (static_cast<uint64_t>(loadOff) + loadFilesz > size)
    {
        std::cerr << "[iop:loader] PT_LOAD out of file bounds (" << nameHint << ")" << std::endl;
        return mod;
    }

    // ---- allocate IOP RAM (64 KiB aligned so HI16/LO16 relocation is carry-free) ----
    const uint32_t base = (m_allocTop + 0xFFFFu) & ~0xFFFFu;
    const uint32_t imageSize = (loadMemsz + 0xFu) & ~0xFu;
    if (static_cast<uint64_t>(base) + imageSize > PS2Memory::IOP_RAM_SIZE)
    {
        std::cerr << "[iop:loader] IOP RAM exhausted loading " << nameHint << std::endl;
        return mod;
    }
    uint8_t *iopram = m_mem->getIOPRAM();
    std::memcpy(iopram + base, data + loadOff, loadFilesz);
    if (loadMemsz > loadFilesz)
        std::memset(iopram + base + loadFilesz, 0, loadMemsz - loadFilesz);

    mod.base = base;
    mod.imageSize = imageSize;
    mod.entry = base + e_entry;
    m_allocTop = base + imageSize;

    // ---- .iopmod (module info) ----
    if (haveIopmod && iopmodOff + 0x18 <= size)
    {
        const uint32_t moduleinfo = rd32(data + iopmodOff + 0x00);
        const uint32_t modEntry = rd32(data + iopmodOff + 0x04);
        const uint32_t gpv = rd32(data + iopmodOff + 0x08);
        mod.textSize = rd32(data + iopmodOff + 0x0C);
        mod.dataSize = rd32(data + iopmodOff + 0x10);
        mod.bssSize = rd32(data + iopmodOff + 0x14);
        mod.gp = base + gpv;
        if (modEntry != 0xFFFFFFFFu && modEntry != 0)
            mod.entry = base + modEntry; // matches e_entry in practice
        // name (NUL-terminated) follows version@0x18; present only if section carries it
        size_t np = iopmodOff + 0x1A;
        if (moduleinfo != 0xFFFFFFFFu && np < size && data[np] != 0)
        {
            std::string nm;
            while (np < size && data[np] && nm.size() < 64)
                nm.push_back(static_cast<char>(data[np++]));
            if (!nm.empty())
                mod.name = nm;
        }
    }

    // ---- relocations (.rel.*) : sections are SHT_REL, entsize 8 ----
    // For a relexec loaded at `base`, every reloc is a local self-reloc whose
    // implicit addend is the in-place value; the symbol value resolves to `base`.
    uint32_t relocCount = 0;
    if (e_shoff && e_shnum && e_shentsize >= 40)
    {
        for (uint16_t i = 0; i < e_shnum; ++i)
        {
            const uint32_t sh = e_shoff + static_cast<uint32_t>(i) * e_shentsize;
            if (sh + 40 > size)
                break;
            const uint32_t sh_type = rd32(data + sh + 0x04);
            if (sh_type != SHT_REL)
                continue;
            const uint32_t sh_offset = rd32(data + sh + 0x10);
            const uint32_t sh_size = rd32(data + sh + 0x14);
            if (static_cast<uint64_t>(sh_offset) + sh_size > size)
                continue;
            const uint32_t count = sh_size / 8u;
            for (uint32_t r = 0; r < count; ++r)
            {
                const uint32_t r_offset = rd32(data + sh_offset + r * 8u + 0u);
                const uint32_t r_info = rd32(data + sh_offset + r * 8u + 4u);
                const uint8_t r_type = static_cast<uint8_t>(r_info & 0xFFu);
                const uint32_t loc = base + r_offset; // IOP RAM offset to patch
                if (loc + 4u > PS2Memory::IOP_RAM_SIZE)
                    continue;
                uint32_t w;
                std::memcpy(&w, iopram + loc, 4);
                switch (r_type)
                {
                case R_MIPS_32:
                    w += base;
                    break;
                case R_MIPS_26:
                {
                    const uint32_t tgt = ((w & 0x03FFFFFFu) << 2) + base;
                    w = (w & 0xFC000000u) | ((tgt >> 2) & 0x03FFFFFFu);
                    break;
                }
                case R_MIPS_HI16:
                    // base is 64KB aligned -> no carry from LO16; add base>>16 to imm
                    w = (w & 0xFFFF0000u) | (((w & 0xFFFFu) + (base >> 16)) & 0xFFFFu);
                    break;
                case R_MIPS_LO16:
                    // base low 16 bits are 0 -> immediate unchanged
                    break;
                default:
                    break; // other types unexpected in relexec; leave as-is
                }
                std::memcpy(iopram + loc, &w, 4);
                ++relocCount;
            }
        }
    }
    mod.relocCount = relocCount;

    // ---- scan loaded image for import/export library tables ----
    for (uint32_t off = 0; off + 0x14 <= loadMemsz; off += 4)
    {
        const uint32_t magic = rd32(iopram + base + off);
        if (magic != IMPORT_MAGIC && magic != EXPORT_MAGIC)
            continue;
        char name[9] = {0};
        std::memcpy(name, iopram + base + off + 0x0C, 8);
        for (int k = 0; k < 8; ++k)
            if (name[k] == ' ')
                name[k] = 0;
        std::string libname(name);
        if (libname.empty())
            continue;
        if (magic == IMPORT_MAGIC)
        {
            mod.imports.push_back(libname);
            // Per-function stub entries (2 words each) follow the 0x14-byte
            // header; each stub's 2nd word is `addiu $0,$0,index` (0x2400|idx).
            for (uint32_t s = off + 0x14u; s + 8u <= loadMemsz; s += 8u)
            {
                const uint32_t w1 = rd32(iopram + base + s + 4u);
                if ((w1 >> 16) != 0x2400u)
                    break; // end of this library's stub list
                IopImportStub st;
                st.addr = base + s;
                st.lib = libname;
                st.index = static_cast<uint16_t>(w1 & 0xFFFFu);
                mod.stubs.push_back(st);
            }
        }
        else
        {
            mod.exports.push_back(libname);
        }
    }

    mod.valid = true;
    return mod;
}

bool iopLoaderSelfTest(PS2Memory *mem)
{
    const char *paths = std::getenv("PS2_IOP_LOAD_TEST");
    if (!paths || !*paths)
        return false;
    if (!mem || !mem->getIOPRAM())
    {
        std::cerr << "[iop:loader:selftest] no IOP RAM" << std::endl;
        return false;
    }

    IopModuleLoader loader(mem);
    loader.reset(0x00100000u);

    bool any = false;
    std::stringstream ss(paths);
    std::string path;
    while (std::getline(ss, path, ','))
    {
        if (path.empty())
            continue;
        const IopModule m = loader.loadFile(path);
        if (!m.valid)
        {
            std::cerr << "[iop:loader:selftest] FAILED to load " << path << std::endl;
            continue;
        }
        any = true;
        std::stringstream imp, exp;
        for (const auto &s : m.imports)
            imp << s << " ";
        for (const auto &s : m.exports)
            exp << s << " ";
        std::cerr << "[iop:loader:selftest] " << m.name
                  << " base=0x" << std::hex << m.base
                  << " entry=0x" << m.entry
                  << " gp=0x" << m.gp
                  << " text=0x" << m.textSize
                  << " data=0x" << m.dataSize
                  << " bss=0x" << m.bssSize
                  << std::dec
                  << " relocs=" << m.relocCount
                  << " imports=[" << imp.str() << "]"
                  << " exports=[" << exp.str() << "]"
                  << std::endl;
    }

    // Clean the IOP RAM we used so the guest starts from zero.
    if (any)
        std::memset(mem->getIOPRAM() + 0x00100000u, 0,
                    (loader.allocTop() > 0x00100000u) ? (loader.allocTop() - 0x00100000u) : 0u);
    return any;
}

bool iopRunModuleTest(PS2Memory *mem)
{
    const char *path = std::getenv("PS2_IOP_RUN_TEST");
    if (!path || !*path)
        return false;
    if (!mem || !mem->getIOPRAM())
        return false;

    IopModuleLoader loader(mem);
    loader.reset(0x00100000u);
    IopModule mod = loader.loadFile(path);
    if (!mod.valid)
    {
        std::cerr << "[iop:run] load failed: " << path << std::endl;
        return false;
    }

    // stub address -> (lib,index)
    std::unordered_map<uint32_t, const IopImportStub *> stubMap;
    for (const auto &s : mod.stubs)
        stubMap[s.addr] = &s;

    IopCpu cpu(mem);
    cpu.reset();

    std::map<std::string, uint32_t> callCounts;
    uint32_t totalCalls = 0;
    int logged = 0;

    cpu.setImportHook([&](IopCpu &c, uint32_t addr) -> bool {
        auto it = stubMap.find(addr);
        if (it == stubMap.end())
            return false;
        const IopImportStub *s = it->second;
        const std::string key = s->lib + "[" + std::to_string(s->index) + "]";
        callCounts[key]++;
        ++totalCalls;
        if (logged < 48)
        {
            std::cerr << "[iop:run] import " << key
                      << " ra=0x" << std::hex << c.gpr(31) << std::dec << std::endl;
            ++logged;
        }
        c.setGpr(2, 0);        // $v0 = 0 (HLE stub return)
        c.setPC(c.gpr(31));    // return to caller ($ra)
        return true;
    });

    // Call frame for module_start(argc=0, argv=0).
    const uint32_t sentinel = 0x00FFF000u;
    const uint32_t stackTop = 0x001F0000u;
    cpu.setGpr(29, stackTop); // $sp
    cpu.setGpr(28, mod.gp);   // $gp
    cpu.setGpr(4, 0);         // $a0 = argc
    cpu.setGpr(5, 0);         // $a1 = argv
    cpu.setGpr(31, sentinel); // $ra
    cpu.setHaltPc(sentinel);
    cpu.setPC(mod.entry);

    const uint32_t retired = cpu.run(500000u);

    std::cerr << "[iop:run] " << mod.name
              << " entry=0x" << std::hex << mod.entry
              << " stopPc=0x" << cpu.pc() << std::dec
              << " instrs=" << retired
              << " reachedSentinel=" << (cpu.pc() == sentinel ? "yes" : "no")
              << " importCalls=" << totalCalls
              << " v0=0x" << std::hex << cpu.gpr(2) << std::dec << std::endl;
    std::cerr << "[iop:run] distinct imports called (" << callCounts.size() << "):";
    for (const auto &kv : callCounts)
        std::cerr << " " << kv.first << "x" << kv.second;
    std::cerr << std::endl;

    std::memset(mem->getIOPRAM() + 0x00100000u, 0,
                (loader.allocTop() > 0x00100000u) ? (loader.allocTop() - 0x00100000u) : 0u);
    return true;
}
