#pragma once
#include <cstdint>

namespace ps2vu
{

    constexpr uint8_t destMask(uint32_t i)
    {
        return (uint8_t)((i >> 21) & 0xF);
    }
    constexpr uint8_t vfTarget(uint32_t i)
    {
        return (uint8_t)((i >> 16) & 0x1F);
    }
    constexpr uint8_t vfSource(uint32_t i)
    {
        return (uint8_t)((i >> 11) & 0x1F);
    }
    constexpr uint8_t vfDestination(uint32_t i)
    {
        return (uint8_t)((i >> 6) & 0x1F);
    }
    constexpr uint8_t broadcastComponent(uint32_t i)
    {
        return (uint8_t)(i & 0x3);
    }

    constexpr uint8_t lowerTarget(uint32_t i)
    {
        return (uint8_t)((i >> 16) & 0x1F);
    }
    constexpr uint8_t lowerSource(uint32_t i)
    {
        return (uint8_t)((i >> 11) & 0x1F);
    }
    constexpr uint8_t lowerDestination(uint32_t i)
    {
        return (uint8_t)((i >> 6) & 0x1F);
    }
    constexpr uint8_t viTarget(uint32_t i)
    {
        return (uint8_t)((i >> 16) & 0xF);
    }
    constexpr uint8_t viSource(uint32_t i)
    {
        return (uint8_t)((i >> 11) & 0xF);
    }
    constexpr uint8_t viDestination(uint32_t i)
    {
        return (uint8_t)((i >> 6) & 0xF);
    }
    constexpr int16_t immediate11(uint32_t i)
    {
        return (int16_t)(int32_t)((int32_t)(i << 21) >> 21);
    }
    constexpr int16_t immediate15(uint32_t i)
    {
        uint32_t lo11 = i & 0x7FF;
        uint32_t hi4 = (i >> 21) & 0xF;
        uint32_t raw = (hi4 << 11) | lo11;
        return (int16_t)(int32_t)((int32_t)(raw << 17) >> 17);
    }

}
