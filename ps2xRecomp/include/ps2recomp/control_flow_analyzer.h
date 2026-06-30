#ifndef PS2RECOMP_CONTROL_FLOW_ANALYZER_H
#define PS2RECOMP_CONTROL_FLOW_ANALYZER_H

#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ps2recomp
{
    struct Function;
    struct Instruction;
    struct Section;
    class RecompilerReporter;

    struct ControlFlowAnalysisResult
    {
        std::unordered_set<uint32_t> entryPoints;
        std::unordered_set<uint32_t> externalEntryPoints;
        std::unordered_set<uint32_t> resumeEntryPoints;
        std::unordered_set<uint32_t> indirectFallbackEntryPoints;
        std::unordered_map<uint32_t, std::vector<uint32_t>> jumpTableTargets;
    };

    class ControlFlowAnalyzer
    {
    public:
        ControlFlowAnalyzer(const std::vector<Section> &sections,
                            const std::unordered_map<uint32_t, std::vector<uint32_t>> &configuredJumpTableTargetsByAddress,
                            RecompilerReporter *reporter);

        ControlFlowAnalysisResult analyze(const Function &function,
                                          const std::vector<Instruction> &instructions,
                                          const std::vector<Function> *allFunctions = nullptr) const;

    private:
        const std::vector<Section> &m_sections;
        const std::unordered_map<uint32_t, std::vector<uint32_t>> &m_configJumpTableTargetsByAddress;
        RecompilerReporter *m_reporter = nullptr;
    };
}

#endif // PS2RECOMP_CONTROL_FLOW_ANALYZER_H
