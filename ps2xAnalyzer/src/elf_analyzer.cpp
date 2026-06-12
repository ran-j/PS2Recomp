#include "ps2recomp/elf_analyzer.h"
#include "ps2recomp/analysis_passes.h"
#include "ps2recomp/elf_parser.h"
#include "ps2recomp/r5900_decoder.h"
#include "ps2recomp/sce_symbol_scanner.h"
#include "ps2recomp/toml_generator.h"
#include "ps2recomp/types.h"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <functional>
#include <limits>
#include <cstdlib>

namespace ps2recomp
{
    static uint32_t decodeAbsoluteJumpTarget(uint32_t instructionAddress, uint32_t targetField);
    static bool tryReadWord(const ElfParser *parser, uint32_t address, uint32_t &outWord);

    ElfAnalyzer::ElfAnalyzer(const std::string &elfPath)
        : m_elfPath(elfPath)
    {
        m_elfParser = std::make_unique<ElfParser>(elfPath);
        m_decoder = std::make_unique<R5900Decoder>();
        m_classifier.setSceSdkFunctionNames(&m_sceSdkFunctionNames);
    }

    ElfAnalyzer::~ElfAnalyzer() = default;

    void ElfAnalyzer::setSceSymbolDatabasePath(const std::string &databasePath)
    {
        m_sceSymbolDatabasePath = databasePath;
    }

    bool ElfAnalyzer::analyze()
    {
        std::cout << "Analyzing ELF file: " << m_elfPath << std::endl;

        if (!loadElf())
        {
            return false;
        }

        discoverSceSdkSymbols();
        buildFunctionIndex();

        analyzeEntryPoint();
        analyzeLibraryFunctions();
        decodeAllFunctionsOnce();
        classifyFunctions();

        runDataUsagePass();
        runPatchDetectionPass();
        runControlFlowPass();
        runJumpTablePass();
        runPerformancePass();
        identifyRecursiveFunctions();
        analyzeRegisterUsage();
        runSignaturePass();

        optimizePatches();
        printAnalysisSummary();

        return true;
    }

    bool ElfAnalyzer::loadElf()
    {
        if (!m_elfParser->parse())
        {
            std::cerr << "Failed to parse ELF file" << std::endl;
            return false;
        }

        m_context.clear();
        m_libFunctions.clear();
        m_untrackedStubFunctions.clear();
        m_forceRecompileStarts.clear();
        m_sceSdkFunctionNames.clear();
        m_functionDataUsage.clear();
        m_commonDataAccess.clear();
        m_patches.clear();
        m_patchReasons.clear();
        m_functionCFGs.clear();
        m_jumpTables.clear();
        m_functionCalls.clear();
        m_performanceCriticalReasons.clear();
        m_mmioByInstructionAddress.clear();

        m_context.functions = m_elfParser->extractFunctions();
        m_context.symbols = m_elfParser->extractSymbols();
        m_context.sections = m_elfParser->getSections();
        m_context.relocations = m_elfParser->getRelocations();
        m_context.buildFunctionIndex();

        std::cout << "Extracted " << m_context.functions.size() << " functions" << std::endl;
        std::cout << "Extracted " << m_context.symbols.size() << " symbols" << std::endl;
        std::cout << "Extracted " << m_context.sections.size() << " sections" << std::endl;
        std::cout << "Extracted " << m_context.relocations.size() << " relocations" << std::endl;

        return true;
    }

    bool ElfAnalyzer::generateToml(const std::string &outputPath)
    {
        const TomlGeneratorInput input{
            m_elfPath,
            m_context,
            m_libFunctions,
            m_untrackedStubFunctions,
            m_mmioByInstructionAddress,
            m_jumpTables,
            m_patches,
            m_patchReasons,
            m_performanceCriticalReasons};

        return TomlGenerator::generate(input, outputPath);
    }

    void ElfAnalyzer::discoverSceSdkSymbols()
    {
        std::string databasePath = m_sceSymbolDatabasePath;
        if (databasePath.empty())
        {
            if (const char *envPath = std::getenv("PS2RECOMP_SCE_SYMBOL_DB"))
            {
                databasePath = envPath;
            }
        }

        SceSymbolScanner scanner;
        if (!scanner.loadDatabase(databasePath))
        {
            const std::string sourceDescription = databasePath.empty()
                                                      ? std::string("embedded database")
                                                      : databasePath;
            std::cerr << "Failed to load SCE symbol database from " << sourceDescription
                      << ": " << scanner.lastError() << std::endl;
            return;
        }

        const std::string sourceDescription = databasePath.empty()
                                                  ? std::string("embedded database")
                                                  : databasePath;

        const std::vector<SceSymbolMatch> matches = scanner.scan(m_context.sections);
        if (matches.empty())
        {
            std::cout << "No SCE SDK symbols discovered from " << sourceDescription << std::endl;
            return;
        }

        size_t renamedFunctions = 0;
        size_t addedFunctions = 0;

        for (const auto &match : matches)
        {
            if (match.size > std::numeric_limits<uint32_t>::max() - match.address)
            {
                continue;
            }

            m_sceSdkFunctionNames.insert(match.name);

            Symbol symbol;
            symbol.name = match.name;
            symbol.address = match.address;
            symbol.size = match.size;
            symbol.isFunction = true;
            symbol.isImported = false;
            symbol.isExported = true;
            m_context.symbols.push_back(std::move(symbol));

            const uint32_t discoveredEnd = match.address + match.size;
            if (Function *func = m_context.findFunction(match.address))
            {
                if (func->name != match.name)
                {
                    func->name = match.name;
                    renamedFunctions++;
                }

                if (func->end < discoveredEnd)
                {
                    func->end = discoveredEnd;
                }
                continue;
            }

            Function function;
            function.name = match.name;
            function.start = match.address;
            function.end = discoveredEnd;
            m_context.functions.push_back(std::move(function));
            m_context.functionIndexByStart[match.address] = m_context.functions.size() - 1;
            addedFunctions++;
        }

        std::sort(m_context.functions.begin(), m_context.functions.end(),
                  [](const Function &a, const Function &b)
                  {
                      return a.start < b.start;
                  });

        buildFunctionIndex();
        clearDecodedInstructionCache();

        std::cout << "Discovered " << matches.size() << " SCE SDK symbol match(es) from "
                  << sourceDescription << std::endl;
        std::cout << "- renamed " << renamedFunctions << " existing function(s)" << std::endl;
        std::cout << "- added " << addedFunctions << " function(s)" << std::endl;
    }

    int ElfAnalyzer::findEntryFunctionIndexForHeuristics(const std::vector<Function> &functions, uint32_t entryAddress)
    {
        auto it = std::find_if(functions.begin(), functions.end(),
                               [entryAddress](const Function &f)
                               { return f.start == entryAddress; });
        if (it != functions.end())
        {
            return static_cast<int>(std::distance(functions.begin(), it));
        }

        it = std::find_if(functions.begin(), functions.end(),
                          [entryAddress](const Function &f)
                          { return f.start <= entryAddress && entryAddress < f.end; });
        if (it != functions.end())
        {
            return static_cast<int>(std::distance(functions.begin(), it));
        }

        return -1;
    }

