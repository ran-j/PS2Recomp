#include "ps2recomp/ps2_recompiler.h"
#include "ps2recomp/instructions.h"
#include "ps2recomp/types.h"
#include "ps2recomp/elf_parser.h"
#include "ps2recomp/r5900_decoder.h"
#include "ps2recomp/control_flow_utils.h"
#include "ps2_runtime_calls.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <stdexcept>
#include <filesystem>
#include <cctype>
#include <condition_variable>
#include <exception>
#include <mutex>
#include <queue>
#include <unordered_set>
#include <optional>
#include <limits>
#include <functional>
#include <thread>

namespace fs = std::filesystem;

namespace ps2recomp
{
    namespace
    {
        uint32_t decodeAbsoluteJumpTarget(uint32_t address, uint32_t target)
        {
            return ((address + 4) & 0xF0000000u) | (target << 2);
        }

        bool isReservedCxxIdentifier(const std::string &name)
        {
            if (name.size() >= 2 && name[0] == '_' && name[1] == '_')
            {
                return true;
            }
            if (name.size() >= 2 && name[0] == '_' && std::isupper(static_cast<unsigned char>(name[1])))
            {
                return true;
            }
            return false;
        }

        std::string sanitizeIdentifierBody(const std::string &name)
        {
            std::string sanitized;
            sanitized.reserve(name.size() + 1);

            for (char c : name)
            {
                const unsigned char uc = static_cast<unsigned char>(c);
                if (std::isalnum(uc) || c == '_')
                {
                    sanitized.push_back(c);
                }
                else
                {
                    sanitized.push_back('_');
                }
            }

            if (sanitized.empty())
            {
                return sanitized;
            }

            const unsigned char first = static_cast<unsigned char>(sanitized.front());
            if (!(std::isalpha(first) || sanitized.front() == '_'))
            {
                sanitized.insert(sanitized.begin(), '_');
            }

            return sanitized;
        }

        bool shouldGenerateCodeForFunction(const Function &function)
        {
            return function.isRecompiled || function.isStub || function.isSkipped;
        }

        size_t resolveOutputWorkerCount(uint32_t configuredWorkerCount)
        {
            if (configuredWorkerCount > 0)
            {
                return configuredWorkerCount;
            }

            const unsigned int hardwareWorkers = std::thread::hardware_concurrency();
            if (hardwareWorkers >= 2)
            {
                return static_cast<size_t>(hardwareWorkers - 1);
            }

            return 1;
        }

        void writeCombinedOutputPreamble(std::ostream &output)
        {
            output << "#include <stdexcept>\n";
            output << "#include \"ps2_recompiled_functions.h\"\n\n";
            output << "#include \"ps2_runtime_macros.h\"\n";
            output << "#include \"ps2_runtime.h\"\n";
            output << "#include \"ps2_recompiled_stubs.h\"\n";
            output << "#include \"ps2_syscalls.h\"\n";
            output << "#include \"ps2_stubs.h\"\n";
            output << "#ifdef _DEBUG\n";
            output << "#include \"ps2_log.h\"\n";
            output << "#endif\n";
            output << "\n";
        }

        enum class PatchClass
        {
            Generic,
            Syscall,
            Cop0,
            Cache
        };

        PatchClass classifyPatchedInstruction(uint32_t rawInstruction)
        {
            const uint32_t opcode = OPCODE(rawInstruction);
            if (opcode == OPCODE_SPECIAL && FUNCTION(rawInstruction) == SPECIAL_SYSCALL)
            {
                return PatchClass::Syscall;
            }
            if (opcode == OPCODE_COP0)
            {
                return PatchClass::Cop0;
            }
            if (opcode == OPCODE_CACHE)
            {
                return PatchClass::Cache;
            }
            return PatchClass::Generic;
        }

        bool shouldApplyConfiguredPatch(PatchClass patchClass, const RecompilerConfig &config)
        {
            switch (patchClass)
            {
            case PatchClass::Syscall:
                return config.patchSyscalls;
            case PatchClass::Cop0:
                return config.patchCop0;
            case PatchClass::Cache:
                return config.patchCache;
            default:
                return true;
            }
        }

        std::string escapeCStringLiteral(const std::string &value)
        {
            std::string escaped;
            escaped.reserve(value.size());
            for (char c : value)
            {
                switch (c)
                {
                case '\\':
                    escaped += "\\\\";
                    break;
                case '"':
                    escaped += "\\\"";
                    break;
                case '\n':
                    escaped += "\\n";
                    break;
                case '\r':
                    escaped += "\\r";
                    break;
                case '\t':
                    escaped += "\\t";
                    break;
                default:
                    escaped.push_back(c);
                    break;
                }
            }
            return escaped;
        }

        std::string trimAsciiWhitespace(const std::string &value)
        {
            const auto first = std::find_if_not(value.begin(), value.end(),
                                                [](unsigned char c)
                                                { return std::isspace(c) != 0; });
            if (first == value.end())
            {
                return {};
            }

            const auto last = std::find_if_not(value.rbegin(), value.rend(),
                                               [](unsigned char c)
                                               { return std::isspace(c) != 0; })
                                  .base();
            return std::string(first, last);
        }

        bool tryParseU32AddressLiteral(const std::string &literal, uint32_t &outAddress)
        {
            if (literal.empty())
            {
                return false;
            }

            try
            {
                size_t parsedCount = 0;
                const unsigned long parsed = std::stoul(literal, &parsedCount, 0);
                if (parsedCount != literal.size() || parsed > std::numeric_limits<uint32_t>::max())
                {
                    return false;
                }

                outAddress = static_cast<uint32_t>(parsed);
                return true;
            }
            catch (...)
            {
                return false;
            }
        }

        struct FunctionSelector
        {
            std::string name;
            std::optional<uint32_t> start;
        };

        FunctionSelector parseFunctionSelector(const std::string &rawSelector)
        {
            FunctionSelector selector{};
            const std::string trimmed = trimAsciiWhitespace(rawSelector);
            if (trimmed.empty())
            {
                return selector;
            }

            const std::size_t at = trimmed.rfind('@');
            if (at != std::string::npos)
            {
                selector.name = trimAsciiWhitespace(trimmed.substr(0, at));

                uint32_t parsedAddress = 0;
                const std::string addressLiteral = trimAsciiWhitespace(trimmed.substr(at + 1));
                if (tryParseU32AddressLiteral(addressLiteral, parsedAddress))
                {
                    selector.start = parsedAddress;
                    return selector;
                }

                // for now backward compatibility
                selector.name = trimmed;
                return selector;
            }

            uint32_t parsedAddress = 0;
            if (tryParseU32AddressLiteral(trimmed, parsedAddress))
            {
                selector.start = parsedAddress;
                return selector;
            }

            selector.name = trimmed;
            return selector;
        }

        struct EntryDiscoveryStats
        {
            size_t discoveredCount = 0;
            size_t passCount = 0;
        };

        bool isEntryFunctionName(const std::string &name)
        {
            return name.rfind("entry_", 0) == 0;
        }

