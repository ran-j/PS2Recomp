#include "ps2recomp/Translators/instruction_translator.h"
#include "ps2recomp/code_generator.h"
#include "ps2recomp/codegen_helpers.h"
#include "ps2recomp/instructions.h"
#include "ps2recomp/types.h"
#include "ps2recomp/control_flow_utils.h"
#include "runtime/ps2_address.h"

#include <fmt/format.h>

namespace ps2recomp
{
    namespace
    {
        std::string addressLiteral(uint32_t address)
        {
            return fmt::format("0x{:X}u", address);
        }

        uint32_t memoryAccessSize(int width)
        {
            return static_cast<uint32_t>(width / 8);
        }

        std::string memoryValueType(int width)
        {
            switch (width)
            {
            case 8:
                return "uint8_t";
            case 16:
                return "uint16_t";
            case 32:
                return "uint32_t";
            case 64:
                return "uint64_t";
            default:
                return "";
            }
        }

        std::string genFastWrite(int width, uint32_t address, const std::string &val)
        {
            const std::string addr = addressLiteral(address);
            if (width == 128)
            {
                return fmt::format(
                    "do {{ __m128i _value = ({}); "
                    "const uint64_t _lo = static_cast<uint64_t>(PS2_EXTRACT_EPI64_0(_value)); "
                    "const uint64_t _hi = static_cast<uint64_t>(PS2_EXTRACT_EPI64_1(_value)); "
                    "ps2TraceGuestWrite(rdram, {}, 16u, _lo, _hi, \"WRITE128\", ctx); "
                    "FAST_WRITE128({}, _value); }} while (0)",
                    val, addr, addr);
            }

            const std::string valueType = memoryValueType(width);
            return fmt::format(
                "do {{ {} _value = static_cast<{}>({}); "
                "ps2TraceGuestWrite(rdram, {}, {}u, _value, 0u, \"WRITE{}\", ctx); "
                "FAST_WRITE{}({}, _value); }} while (0)",
                valueType, valueType, val, addr, memoryAccessSize(width), width, width, addr);
        }
    }

    InstructionTranslator::InstructionTranslator(CodeGenerator &codeGenerator)
        : m_codeGenerator(codeGenerator)
    {
    }

    MemoryAccessHint InstructionTranslator::effectiveMemoryHintFor(const Instruction &inst, const MemoryAccessHint &memoryHint) const
    {
        MemoryAccessHint effectiveMemoryHint = memoryHint;
        if (inst.isMmio)
        {
            effectiveMemoryHint.hasAddress = true;
            effectiveMemoryHint.address = inst.mmioAddress;
        }

        return effectiveMemoryHint;
    }

    std::string InstructionTranslator::translateMemoryRead(const Instruction &inst,
                                                           const MemoryAccessHint &memoryHint,
                                                           int width,
                                                           const std::string &addr) const
    {
        if (memoryHint.hasAddress)
        {
            const uint32_t resolvedAddress = memoryHint.address;
            const std::string resolvedAddressExpr = addressLiteral(resolvedAddress);
            if (inst.isMmio || Ps2IsSpecialAddress(resolvedAddress))
            {
                return fmt::format("runtime->Load{}(rdram, ctx, {})", width, resolvedAddressExpr);
            }
            return fmt::format("FAST_READ{}({})", width, resolvedAddressExpr);
        }

        if (inst.isMmio)
        {
            return fmt::format("runtime->Load{}(rdram, ctx, {})", width, addr);
        }
        return fmt::format("READ{}({})", width, addr);
    }

    std::string InstructionTranslator::translateMemoryWrite(const Instruction &inst,
                                                            const MemoryAccessHint &memoryHint,
                                                            int width,
                                                            const std::string &addr,
                                                            const std::string &value) const
    {
        if (memoryHint.hasAddress)
        {
            const uint32_t resolvedAddress = memoryHint.address;
            const std::string resolvedAddressExpr = addressLiteral(resolvedAddress);
            if (inst.isMmio || Ps2IsSpecialAddress(resolvedAddress))
            {
                return fmt::format("runtime->Store{}(rdram, ctx, {}, {})", width, resolvedAddressExpr, value);
            }
            return genFastWrite(width, resolvedAddress, value);
        }

        if (inst.isMmio)
        {
            return fmt::format("runtime->Store{}(rdram, ctx, {}, {})", width, addr, value);
        }
        return fmt::format("WRITE{}({}, {})", width, addr, value);
    }

