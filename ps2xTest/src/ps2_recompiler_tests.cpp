#include "MiniTest.h"
#include "ps2recomp/ps2_recompiler.h"
#include "ps2recomp/instructions.h"
#include "ps2recomp/types.h"
#include <algorithm>
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
        }); });
}
