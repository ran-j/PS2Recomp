#include "MiniTest.h"
#include "ps2recomp/code_generator.h"
#include "ps2recomp/codegen_helpers.h"
#include "ps2recomp/instructions.h"
#include "ps2recomp/ps2_recompiler.h"
#include "ps2recomp/r5900_decoder.h"
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

static uint32_t signExtend16(uint16_t value)
{
    return static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(value)));
}

static Instruction makeIType(uint32_t address, uint32_t opcode, uint8_t rs, uint8_t rt, uint16_t immediate)
{
    Instruction inst{};
    inst.address = address;
    inst.opcode = opcode;
    inst.rs = rs;
    inst.rt = rt;
    inst.immediate = immediate;
    inst.simmediate = signExtend16(immediate);
    inst.raw = (opcode << 26) | (static_cast<uint32_t>(rs) << 21) |
               (static_cast<uint32_t>(rt) << 16) | immediate;
    return inst;
}

static Instruction makeLui(uint32_t address, uint8_t rt, uint16_t immediate)
{
    return makeIType(address, OPCODE_LUI, 0, rt, immediate);
}

static Instruction makeOri(uint32_t address, uint8_t rt, uint8_t rs, uint16_t immediate)
{
    return makeIType(address, OPCODE_ORI, rs, rt, immediate);
}

static Instruction makeAddiu(uint32_t address, uint8_t rt, uint8_t rs, uint16_t immediate)
{
    return makeIType(address, OPCODE_ADDIU, rs, rt, immediate);
}

static Instruction makeLw(uint32_t address, uint8_t rt, uint8_t rs, uint16_t immediate)
{
    return makeIType(address, OPCODE_LW, rs, rt, immediate);
}