    std::string InstructionTranslator::translate(const Instruction &inst, const MemoryAccessHint &memoryHint)
    {
        if (inst.isMMI)
        {
            return m_codeGenerator.translateMMIInstruction(inst);
        }

        const MemoryAccessHint effectiveMemoryHint = effectiveMemoryHintFor(inst, memoryHint);

        auto genRead = [&](int width, const std::string &addr)
        {
            return translateMemoryRead(inst, effectiveMemoryHint, width, addr);
        };

        auto genWrite = [&](int width, const std::string &addr, const std::string &val)
        {
            return translateMemoryWrite(inst, effectiveMemoryHint, width, addr, val);
        };

        switch (inst.opcode)
        {
        case OPCODE_SPECIAL:
            return m_codeGenerator.translateSpecialInstruction(inst);
        case OPCODE_REGIMM:
            return m_codeGenerator.translateRegimmInstruction(inst);
        case OPCODE_COP0:
            return m_codeGenerator.translateCOP0Instruction(inst);
        case OPCODE_COP1:
            return m_codeGenerator.translateFPUInstruction(inst);
        case OPCODE_COP2:
            return m_codeGenerator.translateVUInstruction(inst);
        case OPCODE_ADDI:
            if (inst.rt == 0)
                return "// NOP (addi to $zero)";
            return fmt::format(
                "{{ uint32_t tmp; bool ov; "
                "ADD32_OV(GPR_U32(ctx, {}), (int32_t){}, tmp, ov); "
                "if (ov) runtime->SignalException(ctx, EXCEPTION_INTEGER_OVERFLOW); "
                "else SET_GPR_S32(ctx, {}, (int32_t)tmp); }}",
                inst.rs, inst.simmediate, inst.rt);

        case OPCODE_ADDIU:
            if (inst.rt == 0)
                return "// NOP (addiu $zero, ...)";
            return fmt::format("SET_GPR_S32(ctx, {}, (int32_t)ADD32(GPR_U32(ctx, {}), {}));", inst.rt, inst.rs, inst.simmediate);
        case OPCODE_SLTI:
            return fmt::format("SET_GPR_U64(ctx, {}, ((int64_t)GPR_S64(ctx, {}) < (int64_t)(int32_t){}) ? 1 : 0);", inst.rt, inst.rs, inst.simmediate);
        case OPCODE_SLTIU:
            return fmt::format("SET_GPR_U64(ctx, {}, ((uint64_t)GPR_U64(ctx, {}) < (uint64_t)(int64_t)(int32_t){}) ? 1 : 0);", inst.rt, inst.rs, inst.simmediate);
        case OPCODE_ANDI:
            return fmt::format("SET_GPR_U64(ctx, {}, GPR_U64(ctx, {}) & (uint64_t)(uint16_t){});", inst.rt, inst.rs, inst.immediate);
        case OPCODE_ORI:
            return fmt::format("SET_GPR_U64(ctx, {}, GPR_U64(ctx, {}) | (uint64_t)(uint16_t){});", inst.rt, inst.rs, inst.immediate);
        case OPCODE_XORI:
            return fmt::format("SET_GPR_U64(ctx, {}, GPR_U64(ctx, {}) ^ (uint64_t)(uint16_t){});", inst.rt, inst.rs, inst.immediate);
        case OPCODE_LUI:
            return fmt::format("SET_GPR_S32(ctx, {}, (int32_t)((uint32_t){} << 16));", inst.rt, inst.immediate);
        case OPCODE_LB:
            return fmt::format("SET_GPR_S32(ctx, {}, (int8_t){});", inst.rt, genRead(8, fmt::format("ADD32(GPR_U32(ctx, {}), {})", inst.rs, inst.simmediate)));
        case OPCODE_LH:
            return fmt::format("SET_GPR_S32(ctx, {}, (int16_t){});", inst.rt, genRead(16, fmt::format("ADD32(GPR_U32(ctx, {}), {})", inst.rs, inst.simmediate)));
        case OPCODE_LW:
            return fmt::format("SET_GPR_S32(ctx, {}, (int32_t){});", inst.rt, genRead(32, fmt::format("ADD32(GPR_U32(ctx, {}), {})", inst.rs, inst.simmediate)));
        case OPCODE_LBU:
            return fmt::format("SET_GPR_U32(ctx, {}, (uint8_t){});", inst.rt, genRead(8, fmt::format("ADD32(GPR_U32(ctx, {}), {})", inst.rs, inst.simmediate)));
        case OPCODE_LHU:
            return fmt::format("SET_GPR_U32(ctx, {}, (uint16_t){});", inst.rt, genRead(16, fmt::format("ADD32(GPR_U32(ctx, {}), {})", inst.rs, inst.simmediate)));
        case OPCODE_LWU:
            return fmt::format("SET_GPR_U32(ctx, {}, {});", inst.rt, genRead(32, fmt::format("ADD32(GPR_U32(ctx, {}), {})", inst.rs, inst.simmediate)));
        case OPCODE_SB:
            return genWrite(8, fmt::format("ADD32(GPR_U32(ctx, {}), {})", inst.rs, inst.simmediate), fmt::format("(uint8_t)GPR_U32(ctx, {})", inst.rt)) + ";";
        case OPCODE_SH:
            return genWrite(16, fmt::format("ADD32(GPR_U32(ctx, {}), {})", inst.rs, inst.simmediate), fmt::format("(uint16_t)GPR_U32(ctx, {})", inst.rt)) + ";";
        case OPCODE_SW:
            return genWrite(32, fmt::format("ADD32(GPR_U32(ctx, {}), {})", inst.rs, inst.simmediate), fmt::format("GPR_U32(ctx, {})", inst.rt)) + ";";
        case OPCODE_LQ:
            return fmt::format("SET_GPR_VEC(ctx, {}, {});", inst.rt, genRead(128, fmt::format("ADD32(GPR_U32(ctx, {}), {})", inst.rs, inst.simmediate)));
        case OPCODE_SQ:
            return genWrite(128, fmt::format("ADD32(GPR_U32(ctx, {}), {})", inst.rs, inst.simmediate), fmt::format("GPR_VEC(ctx, {})", inst.rt)) + ";";
        case OPCODE_LD:
            return fmt::format("SET_GPR_U64(ctx, {}, {});", inst.rt, genRead(64, fmt::format("ADD32(GPR_U32(ctx, {}), {})", inst.rs, inst.simmediate)));
        case OPCODE_SD:
            return genWrite(64, fmt::format("ADD32(GPR_U32(ctx, {}), {})", inst.rs, inst.simmediate), fmt::format("GPR_U64(ctx, {})", inst.rt)) + ";";
        case OPCODE_LWC1:
            return fmt::format("{{ uint32_t bits = {}; float f; std::memcpy(&f, &bits, sizeof(f)); ctx->f[{}] = f; }}", genRead(32, fmt::format("ADD32(GPR_U32(ctx, {}), {})", inst.rs, inst.simmediate)), inst.rt);
        case OPCODE_SWC1:
            return fmt::format(
                "{{ float f = ctx->f[{}]; uint32_t bits; std::memcpy(&bits, &f, sizeof(bits)); {}; }}",
                inst.rt,
                genWrite(32, fmt::format("ADD32(GPR_U32(ctx, {}), {})", inst.rs, inst.simmediate), "bits"));
        case OPCODE_LDC2:
            return fmt::format("ctx->vu0_vf[{}] = _mm_castsi128_ps({});", inst.rt, genRead(128, fmt::format("ADD32(GPR_U32(ctx, {}), {})", inst.rs, inst.simmediate)));
        case OPCODE_SDC2:
            return genWrite(128, fmt::format("ADD32(GPR_U32(ctx, {}), {})", inst.rs, inst.simmediate), fmt::format("_mm_castps_si128(ctx->vu0_vf[{}])", inst.rt)) + ";";
        case OPCODE_DADDI:
            return fmt::format(
                "{{ int64_t src = (int64_t)GPR_S64(ctx, {}); "
                "int64_t imm = (int64_t)(int32_t){}; "
                "int64_t res = src + imm; "
                "if (((src ^ imm) >= 0) && ((src ^ res) < 0)) "
                "    runtime->SignalException(ctx, EXCEPTION_INTEGER_OVERFLOW); "
                "else SET_GPR_S64(ctx, {}, res); }}",
                inst.rs, inst.simmediate, inst.rt);
        case OPCODE_DADDIU:
            return fmt::format(
                "SET_GPR_S64(ctx, {}, (int64_t)GPR_S64(ctx, {}) + (int64_t)(int32_t){});",
                inst.rt, inst.rs, inst.simmediate);
        case OPCODE_J:
            return fmt::format("// J 0x{:X} - Handled by branch logic", buildAbsoluteJumpTarget(inst.address, inst.target));
        case OPCODE_JAL:
            return fmt::format("// JAL 0x{:X} - Handled by branch logic", buildAbsoluteJumpTarget(inst.address, inst.target));
        case OPCODE_BEQ:
        case OPCODE_BNE:
        case OPCODE_BLEZ:
        case OPCODE_BGTZ:
        case OPCODE_BEQL:
        case OPCODE_BNEL:
        case OPCODE_BLEZL:
        case OPCODE_BGTZL:
            return fmt::format("// Likely branch instruction at 0x{:X} - Handled by branch logic", inst.address);

        case OPCODE_LDL:
            return fmt::format("{{ uint32_t addr = ADD32(GPR_U32(ctx, {}), {}); "
                               "uint32_t aligned_addr = addr & ~7u; "
                               "uint32_t offset = addr & 7u; "
                               "uint64_t mem = {}; "
                               "uint32_t shift = (7u - offset) << 3; "
                               "uint64_t keepMask = (shift == 0) ? 0ull : ((1ull << shift) - 1ull); "
                               "SET_GPR_U64(ctx, {}, (GPR_U64(ctx, {}) & keepMask) | (mem << shift)); }}",
                               inst.rs, inst.simmediate, genRead(64, "aligned_addr"), inst.rt, inst.rt);

        case OPCODE_LDR:
            return fmt::format("{{ uint32_t addr = ADD32(GPR_U32(ctx, {}), {}); "
                               "uint32_t aligned_addr = addr & ~7u; "
                               "uint32_t offset = addr & 7u; "
                               "uint64_t mem = {}; "
                               "uint32_t shift = offset << 3; "
                               "uint64_t keepMask = (offset == 0) ? 0ull : (0xFFFFFFFFFFFFFFFFull << ((8u - offset) << 3)); "
                               "SET_GPR_U64(ctx, {}, (GPR_U64(ctx, {}) & keepMask) | (mem >> shift)); }}",
                               inst.rs, inst.simmediate, genRead(64, "aligned_addr"), inst.rt, inst.rt);

        case OPCODE_LWL:
            return fmt::format("{{ uint32_t addr = ADD32(GPR_U32(ctx, {}), {}); "
                               "uint32_t aligned_addr = addr & ~3u; "
                               "uint32_t offset = addr & 3u; "
                               "uint32_t mem = {}; "
                               "uint32_t shift = (3u - offset) << 3; "
                               "uint32_t keepMask = (shift == 0) ? 0u : ((1u << shift) - 1u); "
                               "uint32_t merged = (GPR_U32(ctx, {}) & keepMask) | (mem << shift); "
                               "SET_GPR_S32(ctx, {}, (int32_t)merged); }}",
                               inst.rs, inst.simmediate, genRead(32, "aligned_addr"), inst.rt, inst.rt);

        case OPCODE_LWR:
            return fmt::format("{{ uint32_t addr = ADD32(GPR_U32(ctx, {}), {}); "
                               "uint32_t aligned_addr = addr & ~3u; "
                               "uint32_t offset = addr & 3u; "
                               "uint32_t mem = {}; "
                               "uint32_t shift = offset << 3; "
                               "uint32_t keepMask = (offset == 0) ? 0u : (0xFFFFFFFFu << ((4u - offset) << 3)); "
                               "uint32_t merged32 = (GPR_U32(ctx, {}) & keepMask) | (mem >> shift); "
                               "uint64_t merged64 = (GPR_U64(ctx, {}) & 0xFFFFFFFF00000000ull) | (uint64_t)merged32; "
                               "if (offset == 0) merged64 = (uint64_t)(int64_t)(int32_t)merged32; "
                               "SET_GPR_U64(ctx, {}, merged64); }}",
                               inst.rs, inst.simmediate, genRead(32, "aligned_addr"),
                               inst.rt, inst.rt, inst.rt);

        case OPCODE_SWL:
            return fmt::format("{{ uint32_t addr = ADD32(GPR_U32(ctx, {}), {}); "
                               "uint32_t aligned_addr = addr & ~3u; "
                               "uint32_t offset = addr & 3u; "
                               "uint32_t shift = (3u - offset) << 3; "
                               "uint32_t mask = 0xFFFFFFFFu >> shift; "
                               "uint32_t old_data = {}; "
                               "uint32_t val = GPR_U32(ctx, {}); "
                               "uint32_t new_data = (old_data & ~mask) | ((val >> shift) & mask); "
                               "{}; }}",
                               inst.rs, inst.simmediate, genRead(32, "aligned_addr"), inst.rt, genWrite(32, "aligned_addr", "new_data"));

        case OPCODE_SWR:
            return fmt::format("{{ uint32_t addr = ADD32(GPR_U32(ctx, {}), {}); "
                               "uint32_t aligned_addr = addr & ~3u; "
                               "uint32_t offset = addr & 3u; "
                               "uint32_t shift = offset << 3; "
                               "uint32_t mask = 0xFFFFFFFFu << shift; "
                               "uint32_t old_data = {}; "
                               "uint32_t val = GPR_U32(ctx, {}); "
                               "uint32_t new_data = (old_data & ~mask) | ((val << shift) & mask); "
                               "{}; }}",
                               inst.rs, inst.simmediate, genRead(32, "aligned_addr"), inst.rt, genWrite(32, "aligned_addr", "new_data"));

        case OPCODE_SDL:
            return fmt::format("{{ uint32_t addr = ADD32(GPR_U32(ctx, {}), {}); "
                               "uint32_t aligned_addr = addr & ~7u; "
                               "uint32_t offset = addr & 7u; "
                               "uint32_t shift = (7u - offset) << 3; "
                               "uint64_t mask = 0xFFFFFFFFFFFFFFFFull >> shift; "
                               "uint64_t old_data = {}; "
                               "uint64_t val = GPR_U64(ctx, {}); "
                               "uint64_t new_data = (old_data & ~mask) | ((val >> shift) & mask); "
                               "{}; }}",
                               inst.rs, inst.simmediate, genRead(64, "aligned_addr"), inst.rt, genWrite(64, "aligned_addr", "new_data"));

        case OPCODE_SDR:
            return fmt::format("{{ uint32_t addr = ADD32(GPR_U32(ctx, {}), {}); "
                               "uint32_t aligned_addr = addr & ~7u; "
                               "uint32_t offset = addr & 7u; "
                               "uint32_t shift = offset << 3; "
                               "uint64_t mask = 0xFFFFFFFFFFFFFFFFull << shift; "
                               "uint64_t old_data = {}; "
                               "uint64_t val = GPR_U64(ctx, {}); "
                               "uint64_t new_data = (old_data & ~mask) | ((val << shift) & mask); "
                               "{}; }}",
                               inst.rs, inst.simmediate, genRead(64, "aligned_addr"), inst.rt, genWrite(64, "aligned_addr", "new_data"));
        case OPCODE_CACHE:
            return "// CACHE instruction (ignored)";
        case OPCODE_PREF:
            return "// PREF instruction (ignored)";
        case OPCODE_LL:
            return fmt::format(
                "{{ uint32_t addr = ADD32(GPR_U32(ctx, {}), {}); "
                "SET_GPR_S32(ctx, {}, (int32_t)READ32(addr)); "
                "ctx->llbit = 1; ctx->lladdr = addr; }}",
                inst.rs, inst.simmediate, inst.rt);
        case OPCODE_SC:
            return fmt::format(
                "{{ uint32_t addr = ADD32(GPR_U32(ctx, {}), {}); "
                "if (ctx->llbit && ctx->lladdr == addr) {{ WRITE32(addr, GPR_U32(ctx, {})); "
                "SET_GPR_S32(ctx, {}, 1); }} "
                "else {{ SET_GPR_S32(ctx, {}, 0); }} "
                "ctx->llbit = 0; ctx->lladdr = 0; }}",
                inst.rs, inst.simmediate, inst.rt, inst.rt, inst.rt);
        default:
            return m_codeGenerator.emitUnhandledInstruction(inst, fmt::format("Unhandled opcode: 0x{:X}", inst.opcode));
        }
    }

}
