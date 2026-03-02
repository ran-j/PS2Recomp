#include "MiniTest.h"
#include "ps2recomp/r5900_decoder.h"

using namespace ps2recomp;

void register_r5900_decoder_tests()
{
    MiniTest::Case("R5900Decoder", [](TestCase &tc)
                   {
    tc.Run("decodes JAL with jump target and call flag", [](TestCase &t) {
        // jal 0x00400000 at address 0x1000 => opcode 0x0C100000 (target = 0x00400000 >> 2)
        uint32_t address = 0x1000;
        uint32_t target = 0x00400000;
        uint32_t raw = (OPCODE_JAL << 26) | ((target >> 2) & 0x03FFFFFF);

        R5900Decoder decoder;
        Instruction inst = decoder.decodeInstruction(address, raw);

        t.IsTrue(inst.isJump, "jal should be marked as jump");
        t.IsTrue(inst.isCall, "jal should be marked as call");
        t.IsTrue(inst.hasDelaySlot, "jal has a delay slot");
        t.Equals(decoder.getJumpTarget(inst), target, "jal jump target should match encoded target");
    });

    tc.Run("J computes target with upper PC bits", [](TestCase &t) {
        // Place J at address 0x8FFF_FFFC targeting 0x8123_4560 (upper bits from PC+4)
        uint32_t address = 0x8FFFFFFC;
        uint32_t encodedTarget = 0x0123456; // 0x048D1598 >> 2, but we want lower bits of 0x1234560
        uint32_t raw = (OPCODE_J << 26) | (encodedTarget & 0x03FFFFFF);

        R5900Decoder decoder;
        Instruction inst = decoder.decodeInstruction(address, raw);

        uint32_t expectedPcUpper = (address + 4) & 0xF0000000;
        uint32_t expected = expectedPcUpper | (encodedTarget << 2);
        t.Equals(decoder.getJumpTarget(inst), expected, "J target should combine PC upper bits with encoded target");
    });

    tc.Run("JR/JALR jump target is zero (dynamic)", [](TestCase &t) {
        uint32_t address = 0x1200;
        uint32_t jrRaw = (OPCODE_SPECIAL << 26) | (2 << 21) | SPECIAL_JR;
        uint32_t jalrRaw = (OPCODE_SPECIAL << 26) | (3 << 21) | (31 << 11) | SPECIAL_JALR;

        R5900Decoder decoder;
        Instruction jr = decoder.decodeInstruction(address, jrRaw);
        Instruction jalr = decoder.decodeInstruction(address + 4, jalrRaw);

        t.Equals(decoder.getJumpTarget(jr), 0u, "JR jump target should be unknown (0)");
        t.Equals(decoder.getJumpTarget(jalr), 0u, "JALR jump target should be unknown (0)");
    });

    tc.Run("decodes BEQ sets branch flags and target", [](TestCase &t) {
        // beq r1, r2, offset 0x4 (word offset) at address 0x2000
        uint32_t address = 0x2000;
        uint16_t offset = 0x0004;
        uint32_t raw = (OPCODE_BEQ << 26) | (1 << 21) | (2 << 16) | offset;

        R5900Decoder decoder;
        Instruction inst = decoder.decodeInstruction(address, raw);

        t.IsTrue(inst.isBranch, "beq should be marked as branch");
        t.IsTrue(inst.hasDelaySlot, "beq has a delay slot");
        uint32_t expectedTarget = address + 4 + (static_cast<int16_t>(offset) << 2);
        t.Equals(decoder.getBranchTarget(inst), expectedTarget, "beq target should be computed from simmediate");
    });

    tc.Run("branch target sign-extends negative offset", [](TestCase &t) {
        uint32_t address = 0x2100;
        int16_t negOffset = -4; // jump back 16 bytes
        uint32_t raw = (OPCODE_BNE << 26) | (1 << 21) | (2 << 16) | (negOffset & 0xFFFF);

        R5900Decoder decoder;
        Instruction inst = decoder.decodeInstruction(address, raw);

        uint32_t expectedTarget = address + 4 + (static_cast<int16_t>(negOffset) << 2);
        t.Equals(decoder.getBranchTarget(inst), expectedTarget, "negative branch offsets should sign-extend");
    });

    tc.Run("decodes load/store flags", [](TestCase &t) {
        uint32_t address = 0x3000;
        uint32_t lwRaw = (OPCODE_LW << 26) | (1 << 21) | (2 << 16) | 0x10;
        uint32_t swRaw = (OPCODE_SW << 26) | (3 << 21) | (4 << 16) | 0x20;

        R5900Decoder decoder;
        Instruction lw = decoder.decodeInstruction(address, lwRaw);
        Instruction sw = decoder.decodeInstruction(address + 4, swRaw);

        t.IsTrue(lw.isLoad, "lw should be marked as load");
        t.IsFalse(lw.isStore, "lw should not be marked as store");
        t.IsTrue(sw.isStore, "sw should be marked as store");
        t.IsFalse(sw.isLoad, "sw should not be marked as load");
    });

    tc.Run("JR is marked as return when rs is $ra", [](TestCase &t) {
        uint32_t address = 0x4000;
        uint32_t raw = (OPCODE_SPECIAL << 26) | (31 << 21) | SPECIAL_JR; // jr $ra

        R5900Decoder decoder;
        Instruction inst = decoder.decodeInstruction(address, raw);

        t.IsTrue(inst.isJump, "jr should be jump");
        t.IsTrue(inst.isReturn, "jr $ra should be marked as return");
        t.IsTrue(inst.hasDelaySlot, "jr has delay slot");
    });

    tc.Run("JALR marks call and writes rd when non-zero", [](TestCase &t) {
        uint32_t address = 0x5000;
        uint32_t rd = 5;
        uint32_t raw = (OPCODE_SPECIAL << 26) | (2 << 21) | (rd << 11) | SPECIAL_JALR; // jalr $v0, $a0

        R5900Decoder decoder;
        Instruction inst = decoder.decodeInstruction(address, raw);

        t.IsTrue(inst.isJump, "jalr should be jump");
        t.IsTrue(inst.isCall, "jalr should be call");
        t.IsTrue(inst.hasDelaySlot, "jalr has delay slot");
        t.IsTrue(inst.modificationInfo.modifiesGPR, "jalr with rd!=0 should mark GPR modification");
    });

    tc.Run("R5900 MULT marks rd modification when rd is non-zero", [](TestCase &t) {
        uint32_t address = 0x5800;
        uint32_t rawWithRd = (OPCODE_SPECIAL << 26) | (4 << 21) | (5 << 16) | (3 << 11) | SPECIAL_MULT;
        uint32_t rawRdZero = (OPCODE_SPECIAL << 26) | (4 << 21) | (5 << 16) | (0 << 11) | SPECIAL_MULT;

        R5900Decoder decoder;
        Instruction withRd = decoder.decodeInstruction(address, rawWithRd);
        Instruction rdZero = decoder.decodeInstruction(address + 4, rawRdZero);

        t.IsTrue(withRd.modificationInfo.modifiesControl, "MULT should modify HI/LO");
        t.IsTrue(withRd.modificationInfo.modifiesGPR, "MULT should mark rd modification when rd!=0");
        t.IsFalse(rdZero.modificationInfo.modifiesGPR, "MULT should not mark rd modification when rd==0");
    });

    tc.Run("R5900 MMI MULT1 marks rd modification when rd is non-zero", [](TestCase &t) {
        uint32_t address = 0x5900;
        uint32_t raw = (OPCODE_MMI << 26) | (6 << 21) | (7 << 16) | (8 << 11) | MMI_MULT1;

        R5900Decoder decoder;
        Instruction inst = decoder.decodeInstruction(address, raw);

        t.IsTrue(inst.modificationInfo.modifiesControl, "MULT1 should modify HI1/LO1");
        t.IsTrue(inst.modificationInfo.modifiesGPR, "MULT1 should mark rd modification when rd!=0");
    });

    tc.Run("MMI instruction sets MMI flags", [](TestCase &t) {
        uint32_t address = 0x6000;
        // Use opcode 0x1C (MMI), rs=1, rt=2, rd=3, sa=MMI0_PADDW (0)
        uint32_t raw = (OPCODE_MMI << 26) | (1 << 21) | (2 << 16) | (3 << 11) | MMI0_PADDW;

        R5900Decoder decoder;
        Instruction inst = decoder.decodeInstruction(address, raw);

        t.IsTrue(inst.isMMI, "MMI opcode should set isMMI");
        t.IsTrue(inst.isMultimedia, "MMI opcode should set multimedia flag");
        t.Equals(inst.mmiType, static_cast<uint8_t>(0), "MMI0 should set mmiType to 0");
        t.Equals(inst.mmiFunction, static_cast<uint8_t>(MMI0_PADDW), "MMI function should match sa field");
    });

    tc.Run("COP2 VU macro op marks VU flags", [](TestCase &t) {
        uint32_t address = 0x7000;
        uint8_t s2op = VU0_S2_VDIV;          // 0x38
        uint8_t fhi  = s2op >> 2;             // 0x0E -> bits[10:6]
        uint8_t flo  = s2op & 0x3;            // 0x00 -> bits[1:0]
        uint32_t raw = (OPCODE_COP2 << 26) | (COP2_CO << 21) | (fhi << 6) | (0x3C | flo);

        R5900Decoder decoder;
        Instruction inst = decoder.decodeInstruction(address, raw);

        t.IsTrue(inst.isVU, "VU macro should set isVU");
        t.IsTrue(inst.isMultimedia, "VU macro should set multimedia");
        t.IsTrue(inst.modificationInfo.modifiesControl, "VDIV should mark control modification");
        t.IsTrue(inst.vectorInfo.usesQReg, "VDIV should use Q register");
        uint8_t expectedVecField = static_cast<uint8_t>((raw >> 21) & 0xF);
        t.Equals(inst.vectorInfo.vectorField, expectedVecField, "vector field should reflect encoding");
    });

    tc.Run("REGIMM branch and link marks call and GPR modification", [](TestCase &t) {
        uint32_t address = 0x8000;
        uint16_t offset = 0x2;
        uint32_t raw = (OPCODE_REGIMM << 26) | (1 << 21) | (REGIMM_BGEZAL << 16) | offset;

        R5900Decoder decoder;
        Instruction inst = decoder.decodeInstruction(address, raw);

        t.IsTrue(inst.isBranch, "bgezal should be branch");
        t.IsTrue(inst.isCall, "bgezal should be call (link)");
        t.IsTrue(inst.hasDelaySlot, "bgezal has delay slot");
        t.IsTrue(inst.modificationInfo.modifiesGPR, "bgezal should mark GPR modification for $ra");
        uint32_t expectedTarget = address + 4 + (static_cast<int16_t>(offset) << 2);
        t.Equals(decoder.getBranchTarget(inst), expectedTarget, "bgezal target should be computed");
    });

    tc.Run("LL/SC modify control and set load/store flags", [](TestCase &t) {
        uint32_t address = 0x9000;
        uint32_t llRaw = (OPCODE_LL << 26) | (2 << 21) | (3 << 16) | 0x10;
        uint32_t scRaw = (OPCODE_SC << 26) | (4 << 21) | (5 << 16) | 0x20;

        R5900Decoder decoder;
        Instruction ll = decoder.decodeInstruction(address, llRaw);
        Instruction sc = decoder.decodeInstruction(address + 4, scRaw);

        t.IsTrue(ll.isLoad, "ll should be load");
        t.IsTrue(ll.modificationInfo.modifiesControl, "ll should modify control (LL bit)");
        t.IsTrue(sc.isStore, "sc should be store");
        t.IsTrue(sc.modificationInfo.modifiesControl, "sc should modify control (LL bit)");
        t.IsTrue(sc.modificationInfo.modifiesGPR, "sc writes success flag to rt");
    });

    tc.Run("COP0 ERET is marked as return without delay slot", [](TestCase &t) {
        uint32_t address = 0xA000;
        uint32_t raw = (OPCODE_COP0 << 26) | (COP0_CO << 21) | COP0_CO_ERET;

        R5900Decoder decoder;
        Instruction inst = decoder.decodeInstruction(address, raw);

        t.IsTrue(inst.isReturn, "eret should be marked as return");
        t.IsFalse(inst.hasDelaySlot, "eret should not have a delay slot");
        t.IsTrue(inst.modificationInfo.modifiesControl, "eret changes control state");
    }); });
}
