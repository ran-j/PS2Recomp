#include "ps2recomp/elf_analysis_context.h"

#include <algorithm>

namespace ps2recomp
{
    void ElfAnalysisContext::clear()
    {
        functions.clear();
        symbols.clear();
        sections.clear();
        relocations.clear();
        functionIndexByStart.clear();
        instructionCache.clear();
    }

    void ElfAnalysisContext::buildFunctionIndex()
    {
        functionIndexByStart.clear();
        functionIndexByStart.reserve(functions.size());
        for (size_t index = 0; index < functions.size(); ++index)
        {
            functionIndexByStart[functions[index].start] = index;
        }
    }

    void ElfAnalysisContext::clearInstructionCache()
    {
        instructionCache.clear();
        for (auto &func : functions)
        {
            func.instructions.clear();
        }
    }

    Function *ElfAnalysisContext::findFunction(uint32_t start)
    {
        const auto it = functionIndexByStart.find(start);
        if (it == functionIndexByStart.end())
        {
            return nullptr;
        }
        return &functions[it->second];
    }

    const Function *ElfAnalysisContext::findFunction(uint32_t start) const
    {
        const auto it = functionIndexByStart.find(start);
        if (it == functionIndexByStart.end())
        {
            return nullptr;
        }
        return &functions[it->second];
    }

    Function *ElfAnalysisContext::findFunctionContaining(uint32_t address)
    {
        auto it = std::find_if(functions.begin(), functions.end(),
                               [address](const Function &function)
                               {
                                   return function.start <= address && address < function.end;
                               });
        if (it == functions.end())
        {
            return nullptr;
        }
        return &(*it);
    }

    const Function *ElfAnalysisContext::findFunctionContaining(uint32_t address) const
    {
        auto it = std::find_if(functions.begin(), functions.end(),
                               [address](const Function &function)
                               {
                                   return function.start <= address && address < function.end;
                               });
        if (it == functions.end())
        {
            return nullptr;
        }
        return &(*it);
    }
}
