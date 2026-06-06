#ifndef PS2RECOMP_ANALYSIS_PASSES_H
#define PS2RECOMP_ANALYSIS_PASSES_H

#include "ps2recomp/types.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ps2recomp
{
    class AnalysisPasses
    {
    public:
        static bool hasHardwareIOSignal(const std::vector<Instruction> &instructions);
        static bool hasLargeComplexMMISignal(const std::vector<Instruction> &instructions,
                                             size_t largeInstructionThreshold = 500);
        static bool hasSelfModifyingSignal(const std::vector<Instruction> &instructions,
                                           const std::vector<Section> &sections);
        static bool shouldSkipForPatchDensity(const std::string &functionName,
                                              uint32_t functionSizeBytes,
                                              size_t patchCount,
                                              bool isLibraryFunction);
        static std::vector<JumpTable> detectJumpTables(
            const std::vector<Instruction> &instructions,
            const std::vector<Section> &sections,
            const std::function<bool(uint32_t, uint32_t &)> &readWord);
        static std::unordered_set<std::string> findRecursiveFunctions(
            const std::unordered_map<std::string, std::vector<std::string>> &callGraph);
    };
}

#endif // PS2RECOMP_ANALYSIS_PASSES_H
