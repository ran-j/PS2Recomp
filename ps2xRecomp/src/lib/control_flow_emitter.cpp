#include "ps2recomp/Emitters/control_flow_emitter.h"

#include "ps2recomp/control_flow_utils.h"
#include "ps2recomp/instructions.h"
#include "ps2recomp/r5900_decoder.h"
#include "ps2_runtime_calls.h"

#include <algorithm>
#include <fmt/format.h>
#include <sstream>

namespace ps2recomp
{
    ControlFlowEmitter::ControlFlowEmitter(CodeGenerator &generator,
                                           const Instruction &branchInst,
                                           const Instruction &delaySlot,
                                           const Function &function,
                                           const CodeGenerator::AnalysisResult &analysisResult)
        : m_gen(generator),
          m_branchInst(branchInst),
          m_delaySlot(delaySlot),
          m_function(function),
          m_analysisResult(analysisResult)
    {
    }

    uint32_t ControlFlowEmitter::branchPc() const
    {
        return m_branchInst.address;
    }

    uint32_t ControlFlowEmitter::delayPc() const
    {
        return m_branchInst.address + 4u;
    }

    uint32_t ControlFlowEmitter::fallthroughPc() const
    {
        return m_branchInst.address + 8u;
    }

    bool ControlFlowEmitter::hasRealDelaySlot() const
    {
        return !isGuestNop(m_delaySlot);
    }

    bool ControlFlowEmitter::isCallLikeEdge() const
    {
        return (m_branchInst.opcode == OPCODE_JAL) ||
               (m_branchInst.opcode == OPCODE_SPECIAL && m_branchInst.function == SPECIAL_JALR);
    }

    bool ControlFlowEmitter::isInternalTarget(uint32_t target) const
    {
        return m_analysisResult.entryPoints.contains(target);
    }

    bool ControlFlowEmitter::isLikelyBranch() const
    {
        return (m_branchInst.opcode == OPCODE_BEQL || m_branchInst.opcode == OPCODE_BNEL ||
                m_branchInst.opcode == OPCODE_BLEZL || m_branchInst.opcode == OPCODE_BGTZL ||
                (m_branchInst.opcode == OPCODE_REGIMM &&
                 (m_branchInst.rt == REGIMM_BLTZL || m_branchInst.rt == REGIMM_BGEZL ||
                  m_branchInst.rt == REGIMM_BLTZALL || m_branchInst.rt == REGIMM_BGEZALL)) ||
                (m_branchInst.opcode == OPCODE_COP1 && m_branchInst.rs == COP1_BC &&
                 (m_branchInst.rt == COP1_BC_BCFL || m_branchInst.rt == COP1_BC_BCTL)) ||
                (m_branchInst.opcode == OPCODE_COP2 && m_branchInst.rs == COP2_BC &&
                 (m_branchInst.rt == COP2_BC_BCFL || m_branchInst.rt == COP2_BC_BCTL)));
    }

    std::vector<uint32_t> ControlFlowEmitter::resolvedLocalIndirectTargets() const
    {
        if (m_branchInst.opcode != OPCODE_SPECIAL)
        {
            return {};
        }

        if (!((m_branchInst.function == SPECIAL_JR && m_branchInst.rs != 31u) ||
              m_branchInst.function == SPECIAL_JALR))
        {
            return {};
        }

        auto jtIt = m_analysisResult.jumpTableTargets.find(m_branchInst.address);
        if (jtIt == m_analysisResult.jumpTableTargets.end())
        {
            return {};
        }

        std::vector<uint32_t> targets = jtIt->second;
        std::sort(targets.begin(), targets.end());
        targets.erase(std::unique(targets.begin(), targets.end()), targets.end());
        return targets;
    }

    std::string ControlFlowEmitter::delaySlotCode() const
    {
        if (!hasRealDelaySlot())
        {
            return {};
        }

        std::string code;
        if (m_gen.m_emitInstructionComments)
        {
            code = "// 0x" + fmt::format("{:x}", m_delaySlot.address) + ": 0x" + fmt::format("{:x}", m_delaySlot.raw);
            std::string disassembly = R5900Decoder::disassembleInstruction(m_delaySlot);
            if (!disassembly.empty())
            {
                code += "  " + disassembly;
            }
            code += " (Delay Slot)\n";
        }

        code += m_gen.translateInstruction(m_delaySlot);
        return code;
    }

