#ifndef PS2RECOMP_PS2_RECOMPILER_H
#define PS2RECOMP_PS2_RECOMPILER_H

#include "code_generator.h"
#include "config_manager.h"
#include "recompiler_reporter.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <filesystem>
#include <memory>
#include <map>
#include <istream>
#include <functional>

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
        void printReport() const;

        static StubTarget resolveStubTarget(const std::string& name);
        static size_t DiscoverAdditionalEntryPoints(
            std::vector<Function> &functions,
            std::unordered_map<uint32_t, std::vector<Instruction>> &decodedFunctions,
            const std::vector<Section> &sections);
        static size_t ResliceEntryFunctions(
            std::vector<Function> &functions,
            std::unordered_map<uint32_t, std::vector<Instruction>> &decodedFunctions);

        static std::string ClampFilenameLength(const std::string& baseName, const std::string& extension, std::size_t maxLength);

        // Parses a call-target manifest (one "0x%08x" address per line, blank lines and
        // '#' comments ignored) into a sorted, de-duplicated list of addresses. Shared by
        // the production manifest loader and unit tests.
        static std::vector<uint32_t> ParseCallTargetManifest(std::istream &input);

        // Discovers thread entry function pointers embedded in static ThreadParam
        // structs passed to the CreateThread syscall (0x20). Returns the entry-pointer
        // values read from data memory, sorted and de-duplicated.
        static std::vector<uint32_t> DiscoverDataEmbeddedThreadEntries(
            const std::unordered_map<uint32_t, std::vector<Instruction>> &decodedFunctions,
            const std::function<bool(uint32_t)> &isValidAddress,
            const std::function<uint32_t(uint32_t)> &readWord);

        // Collects jal/j targets that fall in executable sections but outside every
        // recompiled local function range - candidate cross-unit call targets to
        // publish in the external call-target manifest. Sorted and de-duplicated.
        static std::vector<uint32_t> CollectExternalCallTargets(
            const std::unordered_map<uint32_t, std::vector<Instruction>> &decodedFunctions,
            const std::vector<Function> &functions,
            const std::vector<Section> &sections);

    private:
        ConfigManager m_configManager;
        std::unique_ptr<ElfParser> m_elfParser;
        std::unique_ptr<R5900Decoder> m_decoder;
        std::unique_ptr<CodeGenerator> m_codeGenerator;
        RecompilerConfig m_config;
        RecompilerReporter m_reporter;

        std::vector<Function> m_functions;
        std::vector<Symbol> m_symbols;
        std::vector<Section> m_sections;
        std::vector<Relocation> m_relocations;

        std::unordered_map<uint32_t, std::vector<Instruction>> m_decodedFunctions;
        std::unordered_map<std::string, bool> m_skipFunctions;
        std::unordered_set<uint32_t> m_skipFunctionStarts;
        std::unordered_set<std::string> m_stubFunctions;
        std::unordered_set<uint32_t> m_stubFunctionStarts;
        std::unordered_map<uint32_t, std::string> m_stubHandlerBindingsByStart;
        std::map<uint32_t, std::string> m_generatedStubs;
        std::unordered_map<uint32_t, std::string> m_functionRenames;
        std::unordered_map<uint32_t, std::vector<uint32_t>> m_resumeEntryTargetsByOwner;
        std::vector<uint32_t> m_ingestedExternalCallTargets;
        CodeGenerator::BootstrapInfo m_bootstrapInfo;

        bool decodeFunction(Function &function);
        void discoverAdditionalEntryPoints();
        void loadExternalCallTargetManifests();
        void emitExternalCallTargetManifest();
        bool shouldSkipFunction(const Function &function) const;
        bool isStubFunction(const Function &function) const;
        bool generateFunctionHeader();
        bool generateStubHeader();
        bool writeToFile(const std::string &path, const std::string &content);
        std::filesystem::path getOutputPath(const Function &function) const;
        static std::string clampFilenameLength(const std::string& baseName, const std::string& extension, std::size_t maxLength);
        std::string sanitizeFunctionName(const std::string &name) const;       
    };

}

#endif
