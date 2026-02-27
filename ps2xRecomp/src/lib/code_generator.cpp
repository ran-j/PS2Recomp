#include "ps2recomp/code_generator.h"
#include "ps2recomp/instructions.h"
#include "ps2recomp/ps2_recompiler.h"
#include "ps2recomp/types.h"
#include "ps2_runtime_calls.h"
#include <fmt/format.h>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <unordered_set>
#include <unordered_map>
#include <iostream>
#include <cctype>
#include <cmath>

namespace ps2recomp
{
    const std::unordered_set<std::string> kKeywords = {
        "alignas", "alignof", "and", "and_eq", "asm", "auto", "bitand", "bitor", "bool",
        "break", "case", "catch", "char", "char8_t", "char16_t", "char32_t", "class",
        "compl", "concept", "const", "consteval", "constexpr", "constinit", "const_cast",
        "continue", "co_await", "co_return", "co_yield", "decltype", "default", "delete",
        "do", "double", "dynamic_cast", "else", "enum", "explicit", "export", "extern",
        "false", "float", "for", "friend", "goto", "if", "inline", "int", "long", "mutable",
        "namespace", "new", "noexcept", "not", "not_eq", "nullptr", "operator", "or", "or_eq",
        "private", "protected", "public", "register", "reinterpret_cast", "requires", "return",
        "short", "signed", "sizeof", "static", "static_assert", "static_cast", "struct",
        "switch", "template", "this", "thread_local", "throw", "true", "try", "typedef",
        "typeid", "typename", "union", "unsigned", "using", "virtual", "void", "volatile",
        "wchar_t", "while", "xor", "xor_eq"};
}

namespace ps2recomp
{
    static uint32_t buildAbsoluteJumpTarget(uint32_t address, uint32_t target)
    {
        return ((address + 4) & 0xF0000000u) | (target << 2);
    }

    static std::string formatFloatLiteral(float value)
    {
        if (!std::isfinite(value))
        {
            return (value < 0.0f) ? "-INFINITY" : "INFINITY";
        }

        std::string literal = fmt::format("{:.9g}", value);
        if (literal.find_first_of(".eE") == std::string::npos)
        {
            literal += ".0";
        }
        literal += 'f';
        return literal;
    }

    static std::string sanitizeIdentifierBody(const std::string &name)
    {
        std::string sanitized;
        sanitized.reserve(name.size() + 1);

        for (char c : name)
        {
            const unsigned char uc = static_cast<unsigned char>(c);
            if (std::isalnum(uc) || c == '_')
            {
                sanitized.push_back(c);
            }
            else
            {
                sanitized.push_back('_');
            }
        }

        if (sanitized.empty())
        {
            return sanitized;
        }

        const unsigned char first = static_cast<unsigned char>(sanitized.front());
        if (!(std::isalpha(first) || sanitized.front() == '_'))
        {
            sanitized.insert(sanitized.begin(), '_');
        }

        return sanitized;
    }

    static bool isReservedCxxIdentifier(const std::string &name)
    {
        if (name.size() >= 2 && name[0] == '_' && name[1] == '_')
            return true;
        if (name.size() >= 2 && name[0] == '_' && std::isupper(static_cast<unsigned char>(name[1])))
            return true;
        return false;
    }

    static bool isReservedCxxKeyword(const std::string &name)
    {
        return kKeywords.contains(name);
    }

    CodeGenerator::CodeGenerator(const std::vector<Symbol> &symbols, const std::vector<Section> &sections)
        : m_sections(sections)
    {
        for (auto &symbol : symbols)
        {
            m_symbols.emplace(symbol.address, symbol);
        }
    }

    void CodeGenerator::setRenamedFunctions(const std::unordered_map<uint32_t, std::string> &renames)
    {
        m_renamedFunctions = renames;
    }

    void CodeGenerator::setBootstrapInfo(const BootstrapInfo &info)
    {
        m_bootstrapInfo = info;
    }

    void CodeGenerator::setRelocationCallNames(const std::unordered_map<uint32_t, std::string> &callNames)
    {
        m_relocationCallNames = callNames;
    }

    void CodeGenerator::setConfiguredJumpTables(const std::vector<JumpTable> &jumpTables)
    {
        m_configJumpTableTargetsByAddress.clear();
        for (const auto &table : jumpTables)
        {
            auto &targets = m_configJumpTableTargetsByAddress[table.address];
            for (const auto &entry : table.entries)
            {
                targets.push_back(entry.target);
            }
        }

        for (auto &[address, targets] : m_configJumpTableTargetsByAddress)
        {
            (void)address;
            std::sort(targets.begin(), targets.end());
            targets.erase(std::unique(targets.begin(), targets.end()), targets.end());
        }
    }

    std::string CodeGenerator::getFunctionName(uint32_t address) const
    {
        auto it = m_renamedFunctions.find(address);
        if (it != m_renamedFunctions.end())
        {
            return it->second;
        }

        const Symbol *sym = findSymbolByAddress(address);
        if (sym && sym->isFunction)
        {
            return CodeGenerator::sanitizeFunctionName(sym->name);
        }

        return "";
    }

    std::string CodeGenerator::sanitizeFunctionName(const std::string &name) const
    {
        std::string sanitized = sanitizeIdentifierBody(name);
        if (sanitized.empty())
            return sanitized;

        // ugly but will do for now
        if (sanitized == "main")
            return "ps2_main";

        if (isReservedCxxKeyword(sanitized))
            return "ps2_" + sanitized;

        if (sanitized[0] == '_')
            return "ps2" + sanitized;

        if (!isReservedCxxIdentifier(sanitized))
            return sanitized;

        return "ps2_" + sanitized;
    }

    std::string CodeGenerator::handleBranchDelaySlots(
        const Instruction &branchInst,
        const Instruction &delaySlot,
        const Function &function,
        const AnalysisResult &analysisResult)
    {
        const std::unordered_set<uint32_t> &internalTargets = analysisResult.entryPoints;
        std::stringstream ss;

        const bool hasValidDelaySlot = !(delaySlot.opcode == OPCODE_SPECIAL &&
                                         delaySlot.function == SPECIAL_SLL &&
                                         delaySlot.rd == 0 &&
                                         delaySlot.rt == 0 &&
                                         delaySlot.sa == 0);

        std::string delaySlotCode = "";
        std::string delaySlotPrefix = "";
        std::string delaySlotSuffix = "";
        if (hasValidDelaySlot) {
            delaySlotPrefix = "ctx->in_delay_slot = true; ctx->branch_pc = 0x" + fmt::format("{:X}", branchInst.address) + "u;\n        ";
            delaySlotCode = "    // 0x" + fmt::format("{:x}", delaySlot.address) + ": 0x" + fmt::format("{:x}", delaySlot.raw);
            if (!delaySlot.disassembly.empty()) {
                delaySlotCode += "  " + delaySlot.disassembly;
            }
            delaySlotCode += " (Delay Slot)\n        " + translateInstruction(delaySlot);
            delaySlotSuffix = "\n        ctx->in_delay_slot = false;";
        }

        const uint8_t rs_reg = branchInst.rs;
        const uint8_t rt_reg = branchInst.rt;
        const uint8_t rd_reg = branchInst.rd;

        const uint32_t branchPc = branchInst.address;
        const uint32_t delayPc = branchInst.address + 4u;
        const uint32_t fallthroughPc = branchInst.address + 8u;

        std::vector<uint32_t> sortedInternalTargets;
        if (branchInst.opcode == OPCODE_SPECIAL &&
            (branchInst.function == SPECIAL_JR || branchInst.function == SPECIAL_JALR) &&
            !internalTargets.empty())
        {
            auto jtIt = analysisResult.jumpTableTargets.find(branchInst.address);
            if (jtIt != analysisResult.jumpTableTargets.end()) {
                sortedInternalTargets = jtIt->second;
                std::sort(sortedInternalTargets.begin(), sortedInternalTargets.end());
            } else {
                sortedInternalTargets.reserve(internalTargets.size());
                for (uint32_t t : internalTargets)
                {
                    sortedInternalTargets.push_back(t);
                }
                std::sort(sortedInternalTargets.begin(), sortedInternalTargets.end());
            }
        }

        if (internalTargets.contains(delayPc))
        {
            ss << fmt::format("    if (ctx->pc == 0x{:X}u) {{\n", delayPc);

            if (hasValidDelaySlot)
            {
                ss << fmt::format("        ctx->pc = 0x{:X}u;\n", delayPc);
                ss << "        " << delaySlotCode << "\n";
            }

            ss << fmt::format("        ctx->pc = 0x{:X}u;\n", fallthroughPc);

            if (internalTargets.contains(fallthroughPc))
            {
                ss << fmt::format("        goto label_{:x};\n", fallthroughPc); // label uses lowercase usually, but let's keep consistency. Labels are case insensitive in C but check expectation.
            }
            else
            {
                ss << fmt::format("        goto label_fallthrough_0x{:x};\n", branchPc);
            }

            ss << "    }\n";
        }

        ss << fmt::format("    ctx->pc = 0x{:X}u;\n", branchPc);

        // -------------------------
        // J / JAL (static jump)
        // -------------------------
        if (branchInst.opcode == OPCODE_J || branchInst.opcode == OPCODE_JAL)
        {
            if (branchInst.opcode == OPCODE_JAL)
            {
                ss << fmt::format("    SET_GPR_U32(ctx, 31, 0x{:X}u);\n", fallthroughPc);
            }

            if (hasValidDelaySlot)
            {
                ss << fmt::format("    ctx->pc = 0x{:X}u;\n", delayPc);
                ss << "    " << delaySlotPrefix << delaySlotCode << delaySlotSuffix << "\n";
            }

            const uint32_t target = buildAbsoluteJumpTarget(branchInst.address, branchInst.target);

            if (internalTargets.contains(target))
            {
                ss << fmt::format("    ctx->pc = 0x{:X}u;\n", target);
                ss << fmt::format("    goto label_{:x};\n", target);
            }
            else
            {
                std::string funcName = getFunctionName(target);
                ss << fmt::format("    ctx->pc = 0x{:X}u;\n", target);

                if (!funcName.empty())
                {
                    ss << fmt::format("    if (runtime->hasFunction(0x{:X}u)) {{\n", target);
                    ss << fmt::format("        auto targetFn = runtime->lookupFunction(0x{:X}u);\n", target);
                    if (branchInst.opcode == OPCODE_J)
                    {
                        ss << "        targetFn(rdram, ctx, runtime); return;\n";
                    }
                    else
                    {
                        ss << "        const uint32_t __entryPc = ctx->pc;\n";
                        ss << "        targetFn(rdram, ctx, runtime);\n";
                        ss << fmt::format("        if (ctx->pc == __entryPc) {{ ctx->pc = 0x{:X}u; }}\n", fallthroughPc);
                        ss << fmt::format("        if (ctx->pc != 0x{:X}u) {{ return; }}\n", fallthroughPc);
                    }
                    ss << "    } else {\n";
                    
                    if (branchInst.opcode == OPCODE_J)
                    {
                        ss << "        " << funcName << "(rdram, ctx, runtime); return;\n";
                    }
                    else
                    {
                        ss << "        const uint32_t __entryPc = ctx->pc;\n";
                        ss << "        " << funcName << "(rdram, ctx, runtime);\n";
                        ss << fmt::format("        if (ctx->pc == __entryPc) {{ ctx->pc = 0x{:X}u; }}\n", fallthroughPc);
                        ss << fmt::format("        if (ctx->pc != 0x{:X}u) {{ return; }}\n", fallthroughPc);
                    }
                    ss << "    }\n";
                }
                else
                {
                    bool emittedRelocCall = false;
                    const auto relocIt = m_relocationCallNames.find(branchInst.address);
                    if (relocIt != m_relocationCallNames.end() && !relocIt->second.empty())
                    {
                        const std::string_view resolvedSyscallName =
                            ps2_runtime_calls::resolveSyscallName(relocIt->second);
                        const std::string_view resolvedStubName =
                            ps2_runtime_calls::resolveStubName(relocIt->second);

                        if (!resolvedSyscallName.empty() || !resolvedStubName.empty())
                        {
                            const bool isSyscall = !resolvedSyscallName.empty();
                            const std::string_view handlerName = isSyscall ? resolvedSyscallName : resolvedStubName;

                            ss << "    {\n";
                            ss << "        const uint32_t __entryPc = ctx->pc;\n";
                            ss << "        "
                               << (isSyscall ? "ps2_syscalls::" : "ps2_stubs::")
                               << handlerName << "(rdram, ctx, runtime);\n";
                            ss << "        if (ctx->pc == __entryPc) { ctx->pc = getRegU32(ctx, 31); }\n";
                            ss << "    }\n";
                            if (branchInst.opcode == OPCODE_J)
                            {
                                ss << "    return;\n";
                            }
                            else
                            {
                                ss << fmt::format("    if (ctx->pc != 0x{:X}u) {{ return; }}\n", fallthroughPc);
                            }
                            emittedRelocCall = true;
                        }
                    }

                    if (!emittedRelocCall)
                    {
                        ss << "    {\n";
                        ss << fmt::format("        auto targetFn = runtime->lookupFunction(0x{:X}u);\n", target);
                        ss << "        const uint32_t __entryPc = ctx->pc;\n";
                        ss << "        targetFn(rdram, ctx, runtime);\n";
                        if (branchInst.opcode == OPCODE_J)
                        {
                            ss << "        return;\n";
                        }
                        else
                        {
                            ss << fmt::format("        if (ctx->pc == __entryPc) {{ ctx->pc = 0x{:X}u; }}\n", fallthroughPc);
                            ss << fmt::format("        if (ctx->pc != 0x{:X}u) {{ return; }}\n", fallthroughPc);
                        }
                        ss << "    }\n";
                    }
                }
            }
        }
        // -------------------------
        // JR / JALR (register jump)
        // -------------------------
        else if (branchInst.opcode == OPCODE_SPECIAL &&
                 (branchInst.function == SPECIAL_JR || branchInst.function == SPECIAL_JALR))
        {
            ss << "    {\n";
            ss << "        uint32_t jumpTarget = GPR_U32(ctx, " << static_cast<int>(rs_reg) << ");\n";

            if (branchInst.function == SPECIAL_JALR && rd_reg != 0)
            {
                ss << fmt::format("        SET_GPR_U32(ctx, {}, 0x{:X}u);\n", rd_reg, fallthroughPc);
            }

            if (hasValidDelaySlot)
            {
                ss << fmt::format("        ctx->pc = 0x{:X}u;\n", delayPc);
                ss << "        " << delaySlotPrefix << delaySlotCode << delaySlotSuffix << "\n";
            }

            if (branchInst.function == SPECIAL_JALR)
            {
                ss << "        if (jumpTarget == 0u) {\n";
                ss << fmt::format("            ctx->pc = 0x{:X}u;\n", fallthroughPc);
                ss << "        } else {\n";
            }

            ss << "        ctx->pc = jumpTarget;\n";

            if (!sortedInternalTargets.empty())
            {
                ss << "        switch (jumpTarget) {\n";
                for (uint32_t t : sortedInternalTargets)
                {
                    ss << fmt::format("            case 0x{:X}u: goto label_{:x};\n", t, t);
                }
                ss << "            default: break;\n";
                ss << "        }\n";
            }

            if (branchInst.function == SPECIAL_JR)
            {
                ss << "        return;\n";
            }
            else
            {
                ss << "        {\n";
                ss << "            auto targetFn = runtime->lookupFunction(jumpTarget);\n";
                ss << "            const uint32_t __entryPc = ctx->pc;\n";
                ss << "            targetFn(rdram, ctx, runtime);\n";
                ss << fmt::format("            if (ctx->pc == __entryPc) {{ ctx->pc = 0x{:X}u; }}\n", fallthroughPc);
                ss << fmt::format("            if (ctx->pc != 0x{:X}u) {{ return; }}\n", fallthroughPc);
                ss << "        }\n";
            }

            if (branchInst.function == SPECIAL_JALR)
            {
                ss << "        }\n";
            }

            ss << "    }\n";
        }
        // -------------------------
        // Conditional Branches
        // -------------------------
        else if (branchInst.isBranch)
        {
            std::string conditionStr = "false";
            std::string conditionalLinkCode;
            std::string unconditionalLinkCode;

            switch (branchInst.opcode)
            {
            case OPCODE_BEQ:
                conditionStr = fmt::format("GPR_U64(ctx, {}) == GPR_U64(ctx, {})", rs_reg, rt_reg);
                break;
            case OPCODE_BNE:
                conditionStr = fmt::format("GPR_U64(ctx, {}) != GPR_U64(ctx, {})", rs_reg, rt_reg);
                break;
            case OPCODE_BLEZ:
                conditionStr = fmt::format("GPR_S32(ctx, {}) <= 0", rs_reg);
                break;
            case OPCODE_BGTZ:
                conditionStr = fmt::format("GPR_S32(ctx, {}) > 0", rs_reg);
                break;
            case OPCODE_BEQL:
                conditionStr = fmt::format("GPR_U64(ctx, {}) == GPR_U64(ctx, {})", rs_reg, rt_reg);
                break;
            case OPCODE_BNEL:
                conditionStr = fmt::format("GPR_U64(ctx, {}) != GPR_U64(ctx, {})", rs_reg, rt_reg);
                break;
            case OPCODE_BLEZL:
                conditionStr = fmt::format("GPR_S32(ctx, {}) <= 0", rs_reg);
                break;
            case OPCODE_BGTZL:
                conditionStr = fmt::format("GPR_S32(ctx, {}) > 0", rs_reg);
                break;
            case OPCODE_REGIMM:
                switch (rt_reg)
                {
                case REGIMM_BLTZ:
                    conditionStr = fmt::format("GPR_S32(ctx, {}) < 0", rs_reg);
                    break;
                case REGIMM_BGEZ:
                    conditionStr = fmt::format("GPR_S32(ctx, {}) >= 0", rs_reg);
                    break;
                case REGIMM_BLTZL:
                    conditionStr = fmt::format("GPR_S32(ctx, {}) < 0", rs_reg);
                    break;
                case REGIMM_BGEZL:
                    conditionStr = fmt::format("GPR_S32(ctx, {}) >= 0", rs_reg);
                    break;
                case REGIMM_BLTZAL:
                    conditionStr = fmt::format("GPR_S32(ctx, {}) < 0", rs_reg);
                    unconditionalLinkCode = fmt::format("SET_GPR_U32(ctx, 31, 0x{:X}u);", fallthroughPc);
                    break;
                case REGIMM_BGEZAL:
                    conditionStr = fmt::format("GPR_S32(ctx, {}) >= 0", rs_reg);
                    unconditionalLinkCode = fmt::format("SET_GPR_U32(ctx, 31, 0x{:X}u);", fallthroughPc);
                    break;
                case REGIMM_BLTZALL:
                    conditionStr = fmt::format("GPR_S32(ctx, {}) < 0", rs_reg);
                    conditionalLinkCode = fmt::format("SET_GPR_U32(ctx, 31, 0x{:X}u);", fallthroughPc);
                    break;
                case REGIMM_BGEZALL:
                    conditionStr = fmt::format("GPR_S32(ctx, {}) >= 0", rs_reg);
                    conditionalLinkCode = fmt::format("SET_GPR_U32(ctx, 31, 0x{:X}u);", fallthroughPc);
                    break;
                default:
                    break;
                }
                break;
            case OPCODE_COP1:
                if (branchInst.rs == COP1_BC)
                {
                    const uint8_t bc_cond = branchInst.rt;
                    conditionStr = (bc_cond == COP1_BC_BCF || bc_cond == COP1_BC_BCFL)
                                       ? "!(ctx->fcr31 & 0x800000)"
                                       : "(ctx->fcr31 & 0x800000)";
                }
                break;
            case OPCODE_COP2:
                if (branchInst.rs == COP2_BC)
                {
                    const uint8_t bc_cond = branchInst.rt;
                    conditionStr = (bc_cond == COP2_BC_BCF || bc_cond == COP2_BC_BCFL)
                                       ? "!(ctx->vu0_status & 0x1)"
                                       : "(ctx->vu0_status & 0x1)";
                }
                break;
            default:
                break;
            }

            const int32_t offsetBytes = (static_cast<int32_t>(static_cast<int16_t>(branchInst.simmediate)) << 2);
            const uint32_t target = static_cast<uint32_t>(
                static_cast<int64_t>(branchInst.address + 4u) + static_cast<int64_t>(offsetBytes));

            const bool isLikely =
                (branchInst.opcode == OPCODE_BEQL || branchInst.opcode == OPCODE_BNEL ||
                 branchInst.opcode == OPCODE_BLEZL || branchInst.opcode == OPCODE_BGTZL ||
                 (branchInst.opcode == OPCODE_REGIMM &&
                  (branchInst.rt == REGIMM_BLTZL || branchInst.rt == REGIMM_BGEZL ||
                   branchInst.rt == REGIMM_BLTZALL || branchInst.rt == REGIMM_BGEZALL)) ||
                 (branchInst.opcode == OPCODE_COP1 && branchInst.rs == COP1_BC &&
                  (branchInst.rt == COP1_BC_BCFL || branchInst.rt == COP1_BC_BCTL)) ||
                 (branchInst.opcode == OPCODE_COP2 && branchInst.rs == COP2_BC &&
                  (branchInst.rt == COP2_BC_BCFL || branchInst.rt == COP2_BC_BCTL)));

            const std::string branchTakenVar = fmt::format("branch_taken_0x{:x}", branchInst.address);
            ss << "    {\n";
            ss << "        const bool " << branchTakenVar << " = (" << conditionStr << ");\n";

            if (!unconditionalLinkCode.empty())
            {
                ss << "        " << unconditionalLinkCode << "\n";
            }

            if (isLikely)
            {
                ss << "        if (" << branchTakenVar << ") {\n";
                if (!conditionalLinkCode.empty())
                {
                    ss << "            " << conditionalLinkCode << "\n";
                }
                if (hasValidDelaySlot)
                {
                    ss << fmt::format("            ctx->pc = 0x{:X}u;\n", delayPc);
                    ss << "            " << delaySlotPrefix << delaySlotCode << delaySlotSuffix << "\n";
                }

                if (internalTargets.contains(target))
                {
                    ss << fmt::format("            ctx->pc = 0x{:X}u;\n", target);
                    ss << fmt::format("            goto label_{:x};\n", target);
                }
                else
                {
                    ss << fmt::format("            ctx->pc = 0x{:X}u;\n", target);
                    ss << "            return;\n";
                }

                ss << "        }\n";
            }
            else
            {
                if (!conditionalLinkCode.empty())
                {
                    ss << "        if (" << branchTakenVar << ") { " << conditionalLinkCode << " }\n";
                }

                if (hasValidDelaySlot)
                {
                    ss << fmt::format("        ctx->pc = 0x{:X}u;\n", delayPc);
                    ss << "        " << delaySlotPrefix << delaySlotCode << delaySlotSuffix << "\n";
                }

                ss << "        if (" << branchTakenVar << ") {\n";
                if (internalTargets.contains(target))
                {
                    ss << fmt::format("            ctx->pc = 0x{:X}u;\n", target);
                    ss << fmt::format("            goto label_{:x};\n", target);
                }
                else
                {
                    ss << fmt::format("            ctx->pc = 0x{:X}u;\n", target);
                    ss << "            return;\n";
                }
                ss << "        }\n";
            }

            ss << "    }\n";
        }
        else
        {
            ss << "    " << translateInstruction(branchInst) << "\n";
            if (hasValidDelaySlot)
            {
                ss << fmt::format("    ctx->pc = 0x{:X}u;\n", delayPc);
                ss << "    " << delaySlotPrefix << delaySlotCode << delaySlotSuffix << "\n";
            }
        }

        if (internalTargets.contains(delayPc) && !internalTargets.contains(fallthroughPc))
        {
            ss << fmt::format("label_fallthrough_0x{:x}:\n", branchPc);
        }

        ss << fmt::format("    ctx->pc = 0x{:X}u;\n", fallthroughPc);

        return ss.str();
    }

    CodeGenerator::~CodeGenerator() = default;