    void ControlFlowEmitter::emitDelaySlot(std::string_view indent)
    {
        if (!hasRealDelaySlot())
        {
            return;
        }

        m_ss << fmt::format("{}ctx->pc = 0x{:X}u;\n", indent, delayPc());
        m_ss << fmt::format("{}ctx->in_delay_slot = true;\n", indent);
        m_ss << fmt::format("{}ctx->branch_pc = 0x{:X}u;\n", indent, branchPc());

        const std::string code = delaySlotCode();
        std::istringstream lines(code);
        std::string line;
        while (std::getline(lines, line))
        {
            if (!line.empty())
            {
                m_ss << indent << line << "\n";
            }
        }

        m_ss << fmt::format("{}ctx->in_delay_slot = false;\n", indent);
    }

    void ControlFlowEmitter::emitResumeFromDelaySlotEntry()
    {
        if (!isInternalTarget(delayPc()))
        {
            return;
        }

        m_ss << fmt::format("    if (ctx->pc == 0x{:X}u) {{\n", delayPc());
        emitDelaySlot("        ");
        m_ss << fmt::format("        ctx->pc = 0x{:X}u;\n", fallthroughPc());

        if (isInternalTarget(fallthroughPc()))
        {
            m_ss << fmt::format("        goto label_{:x};\n", fallthroughPc());
        }
        else
        {
            m_ss << fmt::format("        goto label_fallthrough_0x{:x};\n", branchPc());
        }

        m_ss << "    }\n";
    }

    void ControlFlowEmitter::emitInternalTarget(uint32_t target, uint32_t sourcePc, std::string_view indent)
    {
        m_ss << fmt::format("{}ctx->pc = 0x{:X}u;\n", indent, target);
        if (target <= sourcePc && !isCallLikeEdge())
        {
            m_ss << fmt::format("{}if (runtime->shouldPreemptGuestExecution()) {{\n", indent);
            m_ss << fmt::format("{}    return;\n", indent);
            m_ss << fmt::format("{}}}\n", indent);
        }
        m_ss << fmt::format("{}goto label_{:x};\n", indent, target);
    }

    void ControlFlowEmitter::emitRuntimeBranchDispatch(std::string_view targetExpression,
                                                       uint32_t sourcePc,
                                                       uint32_t returnPc,
                                                       std::string_view runtimeKind,
                                                       std::string_view debugName,
                                                       std::string_view indent,
                                                       bool returnOnTransfer)
    {
        m_ss << fmt::format(
            "{}if (!runtime->dispatchGuestBranch(rdram, ctx, {}, 0x{:X}u, 0x{:X}u, PS2Runtime::GuestBranchKind::{}, \"{}\")) {{\n",
            indent,
            targetExpression,
            sourcePc,
            returnPc,
            runtimeKind,
            debugName);
        if (returnOnTransfer)
        {
            m_ss << fmt::format("{}    return;\n", indent);
        }
        m_ss << fmt::format("{}}}\n", indent);
    }

    bool ControlFlowEmitter::emitDirectFunctionJumpIfAvailable(uint32_t target, StaticBranchKind kind, std::string_view indent)
    {
        if (kind != StaticBranchKind::Jump)
        {
            return false;
        }

        const std::string functionName = m_gen.getFunctionName(target);
        if (functionName.empty())
        {
            return false;
        }

        m_ss << indent << functionName << "(rdram, ctx, runtime); return;\n";
        return true;
    }

    void ControlFlowEmitter::emitExternalJumpDispatch(uint32_t target, StaticBranchKind kind, std::string_view indent)
    {
        const bool isCall = kind == StaticBranchKind::Call;
        emitRuntimeBranchDispatch(fmt::format("0x{:X}u", target),
                                  branchPc(),
                                  isCall ? fallthroughPc() : 0u,
                                  isCall ? "DirectCall" : "DirectJump",
                                  isCall ? "JAL" : "J",
                                  indent,
                                  true);
    }

