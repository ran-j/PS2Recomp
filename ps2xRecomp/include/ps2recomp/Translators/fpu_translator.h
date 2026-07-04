#ifndef PS2RECOMP_FPU_TRANSLATOR_H
#define PS2RECOMP_FPU_TRANSLATOR_H

#include <string>

namespace ps2recomp
{
    struct Instruction;
    class CodeGenerator;

    class FpuTranslator
    {
    public:
        explicit FpuTranslator(CodeGenerator &codeGenerator);
        std::string translate(const Instruction &inst);

    private:
        CodeGenerator &m_codeGenerator;
    };
}

#endif // PS2RECOMP_FPU_TRANSLATOR_H