    CodeGenerator::AnalysisResult CodeGenerator::collectInternalBranchTargets(
        const Function &function, const std::vector<Instruction> &instructions)
    {
        AnalysisResult result;
        std::unordered_set<uint32_t> instructionAddresses;
        instructionAddresses.reserve(instructions.size());
        bool hasIndirectRegisterJump = false;
        std::vector<const Instruction*> indirectJumps;

        for (const auto &inst : instructions)
        {
            instructionAddresses.insert(inst.address);
            if (inst.opcode == OPCODE_SPECIAL &&
                ((inst.function == SPECIAL_JR && inst.rs != 31) ||
                 inst.function == SPECIAL_JALR))
            {
                hasIndirectRegisterJump = true;
                indirectJumps.push_back(&inst);
            }
        }

        for (const auto &inst : instructions)
        {
            bool isStaticJump = (inst.opcode == OPCODE_J || inst.opcode == OPCODE_JAL);
            if (inst.isBranch && inst.opcode != OPCODE_J && inst.opcode != OPCODE_JAL)
            {
                const int32_t offsetBytes = (static_cast<int32_t>(static_cast<int16_t>(inst.simmediate)) << 2);
                const uint32_t target = static_cast<uint32_t>(
                    static_cast<int64_t>(inst.address + 4u) + static_cast<int64_t>(offsetBytes));

                if (target >= function.start && target < function.end &&
                    instructionAddresses.contains(target))
                {
                    result.entryPoints.insert(target);
                }
            }
            else if (isStaticJump)
            {
                uint32_t target = buildAbsoluteJumpTarget(inst.address, inst.target);
                if (target >= function.start && target < function.end &&
                    instructionAddresses.contains(target))
                {
                    result.entryPoints.insert(target);

                    if (inst.opcode == OPCODE_JAL)
                    {
                        uint32_t returnAddr = inst.address + 8;
                        if (returnAddr >= function.start && returnAddr < function.end &&
                            instructionAddresses.contains(returnAddr))
                        {
                            result.entryPoints.insert(returnAddr);
                        }
                    }
                }
            }
        }

        if (hasIndirectRegisterJump)
        {
            bool needsJrFallback = false;
            for (const Instruction* jrInst : indirectJumps) {
                bool foundTable = false;
                
                uint32_t jrReg = jrInst->rs;
                
                int lwIndex = -1;
                uint32_t baseReg = 0;
                int32_t lwOffset = 0;
                
                auto it = std::find_if(instructions.begin(), instructions.end(), [&](const Instruction& inst) { return inst.address == jrInst->address; });
                if (it != instructions.end()) {
                    int jrIndex = std::distance(instructions.begin(), it);
                    for (int i = jrIndex - 1; i >= 0 && i >= jrIndex - 20; --i) {
                        const auto& inst = instructions[i];
                        if ((inst.opcode == OPCODE_LW || inst.opcode == OPCODE_LWU) && inst.rt == jrReg) {
                            lwIndex = i;
                            baseReg = inst.rs;
                            lwOffset = inst.simmediate;
                            break;
                        }
                    }
                    
                    if (lwIndex != -1) {
                        int adduIndex = -1;
                        uint32_t tableBaseReg = 0;
                        uint32_t indexReg = 0;
                        for (int i = lwIndex - 1; i >= 0 && i >= lwIndex - 10; --i) {
                            const auto& inst = instructions[i];
                            if (inst.opcode == OPCODE_SPECIAL && inst.function == SPECIAL_ADDU && inst.rd == baseReg) {
                                adduIndex = i;
                                tableBaseReg = inst.rs;  
                                indexReg = inst.rt;
                                break;
                            }
                        }
                        
                        uint32_t tableAddress = 0;
                        bool foundTableAddress = false;

                        if (adduIndex != -1) {
                            for (int i = adduIndex - 1; i >= 0 && i >= adduIndex - 20; --i) {
                                const auto& inst = instructions[i];
                                if (inst.opcode == OPCODE_LUI) {
                                    if (inst.rt == tableBaseReg || inst.rt == indexReg) {
                                        uint32_t high = inst.immediate << 16;
                                        uint32_t low = 0;
                                        for (int j = i + 1; j < adduIndex; ++j) {
                                            const auto& lowInst = instructions[j];
                                            if (lowInst.rs == inst.rt && lowInst.rt == inst.rt) {
                                                if (lowInst.opcode == OPCODE_ADDIU) {
                                                    low = (uint32_t)lowInst.simmediate;
                                                } else if (lowInst.opcode == OPCODE_ORI) {
                                                    low = lowInst.immediate;
                                                }
                                            }
                                        }
                                        tableAddress = high + low;
                                        foundTableAddress = true;
                                        break;
                                    }
                                }
                            }
                        }
                        
                        if (foundTableAddress) {
                            tableAddress += lwOffset;

                            const auto configuredTableIt = m_configJumpTableTargetsByAddress.find(tableAddress);
                            if (configuredTableIt != m_configJumpTableTargetsByAddress.end())
                            {
                                std::vector<uint32_t> jrTargets;
                                jrTargets.reserve(configuredTableIt->second.size());
                                for (uint32_t target : configuredTableIt->second)
                                {
                                    if (target >= function.start && target < function.end &&
                                        instructionAddresses.contains(target))
                                    {
                                        jrTargets.push_back(target);
                                    }
                                }

                                if (!jrTargets.empty())
                                {
                                    std::sort(jrTargets.begin(), jrTargets.end());
                                    jrTargets.erase(std::unique(jrTargets.begin(), jrTargets.end()), jrTargets.end());
                                    result.jumpTableTargets[jrInst->address] = jrTargets;
                                    for (uint32_t target : jrTargets)
                                    {
                                        result.entryPoints.insert(target);
                                    }
                                    foundTable = true;
                                }
                            }

                            uint32_t unshiftedIndexReg = 0;
                            for (int i = adduIndex - 1; i >= 0 && i >= adduIndex - 10; --i) {
                                const auto& inst = instructions[i];
                                if (inst.opcode == OPCODE_SPECIAL && inst.function == SPECIAL_SLL && (inst.rd == tableBaseReg || inst.rd == indexReg)) {
                                    unshiftedIndexReg = inst.rt;
                                    break;
                                }
                            }

                            uint32_t numCases = 0;
                            if (unshiftedIndexReg != 0) {
                                for (int i = adduIndex - 1; i >= 0 && i >= adduIndex - 30; --i) {
                                    const auto& inst = instructions[i];
                                    if ((inst.opcode == OPCODE_SLTIU || inst.opcode == OPCODE_SLTI) && inst.rs == unshiftedIndexReg) {
                                        numCases = inst.immediate; 
                                        break;
                                    }
                                }
                            }
                            
                            if (!foundTable && numCases > 0 && numCases <= 1000) {
                                const Section* rodata = nullptr;
                                for (const auto& sec : m_sections) {
                                    if (tableAddress >= sec.address && tableAddress < sec.address + sec.size) {
                                        rodata = &sec;
                                        break;
                                    }
                                }

                                if (rodata && rodata->data) {
                                    std::vector<uint32_t> jrTargets;
                                    bool validJumpTable = true;
                                    std::unordered_set<uint32_t> uniqueTargets;
                                    for (uint32_t i = 0; i < numCases; ++i) {
                                        uint32_t addr = tableAddress + i * 4;
                                        if (addr >= rodata->address && addr + 4 <= rodata->address + rodata->size) {
                                            uint32_t target = 0;
                                            std::memcpy(&target, rodata->data + (addr - rodata->address), 4);
                                            if (target >= function.start && target < function.end && instructionAddresses.contains(target)) {
                                                if (!uniqueTargets.contains(target)) {
                                                    jrTargets.push_back(target);
                                                    uniqueTargets.insert(target);
                                                }
                                            }
                                        } else {
                                            validJumpTable = false;
                                            break;
                                        }
                                    }
                                    if (validJumpTable && !jrTargets.empty()) {
                                        result.jumpTableTargets[jrInst->address] = jrTargets;
                                        for (uint32_t t : jrTargets) {
                                            result.entryPoints.insert(t);
                                        }
                                        foundTable = true;
                                    }
                                }
                            }
                        }
                    }
                }
                if (!foundTable) {
                    if (!(jrInst->function == SPECIAL_JALR))
                    {
                        needsJrFallback = true;
                    }
                }
            }

            if (needsJrFallback) {
                for (uint32_t addr : instructionAddresses)
                {
                    if (addr >= function.start && addr < function.end)
                    {
                        result.entryPoints.insert(addr);
                    }
                }
            }
        }

        return result;
    }

    std::string ps2recomp::CodeGenerator::generateFunction(
        const Function &function,
        const std::vector<Instruction> &instructions,
        const bool &useHeaders)
    {
        std::stringstream ss;

        if (useHeaders)
        {
            ss << "#include \"ps2_runtime_macros.h\"\n";
            ss << "#include \"ps2_runtime.h\"\n";
            ss << "#include \"ps2_recompiled_functions.h\"\n";
            ss << "#include \"ps2_recompiled_stubs.h\"\n\n";
            ss << "#include \"ps2_syscalls.h\"\n";
            ss << "#include \"ps2_stubs.h\"\n\n";
            ss << "#ifdef PS2_FUNCTION_LOG_TRACKER\n";
            ss << "#include \"ps2_log.h\"\n";
            ss << "#endif\n\n";
        }

        AnalysisResult analysisResult = collectInternalBranchTargets(function, instructions);
        const std::unordered_set<uint32_t>& internalTargets = analysisResult.entryPoints;
        ss << "// Function: " << function.name << "\n";
        ss << "// Address: 0x" << std::hex << function.start << " - 0x" << function.end << std::dec << "\n";

        std::string sanitizedName = getFunctionName(function.start);
        if (sanitizedName.empty())
        {
            std::stringstream nameBuilder;
            nameBuilder << "Errorfunc_" << std::hex << function.start;
            sanitizedName = nameBuilder.str();
        }

        ss << "void " << sanitizedName << "(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {\n";
        ss << "#ifdef PS2_FUNCTION_LOG_TRACKER\n";
        ss << "    PS_LOG_ENTRY(\"" << sanitizedName << "\");\n";
        ss << "#endif\n";
        ss << "\n";
        ss << "    ctx->pc = 0x" << std::hex << function.start << "u;\n"
           << std::dec;
        ss << "\n";

        for (size_t i = 0; i < instructions.size(); ++i)
        {
            const Instruction &inst = instructions[i];

            if (internalTargets.contains(inst.address))
            {
                ss << "label_" << std::hex << inst.address << std::dec << ":\n";
            }

            ss << "    // 0x" << std::hex << inst.address << ": 0x" << inst.raw << std::dec;
            if (!inst.disassembly.empty()) {
                ss << "  " << inst.disassembly;
            }
            ss << "\n";

            try
            {
                if (inst.hasDelaySlot && i + 1 < instructions.size())
                {
                    const Instruction &delaySlot = instructions[i + 1];

                    if (internalTargets.contains(delaySlot.address))
                    {
                        ss << "label_" << std::hex << delaySlot.address << std::dec << ":\n";
                    }

                    ss << handleBranchDelaySlots(inst, delaySlot, function, analysisResult);

                    ++i; // Skip delay slot instruction (handled inside branch logic)
                }
                else
                {
                    ss << "    ctx->pc = 0x" << std::hex << inst.address << "u;\n"
                       << std::dec;

                    ss << "    " << translateInstruction(inst);
                    if (inst.isMmio)
                    {
                        ss << " // MMIO: 0x" << std::hex << inst.mmioAddress << std::dec;
                    }
                    ss << "\n";
                }
            }
            catch (const std::exception &e)
            {
                std::cerr << "Error in CodeGenerator::generateFunction while translating instruction\n"
                          << "  Function: " << function.name << "\n"
                          << "  Start: 0x" << std::hex << function.start << "\n"
                          << "  Instruction address: 0x" << inst.address << "\n"
                          << "  Raw: 0x" << inst.raw << "\n"
                          << "  What: " << e.what() << std::endl;

                throw;
            }
        }

        ss << "}\n";
        return ss.str();
    }

    std::string CodeGenerator::translateInstruction(const Instruction &inst)
    {
        if (inst.isMMI)
        {
            return translateMMIInstruction(inst);
        }

        auto genRead = [&](int width, const std::string &addr)
        {
            if (inst.isMmio)
            {
                return fmt::format("runtime->Load{}(rdram, ctx, {})", width, addr);
            }
            return fmt::format("READ{}({})", width, addr);
        };

        auto genWrite = [&](int width, const std::string &addr, const std::string &val)
        {
            if (inst.isMmio)
            {
                return fmt::format("runtime->Store{}(rdram, ctx, {}, {})", width, addr, val);
            }
            return fmt::format("WRITE{}({}, {})", width, addr, val);
        };

        switch (inst.opcode)
        {
        case OPCODE_SPECIAL:
            return translateSpecialInstruction(inst);
        case OPCODE_REGIMM:
            return translateRegimmInstruction(inst);
        case OPCODE_COP0:
            return translateCOP0Instruction(inst);
        case OPCODE_COP1:
            return translateFPUInstruction(inst);
        case OPCODE_COP2:
            return translateVUInstruction(inst);
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
            return fmt::format("// Unhandled opcode: 0x{:X}", inst.opcode);
        }
    }

    std::string CodeGenerator::translateSpecialInstruction(const Instruction &inst)
    {
        switch (inst.function)
        {
        case SPECIAL_SLL:
            if (inst.rd == 0 && inst.rt == 0 && inst.sa == 0)
                return "// NOP";
            if (inst.rd == 0)
                return "";
            return fmt::format("SET_GPR_S32(ctx, {}, (int32_t)SLL32(GPR_U32(ctx, {}), {}));", inst.rd, inst.rt, inst.sa);
        case SPECIAL_SRL:
            return fmt::format("SET_GPR_S32(ctx, {}, (int32_t)SRL32(GPR_U32(ctx, {}), {}));", inst.rd, inst.rt, inst.sa);
        case SPECIAL_SRA:
            return fmt::format("SET_GPR_S32(ctx, {}, SRA32(GPR_S32(ctx, {}), {}));", inst.rd, inst.rt, inst.sa);
        case SPECIAL_SLLV:
            return fmt::format("SET_GPR_S32(ctx, {}, (int32_t)SLL32(GPR_U32(ctx, {}), GPR_U32(ctx, {}) & 0x1F));", inst.rd, inst.rt, inst.rs);
        case SPECIAL_SRLV:
            return fmt::format("SET_GPR_S32(ctx, {}, (int32_t)SRL32(GPR_U32(ctx, {}), GPR_U32(ctx, {}) & 0x1F));", inst.rd, inst.rt, inst.rs);
        case SPECIAL_SRAV:
            return fmt::format("SET_GPR_S32(ctx, {}, SRA32(GPR_S32(ctx, {}), GPR_U32(ctx, {}) & 0x1F));", inst.rd, inst.rt, inst.rs);
        case SPECIAL_JR:
            return fmt::format("// JR ${} - Handled by branch logic", inst.rs);
        case SPECIAL_JALR:
            return fmt::format("// JALR ${}, ${} - Handled by branch logic", inst.rd, inst.rs);
        case SPECIAL_SYSCALL:
            return fmt::format("runtime->handleSyscall(rdram, ctx, 0x{:X}u);", (inst.raw >> 6) & 0xFFFFFu);
        case SPECIAL_BREAK:
            return fmt::format("runtime->handleBreak(rdram, ctx);");
        case SPECIAL_SYNC:
            return "// SYNC instruction - memory barrier\n// In recompiled code, we don't need explicit memory barriers";
        case SPECIAL_MFHI:
            return fmt::format("SET_GPR_U64(ctx, {}, ctx->hi);", inst.rd);
        case SPECIAL_MTHI:
            return fmt::format("ctx->hi = GPR_U64(ctx, {});", inst.rs);
        case SPECIAL_MFLO:
            return fmt::format("SET_GPR_U64(ctx, {}, ctx->lo);", inst.rd);
        case SPECIAL_MTLO:
            return fmt::format("ctx->lo = GPR_U64(ctx, {});", inst.rs);
        case SPECIAL_MULT:
            if (inst.rd != 0)
            {
                return fmt::format("{{ int64_t result = (int64_t)GPR_S32(ctx, {}) * (int64_t)GPR_S32(ctx, {}); ctx->lo = (uint64_t)(int64_t)(int32_t)result; ctx->hi = (uint64_t)(int64_t)(int32_t)(result >> 32); SET_GPR_S32(ctx, {}, (int32_t)result); }}", inst.rs, inst.rt, inst.rd);
            }
            return fmt::format("{{ int64_t result = (int64_t)GPR_S32(ctx, {}) * (int64_t)GPR_S32(ctx, {}); ctx->lo = (uint64_t)(int64_t)(int32_t)result; ctx->hi = (uint64_t)(int64_t)(int32_t)(result >> 32); }}", inst.rs, inst.rt);
        case SPECIAL_MULTU:
            if (inst.rd != 0)
            {
                return fmt::format("{{ uint64_t result = (uint64_t)GPR_U32(ctx, {}) * (uint64_t)GPR_U32(ctx, {}); ctx->lo = (uint64_t)(int64_t)(int32_t)result; ctx->hi = (uint64_t)(int64_t)(int32_t)(result >> 32); SET_GPR_S32(ctx, {}, (int32_t)result); }}", inst.rs, inst.rt, inst.rd);
            }
            return fmt::format("{{ uint64_t result = (uint64_t)GPR_U32(ctx, {}) * (uint64_t)GPR_U32(ctx, {}); ctx->lo = (uint64_t)(int64_t)(int32_t)result; ctx->hi = (uint64_t)(int64_t)(int32_t)(result >> 32); }}", inst.rs, inst.rt);
        case SPECIAL_DIV:
            return fmt::format("{{ int32_t divisor = GPR_S32(ctx, {}); "
                               "   int32_t dividend = GPR_S32(ctx, {}); "
                               "   if (divisor != 0) {{ "
                               "       if (divisor == -1 && dividend == INT32_MIN) {{ "
                               "           ctx->lo = (uint64_t)(int64_t)INT32_MIN; ctx->hi = 0; "
                               "       }} else {{ "
                               "           ctx->lo = (uint64_t)(int64_t)(dividend / divisor); "
                               "           ctx->hi = (uint64_t)(int64_t)(dividend % divisor); "
                               "       }} "
                               "   }} else {{ "
                               "       ctx->lo = (dividend < 0) ? 1ull : 0xFFFFFFFFFFFFFFFFull; ctx->hi = (uint64_t)(int64_t)dividend; "
                               "   }} }}",
                               inst.rt, inst.rs);
        case SPECIAL_DIVU:
            return fmt::format("{{ uint32_t divisor = GPR_U32(ctx, {}); if (divisor != 0) {{ ctx->lo = (uint64_t)(int64_t)(int32_t)(GPR_U32(ctx, {}) / divisor); ctx->hi = (uint64_t)(int64_t)(int32_t)(GPR_U32(ctx, {}) % divisor); }} else {{ ctx->lo = 0xFFFFFFFFFFFFFFFFull; ctx->hi = (uint64_t)(int64_t)(int32_t)GPR_U32(ctx,{}); }} }}", inst.rt, inst.rs, inst.rs, inst.rs);
        case SPECIAL_ADD:
            return fmt::format(
                "{{ "
                "    int32_t rs_val = GPR_S32(ctx, {}); "
                "    int32_t rt_val = GPR_S32(ctx, {}); "
                "    int64_t result = (int64_t)rs_val + (int64_t)rt_val; "
                "    if (result > INT32_MAX || result < INT32_MIN) {{ "
                "        runtime->SignalException(ctx, EXCEPTION_INTEGER_OVERFLOW); "
                "    }} else {{ "
                "        SET_GPR_S32(ctx, {}, (int32_t)result); "
                "    }} "
                "}}",
                inst.rs, inst.rt, inst.rd);
        case SPECIAL_ADDU:
            return fmt::format("SET_GPR_S32(ctx, {}, (int32_t)ADD32(GPR_U32(ctx, {}), GPR_U32(ctx, {})));", inst.rd, inst.rs, inst.rt);
        case SPECIAL_SUB:
            return fmt::format(
                "{{ uint32_t tmp; bool ov; "
                "SUB32_OV(GPR_U32(ctx, {}), GPR_U32(ctx, {}), tmp, ov); "
                "if (ov) runtime->SignalException(ctx, EXCEPTION_INTEGER_OVERFLOW); "
                "else SET_GPR_S32(ctx, {}, (int32_t)tmp); }}",
                inst.rs, inst.rt, inst.rd);
        case SPECIAL_SUBU:
            return fmt::format("SET_GPR_S32(ctx, {}, (int32_t)SUB32(GPR_U32(ctx, {}), GPR_U32(ctx, {})));", inst.rd, inst.rs, inst.rt);
        case SPECIAL_AND:
            return fmt::format("SET_GPR_U64(ctx, {}, GPR_U64(ctx, {}) & GPR_U64(ctx, {}));", inst.rd, inst.rs, inst.rt);
        case SPECIAL_OR:
            return fmt::format("SET_GPR_U64(ctx, {}, GPR_U64(ctx, {}) | GPR_U64(ctx, {}));", inst.rd, inst.rs, inst.rt);
        case SPECIAL_XOR:
            return fmt::format("SET_GPR_U64(ctx, {}, GPR_U64(ctx, {}) ^ GPR_U64(ctx, {}));", inst.rd, inst.rs, inst.rt);
        case SPECIAL_NOR:
            return fmt::format("SET_GPR_U64(ctx, {}, ~(GPR_U64(ctx, {}) | GPR_U64(ctx, {})));", inst.rd, inst.rs, inst.rt);
        case SPECIAL_SLT:
            return fmt::format("SET_GPR_U64(ctx, {}, ((int64_t)GPR_S64(ctx, {}) < (int64_t)GPR_S64(ctx, {})) ? 1 : 0);", inst.rd, inst.rs, inst.rt);
        case SPECIAL_SLTU:
            return fmt::format("SET_GPR_U64(ctx, {}, ((uint64_t)GPR_U64(ctx, {}) < (uint64_t)GPR_U64(ctx, {})) ? 1 : 0);", inst.rd, inst.rs, inst.rt);
        case SPECIAL_MOVZ:
            return fmt::format("if (GPR_U64(ctx, {}) == 0) SET_GPR_VEC(ctx, {}, GPR_VEC(ctx, {}));", inst.rt, inst.rd, inst.rs);
        case SPECIAL_MOVN:
            return fmt::format("if (GPR_U64(ctx, {}) != 0) SET_GPR_VEC(ctx, {}, GPR_VEC(ctx, {}));", inst.rt, inst.rd, inst.rs);
        case SPECIAL_MFSA:
            return fmt::format("SET_GPR_U32(ctx, {}, ctx->sa);", inst.rd);
        case SPECIAL_MTSA:
            return fmt::format("ctx->sa = GPR_U32(ctx, {}) & 0x7F;", inst.rs);
        case SPECIAL_DADD:
            return fmt::format(
                "{{ int64_t a = (int64_t)GPR_S64(ctx, {}); "
                "int64_t b = (int64_t)GPR_S64(ctx, {}); "
                "int64_t r = a + b; "
                "if (((a ^ b) >= 0) && ((a ^ r) < 0)) runtime->SignalException(ctx, EXCEPTION_INTEGER_OVERFLOW); "
                "else SET_GPR_S64(ctx, {}, r); }}",
                inst.rs, inst.rt, inst.rd);
        case SPECIAL_DADDU:
            return fmt::format(
                "SET_GPR_U64(ctx, {}, (uint64_t)GPR_U64(ctx, {}) + (uint64_t)GPR_U64(ctx, {}));",
                inst.rd, inst.rs, inst.rt);
        case SPECIAL_DSUB:
            return fmt::format(
                "{{ int64_t a = (int64_t)GPR_S64(ctx, {}); "
                "int64_t b = (int64_t)GPR_S64(ctx, {}); "
                "int64_t r = a - b; "
                "if (((a ^ b) < 0) && ((a ^ r) < 0)) runtime->SignalException(ctx, EXCEPTION_INTEGER_OVERFLOW); "
                "else SET_GPR_S64(ctx, {}, r); }}",
                inst.rs, inst.rt, inst.rd);
        case SPECIAL_DSUBU:
            return fmt::format("SET_GPR_U64(ctx, {}, GPR_U64(ctx, {}) - GPR_U64(ctx, {}));", inst.rd, inst.rs, inst.rt);
        case SPECIAL_DSLL:
            return fmt::format("SET_GPR_U64(ctx, {}, GPR_U64(ctx, {}) << {});", inst.rd, inst.rt, inst.sa);
        case SPECIAL_DSRL:
            return fmt::format("SET_GPR_U64(ctx, {}, GPR_U64(ctx, {}) >> {});", inst.rd, inst.rt, inst.sa);
        case SPECIAL_DSRA:
            return fmt::format("SET_GPR_S64(ctx, {}, GPR_S64(ctx, {}) >> {});", inst.rd, inst.rt, inst.sa);
        case SPECIAL_DSLLV:
            return fmt::format("SET_GPR_U64(ctx, {}, GPR_U64(ctx, {}) << (GPR_U32(ctx, {}) & 0x3F));", inst.rd, inst.rt, inst.rs);
        case SPECIAL_DSRLV:
            return fmt::format("SET_GPR_U64(ctx, {}, GPR_U64(ctx, {}) >> (GPR_U32(ctx, {}) & 0x3F));", inst.rd, inst.rt, inst.rs);
        case SPECIAL_DSRAV:
            return fmt::format("SET_GPR_S64(ctx, {}, GPR_S64(ctx, {}) >> (GPR_U32(ctx, {}) & 0x3F));", inst.rd, inst.rt, inst.rs);
        case SPECIAL_DSLL32:
            return fmt::format("SET_GPR_U64(ctx, {}, GPR_U64(ctx, {}) << (32 + {}));", inst.rd, inst.rt, inst.sa);
        case SPECIAL_DSRL32:
            return fmt::format("SET_GPR_U64(ctx, {}, GPR_U64(ctx, {}) >> (32 + {}));", inst.rd, inst.rt, inst.sa);
        case SPECIAL_DSRA32:
            return fmt::format("SET_GPR_S64(ctx, {}, GPR_S64(ctx, {}) >> (32 + {}));", inst.rd, inst.rt, inst.sa);
        case SPECIAL_TGE:
            return fmt::format("if (GPR_S64(ctx, {}) >= GPR_S64(ctx, {})) {{ runtime->handleTrap(rdram, ctx); }}", inst.rs, inst.rt);
        case SPECIAL_TGEU:
            return fmt::format("if (GPR_U64(ctx, {}) >= GPR_U64(ctx, {})) {{ runtime->handleTrap(rdram, ctx); }}", inst.rs, inst.rt);
        case SPECIAL_TLT:
            return fmt::format("if (GPR_S64(ctx, {}) < GPR_S64(ctx, {})) {{ runtime->handleTrap(rdram, ctx); }}", inst.rs, inst.rt);
        case SPECIAL_TLTU:
            return fmt::format("if (GPR_U64(ctx, {}) < GPR_U64(ctx, {})) {{ runtime->handleTrap(rdram, ctx); }}", inst.rs, inst.rt);
        case SPECIAL_TEQ:
            return fmt::format("if (GPR_U64(ctx, {}) == GPR_U64(ctx, {})) {{ runtime->handleTrap(rdram, ctx); }}", inst.rs, inst.rt);
        case SPECIAL_TNE:
            return fmt::format("if (GPR_U64(ctx, {}) != GPR_U64(ctx, {})) {{ runtime->handleTrap(rdram, ctx); }}", inst.rs, inst.rt);
        default:
            return fmt::format("// Unhandled SPECIAL instruction: 0x{:X}", inst.function);
        }
    }

