#include "MiniTest.h"
#include "ps2recomp/code_generator.h"
#include "ps2recomp/instructions.h"

using namespace ps2recomp;

static Instruction makeBranch(uint32_t address, uint32_t targetOffsetWords)
{
    Instruction inst;
    inst.address = address;
    inst.raw = 0x10000000 | (address & 0xFFFF); // arbitrary debug value
    inst.opcode = OPCODE_BEQ;
    inst.rs = 1;
    inst.rt = 1; // always equal
    inst.simmediate = static_cast<uint32_t>(targetOffsetWords);
    inst.isBranch = true;
    inst.hasDelaySlot = true;
    return inst;
}

static Instruction makeNop(uint32_t address)
{
    Instruction inst;
    inst.address = address;
    inst.raw = 0;
    inst.opcode = OPCODE_ADDIU;
    inst.rt = 0; // encode as nop in translator
    inst.hasDelaySlot = false;
    return inst;
}

void register_code_generator_tests()
{
    MiniTest::Case("CodeGenerator", [](TestCase &tc)
                   {
        tc.Run("emits labels and gotos for internal branches", [](TestCase &t) {
            Function func;
            func.name = "test_func";
            func.start = 0x1000;
        func.end = 0x1020;
        func.isRecompiled = true;
        func.isStub = false;

        // Build a small function:
        // 0x1000: nop
        // 0x1004: beq $1,$1, target (0x100c) with delay slot at 0x1008
        // 0x1008: nop (delay slot)
        // 0x100c: nop (branch target)
        // 0x1010: nop (fallthrough)
        std::vector<Instruction> instructions;
        instructions.push_back(makeNop(0x1000));
        instructions.push_back(makeBranch(0x1004, 1)); // target = 0x1004 + 4 + (1<<2) = 0x100c
        instructions.push_back(makeNop(0x1008));       // delay slot
        instructions.push_back(makeNop(0x100c));       // branch target
        instructions.push_back(makeNop(0x1010));       // extra

        CodeGenerator gen({});
        std::string generated = gen.generateFunction(func, instructions, false);

        t.IsTrue(generated.find("label_100c:") != std::string::npos, "branch target should emit a label");
        t.IsTrue(generated.find("goto label_100c;") != std::string::npos, "internal branch should jump via goto");
        t.IsTrue(generated.find("label_1008:") == std::string::npos, "delay slot without incoming branch should not get a label");
    });

    tc.Run("labels delay slot when it is a branch target", [](TestCase &t) {
        Function func;
        func.name = "delay_slot_label";
        func.start = 0x2000;
        func.end = 0x2020;
        func.isRecompiled = true;
        func.isStub = false;

        // Branch at 0x2000 targets 0x2004 (its own delay slot)
        std::vector<Instruction> instructions;
        instructions.push_back(makeBranch(0x2000, 0)); // target = 0x2004
        instructions.push_back(makeNop(0x2004));       // delay slot and target
        instructions.push_back(makeNop(0x2008));       // extra

        CodeGenerator gen({});
        std::string generated = gen.generateFunction(func, instructions, false);

        t.IsTrue(generated.find("label_2004:") != std::string::npos, "delay slot that is a target should emit a label");
        t.IsTrue(generated.find("goto label_2004;") != std::string::npos, "branch to delay slot should use goto");
    });

    tc.Run("branches outside function still set pc", [](TestCase &t) {
        Function func;
        func.name = "external_branch";
        func.start = 0x3000;
        func.end = 0x3020;
        func.isRecompiled = true;
        func.isStub = false;

        // Branch targets outside the function range
        std::vector<Instruction> instructions;
        instructions.push_back(makeBranch(0x3000, 4)); // target = 0x3014 (inside) -> make it outside by adjusting end? easier: set end smaller? Instead use large offset
        instructions.clear();
        Instruction br = makeBranch(0x3000, 0x100); // target far outside
        instructions.push_back(br);
        instructions.push_back(makeNop(0x3004)); // delay slot

        CodeGenerator gen({});
        std::string generated = gen.generateFunction(func, instructions, false);

        t.IsTrue(generated.find("ctx->pc = 0x") != std::string::npos, "external branch should set ctx->pc");
        t.IsTrue(generated.find("goto label_") == std::string::npos, "external branch should not use goto");
    });

    tc.Run("jumps to known symbols call by name", [](TestCase &t) {
        Function func;
        func.name = "call_symbol";
        func.start = 0x4000;
        func.end = 0x4018;
        func.isRecompiled = true;
        func.isStub = false;

        Symbol targetSym;
        targetSym.name = "target_func";
        targetSym.address = 0x5000;
        targetSym.isFunction = true;

        Instruction j{};
        j.address = 0x4000;
        j.opcode = OPCODE_J;
        j.target = (targetSym.address >> 2) & 0x3FFFFFF;
        j.hasDelaySlot = true;
        j.raw = 0x08000000 | (j.target & 0x3FFFFFF);

        Instruction delay = makeNop(0x4004);

        std::vector<Instruction> instructions{j, delay, makeNop(0x4008)};

        CodeGenerator gen({targetSym});
        std::string generated = gen.generateFunction(func, instructions, false);

        t.IsTrue(generated.find("target_func(rdram, ctx, runtime); return;") != std::string::npos,
                 "jump to known function should emit direct call");
    });

    tc.Run("jump to unknown target sets pc", [](TestCase &t) {
            Function func;
            func.name = "jump_unknown";
            func.start = 0x6000;
            func.end = 0x6010;
            func.isRecompiled = true;
            func.isStub = false;

            Instruction j{};
            j.address = 0x6000;
            j.opcode = OPCODE_J;
            j.target = 0x001234; // target = 0x00048d0
            j.hasDelaySlot = true;
            j.raw = (OPCODE_J << 26) | (j.target & 0x3FFFFFF);
            Instruction delay = makeNop(0x6004);

            std::vector<Instruction> instructions{j, delay};

            CodeGenerator gen({});
            std::string generated = gen.generateFunction(func, instructions, false);

            t.IsTrue(generated.find("ctx->pc = 0x") != std::string::npos, "unknown jump target should set ctx->pc");
            t.IsTrue(generated.find("goto label_") == std::string::npos, "external jump should not use goto");
        });

        tc.Run("renamed function used in jump table", [](TestCase &t) {
            Function func;
            func.name = "jt_func";
            func.start = 0x7000;
            func.end = 0x7010;
            func.isRecompiled = true;
            func.isStub = false;

            JumpTableEntry entry;
            entry.index = 0;
            entry.target = 0x8000;
            std::vector<JumpTableEntry> entries{entry};

            Instruction inst{};
            inst.opcode = OPCODE_REGIMM;

            CodeGenerator gen({});
            gen.setRenamedFunctions({{0x8000, "renamed_target"}});

            std::string sw = gen.generateJumpTableSwitch(inst, 0x0, entries);

        t.IsTrue(sw.find("renamed_target(rdram, ctx, runtime);") != std::string::npos,
                 "jump table should use renamed function name");
        });

        tc.Run("reserved identifiers are sanitized and used in calls", [](TestCase &t) {
            Function func;
            func.name = "__is_pointer";
            func.start = 0x9000;
            func.end = 0x9010;
            func.isRecompiled = true;
            func.isStub = false;

            Symbol targetSym;
            targetSym.name = "__is_pointer";
            targetSym.address = func.start;
            targetSym.isFunction = true;

            Instruction j{};
            j.address = 0x8000;
            j.opcode = OPCODE_J;
            j.target = (targetSym.address >> 2) & 0x3FFFFFF;
            j.hasDelaySlot = true;
            j.raw = (OPCODE_J << 26) | (j.target & 0x3FFFFFF);
            Instruction delay = makeNop(0x8004);

            std::vector<Instruction> instructions{j, delay};

            CodeGenerator gen({targetSym});
            gen.setRenamedFunctions({{targetSym.address, "ps2___is_pointer"}});

            std::string generated = gen.generateFunction(func, instructions, false);

            t.IsTrue(generated.find("void ps2___is_pointer(") != std::string::npos,
                     "definition should use sanitized name");
            t.IsTrue(generated.find("ps2___is_pointer(rdram, ctx, runtime); return;") != std::string::npos,
                     "call should use sanitized name");
        }); });
}
