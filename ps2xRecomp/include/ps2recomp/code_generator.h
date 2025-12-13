#ifndef PS2RECOMP_CODE_GENERATOR_H
#define PS2RECOMP_CODE_GENERATOR_H

#include "ps2recomp/types.h"
#include <string>
#include <vector>
#include <map>
#include <set>

namespace ps2recomp
{

    class CodeGenerator
    {
    public:
        CodeGenerator(const std::vector<Symbol> &symbols);
        ~CodeGenerator();

        std::string generateFunction(const Function &function, const std::vector<Instruction> &instructions, const bool &useHeaders,
                                      const std::set<uint32_t> &midFunctionEntryPoints = {});
        std::string generateFunctionRegistration(const std::vector<Function> &functions, const std::map<uint32_t, std::string> &stubs,
                                                  const std::set<uint32_t> &midFunctionEntryPoints = {});
        std::string generateMacroHeader();
        std::string handleBranchDelaySlots(const Instruction &branchInst, const Instruction &delaySlot);

        // Generate stub for mid-function entry point
        std::string generateMidFunctionStub(uint32_t entryAddr, const Function &containingFunc,
                                            const std::vector<Instruction> &instructions);

    private:
        std::vector<Symbol> m_symbols;

        // Current function bounds for internal branch detection
        uint32_t m_currentFuncStart = 0;
        uint32_t m_currentFuncEnd = 0;
        std::set<uint32_t> m_internalBranchTargets;

        bool isInternalBranch(uint32_t target) const;
        void collectBranchTargets(const std::vector<Instruction> &instructions);

        std::string translateInstruction(const Instruction &inst);
        std::string translateMMIInstruction(const Instruction &inst);
        std::string translateVUInstruction(const Instruction &inst);
        std::string translateFPUInstruction(const Instruction &inst);
        std::string translateCOP0Instruction(const Instruction &inst);
        std::string translateRegimmInstruction(const Instruction &inst);
        std::string translateSpecialInstruction(const Instruction &inst);

        // MMI Translation functions
        std::string translateMMI0Instruction(const Instruction &inst);
        std::string translateMMI1Instruction(const Instruction &inst);
        std::string translateMMI2Instruction(const Instruction &inst);
        std::string translateMMI3Instruction(const Instruction &inst);
        std::string translatePMFHLInstruction(const Instruction &inst);
        std::string translatePMTHLInstruction(const Instruction &inst);

        // Instruction Helpers
        std::string translateQFSRV(const Instruction &inst);
        std::string translatePMADDW(const Instruction &inst);
        std::string translatePDIVW(const Instruction &inst);
        std::string translatePCPYLD(const Instruction &inst);
        std::string translatePMADDH(const Instruction &inst);
        std::string translatePHMADH(const Instruction &inst);
        std::string translatePEXEH(const Instruction &inst);
        std::string translatePREVH(const Instruction &inst);
        std::string translatePMULTH(const Instruction &inst);
        std::string translatePDIVBW(const Instruction &inst);
        std::string translatePEXEW(const Instruction &inst);
        std::string translatePROT3W(const Instruction &inst);
        std::string translatePMULTUW(const Instruction &inst);
        std::string translatePDIVUW(const Instruction &inst);
        std::string translatePCPYUD(const Instruction &inst);
        std::string translatePEXCH(const Instruction &inst);
        std::string translatePCPYH(const Instruction &inst);
        std::string translatePEXCW(const Instruction &inst);
        std::string translatePMTHI(const Instruction &inst);
        std::string translatePMTLO(const Instruction &inst);

        // VU instruction translations
        std::string translateVU_VADD_Field(const Instruction &inst);
        std::string translateVU_VSUB_Field(const Instruction &inst);
        std::string translateVU_VMUL_Field(const Instruction &inst);
        std::string translateVU_VADD(const Instruction &inst);
        std::string translateVU_VSUB(const Instruction &inst);
        std::string translateVU_VMUL(const Instruction &inst);
        std::string translateVU_VDIV(const Instruction &inst);
        std::string translateVU_VSQRT(const Instruction &inst);
        std::string translateVU_VRSQRT(const Instruction &inst);
        std::string translateVU_VMTIR(const Instruction &inst);
        std::string translateVU_VMFIR(const Instruction &inst);
        std::string translateVU_VILWR(const Instruction &inst);
        std::string translateVU_VISWR(const Instruction &inst);
        std::string translateVU_VIADD(const Instruction &inst);
        std::string translateVU_VISUB(const Instruction &inst);
        std::string translateVU_VIADDI(const Instruction &inst);
        std::string translateVU_VIAND(const Instruction &inst);
        std::string translateVU_VIOR(const Instruction &inst);
        std::string translateVU_VCALLMS(const Instruction &inst);
        std::string translateVU_VCALLMSR(const Instruction &inst);
        std::string translateVU_VRNEXT(const Instruction &inst);
        std::string translateVU_VRGET(const Instruction &inst);
        std::string translateVU_VRINIT(const Instruction &inst);
        std::string translateVU_VRXOR(const Instruction &inst);
        std::string translateVU_VMADD_Field(const Instruction &inst);
        std::string translateVU_VMINI_Field(const Instruction &inst);
        std::string translateVU_VMADD(const Instruction &inst);
        std::string translateVU_VMAX(const Instruction &inst);
        std::string translateVU_VOPMSUB(const Instruction &inst);
        std::string translateVU_VMINI(const Instruction &inst);

        // Jump Table Generation
        std::string generateJumpTableSwitch(const Instruction &inst, uint32_t tableAddress,
                                            const std::vector<JumpTableEntry> &entries);

        Symbol *findSymbolByAddress(uint32_t address);
    };

}

#endif // PS2RECOMP_CODE_GENERATOR_H