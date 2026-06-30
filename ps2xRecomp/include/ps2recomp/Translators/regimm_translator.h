#ifndef PS2RECOMP_REGIMM_TRANSLATOR_H
#define PS2RECOMP_REGIMM_TRANSLATOR_H

#include <string>

namespace ps2recomp
{
    struct Instruction;
    class CodeGenerator;

    class RegimmTranslator
    {
    public:
        explicit RegimmTranslator(CodeGenerator &codeGenerator);
        std::string translate(const Instruction &inst);

    private:
        CodeGenerator &m_codeGenerator;
    };
}

#endif // PS2RECOMP_REGIMM_TRANSLATOR_H
