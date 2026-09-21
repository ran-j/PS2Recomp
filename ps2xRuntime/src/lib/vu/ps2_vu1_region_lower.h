#ifndef PS2_VU1_REGION_LOWER_H
#define PS2_VU1_REGION_LOWER_H

#include "ps2_vu1_detail.h"
#include <bit>
#include <cstdint>
#include <cstring>

namespace ps2_vu_detail {

template <class Inputs, class Sink>
PS2_VU_FORCE_INLINE void computeRegionLower(uint32_t instr, const Inputs &input, Sink &sink,
                               uint8_t *data, uint32_t dataSize)
{
    if (instr == 0u || instr == 0x8000033cu)
        return;
    const uint32_t opcode = instr >> 25u, dest = (instr >> 21u) & 15u;
    const uint32_t fs = (instr >> 11u) & 31u, ft = (instr >> 16u) & 31u;
    const uint32_t is = fs & 15u, it = ft & 15u, id = (instr >> 6u) & 15u;
    const int32_t viS = input.viS(), viT = input.viT();
    const int32_t immediate = static_cast<int32_t>(instr << 21u) >> 21u;
    const auto address = [&](int32_t qword) PS2_VU_INLINE_LAMBDA { return static_cast<uint32_t>(qword) * 16u & (dataSize - 1u); };
    const auto loadVector = [&](uint32_t addr) PS2_VU_INLINE_LAMBDA {
        if (addr + 16ull <= dataSize)
        {
            float value[4];
            std::memcpy(value, data + addr, sizeof(value));
            sink.vf(ft, dest, value);
        }
    };
    const auto storeVector = [&](uint32_t addr) PS2_VU_INLINE_LAMBDA {
        if (addr + 16ull <= dataSize)
            for (unsigned lane = 0; lane < 4; ++lane)
                if ((dest & (8u >> lane)) != 0u)
                {
                    const float value = input.fs(lane);
                    std::memcpy(data + addr + lane * 4u, &value, sizeof(value));
                }
    };
    const auto loadInteger = [&](uint32_t addr) PS2_VU_INLINE_LAMBDA {
        if (addr + 16ull <= dataSize)
        {
            const unsigned lane = (dest & 8u) != 0u ? 0u : (dest & 4u) != 0u ? 1u : (dest & 2u) != 0u ? 2u : 3u;
            uint32_t value;
            std::memcpy(&value, data + addr + lane * 4u, sizeof(value));
            sink.vi(it, static_cast<int16_t>(value));
        }
    };
    const auto storeInteger = [&](uint32_t addr) PS2_VU_INLINE_LAMBDA {
        if (addr + 16ull <= dataSize)
        {
            const uint32_t value = static_cast<uint16_t>(viT);
            for (unsigned lane = 0; lane < 4; ++lane)
                if ((dest & (8u >> lane)) != 0u)
                    std::memcpy(data + addr + lane * 4u, &value, sizeof(value));
        }
    };
    switch (opcode)
    {
    case 0u: loadVector(address(viS + immediate)); return;
    case 1u: storeVector(address(viT + immediate)); return;
    case 4u: loadInteger(address(viS + immediate)); return;
    case 5u: storeInteger(address(viS + immediate)); return;
    case 0x10u: sink.vi(1u, (input.clip() & 0xffffffu) == (instr & 0xffffffu)); return;
    case 0x12u: sink.vi(1u, (input.clip() & (instr & 0xffffffu)) != 0u); return;
    case 0x13u: sink.vi(1u, (input.clip() | (instr & 0xffffffu)) == 0xffffffu); return;
    case 0x1cu: sink.vi(it, input.clip() & 0xfffu); return;
    case 8u:
    case 9u:
    {
        const int32_t value = static_cast<int16_t>(instr & 0x7ffu) | ((instr >> 10u) & 0x7800u);
        sink.vi(it, static_cast<int16_t>(opcode == 8u ? viS + value : viS - value));
        return;
    }
    }
    const uint32_t direct = instr & 63u;
    switch (direct)
    {
    case 0x30u: sink.vi(id, static_cast<int16_t>(viS + viT)); return;
    case 0x31u: sink.vi(id, static_cast<int16_t>(viS - viT)); return;
    case 0x32u:
    {
        const int32_t value = static_cast<int32_t>(instr << 21u) >> 27u;
        sink.vi(it, static_cast<int16_t>(viS + value));
        return;
    }
    case 0x34u: sink.vi(id, viS & viT); return;
    case 0x35u: sink.vi(id, viS | viT); return;
    }
    switch ((instr & 3u) | ((instr >> 4u) & 0x7cu))
    {
    case 0x30u:
    case 0x31u:
    {
        const unsigned rotate = ((instr & 3u) == 1u) ? 1u : 0u;
        const float value[4] = {input.fs(rotate), input.fs((1u + rotate) & 3u),
                                input.fs((2u + rotate) & 3u), input.fs((3u + rotate) & 3u)};
        sink.vf(ft, dest, value);
        return;
    }
    case 0x34u:
        loadVector(address(static_cast<uint16_t>(viS)));
        sink.vi(is, static_cast<int16_t>(viS + 1));
        return;
    case 0x35u:
        storeVector(address(static_cast<uint16_t>(viT)));
        sink.vi(it, static_cast<int16_t>(viT + 1));
        return;
    case 0x36u:
    {
        const int16_t value = is != 0u ? static_cast<int16_t>(viS - 1) : viS;
        sink.vi(is, value);
        loadVector(address(static_cast<uint16_t>(value)));
        return;
    }
    case 0x37u:
    {
        const int16_t value = it != 0u ? static_cast<int16_t>(viT - 1) : viT;
        sink.vi(it, value);
        storeVector(address(static_cast<uint16_t>(value)));
        return;
    }
    case 0x3cu:
        sink.vi(it, static_cast<int16_t>(std::bit_cast<uint32_t>(input.fs((instr >> 21u) & 3u))));
        return;
    case 0x3du:
    {
        const float bits = std::bit_cast<float>(static_cast<int32_t>(static_cast<int16_t>(viS)));
        const float value[4] = {bits, bits, bits, bits};
        sink.vf(ft, dest, value);
        return;
    }
    case 0x3eu: loadInteger(address(static_cast<uint16_t>(viS))); return;
    case 0x3fu: storeInteger(address(static_cast<uint16_t>(viS))); return;
    }
}

}
#endif
