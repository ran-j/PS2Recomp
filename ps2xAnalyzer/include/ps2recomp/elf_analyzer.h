#ifndef PS2RECOMP_ELF_ANALYZER_H
#define PS2RECOMP_ELF_ANALYZER_H

#include "ps2recomp/types.h"
#include "ps2recomp/elf_parser.h"
#include "ps2recomp/r5900_decoder.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <fstream>
#include <filesystem>

namespace ps2recomp
{
    class ElfAnalyzer
    {
    public:
        ElfAnalyzer(const std::string &elfPath);
        ~ElfAnalyzer();

        bool analyze();
        bool generateToml(const std::string &outputPath);

    private:
        std::string m_elfPath;
        std::unique_ptr<ElfParser> m_elfParser;
        std::unique_ptr<R5900Decoder> m_decoder;

        std::vector<Function> m_functions;
        std::vector<Symbol> m_symbols;
        std::vector<Section> m_sections;
        std::vector<Relocation> m_relocations;

        std::unordered_set<std::string> m_libFunctions;   // Library functions to stub
        std::unordered_set<std::string> m_skipFunctions;  // Functions to skip
        std::unordered_map<uint32_t, uint32_t> m_patches; // Address -> instruction patches

        // Common PS2 library function names
        void initializeLibraryFunctions();

        // Analysis methods
        void analyzeEntryPoint();
        void analyzeLibraryFunctions();
        void analyzeCallGraph();
        void analyzeDataUsage();
        void identifyPotentialPatches();
        std::string escapeBackslashes(const std::string &path);

        // Helpers
        bool isSystemFunction(const std::string &name) const;
        bool isLibraryFunction(const std::string &name) const;
        void decodeFunction(const Function &function);
        std::string formatAddress(uint32_t address) const;
    };

}

#endif // PS2RECOMP_ELF_ANALYZER_H