#include "MiniTest.h"
#include "ps2recomp/code_generator.h"
#include "ps2recomp/instructions.h"
#include "ps2recomp/types.h"

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

static Instruction makeJal(uint32_t address, uint32_t target)
{
    Instruction inst{};
    inst.address = address;
    inst.opcode = OPCODE_JAL;
    inst.target = (target >> 2) & 0x3FFFFFF;
    inst.hasDelaySlot = true;
    inst.raw = (OPCODE_JAL << 26) | inst.target;
    return inst;
}

static Instruction makeJalr(uint32_t address, uint8_t rs, uint8_t rd)
{
    Instruction inst{};
    inst.address = address;
    inst.opcode = OPCODE_SPECIAL;
    inst.function = SPECIAL_JALR;
    inst.rs = rs;
    inst.rd = rd; // Destination for link address (default 31)
    inst.hasDelaySlot = true;
    inst.raw = (OPCODE_SPECIAL << 26) | (rs << 21) | (0 << 16) | (rd << 11) | (0 << 6) | SPECIAL_JALR;
    return inst;
}

static Instruction makeJr(uint32_t address, uint8_t rs)
{
    Instruction inst{};
    inst.address = address;
    inst.opcode = OPCODE_SPECIAL;
    inst.function = SPECIAL_JR;
    inst.rs = rs;
    inst.hasDelaySlot = true;
    inst.raw = (OPCODE_SPECIAL << 26) | (rs << 21) | SPECIAL_JR;
    return inst;
}

