#ifndef PS2RECOMP_DECODER_H
#define PS2RECOMP_DECODER_H

#include "ps2recomp/types.h"
#include "ps2recomp/instructions.h"
#include <cstdint>

namespace ps2recomp
{
    Instruction decodeInstruction(uint32_t address, uint32_t rawInstruction);
    uint32_t getBranchTarget(const Instruction &inst);
    uint32_t getJumpTarget(const Instruction &inst);
}

#endif // PS2RECOMP_DECODER_H
