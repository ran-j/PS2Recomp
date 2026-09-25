#include "iop_compat_test_support.h"
#include "emulator/iop_emulator.h"
#include "emulator/core/iop_cpu.h"
#include "emulator/core/iop_memory.h"
#include "emulator/imports/iop_imports.h"

namespace
{
    using namespace iop_test;
    using namespace ps2x::iop::detail;

    // Real guest stores start SPU DMA; intrman invokes the guest handler below.
    struct DmaFixture
    {
        Host host;
        IopEmulator iop{host};
        Irx image{0x10000, 0x900};
        uint32_t offset = 0;
        std::array<uint32_t, 2> starts{};
        std::array<uint32_t, 2> delays{};
        std::array<uint64_t, 2> deadlines{};

        DmaFixture()
        {
            emit({0x27bdfff0, 0xafbf000c}); // save ra
            for (unsigned irq : {0x24u, 0x28u})
            {
                emit({0x24040000 | irq, 0x24050000, 0x3c060001, 0x34c60300,
                      0x24070000 | irq, 0x0c004185, 0}); // RegisterIntrHandler
                emit({0x24040000 | irq, 0x0c004187, 0}); // EnableIntr
            }
            image.words(0x600, {0x41e00000, 0, 0x0101, 0x72746e69, 0x006e616d,
                               0x03e00008, 0x24000004, 0x03e00008, 0x24000006, 0, 0});
            image.words(0x300, {
                0x3c080001, 0x8d090800, 0, // t1 = completed count (load delay)
                0x00095080, 0x01485021, 0xad440810, // trace[count] = irq
                0x25290001, 0xad090800,
                0x8d090804, 0, 0x1120000c, 0, // optionally rearm DMA from the handler
                0xad000804, 0x3c091f80, 0x352910c0,
                0x240a0020, 0xad2a0004, 0x3c0a0100, 0xad2a0008,
                0x24090050, 0x2529ffff, 0x1520fffe, 0, // handler runs past the new deadline
                0x03e00008, 0,
            });
        }

        void emit(std::initializer_list<uint32_t> words)
        {
            image.words(offset, words);
            offset += uint32_t(words.size()) * 4;
        }

        void start(unsigned channel, uint32_t delay)
        {
            emit({0x3c081f80, channel ? 0x35081500u : 0x350810c0u,
                  0x24090000 | (delay / 2), 0xad090004, 0x3c090100});
            starts[channel] = offset;
            delays[channel] = delay;
            emit({0xad090008});
        }

        void load()
        {
            const std::initializer_list<uint32_t> epilogue{0x8fbf000c, 0x27bd0010, 0x00001021, 0x03e00008, 0};
            image.words(offset, epilogue);
            const uint32_t entryEnd = offset + uint32_t(epilogue.size()) * 4;
            image.install(host);
            const auto result = iop.loadModuleBuffer(0x1000, nullptr, 0);
            require(result.moduleId > 0 && result.startResult == 0, "DMA IRX initialization failed");
            for (unsigned channel = 0; channel < 2; ++channel)
                if (delays[channel])
                    deadlines[channel] = iop.cycles() - (entryEnd - starts[channel]) / 4 + delays[channel];
        }

        uint32_t word(uint32_t address)
        {
            uint32_t value = 0;
            require(iop.readMemory(address, &value, sizeof(value)), "IOP trace read failed");
            return value;
        }

        void advanceTo(uint64_t cycle)
        {
            require(cycle >= iop.cycles(), "test attempted to reverse time");
            iop.runEeCycles((cycle - iop.cycles()) * 8);
        }
    };

    void dmaDeadlines()
    {
        DmaFixture f;
        f.start(0, 1000);
        f.start(1, 200);
        f.load();
        f.advanceTo(f.deadlines[1]);
        require(f.word(0x10800) == 0, "idle DMA dispatched before its deadline was serviced");
        f.iop.runEeCycles(8);
        require(f.word(0x10800) == 1 && f.word(0x10810) == 0x28, "earliest DMA did not wake the idle IOP");
        f.advanceTo(f.deadlines[0]);
        require(f.word(0x10800) == 1, "later DMA dispatched early");
        f.iop.runEeCycles(8);
        require(f.word(0x10800) == 2 && f.word(0x10814) == 0x24, "later DMA was lost");
        f.iop.runEeCycles(8000);
        require(f.word(0x10800) == 2, "DMA completion dispatched twice");
    }

    void dmaReplacementAndReset()
    {
        DmaFixture f;
        f.start(0, 200);
        f.start(1, 400);
        f.start(0, 1000); // replace the earliest event with a later one
        f.load();
        f.advanceTo(f.deadlines[1]);
        require(f.word(0x10800) == 0, "replaced DMA deadline survived");
        f.iop.runEeCycles(8);
        require(f.word(0x10800) == 1 && f.word(0x10810) == 0x28, "replacement hid the other channel");
        f.iop.reset();
        f.iop.runEeCycles(16000);
        require(f.iop.instructions() == 0, "reset retained a DMA callback");
        f.load();
        f.advanceTo(f.deadlines[1]);
        f.iop.runEeCycles(8);
        require(f.word(0x10800) == 1, "DMA scheduling did not recover after reset");
    }

