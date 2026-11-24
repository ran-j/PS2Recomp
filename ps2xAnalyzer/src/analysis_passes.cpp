#include "ps2recomp/analysis_passes.h"

#include "ps2recomp/instructions.h"

#include <algorithm>
#include <cstring>
#include <optional>
#include <utility>

namespace ps2recomp
{
    bool AnalysisPasses::hasHardwareIOSignal(const std::vector<Instruction> &instructions)
    {
        for (const auto &inst : instructions)
        {
            if (inst.opcode == OPCODE_LUI)
            {
                const uint32_t upperAddr = inst.immediate << 16;
                if ((upperAddr >= 0x10000000 && upperAddr < 0x14000000) || // I/O area
                    (upperAddr >= 0x1F800000 && upperAddr < 0x1F900000))   // Scratchpad RAM
                {
                    return true;
                }
            }
        }

        return false;
    }

    bool AnalysisPasses::hasLargeComplexMMISignal(const std::vector<Instruction> &instructions,
                                                  size_t largeInstructionThreshold)
    {
        if (instructions.size() <= largeInstructionThreshold)
        {
            return false;
        }

        for (const auto &inst : instructions)
        {
            if (inst.isMMI &&
                inst.opcode == OPCODE_MMI &&
                (inst.function == MMI_MMI0 || inst.function == MMI_MMI1 ||
                 inst.function == MMI_MMI2 || inst.function == MMI_MMI3))
            {
                return true;
            }
        }

        return false;
    }

    bool AnalysisPasses::hasSelfModifyingSignal(const std::vector<Instruction> &instructions,
                                                const std::vector<Section> &sections)
    {
        for (size_t i = 0; i < instructions.size(); i++)
        {
            const auto &inst = instructions[i];
            if (!(inst.opcode == OPCODE_SW || inst.opcode == OPCODE_SH ||
                  inst.opcode == OPCODE_SB || inst.opcode == OPCODE_SQ))
            {
                continue;
            }

            uint32_t baseAddr = 0;
            for (int j = static_cast<int>(i) - 1; j >= 0 && j >= static_cast<int>(i) - 5; j--)
            {
                const auto &prevInst = instructions[static_cast<size_t>(j)];
                if (prevInst.opcode == OPCODE_LUI && prevInst.rt == inst.rs)
                {
                    baseAddr = prevInst.immediate << 16;
                    break;
                }
            }

            if (baseAddr == 0)
            {
                continue;
            }

            const uint32_t targetAddr = baseAddr + static_cast<int16_t>(inst.immediate);
            for (const auto &section : sections)
            {
                if (section.isCode &&
                    targetAddr >= section.address &&
                    targetAddr < section.address + section.size)
                {
                    return true;
                }
            }
        }

        return false;
    }

