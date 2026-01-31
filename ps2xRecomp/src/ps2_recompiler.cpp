#include "ps2recomp/ps2_recompiler.h"
#include "ps2recomp/instructions.h"
#include "ps2recomp/types.h"
#include "ps2recomp/elf_parser.h"
#include "ps2recomp/r5900_decoder.h"
#include "ps2_runtime_calls.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <stdexcept>
#include <filesystem>
#include <cctype>
#include <unordered_set>
#include <optional>
#include <limits>

namespace fs = std::filesystem;

namespace ps2recomp
{
    namespace
    {
        enum class StubTarget
        {
            Unknown,
            Syscall,
            Stub
        };

        StubTarget resolveStubTarget(const std::string &name)
        {
            if (ps2_runtime_calls::isSyscallName(name))
            {
                return StubTarget::Syscall;
            }
            if (ps2_runtime_calls::isStubName(name))
            {
                return StubTarget::Stub;
            }
            return StubTarget::Unknown;
        }
    }

    PS2Recompiler::PS2Recompiler(const std::string &configPath)
        : m_configManager(configPath)
    {
    }

    PS2Recompiler::~PS2Recompiler() = default;

    bool PS2Recompiler::initialize()
    {
        try
        {
            m_config = m_configManager.loadConfig();

            for (const auto &name : m_config.skipFunctions)
            {
                m_skipFunctions[name] = true;
            }
            for (const auto &name : m_config.stubImplementations)
            {
                m_stubFunctions.insert(name);
            }

            m_elfParser = std::make_unique<ElfParser>(m_config.inputPath);
            if (!m_elfParser->parse())
            {
                std::cerr << "Failed to parse ELF file: " << m_config.inputPath << std::endl;
                return false;
            }

            m_functions = m_elfParser->extractFunctions();
            m_symbols = m_elfParser->extractSymbols();
            m_sections = m_elfParser->getSections();
            m_relocations = m_elfParser->getRelocations();

            if (m_functions.empty())
            {
                std::cerr << "No functions found in ELF file." << std::endl;
                return false;
            }

            {
                m_bootstrapInfo = {};
                uint32_t entry = m_elfParser->getEntryPoint();
                std::cout << "ELF entry point: 0x" << std::hex << entry << std::dec << std::endl;
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
                    std::cout << "BSS range: 0x" << std::hex << bssStart << " - 0x" << bssEnd
                              << " (size 0x" << (bssEnd - bssStart) << "), gp=0x" << gp << std::dec << std::endl;
                }
                else
                {
                    std::cout << "No BSS found, gp=0x" << std::hex << gp << std::dec << std::endl;
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

            std::cout << "Extracted " << m_functions.size() << " functions, "
                      << m_symbols.size() << " symbols, "
                      << m_sections.size() << " sections, "
                      << m_relocations.size() << " relocations." << std::endl;

            m_decoder = std::make_unique<R5900Decoder>();
            m_codeGenerator = std::make_unique<CodeGenerator>(m_symbols);
            m_codeGenerator->setBootstrapInfo(m_bootstrapInfo);

            fs::create_directories(m_config.outputPath);

            return true;
        }
        catch (const std::exception &e)
        {
            std::cerr << "Error during initialization: " << e.what() << std::endl;
            return false;
        }
    }

    bool PS2Recompiler::recompile()
    {
        try
        {
            std::cout << "Recompiling " << m_functions.size() << " functions..." << std::endl;

            std::string runtimeHeader = generateRuntimeHeader();
            fs::path runtimeHeaderPath = fs::path(m_config.outputPath) / "ps2_runtime_macros.h";

            writeToFile(runtimeHeaderPath.string(), runtimeHeader);

            size_t processedCount = 0;
            for (auto &function : m_functions)
            {
                std::cout << "processing function: " << function.name << std::endl;

                if (isStubFunction(function.name))
                {
                    function.isStub = true;
                    continue;
                }

                if (shouldSkipFunction(function.name))
                {
                    std::cout << "Skipping function (stubbed): " << function.name << std::endl;
                    function.isStub = true;
                    continue;
                }

                if (!decodeFunction(function))
                {
                    std::cerr << "Failed to decode function: " << function.name << std::endl;
                    return false;
                }

                function.isRecompiled = true;
#if _DEBUG
                processedCount++;
                if (processedCount % 100 == 0)
                {
                    std::cout << "Processed " << processedCount << " functions." << std::endl;
                }
#endif
            }

            discoverAdditionalEntryPoints();

            std::cout << "Recompilation completed successfully." << std::endl;
            return true;
        }
        catch (const std::exception &e)
        {
            std::cerr << "Error during recompilation: " << e.what() << std::endl;
            return false;
        }
    }

    void PS2Recompiler::generateOutput()
    {
        try
        {
            m_functionRenames.clear();

            std::unordered_map<std::string, int> nameCounts;
            for (const auto &function : m_functions)
            {
                if (!function.isRecompiled && !function.isStub)
                    continue;
                std::string sanitized = sanitizeFunctionName(function.name);
                nameCounts[sanitized]++;
            }

            for (const auto &function : m_functions)
            {
                if (!function.isRecompiled && !function.isStub)
                    continue;

                std::string sanitized = sanitizeFunctionName(function.name);
                bool isDuplicate = nameCounts[sanitized] > 1;

                if (isDuplicate || sanitized != function.name)
                {
                    std::stringstream ss;
                    if (isDuplicate)
                    {
                        ss << sanitized << "_0x" << std::hex << function.start;
                    }
                    else
                    {
                        ss << sanitized;
                    }
                    m_functionRenames[function.start] = ss.str();
                }
            }

            if (m_codeGenerator)
            {
                m_codeGenerator->setRenamedFunctions(m_functionRenames);
            }

            m_generatedStubs.clear();
            for (const auto &function : m_functions)
            {
                if (function.isStub)
                {
                    std::string generatedName = m_codeGenerator->getGeneratedFunctionName(function);
                    std::stringstream stub;
                    stub << "void " << generatedName
                         << "(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) { ";

                    switch (resolveStubTarget(function.name))
                    {
                    case StubTarget::Syscall:
                        stub << "ps2_syscalls::" << function.name << "(rdram, ctx, runtime); ";
                        break;
                    case StubTarget::Stub:
                        stub << "ps2_stubs::" << function.name << "(rdram, ctx, runtime); ";
                        break;
                    default:
                        stub << "ps2_stubs::TODO(rdram, ctx, runtime); ";
                        break;
                    }

                    stub << "}";
                    m_generatedStubs[function.start] = stub.str();
                }
            }

            generateFunctionHeader();

            if (m_config.singleFileOutput)
            {
                std::stringstream combinedOutput;

                combinedOutput << "#include \"ps2_recompiled_functions.h\"\n\n";
                combinedOutput << "#include \"ps2_runtime_macros.h\"\n";
                combinedOutput << "#include \"ps2_runtime.h\"\n";
                combinedOutput << "#include \"ps2_recompiled_stubs.h\"\n";
                combinedOutput << "#include \"ps2_syscalls.h\"\n";
                combinedOutput << "#include \"ps2_stubs.h\"\n";
                if (m_bootstrapInfo.valid)
                {
                    combinedOutput << "\n"
                                   << m_codeGenerator->generateBootstrapFunction() << "\n\n";
                }

                for (const auto &function : m_functions)
                {
                    if (!function.isRecompiled && !function.isStub)
                    {
                        continue;
                    }

                    try
                    {
                        if (function.isStub)
                        {
                            combinedOutput << m_generatedStubs.at(function.start) << "\n\n";
                        }
                        else
                        {
                            const auto &instructions = m_decodedFunctions.at(function.start);
                            std::string code = m_codeGenerator->generateFunction(function, instructions, false);
                            combinedOutput << code << "\n\n";
                        }
                    }
                    catch (const std::exception &e)
                    {
                        std::cerr << "Error generating code for function "
                                  << function.name << " (start 0x"
                                  << std::hex << function.start << "): "
                                  << e.what() << std::endl;
                        throw;
                    }
                }

                fs::path outputPath = fs::path(m_config.outputPath) / "ps2_recompiled_functions.cpp";
                writeToFile(outputPath.string(), combinedOutput.str());
                std::cout << "Wrote recompiled to combined output to: " << outputPath << std::endl;
            }
            else
            {
                if (m_bootstrapInfo.valid)
                {
                    std::stringstream boot;
                    boot << "#include \"ps2_recompiled_functions.h\"\n\n";
                    boot << "#include \"ps2_runtime_macros.h\"\n";
                    boot << "#include \"ps2_runtime.h\"\n\n";
                    boot << m_codeGenerator->generateBootstrapFunction() << "\n";
                    fs::path bootPath = fs::path(m_config.outputPath) / "ps2_entry_bootstrap.cpp";
                    writeToFile(bootPath.string(), boot.str());
                }

                for (const auto &function : m_functions)
                {
                    if (!function.isRecompiled && !function.isStub)
                    {
                        continue;
                    }

                    std::string code;
                    try
                    {
                        if (function.isStub)
                        {
                            std::stringstream stubFile;
                            stubFile << "#include \"ps2_runtime.h\"\n";
                            stubFile << "#include \"ps2_syscalls.h\"\n";
                            stubFile << "#include \"ps2_stubs.h\"\n\n";
                            stubFile << m_generatedStubs[function.start] << "\n";
                            code = stubFile.str();
                        }
                        else
                        {
                            const auto &instructions = m_decodedFunctions[function.start];
                            code = m_codeGenerator->generateFunction(function, instructions, true);
                        }
                    }
                    catch (const std::exception &e)
                    {
                        std::cerr << "Error generating code for function "
                                  << function.name << " (start 0x"
                                  << std::hex << function.start << "): "
                                  << e.what() << std::endl;
                        throw;
                    }

                    fs::path outputPath = getOutputPath(function);
                    fs::create_directories(outputPath.parent_path());
                    writeToFile(outputPath.string(), code);
                }

                std::cout << "Wrote individual function files to: " << m_config.outputPath << std::endl;
            }

            std::string registerFunctions = m_codeGenerator->generateFunctionRegistration(m_functions, m_generatedStubs);

            fs::path registerPath = fs::path(m_config.outputPath) / "register_functions.cpp";
            writeToFile(registerPath.string(), registerFunctions);
            std::cout << "Generated function registration file: " << registerPath << std::endl;

            generateStubHeader();
        }
        catch (const std::exception &e)
        {
            std::cerr << "Error during output generation: " << e.what() << std::endl;
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
            stubNames.insert(m_config.skipFunctions.begin(), m_config.skipFunctions.end());
            stubNames.insert(m_config.stubImplementations.begin(), m_config.stubImplementations.end());

            for (const auto &funcName : stubNames)
            {
                ss << "void " << funcName << "(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime);\n";
            }

            // ss << "\n} // namespace stubs\n";
            // ss << "} // namespace ps2recomp\n";

            fs::path headerPath = fs::path(m_config.outputPath) / "ps2_recompiled_stubs.h";
            writeToFile(headerPath.string(), ss.str());

            std::cout << "Generated generating header file: " << headerPath << std::endl;
            return true;
        }
        catch (const std::exception &e)
        {
            std::cerr << "Error generating stub header: " << e.what() << std::endl;
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
                if (!function.isRecompiled && !function.isStub)
                {
                    continue;
                }

                std::string finalName = sanitizeFunctionName(function.name);
                finalName = m_codeGenerator->getGeneratedFunctionName(function);

                ss << "void " << finalName << "(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime);\n";
            }

            if (m_bootstrapInfo.valid)
            {
                ss << "void entry_" << std::hex << m_bootstrapInfo.entry << std::dec
                   << "(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime);\n";
            }

            ss << "\n#endif // PS2_RECOMPILED_FUNCTIONS_H\n";

            fs::path headerPath = fs::path(m_config.outputPath) / "ps2_recompiled_functions.h";
            writeToFile(headerPath.string(), ss.str());

            std::cout << "Generated function header file: " << headerPath << std::endl;
            return true;
        }
        catch (const std::exception &e)
        {
            std::cerr << "Error generating function header: " << e.what() << std::endl;
            return false;
        }
    }