static Instruction makeSw(uint32_t address, uint8_t rt, uint8_t rs, uint16_t immediate)
{
    return makeIType(address, OPCODE_SW, rs, rt, immediate);
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

// Enumerators with the given prefix, counted however they are initialised. parseEnumValues only
// matches a hexadecimal initialiser; held against this, a list that silently stopped covering an
// opcode fails instead of shrinking. No number is written down, so adding an opcode does not touch
// either side.
static size_t countEnumDeclarations(const std::string &text, const std::string &prefix)
{
    std::regex re("\\b" + prefix + "[A-Za-z0-9_]+\\b\\s*=");
    size_t count = 0;
    for (auto it = std::sregex_iterator(text.begin(), text.end(), re); it != std::sregex_iterator(); ++it)
    {
        ++count;
    }
    return count;
}

static std::vector<std::pair<bool, uint32_t>> vu0MacroOps(TestCase &t)
{
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
    t.IsTrue(s1.size() == countEnumDeclarations(text, "VU0_S1_"),
             "every VU0_S1_ enumerator must be parsed out of instructions.h");
    t.IsTrue(s2.size() == countEnumDeclarations(text, "VU0_S2_"),
             "every VU0_S2_ enumerator must be parsed out of instructions.h");

    std::vector<std::pair<bool, uint32_t>> ops;
    for (uint32_t value : s1)
    {
        ops.emplace_back(true, value);
    }
    for (uint32_t value : s2)
    {
        ops.emplace_back(false, value);
    }
    return ops;
}

// A guard added at one of these sites can read any field of the instruction, not just the two the
// emission reads, so the sweep builds its operands into a word and decodes it exactly as a
// recompile does. Every field then carries the value production carries - including the format
// field, which the encoding ties to the destination mask, and the modification-info flags - rather
// than a value-initialised zero a guard could key on unnoticed.
static Instruction makeVu0MacroOp(bool isS1, uint32_t value, uint32_t sa, uint32_t rd, uint32_t rt,
                                  uint8_t vectorField = 0xF, uint32_t address = 0x00100000u)
{
    const uint32_t format = static_cast<uint32_t>(COP2_CO) | (vectorField & 0xFu);
    uint32_t raw = (static_cast<uint32_t>(OPCODE_COP2) << 26) | (format << 21) |
                   ((rt & 0x1Fu) << 16) | ((rd & 0x1Fu) << 11);
    if (isS1)
    {
        raw |= ((sa & 0x1Fu) << 6) | (value & 0x3Fu);
    }
    else
    {
        raw |= (((value >> 2) & 0x1Fu) << 6) | (0x3Cu | (value & 0x3u));
    }

    static const R5900Decoder decoder;
    // Both configurations the recompiler runs in: the disassembly text is populated unless the
    // config asks for low memory.
    Instruction inst = decoder.decodeInstruction(address, raw, (vectorField & 0x1u) != 0);
    if (!isS1)
    {
        // A Special2 word has no shift-amount operand - those bits carry its function code - so the
        // sweep's third probe slot is set after the decode.
        inst.sa = sa;
    }
    return inst;
}

static std::vector<std::string> collectMaskLiterals(const std::string &text)
{
    std::vector<std::string> literals;
    const std::string tag = "_mm_set_epi32(";
    size_t pos = 0;
    while ((pos = text.find(tag, pos)) != std::string::npos)
    {
        const size_t end = text.find(')', pos);
        if (end == std::string::npos)
        {
            break;
        }
        literals.push_back(text.substr(pos, end - pos + 1));
        pos = end + 1;
    }
    return literals;
}

// Every `ctx->vu0_vf[N] = ` assignment in an emission, by index, read exhaustively rather than
// probed at the sweep's three operand values: "and no other" is a claim about all thirty-two VF
// registers, and a probe at three of them is not that claim.
static std::vector<uint32_t> collectVfAssignmentIndices(const std::string &text)
{
    std::vector<uint32_t> indices;
    const std::string tag = "ctx->vu0_vf[";
    size_t pos = 0;
    while ((pos = text.find(tag, pos)) != std::string::npos)
    {
        const size_t start = pos + tag.size();
        size_t end = start;
        while (end < text.size() && text[end] >= '0' && text[end] <= '9')
        {
            ++end;
        }
        if (end > start && text.compare(end, 4, "] = ") == 0)
        {
            indices.push_back(static_cast<uint32_t>(std::stoul(text.substr(start, end - start))));
        }
        pos = start;
    }
    return indices;
}

static std::string vuFieldMaskLiteral(uint32_t mask)
{
    std::ostringstream ss;
    ss << "_mm_set_epi32(" << ((mask & 0x1) ? -1 : 0)
       << ", " << ((mask & 0x2) ? -1 : 0)
       << ", " << ((mask & 0x4) ? -1 : 0)
       << ", " << ((mask & 0x8) ? -1 : 0) << ")";
    return ss.str();
}

static bool isVuFieldMaskLiteral(const std::string &literal)
{
    static const std::vector<std::string> table = []() {
        std::vector<std::string> values;
        for (uint32_t mask = 0; mask <= 0xF; ++mask)
        {
            values.push_back(vuFieldMaskLiteral(mask));
        }
        return values;
    }();

    for (const std::string &candidate : table)
    {
        if (literal == candidate)
        {
            return true;
        }
    }
    return false;
}

// The shape each VU0 macro-mode opcode must emit, declared here rather than read back off an
// emission. Every expectation this file computes by calling the generator - a reference emission, a
// sibling emission, the absence of an "Unhandled" marker - is satisfied by a translator that emits
// nothing, because an empty string carries no mask literal, no destination assignment and no
// marker. These two numbers are facts about the dispatch in vu_translator.cpp and the format strings
// in vu_translation_helpers.cpp, and they do not move when a site stops emitting.
//
// maskLiterals: how many _mm_set_epi32 literals in the field-mask table the site carries.
// vfDestSlot:   which of the sweep's three probe slots (0 = sa, 2 = rt) holds this opcode's VF
//               destination register, or -1 for the sites that write no VF register at all - the
//               accumulator "A" family, the two stores, the VI and Q and R writers.
//
// The default arms are deliberately the stronger claim, so an opcode added to instructions.h
// without a row here fails this file rather than passing unanchored.
struct Vu0MacroShape
{
    int maskLiterals;
    int vfDestSlot;
};

static Vu0MacroShape vu0MacroShape(bool isS1, uint32_t value)
{
    if (isS1)
    {
        switch (value)
        {
        case VU0_S1_VIADD:
        case VU0_S1_VISUB:
        case VU0_S1_VIADDI:
        case VU0_S1_VIAND:
        case VU0_S1_VIOR:
        case VU0_S1_VCALLMS:
        case VU0_S1_VCALLMSR:
            return {0, -1};
        default:
            // 0x00..0x2F: every Special1 vector ALU form, masked-blend into the shift-amount slot.
            return {1, 0};
        }
    }

    switch (value)
    {
    case VU0_S2_VITOF0:
    case VU0_S2_VITOF4:
    case VU0_S2_VITOF12:
    case VU0_S2_VITOF15:
    case VU0_S2_VFTOI0:
    case VU0_S2_VFTOI4:
    case VU0_S2_VFTOI12:
    case VU0_S2_VFTOI15:
    case VU0_S2_VABS:
    case VU0_S2_VMOVE:
    case VU0_S2_VMR32:
    case VU0_S2_VLQI:
    case VU0_S2_VLQD:
    case VU0_S2_VMFIR:
    case VU0_S2_VRGET:
        return {1, 2};
    case VU0_S2_VCLIPw:
    case VU0_S2_VNOP:
    case VU0_S2_VDIV:
    case VU0_S2_VSQRT:
    case VU0_S2_VRSQRT:
    case VU0_S2_VWAITQ:
    case VU0_S2_VMTIR:
    case VU0_S2_VILWR:
    case VU0_S2_VISWR:
    case VU0_S2_VRNEXT:
    case VU0_S2_VRINIT:
    case VU0_S2_VRXOR:
        return {0, -1};
    default:
        // The accumulator "A" family and the VSQI/VSQD stores: a field mask, no VF destination.
        return {1, -1};
    }
}

// QMTC2's encoding is COP2 | QMTC2 | rt | fd | 00 0000 0000 | I: bit 0 selects the interlocked form,
// and the ten bits above it are fixed at zero. The first two values are the whole of what a
// conforming encoding can carry; the other two drive the fixed bits anyway, so a guard keyed on
// them cannot pass unnoticed either.
static const uint32_t qmtc2LowBits[] = {0x000u, 0x001u, 0x2AAu, 0x7FFu};

static Instruction makeQmtc2(uint32_t address, uint32_t rd, uint32_t rt, uint32_t lowBits,
                             bool includeDisassembly)
{
    const uint32_t raw = (static_cast<uint32_t>(OPCODE_COP2) << 26) |
                         (static_cast<uint32_t>(COP2_QMTC2) << 21) |
                         ((rt & 0x1Fu) << 16) | ((rd & 0x1Fu) << 11) | (lowBits & 0x7FFu);
    static const R5900Decoder decoder;
    return decoder.decodeInstruction(address, raw, includeDisassembly);
}

static Instruction makeLqc2(uint32_t address, uint32_t rt, uint32_t rs, uint16_t offset,
                            bool includeDisassembly)
{
    const uint32_t raw = (static_cast<uint32_t>(OPCODE_LDC2) << 26) |
                         ((rs & 0x1Fu) << 21) | ((rt & 0x1Fu) << 16) | offset;
    static const R5900Decoder decoder;
    return decoder.decodeInstruction(address, raw, includeDisassembly);
}

// The sixteen-bit offset field at zero, a positive offset, both sign boundaries, and minus one.
// The sign-extended value each produces is read back off the decoded instruction.
static const uint16_t lqc2Immediates[] = {0x0000u, 0x0010u, 0x7FFFu, 0x8000u, 0xFFFFu};

// The load expression an LQC2 emits is a function of three things: the post-decode MMIO flag, the
// resolved-address hint the caller passes, and the address itself. The flag routes the load to the
// runtime loader; so does a resolved address inside a special region, with the flag clear; an
// ordinary resolved address takes the fast read; and with neither set the load reads through the
// address built from the instruction's own operands. The first ten rows below are the whole
// cross-product of the flag, the hint, and an address at each of the three values that matter:
// zero, an ordinary main-RAM address, and one inside the I/O range. Nothing is argued away except
// the address when neither the flag nor the hint is set, which no branch reads - that case is one
// row rather than three.
//
// Those ten rows carry one address value in both members, because setting the flag overwrites the
// hint's address with the MMIO address and the emission therefore cannot tell the two apart. A
// guard is not restricted to what the emission reads: it can read the relation between the two
// members, and the two come from independent sources - inst.mmioAddress from a configuration map
// keyed on instruction address, the hint's address from constant folding of rs plus the immediate -
// so a recompile reaches states in which they disagree. The last three rows drive that disagreement
// at each flag/hint combination that can produce it; with neither flag nor hint set both members
// are zero, so that combination cannot produce it.
// expectedLoad is the exact load expression the row must produce, written out here rather than
// recomputed from the row's own members: a mirror of the branch logic would agree with a generator
// that took the wrong branch, which is the whole thing being guarded against. It is null on the one
// row where neither the flag nor the hint is set, because there the address is built from the
// instruction's own base register and immediate and the test constructs it from the fixture.
struct Lqc2MemoryPath
{
    bool hasHint;
    bool isMmio;
    uint32_t hintAddress;
    uint32_t mmioAddress;
    const char *expectedLoad;
};

static const Lqc2MemoryPath lqc2MemoryPaths[] = {
    {false, false, 0x00000000u, 0x00000000u, nullptr},
    {false, true, 0x00000000u, 0x00000000u, "runtime->Load128(rdram, ctx, 0x0u)"},
    {false, true, 0x00100000u, 0x00100000u, "runtime->Load128(rdram, ctx, 0x100000u)"},
    {false, true, 0x10002000u, 0x10002000u, "runtime->Load128(rdram, ctx, 0x10002000u)"},
    {true, false, 0x00000000u, 0x00000000u, "FAST_READ128(0x0u)"},
    {true, false, 0x00100000u, 0x00100000u, "FAST_READ128(0x100000u)"},
    {true, false, 0x10002000u, 0x10002000u, "runtime->Load128(rdram, ctx, 0x10002000u)"},
    {true, true, 0x00000000u, 0x00000000u, "runtime->Load128(rdram, ctx, 0x0u)"},
    {true, true, 0x00100000u, 0x00100000u, "runtime->Load128(rdram, ctx, 0x100000u)"},
    {true, true, 0x10002000u, 0x10002000u, "runtime->Load128(rdram, ctx, 0x10002000u)"},
    {false, true, 0x00000000u, 0x10002000u, "runtime->Load128(rdram, ctx, 0x10002000u)"},
    {true, false, 0x00100000u, 0x00000000u, "FAST_READ128(0x100000u)"},
    {true, true, 0x00100000u, 0x10002000u, "runtime->Load128(rdram, ctx, 0x10002000u)"},
};

// The load expression a row must produce for a given decoded fixture.
static std::string lqc2ExpectedLoad(const Lqc2MemoryPath &path, const Instruction &inst)
{
    if (path.expectedLoad != nullptr)
    {
        return path.expectedLoad;
    }
    std::ostringstream ss;
    ss << "READ128(ADD32(GPR_U32(ctx, " << inst.rs << "), " << inst.simmediate << "))";
    return ss.str();
}

// The decoder never derives these two members from the instruction word - it clears both
// unconditionally. The recompiler assigns them after the decode, from a configuration map keyed on
// instruction address that carries no opcode filter, so a macro-mode instruction or a QMTC2 listed
// at such an address reaches the code generator with them set. They are the only members of the
// instruction structure a decoder-built fixture does not already pin at its production value, so
// every guard shape is run over the states a recompile can produce: flag clear, flag set with a
// zero address, and flag set with a non-zero address inside the I/O range the analyzer records.
// The flag is never set with the address left unassigned - the two are written together.
struct PostDecodeMmioState
{
    bool isMmio;
    uint32_t mmioAddress;
};

static const PostDecodeMmioState postDecodeMmioStates[] = {
    {false, 0x00000000u},
    {true, 0x00000000u},
    {true, 0x10002000u},
};

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
    tc.Run("R5900 MULT writes rd when rd is non-zero", [](TestCase &t) {
        CodeGenerator gen({}, {});

        Instruction mult{};
        mult.opcode = OPCODE_SPECIAL;
        mult.function = SPECIAL_MULT;
        mult.rs = 4;
        mult.rt = 5;
        mult.rd = 3;

        std::string generated = gen.translateInstruction(mult);
        printGeneratedCode("R5900 MULT writes rd when rd is non-zero", generated);

        t.IsTrue(generated.find("SET_GPR_S32(ctx, 3, (int32_t)result);") != std::string::npos,
                 "MULT should write low product to rd on R5900");

        mult.rd = 0;
        generated = gen.translateInstruction(mult);
        t.IsTrue(generated.find("SET_GPR_S32(") == std::string::npos,
                 "MULT should not write rd when rd is zero");
    });

    tc.Run("R5900 MMI MULT1 writes rd when rd is non-zero", [](TestCase &t) {
        CodeGenerator gen({}, {});

        Instruction mult1{};
        mult1.opcode = OPCODE_MMI;
        mult1.isMMI = true;
        mult1.function = MMI_MULT1;
        mult1.rs = 8;
        mult1.rt = 9;
        mult1.rd = 10;

        std::string generated = gen.translateInstruction(mult1);
        printGeneratedCode("R5900 MMI MULT1 writes rd when rd is non-zero", generated);

        t.IsTrue(generated.find("SET_GPR_S32(ctx, 10, (int32_t)result);") != std::string::npos,
                 "MULT1 should write low product to rd on R5900");
    });

    tc.Run("constant MMIO store emits direct runtime store", [](TestCase &t) {
        Function func;
        func.name = "mmio_store";
        func.start = 0x1000;
        func.end = 0x1010;
        func.isRecompiled = true;

        std::vector<Instruction> instructions;
        instructions.push_back(makeLui(0x1000, 1, 0x1000));
        instructions.push_back(makeOri(0x1004, 1, 1, 0xE020));
        instructions.push_back(makeSw(0x1008, 2, 1, 0));

        CodeGenerator gen({}, {});
        std::string generated = gen.generateFunction(func, instructions, false);
        printGeneratedCode("constant MMIO store emits direct runtime store", generated);

        t.IsTrue(generated.find("runtime->Store32(rdram, ctx, 0x1000E020u, GPR_U32(ctx, 2));") != std::string::npos,
                 "constant MMIO SW should emit a direct runtime Store32");
        t.IsTrue(generated.find("WRITE32(ADD32(GPR_U32(ctx, 1)") == std::string::npos,
                 "constant MMIO SW should not go through WRITE32 address classification");
    });

    tc.Run("constant RDRAM load and store emit fast memory access", [](TestCase &t) {
        Function func;
        func.name = "rdram_access";
        func.start = 0x2000;
        func.end = 0x2014;
        func.isRecompiled = true;

        std::vector<Instruction> instructions;
        instructions.push_back(makeLui(0x2000, 1, 0x0012));
        instructions.push_back(makeOri(0x2004, 1, 1, 0x3450));
        instructions.push_back(makeLw(0x2008, 3, 1, 0x0010));
        instructions.push_back(makeSw(0x200C, 4, 1, 0x0014));

        CodeGenerator gen({}, {});
        std::string generated = gen.generateFunction(func, instructions, false);
        printGeneratedCode("constant RDRAM load and store emit fast memory access", generated);

        t.IsTrue(generated.find("SET_GPR_S32(ctx, 3, (int32_t)FAST_READ32(0x123460u));") != std::string::npos,
                 "constant RDRAM LW should emit FAST_READ32 with the resolved address");
        t.IsTrue(generated.find("FAST_WRITE32(0x123464u, _value);") != std::string::npos,
                 "constant RDRAM SW should emit FAST_WRITE32 with the resolved address");
        t.IsTrue(generated.find("READ32(ADD32(GPR_U32(ctx, 1)") == std::string::npos,
                 "constant RDRAM LW should not go through READ32 address classification");
        t.IsTrue(generated.find("WRITE32(ADD32(GPR_U32(ctx, 1)") == std::string::npos,
                 "constant RDRAM SW should not go through WRITE32 address classification");
    });

    tc.Run("known GIF DMA MMIO sequence emits native kick helper", [](TestCase &t) {
        Function func;
        func.name = "gif_dma_kick";
        func.start = 0x3000;
        func.end = 0x3030;
        func.isRecompiled = true;

        std::vector<Instruction> instructions;
        instructions.push_back(makeAddiu(0x3000, 2, 0, 4));
        instructions.push_back(makeLui(0x3004, 1, 0x1000));
        instructions.push_back(makeOri(0x3008, 1, 1, 0xE020));
        instructions.push_back(makeSw(0x300C, 2, 1, 0));
        instructions.push_back(makeLui(0x3010, 1, 0x1000));
        instructions.push_back(makeOri(0x3014, 1, 1, 0xE010));
        instructions.push_back(makeSw(0x3018, 2, 1, 0));
        instructions.push_back(makeLui(0x301C, 1, 0x1000));
        instructions.push_back(makeOri(0x3020, 1, 1, 0xA030));
        instructions.push_back(makeSw(0x3024, 4, 1, 0));
        instructions.push_back(makeAddiu(0x3028, 5, 0, 0x0105));
        instructions.push_back(makeAddiu(0x302C, 1, 1, 0xFFD0));
        instructions.push_back(makeSw(0x3030, 5, 1, 0));

        CodeGenerator gen({}, {});
        std::string generated = gen.generateFunction(func, instructions, false);
        printGeneratedCode("known GIF DMA MMIO sequence emits native kick helper", generated);

        t.IsTrue(generated.find("uint32_t gifDmaKickValue_3024_2 = GPR_U32(ctx, 4);") != std::string::npos,
                 "dynamic GIF TADR source should be captured when the store is coalesced");
        t.IsTrue(generated.find("runtime->kickGifDmaChainFromMMIO(rdram, ctx, 0x4u, 0x4u, gifDmaKickValue_3024_2, 0x105u);") != std::string::npos,
                 "known GIF DMA MMIO stores should coalesce into the native kick helper");
        t.IsTrue(generated.find("runtime->Store32(rdram, ctx, 0x1000E020u") == std::string::npos,
                 "coalesced D_PCR store should not remain as an individual Store32");
        t.IsTrue(generated.find("runtime->Store32(rdram, ctx, 0x1000A000u") == std::string::npos,
                 "coalesced GIF CHCR store should not remain as an individual Store32");
    });

    tc.Run("GIF DMA kick coalesces when CHCR store is a return delay slot", [](TestCase &t) {
        Function func;
        func.name = "loadImage_like";
        func.start = 0x2E7C90;
        func.end = 0x2E7CC8;
        func.isRecompiled = true;

        std::vector<Instruction> instructions;
        instructions.push_back(makeBranch(0x2E7C90, 2));
        instructions.push_back(makeLui(0x2E7C94, 5, 0x1000));
        instructions.push_back(makeLui(0x2E7C98, 5, 0x1000));
        instructions.push_back(makeAddiu(0x2E7C9C, 6, 0, 4));
        instructions.push_back(makeOri(0x2E7CA0, 3, 5, 0xE020));
        instructions.push_back(makeSw(0x2E7CA4, 6, 3, 0));
        instructions.push_back(makeOri(0x2E7CA8, 3, 5, 0xE010));
        instructions.push_back(makeSw(0x2E7CAC, 6, 3, 0));
        instructions.push_back(makeOri(0x2E7CB0, 3, 5, 0xA030));
        instructions.push_back(makeSw(0x2E7CB4, 4, 3, 0));
        instructions.push_back(makeAddiu(0x2E7CB8, 4, 0, 0x0105));
        instructions.push_back(makeOri(0x2E7CBC, 3, 5, 0xA000));
        instructions.push_back(makeJr(0x2E7CC0, 31));
        instructions.push_back(makeSw(0x2E7CC4, 4, 3, 0));

        CodeGenerator gen({}, {});
        std::string generated = gen.generateFunction(func, instructions, false);
        printGeneratedCode("GIF DMA kick coalesces when CHCR store is a return delay slot", generated);

        t.IsTrue(generated.find("label_2e7c9c:") != std::string::npos,
                 "test should cover a branch target inside the GIF DMA setup");
        t.IsTrue(generated.find("uint32_t gifDmaKickValue_2e7cb4_2 = GPR_U32(ctx, 4);") != std::string::npos,
                 "TADR value should be captured before a0 is reused for CHCR");
        t.IsTrue(generated.find("ctx->in_delay_slot = true;") != std::string::npos,
                 "coalesced helper should still run as the return delay slot");
        t.IsTrue(generated.find("runtime->kickGifDmaChainFromMMIO(rdram, ctx, 0x4u, 0x4u, gifDmaKickValue_2e7cb4_2, 0x105u);") != std::string::npos,
                 "loadImage-like GIF DMA stores should coalesce into the native kick helper");
        t.IsTrue(generated.find("WRITE32(ADD32(GPR_U32(ctx, 3), 0), GPR_U32(ctx, 4));") == std::string::npos,
                 "coalesced delay-slot CHCR store should not remain as an individual WRITE32");
    });

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

    tc.Run("control-flow analysis keeps same-function JAL target internal and promotes only the return pc", [](TestCase &t) {
        Function func;
        func.name = "same_function_call";
        func.start = 0x1000;
        func.end = 0x101C;
        func.isRecompiled = true;
        func.isStub = false;

        std::vector<Instruction> instructions;
        instructions.push_back(makeJal(0x1000, 0x100C));
        instructions.push_back(makeNop(0x1004));
        instructions.push_back(makeNop(0x1008));
        instructions.push_back(makeNop(0x100C));
        instructions.push_back(makeNop(0x1010));
        instructions.push_back(makeJr(0x1014, 31));
        instructions.push_back(makeNop(0x1018));

        std::vector<Function> functions{func};
        std::vector<Section> sections = {
            {".text", 0x1000u, 0x100u, 0u, true, false, false, true, nullptr}
        };

        CodeGenerator gen({}, sections);
        CodeGenerator::AnalysisResult analysis =
            gen.collectInternalBranchTargets(func, instructions, &functions);

        t.IsTrue(analysis.entryPoints.contains(0x100Cu),
                 "same-function JAL target should stay as an internal label target");
        t.IsTrue(analysis.resumeEntryPoints.contains(0x1008u),
                 "same-function JAL should mark the fallthrough pc as resumable");
        t.IsFalse(analysis.externalEntryPoints.contains(0x100Cu),
                  "same-function JAL target should not become an external entry candidate");
    });

    tc.Run("control-flow analysis reports cross-function mid-function jumps as external entry candidates", [](TestCase &t) {
        Function caller;
        caller.name = "caller";
        caller.start = 0x4000;
        caller.end = 0x4008;
        caller.isRecompiled = true;
        caller.isStub = false;

        Function target;
        target.name = "target";
        target.start = 0x5000;
        target.end = 0x5010;
        target.isRecompiled = true;
        target.isStub = false;

        Instruction j{};
        j.address = 0x4000;
        j.opcode = OPCODE_J;
        j.target = (0x5004u >> 2) & 0x3FFFFFFu;
        j.hasDelaySlot = true;
        j.raw = (OPCODE_J << 26) | (j.target & 0x3FFFFFFu);

        std::vector<Instruction> instructions{j, makeNop(0x4004)};
        std::vector<Function> functions{caller, target};
        std::vector<Section> sections = {
            {".text", 0x4000u, 0x2000u, 0u, true, false, false, true, nullptr}
        };

        CodeGenerator gen({}, sections);
        CodeGenerator::AnalysisResult analysis =
            gen.collectInternalBranchTargets(caller, instructions, &functions);

        t.IsTrue(analysis.externalEntryPoints.contains(0x5004u),
                 "cross-function jump into the middle of a function should become an external entry candidate");
    });

    tc.Run("control-flow analysis promotes JALR fallthrough as a resumable entry", [](TestCase &t) {
        Function func;
        func.name = "jalr_resume";
        func.start = 0x1200;
        func.end = 0x1218;
        func.isRecompiled = true;
        func.isStub = false;

        std::vector<Instruction> instructions;
        instructions.push_back(makeNop(0x1200));
        instructions.push_back(makeJalr(0x1204, 2, 31));
        instructions.push_back(makeNop(0x1208));
        instructions.push_back(makeNop(0x120C));
        instructions.push_back(makeJr(0x1210, 31));
        instructions.push_back(makeNop(0x1214));

        std::vector<Function> functions{func};
        std::vector<Section> sections = {
            {".text", 0x1200u, 0x100u, 0u, true, false, false, true, nullptr}
        };

        CodeGenerator gen({}, sections);
        CodeGenerator::AnalysisResult analysis =
            gen.collectInternalBranchTargets(func, instructions, &functions);

        t.IsTrue(analysis.resumeEntryPoints.contains(0x120Cu),
                 "JALR should mark its return/fallthrough pc as resumable");
        t.IsTrue(analysis.entryPoints.contains(0x120Cu),
                 "JALR resume pc should also be emitted as an internal label");
    });

    tc.Run("unresolved JR marks internal labels as indirect fallback resume entries", [](TestCase &t) {
        Function func;
        func.name = "unresolved_jr_fallback";
        func.start = 0x3100;
        func.end = 0x3120;
        func.isRecompiled = true;
        func.isStub = false;

        std::vector<Instruction> instructions{
            makeNop(0x3100),
            makeJr(0x3104, 8),
            makeNop(0x3108),
            makeNop(0x310C),
            makeNop(0x3110),
        };

        CodeGenerator gen({}, {});
        CodeGenerator::AnalysisResult analysis = gen.collectInternalBranchTargets(func, instructions);

        t.IsTrue(analysis.indirectFallbackEntryPoints.contains(0x310Cu),
                 "unresolved JR should register internal labels as resumable entries for the owning function");
        t.IsTrue(analysis.entryPoints.contains(0x310Cu),
                 "unresolved JR fallback targets should still emit labels in the owner");
        t.IsFalse(analysis.jumpTableTargets.contains(0x3104u),
                  "unresolved JR should not pretend it has a resolved local jump table");
    });

    tc.Run("unresolved JALR marks internal labels as indirect fallback resume entries", [](TestCase &t) {
        Function func;
        func.name = "unresolved_jalr_fallback";
        func.start = 0x3200;
        func.end = 0x3220;
        func.isRecompiled = true;
        func.isStub = false;

        std::vector<Instruction> instructions{
            makeNop(0x3200),
            makeJalr(0x3204, 25, 31),
            makeNop(0x3208),
            makeNop(0x320C),
            makeNop(0x3210),
        };

        CodeGenerator gen({}, {});
        CodeGenerator::AnalysisResult analysis = gen.collectInternalBranchTargets(func, instructions);

        t.IsTrue(analysis.indirectFallbackEntryPoints.contains(0x320Cu),
                 "unresolved JALR should register internal labels as resumable entries for the owning function");
        t.IsTrue(analysis.entryPoints.contains(0x320Cu),
                 "unresolved JALR fallback targets should still emit labels in the owner");
        t.IsFalse(analysis.jumpTableTargets.contains(0x3204u),
                  "unresolved JALR should not pretend it has a resolved local jump table");
    });

    tc.Run("resume entry targets emit a top-level pc switch in the owner wrapper", [](TestCase &t) {
        Function func;
        func.name = "resume_owner";
        func.start = 0x6000;
        func.end = 0x6010;
        func.isRecompiled = true;
        func.isStub = false;

        std::vector<Instruction> instructions{
            makeNop(0x6000),
            makeNop(0x6004),
            makeNop(0x6008),
            makeNop(0x600C)
        };

        CodeGenerator gen({}, {});
        gen.setResumeEntryTargets({{0x6000u, {0x6008u}}});

        std::string generated = gen.generateFunction(func, instructions, false);
        printGeneratedCode("resume entry targets emit a top-level pc switch in the owner wrapper", generated);

        t.IsTrue(generated.find("switch (ctx->pc)") != std::string::npos,
                 "owner wrapper should dispatch resumable pcs with a switch");
        t.IsTrue(generated.find("case 0x6008u: goto label_6008;") != std::string::npos,
                 "resume pc should jump directly to the internal label");
        t.IsTrue(generated.find("label_6008:") != std::string::npos,
                 "resume pc should force label emission for that instruction");
    });

    tc.Run("resume entry targets register to the owner wrapper", [](TestCase &t) {
        Function func;
        func.name = "resume_owner";
        func.start = 0x7000;
        func.end = 0x7010;
        func.isRecompiled = true;
        func.isStub = false;

        CodeGenerator gen({}, {});
        gen.setRenamedFunctions({{0x7000u, "resume_owner_0x7000"}});
        gen.setResumeEntryTargets({{0x7000u, {0x7008u, 0x700Cu}}});

        std::string registration = gen.generateFunctionRegistration({func}, {});
        printGeneratedCode("resume entry targets register to the owner wrapper", registration);

        t.IsTrue(registration.find("g_ps2RecompiledFunctionTable[2] = resume_owner_0x7000; // 0x7008") != std::string::npos,
                 "resume entry pc should register to the owner wrapper");
        t.IsTrue(registration.find("g_ps2RecompiledFunctionTable[3] = resume_owner_0x7000; // 0x700c") != std::string::npos,
                 "multiple resume pcs should register to the same owner wrapper");
    });

    tc.Run("external mid-function entry can register to the owner wrapper", [](TestCase &t) {
        Function caller;
        caller.name = "caller";
        caller.start = 0x4000;
        caller.end = 0x4008;
        caller.isRecompiled = true;
        caller.isStub = false;

        Function owner;
        owner.name = "owner";
        owner.start = 0x5000;
        owner.end = 0x5010;
        owner.isRecompiled = true;
        owner.isStub = false;

        Instruction j{};
        j.address = 0x4000;
        j.opcode = OPCODE_J;
        j.target = (0x5004u >> 2) & 0x3FFFFFFu;
        j.hasDelaySlot = true;
        j.raw = (OPCODE_J << 26) | (j.target & 0x3FFFFFFu);

        std::vector<Instruction> callerInstructions{j, makeNop(0x4004)};
        std::vector<Function> functions{caller, owner};
        std::vector<Section> sections = {
            {".text", 0x4000u, 0x2000u, 0u, true, false, false, true, nullptr}
        };

        CodeGenerator gen({}, sections);
        CodeGenerator::AnalysisResult analysis =
            gen.collectInternalBranchTargets(caller, callerInstructions, &functions);

        t.IsTrue(analysis.externalEntryPoints.contains(0x5004u),
                 "cross-function jump should identify the mid-function target as externally reachable");

        gen.setRenamedFunctions({{0x5000u, "owner_0x5000"}});
        gen.setResumeEntryTargets({{0x5000u, {0x5004u}}});

        std::string registration = gen.generateFunctionRegistration({owner}, {});
        printGeneratedCode("external mid-function entry can register to the owner wrapper", registration);

        t.IsTrue(registration.find("g_ps2RecompiledFunctionTable[1] = owner_0x5000; // 0x5004") != std::string::npos,
                 "mid-function external entry should register back to the owner wrapper");
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
            t.IsTrue(mfc0Code.find("SET_GPR_S32(ctx, 5") != std::string::npos, "MFC0 should write to rt");
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

        tc.Run("scalar logical immediates emit low64 operations", [](TestCase &t) {
            CodeGenerator gen({}, {});

            Instruction andi{};
            andi.opcode = OPCODE_ANDI;
            andi.rs = 4;
            andi.rt = 5;
            andi.immediate = 0xABCD;

            std::string andiCode = gen.translateInstruction(andi);
            t.IsTrue(andiCode.find("SET_GPR_U64(ctx, 5, GPR_U64(ctx, 4) & (uint64_t)(uint16_t)43981);") != std::string::npos,
                     "ANDI should use low64 scalar emission");
            t.IsTrue(andiCode.find("SET_GPR_VEC") == std::string::npos,
                     "ANDI should not use vector emission");

            Instruction ori{};
            ori.opcode = OPCODE_ORI;
            ori.rs = 6;
            ori.rt = 7;
            ori.immediate = 0x1234;

            std::string oriCode = gen.translateInstruction(ori);
            t.IsTrue(oriCode.find("SET_GPR_U64(ctx, 7, GPR_U64(ctx, 6) | (uint64_t)(uint16_t)4660);") != std::string::npos,
                     "ORI should use low64 scalar emission");
            t.IsTrue(oriCode.find("SET_GPR_VEC") == std::string::npos,
                     "ORI should not use vector emission");

            Instruction xori{};
            xori.opcode = OPCODE_XORI;
            xori.rs = 8;
            xori.rt = 9;
            xori.immediate = 0x00FF;

            std::string xoriCode = gen.translateInstruction(xori);
            t.IsTrue(xoriCode.find("SET_GPR_U64(ctx, 9, GPR_U64(ctx, 8) ^ (uint64_t)(uint16_t)255);") != std::string::npos,
                     "XORI should use low64 scalar emission");
            t.IsTrue(xoriCode.find("SET_GPR_VEC") == std::string::npos,
                     "XORI should not use vector emission");
        });

        tc.Run("scalar logical register ops emit low64 operations", [](TestCase &t) {
            CodeGenerator gen({}, {});

            Instruction andInst{};
            andInst.opcode = OPCODE_SPECIAL;
            andInst.function = SPECIAL_AND;
            andInst.rs = 2;
            andInst.rt = 3;
            andInst.rd = 1;

            std::string andCode = gen.translateInstruction(andInst);
            t.IsTrue(andCode.find("SET_GPR_U64(ctx, 1, GPR_U64(ctx, 2) & GPR_U64(ctx, 3));") != std::string::npos,
                     "AND should use low64 scalar emission");

            Instruction orInst{};
            orInst.opcode = OPCODE_SPECIAL;
            orInst.function = SPECIAL_OR;
            orInst.rs = 4;
            orInst.rt = 5;
            orInst.rd = 6;

            std::string orCode = gen.translateInstruction(orInst);
            t.IsTrue(orCode.find("SET_GPR_U64(ctx, 6, GPR_U64(ctx, 4) | GPR_U64(ctx, 5));") != std::string::npos,
                     "OR should use low64 scalar emission");

            Instruction xorInst{};
            xorInst.opcode = OPCODE_SPECIAL;
            xorInst.function = SPECIAL_XOR;
            xorInst.rs = 7;
            xorInst.rt = 8;
            xorInst.rd = 9;

            std::string xorCode = gen.translateInstruction(xorInst);
            t.IsTrue(xorCode.find("SET_GPR_U64(ctx, 9, GPR_U64(ctx, 7) ^ GPR_U64(ctx, 8));") != std::string::npos,
                     "XOR should use low64 scalar emission");

            Instruction norInst{};
            norInst.opcode = OPCODE_SPECIAL;
            norInst.function = SPECIAL_NOR;
            norInst.rs = 10;
            norInst.rt = 11;
            norInst.rd = 12;

            std::string norCode = gen.translateInstruction(norInst);
            t.IsTrue(norCode.find("SET_GPR_U64(ctx, 12, ~(GPR_U64(ctx, 10) | GPR_U64(ctx, 11)));") != std::string::npos,
                     "NOR should use low64 scalar emission");
            t.IsTrue(norCode.find("SET_GPR_VEC") == std::string::npos,
                     "SPECIAL logical ops should not use vector emission");
        });

        tc.Run("SC requires matching LL reservation address", [](TestCase &t) {
            CodeGenerator gen({}, {});

            Instruction sc{};
            sc.opcode = OPCODE_SC;
            sc.rs = 9;
            sc.rt = 10;
            sc.simmediate = static_cast<uint32_t>(static_cast<int16_t>(4));

            std::string out = gen.translateInstruction(sc);
            t.IsTrue(out.find("ctx->llbit && ctx->lladdr == addr") != std::string::npos,
                     "SC must require both llbit and matching lladdr");
            t.IsTrue(out.find("ctx->llbit = 0; ctx->lladdr = 0;") != std::string::npos,
                     "SC must clear reservation state after attempting the store");
        });

        tc.Run("QFSRV translation uses runtime helper macro", [](TestCase &t) {
            CodeGenerator gen({}, {});

            Instruction qfsrv{};
            qfsrv.isMMI = true;
            qfsrv.opcode = OPCODE_MMI;
            qfsrv.function = MMI_MMI1;
            qfsrv.sa = MMI1_QFSRV;
            qfsrv.rd = 3;
            qfsrv.rs = 4;
            qfsrv.rt = 5;

            std::string out = gen.translateInstruction(qfsrv);
            t.IsTrue(out.find("PS2_QFSRV(GPR_VEC(ctx, 4), GPR_VEC(ctx, 5), ctx->sa & 0x7F)") != std::string::npos,
                     "QFSRV should map to PS2_QFSRV with rs/rt ordering");
        });

        tc.Run("PCPYLD and PEXEW use runtime helper macros", [](TestCase &t) {
            CodeGenerator gen({}, {});

            Instruction pcpyld{};
            pcpyld.isMMI = true;
            pcpyld.opcode = OPCODE_MMI;
            pcpyld.function = MMI_MMI2;
            pcpyld.sa = MMI2_PCPYLD;
            pcpyld.rd = 6;
            pcpyld.rs = 7;
            pcpyld.rt = 8;

            std::string pcpyldOut = gen.translateInstruction(pcpyld);
            t.IsTrue(pcpyldOut.find("PS2_PCPYLD(GPR_VEC(ctx, 7), GPR_VEC(ctx, 8))") != std::string::npos,
                     "PCPYLD should use PS2_PCPYLD helper");

            Instruction pexew{};
            pexew.isMMI = true;
            pexew.opcode = OPCODE_MMI;
            pexew.function = MMI_MMI2;
            pexew.sa = MMI2_PEXEW;
            pexew.rd = 9;
            pexew.rs = 10;

            std::string pexewOut = gen.translateInstruction(pexew);
            t.IsTrue(pexewOut.find("PS2_PEXEW(GPR_VEC(ctx, 10))") != std::string::npos,
                     "PEXEW should use PS2_PEXEW helper");
        });

        tc.Run("VU0 macro mappings cover all S1/S2 enums", [](TestCase &t) {
            CodeGenerator gen({}, {});

            for (const auto &op : vu0MacroOps(t))
            {
                bool isS1 = op.first;
                uint32_t value = op.second;
                Instruction inst = makeVu0MacroOp(isS1, value, 3, 3, 2);

                std::string out = gen.translateInstruction(inst);
                std::ostringstream msg;
                msg << (isS1 ? "VU0 S1 0x" : "VU0 S2 0x") << std::hex << value << " should be mapped";
                const std::string unhandled = isS1 ? "Unhandled VU0 Special1" : "Unhandled VU0 Special2";
                // The marker check is a negative, and an empty emission carries no marker either.
                t.IsTrue(!out.empty(), msg.str());
                t.IsTrue(out.find(unhandled) == std::string::npos, msg.str());
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

        tc.Run("vuVfDestMask yields an empty mask for a vf0 destination", [](TestCase &t) {
            std::string failure;
            for (uint32_t mask = 0; mask <= 0xF; ++mask)
            {
                if (codegen::vuVfDestMask(0, static_cast<uint8_t>(mask)) != 0)
                {
                    std::ostringstream ss;
                    ss << "vuVfDestMask(0, 0x" << std::hex << mask
                       << ") must yield an empty write mask";
                    failure = ss.str();
                    break;
                }
            }
            t.IsTrue(failure.empty(), failure);
        });

        tc.Run("vuVfDestMask passes the field mask through for every non-vf0 destination", [](TestCase &t) {
            std::string failure;
            for (uint32_t reg = 1; reg <= 31 && failure.empty(); ++reg)
            {
                for (uint32_t mask = 0; mask <= 0xF; ++mask)
                {
                    if (codegen::vuVfDestMask(reg, static_cast<uint8_t>(mask)) != mask)
                    {
                        std::ostringstream ss;
                        ss << "vuVfDestMask(" << std::dec << reg << ", 0x" << std::hex << mask
                           << ") must return the field mask unchanged";
                        failure = ss.str();
                        break;
                    }
                }
            }
            t.IsTrue(failure.empty(), failure);
        });

        // The anchor the two sweeps below stand on. Both derive their expectations from a reference
        // emission built at the same opcode and field mask as the emission under test, so a discard
        // keyed on the field mask - or an unconditional one - empties reference and subject
        // together and leaves every derived comparison true over an empty collection. What is
        // asserted here is declared, not derived: the site emits something, it carries the number
        // of write masks its opcode carries, and it writes the VF register its opcode names.
        //
        // The fixture is built exactly as the sweeps' reference is - same operand triple, same
        // field range, same address formula - so this covers the very string they derive from. If
        // a sweep's reference fixture changes, change this one with it.
        tc.Run("every VU0 macro site emits the write masks and VF destination its opcode declares", [](TestCase &t) {
            CodeGenerator gen({}, {});
            const uint32_t refOperands[3] = {9, 11, 7}; // sa, rd, rt
            std::string failure;

            for (const auto &op : vu0MacroOps(t))
            {
                if (!failure.empty())
                {
                    break;
                }
                const bool isS1 = op.first;
                const uint32_t value = op.second;
                const Vu0MacroShape shape = vu0MacroShape(isS1, value);

                for (uint32_t fieldValue = 0; fieldValue <= 0xF && failure.empty(); ++fieldValue)
                {
                    const uint8_t field = static_cast<uint8_t>(fieldValue);
                    const std::string out = gen.translateInstruction(
                        makeVu0MacroOp(isS1, value, refOperands[0], refOperands[1], refOperands[2], field, 0x00100000u + 4u * fieldValue));

                    std::ostringstream label;
                    label << (isS1 ? "S1 0x" : "S2 0x") << std::hex << value
                          << " field 0x" << static_cast<uint32_t>(field);

                    if (out.empty())
                    {
                        failure = label.str() + ": the site must emit a statement";
                        break;
                    }

                    int masks = 0;
                    for (const std::string &literal : collectMaskLiterals(out))
                    {
                        if (isVuFieldMaskLiteral(literal))
                        {
                            ++masks;
                        }
                    }
                    if (masks != shape.maskLiterals)
                    {
                        failure = label.str() + ": the site must emit the write masks its opcode carries";
                        break;
                    }

                    std::vector<uint32_t> expectedDest;
                    if (shape.vfDestSlot >= 0)
                    {
                        expectedDest.push_back(refOperands[shape.vfDestSlot]);
                    }
                    if (collectVfAssignmentIndices(out) != expectedDest)
                    {
                        failure = label.str() +
                                  ": the site must write the VF destination its opcode names, and no other";
                        break;
                    }
                }
            }
            t.IsTrue(failure.empty(), failure);
        });

        tc.Run("every VU0 macro write site emits its own field mask, and a vf0 destination changes only that mask", [](TestCase &t) {
            CodeGenerator gen({}, {});
            const std::string zeroMask = "_mm_set_epi32(0, 0, 0, 0)";
            const uint32_t refOperands[3] = {9, 11, 7}; // sa, rd, rt

            for (const auto &op : vu0MacroOps(t))
            {
                bool isS1 = op.first;
                uint32_t value = op.second;

                for (uint32_t fieldValue = 0; fieldValue <= 0xF; ++fieldValue)
                {
                    const uint8_t field = static_cast<uint8_t>(fieldValue);
                    const std::string liveMask = vuFieldMaskLiteral(field);

                    Instruction ref = makeVu0MacroOp(isS1, value, refOperands[0], refOperands[1], refOperands[2], field, 0x00100000u + 4u * fieldValue);
                    std::string outRef = gen.translateInstruction(ref);
                    const Vu0MacroShape shape = vu0MacroShape(isS1, value);

                    // Absolute: the pass comparisons below are relative to outRef, so a mask that
                    // is dead in both emissions is invisible to them.
                    for (const std::string &literal : collectMaskLiterals(outRef))
                    {
                        if (!isVuFieldMaskLiteral(literal))
                        {
                            continue;
                        }
                        std::ostringstream anchorMsg;
                        anchorMsg << (isS1 ? "S1 0x" : "S2 0x") << std::hex << value
                                  << " field 0x" << static_cast<uint32_t>(field)
                                  << ": a non-vf0 destination must emit this instruction's own field mask";
                        t.IsTrue(literal == liveMask, anchorMsg.str());
                    }

                    for (int pass = 0; pass < 3; ++pass)
                    {
                        uint32_t operands[3] = {refOperands[0], refOperands[1], refOperands[2]};
                        operands[pass] = 0;
                        const uint32_t zeroedDest = refOperands[pass];

                        Instruction mutated = makeVu0MacroOp(isS1, value, operands[0], operands[1], operands[2], field, 0x00100000u + 4u * fieldValue);
                        std::string out0 = gen.translateInstruction(mutated);

                        std::ostringstream destAssign;
                        destAssign << "ctx->vu0_vf[" << zeroedDest << "] = ";

                        std::ostringstream label;
                        label << (isS1 ? "S1 0x" : "S2 0x") << std::hex << value
                              << " field 0x" << static_cast<uint32_t>(field);

                        // Gated on the opcode's declared destination slot rather than on whether
                        // the reference emission happens to contain the assignment: a discard that
                        // empties the reference would otherwise steer every pass into the weak
                        // branch below, where an empty collection compares equal to an empty one.
                        if (shape.vfDestSlot == pass)
                        {
                            std::ostringstream oldDestStream;
                            oldDestStream << "ctx->vu0_vf[" << zeroedDest << "]";
                            const std::string oldDest = oldDestStream.str();
                            const std::string newDest = "ctx->vu0_vf[0]";

                            std::string expect = outRef;
                            size_t pos = 0;
                            while ((pos = expect.find(oldDest, pos)) != std::string::npos)
                            {
                                expect.replace(pos, oldDest.size(), newDest);
                                pos += newDest.size();
                            }
                            pos = 0;
                            while ((pos = expect.find(liveMask, pos)) != std::string::npos)
                            {
                                expect.replace(pos, liveMask.size(), zeroMask);
                                pos += zeroMask.size();
                            }

                            std::ostringstream deadMsg;
                            deadMsg << label.str() << ": a vf0 destination must emit an empty write mask";
                            t.IsTrue(out0.find(zeroMask) != std::string::npos, deadMsg.str());

                            std::ostringstream msg;
                            msg << label.str() << ": a vf0 destination must change only the destination index and the write mask";
                            t.IsTrue(out0 == expect, msg.str());
                        }
                        else
                        {
                            std::ostringstream msg;
                            msg << label.str() << ": zeroing a non-destination operand must not change any write mask";
                            t.IsTrue(collectMaskLiterals(out0) == collectMaskLiterals(outRef), msg.str());
                        }
                    }
                }
            }
        });

        tc.Run("VU0 macro write masks depend only on the destination index and the field mask", [](TestCase &t) {
            CodeGenerator gen({}, {});
            const std::string zeroMask = "_mm_set_epi32(0, 0, 0, 0)";
            const uint32_t refOperands[3] = {9, 11, 7}; // sa, rd, rt
            std::string failure;

            for (const auto &op : vu0MacroOps(t))
            {
                if (!failure.empty())
                {
                    break;
                }
                const bool isS1 = op.first;
                const uint32_t value = op.second;

                for (uint32_t fieldValue = 0; fieldValue <= 0xF && failure.empty(); ++fieldValue)
                {
                    const uint8_t field = static_cast<uint8_t>(fieldValue);
                    const std::string liveMask = vuFieldMaskLiteral(field);
                    const std::string outRef = gen.translateInstruction(
                        makeVu0MacroOp(isS1, value, refOperands[0], refOperands[1], refOperands[2], field, 0x00100000u + 4u * fieldValue));

                    // The accumulator family and the two store forms hold no VF destination in any
                    // swept slot, so the destination loop below skips them outright and their masks
                    // would be pinned only at the operand triples the other sweep drives. They are
                    // deliberately unguarded, which makes the property one-sided: the mask must stay
                    // this instruction's own field mask at every value of every slot. That domain
                    // contains the value the decoder gives the shift-amount slot, which is the one
                    // slot a fixture here sets after the decode.
                    // Declared, not read back off outRef: a discard that empties the reference would
                    // otherwise make every site look destination-free and send all 96 probes below
                    // through a comparison of two empty collections.
                    const Vu0MacroShape shape = vu0MacroShape(isS1, value);
                    const bool anyDest = shape.vfDestSlot >= 0;

                    for (int slot = 0; !anyDest && slot < 3 && failure.empty(); ++slot)
                    {
                        for (uint32_t v = 0; v <= 31 && failure.empty(); ++v)
                        {
                            uint32_t operands[3] = {refOperands[0], refOperands[1], refOperands[2]};
                            operands[slot] = v;

                            const std::string out = gen.translateInstruction(
                                makeVu0MacroOp(isS1, value, operands[0], operands[1], operands[2], field, 0x00100000u + 4u * fieldValue));

                            std::ostringstream label;
                            label << (isS1 ? "S1 0x" : "S2 0x") << std::hex << value
                                  << " field 0x" << static_cast<uint32_t>(field)
                                  << " operand " << std::dec << slot << " = " << v;

                            // Held against the reference emission rather than only walked: an
                            // operand value that empties the emission, or drops a mask out of it,
                            // leaves the walk below nothing to look at, and a walk over nothing
                            // reports nothing.
                            if (collectMaskLiterals(out) != collectMaskLiterals(outRef))
                            {
                                failure = label.str() +
                                          ": a site with no vf destination must emit the same write masks at every value of every operand";
                                break;
                            }

                            for (const std::string &literal : collectMaskLiterals(out))
                            {
                                if (isVuFieldMaskLiteral(literal) && literal != liveMask)
                                {
                                    failure = label.str() +
                                              ": a site with no vf destination must emit this instruction's own field mask at every value of every operand";
                                    break;
                                }
                            }
                        }
                    }

                    for (int destSlot = 0; destSlot < 3 && failure.empty(); ++destSlot)
                    {
                        if (destSlot != shape.vfDestSlot)
                        {
                            continue;
                        }

                        for (int other = 0; other < 3 && failure.empty(); ++other)
                        {
                            if (other == destSlot)
                            {
                                continue;
                            }
                            for (uint32_t v = 0; v <= 31 && failure.empty(); ++v)
                            {
                                uint32_t live[3] = {refOperands[0], refOperands[1], refOperands[2]};
                                live[other] = v;
                                uint32_t zero[3] = {live[0], live[1], live[2]};
                                zero[destSlot] = 0;

                                const std::string outLive = gen.translateInstruction(
                                    makeVu0MacroOp(isS1, value, live[0], live[1], live[2], field, 0x00100000u + 4u * fieldValue));
                                const std::string outZero = gen.translateInstruction(
                                    makeVu0MacroOp(isS1, value, zero[0], zero[1], zero[2], field, 0x00100000u + 4u * fieldValue));

                                std::ostringstream label;
                                label << (isS1 ? "S1 0x" : "S2 0x") << std::hex << value
                                      << " field 0x" << static_cast<uint32_t>(field)
                                      << " operand " << std::dec << other << " = " << v;

                                if (collectMaskLiterals(outLive) != collectMaskLiterals(outRef))
                                {
                                    failure = label.str() +
                                              ": a non-vf0 destination must emit the same write masks at every value of its other operands";
                                    break;
                                }

                                for (const std::string &literal : collectMaskLiterals(outLive))
                                {
                                    if (isVuFieldMaskLiteral(literal) && literal != liveMask)
                                    {
                                        failure = label.str() +
                                                  ": a non-vf0 destination must emit this instruction's own field mask at every value of its other operands";
                                        break;
                                    }
                                }
                                if (!failure.empty())
                                {
                                    break;
                                }
                                // The vf0 emission carries a different mask value, so what is held
                                // against the reference here is that the masks are still there.
                                if (collectMaskLiterals(outZero).size() != collectMaskLiterals(outRef).size())
                                {
                                    failure = label.str() +
                                              ": a vf0 destination must still emit its write masks at every value of its other operands";
                                    break;
                                }

                                for (const std::string &literal : collectMaskLiterals(outZero))
                                {
                                    if (isVuFieldMaskLiteral(literal) && literal != zeroMask)
                                    {
                                        failure = label.str() +
                                                  ": a vf0 destination must emit an empty write mask at every value of its other operands";
                                        break;
                                    }
                                }
                            }
                        }
                    }
                }
            }
            t.IsTrue(failure.empty(), failure);
        });

        tc.Run("VU0 non-zero destination keeps its write mask", [](TestCase &t) {
            const Instruction inst = makeVu0MacroOp(true, VU0_S1_VADD, 7, 11, 5, 0xF);

            CodeGenerator gen({}, {});
            std::string out = gen.translateInstruction(inst);

            t.IsTrue(out.find("ctx->vu0_vf[7] = ") != std::string::npos, "non-zero destination should still be written");
            t.IsTrue(out.find("_mm_set_epi32(-1, -1, -1, -1)") != std::string::npos, "non-zero destination should keep a live write mask");
        });

        tc.Run("QMTC2 to vf0 emits no vf0 write", [](TestCase &t) {
            CodeGenerator gen({}, {});
            std::string failure;
            for (uint32_t lowBits : qmtc2LowBits)
            {
                for (int disasm = 0; disasm < 2 && failure.empty(); ++disasm)
                {
                    for (uint32_t rt = 0; rt <= 31 && failure.empty(); ++rt)
                    {
                        const Instruction inst = makeQmtc2(0x00100000u + 4u * rt, 0, rt, lowBits, disasm != 0);
                        const std::string out = gen.translateInstruction(inst);

                        if (out.find("ctx->vu0_vf[") != std::string::npos)
                        {
                            std::ostringstream ss;
                            ss << "QMTC2 from $" << std::dec << rt << " to $vf0 (encoding low bits 0x"
                               << std::hex << lowBits << ") must not write any vu0_vf entry";
                            failure = ss.str();
                        }
                        // The discard's own text, not only the absence of a write: EVIDENCE.md's
                        // corpus recipe greps a generated tree for exactly this comment, and an
                        // emission that stopped carrying it would leave that grep silently empty.
                        else if (out != "// QMTC2 to $vf0 discarded")
                        {
                            std::ostringstream ss;
                            ss << "QMTC2 from $" << std::dec << rt << " to $vf0 (encoding low bits 0x"
                               << std::hex << lowBits << ") must emit the discard comment, got \""
                               << out << "\"";
                            failure = ss.str();
                        }
                    }
                }
                if (!failure.empty())
                {
                    break;
                }
            }
            t.IsTrue(failure.empty(), failure);
        });

        tc.Run("QMTC2 to a non-zero vf still writes", [](TestCase &t) {
            CodeGenerator gen({}, {});
            std::string failure;
            for (uint32_t lowBits : qmtc2LowBits)
            {
                for (int disasm = 0; disasm < 2 && failure.empty(); ++disasm)
                {
                    for (uint32_t rd = 1; rd <= 31 && failure.empty(); ++rd)
                    {
                        for (uint32_t rt = 0; rt <= 31 && failure.empty(); ++rt)
                        {
                            const Instruction inst = makeQmtc2(0x00100000u + 4u * rd, rd, rt, lowBits, disasm != 0);
                            const std::string out = gen.translateInstruction(inst);

                            std::ostringstream expect;
                            expect << "ctx->vu0_vf[" << rd << "] = _mm_castsi128_ps(GPR_VEC(ctx, " << rt << "));";
                            if (out.find(expect.str()) == std::string::npos)
                            {
                                std::ostringstream ss;
                                ss << "QMTC2 from $" << std::dec << rt << " to $vf" << rd
                                   << " (encoding low bits 0x" << std::hex << lowBits << ") should still write that source";
                                failure = ss.str();
                            }
                        }
                    }
                }
                if (!failure.empty())
                {
                    break;
                }
            }
            t.IsTrue(failure.empty(), failure);
        });

        // The two members below take no part in what these sites emit, which is exactly why a guard
        // could read one unnoticed: a decoder-built fixture leaves both cleared. What is pinned here
        // is that the translated statement is unchanged across every state a recompile can reach.
        // LQC2 is deliberately not covered this way - its emission does read them, and it is driven
        // over the memory-path table instead.
        tc.Run("VU0 macro emission does not depend on the post-decode MMIO fields", [](TestCase &t) {
            CodeGenerator gen({}, {});
            const uint32_t refOperands[3] = {9, 11, 7}; // sa, rd, rt
            std::string failure;

            for (const auto &op : vu0MacroOps(t))
            {
                if (!failure.empty())
                {
                    break;
                }
                const bool isS1 = op.first;
                const uint32_t value = op.second;

                for (uint32_t fieldValue = 0; fieldValue <= 0xF && failure.empty(); ++fieldValue)
                {
                    const uint8_t field = static_cast<uint8_t>(fieldValue);

                    // pass 3 leaves every operand non-zero; passes 0..2 put vf0 in each slot in
                    // turn, so both halves of the guarded property are run over every state.
                    for (int pass = 0; pass < 4 && failure.empty(); ++pass)
                    {
                        uint32_t operands[3] = {refOperands[0], refOperands[1], refOperands[2]};
                        if (pass < 3)
                        {
                            operands[pass] = 0;
                        }

                        const Instruction base = makeVu0MacroOp(
                            isS1, value, operands[0], operands[1], operands[2], field,
                            0x00100000u + 4u * fieldValue);
                        const std::string expected = gen.translateInstruction(base);
                        // Invariance across the MMIO states is a comparison of two emissions, so it
                        // holds vacuously if the site emits nothing at all.
                        if (expected.empty())
                        {
                            std::ostringstream ss;
                            ss << (isS1 ? "S1 0x" : "S2 0x") << std::hex << value
                               << " field 0x" << static_cast<uint32_t>(field)
                               << ", operand slot " << std::dec << pass
                               << ": the site must emit a statement";
                            failure = ss.str();
                            break;
                        }

                        for (const PostDecodeMmioState &state : postDecodeMmioStates)
                        {
                            Instruction inst = base;
                            inst.isMmio = state.isMmio;         // set post-decode, as the recompiler does
                            inst.mmioAddress = state.mmioAddress;

                            if (gen.translateInstruction(inst) != expected)
                            {
                                std::ostringstream ss;
                                ss << (isS1 ? "S1 0x" : "S2 0x") << std::hex << value
                                   << " field 0x" << static_cast<uint32_t>(field)
                                   << ", operand slot " << std::dec << pass
                                   << ": the emitted statement must not change when the post-decode"
                                      " MMIO fields are set";
                                failure = ss.str();
                                break;
                            }
                        }
                    }
                }
            }
            t.IsTrue(failure.empty(), failure);
        });

        tc.Run("QMTC2 emission does not depend on the post-decode MMIO fields", [](TestCase &t) {
            CodeGenerator gen({}, {});
            std::string failure;
            for (uint32_t lowBits : qmtc2LowBits)
            {
                if (!failure.empty())
                {
                    break;
                }
                for (int disasm = 0; disasm < 2 && failure.empty(); ++disasm)
                {
                    for (uint32_t rd = 0; rd <= 31 && failure.empty(); ++rd)
                    {
                        for (uint32_t rt = 0; rt <= 31 && failure.empty(); ++rt)
                        {
                            const Instruction base =
                                makeQmtc2(0x00100000u + 4u * rd, rd, rt, lowBits, disasm != 0);
                            const std::string expected = gen.translateInstruction(base);
                            // Same reason as the VU0 macro case: two empty emissions are invariant.
                            if (expected.empty())
                            {
                                std::ostringstream ss;
                                ss << "QMTC2 from $" << std::dec << rt << " to $vf" << rd
                                   << " (encoding low bits 0x" << std::hex << lowBits
                                   << "): the emission must not be empty";
                                failure = ss.str();
                                break;
                            }

                            for (const PostDecodeMmioState &state : postDecodeMmioStates)
                            {
                                Instruction inst = base;
                                inst.isMmio = state.isMmio;         // set post-decode, as the recompiler does
                                inst.mmioAddress = state.mmioAddress;

                                if (gen.translateInstruction(inst) != expected)
                                {
                                    std::ostringstream ss;
                                    ss << "QMTC2 from $" << std::dec << rt << " to $vf" << rd
                                       << " (encoding low bits 0x" << std::hex << lowBits
                                       << "): the emitted statement must not change when the"
                                          " post-decode MMIO fields are set";
                                    failure = ss.str();
                                    break;
                                }
                            }
                        }
                    }
                }
            }
            t.IsTrue(failure.empty(), failure);
        });

        tc.Run("LQC2 to vf0 emits no vf0 write", [](TestCase &t) {
            CodeGenerator gen({}, {});
            std::string failure;
            for (const Lqc2MemoryPath &path : lqc2MemoryPaths)
            {
                if (!failure.empty())
                {
                    break;
                }
                for (int disasm = 0; disasm < 2 && failure.empty(); ++disasm)
                {
                    for (uint32_t rs = 0; rs <= 31 && failure.empty(); ++rs)
                    {
                        for (uint16_t offset : lqc2Immediates)
                        {
                            Instruction inst = makeLqc2(0x00100000u + 4u * rs, /*rt*/ 0, rs, offset, disasm != 0);
                            inst.isMmio = path.isMmio;          // set post-decode, as the recompiler does
                            inst.mmioAddress = path.mmioAddress;

                            MemoryAccessHint hint{};
                            hint.hasAddress = path.hasHint;
                            hint.address = path.hintAddress;

                            const std::string out = gen.translateInstruction(inst, hint);

                            if (out.find("ctx->vu0_vf[") != std::string::npos)
                            {
                                std::ostringstream ss;
                                ss << "LQC2 to vf0 based on $" << rs << " must not write any vu0_vf entry";
                                failure = ss.str();
                                break;
                            }
                        }
                    }
                }
            }
            t.IsTrue(failure.empty(), failure);
        });

        // The load takes three shapes - the runtime loader for an MMIO or special-region address,
        // the fast read for an ordinary resolved one, and a read through the operand-built address
        // when neither the flag nor the hint is set - and the discard has to keep all three. Driven
        // over the same memory-path table as the two siblings, and held against the whole emitted
        // statement rather than a token: "READ128(" is a substring of "FAST_READ128(", so no single
        // substring can tell the three apart, and a substring check leaves the address and the
        // discard's own spelling unpinned. EVIDENCE.md's corpus recipe greps for that spelling.
        tc.Run("LQC2 to vf0 still performs the load", [](TestCase &t) {
            CodeGenerator gen({}, {});
            std::string failure;
            for (const Lqc2MemoryPath &path : lqc2MemoryPaths)
            {
                if (!failure.empty())
                {
                    break;
                }
                for (int disasm = 0; disasm < 2 && failure.empty(); ++disasm)
                {
                    for (uint32_t rs = 0; rs <= 31 && failure.empty(); ++rs)
                    {
                        for (uint16_t offset : lqc2Immediates)
                        {
                            Instruction inst = makeLqc2(0x00100000u + 4u * rs, /*rt*/ 0, rs, offset, disasm != 0);
                            inst.isMmio = path.isMmio;          // set post-decode, as the recompiler does
                            inst.mmioAddress = path.mmioAddress;

                            MemoryAccessHint hint{};
                            hint.hasAddress = path.hasHint;
                            hint.address = path.hintAddress;

                            const std::string out = gen.translateInstruction(inst, hint);
                            const std::string expect = "(void)" + lqc2ExpectedLoad(path, inst) + ";";

                            if (out != expect)
                            {
                                std::ostringstream ss;
                                ss << "LQC2 to vf0 based on $" << rs
                                   << " must discard the loaded value and keep the load: expected \""
                                   << expect << "\", got \"" << out << "\"";
                                failure = ss.str();
                                break;
                            }
                        }
                    }
                }
            }
            t.IsTrue(failure.empty(), failure);
        });

        tc.Run("LQC2 to a non-zero vf still writes", [](TestCase &t) {
            CodeGenerator gen({}, {});
            std::string failure;
            for (const Lqc2MemoryPath &path : lqc2MemoryPaths)
            {
                if (!failure.empty())
                {
                    break;
                }
                for (int disasm = 0; disasm < 2 && failure.empty(); ++disasm)
                {
                    for (uint32_t rt = 1; rt <= 31 && failure.empty(); ++rt)
                    {
                        for (uint32_t rs = 0; rs <= 31 && failure.empty(); ++rs)
                        {
                            for (uint16_t offset : lqc2Immediates)
                            {
                                Instruction inst = makeLqc2(0x00100000u + 4u * rs, rt, rs, offset, disasm != 0);
                                inst.isMmio = path.isMmio;          // set post-decode, as the recompiler does
                                inst.mmioAddress = path.mmioAddress;

                                MemoryAccessHint hint{};
                                hint.hasAddress = path.hasHint;
                                hint.address = path.hintAddress;

                                const std::string out = gen.translateInstruction(inst, hint);

                                // The whole statement, on every memory path. The address used to be
                                // checked only on the default path, so twelve of the thirteen rows
                                // asserted the write and nothing about what it loaded or from where.
                                std::ostringstream expect;
                                expect << "ctx->vu0_vf[" << rt << "] = _mm_castsi128_ps("
                                       << lqc2ExpectedLoad(path, inst) << ");";

                                if (out != expect.str())
                                {
                                    std::ostringstream ss;
                                    ss << "LQC2 to $vf" << rt << " must write the loaded value: expected \""
                                       << expect.str() << "\", got \"" << out << "\"";
                                    failure = ss.str();
                                    break;
                                }
                            }
                        }
                    }
                }
            }
            t.IsTrue(failure.empty(), failure);
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
            // runtime->dispatchGuestBranch(..., DirectCall, "JAL")

            t.IsTrue(generated.find("SET_GPR_U32(ctx, 31, 0xA008u);") != std::string::npos, "JAL should set RA");
            t.IsTrue(generated.find("runtime->dispatchGuestBranch(rdram, ctx, 0xB000u") != std::string::npos,
                     "JAL should dispatch through the runtime branch helper");
            t.IsTrue(generated.find("PS2Runtime::GuestBranchKind::DirectCall") != std::string::npos,
                     "JAL should identify itself as a direct call");
            t.IsTrue(generated.find("0xA000u, 0xA008u") != std::string::npos,
                     "JAL should pass call-site and fallthrough PCs to the runtime helper");
        });

        tc.Run("trailing JAL without decoded delay slot still emits call flow", [](TestCase &t) {
            Function func;
            func.name = "jal_truncated";
            func.start = 0xA100;
            func.end = 0xA108;
            func.isRecompiled = true;
            func.isStub = false;

            Symbol targetSym;
            targetSym.name = "some_func";
            targetSym.address = 0xB000;
            targetSym.isFunction = true;

            Instruction jal = makeJal(0xA100, 0xB000);

            CodeGenerator gen({targetSym}, {});
            std::string generated = gen.generateFunction(func, {jal}, false);
            printGeneratedCode("trailing JAL without decoded delay slot still emits call flow", generated);

            t.IsTrue(generated.find("SET_GPR_U32(ctx, 31, 0xA108u);") != std::string::npos,
                     "truncated trailing JAL should still set RA");
            t.IsTrue(generated.find("runtime->dispatchGuestBranch(rdram, ctx, 0xB000u") != std::string::npos,
                     "truncated trailing JAL should still emit the call dispatch");
            t.IsTrue(generated.find("0xA100u, 0xA108u") != std::string::npos,
                     "truncated trailing JAL should still pass the fallthrough to runtime dispatch");
            t.IsTrue(generated.find("// JAL 0xB000 - Handled by branch logic") == std::string::npos,
                     "truncated trailing JAL must not degrade to comment-only output");
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

        tc.Run("backward internal JAL stays inline and does not return to dispatcher", [](TestCase &t) {
            Function func;
            func.name = "jal_internal_backward";
            func.start = 0xC100;
            func.end = 0xC120;
            func.isRecompiled = true;
            func.isStub = false;

            Instruction loopHead = makeNop(0xC100);
            Instruction backwardJal = makeJal(0xC108, 0xC100);
            Instruction delay = makeNop(0xC10C);

            CodeGenerator gen({}, {});
            std::string generated = gen.generateFunction(func, {loopHead, backwardJal, delay}, false);
            printGeneratedCode("backward internal JAL stays inline and does not return to dispatcher", generated);

            t.IsTrue(generated.find("SET_GPR_U32(ctx, 31, 0xC110u);") != std::string::npos,
                     "backward internal JAL should still set RA");
            t.IsTrue(generated.find("goto label_c100;") != std::string::npos,
                     "backward internal JAL should still re-enter the internal target directly");
            t.IsTrue(generated.find("if (runtime->shouldPreemptGuestExecution())") == std::string::npos,
                     "backward internal JAL should not expose a mid-call dispatcher return");
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

            t.IsTrue(generated.find("const uint32_t jumpTarget = GPR_U32(ctx, 4);") != std::string::npos, "JALR should read target from RS");
            t.IsTrue(generated.find("SET_GPR_U32(ctx, 31, 0xD008u);") != std::string::npos, "JALR should set link register");
            t.IsTrue(generated.find("runtime->dispatchGuestBranch(rdram, ctx, jumpTarget") != std::string::npos, "JALR should dispatch through runtime branch helper");
            t.IsTrue(generated.find("PS2Runtime::GuestBranchKind::IndirectCall") != std::string::npos,
                     "JALR should identify itself as an indirect call");
            t.IsTrue(generated.find("0xD000u, 0xD008u") != std::string::npos,
                     "JALR should pass call-site and fallthrough PCs to the runtime helper");
            t.IsFalse(generated.find("jumpTarget == 0u") != std::string::npos,
                      "JALR must not silently turn target 0 into a successful call return");
        }); 

        tc.Run("backward BEQ returns to the dispatcher through a resumable loop head", [](TestCase &t) {
            Function func;
            func.name = "backward_branch";
            func.start = 0x1100;
            func.end = 0x1120;
            func.isRecompiled = true;
            func.isStub = false;

            // 0x1100: nop
            // 0x1104: nop (loop head)
            // 0x1108: beq $1,$1, target 0x1104 (offset = -2 words)
            // 0x110c: nop (delay)
            std::vector<Instruction> instructions;
            instructions.push_back(makeNop(0x1100));
            instructions.push_back(makeNop(0x1104));

            Instruction br = makeBranch(0x1108, 0);
            br.simmediate = static_cast<uint32_t>(static_cast<int16_t>(-2));
            instructions.push_back(br);

            instructions.push_back(makeNop(0x110c));
            instructions.push_back(makeNop(0x1110));

            CodeGenerator gen({}, {});
            CodeGenerator::AnalysisResult analysis = gen.collectInternalBranchTargets(func, instructions);
            t.IsTrue(analysis.resumeEntryPoints.contains(0x1104u),
                     "mid-function backward loop head should be resumable so preemption can return to the dispatcher");

            std::string generated = gen.generateFunction(func, instructions, false);
            printGeneratedCode("backward BEQ returns to the dispatcher through a resumable loop head", generated);

            t.IsTrue(generated.find("label_1104:") != std::string::npos, "target should emit a label");
            t.IsTrue(generated.find("switch (ctx->pc)") != std::string::npos,
                     "resumable backward loop heads should emit a wrapper resume switch");
            t.IsTrue(generated.find("case 0x1104u: goto label_1104;") != std::string::npos,
                     "mid-function backward loop head should be re-enterable after returning to the dispatcher");
            t.IsTrue(generated.find("ctx->pc = 0x1104u;") != std::string::npos,
                     "backward internal branch should preserve the loop target in ctx->pc");
            t.IsTrue(generated.find("if (runtime->shouldPreemptGuestExecution()) {") != std::string::npos,
                     "backward internal branch should consult the runtime preemption policy before re-entering the loop");
            t.IsTrue(generated.find("return;") != std::string::npos,
                     "backward internal branch should return to the dispatcher when the runtime preemption policy requests it");
            t.IsTrue(generated.find("runtime->cooperativeGuestYield();") == std::string::npos,
                     "backward internal branch should no longer emit an unconditional cooperative-yield call");
            t.IsTrue(generated.find("goto label_1104;") != std::string::npos,
                     "backward internal branch should still re-enter the in-function label when it keeps the current slice");
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

        tc.Run("JR $31 returns through dynamic target without broad local switch", [](TestCase &t) {
            Function func;
            func.name = "jr_ra_return";
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
            printGeneratedCode("JR $31 returns through dynamic target without broad local switch", generated);

            t.IsTrue(generated.find("const uint32_t jumpTarget = GPR_U32(ctx, 31);") != std::string::npos,
                     "JR $31 should still read the dynamic return target");
            t.IsFalse(generated.find("switch (jumpTarget)") != std::string::npos,
                      "JR $31 should not emit a broad local switch over internal labels");
            t.IsTrue(generated.find("label_1308:") != std::string::npos,
                     "internal JAL return address should still be emitted as a label");
            t.IsTrue(generated.find("        return;") != std::string::npos,
                     "JR $31 should return to the dispatcher/runtime after setting ctx->pc");
            t.IsTrue(generated.find("PS2Runtime::GuestBranchKind::Return") != std::string::npos,
                     "JR $31 should use the Return branch kind for precise diagnostics");
            t.IsTrue(generated.find("\"JR $ra\"") != std::string::npos,
                     "JR $31 should pass a return-specific debug name");
        });

        tc.Run("trailing JR $31 without decoded delay slot still emits return flow", [](TestCase &t) {
            Function func;
            func.name = "jr_ra_truncated";
            func.start = 0x1500;
            func.end = 0x1540;
            func.isRecompiled = true;
            func.isStub = false;

            Instruction jal = makeJal(0x1500, 0x1510);
            Instruction jalDelay = makeNop(0x1504);
            Instruction atReturn = makeNop(0x1508);
            Instruction atTarget = makeNop(0x1510);
            Instruction jr = makeJr(0x1514, 31);

            CodeGenerator gen({}, {});
            std::string generated = gen.generateFunction(func, {jal, jalDelay, atReturn, atTarget, jr}, false);
            printGeneratedCode("trailing JR $31 without decoded delay slot still emits return flow", generated);

            t.IsTrue(generated.find("const uint32_t jumpTarget = GPR_U32(ctx, 31);") != std::string::npos,
                     "truncated trailing JR should still read the return target");
            t.IsFalse(generated.find("switch (jumpTarget)") != std::string::npos,
                      "truncated trailing JR should not emit a broad local return-target switch");
            t.IsTrue(generated.find("label_1508:") != std::string::npos,
                     "truncated trailing JR should still include the internal return label");
            t.IsTrue(generated.find("// JR $31 - Handled by branch logic") == std::string::npos,
                     "truncated trailing JR must not degrade to comment-only output");
            t.IsTrue(generated.find("PS2Runtime::GuestBranchKind::Return") != std::string::npos,
                     "truncated JR $31 should still use return diagnostics");
        });

        tc.Run("unresolved JR non-RA uses dispatcher resume entries without broad local switch", [](TestCase &t) {
            Function func;
            func.name = "jr_non_ra_dispatcher_resume";
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
            CodeGenerator::AnalysisResult analysis = gen.collectInternalBranchTargets(func, {i0, jr, delay, i3});
            gen.setResumeEntryTargets({{func.start, std::vector<uint32_t>(
                                                        analysis.indirectFallbackEntryPoints.begin(),
                                                        analysis.indirectFallbackEntryPoints.end())}});
            std::string generated = gen.generateFunction(func, {i0, jr, delay, i3}, false);
            printGeneratedCode("unresolved JR non-RA uses dispatcher resume entries without broad local switch", generated);

            t.IsFalse(generated.find("switch (jumpTarget)") != std::string::npos,
                      "unresolved JR via non-RA register should not emit a broad local switch over internal labels");
            t.IsTrue(generated.find("switch (ctx->pc)") != std::string::npos,
                     "JR fallback labels should be promoted to dispatcher resume sites");
            t.IsTrue(generated.find("case 0x140cu: goto label_140c;") != std::string::npos,
                     "owner resume switch should include internal fallback labels");
            t.IsTrue(generated.find("ctx->pc = jumpTarget;") != std::string::npos,
                     "unresolved JR should hand the dynamic target back through ctx->pc");
            t.IsTrue(generated.find("PS2Runtime::GuestBranchKind::IndirectJump") != std::string::npos,
                     "unresolved non-RA JR should use indirect-jump diagnostics");
        });

        tc.Run("configured jump table addresses drive JR dispatch targets", [](TestCase &t) {
            Function func;
            func.name = "jr_configured_jump_table";
            func.start = 0x1600;
            func.end = 0x1640;
            func.isRecompiled = true;
            func.isStub = false;

            constexpr uint32_t tableAddress = 0x00200000u;

            Instruction lui{};
            lui.address = 0x1600;
            lui.opcode = OPCODE_LUI;
            lui.rt = 9;
            lui.immediate = static_cast<uint16_t>((tableAddress >> 16) & 0xFFFFu);

            Instruction addiu{};
            addiu.address = 0x1604;
            addiu.opcode = OPCODE_ADDIU;
            addiu.rs = 9;
            addiu.rt = 9;
            addiu.immediate = static_cast<uint16_t>(tableAddress & 0xFFFFu);
            addiu.simmediate = addiu.immediate;

            Instruction sll{};
            sll.address = 0x1608;
            sll.opcode = OPCODE_SPECIAL;
            sll.function = SPECIAL_SLL;
            sll.rd = 8;
            sll.rt = 4;
            sll.sa = 2;

            Instruction addu{};
            addu.address = 0x160C;
            addu.opcode = OPCODE_SPECIAL;
            addu.function = SPECIAL_ADDU;
            addu.rs = 9;
            addu.rt = 8;
            addu.rd = 9;

            Instruction lw{};
            lw.address = 0x1610;
            lw.opcode = OPCODE_LW;
            lw.rs = 9;
            lw.rt = 10;
            lw.immediate = 0;
            lw.simmediate = 0;

            Instruction jr = makeJr(0x1614, 10);
            Instruction jrDelay = makeNop(0x1618);
            Instruction target0 = makeNop(0x1620);
            Instruction target1 = makeNop(0x1630);

            JumpTable configured{};
            configured.address = tableAddress;
            configured.entries.push_back({0u, 0x1620u});
            configured.entries.push_back({1u, 0x1630u});

            CodeGenerator gen({}, {});
            gen.setConfiguredJumpTables({configured});
            CodeGenerator::AnalysisResult analysis = gen.collectInternalBranchTargets(
                func,
                {lui, addiu, sll, addu, lw, jr, jrDelay, target0, target1});

            t.IsTrue(analysis.jumpTableTargets.contains(0x1614u),
                     "configured JR table should be tracked as a resolved local jump table");
            t.IsFalse(analysis.resumeEntryPoints.contains(0x1620u),
                      "configured JR table targets should stay in-function dispatch labels");
            t.IsFalse(analysis.resumeEntryPoints.contains(0x1630u),
                      "configured JR table targets should not become dispatcher resume sites");

            std::string generated = gen.generateFunction(
                func,
                {lui, addiu, sll, addu, lw, jr, jrDelay, target0, target1},
                false);
            printGeneratedCode("configured jump table addresses drive JR dispatch targets", generated);

            t.IsTrue(generated.find("switch (jumpTarget)") != std::string::npos,
                     "JR should emit a switch");
            t.IsTrue(generated.find("case 0x1620u: goto label_1620;") != std::string::npos,
                     "configured table target 0x1620 should be emitted");
            t.IsTrue(generated.find("case 0x1630u: goto label_1630;") != std::string::npos,
                     "configured table target 0x1630 should be emitted");
            t.IsTrue(generated.find("switch (ctx->pc)") == std::string::npos,
                     "configured JR table labels should not emit a top-level dispatcher resume switch");
            t.IsTrue(generated.find("case 0x1600u: goto label_1600;") == std::string::npos,
                     "configured table should avoid broad JR fallback labels");
        });

        tc.Run("unresolved JALR uses runtime dispatch without broad local switch", [](TestCase &t) {
            Function func;
            func.name = "jalr_runtime_fallback";
            func.start = 0x1500;
            func.end = 0x1530;
            func.isRecompiled = true;
            func.isStub = false;

            // A call-like setup so there are multiple in-function labels available.
            Instruction jal = makeJal(0x1500, 0x1510);
            Instruction jalDelay = makeNop(0x1504);
            Instruction atReturn = makeNop(0x1508);
            Instruction atTarget = makeNop(0x1510);
            Instruction jalr = makeJalr(0x1514, 4, 31);
            Instruction jalrDelay = makeNop(0x1518);

            CodeGenerator gen({}, {});
            std::string generated = gen.generateFunction(func, {jal, jalDelay, atReturn, atTarget, jalr, jalrDelay}, false);
            printGeneratedCode("unresolved JALR uses runtime dispatch without broad local switch", generated);

            t.IsFalse(generated.find("switch (jumpTarget)") != std::string::npos,
                      "unresolved JALR should not emit a broad local switch over every internal label");
            t.IsTrue(generated.find("runtime->dispatchGuestBranch(rdram, ctx, jumpTarget") != std::string::npos,
                     "unresolved JALR should dispatch through the runtime branch helper");
            t.IsTrue(generated.find("PS2Runtime::GuestBranchKind::IndirectCall") != std::string::npos,
                     "JALR should retain indirect-call dispatch kind");
            t.IsTrue(generated.find("0x1514u, 0x151Cu") != std::string::npos,
                     "JALR should pass call-site and fallthrough PCs to runtime dispatch");
        });

        tc.Run("JALR fallback should not expose epilogue tail-jump labels", [](TestCase &t) {
            Function func;
            func.name = "jalr_epilogue_guard";
            func.start = 0x2000;
            func.end = 0x2030;
            func.isRecompiled = true;
            func.isStub = false;

            Instruction prolog{};
            prolog.address = 0x2000;
            prolog.opcode = OPCODE_ADDIU;
            prolog.rs = 29;
            prolog.rt = 29;
            prolog.simmediate = static_cast<uint32_t>(static_cast<int32_t>(-0x20));
            prolog.raw = 0;

            Instruction saveRa{};
            saveRa.address = 0x2004;
            saveRa.opcode = OPCODE_SD;
            saveRa.rs = 29;
            saveRa.rt = 31;
            saveRa.simmediate = 0x10;
            saveRa.raw = 0;

            // Dynamic callback entry point.
            Instruction jalr = makeJalr(0x2008, 2, 31);
            Instruction jalrDelay = makeNop(0x200C);

            Instruction restoreRa{};
            restoreRa.address = 0x2010;
            restoreRa.opcode = OPCODE_LD;
            restoreRa.rs = 29;
            restoreRa.rt = 31;
            restoreRa.simmediate = 0x10;
            restoreRa.raw = 0;

            // Tail jump sequence that must not be reachable from jalr fallback dispatch.
            Instruction tailJump{};
            tailJump.address = 0x2014;
            tailJump.opcode = OPCODE_J;
            tailJump.target = (0x3000u >> 2) & 0x3FFFFFFu;
            tailJump.hasDelaySlot = true;
            tailJump.raw = 0;

            Instruction tailDelay{};
            tailDelay.address = 0x2018;
            tailDelay.opcode = OPCODE_ADDIU;
            tailDelay.rs = 29;
            tailDelay.rt = 29;
            tailDelay.simmediate = 0x20;
            tailDelay.raw = 0;

            CodeGenerator gen({}, {});
            std::string generated = gen.generateFunction(
                func,
                {prolog, saveRa, jalr, jalrDelay, restoreRa, tailJump, tailDelay},
                false);
            printGeneratedCode("JALR fallback should not expose epilogue tail-jump labels", generated);

            t.IsTrue(generated.find("case 0x2014u: goto label_2014;") == std::string::npos,
                     "jalr fallback should not dispatch directly to epilogue tail-jump block");
            t.IsTrue(generated.find("case 0x2018u: goto label_2018;") == std::string::npos,
                     "jalr fallback should not dispatch directly to tail-jump delay slot");
        });

        tc.Run("VU random helpers emit line comments on separate lines", [](TestCase &t) {
            CodeGenerator gen({}, {});

            Instruction inst{};
            inst.rd = 7;
            inst.vectorInfo.fsf = 2;

            std::string vrnext = gen.translateVU_VRNEXT(inst);
            printGeneratedCode("VU random helpers emit line comments on separate lines - VRNEXT", vrnext);
            t.IsTrue(vrnext.find("// Simple LFSR-based random number generation (PS2-like behavior)\n"
                                 "    uint32_t feedback") != std::string::npos,
                     "VRNEXT should place the generated line comment on its own line");

            std::string vrinit = gen.translateVU_VRINIT(inst);
            printGeneratedCode("VU random helpers emit line comments on separate lines - VRINIT", vrinit);
            t.IsTrue(vrinit.find("// PS2 uses a specific LFSR initialization pattern\n"
                                 "    if (seed == 0) seed = 1;") != std::string::npos,
                     "VRINIT should place the generated line comment on its own line");

            std::string vrxor = gen.translateVU_VRXOR(inst);
            printGeneratedCode("VU random helpers emit line comments on separate lines - VRXOR", vrxor);
            t.IsTrue(vrxor.find("// XOR the current random value with the data from the VU vector register\n"
                                "    __m128i xored") != std::string::npos,
                     "VRXOR should keep the XOR comment on its own line");
            t.IsTrue(vrxor.find("// Apply a simple mixing function similar to PS2's LFSR\n"
                                "    __m128i mixed") != std::string::npos,
                     "VRXOR should keep the LFSR comment on its own line");
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
