#include "ps2recomp/sce_symbol_scanner.h"
#include "ps2recomp/types.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace fs = std::filesystem;

namespace ps2recomp
{
    namespace
    {
        enum class RelocationType
        {
            None,
            Mips26,
            MipsLo16,
            MipsHi16,
            Mips32,
            MipsGpRel16,
            MipsLiteral,
        };

        struct MatchSymbolKey
        {
            std::string library;
            std::string name;
            std::string hash;
            uint32_t variantHash = 0;
        };

        struct RelocationRecord
        {
            uint32_t offset = 0;
            RelocationType type = RelocationType::None;
        };

        struct SymbolRecord
        {
            std::string library;
            std::string name;
            std::string hashText;
            std::array<uint8_t, 20> hash = {};
            uint32_t variantHash = 0;
            uint32_t size = 0;
            bool isFunction = false;
            std::vector<RelocationRecord> relocations;

            size_t staticBitCount() const
            {
                size_t relocatedStaticBits = 0;
                for (const auto &relocation : relocations)
                {
                    switch (relocation.type)
                    {
                    case RelocationType::None:
                        relocatedStaticBits += 32;
                        break;
                    case RelocationType::Mips26:
                        relocatedStaticBits += 6;
                        break;
                    case RelocationType::MipsLo16:
                    case RelocationType::MipsHi16:
                    case RelocationType::MipsGpRel16:
                    case RelocationType::MipsLiteral:
                        relocatedStaticBits += 16;
                        break;
                    case RelocationType::Mips32:
                        break;
                    }
                }

                const size_t totalBits = static_cast<size_t>(size) * 8;
                if (relocatedStaticBits >= totalBits)
                {
                    return 0;
                }
                return totalBits - relocatedStaticBits;
            }
        };

        struct MatchNode;

        struct MatchEdge
        {
            uint32_t value = 0;
            RelocationType relocationType = RelocationType::None;
            std::unique_ptr<MatchNode> child;
        };

        struct MatchNode
        {
            uint32_t offset = 0;
            std::vector<MatchEdge> next;
            std::vector<MatchSymbolKey> symbols;
        };

        struct Candidate
        {
            const SymbolRecord *symbol = nullptr;
            uint32_t address = 0;
            uint32_t actualSize = 0;
        };

        static std::string toUpperAscii(std::string value)
        {
            for (char &ch : value)
            {
                ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            }
            return value;
        }

        static RelocationType parseRelocationType(const std::string &value)
        {
            const std::string upper = toUpperAscii(value);
            if (upper == "NONE")
            {
                return RelocationType::None;
            }
            if (upper == "MIPS_26" || upper == "MIPS26")
            {
                return RelocationType::Mips26;
            }
            if (upper == "LO16" || upper == "MIPS_LO16" || upper == "MIPSLO16")
            {
                return RelocationType::MipsLo16;
            }
            if (upper == "HI16" || upper == "MIPS_HI16" || upper == "MIPSHI16")
            {
                return RelocationType::MipsHi16;
            }
            if (upper == "MIPS_32" || upper == "MIPS32")
            {
                return RelocationType::Mips32;
            }
            if (upper == "MIPS_GPREL16" || upper == "MIPSGPREL16")
            {
                return RelocationType::MipsGpRel16;
            }
            if (upper == "MIPS_LITERAL" || upper == "MIPSLITERAL")
            {
                return RelocationType::MipsLiteral;
            }
            return RelocationType::None;
        }

        static uint32_t relocationMask(RelocationType type)
        {
            switch (type)
            {
            case RelocationType::None:
                return 0xFFFFFFFFu;
            case RelocationType::Mips26:
                return 0xFC000000u;
            case RelocationType::MipsLo16:
            case RelocationType::MipsHi16:
            case RelocationType::MipsGpRel16:
            case RelocationType::MipsLiteral:
                return 0xFFFF0000u;
            case RelocationType::Mips32:
                return 0u;
            }
            return 0xFFFFFFFFu;
        }

        static uint32_t readLe32(const uint8_t *data)
        {
            return static_cast<uint32_t>(data[0]) |
                   (static_cast<uint32_t>(data[1]) << 8) |
                   (static_cast<uint32_t>(data[2]) << 16) |
                   (static_cast<uint32_t>(data[3]) << 24);
        }

