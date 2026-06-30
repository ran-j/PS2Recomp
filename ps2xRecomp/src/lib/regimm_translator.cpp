#include "ps2recomp/Translators/regimm_translator.h"
#include "ps2recomp/code_generator.h"
#include "ps2recomp/codegen_helpers.h"
#include "ps2recomp/instructions.h"
#include "ps2recomp/types.h"

#include <fmt/format.h>
#include <sstream>
#include <cmath>


namespace ps2recomp
{
    RegimmTranslator::RegimmTranslator(CodeGenerator &codeGenerator)
        : m_codeGenerator(codeGenerator)
    {
    }

    std::string RegimmTranslator::translate(const Instruction &inst)
    {
        switch (inst.rt)
        {
        case REGIMM_BLTZ:
        case REGIMM_BGEZ:
        case REGIMM_BLTZL:
        case REGIMM_BGEZL:
        case REGIMM_BLTZAL:
        case REGIMM_BGEZAL:
        case REGIMM_BLTZALL:
        case REGIMM_BGEZALL:
        {
            const int32_t offsetBytes = (static_cast<int32_t>(static_cast<int16_t>(inst.simmediate)) << 2);
            const uint32_t target = static_cast<uint32_t>(static_cast<int64_t>(inst.address + 4u) + static_cast<int64_t>(offsetBytes));
            return fmt::format("// REGIMM branch instruction to 0x{:X} - Handled by branch logic", target);
        }
        case REGIMM_MTSAB:
            return fmt::format("ctx->sa = ((GPR_U32(ctx, {}) ^ (uint32_t){}) & 0xF) << 3;", inst.rs, inst.simmediate);
        case REGIMM_MTSAH:
            return fmt::format("ctx->sa = ((GPR_U32(ctx, {}) ^ (uint32_t){}) & 0x7) << 4;", inst.rs, inst.simmediate);
        case REGIMM_TGEI:
            return fmt::format("if (GPR_S64(ctx, {}) >= (int64_t)(int32_t){}) {{ runtime->handleTrap(rdram, ctx); }}", inst.rs, inst.simmediate);
        case REGIMM_TGEIU:
            return fmt::format("if (GPR_U64(ctx, {}) >= (uint64_t)(int64_t)(int32_t){}) {{ runtime->handleTrap(rdram, ctx); }}", inst.rs, inst.simmediate);
        case REGIMM_TLTI:
            return fmt::format("if (GPR_S64(ctx, {}) < (int64_t)(int32_t){}) {{ runtime->handleTrap(rdram, ctx); }}", inst.rs, inst.simmediate);
        case REGIMM_TLTIU:
            return fmt::format("if (GPR_U64(ctx, {}) < (uint64_t)(int64_t)(int32_t){}) {{ runtime->handleTrap(rdram, ctx); }}", inst.rs, inst.simmediate);
        case REGIMM_TEQI:
            return fmt::format("if (GPR_S64(ctx, {}) == (int64_t)(int32_t){}) {{ runtime->handleTrap(rdram, ctx); }}", inst.rs, inst.simmediate);
        case REGIMM_TNEI:
            return fmt::format("if (GPR_S64(ctx, {}) != (int64_t)(int32_t){}) {{ runtime->handleTrap(rdram, ctx); }}", inst.rs, inst.simmediate);
        default:
            return m_codeGenerator.emitUnhandledInstruction(inst, fmt::format("Unhandled REGIMM instruction: 0x{:X}", inst.rt));
        }
    }

}
