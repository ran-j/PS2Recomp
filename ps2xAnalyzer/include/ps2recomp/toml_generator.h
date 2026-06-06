#ifndef PS2RECOMP_TOML_GENERATOR_H
#define PS2RECOMP_TOML_GENERATOR_H

#include "ps2recomp/elf_analysis_context.h"
#include "ps2recomp/types.h"

#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ps2recomp
{
    struct TomlGeneratorInput
    {
        const std::string &elfPath;
        const ElfAnalysisContext &context;
        const std::unordered_set<std::string> &libFunctions;
        const std::unordered_set<std::string> &skipFunctions;
        const std::unordered_set<std::string> &untrackedStubFunctions;
        const std::unordered_map<uint32_t, uint32_t> &mmioByInstructionAddress;
        const std::vector<JumpTable> &jumpTables;
        const std::map<uint32_t, uint32_t> &patches;
        const std::map<uint32_t, std::string> &patchReasons;
        const std::map<uint32_t, std::string> &performanceCriticalReasons;
    };

    class TomlGenerator
    {
    public:
        static bool generate(const TomlGeneratorInput &input, const std::string &outputPath);

    private:
        static std::string escapeBackslashes(const std::string &path);
    };
}

#endif // PS2RECOMP_TOML_GENERATOR_H
