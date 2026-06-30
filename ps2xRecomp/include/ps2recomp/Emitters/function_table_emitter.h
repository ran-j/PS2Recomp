#ifndef PS2RECOMP_FUNCTION_TABLE_EMITTER_H
#define PS2RECOMP_FUNCTION_TABLE_EMITTER_H

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace ps2recomp
{
    struct Function;
    class CodeGenerator;

    class FunctionTableEmitter
    {
    public:
        explicit FunctionTableEmitter(CodeGenerator &codeGenerator);

        std::string emit(const std::vector<Function> &functions, const std::map<uint32_t, std::string> &stubs);

    private:
        CodeGenerator &m_codeGenerator;
    };
}

#endif // PS2RECOMP_FUNCTION_TABLE_EMITTER_H