    int ElfAnalyzer::findFallbackEntryFunctionIndexForHeuristics(const std::vector<Function> &functions)
    {
        auto it = std::find_if(functions.begin(), functions.end(),
                               [](const Function &f)
                               { return f.start == 0x100000 || f.start == 0x80100000; });
        if (it == functions.end())
        {
            return -1;
        }

        return static_cast<int>(std::distance(functions.begin(), it));
    }

    void ElfAnalyzer::analyzeEntryPoint()
    {
        const uint32_t entryAddress = m_elfParser->getEntryPoint();
        const int entryIndex = findEntryFunctionIndexForHeuristics(m_context.functions, entryAddress);
        if (entryIndex >= 0)
        {
            const Function &entryFunction = m_context.functions[static_cast<size_t>(entryIndex)];
            std::cout << "Found entry point from ELF header: 0x" << std::hex << entryAddress
                      << " in function " << entryFunction.name << " (starts at 0x" << entryFunction.start << ")"
                      << std::dec << std::endl;

            m_forceRecompileStarts.insert(entryFunction.start);

            const auto &instructions = getDecodedInstructions(entryFunction);

            for (const auto &inst : instructions)
            {
                if (inst.opcode == OPCODE_JAL)
                {
                    uint32_t target = decodeAbsoluteJumpTarget(inst.address, inst.target);

                    if (const Function *func = m_context.findFunction(target))
                    {
                        std::cout << "Found entry call to: " << func->name << " at 0x"
                                  << std::hex << inst.address << std::dec << std::endl;
                    }
                }
            }
        }
        else
        {
            std::cout << "Entry point 0x" << std::hex << entryAddress
                      << " not mapped to an extracted function" << std::dec << std::endl;

            const int fallbackIndex = findFallbackEntryFunctionIndexForHeuristics(m_context.functions);
            if (fallbackIndex >= 0)
            {
                const Function &fallbackEntry = m_context.functions[static_cast<size_t>(fallbackIndex)];
                std::cout << "Found potential entry point by address: " << fallbackEntry.name
                          << " at 0x" << std::hex << fallbackEntry.start << std::dec << std::endl;
                m_forceRecompileStarts.insert(fallbackEntry.start);
            }
        }
    }

    void ElfAnalyzer::analyzeLibraryFunctions()
    {
        for (const auto &symbol : m_context.symbols)
        {
            if (symbol.isFunction)
            {
                if (!FunctionClassifier::isReliableSymbolName(symbol.name))
                {
                    continue;
                }

                if (isLibraryFunction(symbol.name))
                {
                    if (!m_forceRecompileStarts.contains(symbol.address))
                    {
                        if (FunctionClassifier::hasRuntimeHandler(symbol.name))
                        {
                            m_libFunctions.insert(symbol.name);
                        }
                        else
                        {
                            m_untrackedStubFunctions.insert(symbol.name);
                        }
                    }
                }
            }
        }

        for (const auto &func : m_context.functions)
        {
            if (!FunctionClassifier::isReliableSymbolName(func.name))
            {
                continue;
            }

            if (isLibraryFunction(func.name))
            {
                if (!m_forceRecompileStarts.contains(func.start))
                {
                    if (FunctionClassifier::hasRuntimeHandler(func.name))
                    {
                        m_libFunctions.insert(func.name);
                    }
                    else
                    {
                        m_untrackedStubFunctions.insert(func.name);
                    }
                }
            }
        }
    }

    void ElfAnalyzer::analyzeDataUsage()
    {
        std::cout << "Analyzing data usage patterns..." << std::endl;

        std::map<uint32_t, std::set<std::string>> memoryAccessMap;

        for (const auto &func : m_context.functions)
        {
            if (m_libFunctions.contains(func.name))
            {
                continue;
            }

            const auto &instructions = getDecodedInstructions(func);

            for (const auto &inst : instructions)
            {
                if (inst.opcode == OPCODE_LW || inst.opcode == OPCODE_SW ||
                    inst.opcode == OPCODE_LB || inst.opcode == OPCODE_SB ||
                    inst.opcode == OPCODE_LH || inst.opcode == OPCODE_SH ||
                    inst.opcode == OPCODE_LBU || inst.opcode == OPCODE_LHU ||
                    inst.opcode == OPCODE_LQ || inst.opcode == OPCODE_SQ)
                {
                    // Check if the memory address involves $gp (global pointer)
                    if (inst.rs == 28) // $gp is typically register 28
                    {
                        int16_t offset = static_cast<int16_t>(inst.immediate);
                        uint32_t gpValue = 0;

                        // Try to find GP value in ELF sections
                        for (const auto &section : m_context.sections)
                        {
                            if (section.name == ".got" || section.name == ".data" ||
                                section.name == ".sdata" || section.name == ".sbss")
                            {
                                gpValue = section.address;
                                break;
                            }
                        }

                        if (gpValue != 0)
                        {
                            uint32_t targetAddr = gpValue + offset;
                            memoryAccessMap[targetAddr].insert(func.name);

                            auto symIt = std::find_if(m_context.symbols.begin(), m_context.symbols.end(),
                                                      [targetAddr](const Symbol &s)
                                                      { return !s.isFunction && s.address == targetAddr; });

                            if (symIt != m_context.symbols.end())
                            {
                                std::cout << "Function " << func.name << " accesses data symbol "
                                          << symIt->name << " at 0x" << std::hex << targetAddr
                                          << std::dec << std::endl;

                                m_functionDataUsage[func.name].insert(symIt->name);
                            }
                            else
                            {
                                // Try to find the symbol it belongs to, even if not exact match
                                for (const auto &sym : m_context.symbols)
                                {
                                    if (!sym.isFunction && targetAddr >= sym.address &&
                                        targetAddr < sym.address + sym.size)
                                    {
                                        std::cout << "Function " << func.name << " accesses data within symbol "
                                                  << sym.name << " at offset 0x" << std::hex << (targetAddr - sym.address)
                                                  << std::dec << std::endl;

                                        m_functionDataUsage[func.name].insert(sym.name);
                                        break;
                                    }
                                }
                            }
                        }
                    }
                    // Also check for direct addressing with LUI+ADDIU combinations
                    else if (inst.opcode == OPCODE_LW || inst.opcode == OPCODE_SW)
                    {
                        // Look for the LUI instruction that sets up the high bits
                        uint32_t baseAddr = 0;
                        for (int i = 1; i <= 5 && static_cast<int>(inst.address) - i * 4 >= static_cast<int>(func.start); i++)
                        {
                            uint32_t prevAddr = inst.address - i * 4;
                            uint32_t prevInst = 0;
                            if (!tryReadWord(m_elfParser.get(), prevAddr, prevInst))
                            {
                                continue;
                            }

                            // Check if it's a LUI instruction for the same register
                            if (OPCODE(prevInst) == OPCODE_LUI && RT(prevInst) == inst.rs)
                            {
                                baseAddr = IMMEDIATE(prevInst) << 16;
                                break;
                            }
                        }

                        if (baseAddr != 0)
                        {
                            uint32_t targetAddr = baseAddr + static_cast<int16_t>(inst.immediate);

                            // Detect MMIO accesses
                            if ((targetAddr >= 0x10000000 && targetAddr < 0x14000000) || // I/O
                                (targetAddr >= 0x70000000 && targetAddr < 0x70004000))   // Scratchpad
                            {
                                m_mmioByInstructionAddress[inst.address] = targetAddr;
                                std::cout << "Detected MMIO access at " << std::hex << inst.address
                                          << " -> " << targetAddr << std::dec << std::endl;
                            }

                            for (const auto &section : m_context.sections)
                            {
                                if (targetAddr >= section.address && targetAddr < section.address + section.size)
                                {
                                    auto symIt = std::find_if(m_context.symbols.begin(), m_context.symbols.end(),
                                                              [targetAddr](const Symbol &s)
                                                              { return !s.isFunction && s.address <= targetAddr &&
                                                                       s.address + s.size > targetAddr; });

                                    if (symIt != m_context.symbols.end())
                                    {
                                        std::cout << "Function " << func.name << " directly accesses "
                                                  << (inst.opcode == OPCODE_LW ? "reads from" : "writes to")
                                                  << " data symbol " << symIt->name
                                                  << " at 0x" << std::hex << targetAddr << std::dec << std::endl;

                                        m_functionDataUsage[func.name].insert(symIt->name);
                                    }
                                    break;
                                }
                            }
                        }
                    }
                }
            }
        }

        // Identify commonly accessed data (potential global structures)
        for (const auto &[addr, funcs] : memoryAccessMap)
        {
            if (funcs.size() > 3) // If multiple functions access this data
            {
                std::string dataName = formatAddress(addr);

                auto symIt = std::find_if(m_context.symbols.begin(), m_context.symbols.end(),
                                          [addr](const Symbol &s)
                                          { return !s.isFunction && s.address == addr; });

                if (symIt != m_context.symbols.end())
                {
                    dataName = symIt->name;
                }

                std::cout << "Common data: " << dataName << " at 0x" << std::hex << addr
                          << std::dec << " accessed by " << funcs.size() << " functions" << std::endl;

                m_commonDataAccess[addr] = dataName;
            }
        }

        for (const auto &func : m_context.functions)
        {
            if (m_libFunctions.contains(func.name))
            {
                continue;
            }

            const auto &instructions = getDecodedInstructions(func);

            for (size_t i = 0; i < instructions.size(); i++)
            {
                const auto &inst = instructions[i];

                if ((inst.opcode == OPCODE_SPECIAL && (inst.function == SPECIAL_SLL || inst.function == SPECIAL_SLLV)) ||
                    (inst.opcode == OPCODE_SPECIAL && inst.function == SPECIAL_MULT))
                {
                    for (size_t j = i + 1; j < std::min(i + 5, instructions.size()); j++)
                    {
                        const auto &nextInst = instructions[j];

                        if ((nextInst.opcode == OPCODE_LW || nextInst.opcode == OPCODE_SW) &&
                            nextInst.rs == inst.rd)
                        {
                            std::cout << "Found possible array access in function " << func.name
                                      << " at 0x" << std::hex << nextInst.address << std::dec << std::endl;
                            break;
                        }
                    }
                }
            }
        }
    }