static void printGeneratedCode(const std::string& name, const std::string& code)
{
#ifdef PRINT_GENERATED_CODE
    std::cout << "=== Generated Code for " << name << " ===" << std::endl;
    std::cout << code << std::endl;
    std::cout << "========================================" << std::endl;
#endif
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
        printGeneratedCode("emits labels and gotos for internal branches", generated);

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
        printGeneratedCode("labels delay slot when it is a branch target", generated);

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
        printGeneratedCode("branches outside function still set pc", generated);

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
        printGeneratedCode("jumps to known symbols call by name", generated);

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
            printGeneratedCode("jump to unknown target sets pc", generated);

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
            printGeneratedCode("renamed function used in jump table", sw);

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
            printGeneratedCode("reserved identifiers are sanitized and used in calls", generated);

            t.IsTrue(generated.find("void ps2___is_pointer(") != std::string::npos,
                     "definition should use sanitized name");
            t.IsTrue(generated.find("ps2___is_pointer(rdram, ctx, runtime); return;") != std::string::npos,
                "call should use sanitized name but got: " + generated);
        });

        tc.Run("JAL to known function emits call and check", [](TestCase &t) {
            Function func;
            func.name = "jal_test";
            func.start = 0xA000;
            func.end = 0xA020;
            func.isRecompiled = true;
            func.isStub = false;

            Symbol targetSym;
            targetSym.name = "some_func";
            targetSym.address = 0xB000;
            targetSym.isFunction = true;

            // 0xA000: JAL 0xB000
            // 0xA004: NOP (delay slot)
            Instruction jal = makeJal(0xA000, 0xB000);
            Instruction delay = makeNop(0xA004);
            
            CodeGenerator gen({targetSym});
            std::string generated = gen.generateFunction(func, {jal, delay}, false);
            printGeneratedCode("JAL to known function emits call and check", generated);

            // Expect:
            // SET_GPR_U32(ctx, 31, 0xA008u);
            // ctx->pc = 0xA004u;
            // ... delay slot ...
            // some_func(rdram, ctx, runtime);
            // if (ctx->pc != 0xA008u) { return; }

            t.IsTrue(generated.find("SET_GPR_U32(ctx, 31, 0xA008u);") != std::string::npos, "JAL should set RA");
            t.IsTrue(generated.find("some_func(rdram, ctx, runtime);") != std::string::npos, "JAL should call function");
            t.IsTrue(generated.find("if (ctx->pc != 0xA008u) { return; }") != std::string::npos, "JAL should check return PC");
        });

        tc.Run("JAL to internal target becomes goto", [](TestCase &t) {
            Function func;
            func.name = "jal_internal";
            func.start = 0xC000;
            func.end = 0xC020;
            func.isRecompiled = true;
            func.isStub = false;

            // 0xC000: JAL 0xC010
            // 0xC004: NOP
            // ...
            // 0xC010: NOP
            Instruction jal = makeJal(0xC000, 0xC010);
            Instruction delay = makeNop(0xC004);
            Instruction targetInst = makeNop(0xC010);

            CodeGenerator gen({});
            std::string generated = gen.generateFunction(func, {jal, delay, targetInst}, false);
            printGeneratedCode("JAL to internal target becomes goto", generated);

            t.IsTrue(generated.find("SET_GPR_U32(ctx, 31, 0xC008u);") != std::string::npos, "Internal JAL should set RA");
            t.IsTrue(generated.find("goto label_c010;") != std::string::npos, "Internal JAL should use goto");
        });

        tc.Run("JALR emits indirect call", [](TestCase &t) {
            Function func;
            func.name = "jalr_test";
            func.start = 0xD000;
            func.end = 0xD020;
            func.isRecompiled = true;
            func.isStub = false;

            // 0xD000: JALR $4, $31 (call addr in $4, link to $31)
            // 0xD004: NOP
            Instruction jalr = makeJalr(0xD000, 4, 31);
            Instruction delay = makeNop(0xD004);

            CodeGenerator gen({});
            std::string generated = gen.generateFunction(func, {jalr, delay}, false);
            printGeneratedCode("JALR emits indirect call", generated);

            t.IsTrue(generated.find("uint32_t jumpTarget = GPR_U32(ctx, 4);") != std::string::npos, "JALR should read target from RS");
            t.IsTrue(generated.find("SET_GPR_U32(ctx, 31, 0xD008u);") != std::string::npos, "JALR should set link register");
            t.IsTrue(generated.find("auto targetFn = runtime->lookupFunction(jumpTarget);") != std::string::npos, "JALR should lookup function");
            t.IsTrue(generated.find("targetFn(rdram, ctx, runtime);") != std::string::npos, "JALR should call function");
             t.IsTrue(generated.find("if (ctx->pc != 0xD008u) { return; }") != std::string::npos, "JALR should check return PC");
        }); 

        tc.Run("backward BEQ emits label and goto (sign-extended offset)", [](TestCase &t) {
            Function func;
            func.name = "backward_branch";
            func.start = 0x1100;
            func.end = 0x1120;
            func.isRecompiled = true;
            func.isStub = false;

            // 0x1100: nop
            // 0x1104: beq $1,$1, target 0x1100 (offset = -2 words)
            // 0x1108: nop (delay)
            std::vector<Instruction> instructions;
            instructions.push_back(makeNop(0x1100));

            Instruction br = makeBranch(0x1104, 0); 
            br.simmediate = static_cast<uint32_t>(static_cast<int16_t>(-2)); 
            instructions.push_back(br);

            instructions.push_back(makeNop(0x1108));
            instructions.push_back(makeNop(0x110c));

            CodeGenerator gen({});
            std::string generated = gen.generateFunction(func, instructions, false);
            printGeneratedCode("backward BEQ emits label and goto (sign-extended offset)", generated);

            t.IsTrue(generated.find("label_1100:") != std::string::npos, "target should emit a label");
            t.IsTrue(generated.find("goto label_1100;") != std::string::npos, "backward internal branch should goto label");
        });

        tc.Run("branch-likely places delay slot only in taken path", [](TestCase &t) {
            Function func;
            func.name = "branch_likely";
            func.start = 0x1200;
            func.end = 0x1220;
            func.isRecompiled = true;
            func.isStub = false;

            Instruction br{};
            br.address = 0x1200;
            br.opcode = OPCODE_BEQL;   // likely
            br.rs = 1;
            br.rt = 2;
            br.simmediate = 1;         // target = 0x1208
            br.isBranch = true;
            br.hasDelaySlot = true;
            br.raw = 0;  

            Instruction delay{};
            delay.address = 0x1204;
            delay.opcode = OPCODE_ADDIU;
            delay.rs = 0;
            delay.rt = 7;              // make it non-nop so translation is distinctive
            delay.simmediate = 123;
            delay.raw = 0;

            Instruction target = makeNop(0x1208);

            CodeGenerator gen({});
            std::string generated = gen.generateFunction(func, { br, delay, target }, false);
            printGeneratedCode("branch-likely places delay slot only in taken path", generated);
 
            t.IsTrue(generated.find("SET_GPR_S32(ctx, 7,") != std::string::npos, "delay slot should be translated");
            t.IsTrue(generated.find("if (branch_taken_0x1200)") != std::string::npos, "should generate branch_taken variable and if for likely branch");
        });

        tc.Run("JR $31 emits switch for internal return targets", [](TestCase &t) {
            Function func;
            func.name = "jr_ra_switch";
            func.start = 0x1300;
            func.end = 0x1340;
            func.isRecompiled = true;
            func.isStub = false;

            // Create an internal JAL so collectInternalBranchTargets inserts returnAddr (0x1308) as internal target.
            Instruction jal = makeJal(0x1300, 0x1310);
            Instruction jalDelay = makeNop(0x1304);
            Instruction atTarget = makeNop(0x1310);

            // JR $31 at 0x1314 with delay slot at 0x1318
            Instruction jr = makeJr(0x1314, 31);
            Instruction jrDelay = makeNop(0x1318);

            CodeGenerator gen({});
            std::string generated = gen.generateFunction(func, { jal, jalDelay, atTarget, jr, jrDelay }, false);
            printGeneratedCode("JR $31 emits switch for internal return targets", generated);

            t.IsTrue(generated.find("switch (jumpTarget)") != std::string::npos, "JR $31 should emit switch for internal targets");
            t.IsTrue(generated.find("case 0x1308u: goto label_1308;") != std::string::npos, "switch should include return address from internal JAL");
        });
    
    
    });
}