        EntryDiscoveryStats discoverAdditionalEntryPointsImpl(
            std::vector<Function> &functions,
            std::unordered_map<uint32_t, std::vector<Instruction>> &decodedFunctions,
            const std::vector<Section> &sections,
            CodeGenerator *codeGenerator,
            const std::function<bool(Function &)> &decodeExternalFunction)
        {
            std::unordered_set<uint32_t> existingStarts;
            for (const auto &function : functions)
            {
                existingStarts.insert(function.start);
            }

            auto isExecutableAddress = [&](uint32_t address) -> bool
            {
                for (const auto &section : sections)
                {
                    if (!section.isCode)
                    {
                        continue;
                    }
                    if (address >= section.address && address < (section.address + section.size))
                    {
                        return true;
                    }
                }
                return false;
            };

            auto isSimpleReturnThunkStart = [](const Instruction &inst) -> bool
            {
                return inst.opcode == OPCODE_SPECIAL &&
                       inst.function == SPECIAL_JR &&
                       inst.rs == 31;
            };

            auto findContainingFunction = [&](uint32_t address) -> const Function *
            {
                const Function *best = nullptr;
                for (const auto &function : functions)
                {
                    if (address < function.start || address >= function.end)
                    {
                        continue;
                    }

                    if (!function.isRecompiled || function.isStub || function.isSkipped)
                    {
                        continue;
                    }

                    auto decodedIt = decodedFunctions.find(function.start);
                    if (decodedIt == decodedFunctions.end())
                    {
                        continue;
                    }

                    const auto &decoded = decodedIt->second;
                    const bool hasAddress = std::any_of(decoded.begin(), decoded.end(),
                                                        [&](const Instruction &candidate)
                                                        { return candidate.address == address; });
                    if (!hasAddress)
                    {
                        continue;
                    }

                    if (!best || function.start > best->start)
                    {
                        best = &function;
                    }
                }
                return best;
            };

            EntryDiscoveryStats stats{};

            while (true)
            {
                ++stats.passCount;
                struct PendingEntry
                {
                    uint32_t target = 0;
                    std::optional<uint32_t> containingStart;
                    uint32_t containingEnd = 0;
                };

                std::vector<PendingEntry> pendingEntries;
                std::vector<Function> newEntries;
                std::unordered_set<uint32_t> pendingStarts;

                auto queuePendingEntry = [&](uint32_t target)
                {
                    if (!isExecutableAddress(target))
                    {
                        return;
                    }

                    if (existingStarts.contains(target) || pendingStarts.contains(target))
                    {
                        return;
                    }

                    PendingEntry pending{};
                    pending.target = target;
                    if (const Function *containingFunction = findContainingFunction(target))
                    {
                        pending.containingStart = containingFunction->start;
                        pending.containingEnd = containingFunction->end;
                    }

                    pendingEntries.push_back(pending);
                    pendingStarts.insert(target);
                };

                for (const auto &function : functions)
                {
                    if (!function.isRecompiled || function.isStub || function.isSkipped)
                    {
                        continue;
                    }

                    if (isEntryFunctionName(function.name))
                    {
                        continue;
                    }

                    auto decodedIt = decodedFunctions.find(function.start);
                    if (decodedIt == decodedFunctions.end())
                    {
                        continue;
                    }

                    const auto &instructions = decodedIt->second;
                    CodeGenerator::AnalysisResult analysisResult{};
                    if (codeGenerator)
                    {
                        analysisResult = codeGenerator->collectInternalBranchTargets(
                            function, instructions, &functions);
                    }
                    else
                    {
                        analysisResult.resumeEntryPoints.clear();
                        analysisResult.externalEntryPoints.clear();
                    }

                    for (uint32_t target : analysisResult.externalEntryPoints)
                    {
                        queuePendingEntry(target);
                    }

                    for (uint32_t target : analysisResult.resumeEntryPoints)
                    {
                        queuePendingEntry(target);
                    }
                }

                if (pendingEntries.empty())
                {
                    break;
                }

                std::sort(pendingEntries.begin(), pendingEntries.end(),
                          [](const PendingEntry &a, const PendingEntry &b)
                          { return a.target < b.target; });

                std::vector<uint32_t> boundaryStarts;
                boundaryStarts.reserve(existingStarts.size() + pendingStarts.size());
                boundaryStarts.insert(boundaryStarts.end(), existingStarts.begin(), existingStarts.end());
                boundaryStarts.insert(boundaryStarts.end(), pendingStarts.begin(), pendingStarts.end());
                std::sort(boundaryStarts.begin(), boundaryStarts.end());
                boundaryStarts.erase(std::unique(boundaryStarts.begin(), boundaryStarts.end()), boundaryStarts.end());

                auto findNextBoundaryStart = [&](uint32_t address) -> std::optional<uint32_t>
                {
                    auto it = std::upper_bound(boundaryStarts.begin(), boundaryStarts.end(), address);
                    if (it == boundaryStarts.end())
                    {
                        return std::nullopt;
                    }
                    return *it;
                };

                std::unordered_set<uint32_t> successfulStarts;

                for (const auto &pending : pendingEntries)
                {
                    const uint32_t target = pending.target;

                    Function entryFunction;
                    std::stringstream name;
                    name << "entry_" << std::hex << target;
                    entryFunction.name = name.str();
                    entryFunction.start = target;
                    entryFunction.isStub = false;
                    entryFunction.isSkipped = false;
                    entryFunction.isRecompiled = true;

                    if (pending.containingStart.has_value())
                    {
                        auto containingDecodedIt = decodedFunctions.find(*pending.containingStart);
                        if (containingDecodedIt == decodedFunctions.end())
                        {
                            continue;
                        }

                        const auto &containingInstructions = containingDecodedIt->second;
                        auto sliceIt = std::find_if(containingInstructions.begin(), containingInstructions.end(),
                                                    [&](const Instruction &candidate)
                                                    { return candidate.address == target; });

                        if (sliceIt == containingInstructions.end())
                        {
                            continue;
                        }

                        uint32_t sliceEndAddress = pending.containingEnd;
                        auto nextStartOpt = findNextBoundaryStart(target);
                        if (nextStartOpt.has_value() && nextStartOpt.value() < sliceEndAddress)
                        {
                            sliceEndAddress = nextStartOpt.value();
                        }

                        if (isSimpleReturnThunkStart(*sliceIt) &&
                            target <= (std::numeric_limits<uint32_t>::max() - 8u))
                        {
                            const uint32_t returnThunkEnd = target + 8u;
                            if (returnThunkEnd < sliceEndAddress)
                            {
                                sliceEndAddress = returnThunkEnd;
                            }
                        }

                        if (sliceEndAddress <= target)
                        {
                            continue;
                        }

                        auto sliceEndIt = std::find_if(sliceIt, containingInstructions.end(),
                                                       [&](const Instruction &candidate)
                                                       { return candidate.address >= sliceEndAddress; });
                        if (sliceEndIt == sliceIt)
                        {
                            continue;
                        }

                        std::vector<Instruction> slicedInstructions(sliceIt, sliceEndIt);
                        decodedFunctions[target] = std::move(slicedInstructions);
                        entryFunction.end = sliceEndAddress;
                    }
                    else
                    {
                        auto nextStartOpt = findNextBoundaryStart(target);
                        if (!nextStartOpt.has_value() || nextStartOpt.value() <= target)
                        {
                            continue;
                        }

                        entryFunction.end = nextStartOpt.value();
                        if (!decodeExternalFunction(entryFunction))
                        {
                            continue;
                        }
                    }

                    newEntries.push_back(entryFunction);
                    successfulStarts.insert(target);
                }

                if (newEntries.empty())
                {
                    break;
                }

                stats.discoveredCount += newEntries.size();
                functions.insert(functions.end(), newEntries.begin(), newEntries.end());
                existingStarts.insert(successfulStarts.begin(), successfulStarts.end());
                std::sort(functions.begin(), functions.end(),
                          [](const Function &a, const Function &b)
                          { return a.start < b.start; });
            }

            return stats;
        }
        size_t resliceEntryFunctionsImpl(
            std::vector<Function> &functions,
            std::unordered_map<uint32_t, std::vector<Instruction>> &decodedFunctions)
        {
            std::vector<uint32_t> boundaryStarts;
            boundaryStarts.reserve(functions.size());
            for (const auto &function : functions)
            {
                if (!function.isRecompiled || function.isStub || function.isSkipped)
                {
                    continue;
                }
                boundaryStarts.push_back(function.start);
            }

            std::sort(boundaryStarts.begin(), boundaryStarts.end());
            boundaryStarts.erase(std::unique(boundaryStarts.begin(), boundaryStarts.end()), boundaryStarts.end());

            auto findContainingFunction = [&](uint32_t address) -> const Function *
            {
                const Function *best = nullptr;
                for (const auto &function : functions)
                {
                    if (!function.isRecompiled || function.isStub || function.isSkipped)
                    {
                        continue;
                    }

                    if (isEntryFunctionName(function.name))
                    {
                        continue;
                    }

                    if (address < function.start || address >= function.end)
                    {
                        continue;
                    }

                    auto decodedIt = decodedFunctions.find(function.start);
                    if (decodedIt == decodedFunctions.end())
                    {
                        continue;
                    }

                    const auto &decoded = decodedIt->second;
                    const bool hasAddress = std::any_of(decoded.begin(), decoded.end(),
                                                        [&](const Instruction &candidate)
                                                        { return candidate.address == address; });
                    if (!hasAddress)
                    {
                        continue;
                    }

                    if (!best || function.start > best->start)
                    {
                        best = &function;
                    }
                }
                return best;
            };

            size_t reslicedCount = 0;

            for (auto &function : functions)
            {
                if (!function.isRecompiled || function.isStub || function.isSkipped)
                {
                    continue;
                }

                if (!isEntryFunctionName(function.name))
                {
                    continue;
                }

                const Function *containingFunction = findContainingFunction(function.start);
                uint32_t sliceEndAddress = containingFunction ? containingFunction->end : function.end;
                auto nextIt = std::upper_bound(boundaryStarts.begin(), boundaryStarts.end(), function.start);
                if (nextIt != boundaryStarts.end() && *nextIt < sliceEndAddress)
                {
                    sliceEndAddress = *nextIt;
                }

                if (sliceEndAddress <= function.start)
                {
                    continue;
                }

                const std::vector<Instruction> *sourceInstructions = nullptr;
                if (containingFunction)
                {
                    auto containingDecodedIt = decodedFunctions.find(containingFunction->start);
                    if (containingDecodedIt == decodedFunctions.end())
                    {
                        continue;
                    }
                    sourceInstructions = &containingDecodedIt->second;
                }
                else
                {
                    auto entryDecodedIt = decodedFunctions.find(function.start);
                    if (entryDecodedIt == decodedFunctions.end())
                    {
                        continue;
                    }
                    sourceInstructions = &entryDecodedIt->second;
                }

                auto sliceBeginIt = std::find_if(sourceInstructions->begin(), sourceInstructions->end(),
                                                 [&](const Instruction &candidate)
                                                 { return candidate.address == function.start; });
                if (sliceBeginIt == sourceInstructions->end())
                {
                    continue;
                }

                auto sliceEndIt = std::find_if(sliceBeginIt, sourceInstructions->end(),
                                               [&](const Instruction &candidate)
                                               { return candidate.address >= sliceEndAddress; });
                if (sliceEndIt == sliceBeginIt)
                {
                    continue;
                }

                std::vector<Instruction> slicedInstructions(sliceBeginIt, sliceEndIt);
                bool changed = (function.end != sliceEndAddress);
                auto existingIt = decodedFunctions.find(function.start);
                if (existingIt == decodedFunctions.end())
                {
                    changed = true;
                }
                else if (!changed)
                {
                    const auto &existing = existingIt->second;
                    if (existing.size() != slicedInstructions.size())
                    {
                        changed = true;
                    }
                    else if (!existing.empty())
                    {
                        if (existing.front().address != slicedInstructions.front().address ||
                            existing.back().address != slicedInstructions.back().address)
                        {
                            changed = true;
                        }
                    }
                }

                function.end = sliceEndAddress;
                decodedFunctions[function.start] = std::move(slicedInstructions);

                if (changed)
                {
                    ++reslicedCount;
                }
            }

            return reslicedCount;
        }
    }