    void ElfAnalyzer::identifyPotentialPatches()
    {
        std::cout << "Identifying potential patches..." << std::endl;

        for (const auto &func : m_context.functions)
        {
            const auto &instructions = getDecodedInstructions(func);

            for (size_t index = 0; index + 1 < instructions.size(); ++index)
            {
                tryPatchSelfModifyingStore(func, instructions, index);
            }
        }
    }

    bool ElfAnalyzer::tryPatchSelfModifyingStore(
        const Function &func,
        const std::vector<Instruction> &instructions,
        size_t index)
    {
        const Instruction &storeInst = instructions[index];
        if (storeInst.opcode != OPCODE_SW)
        {
            return false;
        }

        const Instruction &nextInst = instructions[index + 1];
        if (nextInst.opcode != OPCODE_J && nextInst.opcode != OPCODE_JAL)
        {
            return false;
        }

        uint32_t targetAddr = 0;
        if (!tryResolveBasePlusOffset(instructions, index, storeInst.rs, static_cast<int16_t>(storeInst.immediate), targetAddr))
        {
            return false;
        }

        if (!isCodeAddress(targetAddr))
        {
            return false;
        }

        const uint32_t jumpTarget = decodeAbsoluteJumpTarget(nextInst.address, nextInst.target);
        if (!isCodeAddress(jumpTarget))
        {
            return false;
        }

        std::cout << "Potential self-modifying code at " << formatAddress(storeInst.address)
                  << " writing to " << formatAddress(targetAddr)
                  << " then jumping to " << formatAddress(jumpTarget)
                  << " in function " << func.name << std::endl;

        m_patches[storeInst.address] = 0x00000000;
        m_patchReasons[storeInst.address] = "Potential self-modifying code";
        return true;
    }

    bool ElfAnalyzer::tryResolveBasePlusOffset(
        const std::vector<Instruction> &instructions,
        size_t index,
        uint32_t baseReg,
        int16_t offset,
        uint32_t &outAddr) const
    {
        uint32_t baseAddr = 0;
        if (!tryResolveLuiBase(instructions, index, baseReg, baseAddr))
        {
            return false;
        }

        outAddr = baseAddr + static_cast<int32_t>(offset);
        return true;
    }

    bool ElfAnalyzer::tryResolveLuiBase(
        const std::vector<Instruction> &instructions,
        size_t index,
        uint32_t reg,
        uint32_t &baseAddr) const
    {
        baseAddr = 0;

        const size_t start = (index > 8) ? (index - 8) : 0;
        for (size_t pos = index; pos-- > start;)
        {
            const Instruction &prev = instructions[pos];

            if ((prev.opcode == OPCODE_ADDIU || prev.opcode == OPCODE_ORI) && prev.rt == reg)
            {
                uint32_t hiBase = 0;
                if (!tryResolveLuiBase(instructions, pos, prev.rs, hiBase))
                {
                    return false;
                }

                if (prev.opcode == OPCODE_ADDIU)
                {
                    baseAddr = hiBase + static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(prev.immediate)));
                }
                else
                {
                    baseAddr = hiBase | static_cast<uint32_t>(prev.immediate);
                }

                return true;
            }

            if (prev.opcode == OPCODE_LUI && prev.rt == reg)
            {
                baseAddr = prev.immediate << 16;
                return true;
            }

