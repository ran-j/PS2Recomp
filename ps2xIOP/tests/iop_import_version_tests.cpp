#include "iop_compat_test_support.h"

#include "emulator/core/iop_cpu.h"
#include "emulator/core/iop_memory.h"
#include "emulator/imports/iop_imports.h"
#include "emulator/imports/iop_loadcore.h"

namespace
{
    using namespace iop_test;
    using namespace ps2x::iop::detail;

    void addExport(IopMemory &memory, IopImportRegistry &imports, uint32_t address,
                   uint16_t version, uint32_t target, uint32_t count = 4u)
    {
        require(memory.zeroRam(address, 128u), "export table does not fit");
        memory.write32(address, 0x41C00000u);
        memory.write16(address + 8u, version);
        constexpr char name[8] = "tstlib";
        require(memory.writeRam(address + 12u, name, sizeof(name)), "export name does not fit");
        for (uint32_t i = 0u; i < count; ++i)
            memory.write32(address + 20u + 4u * i, target);
        require(imports.registerExportTable(address), "export registration failed");
    }

    void importTable(IopMemory &memory, uint32_t address, uint16_t version)
    {
        require(memory.zeroRam(address, 64u), "import table does not fit");
        memory.write32(address, 0x41E00000u);
        memory.write16(address + 8u, version);
        constexpr char name[8] = "tstlib";
        require(memory.writeRam(address + 12u, name, sizeof(name)), "import name does not fit");
        memory.write32(address + 20u, 0x03E00008u);
        memory.write32(address + 24u, 0x24000003u);
    }

    void decodeVersion()
    {
        IopMemory memory;
        IopImportRegistry imports(memory);
        importTable(memory, 0x1000u, 0x0310u);
        const auto call = imports.decode(0x1014u);
        require(call && call->library == "tstlib" && call->ordinal == 3u && call->version == 0x0310u,
                "decoder dropped the import library version");
        const auto alias = imports.decode(0x80001014u);
        require(alias && alias->version == 0x0310u, "cached alias lost import version");
    }

    void majorIsolation()
    {
        IopMemory memory;
        IopImportRegistry imports(memory);
        addExport(memory, imports, 0x1000u, 0x0201u, 0x2100u);
        addExport(memory, imports, 0x1800u, 0x0101u, 0x3100u);
        require(imports.resolve("tstlib", 3u, 0x0101u) == 0x3100u, "linked to wrong library major");
        require(imports.resolve("tstlib", 3u, 0x0201u) == 0x2100u, "second major unavailable");
        require(imports.resolve("tstlib", 3u, 0x0300u) == 0u, "incompatible major silently linked");
        require(imports.findTable("tstlib", 0x0300u) == 0u, "query ignored requested major");
    }

    void newestMinor()
    {
        IopMemory memory;
        IopImportRegistry imports(memory);
        addExport(memory, imports, 0x1000u, 0x0101u, 0x2100u);
        addExport(memory, imports, 0x1800u, 0x0104u, 0x3100u);
        addExport(memory, imports, 0x1400u, 0x0103u, 0x4100u);
        require(imports.resolve("tstlib", 3u, 0x0101u) == 0x3100u, "selected lowest address, not newest minor");
        require(imports.resolve("tstlib", 3u, 0x017Fu) == 0x3100u,
                "invented a minimum-minor rule absent from LOADCORE linking");
        require(imports.releaseExportTable(0x1800u), "unregister failed");
        require(imports.resolve("tstlib", 3u, 0x0101u) == 0x4100u, "unregistered library remained selected");
    }

    void missingOrdinal()
    {
        IopMemory memory;
        IopImportRegistry imports(memory);
        addExport(memory, imports, 0x1000u, 0x0101u, 0x2100u, 8u);
        addExport(memory, imports, 0x1800u, 0x0102u, 0x3100u, 4u);
        require(imports.resolve("tstlib", 7u, 0x0101u) == 0u,
                "missing ordinal fell back to a different export table");
        require(imports.resolve("missing", 0u, 0x0101u) == 0u, "missing library resolved");
        imports.reset();
        require(imports.resolve("tstlib", 0u, 0x0101u) == 0u, "registry reset left exports");
    }