    void PS2Recompiler::discoverAdditionalEntryPoints()
    {
        std::unordered_set<uint32_t> existingStarts;
        for (const auto &function : m_functions)
        {
            existingStarts.insert(function.start);
        }

        auto getStaticBranchTarget = [](const Instruction &inst) -> std::optional<uint32_t>
        {
            if (inst.opcode == OPCODE_J || inst.opcode == OPCODE_JAL)
            {
                return (inst.address & 0xF0000000) | (inst.target << 2);
            }

            if (inst.opcode == OPCODE_SPECIAL &&
                (inst.function == SPECIAL_JR || inst.function == SPECIAL_JALR))
            {
                return std::nullopt;
            }

            if (inst.isBranch)
            {
                int32_t offset = static_cast<int32_t>(inst.simmediate) << 2;
                return inst.address + 4 + offset;
            }

            return std::nullopt;
        };

        auto findContainingFunction = [&](uint32_t address) -> const Function *
        {
            for (const auto &function : m_functions)
            {
                if (address >= function.start && address < function.end)
                {
                    return &function;
                }
            }
            return nullptr;
        };

        std::vector<Function> newEntries;

        for (const auto &function : m_functions)
        {
            if (!function.isRecompiled || function.isStub)
            {
                continue;
            }

            auto decodedIt = m_decodedFunctions.find(function.start);
            if (decodedIt == m_decodedFunctions.end())
            {
                continue;
            }

            const auto &instructions = decodedIt->second;

            for (const auto &inst : instructions)
            {
                auto targetOpt = getStaticBranchTarget(inst);
                if (!targetOpt.has_value())
                {
                    continue;
                }

                uint32_t target = targetOpt.value();

                if ((target & 0x3) != 0 || !m_elfParser->isValidAddress(target))
                {
                    continue;
                }

                if (existingStarts.contains(target))
                {
                    continue;
                }

                const Function *containingFunction = findContainingFunction(target);
                if (!containingFunction || containingFunction->isStub || !containingFunction->isRecompiled)
                {
                    continue;
                }

                auto containingDecodedIt = m_decodedFunctions.find(containingFunction->start);
                if (containingDecodedIt == m_decodedFunctions.end())
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

                std::vector<Instruction> slicedInstructions(sliceIt, containingInstructions.end());
                m_decodedFunctions[target] = slicedInstructions;

                Function entryFunction;
                std::stringstream name;
                name << "entry_" << std::hex << target;
                entryFunction.name = name.str();
                entryFunction.start = target;
                entryFunction.end = containingFunction->end;
                entryFunction.isRecompiled = true;
                entryFunction.isStub = false;

                newEntries.push_back(entryFunction);
                existingStarts.insert(target);
            }
        }

        if (!newEntries.empty())
        {
            m_functions.insert(m_functions.end(), newEntries.begin(), newEntries.end());
            std::sort(m_functions.begin(), m_functions.end(),
                      [](const Function &a, const Function &b)
                      { return a.start < b.start; });

            std::cout << "Discovered " << newEntries.size()
                      << " additional entry point(s) inside existing functions." << std::endl;
        }
    }

