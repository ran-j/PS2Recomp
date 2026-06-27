#include "ps2recomp/recompiler_reporter.h"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace ps2recomp
{
    namespace
    {
        const char *severityName(RecompilerReporter::Severity severity)
        {
            switch (severity)
            {
            case RecompilerReporter::Severity::Info:
                return "info";
            case RecompilerReporter::Severity::Warning:
                return "warning";
            case RecompilerReporter::Severity::Error:
                return "error";
            }
            return "unknown";
        }

        std::string hexAddress(uint32_t address)
        {
            std::ostringstream ss;
            ss << "0x" << std::hex << address;
            return ss.str();
        }
    }

    void RecompilerReporter::progress(const std::string &message)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::cout << "[recompiler] " << message << std::endl;
    }

    void RecompilerReporter::info(const std::string &category, const std::string &message)
    {
        addEvent(Severity::Info, category, message);
    }

    void RecompilerReporter::warning(const std::string &category, const std::string &message)
    {
        addEvent(Severity::Warning, category, message);
    }

    void RecompilerReporter::error(const std::string &category, const std::string &message)
    {
        addEvent(Severity::Error, category, message);
    }

    void RecompilerReporter::warningAt(const std::string &category, const std::string &functionName, uint32_t address, const std::string &message)
    {
        addEvent(Severity::Warning, category, message, functionName, address, true);
    }

    void RecompilerReporter::errorAt(const std::string &category, const std::string &functionName, uint32_t address, const std::string &message)
    {
        addEvent(Severity::Error, category, message, functionName, address, true);
    }

    void RecompilerReporter::recordDiscovered(size_t functions, size_t symbols, size_t sections, size_t relocations)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_counters.functionsDiscovered = functions;
        m_counters.symbolsDiscovered = symbols;
        m_counters.sectionsDiscovered = sections;
        m_counters.relocationsDiscovered = relocations;
    }

    void RecompilerReporter::recordFunctionProcessed()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        ++m_counters.functionsProcessed;
    }

    void RecompilerReporter::recordFunctionRecompiled()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        ++m_counters.functionsRecompiled;
    }

    void RecompilerReporter::recordFunctionStubbed()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        ++m_counters.functionsStubbed;
    }

    void RecompilerReporter::recordFunctionSkipped()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        ++m_counters.functionsSkipped;
    }

    void RecompilerReporter::recordDecodeFailure()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        ++m_counters.decodeFailures;
    }

    void RecompilerReporter::recordAdditionalEntryPoints(size_t count)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_counters.additionalEntryPoints += count;
    }

    void RecompilerReporter::recordGeneratedFunctions(size_t count)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_counters.generatedFunctions += count;
    }

    void RecompilerReporter::recordIndirectFallbackPromotion(const std::string &functionName,
                                                             const std::vector<uint32_t> &jumpAddresses,
                                                             size_t promotedEntryCount)
    {
        std::ostringstream ss;
        ss << "unresolved JR/JALR at";
        for (uint32_t address : jumpAddresses)
        {
            ss << ' ' << hexAddress(address);
        }
        ss << "; promoted " << promotedEntryCount << " fallback entr" << (promotedEntryCount == 1 ? "y" : "ies");

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            ++m_counters.indirectFallbackPromotions;
            m_counters.indirectFallbackEntries += promotedEntryCount;
            m_events.push_back(Event{Severity::Warning, "control-flow", ss.str(), functionName, jumpAddresses.empty() ? 0u : jumpAddresses.front(), !jumpAddresses.empty()});
        }
    }

    void RecompilerReporter::recordUnhandledInstruction(const std::string &functionName,
                                                        uint32_t address,
                                                        uint32_t raw,
                                                        const std::string &message)
    {
        std::ostringstream ss;
        ss << message << " raw=0x" << std::hex << raw;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            ++m_counters.unhandledInstructions;
            m_events.push_back(Event{Severity::Error, "unhandled-instruction", ss.str(), functionName, address, true});
        }
    }

    const RecompilerReporter::Counters &RecompilerReporter::counters() const
    {
        return m_counters;
    }

    bool RecompilerReporter::hasErrors() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return std::any_of(m_events.begin(), m_events.end(), [](const Event &event) { return event.severity == Severity::Error; });
    }

    bool RecompilerReporter::hasWarnings() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return std::any_of(m_events.begin(), m_events.end(), [](const Event &event) { return event.severity == Severity::Warning; });
    }

    void RecompilerReporter::printSummary(std::ostream &os) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        os << "\n========== PS2Recomp report ==========" << std::endl;
        os << "Functions discovered: " << m_counters.functionsDiscovered << std::endl;
        os << "Symbols: " << m_counters.symbolsDiscovered
           << ", sections: " << m_counters.sectionsDiscovered
           << ", relocations: " << m_counters.relocationsDiscovered << std::endl;
        os << "Functions processed: " << m_counters.functionsProcessed
           << ", recompiled: " << m_counters.functionsRecompiled
           << ", stubs: " << m_counters.functionsStubbed
           << ", skipped: " << m_counters.functionsSkipped
           << ", decode failures: " << m_counters.decodeFailures << std::endl;
        os << "Additional entrypoints: " << m_counters.additionalEntryPoints << std::endl;
        os << "Generated functions: " << m_counters.generatedFunctions << std::endl;
        os << "Indirect fallback promotions: " << m_counters.indirectFallbackPromotions
           << " (" << m_counters.indirectFallbackEntries << " fallback entries)" << std::endl;
        os << "Unhandled instructions: " << m_counters.unhandledInstructions << std::endl;

        size_t warnings = 0;
        size_t errors = 0;
        for (const Event &event : m_events)
        {
            warnings += event.severity == Severity::Warning ? 1u : 0u;
            errors += event.severity == Severity::Error ? 1u : 0u;
        }
        os << "Warnings: " << warnings << ", errors: " << errors << std::endl;

        if (!m_events.empty())
        {
            os << "\nEvents:" << std::endl;
            const size_t count =  m_events.size();
            for (size_t i = 0; i < count; ++i)
            {
                const Event &event = m_events[i];
                os << "  [" << severityName(event.severity) << "] " << event.category;
                if (!event.functionName.empty())
                {
                    os << " function=" << event.functionName;
                }
                if (event.hasAddress)
                {
                    os << " addr=" << hexAddress(event.address);
                }
                os << " - " << event.message << std::endl;
            }
        }

        os << "======================================" << std::endl;
    }

    void RecompilerReporter::addEvent(Severity severity,
                                      const std::string &category,
                                      const std::string &message,
                                      const std::string &functionName,
                                      uint32_t address,
                                      bool hasAddress)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_events.push_back(Event{severity, category, message, functionName, address, hasAddress});
    }
}