    void queryFunctionArray()
    {
        IopMemory memory;
        IopImportRegistry imports(memory);
        IopLoadcore loadcore(memory, imports);
        addExport(memory, imports, 0x1000u, 0x0201u, 0x2100u);
        addExport(memory, imports, 0x1800u, 0x0101u, 0x3100u);
        importTable(memory, 0x800u, 0x0102u);
        IopCpuState cpu{};
        cpu.gpr[4] = 0x800u;
        require(loadcore.dispatchImport(11u, cpu), "QueryLibraryEntryTable unhandled");
        require(cpu.gpr[2] == 0x1814u && memory.read32(cpu.gpr[2]) == 0x3100u,
                "query returned an export header instead of function array");
        memory.write16(0x808u, 0x0300u);
        require(loadcore.dispatchImport(11u, cpu) && cpu.gpr[2] == 0u, "query accepted wrong major");
        for (uint32_t address : {0u, 0xFFFFFFF8u, IopMemory::RamSize - 4u})
        {
            cpu.gpr[4] = address;
            require(loadcore.dispatchImport(11u, cpu) && cpu.gpr[2] == 0u, "invalid query pointer accepted");
        }
    }

    Irx provider(uint32_t base, uint16_t version, uint32_t result)
    {
        Irx image(base);
        const uint32_t table = base + 0x80u;
        const uint32_t importStub = base + 0xC0u + 20u;
        image.words(0u, {0x27BDFFE0u, 0xAFBF001Cu,
            0x3C040000u | (table >> 16u), 0x34840000u | (table & 0xFFFFu),
            0x0C000000u | (importStub >> 2u), 0u,
            0x8FBF001Cu, 0x00001021u, 0x27BD0020u, 0x03E00008u, 0u});
        image.words(0x60u, {0x03E00008u, 0x24020000u | result});
        image.words(0x80u, {0x41C00000u, 0u, version, 0x6C747374u, 0x00006269u,
                             base, base, base, base + 0x60u, 0u});
        image.words(0xC0u, {0x41E00000u, 0u, 0x0101u, 0x64616F6Cu, 0x65726F63u,
                             0x03E00008u, 0x24000006u, 0u, 0u});
        return image;
    }

    Irx consumer(uint16_t version)
    {
        constexpr uint32_t base = 0x13000u;
        Irx image(base);
        image.words(0u, {0x27BDFFF0u, 0xAFBF000Cu,
            0x0C000000u | ((base + 0x54u) >> 2u), 0u,
            0x8FBF000Cu, 0x27BD0010u, 0x03E00008u, 0u});
        image.words(0x40u, {0x41E00000u, 0u, version, 0x6C747374u, 0x00006269u,
                             0x03E00008u, 0x24000003u, 0u, 0u});
        return image;
    }

    void physicalImportsEndToEnd()
    {
        Host host;
        IopSubsystem iop(host);
        auto wrongMajor = provider(0x10000u, 0x0201u, 0x22u);
        wrongMajor.install(host);
        require(iop.loadModuleBuffer(0x1000u).startResult == 0, "provider 2 failed");
        auto oldMinor = provider(0x11000u, 0x0101u, 0x11u);
        oldMinor.install(host);
        require(iop.loadModuleBuffer(0x1000u).startResult == 0, "provider 1 failed");
        auto newMinor = provider(0x12000u, 0x0103u, 0x13u);
        newMinor.install(host);
        require(iop.loadModuleBuffer(0x1000u).startResult == 0, "provider 1.3 failed");
        auto client = consumer(0x0101u);
        client.install(host);
        const auto result = iop.loadModuleBuffer(0x1000u);
        require(result.moduleId > 0 && result.startResult == 0x13, "R3000A called wrong export version");
    }
}

int main()
{
    const Test tests[] = {
        {"Import decoder preserves library ABI version", decodeVersion},
        {"Different major versions cannot cross-link", majorIsolation},
        {"Newest registered minor wins within the requested major", newestMinor},
        {"Ordinal lookup stays in the selected table", missingOrdinal},
        {"LOADCORE query returns function array and honors major", queryFunctionArray},
        {"Physical IRX consumer links correct version end to end", physicalImportsEndToEnd},
    };
    return run(tests);
}
