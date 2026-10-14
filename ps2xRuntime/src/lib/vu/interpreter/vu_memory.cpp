#include "runtime/vu/vu_unit.h"
#include <cstring>
#include <stdexcept>

void VUExecutor::readData(uint32_t address, void *result)
{
    if (m_unit == Unit::VU0 && (address & 0x4000u))
    {
        uint32_t words[4]{};
        if (!m_services || !m_services->readShared || !m_services->readShared(m_services->context, m_cycle, address & 0x3ffu, words))
            throw std::runtime_error("VU shared register read failed");
        std::memcpy(result, words, sizeof(words));
    }
    else
        std::memcpy(result, m_activeVuData + (address & (m_activeVuDataSize - 1)), 16);
}

void VUExecutor::writeData(uint32_t address, const uint32_t *words, uint8_t lanes)
{
    if (m_unit == Unit::VU0 && (address & 0x4000u))
    {
        if (!m_services || !m_services->writeShared || !m_services->writeShared(m_services->context, m_cycle, address & 0x3ffu, words, lanes))
            throw std::runtime_error("VU shared register write failed");
    }
    else if (m_activeVuData)
        for (uint32_t lane = 0; lane < 4; ++lane)
            if (lanes & (8u >> lane))
                std::memcpy(m_activeVuData + (address & (m_activeVuDataSize - 1)) + lane * 4, words + lane, 4);
}
