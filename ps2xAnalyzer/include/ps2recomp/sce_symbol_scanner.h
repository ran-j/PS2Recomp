#ifndef PS2RECOMP_SCE_SYMBOL_SCANNER_H
#define PS2RECOMP_SCE_SYMBOL_SCANNER_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ps2recomp
{
    struct Section;

    struct SceSymbolMatch
    {
        uint32_t address = 0;
        uint32_t size = 0;
        std::string name;
        std::string library;
        std::string hash;
        uint32_t variantHash = 0;
    };

    class SceSymbolScanner
    {
    public:
        SceSymbolScanner();
        ~SceSymbolScanner();

        bool loadDatabase(const std::string &databasePath);
        std::vector<SceSymbolMatch> scan(const std::vector<Section> &sections) const;
        const std::string &lastError() const;

    private:
        class Impl;
        std::unique_ptr<Impl> m_impl;
    };
}

#endif // PS2RECOMP_SCE_SYMBOL_SCANNER_H
