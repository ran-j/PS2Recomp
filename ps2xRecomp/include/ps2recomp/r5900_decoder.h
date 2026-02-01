#ifndef PS2RECOMP_R5900_DECODER_H
#define PS2RECOMP_R5900_DECODER_H

#include "ps2recomp/types.h"
#include "ps2recomp/instructions.h"
#include <cstdint>

namespace ps2recomp
{

    class R5900Decoder
    {
    public:
        R5900Decoder();
        ~R5900Decoder();

        Instruction decodeInstruction(uint32_t address, uint32_t rawInstruction) const;

        bool isBranchInstruction(const Instruction &inst) const;
        bool isJumpInstruction(const Instruction &inst) const;
        bool isCallInstruction(const Instruction &inst) const;
        bool isReturnInstruction(const Instruction &inst) const;
        bool isMMIInstruction(const Instruction &inst) const;
        bool isVUInstruction(const Instruction &inst) const;
        bool isStore(const Instruction &inst) const;
        bool isLoad(const Instruction &inst) const;
        bool hasDelaySlot(const Instruction &inst) const;

        uint32_t getBranchTarget(const Instruction &inst) const;
        uint32_t getJumpTarget(const Instruction &inst) const;

    private:
        void decodeRType(Instruction &inst) const;
        void decodeIType(Instruction &inst) const;
        void decodeJType(Instruction &inst) const;
 
        void decodeSpecial(Instruction &inst) const;
        void decodeRegimm(Instruction &inst) const;
        
        void decodeCOP0(Instruction& inst) const;
        void decodeCOP1(Instruction &inst) const;
        void decodeCOP2(Instruction &inst) const;

        void decodeMMI(Instruction &inst) const;
        void decodeMMI0(Instruction &inst) const;
        void decodeMMI1(Instruction &inst) const;
        void decodeMMI2(Instruction &inst) const;
        void decodeMMI3(Instruction &inst) const;
        
        void decodePMFHL(Instruction &inst) const;
    };

} // namespace ps2recomp

#endif // PS2RECOMP_R5900_DECODER_H