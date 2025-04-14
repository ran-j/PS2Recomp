#ifndef PS2RECOMP_PS2_RECOMPILER_H
#define PS2RECOMP_PS2_RECOMPILER_H

#include "ps2recomp/types.h"
#include "ps2recomp/elf_parser.h"
#include "ps2recomp/r5900_decoder.h"
#include "ps2recomp/code_generator.h"
#include "ps2recomp/config_manager.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <filesystem>

namespace ps2recomp
{

    class PS2Recompiler
    {
    public:
        PS2Recompiler(const std::string &configPath);
        ~PS2Recompiler() = default;

        bool initialize();
        bool recompile();
        void generateOutput();

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
        std::unordered_map<std::string, bool> m_stubFunctions;
        std::unordered_map<std::string, bool> m_skipFunctions;
        std::map<uint32_t, std::string> m_generatedStubs;

        bool decodeFunction(Function &function);
        bool shouldStubFunction(const std::string &name) const;
        bool shouldSkipFunction(const std::string &name) const;
        std::string generateRuntimeHeader();
        std::string generateStubFunction(const Function& function);
		bool generateFunctionHeader();
        bool writeToFile(const std::string &path, const std::string &content);
        std::filesystem::path getOutputPath(const Function &function) const;
    };

}

#endif