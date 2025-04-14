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
                std::cout << "processing function: " << function.name << std::endl;

                if (shouldSkipFunction(function.name))
                {
                    std::cout << "Skipping function: " << function.name << std::endl;
                    continue;
                }

                if (shouldStubFunction(function.name))
                {
                    std::cout << "Stubbing function: " << function.name << std::endl;
                    function.isStub = true;
                    function.isRecompiled = true; // we're generating code for it
 
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
                        std::string code = m_codeGenerator->generateFunction(function, instructions, false);
                        combinedOutput << code << "\n\n";
                    }
                }

                fs::path outputPath = fs::path(m_config.outputPath) / "recompiled.cpp";
                writeToFile(outputPath.string(), combinedOutput.str());
                std::cout << "Wrote combined output to: " << outputPath << std::endl;
            }
            else
            {
                generateFunctionHeader();
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
                        code = m_codeGenerator->generateFunction(function, instructions, true);
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

    bool PS2Recompiler::generateFunctionHeader()
    {
        try
        {
            std::stringstream ss;

            ss << "#ifndef PS2_RECOMPILED_FUNCTIONS_H\n";
            ss << "#define PS2_RECOMPILED_FUNCTIONS_H\n\n";

            ss << "#include <cstdint>\n\n";
            ss << "struct R5900Context;\n\n";

            for (const auto &function : m_functions)
            {
                if (function.isRecompiled)
                {
                    ss << "void " << function.name << "(uint8_t* rdram, R5900Context* ctx);\n";
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
        ss << "#include \"ps2_runtime.h\"\n";
        ss << "#include \"ps2_recompiled_functions.h\"\n\n";

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
            const std::string &name = function.name;

            if (name == "printf" || name == "fprintf" || name == "sprintf" ||
                name == "snprintf" || name == "vprintf" || name == "vfprintf" ||
                name == "vsprintf" || name == "vsnprintf")
            {
                ss << generatePrintfStub(name);
            }
            else if (name == "fopen" || name == "fclose" || name == "fread" ||
                     name == "fwrite" || name == "fseek" || name == "ftell" ||
                     name == "rewind" || name == "fflush" || name == "ferror" ||
                     name == "clearerr" || name == "feof" || name == "open" ||
                     name == "close" || name == "read" || name == "write" ||
                     name == "lseek" || name == "stat" || name == "fstat")
            {
                ss << generateFileIOStub(name);
            }
            else if (name == "strlen" || name == "strcpy" || name == "strncpy" ||
                     name == "strcat" || name == "strncat" || name == "strcmp" ||
                     name == "strncmp" || name == "strchr" || name == "strrchr" ||
                     name == "strstr" || name == "strtok" || name == "strtok_r")
            {
                ss << generateStringStub(name);
            }
            else if (name == "sin" || name == "cos" || name == "tan" ||
                     name == "asin" || name == "acos" || name == "atan" ||
                     name == "atan2" || name == "sinh" || name == "cosh" ||
                     name == "tanh" || name == "exp" || name == "log" ||
                     name == "log10" || name == "pow" || name == "sqrt" ||
                     name == "ceil" || name == "floor" || name == "fabs" ||
                     name == "fmod")
            {
                ss << generateMathStub(name);
            }
            else if (name == "time" || name == "clock" || name == "difftime" ||
                     name == "mktime" || name == "localtime" || name == "gmtime" ||
                     name == "asctime" || name == "ctime" || name == "strftime")
            {
                ss << generateTimeStub(name);
            }
            else if (name == "socket" || name == "bind" || name == "listen" ||
                     name == "accept" || name == "connect" || name == "send" ||
                     name == "recv" || name == "sendto" || name == "recvfrom" ||
                     name == "gethostbyname" || name == "gethostbyaddr")
            {
                ss << generateNetworkStub(name);
            }
            else if (name == "pthread_create" || name == "pthread_join" ||
                     name == "pthread_exit" || name == "pthread_mutex_init" ||
                     name == "pthread_mutex_lock" || name == "pthread_mutex_unlock" ||
                     name == "pthread_cond_init" || name == "pthread_cond_wait" ||
                     name == "pthread_cond_signal")
            {
                ss << generateThreadStub(name);
            }
            else
            {
                ss << generateDefaultStub(name);
            }
        }

        ss << "}\n";

        return ss.str();
    }

    std::string PS2Recompiler::generatePrintfStub(const std::string &name)
    {
        std::stringstream ss;

        ss << "    // Format string is in $a0 (r4), args start at $a1 (r5)\n";
        ss << "    #ifdef _DEBUG\n";

        if (name == "fprintf" || name == "vfprintf")
        {
            ss << "    printf(\"Stub called: " << name << " with file handle=0x%08X, format at 0x%08X\\n\", ctx->r[4], ctx->r[5]);\n";
        }
        else
        {
            ss << "    printf(\"Stub called: " << name << " with format at 0x%08X\\n\", ctx->r[4]);\n";
        }

        ss << "    #endif\n";
        ss << "    // Return success (number of characters, but we'll just say 1)\n";
        ss << "    ctx->r[2] = 1;\n";

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

    std::string PS2Recompiler::generateMemoryCopyStub(const std::string &name)
    {
        std::stringstream ss;

        ss << "    // Memory copy\n";

        if (name == "bcopy")
        {
            ss << "    uint32_t src = ctx->r[4]; // Source in $a0\n";
            ss << "    uint32_t dst = ctx->r[5]; // Destination in $a1\n";
        }
        else
        {
            ss << "    uint32_t dst = ctx->r[4]; // Destination in $a0\n";
            ss << "    uint32_t src = ctx->r[5]; // Source in $a1\n";
        }

        ss << "    uint32_t size = ctx->r[6]; // Size in $a2\n";
        ss << "    #ifdef _DEBUG\n";
        ss << "    printf(\"Stub called: " << name << "(dst=0x%08X, src=0x%08X, size=%u)\\n\", "
           << (name == "bcopy" ? "dst, src" : "dst, src") << ", size);\n";
        ss << "    #endif\n";
        ss << "    // Only copy if within valid memory range\n";
        ss << "    if (dst < 0x2000000 && src < 0x2000000 && dst + size < 0x2000000 && src + size < 0x2000000) {\n";

        if (name == "memmove")
        {
            ss << "        memmove(rdram + dst, rdram + src, size);\n";
        }
        else
        {
            ss << "        memcpy(rdram + dst, rdram + src, size);\n";
        }

        ss << "    }\n";

        if (name == "bcopy")
        {
            ss << "    // bcopy doesn't return a value\n";
        }
        else
        {
            ss << "    ctx->r[2] = dst; // Return destination pointer\n";
        }

        return ss.str();
    }

    std::string PS2Recompiler::generateFileIOStub(const std::string &name)
    {
        std::stringstream ss;

        ss << "    // File I/O operation: " << name << "\n";
        ss << "    #ifdef _DEBUG\n";

        if (name == "fopen" || name == "open")
        {
            ss << "    uint32_t path_ptr = ctx->r[4]; // Path pointer in $a0\n";
            ss << "    uint32_t mode_ptr = ctx->r[5]; // Mode pointer in $a1\n";
            ss << "    printf(\"Stub called: " << name << "(path=0x%08X, mode=0x%08X)\\n\", path_ptr, mode_ptr);\n";
            ss << "    #endif\n";
            ss << "    // Return a fake file handle\n";
            ss << "    ctx->r[2] = 0x12345678; // Fake file handle\n";
        }
        else if (name == "fclose" || name == "close")
        {
            ss << "    uint32_t handle = ctx->r[4]; // File handle in $a0\n";
            ss << "    printf(\"Stub called: " << name << "(handle=0x%08X)\\n\", handle);\n";
            ss << "    #endif\n";
            ss << "    // Return success\n";
            ss << "    ctx->r[2] = 0;\n";
        }
        else if (name == "fread" || name == "read")
        {
            ss << "    uint32_t buffer = ctx->r[4]; // Buffer pointer in $a0\n";
            ss << "    uint32_t size = ctx->r[5]; // Size in $a1\n";
            ss << "    uint32_t count = ctx->r[6]; // Count in $a2 (for fread)\n";
            ss << "    uint32_t handle = ctx->r[7]; // File handle in $a3\n";
            ss << "    printf(\"Stub called: " << name << "(buffer=0x%08X, size=%u, count=%u, handle=0x%08X)\\n\", buffer, size, count, handle);\n";
            ss << "    #endif\n";
            ss << "    // Return number of bytes/items read\n";
            ss << "    ctx->r[2] = " << (name == "fread" ? "count" : "size") << "; // Pretend we read everything\n";
        }
        else if (name == "fwrite" || name == "write")
        {
            ss << "    uint32_t buffer = ctx->r[4]; // Buffer pointer in $a0\n";
            ss << "    uint32_t size = ctx->r[5]; // Size in $a1\n";
            ss << "    uint32_t count = ctx->r[6]; // Count in $a2 (for fwrite)\n";
            ss << "    uint32_t handle = ctx->r[7]; // File handle in $a3\n";
            ss << "    printf(\"Stub called: " << name << "(buffer=0x%08X, size=%u, count=%u, handle=0x%08X)\\n\", buffer, size, count, handle);\n";
            ss << "    #endif\n";
            ss << "    // Return number of bytes/items written\n";
            ss << "    ctx->r[2] = " << (name == "fwrite" ? "count" : "size") << "; // Pretend we wrote everything\n";
        }
        else
        {
            ss << "    printf(\"Stub called: " << name << "()\\n\");\n";
            ss << "    #endif\n";
            ss << "    // Return success\n";
            ss << "    ctx->r[2] = 0;\n";
        }

        return ss.str();
    }

    std::string PS2Recompiler::generateStringStub(const std::string &name)
    {
        std::stringstream ss;

        ss << "    // String operation: " << name << "\n";
        ss << "    #ifdef _DEBUG\n";

        if (name == "strlen")
        {
            ss << "    uint32_t str_ptr = ctx->r[4]; // String pointer in $a0\n";
            ss << "    printf(\"Stub called: strlen(str=0x%08X)\\n\", str_ptr);\n";
            ss << "    #endif\n";
            ss << "    // If string is in valid memory range, calculate real length\n";
            ss << "    if (str_ptr < 0x2000000) {\n";
            ss << "        const char* str = (const char*)(rdram + str_ptr);\n";
            ss << "        // Safely calculate strlen with bounds checking\n";
            ss << "        size_t len = 0;\n";
            ss << "        while (str_ptr + len < 0x2000000 && str[len] != '\\0') {\n";
            ss << "            len++;\n";
            ss << "        }\n";
            ss << "        ctx->r[2] = len;\n";
            ss << "    } else {\n";
            ss << "        ctx->r[2] = 0;\n";
            ss << "    }\n";
        }
        else if (name == "strcpy" || name == "strncpy")
        {
            ss << "    uint32_t dst_ptr = ctx->r[4]; // Destination pointer in $a0\n";
            ss << "    uint32_t src_ptr = ctx->r[5]; // Source pointer in $a1\n";
            if (name == "strncpy")
            {
                ss << "    uint32_t max_len = ctx->r[6]; // Max length in $a2\n";
                ss << "    printf(\"Stub called: strncpy(dst=0x%08X, src=0x%08X, n=%u)\\n\", dst_ptr, src_ptr, max_len);\n";
            }
            else
            {
                ss << "    printf(\"Stub called: strcpy(dst=0x%08X, src=0x%08X)\\n\", dst_ptr, src_ptr);\n";
            }
            ss << "    #endif\n";
            ss << "    // Return destination pointer\n";
            ss << "    ctx->r[2] = dst_ptr;\n";
        }
        else if (name == "strcmp" || name == "strncmp")
        {
            ss << "    uint32_t str1_ptr = ctx->r[4]; // String 1 pointer in $a0\n";
            ss << "    uint32_t str2_ptr = ctx->r[5]; // String 2 pointer in $a1\n";
            if (name == "strncmp")
            {
                ss << "    uint32_t max_len = ctx->r[6]; // Max length in $a2\n";
                ss << "    printf(\"Stub called: strncmp(s1=0x%08X, s2=0x%08X, n=%u)\\n\", str1_ptr, str2_ptr, max_len);\n";
            }
            else
            {
                ss << "    printf(\"Stub called: strcmp(s1=0x%08X, s2=0x%08X)\\n\", str1_ptr, str2_ptr);\n";
            }
            ss << "    #endif\n";
            ss << "    // Return comparison result (0 for equal)\n";
            ss << "    ctx->r[2] = 0;\n";
        }
        else
        {
            ss << "    printf(\"Stub called: " << name << "()\\n\");\n";
            ss << "    #endif\n";
            ss << "    // Return success\n";
            ss << "    ctx->r[2] = 0;\n";
        }

        return ss.str();
    }

    std::string PS2Recompiler::generateMathStub(const std::string &name)
    {
        std::stringstream ss;

        ss << "    // Math operation: " << name << "\n";
        ss << "    #ifdef _DEBUG\n";
        ss << "    printf(\"Stub called: " << name << "()\\n\");\n";
        ss << "    #endif\n";

        if (name == "sin" || name == "cos" || name == "tan" ||
            name == "asin" || name == "acos" || name == "atan" ||
            name == "atan2" || name == "sinh" || name == "cosh" ||
            name == "tanh" || name == "exp" || name == "log" ||
            name == "log10" || name == "pow" || name == "sqrt" ||
            name == "ceil" || name == "floor" || name == "fabs" ||
            name == "fmod")
        {
            ss << "    // Simply delegate to host function\n";
            ss << "    float arg1 = *(float*)&ctx->r[4]; // First argument in $a0\n";

            if (name == "atan2" || name == "pow" || name == "fmod")
            {
                ss << "    float arg2 = *(float*)&ctx->r[5]; // Second argument in $a1\n";
                ss << "    float result = " << name << "f(arg1, arg2);\n";
            }
            else
            {
                ss << "    float result = " << name << "f(arg1);\n";
            }

            ss << "    *(float*)&ctx->r[2] = result; // Return result in $v0\n";
        }
        else
        {
            ss << "    // Return 0.0f as default\n";
            ss << "    *(float*)&ctx->r[2] = 0.0f;\n";
        }

        return ss.str();
    }

    std::string PS2Recompiler::generateTimeStub(const std::string &name)
    {
        std::stringstream ss;

        ss << "    // Time operation: " << name << "\n";
        ss << "    #ifdef _DEBUG\n";
        ss << "    printf(\"Stub called: " << name << "()\\n\");\n";
        ss << "    #endif\n";

        if (name == "time")
        {
            ss << "    // Return current time value (seconds since epoch)\n";
            ss << "    ctx->r[2] = (uint32_t)time(NULL);\n";
        }
        else if (name == "clock")
        {
            ss << "    // Return clock ticks\n";
            ss << "    ctx->r[2] = (uint32_t)clock();\n";
        }
        else
        {
            ss << "    // Return success\n";
            ss << "    ctx->r[2] = 0;\n";
        }

        return ss.str();
    }

    // TODO give this more attention
    std::string PS2Recompiler::generateNetworkStub(const std::string &name)
    {
        std::stringstream ss;

        ss << "    // Network operation: " << name << "\n";
        ss << "    #ifdef _DEBUG\n";
        ss << "    printf(\"Stub called: " << name << "()\\n\");\n";
        ss << "    #endif\n";

        if (name == "socket")
        {
            ss << "    // Return fake socket descriptor\n";
            ss << "    ctx->r[2] = 3; // Fake socket fd\n";
        }
        else if (name == "bind" || name == "listen" || name == "connect")
        {
            ss << "    // Return success\n";
            ss << "    ctx->r[2] = 0;\n";
        }
        else if (name == "accept")
        {
            ss << "    // Return fake client socket descriptor\n";
            ss << "    ctx->r[2] = 4; // Fake client socket fd\n";
        }
        else if (name == "send" || name == "sendto")
        {
            ss << "    uint32_t size = ctx->r[6]; // Size in $a2\n";
            ss << "    // Return bytes sent (all of them)\n";
            ss << "    ctx->r[2] = size;\n";
        }
        else if (name == "recv" || name == "recvfrom")
        {
            ss << "    // Return 0 (no data)\n";
            ss << "    ctx->r[2] = 0;\n";
        }
        else
        {
            ss << "    // Return failure (-1)\n";
            ss << "    ctx->r[2] = (uint32_t)-1;\n";
        }

        return ss.str();
    }

    std::string PS2Recompiler::generateThreadStub(const std::string &name)
    {
        std::stringstream ss;

        ss << "    // Thread operation: " << name << "\n";
        ss << "    #ifdef _DEBUG\n";
        ss << "    printf(\"Stub called: " << name << "()\\n\");\n";
        ss << "    #endif\n";

        if (name == "pthread_create")
        {
            ss << "    // Return success\n";
            ss << "    ctx->r[2] = 0;\n";
            ss << "    // Set thread ID to 1 in the output parameter\n";
            ss << "    uint32_t thread_id_ptr = ctx->r[4]; // Thread ID pointer in $a0\n";
            ss << "    if (thread_id_ptr < 0x2000000) {\n";
            ss << "        *(uint32_t*)(rdram + thread_id_ptr) = 1;\n";
            ss << "    }\n";
        }
        else if (name.find("pthread_mutex_") != std::string::npos ||
                 name.find("pthread_cond_") != std::string::npos)
        {
            ss << "    // Return success\n";
            ss << "    ctx->r[2] = 0;\n";
        }
        else
        {
            ss << "    // Return success\n";
            ss << "    ctx->r[2] = 0;\n";
        }

        return ss.str();
    }

    std::string PS2Recompiler::generateDefaultStub(const std::string &name)
    {
        std::stringstream ss;

        ss << "    // Default stub implementation\n";
        ss << "    #ifdef _DEBUG\n";
        ss << "    printf(\"Stub function called: " << name << " at PC=0x%08X\\n\", ctx->pc);\n";
        ss << "    #endif\n";
        ss << "    // Default return value (0)\n";
        ss << "    ctx->r[2] = 0;\n";

        return ss.str();
    }
}