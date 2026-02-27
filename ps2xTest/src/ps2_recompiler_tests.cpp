#include "MiniTest.h"
#include "ps2recomp/ps2_recompiler.h"
#include "ps2recomp/config_manager.h"
#include "ps2recomp/elf_parser.h"
#include "ps2recomp/instructions.h"
#include "ps2recomp/types.h"
#include <elfio/elfio.hpp>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <vector>

using namespace ps2recomp;

static Instruction makeNopLike(uint32_t address)
{
    Instruction inst{};
    inst.address = address;
    inst.opcode = OPCODE_ADDIU;
    inst.rt = 0;
    inst.raw = 0;
    return inst;
}

static Instruction makeAbsJump(uint32_t address, uint32_t target, uint32_t opcode)
{
    Instruction inst{};
    inst.address = address;
    inst.opcode = opcode;
    inst.target = (target >> 2) & 0x03FFFFFFu;
    inst.hasDelaySlot = true;
    inst.raw = (opcode << 26) | inst.target;
    return inst;
}

static Instruction makeJrRa(uint32_t address)
{
    Instruction inst{};
    inst.address = address;
    inst.opcode = OPCODE_SPECIAL;
    inst.function = SPECIAL_JR;
    inst.rs = 31;
    inst.hasDelaySlot = true;
    inst.raw = 0x03E00008u;
    return inst;
}

static Function makeFunction(const std::string &name, uint32_t start, uint32_t end)
{
    Function fn{};
    fn.name = name;
    fn.start = start;
    fn.end = end;
    fn.isRecompiled = true;
    fn.isStub = false;
    fn.isSkipped = false;
    return fn;
}

static bool writeMinimalMipsElfWithCodeAndDataFunctionSymbols(const std::filesystem::path &elfPath)
{
    ELFIO::elfio writer;
    writer.create(ELFIO::ELFCLASS32, ELFIO::ELFDATA2LSB);
    writer.set_os_abi(ELFIO::ELFOSABI_NONE);
    writer.set_type(ELFIO::ET_EXEC);
    writer.set_machine(ELFIO::EM_MIPS);
    writer.set_entry(0x00100000u);

    ELFIO::section *text = writer.sections.add(".text");
    text->set_type(ELFIO::SHT_PROGBITS);
    text->set_flags(ELFIO::SHF_ALLOC | ELFIO::SHF_EXECINSTR);
    text->set_addr_align(4);
    text->set_address(0x00100000u);
    const char textBytes[] = {0x08, 0x00, static_cast<char>(0xE0), 0x03, 0x00, 0x00, 0x00, 0x00};
    text->set_data(textBytes, sizeof(textBytes));

    ELFIO::section *data = writer.sections.add(".data");
    data->set_type(ELFIO::SHT_PROGBITS);
    data->set_flags(ELFIO::SHF_ALLOC | ELFIO::SHF_WRITE);
    data->set_addr_align(4);
    data->set_address(0x00200000u);
    const char dataBytes[] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, static_cast<char>(0x88)};
    data->set_data(dataBytes, sizeof(dataBytes));

    ELFIO::section *strtab = writer.sections.add(".strtab");
    strtab->set_type(ELFIO::SHT_STRTAB);
    strtab->set_addr_align(1);

    ELFIO::section *symtab = writer.sections.add(".symtab");
    symtab->set_type(ELFIO::SHT_SYMTAB);
    symtab->set_info(1);
    symtab->set_link(strtab->get_index());
    symtab->set_addr_align(4);
    symtab->set_entry_size(writer.get_default_entry_size(ELFIO::SHT_SYMTAB));

    ELFIO::symbol_section_accessor symbols(writer, symtab);
    ELFIO::string_section_accessor strings(strtab);
    symbols.add_symbol(strings, "", 0, 0, ELFIO::STB_LOCAL, ELFIO::STT_NOTYPE, 0, ELFIO::SHN_UNDEF);
    symbols.add_symbol(strings, "code_func", text->get_address(), text->get_size(),
                       ELFIO::STB_GLOBAL, ELFIO::STT_FUNC, 0, text->get_index());
    symbols.add_symbol(strings, "data_func", data->get_address(), data->get_size(),
                       ELFIO::STB_GLOBAL, ELFIO::STT_FUNC, 0, data->get_index());

    ELFIO::segment *textSegment = writer.segments.add();
    textSegment->set_type(ELFIO::PT_LOAD);
    textSegment->set_flags(ELFIO::PF_R | ELFIO::PF_X);
    textSegment->set_align(0x1000);
    textSegment->add_section_index(text->get_index(), text->get_addr_align());

    ELFIO::segment *dataSegment = writer.segments.add();
    dataSegment->set_type(ELFIO::PT_LOAD);
    dataSegment->set_flags(ELFIO::PF_R | ELFIO::PF_W);
    dataSegment->set_align(0x1000);
    dataSegment->add_section_index(data->get_index(), data->get_addr_align());

    return writer.save(elfPath.string());
}

