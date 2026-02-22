#include "MiniTest.h"
#include "ps2recomp/code_generator.h"
#include "ps2recomp/instructions.h"
#include "ps2recomp/ps2_recompiler.h"
#include "ps2recomp/types.h"
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>

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

static std::string readFileFromCandidates(const std::vector<std::string> &candidates)
{
    for (const auto &path : candidates)
    {
        std::ifstream file(path);
        if (file)
        {
            std::ostringstream ss;
            ss << file.rdbuf();
            return ss.str();
        }
    }
    return {};
}

static std::vector<uint32_t> parseEnumValues(const std::string &text, const std::string &prefix)
{
    std::vector<uint32_t> values;
    std::regex re("\\b(" + prefix + "[A-Za-z0-9_]+)\\b\\s*=\\s*0x([0-9A-Fa-f]+)");
    for (auto it = std::sregex_iterator(text.begin(), text.end(), re); it != std::sregex_iterator(); ++it)
    {
        const auto &match = *it;
        uint32_t value = static_cast<uint32_t>(std::stoul(match[2].str(), nullptr, 16));
        values.push_back(value);
    }
    return values;
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

        CodeGenerator gen({}, {});
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

        CodeGenerator gen({}, {});
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

        CodeGenerator gen({}, {});
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

        CodeGenerator gen({targetSym}, {});
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

            CodeGenerator gen({}, {});
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

            CodeGenerator gen({}, {});
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

            CodeGenerator gen({targetSym}, {});
            gen.setRenamedFunctions({{targetSym.address, "ps2___is_pointer"}});

            std::string generated = gen.generateFunction(func, instructions, false);
            printGeneratedCode("reserved identifiers are sanitized and used in calls", generated);

            t.IsTrue(generated.find("void ps2___is_pointer(") != std::string::npos,
                     "definition should use sanitized name");
            t.IsTrue(generated.find("ps2___is_pointer(rdram, ctx, runtime); return;") != std::string::npos,
                "call should use sanitized name but got: " + generated);
        });

        tc.Run("COP0 MFC0/MTC0 translate to COP0 register access", [](TestCase &t) {
            CodeGenerator gen({}, {});

            Instruction mfc0{};
            mfc0.opcode = OPCODE_COP0;
            mfc0.rs = COP0_MF;
            mfc0.rt = 5;
            mfc0.rd = COP0_REG_STATUS;

            std::string mfc0Code = gen.translateInstruction(mfc0);
            printGeneratedCode("COP0 MFC0/MTC0 translate to COP0 register access (MFC0)", mfc0Code);
            t.IsTrue(mfc0Code.find("SET_GPR_U32(ctx, 5") != std::string::npos, "MFC0 should write to rt");
            t.IsTrue(mfc0Code.find("ctx->cop0_status") != std::string::npos, "MFC0 STATUS should read cop0_status");
            t.IsTrue(mfc0Code.find("Unimplemented COP0 register") == std::string::npos, "MFC0 should not hit unimplemented COP0 register path");
            t.IsTrue(mfc0Code.find("Unhandled COP0") == std::string::npos, "MFC0 should not hit unhandled COP0 path");

            Instruction mtc0{};
            mtc0.opcode = OPCODE_COP0;
            mtc0.rs = COP0_MT;
            mtc0.rt = 7;
            mtc0.rd = COP0_REG_STATUS;

            std::string mtc0Code = gen.translateInstruction(mtc0);
            printGeneratedCode("COP0 MFC0/MTC0 translate to COP0 register access (MTC0)", mtc0Code);
            t.IsTrue(mtc0Code.find("ctx->cop0_status") != std::string::npos, "MTC0 STATUS should write cop0_status");
            t.IsTrue(mtc0Code.find("GPR_U32(ctx, 7)") != std::string::npos, "MTC0 should read from rt");
            t.IsTrue(mtc0Code.find("Unimplemented MTC0") == std::string::npos, "MTC0 should not hit unimplemented path");
            t.IsTrue(mtc0Code.find("Unhandled COP0") == std::string::npos, "MTC0 should not hit unhandled COP0 path");
        });

        tc.Run("FCR access uses CFC1/CTC1", [](TestCase &t) {
            CodeGenerator gen({}, {});

            Instruction cfc1{};
            cfc1.opcode = OPCODE_COP1;
            cfc1.rs = COP1_CF;
            cfc1.rt = 4;
            cfc1.rd = 31;

            std::string cfc1Code = gen.translateInstruction(cfc1);
            printGeneratedCode("FCR access uses CFC1/CTC1 (CFC1)", cfc1Code);
            t.IsTrue(cfc1Code.find("SET_GPR_U32(ctx, 4") != std::string::npos, "CFC1 should write to rt");
            t.IsTrue(cfc1Code.find("ctx->fcr31") != std::string::npos, "CFC1 FCR31 should read fcr31");
            t.IsTrue(cfc1Code.find("Unimplemented FCR") == std::string::npos, "CFC1 should not hit unimplemented FCR path");

            Instruction ctc1{};
            ctc1.opcode = OPCODE_COP1;
            ctc1.rs = COP1_CT;
            ctc1.rt = 4;
            ctc1.rd = 31;

            std::string ctc1Code = gen.translateInstruction(ctc1);
            printGeneratedCode("FCR access uses CFC1/CTC1 (CTC1)", ctc1Code);
            t.IsTrue(ctc1Code.find("ctx->fcr31 = GPR_U32(ctx, 4) & 0x0183FFFF") != std::string::npos,
                     "CTC1 FCR31 should mask and write fcr31");
            t.IsTrue(ctc1Code.find("ignored") == std::string::npos, "CTC1 FCR31 should not be ignored");
        });

        tc.Run("VU CReg access uses CFC2/CTC2", [](TestCase &t) {
            CodeGenerator gen({}, {});

            Instruction cfc2{};
            cfc2.opcode = OPCODE_COP2;
            cfc2.rs = COP2_CFC2;
            cfc2.rt = 2;
            cfc2.rd = VU0_CR_STATUS;

            std::string cfc2Code = gen.translateInstruction(cfc2);
            printGeneratedCode("VU CReg access uses CFC2/CTC2 (CFC2)", cfc2Code);
            t.IsTrue(cfc2Code.find("SET_GPR_U32(ctx, 2") != std::string::npos, "CFC2 should write to rt");
            t.IsTrue(cfc2Code.find("ctx->vu0_status") != std::string::npos, "CFC2 STATUS should read vu0_status");
            t.IsTrue(cfc2Code.find("Unimplemented CFC2 VU CReg") == std::string::npos, "CFC2 should not hit unimplemented CReg path");

            Instruction ctc2{};
            ctc2.opcode = OPCODE_COP2;
            ctc2.rs = COP2_CTC2;
            ctc2.rt = 3;
            ctc2.rd = VU0_CR_ITOP;

            std::string ctc2Code = gen.translateInstruction(ctc2);
            printGeneratedCode("VU CReg access uses CFC2/CTC2 (CTC2)", ctc2Code);
            t.IsTrue(ctc2Code.find("ctx->vu0_itop") != std::string::npos, "CTC2 ITOP should write vu0_itop");
            t.IsTrue(ctc2Code.find("GPR_U32(ctx, 3) & 0x3FF") != std::string::npos, "CTC2 ITOP should mask to 10 bits");
            t.IsTrue(ctc2Code.find("Unimplemented CTC2 VU CReg") == std::string::npos, "CTC2 should not hit unimplemented CReg path");
        });

        tc.Run("VU0 macro mappings cover all S1/S2 enums", [](TestCase &t) {
            const std::vector<std::string> candidates = {
                "ps2xRecomp/include/ps2recomp/instructions.h",
                "../ps2xRecomp/include/ps2recomp/instructions.h",
                "../../ps2xRecomp/include/ps2recomp/instructions.h"
            };

            std::string text = readFileFromCandidates(candidates);
            t.IsTrue(!text.empty(), "instructions.h should be readable from the test working directory");

            std::vector<uint32_t> s1 = parseEnumValues(text, "VU0_S1_");
            std::vector<uint32_t> s2 = parseEnumValues(text, "VU0_S2_");
            t.IsTrue(!s1.empty(), "VU0_S1 enum list should not be empty");
            t.IsTrue(!s2.empty(), "VU0_S2 enum list should not be empty");

            CodeGenerator gen({}, {});

            for (uint32_t value : s1)
            {
                Instruction inst;
                inst.opcode = OPCODE_COP2;
                inst.rs = COP2_CO; // format
                inst.rt = 2;
                inst.rd = 3;
                inst.function = value;
                inst.vectorInfo.vectorField = 0xF;

                std::string out = gen.translateInstruction(inst);
                std::ostringstream msg;
                msg << "VU0 S1 0x" << std::hex << value << " should be mapped";
                t.IsTrue(out.find("Unhandled VU0 Special1") == std::string::npos, msg.str().c_str());
            }

            for (uint32_t value : s2)
            {
                Instruction inst;
                inst.opcode = OPCODE_COP2;
                inst.rs = COP2_CO; // format
                inst.rt = 2;
                inst.rd = 3;
                inst.function = 0x3C; // force Special2 path
                inst.vectorInfo.vectorField = 0xF;

                uint32_t upper = (value >> 2) & 0x1F;
                uint32_t lower = value & 0x3;
                inst.raw = (upper << 6) | lower;

                std::string out = gen.translateInstruction(inst);
                std::ostringstream msg;
                msg << "VU0 S2 0x" << std::hex << value << " should be mapped";
                t.IsTrue(out.find("Unhandled VU0 Special2") == std::string::npos, msg.str().c_str());
            }
        });

        tc.Run("VU0 S1 uses fd/fs/ft fields (sa/rd/rt)", [](TestCase &t) {
            Instruction inst{};
            inst.opcode = OPCODE_COP2;
            inst.rs = COP2_CO | 0xB; // format + destination mask bits, not a VF register index
            inst.rt = 7;
            inst.rd = 11;
            inst.sa = 3;
            inst.function = VU0_S1_VADD;
            inst.vectorInfo.vectorField = 0xF;

            CodeGenerator gen({}, {});
            std::string out = gen.translateInstruction(inst);

            t.IsTrue(out.find("ctx->vu0_vf[11]") != std::string::npos, "S1 fs should come from rd");
            t.IsTrue(out.find("ctx->vu0_vf[7]") != std::string::npos, "S1 ft should come from rt");
            t.IsTrue(out.find("ctx->vu0_vf[3]") != std::string::npos, "S1 fd should come from sa");
            t.IsTrue(out.find("ctx->vu0_vf[27]") == std::string::npos, "S1 must not use rs(format) as register index");
        });

        tc.Run("VU0 S1 q/i forms keep mask and use sa as destination", [](TestCase &t) {
            Instruction inst{};
            inst.opcode = OPCODE_COP2;
            inst.rs = COP2_CO | 0x9; // format + destination mask bits
            inst.rt = 5;
            inst.rd = 13;
            inst.sa = 4;
            inst.function = VU0_S1_VADDq;
            inst.vectorInfo.vectorField = 0x9;

            CodeGenerator gen({}, {});
            std::string out = gen.translateInstruction(inst);

            t.IsTrue(out.find("_mm_blendv_ps") != std::string::npos, "S1 q/i form should honor destination mask");
            t.IsTrue(out.find("ctx->vu0_vf[13]") != std::string::npos, "S1 q/i source should come from rd");
            t.IsTrue(out.find("ctx->vu0_vf[4]") != std::string::npos, "S1 q/i destination should come from sa");
            t.IsTrue(out.find("ctx->vu0_vf[25]") == std::string::npos, "S1 q/i must not use rs(format) as register index");
        });

        tc.Run("VU0 S2 vector ops use rd as source and rt as destination", [](TestCase &t) {
            Instruction inst{};
            inst.opcode = OPCODE_COP2;
            inst.rs = COP2_CO | 0x6; // format + destination mask bits
            inst.rt = 8;
            inst.rd = 12;
            inst.function = 0x3C; // force Special2 path
            inst.vectorInfo.vectorField = 0xF;

            uint32_t upper = (VU0_S2_VABS >> 2) & 0x1F;
            uint32_t lower = VU0_S2_VABS & 0x3;
            inst.raw = (upper << 6) | lower;

            CodeGenerator gen({}, {});
            std::string out = gen.translateInstruction(inst);

            t.IsTrue(out.find("ctx->vu0_vf[12]") != std::string::npos, "S2 source VF should come from rd");
            t.IsTrue(out.find("ctx->vu0_vf[8]") != std::string::npos, "S2 destination VF should come from rt");
            t.IsTrue(out.find("ctx->vu0_vf[22]") == std::string::npos, "S2 must not use rs(format) as register index");
        });

        tc.Run("VU0 S2 VI memory ops use rd as VI base register", [](TestCase &t) {
            Instruction inst{};
            inst.opcode = OPCODE_COP2;
            inst.rs = COP2_CO | 0x4; // format + destination mask bits
            inst.rt = 6;
            inst.rd = 14;
            inst.function = 0x3C; // force Special2 path
            inst.vectorInfo.vectorField = 0xF;

            uint32_t upper = (VU0_S2_VLQI >> 2) & 0x1F;
            uint32_t lower = VU0_S2_VLQI & 0x3;
            inst.raw = (upper << 6) | lower;

            CodeGenerator gen({}, {});
            std::string out = gen.translateInstruction(inst);

            t.IsTrue(out.find("ctx->vi[14]") != std::string::npos, "S2 VLQI base VI should come from rd");
            t.IsTrue(out.find("ctx->vu0_vf[6]") != std::string::npos, "S2 VLQI destination VF should come from rt");
            t.IsTrue(out.find("ctx->vi[20]") == std::string::npos, "S2 VLQI must not use rs(format) as VI index");
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
            
            CodeGenerator gen({targetSym}, {});
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
            t.IsTrue(generated.find("const uint32_t __entryPc = ctx->pc;") != std::string::npos,
                     "JAL should capture entry PC before call");
            t.IsTrue(generated.find("if (ctx->pc == __entryPc) { ctx->pc = 0xA008u; }") != std::string::npos,
                     "JAL should recover fallthrough when callee leaves ctx->pc unchanged");
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

            CodeGenerator gen({}, {});
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

            CodeGenerator gen({}, {});
            std::string generated = gen.generateFunction(func, {jalr, delay}, false);
            printGeneratedCode("JALR emits indirect call", generated);

            t.IsTrue(generated.find("uint32_t jumpTarget = GPR_U32(ctx, 4);") != std::string::npos, "JALR should read target from RS");
            t.IsTrue(generated.find("SET_GPR_U32(ctx, 31, 0xD008u);") != std::string::npos, "JALR should set link register");
            t.IsTrue(generated.find("auto targetFn = runtime->lookupFunction(jumpTarget);") != std::string::npos, "JALR should lookup function");
            t.IsTrue(generated.find("targetFn(rdram, ctx, runtime);") != std::string::npos, "JALR should call function");
            t.IsTrue(generated.find("const uint32_t __entryPc = ctx->pc;") != std::string::npos,
                     "JALR should capture entry PC before indirect call");
            t.IsTrue(generated.find("if (ctx->pc == __entryPc) { ctx->pc = 0xD008u; }") != std::string::npos,
                     "JALR should recover fallthrough when callee leaves ctx->pc unchanged");
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

            CodeGenerator gen({}, {});
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

            CodeGenerator gen({}, {});
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

            // Create an internal JAL with an explicit instruction at returnAddr (0x1308).
            Instruction jal = makeJal(0x1300, 0x1310);
            Instruction jalDelay = makeNop(0x1304);
            Instruction atReturn = makeNop(0x1308);
            Instruction atTarget = makeNop(0x1310);

            // JR $31 at 0x1314 with delay slot at 0x1318
            Instruction jr = makeJr(0x1314, 31);
            Instruction jrDelay = makeNop(0x1318);

            CodeGenerator gen({}, {});
            std::string generated = gen.generateFunction(func, { jal, jalDelay, atReturn, atTarget, jr, jrDelay }, false);
            printGeneratedCode("JR $31 emits switch for internal return targets", generated);

            t.IsTrue(generated.find("switch (jumpTarget)") != std::string::npos, "JR $31 should emit switch for internal targets");
            t.IsTrue(generated.find("case 0x1308u: goto label_1308;") != std::string::npos, "switch should include return address from internal JAL");
        });

        tc.Run("JR non-RA emits switch for in-function jump targets", [](TestCase &t) {
            Function func;
            func.name = "jr_non_ra_switch";
            func.start = 0x1400;
            func.end = 0x1420;
            func.isRecompiled = true;
            func.isStub = false;

            // 0x1400: nop
            // 0x1404: jr $16 (register jump)
            // 0x1408: nop (delay slot)
            // 0x140c: nop
            Instruction i0 = makeNop(0x1400);
            Instruction jr = makeJr(0x1404, 16);
            Instruction delay = makeNop(0x1408);
            Instruction i3 = makeNop(0x140c);

            CodeGenerator gen({}, {});
            std::string generated = gen.generateFunction(func, {i0, jr, delay, i3}, false);
            printGeneratedCode("JR non-RA emits switch for in-function jump targets", generated);

            t.IsTrue(generated.find("switch (jumpTarget)") != std::string::npos,
                     "JR via non-RA register should emit switch for internal targets");
            t.IsTrue(generated.find("case 0x1400u: goto label_1400;") != std::string::npos,
                     "switch should include in-function entry label");
            t.IsTrue(generated.find("case 0x140Cu: goto label_140c;") != std::string::npos,
                     "switch should include other in-function labels");
        });

        tc.Run("JALR includes switch and fallback/guard pair", [](TestCase &t) {
            Function func;
            func.name = "jalr_switch_and_fallback";
            func.start = 0x1500;
            func.end = 0x1530;
            func.isRecompiled = true;
            func.isStub = false;

            // A call-like setup so there are multiple in-function labels to dispatch to.
            Instruction jal = makeJal(0x1500, 0x1510);
            Instruction jalDelay = makeNop(0x1504);
            Instruction atReturn = makeNop(0x1508);
            Instruction atTarget = makeNop(0x1510);
            Instruction jalr = makeJalr(0x1514, 4, 31);
            Instruction jalrDelay = makeNop(0x1518);

            CodeGenerator gen({}, {});
            std::string generated = gen.generateFunction(func, {jal, jalDelay, atReturn, atTarget, jalr, jalrDelay}, false);
            printGeneratedCode("JALR includes switch and fallback/guard pair", generated);

            t.IsTrue(generated.find("switch (jumpTarget)") != std::string::npos,
                     "JALR should emit switch when in-function register-jump targets exist");
            t.IsTrue(generated.find("case 0x1508u: goto label_1508;") != std::string::npos,
                     "switch should include internal return label from JAL in same function");
            t.IsTrue(generated.find("if (ctx->pc == __entryPc) { ctx->pc = 0x151Cu; }") != std::string::npos,
                     "JALR should contain unchanged-PC fallback to fallthrough");
            t.IsTrue(generated.find("if (ctx->pc != 0x151Cu) { return; }") != std::string::npos,
                     "JALR should retain non-fallthrough guard");
        });

        tc.Run("resolveStubTarget allows leading underscore alias", [](TestCase &t) {
            t.Equals(PS2Recompiler::resolveStubTarget("_rand"), StubTarget::Stub,
                     "_rand should resolve via rand stub alias");
            t.Equals(PS2Recompiler::resolveStubTarget("_GetThreadId"), StubTarget::Syscall,
                     "_GetThreadId should resolve via GetThreadId syscall alias");
            t.Equals(PS2Recompiler::resolveStubTarget("_DefinitelyNotARealCall"), StubTarget::Unknown,
                     "unknown names must still stay unknown");
        });
    
    });
}
