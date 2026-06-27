#ifndef PS2RECOMP_CONTROL_FLOW_UTILS_H
#define PS2RECOMP_CONTROL_FLOW_UTILS_H

#include "ps2recomp/instructions.h"

#include <cstdint>

namespace ps2recomp
{
    inline uint32_t buildAbsoluteJumpTarget(uint32_t address, uint32_t target) noexcept
    {
        return ((address + 4u) & 0xF0000000u) | (target << 2);
    }

    inline bool isGuestNop(const Instruction &inst) noexcept
    {
        if (inst.raw == 0u)
        {
            return true;
        }

        // Some tests and decoded streams represent a no-op as addiu $zero, $zero, 0.
        return inst.opcode == OPCODE_ADDIU &&
               inst.rs == 0u &&
               inst.rt == 0u &&
               static_cast<int16_t>(inst.simmediate) == 0;
    }
}

#endif // PS2RECOMP_CONTROL_FLOW_UTILS_H
