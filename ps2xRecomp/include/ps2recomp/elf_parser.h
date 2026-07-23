#ifndef PS2RECOMP_ELF_PARSER_H
#define PS2RECOMP_ELF_PARSER_H

// ELFIO (Release_3.12, vendored via FetchContent) uses uint16_t/uint32_t/uint64_t
// in elf_types.hpp without including <cstdint> itself. Newer libstdc++ releases
// trimmed the transitive includes that used to pull those typedefs in, so this
// header must guarantee they're visible before elfio.hpp is processed.
#include <cstdint>
#include <elfio/elfio.hpp>
#include <string>
#include <vector>
#include <memory>
#include <unordered_set>

namespace ps2recomp
{
        struct Relocation;
        struct Section;
        struct Function;
        struct Symbol;
        class RecompilerReporter;

        class ElfParser
        {
        public:
                explicit ElfParser(const std::string &filePath);
                ~ElfParser();

                bool parse();

                bool loadGhidraFunctionMap(const std::string &mapPath);
                std::vector<Function> extractFunctions() const;
                std::vector<Function> extractExtraFunctions() const;
                std::vector<Symbol> extractSymbols();
                std::vector<Section> getSections();
                std::vector<Relocation> getRelocations();

                // Helper methods
                bool isValidAddress(uint32_t address) const;
                uint32_t readWord(uint32_t address) const;
                uint8_t *getSectionData(const std::string &sectionName) const;
                uint32_t getSectionAddress(const std::string &sectionName) const;
                uint32_t getSectionSize(const std::string &sectionName) const;
                uint32_t getEntryPoint() const;
                void setReporter(RecompilerReporter *reporter);
                void debugAddress(uint32_t address) const;

        private:
                std::string m_filePath;
                std::unique_ptr<ELFIO::elfio> m_elf;

                std::vector<Section> m_sections;
                std::vector<Symbol> m_symbols;
                std::vector<Relocation> m_relocations;
                std::vector<Function> m_extraFunctions;
                bool m_hasLoadedGhidraMap = false;
                RecompilerReporter *m_reporter = nullptr;
                std::unordered_set<uint32_t> m_ghidraMapStarts;

                void loadSections();
                void loadSymbols();
                void loadRelocations();
                void loadDebugFunctions();
                bool isExecutableSection(const ELFIO::section *section) const;
                bool isDataSection(const ELFIO::section *section) const;
        };

} // namespace ps2recomp

#endif // PS2RECOMP_ELF_PARSER_H