    PS2Recompiler::PS2Recompiler(const std::string &configPath)
        : m_configManager(configPath)
    {
        m_configManager.setReporter(&m_reporter);
    }

    PS2Recompiler::~PS2Recompiler() = default;

    void PS2Recompiler::printReport() const
    {
        m_reporter.printSummary(std::cout);
    }

    bool PS2Recompiler::initialize()
    {
        try
        {
            m_reporter.progress("parsing config");
            m_config = m_configManager.loadConfig();
            m_skipFunctions.clear();
            m_skipFunctionStarts.clear();
            m_stubFunctions.clear();
            m_stubFunctionStarts.clear();
            m_stubHandlerBindingsByStart.clear();

            for (const auto &name : m_config.skipFunctions)
            {
                const FunctionSelector selector = parseFunctionSelector(name);
                if (!selector.name.empty())
                {
                    m_skipFunctions[selector.name] = true;
                }
                if (selector.start.has_value())
                {
                    m_skipFunctionStarts.insert(*selector.start);
                }
            }
            for (const auto &name : m_config.stubImplementations)
            {
                const FunctionSelector selector = parseFunctionSelector(name);
                if (!selector.name.empty())
                {
                    m_stubFunctions.insert(selector.name);
                }
                if (selector.start.has_value())
                {
                    m_stubFunctionStarts.insert(*selector.start);
                    if (!selector.name.empty())
                    {
                        auto bindingIt = m_stubHandlerBindingsByStart.find(*selector.start);
                        if (bindingIt != m_stubHandlerBindingsByStart.end() &&
                            bindingIt->second != selector.name)
                        {
                            std::ostringstream msg;
                            msg << "Multiple stub handler bindings for 0x"
                                << std::hex << *selector.start
                                << " (keeping latest '" << selector.name
                                << "', previous '" << bindingIt->second << "')";
                            m_reporter.warning("config", msg.str());
                        }
                        m_stubHandlerBindingsByStart[*selector.start] = selector.name;
                    }
                }
            }

            m_reporter.progress("parsing ELF");
            m_elfParser = std::make_unique<ElfParser>(m_config.inputPath);
            m_elfParser->setReporter(&m_reporter);
            if (!m_elfParser->parse())
            {
                m_reporter.error("elf", "Failed to parse ELF file: " + m_config.inputPath);
                return false;
            }

            if (!m_config.ghidraMapPath.empty())
            {
                m_elfParser->loadGhidraFunctionMap(m_config.ghidraMapPath);
            }

            m_functions = m_elfParser->extractFunctions();
            m_symbols = m_elfParser->extractSymbols();
            m_sections = m_elfParser->getSections();
            m_relocations = m_elfParser->getRelocations();

            if (m_functions.empty())
            {
                m_reporter.error("elf", "No functions found in ELF file.");
                return false;
            }

            {
                m_bootstrapInfo = {};
                uint32_t entry = m_elfParser->getEntryPoint();
                {
                    std::ostringstream msg;
                    msg << "ELF entry point: 0x" << std::hex << entry;
                    m_reporter.info("elf", msg.str());
                }
                uint32_t bssStart = std::numeric_limits<uint32_t>::max();
                uint32_t bssEnd = 0;
                for (const auto &sec : m_sections)
                {
                    if (sec.isBSS && sec.size > 0)
                    {
                        bssStart = std::min(bssStart, sec.address);
                        bssEnd = std::max(bssEnd, sec.address + sec.size);
                    }
                }

                uint32_t gp = 0;
                for (const auto &sym : m_symbols)
                {
                    if (sym.name == "_gp")
                    {
                        gp = sym.address;
                        break;
                    }
                }

                if (bssStart != std::numeric_limits<uint32_t>::max())
                {
                    std::ostringstream msg;
                    msg << "BSS range: 0x" << std::hex << bssStart << " - 0x" << bssEnd
                        << " (size 0x" << (bssEnd - bssStart) << "), gp=0x" << gp;
                    m_reporter.info("elf", msg.str());
                }
                else
                {
                    std::ostringstream msg;
                    msg << "No BSS found, gp=0x" << std::hex << gp;
                    m_reporter.info("elf", msg.str());
                }

                if (entry != 0)
                {
                    m_bootstrapInfo.valid = true;
                    m_bootstrapInfo.entry = entry;
                    if (bssStart != std::numeric_limits<uint32_t>::max() && bssEnd > bssStart)
                    {
                        m_bootstrapInfo.bssStart = bssStart;
                        m_bootstrapInfo.bssEnd = bssEnd;
                    }
                    else
                    {
                        m_bootstrapInfo.bssStart = 0;
                        m_bootstrapInfo.bssEnd = 0;
                    }
                    m_bootstrapInfo.gp = gp;
                }
            }

            m_reporter.recordDiscovered(m_functions.size(), m_symbols.size(), m_sections.size(), m_relocations.size());
            {
                std::ostringstream msg;
                msg << "extracted " << m_functions.size() << " functions, "
                    << m_symbols.size() << " symbols, "
                    << m_sections.size() << " sections, "
                    << m_relocations.size() << " relocations";
                m_reporter.progress(msg.str());
            }

            m_decoder = std::make_unique<R5900Decoder>();
            m_codeGenerator = std::make_unique<CodeGenerator>(m_symbols, m_sections);
            m_codeGenerator->setReporter(&m_reporter);
            std::unordered_map<uint32_t, std::string> relocationCallNames;
            relocationCallNames.reserve(m_relocations.size());
            for (const auto &reloc : m_relocations)
            {
                if (reloc.symbolName.empty())
                {
                    continue;
                }

                auto inserted = relocationCallNames.emplace(reloc.offset, reloc.symbolName);
                if (!inserted.second && inserted.first->second != reloc.symbolName)
                {
                    std::ostringstream msg;
                    msg << "multiple relocation symbols at 0x" << std::hex << reloc.offset
                        << " (keeping '" << inserted.first->second
                        << "', ignoring '" << reloc.symbolName << "')";
                    m_reporter.warning("relocation", msg.str());
                }
            }
            m_codeGenerator->setRelocationCallNames(relocationCallNames);
            m_codeGenerator->setBootstrapInfo(m_bootstrapInfo);
            m_codeGenerator->setConfiguredJumpTables(m_config.jumpTables);
            m_codeGenerator->setEmitInstructionComments(true);

            fs::create_directories(m_config.outputPath);

            return true;
        }
        catch (const std::exception &e)
        {
            m_reporter.error("initialize", e.what());
            return false;
        }
    }

    bool PS2Recompiler::recompile()
    {
        try
        {
            {
                std::ostringstream msg;
                msg << "recompiling " << m_functions.size() << " functions";
                m_reporter.progress(msg.str());
            }

            size_t processedCount = 0;
            size_t failedCount = 0;
            for (auto &function : m_functions)
            {
                m_reporter.recordFunctionProcessed();

                if (isStubFunction(function))
                {
                    function.isStub = true;
                    function.isSkipped = false;
                    m_reporter.recordFunctionStubbed();
                    continue;
                }

                if (shouldSkipFunction(function))
                {
                    function.isSkipped = true;
                    function.isStub = false;
                    m_reporter.recordFunctionSkipped();
                    continue;
                }

                if (!decodeFunction(function))
                {
                    ++failedCount;
                    m_reporter.recordDecodeFailure();
                    m_reporter.recordFunctionSkipped();
                    m_reporter.warningAt("decode", function.name, function.start, "Skipping function due decode failure");
                    function.isSkipped = true;
                    continue;
                }

                function.isRecompiled = true;
                m_reporter.recordFunctionRecompiled();
#if _DEBUG
                processedCount++;
                if (processedCount % 100 == 0)
                {
                    {
                        std::ostringstream msg;
                        msg << "processed " << processedCount << " functions";
                        m_reporter.progress(msg.str());
                    }
                }
#endif
            }

            loadExternalCallTargetManifests();
            discoverAdditionalEntryPoints();

            if (failedCount > 0)
            {
                std::ostringstream msg;
                msg << "Recompile completed with " << failedCount << " function(s) skipped due decode issues.";
                m_reporter.warning("decode", msg.str());
            }

            m_reporter.progress("recompilation pass completed");
            return true;
        }
        catch (const std::exception &e)
        {
            m_reporter.error("recompile", e.what());
            return false;
        }
    }