    void ControlFlowEmitter::emitExternalRegisterCallDispatch(std::string_view jumpTargetExpression, std::string_view indent)
    {
        emitRuntimeBranchDispatch(jumpTargetExpression,
                                  branchPc(),
                                  fallthroughPc(),
                                  "IndirectCall",
                                  "JALR",
                                  indent,
                                  true);
    }

    void ControlFlowEmitter::emitExternalRegisterJumpDispatch(std::string_view jumpTargetExpression,
                                                              RegisterBranchKind kind,
                                                              uint8_t rsReg,
                                                              std::string_view indent)
    {
        const bool isReturn = kind == RegisterBranchKind::Jump && rsReg == 31u;

        if (isReturn)
        {
            m_ss << indent << "#if defined(PS2X_STRICT_RETURN_DIAGNOSTICS) && PS2X_STRICT_RETURN_DIAGNOSTICS\n";
            m_ss << indent << "(void)runtime->dispatchGuestBranch(rdram, ctx, " << jumpTargetExpression
                 << ", 0x" << fmt::format("{:X}", branchPc())
                 << "u, 0u, PS2Runtime::GuestBranchKind::Return, \"JR $ra\");\n";
            m_ss << indent << "return;\n";
            m_ss << indent << "#else\n";
            m_ss << indent << "ctx->pc = " << jumpTargetExpression << ";\n";
            m_ss << indent << "return;\n";
            m_ss << indent << "#endif\n";
            return;
        }

        emitRuntimeBranchDispatch(jumpTargetExpression,
                                  branchPc(),
                                  0u,
                                  "IndirectJump",
                                  "JR",
                                  indent,
                                  true);
    }

    bool ControlFlowEmitter::emitRelocationCallIfAvailable(StaticBranchKind kind, std::string_view indent)
    {
        const auto relocIt = m_gen.m_relocationCallNames.find(m_branchInst.address);
        if (relocIt == m_gen.m_relocationCallNames.end() || relocIt->second.empty())
        {
            return false;
        }

        const std::string_view resolvedSyscallName = ps2_runtime_calls::resolveSyscallName(relocIt->second);
        const std::string_view resolvedStubName = ps2_runtime_calls::resolveStubName(relocIt->second);
        if (resolvedSyscallName.empty() && resolvedStubName.empty())
        {
            return false;
        }

        const bool isSyscall = !resolvedSyscallName.empty();
        const std::string_view handlerName = isSyscall ? resolvedSyscallName : resolvedStubName;

        m_ss << indent << "{\n";
        m_ss << indent << "    const uint32_t __entryPc = ctx->pc;\n";
        m_ss << indent << "    " << (isSyscall ? "ps2_syscalls::" : "ps2_stubs::")
             << handlerName << "(rdram, ctx, runtime);\n";
        m_ss << indent << "    if (ctx->pc == __entryPc) { ctx->pc = getRegU32(ctx, 31); }\n";
        m_ss << indent << "}\n";

        if (kind == StaticBranchKind::Jump)
        {
            m_ss << indent << "return;\n";
        }
        else
        {
            m_ss << fmt::format("{}if (ctx->pc != 0x{:X}u) {{ return; }}\n", indent, fallthroughPc());
        }

        return true;
    }

    void ControlFlowEmitter::emitStaticJump(StaticBranchKind kind)
    {
        if (kind == StaticBranchKind::Call)
        {
            m_ss << fmt::format("    SET_GPR_U32(ctx, 31, 0x{:X}u);\n", fallthroughPc());
        }

        emitDelaySlot("    ");

        const uint32_t target = buildAbsoluteJumpTarget(m_branchInst.address, m_branchInst.target);
        if (isInternalTarget(target))
        {
            emitInternalTarget(target, branchPc(), "    ");
            return;
        }

        m_ss << fmt::format("    ctx->pc = 0x{:X}u;\n", target);

        if (kind == StaticBranchKind::Call && emitRelocationCallIfAvailable(kind, "    "))
        {
            return;
        }

        if (emitDirectFunctionJumpIfAvailable(target, kind, "    "))
        {
            return;
        }

        emitExternalJumpDispatch(target, kind, "    ");
    }

