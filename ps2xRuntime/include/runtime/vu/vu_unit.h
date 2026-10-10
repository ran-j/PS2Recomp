#pragma once
#include "interpreter/vu_executor.h"
#include <span>

namespace ps2vu
{
    enum class UnitStatus : uint8_t
    {
        Ready,
        Running,
        Stopped,
        Fault
    };
    enum class StartSource : uint8_t
    {
        Ee,
        Vif
    };
    enum class ExitReason : uint8_t
    {
        Budget,
        Event,
        Halt,
        Fault
    };

    struct ExecutionExit
    {
        ExitReason reason;
        uint32_t pc;
        uint64_t cycles;
    };

    struct MemoryServices
    {
        uint8_t *data = nullptr;
        void *context = nullptr;
        bool (*readShared)(void *, uint64_t, uint32_t, uint32_t *) noexcept = nullptr;
        bool (*writeShared)(void *, uint64_t, uint32_t, const uint32_t *, uint8_t) noexcept = nullptr;
        bool (*submitGif)(void *, const uint8_t *, uint32_t) noexcept = nullptr;
    };

    struct UnitFailure
    {
        uint32_t pc = 0;
        std::array<char, 192> reason{};
    };

    struct Readiness
    {
        std::array<uint32_t, kRegisterReadyCount> registers{};
        uint32_t q = 0;
    };

    class Unit
    {
    public:
        Unit(bool vu1, std::span<const uint8_t> code, MemoryServices services);
        Unit(const Unit &) = delete;
        Unit &operator=(const Unit &) = delete;

    public:
        void reset() noexcept;
        bool start(uint32_t pc, uint32_t top, uint32_t itop, StartSource source = StartSource::Vif) noexcept;
        bool resume(uint32_t top, uint32_t itop) noexcept;
        void forceBreak() noexcept;

    public:
        ExecutionExit advance(uint32_t cycles) noexcept;
        VU1State &state() noexcept { return m_executor.state(); }
        const VU1State &state() const noexcept { return m_executor.state(); }
        Readiness readiness() const noexcept;
        UnitStatus status() const noexcept { return m_status; }
        const UnitFailure &failure() const noexcept { return m_failure; }
        bool transferring() const noexcept { return m_executor.m_xgkick.active; }
        bool pending() const noexcept { return m_status == UnitStatus::Running || m_executor.pipelinesPending(); }

    private:
        void begin(uint32_t top, uint32_t itop) noexcept;
        void fail(const char *reason) noexcept;
        VUExecutor m_executor;
        MemoryServices m_services;
        std::span<const uint8_t> m_code;
        uint32_t m_dataSize;
        UnitFailure m_failure;
        UnitStatus m_status = UnitStatus::Ready;
    };
}