    void PS2Recompiler::generateOutput()
    {
        try
        {
            m_functionRenames.clear();

            auto makeName = [&](const Function &function) -> std::string
            {
                std::string sanitized = sanitizeFunctionName(function.name);
                if (sanitized.empty())
                {
                    sanitized = "func";
                }

                if (sanitized.rfind("entry_", 0) == 0)
                {
                    std::stringstream expectedStartName;
                    expectedStartName << "entry_" << std::hex << function.start;
                    if (sanitized == expectedStartName.str())
                    {
                        std::stringstream entryName;
                        entryName << sanitized << "_0x" << std::hex << function.end;
                        return entryName.str();
                    }
                }

                std::stringstream ss;
                ss << sanitized << "_0x" << std::hex << function.start;
                return ss.str();
            };

            for (const auto &function : m_functions)
            {
                if (!shouldGenerateCodeForFunction(function))
                    continue;

                m_functionRenames[function.start] = makeName(function);
            }

            if (m_codeGenerator)
            {
                m_codeGenerator->setRenamedFunctions(m_functionRenames);
            }

            if (m_bootstrapInfo.valid && m_codeGenerator)
            {
                auto entryIt = std::find_if(m_functions.begin(), m_functions.end(),
                                            [&](const Function &fn)
                                            { return fn.start == m_bootstrapInfo.entry; });
                if (entryIt != m_functions.end())
                {
                    auto renameIt = m_functionRenames.find(entryIt->start);
                    if (renameIt != m_functionRenames.end())
                    {
                        m_bootstrapInfo.entryName = renameIt->second;
                    }
                    else
                    {
                        m_bootstrapInfo.entryName = sanitizeFunctionName(entryIt->name);
                    }
                }

                m_codeGenerator->setBootstrapInfo(m_bootstrapInfo);
            }

            m_generatedStubs.clear();
            for (const auto &function : m_functions)
            {
                if (function.isStub || function.isSkipped)
                {
                    std::string generatedName = m_codeGenerator->getFunctionName(function.start);
                    std::stringstream stub;
                    stub << "void " << generatedName
                         << "(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {\n";
                    stub << "#ifdef _DEBUG\n";
                    stub << "    PS_LOG_ENTRY(\"" << generatedName << "\");\n";
                    stub << "#endif\n";
                    stub << "    const uint32_t __entryPc = ctx->pc;\n"
                         << "    ";

                    if (function.isSkipped)
                    {
                        stub << "ps2_stubs::TODO_NAMED(\"" << escapeCStringLiteral(function.name) << "\", rdram, ctx, runtime); ";
                    }
                    else
                    {
                        std::string dispatchName = function.name;
                        const auto bindingIt = m_stubHandlerBindingsByStart.find(function.start);
                        if (bindingIt != m_stubHandlerBindingsByStart.end() &&
                            !bindingIt->second.empty())
                        {
                            dispatchName = bindingIt->second;
                        }

                        const std::string_view resolvedSyscallName = ps2_runtime_calls::resolveSyscallName(dispatchName);
                        const std::string_view resolvedStubName = ps2_runtime_calls::resolveStubName(dispatchName);
                        if (!resolvedSyscallName.empty())
                        {
                            stub << "ps2_syscalls::" << resolvedSyscallName << "(rdram, ctx, runtime); ";
                        }
                        else if (!resolvedStubName.empty())
                        {
                            stub << "ps2_stubs::" << resolvedStubName << "(rdram, ctx, runtime); ";
                        }
                        else
                        {
                            if (dispatchName.empty())
                            {
                                dispatchName = function.name;
                            }
                            stub << "ps2_stubs::TODO_NAMED(\"" << escapeCStringLiteral(dispatchName) << "\", rdram, ctx, runtime); ";
                        }
                    }

                    stub << "\n"
                         << "    if (ctx->pc == __entryPc)\n"
                         << "    {\n"
                         << "        ctx->pc = getRegU32(ctx, 31);\n"
                         << "    }\n"
                         << "}";
                    m_generatedStubs[function.start] = stub.str();
                }
            }

            if (!generateFunctionHeader())
            {
                throw std::runtime_error("Failed to generate function header");
            }

            std::vector<const Function *> outputFunctions;
            outputFunctions.reserve(m_functions.size());
            for (const auto &function : m_functions)
            {
                if (shouldGenerateCodeForFunction(function))
                {
                    outputFunctions.push_back(&function);
                }
            }

            m_reporter.recordGeneratedFunctions(outputFunctions.size());

            const size_t outputWorkerCount = m_config.lowMemoryMode ? 1 : resolveOutputWorkerCount(m_config.outputWorkerThreads);
            if (outputFunctions.size() > 1 && outputWorkerCount > 1)
            {
                {
                    std::ostringstream msg;
                    msg << "generating function output with " << outputWorkerCount << " worker(s)";
                    m_reporter.progress(msg.str());
                }
            }

            const auto &generatedStubs = m_generatedStubs;
            const auto &decodedFunctions = m_decodedFunctions;

            auto generateFunctionCode = [&](CodeGenerator &codeGenerator, const Function &function, bool useHeaders) -> std::string
            {
                try
                {
                    if (function.isStub || function.isSkipped)
                    {
                        if (!useHeaders)
                        {
                            return generatedStubs.at(function.start);
                        }

                        std::stringstream stubFile;
                        stubFile << "#include \"ps2_runtime.h\"\n";
                        stubFile << "#include \"ps2_syscalls.h\"\n";
                        stubFile << "#include \"ps2_stubs.h\"\n";
                        stubFile << "#ifdef _DEBUG\n";
                        stubFile << "#include \"ps2_log.h\"\n";
                        stubFile << "#endif\n";
                        stubFile << "\n";
                        stubFile << generatedStubs.at(function.start) << "\n";
                        return stubFile.str();
                    }

                    const auto &instructions = decodedFunctions.at(function.start);
                    return codeGenerator.generateFunction(function, instructions, useHeaders);
                }
                catch (const std::exception &e)
                {
                    {
                        std::ostringstream msg;
                        msg << "Error generating code: " << e.what();
                        m_reporter.errorAt("codegen", function.name, function.start, msg.str());
                    }
                    throw;
                }
            };

            if (m_config.singleFileOutput)
            {
                fs::path outputPath = fs::path(m_config.outputPath) / "ps2_recompiled_functions.cpp";
                std::ofstream combinedOutput(outputPath);
                if (!combinedOutput)
                {
                    throw std::runtime_error("Failed to open combined output: " + outputPath.string());
                }

                writeCombinedOutputPreamble(combinedOutput);

                if (outputWorkerCount <= 1)
                {
                    for (const Function *function : outputFunctions)
                    {
                        combinedOutput << generateFunctionCode(*m_codeGenerator, *function, false) << "\n\n";
                    }
                }
                else
                {
                    struct CompletedCode
                    {
                        size_t outputIndex = 0;
                        std::string code;
                    };

                    std::mutex outputMutex;
                    std::condition_variable workAvailable;
                    std::condition_variable resultAvailable;
                    std::queue<size_t> pendingWork;
                    std::queue<CompletedCode> readyCode;
                    std::unordered_map<size_t, std::string> completedCode;
                    std::exception_ptr workerException;
                    bool stopWorkers = false;
                    size_t outstandingWork = 0;
                    size_t nextFunction = 0;
                    size_t nextOutputIndex = 0;
                    const size_t maxBufferedOutput = std::max<size_t>(outputWorkerCount * 2, outputWorkerCount + 1);

                    auto workerMain = [&]()
                    {
                        CodeGenerator generator(*m_codeGenerator);
                        while (true)
                        {
                            size_t outputIndex = 0;
                            {
                                std::unique_lock<std::mutex> lock(outputMutex);
                                workAvailable.wait(lock, [&]()
                                                   { return stopWorkers || !pendingWork.empty(); });
                                if (stopWorkers)
                                {
                                    return;
                                }

                                outputIndex = pendingWork.front();
                                pendingWork.pop();
                            }

                            try
                            {
                                std::string code = generateFunctionCode(generator, *outputFunctions[outputIndex], false);
                                {
                                    std::lock_guard<std::mutex> lock(outputMutex);
                                    readyCode.push(CompletedCode{outputIndex, std::move(code)});
                                    --outstandingWork;
                                }
                                resultAvailable.notify_one();
                            }
                            catch (...)
                            {
                                {
                                    std::lock_guard<std::mutex> lock(outputMutex);
                                    if (!workerException)
                                    {
                                        workerException = std::current_exception();
                                    }
                                    --outstandingWork;
                                    stopWorkers = true;
                                }
                                resultAvailable.notify_one();
                                workAvailable.notify_all();
                                return;
                            }
                        }
                    };

                    std::vector<std::thread> workers;
                    workers.reserve(outputWorkerCount);
                    for (size_t i = 0; i < outputWorkerCount; ++i)
                    {
                        workers.emplace_back(workerMain);
                    }

                    auto stopAndJoinWorkers = [&]()
                    {
                        {
                            std::lock_guard<std::mutex> lock(outputMutex);
                            stopWorkers = true;
                        }
                        workAvailable.notify_all();
                        for (std::thread &worker : workers)
                        {
                            if (worker.joinable())
                            {
                                worker.join();
                            }
                        }
                    };

                    auto scheduleAvailableWork = [&]()
                    {
                        bool scheduledAny = false;
                        {
                            std::lock_guard<std::mutex> lock(outputMutex);
                            while (nextFunction < outputFunctions.size() &&
                                   outstandingWork + completedCode.size() < maxBufferedOutput &&
                                   !stopWorkers)
                            {
                                pendingWork.push(nextFunction++);
                                ++outstandingWork;
                                scheduledAny = true;
                            }
                        }

                        if (scheduledAny)
                        {
                            workAvailable.notify_all();
                        }
                    };

                    auto flushCompletedOutput = [&]()
                    {
                        while (true)
                        {
                            auto completedIt = completedCode.find(nextOutputIndex);
                            if (completedIt == completedCode.end())
                            {
                                break;
                            }

                            combinedOutput << completedIt->second << "\n\n";
                            completedCode.erase(completedIt);
                            ++nextOutputIndex;

                            if (!combinedOutput)
                            {
                                throw std::runtime_error("Failed while writing combined output: " + outputPath.string());
                            }
                        }
                    };

                    try
                    {
                        scheduleAvailableWork();

                        while (nextOutputIndex < outputFunctions.size())
                        {
                            std::unique_lock<std::mutex> lock(outputMutex);
                            resultAvailable.wait(lock, [&]()
                                                 { return workerException || !readyCode.empty() || (outstandingWork == 0 && nextFunction >= outputFunctions.size()); });

                            if (workerException)
                            {
                                lock.unlock();
                                stopAndJoinWorkers();
                                std::rethrow_exception(workerException);
                            }

                            while (!readyCode.empty())
                            {
                                CompletedCode completed = std::move(readyCode.front());
                                readyCode.pop();
                                completedCode.emplace(completed.outputIndex, std::move(completed.code));
                            }
                            lock.unlock();

                            flushCompletedOutput();
                            scheduleAvailableWork();

                            if (nextOutputIndex >= outputFunctions.size())
                            {
                                break;
                            }

                            std::lock_guard<std::mutex> finalLock(outputMutex);
                            if (outstandingWork == 0 && nextFunction >= outputFunctions.size() && completedCode.empty())
                            {
                                throw std::runtime_error("Internal error: combined output completion queue is missing index " + std::to_string(nextOutputIndex));
                            }
                        }

                        stopAndJoinWorkers();
                    }
                    catch (...)
                    {
                        stopAndJoinWorkers();
                        throw;
                    }
                }

                combinedOutput.close();
                if (!combinedOutput)
                {
                    throw std::runtime_error("Failed to finish combined output: " + outputPath.string());
                }

                {
                    std::ostringstream msg;
                    msg << "wrote combined output to " << outputPath;
                    m_reporter.progress(msg.str());
                }
            }
            else
            {
                struct GeneratedFile
                {
                    fs::path outputPath;
                    std::string code;
                };

                std::vector<fs::path> outputPaths;
                outputPaths.reserve(outputFunctions.size());
                for (const Function *function : outputFunctions)
                {
                    outputPaths.push_back(getOutputPath(*function));
                }

                auto generateFile = [&](CodeGenerator &codeGenerator, size_t outputIndex) -> GeneratedFile
                {
                    return GeneratedFile{outputPaths[outputIndex], generateFunctionCode(codeGenerator, *outputFunctions[outputIndex], true)};
                };

                auto writeGeneratedFile = [&](GeneratedFile generated)
                {
                    fs::create_directories(generated.outputPath.parent_path());
                    if (!writeToFile(generated.outputPath.string(), generated.code))
                    {
                        throw std::runtime_error("Failed to write function output: " + generated.outputPath.string());
                    }
                };

                if (outputWorkerCount <= 1)
                {
                    for (size_t outputIndex = 0; outputIndex < outputFunctions.size(); ++outputIndex)
                    {
                        writeGeneratedFile(generateFile(*m_codeGenerator, outputIndex));
                    }
                }
                else
                {
                    std::mutex outputMutex;
                    std::condition_variable workAvailable;
                    std::condition_variable resultAvailable;
                    std::queue<size_t> pendingWork;
                    std::queue<GeneratedFile> readyFiles;
                    std::exception_ptr workerException;
                    bool stopWorkers = false;
                    size_t outstandingWork = 0;
                    size_t nextFunction = 0;
                    const size_t maxBufferedOutput = std::max<size_t>(outputWorkerCount * 2, outputWorkerCount + 1);

                    auto workerMain = [&]()
                    {
                        CodeGenerator generator(*m_codeGenerator);
                        while (true)
                        {
                            size_t outputIndex = 0;
                            {
                                std::unique_lock<std::mutex> lock(outputMutex);
                                workAvailable.wait(lock, [&]()
                                                   { return stopWorkers || !pendingWork.empty(); });
                                if (stopWorkers)
                                {
                                    return;
                                }

                                outputIndex = pendingWork.front();
                                pendingWork.pop();
                            }

                            try
                            {
                                GeneratedFile generated = generateFile(generator, outputIndex);
                                {
                                    std::lock_guard<std::mutex> lock(outputMutex);
                                    readyFiles.push(std::move(generated));
                                    --outstandingWork;
                                }
                                resultAvailable.notify_one();
                            }
                            catch (...)
                            {
                                {
                                    std::lock_guard<std::mutex> lock(outputMutex);
                                    if (!workerException)
                                    {
                                        workerException = std::current_exception();
                                    }
                                    --outstandingWork;
                                    stopWorkers = true;
                                }
                                resultAvailable.notify_one();
                                workAvailable.notify_all();
                                return;
                            }
                        }
                    };

                    std::vector<std::thread> workers;
                    workers.reserve(outputWorkerCount);
                    for (size_t i = 0; i < outputWorkerCount; ++i)
                    {
                        workers.emplace_back(workerMain);
                    }

                    auto stopAndJoinWorkers = [&]()
                    {
                        {
                            std::lock_guard<std::mutex> lock(outputMutex);
                            stopWorkers = true;
                        }
                        workAvailable.notify_all();
                        for (std::thread &worker : workers)
                        {
                            if (worker.joinable())
                            {
                                worker.join();
                            }
                        }
                    };

                    auto scheduleAvailableWork = [&]()
                    {
                        {
                            std::lock_guard<std::mutex> lock(outputMutex);
                            while (nextFunction < outputFunctions.size() &&
                                   outstandingWork < maxBufferedOutput &&
                                   !stopWorkers)
                            {
                                pendingWork.push(nextFunction++);
                                ++outstandingWork;
                            }
                        }
                        workAvailable.notify_all();
                    };

                    try
                    {
                        scheduleAvailableWork();

                        size_t writtenCount = 0;
                        while (writtenCount < outputFunctions.size())
                        {
                            std::unique_lock<std::mutex> lock(outputMutex);
                            resultAvailable.wait(lock, [&]()
                                                 { return workerException || !readyFiles.empty() || (outstandingWork == 0 && nextFunction >= outputFunctions.size()); });

                            if (workerException)
                            {
                                lock.unlock();
                                stopAndJoinWorkers();
                                std::rethrow_exception(workerException);
                            }

                            if (readyFiles.empty())
                            {
                                break;
                            }

                            GeneratedFile generated = std::move(readyFiles.front());
                            readyFiles.pop();
                            lock.unlock();

                            writeGeneratedFile(std::move(generated));
                            ++writtenCount;
                            scheduleAvailableWork();
                        }

                        stopAndJoinWorkers();
                    }
                    catch (...)
                    {
                        stopAndJoinWorkers();
                        throw;
                    }
                }

                {
                    std::ostringstream msg;
                    msg << "wrote individual function files to " << m_config.outputPath;
                    m_reporter.progress(msg.str());
                }
            }

            m_decodedFunctions.clear();

            std::string registerFunctions = m_codeGenerator->generateFunctionRegistration(m_functions, m_generatedStubs);
            m_generatedStubs.clear();

            fs::path registerPath = fs::path(m_config.outputPath) / "register_functions.cpp";
            if (!writeToFile(registerPath.string(), registerFunctions))
            {
                throw std::runtime_error("Failed to write function registration file: " + registerPath.string());
            }
            {
                std::ostringstream msg;
                msg << "generated function registration file: " << registerPath;
                m_reporter.progress(msg.str());
            }

            if (!generateStubHeader())
            {
                throw std::runtime_error("Failed to generate stub header");
            }
        }
        catch (const std::exception &e)
        {
            m_reporter.error("output", e.what());
            m_reporter.printSummary(std::cout);
            throw;
        }
    }