    void ControlFlowEmitter::emitRegisterJump(RegisterBranchKind kind)
    {
        const uint8_t rsReg = static_cast<uint8_t>(m_branchInst.rs);
        const uint8_t rdReg = static_cast<uint8_t>(m_branchInst.rd);
        const std::vector<uint32_t> sortedInternalTargets = resolvedLocalIndirectTargets();

        m_ss << "    {\n";
        m_ss << "        const uint32_t jumpTarget = GPR_U32(ctx, " << static_cast<int>(rsReg) << ");\n";

        if (kind == RegisterBranchKind::Call && rdReg != 0u)
        {
            m_ss << fmt::format("        SET_GPR_U32(ctx, {}, 0x{:X}u);\n", rdReg, fallthroughPc());
        }

        emitDelaySlot("        ");
        m_ss << "        ctx->pc = jumpTarget;\n";

        if (!sortedInternalTargets.empty())
        {
            m_ss << "        switch (jumpTarget) {\n";
            for (uint32_t target : sortedInternalTargets)
            {
                m_ss << fmt::format("            case 0x{:X}u: goto label_{:x};\n", target, target);
            }
            m_ss << "            default: break;\n";
            m_ss << "        }\n";
        }

        if (kind == RegisterBranchKind::Jump)
        {
            emitExternalRegisterJumpDispatch("jumpTarget", kind, rsReg, "        ");
        }
        else
        {
            emitExternalRegisterCallDispatch("jumpTarget", "        ");
        }

        m_ss << "    }\n";
    }

    std::string ControlFlowEmitter::conditionalBranchExpression() const
    {
        const uint8_t rsReg = static_cast<uint8_t>(m_branchInst.rs);
        const uint8_t rtReg = static_cast<uint8_t>(m_branchInst.rt);

        switch (m_branchInst.opcode)
        {
        case OPCODE_BEQ:
        case OPCODE_BEQL:
            return fmt::format("GPR_U64(ctx, {}) == GPR_U64(ctx, {})", rsReg, rtReg);
        case OPCODE_BNE:
        case OPCODE_BNEL:
            return fmt::format("GPR_U64(ctx, {}) != GPR_U64(ctx, {})", rsReg, rtReg);
        case OPCODE_BLEZ:
        case OPCODE_BLEZL:
            return fmt::format("GPR_S32(ctx, {}) <= 0", rsReg);
        case OPCODE_BGTZ:
        case OPCODE_BGTZL:
            return fmt::format("GPR_S32(ctx, {}) > 0", rsReg);
        case OPCODE_REGIMM:
            switch (m_branchInst.rt)
            {
            case REGIMM_BLTZ:
            case REGIMM_BLTZL:
            case REGIMM_BLTZAL:
            case REGIMM_BLTZALL:
                return fmt::format("GPR_S32(ctx, {}) < 0", rsReg);
            case REGIMM_BGEZ:
            case REGIMM_BGEZL:
            case REGIMM_BGEZAL:
            case REGIMM_BGEZALL:
                return fmt::format("GPR_S32(ctx, {}) >= 0", rsReg);
            default:
                return "false";
            }
        case OPCODE_COP1:
            if (m_branchInst.rs == COP1_BC)
            {
                const uint8_t bcCond = static_cast<uint8_t>(m_branchInst.rt);
                return (bcCond == COP1_BC_BCF || bcCond == COP1_BC_BCFL)
                           ? "!(ctx->fcr31 & 0x800000)"
                           : "(ctx->fcr31 & 0x800000)";
            }
            break;
        case OPCODE_COP2:
            if (m_branchInst.rs == COP2_BC)
            {
                const uint8_t bcCond = static_cast<uint8_t>(m_branchInst.rt);
                return (bcCond == COP2_BC_BCF || bcCond == COP2_BC_BCFL)
                           ? "!(ctx->vu0_status & 0x1)"
                           : "(ctx->vu0_status & 0x1)";
            }
            break;
        default:
            break;
        }

        return "false";
    }

    uint32_t ControlFlowEmitter::conditionalBranchTarget() const
    {
        const int32_t offsetBytes = static_cast<int32_t>(static_cast<int16_t>(m_branchInst.simmediate)) << 2;
        return static_cast<uint32_t>(static_cast<int64_t>(m_branchInst.address + 4u) +
                                     static_cast<int64_t>(offsetBytes));
    }