void register_ps2_recompiler_tests()
{
    MiniTest::Case("PS2Recompiler", [](TestCase &tc)
                   {
        tc.Run("additional entries split at nearest discovered boundary", [](TestCase &t) {
            std::vector<Section> sections = {
                {".text", 0x1000u, 0x3000u, 0u, true, false, false, true, nullptr}
            };

            std::vector<Function> functions = {
                makeFunction("container", 0x1000u, 0x1018u),
                makeFunction("caller", 0x2000u, 0x2010u)
            };

            std::unordered_map<uint32_t, std::vector<Instruction>> decodedFunctions;
            decodedFunctions[0x1000u] = {
                makeNopLike(0x1000u),
                makeNopLike(0x1004u),
                makeNopLike(0x1008u),
                makeNopLike(0x100Cu),
                makeNopLike(0x1010u),
                makeNopLike(0x1014u)
            };
            decodedFunctions[0x2000u] = {
                makeAbsJump(0x2000u, 0x1008u, OPCODE_JAL),
                makeNopLike(0x2004u),
                makeAbsJump(0x2008u, 0x100Cu, OPCODE_J),
                makeNopLike(0x200Cu)
            };

            size_t discovered = PS2Recompiler::DiscoverAdditionalEntryPoints(
                functions, decodedFunctions, sections);

            t.Equals(discovered, static_cast<size_t>(2),
                     "expected two additional entries to be discovered");

            auto findByStart = [&](uint32_t start) -> const Function* {
                auto it = std::find_if(functions.begin(), functions.end(),
                                       [&](const Function &fn) { return fn.start == start; });
                if (it == functions.end())
                {
                    return nullptr;
                }
                return &(*it);
            };

            const Function *entry1008 = findByStart(0x1008u);
            const Function *entry100C = findByStart(0x100Cu);
            t.IsNotNull(entry1008, "entry at 0x1008 should exist");
            t.IsNotNull(entry100C, "entry at 0x100C should exist");
            if (entry1008 && entry100C)
            {
                t.Equals(entry1008->end, 0x100Cu,
                         "entry 0x1008 should end at nearest discovered start 0x100C");
                t.Equals(entry100C->end, 0x1018u,
                         "entry 0x100C should end at containing function end");
            }

            auto decoded1008It = decodedFunctions.find(0x1008u);
            auto decoded100CIt = decodedFunctions.find(0x100Cu);
            t.IsTrue(decoded1008It != decodedFunctions.end(), "decoded slice for 0x1008 should exist");
            t.IsTrue(decoded100CIt != decodedFunctions.end(), "decoded slice for 0x100C should exist");
            if (decoded1008It != decodedFunctions.end())
            {
                t.Equals(decoded1008It->second.size(), static_cast<size_t>(1),
                         "entry 0x1008 slice should stop before 0x100C");
                if (!decoded1008It->second.empty())
                {
                    t.Equals(decoded1008It->second.front().address, 0x1008u,
                             "entry 0x1008 slice should begin at 0x1008");
                }
            }
            if (decoded100CIt != decodedFunctions.end() && !decoded100CIt->second.empty())
            {
                t.Equals(decoded100CIt->second.front().address, 0x100Cu,
                         "entry 0x100C slice should begin at 0x100C");
            }
        });

        tc.Run("entry reslice trims earlier entries after late discovery", [](TestCase &t) {
            std::vector<Function> functions = {
                makeFunction("container", 0x1000u, 0x1018u),
                makeFunction("entry_1008", 0x1008u, 0x1018u),
                makeFunction("entry_100c", 0x100Cu, 0x1018u)
            };

            std::unordered_map<uint32_t, std::vector<Instruction>> decodedFunctions;
            decodedFunctions[0x1000u] = {
                makeNopLike(0x1000u),
                makeNopLike(0x1004u),
                makeNopLike(0x1008u),
                makeNopLike(0x100Cu),
                makeNopLike(0x1010u),
                makeNopLike(0x1014u)
            };
            decodedFunctions[0x1008u] = {
                makeNopLike(0x1008u),
                makeNopLike(0x100Cu),
                makeNopLike(0x1010u),
                makeNopLike(0x1014u)
            };
            decodedFunctions[0x100Cu] = {
                makeNopLike(0x100Cu),
                makeNopLike(0x1010u),
                makeNopLike(0x1014u)
            };

            size_t resliced = PS2Recompiler::ResliceEntryFunctions(functions, decodedFunctions);
            t.Equals(resliced, static_cast<size_t>(1),
                     "expected only the earlier entry to be resliced");

            auto findByStart = [&](uint32_t start) -> const Function* {
                auto it = std::find_if(functions.begin(), functions.end(),
                                       [&](const Function &fn) { return fn.start == start; });
                if (it == functions.end())
                {
                    return nullptr;
                }
                return &(*it);
            };

            const Function *entry1008 = findByStart(0x1008u);
            const Function *entry100C = findByStart(0x100Cu);
            t.IsNotNull(entry1008, "entry at 0x1008 should exist");
            t.IsNotNull(entry100C, "entry at 0x100C should exist");
            if (entry1008)
            {
                t.Equals(entry1008->end, 0x100Cu,
                         "entry 0x1008 should be trimmed to next entry start");
            }
            if (entry100C)
            {
                t.Equals(entry100C->end, 0x1018u,
                         "entry 0x100C should still end at containing end");
            }

            auto decoded1008It = decodedFunctions.find(0x1008u);
            auto decoded100CIt = decodedFunctions.find(0x100Cu);
            t.IsTrue(decoded1008It != decodedFunctions.end(), "decoded slice for 0x1008 should exist");
            t.IsTrue(decoded100CIt != decodedFunctions.end(), "decoded slice for 0x100C should exist");
            if (decoded1008It != decodedFunctions.end())
            {
                t.Equals(decoded1008It->second.size(), static_cast<size_t>(1),
                         "entry 0x1008 slice should stop before 0x100C");
                if (!decoded1008It->second.empty())
                {
                    t.Equals(decoded1008It->second.front().address, 0x1008u,
                             "entry 0x1008 slice should begin at 0x1008");
                }
            }
            if (decoded100CIt != decodedFunctions.end())
            {
                t.Equals(decoded100CIt->second.size(), static_cast<size_t>(3),
                         "entry 0x100C slice should keep remaining instructions");
            }
        });

        tc.Run("entry reslice handles entries without containing function", [](TestCase &t) {
            std::vector<Function> functions = {
                makeFunction("entry_1008", 0x1008u, 0x1018u),
                makeFunction("entry_100c", 0x100Cu, 0x1018u)
            };

            std::unordered_map<uint32_t, std::vector<Instruction>> decodedFunctions;
            decodedFunctions[0x1008u] = {
                makeNopLike(0x1008u),
                makeNopLike(0x100Cu),
                makeNopLike(0x1010u),
                makeNopLike(0x1014u)
            };
            decodedFunctions[0x100Cu] = {
                makeNopLike(0x100Cu),
                makeNopLike(0x1010u),
                makeNopLike(0x1014u)
            };

            size_t resliced = PS2Recompiler::ResliceEntryFunctions(functions, decodedFunctions);
            t.Equals(resliced, static_cast<size_t>(1),
                     "expected only the earlier entry to be resliced");

            auto findByStart = [&](uint32_t start) -> const Function* {
                auto it = std::find_if(functions.begin(), functions.end(),
                                       [&](const Function &fn) { return fn.start == start; });
                if (it == functions.end())
                {
                    return nullptr;
                }
                return &(*it);
            };

            const Function *entry1008 = findByStart(0x1008u);
            const Function *entry100C = findByStart(0x100Cu);
            t.IsNotNull(entry1008, "entry at 0x1008 should exist");
            t.IsNotNull(entry100C, "entry at 0x100C should exist");
            if (entry1008)
            {
                t.Equals(entry1008->end, 0x100Cu,
                         "entry 0x1008 should be trimmed to next entry start");
            }
            if (entry100C)
            {
                t.Equals(entry100C->end, 0x1018u,
                         "entry 0x100C should keep original end");
            }

            auto decoded1008It = decodedFunctions.find(0x1008u);
            auto decoded100CIt = decodedFunctions.find(0x100Cu);
            t.IsTrue(decoded1008It != decodedFunctions.end(), "decoded slice for 0x1008 should exist");
            t.IsTrue(decoded100CIt != decodedFunctions.end(), "decoded slice for 0x100C should exist");
            if (decoded1008It != decodedFunctions.end())
            {
                t.Equals(decoded1008It->second.size(), static_cast<size_t>(1),
                         "entry 0x1008 slice should stop before 0x100C");
            }
            if (decoded100CIt != decodedFunctions.end())
            {
                t.Equals(decoded100CIt->second.size(), static_cast<size_t>(3),
                         "entry 0x100C slice should keep remaining instructions");
            }
        });

        tc.Run("non-executable section targets are ignored", [](TestCase &t) {
            std::vector<Section> sections = {
                {".text", 0x1000u, 0x2000u, 0u, true, false, false, true, nullptr},
                {".data", 0x3000u, 0x1000u, 0u, false, true, false, false, nullptr}
            };

            std::vector<Function> functions = {
                makeFunction("data_container", 0x3000u, 0x3010u),
                makeFunction("caller", 0x1800u, 0x1810u)
            };

            std::unordered_map<uint32_t, std::vector<Instruction>> decodedFunctions;
            decodedFunctions[0x3000u] = {
                makeNopLike(0x3000u),
                makeNopLike(0x3004u),
                makeNopLike(0x3008u),
                makeNopLike(0x300Cu)
            };
            decodedFunctions[0x1800u] = {
                makeAbsJump(0x1800u, 0x3004u, OPCODE_J),
                makeNopLike(0x1804u)
            };

            size_t discovered = PS2Recompiler::DiscoverAdditionalEntryPoints(
                functions, decodedFunctions, sections);
            t.Equals(discovered, static_cast<size_t>(0),
                     "non-executable targets should not produce additional entries");

            const bool hasDataEntry = std::any_of(functions.begin(), functions.end(),
                                                  [](const Function &fn) { return fn.start == 0x3004u; });
            t.IsFalse(hasDataEntry, "target in data section must not produce entry wrapper");
        });

        tc.Run("entry starting at jr ra is capped to return thunk", [](TestCase &t) {
            std::vector<Section> sections = {
                {".text", 0x1000u, 0x2000u, 0u, true, false, false, true, nullptr}
            };

            std::vector<Function> functions = {
                makeFunction("container", 0x1000u, 0x1200u),
                makeFunction("caller", 0x1300u, 0x1310u)
            };

            std::unordered_map<uint32_t, std::vector<Instruction>> decodedFunctions;
            decodedFunctions[0x1000u] = {
                makeNopLike(0x1000u),
                makeNopLike(0x1004u),
                makeNopLike(0x1008u),
                makeJrRa(0x10A0u),
                makeNopLike(0x10A4u),
                makeNopLike(0x10A8u),
                makeNopLike(0x10ACu)
            };
            decodedFunctions[0x1300u] = {
                makeAbsJump(0x1300u, 0x10A0u, OPCODE_J),
                makeNopLike(0x1304u)
            };

            size_t discovered = PS2Recompiler::DiscoverAdditionalEntryPoints(
                functions, decodedFunctions, sections);
            t.Equals(discovered, static_cast<size_t>(1),
                     "expected one additional entry from cross-function jump");

            auto entryIt = std::find_if(functions.begin(), functions.end(),
                                        [](const Function &fn) { return fn.start == 0x10A0u; });
            t.IsTrue(entryIt != functions.end(), "entry wrapper at 0x10A0 should exist");
            if (entryIt != functions.end())
            {
                t.Equals(entryIt->end, 0x10A8u,
                         "jr ra entry should end after delay slot, not at container end");
            }

            auto decodedEntryIt = decodedFunctions.find(0x10A0u);
            t.IsTrue(decodedEntryIt != decodedFunctions.end(),
                     "decoded entry slice for 0x10A0 should exist");
            if (decodedEntryIt != decodedFunctions.end())
            {
                t.Equals(decodedEntryIt->second.size(), static_cast<size_t>(2),
                         "jr ra entry slice should contain exactly jr+delay");
                if (!decodedEntryIt->second.empty())
                {
                    t.Equals(decodedEntryIt->second.front().address, 0x10A0u,
                             "entry slice should start at 0x10A0");
                }
            }
        });

        tc.Run("config manager parses jump_tables table entries", [](TestCase &t) {
            const auto uniqueSuffix = std::to_string(
                static_cast<unsigned long long>(std::chrono::steady_clock::now().time_since_epoch().count()));
            const std::filesystem::path configPath =
                std::filesystem::temp_directory_path() / ("ps2recomp-jump-table-" + uniqueSuffix + ".toml");

            std::ofstream configFile(configPath);
            t.IsTrue(static_cast<bool>(configFile), "temp config file should be writable");
            if (!configFile)
            {
                return;
            }

            configFile << "[general]\n";
            configFile << "input = \"dummy.elf\"\n";
            configFile << "output = \"out\"\n\n";
            configFile << "[jump_tables]\n";
            configFile << "[[jump_tables.table]]\n";
            configFile << "address = \"0x200000\"\n";
            configFile << "base_register = 9\n";
            configFile << "entries = [\n";
            configFile << "  { index = 0, target = \"0x1620\" },\n";
            configFile << "  { index = 1, target = \"0x1630\" },\n";
            configFile << "]\n";
            configFile.close();

            ConfigManager manager(configPath.string());
            RecompilerConfig config = manager.loadConfig();

            t.Equals(config.jumpTables.size(), static_cast<size_t>(1),
                     "one configured jump table should be loaded");
            if (!config.jumpTables.empty())
            {
                const JumpTable &table = config.jumpTables.front();
                t.Equals(table.address, 0x200000u, "table address should parse from hex string");
                t.Equals(table.baseRegister, 9u, "base register should parse");
                t.Equals(table.entries.size(), static_cast<size_t>(2),
                         "two jump table entries should parse");
                if (table.entries.size() >= 2)
                {
                    t.Equals(table.entries[0].index, 0u, "first entry index should parse");
                    t.Equals(table.entries[0].target, 0x1620u, "first entry target should parse");
                    t.Equals(table.entries[1].index, 1u, "second entry index should parse");
                    t.Equals(table.entries[1].target, 0x1630u, "second entry target should parse");
                }
            }

            std::error_code removeError;
            std::filesystem::remove(configPath, removeError);
        });

        tc.Run("elf parser ignores STT_FUNC symbols in non-executable sections", [](TestCase &t) {
            const auto uniqueSuffix = std::to_string(
                static_cast<unsigned long long>(std::chrono::steady_clock::now().time_since_epoch().count()));
            const std::filesystem::path elfPath =
                std::filesystem::temp_directory_path() / ("ps2recomp-parser-" + uniqueSuffix + ".elf");

            const bool writeOk = writeMinimalMipsElfWithCodeAndDataFunctionSymbols(elfPath);
            t.IsTrue(writeOk, "temporary ELF should be generated");
            if (!writeOk)
            {
                return;
            }

            ElfParser parser(elfPath.string());
            const bool parseOk = parser.parse();
            t.IsTrue(parseOk, "generated ELF should parse");
            if (!parseOk)
            {
                std::error_code removeError;
                std::filesystem::remove(elfPath, removeError);
                return;
            }

            const auto functions = parser.extractFunctions();
            const bool hasCodeFunction = std::any_of(functions.begin(), functions.end(),
                                                     [](const Function &fn)
                                                     { return fn.start == 0x00100000u; });
            const bool hasDataFunction = std::any_of(functions.begin(), functions.end(),
                                                     [](const Function &fn)
                                                     { return fn.start == 0x00200000u; });

            t.IsTrue(hasCodeFunction, "function in executable section should be retained");
            t.IsFalse(hasDataFunction, "STT_FUNC symbol in .data must be ignored");

            std::error_code removeError;
            std::filesystem::remove(elfPath, removeError);
        });

        tc.Run("respect max length for .cpp filenames", [](TestCase& t) {
            
            t.IsTrue(PS2Recompiler::ClampFilenameLength("ReallyLongFunctionNameReallyLongFunctionNameReallyLongFunctionName_0x12345678",".cpp",50).length() <= 50,"Function name must be max 50 characters");

            t.IsTrue(PS2Recompiler::ClampFilenameLength("ReallyLongFunctionNameReallyLongFunctionNameReallyLongFunctionName_0x12345678", ".cpp", 50).rfind("0x12345678") != std::string::npos, "Function name must mantain the function address at the end, if present");
            
        });
    });
}
