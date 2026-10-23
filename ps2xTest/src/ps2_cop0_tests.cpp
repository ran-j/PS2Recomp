#include "MiniTest.h"
#include "ps2recomp/code_generator.h"
#include "ps2recomp/instructions.h"
#include "ps2recomp/r5900_decoder.h"

#include <string>

using namespace ps2recomp;

namespace
{
    constexpr uint32_t kAddress = 0x1000u;

    uint32_t encodeMfc0(uint32_t rt, uint32_t rd)
    {
        return (OPCODE_COP0 << 26) | (COP0_MF << 21) | (rt << 16) | (rd << 11);
    }

    uint32_t encodeMtc0(uint32_t rt, uint32_t rd)
    {
        return (OPCODE_COP0 << 26) | (COP0_MT << 21) | (rt << 16) | (rd << 11);
    }

    uint32_t encodeCo(uint32_t function)
    {
        return (OPCODE_COP0 << 26) | (COP0_CO << 21) | function;
    }

    uint32_t encodeBc0(uint32_t condition, uint16_t offset)
    {
        return (OPCODE_COP0 << 26) | (COP0_BC << 21) | (condition << 16) | offset;
    }

    Instruction decode(uint32_t raw)
    {
        R5900Decoder decoder;
        return decoder.decodeInstruction(kAddress, raw);
    }

    std::string translate(uint32_t raw)
    {
        CodeGenerator gen({}, {});
        return gen.translateInstruction(decode(raw));
    }

    struct Cop0Register
    {
        uint32_t index;
        const char *field;
    };

    // Every COP0 register the translator models, with the R5900Context field that backs it.
    constexpr Cop0Register kImplementedRegisters[] = {
        {COP0_REG_INDEX, "cop0_index"},
        {COP0_REG_RANDOM, "cop0_random"},
        {COP0_REG_ENTRYLO0, "cop0_entrylo0"},
        {COP0_REG_ENTRYLO1, "cop0_entrylo1"},
        {COP0_REG_CONTEXT, "cop0_context"},
        {COP0_REG_PAGEMASK, "cop0_pagemask"},
        {COP0_REG_WIRED, "cop0_wired"},
        {COP0_REG_BADVADDR, "cop0_badvaddr"},
        {COP0_REG_COUNT, "cop0_count"},
        {COP0_REG_ENTRYHI, "cop0_entryhi"},
        {COP0_REG_COMPARE, "cop0_compare"},
        {COP0_REG_STATUS, "cop0_status"},
        {COP0_REG_CAUSE, "cop0_cause"},
        {COP0_REG_EPC, "cop0_epc"},
        {COP0_REG_PRID, "cop0_prid"},
        {COP0_REG_CONFIG, "cop0_config"},
        {COP0_REG_BADPADDR, "cop0_badpaddr"},
        {COP0_REG_DEBUG, "cop0_debug"},
        {COP0_REG_PERF, "cop0_perf"},
        {COP0_REG_TAGLO, "cop0_taglo"},
        {COP0_REG_TAGHI, "cop0_taghi"},
        {COP0_REG_ERROREPC, "cop0_errorepc"},
    };

    // Register numbers with no backing field (reserved on the R5900).
    constexpr uint32_t kUnmodelledRegisters[] = {7, 17, 18, 19, 20, 21, 22, 26, 27, 31};
}

