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

        std::string generateJumpTableSwitch(const Instruction &inst, uint32_t tableAddress,
                                            const std::vector<JumpTableEntry> &entries);

        Symbol *findSymbolByAddress(uint32_t address);
    };

}

#endif // PS2RECOMP_CODE_GENERATOR_H