    std::string CodeGenerator::translateRegimmInstruction(const Instruction &inst)
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
            return fmt::format("// Unhandled REGIMM instruction: 0x{:X}", inst.rt);
        }
    }

    std::string CodeGenerator::translateCOP0Instruction(const Instruction &inst)
    {
        uint32_t format = inst.rs; // Format field
        uint32_t rt = inst.rt;     // GPR register
        uint32_t rd = inst.rd;     // COP0 register

        switch (format)
        {
        case COP0_MF:
            switch (rd)
            {
            case COP0_REG_INDEX:
                return fmt::format("SET_GPR_S32(ctx, {}, (int32_t)ctx->cop0_index);", rt);
            case COP0_REG_RANDOM:
                return fmt::format("SET_GPR_S32(ctx, {}, (int32_t)ctx->cop0_random);", rt);
            case COP0_REG_ENTRYLO0:
                return fmt::format("SET_GPR_S32(ctx, {}, (int32_t)ctx->cop0_entrylo0);", rt);
            case COP0_REG_ENTRYLO1:
                return fmt::format("SET_GPR_S32(ctx, {}, (int32_t)ctx->cop0_entrylo1);", rt);
            case COP0_REG_CONTEXT:
                return fmt::format("SET_GPR_S32(ctx, {}, (int32_t)ctx->cop0_context);", rt);
            case COP0_REG_PAGEMASK:
                return fmt::format("SET_GPR_S32(ctx, {}, (int32_t)ctx->cop0_pagemask);", rt);
            case COP0_REG_WIRED:
                return fmt::format("SET_GPR_S32(ctx, {}, (int32_t)ctx->cop0_wired);", rt);
            case COP0_REG_BADVADDR:
                return fmt::format("SET_GPR_S32(ctx, {}, (int32_t)ctx->cop0_badvaddr);", rt);
            case COP0_REG_COUNT:
                return fmt::format("SET_GPR_S32(ctx, {}, (int32_t)ctx->cop0_count);", rt);
            case COP0_REG_ENTRYHI:
                return fmt::format("SET_GPR_S32(ctx, {}, (int32_t)ctx->cop0_entryhi);", rt);
            case COP0_REG_COMPARE:
                return fmt::format("SET_GPR_S32(ctx, {}, (int32_t)ctx->cop0_compare);", rt);
            case COP0_REG_STATUS:
                return fmt::format("SET_GPR_S32(ctx, {}, (int32_t)ctx->cop0_status);", rt);
            case COP0_REG_CAUSE:
                return fmt::format("SET_GPR_S32(ctx, {}, (int32_t)ctx->cop0_cause);", rt);
            case COP0_REG_EPC:
                return fmt::format("SET_GPR_S32(ctx, {}, (int32_t)ctx->cop0_epc);", rt);
            case COP0_REG_PRID:
                return fmt::format("SET_GPR_S32(ctx, {}, (int32_t)ctx->cop0_prid);", rt);
            case COP0_REG_CONFIG:
                return fmt::format("SET_GPR_S32(ctx, {}, (int32_t)ctx->cop0_config);", rt);
            case COP0_REG_BADPADDR:
                return fmt::format("SET_GPR_S32(ctx, {}, (int32_t)ctx->cop0_badpaddr);", rt);
            case COP0_REG_DEBUG:
                return fmt::format("SET_GPR_S32(ctx, {}, (int32_t)ctx->cop0_debug);", rt);
            case COP0_REG_PERF:
                return fmt::format("SET_GPR_S32(ctx, {}, (int32_t)ctx->cop0_perf);", rt);
            case COP0_REG_TAGLO:
                return fmt::format("SET_GPR_S32(ctx, {}, (int32_t)ctx->cop0_taglo);", rt);
            case COP0_REG_TAGHI:
                return fmt::format("SET_GPR_S32(ctx, {}, (int32_t)ctx->cop0_taghi);", rt);
            case COP0_REG_ERROREPC:
                return fmt::format("SET_GPR_S32(ctx, {}, (int32_t)ctx->cop0_errorepc);", rt);
            default:
                return fmt::format("SET_GPR_S32(ctx, {}, 0);  // Unimplemented COP0 register {}", rt, rd);
            }
        case COP0_MT:
            switch (rd)
            {
            case COP0_REG_INDEX:
                return fmt::format("ctx->cop0_index = GPR_U32(ctx, {}) & 0x3F;", rt);
            case COP0_REG_RANDOM:
                return "// MTC0 to RANDOM register ignored (read-only)";
            case COP0_REG_ENTRYLO0:
                return fmt::format("ctx->cop0_entrylo0 = GPR_U32(ctx, {}) & 0x3FFFFFFF;", rt);
            case COP0_REG_ENTRYLO1:
                return fmt::format("ctx->cop0_entrylo1 = GPR_U32(ctx, {}) & 0x3FFFFFFF;", rt);
            case COP0_REG_CONTEXT:
                return fmt::format("ctx->cop0_context = (ctx->cop0_context & 0xFF800000) | (GPR_U32(ctx, {}) & 0x7FFFFF);", rt);
            case COP0_REG_PAGEMASK:
                return fmt::format("ctx->cop0_pagemask = GPR_U32(ctx, {}) & 0x01FFE000;", rt);
            case COP0_REG_WIRED:
                return fmt::format("ctx->cop0_wired = GPR_U32(ctx, {}) & 0x3F; ctx->cop0_random = 47;", rt);
            case COP0_REG_BADVADDR:
                return "// MTC0 to BADVADDR register ignored (read-only)";
            case COP0_REG_COUNT:
                return fmt::format("ctx->cop0_count = GPR_U32(ctx, {});", rt);
            case COP0_REG_ENTRYHI:
                return fmt::format("ctx->cop0_entryhi = GPR_U32(ctx, {}) & 0xC00000FF;", rt);
            case COP0_REG_COMPARE:
                return fmt::format("ctx->cop0_compare = GPR_U32(ctx, {}); ctx->cop0_cause &= ~0x8000;", rt);
            case COP0_REG_STATUS:
                return fmt::format("ctx->cop0_status = GPR_U32(ctx, {}) & 0xFF57FFFF;", rt);
            case COP0_REG_CAUSE:
                return fmt::format("ctx->cop0_cause = (ctx->cop0_cause & ~0x00000300) | (GPR_U32(ctx, {}) & 0x00000300);", rt);
            case COP0_REG_EPC:
                return fmt::format("ctx->cop0_epc = GPR_U32(ctx, {});", rt);
            case COP0_REG_PRID:
                return "// MTC0 to PRID register ignored (read-only)";
            case COP0_REG_CONFIG:
                return fmt::format("ctx->cop0_config = (ctx->cop0_config & ~0x7) | (GPR_U32(ctx, {}) & 0x7);", rt);
            case COP0_REG_BADPADDR:
                return "// MTC0 to BADPADDR register ignored (read-only)";
            case COP0_REG_DEBUG:
                return fmt::format("ctx->cop0_debug = GPR_U32(ctx, {});", rt);
            case COP0_REG_PERF:
                return fmt::format("ctx->cop0_perf = GPR_U32(ctx, {});", rt);
            case COP0_REG_TAGLO:
                return fmt::format("ctx->cop0_taglo = GPR_U32(ctx, {});", rt);
            case COP0_REG_TAGHI:
                return fmt::format("ctx->cop0_taghi = GPR_U32(ctx, {});", rt);
            case COP0_REG_ERROREPC:
                return fmt::format("ctx->cop0_errorepc = GPR_U32(ctx, {});", rt);
            default:
                return fmt::format("// Unimplemented MTC0 to COP0 {}", rd);
            }
        case COP0_BC:
            return fmt::format("// BC0 (Condition: 0x{:X}) - Handled by branch logic", rt);
        case COP0_CO:
        {
            uint8_t function = FUNCTION(inst.raw);
            switch (function)
            {
            case COP0_CO_TLBR:
                return fmt::format("runtime->handleTLBR(rdram, ctx);");
            case COP0_CO_TLBWI:
                return fmt::format("runtime->handleTLBWI(rdram, ctx);");
            case COP0_CO_TLBWR:
                return fmt::format("runtime->handleTLBWR(rdram, ctx);");
            case COP0_CO_TLBP:
                return fmt::format("runtime->handleTLBP(rdram, ctx);");
            case COP0_CO_ERET:
                return fmt::format(
                    "if (ctx->cop0_status & 0x4) {{ \n" // Check ERL bit (bit 2)
                    "    ctx->pc = ctx->cop0_errorepc; \n"
                    "    ctx->cop0_status &= ~0x4; \n" // Clear ERL bit
                    "}} else {{ \n"                    // If ERL is not set, use EPC and clear EXL (bit 1)
                    "    ctx->pc = ctx->cop0_epc; \n"  // Note: If neither ERL/EXL set, behavior is undefined; using EPC is common.
                    "    ctx->cop0_status &= ~0x2; \n" // Clear EXL bit
                    "}} \n"
                    "runtime->clearLLBit(ctx); \n" // Essential: Clear Load-Linked bit
                    "return;"                      // Stop execution in this recompiled block
                );
            case COP0_CO_EI:
                return fmt::format("ctx->cop0_status |= 0x10000; // Enable interrupts");
            case COP0_CO_DI:
                return fmt::format("ctx->cop0_status &= ~0x10000; // Disable interrupts");
            default:
                return fmt::format("// Unhandled COP0 CO-OP: 0x{:X}", function);
            }
        }
        default:
            return fmt::format("// Unhandled COP0 instruction format: 0x{:X}", format);
        }
    }

    std::string CodeGenerator::translateFPUInstruction(const Instruction &inst)
    {
        uint8_t format = inst.rs; // Format field
        uint32_t ft = inst.rt;    // FPU source register
        uint32_t fs = inst.rd;    // FPU source register
        uint32_t fd = inst.sa;    // FPU destination register
        uint32_t function = inst.function;

        switch (format)
        {
        case COP1_MF:
            return fmt::format("{{ uint32_t bits; std::memcpy(&bits, &ctx->f[{}], sizeof(bits)); SET_GPR_U32(ctx, {}, bits); }}", fs, ft);
        case COP1_MT:
            return fmt::format("{{ uint32_t bits = GPR_U32(ctx, {}); std::memcpy(&ctx->f[{}], &bits, sizeof(bits)); }}", ft, fs);
        case COP1_CF:
            if (fs == 31)
                return fmt::format("SET_GPR_U32(ctx, {}, ctx->fcr31);", ft); // FCR31 contains status/control
            if (fs == 0)
                return fmt::format("SET_GPR_U32(ctx, {}, 0x00000000);", ft); // FCR0 is the FPU implementation register
            return fmt::format("SET_GPR_U32(ctx, {}, 0); // Unimplemented FCR{}", ft, fs);
        case COP1_CT:
            if (fs == 31)
                return fmt::format("ctx->fcr31 = GPR_U32(ctx, {}) & 0x0183FFFF;", ft);
            else
                return fmt::format("// CTC1 to FCR{} ignored", fs);
            return "";
        case COP1_BC:
            return "// FPU branch instruction - handled elsewhere";
        case COP1_S:
            switch (function)
            {
            case COP1_S_ADD:
                return fmt::format("ctx->f[{}] = FPU_ADD_S(ctx->f[{}], ctx->f[{}]);", fd, fs, ft);
            case COP1_S_SUB:
                return fmt::format("ctx->f[{}] = FPU_SUB_S(ctx->f[{}], ctx->f[{}]);", fd, fs, ft);
            case COP1_S_MUL:
                return fmt::format("ctx->f[{}] = FPU_MUL_S(ctx->f[{}], ctx->f[{}]);", fd, fs, ft);
            case COP1_S_DIV:
                return fmt::format("if (ctx->f[{}] == 0.0f) {{ ctx->fcr31 |= 0x100000; /* DZ flag */ "
                                   "ctx->f[{}] = copysignf(INFINITY, ctx->f[{}] * 0.0f); }} "
                                   "else ctx->f[{}] = ctx->f[{}] / ctx->f[{}];",
                                   ft, fd, fs, fd, fs, ft);
            case COP1_S_SQRT:
                return fmt::format("ctx->f[{}] = FPU_SQRT_S(ctx->f[{}]);", fd, fs);
            case COP1_S_ABS:
                return fmt::format("ctx->f[{}] = FPU_ABS_S(ctx->f[{}]);", fd, fs);
            case COP1_S_MOV:
                return fmt::format("ctx->f[{}] = FPU_MOV_S(ctx->f[{}]);", fd, fs);
            case COP1_S_NEG:
                return fmt::format("ctx->f[{}] = FPU_NEG_S(ctx->f[{}]);", fd, fs);
            case COP1_S_ROUND_W:
                return fmt::format("{{ int32_t tmp = FPU_ROUND_W_S(ctx->f[{}]); std::memcpy(&ctx->f[{}], &tmp, sizeof(tmp)); }}", fs, fd);
            case COP1_S_TRUNC_W:
                return fmt::format("{{ int32_t tmp = FPU_TRUNC_W_S(ctx->f[{}]); std::memcpy(&ctx->f[{}], &tmp, sizeof(tmp)); }}", fs, fd);
            case COP1_S_CEIL_W:
                return fmt::format("{{ int32_t tmp = FPU_CEIL_W_S(ctx->f[{}]); std::memcpy(&ctx->f[{}], &tmp, sizeof(tmp)); }}", fs, fd);
            case COP1_S_FLOOR_W:
                return fmt::format("{{ int32_t tmp = FPU_FLOOR_W_S(ctx->f[{}]); std::memcpy(&ctx->f[{}], &tmp, sizeof(tmp)); }}", fs, fd);
            case COP1_S_CVT_W:
                return fmt::format("{{ int32_t tmp = FPU_CVT_W_S(ctx->f[{}]); std::memcpy(&ctx->f[{}], &tmp, sizeof(tmp)); }}", fs, fd);
            case COP1_S_RSQRT:
                return fmt::format("ctx->f[{}] = 1.0f / sqrtf(ctx->f[{}]);", fd, fs);
            case COP1_S_ADDA:
                return fmt::format("ctx->f[31] = FPU_ADD_S(ctx->f[{}], ctx->f[{}]);", fs, ft);
            case COP1_S_SUBA:
                return fmt::format("ctx->f[31] = FPU_SUB_S(ctx->f[{}], ctx->f[{}]);", fs, ft);
            case COP1_S_MULA:
                return fmt::format("ctx->f[31] = FPU_MUL_S(ctx->f[{}], ctx->f[{}]);", fs, ft);
            case COP1_S_MADD:
                return fmt::format("ctx->f[{}] = FPU_ADD_S(ctx->f[31], FPU_MUL_S(ctx->f[{}], ctx->f[{}]));", fd, fs, ft);
            case COP1_S_MSUB:
                return fmt::format("ctx->f[{}] = FPU_SUB_S(ctx->f[31], FPU_MUL_S(ctx->f[{}], ctx->f[{}]));", fd, fs, ft);
            case COP1_S_MADDA:
                return fmt::format("ctx->f[31] = FPU_ADD_S(ctx->f[31], FPU_MUL_S(ctx->f[{}], ctx->f[{}]));", fs, ft);
            case COP1_S_MSUBA:
                return fmt::format("ctx->f[31] = FPU_SUB_S(ctx->f[31], FPU_MUL_S(ctx->f[{}], ctx->f[{}]));", fs, ft);
            case COP1_S_MAX:
                return fmt::format("ctx->f[{}] = std::max(ctx->f[{}], ctx->f[{}]);", fd, fs, ft);
            case COP1_S_MIN:
                return fmt::format("ctx->f[{}] = std::min(ctx->f[{}], ctx->f[{}]);", fd, fs, ft);
            case COP1_S_C_F:
                return fmt::format("ctx->fcr31 &= ~0x800000;");
            case COP1_S_C_UN:
                return fmt::format("ctx->fcr31 = (FPU_C_UN_S(ctx->f[{}], ctx->f[{}])) ? (ctx->fcr31 | 0x800000) : (ctx->fcr31 & ~0x800000);", fs, ft);
            case COP1_S_C_EQ:
                return fmt::format("ctx->fcr31 = (FPU_C_EQ_S(ctx->f[{}], ctx->f[{}])) ? (ctx->fcr31 | 0x800000) : (ctx->fcr31 & ~0x800000);", fs, ft);
            case COP1_S_C_UEQ:
                return fmt::format("ctx->fcr31 = (FPU_C_UEQ_S(ctx->f[{}], ctx->f[{}])) ? (ctx->fcr31 | 0x800000) : (ctx->fcr31 & ~0x800000);", fs, ft);
            case COP1_S_C_OLT:
                return fmt::format("ctx->fcr31 = (FPU_C_OLT_S(ctx->f[{}], ctx->f[{}])) ? (ctx->fcr31 | 0x800000) : (ctx->fcr31 & ~0x800000);", fs, ft);
            case COP1_S_C_ULT:
                return fmt::format("ctx->fcr31 = (FPU_C_ULT_S(ctx->f[{}], ctx->f[{}])) ? (ctx->fcr31 | 0x800000) : (ctx->fcr31 & ~0x800000);", fs, ft);
            case COP1_S_C_OLE:
                return fmt::format("ctx->fcr31 = (FPU_C_OLE_S(ctx->f[{}], ctx->f[{}])) ? (ctx->fcr31 | 0x800000) : (ctx->fcr31 & ~0x800000);", fs, ft);
            case COP1_S_C_ULE:
                return fmt::format("ctx->fcr31 = (FPU_C_ULE_S(ctx->f[{}], ctx->f[{}])) ? (ctx->fcr31 | 0x800000) : (ctx->fcr31 & ~0x800000);", fs, ft);
            case COP1_S_C_SF:
                return fmt::format("ctx->fcr31 &= ~0x800000;");
            case COP1_S_C_NGLE:
                return fmt::format("ctx->fcr31 = (FPU_C_NGLE_S(ctx->f[{}], ctx->f[{}])) ? (ctx->fcr31 | 0x800000) : (ctx->fcr31 & ~0x800000);", fs, ft);
            case COP1_S_C_SEQ:
                return fmt::format("ctx->fcr31 = (FPU_C_SEQ_S(ctx->f[{}], ctx->f[{}])) ? (ctx->fcr31 | 0x800000) : (ctx->fcr31 & ~0x800000);", fs, ft);
            case COP1_S_C_NGL:
                return fmt::format("ctx->fcr31 = (FPU_C_NGL_S(ctx->f[{}], ctx->f[{}])) ? (ctx->fcr31 | 0x800000) : (ctx->fcr31 & ~0x800000);", fs, ft);
            case COP1_S_C_LT:
                return fmt::format("ctx->fcr31 = (FPU_C_LT_S(ctx->f[{}], ctx->f[{}])) ? (ctx->fcr31 | 0x800000) : (ctx->fcr31 & ~0x800000);", fs, ft);
            case COP1_S_C_NGE:
                return fmt::format("ctx->fcr31 = (FPU_C_NGE_S(ctx->f[{}], ctx->f[{}])) ? (ctx->fcr31 | 0x800000) : (ctx->fcr31 & ~0x800000);", fs, ft);
            case COP1_S_C_LE:
                return fmt::format("ctx->fcr31 = (FPU_C_LE_S(ctx->f[{}], ctx->f[{}])) ? (ctx->fcr31 | 0x800000) : (ctx->fcr31 & ~0x800000);", fs, ft);
            case COP1_S_C_NGT:
                return fmt::format("ctx->fcr31 = (FPU_C_NGT_S(ctx->f[{}], ctx->f[{}])) ? (ctx->fcr31 | 0x800000) : (ctx->fcr31 & ~0x800000);", fs, ft);
            default:
                return fmt::format("// Unhandled FPU.S instruction: function 0x{:X}", function);
            }
        case COP1_W:
            switch (function)
            {
            case COP1_W_CVT_S:
                return fmt::format("{{ int32_t tmp; std::memcpy(&tmp, &ctx->f[{}], sizeof(tmp)); ctx->f[{}] = FPU_CVT_S_W(tmp); }}", fs, fd);
            default:
                return fmt::format("// Unhandled FPU.W instruction: function 0x{:X}", function);
            }
        default:
            return fmt::format("// Unhandled FPU instruction: format 0x{:X}, function 0x{:X}", format, function);
        }
    }

    std::string CodeGenerator::translateMMIInstruction(const Instruction &inst)
    {
        uint32_t function = inst.function;
        uint8_t rs = inst.rs;
        uint8_t rt = inst.rt;
        uint8_t rd = inst.rd;
        uint8_t sa = inst.sa;
        switch (function)
        {
        case MMI_MFHI1:
            return fmt::format("SET_GPR_U64(ctx, {}, ctx->hi1);", rd);
        case MMI_MTHI1:
            return fmt::format("ctx->hi1 = GPR_U64(ctx, {});", rs);
        case MMI_MFLO1:
            return fmt::format("SET_GPR_U64(ctx, {}, ctx->lo1);", rd);
        case MMI_MTLO1:
            return fmt::format("ctx->lo1 = GPR_U64(ctx, {});", rs);
        case MMI_MULT1:
            if (rd != 0)
            {
                return fmt::format("{{ int64_t result = (int64_t)GPR_S32(ctx, {}) * (int64_t)GPR_S32(ctx, {}); ctx->lo1 = (uint64_t)(int64_t)(int32_t)result; ctx->hi1 = (uint64_t)(int64_t)(int32_t)(result >> 32); SET_GPR_S32(ctx, {}, (int32_t)result); }}", rs, rt, rd);
            }
            return fmt::format("{{ int64_t result = (int64_t)GPR_S32(ctx, {}) * (int64_t)GPR_S32(ctx, {}); ctx->lo1 = (uint64_t)(int64_t)(int32_t)result; ctx->hi1 = (uint64_t)(int64_t)(int32_t)(result >> 32); }}", rs, rt);
        case MMI_MULTU1:
            if (rd != 0)
            {
                return fmt::format("{{ uint64_t result = (uint64_t)GPR_U32(ctx, {}) * (uint64_t)GPR_U32(ctx, {}); ctx->lo1 = (uint64_t)(int64_t)(int32_t)result; ctx->hi1 = (uint64_t)(int64_t)(int32_t)(result >> 32); SET_GPR_S32(ctx, {}, (int32_t)result); }}", rs, rt, rd);
            }
            return fmt::format("{{ uint64_t result = (uint64_t)GPR_U32(ctx, {}) * (uint64_t)GPR_U32(ctx, {}); ctx->lo1 = (uint64_t)(int64_t)(int32_t)result; ctx->hi1 = (uint64_t)(int64_t)(int32_t)(result >> 32); }}", rs, rt);
        case MMI_DIV1:
            return fmt::format("{{ int32_t divisor = GPR_S32(ctx, {}); "
                               "int32_t dividend = GPR_S32(ctx, {}); "
                               "if (divisor != 0) {{ "
                               "    if (divisor == -1 && dividend == INT32_MIN) {{ "
                               "        ctx->lo1 = (uint64_t)(int64_t)INT32_MIN; ctx->hi1 = 0; "
                               "    }} else {{ "
                               "        ctx->lo1 = (uint64_t)(int64_t)(dividend / divisor); "
                               "        ctx->hi1 = (uint64_t)(int64_t)(dividend % divisor); "
                               "    }} "
                               "}} else {{ "
                               "    ctx->lo1 = (dividend < 0) ? 1ull : 0xFFFFFFFFFFFFFFFFull; ctx->hi1 = (uint64_t)(int64_t)dividend; "
                               "}} }}",
                               inst.rt, inst.rs);
        case MMI_DIVU1:
            return fmt::format("{{ uint32_t divisor = GPR_U32(ctx, {}); if (divisor != 0) {{ ctx->lo1 = (uint64_t)(int64_t)(int32_t)(GPR_U32(ctx, {}) / divisor); ctx->hi1 = (uint64_t)(int64_t)(int32_t)(GPR_U32(ctx, {}) % divisor); }} else {{ ctx->lo1=0xFFFFFFFFFFFFFFFFull; ctx->hi1=(uint64_t)(int64_t)(int32_t)GPR_U32(ctx,{}); }} }}", rt, rs, rs, rs);
        case MMI_MADD:
            if (rd != 0)
            {
                return fmt::format("{{ uint64_t acc = Ps2HiLoToU64(ctx->hi, ctx->lo); int64_t prod = (int64_t)GPR_S32(ctx, {}) * (int64_t)GPR_S32(ctx, {}); int64_t result = acc + prod; ctx->lo = Ps2SignExt32ToU64((uint32_t)result); ctx->hi = Ps2SignExt32ToU64((uint32_t)(result >> 32)); SET_GPR_S32(ctx, {}, (int32_t)result); }}", rs, rt, rd);
            }
            return fmt::format("{{ uint64_t acc = Ps2HiLoToU64(ctx->hi, ctx->lo); int64_t prod = (int64_t)GPR_S32(ctx, {}) * (int64_t)GPR_S32(ctx, {}); int64_t result = acc + prod; ctx->lo = Ps2SignExt32ToU64((uint32_t)result); ctx->hi = Ps2SignExt32ToU64((uint32_t)(result >> 32)); }}", rs, rt);
        case MMI_MADDU:
            if (rd != 0)
            {
                return fmt::format("{{ uint64_t acc = Ps2HiLoToU64(ctx->hi, ctx->lo); uint64_t prod = (uint64_t)GPR_U32(ctx, {}) * (uint64_t)GPR_U32(ctx, {}); uint64_t result = acc + prod; ctx->lo = Ps2SignExt32ToU64((uint32_t)result); ctx->hi = Ps2SignExt32ToU64((uint32_t)(result >> 32)); SET_GPR_S32(ctx, {}, (int32_t)result); }}", rs, rt, rd);
            }
            return fmt::format("{{ uint64_t acc = Ps2HiLoToU64(ctx->hi, ctx->lo); uint64_t prod = (uint64_t)GPR_U32(ctx, {}) * (uint64_t)GPR_U32(ctx, {}); uint64_t result = acc + prod; ctx->lo = Ps2SignExt32ToU64((uint32_t)result); ctx->hi = Ps2SignExt32ToU64((uint32_t)(result >> 32)); }}", rs, rt);
        case MMI_MSUB:
            if (rd != 0)
            {
                return fmt::format("{{ uint64_t acc = Ps2HiLoToU64(ctx->hi, ctx->lo); int64_t prod = (int64_t)GPR_S32(ctx, {}) * (int64_t)GPR_S32(ctx, {}); int64_t result = acc - prod; ctx->lo = Ps2SignExt32ToU64((uint32_t)result); ctx->hi = Ps2SignExt32ToU64((uint32_t)(result >> 32)); SET_GPR_S32(ctx, {}, (int32_t)result); }}", rs, rt, rd);
            }
            return fmt::format("{{ uint64_t acc = Ps2HiLoToU64(ctx->hi, ctx->lo); int64_t prod = (int64_t)GPR_S32(ctx, {}) * (int64_t)GPR_S32(ctx, {}); int64_t result = acc - prod; ctx->lo = Ps2SignExt32ToU64((uint32_t)result); ctx->hi = Ps2SignExt32ToU64((uint32_t)(result >> 32)); }}", rs, rt);
        case MMI_MSUBU:
            if (rd != 0)
            {
                return fmt::format("{{ uint64_t acc = Ps2HiLoToU64(ctx->hi, ctx->lo); uint64_t prod = (uint64_t)GPR_U32(ctx, {}) * (uint64_t)GPR_U32(ctx, {}); uint64_t result = acc - prod; ctx->lo = Ps2SignExt32ToU64((uint32_t)result); ctx->hi = Ps2SignExt32ToU64((uint32_t)(result >> 32)); SET_GPR_S32(ctx, {}, (int32_t)result); }}", rs, rt, rd);
            }
            return fmt::format("{{ uint64_t acc = Ps2HiLoToU64(ctx->hi, ctx->lo); uint64_t prod = (uint64_t)GPR_U32(ctx, {}) * (uint64_t)GPR_U32(ctx, {}); uint64_t result = acc - prod; ctx->lo = Ps2SignExt32ToU64((uint32_t)result); ctx->hi = Ps2SignExt32ToU64((uint32_t)(result >> 32)); }}", rs, rt);
        case MMI_MADD1:
            if (rd != 0)
            {
                return fmt::format("{{ uint64_t acc = Ps2HiLoToU64(ctx->hi1, ctx->lo1); int64_t prod = (int64_t)GPR_S32(ctx, {}) * (int64_t)GPR_S32(ctx, {}); int64_t result = acc + prod; ctx->lo1 = Ps2SignExt32ToU64((uint32_t)result); ctx->hi1 = Ps2SignExt32ToU64((uint32_t)(result >> 32)); SET_GPR_S32(ctx, {}, (int32_t)result); }}", rs, rt, rd);
            }
            return fmt::format("{{ uint64_t acc = Ps2HiLoToU64(ctx->hi1, ctx->lo1); int64_t prod = (int64_t)GPR_S32(ctx, {}) * (int64_t)GPR_S32(ctx, {}); int64_t result = acc + prod; ctx->lo1 = Ps2SignExt32ToU64((uint32_t)result); ctx->hi1 = Ps2SignExt32ToU64((uint32_t)(result >> 32)); }}", rs, rt);
        case MMI_MADDU1:
            if (rd != 0)
            {
                return fmt::format("{{ uint64_t acc = Ps2HiLoToU64(ctx->hi1, ctx->lo1); uint64_t prod = (uint64_t)GPR_U32(ctx, {}) * (uint64_t)GPR_U32(ctx, {}); uint64_t result = acc + prod; ctx->lo1 = Ps2SignExt32ToU64((uint32_t)result); ctx->hi1 = Ps2SignExt32ToU64((uint32_t)(result >> 32)); SET_GPR_S32(ctx, {}, (int32_t)result); }}", rs, rt, rd);
            }
            return fmt::format("{{ uint64_t acc = Ps2HiLoToU64(ctx->hi1, ctx->lo1); uint64_t prod = (uint64_t)GPR_U32(ctx, {}) * (uint64_t)GPR_U32(ctx, {}); uint64_t result = acc + prod; ctx->lo1 = Ps2SignExt32ToU64((uint32_t)result); ctx->hi1 = Ps2SignExt32ToU64((uint32_t)(result >> 32)); }}", rs, rt);
        case MMI_PLZCW:
            return fmt::format(
                "{{ "
                "uint64_t v = GPR_U64(ctx, {}); "
                "uint32_t lo = (uint32_t)(v & 0xFFFFFFFFu); "
                "uint32_t hi = (uint32_t)(v >> 32); "
                "uint64_t out = ((uint64_t)ps2_plzcw32(hi) << 32) | (uint64_t)ps2_plzcw32(lo); "
                "SET_GPR_U64(ctx, {}, out); "
                "}}",
                rs, rd);
        case MMI_PSLLH:
            return fmt::format("SET_GPR_VEC(ctx, {}, _mm_slli_epi16(GPR_VEC(ctx, {}), {}));", rd, rt, sa);
        case MMI_PSRLH:
            return fmt::format("SET_GPR_VEC(ctx, {}, _mm_srli_epi16(GPR_VEC(ctx, {}), {}));", rd, rt, sa);
        case MMI_PSRAH:
            return fmt::format("SET_GPR_VEC(ctx, {}, _mm_srai_epi16(GPR_VEC(ctx, {}), {}));", rd, rt, sa);
        case MMI_PSLLW:
            return fmt::format("SET_GPR_VEC(ctx, {}, _mm_slli_epi32(GPR_VEC(ctx, {}), {}));", rd, rt, sa);
        case MMI_PSRLW:
            return fmt::format("SET_GPR_VEC(ctx, {}, _mm_srli_epi32(GPR_VEC(ctx, {}), {}));", rd, rt, sa);
        case MMI_PSRAW:
            return fmt::format("SET_GPR_VEC(ctx, {}, _mm_srai_epi32(GPR_VEC(ctx, {}), {}));", rd, rt, sa);
        case MMI_MMI0:
            return translateMMI0Instruction(inst);
        case MMI_MMI1:
            return translateMMI1Instruction(inst);
        case MMI_MMI2:
            return translateMMI2Instruction(inst);
        case MMI_MMI3:
            return translateMMI3Instruction(inst);
        case MMI_PMFHL:
            return translatePMFHLInstruction(inst);
        case MMI_PMTHL:
            return translatePMTHLInstruction(inst);
        default:
            return fmt::format("// Unhandled MMI instruction: function 0x{:X}", function);
        }
    }

    std::string CodeGenerator::translateMMI0Instruction(const Instruction &inst)
    {
        uint8_t subfunc = inst.sa;
        uint8_t rs = inst.rs;
        uint8_t rt = inst.rt;
        uint8_t rd = inst.rd;
        switch (subfunc)
        {
        case MMI0_PADDW:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PADDW(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI0_PSUBW:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PSUBW(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI0_PCGTW:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PCGTW(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI0_PMAXW:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PMAXW(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI0_PADDH:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PADDH(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI0_PSUBH:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PSUBH(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI0_PCGTH:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PCGTH(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI0_PMAXH:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PMAXH(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI0_PADDB:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PADDB(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI0_PSUBB:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PSUBB(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI0_PCGTB:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PCGTB(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        // thouse 2 require SSE4.1 now TODO implement  on SSE2
        case MMI0_PADDSW:
            return fmt::format("SET_GPR_VEC(ctx, {}, _mm_min_epi32(_mm_max_epi32(_mm_add_epi32(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})), "
                               "_mm_set1_epi32(INT32_MIN)), _mm_set1_epi32(INT32_MAX)));",
                               rd, rs, rt);
        case MMI0_PSUBSW:
            return fmt::format("SET_GPR_VEC(ctx, {}, _mm_min_epi32(_mm_max_epi32(_mm_sub_epi32(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})), "
                               "_mm_set1_epi32(INT32_MIN)), _mm_set1_epi32(INT32_MAX)));",
                               rd, rs, rt);
        case MMI0_PEXTLW:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PEXTLW(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI0_PPACW:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PPACW(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI0_PADDSH:
            return fmt::format("SET_GPR_VEC(ctx, {}, _mm_adds_epi16(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI0_PSUBSH:
            return fmt::format("SET_GPR_VEC(ctx, {}, _mm_subs_epi16(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI0_PEXTLH:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PEXTLH(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI0_PPACH:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PPACH(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI0_PADDSB:
            return fmt::format("SET_GPR_VEC(ctx, {}, _mm_adds_epi8(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI0_PSUBSB:
            return fmt::format("SET_GPR_VEC(ctx, {}, _mm_subs_epi8(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI0_PEXTLB:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PEXTLB(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI0_PPACB:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PPACB(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI0_PEXT5:
            return translatePEXT5(inst);
        case MMI0_PPAC5:
            return translatePPAC5(inst);
        default:
            return fmt::format("// Unhandled MMI0 instruction: function 0x{:X}", subfunc);
        }
    }

    std::string CodeGenerator::translateMMI1Instruction(const Instruction &inst)
    {
        uint8_t subfunc = inst.sa;
        uint8_t rs = inst.rs;
        uint8_t rt = inst.rt;
        uint8_t rd = inst.rd;
        switch (subfunc)
        {
        case MMI1_PABSW:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PABSW(GPR_VEC(ctx, {})));", rd, rs);
        case MMI1_PCEQW:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PCEQW(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI1_PMINW:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PMINW(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI1_PADSBH:
            return translatePADSBH(inst);
        case MMI1_PABSH:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PABSH(GPR_VEC(ctx, {})));", rd, rs);
        case MMI1_PCEQH:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PCEQH(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI1_PMINH:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PMINH(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI1_PCEQB:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PCEQB(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI1_PADDUW:
            return fmt::format(
                "SET_GPR_VEC(ctx, {}, ps2_paddu32(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI1_PSUBUW:
            return fmt::format(
                "SET_GPR_VEC(ctx, {}, ps2_psubu32(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI1_PEXTUW:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PEXTUW(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI1_PADDUH:
            return fmt::format("SET_GPR_VEC(ctx, {}, _mm_add_epi16(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI1_PSUBUH:
            return fmt::format("SET_GPR_VEC(ctx, {}, _mm_sub_epi16(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI1_PEXTUH:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PEXTUH(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI1_PADDUB:
            return fmt::format("SET_GPR_VEC(ctx, {}, _mm_adds_epu8(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI1_PSUBUB:
            return fmt::format("SET_GPR_VEC(ctx, {}, _mm_subs_epu8(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI1_PEXTUB:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PEXTUB(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI1_QFSRV:
            return translateQFSRV(inst);
        default:
            return fmt::format("// Unhandled MMI1 instruction: function 0x{:X}", subfunc);
        }
    }

    std::string CodeGenerator::translateMMI2Instruction(const Instruction &inst)
    {
        uint8_t subfunc = inst.sa;
        uint8_t rs = inst.rs;
        uint8_t rt = inst.rt;
        uint8_t rd = inst.rd;
        switch (subfunc)
        {
        case MMI2_PMADDW:
            return translatePMADDW(inst);
        case MMI2_PSLLVW:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PSLLVW(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI2_PSRLVW:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PSRLVW(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI2_PMSUBW:
            return translatePMSUBW(inst);
        case MMI2_PMFHI:
            return fmt::format("SET_GPR_U64(ctx, {}, ctx->hi);", rd);
        case MMI2_PMFLO:
            return fmt::format("SET_GPR_U64(ctx, {}, ctx->lo);", rd);
        case MMI2_PINTH:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PINTH(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI2_PMULTW:
            return translatePMULTW(inst);
        case MMI2_PDIVW:
            return translatePDIVW(inst);
        case MMI2_PCPYLD:
            return translatePCPYLD(inst);
        case MMI2_PAND:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PAND(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI2_PXOR:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PXOR(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI2_PMADDH:
            return translatePMADDH(inst);
        case MMI2_PHMADH:
            return translatePHMADH(inst);
        case MMI2_PMSUBH:
            return translatePMSUBH(inst);
        case MMI2_PHMSBH:
            return translatePHMSBH(inst);
        case MMI2_PEXEH:
            return translatePEXEH(inst);
        case MMI2_PREVH:
            return translatePREVH(inst);
        case MMI2_PMULTH:
            return translatePMULTH(inst);
        case MMI2_PDIVBW:
            return translatePDIVBW(inst);
        case MMI2_PEXEW:
            return translatePEXEW(inst);
        case MMI2_PROT3W:
            return translatePROT3W(inst);
        default:
            return fmt::format("// Unhandled MMI2 instruction: function 0x{:X}", subfunc);
        }
    }

    std::string CodeGenerator::translateMMI3Instruction(const Instruction &inst)
    {
        uint8_t subfunc = inst.sa;
        uint8_t rs = inst.rs;
        uint8_t rt = inst.rt;
        uint8_t rd = inst.rd;
        switch (subfunc)
        {
        case MMI3_PMADDUW:
            return translatePMADDUW(inst);
        case MMI3_PSRAVW:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PSRAVW(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI3_PMTHI:
            return translatePMTHI(inst);
        case MMI3_PMTLO:
            return translatePMTLO(inst);
        case MMI3_PINTEH:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PINTEH(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI3_PMULTUW:
            return translatePMULTUW(inst);
        case MMI3_PDIVUW:
            return translatePDIVUW(inst);
        case MMI3_PCPYUD:
            return translatePCPYUD(inst);
        case MMI3_POR:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_POR(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI3_PNOR:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PNOR(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI3_PEXCH:
            return translatePEXCH(inst);
        case MMI3_PCPYH:
            return translatePCPYH(inst);
        case MMI3_PEXCW:
            return translatePEXCW(inst);
        default:
            return fmt::format("// Unhandled MMI3 instruction: function 0x{:X}", subfunc);
        }
    }

    std::string CodeGenerator::translatePMFHLInstruction(const Instruction &inst)
    {
        uint8_t subfunc = inst.sa;
        switch (subfunc)
        {
        case PMFHL_LW:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PMFHL_LW(ctx->hi, ctx->lo));", inst.rd);
        case PMFHL_UW:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PMFHL_UW(ctx->hi, ctx->lo));", inst.rd);
        case PMFHL_SLW:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PMFHL_SLW(ctx->hi, ctx->lo));", inst.rd);
        case PMFHL_LH:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PMFHL_LH(ctx->hi, ctx->lo));", inst.rd);
        case PMFHL_SH:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PMFHL_SH(ctx->hi, ctx->lo));", inst.rd);
        default:
            return fmt::format("// Unhandled PMFHL instruction: function 0x{:X}", subfunc);
        }
    }

    std::string CodeGenerator::translatePMTHLInstruction(const Instruction &inst)
    {
        uint8_t subfunc = inst.sa;
        switch (subfunc)
        {
        case PMFHL_LW:
            return fmt::format("{{ __m128i val = GPR_VEC(ctx, {}); ctx->lo = _mm_extract_epi32(val, 0); ctx->hi = _mm_extract_epi32(val, 1); }}", inst.rs);
        default:
            return fmt::format("// Unhandled PMTHL instruction: function 0x{:X}", subfunc);
        }
    }

    std::string CodeGenerator::translateVUInstruction(const Instruction &inst)
    {
        uint8_t format = inst.rs; // Use parsed rs field for COP2 format
        uint8_t rt = inst.rt;
        uint8_t rd = inst.rd;
        uint8_t sa = inst.sa;

        switch (format)
        {
        case COP2_QMFC2:
            return fmt::format("SET_GPR_VEC(ctx, {}, _mm_castps_si128(ctx->vu0_vf[{}]));", rt, rd);
        case COP2_CFC2:
        {
            switch (rd) // Control register number is in rd
            {
            case VU0_CR_STATUS:
                return fmt::format("SET_GPR_U32(ctx, {}, ctx->vu0_status);", rt);
            case VU0_CR_MAC:
                return fmt::format("SET_GPR_U32(ctx, {}, ctx->vu0_mac_flags);", rt);
            case VU0_CR_VPU_STAT:
                return fmt::format("SET_GPR_U32(ctx, {}, ctx->vu0_vpu_stat);", rt);
            case VU0_CR_R:
                return fmt::format("SET_GPR_VEC(ctx, {}, _mm_castps_si128(ctx->vu0_r));", rt);
            case VU0_CR_I:
                return fmt::format("{{ uint32_t bits; std::memcpy(&bits, &ctx->vu0_i, sizeof(bits)); SET_GPR_U32(ctx, {}, bits); }}", rt);
            case VU0_CR_CLIP:
                return fmt::format("SET_GPR_U32(ctx, {}, ctx->vu0_clip_flags);", rt);
            case VU0_CR_TPC:
                return fmt::format("SET_GPR_U32(ctx, {}, ctx->vu0_tpc);", rt);
            case VU0_CR_CMSAR0:
                return fmt::format("SET_GPR_U32(ctx, {}, ctx->vu0_cmsar0);", rt);
            case VU0_CR_FBRST:
                return fmt::format("SET_GPR_U32(ctx, {}, ctx->vu0_fbrst);", rt);
            case VU0_CR_VPU_STAT2:
                return fmt::format("SET_GPR_U32(ctx, {}, ctx->vu0_vpu_stat2);", rt);
            case VU0_CR_TPC2:
                return fmt::format("SET_GPR_U32(ctx, {}, ctx->vu0_tpc2);", rt);
            case VU0_CR_CMSAR1:
                return fmt::format("SET_GPR_U32(ctx, {}, ctx->vu0_cmsar1);", rt);
            case VU0_CR_FBRST2:
                return fmt::format("SET_GPR_U32(ctx, {}, ctx->vu0_fbrst2);", rt);
            case VU0_CR_VPU_STAT3:
                return fmt::format("SET_GPR_U32(ctx, {}, ctx->vu0_vpu_stat3);", rt);
            case VU0_CR_CMSAR2:
                return fmt::format("SET_GPR_U32(ctx, {}, ctx->vu0_cmsar2);", rt);
            case VU0_CR_FBRST3:
                return fmt::format("SET_GPR_U32(ctx, {}, ctx->vu0_fbrst3);", rt);
            case VU0_CR_VPU_STAT4:
                return fmt::format("SET_GPR_U32(ctx, {}, ctx->vu0_vpu_stat4);", rt);
            case VU0_CR_CMSAR3:
                return fmt::format("SET_GPR_U32(ctx, {}, ctx->vu0_cmsar3);", rt);
            case VU0_CR_FBRST4:
                return fmt::format("SET_GPR_U32(ctx, {}, ctx->vu0_fbrst4);", rt);
            case VU0_CR_ACC:
                return fmt::format("SET_GPR_VEC(ctx, {}, _mm_castps_si128(ctx->vu0_acc));", rt);
            case VU0_CR_INFO: // I dd found on offical docs but ok
                return fmt::format("SET_GPR_U32(ctx, {}, ctx->vu0_info);", rt);
            case VU0_CR_CLIP2:
                return fmt::format("SET_GPR_U32(ctx, {}, ctx->vu0_clip_flags2);", rt);
            case VU0_CR_P:
                return fmt::format("{{ uint32_t bits; std::memcpy(&bits, &ctx->vu0_p, sizeof(bits)); SET_GPR_U32(ctx, {}, bits); }}", rt);
            case VU0_CR_XITOP: // Maybe this does not exist, maybe we handle to vu0_itop
                return fmt::format("SET_GPR_U32(ctx, {}, ctx->vu0_xitop);", rt);
            case VU0_CR_ITOP:
                return fmt::format("SET_GPR_U32(ctx, {}, ctx->vu0_itop);", rt);
            case VU0_CR_TOP:
                return fmt::format("SET_GPR_U32(ctx, {}, ctx->vu0_top);", rt); // TODO: verify vu0_top field exists in R5900Context
            default:
                return fmt::format("// Unimplemented CFC2 VU CReg: {}", rt);
            }
        }
        case COP2_QMTC2:
            return fmt::format("ctx->vu0_vf[{}] = _mm_castsi128_ps(GPR_VEC(ctx, {}));", rd, rt);
        case COP2_CTC2:
        {
            switch (rd) // Control register number is in rd
            {
            case VU0_CR_STATUS:
                return fmt::format("ctx->vu0_status = GPR_U32(ctx, {}) & 0xFFFF;", rt);
            case VU0_CR_MAC:
                return fmt::format("ctx->vu0_mac_flags = GPR_U32(ctx, {});", rt);
            case VU0_CR_VPU_STAT:
                return fmt::format("ctx->vu0_vpu_stat = GPR_U32(ctx, {});", rt);
            case VU0_CR_CLIP:
                return fmt::format("ctx->vu0_clip_flags = GPR_U32(ctx, {});", rt);
            case VU0_CR_R:
                return fmt::format("ctx->vu0_r = _mm_castsi128_ps(GPR_VEC(ctx, {}));", rt);
            case VU0_CR_I:
                return fmt::format("{{ uint32_t tmp = GPR_U32(ctx, {}); std::memcpy(&ctx->vu0_i, &tmp, sizeof(tmp)); }}", rt);
            case VU0_CR_TPC:
                return fmt::format("ctx->vu0_tpc = GPR_U32(ctx, {});", rt);
            case VU0_CR_CMSAR0:
                return fmt::format("ctx->vu0_cmsar0 = GPR_U32(ctx, {});", rt);
            case VU0_CR_FBRST:
                return fmt::format("ctx->vu0_fbrst = GPR_U32(ctx, {});", rt);
            case VU0_CR_VPU_STAT2:
                return fmt::format("ctx->vu0_vpu_stat2 = GPR_U32(ctx, {});", rt);
            case VU0_CR_TPC2:
                return fmt::format("ctx->vu0_tpc2 = GPR_U32(ctx, {});", rt);
            case VU0_CR_CMSAR1:
                return fmt::format("ctx->vu0_cmsar1 = GPR_U32(ctx, {});", rt);
            case VU0_CR_FBRST2:
                return fmt::format("ctx->vu0_fbrst2 = GPR_U32(ctx, {});", rt);
            case VU0_CR_VPU_STAT3:
                return fmt::format("ctx->vu0_vpu_stat3 = GPR_U32(ctx, {});", rt);
            case VU0_CR_CMSAR2:
                return fmt::format("ctx->vu0_cmsar2 = GPR_U32(ctx, {});", rt);
            case VU0_CR_FBRST3:
                return fmt::format("ctx->vu0_fbrst3 = GPR_U32(ctx, {});", rt);
            case VU0_CR_VPU_STAT4:
                return fmt::format("ctx->vu0_vpu_stat4 = GPR_U32(ctx, {});", rt);
            case VU0_CR_CMSAR3:
                return fmt::format("ctx->vu0_cmsar3 = GPR_U32(ctx, {});", rt);
            case VU0_CR_FBRST4:
                return fmt::format("ctx->vu0_fbrst4 = GPR_U32(ctx, {});", rt);
            case VU0_CR_ACC:
                return fmt::format("ctx->vu0_acc = _mm_castsi128_ps(GPR_VEC(ctx, {}));", rt);
            case VU0_CR_INFO:
                return fmt::format("ctx->vu0_info = GPR_U32(ctx, {});", rt);
            case VU0_CR_CLIP2:
                return fmt::format("ctx->vu0_clip_flags2 = GPR_U32(ctx, {});", rt);
            case VU0_CR_P:
                return fmt::format("{{ uint32_t tmp = GPR_U32(ctx, {}); std::memcpy(&ctx->vu0_p, &tmp, sizeof(tmp)); }}", rt);
            case VU0_CR_XITOP:
                return fmt::format("ctx->vu0_xitop = GPR_U32(ctx, {}) & 0x3FF;", rt);
            case VU0_CR_ITOP:
                return fmt::format("ctx->vu0_itop = GPR_U32(ctx, {}) & 0x3FF;", rt);
            case VU0_CR_TOP:
                return fmt::format("ctx->vu0_top = GPR_U32(ctx, {}) & 0x3FF;", rt);
            default:
                return fmt::format("// Unimplemented CTC2 VU CReg: {}", rd);
            }
        }
        case COP2_BC:
            return fmt::format("// BC2 (Condition: 0x{:X}) - Handled by branch logic", rt);
        case COP2_CO:
        case COP2_CO + 1:
        case COP2_CO + 2:
        case COP2_CO + 3:
        case COP2_CO + 4:
        case COP2_CO + 5:
        case COP2_CO + 6:
        case COP2_CO + 7:
        case COP2_CO + 8:
        case COP2_CO + 9:
        case COP2_CO + 10:
        case COP2_CO + 11:
        case COP2_CO + 12:
        case COP2_CO + 13:
        case COP2_CO + 14:
        case COP2_CO + 15:
        {
            const uint8_t special1_func = static_cast<uint8_t>(inst.function & 0x3F);
            if (special1_func >= 0x3C) // Special2 Table
            {
                const uint8_t vu_func = static_cast<uint8_t>((((inst.raw >> 6) & 0x1F) << 2) | (inst.raw & 0x3));
                switch (vu_func)
                {
                case VU0_S2_VADDAx:
                case VU0_S2_VADDAy:
                case VU0_S2_VADDAz:
                case VU0_S2_VADDAw:
                    return translateVU_VADDA_Field(inst);
                case VU0_S2_VSUBAx:
                case VU0_S2_VSUBAy:
                case VU0_S2_VSUBAz:
                case VU0_S2_VSUBAw:
                    return translateVU_VSUBA_Field(inst);
                case VU0_S2_VMADDAx:
                case VU0_S2_VMADDAy:
                case VU0_S2_VMADDAz:
                case VU0_S2_VMADDAw:
                    return translateVU_VMADDA_Field(inst);
                case VU0_S2_VMSUBAx:
                case VU0_S2_VMSUBAy:
                case VU0_S2_VMSUBAz:
                case VU0_S2_VMSUBAw:
                    return translateVU_VMSUBA_Field(inst);
                case VU0_S2_VMULAx:
                case VU0_S2_VMULAy:
                case VU0_S2_VMULAz:
                case VU0_S2_VMULAw:
                    return translateVU_VMULA_Field(inst);
                case VU0_S2_VADDA:
                    return translateVU_VADDA(inst);
                case VU0_S2_VADDAq:
                    return translateVU_VADDAq(inst);
                case VU0_S2_VADDAi:
                    return translateVU_VADDAi(inst);
                case VU0_S2_VMADDA:
                    return translateVU_VMADDA(inst);
                case VU0_S2_VMADDAq:
                    return translateVU_VMADDAq(inst);
                case VU0_S2_VMADDAi:
                    return translateVU_VMADDAi(inst);
                case VU0_S2_VSUBA:
                    return translateVU_VSUBA(inst);
                case VU0_S2_VSUBAq:
                    return translateVU_VSUBAq(inst);
                case VU0_S2_VSUBAi:
                    return translateVU_VSUBAi(inst);
                case VU0_S2_VMSUBA:
                    return translateVU_VMSUBA(inst);
                case VU0_S2_VMSUBAq:
                    return translateVU_VMSUBAq(inst);
                case VU0_S2_VMSUBAi:
                    return translateVU_VMSUBAi(inst);
                case VU0_S2_VMULA:
                    return translateVU_VMULA(inst);
                case VU0_S2_VMULAq:
                    return translateVU_VMULAq(inst);
                case VU0_S2_VMULAi:
                    return translateVU_VMULAi(inst);
                case VU0_S2_VOPMULA:
                    return translateVU_VOPMULA(inst);
                case VU0_S2_VITOF0:
                    return translateVU_VITOF(inst, 0);
                case VU0_S2_VITOF4:
                    return translateVU_VITOF(inst, 4);
                case VU0_S2_VITOF12:
                    return translateVU_VITOF(inst, 12);
                case VU0_S2_VITOF15:
                    return translateVU_VITOF(inst, 15);
                case VU0_S2_VFTOI0:
                    return translateVU_VFTOI(inst, 0);
                case VU0_S2_VFTOI4:
                    return translateVU_VFTOI(inst, 4);
                case VU0_S2_VFTOI12:
                    return translateVU_VFTOI(inst, 12);
                case VU0_S2_VFTOI15:
                    return translateVU_VFTOI(inst, 15);
                case VU0_S2_VLQI:
                    return translateVU_VLQI(inst);
                case VU0_S2_VSQI:
                    return translateVU_VSQI(inst);
                case VU0_S2_VLQD:
                    return translateVU_VLQD(inst);
                case VU0_S2_VSQD:
                    return translateVU_VSQD(inst);
                case VU0_S2_VDIV:
                    return translateVU_VDIV(inst);
                case VU0_S2_VSQRT:
                    return translateVU_VSQRT(inst);
                case VU0_S2_VRSQRT:
                    return translateVU_VRSQRT(inst);
                case VU0_S2_VWAITQ:
                    return fmt::format("// VWAITQ (Q already resolved in this runtime)");
                case VU0_S2_VMTIR:
                    return translateVU_VMTIR(inst);
                case VU0_S2_VMFIR:
                    return translateVU_VMFIR(inst);
                case VU0_S2_VILWR:
                    return translateVU_VILWR(inst);
                case VU0_S2_VISWR:
                    return translateVU_VISWR(inst);
                case VU0_S2_VABS:
                {
                    uint8_t dest_mask = inst.vectorInfo.vectorField;
                    return fmt::format("{{ __m128 res = _mm_and_ps(ctx->vu0_vf[{}], _mm_castsi128_ps(_mm_set1_epi32(0x7FFFFFFF))); "
                                       "__m128i mask = _mm_set_epi32({}, {}, {}, {}); "
                                       "ctx->vu0_vf[{}] = _mm_blendv_ps(ctx->vu0_vf[{}], res, _mm_castsi128_ps(mask)); }}",
                                       inst.rd,
                                       (dest_mask & 0x8) ? -1 : 0, (dest_mask & 0x4) ? -1 : 0,
                                       (dest_mask & 0x2) ? -1 : 0, (dest_mask & 0x1) ? -1 : 0,
                                       inst.rt, inst.rt);
                }
                case VU0_S2_VMOVE:
                    return fmt::format("ctx->vu0_vf[{}] = ctx->vu0_vf[{}];", inst.rt, inst.rd);
                case VU0_S2_VMR32:
                    return fmt::format("ctx->vu0_vf[{}] = _mm_shuffle_ps(ctx->vu0_vf[{}], ctx->vu0_vf[{}], _MM_SHUFFLE(0,0,0,1));", inst.rt, inst.rd, inst.rd);
                case VU0_S2_VCLIPw:
                {
                    uint8_t field = inst.function & 0x3;
                    std::string shuffle_pattern = fmt::format("_MM_SHUFFLE({},{},{},{})", field, field, field, field);

                    return fmt::format(
                        "{{ __m128 fs = ctx->vu0_vf[{}]; "
                        "__m128 ft = _mm_shuffle_ps(ctx->vu0_vf[{}], ctx->vu0_vf[{}], {}); "
                        "__m128 neg_ft = _mm_xor_ps(ft, _mm_castsi128_ps(_mm_set1_epi32(0x80000000))); "
                        "__m128 gt = _mm_cmpgt_ps(fs, ft); "
                        "__m128 lt = _mm_cmplt_ps(fs, neg_ft); "
                        "uint32_t gt_mask = (uint32_t)_mm_movemask_ps(gt); "
                        "uint32_t lt_mask = (uint32_t)_mm_movemask_ps(lt); "
                        "uint32_t flags = ((lt_mask & 0x1) << 0) | ((gt_mask & 0x1) << 1) | "
                        "((lt_mask & 0x2) << 1) | ((gt_mask & 0x2) << 2) | "
                        "((lt_mask & 0x4) << 2) | ((gt_mask & 0x4) << 3); "
                        "ctx->vu0_clip_flags = ((ctx->vu0_clip_flags << 6) | (flags & 0x3F)) & 0xFFFFFF; }}",
                        inst.rd, inst.rt, inst.rt, shuffle_pattern);
                }
                case VU0_S2_VNOP:
                    return fmt::format("// NOP operation, no action needed for VU0");
                case VU0_S2_VRNEXT:
                    return translateVU_VRNEXT(inst);
                case VU0_S2_VRGET:
                    return translateVU_VRGET(inst);
                case VU0_S2_VRINIT:
                    return translateVU_VRINIT(inst);
                case VU0_S2_VRXOR:
                    return translateVU_VRXOR(inst);
                default:
                    return fmt::format("// Unhandled VU0 Special2 function: 0x{:X}", vu_func);
                }
            }

            // Special1 Table (function-based)
            switch (special1_func)
            {
            case VU0_S1_VADDx:
            case VU0_S1_VADDy:
            case VU0_S1_VADDz:
            case VU0_S1_VADDw:
                return translateVU_VADD_Field(inst);
            case VU0_S1_VSUBx:
            case VU0_S1_VSUBy:
            case VU0_S1_VSUBz:
            case VU0_S1_VSUBw:
                return translateVU_VSUB_Field(inst);
            case VU0_S1_VMULx:
            case VU0_S1_VMULy:
            case VU0_S1_VMULz:
            case VU0_S1_VMULw:
                return translateVU_VMUL_Field(inst);
            case VU0_S1_VADD:
                return translateVU_VADD(inst);
            case VU0_S1_VSUB:
                return translateVU_VSUB(inst);
            case VU0_S1_VMUL:
                return translateVU_VMUL(inst);
            case VU0_S1_VIADD:
                return translateVU_VIADD(inst);
            case VU0_S1_VISUB:
                return translateVU_VISUB(inst);
            case VU0_S1_VIADDI:
                return translateVU_VIADDI(inst);
            case VU0_S1_VIAND:
                return translateVU_VIAND(inst);
            case VU0_S1_VIOR:
                return translateVU_VIOR(inst);
            case VU0_S1_VCALLMS:
                return translateVU_VCALLMS(inst);
            case VU0_S1_VCALLMSR:
                return translateVU_VCALLMSR(inst);
            case VU0_S1_VADDq:
                return translateVU_VADDq(inst);
            case VU0_S1_VSUBq:
                return translateVU_VSUBq(inst);
            case VU0_S1_VMULq:
                return translateVU_VMULq(inst);
            case VU0_S1_VADDi:
                return translateVU_VADDi(inst);
            case VU0_S1_VSUBi:
                return translateVU_VSUBi(inst);
            case VU0_S1_VMULi:
                return translateVU_VMULi(inst);
            case VU0_S1_VMADDx:
            case VU0_S1_VMADDy:
            case VU0_S1_VMADDz:
            case VU0_S1_VMADDw:
                return translateVU_VMADD_Field(inst);
            case VU0_S1_VMSUBx:
            case VU0_S1_VMSUBy:
            case VU0_S1_VMSUBz:
            case VU0_S1_VMSUBw:
                return translateVU_VMSUB_Field(inst);
            case VU0_S1_VMAXx:
            case VU0_S1_VMAXy:
            case VU0_S1_VMAXz:
            case VU0_S1_VMAXw:
                return translateVU_VMAX_Field(inst);
            case VU0_S1_VMINIx:
            case VU0_S1_VMINIy:
            case VU0_S1_VMINIz:
            case VU0_S1_VMINIw:
                return translateVU_VMINI_Field(inst);
            case VU0_S1_VMAXi:
                return translateVU_VMAXi(inst);
            case VU0_S1_VMINIi:
                return translateVU_VMINIi(inst);
            case VU0_S1_VMADD:
                return translateVU_VMADD(inst);
            case VU0_S1_VMADDq:
                return translateVU_VMADDq(inst);
            case VU0_S1_VMADDi:
                return translateVU_VMADDi(inst);
            case VU0_S1_VMAX:
                return translateVU_VMAX(inst);
            case VU0_S1_VOPMSUB:
                return translateVU_VOPMSUB(inst);
            case VU0_S1_VMINI:
                return translateVU_VMINI(inst);
            case VU0_S1_VMSUB:
                return translateVU_VMSUB(inst);
            case VU0_S1_VMSUBq:
                return translateVU_VMSUBq(inst);
            case VU0_S1_VMSUBi:
                return translateVU_VMSUBi(inst);
            default:
                return fmt::format("// Unhandled VU0 Special1 function: 0x{:X}", special1_func);
            }
        }
        default:
            return fmt::format("// Unhandled COP2 format: 0x{:X}", format);
        }
    }

    std::string CodeGenerator::translateVU_VADD_Field(const Instruction &inst)
    {
        uint8_t vfd = inst.sa;
        uint8_t vfs = inst.rd;
        uint8_t vft = inst.rt;
        uint8_t dest_mask = inst.vectorInfo.vectorField;
        uint8_t field = inst.function & 0x3;
        std::string shuffle_pattern = fmt::format("_MM_SHUFFLE({},{},{},{})", field, field, field, field);
        return fmt::format("{{ __m128 res = PS2_VADD(ctx->vu0_vf[{}], _mm_shuffle_ps(ctx->vu0_vf[{}], ctx->vu0_vf[{}], {})); __m128i mask = _mm_set_epi32({}, {}, {}, {}); ctx->vu0_vf[{}] = _mm_blendv_ps(ctx->vu0_vf[{}], res, _mm_castsi128_ps(mask)); }}", vfs, vft, vft, shuffle_pattern, (dest_mask & 0x8) ? -1 : 0, (dest_mask & 0x4) ? -1 : 0, (dest_mask & 0x2) ? -1 : 0, (dest_mask & 0x1) ? -1 : 0, vfd, vfd);
    }

    std::string CodeGenerator::translateVU_VSUB_Field(const Instruction &inst)
    {
        uint8_t vfd = inst.sa;
        uint8_t vfs = inst.rd;
        uint8_t vft = inst.rt;
        uint8_t dest_mask = inst.vectorInfo.vectorField;
        uint8_t field = inst.function & 0x3;
        std::string shuffle_pattern = fmt::format("_MM_SHUFFLE({},{},{},{})", field, field, field, field);
        return fmt::format("{{ __m128 res = PS2_VSUB(ctx->vu0_vf[{}], _mm_shuffle_ps(ctx->vu0_vf[{}], ctx->vu0_vf[{}], {})); __m128i mask = _mm_set_epi32({}, {}, {}, {}); ctx->vu0_vf[{}] = _mm_blendv_ps(ctx->vu0_vf[{}], res, _mm_castsi128_ps(mask)); }}", vfs, vft, vft, shuffle_pattern, (dest_mask & 0x8) ? -1 : 0, (dest_mask & 0x4) ? -1 : 0, (dest_mask & 0x2) ? -1 : 0, (dest_mask & 0x1) ? -1 : 0, vfd, vfd);
    }

    std::string CodeGenerator::translateVU_VMUL_Field(const Instruction &inst)
    {
        uint8_t vfd = inst.sa;
        uint8_t vfs = inst.rd;
        uint8_t vft = inst.rt;
        uint8_t dest_mask = inst.vectorInfo.vectorField;
        uint8_t field = inst.function & 0x3;
        std::string shuffle_pattern = fmt::format("_MM_SHUFFLE({},{},{},{})", field, field, field, field);
        return fmt::format("{{ __m128 res = PS2_VMUL(ctx->vu0_vf[{}], _mm_shuffle_ps(ctx->vu0_vf[{}], ctx->vu0_vf[{}], {})); __m128i mask = _mm_set_epi32({}, {}, {}, {}); ctx->vu0_vf[{}] = _mm_blendv_ps(ctx->vu0_vf[{}], res, _mm_castsi128_ps(mask)); }}", vfs, vft, vft, shuffle_pattern, (dest_mask & 0x8) ? -1 : 0, (dest_mask & 0x4) ? -1 : 0, (dest_mask & 0x2) ? -1 : 0, (dest_mask & 0x1) ? -1 : 0, vfd, vfd);
    }

    std::string CodeGenerator::translateVU_VADD(const Instruction &inst)
    {
        uint8_t vfd = inst.sa;
        uint8_t vfs = inst.rd;
        uint8_t vft = inst.rt;
        uint8_t dest_mask = inst.vectorInfo.vectorField;
        return fmt::format("{{ __m128 res = PS2_VADD(ctx->vu0_vf[{}], ctx->vu0_vf[{}]); __m128i mask = _mm_set_epi32({}, {}, {}, {}); ctx->vu0_vf[{}] = PS2_VBLEND(ctx->vu0_vf[{}], res, _mm_castsi128_ps(mask)); }}", vfs, vft, (dest_mask & 0x8) ? -1 : 0, (dest_mask & 0x4) ? -1 : 0, (dest_mask & 0x2) ? -1 : 0, (dest_mask & 0x1) ? -1 : 0, vfd, vfd);
    }

    std::string CodeGenerator::translateVU_VSUB(const Instruction &inst)
    {
        uint8_t vfd = inst.sa;
        uint8_t vfs = inst.rd;
        uint8_t vft = inst.rt;
        uint8_t dest_mask = inst.vectorInfo.vectorField;
        return fmt::format("{{ __m128 res = PS2_VSUB(ctx->vu0_vf[{}], ctx->vu0_vf[{}]); __m128i mask = _mm_set_epi32({}, {}, {}, {}); ctx->vu0_vf[{}] = PS2_VBLEND(ctx->vu0_vf[{}], res, _mm_castsi128_ps(mask)); }}", vfs, vft, (dest_mask & 0x8) ? -1 : 0, (dest_mask & 0x4) ? -1 : 0, (dest_mask & 0x2) ? -1 : 0, (dest_mask & 0x1) ? -1 : 0, vfd, vfd);
    }

    std::string CodeGenerator::translateVU_VMUL(const Instruction &inst)
    {
        uint8_t vfd = inst.sa;
        uint8_t vfs = inst.rd;
        uint8_t vft = inst.rt;
        uint8_t dest_mask = inst.vectorInfo.vectorField;
        return fmt::format("{{ __m128 res = PS2_VMUL(ctx->vu0_vf[{}], ctx->vu0_vf[{}]); __m128i mask = _mm_set_epi32({}, {}, {}, {}); ctx->vu0_vf[{}] = PS2_VBLEND(ctx->vu0_vf[{}], res, _mm_castsi128_ps(mask)); }}", vfs, vft, (dest_mask & 0x8) ? -1 : 0, (dest_mask & 0x4) ? -1 : 0, (dest_mask & 0x2) ? -1 : 0, (dest_mask & 0x1) ? -1 : 0, vfd, vfd);
    }

    std::string CodeGenerator::translatePEXT5(const Instruction &inst)
    {
        return fmt::format(
            "{{ __m128i rt = GPR_VEC(ctx, {}); \n"
            "   __m128i m1 = _mm_set1_epi32(0x0000001F); \n"
            "   __m128i m2 = _mm_set1_epi32(0x000003E0); \n"
            "   __m128i m3 = _mm_set1_epi32(0x00007C00); \n"
            "   __m128i m4 = _mm_set1_epi32(0x00008000); \n"
            "   __m128i a1 = _mm_slli_epi32(_mm_and_si128(rt, m1), 3); \n"
            "   __m128i a2 = _mm_slli_epi32(_mm_and_si128(rt, m2), 6); \n"
            "   __m128i a3 = _mm_slli_epi32(_mm_and_si128(rt, m3), 9); \n"
            "   __m128i a4 = _mm_slli_epi32(_mm_and_si128(rt, m4), 16); \n"
            "   SET_GPR_VEC(ctx, {}, _mm_or_si128(_mm_or_si128(a1, a2), _mm_or_si128(a3, a4))); }}",
            inst.rt, inst.rd);
    }

    std::string CodeGenerator::translatePPAC5(const Instruction &inst)
    {
        return fmt::format(
            "{{ __m128i rt = GPR_VEC(ctx, {}); \n"
            "   __m128i m1 = _mm_set1_epi32(0x0000001F); \n"
            "   __m128i m2 = _mm_set1_epi32(0x000003E0); \n"
            "   __m128i m3 = _mm_set1_epi32(0x00007C00); \n"
            "   __m128i m4 = _mm_set1_epi32(0x00008000); \n"
            "   __m128i a1 = _mm_and_si128(_mm_srli_epi32(rt, 3), m1); \n"
            "   __m128i a2 = _mm_and_si128(_mm_srli_epi32(rt, 6), m2); \n"
            "   __m128i a3 = _mm_and_si128(_mm_srli_epi32(rt, 9), m3); \n"
            "   __m128i a4 = _mm_and_si128(_mm_srli_epi32(rt, 16), m4); \n"
            "   SET_GPR_VEC(ctx, {}, _mm_or_si128(_mm_or_si128(a1, a2), _mm_or_si128(a3, a4))); }}",
            inst.rt, inst.rd);
    }

    std::string CodeGenerator::translatePADSBH(const Instruction &inst)
    {
        return fmt::format(
            "{{ __m128i rs = GPR_VEC(ctx, {}); __m128i rt = GPR_VEC(ctx, {}); \n"
            "   __m128i sub = _mm_sub_epi16(rs, rt); \n"
            "   __m128i add = _mm_add_epi16(rs, rt); \n"
            "   SET_GPR_VEC(ctx, {}, _mm_unpacklo_epi64(sub, _mm_unpackhi_epi64(add, add))); }}",
            inst.rs, inst.rt, inst.rd);
    }

    std::string CodeGenerator::translatePMADDW(const Instruction &inst)
    {
        return fmt::format("{{ __m128i p01 = _mm_mul_epu32(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})); \n"
                           "   __m128i p23 = _mm_mul_epu32(_mm_srli_si128(GPR_VEC(ctx, {}), 8), _mm_srli_si128(GPR_VEC(ctx, {}), 8)); \n"
                           "   uint64_t acc = Ps2HiLoToU64(ctx->hi, ctx->lo); \n"
                           "   acc += _mm_cvtsi128_si64(p01); \n"
                           "   acc += _mm_cvtsi128_si64(_mm_srli_si128(p01, 8)); \n"
                           "   acc += _mm_cvtsi128_si64(p23); \n"
                           "   acc += _mm_cvtsi128_si64(_mm_srli_si128(p23, 8)); \n"
                           "   ctx->lo = (uint32_t)acc; ctx->hi = (uint32_t)(acc >> 32); \n"
                           "   SET_GPR_U64(ctx, {}, acc); }}",
                           inst.rs, inst.rt, inst.rs, inst.rt, inst.rd);
    }

    std::string CodeGenerator::translatePMSUBW(const Instruction &inst)
    {
        return fmt::format("{{ __m128i p01 = _mm_mul_epu32(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})); \n"
                           "   __m128i p23 = _mm_mul_epu32(_mm_srli_si128(GPR_VEC(ctx, {}), 8), _mm_srli_si128(GPR_VEC(ctx, {}), 8)); \n"
                           "   uint64_t acc = Ps2HiLoToU64(ctx->hi, ctx->lo); \n"
                           "   acc -= _mm_cvtsi128_si64(p01); \n"
                           "   acc -= _mm_cvtsi128_si64(_mm_srli_si128(p01, 8)); \n"
                           "   acc -= _mm_cvtsi128_si64(p23); \n"
                           "   acc -= _mm_cvtsi128_si64(_mm_srli_si128(p23, 8)); \n"
                           "   ctx->lo = (uint32_t)acc; ctx->hi = (uint32_t)(acc >> 32); \n"
                           "   SET_GPR_U64(ctx, {}, acc); }}",
                           inst.rs, inst.rt, inst.rs, inst.rt, inst.rd);
    }

    std::string CodeGenerator::translatePMULTW(const Instruction &inst)
    {
        return fmt::format("{{ __m128i p01 = _mm_mul_epu32(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})); \n"
                           "   __m128i p23 = _mm_mul_epu32(_mm_srli_si128(GPR_VEC(ctx, {}), 8), _mm_srli_si128(GPR_VEC(ctx, {}), 8)); \n"
                           "   uint64_t acc = 0; \n"
                           "   acc += _mm_cvtsi128_si64(p01); \n"
                           "   acc += _mm_cvtsi128_si64(_mm_srli_si128(p01, 8)); \n"
                           "   acc += _mm_cvtsi128_si64(p23); \n"
                           "   acc += _mm_cvtsi128_si64(_mm_srli_si128(p23, 8)); \n"
                           "   ctx->lo = (uint32_t)acc; ctx->hi = (uint32_t)(acc >> 32); \n"
                           "   SET_GPR_U64(ctx, {}, acc); }}",
                           inst.rs, inst.rt, inst.rs, inst.rt, inst.rd);
    }

    std::string CodeGenerator::translatePMADDUW(const Instruction &inst)
    {
        return fmt::format("{{ __m128i p01 = _mm_mul_epu32(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})); \n"
                           "   __m128i p23 = _mm_mul_epu32(_mm_srli_si128(GPR_VEC(ctx, {}), 8), _mm_srli_si128(GPR_VEC(ctx, {}), 8)); \n"
                           "   uint64_t acc = Ps2HiLoToU64(ctx->hi, ctx->lo); \n"
                           "   acc += _mm_cvtsi128_si64(p01); \n"
                           "   acc += _mm_cvtsi128_si64(_mm_srli_si128(p01, 8)); \n"
                           "   acc += _mm_cvtsi128_si64(p23); \n"
                           "   acc += _mm_cvtsi128_si64(_mm_srli_si128(p23, 8)); \n"
                           "   ctx->lo = (uint32_t)acc; ctx->hi = (uint32_t)(acc >> 32); \n"
                           "   SET_GPR_U64(ctx, {}, acc); }}",
                           inst.rs, inst.rt, inst.rs, inst.rt, inst.rd);
    }

    std::string CodeGenerator::translatePDIVW(const Instruction &inst)
    {
        // Only divides the first word element rs[0] / rt[0]
        return fmt::format("{{ int32_t rs0 = GPR_S32(ctx, {}); int32_t rt0 = GPR_S32(ctx, {}); \n"
                           "   if (rt0 != 0) {{ \n"
                           "       if (rt0 == -1 && rs0 == INT32_MIN) {{ ctx->lo = (uint32_t)INT32_MIN; ctx->hi = 0; }} \n"
                           "       else {{ ctx->lo = (uint32_t)(rs0 / rt0); ctx->hi = (uint32_t)(rs0 % rt0); }} \n"
                           "   }} else {{ ctx->lo = (rs0 < 0) ? 1 : -1; ctx->hi = (uint32_t)rs0; }} \n"
                           "   SET_GPR_U32(ctx, {}, ctx->lo); }}",
                           inst.rs, inst.rt, inst.rd);
    }

    std::string CodeGenerator::translatePCPYLD(const Instruction &inst)
    {
        // PCPYLD uses rs as the upper source and rt as the lower source.
        return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PCPYLD(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));",
                           inst.rd, inst.rs, inst.rt);
    }

    std::string CodeGenerator::translatePMADDH(const Instruction &inst)
    {
        // Parallel multiply add halfword -> results to HI/LO and rd
        return fmt::format("{{ __m128i prod = _mm_madd_epi16(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})); \n" // Packed multiply and add adjacent pairs
                           "   int32_t p0 = _mm_cvtsi128_si32(prod); \n"
                           "   int32_t p1 = _mm_cvtsi128_si32(_mm_srli_si128(prod, 4)); \n"
                           "   int32_t p2 = _mm_cvtsi128_si32(_mm_srli_si128(prod, 8)); \n"
                           "   int32_t p3 = _mm_cvtsi128_si32(_mm_srli_si128(prod, 12)); \n"
                           "   int64_t acc = Ps2HiLoToU64(ctx->hi, ctx->lo); \n"
                           "   acc += (int64_t)p0 + (int64_t)p1 + (int64_t)p2 + (int64_t)p3; \n"
                           "   ctx->lo = (uint32_t)acc; ctx->hi = (uint32_t)(acc >> 32); \n"
                           "   SET_GPR_U64(ctx, {}, acc); }}",
                           inst.rs, inst.rt, inst.rd);
    }

    std::string CodeGenerator::translatePHMADH(const Instruction &inst)
    {
        // Parallel Horizontal Multiply Add Halfword -> results to HI/LO and rd
        return fmt::format("{{ __m128i evens = _mm_shuffle_epi32(GPR_VEC(ctx, {}), _MM_SHUFFLE(2,0,2,0)); \n" // Select even halfwords
                           "   __m128i odds  = _mm_shuffle_epi32(GPR_VEC(ctx, {}), _MM_SHUFFLE(3,1,3,1)); \n" // Select odd halfwords
                           "   __m128i prod_ev = _mm_mullo_epi16(evens, _mm_shuffle_epi32(GPR_VEC(ctx, {}), _MM_SHUFFLE(2,0,2,0))); \n"
                           "   __m128i prod_od = _mm_mullo_epi16(odds,  _mm_shuffle_epi32(GPR_VEC(ctx, {}), _MM_SHUFFLE(3,1,3,1))); \n"
                           "   __m128i sum_pairs = _mm_add_epi16(prod_ev, prod_od); \n"                            // Add rs[0]*rt[0] + rs[1]*rt[1], etc.
                           "   int32_t h0 = _mm_extract_epi16(sum_pairs, 0) + _mm_extract_epi16(sum_pairs, 1); \n" // Horizontal add within low 32b
                           "   int32_t h1 = _mm_extract_epi16(sum_pairs, 2) + _mm_extract_epi16(sum_pairs, 3); \n" // Horizontal add within next 32b
                           "   int32_t h2 = _mm_extract_epi16(sum_pairs, 4) + _mm_extract_epi16(sum_pairs, 5); \n"
                           "   int32_t h3 = _mm_extract_epi16(sum_pairs, 6) + _mm_extract_epi16(sum_pairs, 7); \n"
                           "   int64_t acc = Ps2HiLoToU64(ctx->hi, ctx->lo); \n"
                           "   acc += (int64_t)h0 + (int64_t)h1 + (int64_t)h2 + (int64_t)h3; \n"
                           "   ctx->lo = (uint32_t)acc; ctx->hi = (uint32_t)(acc >> 32); \n"
                           "   SET_GPR_U64(ctx, {}, acc); }}",
                           inst.rs, inst.rt, inst.rs, inst.rt, inst.rd);
    }

    std::string CodeGenerator::translatePMSUBH(const Instruction &inst)
    {
        return fmt::format("{{ __m128i prod = _mm_madd_epi16(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})); \n"
                           "   int32_t p0 = _mm_cvtsi128_si32(prod); \n"
                           "   int32_t p1 = _mm_cvtsi128_si32(_mm_srli_si128(prod, 4)); \n"
                           "   int32_t p2 = _mm_cvtsi128_si32(_mm_srli_si128(prod, 8)); \n"
                           "   int32_t p3 = _mm_cvtsi128_si32(_mm_srli_si128(prod, 12)); \n"
                           "   int64_t acc = Ps2HiLoToU64(ctx->hi, ctx->lo); \n"
                           "   acc -= (int64_t)p0 + (int64_t)p1 + (int64_t)p2 + (int64_t)p3; \n"
                           "   ctx->lo = (uint32_t)acc; ctx->hi = (uint32_t)(acc >> 32); \n"
                           "   SET_GPR_U64(ctx, {}, acc); }}",
                           inst.rs, inst.rt, inst.rd);
    }

    std::string CodeGenerator::translatePHMSBH(const Instruction &inst)
    {
        return fmt::format("{{ __m128i evens = _mm_shuffle_epi32(GPR_VEC(ctx, {}), _MM_SHUFFLE(2,0,2,0)); \n"
                           "   __m128i odds  = _mm_shuffle_epi32(GPR_VEC(ctx, {}), _MM_SHUFFLE(3,1,3,1)); \n"
                           "   __m128i prod_ev = _mm_mullo_epi16(evens, _mm_shuffle_epi32(GPR_VEC(ctx, {}), _MM_SHUFFLE(2,0,2,0))); \n"
                           "   __m128i prod_od = _mm_mullo_epi16(odds,  _mm_shuffle_epi32(GPR_VEC(ctx, {}), _MM_SHUFFLE(3,1,3,1))); \n"
                           "   __m128i sub_pairs = _mm_sub_epi16(prod_od, prod_ev); \n"
                           "   int32_t h0 = _mm_extract_epi16(sub_pairs, 0) + _mm_extract_epi16(sub_pairs, 1); \n"
                           "   int32_t h1 = _mm_extract_epi16(sub_pairs, 2) + _mm_extract_epi16(sub_pairs, 3); \n"
                           "   int32_t h2 = _mm_extract_epi16(sub_pairs, 4) + _mm_extract_epi16(sub_pairs, 5); \n"
                           "   int32_t h3 = _mm_extract_epi16(sub_pairs, 6) + _mm_extract_epi16(sub_pairs, 7); \n"
                           "   int64_t acc = Ps2HiLoToU64(ctx->hi, ctx->lo); \n"
                           "   acc += (int64_t)h0 + (int64_t)h1 + (int64_t)h2 + (int64_t)h3; \n"
                           "   ctx->lo = (uint32_t)acc; ctx->hi = (uint32_t)(acc >> 32); \n"
                           "   SET_GPR_U64(ctx, {}, acc); }}",
                           inst.rs, inst.rt, inst.rs, inst.rt, inst.rd);
    }

    std::string CodeGenerator::translatePEXEH(const Instruction &inst)
    {
        // Swaps halfwords 1<->3 and 5<->7 within the 128-bit register
        return fmt::format("SET_GPR_VEC(ctx, {}, _mm_shufflelo_epi16(_mm_shufflehi_epi16(GPR_VEC(ctx, {}), _MM_SHUFFLE(2,3,0,1)), _MM_SHUFFLE(2,3,0,1)));",
                           inst.rd, inst.rs);
    }

    std::string CodeGenerator::translatePREVH(const Instruction &inst)
    {
        // Reverses the order of the 8 halfwords
        return fmt::format("{{ __m128i mask = _mm_setr_epi8(14,15, 12,13, 10,11, 8,9, 6,7, 4,5, 2,3, 0,1); "
                           "SET_GPR_VEC(ctx, {}, PS2_SHUFFLE_EPI8(GPR_VEC(ctx, {}), mask)); }}",
                           inst.rd, inst.rs);
    }

    std::string CodeGenerator::translatePMULTH(const Instruction &inst)
    {
        // Parallel multiply halfword, results sum to HI/LO and rd
        return fmt::format("{{ __m128i prod = _mm_madd_epi16(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})); \n"
                           "   int32_t p0 = _mm_cvtsi128_si32(prod); \n"
                           "   int32_t p1 = _mm_cvtsi128_si32(_mm_srli_si128(prod, 4)); \n"
                           "   int32_t p2 = _mm_cvtsi128_si32(_mm_srli_si128(prod, 8)); \n"
                           "   int32_t p3 = _mm_cvtsi128_si32(_mm_srli_si128(prod, 12)); \n"
                           "   int64_t result = (int64_t)p0 + (int64_t)p1 + (int64_t)p2 + (int64_t)p3; \n"
                           "   ctx->lo = (uint32_t)result; ctx->hi = (uint32_t)(result >> 32); \n"
                           "   SET_GPR_U64(ctx, {}, result); }}",
                           inst.rs, inst.rt, inst.rd);
    }

    std::string CodeGenerator::translatePDIVBW(const Instruction &inst)
    {
        return fmt::format(
            "{{\n"
            "    __m128i rsVec = GPR_VEC(ctx, {});\n"
            "    __m128i rtVec = GPR_VEC(ctx, {});\n"
            "    alignas(16) int32_t rsWords[4];\n"
            "    alignas(16) int32_t rtWords[4];\n"
            "    _mm_store_si128((__m128i*)rsWords, rsVec);\n"
            "    _mm_store_si128((__m128i*)rtWords, rtVec);\n"
            "    int32_t div = rtWords[0];\n"
            "    int32_t q0 = 0, q1 = 0, q2 = 0, q3 = 0;\n"
            "    if (div != 0) {{\n"
            "        q0 = rsWords[0] / div; ctx->lo = (uint32_t)q0; ctx->hi = (uint32_t)(rsWords[0] % div);\n"
            "        q1 = rsWords[1] / div;\n"
            "        q2 = rsWords[2] / div;\n"
            "        q3 = rsWords[3] / div;\n"
            "    }} else {{\n"
            "        ctx->lo = (rsWords[0] < 0) ? 1 : -1;\n"
            "        ctx->hi = (uint32_t)rsWords[0];\n"
            "    }}\n"
            "    SET_GPR_VEC(ctx, {}, _mm_set_epi32(q3, q2, q1, q0));\n"
            "}}",
            inst.rs, inst.rt, inst.rd);
    }

    std::string CodeGenerator::translatePEXEW(const Instruction &inst)
    {
        return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PEXEW(GPR_VEC(ctx, {})));",
                           inst.rd, inst.rs);
    }

    std::string CodeGenerator::translatePROT3W(const Instruction &inst)
    {
        // Rotates words left by 3: [d,c,b,a] -> [a,d,c,b]
        return fmt::format("SET_GPR_VEC(ctx, {}, _mm_shuffle_epi32(GPR_VEC(ctx, {}), _MM_SHUFFLE(0,3,2,1)));",
                           inst.rd, inst.rs);
    }

    std::string CodeGenerator::translatePMULTUW(const Instruction &inst)
    {
        // Parallel multiply unsigned word -> results to HI/LO and rd (lower 32 bits)
        return fmt::format("{{ __m128i p01 = _mm_mul_epu32(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})); \n"
                           "   __m128i p23 = _mm_mul_epu32(_mm_srli_si128(GPR_VEC(ctx, {}), 8), _mm_srli_si128(GPR_VEC(ctx, {}), 8)); \n"
                           "   uint64_t res0 = _mm_cvtsi128_si64(p01); uint64_t res1 = _mm_cvtsi128_si64(_mm_srli_si128(p01, 8)); \n"
                           "   uint64_t res2 = _mm_cvtsi128_si64(p23); uint64_t res3 = _mm_cvtsi128_si64(_mm_srli_si128(p23, 8)); \n"
                           "   ctx->lo = (uint32_t)res0; ctx->hi = (uint32_t)(res0 >> 32); \n" // HI/LO from first product only
                           "   SET_GPR_VEC(ctx, {}, _mm_set_epi32((uint32_t)res3, (uint32_t)res2, (uint32_t)res1, (uint32_t)res0)); }}",
                           inst.rs, inst.rt, inst.rs, inst.rt, inst.rd);
    }

    std::string CodeGenerator::translatePDIVUW(const Instruction &inst)
    {
        // Parallel divide unsigned word (only first element) -> results to HI/LO and rd (quotient)
        return fmt::format("{{ uint32_t rs0 = GPR_U32(ctx, {}); uint32_t rt0 = GPR_U32(ctx, {}); \n"
                           "   if (rt0 != 0) {{ ctx->lo = rs0 / rt0; ctx->hi = rs0 % rt0; }} \n"
                           "   else {{ ctx->lo = 0xFFFFFFFF; ctx->hi = rs0; }} \n" // Div by zero behavior
                           "   SET_GPR_U32(ctx, {}, ctx->lo); }}",
                           inst.rs, inst.rt, inst.rd);
    }

    std::string CodeGenerator::translatePCPYUD(const Instruction &inst)
    {
        // Copies upper 64 of rs to lower 64 of rd, upper 64 of rt to upper 64 of rd
        return fmt::format("SET_GPR_VEC(ctx, {}, _mm_unpackhi_epi64(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));",
                           inst.rd, inst.rs, inst.rt); // Order matters
    }

    std::string CodeGenerator::translatePEXCH(const Instruction &inst)
    {
        // Parallel Exchange Center Halfword (same as MMI2 PEXEH)
        return fmt::format("SET_GPR_VEC(ctx, {}, _mm_shufflelo_epi16(_mm_shufflehi_epi16(GPR_VEC(ctx, {}), _MM_SHUFFLE(2,3,0,1)), _MM_SHUFFLE(2,3,0,1)));",
                           inst.rd, inst.rs);
    }

    std::string CodeGenerator::translatePCPYH(const Instruction &inst)
    {
        // Parallel Copy Halfword (Broadcast lower 16 bits of each 64-bit half)
        return fmt::format("{{ __m128i src = GPR_VEC(ctx, {}); uint16_t l = _mm_extract_epi16(src, 0); uint16_t h = _mm_extract_epi16(src, 4); \n"
                           "   SET_GPR_VEC(ctx, {}, _mm_set_epi16(h,h,h,h, l,l,l,l)); }}",
                           inst.rs, inst.rd);
    }

    std::string CodeGenerator::translatePEXCW(const Instruction &inst)
    {
        // Parallel Exchange Center Word (Swaps words 0<>2, 1<>3)
        return fmt::format("SET_GPR_VEC(ctx, {}, _mm_shuffle_epi32(GPR_VEC(ctx, {}), _MM_SHUFFLE(1,0,3,2)));",
                           inst.rd, inst.rs);
    }

    std::string CodeGenerator::translatePMTHI(const Instruction &inst)
    {
        return fmt::format("ctx->hi = GPR_U32(ctx, {});", inst.rs); // PMTHI uses standard HI/LO
    }

    std::string CodeGenerator::translatePMTLO(const Instruction &inst)
    {
        return fmt::format("ctx->lo = GPR_U32(ctx, {});", inst.rs); // PMTLO uses standard HI/LO
    }

    std::string CodeGenerator::translateVU_VDIV(const Instruction &inst)
    {
        uint8_t fsf = inst.vectorInfo.fsf;
        uint8_t ftf = inst.vectorInfo.ftf;
        uint8_t fs_reg = inst.rd;
        uint8_t ft_reg = inst.rt;

        return fmt::format("{{ float fs = _mm_cvtss_f32(_mm_shuffle_ps(ctx->vu0_vf[{}], ctx->vu0_vf[{}], _MM_SHUFFLE(0,0,0,{}))); float ft = _mm_cvtss_f32(_mm_shuffle_ps(ctx->vu0_vf[{}], ctx->vu0_vf[{}], _MM_SHUFFLE(0,0,0,{}))); ctx->vu0_q = (ft != 0.0f) ? (fs / ft) : 0.0f; }}", fs_reg, fs_reg, fsf, ft_reg, ft_reg, ftf);
    }

    std::string CodeGenerator::translateVU_VSQRT(const Instruction &inst)
    {
        uint8_t ftf = inst.vectorInfo.ftf;
        uint8_t ft_reg = inst.rt;
        return fmt::format("{{ float ft = _mm_cvtss_f32(_mm_shuffle_ps(ctx->vu0_vf[{}], ctx->vu0_vf[{}], _MM_SHUFFLE(0,0,0,{}))); ctx->vu0_q = sqrtf(std::max(0.0f, ft)); }}", ft_reg, ft_reg, ftf);
    }

    std::string CodeGenerator::translateVU_VRSQRT(const Instruction &inst)
    {
        uint8_t ftf = inst.vectorInfo.ftf;
        uint8_t ft_reg = inst.rt;
        return fmt::format("{{ float ft = _mm_cvtss_f32(_mm_shuffle_ps(ctx->vu0_vf[{}], ctx->vu0_vf[{}], _MM_SHUFFLE(0,0,0,{}))); ctx->vu0_q = (ft > 0.0f) ? (1.0f / sqrtf(ft)) : 0.0f; }}", ft_reg, ft_reg, ftf);
    }

    std::string CodeGenerator::translateVU_VMTIR(const Instruction &inst)
    {
        uint8_t fsf = inst.vectorInfo.fsf;
        return fmt::format("{{ uint32_t bits; float src = _mm_cvtss_f32(_mm_shuffle_ps(ctx->vu0_vf[{}], ctx->vu0_vf[{}], _MM_SHUFFLE(0,0,0,{}))); std::memcpy(&bits, &src, sizeof(bits)); ctx->vi[{}] = (uint16_t)(bits & 0xFFFF); }}", inst.rd, inst.rd, fsf, inst.rt);
    }

    std::string CodeGenerator::translateVU_VMFIR(const Instruction &inst)
    {
        uint8_t dest_mask = inst.vectorInfo.vectorField;
        return fmt::format("{{ uint32_t tmp = (uint32_t)(int32_t)(int16_t)ctx->vi[{}]; float val; std::memcpy(&val, &tmp, sizeof(val)); "
                           "__m128 res = _mm_set1_ps(val); "
                           "__m128i mask = _mm_set_epi32({}, {}, {}, {}); "
                           "ctx->vu0_vf[{}] = _mm_blendv_ps(ctx->vu0_vf[{}], res, _mm_castsi128_ps(mask)); }}",
                           inst.rd,
                           (dest_mask & 0x8) ? -1 : 0, (dest_mask & 0x4) ? -1 : 0,
                           (dest_mask & 0x2) ? -1 : 0, (dest_mask & 0x1) ? -1 : 0,
                           inst.rt, inst.rt);
    }

    std::string CodeGenerator::translateVU_VILWR(const Instruction &inst)
    {
        return fmt::format("{{ uint32_t addr = (uint32_t)(ctx->vi[{}] << 2) & 0x3FFC; ctx->vi[{}] = static_cast<uint16_t>(READ32(addr)); }}", inst.rd, inst.rt); // VILWR.<f> vit, (vis)
    }

    std::string CodeGenerator::translateVU_VISWR(const Instruction &inst)
    {
        return fmt::format("{{ uint32_t addr = (uint32_t)(ctx->vi[{}] << 2) & 0x3FFC; WRITE32(addr, (uint32_t)ctx->vi[{}]); }}", inst.rd, inst.rt); // VISWR.<f> vit, (vis)
    }

    std::string CodeGenerator::translateVU_VIADD(const Instruction &inst)
    {
        return fmt::format("ctx->vi[{}] = ctx->vi[{}] + ctx->vi[{}];", inst.sa, inst.rd, inst.rt); // vid, vis, vit
    }

    std::string CodeGenerator::translateVU_VISUB(const Instruction &inst)
    {
        return fmt::format("ctx->vi[{}] = ctx->vi[{}] - ctx->vi[{}];", inst.sa, inst.rd, inst.rt); // vid, vis, vit
    }

    std::string CodeGenerator::translateVU_VIADDI(const Instruction &inst)
    {
        int32_t imm5 = (inst.sa & 0x10) ? static_cast<int32_t>(inst.sa | ~0x1F) : static_cast<int32_t>(inst.sa);
        return fmt::format("ctx->vi[{}] = ctx->vi[{}] + {};", inst.rt, inst.rd, imm5); // vit, vis, imm5
    }

    std::string CodeGenerator::translateVU_VIAND(const Instruction &inst)
    {
        return fmt::format("ctx->vi[{}] = ctx->vi[{}] & ctx->vi[{}];", inst.sa, inst.rd, inst.rt); // vid, vis, vit
    }

    std::string CodeGenerator::translateVU_VIOR(const Instruction &inst)
    {
        return fmt::format("ctx->vi[{}] = ctx->vi[{}] | ctx->vi[{}];", inst.sa, inst.rd, inst.rt); // vid, vis, vit
    }

    std::string CodeGenerator::translateVU_VCALLMS(const Instruction &inst)
    {
        // VCALLMS calls a VU0 microprogram at the specified immediate address.
        // VU0 micro memory is 4KB = 512 instructions (8 bytes each). Index is 0-511.
        uint16_t instr_index = static_cast<uint16_t>((inst.raw >> 6) & 0x1FF); // imm15[8:0]
        uint32_t target_byte_addr = static_cast<uint32_t>(instr_index) << 3;   // Convert instruction index to byte address

        return fmt::format(
            "{{ "
            "    ctx->vu0_tpc = 0x{:X}; " // Set target program counter
            "    runtime->executeVU0Microprogram(rdram, ctx, 0x{:X}); "
            "}}",
            target_byte_addr, target_byte_addr);
    }

    std::string CodeGenerator::translateVU_VCALLMSR(const Instruction &inst)
    {
        // VCALLMSR calls a VU0 microprogram at address stored in integer register
        uint8_t vis_reg_idx = inst.rd; // Source integer register (vis)

        return fmt::format(
            "{{ "
            "    uint16_t instr_index = ctx->vi[{}] & 0x1FF; "             // Get instruction index from VI[IS], mask to 9 bits
            "    uint32_t target_byte_addr = (uint32_t)instr_index << 3; " // Convert to byte address
            "    ctx->vu0_pc = target_byte_addr; "
            "    runtime->vu0StartMicroProgram(rdram, ctx, target_byte_addr); "
            "}}",
            vis_reg_idx);
    }

    std::string CodeGenerator::translateVU_VRNEXT(const Instruction &inst)
    {
        return fmt::format(
            "{{ "
            "    uint32_t r_vals[4]; "
            "    _mm_storeu_si128((__m128i*)r_vals, _mm_castps_si128(ctx->vu0_r)); "
            "    "
            "    // Simple LFSR-based random number generation (PS2-like behavior) "
            "    uint32_t feedback = r_vals[0] ^ (r_vals[0] << 13) ^ (r_vals[1] >> 19) ^ (r_vals[2] << 7); "
            "    r_vals[0] = r_vals[1]; "
            "    r_vals[1] = r_vals[2]; "
            "    r_vals[2] = r_vals[3]; "
            "    r_vals[3] = feedback; "
            "    "
            "    ctx->vu0_r = _mm_castsi128_ps(_mm_loadu_si128((__m128i*)r_vals)); \n"
            "}}");
    }

    std::string CodeGenerator::translateVU_VMADD_Field(const Instruction &inst)
    {
        uint8_t vfd = inst.sa;
        uint8_t vfs = inst.rd;
        uint8_t vft = inst.rt;
        uint8_t dest_mask = inst.vectorInfo.vectorField;
        uint8_t field = inst.function & 0x3; // Extract field from function code

        // Pre-construct the shuffle pattern to avoid format string issues
        std::string shuffle_pattern = fmt::format("_MM_SHUFFLE({},{},{},{})", field, field, field, field);

        return fmt::format("{{ __m128 mul_res = PS2_VMUL(ctx->vu0_vf[{}], _mm_shuffle_ps(ctx->vu0_vf[{}], ctx->vu0_vf[{}], {})); "
                           "__m128 res = PS2_VADD(ctx->vu0_acc, mul_res); "
                           "__m128i mask = _mm_set_epi32({}, {}, {}, {}); "
                           "ctx->vu0_vf[{}] = _mm_blendv_ps(ctx->vu0_vf[{}], res, _mm_castsi128_ps(mask)); "
                           "ctx->vu0_acc = res; }}",
                           vfs, vft, vft, shuffle_pattern,
                           (dest_mask & 0x8) ? -1 : 0, (dest_mask & 0x4) ? -1 : 0,
                           (dest_mask & 0x2) ? -1 : 0, (dest_mask & 0x1) ? -1 : 0,
                           vfd, vfd);
    }

    std::string CodeGenerator::translateVU_VMSUB_Field(const Instruction &inst)
    {
        uint8_t vfd = inst.sa;
        uint8_t vfs = inst.rd;
        uint8_t vft = inst.rt;
        uint8_t dest_mask = inst.vectorInfo.vectorField;
        uint8_t field = inst.function & 0x3; // Extract field from function code

        std::string shuffle_pattern = fmt::format("_MM_SHUFFLE({},{},{},{})", field, field, field, field);

        return fmt::format("{{ __m128 mul_res = PS2_VMUL(ctx->vu0_vf[{}], _mm_shuffle_ps(ctx->vu0_vf[{}], ctx->vu0_vf[{}], {})); "
                           "__m128 res = PS2_VSUB(ctx->vu0_acc, mul_res); "
                           "__m128i mask = _mm_set_epi32({}, {}, {}, {}); "
                           "ctx->vu0_vf[{}] = _mm_blendv_ps(ctx->vu0_vf[{}], res, _mm_castsi128_ps(mask)); "
                           "ctx->vu0_acc = res; }}",
                           vfs, vft, vft, shuffle_pattern,
                           (dest_mask & 0x8) ? -1 : 0, (dest_mask & 0x4) ? -1 : 0,
                           (dest_mask & 0x2) ? -1 : 0, (dest_mask & 0x1) ? -1 : 0,
                           vfd, vfd);
    }

    std::string CodeGenerator::translateVU_VMINI_Field(const Instruction &inst)
    {
        uint8_t vfd = inst.sa;
        uint8_t vfs = inst.rd;
        uint8_t vft = inst.rt;
        uint8_t dest_mask = inst.vectorInfo.vectorField;
        uint8_t field = inst.function & 0x3;

        std::string shuffle_pattern = fmt::format("_MM_SHUFFLE({},{},{},{})", field, field, field, field);

        return fmt::format("{{ __m128 res = _mm_min_ps(ctx->vu0_vf[{}], _mm_shuffle_ps(ctx->vu0_vf[{}], ctx->vu0_vf[{}], {})); "
                           "__m128i mask = _mm_set_epi32({}, {}, {}, {}); "
                           "ctx->vu0_vf[{}] = _mm_blendv_ps(ctx->vu0_vf[{}], res, _mm_castsi128_ps(mask)); }}",
                           vfs, vft, vft, shuffle_pattern,
                           (dest_mask & 0x8) ? -1 : 0, (dest_mask & 0x4) ? -1 : 0,
                           (dest_mask & 0x2) ? -1 : 0, (dest_mask & 0x1) ? -1 : 0,
                           vfd, vfd);
    }

    std::string CodeGenerator::translateVU_VMAX_Field(const Instruction &inst)
    {
        uint8_t vfd = inst.sa;
        uint8_t vfs = inst.rd;
        uint8_t vft = inst.rt;
        uint8_t dest_mask = inst.vectorInfo.vectorField;
        uint8_t field = inst.function & 0x3;

        std::string shuffle_pattern = fmt::format("_MM_SHUFFLE({},{},{},{})", field, field, field, field);

        return fmt::format("{{ __m128 res = _mm_max_ps(ctx->vu0_vf[{}], _mm_shuffle_ps(ctx->vu0_vf[{}], ctx->vu0_vf[{}], {})); "
                           "__m128i mask = _mm_set_epi32({}, {}, {}, {}); "
                           "ctx->vu0_vf[{}] = _mm_blendv_ps(ctx->vu0_vf[{}], res, _mm_castsi128_ps(mask)); }}",
                           vfs, vft, vft, shuffle_pattern,
                           (dest_mask & 0x8) ? -1 : 0, (dest_mask & 0x4) ? -1 : 0,
                           (dest_mask & 0x2) ? -1 : 0, (dest_mask & 0x1) ? -1 : 0,
                           vfd, vfd);
    }

    std::string CodeGenerator::translateVU_VMADD(const Instruction &inst)
    {
        uint8_t vfd = inst.sa;
        uint8_t vfs = inst.rd;
        uint8_t vft = inst.rt;
        uint8_t dest_mask = inst.vectorInfo.vectorField;
        return fmt::format("{{ __m128 mul_res = PS2_VMUL(ctx->vu0_vf[{}], ctx->vu0_vf[{}]); "
                           "__m128 res = PS2_VADD(ctx->vu0_acc, mul_res); "
                           "__m128i mask = _mm_set_epi32({}, {}, {}, {}); "
                           "ctx->vu0_vf[{}] = _mm_blendv_ps(ctx->vu0_vf[{}], res, _mm_castsi128_ps(mask)); "
                           "ctx->vu0_acc = res; }}",
                           vfs, vft,
                           (dest_mask & 0x8) ? -1 : 0, (dest_mask & 0x4) ? -1 : 0,
                           (dest_mask & 0x2) ? -1 : 0, (dest_mask & 0x1) ? -1 : 0,
                           vfd, vfd);
    }

    std::string CodeGenerator::translateVU_VMADDq(const Instruction &inst)
    {
        uint8_t vfd = inst.sa;
        uint8_t vfs = inst.rd;
        uint8_t dest_mask = inst.vectorInfo.vectorField;
        return fmt::format("{{ __m128 mul_res = PS2_VMUL(ctx->vu0_vf[{}], _mm_set1_ps(ctx->vu0_q)); "
                           "__m128 res = PS2_VADD(ctx->vu0_acc, mul_res); "
                           "__m128i mask = _mm_set_epi32({}, {}, {}, {}); "
                           "ctx->vu0_vf[{}] = _mm_blendv_ps(ctx->vu0_vf[{}], res, _mm_castsi128_ps(mask)); "
                           "ctx->vu0_acc = res; }}",
                           vfs,
                           (dest_mask & 0x8) ? -1 : 0, (dest_mask & 0x4) ? -1 : 0,
                           (dest_mask & 0x2) ? -1 : 0, (dest_mask & 0x1) ? -1 : 0,
                           vfd, vfd);
    }

    std::string CodeGenerator::translateVU_VMADDi(const Instruction &inst)
    {
        uint8_t vfd = inst.sa;
        uint8_t vfs = inst.rd;
        uint8_t dest_mask = inst.vectorInfo.vectorField;
        return fmt::format("{{ __m128 mul_res = PS2_VMUL(ctx->vu0_vf[{}], _mm_set1_ps(ctx->vu0_i)); "
                           "__m128 res = PS2_VADD(ctx->vu0_acc, mul_res); "
                           "__m128i mask = _mm_set_epi32({}, {}, {}, {}); "
                           "ctx->vu0_vf[{}] = _mm_blendv_ps(ctx->vu0_vf[{}], res, _mm_castsi128_ps(mask)); "
                           "ctx->vu0_acc = res; }}",
                           vfs,
                           (dest_mask & 0x8) ? -1 : 0, (dest_mask & 0x4) ? -1 : 0,
                           (dest_mask & 0x2) ? -1 : 0, (dest_mask & 0x1) ? -1 : 0,
                           vfd, vfd);
    }

    std::string CodeGenerator::translateVU_VMAX(const Instruction &inst)
    {
        uint8_t vfd = inst.sa;
        uint8_t vfs = inst.rd;
        uint8_t vft = inst.rt;
        uint8_t dest_mask = inst.vectorInfo.vectorField;
        return fmt::format("{{ __m128 res = _mm_max_ps(ctx->vu0_vf[{}], ctx->vu0_vf[{}]); "
                           "__m128i mask = _mm_set_epi32({}, {}, {}, {}); "
                           "ctx->vu0_vf[{}] = _mm_blendv_ps(ctx->vu0_vf[{}], res, _mm_castsi128_ps(mask)); }}",
                           vfs, vft,
                           (dest_mask & 0x8) ? -1 : 0, (dest_mask & 0x4) ? -1 : 0,
                           (dest_mask & 0x2) ? -1 : 0, (dest_mask & 0x1) ? -1 : 0,
                           vfd, vfd);
    }

    std::string CodeGenerator::translateVU_VMAXi(const Instruction &inst)
    {
        uint8_t vfd = inst.sa;
        uint8_t vfs = inst.rd;
        uint8_t dest_mask = inst.vectorInfo.vectorField;
        return fmt::format("{{ __m128 res = _mm_max_ps(ctx->vu0_vf[{}], _mm_set1_ps(ctx->vu0_i)); "
                           "__m128i mask = _mm_set_epi32({}, {}, {}, {}); "
                           "ctx->vu0_vf[{}] = _mm_blendv_ps(ctx->vu0_vf[{}], res, _mm_castsi128_ps(mask)); }}",
                           vfs,
                           (dest_mask & 0x8) ? -1 : 0, (dest_mask & 0x4) ? -1 : 0,
                           (dest_mask & 0x2) ? -1 : 0, (dest_mask & 0x1) ? -1 : 0,
                           vfd, vfd);
    }

    std::string CodeGenerator::translateVU_VMINIi(const Instruction &inst)
    {
        uint8_t vfd = inst.sa;
        uint8_t vfs = inst.rd;
        uint8_t dest_mask = inst.vectorInfo.vectorField;
        return fmt::format("{{ __m128 res = _mm_min_ps(ctx->vu0_vf[{}], _mm_set1_ps(ctx->vu0_i)); "
                           "__m128i mask = _mm_set_epi32({}, {}, {}, {}); "
                           "ctx->vu0_vf[{}] = _mm_blendv_ps(ctx->vu0_vf[{}], res, _mm_castsi128_ps(mask)); }}",
                           vfs,
                           (dest_mask & 0x8) ? -1 : 0, (dest_mask & 0x4) ? -1 : 0,
                           (dest_mask & 0x2) ? -1 : 0, (dest_mask & 0x1) ? -1 : 0,
                           vfd, vfd);
    }

    std::string CodeGenerator::translateVU_VMULi(const Instruction &inst)
    {
        uint8_t vfd = inst.sa;
        uint8_t vfs = inst.rd;
        uint8_t dest_mask = inst.vectorInfo.vectorField;
        return fmt::format("{{ __m128 res = PS2_VMUL(ctx->vu0_vf[{}], _mm_set1_ps(ctx->vu0_i)); "
                           "__m128i mask = _mm_set_epi32({}, {}, {}, {}); "
                           "ctx->vu0_vf[{}] = _mm_blendv_ps(ctx->vu0_vf[{}], res, _mm_castsi128_ps(mask)); }}",
                           vfs,
                           (dest_mask & 0x8) ? -1 : 0, (dest_mask & 0x4) ? -1 : 0,
                           (dest_mask & 0x2) ? -1 : 0, (dest_mask & 0x1) ? -1 : 0,
                           vfd, vfd);
    }

    std::string CodeGenerator::translateVU_VMULq(const Instruction &inst)
    {
        uint8_t vfd = inst.sa;
        uint8_t vfs = inst.rd;
        uint8_t dest_mask = inst.vectorInfo.vectorField;
        return fmt::format("{{ __m128 res = PS2_VMUL(ctx->vu0_vf[{}], _mm_set1_ps(ctx->vu0_q)); "
                           "__m128i mask = _mm_set_epi32({}, {}, {}, {}); "
                           "ctx->vu0_vf[{}] = _mm_blendv_ps(ctx->vu0_vf[{}], res, _mm_castsi128_ps(mask)); }}",
                           vfs,
                           (dest_mask & 0x8) ? -1 : 0, (dest_mask & 0x4) ? -1 : 0,
                           (dest_mask & 0x2) ? -1 : 0, (dest_mask & 0x1) ? -1 : 0,
                           vfd, vfd);
    }


    std::string CodeGenerator::translateVU_VOPMSUB(const Instruction &inst)
    {
        uint8_t vfd = inst.sa;
        uint8_t vfs = inst.rd;
        uint8_t vft = inst.rt;
        uint8_t dest_mask = inst.vectorInfo.vectorField;
        return fmt::format("{{ __m128 mul_res = PS2_VMUL(ctx->vu0_vf[{}], ctx->vu0_vf[{}]); "
                           "__m128 res = PS2_VSUB(ctx->vu0_acc, mul_res); "
                           "__m128i mask = _mm_set_epi32({}, {}, {}, {}); "
                           "ctx->vu0_vf[{}] = _mm_blendv_ps(ctx->vu0_vf[{}], res, _mm_castsi128_ps(mask)); "
                           "ctx->vu0_acc = res; }}",
                           vfs, vft,
                           (dest_mask & 0x8) ? -1 : 0, (dest_mask & 0x4) ? -1 : 0,
                           (dest_mask & 0x2) ? -1 : 0, (dest_mask & 0x1) ? -1 : 0,
                           vfd, vfd);
    }



    std::string CodeGenerator::translateVU_VADDq(const Instruction &inst)
    {
        uint8_t vfd = inst.sa;
        uint8_t vfs = inst.rd;
        uint8_t dest_mask = inst.vectorInfo.vectorField;
        return fmt::format("{{ __m128 res = PS2_VADD(ctx->vu0_vf[{}], _mm_set1_ps(ctx->vu0_q)); "
                           "__m128i mask = _mm_set_epi32({}, {}, {}, {}); "
                           "ctx->vu0_vf[{}] = _mm_blendv_ps(ctx->vu0_vf[{}], res, _mm_castsi128_ps(mask)); }}",
                           vfs,
                           (dest_mask & 0x8) ? -1 : 0, (dest_mask & 0x4) ? -1 : 0,
                           (dest_mask & 0x2) ? -1 : 0, (dest_mask & 0x1) ? -1 : 0,
                           vfd, vfd);
    }

    std::string CodeGenerator::translateVU_VADDi(const Instruction &inst)
    {
        uint8_t vfd = inst.sa;
        uint8_t vfs = inst.rd;
        uint8_t dest_mask = inst.vectorInfo.vectorField;
        return fmt::format("{{ __m128 res = PS2_VADD(ctx->vu0_vf[{}], _mm_set1_ps(ctx->vu0_i)); "
                           "__m128i mask = _mm_set_epi32({}, {}, {}, {}); "
                           "ctx->vu0_vf[{}] = _mm_blendv_ps(ctx->vu0_vf[{}], res, _mm_castsi128_ps(mask)); }}",
                           vfs,
                           (dest_mask & 0x8) ? -1 : 0, (dest_mask & 0x4) ? -1 : 0,
                           (dest_mask & 0x2) ? -1 : 0, (dest_mask & 0x1) ? -1 : 0,
                           vfd, vfd);
    }



    std::string CodeGenerator::translateVU_VMSUB(const Instruction &inst)
    {
        uint8_t vfd = inst.sa;
        uint8_t vfs = inst.rd;
        uint8_t vft = inst.rt;
        uint8_t dest_mask = inst.vectorInfo.vectorField;
        return fmt::format("{{ __m128 mul_res = PS2_VMUL(ctx->vu0_vf[{}], ctx->vu0_vf[{}]); "
                           "__m128 res = PS2_VSUB(ctx->vu0_acc, mul_res); "
                           "__m128i mask = _mm_set_epi32({}, {}, {}, {}); "
                           "ctx->vu0_vf[{}] = _mm_blendv_ps(ctx->vu0_vf[{}], res, _mm_castsi128_ps(mask)); "
                           "ctx->vu0_acc = res; }}",
                           vfs, vft,
                           (dest_mask & 0x8) ? -1 : 0, (dest_mask & 0x4) ? -1 : 0,
                           (dest_mask & 0x2) ? -1 : 0, (dest_mask & 0x1) ? -1 : 0,
                           vfd, vfd);
    }

    std::string CodeGenerator::translateVU_VMINI(const Instruction &inst)
    {
        uint8_t vfd = inst.sa;
        uint8_t vfs = inst.rd;
        uint8_t vft = inst.rt;
        uint8_t dest_mask = inst.vectorInfo.vectorField;
        return fmt::format("{{ __m128 res = _mm_min_ps(ctx->vu0_vf[{}], ctx->vu0_vf[{}]); "
                           "__m128i mask = _mm_set_epi32({}, {}, {}, {}); "
                           "ctx->vu0_vf[{}] = _mm_blendv_ps(ctx->vu0_vf[{}], res, _mm_castsi128_ps(mask)); }}",
                           vfs, vft,
                           (dest_mask & 0x8) ? -1 : 0, (dest_mask & 0x4) ? -1 : 0,
                           (dest_mask & 0x2) ? -1 : 0, (dest_mask & 0x1) ? -1 : 0,
                           vfd, vfd);
    }



    std::string CodeGenerator::translateVU_VSUBi(const Instruction &inst)
    {
        uint8_t vfd = inst.sa;
        uint8_t vfs = inst.rd;
        uint8_t dest_mask = inst.vectorInfo.vectorField;
        return fmt::format("{{ __m128 res = PS2_VSUB(ctx->vu0_vf[{}], _mm_set1_ps(ctx->vu0_i)); "
                           "__m128i mask = _mm_set_epi32({}, {}, {}, {}); "
                           "ctx->vu0_vf[{}] = _mm_blendv_ps(ctx->vu0_vf[{}], res, _mm_castsi128_ps(mask)); }}",
                           vfs,
                           (dest_mask & 0x8) ? -1 : 0, (dest_mask & 0x4) ? -1 : 0,
                           (dest_mask & 0x2) ? -1 : 0, (dest_mask & 0x1) ? -1 : 0,
                           vfd, vfd);
    }

    std::string CodeGenerator::translateVU_VSUBq(const Instruction &inst)
    {
        uint8_t vfd = inst.sa;
        uint8_t vfs = inst.rd;
        uint8_t dest_mask = inst.vectorInfo.vectorField;
        return fmt::format("{{ __m128 res = PS2_VSUB(ctx->vu0_vf[{}], _mm_set1_ps(ctx->vu0_q)); "
                           "__m128i mask = _mm_set_epi32({}, {}, {}, {}); "
                           "ctx->vu0_vf[{}] = _mm_blendv_ps(ctx->vu0_vf[{}], res, _mm_castsi128_ps(mask)); }}",
                           vfs,
                           (dest_mask & 0x8) ? -1 : 0, (dest_mask & 0x4) ? -1 : 0,
                           (dest_mask & 0x2) ? -1 : 0, (dest_mask & 0x1) ? -1 : 0,
                           vfd, vfd);
    }


    std::string CodeGenerator::translateVU_VMSUBq(const Instruction &inst)
    {
        uint8_t vfd = inst.sa;
        uint8_t vfs = inst.rd;
        uint8_t dest_mask = inst.vectorInfo.vectorField;
        return fmt::format("{{ __m128 mul_res = PS2_VMUL(ctx->vu0_vf[{}], _mm_set1_ps(ctx->vu0_q)); "
                           "__m128 res = PS2_VSUB(ctx->vu0_acc, mul_res); "
                           "__m128i mask = _mm_set_epi32({}, {}, {}, {}); "
                           "ctx->vu0_vf[{}] = _mm_blendv_ps(ctx->vu0_vf[{}], res, _mm_castsi128_ps(mask)); "
                           "ctx->vu0_acc = res; }}",
                           vfs,
                           (dest_mask & 0x8) ? -1 : 0, (dest_mask & 0x4) ? -1 : 0,
                           (dest_mask & 0x2) ? -1 : 0, (dest_mask & 0x1) ? -1 : 0,
                           vfd, vfd);
    }

    std::string CodeGenerator::translateVU_VMSUBi(const Instruction &inst)
    {
        uint8_t vfd = inst.sa;
        uint8_t vfs = inst.rd;
        uint8_t dest_mask = inst.vectorInfo.vectorField;
        return fmt::format("{{ __m128 mul_res = PS2_VMUL(ctx->vu0_vf[{}], _mm_set1_ps(ctx->vu0_i)); "
                           "__m128 res = PS2_VSUB(ctx->vu0_acc, mul_res); "
                           "__m128i mask = _mm_set_epi32({}, {}, {}, {}); "
                           "ctx->vu0_vf[{}] = _mm_blendv_ps(ctx->vu0_vf[{}], res, _mm_castsi128_ps(mask)); "
                           "ctx->vu0_acc = res; }}",
                           vfs,
                           (dest_mask & 0x8) ? -1 : 0, (dest_mask & 0x4) ? -1 : 0,
                           (dest_mask & 0x2) ? -1 : 0, (dest_mask & 0x1) ? -1 : 0,
                           vfd, vfd);
    }

    std::string CodeGenerator::translateVU_VADDA_Field(const Instruction &inst)
    {
        uint8_t vfs = inst.rd;
        uint8_t vft = inst.rt;
        uint8_t field = inst.function & 0x3;
        std::string shuffle_pattern = fmt::format("_MM_SHUFFLE({},{},{},{})", field, field, field, field);

        return fmt::format("{{ __m128 res = PS2_VADD(ctx->vu0_vf[{}], _mm_shuffle_ps(ctx->vu0_vf[{}], ctx->vu0_vf[{}], {})); "
                           "ctx->vu0_acc = res; }}",
                           vfs, vft, vft, shuffle_pattern);
    }

    std::string CodeGenerator::translateVU_VSUBA_Field(const Instruction &inst)
    {
        uint8_t vfs = inst.rd;
        uint8_t vft = inst.rt;
        uint8_t field = inst.function & 0x3;
        std::string shuffle_pattern = fmt::format("_MM_SHUFFLE({},{},{},{})", field, field, field, field);

        return fmt::format("{{ __m128 res = PS2_VSUB(ctx->vu0_vf[{}], _mm_shuffle_ps(ctx->vu0_vf[{}], ctx->vu0_vf[{}], {})); "
                           "ctx->vu0_acc = res; }}",
                           vfs, vft, vft, shuffle_pattern);
    }

    std::string CodeGenerator::translateVU_VMADDA_Field(const Instruction &inst)
    {
        uint8_t vfs = inst.rd;
        uint8_t vft = inst.rt;
        uint8_t field = inst.function & 0x3;
        std::string shuffle_pattern = fmt::format("_MM_SHUFFLE({},{},{},{})", field, field, field, field);

        return fmt::format("{{ __m128 mul_res = PS2_VMUL(ctx->vu0_vf[{}], _mm_shuffle_ps(ctx->vu0_vf[{}], ctx->vu0_vf[{}], {})); "
                           "__m128 res = PS2_VADD(ctx->vu0_acc, mul_res); "
                           "ctx->vu0_acc = res; }}",
                           vfs, vft, vft, shuffle_pattern);
    }

    std::string CodeGenerator::translateVU_VMSUBA_Field(const Instruction &inst)
    {
        uint8_t vfs = inst.rd;
        uint8_t vft = inst.rt;
        uint8_t field = inst.function & 0x3;
        std::string shuffle_pattern = fmt::format("_MM_SHUFFLE({},{},{},{})", field, field, field, field);

        return fmt::format("{{ __m128 mul_res = PS2_VMUL(ctx->vu0_vf[{}], _mm_shuffle_ps(ctx->vu0_vf[{}], ctx->vu0_vf[{}], {})); "
                           "__m128 res = PS2_VSUB(ctx->vu0_acc, mul_res); "
                           "ctx->vu0_acc = res; }}",
                           vfs, vft, vft, shuffle_pattern);
    }

    std::string CodeGenerator::translateVU_VMULA_Field(const Instruction &inst)
    {
        uint8_t vfs = inst.rd;
        uint8_t vft = inst.rt;
        uint8_t field = inst.function & 0x3;
        std::string shuffle_pattern = fmt::format("_MM_SHUFFLE({},{},{},{})", field, field, field, field);

        return fmt::format("{{ __m128 res = PS2_VMUL(ctx->vu0_vf[{}], _mm_shuffle_ps(ctx->vu0_vf[{}], ctx->vu0_vf[{}], {})); "
                           "ctx->vu0_acc = res; }}",
                           vfs, vft, vft, shuffle_pattern);
    }

    std::string CodeGenerator::translateVU_VADDA(const Instruction &inst)
    {
        uint8_t vfs = inst.rd;
        uint8_t vft = inst.rt;
        return fmt::format("ctx->vu0_acc = PS2_VADD(ctx->vu0_vf[{}], ctx->vu0_vf[{}]);",
                           vfs, vft);
    }

    std::string CodeGenerator::translateVU_VADDAq(const Instruction &inst)
    {
        uint8_t vfs = inst.rd;
        return fmt::format("ctx->vu0_acc = PS2_VADD(ctx->vu0_vf[{}], _mm_set1_ps(ctx->vu0_q));",
                           vfs);
    }

    std::string CodeGenerator::translateVU_VADDAi(const Instruction &inst)
    {
        uint8_t vfs = inst.rd;
        return fmt::format("ctx->vu0_acc = PS2_VADD(ctx->vu0_vf[{}], _mm_set1_ps(ctx->vu0_i));",
                           vfs);
    }

    std::string CodeGenerator::translateVU_VSUBA(const Instruction &inst)
    {
        uint8_t vfs = inst.rd;
        uint8_t vft = inst.rt;
        return fmt::format("ctx->vu0_acc = PS2_VSUB(ctx->vu0_vf[{}], ctx->vu0_vf[{}]);",
                           vfs, vft);
    }

    std::string CodeGenerator::translateVU_VSUBAq(const Instruction &inst)
    {
        uint8_t vfs = inst.rd;
        return fmt::format("ctx->vu0_acc = PS2_VSUB(ctx->vu0_vf[{}], _mm_set1_ps(ctx->vu0_q));",
                           vfs);
    }

    std::string CodeGenerator::translateVU_VSUBAi(const Instruction &inst)
    {
        uint8_t vfs = inst.rd;
        return fmt::format("ctx->vu0_acc = PS2_VSUB(ctx->vu0_vf[{}], _mm_set1_ps(ctx->vu0_i));",
                           vfs);
    }

    std::string CodeGenerator::translateVU_VMADDA(const Instruction &inst)
    {
        uint8_t vfs = inst.rd;
        uint8_t vft = inst.rt;
        return fmt::format("{{ __m128 mul_res = PS2_VMUL(ctx->vu0_vf[{}], ctx->vu0_vf[{}]); "
                           "ctx->vu0_acc = PS2_VADD(ctx->vu0_acc, mul_res); }}",
                           vfs, vft);
    }

    std::string CodeGenerator::translateVU_VMADDAq(const Instruction &inst)
    {
        uint8_t vfs = inst.rd;
        return fmt::format("{{ __m128 mul_res = PS2_VMUL(ctx->vu0_vf[{}], _mm_set1_ps(ctx->vu0_q)); "
                           "ctx->vu0_acc = PS2_VADD(ctx->vu0_acc, mul_res); }}",
                           vfs);
    }

    std::string CodeGenerator::translateVU_VMADDAi(const Instruction &inst)
    {
        uint8_t vfs = inst.rd;
        return fmt::format("{{ __m128 mul_res = PS2_VMUL(ctx->vu0_vf[{}], _mm_set1_ps(ctx->vu0_i)); "
                           "ctx->vu0_acc = PS2_VADD(ctx->vu0_acc, mul_res); }}",
                           vfs);
    }

    std::string CodeGenerator::translateVU_VMSUBA(const Instruction &inst)
    {
        uint8_t vfs = inst.rd;
        uint8_t vft = inst.rt;
        return fmt::format("{{ __m128 mul_res = PS2_VMUL(ctx->vu0_vf[{}], ctx->vu0_vf[{}]); "
                           "ctx->vu0_acc = PS2_VSUB(ctx->vu0_acc, mul_res); }}",
                           vfs, vft);
    }

    std::string CodeGenerator::translateVU_VMSUBAq(const Instruction &inst)
    {
        uint8_t vfs = inst.rd;
        return fmt::format("{{ __m128 mul_res = PS2_VMUL(ctx->vu0_vf[{}], _mm_set1_ps(ctx->vu0_q)); "
                           "ctx->vu0_acc = PS2_VSUB(ctx->vu0_acc, mul_res); }}",
                           vfs);
    }

    std::string CodeGenerator::translateVU_VMSUBAi(const Instruction &inst)
    {
        uint8_t vfs = inst.rd;
        return fmt::format("{{ __m128 mul_res = PS2_VMUL(ctx->vu0_vf[{}], _mm_set1_ps(ctx->vu0_i)); "
                           "ctx->vu0_acc = PS2_VSUB(ctx->vu0_acc, mul_res); }}",
                           vfs);
    }

    std::string CodeGenerator::translateVU_VMULA(const Instruction &inst)
    {
        uint8_t vfs = inst.rd;
        uint8_t vft = inst.rt;
        return fmt::format("ctx->vu0_acc = PS2_VMUL(ctx->vu0_vf[{}], ctx->vu0_vf[{}]);",
                           vfs, vft);
    }

    std::string CodeGenerator::translateVU_VMULAq(const Instruction &inst)
    {
        uint8_t vfs = inst.rd;
        return fmt::format("ctx->vu0_acc = PS2_VMUL(ctx->vu0_vf[{}], _mm_set1_ps(ctx->vu0_q));",
                           vfs);
    }

    std::string CodeGenerator::translateVU_VMULAi(const Instruction &inst)
    {
        uint8_t vfs = inst.rd;
        return fmt::format("ctx->vu0_acc = PS2_VMUL(ctx->vu0_vf[{}], _mm_set1_ps(ctx->vu0_i));",
                           vfs);
    }

    std::string CodeGenerator::translateVU_VOPMULA(const Instruction &inst)
    {
        uint8_t vfs = inst.rd;
        uint8_t vft = inst.rt;
        return fmt::format("ctx->vu0_acc = PS2_VMUL(ctx->vu0_vf[{}], ctx->vu0_vf[{}]);",
                           vfs, vft);
    }

    std::string CodeGenerator::translateVU_VITOF(const Instruction &inst, int shift)
    {
        uint8_t vfs = inst.rd;
        uint8_t dest_mask = inst.vectorInfo.vectorField;
        float scale = (shift == 0) ? 1.0f : (1.0f / static_cast<float>(1 << shift));

        return fmt::format("{{ __m128i src = _mm_castps_si128(ctx->vu0_vf[{}]); "
                           "__m128 res = _mm_cvtepi32_ps(src); "
                           "res = _mm_mul_ps(res, _mm_set1_ps({})); "
                           "__m128i mask = _mm_set_epi32({}, {}, {}, {}); "
                           "ctx->vu0_vf[{}] = _mm_blendv_ps(ctx->vu0_vf[{}], res, _mm_castsi128_ps(mask)); }}",
                           vfs, formatFloatLiteral(scale),
                           (dest_mask & 0x8) ? -1 : 0, (dest_mask & 0x4) ? -1 : 0,
                           (dest_mask & 0x2) ? -1 : 0, (dest_mask & 0x1) ? -1 : 0,
                           inst.rt, inst.rt);
    }

    std::string CodeGenerator::translateVU_VFTOI(const Instruction &inst, int shift)
    {
        uint8_t vfs = inst.rd;
        uint8_t dest_mask = inst.vectorInfo.vectorField;
        float scale = (shift == 0) ? 1.0f : static_cast<float>(1 << shift);

        return fmt::format("{{ __m128 src = ctx->vu0_vf[{}]; "
                           "src = _mm_mul_ps(src, _mm_set1_ps({})); "
                           "__m128i res_i = _mm_cvttps_epi32(src); "
                           "__m128 res = _mm_castsi128_ps(res_i); "
                           "__m128i mask = _mm_set_epi32({}, {}, {}, {}); "
                           "ctx->vu0_vf[{}] = _mm_blendv_ps(ctx->vu0_vf[{}], res, _mm_castsi128_ps(mask)); }}",
                           vfs, formatFloatLiteral(scale),
                           (dest_mask & 0x8) ? -1 : 0, (dest_mask & 0x4) ? -1 : 0,
                           (dest_mask & 0x2) ? -1 : 0, (dest_mask & 0x1) ? -1 : 0,
                           inst.rt, inst.rt);
    }

    std::string CodeGenerator::translateVU_VLQI(const Instruction &inst)
    {
        uint8_t vis = inst.rd;
        uint8_t dest_mask = inst.vectorInfo.vectorField;
        return fmt::format("{{ uint32_t addr = ((uint32_t)(ctx->vi[{}] & 0x3FF)) << 4; "
                           "__m128 res = _mm_castsi128_ps(READ128(addr)); "
                           "__m128i mask = _mm_set_epi32({}, {}, {}, {}); "
                           "ctx->vu0_vf[{}] = _mm_blendv_ps(ctx->vu0_vf[{}], res, _mm_castsi128_ps(mask)); "
                           "ctx->vi[{}] = (ctx->vi[{}] + 1) & 0x3FF; }}",
                           vis,
                           (dest_mask & 0x8) ? -1 : 0, (dest_mask & 0x4) ? -1 : 0,
                           (dest_mask & 0x2) ? -1 : 0, (dest_mask & 0x1) ? -1 : 0,
                           inst.rt, inst.rt,
                           vis, vis);
    }

    std::string CodeGenerator::translateVU_VSQI(const Instruction &inst)
    {
        uint8_t vis = inst.rd;
        uint8_t dest_mask = inst.vectorInfo.vectorField;
        return fmt::format("{{ uint32_t addr = ((uint32_t)(ctx->vi[{}] & 0x3FF)) << 4; "
                           "__m128i old_val = READ128(addr); "
                           "__m128 res = _mm_blendv_ps(_mm_castsi128_ps(old_val), ctx->vu0_vf[{}], _mm_castsi128_ps(_mm_set_epi32({}, {}, {}, {}))); "
                           "WRITE128(addr, _mm_castps_si128(res)); "
                           "ctx->vi[{}] = (ctx->vi[{}] + 1) & 0x3FF; }}",
                           vis,
                           inst.rt,
                           (dest_mask & 0x8) ? -1 : 0, (dest_mask & 0x4) ? -1 : 0,
                           (dest_mask & 0x2) ? -1 : 0, (dest_mask & 0x1) ? -1 : 0,
                           vis, vis);
    }

    std::string CodeGenerator::translateVU_VLQD(const Instruction &inst)
    {
        uint8_t vis = inst.rd;
        uint8_t dest_mask = inst.vectorInfo.vectorField;
        return fmt::format("{{ ctx->vi[{}] = (ctx->vi[{}] - 1) & 0x3FF; "
                           "uint32_t addr = ((uint32_t)(ctx->vi[{}] & 0x3FF)) << 4; "
                           "__m128 res = _mm_castsi128_ps(READ128(addr)); "
                           "__m128i mask = _mm_set_epi32({}, {}, {}, {}); "
                           "ctx->vu0_vf[{}] = _mm_blendv_ps(ctx->vu0_vf[{}], res, _mm_castsi128_ps(mask)); }}",
                           vis, vis,
                           vis,
                           (dest_mask & 0x8) ? -1 : 0, (dest_mask & 0x4) ? -1 : 0,
                           (dest_mask & 0x2) ? -1 : 0, (dest_mask & 0x1) ? -1 : 0,
                           inst.rt, inst.rt);
    }

    std::string CodeGenerator::translateVU_VSQD(const Instruction &inst)
    {
        uint8_t vis = inst.rd;
        uint8_t dest_mask = inst.vectorInfo.vectorField;
        return fmt::format("{{ ctx->vi[{}] = (ctx->vi[{}] - 1) & 0x3FF; "
                           "uint32_t addr = ((uint32_t)(ctx->vi[{}] & 0x3FF)) << 4; "
                           "__m128i old_val = READ128(addr); "
                           "__m128 res = _mm_blendv_ps(_mm_castsi128_ps(old_val), ctx->vu0_vf[{}], _mm_castsi128_ps(_mm_set_epi32({}, {}, {}, {}))); "
                           "WRITE128(addr, _mm_castps_si128(res)); }}",
                           vis, vis,
                           vis,
                           inst.rt,
                           (dest_mask & 0x8) ? -1 : 0, (dest_mask & 0x4) ? -1 : 0,
                           (dest_mask & 0x2) ? -1 : 0, (dest_mask & 0x1) ? -1 : 0);
    }

    std::string CodeGenerator::translateVU_VRGET(const Instruction &inst)
    {
        uint8_t dest_mask = inst.vectorInfo.vectorField;
        uint8_t ft_reg = inst.rt;
        return fmt::format("{{ __m128 res = ctx->vu0_r; __m128i mask = _mm_set_epi32({}, {}, {}, {}); ctx->vu0_vf[{}] = _mm_blendv_ps(ctx->vu0_vf[{}], res, _mm_castsi128_ps(mask)); }}", (dest_mask & 0x8) ? -1 : 0, (dest_mask & 0x4) ? -1 : 0, (dest_mask & 0x2) ? -1 : 0, (dest_mask & 0x1) ? -1 : 0, ft_reg, ft_reg);
    }

    std::string CodeGenerator::translateVU_VRINIT(const Instruction &inst)
    {
        uint8_t fs_reg = inst.rd;
        uint8_t fsf = inst.vectorInfo.fsf;

        return fmt::format(
            "{{ "
            "    float src = _mm_cvtss_f32(_mm_shuffle_ps(ctx->vu0_vf[{}], ctx->vu0_vf[{}], _MM_SHUFFLE(0,0,0,{}))); "
            "    uint32_t seed; std::memcpy(&seed, &src, sizeof(seed)); "
            "    "
            "    // PS2 uses a specific LFSR initialization pattern "
            "    if (seed == 0) seed = 1; " // Prevent zero seed
            "    "
            "    uint32_t r0 = seed; "
            "    uint32_t r1 = seed * 0x41C64E6D + 0x3039; " // PS2-like LCG constants
            "    uint32_t r2 = r1 * 0x41C64E6D + 0x3039; "
            "    uint32_t r3 = r2 * 0x41C64E6D + 0x3039; "
            "    "
            "    ctx->vu0_r = _mm_castsi128_ps(_mm_set_epi32(r3, r2, r1, r0)); \n "
            "}}",
            fs_reg, fs_reg, fsf);
    }

    std::string CodeGenerator::translateVU_VRXOR(const Instruction &inst)
    {
        uint8_t fs_reg = inst.rd;
        uint8_t fsf = inst.vectorInfo.fsf;

        return fmt::format(
            "{{ "
            "    float src = _mm_cvtss_f32(_mm_shuffle_ps(ctx->vu0_vf[{}], ctx->vu0_vf[{}], _MM_SHUFFLE(0,0,0,{}))); "
            "    uint32_t src_bits; std::memcpy(&src_bits, &src, sizeof(src_bits)); "
            "    __m128i r_current = _mm_castps_si128(ctx->vu0_r); "
            "    __m128i fs_data = _mm_set1_epi32((int)src_bits); "
            "    "
            "     // XOR the current random value with the data from the VU vector register "
            "    __m128i xored = _mm_xor_si128(r_current, fs_data); "
            "    "
            "    // Apply a simple mixing function similar to PS2's LFSR "
            "    __m128i mixed = _mm_xor_si128(xored, _mm_slli_epi32(xored, 7)); "
            "    mixed = _mm_xor_si128(mixed, _mm_srli_epi32(mixed, 9)); "
            "    "
            "    ctx->vu0_r = _mm_castsi128_ps(mixed);"
            "}}",
            fs_reg, fs_reg, fsf);
    }

    std::string CodeGenerator::translateQFSRV(const Instruction &inst)
    {
        uint8_t rd = inst.rd;
        uint8_t rs = inst.rs;
        uint8_t rt = inst.rt;
        // QFSRV semantics are centralized in runtime macro helpers.
        return fmt::format("SET_GPR_VEC(ctx, {}, PS2_QFSRV(GPR_VEC(ctx, {}), GPR_VEC(ctx, {}), ctx->sa & 0x7F));",
                           rd, rs, rt);
    }

    std::string CodeGenerator::generateFunctionRegistration(const std::vector<Function> &functions,
                                                            const std::map<uint32_t, std::string> &stubs)
    {
        std::stringstream ss;

        std::unordered_set<uint32_t> registeredAddresses;
        auto emitRegistration = [&](uint32_t address, const std::string &name)
        {
            if (!registeredAddresses.insert(address).second)
            {
                return;
            }

            ss << "    runtime.registerFunction(0x" << std::hex << address << std::dec
               << ", " << name << ");\n";
        };

        // Begin function
        ss << "#include \"ps2_runtime.h\"\n";
        ss << "#include \"ps2_recompiled_functions.h\"\n";
        ss << "#include \"ps2_stubs.h\"\n";
        ss << "#include \"ps2_recompiled_stubs.h\"//this will give duplicated erros because runtime maybe has it define already, just delete the TODOS ones\n";
        ss << "#include \"ps2_syscalls.h\"\n\n";

        // Registration function
        ss << "void registerAllFunctions(PS2Runtime& runtime) {\n";

        std::vector<std::pair<uint32_t, std::string>> normalFunctions;
        std::vector<std::pair<uint32_t, std::string>> stubFunctions;
        std::vector<std::pair<uint32_t, std::string>> systemCallFunctions;
        std::vector<std::pair<uint32_t, std::string>> libraryFunctions;

        uint32_t libBaseAddr = 0x00110000;
        uint32_t libOffset = 0;

        for (const auto &function : functions)
        {
            if (!function.isRecompiled && !function.isStub && !function.isSkipped)
                continue;

            std::string generatedName = getFunctionName(function.start);

            if (function.isSkipped)
            {
                libraryFunctions.emplace_back(function.start, generatedName);
            }
            else if (function.isStub)
            {
                const auto target = PS2Recompiler::resolveStubTarget(function.name);
                if (target == StubTarget::Syscall)
                {
                    systemCallFunctions.emplace_back(function.start, generatedName);
                }
                else
                {
                    stubFunctions.emplace_back(function.start, generatedName);
                }
            }
            else
            {
                normalFunctions.emplace_back(function.start, generatedName);
            }
        }

        if (m_bootstrapInfo.valid)
        {
            ss << "    // Register ELF entry function\n";
            std::string entryTarget = m_bootstrapInfo.entryName;
            if (entryTarget.empty())
            {
                entryTarget = getFunctionName(m_bootstrapInfo.entry);
            }
            if (entryTarget.empty())
            {
                throw std::runtime_error("No entry function name available for registration.");
            }
            emitRegistration(m_bootstrapInfo.entry, entryTarget);
            ss << "\n";
        }

        ss << "    // Register recompiled functions\n";
        for (const auto &[first, second] : normalFunctions)
        {
            emitRegistration(first, second);
        }

        ss << "\n    // Register stub functions\n";
        for (const auto &[first, second] : stubFunctions)
        {
            emitRegistration(first, second);
        }

        ss << "\n    // Register system call stubs\n";
        for (const auto &[first, second] : systemCallFunctions)
        {
            emitRegistration(first, second);
        }

        ss << "\n    // Register library stubs\n";
        for (const auto &[first, second] : libraryFunctions)
        {
            emitRegistration(first, second);
        }

        ss << "}\n";

        return ss.str();
    }

    std::string CodeGenerator::generateJumpTableSwitch(const Instruction &inst, uint32_t tableAddress,
                                                       const std::vector<JumpTableEntry> &entries)
    {
        std::stringstream ss;

        uint32_t indexReg = inst.rs;

        ss << "switch (GPR_U32(ctx, " << indexReg << ")) {\n";

        for (const auto &[index, target] : entries)
        {
            ss << "    case " << index << ": {\n";

            std::string funcName = getFunctionName(target);
            if (!funcName.empty())
            {
                ss << "        " << funcName << "(rdram, ctx, runtime);\n";
            }
            else
            {
                ss << "        func_" << std::hex << target << std::dec << "(rdram, ctx,  runtime);\n";
            }

            ss << "        return;\n";
            ss << "    }\n";
        }

        ss << "    default:\n";
        ss << "        // Unknown jump table target\n";
        ss << "        return;\n";
        ss << "}\n";

        return ss.str();
    }

    const Symbol *CodeGenerator::findSymbolByAddress(uint32_t address) const
    {
        auto it = m_symbols.find(address);
        if (it != m_symbols.end())
        {
            return &it->second;
        }

        return nullptr;
    }
}
