#include "ps2recomp/control_flow_analyzer.h"
#include "ps2recomp/types.h"
#include "ps2recomp/instructions.h"
#include "ps2recomp/control_flow_utils.h"
#include "ps2recomp/recompiler_reporter.h"

#include <algorithm>
#include <cstring>

namespace ps2recomp
{
    ControlFlowAnalyzer::ControlFlowAnalyzer(
        const std::vector<Section> &sections,
        const std::unordered_map<uint32_t, std::vector<uint32_t>> &configuredJumpTableTargetsByAddress,
        RecompilerReporter *reporter)
        : m_sections(sections),
          m_configJumpTableTargetsByAddress(configuredJumpTableTargetsByAddress),
          m_reporter(reporter)
    {
    }

    ControlFlowAnalysisResult ControlFlowAnalyzer::analyze(
        const Function &function,
        const std::vector<Instruction> &instructions,
        const std::vector<Function> *allFunctions) const
    {
        ControlFlowAnalysisResult result;
        std::unordered_set<uint32_t> instructionAddresses;
        instructionAddresses.reserve(instructions.size());
        bool hasIndirectRegisterJump = false;
        std::vector<const Instruction *> indirectJumps;

        auto isExecutableAddress = [&](uint32_t address) -> bool
        {
            for (const auto &section : m_sections)
            {
                if (!section.isCode)
                {
                    continue;
                }
                if (address >= section.address && address < (section.address + section.size))
                {
                    return true;
                }
            }
            return false;
        };

        auto findContainingExternalFunction = [&](uint32_t address) -> const Function *
        {
            if (!allFunctions || !isExecutableAddress(address))
            {
                return nullptr;
            }

            const Function *best = nullptr;
            for (const auto &candidateFn : *allFunctions)
            {
                if (!candidateFn.isRecompiled || candidateFn.isStub || candidateFn.isSkipped)
                {
                    continue;
                }

                if (candidateFn.name.rfind("entry_", 0) == 0)
                {
                    continue;
                }

                if (address < candidateFn.start || address >= candidateFn.end)
                {
                    continue;
                }

                if (!best || candidateFn.start > best->start)
                {
                    best = &candidateFn;
                }
            }

            return best;
        };

        auto queueExternalEntryTarget = [&](uint32_t target)
        {
            const Function *containingFn = findContainingExternalFunction(target);
            if (!containingFn)
            {
                return;
            }

            if (containingFn->start == function.start)
            {
                return;
            }

            if (target == containingFn->start)
            {
                return;
            }

            result.externalEntryPoints.insert(target);
        };

        auto queueResumeEntryTarget = [&](uint32_t resumeAddr)
        {
            if (resumeAddr >= function.start && resumeAddr < function.end &&
                instructionAddresses.contains(resumeAddr))
            {
                result.entryPoints.insert(resumeAddr);
                result.resumeEntryPoints.insert(resumeAddr);
            }
        };

        auto queueLoopResumeEntryTarget = [&](uint32_t target, uint32_t sourcePc)
        {
            if (target > sourcePc || target == function.start)
            {
                return;
            }

            queueResumeEntryTarget(target);
        };

        for (const auto &inst : instructions)
        {
            instructionAddresses.insert(inst.address);
            if (inst.opcode == OPCODE_SPECIAL &&
                ((inst.function == SPECIAL_JR && inst.rs != 31) ||
                 inst.function == SPECIAL_JALR))
            {
                hasIndirectRegisterJump = true;
                indirectJumps.push_back(&inst);
            }
        }

        for (const auto &inst : instructions)
        {
            bool isStaticJump = (inst.opcode == OPCODE_J || inst.opcode == OPCODE_JAL);
            if (inst.isBranch && inst.opcode != OPCODE_J && inst.opcode != OPCODE_JAL)
            {
                const int32_t offsetBytes = (static_cast<int32_t>(static_cast<int16_t>(inst.simmediate)) << 2);
                const uint32_t target = static_cast<uint32_t>(
                    static_cast<int64_t>(inst.address + 4u) + static_cast<int64_t>(offsetBytes));

                if (target >= function.start && target < function.end &&
                    instructionAddresses.contains(target))
                {
                    result.entryPoints.insert(target);
                    queueLoopResumeEntryTarget(target, inst.address);
                }
                else
                {
                    queueExternalEntryTarget(target);
                }
            }
            else if (isStaticJump)
            {
                uint32_t target = buildAbsoluteJumpTarget(inst.address, inst.target);
                if (target >= function.start && target < function.end &&
                    instructionAddresses.contains(target))
                {
                    result.entryPoints.insert(target);
                    queueLoopResumeEntryTarget(target, inst.address);

                    if (inst.opcode == OPCODE_JAL)
                    {
                        queueResumeEntryTarget(inst.address + 8u);
                    }
                }
                else
                {
                    queueExternalEntryTarget(target);

                    if (inst.opcode == OPCODE_JAL)
                    {
                        queueResumeEntryTarget(inst.address + 8u);
                    }
                }
            }
        }

        if (hasIndirectRegisterJump)
        {
            bool needsIndirectFallback = false;
            for (const Instruction *jrInst : indirectJumps)
            {
                if (jrInst->function == SPECIAL_JALR)
                {
                    queueResumeEntryTarget(jrInst->address + 8u);
                }

                bool foundTable = false;

                uint32_t jrReg = jrInst->rs;

                int lwIndex = -1;
                uint32_t baseReg = 0;
                int32_t lwOffset = 0;

                auto it = std::find_if(instructions.begin(), instructions.end(), [&](const Instruction &inst)
                                       { return inst.address == jrInst->address; });
                if (it != instructions.end())
                {
                    int jrIndex = std::distance(instructions.begin(), it);
                    for (int i = jrIndex - 1; i >= 0 && i >= jrIndex - 20; --i)
                    {
                        const auto &inst = instructions[i];
                        if ((inst.opcode == OPCODE_LW || inst.opcode == OPCODE_LWU) && inst.rt == jrReg)
                        {
                            lwIndex = i;
                            baseReg = inst.rs;
                            lwOffset = inst.simmediate;
                            break;
                        }
                    }

                    if (lwIndex != -1)
                    {
                        int adduIndex = -1;
                        uint32_t tableBaseReg = 0;
                        uint32_t indexReg = 0;
                        for (int i = lwIndex - 1; i >= 0 && i >= lwIndex - 10; --i)
                        {
                            const auto &inst = instructions[i];
                            if (inst.opcode == OPCODE_SPECIAL && inst.function == SPECIAL_ADDU && inst.rd == baseReg)
                            {
                                adduIndex = i;
                                tableBaseReg = inst.rs;
                                indexReg = inst.rt;
                                break;
                            }
                        }

                        uint32_t tableAddress = 0;
                        bool foundTableAddress = false;

                        if (adduIndex != -1)
                        {
                            for (int i = adduIndex - 1; i >= 0 && i >= adduIndex - 20; --i)
                            {
                                const auto &inst = instructions[i];
                                if (inst.opcode == OPCODE_LUI)
                                {
                                    if (inst.rt == tableBaseReg || inst.rt == indexReg)
                                    {
                                        uint32_t high = inst.immediate << 16;
                                        uint32_t low = 0;
                                        for (int j = i + 1; j < adduIndex; ++j)
                                        {
                                            const auto &lowInst = instructions[j];
                                            if (lowInst.rs == inst.rt && lowInst.rt == inst.rt)
                                            {
                                                if (lowInst.opcode == OPCODE_ADDIU)
                                                {
                                                    low = (uint32_t)lowInst.simmediate;
                                                }
                                                else if (lowInst.opcode == OPCODE_ORI)
                                                {
                                                    low = lowInst.immediate;
                                                }
                                            }
                                        }
                                        tableAddress = high + low;
                                        foundTableAddress = true;
                                        break;
                                    }
                                }
                            }
                        }

                        if (foundTableAddress)
                        {
                            tableAddress += lwOffset;

                            const auto configuredTableIt = m_configJumpTableTargetsByAddress.find(tableAddress);
                            if (configuredTableIt != m_configJumpTableTargetsByAddress.end())
                            {
                                std::vector<uint32_t> jrTargets;
                                jrTargets.reserve(configuredTableIt->second.size());
                                for (uint32_t target : configuredTableIt->second)
                                {
                                    if (target >= function.start && target < function.end &&
                                        instructionAddresses.contains(target))
                                    {
                                        jrTargets.push_back(target);
                                    }
                                    else
                                    {
                                        queueExternalEntryTarget(target);
                                    }
                                }

                                if (!jrTargets.empty())
                                {
                                    std::sort(jrTargets.begin(), jrTargets.end());
                                    jrTargets.erase(std::unique(jrTargets.begin(), jrTargets.end()), jrTargets.end());
                                    result.jumpTableTargets[jrInst->address] = jrTargets;
                                    for (uint32_t target : jrTargets)
                                    {
                                        result.entryPoints.insert(target);
                                    }
                                    foundTable = true;
                                }
                            }

                            uint32_t unshiftedIndexReg = 0;
                            for (int i = adduIndex - 1; i >= 0 && i >= adduIndex - 10; --i)
                            {
                                const auto &inst = instructions[i];
                                if (inst.opcode == OPCODE_SPECIAL && inst.function == SPECIAL_SLL && (inst.rd == tableBaseReg || inst.rd == indexReg))
                                {
                                    unshiftedIndexReg = inst.rt;
                                    break;
                                }
                            }

                            uint32_t numCases = 0;
                            if (unshiftedIndexReg != 0)
                            {
                                for (int i = adduIndex - 1; i >= 0 && i >= adduIndex - 30; --i)
                                {
                                    const auto &inst = instructions[i];
                                    if ((inst.opcode == OPCODE_SLTIU || inst.opcode == OPCODE_SLTI) && inst.rs == unshiftedIndexReg)
                                    {
                                        numCases = inst.immediate;
                                        break;
                                    }
                                }
                            }

                            if (!foundTable && numCases > 0 && numCases <= 1000)
                            {
                                const Section *rodata = nullptr;
                                for (const auto &sec : m_sections)
                                {
                                    if (tableAddress >= sec.address && tableAddress < sec.address + sec.size)
                                    {
                                        rodata = &sec;
                                        break;
                                    }
                                }

                                if (rodata && rodata->data)
                                {
                                    std::vector<uint32_t> jrTargets;
                                    bool validJumpTable = true;
                                    std::unordered_set<uint32_t> uniqueTargets;
                                    for (uint32_t i = 0; i < numCases; ++i)
                                    {
                                        uint32_t addr = tableAddress + i * 4;
                                        if (addr >= rodata->address && addr + 4 <= rodata->address + rodata->size)
                                        {
                                            uint32_t target = 0;
                                            std::memcpy(&target, rodata->data + (addr - rodata->address), 4);
                                            if (target >= function.start && target < function.end && instructionAddresses.contains(target))
                                            {
                                                if (!uniqueTargets.contains(target))
                                                {
                                                    jrTargets.push_back(target);
                                                    uniqueTargets.insert(target);
                                                }
                                            }
                                            else
                                            {
                                                queueExternalEntryTarget(target);
                                            }
                                        }
                                        else
                                        {
                                            validJumpTable = false;
                                            break;
                                        }
                                    }
                                    if (validJumpTable && !jrTargets.empty())
                                    {
                                        result.jumpTableTargets[jrInst->address] = jrTargets;
                                        for (uint32_t t : jrTargets)
                                        {
                                            result.entryPoints.insert(t);
                                        }
                                        foundTable = true;
                                    }
                                }
                            }
                        }
                    }
                }
                if (!foundTable)
                {
                    needsIndirectFallback = true;
                }
            }

            if (needsIndirectFallback)
            {
                if (m_reporter)
                {
                    std::vector<uint32_t> jumpAddresses;
                    jumpAddresses.reserve(indirectJumps.size());
                    for (const Instruction *jrInst : indirectJumps)
                    {
                        jumpAddresses.push_back(jrInst->address);
                    }
                    m_reporter->recordIndirectFallbackPromotion(function.name, jumpAddresses, instructionAddresses.size());
                }

                for (uint32_t addr : instructionAddresses)
                {
                    if (addr >= function.start && addr < function.end)
                    {
                        result.entryPoints.insert(addr);
                        // Keep labels and runtime registration for unresolved JR/JALR targets
                        // without emitting a local switch over every possible target.
                        result.indirectFallbackEntryPoints.insert(addr);
                    }
                }
            }
        }

        return result;
    }

}
