#include "iop_stdio.h"
#include "iop_format.h"

#include "../core/iop_cpu.h"
#include "../core/iop_memory.h"
#include "ps2x/iop/iop_host.h"

#include <string>

namespace ps2x::iop::detail
{
    IopStdio::IopStdio(IopHost &host, IopMemory &memory) noexcept
        : m_host(host), m_memory(memory)
    {
    }

    bool IopStdio::dispatchImport(uint16_t ordinal, IopCpuState &cpu)
    {
        const uint32_t a0 = cpu.gpr[4];
        const uint32_t a1 = cpu.gpr[5];
        const auto setV0 = [&](uint32_t value)
        {
            cpu.gpr[2] = value;
        };
        const auto logString = [&](std::string_view prefix, uint32_t address, uint32_t resultBias = 0u)
        {
            const std::string text = m_memory.readString(address, 2048u);
            m_host.log(LogLevel::Info, std::string(prefix) + text);
            setV0(static_cast<uint32_t>(text.size()) + resultBias);
        };

        // o32 varargs: argument words after the format continue in a1..a3, then at sp+16+4n.
        const auto logFormatted = [&](std::string_view prefix, uint32_t formatAddress, uint32_t firstArgSlot)
        {
            uint32_t slot = firstArgSlot;
            const auto nextArg = [&]() -> uint32_t
            {
                const uint32_t index = slot++;
                return index < 4u ? cpu.gpr[4u + index] : m_memory.read32(cpu.gpr[29] + index * 4u);
            };
            const std::string text = formatIopString(m_memory, m_memory.readString(formatAddress, 2048u), nextArg);
            m_host.log(LogLevel::Info, std::string(prefix) + text);
            setV0(static_cast<uint32_t>(text.size()));
        };
        const auto logFormattedList = [&](std::string_view prefix, uint32_t formatAddress, uint32_t argList)
        {
            const auto nextArg = [&]() -> uint32_t
            {
                const uint32_t value = m_memory.read32(argList);
                argList += 4u;
                return value;
            };
            const std::string text = formatIopString(m_memory, m_memory.readString(formatAddress, 2048u), nextArg);
            m_host.log(LogLevel::Info, std::string(prefix) + text);
            setV0(static_cast<uint32_t>(text.size()));
        };

        switch (ordinal)
        {
        case 4: // printf
            logFormatted("[IOP printf] ", a0, 1u);
            return true;
        case 5: // getchar
        case 10:
            setV0(0xFFFFFFFFu);
            return true;
        case 6: // putchar
            m_host.log(LogLevel::Info, std::string("[IOP putchar] ") + static_cast<char>(a0 & 0xFFu));
            setV0(a0 & 0xFFu);
            return true;
        case 7: // puts
            logString("[IOP puts] ", a0, 1u);
            return true;
        case 8: // gets
        case 13:
            setV0(0u);
            return true;
        case 9: // fdprintf
            logFormatted("[IOP fdprintf] ", a1, 2u);
            return true;
        case 11:
            setV0(a0 & 0xFFu);
            return true;
        case 12: // fdputs
            logString("[IOP fdputs] ", a0);
            return true;
        case 14: // vfdprintf
            logFormattedList("[IOP vfdprintf] ", a1, cpu.gpr[6]);
            return true;
        default:
            return false;
        }
    }
}
