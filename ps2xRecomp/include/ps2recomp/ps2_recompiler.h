#ifndef PS2RECOMP_PS2_RECOMPILER_H
#define PS2RECOMP_PS2_RECOMPILER_H

#include "code_generator.h"
#include "config_manager.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <filesystem>

namespace ps2recomp
{
    class R5900Decoder;
    class ElfParser;

    enum class StubTarget
    {
        Unknown,
        Syscall,
        Stub
    };

    class PS2Recompiler
    {
    public:
        explicit PS2Recompiler(const std::string &configPath);
        ~PS2Recompiler();

        bool initialize();
        bool recompile();
        void generateOutput();

        static StubTarget resolveStubTarget(const std::string& name);

    private:
        ConfigManager m_configManager;
        std::unique_ptr<ElfParser> m_elfParser;
        std::unique_ptr<R5900Decoder> m_decoder;
        std::unique_ptr<CodeGenerator> m_codeGenerator;
        RecompilerConfig m_config;

        std::vector<Function> m_functions;
        std::vector<Symbol> m_symbols;
        std::vector<Section> m_sections;
        std::vector<Relocation> m_relocations;

        std::unordered_map<uint32_t, std::vector<Instruction>> m_decodedFunctions;
        std::unordered_map<std::string, bool> m_skipFunctions;
        std::unordered_set<uint32_t> m_skipFunctionStarts;
        std::unordered_set<std::string> m_stubFunctions;
        std::unordered_set<uint32_t> m_stubFunctionStarts;
        std::map<uint32_t, std::string> m_generatedStubs;
        std::unordered_map<uint32_t, std::string> m_functionRenames;
        CodeGenerator::BootstrapInfo m_bootstrapInfo;

        bool decodeFunction(Function &function);
        void discoverAdditionalEntryPoints();
        bool shouldSkipFunction(const Function &function) const;
        bool isStubFunction(const Function &function) const;
        bool generateFunctionHeader();
        bool generateStubHeader();
        bool writeToFile(const std::string &path, const std::string &content);
        std::filesystem::path getOutputPath(const Function &function) const;
        std::string sanitizeFunctionName(const std::string &name) const;       
    };

}

#endif