    bool PS2Recompiler::generateStubHeader()
    {
        try
        {
            std::stringstream ss;

            ss << "#pragma once\n\n";
            ss << "#include <cstdint>\n";
            ss << "#include \"ps2_runtime.h\"\n";
            ss << "#include \"ps2_syscalls.h\"\n\n";
            // ss << "namespace ps2recomp {\n";
            // ss << "namespace stubs {\n\n";

            std::unordered_set<std::string> stubNames;
            for (const auto &function : m_functions)
            {
                if (!function.isStub && !function.isSkipped)
                {
                    continue;
                }

                const std::string generatedName = m_codeGenerator->getFunctionName(function.start);
                if (generatedName.empty())
                {
                    continue;
                }

                if (!stubNames.insert(generatedName).second)
                {
                    continue;
                }

                ss << "void " << generatedName << "(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime);\n";
            }

            // ss << "\n} // namespace stubs\n";
            // ss << "} // namespace ps2recomp\n";

            fs::path headerPath = fs::path(m_config.outputPath) / "ps2_recompiled_stubs.h";
            writeToFile(headerPath.string(), ss.str());

            {
                std::ostringstream msg;
                msg << "generated stub header file: " << headerPath;
                m_reporter.progress(msg.str());
            }
            return true;
        }
        catch (const std::exception &e)
        {
            m_reporter.error("stub-header", e.what());
            return false;
        }
    }

