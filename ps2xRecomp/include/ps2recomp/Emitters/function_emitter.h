#ifndef PS2RECOMP_FUNCTION_EMITTER_H
#define PS2RECOMP_FUNCTION_EMITTER_H

#include <string>
#include <vector>

namespace ps2recomp
{
    struct Function;
    struct Instruction;
    class CodeGenerator;

    class FunctionEmitter
    {
    public:
        explicit FunctionEmitter(CodeGenerator &codeGenerator);

        std::string emit(const Function &function, const std::vector<Instruction> &instructions, bool useHeaders);

    private:
        CodeGenerator &m_codeGenerator;
    };
}

#endif // PS2RECOMP_FUNCTION_EMITTER_H