        static void writeLe32(uint8_t *data, uint32_t value)
        {
            data[0] = static_cast<uint8_t>(value & 0xFFu);
            data[1] = static_cast<uint8_t>((value >> 8) & 0xFFu);
            data[2] = static_cast<uint8_t>((value >> 16) & 0xFFu);
            data[3] = static_cast<uint8_t>((value >> 24) & 0xFFu);
        }

        static uint32_t disabledRelocationValue(RelocationType type, uint32_t value)
        {
            switch (type)
            {
            case RelocationType::None:
                return value;
            case RelocationType::Mips26:
                return value & 0xFC000000u;
            case RelocationType::MipsLo16:
            case RelocationType::MipsHi16:
            case RelocationType::MipsGpRel16:
            case RelocationType::MipsLiteral:
                return value & 0xFFFF0000u;
            case RelocationType::Mips32:
                return 0u;
            }
            return value;
        }

        static std::string toHex8(uint32_t value)
        {
            std::ostringstream stream;
            stream << std::hex;
            stream.width(8);
            stream.fill('0');
            stream << value;
            return stream.str();
        }

        static std::string makeSymbolKey(const std::string &library,
                                         const std::string &name,
                                         const std::string &hash,
                                         uint32_t variantHash)
        {
            return library + '\n' + name + '\n' + hash + '\n' + toHex8(variantHash);
        }

        static std::string makeSymbolKey(const SymbolRecord &symbol)
        {
            return makeSymbolKey(symbol.library, symbol.name, symbol.hashText, symbol.variantHash);
        }

        static uint8_t hexNibble(char ch)
        {
            if (ch >= '0' && ch <= '9')
            {
                return static_cast<uint8_t>(ch - '0');
            }
            if (ch >= 'a' && ch <= 'f')
            {
                return static_cast<uint8_t>(10 + ch - 'a');
            }
            if (ch >= 'A' && ch <= 'F')
            {
                return static_cast<uint8_t>(10 + ch - 'A');
            }
            throw std::runtime_error("invalid hex digit");
        }

        static std::array<uint8_t, 20> parseSha1(const std::string &hex)
        {
            if (hex.size() != 40)
            {
                throw std::runtime_error("invalid SHA-1 length");
            }

            std::array<uint8_t, 20> bytes = {};
            for (size_t i = 0; i < bytes.size(); ++i)
            {
                bytes[i] = static_cast<uint8_t>((hexNibble(hex[i * 2]) << 4) |
                                                hexNibble(hex[i * 2 + 1]));
            }
            return bytes;
        }

        static uint32_t rotateLeft(uint32_t value, uint32_t bits)
        {
            return (value << bits) | (value >> (32 - bits));
        }

