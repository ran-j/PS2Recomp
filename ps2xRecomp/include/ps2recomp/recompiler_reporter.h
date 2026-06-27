#ifndef PS2RECOMP_RECOMPILER_REPORTER_H
#define PS2RECOMP_RECOMPILER_REPORTER_H

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <mutex>
#include <string>
#include <vector>

namespace ps2recomp
{
    class RecompilerReporter
    {
    public:
        enum class Severity
        {
            Info,
            Warning,
            Error
        };

        struct Event
        {
            Severity severity = Severity::Info;
            std::string category;
            std::string message;
            std::string functionName;
            uint32_t address = 0;
            bool hasAddress = false;
        };

        struct Counters
        {
            size_t functionsDiscovered = 0;
            size_t symbolsDiscovered = 0;
            size_t sectionsDiscovered = 0;
            size_t relocationsDiscovered = 0;
            size_t functionsProcessed = 0;
            size_t functionsRecompiled = 0;
            size_t functionsStubbed = 0;
            size_t functionsSkipped = 0;
            size_t decodeFailures = 0;
            size_t additionalEntryPoints = 0;
            size_t generatedFunctions = 0;
            size_t unhandledInstructions = 0;
            size_t indirectFallbackPromotions = 0;
            size_t indirectFallbackEntries = 0;
        };

        void progress(const std::string &message);
        void info(const std::string &category, const std::string &message);
        void warning(const std::string &category, const std::string &message);
        void error(const std::string &category, const std::string &message);
        void warningAt(const std::string &category, const std::string &functionName, uint32_t address, const std::string &message);
        void errorAt(const std::string &category, const std::string &functionName, uint32_t address, const std::string &message);

        void recordDiscovered(size_t functions, size_t symbols, size_t sections, size_t relocations);
        void recordFunctionProcessed();
        void recordFunctionRecompiled();
        void recordFunctionStubbed();
        void recordFunctionSkipped();
        void recordDecodeFailure();
        void recordAdditionalEntryPoints(size_t count);
        void recordGeneratedFunctions(size_t count);
        void recordIndirectFallbackPromotion(const std::string &functionName,
                                             const std::vector<uint32_t> &jumpAddresses,
                                             size_t promotedEntryCount);
        void recordUnhandledInstruction(const std::string &functionName,
                                        uint32_t address,
                                        uint32_t raw,
                                        const std::string &message);

        const Counters &counters() const;
        bool hasErrors() const;
        bool hasWarnings() const;
        void printSummary(std::ostream &os) const;

    private:
        void addEvent(Severity severity,
                      const std::string &category,
                      const std::string &message,
                      const std::string &functionName = {},
                      uint32_t address = 0,
                      bool hasAddress = false);

        mutable std::mutex m_mutex;
        Counters m_counters;
        std::vector<Event> m_events;
    };
}

#endif
