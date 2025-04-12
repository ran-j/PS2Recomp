#include "ps2recomp/ps2_recompiler.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <stdexcept>
#include <filesystem>

namespace fs = std::filesystem;

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

            for (const auto &name : m_config.stubFunctions)
            {
                m_stubFunctions[name] = true;
            }

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
                if (shouldSkipFunction(function.name))
                {
                    std::cout << "Skipping function: " << function.name << std::endl;
                    continue;
                }

                if (shouldStubFunction(function.name))
                {
                    std::cout << "Stubbing function: " << function.name << std::endl;
                    function.isStub = true;
                    // TODO: Generate stub implementation
                    continue;
                }

                if (shouldStubFunction(function.name))
                {
                    std::cout << "Stubbing function: " << function.name << std::endl;
                    function.isStub = true;
                    function.isRecompiled = true; // we're generating code for it

                    // Generate stub implementation and store it
                    std::string stubCode = generateStubFunction(function);
                    m_generatedStubs[function.start] = stubCode;

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
            if (m_config.singleFileOutput)
            {
                std::stringstream combinedOutput;

                combinedOutput << "#include \"ps2_runtime_macros.h\"\n";
                combinedOutput << "#include \"ps2_runtime.h\"\n\n";

                for (const auto &function : m_functions)
                {
                    if (!function.isRecompiled)
                    {
                        continue;
                    }

                    if (function.isStub)
                    {
                        combinedOutput << m_generatedStubs[function.start] << "\n\n";
                    }
                    else
                    {
                        const auto &instructions = m_decodedFunctions[function.start];
                        std::string code = m_codeGenerator->generateFunction(function, instructions);
                        combinedOutput << code << "\n\n";
                    }
                }

                fs::path outputPath = fs::path(m_config.outputPath) / "recompiled.cpp";
                writeToFile(outputPath.string(), combinedOutput.str());
                std::cout << "Wrote combined output to: " << outputPath << std::endl;
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
                    if (function.isStub)
                    {
                        code = m_generatedStubs[function.start];
                    }
                    else
                    {
                        const auto &instructions = m_decodedFunctions[function.start];
                        code = m_codeGenerator->generateFunction(function, instructions);
                    }

                    fs::path outputPath = getOutputPath(function);
                    fs::create_directories(outputPath.parent_path());
                    writeToFile(outputPath.string(), code);
                }

                std::cout << "Wrote individual function files to: " << m_config.outputPath << std::endl;
            }
        }
        catch (const std::exception &e)
        {
            std::cerr << "Error during output generation: " << e.what() << std::endl;
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

    bool PS2Recompiler::shouldStubFunction(const std::string &name) const
    {
        return m_stubFunctions.find(name) != m_stubFunctions.end();
    }

    bool PS2Recompiler::shouldSkipFunction(const std::string &name) const
    {
        return m_skipFunctions.find(name) != m_skipFunctions.end();
    }

    std::string PS2Recompiler::generateRuntimeHeader()
    {
        return m_codeGenerator->generateMacroHeader();
    }

    std::string PS2Recompiler::generateStubFunction(const Function &function)
    {
        std::stringstream ss;

        ss << "#include \"ps2_runtime_macros.h\"\n";
        ss << "#include \"ps2_runtime.h\"\n\n";

        ss << "// STUB FUNCTION: " << function.name << "\n";
        ss << "// Address: 0x" << std::hex << function.start << " - 0x" << function.end << std::dec << "\n";
        ss << "void " << function.name << "(uint8_t* rdram, R5900Context* ctx) {\n";

        auto stubImpl = m_config.stubImplementations.find(function.name);
        if (stubImpl != m_config.stubImplementations.end())
        {
            ss << "    // Custom stub implementation\n";
            ss << "    " << stubImpl->second << "\n";
        }
        else
        {
            // Default stub implementation based on common functions
            if (function.name == "printf" || function.name == "fprintf" ||
                function.name == "sprintf" || function.name == "snprintf")
            {
                ss << "    // Format string is in $a0 (r4), args start at $a1 (r5)\n";
                ss << "    #ifdef PS2_RECOMP_DEBUG\n";
                ss << "    printf(\"Stub called: " << function.name << " with format at 0x%08X\\n\", ctx->r[4]);\n";
                ss << "    #endif\n";
                ss << "    // Return success (number of characters, but we'll just say 1)\n";
                ss << "    ctx->r[2] = 1;\n";
            }
            else if (function.name == "malloc" || function.name == "calloc" ||
                     function.name == "realloc" || function.name == "memalign")
            {
                ss << "    // Memory allocation - Would call the runtime's allocation system\n";
                ss << "    uint32_t size = ctx->r[4]; // Size is in $a0\n";
                ss << "    #ifdef PS2_RECOMP_DEBUG\n";
                ss << "    printf(\"Stub called: " << function.name << " size=%u\\n\", size);\n";
                ss << "    #endif\n";
                ss << "    // In a real implementation, call runtime->allocateMemory(size)\n";
                ss << "    ctx->r[2] = 0; // Return NULL for now - replace with actual allocation in real implementation\n";
            }
            else if (function.name == "free")
            {
                ss << "    // Free memory - Would call the runtime's free system\n";
                ss << "    uint32_t ptr = ctx->r[4]; // Pointer is in $a0\n";
                ss << "    #ifdef PS2_RECOMP_DEBUG\n";
                ss << "    printf(\"Stub called: free(0x%08X)\\n\", ptr);\n";
                ss << "    #endif\n";
                ss << "    // In a real implementation, call runtime->freeMemory(ptr)\n";
            }
            else if (function.name == "memcpy" || function.name == "memmove")
            {
                ss << "    // Memory copy\n";
                ss << "    uint32_t dst = ctx->r[4]; // Destination in $a0\n";
                ss << "    uint32_t src = ctx->r[5]; // Source in $a1\n";
                ss << "    uint32_t size = ctx->r[6]; // Size in $a2\n";
                ss << "    #ifdef PS2_RECOMP_DEBUG\n";
                ss << "    printf(\"Stub called: " << function.name << "(dst=0x%08X, src=0x%08X, size=%u)\\n\", dst, src, size);\n";
                ss << "    #endif\n";
                ss << "    // Only copy if within valid memory range\n";
                ss << "    if (dst < 0x2000000 && src < 0x2000000 && dst + size < 0x2000000 && src + size < 0x2000000) {\n";
                ss << "        memcpy(rdram + dst, rdram + src, size);\n";
                ss << "    }\n";
                ss << "    ctx->r[2] = dst; // Return destination pointer\n";
            }
            else if (function.name == "memset")
            {
                ss << "    // Memory set\n";
                ss << "    uint32_t dst = ctx->r[4]; // Destination in $a0\n";
                ss << "    uint8_t value = (uint8_t)ctx->r[5]; // Value in $a1\n";
                ss << "    uint32_t size = ctx->r[6]; // Size in $a2\n";
                ss << "    #ifdef PS2_RECOMP_DEBUG\n";
                ss << "    printf(\"Stub called: memset(dst=0x%08X, value=%u, size=%u)\\n\", dst, value, size);\n";
                ss << "    #endif\n";
                ss << "    // Only set if within valid memory range\n";
                ss << "    if (dst < 0x2000000 && dst + size < 0x2000000) {\n";
                ss << "        memset(rdram + dst, value, size);\n";
                ss << "    }\n";
                ss << "    ctx->r[2] = dst; // Return destination pointer\n";
            }
            else
            {
                // Generic stub for unknown functions
                ss << "    // Default stub implementation\n";
                ss << "    #ifdef PS2_RECOMP_DEBUG\n";
                ss << "    printf(\"Stub function called: " << function.name << " at PC=0x%08X\\n\", ctx->pc);\n";
                ss << "    #endif\n";
                ss << "    // Default return value (0)\n";
                ss << "    ctx->r[2] = 0;\n";
            }
        }

        ss << "}\n";

        return ss.str();
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
}