    void dmaReentrantHandler()
    {
        DmaFixture f;
        f.start(0, 200);
        f.image.words(0x804, {1});
        f.load();
        f.advanceTo(f.deadlines[0]);
        f.iop.runEeCycles(8);
        require(f.word(0x10800) == 1, "DMA handler recursively dispatched a new completion");
        f.iop.runEeCycles(8);
        require(f.word(0x10800) == 2 && f.word(0x10814) == 0x24,
                "DMA started inside its handler was lost");
    }

    void dmaEqualDeadlines()
    {
        DmaFixture f;
        f.start(1, 206);
        f.start(0, 200); // Six instructions later: same completion cycle.
        f.load();
        require(f.deadlines[0] == f.deadlines[1], "fixture deadlines differ");
        f.advanceTo(f.deadlines[0]);
        f.iop.runEeCycles(8);
        require(f.word(0x10800) == 2 && f.word(0x10810) == 0x24 && f.word(0x10814) == 0x28,
                "simultaneous DMA callbacks changed IRQ order");
    }

    void dmaWhileExecuting()
    {
        DmaFixture f;
        f.start(0, 200);
        // Keep guest execution active beyond completion; no idle path is involved.
        f.emit({0x24100050, 0x2610ffff, 0x1600fffe, 0});
        f.load();
        require(f.word(0x10800) == 1 && f.word(0x10810) == 0x24,
                "DMA did not dispatch during guest execution");
    }

    void instructionChanges()
    {
        IopMemory memory;
        IopCpuCore core(memory);
        IopImportRegistry imports(memory);
        IopCpuState cpu{};
        const auto step = [&]()
        {
            const uint32_t instruction = memory.read32(cpu.pc);
            require(!imports.decode(cpu.pc, instruction), "ordinary instruction mistaken for an import");
            return core.executeInstruction(cpu, instruction);
        };
        cpu.pc = 0x80001000;
        memory.write32(0x1000, 0x24020011); // addiu v0, zero, 0x11
        require(step() && cpu.gpr[2] == 0x11, "first instruction failed");
        cpu.pc = 0x80001000;
        memory.write32(0x1000, 0x24020022);
        require(step() && cpu.gpr[2] == 0x22, "modified instruction was not refetched");
        cpu.pc = 0x1000;
        memory.write32(0x1000, 0x8c020800); // lw v0, 0x800(zero)
        memory.write32(0x1004, 0x24430001); // addiu v1, v0, 1 (sees old v0)
        memory.write32(0x800, 0x44);
        require(step() && cpu.gpr[2] == 0x22, "load delay changed");
        require(step() && cpu.gpr[2] == 0x44 && cpu.gpr[3] == 0x23,
                "instruction sharing changed load delay semantics");
        cpu.pc = 0x1000;
        memory.write32(0x1000, 0x10000001); // beq zero, zero, +1
        memory.write32(0x1004, 0x24020033);
        require(step() && cpu.pc == 0x1004 && cpu.branchPending, "branch delay slot was skipped");
        require(step() && cpu.pc == 0x1008 && cpu.gpr[2] == 0x33, "branch delay slot did not execute");
    }

    void modifiedImportStub()
    {
        IopMemory memory;
        IopImportRegistry imports(memory);
        memory.write32(0x1000, 0x41e00000);
        memory.write16(0x1008, 0x0101);
        constexpr char name[8] = "tstlib";
        require(memory.writeRam(0x100c, name, sizeof(name)), "import name write failed");
        memory.write32(0x1014, 0x03e00008);
        memory.write32(0x1018, 0x24000003);
        const auto decode = [&]() { return imports.decode(0x80001014, memory.read32(0x80001014)); };
        auto call = decode();
        require(call && call->ordinal == 3 && call->version == 0x0101, "shared fetch lost import metadata");
        memory.write16(0x1008, 0x0102);
        memory.write32(0x1018, 0x24000007);
        call = decode();
        require(call && call->ordinal == 7 && call->version == 0x0102, "import metadata became stale");
        memory.write32(0x1014, 0x24020011);
        require(!decode(), "patched import stub still dispatched as an import");
    }
}

int main()
{
    const Test tests[] = {
        {"DMA deadlines and idle wakeup", dmaDeadlines},
        {"DMA replacement and reset", dmaReplacementAndReset},
        {"DMA scheduled from a running IRQ handler", dmaReentrantHandler},
        {"Simultaneous DMA IRQ order", dmaEqualDeadlines},
        {"DMA dispatch while guest instructions execute", dmaWhileExecuting},
        {"Modified instructions and load delay", instructionChanges},
        {"Modified import stub and metadata", modifiedImportStub},
    };
    return run(tests);
}