    std::vector<JumpTable> AnalysisPasses::detectJumpTables(
        const std::vector<Instruction> &instructions,
        const std::vector<Section> &sections,
        const std::function<bool(uint32_t, uint32_t &)> &readWord)
    {
        std::vector<JumpTable> jumpTables;

        auto addSignedImm16 = [](uint32_t hiPart, uint16_t imm16) -> uint32_t
        {
            return hiPart + static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(imm16)));
        };

        auto orUnsignedImm16 = [](uint32_t hiPart, uint16_t imm16) -> uint32_t
        {
            return hiPart | static_cast<uint32_t>(imm16);
        };

        auto looksLikeCodeTarget = [&sections](uint32_t addr) -> bool
        {
            if (addr == 0)
            {
                return false;
            }

            if (sections.empty())
            {
                return true;
            }

            for (const auto &section : sections)
            {
                if (!section.isCode)
                {
                    continue;
                }

                const uint32_t sectionEnd = section.address + section.size;
                if (addr >= section.address && addr < sectionEnd)
                {
                    return true;
                }
            }

            return false;
        };

        auto readJumpEntryCandidate = [&](uint32_t entryAddr, bool isLoadDouble, uint32_t &outTarget) -> bool
        {
            outTarget = 0;

            uint32_t w0 = 0;
            if (!readWord(entryAddr, w0))
            {
                return false;
            }

            if (!isLoadDouble)
            {
                outTarget = w0;
                return true;
            }

            uint32_t w1 = 0;
            if (!readWord(entryAddr + 4u, w1))
            {
                outTarget = w0;
                return true;
            }

            const bool w0Looks = looksLikeCodeTarget(w0);
            const bool w1Looks = looksLikeCodeTarget(w1);

            if (w0Looks && !w1Looks)
            {
                outTarget = w0;
                return true;
            }
            if (w1Looks && !w0Looks)
            {
                outTarget = w1;
                return true;
            }
            outTarget = w0;
            return true;
        };

        auto tryBuildTable = [&](uint32_t baseAddr, uint32_t baseReg, uint32_t numEntries, uint32_t strideBytes, bool isLoadDouble) -> std::optional<JumpTable>
        {
            JumpTable jumpTable;
            jumpTable.address = baseAddr;
            jumpTable.baseRegister = baseReg;

            uint32_t validCodeTargets = 0;
            uint32_t totalRead = 0;

            for (uint32_t e = 0; e < numEntries; e++)
            {
                const uint32_t entryAddr = baseAddr + (e * strideBytes);

                uint32_t targetAddr = 0;
                if (!readJumpEntryCandidate(entryAddr, isLoadDouble, targetAddr))
                {
                    continue;
                }

                totalRead++;

                if (looksLikeCodeTarget(targetAddr))
                {
                    validCodeTargets++;
                }

                JumpTableEntry entry;
                entry.index = e;
                entry.target = targetAddr;
                jumpTable.entries.push_back(entry);
            }

            if (jumpTable.entries.empty())
            {
                return std::nullopt;
            }

            bool ok = false;
            if (sections.empty())
            {
                ok = (totalRead >= 2);
            }
            else
            {
                ok = (validCodeTargets >= 2) &&
                     (totalRead >= 2) &&
                     (validCodeTargets * 2 >= totalRead);
            }

            if (!ok)
            {
                return std::nullopt;
            }

            return jumpTable;
        };

        for (size_t i = 0; i < instructions.size(); i++)
        {
            const auto &inst = instructions[i];

            if (inst.opcode != OPCODE_SLTIU || i + 2 >= instructions.size())
            {
                continue;
            }

            const auto &nextInst = instructions[i + 1];
            if (nextInst.opcode != OPCODE_BNE && nextInst.opcode != OPCODE_BEQ)
            {
                continue;
            }

            for (size_t j = i + 2; j < std::min(i + 10, instructions.size()); j++)
            {
                const auto &loadInst = instructions[j];
                const bool isLoadWord = (loadInst.opcode == OPCODE_LW);
                const bool isLoadDouble = (loadInst.opcode == OPCODE_LD);

                if ((!isLoadWord && !isLoadDouble) || j + 1 >= instructions.size())
                {
                    continue;
                }

                const auto &jumpInst = instructions[j + 1];
                if (jumpInst.opcode != OPCODE_SPECIAL ||
                    jumpInst.function != SPECIAL_JR ||
                    jumpInst.rs != loadInst.rt)
                {
                    continue;
                }

                const uint32_t numEntries = inst.immediate;
                if (numEntries == 0 || numEntries >= 1000)
                {
                    break;
                }

                uint32_t baseAddr = 0;
                for (int k = static_cast<int>(j) - 1; k >= static_cast<int>(i); k--)
                {
                    const auto &addrInst = instructions[static_cast<size_t>(k)];
                    if (addrInst.opcode != OPCODE_LUI)
                    {
                        continue;
                    }

                    const uint32_t hiPart = (addrInst.immediate << 16);

                    if (static_cast<size_t>(k + 1) < instructions.size())
                    {
                        const auto &offsetInst = instructions[static_cast<size_t>(k + 1)];
                        const bool isAddiuOrOri = (offsetInst.opcode == OPCODE_ADDIU || offsetInst.opcode == OPCODE_ORI);

                        if (isAddiuOrOri &&
                            offsetInst.rs == addrInst.rt &&
                            offsetInst.rt == loadInst.rs)
                        {
                            if (offsetInst.opcode == OPCODE_ADDIU)
                            {
                                baseAddr = addSignedImm16(hiPart, offsetInst.immediate);
                            }
                            else
                            {
                                baseAddr = orUnsignedImm16(hiPart, offsetInst.immediate);
                            }
                            break;
                        }
                    }

                    if (addrInst.rt == loadInst.rs)
                    {
                        baseAddr = addSignedImm16(hiPart, loadInst.immediate);
                        break;
                    }
                }

                if (baseAddr == 0)
                {
                    break;
                }

                const uint32_t preferredStride = isLoadDouble ? 8u : 4u;

                std::optional<JumpTable> table = tryBuildTable(baseAddr, loadInst.rs, numEntries, preferredStride, isLoadDouble);
                if (!table && isLoadDouble)
                {
                    table = tryBuildTable(baseAddr, loadInst.rs, numEntries, 4u, isLoadDouble);
                }

                if (table)
                {
                    jumpTables.push_back(std::move(*table));
                }

                break;
            }
        }

        return jumpTables;
    }

    std::unordered_set<std::string> AnalysisPasses::findRecursiveFunctions(
        const std::unordered_map<std::string, std::vector<std::string>> &callGraph)
    {
        std::unordered_set<std::string> nodes;
        for (const auto &[caller, callees] : callGraph)
        {
            nodes.insert(caller);
            for (const auto &callee : callees)
            {
                nodes.insert(callee);
            }
        }

        std::unordered_map<std::string, int> index;
        std::unordered_map<std::string, int> lowlink;
        std::unordered_set<std::string> onStack;
        std::vector<std::string> stack;

        index.reserve(nodes.size());
        lowlink.reserve(nodes.size());
        onStack.reserve(nodes.size());
        stack.reserve(nodes.size());

        int currentIndex = 0;
        std::vector<std::vector<std::string>> sccs;
        sccs.reserve(nodes.size());

        std::function<void(const std::string &)> strongconnect;
        strongconnect = [&](const std::string &v)
        {
            index[v] = currentIndex;
            lowlink[v] = currentIndex;
            currentIndex++;

            stack.push_back(v);
            onStack.insert(v);

            auto it = callGraph.find(v);
            if (it != callGraph.end())
            {
                for (const auto &w : it->second)
                {
                    if (!index.contains(w))
                    {
                        strongconnect(w);
                        lowlink[v] = std::min(lowlink[v], lowlink[w]);
                    }
                    else if (onStack.contains(w))
                    {
                        lowlink[v] = std::min(lowlink[v], index[w]);
                    }
                }
            }

            if (lowlink[v] == index[v])
            {
                std::vector<std::string> scc;
                while (!stack.empty())
                {
                    std::string w = stack.back();
                    stack.pop_back();
                    onStack.erase(w);
                    scc.push_back(w);
                    if (w == v)
                    {
                        break;
                    }
                }

                sccs.push_back(std::move(scc));
            }
        };

        for (const auto &name : nodes)
        {
            if (!index.contains(name))
            {
                strongconnect(name);
            }
        }

        std::unordered_set<std::string> recursive;
        for (const auto &scc : sccs)
        {
            if (scc.size() > 1)
            {
                recursive.insert(scc.begin(), scc.end());
                continue;
            }

            const std::string &name = scc[0];
            auto it = callGraph.find(name);
            if (it == callGraph.end())
            {
                continue;
            }

            if (std::find(it->second.begin(), it->second.end(), name) != it->second.end())
            {
                recursive.insert(name);
            }
        }

        return recursive;
    }

    std::vector<uint32_t> AnalysisPasses::collectDirectBranchTargets(
        const std::vector<Instruction> &instructions)
    {
        std::vector<uint32_t> targets;

        auto branchTarget = [](const Instruction &inst) -> uint32_t
        {
            return inst.address + 4u +
                   (static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(inst.immediate))) << 2);
        };

        for (const auto &inst : instructions)
        {
            switch (inst.opcode)
            {
            case OPCODE_J:
            case OPCODE_JAL:
                targets.push_back(((inst.address + 4u) & 0xF0000000u) | (inst.target << 2));
                break;
            case OPCODE_BEQ:
            case OPCODE_BNE:
            case OPCODE_BLEZ:
            case OPCODE_BGTZ:
            case OPCODE_BEQL:
            case OPCODE_BNEL:
            case OPCODE_BLEZL:
            case OPCODE_BGTZL:
                targets.push_back(branchTarget(inst));
                break;
            case OPCODE_REGIMM:
                switch (inst.rt)
                {
                case REGIMM_BLTZ:
                case REGIMM_BGEZ:
                case REGIMM_BLTZL:
                case REGIMM_BGEZL:
                    targets.push_back(branchTarget(inst));
                    break;
                default:
                    break;
                }
                break;
            default:
                break;
            }
        }

        return targets;
    }

    bool AnalysisPasses::looksLikeFunctionPrologue(uint32_t instructionWord)
    {
        switch (instructionWord >> 16)
        {
        case 0x27BDu: // addiu $sp, $sp, imm
        case 0x67BDu: // daddiu $sp, $sp, imm
        case 0xFFBFu: // sd $ra, imm($sp)
            return true;
        default:
            return false;
        }
    }

    namespace
    {
        const Section *findSectionWithData(const std::vector<Section> &sections, uint32_t address)
        {
            for (const auto &section : sections)
            {
                if (section.data == nullptr || section.size < 4u)
                {
                    continue;
                }

                if (address < section.address || address + 4u > section.address + section.size)
                {
                    continue;
                }

                return &section;
            }

            return nullptr;
        }

        bool readSectionWord(const std::vector<Section> &sections, uint32_t address, uint32_t &outWord)
        {
            const Section *section = findSectionWithData(sections, address);
            if (section == nullptr)
            {
                return false;
            }

            const size_t offset = static_cast<size_t>(address - section->address);
            std::memcpy(&outWord, section->data + offset, sizeof(outWord));
            return true;
        }
    }

    std::vector<uint32_t> AnalysisPasses::collectDataReferencedEntryCandidates(
        const std::vector<Section> &sections,
        uint32_t textStart,
        uint32_t textEnd,
        const std::unordered_set<uint32_t> &knownStarts,
        const std::unordered_set<uint32_t> &servedTargets)
    {
        std::vector<uint32_t> candidates;
        if (textStart >= textEnd)
        {
            return candidates;
        }

        for (const auto &section : sections)
        {
            if (section.data == nullptr || section.isBSS || section.size < 4u)
            {
                continue;
            }

            const size_t wordCount = section.size / 4u;
            for (size_t index = 0; index < wordCount; ++index)
            {
                uint32_t word = 0u;
                std::memcpy(&word, section.data + index * 4u, sizeof(word));

                // Only pointers into executable text matter; keep them
                // 4-aligned and skip the ones discovery already covers.
                if (word < textStart || word >= textEnd || (word & 0x3u) != 0u)
                {
                    continue;
                }

                if (knownStarts.contains(word))
                {
                    continue;
                }

                uint32_t firstInstruction = 0u;
                if (!readSectionWord(sections, word, firstInstruction))
                {
                    continue;
                }

                if (!looksLikeFunctionPrologue(firstInstruction))
                {
                    continue;
                }

                if (std::find(candidates.begin(), candidates.end(), word) == candidates.end())
                {
                    candidates.push_back(word);
                }
            }
        }

        // Second source: pointers formed in code as `lui rd, hi` +
        // `addiu/daddiu rd, rd, lo` pairs and handed straight to
        // registration syscalls (SetAlarm's handler argument, thread entry
        // arguments). These never touch data memory, so the word scan
        // cannot see them.
        for (const auto &section : sections)
        {
            if (section.data == nullptr || section.isBSS || section.size < 8u)
            {
                continue;
            }

            const size_t wordCount = section.size / 4u;
            for (size_t index = 0; index + 1u < wordCount; ++index)
            {
                uint32_t word = 0u;
                std::memcpy(&word, section.data + index * 4u, sizeof(word));
                if ((word >> 26) != 0x0Fu)
                {
                    continue; // not lui
                }

                const uint32_t rd = (word >> 16) & 0x1Fu;
                const uint32_t hi = word & 0xFFFFu;
                for (size_t j = index + 1u; j < std::min(index + 13u, wordCount); ++j)
                {
                    uint32_t lo = 0u;
                    std::memcpy(&lo, section.data + j * 4u, sizeof(lo));
                    const uint32_t loOp = lo >> 26;
                    if (loOp != 0x09u && loOp != 0x18u)
                    {
                        continue; // not addiu/daddiu
                    }
                    if (((lo >> 21) & 0x1Fu) != rd || ((lo >> 16) & 0x1Fu) != rd)
                    {
                        continue;
                    }

                    const uint32_t low = lo & 0xFFFFu;
                    const uint32_t address = ((hi << 16u) + low) & 0xFFFFFFFFu;
                    if (address < textStart || address >= textEnd || (address & 0x3u) != 0u ||
                        knownStarts.contains(address))
                    {
                        break;
                    }

                    uint32_t firstInstruction = 0u;
                    if (readSectionWord(sections, address, firstInstruction) &&
                        looksLikeFunctionPrologue(firstInstruction))
                    {
                        if (std::find(candidates.begin(), candidates.end(), address) == candidates.end())
                        {
                            candidates.push_back(address);
                        }
                    }
                    break;
                }
            }
        }

        // Third source: data-table words that point into a code section at
        // an address the runtime cannot already dispatch to — not a known
        // start, not a decoded direct branch label. These targets are
        // prologue-less continuation fragments (module dispatch tables hand
        // out entries shaped like `lw a0, 0(a1)`), so the prologue-shape
        // filter must not gate them; an indirect call reaching one without
        // a compiled entry kills the calling thread. Function spans are not
        // an oracle here: discovery sweeps them linearly, so a span also
        // covers holes the compiled dispatch table never fills. Pointer
        // sources stay restricted to data sections: code words that merely
        // look like text pointers (every `jr ra` encodes 0x03e00008, which
        // lands inside text) are not table registrations.
        auto insideCodeSection = [&sections](uint32_t address) -> bool
        {
            for (const auto &section : sections)
            {
                if (section.isCode && address >= section.address &&
                    address < section.address + section.size)
                {
                    return true;
                }
            }

            return false;
        };

        for (const auto &section : sections)
        {
            if (section.data == nullptr || section.isBSS || section.isCode || section.size < 4u)
            {
                continue;
            }

            const size_t wordCount = section.size / 4u;
            for (size_t index = 0; index < wordCount; ++index)
            {
                uint32_t word = 0u;
                std::memcpy(&word, section.data + index * 4u, sizeof(word));
                if (word < textStart || word >= textEnd || (word & 0x3u) != 0u)
                {
                    continue;
                }

                if (knownStarts.contains(word) || servedTargets.contains(word) ||
                    !insideCodeSection(word))
                {
                    continue;
                }

                uint32_t firstInstruction = 0u;
                if (!readSectionWord(sections, word, firstInstruction))
                {
                    continue;
                }

                if (std::find(candidates.begin(), candidates.end(), word) == candidates.end())
                {
                    candidates.push_back(word);
                }
            }
        }

        std::sort(candidates.begin(), candidates.end());
        return candidates;
    }
}