            if (prev.rt == reg || prev.rd == reg)
            {
                break;
            }
        }

        return false;
    }

    bool ElfAnalyzer::isCodeAddress(uint32_t addr) const
    {
        for (const auto &section : m_context.sections)
        {
            if (!section.isCode)
            {
                continue;
            }

            const uint32_t sectionEnd = section.address + section.size;
            if (addr >= section.address && addr < sectionEnd)
            {
                return true;
            }
        }

        return false;
    }

    void ElfAnalyzer::analyzeControlFlow()
    {
        std::cout << "Analyzing control flow of functions..." << std::endl;

        for (const auto &func : m_context.functions)
        {
            if (m_libFunctions.contains(func.name))
            {
                continue;
            }

            CFG cfg = buildCFG(func);
            m_functionCFGs[func.start] = cfg;

            const auto &instructions = getDecodedInstructions(func);
            for (const auto &inst : instructions)
            {
                if (inst.opcode == OPCODE_JAL ||
                    (inst.opcode == OPCODE_SPECIAL && inst.function == SPECIAL_JALR))
                {

                    uint32_t targetAddr = 0;

                    if (inst.opcode == OPCODE_JAL)
                    {
                        targetAddr = decodeAbsoluteJumpTarget(inst.address, inst.target);
                    }
                    else
                    {
                        // For JALR, the target is in the register - harder to statically analyze so lets skip it
                        continue;
                    }

                    if (const Function *targetFunc = m_context.findFunction(targetAddr))
                    {
                        FunctionCall call;
                        call.callerAddress = inst.address;
                        call.calleeAddress = targetAddr;
                        call.calleeName = targetFunc->name;

                        m_functionCalls[func.start].push_back(call);

                        std::cout << "Function " << func.name << " calls " << targetFunc->name
                                  << " at " << formatAddress(inst.address) << std::endl;
                    }
                }
            }
        }
    }

    std::vector<JumpTable> ElfAnalyzer::detectJumpTablesForHeuristics(
        const std::vector<Instruction> &instructions,
        const std::vector<Section> &sections,
        const std::function<bool(uint32_t, uint32_t &)> &readWord)
    {
        return AnalysisPasses::detectJumpTables(instructions, sections, readWord);
    }

    void ElfAnalyzer::detectJumpTables()
    {
        std::cout << "Detecting jump tables..." << std::endl;

        for (const auto &func : m_context.functions)
        {
            if (m_libFunctions.contains(func.name))
            {
                continue;
            }

            const auto &instructions = getDecodedInstructions(func);
            std::vector<JumpTable> detectedTables = detectJumpTablesForHeuristics(
                instructions,
                m_context.sections,
                [this](uint32_t address, uint32_t &outWord) -> bool
                {
                    return tryReadWord(m_elfParser.get(), address, outWord);
                });

            for (const auto &jumpTable : detectedTables)
            {
                std::cout << "Detected jump table in function " << func.name
                          << " at " << formatAddress(jumpTable.address) << std::endl;
                for (const auto &[index, target] : jumpTable.entries)
                {
                    std::cout << "  - Jump table entry " << index << ": 0x"
                              << std::hex << target << std::dec << std::endl;
                }

                m_jumpTables.push_back(jumpTable);
            }
        }
    }

    void ElfAnalyzer::analyzePerformanceCriticalPaths()
    {
        std::cout << "Analyzing performance-critical paths..." << std::endl;
        m_performanceCriticalReasons.clear();

        for (const auto &func : m_context.functions)
        {
            if (m_libFunctions.contains(func.name))
            {
                continue;
            }

            const auto &instructions = getDecodedInstructions(func);
            if (hasMMIInstructions(func) || hasVUInstructions(func))
            {
                m_performanceCriticalReasons[func.start] = "Uses SIMD instructions";
            }
            else if (isLoopHeavyFunction(func))
            {
                m_performanceCriticalReasons[func.start] = "Contains heavy loops";
            }

            for (const auto &inst : instructions)
            {
                if (inst.isBranch)
                {
                    int32_t offset = static_cast<int16_t>(inst.immediate) << 2;
                    uint32_t targetAddr = inst.address + 4 + offset;

                    if (targetAddr < inst.address)
                    {
                        size_t loopSize = (inst.address - targetAddr) / 4 + 1;

                        if (loopSize < 20)
                        {
                            std::cout << "Found tight loop in function " << func.name
                                      << " from " << formatAddress(targetAddr)
                                      << " to " << formatAddress(inst.address)
                                      << " (size: " << loopSize << " instructions)" << std::endl;

                            bool hasMultimedia = false;
                            for (const auto &instruction : instructions)
                            {
                                if (instruction.address >= targetAddr && instruction.address <= inst.address)
                                {
                                    if (instruction.isMultimedia)
                                    {
                                        hasMultimedia = true;
                                        break;
                                    }
                                }
                            }

                            if (hasMultimedia)
                            {
                                std::cout << "  - Loop contains multimedia instructions" << std::endl;
                            }
                        }
                    }
                }
            }
        }
    }

    std::unordered_set<std::string> ElfAnalyzer::findRecursiveFunctionsForHeuristics(
        const std::unordered_map<std::string, std::vector<std::string>> &callGraph)
    {
        return AnalysisPasses::findRecursiveFunctions(callGraph);
    }

    void ElfAnalyzer::identifyRecursiveFunctions()
    {
        std::cout << "Identifying recursive functions..." << std::endl;

        // Library stubs are runtime-owned and do not need analyzer graph work.
        std::unordered_set<std::string> eligible;
        eligible.reserve(m_context.functions.size());

        for (const auto &func : m_context.functions)
        {
            if (m_libFunctions.contains(func.name))
            {
                continue;
            }

            eligible.insert(func.name);
        }

        std::unordered_map<std::string, std::vector<std::string>> callGraph;
        callGraph.reserve(eligible.size());

        for (const auto &func : m_context.functions)
        {
            if (!eligible.contains(func.name))
            {
                continue;
            }

            auto itCalls = m_functionCalls.find(func.start);
            if (itCalls == m_functionCalls.end())
            {
                continue;
            }

            auto &edges = callGraph[func.name];
            edges.reserve(itCalls->second.size());

            for (const auto &call : itCalls->second)
            {
                // non-eligible nodes to graph.
                if (!eligible.contains(call.calleeName))
                {
                    continue;
                }

                edges.push_back(call.calleeName);
            }
        }

        std::unordered_set<std::string> recursive = findRecursiveFunctionsForHeuristics(callGraph);
        for (const auto &name : recursive)
        {
            auto it = callGraph.find(name);
            if (it != callGraph.end() &&
                std::find(it->second.begin(), it->second.end(), name) != it->second.end())
            {
                std::cout << "Function " << name << " is directly recursive" << std::endl;
            }
            else
            {
                std::cout << "Function " << name << " is part of a mutually recursive cycle" << std::endl;
            }
        }
    }

    void ElfAnalyzer::analyzeRegisterUsage() const
    {
        std::cout << "Analyzing register usage patterns..." << std::endl;

        for (const auto &func : m_context.functions)
        {
            if (m_libFunctions.contains(func.name))
            {
                continue;
            }

            const auto &instructions = getDecodedInstructions(func);
            std::set<uint32_t> regsRead, regsWritten;

            for (const auto &inst : instructions)
            {
                if (inst.rs != 0)
                    regsRead.insert(inst.rs);
                if (inst.rt != 0 && inst.opcode != OPCODE_SW && inst.opcode != OPCODE_SB &&
                    inst.opcode != OPCODE_SH && inst.opcode != OPCODE_SQ)
                {
                    regsRead.insert(inst.rt);
                }

                if (inst.opcode == OPCODE_SPECIAL || inst.opcode == OPCODE_REGIMM ||
                    inst.opcode == OPCODE_COP1 || inst.opcode == OPCODE_COP2)
                {
                    // R-type instructions
                    if (inst.rd != 0)
                        regsWritten.insert(inst.rd);
                }
                else if (inst.opcode == OPCODE_JAL)
                {
                    // JAL writes to $ra (r31)
                    regsWritten.insert(31);
                }
                else if (inst.opcode == OPCODE_LUI || inst.opcode == OPCODE_ADDIU ||
                         inst.opcode == OPCODE_ORI || inst.opcode == OPCODE_LW ||
                         inst.opcode == OPCODE_LB || inst.opcode == OPCODE_LH)
                {
                    // I-type instructions that write to rt
                    if (inst.rt != 0)
                        regsWritten.insert(inst.rt);
                }
            }

            // Check if function follows standard calling convention
            bool hasStackOps = false;
            bool savesFP = false;
            bool savesRA = false;

            for (size_t i = 0; i < std::min(static_cast<size_t>(10), instructions.size()); i++)
            {
                const auto &inst = instructions[i];

                // ADDIU $sp, $sp, -X (allocate stack frame)
                if (inst.opcode == OPCODE_ADDIU && inst.rs == 29 && inst.rt == 29 &&
                    static_cast<int16_t>(inst.immediate) < 0)
                {
                    hasStackOps = true;
                }

                // SW $fp, X($sp) (save frame pointer)
                if (inst.opcode == OPCODE_SW && inst.rt == 30 && inst.rs == 29)
                {
                    savesFP = true;
                }

                // SW $ra, X($sp) (save return address)
                if (inst.opcode == OPCODE_SW && inst.rt == 31 && inst.rs == 29)
                {
                    savesRA = true;
                }
            }

            if (hasStackOps)
            {
                std::cout << "Function " << func.name << " allocates a stack frame" << std::endl;

                if (savesFP)
                    std::cout << "  - Saves frame pointer ($fp)" << std::endl;
                if (savesRA)
                    std::cout << "  - Saves return address ($ra)" << std::endl;
            }

            if (regsRead.contains(4) || regsRead.contains(5) ||
                regsRead.contains(6) || regsRead.contains(7))
            {
                std::cout << "  - Uses argument registers (a0-a3)" << std::endl;
            }

            if (regsWritten.contains(2) || regsWritten.contains(3))
            {
                std::cout << "  - Sets return values (v0-v1)" << std::endl;
            }
        }
    }

    void ElfAnalyzer::analyzeFunctionSignatures() const
    {
        std::cout << "Analyzing function signatures..." << std::endl;

        for (const auto &func : m_context.functions)
        {
            if (m_libFunctions.contains(func.name))
            {
                continue;
            }

            const auto &instructions = getDecodedInstructions(func);

            int paramCount = 0;
            bool usesFloatingPoint = false;
            bool usesDoublewords = false;
            bool returnsSomething = false;

            for (const auto &inst : instructions)
            {
                if ((inst.rs >= 4 && inst.rs <= 7) || (inst.rt >= 4 && inst.rt <= 7))
                {
                    paramCount = std::max(paramCount, static_cast<int>(std::max(inst.rs, inst.rt) - 3));
                }

                if (inst.opcode == OPCODE_COP1)
                {
                    usesFloatingPoint = true;
                }

                // Check for 64-bit operations
                if (inst.opcode == OPCODE_LD || inst.opcode == OPCODE_SD ||
                    (inst.opcode == OPCODE_SPECIAL &&
                     (inst.function == SPECIAL_DSLL || inst.function == SPECIAL_DSRL ||
                      inst.function == SPECIAL_DSRA || inst.function == SPECIAL_DSLLV ||
                      inst.function == SPECIAL_DSRLV || inst.function == SPECIAL_DSRAV)))
                {
                    usesDoublewords = true;
                }

                // Check for return value setting, addition we could do as well thous check (inst.opcode == OPCODE_ADDIU && inst.rs == 0) ||  // LI pattern using ADDIU $rt, $zero, imm  and (inst.opcode == OPCODE_ORI && i > 0 && instructions[i - 1].opcode == OPCODE_LUI && instructions[i - 1].rt == inst.rs && inst.rt == inst.rs)
                if ((inst.opcode == OPCODE_ADDIU || inst.opcode == OPCODE_ORI ||
                     inst.opcode == OPCODE_LW) &&
                    (inst.rt == 2 || inst.rt == 3))
                {
                    returnsSomething = true;
                }
                else if (inst.opcode == OPCODE_SPECIAL &&
                         (inst.function == SPECIAL_ADD || inst.function == SPECIAL_ADDU ||
                          inst.function == SPECIAL_SUB || inst.function == SPECIAL_SUBU ||
                          inst.function == SPECIAL_AND || inst.function == SPECIAL_OR ||
                          inst.function == SPECIAL_XOR || inst.function == SPECIAL_NOR) &&
                         (inst.rd == 2 || inst.rd == 3))
                {
                    returnsSomething = true;
                }
            }

            if (paramCount > 0 || usesFloatingPoint || usesDoublewords || returnsSomething)
            {
                std::cout << "Function " << func.name << " signature analysis:" << std::endl;
                if (paramCount > 0)
                {
                    std::cout << "  - Uses approximately " << paramCount << " parameter(s)" << std::endl;
                }
                if (usesFloatingPoint)
                {
                    std::cout << "  - Uses floating point operations" << std::endl;
                }
                if (usesDoublewords)
                {
                    std::cout << "  - Uses 64-bit operations" << std::endl;
                }
                if (returnsSomething)
                {
                    std::cout << "  - Returns a value" << std::endl;
                }
            }
        }
    }

    void ElfAnalyzer::optimizePatches()
    {
        std::cout << "Optimizing patches..." << std::endl;

        std::map<uint32_t, std::vector<uint32_t>> functionPatches;

        for (const auto &patch : m_patches)
        {
            uint32_t patchAddr = patch.first;

            if (const Function *func = m_context.findFunctionContaining(patchAddr))
            {
                functionPatches[func->start].push_back(patchAddr);
            }
        }

        for (const auto &[funcStart, patchAddrs] : functionPatches)
        {
            if (Function *func = m_context.findFunction(funcStart))
            {
                if (m_forceRecompileStarts.contains(func->start))
                {
                    continue;
                }

                if (patchAddrs.size() > 3)
                {
                    std::cout << "Function " << func->name << " has " << patchAddrs.size()
                              << " patches. Manual review may be needed." << std::endl;
                }
            }
        }

        std::vector<uint32_t> patchAddrs;
        for (const auto &patch : m_patches)
        {
            patchAddrs.push_back(patch.first);
        }

        if (patchAddrs.size() == 0)
            return;

        std::sort(patchAddrs.begin(), patchAddrs.end());

        for (size_t i = 0; i < patchAddrs.size() - 1; i++)
        {
            if (patchAddrs[i] + 4 == patchAddrs[i + 1])
            {
                std::cout << "Sequential patches at " << formatAddress(patchAddrs[i])
                          << " and " << formatAddress(patchAddrs[i + 1]) << std::endl;

                // If they're both NOPs, we could potentially optimize them together
                if (m_patches[patchAddrs[i]] == 0 && m_patches[patchAddrs[i + 1]] == 0)
                {
                    std::cout << "  - Both are NOPs, could be combined in recompilation" << std::endl;
                }
            }
        }
    }

    bool ElfAnalyzer::identifyMemcpyPattern(const Function &func) const
    {
        const auto &instructions = getDecodedInstructions(func);

        bool hasLoop = false;
        bool loadsData = false;
        bool storesData = false;
        bool incrementsPointers = false;

        for (const auto &inst : instructions)
        {
            if (inst.isBranch)
            {
                int32_t offset = static_cast<int16_t>(inst.immediate) << 2;
                if (inst.address + 4 + offset < inst.address)
                {
                    hasLoop = true;
                }
            }

            if (inst.opcode == OPCODE_LW || inst.opcode == OPCODE_LB ||
                inst.opcode == OPCODE_LH || inst.opcode == OPCODE_LD ||
                inst.opcode == OPCODE_LQ)
            {
                loadsData = true;
            }

            if (inst.opcode == OPCODE_SW || inst.opcode == OPCODE_SB ||
                inst.opcode == OPCODE_SH || inst.opcode == OPCODE_SD ||
                inst.opcode == OPCODE_SQ)
            {
                storesData = true;
            }

            if (inst.opcode == OPCODE_ADDIU &&
                (inst.immediate == 4 || inst.immediate == 8 || inst.immediate == 16))
            {
                incrementsPointers = true;
            }
        }

        return hasLoop && loadsData && storesData && incrementsPointers;
    }

    bool ElfAnalyzer::identifyMemsetPattern(const Function &func) const
    {
        const auto &instructions = getDecodedInstructions(func);

        bool hasLoop = false;
        bool usesConstant = false;
        bool storesData = false;
        bool incrementsPointer = false;

        for (const auto &inst : instructions)
        {
            if (inst.isBranch)
            {
                int32_t offset = static_cast<int16_t>(inst.immediate) << 2;
                if (inst.address + 4 + offset < inst.address)
                {
                    hasLoop = true;
                }
            }

            if (inst.opcode == OPCODE_LUI || inst.opcode == OPCODE_ORI ||
                inst.opcode == OPCODE_ADDIU || inst.opcode == OPCODE_ANDI)
            {
                usesConstant = true;
            }

            if (inst.opcode == OPCODE_SW || inst.opcode == OPCODE_SB ||
                inst.opcode == OPCODE_SH || inst.opcode == OPCODE_SD ||
                inst.opcode == OPCODE_SQ)
            {
                storesData = true;
            }

            if (inst.opcode == OPCODE_ADDIU &&
                (inst.immediate == 4 || inst.immediate == 8 || inst.immediate == 16))
            {
                incrementsPointer = true;
            }
        }

        return hasLoop && usesConstant && storesData && incrementsPointer;
    }

    bool ElfAnalyzer::identifyStringOperationPattern(const Function &func) const
    {
        const auto &instructions = getDecodedInstructions(func);

        bool hasLoop = false;
        bool checksZero = false;
        bool loadsByte = false;
        bool storesByte = false;

        for (const auto &inst : instructions)
        {
            if (inst.isBranch)
            {
                int32_t offset = static_cast<int16_t>(inst.immediate) << 2;
                if (inst.address + 4 + offset < inst.address)
                {
                    hasLoop = true;
                }
            }

            if ((inst.opcode == OPCODE_BEQ && (inst.rs == 0 || inst.rt == 0)) ||
                (inst.opcode == OPCODE_SPECIAL && inst.function == SPECIAL_SLT && inst.rd != 0))
            {
                checksZero = true;
            }

            if (inst.opcode == OPCODE_LB || inst.opcode == OPCODE_LBU)
            {
                loadsByte = true;
            }

            if (inst.opcode == OPCODE_SB)
            {
                storesByte = true;
            }
        }

        return hasLoop && checksZero && (loadsByte || storesByte);
    }

    bool ElfAnalyzer::identifyMathPattern(const Function &func) const
    {
        const auto &instructions = getDecodedInstructions(func);

        int mathOps = 0;
        bool usesFPU = false;

        for (const auto &inst : instructions)
        {
            // Count ALU operations
            if (inst.opcode == OPCODE_SPECIAL &&
                (inst.function == SPECIAL_ADD || inst.function == SPECIAL_ADDU ||
                 inst.function == SPECIAL_SUB || inst.function == SPECIAL_SUBU ||
                 inst.function == SPECIAL_MULT || inst.function == SPECIAL_MULTU ||
                 inst.function == SPECIAL_DIV || inst.function == SPECIAL_DIVU))
            {
                mathOps++;
            }

            // Check for FPU usage
            if (inst.opcode == OPCODE_COP1)
            {
                usesFPU = true;
                mathOps++;
            }
        }

        // If more than 30% of instructions are math operations, it's likely a math function
        return mathOps > instructions.size() * 0.3 || usesFPU;
    }

    CFG ElfAnalyzer::buildCFG(const Function &function) const
    {
        CFG cfg;
        const auto &instructions = getDecodedInstructions(function);
        std::map<uint32_t, size_t> addrToIndex;

        for (size_t i = 0; i < instructions.size(); i++)
        {
            addrToIndex[instructions[i].address] = i;
        }

        std::set<uint32_t> leaders = {function.start}; // Entry point is always a leader

        for (size_t i = 0; i < instructions.size(); i++)
        {
            const auto &inst = instructions[i];

            if (inst.isBranch || inst.isJump)
            {
                size_t fallthroughIndex = i + (inst.hasDelaySlot ? 2 : 1);
                if (fallthroughIndex < instructions.size())
                {
                    leaders.insert(instructions[fallthroughIndex].address);
                }

                if (inst.isBranch)
                {
                    int32_t offset = static_cast<int16_t>(inst.immediate) << 2;
                    uint32_t target = inst.address + 4 + offset;
                    leaders.insert(target);
                }

                // Jump target for J/JAL
                if ((inst.opcode == OPCODE_J || inst.opcode == OPCODE_JAL) && !inst.isCall)
                {
                    uint32_t target = decodeAbsoluteJumpTarget(inst.address, inst.target);
                    leaders.insert(target);
                }
            }
        }

        uint32_t currentLeader = 0;
        CFGNode currentNode;

        for (size_t i = 0; i < instructions.size(); i++)
        {
            const auto &inst = instructions[i];

            if (leaders.contains(inst.address))
            {
                if (currentLeader != 0)
                {
                    currentNode.endAddress = instructions[i - 1].address;
                    cfg[currentLeader] = currentNode;
                }

                currentLeader = inst.address;
                currentNode = CFGNode();
                currentNode.startAddress = currentLeader;
                currentNode.isJumpTarget = true;
                currentNode.instructions.clear();
            }

            currentNode.instructions.push_back(inst);

            if (i == instructions.size() - 1)
            {
                currentNode.endAddress = inst.address;
                cfg[currentLeader] = currentNode;
            }
        }

        for (auto &[addr, node] : cfg)
        {
            if (node.instructions.empty())
            {
                continue;
            }

            const auto &lastInst = node.instructions.back();
            const Instruction *terminator = &lastInst;

            if (node.instructions.size() >= 2)
            {
                const auto &candidate = node.instructions[node.instructions.size() - 2];
                if (candidate.hasDelaySlot &&
                    (candidate.isBranch || candidate.isJump) &&
                    candidate.address + 4 == lastInst.address)
                {
                    terminator = &candidate;
                }
            }

            if (terminator->isBranch)
            {
                int32_t offset = static_cast<int16_t>(terminator->immediate) << 2;
                uint32_t targetAddr = terminator->address + 4 + offset;

                if (cfg.contains(targetAddr))
                {
                    node.successors.push_back(targetAddr);
                    cfg[targetAddr].predecessors.push_back(addr);
                }

                bool likelyBranch = (terminator->opcode == OPCODE_BEQL ||
                                     terminator->opcode == OPCODE_BNEL ||
                                     terminator->opcode == OPCODE_BLEZL ||
                                     terminator->opcode == OPCODE_BGTZL);

                if (!likelyBranch)
                {
                    const uint32_t step = terminator->hasDelaySlot ? 8 : 4;
                    if (terminator->address + step <= function.end)
                    {
                        uint32_t nextAddr = terminator->address + step;

                        for (const auto &[blockAddr, blockNode] : cfg)
                        {
                            if (blockAddr == nextAddr ||
                                (nextAddr > blockAddr && nextAddr <= blockNode.endAddress))
                            {
                                node.successors.push_back(blockAddr);
                                cfg[blockAddr].predecessors.push_back(addr);
                                break;
                            }
                        }
                    }
                }
            }
            else if (terminator->isJump)
            {
                if (terminator->opcode == OPCODE_J || terminator->opcode == OPCODE_JAL)
                {
                    // Direct jump
                    uint32_t targetAddr = decodeAbsoluteJumpTarget(terminator->address, terminator->target);

                    // Only add successor if it's within this function
                    if (targetAddr >= function.start && targetAddr < function.end &&
                        cfg.contains(targetAddr))
                    {
                        node.successors.push_back(targetAddr);
                        cfg[targetAddr].predecessors.push_back(addr);
                    }
                }
                // We don't handle indirect jumps (JR/JALR) statically
            }
            else if (!lastInst.isReturn)
            {
                if (lastInst.address + 4 <= function.end)
                {
                    uint32_t nextAddr = lastInst.address + 4;

                    for (const auto &[blockAddr, blockNode] : cfg)
                    {
                        if (blockAddr == nextAddr)
                        {
                            node.successors.push_back(blockAddr);
                            cfg[blockAddr].predecessors.push_back(addr);
                            break;
                        }
                    }
                }
            }
        }

        return cfg;
    }

    bool ElfAnalyzer::isReliableSymbolNameForHeuristics(const std::string &name)
    {
        return FunctionClassifier::isReliableSymbolName(name);
    }

    bool ElfAnalyzer::isLibraryFunction(const std::string &name) const
    {
        return m_classifier.isLibraryFunction(name);
    }

    bool ElfAnalyzer::isLibrarySymbolNameForHeuristics(const std::string &name) const
    {
        return isLibraryFunction(name);
    }

    bool ElfAnalyzer::hasHardwareIOSignalForHeuristics(const std::vector<Instruction> &instructions)
    {
        return AnalysisPasses::hasHardwareIOSignal(instructions);
    }

    bool ElfAnalyzer::hasLargeComplexMMISignalForHeuristics(const std::vector<Instruction> &instructions,
                                                            size_t largeInstructionThreshold)
    {
        return AnalysisPasses::hasLargeComplexMMISignal(instructions, largeInstructionThreshold);
    }

    bool ElfAnalyzer::hasSelfModifyingSignalForHeuristics(const std::vector<Instruction> &instructions,
                                                          const std::vector<Section> &sections)
    {
        return AnalysisPasses::hasSelfModifyingSignal(instructions, sections);
    }

    void ElfAnalyzer::clearDecodedInstructionCache()
    {
        m_context.clearInstructionCache();
    }

    void ElfAnalyzer::buildFunctionIndex()
    {
        m_context.buildFunctionIndex();
    }

    void ElfAnalyzer::decodeAllFunctionsOnce()
    {
        m_context.instructionCache.clear();
        for (auto &func : m_context.functions)
        {
            if (m_libFunctions.contains(func.name) ||
                func.start >= func.end)
            {
                continue;
            }

            if (func.instructions.empty())
            {
                func.instructions = decodeFunction(func);
            }
        }
    }

    void ElfAnalyzer::classifyFunctions()
    {
        for (auto &func : m_context.functions)
        {
            if (m_libFunctions.contains(func.name))
            {
                continue;
            }

            categorizeFunction(func);
        }
    }

    void ElfAnalyzer::runDataUsagePass()
    {
        analyzeDataUsage();
    }

    void ElfAnalyzer::runPatchDetectionPass()
    {
        identifyPotentialPatches();
    }

    void ElfAnalyzer::runControlFlowPass()
    {
        analyzeControlFlow();
    }

    void ElfAnalyzer::runJumpTablePass()
    {
        detectJumpTables();
    }

    void ElfAnalyzer::runPerformancePass()
    {
        analyzePerformanceCriticalPaths();
    }

    void ElfAnalyzer::runSignaturePass() const
    {
        analyzeFunctionSignatures();
    }

    void ElfAnalyzer::printAnalysisSummary() const
    {
        std::cout << "Analysis completed" << std::endl;
        std::cout << "- " << m_libFunctions.size() << " library functions to stub" << std::endl;
        std::cout << "- " << m_untrackedStubFunctions.size() << " detected library functions without runtime handlers" << std::endl;
        std::cout << "- skip output retained for compatibility; analyzer does not auto-skip functions" << std::endl;
        std::cout << "- " << m_patches.size() << " potential patches identified" << std::endl;
        std::cout << "- " << m_jumpTables.size() << " jump tables detected" << std::endl;
    }

    const std::vector<Instruction> &ElfAnalyzer::getDecodedInstructions(const Function &function) const
    {
        if (!function.instructions.empty() || function.start >= function.end)
        {
            return function.instructions;
        }

        auto cacheIt = m_context.instructionCache.find(function.start);
        if (cacheIt == m_context.instructionCache.end())
        {
            cacheIt = m_context.instructionCache.emplace(function.start, decodeFunction(function)).first;
        }

        return cacheIt->second;
    }

    std::vector<Instruction> ElfAnalyzer::decodeFunction(const Function &function) const
    {
        std::vector<Instruction> instructions;

        for (uint32_t addr = function.start; addr < function.end; addr += 4)
        {
            uint32_t rawInstruction = 0;
            if (!tryReadWord(m_elfParser.get(), addr, rawInstruction))
            {
                continue;
            }

            try
            {
                Instruction inst = m_decoder->decodeInstruction(addr, rawInstruction);
                instructions.push_back(inst);
            }
            catch (const std::exception &e)
            {
                std::cerr << "Error decoding instruction at " << formatAddress(addr)
                          << ": " << e.what() << std::endl;
            }
        }

        return instructions;
    }

    std::string ElfAnalyzer::formatAddress(uint32_t address) const
    {
        std::stringstream ss;
        ss << "0x" << std::hex << std::setw(8) << std::setfill('0') << address;
        return ss.str();
    }

    bool ElfAnalyzer::hasMMIInstructions(const Function &function) const
    {
        const auto &instructions = getDecodedInstructions(function);

        for (const auto &inst : instructions)
        {
            if (inst.isMMI || inst.opcode == OPCODE_MMI)
            {
                return true;
            }
        }

        return false;
    }

    bool ElfAnalyzer::hasVUInstructions(const Function &function) const
    {
        const auto &instructions = getDecodedInstructions(function);

        for (const auto &inst : instructions)
        {
            if (inst.isVU || inst.opcode == OPCODE_COP2)
            {
                return true;
            }
        }

        return false;
    }

    void ElfAnalyzer::identifyFunctionType(const Function &function) const
    {
        if (FunctionClassifier::hasPs2ApiPrefix(function.name))
        {
            return;
        }

        const auto &instructions = getDecodedInstructions(function);

        const bool hasHardwareIO = hasHardwareIOSignalForHeuristics(instructions);
        const bool hasLargeComplexMMI = hasLargeComplexMMISignalForHeuristics(instructions);

        if (hasHardwareIO)
        {
            std::cout << "Function " << function.name
                      << " has a hardware I/O signal" << std::endl;
        }

        if (hasLargeComplexMMI)
        {
            std::cout << "Function " << function.name
                      << " is large and uses complex MMI" << std::endl;
        }
    }

    void ElfAnalyzer::categorizeFunction(Function &function)
    {
        identifyFunctionType(function);

        if (isSelfModifyingCode(function))
        {
            std::cout << "Function " << function.name << " contains self-modifying code" << std::endl;
        }

        if (isLoopHeavyFunction(function))
        {
            std::cout << "Function " << function.name << " is loop-heavy, may need optimization" << std::endl;
        }
    }

    bool ElfAnalyzer::isSelfModifyingCode(const Function &function) const
    {
        const auto &instructions = getDecodedInstructions(function);
        return hasSelfModifyingSignalForHeuristics(instructions, m_context.sections);
    }

    bool ElfAnalyzer::isLoopHeavyFunction(const Function &function) const
    {
        const auto &instructions = getDecodedInstructions(function);
        int loopCount = 0;

        for (const auto &inst : instructions)
        {
            if (inst.isBranch)
            {
                int32_t offset = static_cast<int16_t>(inst.immediate) << 2;
                if (offset < 0)
                {
                    loopCount++;
                }
            }
        }

        // Consider it loop-heavy if it has more than 5 loops
        return loopCount > 5;
    }

    uint32_t ElfAnalyzer::getSuccessor(const Instruction &inst, uint32_t currentAddr)
    {
        if (inst.isBranch)
        {
            int32_t offset = static_cast<int16_t>(inst.immediate) << 2;
            return currentAddr + 4 + offset;
        }

        if (inst.opcode == OPCODE_J || inst.opcode == OPCODE_JAL)
        {
            return decodeAbsoluteJumpTarget(currentAddr, inst.target);
        }

        return currentAddr + 4;
    }

    static uint32_t decodeAbsoluteJumpTarget(uint32_t instructionAddress, uint32_t targetField)
    {
        return ((instructionAddress + 4) & 0xF0000000u) | (targetField << 2);
    }

    static bool tryReadWord(const ElfParser *parser, uint32_t address, uint32_t &outWord)
    {
        if (parser == nullptr)
        {
            return false;
        }

        if (address > (std::numeric_limits<uint32_t>::max() - 3))
        {
            return false;
        }

        if (!parser->isValidAddress(address) || !parser->isValidAddress(address + 3))
        {
            return false;
        }

        try
        {
            outWord = parser->readWord(address);
            return true;
        }
        catch (const std::exception &)
        {
            return false;
        }
    }

    bool ElfAnalyzer::importGhidraMap(const std::string &csvPath)
    {
        std::ifstream file(csvPath);
        if (!file)
        {
            std::cerr << "Failed to open Ghidra CSV file: " << csvPath << std::endl;
            return false;
        }

        std::string line;
        int lineNum = 0;
        int importedCount = 0;

        while (std::getline(file, line))
        {
            lineNum++;
            
            // Skip header line
            if (lineNum == 1 && line.find("Name") != std::string::npos)
            {
                continue;
            }

            // Parse CSV line: Name,Start,End,Size
            std::stringstream ss(line);
            std::string name, startStr, endStr, sizeStr;

            if (!std::getline(ss, name, ',') ||
                !std::getline(ss, startStr, ',') ||
                !std::getline(ss, endStr, ','))
            {
                continue; // Skip malformed lines
            }

            // Parse hex addresses (e.g., "0x00123456")
            uint32_t startAddr = 0;
            uint32_t endAddr = 0;
            
            try
            {
                startAddr = std::stoul(startStr, nullptr, 16);
                endAddr = std::stoul(endStr, nullptr, 16);
            }
            catch (...)
            {
                continue; // Skip lines with invalid addresses
            }

            // Update existing function or create new one
            bool found = false;
            for (auto &func : m_context.functions)
            {
                if (func.start == startAddr)
                {
                    // Update with Ghidra's more accurate boundaries
                    func.name = name;
                    func.end = endAddr;
                    found = true;
                    importedCount++;
                    break;
                }
            }

            // If not found, create new function from Ghidra data
            if (!found)
            {
                Function newFunc;
                newFunc.name = name;
                newFunc.start = startAddr;
                newFunc.end = endAddr;
                m_context.functions.push_back(newFunc);
                importedCount++;
            }
        }

        clearDecodedInstructionCache();
        std::sort(m_context.functions.begin(), m_context.functions.end(),
                  [](const Function &a, const Function &b)
                  {
                      return a.start < b.start;
                  });
        buildFunctionIndex();

        std::cout << "Imported " << importedCount << " functions from Ghidra CSV: " << csvPath << std::endl;
        return true;
    }

    const std::vector<Function>& ElfAnalyzer::getFunctions() const
    {
        return m_context.functions;
    }
}