        static std::array<uint8_t, 20> sha1(const std::vector<uint8_t> &data)
        {
            std::vector<uint8_t> message = data;
            const uint64_t bitLength = static_cast<uint64_t>(message.size()) * 8u;

            message.push_back(0x80u);
            while ((message.size() % 64) != 56)
            {
                message.push_back(0u);
            }

            for (int shift = 56; shift >= 0; shift -= 8)
            {
                message.push_back(static_cast<uint8_t>((bitLength >> shift) & 0xFFu));
            }

            uint32_t h0 = 0x67452301u;
            uint32_t h1 = 0xEFCDAB89u;
            uint32_t h2 = 0x98BADCFEu;
            uint32_t h3 = 0x10325476u;
            uint32_t h4 = 0xC3D2E1F0u;

            for (size_t chunk = 0; chunk < message.size(); chunk += 64)
            {
                std::array<uint32_t, 80> w = {};
                for (size_t i = 0; i < 16; ++i)
                {
                    const size_t base = chunk + i * 4;
                    w[i] = (static_cast<uint32_t>(message[base]) << 24) |
                           (static_cast<uint32_t>(message[base + 1]) << 16) |
                           (static_cast<uint32_t>(message[base + 2]) << 8) |
                           static_cast<uint32_t>(message[base + 3]);
                }
                for (size_t i = 16; i < 80; ++i)
                {
                    w[i] = rotateLeft(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
                }

                uint32_t a = h0;
                uint32_t b = h1;
                uint32_t c = h2;
                uint32_t d = h3;
                uint32_t e = h4;

                for (size_t i = 0; i < 80; ++i)
                {
                    uint32_t f = 0;
                    uint32_t k = 0;
                    if (i < 20)
                    {
                        f = (b & c) | ((~b) & d);
                        k = 0x5A827999u;
                    }
                    else if (i < 40)
                    {
                        f = b ^ c ^ d;
                        k = 0x6ED9EBA1u;
                    }
                    else if (i < 60)
                    {
                        f = (b & c) | (b & d) | (c & d);
                        k = 0x8F1BBCDCu;
                    }
                    else
                    {
                        f = b ^ c ^ d;
                        k = 0xCA62C1D6u;
                    }

                    const uint32_t temp = rotateLeft(a, 5) + f + e + k + w[i];
                    e = d;
                    d = c;
                    c = rotateLeft(b, 30);
                    b = a;
                    a = temp;
                }

                h0 += a;
                h1 += b;
                h2 += c;
                h3 += d;
                h4 += e;
            }

            const std::array<uint32_t, 5> words = {h0, h1, h2, h3, h4};
            std::array<uint8_t, 20> digest = {};
            for (size_t i = 0; i < words.size(); ++i)
            {
                digest[i * 4] = static_cast<uint8_t>((words[i] >> 24) & 0xFFu);
                digest[i * 4 + 1] = static_cast<uint8_t>((words[i] >> 16) & 0xFFu);
                digest[i * 4 + 2] = static_cast<uint8_t>((words[i] >> 8) & 0xFFu);
                digest[i * 4 + 3] = static_cast<uint8_t>(words[i] & 0xFFu);
            }
            return digest;
        }

        static fs::path resolveDatabasePath(const fs::path &inputPath)
        {
            if (fs::exists(inputPath / "symbols.json") && fs::exists(inputPath / "tree.json"))
            {
                return inputPath;
            }

            const fs::path resourcePath = inputPath / "symboldb" / "app" / "src" / "main" / "resources";
            if (fs::exists(resourcePath / "symbols.json") && fs::exists(resourcePath / "tree.json"))
            {
                return resourcePath;
            }

            return inputPath;
        }
    }

    class SceSymbolScanner::Impl
    {
    public:
        bool loadDatabase(const std::string &databasePath)
        {
            m_lastError.clear();
            m_symbols.clear();
            m_root.reset();

            try
            {
                const fs::path resolvedPath = resolveDatabasePath(databasePath);
                loadSymbols(resolvedPath / "symbols.json");
                loadTree(resolvedPath / "tree.json");
                return true;
            }
            catch (const std::exception &e)
            {
                m_lastError = e.what();
                m_symbols.clear();
                m_root.reset();
                return false;
            }
        }

        std::vector<SceSymbolMatch> scan(const std::vector<Section> &sections) const
        {
            std::unordered_map<uint32_t, std::map<std::string, Candidate>> candidatesByAddress;

            if (!m_root)
            {
                return {};
            }

            for (const Section &section : sections)
            {
                if (!section.isCode || section.data == nullptr || section.size < 4)
                {
                    continue;
                }

                for (uint32_t offset = 0; offset + 4 <= section.size; offset += 4)
                {
                    const std::vector<const SymbolRecord *> symbols = findCandidateSymbols(section, offset);
                    if (symbols.empty())
                    {
                        continue;
                    }

                    for (const SymbolRecord *symbol : symbols)
                    {
                        if (symbol == nullptr || !symbol->isFunction || symbol->size == 0)
                        {
                            continue;
                        }

                        if (offset > section.size || symbol->size > section.size - offset)
                        {
                            continue;
                        }

                        if (!matchesSymbol(section, offset, *symbol))
                        {
                            continue;
                        }

                        uint32_t actualSize = symbol->size;
                        while (actualSize <= section.size - offset - 4 &&
                               readLe32(section.data + offset + actualSize) == 0)
                        {
                            actualSize += 4;
                        }

                        Candidate candidate;
                        candidate.symbol = symbol;
                        candidate.address = section.address + offset;
                        candidate.actualSize = actualSize;
                        candidatesByAddress[candidate.address][makeSymbolKey(*symbol)] = candidate;
                    }
                }
            }

            return resolveCandidates(candidatesByAddress);
        }

