#ifndef PS2RECOMP_INSTRUCTION_TRANSLATOR_H
#define PS2RECOMP_INSTRUCTION_TRANSLATOR_H

#include <string>

namespace ps2recomp
{
    struct Instruction;
    class CodeGenerator;

    class InstructionTranslator
    {
    public:
        explicit InstructionTranslator(CodeGenerator &codeGenerator);
        std::string translate(const Instruction &inst);

    private:
        CodeGenerator &m_codeGenerator;
    };
}

#endif // PS2RECOMP_INSTRUCTION_TRANSLATOR_H
