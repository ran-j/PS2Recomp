#ifndef PS2RECOMP_ELF_ANALYZER_H
#define PS2RECOMP_ELF_ANALYZER_H

#include <string>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <memory>
#include <map>
#include <set>
#include <functional>

namespace ps2recomp
{
    struct CFGNode;
    struct Instruction;
    struct FunctionCall;
    struct JumpTable;
    struct Relocation;
    struct Section;
    struct Symbol;
    struct Function;
    class R5900Decoder;
    class ElfParser;

    using CFG = std::unordered_map<uint32_t, CFGNode>;

    class ElfAnalyzer
    {
    public:
        explicit ElfAnalyzer(const std::string &elfPath);
        ~ElfAnalyzer();

        bool analyze();
        bool generateToml(const std::string &outputPath);
        bool importGhidraMap(const std::string &csvPath);
        const std::vector<Function>& getFunctions() const;
        bool isLibrarySymbolNameForHeuristics(const std::string &name) const;
        static bool isReliableSymbolNameForHeuristics(const std::string &name);
        static bool isSystemSymbolNameForHeuristics(const std::string &name);
        static bool shouldAutoSkipNameForHeuristics(const std::string &name);
        static int findEntryFunctionIndexForHeuristics(const std::vector<Function> &functions, uint32_t entryAddress);
        static int findFallbackEntryFunctionIndexForHeuristics(const std::vector<Function> &functions);
        static bool hasHardwareIOSignalForHeuristics(const std::vector<Instruction> &instructions);
        static bool hasLargeComplexMMISignalForHeuristics(const std::vector<Instruction> &instructions, size_t largeInstructionThreshold = 500);
        static bool hasSelfModifyingSignalForHeuristics(const std::vector<Instruction> &instructions, const std::vector<Section> &sections);
        static bool shouldSkipForPatchDensityForHeuristics(const std::string &functionName, uint32_t functionSizeBytes, size_t patchCount, bool isLibraryFunction);
        static std::vector<JumpTable> detectJumpTablesForHeuristics(const std::vector<Instruction> &instructions, const std::vector<Section> &sections, const std::function<bool(uint32_t, uint32_t &)> &readWord);
        static std::unordered_set<std::string> findRecursiveFunctionsForHeuristics(const std::unordered_map<std::string, std::vector<std::string>> &callGraph);

    private:
        std::string m_elfPath;
        std::unique_ptr<ElfParser> m_elfParser;
        std::unique_ptr<R5900Decoder> m_decoder;

        std::vector<Function> m_functions;
        std::vector<Symbol> m_symbols;
        std::vector<Section> m_sections;
        std::vector<Relocation> m_relocations;

        std::unordered_set<std::string> m_libFunctions;
        std::unordered_set<std::string> m_skipFunctions;
        std::unordered_set<uint32_t> m_forceRecompileStarts;
        std::unordered_set<std::string> m_knownLibNames;
        std::unordered_map<std::string, std::set<std::string>> m_functionDataUsage;
        std::unordered_map<uint32_t, std::string> m_commonDataAccess;

        std::map<uint32_t, uint32_t> m_patches;
        std::map<uint32_t, std::string> m_patchReasons;
        std::unordered_map<uint32_t, CFG> m_functionCFGs;
        std::vector<JumpTable> m_jumpTables;
        std::unordered_map<uint32_t, std::vector<FunctionCall>> m_functionCalls;

        std::unordered_map<uint32_t, uint32_t> m_mmioByInstructionAddress;

        void initializeLibraryFunctions();
        void analyzeEntryPoint();
        void analyzeLibraryFunctions();
        void analyzeDataUsage();

        void identifyPotentialPatches();
        bool tryPatchSelfModifyingStore(const Function &func,
                                        const std::vector<Instruction> &instructions,
                                        size_t index);
        bool tryResolveBasePlusOffset(const std::vector<Instruction> &instructions,
                                      size_t index,
                                      uint32_t reg,
                                      int16_t offset,
                                      uint32_t &baseAddr) const;
        bool tryResolveLuiBase(const std::vector<Instruction> &instructions,
                               size_t index,
                               uint32_t reg,
                               uint32_t &baseAddr) const;
        bool isCodeAddress(uint32_t addr) const;

        void analyzeControlFlow();
        void detectJumpTables();
        void analyzePerformanceCriticalPaths() const;
        void identifyRecursiveFunctions();
        void analyzeRegisterUsage() const;
        void analyzeFunctionSignatures() const;
        void optimizePatches();

        bool identifyMemcpyPattern(const Function &func) const;
        bool identifyMemsetPattern(const Function &func) const;
        bool identifyStringOperationPattern(const Function &func) const;
        bool identifyMathPattern(const Function &func) const;

        bool isSystemFunction(const std::string &name) const;
        bool isLibraryFunction(const std::string &name) const;
        std::vector<Instruction> decodeFunction(const Function &function) const;
        CFG buildCFG(const Function &function) const;
        std::string formatAddress(uint32_t address) const;
        std::string escapeBackslashes(const std::string &path);
        bool hasMMIInstructions(const Function &function) const;
        bool hasVUInstructions(const Function &function) const;
        bool shouldAutoSkipByHeuristic(const Function &function) const;
        bool identifyFunctionType(const Function &function);
        void categorizeFunction(Function &function);
        uint32_t getSuccessor(const Instruction &inst, uint32_t currentAddr);
        bool isSelfModifyingCode(const Function &function) const;
        bool isLoopHeavyFunction(const Function &function) const;
    };
}

#endif // PS2RECOMP_ELF_ANALYZER_H
