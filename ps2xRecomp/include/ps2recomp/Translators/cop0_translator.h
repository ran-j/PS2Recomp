#ifndef PS2RECOMP_COP0_TRANSLATOR_H
#define PS2RECOMP_COP0_TRANSLATOR_H

#include <string>

namespace ps2recomp
{
    struct Instruction;
    class CodeGenerator;

    class Cop0Translator
    {
    public:
        explicit Cop0Translator(CodeGenerator &codeGenerator);
        std::string translate(const Instruction &inst);

    private:
        CodeGenerator &m_codeGenerator;
    };
}

#endif // PS2RECOMP_COP0_TRANSLATOR_H