        const std::string &lastError() const
        {
            return m_lastError;
        }

    private:
        std::unordered_map<std::string, SymbolRecord> m_symbols;
        std::unique_ptr<MatchNode> m_root;
        std::string m_lastError;

        void loadSymbols(const fs::path &path)
        {
            std::ifstream file(path);
            if (!file)
            {
                throw std::runtime_error("unable to open " + path.string());
            }

            const nlohmann::json root = nlohmann::json::parse(file);
            for (auto libraryIt = root.begin(); libraryIt != root.end(); ++libraryIt)
            {
                const std::string library = libraryIt.key();
                for (auto nameIt = libraryIt.value().begin(); nameIt != libraryIt.value().end(); ++nameIt)
                {
                    const std::string name = nameIt.key();
                    for (auto hashIt = nameIt.value().begin(); hashIt != nameIt.value().end(); ++hashIt)
                    {
                        const std::string hash = hashIt.key();
                        for (auto variantIt = hashIt.value().begin(); variantIt != hashIt.value().end(); ++variantIt)
                        {
                            SymbolRecord symbol;
                            symbol.library = library;
                            symbol.name = name;
                            symbol.hashText = hash;
                            symbol.hash = parseSha1(hash);
                            symbol.variantHash = static_cast<uint32_t>(std::stoul(variantIt.key(), nullptr, 16));

                            const nlohmann::json &jsonSymbol = variantIt.value();
                            symbol.size = jsonSymbol.value("size", 0u);

                            const std::string type = toUpperAscii(jsonSymbol.value("type", std::string()));
                            symbol.isFunction = (type == "FUNCTION" || type == "FUNC");

                            const nlohmann::json relocations =
                                jsonSymbol.value("relocations", nlohmann::json::object());
                            for (auto relocationIt = relocations.begin(); relocationIt != relocations.end(); ++relocationIt)
                            {
                                RelocationRecord relocation;
                                relocation.offset = static_cast<uint32_t>(std::stoul(relocationIt.key(), nullptr, 0));
                                relocation.type = parseRelocationType(relocationIt.value().value("type", std::string("none")));
                                symbol.relocations.push_back(relocation);
                            }

                            m_symbols[makeSymbolKey(symbol)] = std::move(symbol);
                        }
                    }
                }
            }
        }

        void loadTree(const fs::path &path)
        {
            std::ifstream file(path);
            if (!file)
            {
                throw std::runtime_error("unable to open " + path.string());
            }

            const nlohmann::json root = nlohmann::json::parse(file);
            m_root = parseNode(root);
        }

        std::unique_ptr<MatchNode> parseNode(const nlohmann::json &jsonNode) const
        {
            auto node = std::make_unique<MatchNode>();
            node->offset = jsonNode.value("offset", 0u);

            if (jsonNode.contains("symbols"))
            {
                for (const nlohmann::json &jsonSymbol : jsonNode["symbols"])
                {
                    MatchSymbolKey symbol;
                    symbol.library = jsonSymbol.value("library", std::string());
                    symbol.name = jsonSymbol.value("name", std::string());
                    symbol.hash = jsonSymbol.value("hash", std::string());
                    symbol.variantHash = jsonSymbol.value("variant", 0u);
                    node->symbols.push_back(std::move(symbol));
                }
            }

            if (jsonNode.contains("next"))
            {
                for (const nlohmann::json &jsonEdge : jsonNode["next"])
                {
                    MatchEdge edge;
                    const nlohmann::json &match = jsonEdge["match"];
                    edge.value = match.value("value", 0u);
                    if (match.contains("relocation") && match["relocation"].contains("type"))
                    {
                        edge.relocationType = parseRelocationType(match["relocation"].value("type", std::string("none")));
                    }
                    edge.child = parseNode(jsonEdge["child"]);
                    node->next.push_back(std::move(edge));
                }
            }

            return node;
        }

        const SymbolRecord *findSymbol(const MatchSymbolKey &key) const
        {
            const auto it = m_symbols.find(makeSymbolKey(key.library, key.name, key.hash, key.variantHash));
            if (it == m_symbols.end())
            {
                return nullptr;
            }
            return &it->second;
        }