    bool PS2Recompiler::decodeFunction(Function &function)
    {
        std::vector<Instruction> instructions;

        uint32_t start = function.start;
        uint32_t end = function.end;

        for (uint32_t address = start; address < end; address += 4)
        {
            try
            {
                if (!m_elfParser->isValidAddress(address))
                {
                    std::cerr << "Invalid address: 0x" << std::hex << address << std::dec
                              << " in function: " << function.name << std::endl;
                    return false;
                }

                uint32_t rawInstruction = m_elfParser->readWord(address);

                auto patchIt = m_config.patches.find(address);
                if (patchIt != m_config.patches.end())
                {
                    rawInstruction = std::stoul(patchIt->second, nullptr, 0);
                    std::cout << "Applied patch at 0x" << std::hex << address << std::dec << std::endl;
                }

                Instruction inst = m_decoder->decodeInstruction(address, rawInstruction);

                instructions.push_back(inst);
            }
            catch (const std::exception &e)
            {
                std::cerr << "Error decoding instruction at 0x" << std::hex << address << std::dec
                          << " in function: " << function.name << ": " << e.what() << std::endl;
                return false;
            }
        }

        m_decodedFunctions[function.start] = instructions;

        return true;
    }

    bool PS2Recompiler::shouldSkipFunction(const std::string &name) const
    {
        return m_skipFunctions.contains(name);
    }