    bool PS2Recompiler::generateFunctionHeader()
    {
        try
        {
            std::stringstream ss;

            ss << "#ifndef PS2_RECOMPILED_FUNCTIONS_H\n";
            ss << "#define PS2_RECOMPILED_FUNCTIONS_H\n\n";

            ss << "#include <cstdint>\n\n";
            ss << "struct R5900Context;\n";
            ss << "class PS2Runtime;\n\n";

            for (const auto &function : m_functions)
            {
                if (!shouldGenerateCodeForFunction(function))
                {
                    continue;
                }

                std::string finalName = m_codeGenerator->getFunctionName(function.start);

                ss << "void " << finalName << "(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime);\n";
            }

            ss << "\n#endif // PS2_RECOMPILED_FUNCTIONS_H\n";

            fs::path headerPath = fs::path(m_config.outputPath) / "ps2_recompiled_functions.h";
            writeToFile(headerPath.string(), ss.str());

            {
                std::ostringstream msg;
                msg << "generated function header file: " << headerPath;
                m_reporter.progress(msg.str());
            }
            return true;
        }
        catch (const std::exception &e)
        {
            m_reporter.error("function-header", e.what());
            return false;
        }
    }

    void PS2Recompiler::loadExternalCallTargetManifests()
    {
        m_ingestedExternalCallTargets.clear();

        std::vector<uint32_t> merged;
        for (const auto &manifestPath : m_config.externalCallTargetManifests)
        {
            std::ifstream manifestFile(manifestPath);
            if (!manifestFile)
            {
                m_reporter.warning("external-call-targets", "Failed to open manifest for reading: " + manifestPath);
                continue;
            }

            const std::vector<uint32_t> parsed = ParseCallTargetManifest(manifestFile);
            merged.insert(merged.end(), parsed.begin(), parsed.end());

            std::ostringstream msg;
            msg << "ingested " << parsed.size() << " external call target(s) from " << manifestPath;
            m_reporter.progress(msg.str());
        }

        std::sort(merged.begin(), merged.end());
        merged.erase(std::unique(merged.begin(), merged.end()), merged.end());
        m_ingestedExternalCallTargets = std::move(merged);
    }

    std::vector<uint32_t> PS2Recompiler::CollectExternalCallTargets(
        const std::unordered_map<uint32_t, std::vector<Instruction>> &decodedFunctions,
        const std::vector<Function> &functions,
        const std::vector<Section> &sections)
    {
        std::vector<uint32_t> externalTargets;

        auto isInsideExecutableSection = [&](uint32_t address) -> bool
        {
            for (const auto &section : sections)
            {
                if (!section.isCode)
                {
                    continue;
                }

                if (address >= section.address && address < section.address + section.size)
                {
                    return true;
                }
            }
            return false;
        };

        auto isInsideRecompiledFunction = [&](uint32_t address) -> bool
        {
            for (const auto &function : functions)
            {
                if (!function.isRecompiled || function.isStub || function.isSkipped)
                {
                    continue;
                }

                if (address >= function.start && address < function.end)
                {
                    return true;
                }
            }
            return false;
        };

        for (const auto &[functionStart, instructions] : decodedFunctions)
        {
            for (const auto &inst : instructions)
            {
                if (inst.opcode != OPCODE_JAL && inst.opcode != OPCODE_J)
                {
                    continue;
                }

                const uint32_t target = buildAbsoluteJumpTarget(inst.address, inst.target);
                if (!isInsideExecutableSection(target))
                {
                    continue;
                }

                if (isInsideRecompiledFunction(target))
                {
                    continue;
                }

                externalTargets.push_back(target);
            }
        }

        std::sort(externalTargets.begin(), externalTargets.end());
        externalTargets.erase(std::unique(externalTargets.begin(), externalTargets.end()), externalTargets.end());

        return externalTargets;
    }

    void PS2Recompiler::emitExternalCallTargetManifest()
    {
        const std::vector<uint32_t> externalTargets =
            CollectExternalCallTargets(m_decodedFunctions, m_functions, m_sections);

        std::ostringstream ss;
        for (uint32_t target : externalTargets)
        {
            ss << "0x" << std::hex << std::setw(8) << std::setfill('0') << target << std::dec << "\n";
        }

        const fs::path manifestPath = fs::path(m_config.outputPath) / "external_call_targets.txt";
        writeToFile(manifestPath.string(), ss.str());

        {
            std::ostringstream msg;
            msg << "wrote " << externalTargets.size() << " candidate cross-unit call target(s) to " << manifestPath;
            m_reporter.progress(msg.str());
        }
    }

    void PS2Recompiler::discoverAdditionalEntryPoints()
    {
        m_resumeEntryTargetsByOwner.clear();
        if (!m_codeGenerator)
        {
            return;
        }

        auto findContainingFunction = [&](uint32_t address) -> const Function *
        {
            const Function *best = nullptr;
            for (const auto &function : m_functions)
            {
                if (!function.isRecompiled || function.isStub || function.isSkipped)
                {
                    continue;
                }

                if (isEntryFunctionName(function.name))
                {
                    continue;
                }

                if (address < function.start || address >= function.end)
                {
                    continue;
                }

                auto decodedIt = m_decodedFunctions.find(function.start);
                if (decodedIt == m_decodedFunctions.end())
                {
                    continue;
                }

                const auto &decoded = decodedIt->second;
                const bool hasAddress = std::any_of(decoded.begin(), decoded.end(),
                                                    [&](const Instruction &candidate)
                                                    { return candidate.address == address; });
                if (!hasAddress)
                {
                    continue;
                }

                if (!best || function.start > best->start)
                {
                    best = &function;
                }
            }
            return best;
        };

        for (const auto &function : m_functions)
        {
            if (!function.isRecompiled || function.isStub || function.isSkipped)
            {
                continue;
            }

            if (isEntryFunctionName(function.name))
            {
                continue;
            }

            auto decodedIt = m_decodedFunctions.find(function.start);
            if (decodedIt == m_decodedFunctions.end())
            {
                continue;
            }

            const auto &instructions = decodedIt->second;
            CodeGenerator::AnalysisResult analysisResult =
                m_codeGenerator->collectInternalBranchTargets(function, instructions, &m_functions);

            auto &ownerTargets = m_resumeEntryTargetsByOwner[function.start];
            ownerTargets.insert(ownerTargets.end(),
                                analysisResult.resumeEntryPoints.begin(),
                                analysisResult.resumeEntryPoints.end());
            ownerTargets.insert(ownerTargets.end(),
                                analysisResult.indirectFallbackEntryPoints.begin(),
                                analysisResult.indirectFallbackEntryPoints.end());

            for (uint32_t target : analysisResult.externalEntryPoints)
            {
                const Function *owner = findContainingFunction(target);
                if (!owner)
                {
                    continue;
                }

                if (owner->start == target)
                {
                    continue;
                }

                auto &targets = m_resumeEntryTargetsByOwner[owner->start];
                targets.push_back(target);
            }
        }

        for (uint32_t target : m_ingestedExternalCallTargets)
        {
            const Function *owner = findContainingFunction(target);
            if (!owner)
            {
                continue;
            }

            if (owner->start == target)
            {
                continue;
            }

            auto &targets = m_resumeEntryTargetsByOwner[owner->start];
            targets.push_back(target);
        }

        if (m_elfParser)
        {
            try
            {
                const std::vector<uint32_t> threadEntries = DiscoverDataEmbeddedThreadEntries(
                    m_decodedFunctions,
                    [this](uint32_t address) { return m_elfParser->isValidAddress(address); },
                    [this](uint32_t address) { return m_elfParser->readWord(address); });

                size_t mappedThreadEntries = 0u;
                for (uint32_t target : threadEntries)
                {
                    const Function *owner = findContainingFunction(target);
                    if (!owner)
                    {
                        continue;
                    }

                    if (owner->start == target)
                    {
                        continue;
                    }

                    auto &targets = m_resumeEntryTargetsByOwner[owner->start];
                    targets.push_back(target);
                    ++mappedThreadEntries;
                }

                if (mappedThreadEntries > 0u)
                {
                    std::ostringstream msg;
                    msg << "mapped " << mappedThreadEntries << " data-embedded thread entry point(s)";
                    m_reporter.progress(msg.str());
                }
            }
            catch (const std::exception &ex)
            {
                m_reporter.warning("thread-entries", std::string("failed to discover data-embedded thread entries: ") + ex.what());
            }
        }

        size_t totalTargets = 0u;
        for (auto it = m_resumeEntryTargetsByOwner.begin(); it != m_resumeEntryTargetsByOwner.end();)
        {
            auto &targets = it->second;
            std::sort(targets.begin(), targets.end());
            targets.erase(std::unique(targets.begin(), targets.end()), targets.end());
            if (targets.empty())
            {
                it = m_resumeEntryTargetsByOwner.erase(it);
                continue;
            }

            totalTargets += targets.size();
            ++it;
        }

        m_codeGenerator->setResumeEntryTargets(m_resumeEntryTargetsByOwner);

        if (totalTargets > 0u)
        {
            m_reporter.recordAdditionalEntryPoints(totalTargets);
            std::ostringstream msg;
            msg << "collected " << totalTargets
                << " resumable entry point(s) across "
                << m_resumeEntryTargetsByOwner.size()
                << " owner function(s)";
            m_reporter.progress(msg.str());
        }

        emitExternalCallTargetManifest();
    }

