#ifndef PS2RECOMP_ELF_PARSER_H
#define PS2RECOMP_ELF_PARSER_H

#include <elfio/elfio.hpp>
#include <string>
#include <vector>
#include <memory>

namespace ps2recomp
{
        struct Relocation;
        struct Section;
        struct Function;
        struct Symbol;

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
                void debugAddress(uint32_t address) const;

        private:
                std::string m_filePath;
                std::unique_ptr<ELFIO::elfio> m_elf;

                std::vector<Section> m_sections;
                std::vector<Symbol> m_symbols;
                std::vector<Relocation> m_relocations;
                std::vector<Function> m_extraFunctions;

                void loadSections();
                void loadSymbols();
                void loadRelocations();
                void loadDebugFunctions();
                bool isExecutableSection(const ELFIO::section *section) const;
                bool isDataSection(const ELFIO::section *section) const;
        };

} // namespace ps2recomp

#endif // PS2RECOMP_ELF_PARSER_H