    void ControlFlowEmitter::emitConditionalBranch()
    {
        const uint32_t target = conditionalBranchTarget();
        const bool likely = isLikelyBranch();
        const std::string branchTakenVar = fmt::format("branch_taken_0x{:x}", m_branchInst.address);
        std::string unconditionalLinkCode;
        std::string conditionalLinkCode;

        if (m_branchInst.opcode == OPCODE_REGIMM)
        {
            if (m_branchInst.rt == REGIMM_BLTZAL || m_branchInst.rt == REGIMM_BGEZAL)
            {
                unconditionalLinkCode = fmt::format("SET_GPR_U32(ctx, 31, 0x{:X}u);", fallthroughPc());
            }
            else if (m_branchInst.rt == REGIMM_BLTZALL || m_branchInst.rt == REGIMM_BGEZALL)
            {
                conditionalLinkCode = fmt::format("SET_GPR_U32(ctx, 31, 0x{:X}u);", fallthroughPc());
            }
        }

        m_ss << "    {\n";
        m_ss << "        const bool " << branchTakenVar << " = (" << conditionalBranchExpression() << ");\n";

        if (!unconditionalLinkCode.empty())
        {
            m_ss << "        " << unconditionalLinkCode << "\n";
        }

        if (likely)
        {
            m_ss << "        if (" << branchTakenVar << ") {\n";
            if (!conditionalLinkCode.empty())
            {
                m_ss << "            " << conditionalLinkCode << "\n";
            }
            emitDelaySlot("            ");

            if (isInternalTarget(target))
            {
                emitInternalTarget(target, branchPc(), "            ");
            }
            else
            {
                m_ss << fmt::format("            ctx->pc = 0x{:X}u;\n", target);
                m_ss << "            return;\n";
            }
            m_ss << "        }\n";
        }
        else
        {
            if (!conditionalLinkCode.empty())
            {
                m_ss << "        if (" << branchTakenVar << ") { " << conditionalLinkCode << " }\n";
            }

            emitDelaySlot("        ");

            m_ss << "        if (" << branchTakenVar << ") {\n";
            if (isInternalTarget(target))
            {
                emitInternalTarget(target, branchPc(), "            ");
            }
            else
            {
                m_ss << fmt::format("            ctx->pc = 0x{:X}u;\n", target);
                m_ss << "            return;\n";
            }
            m_ss << "        }\n";
        }

        m_ss << "    }\n";
    }

    void ControlFlowEmitter::emitFallbackInstruction()
    {
        m_ss << "    " << m_gen.translateInstruction(m_branchInst) << "\n";
        emitDelaySlot("    ");
    }

    void ControlFlowEmitter::emitFallthroughLabelIfNeeded()
    {
        if (isInternalTarget(delayPc()) && !isInternalTarget(fallthroughPc()))
        {
            m_ss << fmt::format("label_fallthrough_0x{:x}:\n", branchPc());
        }
    }

    void ControlFlowEmitter::emitFinalFallthrough()
    {
        m_ss << fmt::format("    ctx->pc = 0x{:X}u;\n", fallthroughPc());
    }

    std::string ControlFlowEmitter::emit()
    {
        (void)m_function;
        emitResumeFromDelaySlotEntry();
        m_ss << fmt::format("    ctx->pc = 0x{:X}u;\n", branchPc());

        if (m_branchInst.opcode == OPCODE_J || m_branchInst.opcode == OPCODE_JAL)
        {
            emitStaticJump(m_branchInst.opcode == OPCODE_JAL ? StaticBranchKind::Call : StaticBranchKind::Jump);
        }
        else if (m_branchInst.opcode == OPCODE_SPECIAL &&
                 (m_branchInst.function == SPECIAL_JR || m_branchInst.function == SPECIAL_JALR))
        {
            emitRegisterJump(m_branchInst.function == SPECIAL_JALR ? RegisterBranchKind::Call : RegisterBranchKind::Jump);
        }
        else if (m_branchInst.isBranch)
        {
            emitConditionalBranch();
        }
        else
        {
            emitFallbackInstruction();
        }

        emitFallthroughLabelIfNeeded();
        emitFinalFallthrough();
        return m_ss.str();
    }
}