    bool PS2Recompiler::isStubFunction(const std::string &name) const
    {
        if (m_stubFunctions.contains(name))
        {
            return true;
        }
        return ps2_runtime_calls::isStubName(name);
    }

    std::string PS2Recompiler::generateRuntimeHeader() const
	{
        return m_codeGenerator->generateMacroHeader();
    }

    bool PS2Recompiler::writeToFile(const std::string &path, const std::string &content)
    {
        std::ofstream file(path);
        if (!file)
        {
            std::cerr << "Failed to open file for writing: " << path << std::endl;
            return false;
        }

        file << content;
        file.close();

        return true;
    }

    std::filesystem::path PS2Recompiler::getOutputPath(const Function &function) const
    {
        std::string safeName = function.name;

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

        std::filesystem::path outputPath = m_config.outputPath;
        outputPath /= safeName + ".cpp";

        return outputPath;
    }

    std::string PS2Recompiler::sanitizeFunctionName(const std::string& name) const
    {
        std::string sanitized = name;
        std::replace(sanitized.begin(), sanitized.end(), '.', '_');

        if (sanitized == "main")
        {
            return "ps2_main";
        }

        if (ps2recomp::kKeywords.contains(sanitized))
        {
            return "ps2_" + sanitized;
        }

        if (sanitized.size() >= 2 &&
            sanitized[0] == '_' &&
            (sanitized[1] == '_' ||
                std::isupper(static_cast<unsigned char>(sanitized[1]))))
        {
            return "ps2_" + sanitized;
        }

        return sanitized;
    }
}
