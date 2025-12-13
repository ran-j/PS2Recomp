#include "ps2recomp/ps2_recompiler.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <stdexcept>
#include <filesystem>

namespace fs = std::filesystem;

// Helper to rename reserved C++ identifiers (must match code_generator.cpp)
static std::string sanitizeFunctionName(const std::string& name) {
    if (name == "main") return "game_main";
    if (name == "matherr") return "fn_matherr";  // Conflicts with C library
    // Prefix names starting with double underscore (reserved in C++)
    if (name.size() >= 2 && name[0] == '_' && name[1] == '_') {
        return "fn_" + name;
    }
    // Also handle names starting with underscore followed by uppercase
    if (name.size() >= 2 && name[0] == '_' && std::isupper(name[1])) {
        return "fn_" + name;
    }
    return name;
}

namespace ps2recomp
{

    PS2Recompiler::PS2Recompiler(const std::string &configPath)
        : m_configManager(configPath)
    {
    }

    bool PS2Recompiler::initialize()
    {
        try
        {
            m_config = m_configManager.loadConfig();

            for (const auto &name : m_config.skipFunctions)
            {
                m_skipFunctions[name] = true;
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

            // If no functions found in ELF (stripped), use external functions from config
            if (m_functions.empty() && !m_config.externalFunctions.empty())
            {
                std::cout << "ELF is stripped, using " << m_config.externalFunctions.size()
                          << " external function definitions..." << std::endl;
                for (const auto &extFunc : m_config.externalFunctions)
                {
                    Function func;
                    func.name = extFunc.name;
                    func.start = extFunc.address;
                    func.end = extFunc.address + extFunc.size;
                    func.isRecompiled = false;
                    func.isStub = false;
                    m_functions.push_back(func);

                    // Also add to symbols for better resolution
                    Symbol sym;
                    sym.name = extFunc.name;
                    sym.address = extFunc.address;
                    sym.size = extFunc.size;
                    sym.isFunction = true;
                    sym.isImported = false;
                    sym.isExported = true;
                    m_symbols.push_back(sym);
                }
            }

            std::cout << "Extracted " << m_functions.size() << " functions, "
                      << m_symbols.size() << " symbols, "
                      << m_sections.size() << " sections, "
                      << m_relocations.size() << " relocations." << std::endl;

            m_decoder = std::make_unique<R5900Decoder>();
            m_codeGenerator = std::make_unique<CodeGenerator>(m_symbols);

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

                if (shouldSkipFunction(function.name))
                {
                    std::cout << "Skipping function: " << function.name << std::endl;
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

            // After decoding all functions, find mid-function entry points
            collectMidFunctionEntryPoints();

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
            generateFunctionHeader();

            if (m_config.singleFileOutput)
            {
                std::stringstream combinedOutput;

                combinedOutput << "#include \"ps2_recompiled_functions.h\"\n\n";
                combinedOutput << "#include \"ps2_runtime_macros.h\"\n";
                combinedOutput << "#include \"ps2_runtime.h\"\n";
                combinedOutput << "#include \"ps2_recompiled_stubs.h\"\n";

                for (const auto &function : m_functions)
                {
                    if (!function.isRecompiled)
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
                            std::string code = m_codeGenerator->generateFunction(function, instructions, false, m_midFunctionEntryPoints);
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

                // Generate mid-function entry point stubs
                if (!m_midFunctionEntryPoints.empty())
                {
                    combinedOutput << "\n// ===== Mid-function entry points =====\n";
                    combinedOutput << "// These handle jumps to addresses inside other functions\n\n";

                    for (uint32_t entryAddr : m_midFunctionEntryPoints)
                    {
                        // Find which function contains this address
                        for (const auto &func : m_functions)
                        {
                            if (entryAddr >= func.start && entryAddr < func.end)
                            {
                                // Generate a stub that sets PC and calls the containing function
                                // The containing function should handle the internal branch target
                                combinedOutput << "void entry_" << std::hex << entryAddr << std::dec
                                              << "(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {\n";
                                combinedOutput << "    // Mid-function entry at 0x" << std::hex << entryAddr
                                              << " inside " << func.name << " (0x" << func.start << " - 0x" << func.end << ")\n" << std::dec;
                                combinedOutput << "    ctx->pc = 0x" << std::hex << entryAddr << std::dec << ";\n";
                                combinedOutput << "    " << sanitizeFunctionName(func.name) << "(rdram, ctx, runtime);\n";
                                combinedOutput << "}\n\n";
                                break;
                            }
                        }
                    }
                }

                fs::path outputPath = fs::path(m_config.outputPath) / "ps2_recompiled_functions.cpp";
                writeToFile(outputPath.string(), combinedOutput.str());
                std::cout << "Wrote recompiled to combined output to: " << outputPath << std::endl;
            }
            else
            {
                for (const auto &function : m_functions)
                {
                    if (!function.isRecompiled || function.isStub)
                    {
                        continue;
                    }

                    std::string code;
                    try
                    {
                        if (function.isStub)
                        {
                            code = m_generatedStubs[function.start];
                        }
                        else
                        {
                            const auto &instructions = m_decodedFunctions[function.start];
                            code = m_codeGenerator->generateFunction(function, instructions, true, m_midFunctionEntryPoints);
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

            std::string registerFunctions = m_codeGenerator->generateFunctionRegistration(
                m_functions, m_generatedStubs, m_midFunctionEntryPoints);

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

            for (const auto &funcName : m_config.skipFunctions)
            {
                // Skip 'main' as it conflicts with C++ main()
                if (funcName == "main") continue;
                ss << "void " << funcName << "(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime) { ps2_syscalls::TODO(rdram, ctx, runtime); }\n";
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
                if (function.isRecompiled)
                {
                    std::string funcName = sanitizeFunctionName(function.name);
                    ss << "void " << funcName << "(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime);\n";
                }
            }

            // Declare mid-function entry point stubs
            if (!m_midFunctionEntryPoints.empty())
            {
                ss << "\n// Mid-function entry points (for external jumps into middle of functions)\n";
                for (uint32_t addr : m_midFunctionEntryPoints)
                {
                    ss << "void entry_" << std::hex << addr << std::dec
                       << "(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime);\n";
                }
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
        return m_skipFunctions.find(name) != m_skipFunctions.end();
    }

    std::string PS2Recompiler::generateRuntimeHeader()
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

    void PS2Recompiler::collectMidFunctionEntryPoints()
    {
        // Build a set of all function start addresses for quick lookup
        std::set<uint32_t> functionStarts;
        for (const auto &func : m_functions)
        {
            functionStarts.insert(func.start);
        }

        // Build a map of address ranges to functions
        std::map<uint32_t, const Function *> addressToFunction;
        for (const auto &func : m_functions)
        {
            for (uint32_t addr = func.start; addr < func.end; addr += 4)
            {
                addressToFunction[addr] = &func;
            }
        }

        // Scan all decoded instructions for branch/jump targets
        for (const auto &[funcStart, instructions] : m_decodedFunctions)
        {
            for (const auto &inst : instructions)
            {
                uint32_t targetAddr = 0;

                // Check for conditional branches (BEQ, BNE, etc.)
                if (inst.isBranch)
                {
                    int32_t offset = inst.simmediate << 2;
                    targetAddr = inst.address + 4 + offset;
                }
                // Check for direct jumps (J, JAL)
                else if (inst.opcode == 0x02 || inst.opcode == 0x03) // J, JAL
                {
                    targetAddr = (inst.address & 0xF0000000) | (inst.target << 2);
                }

                if (targetAddr != 0)
                {
                    // Check if target is inside a function but not at its start
                    auto it = addressToFunction.find(targetAddr);
                    if (it != addressToFunction.end())
                    {
                        const Function *containingFunc = it->second;
                        // If target is not the start of a function, it's a mid-function entry point
                        if (targetAddr != containingFunc->start &&
                            functionStarts.find(targetAddr) == functionStarts.end())
                        {
                            m_midFunctionEntryPoints.insert(targetAddr);
                        }
                    }
                }
            }
        }

        std::cout << "Found " << m_midFunctionEntryPoints.size()
                  << " mid-function entry points that need stub generation." << std::endl;
    }
}
