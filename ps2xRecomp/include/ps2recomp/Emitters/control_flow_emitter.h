#ifndef PS2RECOMP_CONTROL_FLOW_EMITTER_H
#define PS2RECOMP_CONTROL_FLOW_EMITTER_H

#include "ps2recomp/code_generator.h"
#include "ps2recomp/types.h"

#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace ps2recomp
{
    class ControlFlowEmitter
    {
    public:
        ControlFlowEmitter(CodeGenerator &generator,
                           const Instruction &branchInst,
                           const Instruction &delaySlot,
                           const Function &function,
                           const CodeGenerator::AnalysisResult &analysisResult,
                           std::string delaySlotOverride = {});

        std::string emit();

    private:
        enum class StaticBranchKind
        {
            Jump,
            Call,
        };

        enum class RegisterBranchKind
        {
            Jump,
            Call,
        };

        CodeGenerator &m_gen;
        const Instruction &m_branchInst;
        const Instruction &m_delaySlot;
        const Function &m_function;
        const CodeGenerator::AnalysisResult &m_analysisResult;
        std::string m_delaySlotOverride;
        std::stringstream m_ss;

        uint32_t branchPc() const;
        uint32_t delayPc() const;
        uint32_t fallthroughPc() const;
        bool hasRealDelaySlot() const;
        bool isLikelyBranch() const;
        bool isCallLikeEdge() const;
        bool isInternalTarget(uint32_t target) const;
        std::vector<uint32_t> resolvedLocalIndirectTargets() const;

        std::string delaySlotCode() const;
        void emitDelaySlot(std::string_view indent);
        void emitResumeFromDelaySlotEntry();
        void emitInternalTarget(uint32_t target, uint32_t sourcePc, std::string_view indent);
        void emitFallthroughLabelIfNeeded();
        void emitFinalFallthrough();

        void emitStaticJump(StaticBranchKind kind);
        void emitRegisterJump(RegisterBranchKind kind);
        void emitConditionalBranch();
        void emitFallbackInstruction();

        bool emitDirectFunctionJumpIfAvailable(uint32_t target, StaticBranchKind kind, std::string_view indent);
        void emitExternalJumpDispatch(uint32_t target, StaticBranchKind kind, std::string_view indent);
        void emitExternalRegisterCallDispatch(std::string_view jumpTargetExpression, std::string_view indent);
        void emitExternalRegisterJumpDispatch(std::string_view jumpTargetExpression, RegisterBranchKind kind, uint8_t rsReg, std::string_view indent);
        void emitRuntimeBranchDispatch(std::string_view targetExpression,
                                       uint32_t sourcePc,
                                       uint32_t returnPc,
                                       std::string_view runtimeKind,
                                       std::string_view debugName,
                                       std::string_view indent,
                                       bool returnOnTransfer);
        bool emitRelocationCallIfAvailable(StaticBranchKind kind, std::string_view indent);
        std::string conditionalBranchExpression() const;
        uint32_t conditionalBranchTarget() const;
    };
}

#endif // PS2RECOMP_CONTROL_FLOW_EMITTER_H