void register_ps2_cop0_tests()
{
    MiniTest::Case("COP0", [](TestCase &tc)
    {
        tc.Run("MFC0 reads every modelled register into rt, sign-extended", [](TestCase &t)
        {
            for (const Cop0Register &reg : kImplementedRegisters)
            {
                const std::string code = translate(encodeMfc0(9, reg.index));
                const std::string expected = std::string("SET_GPR_S32(ctx, 9, (int32_t)ctx->") + reg.field + ");";
                t.Equals(code, expected, "MFC0 $9, $" + std::to_string(reg.index) + " should read " + reg.field);
            }
        });

        tc.Run("MFC0 of an unmodelled register reads zero", [](TestCase &t)
        {
            for (uint32_t index : kUnmodelledRegisters)
            {
                const std::string code = translate(encodeMfc0(9, index));
                t.IsTrue(code.rfind("SET_GPR_S32(ctx, 9, 0);", 0) == 0,
                         "MFC0 $9, $" + std::to_string(index) + " should write 0, got: " + code);
            }
        });

        tc.Run("MFC0 marks a GPR write unless rt is $zero", [](TestCase &t)
        {
            const Instruction toGpr = decode(encodeMfc0(9, COP0_REG_STATUS));
            t.IsTrue(toGpr.modificationInfo.modifiesGPR, "MFC0 $9 should modify a GPR");

            const Instruction toZero = decode(encodeMfc0(0, COP0_REG_STATUS));
            t.IsFalse(toZero.modificationInfo.modifiesGPR, "MFC0 $0 should not modify a GPR");
        });

        tc.Run("MTC0 writes each writable register through its mask", [](TestCase &t)
        {
            struct Case
            {
                uint32_t index;
                const char *expected;
                const char *why;
            };
            const Case cases[] = {
                {COP0_REG_INDEX, "ctx->cop0_index = GPR_U32(ctx, 7) & 0x3F;", "Index selects one of 48 TLB entries (6 bits)"},
                {COP0_REG_ENTRYLO0, "ctx->cop0_entrylo0 = GPR_U32(ctx, 7) & 0x3FFFFFFF;", "EntryLo0 keeps S/PFN/C/D/V/G"},
                {COP0_REG_ENTRYLO1, "ctx->cop0_entrylo1 = GPR_U32(ctx, 7) & 0x3FFFFFFF;", "EntryLo1 keeps PFN/C/D/V/G"},
                {COP0_REG_CONTEXT, "ctx->cop0_context = (ctx->cop0_context & 0xFF800000) | (GPR_U32(ctx, 7) & 0x7FFFFF);", "Context writes only PTEBase, BadVPN2 is preserved"},
                {COP0_REG_PAGEMASK, "ctx->cop0_pagemask = GPR_U32(ctx, 7) & 0x01FFE000;", "PageMask keeps the MASK field"},
                {COP0_REG_WIRED, "ctx->cop0_wired = GPR_U32(ctx, 7) & 0x3F; ctx->cop0_random = 47;", "writing Wired resets Random to the top entry"},
                {COP0_REG_COUNT, "ctx->cop0_count = GPR_U32(ctx, 7);", "Count is a full 32-bit register"},
                {COP0_REG_ENTRYHI, "ctx->cop0_entryhi = GPR_U32(ctx, 7) & 0xC00000FF;", "EntryHi keeps the masked VPN2 and ASID bits"},
                {COP0_REG_COMPARE, "ctx->cop0_compare = GPR_U32(ctx, 7); ctx->cop0_cause &= ~0x8000;", "writing Compare acknowledges the timer interrupt (Cause.IP7)"},
                {COP0_REG_STATUS, "ctx->cop0_status = GPR_U32(ctx, 7) & 0xFF57FFFF;", "Status write mask"},
                {COP0_REG_CAUSE, "ctx->cop0_cause = (ctx->cop0_cause & ~0x00000300) | (GPR_U32(ctx, 7) & 0x00000300);", "only the software interrupt bits IP0/IP1 are writable"},
                {COP0_REG_EPC, "ctx->cop0_epc = GPR_U32(ctx, 7);", "EPC is a full 32-bit register"},
                {COP0_REG_CONFIG, "ctx->cop0_config = (ctx->cop0_config & ~0x7) | (GPR_U32(ctx, 7) & 0x7);", "only the K0 cache mode field is writable"},
                {COP0_REG_DEBUG, "ctx->cop0_debug = GPR_U32(ctx, 7);", "Debug"},
                {COP0_REG_PERF, "ctx->cop0_perf = GPR_U32(ctx, 7);", "Perf"},
                {COP0_REG_TAGLO, "ctx->cop0_taglo = GPR_U32(ctx, 7);", "TagLo"},
                {COP0_REG_TAGHI, "ctx->cop0_taghi = GPR_U32(ctx, 7);", "TagHi"},
                {COP0_REG_ERROREPC, "ctx->cop0_errorepc = GPR_U32(ctx, 7);", "ErrorEPC is a full 32-bit register"},
            };

            for (const Case &c : cases)
            {
                t.Equals(translate(encodeMtc0(7, c.index)), std::string(c.expected),
                         "MTC0 $7, $" + std::to_string(c.index) + ": " + c.why);
            }
        });

        tc.Run("MTC0 to a read-only register changes nothing", [](TestCase &t)
        {
            const uint32_t readOnly[] = {COP0_REG_RANDOM, COP0_REG_BADVADDR, COP0_REG_PRID, COP0_REG_BADPADDR};
            for (uint32_t index : readOnly)
            {
                const std::string code = translate(encodeMtc0(7, index));
                t.IsTrue(code.rfind("//", 0) == 0 && code.find('=') == std::string::npos,
                         "MTC0 to read-only $" + std::to_string(index) + " should emit only a comment, got: " + code);
            }
        });

        tc.Run("MTC0 to an unmodelled register changes nothing", [](TestCase &t)
        {
            for (uint32_t index : kUnmodelledRegisters)
            {
                const std::string code = translate(encodeMtc0(7, index));
                t.Equals(code, "// Unimplemented MTC0 to COP0 " + std::to_string(index),
                         "MTC0 to unmodelled $" + std::to_string(index) + " should be a no-op");
            }
        });

        tc.Run("TLB operations call the runtime TLB handlers", [](TestCase &t)
        {
            t.Equals(translate(encodeCo(COP0_CO_TLBR)), std::string("runtime->handleTLBR(rdram, ctx);"), "TLBR");
            t.Equals(translate(encodeCo(COP0_CO_TLBWI)), std::string("runtime->handleTLBWI(rdram, ctx);"), "TLBWI");
            t.Equals(translate(encodeCo(COP0_CO_TLBWR)), std::string("runtime->handleTLBWR(rdram, ctx);"), "TLBWR");
            t.Equals(translate(encodeCo(COP0_CO_TLBP)), std::string("runtime->handleTLBP(rdram, ctx);"), "TLBP");
        });

        tc.Run("EI and DI set and clear Status.EIE (bit 16)", [](TestCase &t)
        {
            const std::string ei = translate(encodeCo(COP0_CO_EI));
            const std::string di = translate(encodeCo(COP0_CO_DI));
            t.IsTrue(ei.find("ctx->cop0_status |= 0x10000;") != std::string::npos, "EI should set EIE, got: " + ei);
            t.IsTrue(di.find("ctx->cop0_status &= ~0x10000;") != std::string::npos, "DI should clear EIE, got: " + di);
        });

        tc.Run("ERET returns through ErrorEPC when ERL is set, otherwise EPC", [](TestCase &t)
        {
            const std::string code = translate(encodeCo(COP0_CO_ERET));
            const size_t erlCheck = code.find("if (ctx->cop0_status & 0x4)");
            const size_t toErrorEpc = code.find("ctx->pc = ctx->cop0_errorepc;");
            const size_t clearErl = code.find("ctx->cop0_status &= ~0x4;");
            const size_t elseBranch = code.find("} else {");
            const size_t toEpc = code.find("ctx->pc = ctx->cop0_epc;");
            const size_t clearExl = code.find("ctx->cop0_status &= ~0x2;");

            t.IsTrue(erlCheck != std::string::npos, "ERET should test Status.ERL (bit 2)");
            t.IsTrue(toErrorEpc > erlCheck && clearErl > erlCheck && toErrorEpc < elseBranch && clearErl < elseBranch,
                     "with ERL set, ERET should jump to ErrorEPC and clear ERL");
            t.IsTrue(toEpc > elseBranch && clearExl > elseBranch && toEpc != std::string::npos && clearExl != std::string::npos,
                     "with ERL clear, ERET should jump to EPC and clear EXL (bit 1)");
            t.IsTrue(code.find("runtime->clearLLBit(ctx);") != std::string::npos, "ERET should clear the LL bit");
            t.IsTrue(code.find("return;") != std::string::npos, "ERET should leave the recompiled block");
        });

        tc.Run("BC0 forms decode as branches with a delay slot", [](TestCase &t)
        {
            const uint32_t conditions[] = {COP0_BC_BCF, COP0_BC_BCT, COP0_BC_BCFL, COP0_BC_BCTL};
            for (uint32_t condition : conditions)
            {
                const Instruction inst = decode(encodeBc0(condition, 4));
                t.IsTrue(inst.isBranch, "BC0 condition " + std::to_string(condition) + " should be a branch");
                t.IsTrue(inst.hasDelaySlot, "BC0 condition " + std::to_string(condition) + " should have a delay slot");
            }
        });

        tc.Run("Unknown COP0 operations are reported as unhandled", [](TestCase &t)
        {
            const std::string unknownCo = translate(encodeCo(0x20));
            t.IsTrue(unknownCo.find("Unhandled COP0 CO-OP: 0x20") != std::string::npos,
                     "an unknown CO function should be unhandled, got: " + unknownCo);

            // DMFC0 (fmt 0x01) is MIPS64 and does not exist on the R5900.
            const std::string unknownFormat = translate((OPCODE_COP0 << 26) | (0x01u << 21) | (9u << 16) | (COP0_REG_STATUS << 11));
            t.IsTrue(unknownFormat.find("Unhandled COP0 instruction format: 0x1") != std::string::npos,
                     "an unknown COP0 format should be unhandled, got: " + unknownFormat);
        });
    });
}
