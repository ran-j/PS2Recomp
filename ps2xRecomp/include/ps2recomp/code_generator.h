#ifndef PS2RECOMP_CODE_GENERATOR_H
#define PS2RECOMP_CODE_GENERATOR_H

#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <unordered_set>

namespace ps2recomp
{
	struct JumpTableEntry;
	struct Instruction;
	struct Function;
	struct Symbol;

	extern const std::unordered_set<std::string> kKeywords;

    class CodeGenerator
    {
    public:
	    explicit CodeGenerator(const std::vector<Symbol> &symbols);
        ~CodeGenerator();

        struct BootstrapInfo
        {
            bool valid = false;
            uint32_t entry = 0;
            uint32_t bssStart = 0;
            uint32_t bssEnd = 0;
            uint32_t gp = 0;
            std::string entryName;
        };

        std::string generateFunction(const Function &function, const std::vector<Instruction> &instructions, const bool &useHeaders);
        std::string generateFunctionRegistration(const std::vector<Function> &functions, const std::map<uint32_t, std::string> &stubs);
        std::string handleBranchDelaySlots(const Instruction &branchInst, const Instruction &delaySlot,
                                           const Function &function, const std::unordered_set<uint32_t> &internalTargets);

        void setRenamedFunctions(const std::unordered_map<uint32_t, std::string> &renames);
        void setBootstrapInfo(const BootstrapInfo &info);
        std::unordered_set<uint32_t> collectInternalBranchTargets(const Function &function,
                                                                  const std::vector<Instruction> &instructions);

    public:
        std::unordered_map<uint32_t, Symbol> m_symbols;
        std::unordered_map<uint32_t, std::string> m_renamedFunctions;
        BootstrapInfo m_bootstrapInfo;

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
        std::string translateVU_VMSUB_Field(const Instruction &inst);
        std::string translateVU_VADDA_Field(const Instruction &inst);
        std::string translateVU_VSUBA_Field(const Instruction &inst);
        std::string translateVU_VMADDA_Field(const Instruction &inst);
        std::string translateVU_VMSUBA_Field(const Instruction &inst);
        std::string translateVU_VMULA_Field(const Instruction &inst);
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
        std::string translateVU_VMAX_Field(const Instruction &inst);
        std::string translateVU_VMADD(const Instruction &inst);
        std::string translateVU_VMADDq(const Instruction &inst);
        std::string translateVU_VMADDi(const Instruction &inst);
        std::string translateVU_VMAX(const Instruction &inst);
        std::string translateVU_VMAXi(const Instruction &inst);
        std::string translateVU_VADDA(const Instruction &inst);
        std::string translateVU_VADDAq(const Instruction &inst);
        std::string translateVU_VADDAi(const Instruction &inst);
        std::string translateVU_VSUBA(const Instruction &inst);
        std::string translateVU_VSUBAq(const Instruction &inst);
        std::string translateVU_VSUBAi(const Instruction &inst);
        std::string translateVU_VMADDA(const Instruction &inst);
        std::string translateVU_VMADDAq(const Instruction &inst);
        std::string translateVU_VMADDAi(const Instruction &inst);
        std::string translateVU_VMSUBA(const Instruction &inst);
        std::string translateVU_VMSUBAq(const Instruction &inst);
        std::string translateVU_VMSUBAi(const Instruction &inst);
        std::string translateVU_VMULA(const Instruction &inst);
        std::string translateVU_VMULAq(const Instruction &inst);
        std::string translateVU_VMULAi(const Instruction &inst);
        std::string translateVU_VOPMULA(const Instruction &inst);
        std::string translateVU_VOPMSUB(const Instruction &inst);
        std::string translateVU_VMINI(const Instruction &inst);
        std::string translateVU_VMINIi(const Instruction &inst);
        std::string translateVU_VMSUB(const Instruction &inst);
        std::string translateVU_VMSUBq(const Instruction &inst);
        std::string translateVU_VMSUBi(const Instruction &inst);
        std::string translateVU_VITOF(const Instruction &inst, int shift);
        std::string translateVU_VFTOI(const Instruction &inst, int shift);
        std::string translateVU_VLQI(const Instruction &inst);
        std::string translateVU_VSQI(const Instruction &inst);
        std::string translateVU_VLQD(const Instruction &inst);
        std::string translateVU_VSQD(const Instruction &inst);

        // Jump Table Generation
        std::string generateJumpTableSwitch(const Instruction &inst, uint32_t tableAddress,
                                            const std::vector<JumpTableEntry> &entries);

        const Symbol *findSymbolByAddress(uint32_t address) const;
        std::string getFunctionName(uint32_t address) const;
        std::string sanitizeFunctionName(const std::string& name) const;
    };

}

#endif // PS2RECOMP_CODE_GENERATOR_H
