#ifndef PS2RECOMP_CODE_GENERATOR_H
#define PS2RECOMP_CODE_GENERATOR_H

#include "ps2recomp/types.h"
#include <string>
#include <vector>

namespace ps2recomp
{

    class CodeGenerator
    {
    public:
        CodeGenerator(const std::vector<Symbol> &symbols);
        ~CodeGenerator();

        std::string generateFunction(const Function &function, const std::vector<Instruction> &instructions, const bool &useHeaders);
        std::string generateMacroHeader();
        std::string handleBranchDelaySlots(const Instruction &branchInst, const Instruction &delaySlot);

    private:
        std::vector<Symbol> m_symbols;

        std::string translateInstruction(const Instruction &inst);
        std::string translateMMIInstruction(const Instruction &inst);
        std::string translateVUInstruction(const Instruction &inst);
        std::string translateFPUInstruction(const Instruction &inst);
        std::string translateCOP0Instruction(const Instruction &inst);
        std::string translateSpecialInstruction(const Instruction &inst);

        // MMI instruction translations
        std::string translateQFSRV(const Instruction &inst);
        std::string translatePCPYLD(const Instruction &inst);
        std::string translatePCPYUD(const Instruction &inst);
        std::string translatePMADDW(const Instruction &inst);
        std::string translatePMADDH(const Instruction &inst);
        std::string translatePHMADH(const Instruction &inst);
        std::string translatePMULTH(const Instruction &inst);
        std::string translatePDIVW(const Instruction &inst);
        std::string translatePDIVBW(const Instruction &inst);
        std::string translatePEXEH(const Instruction &inst);
        std::string translatePREVH(const Instruction &inst);
        std::string translatePEXEW(const Instruction &inst);
        std::string translatePROT3W(const Instruction &inst);

        // SPECIAL instructions
        std::string translateSYNC(const Instruction &inst);
        std::string translateEI(const Instruction &inst);
        std::string translateDI(const Instruction &inst);
        std::string translateDSLL(const Instruction &inst);
        std::string translateDSRL(const Instruction &inst);
        std::string translateDSRA(const Instruction &inst);
        std::string translateDSLLV(const Instruction &inst);
        std::string translateDSRLV(const Instruction &inst);
        std::string translateDSRAV(const Instruction &inst);
        std::string translateDSLL32(const Instruction &inst);
        std::string translateDSRL32(const Instruction &inst);
        std::string translateDSRA32(const Instruction &inst);
        std::string translateDADD(const Instruction &inst);
        std::string translateDADDU(const Instruction &inst);
        std::string translateDSUB(const Instruction &inst);
        std::string translateDSUBU(const Instruction &inst);

        std::string generateJumpTableSwitch(const Instruction &inst, uint32_t tableAddress,
                                            const std::vector<JumpTableEntry> &entries);

        Symbol *findSymbolByAddress(uint32_t address);
    };

}

#endif // PS2RECOMP_CODE_GENERATOR_H