    bool PS2Recompiler::decodeFunction(Function &function)
    {
        std::vector<Instruction> instructions;
        bool truncated = false;

        uint32_t start = function.start;
        uint32_t end = function.end;

        for (uint32_t address = start; address < end; address += 4)
        {
            try
            {
                if (!m_elfParser->isValidAddress(address))
                {
                    {
                        std::ostringstream msg;
                        msg << "Invalid address 0x" << std::hex << address << " (truncating decode)";
                        m_reporter.warningAt("decode", function.name, address, msg.str());
                    }
                    truncated = true;
                    break;
                }

                uint32_t rawInstruction = m_elfParser->readWord(address);
                const uint32_t originalInstruction = rawInstruction;

                auto patchIt = m_config.patches.find(address);
                if (patchIt != m_config.patches.end())
                {
                    const PatchClass patchClass = classifyPatchedInstruction(originalInstruction);
                    if (shouldApplyConfiguredPatch(patchClass, m_config))
                    {
                        try
                        {
                            rawInstruction = std::stoul(patchIt->second, nullptr, 0);
                            {
                                std::ostringstream msg;
                                msg << "Applied patch at 0x" << std::hex << address;
                                m_reporter.info("patch", msg.str());
                            }
                        }
                        catch (const std::exception &e)
                        {
                            {
                                std::ostringstream msg;
                                msg << "Invalid patch value at 0x" << std::hex << address
                                    << " (" << patchIt->second << "): " << e.what()
                                    << ". Using original instruction.";
                                m_reporter.warningAt("patch", function.name, address, msg.str());
                            }
                        }
                    }
                }

                Instruction inst = m_decoder->decodeInstruction(address, rawInstruction, !m_config.lowMemoryMode);

                auto mmioIt = m_config.mmioByInstructionAddress.find(address);
                if (mmioIt != m_config.mmioByInstructionAddress.end())
                {
                    inst.isMmio = true;
                    inst.mmioAddress = mmioIt->second;
                }

                instructions.push_back(inst);
            }
            catch (const std::exception &e)
            {
                {
                    std::ostringstream msg;
                    msg << "Error decoding instruction: " << e.what() << " (truncating decode)";
                    m_reporter.warningAt("decode", function.name, address, msg.str());
                }
                truncated = true;
                break;
            }
        }

        if (instructions.empty())
        {
            {
                std::ostringstream msg;
                msg << "No decodable instructions found at 0x" << std::hex << function.start;
                m_reporter.warningAt("decode", function.name, function.start, msg.str());
            }
            return false;
        }

        if (truncated)
        {
            function.end = instructions.back().address + 4;
        }

        m_decodedFunctions.insert_or_assign(function.start, std::move(instructions));

        return true;
    }

    bool PS2Recompiler::shouldSkipFunction(const Function &function) const
    {
        if (m_skipFunctionStarts.contains(function.start))
        {
            return true;
        }

        return m_skipFunctions.contains(function.name);
    }

    bool PS2Recompiler::isStubFunction(const Function &function) const
    {
        if (m_stubFunctionStarts.contains(function.start))
        {
            return true;
        }

        if (m_stubFunctions.contains(function.name))
        {
            return true;
        }
        return ps2_runtime_calls::isStubName(function.name);
    }

    bool PS2Recompiler::writeToFile(const std::string &path, const std::string &content)
    {
        std::ofstream file(path);
        if (!file)
        {
            m_reporter.error("file", "Failed to open file for writing: " + path);
            return false;
        }

        file << content;
        file.close();

        return true;
    }

    std::filesystem::path PS2Recompiler::getOutputPath(const Function &function) const
    {
        std::string safeName;
        bool usedRenamedName = false;
        auto renameIt = m_functionRenames.find(function.start);
        if (renameIt != m_functionRenames.end() && !renameIt->second.empty())
        {
            safeName = renameIt->second;
            usedRenamedName = true;
        }
        else
        {
            safeName = sanitizeFunctionName(function.name);
        }

        std::replace_if(safeName.begin(), safeName.end(), [](char c)
                        { return c == '/' || c == '\\' || c == ':' || c == '*' ||
                                 c == '?' || c == '"' || c == '<' || c == '>' ||
                                 c == '|' || c == '$'; }, '_');

        if (safeName.empty())
        {
            std::stringstream ss;
            ss << "func_" << std::hex << function.start;
            safeName = ss.str();
        }

        if (!usedRenamedName)
        {
            std::stringstream suffix;
            suffix << "_0x" << std::hex << function.start;
            const std::string suffixText = suffix.str();
            if (safeName.size() < suffixText.size() ||
                safeName.compare(safeName.size() - suffixText.size(), suffixText.size(), suffixText) != 0)
            {
                safeName += suffixText;
            }
        }

        std::filesystem::path outputPath = m_config.outputPath;
        outputPath /= clampFilenameLength(safeName, ".cpp", 100);

        return outputPath;
    }

    std::string PS2Recompiler::clampFilenameLength(const std::string& baseName, const std::string& extension, std::size_t maxLength)
    {
        if (maxLength == 0)
        {
            // Keep this static helper side-effect free; callers validate arguments.
            //Better go over the limit than create files with an empty path
            return baseName + extension;
        }

        if (baseName.size() + extension.size() <= maxLength)
            return baseName + extension;

        std::string namePart = baseName;
        std::string preservedSuffix;

        auto suffixPos = namePart.rfind("_0x");
        if (suffixPos != std::string::npos)
        {
            preservedSuffix = namePart.substr(suffixPos);
            namePart = namePart.substr(0, suffixPos);
        }

        std::size_t available = maxLength - extension.size() - preservedSuffix.size();

        if (available == 0)
        {
            return preservedSuffix + extension;
        }

        if (namePart.size() > available)
            namePart = namePart.substr(0, available);

        return namePart + preservedSuffix + extension;
    }

    std::string PS2Recompiler::sanitizeFunctionName(const std::string &name) const
    {
        std::string sanitized = sanitizeIdentifierBody(name);
        if (sanitized.empty())
        {
            return sanitized;
        }

        if (sanitized == "main")
        {
            return "ps2_main";
        }

        if (ps2recomp::kKeywords.contains(sanitized) || isReservedCxxIdentifier(sanitized))
        {
            return "ps2_" + sanitized;
        }

        return sanitized;
    }

    size_t PS2Recompiler::DiscoverAdditionalEntryPoints(
        std::vector<Function> &functions,
        std::unordered_map<uint32_t, std::vector<Instruction>> &decodedFunctions,
        const std::vector<Section> &sections)
    {
        CodeGenerator codeGenerator({}, sections);
        const EntryDiscoveryStats stats = discoverAdditionalEntryPointsImpl(
            functions,
            decodedFunctions,
            sections,
            &codeGenerator,
            [](Function &)
            { return false; });
        return stats.discoveredCount;
    }

    size_t PS2Recompiler::ResliceEntryFunctions(
        std::vector<Function> &functions,
        std::unordered_map<uint32_t, std::vector<Instruction>> &decodedFunctions)
    {
        return resliceEntryFunctionsImpl(functions, decodedFunctions);
    }

    StubTarget PS2Recompiler::resolveStubTarget(const std::string &name)
    {
        if (!ps2_runtime_calls::resolveSyscallName(name).empty())
        {
            return StubTarget::Syscall;
        }
        if (!ps2_runtime_calls::resolveStubName(name).empty())
        {
            return StubTarget::Stub;
        }
        return StubTarget::Unknown;
    }

    std::string PS2Recompiler::ClampFilenameLength(const std::string& baseName, const std::string& extension, std::size_t maxLength)
    {
        return clampFilenameLength(baseName, extension, maxLength);
    }

