#ifndef PS2RECOMP_INSTRUCTION_TRANSLATOR_H
#define PS2RECOMP_INSTRUCTION_TRANSLATOR_H

#include <string>

#include "ps2recomp/types.h"

namespace ps2recomp
{
    struct Instruction;
    class CodeGenerator;

    class InstructionTranslator
    {
    public:
        explicit InstructionTranslator(CodeGenerator &codeGenerator);
        std::string translate(const Instruction &inst, const MemoryAccessHint &memoryHint);

    private:
        MemoryAccessHint effectiveMemoryHintFor(const Instruction &inst, const MemoryAccessHint &memoryHint) const;
        std::string translateMemoryRead(const Instruction &inst,
                                        const MemoryAccessHint &memoryHint,
                                        int width,
                                        const std::string &addr) const;
        std::string translateMemoryWrite(const Instruction &inst,
                                         const MemoryAccessHint &memoryHint,
                                         int width,
                                         const std::string &addr,
                                         const std::string &value) const;

        CodeGenerator &m_codeGenerator;
    };
}

#endif // PS2RECOMP_INSTRUCTION_TRANSLATOR_H
