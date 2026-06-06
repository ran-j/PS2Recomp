#ifndef PS2RECOMP_ELF_ANALYSIS_CONTEXT_H
#define PS2RECOMP_ELF_ANALYSIS_CONTEXT_H

#include "ps2recomp/types.h"

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace ps2recomp
{
    struct ElfAnalysisContext
    {
        std::vector<Function> functions;
        std::vector<Symbol> symbols;
        std::vector<Section> sections;
        std::vector<Relocation> relocations;

        std::unordered_map<uint32_t, size_t> functionIndexByStart;
        mutable std::unordered_map<uint32_t, std::vector<Instruction>> instructionCache;

        void clear();
        void buildFunctionIndex();
        void clearInstructionCache();
        Function *findFunction(uint32_t start);
        const Function *findFunction(uint32_t start) const;
        Function *findFunctionContaining(uint32_t address);
        const Function *findFunctionContaining(uint32_t address) const;
    };
}

#endif // PS2RECOMP_ELF_ANALYSIS_CONTEXT_H