    std::vector<uint32_t> PS2Recompiler::ParseCallTargetManifest(std::istream &input)
    {
        std::vector<uint32_t> targets;
        std::string line;

        while (std::getline(input, line))
        {
            const size_t firstNonSpace = line.find_first_not_of(" \t\r\n");
            if (firstNonSpace == std::string::npos)
            {
                continue;
            }

            if (line[firstNonSpace] == '#')
            {
                continue;
            }

            try
            {
                uint32_t target = static_cast<uint32_t>(std::stoul(line.substr(firstNonSpace), nullptr, 0));
                targets.push_back(target);
            }
            catch (const std::exception &)
            {
                continue;
            }
        }

        std::sort(targets.begin(), targets.end());
        targets.erase(std::unique(targets.begin(), targets.end()), targets.end());
        return targets;
    }

    namespace
    {
        constexpr uint32_t kSyscallCreateThreadNumber = 0x20u;
        constexpr uint32_t kThreadEntryRegV1 = 3u;
        constexpr uint32_t kThreadEntryRegA0 = 4u;
        constexpr size_t kThreadWrapperScanWindow = 8u;
        constexpr size_t kThreadSyscallBackScanWindow = 8u;
        constexpr size_t kThreadConstantScanWindow = 12u;

        bool isAddiuV1SyscallImm(const Instruction &inst)
        {
            return inst.opcode == OPCODE_ADDIU && inst.rt == kThreadEntryRegV1 && inst.rs == 0u &&
                   (inst.immediate & 0xFFFFu) == kSyscallCreateThreadNumber;
        }

        bool isSyscallInstruction(const Instruction &inst)
        {
            return inst.opcode == OPCODE_SPECIAL && inst.function == SPECIAL_SYSCALL;
        }

        // Simplified "who writes this register" model shared by the $v1 clobber check
        // and the $a0 constant-propagation walk below: for I-type opcodes the written
        // register is rt, for OPCODE_SPECIAL it is rd. $zero is never considered written.
        // J-type instructions (j/jal) are excluded: the decoder unconditionally populates
        // rs/rt/rd from the raw instruction bits (see R5900Decoder::decodeInstruction),
        // but for a 26-bit jump target those bit positions are part of the target, not a
        // register field, so treating them as a register write would be spurious.
        uint32_t writtenRegisterOrZero(const Instruction &inst)
        {
            if (inst.opcode == OPCODE_J || inst.opcode == OPCODE_JAL)
            {
                return 0u;
            }
            if (inst.opcode == OPCODE_SPECIAL)
            {
                return inst.rd;
            }
            return inst.rt;
        }

        bool writesTrackedRegister(const Instruction &inst, uint32_t reg)
        {
            if (reg == 0u)
            {
                return false;
            }
            return writtenRegisterOrZero(inst) == reg;
        }

        struct ThreadCreateCallSite
        {
            const std::vector<Instruction> *instructions;
            size_t index;
            bool isJal;
        };
    }

    std::vector<uint32_t> PS2Recompiler::DiscoverDataEmbeddedThreadEntries(
        const std::unordered_map<uint32_t, std::vector<Instruction>> &decodedFunctions,
        const std::function<bool(uint32_t)> &isValidAddress,
        const std::function<uint32_t(uint32_t)> &readWord)
    {
        std::vector<uint32_t> results;

        // Step 1: identify CreateThread (syscall 0x20) wrapper function entry points -
        // functions whose first few instructions set $v1 = 0x20 and execute a syscall.
        std::unordered_set<uint32_t> wrapperStarts;
        for (const auto &[functionStart, instructions] : decodedFunctions)
        {
            const size_t window = std::min(instructions.size(), kThreadWrapperScanWindow);
            bool sawAddiuV1Syscall = false;
            bool isWrapper = false;
            for (size_t idx = 0; idx < window; ++idx)
            {
                const Instruction &inst = instructions[idx];
                if (isAddiuV1SyscallImm(inst))
                {
                    sawAddiuV1Syscall = true;
                    continue;
                }
                if (sawAddiuV1Syscall && isSyscallInstruction(inst))
                {
                    isWrapper = true;
                    break;
                }
            }

            if (isWrapper)
            {
                wrapperStarts.insert(functionStart);
            }
        }

        // Step 2: find CreateThread invocation sites - either a jal to a wrapper found
        // above, or a direct inline syscall preceded (within a small window, with no
        // intervening write to $v1) by addiu $v1, $zero, 0x20.
        std::vector<ThreadCreateCallSite> callSites;
        for (const auto &[functionStart, instructions] : decodedFunctions)
        {
            (void)functionStart;
            for (size_t idx = 0; idx < instructions.size(); ++idx)
            {
                const Instruction &inst = instructions[idx];

                if (inst.opcode == OPCODE_JAL)
                {
                    const uint32_t target = buildAbsoluteJumpTarget(inst.address, inst.target);
                    if (wrapperStarts.count(target) != 0u)
                    {
                        callSites.push_back(ThreadCreateCallSite{&instructions, idx, true});
                    }
                    continue;
                }

                if (!isSyscallInstruction(inst))
                {
                    continue;
                }

                // Note: this back-scan can double-attribute a single `addiu $v1,$zero,0x20`
                // write to two syscall instructions if both sit within the scan window with no
                // intervening $v1 clobber. That is harmless here: the worst case is one extra
                // in-range candidate entry, which is de-duplicated downstream (results are sorted
                // and uniqued, and the resume-target map dedups per owner), so no logic change.
                const size_t lowerBound = (idx >= kThreadSyscallBackScanWindow) ? idx - kThreadSyscallBackScanWindow : 0u;
                bool foundAddiuV1 = false;
                for (size_t p = idx; p > lowerBound;)
                {
                    --p;
                    const Instruction &prior = instructions[p];
                    if (isAddiuV1SyscallImm(prior))
                    {
                        foundAddiuV1 = true;
                        break;
                    }
                    if (writesTrackedRegister(prior, kThreadEntryRegV1))
                    {
                        break;
                    }
                }

                if (foundAddiuV1)
                {
                    callSites.push_back(ThreadCreateCallSite{&instructions, idx, false});
                }
            }
        }

        // Step 3 + 4: recover the static $a0 (ThreadParam*) value at each call site by
        // walking a small constant-propagation window forward, then read the embedded
        // entry function pointer (word offset +4 in the ThreadParam struct) from ELF data.
        for (const ThreadCreateCallSite &site : callSites)
        {
            const std::vector<Instruction> &instructions = *site.instructions;

            size_t windowEnd;
            if (site.isJal)
            {
                windowEnd = (site.index + 1u < instructions.size()) ? site.index + 1u : site.index;
            }
            else
            {
                if (site.index == 0u)
                {
                    continue;
                }
                windowEnd = site.index - 1u;
            }
            const size_t windowStart =
                (site.index >= kThreadConstantScanWindow) ? site.index - kThreadConstantScanWindow : 0u;

            std::unordered_map<uint32_t, uint32_t> resolved;
            auto invalidate = [&resolved](uint32_t reg)
            {
                if (reg != 0u)
                {
                    resolved.erase(reg);
                }
            };

            for (size_t idx = windowStart; idx <= windowEnd; ++idx)
            {
                const Instruction &inst = instructions[idx];

                if (inst.opcode == OPCODE_LUI)
                {
                    if (inst.rt != 0u)
                    {
                        resolved[inst.rt] = (inst.immediate & 0xFFFFu) << 16;
                    }
                    continue;
                }

                if (inst.opcode == OPCODE_ADDIU)
                {
                    auto srcIt = resolved.find(inst.rs);
                    if (inst.rs != 0u && srcIt != resolved.end())
                    {
                        const int32_t lo = static_cast<int32_t>(static_cast<int16_t>(inst.immediate & 0xFFFFu));
                        if (inst.rt != 0u)
                        {
                            resolved[inst.rt] = static_cast<uint32_t>(static_cast<int32_t>(srcIt->second) + lo);
                        }
                        continue;
                    }
                }
                else if (inst.opcode == OPCODE_ORI)
                {
                    auto srcIt = resolved.find(inst.rs);
                    if (inst.rs != 0u && srcIt != resolved.end())
                    {
                        const uint32_t lo = inst.immediate & 0xFFFFu;
                        if (inst.rt != 0u)
                        {
                            resolved[inst.rt] = srcIt->second | lo;
                        }
                        continue;
                    }
                }

                invalidate(writtenRegisterOrZero(inst));
            }

            auto a0It = resolved.find(kThreadEntryRegA0);
            if (a0It == resolved.end())
            {
                continue;
            }

            const uint32_t paramAddress = a0It->second;
            if (!isValidAddress(paramAddress) || !isValidAddress(paramAddress + 4u))
            {
                continue;
            }

            uint32_t entry = 0u;
            try
            {
                entry = readWord(paramAddress + 4u);
            }
            catch (const std::exception &)
            {
                // A section can satisfy isValidAddress's byte-range check yet still have
                // no backing data (e.g. BSS), in which case readWord throws. Skip this
                // candidate rather than losing the whole discovery pass.
                continue;
            }

            if (entry != 0u)
            {
                results.push_back(entry);
            }
        }

        std::sort(results.begin(), results.end());
        results.erase(std::unique(results.begin(), results.end()), results.end());
        return results;
    }
}