        std::vector<const SymbolRecord *> findCandidateSymbols(const Section &section, uint32_t offset) const
        {
            std::vector<const SymbolRecord *> symbols;
            std::vector<const MatchNode *> stack;
            stack.push_back(m_root.get());

            while (!stack.empty())
            {
                const MatchNode *node = stack.back();
                stack.pop_back();

                if (node == nullptr || node->offset > section.size || offset > section.size - node->offset)
                {
                    continue;
                }
                if (section.size - offset - node->offset < 4)
                {
                    continue;
                }

                const uint32_t value = readLe32(section.data + offset + node->offset);
                for (const MatchEdge &edge : node->next)
                {
                    const uint32_t mask = relocationMask(edge.relocationType);
                    if ((value & mask) != (edge.value & mask))
                    {
                        continue;
                    }

                    for (const MatchSymbolKey &key : edge.child->symbols)
                    {
                        if (const SymbolRecord *symbol = findSymbol(key))
                        {
                            symbols.push_back(symbol);
                        }
                    }

                    if (!edge.child->next.empty())
                    {
                        stack.push_back(edge.child.get());
                    }
                }
            }

            return symbols;
        }

        bool matchesSymbol(const Section &section, uint32_t offset, const SymbolRecord &symbol) const
        {
            std::vector<uint8_t> bytes(section.data + offset, section.data + offset + symbol.size);
            for (const RelocationRecord &relocation : symbol.relocations)
            {
                if (relocation.offset > bytes.size() || bytes.size() - relocation.offset < 4)
                {
                    continue;
                }

                const uint32_t value = readLe32(bytes.data() + relocation.offset);
                writeLe32(bytes.data() + relocation.offset,
                          disabledRelocationValue(relocation.type, value));
            }

            return sha1(bytes) == symbol.hash;
        }

        std::vector<SceSymbolMatch> resolveCandidates(
            const std::unordered_map<uint32_t, std::map<std::string, Candidate>> &candidatesByAddress) const
        {
            std::vector<SceSymbolMatch> matches;
            matches.reserve(candidatesByAddress.size());

            for (const auto &[address, candidatesByKey] : candidatesByAddress)
            {
                std::vector<const Candidate *> viable;
                viable.reserve(candidatesByKey.size());
                for (const auto &[_, candidate] : candidatesByKey)
                {
                    if (candidate.symbol != nullptr && candidate.symbol->staticBitCount() >= 256)
                    {
                        viable.push_back(&candidate);
                    }
                }

                if (viable.empty())
                {
                    continue;
                }

                // The upstream scanner also uses dependency and adjacent-library context.
                // This analyzer integration keeps only unambiguous direct hash matches for now.
                std::set<std::string> identities;
                for (const Candidate *candidate : viable)
                {
                    identities.insert(candidate->symbol->library + '\n' + candidate->symbol->name);
                }
                if (identities.size() != 1)
                {
                    continue;
                }

                const Candidate *best = *std::max_element(
                    viable.begin(),
                    viable.end(),
                    [](const Candidate *lhs, const Candidate *rhs)
                    {
                        if (lhs->actualSize != rhs->actualSize)
                        {
                            return lhs->actualSize < rhs->actualSize;
                        }
                        return lhs->symbol->staticBitCount() < rhs->symbol->staticBitCount();
                    });

                SceSymbolMatch match;
                match.address = address;
                match.size = best->actualSize;
                match.name = best->symbol->name;
                match.library = best->symbol->library;
                match.hash = best->symbol->hashText;
                match.variantHash = best->symbol->variantHash;
                matches.push_back(std::move(match));
            }

            std::sort(matches.begin(), matches.end(),
                      [](const SceSymbolMatch &a, const SceSymbolMatch &b)
                      {
                          return a.address < b.address;
                      });
            return matches;
        }
    };

    SceSymbolScanner::SceSymbolScanner()
        : m_impl(std::make_unique<Impl>())
    {
    }

    SceSymbolScanner::~SceSymbolScanner() = default;

    bool SceSymbolScanner::loadDatabase(const std::string &databasePath)
    {
        return m_impl->loadDatabase(databasePath);
    }

    std::vector<SceSymbolMatch> SceSymbolScanner::scan(const std::vector<Section> &sections) const
    {
        return m_impl->scan(sections);
    }

    const std::string &SceSymbolScanner::lastError() const
    {
        return m_impl->lastError();
    }
}
