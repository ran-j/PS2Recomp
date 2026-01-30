#ifndef PS2RECOMP_ELF_ANALYZER_H
#define PS2RECOMP_ELF_ANALYZER_H

#include "ps2recomp/elf_parser.h"
#include "ps2recomp/types.h"
#include "ps2recomp/instructions.h"
#include <string>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <memory>
#include <map>
#include <set>

namespace ps2recomp
{
    class ElfAnalyzer
    {
    public:
        ElfAnalyzer(const std::string &elfPath);
        ~ElfAnalyzer();

        bool analyze();
        bool generateToml(const std::string &outputPath);

    private:
        std::string m_elfPath;
        std::unique_ptr<ElfParser> m_elfParser;

        std::vector<Function> m_functions;
        std::vector<Symbol> m_symbols;
        std::vector<Section> m_sections;
        std::vector<Relocation> m_relocations;

        std::unordered_set<std::string> m_libFunctions;
        std::unordered_set<std::string> m_skipFunctions;
        std::unordered_map<std::string, std::set<std::string>> m_functionDataUsage;
        std::unordered_map<uint32_t, std::string> m_commonDataAccess;

        std::map<uint32_t, uint32_t> m_patches;
        std::map<uint32_t, std::string> m_patchReasons;

        std::unordered_map<uint32_t, CFG> m_functionCFGs;
        std::vector<JumpTable> m_jumpTables;
        std::unordered_map<uint32_t, std::vector<FunctionCall>> m_functionCalls;

        void initializeLibraryFunctions();
        void analyzeEntryPoint();
        void analyzeLibraryFunctions();
        void analyzeDataUsage();
        void identifyPotentialPatches();
        void analyzeControlFlow();
        void detectJumpTables();
        void analyzePerformanceCriticalPaths();
        void identifyRecursiveFunctions();
        void analyzeRegisterUsage();
        void analyzeFunctionSignatures();
        void optimizePatches();

        bool identifyMemcpyPattern(const Function &func);
        bool identifyMemsetPattern(const Function &func);
        bool identifyStringOperationPattern(const Function &func);
        bool identifyMathPattern(const Function &func);

        bool isSystemFunction(const std::string &name) const;
        bool isLibraryFunction(const std::string &name) const;
        std::vector<Instruction> decodeFunction(const Function &function);
        CFG buildCFG(const Function &function);
        std::string formatAddress(uint32_t address) const;
        std::string escapeBackslashes(const std::string &path);
        bool hasMMIInstructions(const Function &function);
        bool hasVUInstructions(const Function &function);
        bool identifyFunctionType(const Function &function);
        void categorizeFunction(Function &function);
        uint32_t getSuccessor(const Instruction &inst, uint32_t currentAddr);
        bool isSelfModifyingCode(const Function &function);
        bool isLoopHeavyFunction(const Function &function);
    };
}

#endif // PS2RECOMP_ELF_ANALYZER_H