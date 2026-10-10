// Per-instruction unit tests for the R5900 general instruction set: MipsOpcodes,
// SpecialFunctions and RegimmFunctions (GitHub issue #160).
//
// Each test decodes a real 32-bit instruction word, translates it with the recompiler and
// pins the emitted C++ verbatim. The pinned C++ is then executed against a guest context and
// guest RAM through the same runtime macros a recompiled game uses, so every case checks both
// halves of an instruction: what the recompiler emits and what that code does at runtime.
// Branches and jumps are materialised by the function emitter rather than the per-instruction
// translator, so those cases emit a small function around the instruction under test.
#include "MiniTest.h"
#include "ps2recomp/code_generator.h"
#include "ps2recomp/instructions.h"
#include "ps2recomp/r5900_decoder.h"
#include "ps2recomp/types.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "runtime/ps2_memory.h"

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

using namespace ps2recomp;

namespace
{
    constexpr uint32_t kCodeAddress = 0x1000u;
    constexpr uint32_t kDataAddress = 0x2000u;
    constexpr uint32_t kScratchBytes = 0x10000u;

    constexpr uint32_t kCop0CauseExcCodeMask = 0x0000007Cu;
    constexpr uint32_t kCop0StatusExl = 0x00000002u;
    constexpr uint32_t kGeneralExceptionVector = 0x80000080u;
    constexpr uint32_t kResetStatus = 0x00010001u; // EIE | IE, as R5900Context leaves it

    // ---------------------------------------------------------------- encoding helpers

    constexpr uint32_t encodeI(uint32_t opcode, uint32_t rs, uint32_t rt, uint16_t imm)
    {
        return (opcode << 26) | (rs << 21) | (rt << 16) | imm;
    }

    constexpr uint32_t encodeR(uint32_t function, uint32_t rs, uint32_t rt, uint32_t rd, uint32_t sa = 0u)
    {
        return (static_cast<uint32_t>(OPCODE_SPECIAL) << 26) | (rs << 21) | (rt << 16) | (rd << 11) | (sa << 6) | function;
    }

    constexpr uint32_t encodeRegimm(uint32_t function, uint32_t rs, uint16_t imm)
    {
        return (static_cast<uint32_t>(OPCODE_REGIMM) << 26) | (rs << 21) | (function << 16) | imm;
    }

    constexpr uint32_t encodeJ(uint32_t opcode, uint32_t targetField)
    {
        return (opcode << 26) | (targetField & 0x03FFFFFFu);
    }

    constexpr uint16_t imm16(int32_t value)
    {
        return static_cast<uint16_t>(value);
    }

    Instruction decode(uint32_t raw, uint32_t address = kCodeAddress)
    {
        R5900Decoder decoder;
        return decoder.decodeInstruction(address, raw);
    }

    std::string hex(uint64_t value)
    {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "0x%llx", static_cast<unsigned long long>(value));
        return buffer;
    }

    std::string describe(const Instruction &inst)
    {
        return inst.disassembly.empty() ? ("raw " + hex(inst.raw)) : inst.disassembly;
    }

    // ---------------------------------------------------------------- translation helpers

    const std::vector<Symbol> kNoSymbols;
    const std::vector<Section> kNoSections;

    std::string translate(const Instruction &inst)
    {
        CodeGenerator generator(kNoSymbols, kNoSections);
        return generator.translateInstruction(inst);
    }

    std::string normalizeWhitespace(std::string_view text)
    {
        std::string out;
        bool pendingSpace = false;
        for (char c : text)
        {
            if (std::isspace(static_cast<unsigned char>(c)))
            {
                pendingSpace = !out.empty();
                continue;
            }
            if (pendingSpace)
            {
                out.push_back(' ');
                pendingSpace = false;
            }
            out.push_back(c);
        }
        return out;
    }

    void expectTranslation(TestCase &t, const Instruction &inst, std::string_view expected)
    {
        const std::string actual = translate(inst);
        if (normalizeWhitespace(actual) != normalizeWhitespace(expected))
        {
            t.Fail(describe(inst) + " translated to <" + actual + "> but the test expects <" +
                   std::string(expected) + ">");
        }
    }

    bool emitsRuntimeThrow(const std::string &code)
    {
        return code.find("throw std::runtime_error(") != std::string::npos;
    }

    std::string emitFunction(uint32_t address, std::initializer_list<uint32_t> words)
    {
        R5900Decoder decoder;
        std::vector<Instruction> instructions;
        uint32_t pc = address;
        for (uint32_t word : words)
        {
            instructions.push_back(decoder.decodeInstruction(pc, word));
            pc += 4u;
        }

        Function function{};
        function.name = "instruction_under_test";
        function.start = address;
        function.end = pc;
        function.isRecompiled = true;

        CodeGenerator generator(kNoSymbols, kNoSections);
        return generator.generateFunction(function, instructions, false);
    }

    // Branch and jump cases share one layout at kCodeAddress:
    //   +0  branch or jump under test (conditional branches target +12)
    //   +4  addiu $3, $zero, 7      (delay slot; its translation is distinctive)
    //   +8  addiu $4, $zero, 9      (fallthrough)
    //   +12 addiu $5, $zero, 11     (branch target)
    //   +16 jr $ra
    //   +20 nop
    constexpr uint32_t kDelaySlotWord = encodeI(OPCODE_ADDIU, 0, 3, 7);
    constexpr uint32_t kFallthroughWord = encodeI(OPCODE_ADDIU, 0, 4, 9);
    constexpr uint32_t kTargetWord = encodeI(OPCODE_ADDIU, 0, 5, 11);
    constexpr uint32_t kReturnWord = encodeR(SPECIAL_JR, 31, 0, 0);
    constexpr const char *kDelaySlotCode = "SET_GPR_S32(ctx, 3, (int32_t)ADD32(GPR_U32(ctx, 0), 7));";
    constexpr uint16_t kBranchOffsetWords = 2u; // from the delay slot to +12

    std::string emitBranchFunction(uint32_t branchWord, uint32_t address = kCodeAddress)
    {
        return emitFunction(address, {branchWord, kDelaySlotWord, kFallthroughWord, kTargetWord, kReturnWord, 0u});
    }

    std::string extractBranchCondition(const std::string &code, uint32_t address)
    {
        char marker[64];
        std::snprintf(marker, sizeof(marker), "const bool branch_taken_0x%x = (", address);
        const size_t begin = code.find(marker);
        if (begin == std::string::npos)
        {
            return {};
        }
        const size_t start = begin + std::strlen(marker);
        const size_t end = code.find(");\n", start);
        if (end == std::string::npos)
        {
            return {};
        }
        return code.substr(start, end - start);
    }

    void expectBranchCondition(TestCase &t, uint32_t branchWord, std::string_view expected)
    {
        const std::string condition = extractBranchCondition(emitBranchFunction(branchWord), kCodeAddress);
        if (normalizeWhitespace(condition) != normalizeWhitespace(expected))
        {
            t.Fail(describe(decode(branchWord)) + " branch condition is <" + condition +
                   "> but the test expects <" + std::string(expected) + ">");
        }
    }

    // Structural checks shared by every conditional branch: delay slot placement relative to the
    // branch decision, the in-function target, and whether $ra is written.
    void expectConditionalBranchShape(TestCase &t, uint32_t branchWord, bool likely, bool links)
    {
        const std::string name = describe(decode(branchWord));
        const std::string code = emitBranchFunction(branchWord);
        const size_t slot = code.find(kDelaySlotCode);
        const size_t decision = code.find("if (branch_taken_0x1000)");
        const size_t link = code.find("SET_GPR_U32(ctx, 31, 0x1008u);");

        t.IsTrue(slot != std::string::npos, name + ": delay slot must be translated");
        t.IsTrue(decision != std::string::npos, name + ": branch decision must test branch_taken");
        t.IsTrue(code.find("goto label_100c;") != std::string::npos, name + ": taken path must jump to the in-function target");
        if (slot == std::string::npos || decision == std::string::npos)
        {
            return;
        }

        if (likely)
        {
            t.IsTrue(decision < slot, name + ": a likely branch executes its delay slot only when taken");
        }
        else
        {
            t.IsTrue(slot < decision, name + ": the delay slot executes before the branch decision");
        }

        if (links)
        {
            t.IsTrue(link != std::string::npos, name + ": must write the return address to $ra");
            t.IsTrue(link != std::string::npos && link < decision && link < slot,
                     name + ": $ra is written unconditionally, before the delay slot runs");
        }
        else
        {
            t.IsTrue(link == std::string::npos, name + ": must not write $ra");
        }
    }

    // ---------------------------------------------------------------- guest fixture

    struct GuestMachine
    {
        PS2Runtime runtime;
        std::vector<uint8_t> ram = std::vector<uint8_t>(PS2_RAM_SIZE, 0u);
        R5900Context ctx{};

        void reset()
        {
            ctx = R5900Context{};
            std::memset(ram.data(), 0, kScratchBytes);
        }
    };

    std::unique_ptr<GuestMachine> gGuest;

    GuestMachine &guest()
    {
        if (!gGuest)
        {
            gGuest = std::make_unique<GuestMachine>();
        }
        gGuest->reset();
        return *gGuest;
    }

    void releaseGuest()
    {
        gGuest.reset();
    }

    // Register access through plain memcpy, independent of the macros under test.
    uint64_t gprLo(const R5900Context &ctx, int reg)
    {
        uint64_t value = 0;
        std::memcpy(&value, &ctx.r[reg], sizeof(value));
        return value;
    }

    uint64_t gprHi(const R5900Context &ctx, int reg)
    {
        uint64_t value = 0;
        std::memcpy(&value, reinterpret_cast<const uint8_t *>(&ctx.r[reg]) + 8, sizeof(value));
        return value;
    }

    void setGpr(R5900Context &ctx, int reg, uint64_t lo, uint64_t hi = 0u)
    {
        std::memcpy(&ctx.r[reg], &lo, sizeof(lo));
        std::memcpy(reinterpret_cast<uint8_t *>(&ctx.r[reg]) + 8, &hi, sizeof(hi));
    }

    constexpr uint64_t sext32(uint32_t value)
    {
        return static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(value)));
    }

    // What a 32-bit operation leaves in a 64-bit register.
    void setGpr32(R5900Context &ctx, int reg, uint32_t value)
    {
        setGpr(ctx, reg, sext32(value));
    }

    template <typename T>
    T readRam(const uint8_t *rdram, uint32_t addr)
    {
        T value;
        std::memcpy(&value, rdram + addr, sizeof(value));
        return value;
    }

    template <typename T>
    void writeRam(uint8_t *rdram, uint32_t addr, T value)
    {
        std::memcpy(rdram + addr, &value, sizeof(value));
    }

    void expectU64(TestCase &t, uint64_t actual, uint64_t expected, const std::string &what)
    {
        if (actual != expected)
        {
            t.Fail(what + ": got " + hex(actual) + ", expected " + hex(expected));
        }
    }

    void expectGpr(TestCase &t, const R5900Context &ctx, int reg, uint64_t expected, const std::string &what)
    {
        expectU64(t, gprLo(ctx, reg), expected, what + " ($" + std::to_string(reg) + " low 64 bits)");
    }

    // Arithmetic traps reach COP0 through PS2Runtime, exactly as a recompiled game would see them.
    void armTrapCheck(R5900Context &ctx)
    {
        ctx.pc = kCodeAddress;
        ctx.cop0_status = kResetStatus;
        ctx.cop0_cause = 0u;
        ctx.in_delay_slot = false;
    }

    bool exceptionRaised(const R5900Context &ctx, PS2Exception exception)
    {
        return ((ctx.cop0_cause & kCop0CauseExcCodeMask) >> 2) == static_cast<uint32_t>(exception) &&
               (ctx.cop0_status & kCop0StatusExl) != 0u &&
               ctx.cop0_epc == kCodeAddress &&
               ctx.pc == kGeneralExceptionVector;
    }

    bool noException(const R5900Context &ctx)
    {
        return (ctx.cop0_status & kCop0StatusExl) == 0u;
    }

    // Reference model for the unaligned load/store family, written in terms of guest byte
    // addresses instead of shift tables. "Left" instructions pair the register's most significant
    // byte with the effective address and walk down to the start of the aligned word; "right"
    // instructions pair the least significant byte with the effective address and walk up to the
    // end of the aligned word (little-endian guest).
    uint64_t replaceByte(uint64_t value, unsigned index, uint8_t byte)
    {
        const uint64_t mask = 0xFFull << (8u * index);
        return (value & ~mask) | (static_cast<uint64_t>(byte) << (8u * index));
    }

    uint8_t byteOf(uint64_t value, unsigned index)
    {
        return static_cast<uint8_t>(value >> (8u * index));
    }

    uint64_t refLoadLeft(const uint8_t *rdram, uint32_t addr, unsigned width, uint64_t reg)
    {
        const uint32_t base = addr & ~(width - 1u);
        const unsigned offset = addr & (width - 1u);
        for (unsigned i = 0; i <= offset; ++i)
        {
            reg = replaceByte(reg, width - 1u - offset + i, rdram[base + i]);
        }
        return reg;
    }

    uint64_t refLoadRight(const uint8_t *rdram, uint32_t addr, unsigned width, uint64_t reg)
    {
        const uint32_t base = addr & ~(width - 1u);
        const unsigned offset = addr & (width - 1u);
        for (unsigned i = 0; offset + i < width; ++i)
        {
            reg = replaceByte(reg, i, rdram[base + offset + i]);
        }
        return reg;
    }

    void refStoreLeft(uint8_t *memory, uint32_t addr, unsigned width, uint64_t reg)
    {
        const uint32_t base = addr & ~(width - 1u);
        const unsigned offset = addr & (width - 1u);
        for (unsigned i = 0; i <= offset; ++i)
        {
            memory[base + i] = byteOf(reg, width - 1u - offset + i);
        }
    }

    void refStoreRight(uint8_t *memory, uint32_t addr, unsigned width, uint64_t reg)
    {
        const uint32_t base = addr & ~(width - 1u);
        const unsigned offset = addr & (width - 1u);
        for (unsigned i = 0; offset + i < width; ++i)
        {
            memory[base + offset + i] = byteOf(reg, i);
        }
    }

    constexpr uint8_t kFillByte = 0xEEu;

    void fillRam(uint8_t *rdram, uint32_t addr, uint32_t bytes)
    {
        std::memset(rdram + addr, kFillByte, bytes);
    }

    bool ramUntouched(const uint8_t *rdram, uint32_t addr, uint32_t bytes)
    {
        for (uint32_t i = 0; i < bytes; ++i)
        {
            if (rdram[addr + i] != kFillByte)
            {
                return false;
            }
        }
        return true;
    }
}

// Brings the names the emitted code expects (rdram, ctx, runtime) into scope on a fresh guest.
#define PS2X_GUEST_SCOPE()                     \
    GuestMachine &machine = guest();           \
    uint8_t *rdram = machine.ram.data();       \
    R5900Context *ctx = &machine.ctx;          \
    PS2Runtime *runtime = &machine.runtime;    \
    (void)rdram;                               \
    (void)runtime

// Pins the recompiler output for `inst` to the given C++ and then executes that very C++.
#define PS2X_EXEC_EMITTED(t, inst, ...)              \
    do                                               \
    {                                                \
        expectTranslation(t, inst, #__VA_ARGS__);    \
        __VA_ARGS__                                  \
    } while (0)

// Pins the condition the function emitter evaluates for a conditional branch and evaluates it.
#define PS2X_BRANCH_TAKEN(t, branchWord, ...) (expectBranchCondition(t, branchWord, #__VA_ARGS__), (__VA_ARGS__))

void register_r5900_general_instruction_tests()
{
    MiniTest::Case("R5900Instructions.MipsOpcodes", [](TestCase &tc)
                   {
        tc.After(releaseGuest);

        tc.Run("ADDI adds a sign-extended immediate and traps on signed overflow", [](TestCase &t) {
            PS2X_GUEST_SCOPE();
            const Instruction addi = decode(encodeI(OPCODE_ADDI, 1, 2, 5));            // addi $2, $1, 5
            const Instruction addiNeg = decode(encodeI(OPCODE_ADDI, 1, 2, imm16(-16))); // addi $2, $1, -16
            auto runAddi = [&] {
                PS2X_EXEC_EMITTED(t, addi, { uint32_t tmp; bool ov; ADD32_OV(GPR_U32(ctx, 1), (int32_t)5, tmp, ov); if (ov) runtime->SignalException(ctx, EXCEPTION_INTEGER_OVERFLOW); else SET_GPR_S32(ctx, 2, (int32_t)tmp); });
            };

            armTrapCheck(*ctx);
            setGpr32(*ctx, 1, 0x7FFFFFF0u);
            runAddi();
            expectGpr(t, *ctx, 2, 0x7FFFFFF5u, "addi 0x7FFFFFF0 + 5");
            t.IsTrue(noException(*ctx), "addi without overflow must not raise an exception");

            armTrapCheck(*ctx);
            setGpr32(*ctx, 1, 5u);
            PS2X_EXEC_EMITTED(t, addiNeg, { uint32_t tmp; bool ov; ADD32_OV(GPR_U32(ctx, 1), (int32_t)4294967280, tmp, ov); if (ov) runtime->SignalException(ctx, EXCEPTION_INTEGER_OVERFLOW); else SET_GPR_S32(ctx, 2, (int32_t)tmp); });
            expectGpr(t, *ctx, 2, sext32(0xFFFFFFF5u), "addi 5 + (-16) sign-extends the 32-bit result");

            armTrapCheck(*ctx);
            setGpr32(*ctx, 1, 0x7FFFFFFFu);
            setGpr(*ctx, 2, 0x1234u);
            runAddi();
            t.IsTrue(exceptionRaised(*ctx, EXCEPTION_INTEGER_OVERFLOW),
                     "addi 0x7FFFFFFF + 5 must raise an integer overflow exception at the faulting pc");
            expectGpr(t, *ctx, 2, 0x1234u, "addi overflow must leave rt unchanged");

            armTrapCheck(*ctx);
            setGpr32(*ctx, 1, 0x80000000u);
            PS2X_EXEC_EMITTED(t, addiNeg, { uint32_t tmp; bool ov; ADD32_OV(GPR_U32(ctx, 1), (int32_t)4294967280, tmp, ov); if (ov) runtime->SignalException(ctx, EXCEPTION_INTEGER_OVERFLOW); else SET_GPR_S32(ctx, 2, (int32_t)tmp); });
            t.IsTrue(exceptionRaised(*ctx, EXCEPTION_INTEGER_OVERFLOW), "addi INT32_MIN + (-16) must raise an overflow exception");

            expectTranslation(t, decode(encodeI(OPCODE_ADDI, 1, 0, 5)), "// NOP (addi to $zero)");
        });

        tc.Run("ADDIU wraps at 32 bits, sign-extends and never traps", [](TestCase &t) {
            PS2X_GUEST_SCOPE();
            const Instruction addiu = decode(encodeI(OPCODE_ADDIU, 1, 2, 1));            // addiu $2, $1, 1
            const Instruction addiuNeg = decode(encodeI(OPCODE_ADDIU, 1, 2, imm16(-1))); // addiu $2, $1, -1
            constexpr uint64_t kUpperHalf = 0x0123456789ABCDEFull;

            setGpr32(*ctx, 1, 0x7FFFFFFFu);
            setGpr(*ctx, 2, 0u, kUpperHalf);
            PS2X_EXEC_EMITTED(t, addiu, SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 1), 1)););
            expectGpr(t, *ctx, 2, 0xFFFFFFFF80000000ull, "addiu 0x7FFFFFFF + 1 wraps and sign-extends without trapping");
            expectU64(t, gprHi(*ctx, 2), kUpperHalf, "addiu preserves bits 64..127 of rt");

            setGpr(*ctx, 1, 0x00000000FFFFFFFFull); // only the low 32 bits of rs take part
            PS2X_EXEC_EMITTED(t, addiu, SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 1), 1)););
            expectGpr(t, *ctx, 2, 0u, "addiu uses the low 32 bits of rs");

            setGpr(*ctx, 1, 0u);
            PS2X_EXEC_EMITTED(t, addiuNeg, SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 1), 4294967295)););
            expectGpr(t, *ctx, 2, 0xFFFFFFFFFFFFFFFFull, "addiu 0 + (-1)");

            expectTranslation(t, decode(encodeI(OPCODE_ADDIU, 1, 0, 1)), "// NOP (addiu $zero, ...)");
        });

        tc.Run("SLTI compares the full 64-bit register with the sign-extended immediate", [](TestCase &t) {
            PS2X_GUEST_SCOPE();
            const Instruction sltiZero = decode(encodeI(OPCODE_SLTI, 1, 2, 0));          // slti $2, $1, 0
            const Instruction sltiNeg = decode(encodeI(OPCODE_SLTI, 1, 2, imm16(-1)));   // slti $2, $1, -1

            setGpr(*ctx, 1, 0xFFFFFFFFFFFFFFFFull);
            PS2X_EXEC_EMITTED(t, sltiZero, SET_GPR_U64(ctx, 2, ((int64_t)GPR_S64(ctx, 1) < (int64_t)(int32_t)0) ? 1 : 0););
            expectGpr(t, *ctx, 2, 1u, "slti -1 < 0");

            setGpr(*ctx, 1, 0x00000000FFFFFFFFull); // positive as a 64-bit value even though bit 31 is set
            PS2X_EXEC_EMITTED(t, sltiZero, SET_GPR_U64(ctx, 2, ((int64_t)GPR_S64(ctx, 1) < (int64_t)(int32_t)0) ? 1 : 0););
            expectGpr(t, *ctx, 2, 0u, "slti compares 64 bits, not the low word");

            setGpr(*ctx, 1, 0xFFFFFFFFFFFFFFFEull);
            PS2X_EXEC_EMITTED(t, sltiNeg, SET_GPR_U64(ctx, 2, ((int64_t)GPR_S64(ctx, 1) < (int64_t)(int32_t)4294967295) ? 1 : 0););
            expectGpr(t, *ctx, 2, 1u, "slti -2 < -1");

            setGpr(*ctx, 1, 0xFFFFFFFFFFFFFFFFull);
            PS2X_EXEC_EMITTED(t, sltiNeg, SET_GPR_U64(ctx, 2, ((int64_t)GPR_S64(ctx, 1) < (int64_t)(int32_t)4294967295) ? 1 : 0););
            expectGpr(t, *ctx, 2, 0u, "slti -1 < -1 is false");
        });

        tc.Run("SLTIU sign-extends the immediate and then compares unsigned", [](TestCase &t) {
            PS2X_GUEST_SCOPE();
            const Instruction sltiuNeg = decode(encodeI(OPCODE_SLTIU, 1, 2, imm16(-1))); // sltiu $2, $1, -1
            const Instruction sltiuFive = decode(encodeI(OPCODE_SLTIU, 1, 2, 5));        // sltiu $2, $1, 5

            setGpr(*ctx, 1, 0xFFFFFFFFFFFFFFFEull);
            PS2X_EXEC_EMITTED(t, sltiuNeg, SET_GPR_U64(ctx, 2, ((uint64_t)GPR_U64(ctx, 1) < (uint64_t)(int64_t)(int32_t)4294967295) ? 1 : 0););
            expectGpr(t, *ctx, 2, 1u, "sltiu: -1 becomes 0xFFFFFFFFFFFFFFFF, so 0x...FFFE is below it");

            setGpr(*ctx, 1, 0xFFFFFFFFFFFFFFFFull);
            PS2X_EXEC_EMITTED(t, sltiuNeg, SET_GPR_U64(ctx, 2, ((uint64_t)GPR_U64(ctx, 1) < (uint64_t)(int64_t)(int32_t)4294967295) ? 1 : 0););
            expectGpr(t, *ctx, 2, 0u, "sltiu: nothing is below 0xFFFFFFFFFFFFFFFF");

            setGpr(*ctx, 1, 0xFFFFFFFF00000004ull);
            PS2X_EXEC_EMITTED(t, sltiuFive, SET_GPR_U64(ctx, 2, ((uint64_t)GPR_U64(ctx, 1) < (uint64_t)(int64_t)(int32_t)5) ? 1 : 0););
            expectGpr(t, *ctx, 2, 0u, "sltiu compares all 64 bits unsigned");

            setGpr(*ctx, 1, 4u);
            PS2X_EXEC_EMITTED(t, sltiuFive, SET_GPR_U64(ctx, 2, ((uint64_t)GPR_U64(ctx, 1) < (uint64_t)(int64_t)(int32_t)5) ? 1 : 0););
            expectGpr(t, *ctx, 2, 1u, "sltiu 4 < 5");
        });

        tc.Run("ANDI, ORI and XORI zero-extend the immediate over 64-bit operands", [](TestCase &t) {
            PS2X_GUEST_SCOPE();
            const Instruction andi = decode(encodeI(OPCODE_ANDI, 1, 2, 0xFFFF)); // andi $2, $1, 0xFFFF
            const Instruction ori = decode(encodeI(OPCODE_ORI, 1, 2, 0x8000));   // ori $2, $1, 0x8000
            const Instruction xori = decode(encodeI(OPCODE_XORI, 1, 2, 0xFFFF)); // xori $2, $1, 0xFFFF

            setGpr(*ctx, 1, 0xFFFFFFFFFFFF1234ull);
            PS2X_EXEC_EMITTED(t, andi, SET_GPR_U64(ctx, 2, GPR_U64(ctx, 1) & (uint64_t)(uint16_t)65535););
            expectGpr(t, *ctx, 2, 0x1234u, "andi keeps only the low 16 bits");

            setGpr(*ctx, 1, 0x0000000100000000ull);
            PS2X_EXEC_EMITTED(t, ori, SET_GPR_U64(ctx, 2, GPR_U64(ctx, 1) | (uint64_t)(uint16_t)32768););
            expectGpr(t, *ctx, 2, 0x0000000100008000ull, "ori does not sign-extend bit 15 and keeps the upper bits of rs");

            setGpr(*ctx, 1, 0x00000000FFFFFFFFull);
            PS2X_EXEC_EMITTED(t, xori, SET_GPR_U64(ctx, 2, GPR_U64(ctx, 1) ^ (uint64_t)(uint16_t)65535););
            expectGpr(t, *ctx, 2, 0x00000000FFFF0000ull, "xori flips only the low 16 bits");
        });

        tc.Run("LUI loads bits 16..31 and sign-extends to 64 bits", [](TestCase &t) {
            PS2X_GUEST_SCOPE();
            const Instruction luiHigh = decode(encodeI(OPCODE_LUI, 0, 2, 0x8000)); // lui $2, 0x8000
            const Instruction luiLow = decode(encodeI(OPCODE_LUI, 0, 2, 0x1234));  // lui $2, 0x1234

            PS2X_EXEC_EMITTED(t, luiHigh, SET_GPR_S32(ctx, 2, (int32_t)((uint32_t)32768 << 16)););
            expectGpr(t, *ctx, 2, 0xFFFFFFFF80000000ull, "lui 0x8000");

            PS2X_EXEC_EMITTED(t, luiLow, SET_GPR_S32(ctx, 2, (int32_t)((uint32_t)4660 << 16)););
            expectGpr(t, *ctx, 2, 0x12340000u, "lui 0x1234");
        });

        tc.Run("DADDI and DADDIU add 64-bit values and only DADDI traps on overflow", [](TestCase &t) {
            PS2X_GUEST_SCOPE();
            const Instruction daddi = decode(encodeI(OPCODE_DADDI, 1, 2, imm16(-1)));  // daddi $2, $1, -1
            const Instruction daddiOne = decode(encodeI(OPCODE_DADDI, 1, 2, 1));       // daddi $2, $1, 1
            const Instruction daddiu = decode(encodeI(OPCODE_DADDIU, 1, 2, 1));        // daddiu $2, $1, 1
            const Instruction daddiuNeg = decode(encodeI(OPCODE_DADDIU, 1, 2, imm16(-1)));

            armTrapCheck(*ctx);
            setGpr(*ctx, 1, 0x0000000100000000ull);
            PS2X_EXEC_EMITTED(t, daddi, { int64_t src = (int64_t)GPR_S64(ctx, 1); int64_t imm = (int64_t)(int32_t)4294967295; int64_t res = (int64_t)((uint64_t)src + (uint64_t)imm); if (((src ^ imm) >= 0) && ((src ^ res) < 0)) runtime->SignalException(ctx, EXCEPTION_INTEGER_OVERFLOW); else SET_GPR_S64(ctx, 2, res); });
            expectGpr(t, *ctx, 2, 0x00000000FFFFFFFFull, "daddi 0x100000000 + (-1) is a 64-bit add");
            t.IsTrue(noException(*ctx), "daddi without overflow must not raise an exception");

            armTrapCheck(*ctx);
            setGpr(*ctx, 1, 0x7FFFFFFFFFFFFFFFull);
            setGpr(*ctx, 2, 0x1234u);
            PS2X_EXEC_EMITTED(t, daddiOne, { int64_t src = (int64_t)GPR_S64(ctx, 1); int64_t imm = (int64_t)(int32_t)1; int64_t res = (int64_t)((uint64_t)src + (uint64_t)imm); if (((src ^ imm) >= 0) && ((src ^ res) < 0)) runtime->SignalException(ctx, EXCEPTION_INTEGER_OVERFLOW); else SET_GPR_S64(ctx, 2, res); });
            t.IsTrue(exceptionRaised(*ctx, EXCEPTION_INTEGER_OVERFLOW), "daddi INT64_MAX + 1 must raise an integer overflow exception");
            expectGpr(t, *ctx, 2, 0x1234u, "daddi overflow must leave rt unchanged");

            armTrapCheck(*ctx);
            setGpr(*ctx, 1, 0x7FFFFFFFFFFFFFFFull);
            PS2X_EXEC_EMITTED(t, daddiu, SET_GPR_U64(ctx, 2, GPR_U64(ctx, 1) + (uint64_t)(int64_t)(int32_t)1););
            expectGpr(t, *ctx, 2, 0x8000000000000000ull, "daddiu INT64_MAX + 1 wraps");
            t.IsTrue(noException(*ctx), "daddiu never raises an exception");

            setGpr(*ctx, 1, 0x0000000100000000ull);
            PS2X_EXEC_EMITTED(t, daddiuNeg, SET_GPR_U64(ctx, 2, GPR_U64(ctx, 1) + (uint64_t)(int64_t)(int32_t)4294967295););
            expectGpr(t, *ctx, 2, 0x00000000FFFFFFFFull, "daddiu sign-extends the immediate to 64 bits");
        });

        tc.Run("Byte, halfword, word and doubleword loads extend as encoded", [](TestCase &t) {
            PS2X_GUEST_SCOPE();
            constexpr uint64_t kUpperHalf = 0x0123456789ABCDEFull;
            writeRam<uint8_t>(rdram, kDataAddress + 0u, 0x80u);
            writeRam<uint16_t>(rdram, kDataAddress + 2u, 0x8000u);
            writeRam<uint32_t>(rdram, kDataAddress + 4u, 0x80000000u);
            writeRam<uint64_t>(rdram, kDataAddress + 8u, 0x8000000000000000ull);
            setGpr(*ctx, 1, kDataAddress);
            setGpr(*ctx, 2, 0u, kUpperHalf);

            PS2X_EXEC_EMITTED(t, decode(encodeI(OPCODE_LB, 1, 2, 0)), SET_GPR_S32(ctx, 2, (int8_t)READ8(ADD32(GPR_U32(ctx, 1), 0))););
            expectGpr(t, *ctx, 2, 0xFFFFFFFFFFFFFF80ull, "lb sign-extends");
            PS2X_EXEC_EMITTED(t, decode(encodeI(OPCODE_LBU, 1, 2, 0)), SET_GPR_ZE32(ctx, 2, (uint8_t)READ8(ADD32(GPR_U32(ctx, 1), 0))););
            expectGpr(t, *ctx, 2, 0x80u, "lbu zero-extends");
            PS2X_EXEC_EMITTED(t, decode(encodeI(OPCODE_LH, 1, 2, 2)), SET_GPR_S32(ctx, 2, (int16_t)READ16(ADD32(GPR_U32(ctx, 1), 2))););
            expectGpr(t, *ctx, 2, 0xFFFFFFFFFFFF8000ull, "lh sign-extends");
            PS2X_EXEC_EMITTED(t, decode(encodeI(OPCODE_LHU, 1, 2, 2)), SET_GPR_ZE32(ctx, 2, (uint16_t)READ16(ADD32(GPR_U32(ctx, 1), 2))););
            expectGpr(t, *ctx, 2, 0x8000u, "lhu zero-extends");
            PS2X_EXEC_EMITTED(t, decode(encodeI(OPCODE_LW, 1, 2, 4)), SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 1), 4))););
            expectGpr(t, *ctx, 2, 0xFFFFFFFF80000000ull, "lw sign-extends");
            PS2X_EXEC_EMITTED(t, decode(encodeI(OPCODE_LWU, 1, 2, 4)), SET_GPR_ZE32(ctx, 2, READ32(ADD32(GPR_U32(ctx, 1), 4))););
            expectGpr(t, *ctx, 2, 0x80000000u, "lwu zero-extends");
            PS2X_EXEC_EMITTED(t, decode(encodeI(OPCODE_LD, 1, 2, 8)), SET_GPR_U64(ctx, 2, READ64(ADD32(GPR_U32(ctx, 1), 8))););
            expectGpr(t, *ctx, 2, 0x8000000000000000ull, "ld loads 64 bits");
            expectU64(t, gprHi(*ctx, 2), kUpperHalf, "scalar loads preserve bits 64..127 of rt");

            // Negative displacements are sign-extended before the 32-bit address add.
            setGpr(*ctx, 1, kDataAddress + 16u);
            PS2X_EXEC_EMITTED(t, decode(encodeI(OPCODE_LB, 1, 2, imm16(-16))), SET_GPR_S32(ctx, 2, (int8_t)READ8(ADD32(GPR_U32(ctx, 1), 4294967280))););
            expectGpr(t, *ctx, 2, 0xFFFFFFFFFFFFFF80ull, "lb with a negative displacement");
        });

        tc.Run("Byte, halfword, word and doubleword stores write only the addressed bytes", [](TestCase &t) {
            PS2X_GUEST_SCOPE();
            fillRam(rdram, kDataAddress, 32u);
            setGpr(*ctx, 1, kDataAddress);
            setGpr(*ctx, 2, 0x1122334455667788ull, 0x99AABBCCDDEEFF00ull);

            PS2X_EXEC_EMITTED(t, decode(encodeI(OPCODE_SB, 1, 2, 0)), WRITE8(ADD32(GPR_U32(ctx, 1), 0), (uint8_t)GPR_U32(ctx, 2)););
            expectU64(t, readRam<uint8_t>(rdram, kDataAddress + 0u), 0x88u, "sb stores the low byte");
            t.IsTrue(ramUntouched(rdram, kDataAddress + 1u, 1u), "sb must not touch the next byte");

            PS2X_EXEC_EMITTED(t, decode(encodeI(OPCODE_SH, 1, 2, 2)), WRITE16(ADD32(GPR_U32(ctx, 1), 2), (uint16_t)GPR_U32(ctx, 2)););
            expectU64(t, readRam<uint16_t>(rdram, kDataAddress + 2u), 0x7788u, "sh stores the low halfword");
            t.IsTrue(ramUntouched(rdram, kDataAddress + 4u, 1u), "sh must not touch the next byte");

            PS2X_EXEC_EMITTED(t, decode(encodeI(OPCODE_SW, 1, 2, 4)), WRITE32(ADD32(GPR_U32(ctx, 1), 4), GPR_U32(ctx, 2)););
            expectU64(t, readRam<uint32_t>(rdram, kDataAddress + 4u), 0x55667788u, "sw stores the low word");
            t.IsTrue(ramUntouched(rdram, kDataAddress + 8u, 1u), "sw must not touch the next byte");

            PS2X_EXEC_EMITTED(t, decode(encodeI(OPCODE_SD, 1, 2, 8)), WRITE64(ADD32(GPR_U32(ctx, 1), 8), GPR_U64(ctx, 2)););
            expectU64(t, readRam<uint64_t>(rdram, kDataAddress + 8u), 0x1122334455667788ull, "sd stores the low doubleword");
            t.IsTrue(ramUntouched(rdram, kDataAddress + 16u, 16u), "sd must not touch the following bytes");

            // Negative displacement.
            setGpr(*ctx, 1, kDataAddress + 32u);
            PS2X_EXEC_EMITTED(t, decode(encodeI(OPCODE_SB, 1, 2, imm16(-16))), WRITE8(ADD32(GPR_U32(ctx, 1), 4294967280), (uint8_t)GPR_U32(ctx, 2)););
            expectU64(t, readRam<uint8_t>(rdram, kDataAddress + 16u), 0x88u, "sb with a negative displacement");
        });

        tc.Run("LQ and SQ move 128 bits and ignore the low four address bits", [](TestCase &t) {
            PS2X_GUEST_SCOPE();
            const Instruction lq = decode(encodeI(OPCODE_LQ, 1, 2, 16)); // lq $2, 16($1)
            const Instruction sq = decode(encodeI(OPCODE_SQ, 1, 2, 16)); // sq $2, 16($1)
            constexpr uint64_t kLo = 0x1122334455667788ull;
            constexpr uint64_t kHi = 0x99AABBCCDDEEFF00ull;

            writeRam<uint64_t>(rdram, kDataAddress + 16u, kLo);
            writeRam<uint64_t>(rdram, kDataAddress + 24u, kHi);
            setGpr(*ctx, 1, kDataAddress);
            PS2X_EXEC_EMITTED(t, lq, SET_GPR_VEC(ctx, 2, READ128(ADD32(GPR_U32(ctx, 1), 16) & ~0xFu)););
            expectGpr(t, *ctx, 2, kLo, "lq low 64 bits");
            expectU64(t, gprHi(*ctx, 2), kHi, "lq high 64 bits");

            setGpr(*ctx, 1, kDataAddress + 5u); // effective address 0x2015 is silently aligned to 0x2010
            setGpr(*ctx, 2, 0u, 0u);
            PS2X_EXEC_EMITTED(t, lq, SET_GPR_VEC(ctx, 2, READ128(ADD32(GPR_U32(ctx, 1), 16) & ~0xFu)););
            expectGpr(t, *ctx, 2, kLo, "lq ignores the low four address bits (low half)");
            expectU64(t, gprHi(*ctx, 2), kHi, "lq ignores the low four address bits (high half)");

            fillRam(rdram, kDataAddress + 32u, 32u);
            setGpr(*ctx, 1, kDataAddress + 16u + 7u); // effective address 0x2027 is silently aligned to 0x2020
            setGpr(*ctx, 2, kHi, kLo);
            PS2X_EXEC_EMITTED(t, sq, WRITE128(ADD32(GPR_U32(ctx, 1), 16) & ~0xFu, GPR_VEC(ctx, 2)););
            expectU64(t, readRam<uint64_t>(rdram, kDataAddress + 32u), kHi, "sq low 64 bits land at the aligned address");
            expectU64(t, readRam<uint64_t>(rdram, kDataAddress + 40u), kLo, "sq high 64 bits land at the aligned address");
            t.IsTrue(ramUntouched(rdram, kDataAddress + 48u, 16u), "sq must not spill past the aligned quadword");

            // $zero reads as an all-zero quadword and is never written.
            setGpr(*ctx, 1, kDataAddress);
            PS2X_EXEC_EMITTED(t, decode(encodeI(OPCODE_SQ, 1, 0, 32)), WRITE128(ADD32(GPR_U32(ctx, 1), 32) & ~0xFu, GPR_VEC(ctx, 0)););
            expectU64(t, readRam<uint64_t>(rdram, kDataAddress + 32u), 0u, "sq $zero stores zeros (low)");
            expectU64(t, readRam<uint64_t>(rdram, kDataAddress + 40u), 0u, "sq $zero stores zeros (high)");
            PS2X_EXEC_EMITTED(t, decode(encodeI(OPCODE_LQ, 1, 0, 16)), SET_GPR_VEC(ctx, 0, READ128(ADD32(GPR_U32(ctx, 1), 16) & ~0xFu)););
            expectGpr(t, *ctx, 0, 0u, "lq $zero leaves $zero at zero");
            expectU64(t, gprHi(*ctx, 0), 0u, "lq $zero leaves the upper half of $zero at zero");
        });

        tc.Run("LWL and LWR merge unaligned words and extend the way the hardware does", [](TestCase &t) {
            PS2X_GUEST_SCOPE();
            const Instruction lwl = decode(encodeI(OPCODE_LWL, 1, 2, 0)); // lwl $2, 0($1)
            const Instruction lwr = decode(encodeI(OPCODE_LWR, 1, 2, 0)); // lwr $2, 0($1)
            constexpr uint32_t kMemoryWord = 0x11223344u;
            constexpr uint32_t kRegisterWord = 0xAABBCCDDu;
            constexpr uint64_t kUpperWord = 0x5555AAAA00000000ull; // bits 32..63 of rt before the load
            constexpr uint64_t kUpperHalf = 0x0123456789ABCDEFull; // bits 64..127 of rt
            writeRam<uint32_t>(rdram, kDataAddress, kMemoryWord);

            for (uint32_t offset = 0; offset < 4u; ++offset)
            {
                const std::string at = " at offset " + std::to_string(offset);
                setGpr(*ctx, 1, kDataAddress + offset);

                setGpr(*ctx, 2, kUpperWord | kRegisterWord, kUpperHalf);
                PS2X_EXEC_EMITTED(t, lwl, { uint32_t addr = ADD32(GPR_U32(ctx, 1), 0); uint32_t aligned_addr = addr & ~3u; uint32_t offset = addr & 3u; uint32_t mem = READ32(aligned_addr); uint32_t shift = (3u - offset) << 3; uint32_t keepMask = (shift == 0) ? 0u : ((1u << shift) - 1u); uint32_t merged = (GPR_U32(ctx, 2) & keepMask) | (mem << shift); SET_GPR_S32(ctx, 2, (int32_t)merged); });
                const uint32_t expectedLeft = static_cast<uint32_t>(refLoadLeft(rdram, kDataAddress + offset, 4u, kRegisterWord));
                expectGpr(t, *ctx, 2, sext32(expectedLeft), "lwl merges and sign-extends" + at);
                expectU64(t, gprHi(*ctx, 2), kUpperHalf, "lwl preserves bits 64..127" + at);

                setGpr(*ctx, 2, kUpperWord | kRegisterWord, kUpperHalf);
                PS2X_EXEC_EMITTED(t, lwr, { uint32_t addr = ADD32(GPR_U32(ctx, 1), 0); uint32_t aligned_addr = addr & ~3u; uint32_t offset = addr & 3u; uint32_t mem = READ32(aligned_addr); uint32_t shift = offset << 3; uint32_t keepMask = (offset == 0) ? 0u : (0xFFFFFFFFu << ((4u - offset) << 3)); uint32_t merged32 = (GPR_U32(ctx, 2) & keepMask) | (mem >> shift); uint64_t merged64 = (GPR_U64(ctx, 2) & 0xFFFFFFFF00000000ull) | (uint64_t)merged32; if (offset == 0) merged64 = (uint64_t)(int64_t)(int32_t)merged32; SET_GPR_U64(ctx, 2, merged64); });
                const uint32_t expectedRight = static_cast<uint32_t>(refLoadRight(rdram, kDataAddress + offset, 4u, kRegisterWord));
                // A full-word LWR sign-extends; a partial one leaves bits 32..63 alone.
                const uint64_t expected = (offset == 0u) ? sext32(expectedRight) : (kUpperWord | expectedRight);
                expectGpr(t, *ctx, 2, expected, "lwr merges" + at);
                expectU64(t, gprHi(*ctx, 2), kUpperHalf, "lwr preserves bits 64..127" + at);
            }

            // Spot-check the reference model against the documented merge tables (mem 11223344, reg AABBCCDD).
            expectU64(t, refLoadLeft(rdram, kDataAddress + 1u, 4u, kRegisterWord), 0x3344CCDDu, "lwl reference at offset 1");
            expectU64(t, refLoadRight(rdram, kDataAddress + 1u, 4u, kRegisterWord), 0xAA112233u, "lwr reference at offset 1");
            expectU64(t, refLoadLeft(rdram, kDataAddress + 3u, 4u, kRegisterWord), 0x11223344u, "lwl reference at offset 3 loads the whole word");
            expectU64(t, refLoadRight(rdram, kDataAddress + 0u, 4u, kRegisterWord), 0x11223344u, "lwr reference at offset 0 loads the whole word");
        });

        tc.Run("LDL and LDR merge unaligned doublewords", [](TestCase &t) {
            PS2X_GUEST_SCOPE();
            const Instruction ldl = decode(encodeI(OPCODE_LDL, 1, 2, 0)); // ldl $2, 0($1)
            const Instruction ldr = decode(encodeI(OPCODE_LDR, 1, 2, 0)); // ldr $2, 0($1)
            constexpr uint64_t kMemoryDword = 0x1122334455667788ull;
            constexpr uint64_t kRegisterDword = 0xAABBCCDDEEFF0011ull;
            constexpr uint64_t kUpperHalf = 0x0123456789ABCDEFull;
            writeRam<uint64_t>(rdram, kDataAddress, kMemoryDword);

            for (uint32_t offset = 0; offset < 8u; ++offset)
            {
                const std::string at = " at offset " + std::to_string(offset);
                setGpr(*ctx, 1, kDataAddress + offset);

                setGpr(*ctx, 2, kRegisterDword, kUpperHalf);
                PS2X_EXEC_EMITTED(t, ldl, { uint32_t addr = ADD32(GPR_U32(ctx, 1), 0); uint32_t aligned_addr = addr & ~7u; uint32_t offset = addr & 7u; uint64_t mem = READ64(aligned_addr); uint32_t shift = (7u - offset) << 3; uint64_t keepMask = (shift == 0) ? 0ull : ((1ull << shift) - 1ull); SET_GPR_U64(ctx, 2, (GPR_U64(ctx, 2) & keepMask) | (mem << shift)); });
                expectGpr(t, *ctx, 2, refLoadLeft(rdram, kDataAddress + offset, 8u, kRegisterDword), "ldl merges" + at);
                expectU64(t, gprHi(*ctx, 2), kUpperHalf, "ldl preserves bits 64..127" + at);

                setGpr(*ctx, 2, kRegisterDword, kUpperHalf);
                PS2X_EXEC_EMITTED(t, ldr, { uint32_t addr = ADD32(GPR_U32(ctx, 1), 0); uint32_t aligned_addr = addr & ~7u; uint32_t offset = addr & 7u; uint64_t mem = READ64(aligned_addr); uint32_t shift = offset << 3; uint64_t keepMask = (offset == 0) ? 0ull : (0xFFFFFFFFFFFFFFFFull << ((8u - offset) << 3)); SET_GPR_U64(ctx, 2, (GPR_U64(ctx, 2) & keepMask) | (mem >> shift)); });
                expectGpr(t, *ctx, 2, refLoadRight(rdram, kDataAddress + offset, 8u, kRegisterDword), "ldr merges" + at);
                expectU64(t, gprHi(*ctx, 2), kUpperHalf, "ldr preserves bits 64..127" + at);
            }

            expectU64(t, refLoadLeft(rdram, kDataAddress + 3u, 8u, kRegisterDword), 0x55667788EEFF0011ull, "ldl reference at offset 3");
            expectU64(t, refLoadRight(rdram, kDataAddress + 3u, 8u, kRegisterDword), 0xAABBCC1122334455ull, "ldr reference at offset 3");
        });

        tc.Run("SWL and SWR store the unaligned parts of a word", [](TestCase &t) {
            PS2X_GUEST_SCOPE();
            const Instruction swl = decode(encodeI(OPCODE_SWL, 1, 2, 0)); // swl $2, 0($1)
            const Instruction swr = decode(encodeI(OPCODE_SWR, 1, 2, 0)); // swr $2, 0($1)
            constexpr uint32_t kMemoryWord = 0x11223344u;
            constexpr uint64_t kRegisterValue = 0x5555AAAAAABBCCDDull; // only the low word takes part
            setGpr(*ctx, 2, kRegisterValue);

            for (uint32_t offset = 0; offset < 4u; ++offset)
            {
                const std::string at = " at offset " + std::to_string(offset);
                setGpr(*ctx, 1, kDataAddress + offset);

                uint8_t expected[8];
                fillRam(rdram, kDataAddress, 8u);
                writeRam<uint32_t>(rdram, kDataAddress, kMemoryWord);
                std::memcpy(expected, rdram + kDataAddress, sizeof(expected));
                refStoreLeft(expected, offset, 4u, kRegisterValue & 0xFFFFFFFFu);
                PS2X_EXEC_EMITTED(t, swl, { uint32_t addr = ADD32(GPR_U32(ctx, 1), 0); uint32_t aligned_addr = addr & ~3u; uint32_t offset = addr & 3u; uint32_t shift = (3u - offset) << 3; uint32_t mask = 0xFFFFFFFFu >> shift; uint32_t old_data = READ32(aligned_addr); uint32_t val = GPR_U32(ctx, 2); uint32_t new_data = (old_data & ~mask) | ((val >> shift) & mask); WRITE32(aligned_addr, new_data); });
                t.IsTrue(std::memcmp(expected, rdram + kDataAddress, sizeof(expected)) == 0, "swl stores the high bytes of rt from the effective address down" + at);

                fillRam(rdram, kDataAddress, 8u);
                writeRam<uint32_t>(rdram, kDataAddress, kMemoryWord);
                std::memcpy(expected, rdram + kDataAddress, sizeof(expected));
                refStoreRight(expected, offset, 4u, kRegisterValue & 0xFFFFFFFFu);
                PS2X_EXEC_EMITTED(t, swr, { uint32_t addr = ADD32(GPR_U32(ctx, 1), 0); uint32_t aligned_addr = addr & ~3u; uint32_t offset = addr & 3u; uint32_t shift = offset << 3; uint32_t mask = 0xFFFFFFFFu << shift; uint32_t old_data = READ32(aligned_addr); uint32_t val = GPR_U32(ctx, 2); uint32_t new_data = (old_data & ~mask) | ((val << shift) & mask); WRITE32(aligned_addr, new_data); });
                t.IsTrue(std::memcmp(expected, rdram + kDataAddress, sizeof(expected)) == 0, "swr stores the low bytes of rt from the effective address up" + at);
            }

            // Spot-check the reference model against the documented tables (mem 11223344, reg AABBCCDD).
            uint8_t word[4];
            writeRam<uint32_t>(word, 0u, kMemoryWord);
            refStoreLeft(word, 1u, 4u, 0xAABBCCDDu);
            expectU64(t, readRam<uint32_t>(word, 0u), 0x1122AABBu, "swl reference at offset 1");
            writeRam<uint32_t>(word, 0u, kMemoryWord);
            refStoreRight(word, 1u, 4u, 0xAABBCCDDu);
            expectU64(t, readRam<uint32_t>(word, 0u), 0xBBCCDD44u, "swr reference at offset 1");
        });

        tc.Run("SDL and SDR store the unaligned parts of a doubleword", [](TestCase &t) {
            PS2X_GUEST_SCOPE();
            const Instruction sdl = decode(encodeI(OPCODE_SDL, 1, 2, 0)); // sdl $2, 0($1)
            const Instruction sdr = decode(encodeI(OPCODE_SDR, 1, 2, 0)); // sdr $2, 0($1)
            constexpr uint64_t kMemoryDword = 0x1122334455667788ull;
            constexpr uint64_t kRegisterDword = 0xAABBCCDDEEFF0011ull;
            setGpr(*ctx, 2, kRegisterDword);

            for (uint32_t offset = 0; offset < 8u; ++offset)
            {
                const std::string at = " at offset " + std::to_string(offset);
                setGpr(*ctx, 1, kDataAddress + offset);

                uint8_t expected[16];
                fillRam(rdram, kDataAddress, 16u);
                writeRam<uint64_t>(rdram, kDataAddress, kMemoryDword);
                std::memcpy(expected, rdram + kDataAddress, sizeof(expected));
                refStoreLeft(expected, offset, 8u, kRegisterDword);
                PS2X_EXEC_EMITTED(t, sdl, { uint32_t addr = ADD32(GPR_U32(ctx, 1), 0); uint32_t aligned_addr = addr & ~7u; uint32_t offset = addr & 7u; uint32_t shift = (7u - offset) << 3; uint64_t mask = 0xFFFFFFFFFFFFFFFFull >> shift; uint64_t old_data = READ64(aligned_addr); uint64_t val = GPR_U64(ctx, 2); uint64_t new_data = (old_data & ~mask) | ((val >> shift) & mask); WRITE64(aligned_addr, new_data); });
                t.IsTrue(std::memcmp(expected, rdram + kDataAddress, sizeof(expected)) == 0, "sdl stores the high bytes of rt from the effective address down" + at);

                fillRam(rdram, kDataAddress, 16u);
                writeRam<uint64_t>(rdram, kDataAddress, kMemoryDword);
                std::memcpy(expected, rdram + kDataAddress, sizeof(expected));
                refStoreRight(expected, offset, 8u, kRegisterDword);
                PS2X_EXEC_EMITTED(t, sdr, { uint32_t addr = ADD32(GPR_U32(ctx, 1), 0); uint32_t aligned_addr = addr & ~7u; uint32_t offset = addr & 7u; uint32_t shift = offset << 3; uint64_t mask = 0xFFFFFFFFFFFFFFFFull << shift; uint64_t old_data = READ64(aligned_addr); uint64_t val = GPR_U64(ctx, 2); uint64_t new_data = (old_data & ~mask) | ((val << shift) & mask); WRITE64(aligned_addr, new_data); });
                t.IsTrue(std::memcmp(expected, rdram + kDataAddress, sizeof(expected)) == 0, "sdr stores the low bytes of rt from the effective address up" + at);
            }
        });

        tc.Run("LWC1 and SWC1 copy raw bits between memory and the FPU", [](TestCase &t) {
            PS2X_GUEST_SCOPE();
            const Instruction lwc1 = decode(encodeI(OPCODE_LWC1, 1, 5, 4)); // lwc1 $f5, 4($1)
            const Instruction swc1 = decode(encodeI(OPCODE_SWC1, 1, 5, 8)); // swc1 $f5, 8($1)
            constexpr uint32_t kMinusOne = 0xBF800000u;

            writeRam<uint32_t>(rdram, kDataAddress + 4u, kMinusOne);
            setGpr(*ctx, 1, kDataAddress);
            PS2X_EXEC_EMITTED(t, lwc1, { uint32_t bits = READ32(ADD32(GPR_U32(ctx, 1), 4)); float f; std::memcpy(&f, &bits, sizeof(f)); ctx->f[5] = f; });
            t.IsTrue(ctx->f[5] == -1.0f, "lwc1 loads the word into the FPU register");

            fillRam(rdram, kDataAddress + 8u, 8u);
            PS2X_EXEC_EMITTED(t, swc1, { float f = ctx->f[5]; uint32_t bits; std::memcpy(&bits, &f, sizeof(bits)); WRITE32(ADD32(GPR_U32(ctx, 1), 8), bits); });
            expectU64(t, readRam<uint32_t>(rdram, kDataAddress + 8u), kMinusOne, "swc1 stores the FPU register bits");
            t.IsTrue(ramUntouched(rdram, kDataAddress + 12u, 4u), "swc1 writes exactly one word");
        });

        tc.Run("LQC2 and SQC2 move quadwords between memory and VU0 registers", [](TestCase &t) {
            PS2X_GUEST_SCOPE();
            const Instruction lqc2 = decode(encodeI(OPCODE_LQC2, 1, 5, 16)); // lqc2 $vf5, 16($1)
            const Instruction sqc2 = decode(encodeI(OPCODE_SQC2, 1, 5, 32)); // sqc2 $vf5, 32($1)
            const uint32_t lanes[4] = {0x3F800000u, 0x40000000u, 0x40400000u, 0x40800000u}; // 1.0f, 2.0f, 3.0f, 4.0f

            std::memcpy(rdram + kDataAddress + 16u, lanes, sizeof(lanes));
            setGpr(*ctx, 1, kDataAddress);
            PS2X_EXEC_EMITTED(t, lqc2, ctx->vu0_vf[5] = _mm_castsi128_ps(READ128(ADD32(GPR_U32(ctx, 1), 16))););
            uint32_t loaded[4];
            std::memcpy(loaded, &ctx->vu0_vf[5], sizeof(loaded));
            t.IsTrue(std::memcmp(loaded, lanes, sizeof(lanes)) == 0, "lqc2 loads x, y, z, w from consecutive words");

            fillRam(rdram, kDataAddress + 32u, 32u);
            PS2X_EXEC_EMITTED(t, sqc2, WRITE128(ADD32(GPR_U32(ctx, 1), 32), _mm_castps_si128(ctx->vu0_vf[5])););
            t.IsTrue(std::memcmp(rdram + kDataAddress + 32u, lanes, sizeof(lanes)) == 0, "sqc2 stores x, y, z, w to consecutive words");
            t.IsTrue(ramUntouched(rdram, kDataAddress + 48u, 16u), "sqc2 writes exactly one quadword");
        });

        tc.Run("LL and SC implement the load-linked reservation", [](TestCase &t) {
            PS2X_GUEST_SCOPE();
            const Instruction ll = decode(encodeI(OPCODE_LL, 1, 2, 0)); // ll $2, 0($1)
            const Instruction sc = decode(encodeI(OPCODE_SC, 1, 2, 0)); // sc $2, 0($1)
            auto runSc = [&] {
                PS2X_EXEC_EMITTED(t, sc, { uint32_t addr = ADD32(GPR_U32(ctx, 1), 0); if (ctx->llbit && ctx->lladdr == addr) { WRITE32(addr, GPR_U32(ctx, 2)); SET_GPR_S32(ctx, 2, 1); } else { SET_GPR_S32(ctx, 2, 0); } ctx->llbit = 0; ctx->lladdr = 0; });
            };

            writeRam<uint32_t>(rdram, kDataAddress, 0x80000001u);
            setGpr(*ctx, 1, kDataAddress);
            PS2X_EXEC_EMITTED(t, ll, { uint32_t addr = ADD32(GPR_U32(ctx, 1), 0); SET_GPR_S32(ctx, 2, (int32_t)READ32(addr)); ctx->llbit = 1; ctx->lladdr = addr; });
            expectGpr(t, *ctx, 2, 0xFFFFFFFF80000001ull, "ll loads and sign-extends the word");
            t.IsTrue(ctx->llbit == 1u && ctx->lladdr == kDataAddress, "ll records the reservation address");

            setGpr(*ctx, 2, 0x55u);
            runSc();
            expectU64(t, readRam<uint32_t>(rdram, kDataAddress), 0x55u, "sc with a live reservation stores the word");
            expectGpr(t, *ctx, 2, 1u, "sc success writes 1 to rt");
            t.IsTrue(ctx->llbit == 0u && ctx->lladdr == 0u, "sc consumes the reservation");

            setGpr(*ctx, 2, 0x66u);
            runSc();
            expectU64(t, readRam<uint32_t>(rdram, kDataAddress), 0x55u, "sc without a reservation must not store");
            expectGpr(t, *ctx, 2, 0u, "sc failure writes 0 to rt");

            PS2X_EXEC_EMITTED(t, ll, { uint32_t addr = ADD32(GPR_U32(ctx, 1), 0); SET_GPR_S32(ctx, 2, (int32_t)READ32(addr)); ctx->llbit = 1; ctx->lladdr = addr; });
            setGpr(*ctx, 1, kDataAddress + 4u); // reservation is for a different address
            setGpr(*ctx, 2, 0x77u);
            runSc();
            expectU64(t, readRam<uint32_t>(rdram, kDataAddress + 4u), 0u, "sc to another address than the reservation must not store");
            expectGpr(t, *ctx, 2, 0u, "sc to another address fails");
            t.IsTrue(ctx->llbit == 0u, "a failed sc still clears the reservation");
        });

        tc.Run("CACHE and PREF are hints that emit no code", [](TestCase &t) {
            expectTranslation(t, decode(encodeI(OPCODE_CACHE, 1, 0x14, 0)), "// CACHE instruction (ignored)");
            expectTranslation(t, decode(encodeI(OPCODE_PREF, 1, 0, 32)), "// PREF instruction (ignored)");
        });

        tc.Run("J and JAL jump within the current 256 MiB segment and JAL links $ra", [](TestCase &t) {
            constexpr uint32_t kTargetField = 0x100000u; // byte target 0x00400000
            const uint32_t jWord = encodeJ(OPCODE_J, kTargetField);
            const uint32_t jalWord = encodeJ(OPCODE_JAL, kTargetField);

            expectTranslation(t, decode(jWord), "// J 0x400000 - Handled by branch logic");
            expectTranslation(t, decode(jalWord), "// JAL 0x400000 - Handled by branch logic");

            const std::string j = emitBranchFunction(jWord);
            const size_t jSlot = j.find(kDelaySlotCode);
            const size_t jTarget = j.find("ctx->pc = 0x400000u;");
            t.IsTrue(jSlot != std::string::npos && jTarget != std::string::npos && jSlot < jTarget,
                     "j executes its delay slot before leaving for the target");
            t.IsTrue(j.find("runtime->dispatchGuestBranch(rdram, ctx, 0x400000u, 0x1000u, 0x0u, PS2Runtime::GuestBranchKind::DirectJump, \"J\")") != std::string::npos,
                     "j to an external target dispatches as a direct jump");
            t.IsTrue(j.find("SET_GPR_U32(ctx, 31") == std::string::npos, "j must not write $ra");

            const std::string jal = emitBranchFunction(jalWord);
            const size_t jalLink = jal.find("SET_GPR_U32(ctx, 31, 0x1008u);");
            const size_t jalSlot = jal.find(kDelaySlotCode);
            t.IsTrue(jalLink != std::string::npos, "jal writes the return address (pc + 8) to $ra");
            t.IsTrue(jalLink != std::string::npos && jalSlot != std::string::npos && jalLink < jalSlot,
                     "jal links before the delay slot so the slot can observe $ra");
            t.IsTrue(jal.find("runtime->dispatchGuestBranch(rdram, ctx, 0x400000u, 0x1000u, 0x1008u, PS2Runtime::GuestBranchKind::DirectCall, \"JAL\")") != std::string::npos,
                     "jal to an external target dispatches as a direct call with its return pc");

            // The upper four bits of the target come from the delay-slot pc.
            const std::string far = emitBranchFunction(jWord, 0x10001000u);
            t.IsTrue(far.find("ctx->pc = 0x10400000u;") != std::string::npos, "j keeps the current 256 MiB segment");

            // An in-function target becomes a local jump.
            const std::string local = emitBranchFunction(encodeJ(OPCODE_J, (kCodeAddress + 12u) >> 2));
            t.IsTrue(local.find("goto label_100c;") != std::string::npos, "j to an in-function address jumps locally");
        });

        tc.Run("BEQ and BNE compare 64 bits; BLEZ and BGTZ test the 64-bit sign", [](TestCase &t) {
            PS2X_GUEST_SCOPE();
            const uint32_t beq = encodeI(OPCODE_BEQ, 1, 2, kBranchOffsetWords);
            const uint32_t bne = encodeI(OPCODE_BNE, 1, 2, kBranchOffsetWords);
            const uint32_t blez = encodeI(OPCODE_BLEZ, 1, 0, kBranchOffsetWords);
            const uint32_t bgtz = encodeI(OPCODE_BGTZ, 1, 0, kBranchOffsetWords);

            setGpr(*ctx, 1, 0x0000000100000005ull);
            setGpr(*ctx, 2, 0x0000000000000005ull); // same low word, different high word
            t.IsFalse(PS2X_BRANCH_TAKEN(t, beq, GPR_U64(ctx, 1) == GPR_U64(ctx, 2)), "beq compares all 64 bits");
            t.IsTrue(PS2X_BRANCH_TAKEN(t, bne, GPR_U64(ctx, 1) != GPR_U64(ctx, 2)), "bne compares all 64 bits");
            setGpr(*ctx, 2, 0x0000000100000005ull);
            t.IsTrue(PS2X_BRANCH_TAKEN(t, beq, GPR_U64(ctx, 1) == GPR_U64(ctx, 2)), "beq is taken on equal registers");
            t.IsFalse(PS2X_BRANCH_TAKEN(t, bne, GPR_U64(ctx, 1) != GPR_U64(ctx, 2)), "bne is not taken on equal registers");

            setGpr(*ctx, 1, 0x0000000080000000ull); // positive as a 64-bit value even though bit 31 is set
            t.IsFalse(PS2X_BRANCH_TAKEN(t, blez, GPR_S64(ctx, 1) <= 0), "blez tests the 64-bit sign, not bit 31");
            t.IsTrue(PS2X_BRANCH_TAKEN(t, bgtz, GPR_S64(ctx, 1) > 0), "bgtz tests the 64-bit sign, not bit 31");
            setGpr(*ctx, 1, 0u);
            t.IsTrue(PS2X_BRANCH_TAKEN(t, blez, GPR_S64(ctx, 1) <= 0), "blez is taken on zero");
            t.IsFalse(PS2X_BRANCH_TAKEN(t, bgtz, GPR_S64(ctx, 1) > 0), "bgtz is not taken on zero");
            setGpr(*ctx, 1, 0xFFFFFFFFFFFFFFFFull);
            t.IsTrue(PS2X_BRANCH_TAKEN(t, blez, GPR_S64(ctx, 1) <= 0), "blez is taken on a negative value");
            t.IsFalse(PS2X_BRANCH_TAKEN(t, bgtz, GPR_S64(ctx, 1) > 0), "bgtz is not taken on a negative value");

            for (uint32_t word : {beq, bne, blez, bgtz})
            {
                expectConditionalBranchShape(t, word, false, false);
            }
        });

        tc.Run("BEQL, BNEL, BLEZL and BGTZL execute the delay slot only when taken", [](TestCase &t) {
            PS2X_GUEST_SCOPE();
            const uint32_t beql = encodeI(OPCODE_BEQL, 1, 2, kBranchOffsetWords);
            const uint32_t bnel = encodeI(OPCODE_BNEL, 1, 2, kBranchOffsetWords);
            const uint32_t blezl = encodeI(OPCODE_BLEZL, 1, 0, kBranchOffsetWords);
            const uint32_t bgtzl = encodeI(OPCODE_BGTZL, 1, 0, kBranchOffsetWords);

            setGpr(*ctx, 1, 7u);
            setGpr(*ctx, 2, 7u);
            t.IsTrue(PS2X_BRANCH_TAKEN(t, beql, GPR_U64(ctx, 1) == GPR_U64(ctx, 2)), "beql is taken on equal registers");
            t.IsFalse(PS2X_BRANCH_TAKEN(t, bnel, GPR_U64(ctx, 1) != GPR_U64(ctx, 2)), "bnel is not taken on equal registers");
            setGpr(*ctx, 1, 0xFFFFFFFF00000000ull); // negative as a 64-bit value even though bit 31 is clear
            t.IsTrue(PS2X_BRANCH_TAKEN(t, blezl, GPR_S64(ctx, 1) <= 0), "blezl tests the 64-bit sign");
            t.IsFalse(PS2X_BRANCH_TAKEN(t, bgtzl, GPR_S64(ctx, 1) > 0), "bgtzl tests the 64-bit sign");

            for (uint32_t word : {beql, bnel, blezl, bgtzl})
            {
                expectConditionalBranchShape(t, word, true, false);
            }
        });

        tc.Run("every MipsOpcodes entry translates and only EE-reserved opcodes reject", [](TestCase &t) {
            // Opcodes the R5900 does not implement: no COP3, no 64-bit FPU loads/stores, no LL/SC
            // doubleword forms and no COP2 word loads/stores (COP2 uses LQC2/SQC2).
            const uint32_t reserved[] = {OPCODE_COP3, OPCODE_LWC2, OPCODE_LLD, OPCODE_LDC1,
                                         OPCODE_SWC2, OPCODE_SCD, OPCODE_SDC1, 0x1Du, 0x3Bu};
            const uint32_t implemented[] = {
                OPCODE_SPECIAL, OPCODE_REGIMM, OPCODE_J, OPCODE_JAL, OPCODE_BEQ, OPCODE_BNE, OPCODE_BLEZ, OPCODE_BGTZ,
                OPCODE_ADDI, OPCODE_ADDIU, OPCODE_SLTI, OPCODE_SLTIU, OPCODE_ANDI, OPCODE_ORI, OPCODE_XORI, OPCODE_LUI,
                OPCODE_COP0, OPCODE_COP1, OPCODE_COP2, OPCODE_BEQL, OPCODE_BNEL, OPCODE_BLEZL, OPCODE_BGTZL,
                OPCODE_DADDI, OPCODE_DADDIU, OPCODE_LDL, OPCODE_LDR, OPCODE_MMI, OPCODE_LQ, OPCODE_SQ,
                OPCODE_LB, OPCODE_LH, OPCODE_LWL, OPCODE_LW, OPCODE_LBU, OPCODE_LHU, OPCODE_LWR, OPCODE_LWU,
                OPCODE_SB, OPCODE_SH, OPCODE_SWL, OPCODE_SW, OPCODE_SDL, OPCODE_SDR, OPCODE_SWR, OPCODE_CACHE,
                OPCODE_LL, OPCODE_LWC1, OPCODE_PREF, OPCODE_LQC2, OPCODE_LD, OPCODE_SC, OPCODE_SWC1, OPCODE_SQC2, OPCODE_SD};

            auto probeWord = [](uint32_t opcode) -> uint32_t {
                switch (opcode)
                {
                case OPCODE_SPECIAL:
                    return encodeR(SPECIAL_ADDU, 1, 3, 2);
                case OPCODE_REGIMM:
                    return encodeRegimm(REGIMM_BGEZ, 1, 4);
                case OPCODE_J:
                case OPCODE_JAL:
                    return encodeJ(opcode, 0x100000u);
                case OPCODE_COP0:
                    return (static_cast<uint32_t>(OPCODE_COP0) << 26) | (COP0_MF << 21) | (2u << 16) | (COP0_REG_STATUS << 11);
                case OPCODE_COP1:
                    return (static_cast<uint32_t>(OPCODE_COP1) << 26) | (COP1_MF << 21) | (2u << 16) | (3u << 11);
                case OPCODE_COP2:
                    return (static_cast<uint32_t>(OPCODE_COP2) << 26) | (COP2_QMFC2 << 21) | (2u << 16) | (3u << 11);
                case OPCODE_MMI:
                    return (static_cast<uint32_t>(OPCODE_MMI) << 26) | (1u << 21) | (3u << 16) | (2u << 11) | MMI_MADD;
                default:
                    return encodeI(opcode, 1, 2, 4);
                }
            };

            auto contains = [](const uint32_t *values, size_t count, uint32_t value) {
                for (size_t i = 0; i < count; ++i)
                {
                    if (values[i] == value)
                        return true;
                }
                return false;
            };

            for (uint32_t opcode = 0; opcode < 64u; ++opcode)
            {
                const bool isReserved = contains(reserved, std::size(reserved), opcode);
                const bool isImplemented = contains(implemented, std::size(implemented), opcode);
                t.IsTrue(isReserved != isImplemented, "opcode " + hex(opcode) + " must be listed exactly once in this manifest");

                const Instruction inst = decode(probeWord(opcode));
                const bool rejects = emitsRuntimeThrow(translate(inst));
                t.Equals(rejects, isReserved, describe(inst) + " (opcode " + hex(opcode) + "): " +
                                                   (isReserved ? "an EE-reserved opcode must emit a runtime error"
                                                               : "an implemented opcode must not emit a runtime error"));
            }
        }); });

    MiniTest::Case("R5900Instructions.SpecialFunctions", [](TestCase &tc)
                   {
        tc.After(releaseGuest);

        tc.Run("SLL shifts the low word, sign-extends and encodes NOP", [](TestCase &t) {
            PS2X_GUEST_SCOPE();
            const Instruction sll4 = decode(encodeR(SPECIAL_SLL, 0, 1, 2, 4));   // sll $2, $1, 4
            const Instruction sll31 = decode(encodeR(SPECIAL_SLL, 0, 1, 2, 31)); // sll $2, $1, 31
            const Instruction sll0 = decode(encodeR(SPECIAL_SLL, 0, 1, 2, 0));   // sll $2, $1, 0

            setGpr32(*ctx, 1, 0x12345678u);
            PS2X_EXEC_EMITTED(t, sll4, SET_GPR_S32(ctx, 2, (int32_t)SLL32(GPR_U32(ctx, 1), 4)););
            expectGpr(t, *ctx, 2, 0x23456780u, "sll by 4");

            setGpr32(*ctx, 1, 1u);
            PS2X_EXEC_EMITTED(t, sll31, SET_GPR_S32(ctx, 2, (int32_t)SLL32(GPR_U32(ctx, 1), 31)););
            expectGpr(t, *ctx, 2, 0xFFFFFFFF80000000ull, "sll into bit 31 sign-extends");

            setGpr(*ctx, 1, 0x00000000FFFFFFFFull);
            PS2X_EXEC_EMITTED(t, sll0, SET_GPR_S32(ctx, 2, (int32_t)SLL32(GPR_U32(ctx, 1), 0)););
            expectGpr(t, *ctx, 2, 0xFFFFFFFFFFFFFFFFull, "sll by 0 canonicalises a 32-bit value into 64 bits");

            expectTranslation(t, decode(0u), "// NOP");
            expectTranslation(t, decode(encodeR(SPECIAL_SLL, 0, 1, 0, 4)), "");
        });

        tc.Run("SRL and SRA shift right logically and arithmetically", [](TestCase &t) {
            PS2X_GUEST_SCOPE();
            const Instruction srl4 = decode(encodeR(SPECIAL_SRL, 0, 1, 2, 4));   // srl $2, $1, 4
            const Instruction srl0 = decode(encodeR(SPECIAL_SRL, 0, 1, 2, 0));   // srl $2, $1, 0
            const Instruction sra4 = decode(encodeR(SPECIAL_SRA, 0, 1, 2, 4));   // sra $2, $1, 4
            const Instruction sra31 = decode(encodeR(SPECIAL_SRA, 0, 1, 2, 31)); // sra $2, $1, 31

            setGpr32(*ctx, 1, 0x80000000u);
            PS2X_EXEC_EMITTED(t, srl4, SET_GPR_S32(ctx, 2, (int32_t)SRL32(GPR_U32(ctx, 1), 4)););
            expectGpr(t, *ctx, 2, 0x08000000u, "srl shifts in zeros");
            PS2X_EXEC_EMITTED(t, srl0, SET_GPR_S32(ctx, 2, (int32_t)SRL32(GPR_U32(ctx, 1), 0)););
            expectGpr(t, *ctx, 2, 0xFFFFFFFF80000000ull, "srl by 0 still sign-extends the 32-bit result");
            PS2X_EXEC_EMITTED(t, sra4, SET_GPR_S32(ctx, 2, SRA32(GPR_S32(ctx, 1), 4)););
            expectGpr(t, *ctx, 2, 0xFFFFFFFFF8000000ull, "sra shifts in the sign bit");
            PS2X_EXEC_EMITTED(t, sra31, SET_GPR_S32(ctx, 2, SRA32(GPR_S32(ctx, 1), 31)););
            expectGpr(t, *ctx, 2, 0xFFFFFFFFFFFFFFFFull, "sra by 31 of a negative value is -1");

            setGpr32(*ctx, 1, 0x7FFFFFFFu);
            PS2X_EXEC_EMITTED(t, sra31, SET_GPR_S32(ctx, 2, SRA32(GPR_S32(ctx, 1), 31)););
            expectGpr(t, *ctx, 2, 0u, "sra by 31 of a positive value is 0");
        });

        tc.Run("SLLV, SRLV and SRAV use the low five bits of rs", [](TestCase &t) {
            PS2X_GUEST_SCOPE();
            const Instruction sllv = decode(encodeR(SPECIAL_SLLV, 3, 1, 2)); // sllv $2, $1, $3
            const Instruction srlv = decode(encodeR(SPECIAL_SRLV, 3, 1, 2)); // srlv $2, $1, $3
            const Instruction srav = decode(encodeR(SPECIAL_SRAV, 3, 1, 2)); // srav $2, $1, $3

            setGpr32(*ctx, 1, 0x80000001u);
            setGpr(*ctx, 3, 33u); // only bits 0..4 count: shift by 1
            PS2X_EXEC_EMITTED(t, sllv, SET_GPR_S32(ctx, 2, (int32_t)SLL32(GPR_U32(ctx, 1), GPR_U32(ctx, 3) & 0x1F)););
            expectGpr(t, *ctx, 2, 2u, "sllv masks the shift amount to 5 bits");
            PS2X_EXEC_EMITTED(t, srlv, SET_GPR_S32(ctx, 2, (int32_t)SRL32(GPR_U32(ctx, 1), GPR_U32(ctx, 3) & 0x1F)););
            expectGpr(t, *ctx, 2, 0x40000000u, "srlv masks the shift amount to 5 bits");
            PS2X_EXEC_EMITTED(t, srav, SET_GPR_S32(ctx, 2, SRA32(GPR_S32(ctx, 1), GPR_U32(ctx, 3) & 0x1F)););
            expectGpr(t, *ctx, 2, 0xFFFFFFFFC0000000ull, "srav masks the shift amount to 5 bits and keeps the sign");

            setGpr(*ctx, 3, 32u); // shift by 0
            PS2X_EXEC_EMITTED(t, sllv, SET_GPR_S32(ctx, 2, (int32_t)SLL32(GPR_U32(ctx, 1), GPR_U32(ctx, 3) & 0x1F)););
            expectGpr(t, *ctx, 2, 0xFFFFFFFF80000001ull, "sllv by 32 is a shift by 0");
        });

        tc.Run("JR and JALR read the target before the delay slot and JALR links", [](TestCase &t) {
            const uint32_t jrRa = encodeR(SPECIAL_JR, 31, 0, 0);      // jr $ra
            const uint32_t jrT9 = encodeR(SPECIAL_JR, 25, 0, 0);      // jr $t9
            const uint32_t jalrT9 = encodeR(SPECIAL_JALR, 25, 0, 31); // jalr $t9
            const uint32_t jalrT0 = encodeR(SPECIAL_JALR, 25, 0, 8);  // jalr $t0, $t9

            expectTranslation(t, decode(jrRa), "// JR $31 - Handled by branch logic");
            expectTranslation(t, decode(jalrT0), "// JALR $8, $25 - Handled by branch logic");

            // The emitter may also replay the delay slot in a resume-from-delay-slot prologue, so the
            // ordering checks start from the jump-target read of the main path.
            const std::string ret = emitBranchFunction(jrRa);
            const size_t retRead = ret.find("const uint32_t jumpTarget = GPR_U32(ctx, 31);");
            t.IsTrue(retRead != std::string::npos, "jr $ra reads $ra into the jump target");
            const size_t retSlot = ret.find(kDelaySlotCode, retRead);
            const size_t retSet = ret.find("ctx->pc = jumpTarget;", retRead);
            t.IsTrue(retSlot != std::string::npos && retSet != std::string::npos,
                     "jr $ra runs the delay slot and publishes the target");
            t.IsTrue(retSlot < retSet, "jr $ra reads its target before the delay slot can modify it");
            t.IsTrue(ret.find("PS2Runtime::GuestBranchKind::Return") != std::string::npos, "jr $ra is a return");
            t.IsTrue(ret.find("SET_GPR_U32(ctx, 31") == std::string::npos, "jr must not write $ra");

            const std::string jump = emitBranchFunction(jrT9);
            t.IsTrue(jump.find("const uint32_t jumpTarget = GPR_U32(ctx, 25);") != std::string::npos, "jr $t9 reads $t9");
            t.IsTrue(jump.find("runtime->dispatchGuestBranch(rdram, ctx, jumpTarget, 0x1000u, 0x0u, PS2Runtime::GuestBranchKind::IndirectJump, \"JR\")") != std::string::npos,
                     "jr through a non-return register dispatches as an indirect jump");

            const std::string call = emitBranchFunction(jalrT9);
            const size_t callRead = call.find("const uint32_t jumpTarget = GPR_U32(ctx, 25);");
            t.IsTrue(callRead != std::string::npos, "jalr reads $t9 into the jump target");
            const size_t callLink = call.find("SET_GPR_U32(ctx, 31, 0x1008u);", callRead);
            const size_t callSlot = call.find(kDelaySlotCode, callRead);
            t.IsTrue(callLink != std::string::npos && callSlot != std::string::npos,
                     "jalr links and runs the delay slot after reading its target");
            t.IsTrue(callLink < callSlot, "jalr links before the delay slot so the slot can observe $ra");
            t.IsTrue(call.find("runtime->dispatchGuestBranch(rdram, ctx, jumpTarget, 0x1000u, 0x1008u, PS2Runtime::GuestBranchKind::IndirectCall, \"JALR\")") != std::string::npos,
                     "jalr dispatches as an indirect call with its return pc");

            const std::string callT0 = emitBranchFunction(jalrT0);
            t.IsTrue(callT0.find("SET_GPR_U32(ctx, 8, 0x1008u);") != std::string::npos, "jalr links into the encoded rd");
            t.IsTrue(callT0.find("SET_GPR_U32(ctx, 31") == std::string::npos, "jalr with an explicit rd must not write $ra");
        });

        tc.Run("MOVZ and MOVN test 64 bits of rt and move 64 bits of rs", [](TestCase &t) {
            PS2X_GUEST_SCOPE();
            const Instruction movz = decode(encodeR(SPECIAL_MOVZ, 1, 3, 2)); // movz $2, $1, $3
            const Instruction movn = decode(encodeR(SPECIAL_MOVN, 1, 3, 2)); // movn $2, $1, $3
            constexpr uint64_t kSource = 0x1122334455667788ull;
            constexpr uint64_t kOriginal = 0x99AABBCCDDEEFF00ull;
            constexpr uint64_t kUpperHalf = 0x0123456789ABCDEFull;
            setGpr(*ctx, 1, kSource, 0xFFFFFFFFFFFFFFFFull);

            setGpr(*ctx, 3, 0u);
            setGpr(*ctx, 2, kOriginal, kUpperHalf);
            PS2X_EXEC_EMITTED(t, movz, if (GPR_U64(ctx, 3) == 0) SET_GPR_U64(ctx, 2, GPR_U64(ctx, 1)););
            expectGpr(t, *ctx, 2, kSource, "movz moves when rt is zero");
            expectU64(t, gprHi(*ctx, 2), kUpperHalf, "movz preserves bits 64..127 of rd");
            setGpr(*ctx, 2, kOriginal, kUpperHalf);
            PS2X_EXEC_EMITTED(t, movn, if (GPR_U64(ctx, 3) != 0) SET_GPR_U64(ctx, 2, GPR_U64(ctx, 1)););
            expectGpr(t, *ctx, 2, kOriginal, "movn does not move when rt is zero");

            setGpr(*ctx, 3, 0x0000000100000000ull); // non-zero only above bit 31
            setGpr(*ctx, 2, kOriginal, kUpperHalf);
            PS2X_EXEC_EMITTED(t, movz, if (GPR_U64(ctx, 3) == 0) SET_GPR_U64(ctx, 2, GPR_U64(ctx, 1)););
            expectGpr(t, *ctx, 2, kOriginal, "movz tests all 64 bits of rt");
            PS2X_EXEC_EMITTED(t, movn, if (GPR_U64(ctx, 3) != 0) SET_GPR_U64(ctx, 2, GPR_U64(ctx, 1)););
            expectGpr(t, *ctx, 2, kSource, "movn moves when any of the 64 bits of rt is set");
            expectU64(t, gprHi(*ctx, 2), kUpperHalf, "movn preserves bits 64..127 of rd");
        });

        tc.Run("SYSCALL, BREAK and SYNC hand control to the runtime or do nothing", [](TestCase &t) {
            PS2X_GUEST_SCOPE();
            const Instruction syscall = decode((0x1234u << 6) | SPECIAL_SYSCALL); // syscall 0x1234
            const Instruction brk = decode((0x7u << 6) | SPECIAL_BREAK);          // break 7
            const Instruction sync = decode((0x10u << 6) | SPECIAL_SYNC);         // sync.p

            expectTranslation(t, syscall, "ctx->pc = 0x1004u;\nruntime->handleSyscall(rdram, ctx, 0x1234u);");

            armTrapCheck(*ctx);
            PS2X_EXEC_EMITTED(t, brk, runtime->handleBreak(rdram, ctx););
            t.IsTrue(exceptionRaised(*ctx, EXCEPTION_BREAKPOINT), "break raises a breakpoint exception at the faulting pc");

            expectTranslation(t, sync, "// SYNC instruction - memory barrier\n// In recompiled code, we don't need explicit memory barriers");
        });

        tc.Run("MFHI, MTHI, MFLO and MTLO move 64 bits", [](TestCase &t) {
            PS2X_GUEST_SCOPE();
            constexpr uint64_t kUpperHalf = 0x0123456789ABCDEFull;
            setGpr(*ctx, 1, 0x8000000000000001ull);
            setGpr(*ctx, 2, 0u, kUpperHalf);

            PS2X_EXEC_EMITTED(t, decode(encodeR(SPECIAL_MTHI, 1, 0, 0)), ctx->hi = GPR_U64(ctx, 1););
            expectU64(t, ctx->hi, 0x8000000000000001ull, "mthi copies all 64 bits");
            PS2X_EXEC_EMITTED(t, decode(encodeR(SPECIAL_MFHI, 0, 0, 2)), SET_GPR_U64(ctx, 2, ctx->hi););
            expectGpr(t, *ctx, 2, 0x8000000000000001ull, "mfhi copies all 64 bits");
            expectU64(t, gprHi(*ctx, 2), kUpperHalf, "mfhi preserves bits 64..127 of rd");

            setGpr(*ctx, 1, 0x7FFFFFFFFFFFFFFEull);
            PS2X_EXEC_EMITTED(t, decode(encodeR(SPECIAL_MTLO, 1, 0, 0)), ctx->lo = GPR_U64(ctx, 1););
            expectU64(t, ctx->lo, 0x7FFFFFFFFFFFFFFEull, "mtlo copies all 64 bits");
            PS2X_EXEC_EMITTED(t, decode(encodeR(SPECIAL_MFLO, 0, 0, 2)), SET_GPR_U64(ctx, 2, ctx->lo););
            expectGpr(t, *ctx, 2, 0x7FFFFFFFFFFFFFFEull, "mflo copies all 64 bits");
        });

        tc.Run("DSLLV, DSRLV and DSRAV use the low six bits of rs", [](TestCase &t) {
            PS2X_GUEST_SCOPE();
            const Instruction dsllv = decode(encodeR(SPECIAL_DSLLV, 3, 1, 2)); // dsllv $2, $1, $3
            const Instruction dsrlv = decode(encodeR(SPECIAL_DSRLV, 3, 1, 2)); // dsrlv $2, $1, $3
            const Instruction dsrav = decode(encodeR(SPECIAL_DSRAV, 3, 1, 2)); // dsrav $2, $1, $3

            setGpr(*ctx, 1, 0x8000000000000001ull);
            setGpr(*ctx, 3, 65u); // only bits 0..5 count: shift by 1
            PS2X_EXEC_EMITTED(t, dsllv, SET_GPR_U64(ctx, 2, GPR_U64(ctx, 1) << (GPR_U32(ctx, 3) & 0x3F)););
            expectGpr(t, *ctx, 2, 2u, "dsllv masks the shift amount to 6 bits");
            PS2X_EXEC_EMITTED(t, dsrlv, SET_GPR_U64(ctx, 2, GPR_U64(ctx, 1) >> (GPR_U32(ctx, 3) & 0x3F)););
            expectGpr(t, *ctx, 2, 0x4000000000000000ull, "dsrlv shifts in zeros");
            PS2X_EXEC_EMITTED(t, dsrav, SET_GPR_S64(ctx, 2, GPR_S64(ctx, 1) >> (GPR_U32(ctx, 3) & 0x3F)););
            expectGpr(t, *ctx, 2, 0xC000000000000000ull, "dsrav shifts in the sign bit");

            setGpr(*ctx, 3, 63u);
            PS2X_EXEC_EMITTED(t, dsrav, SET_GPR_S64(ctx, 2, GPR_S64(ctx, 1) >> (GPR_U32(ctx, 3) & 0x3F)););
            expectGpr(t, *ctx, 2, 0xFFFFFFFFFFFFFFFFull, "dsrav by 63 of a negative value is -1");
        });

        tc.Run("MULT and MULTU write sign-extended halves to LO/HI and optionally rd", [](TestCase &t) {
            PS2X_GUEST_SCOPE();
            const Instruction mult = decode(encodeR(SPECIAL_MULT, 1, 3, 0));    // mult $1, $3
            const Instruction mult3 = decode(encodeR(SPECIAL_MULT, 1, 3, 2));   // mult $2, $1, $3
            const Instruction multu = decode(encodeR(SPECIAL_MULTU, 1, 3, 0));  // multu $1, $3
            const Instruction multu3 = decode(encodeR(SPECIAL_MULTU, 1, 3, 2)); // multu $2, $1, $3

            setGpr32(*ctx, 1, static_cast<uint32_t>(-2));
            setGpr32(*ctx, 3, 3u);
            PS2X_EXEC_EMITTED(t, mult, { int64_t result = (int64_t)GPR_S32(ctx, 1) * (int64_t)GPR_S32(ctx, 3); ctx->lo = (uint64_t)(int64_t)(int32_t)result; ctx->hi = (uint64_t)(int64_t)(int32_t)(result >> 32); });
            expectU64(t, ctx->lo, 0xFFFFFFFFFFFFFFFAull, "mult -2 * 3: LO holds the sign-extended low word");
            expectU64(t, ctx->hi, 0xFFFFFFFFFFFFFFFFull, "mult -2 * 3: HI holds the sign-extended high word");

            setGpr32(*ctx, 1, 0x7FFFFFFFu);
            setGpr32(*ctx, 3, 0x7FFFFFFFu);
            setGpr(*ctx, 2, 0u);
            PS2X_EXEC_EMITTED(t, mult3, { int64_t result = (int64_t)GPR_S32(ctx, 1) * (int64_t)GPR_S32(ctx, 3); ctx->lo = (uint64_t)(int64_t)(int32_t)result; ctx->hi = (uint64_t)(int64_t)(int32_t)(result >> 32); SET_GPR_S32(ctx, 2, (int32_t)result); });
            expectU64(t, ctx->lo, 1u, "mult INT32_MAX^2: LO");
            expectU64(t, ctx->hi, 0x3FFFFFFFu, "mult INT32_MAX^2: HI");
            expectGpr(t, *ctx, 2, 1u, "the three-operand mult also writes LO to rd");

            setGpr32(*ctx, 1, 0xFFFFFFFFu);
            setGpr32(*ctx, 3, 0xFFFFFFFFu);
            PS2X_EXEC_EMITTED(t, multu, { uint64_t result = (uint64_t)GPR_U32(ctx, 1) * (uint64_t)GPR_U32(ctx, 3); ctx->lo = (uint64_t)(int64_t)(int32_t)result; ctx->hi = (uint64_t)(int64_t)(int32_t)(result >> 32); });
            expectU64(t, ctx->lo, 1u, "multu 0xFFFFFFFF^2: LO");
            expectU64(t, ctx->hi, 0xFFFFFFFFFFFFFFFEull, "multu 0xFFFFFFFF^2: HI is sign-extended even though the multiply is unsigned");

            setGpr32(*ctx, 1, 0x80000000u);
            setGpr32(*ctx, 3, 2u);
            PS2X_EXEC_EMITTED(t, multu3, { uint64_t result = (uint64_t)GPR_U32(ctx, 1) * (uint64_t)GPR_U32(ctx, 3); ctx->lo = (uint64_t)(int64_t)(int32_t)result; ctx->hi = (uint64_t)(int64_t)(int32_t)(result >> 32); SET_GPR_S32(ctx, 2, (int32_t)result); });
            expectU64(t, ctx->lo, 0u, "multu 0x80000000 * 2: LO");
            expectU64(t, ctx->hi, 1u, "multu 0x80000000 * 2: HI");
            expectGpr(t, *ctx, 2, 0u, "the three-operand multu also writes LO to rd");
        });

        tc.Run("DIV truncates toward zero and defines division by zero and INT32_MIN / -1", [](TestCase &t) {
            PS2X_GUEST_SCOPE();
            const Instruction div = decode(encodeR(SPECIAL_DIV, 1, 3, 0)); // div $1, $3
            auto runDiv = [&] {
                PS2X_EXEC_EMITTED(t, div, { int32_t divisor = GPR_S32(ctx, 3); int32_t dividend = GPR_S32(ctx, 1); if (divisor != 0) { if (divisor == -1 && dividend == INT32_MIN) { ctx->lo = (uint64_t)(int64_t)INT32_MIN; ctx->hi = 0; } else { ctx->lo = (uint64_t)(int64_t)(dividend / divisor); ctx->hi = (uint64_t)(int64_t)(dividend % divisor); } } else { ctx->lo = (dividend < 0) ? 1ull : 0xFFFFFFFFFFFFFFFFull; ctx->hi = (uint64_t)(int64_t)dividend; } });
            };

            setGpr32(*ctx, 1, 7u);
            setGpr32(*ctx, 3, 2u);
            runDiv();
            expectU64(t, ctx->lo, 3u, "div 7 / 2 quotient");
            expectU64(t, ctx->hi, 1u, "div 7 / 2 remainder");

            setGpr32(*ctx, 1, static_cast<uint32_t>(-7));
            runDiv();
            expectU64(t, ctx->lo, 0xFFFFFFFFFFFFFFFDull, "div -7 / 2 truncates toward zero");
            expectU64(t, ctx->hi, 0xFFFFFFFFFFFFFFFFull, "div -7 / 2 remainder takes the dividend's sign");

            setGpr32(*ctx, 1, 7u);
            setGpr32(*ctx, 3, static_cast<uint32_t>(-2));
            runDiv();
            expectU64(t, ctx->lo, 0xFFFFFFFFFFFFFFFDull, "div 7 / -2 quotient");
            expectU64(t, ctx->hi, 1u, "div 7 / -2 remainder");

            setGpr32(*ctx, 3, 0u);
            runDiv();
            expectU64(t, ctx->lo, 0xFFFFFFFFFFFFFFFFull, "div by zero of a positive dividend leaves LO = -1");
            expectU64(t, ctx->hi, 7u, "div by zero leaves HI = dividend");

            setGpr32(*ctx, 1, static_cast<uint32_t>(-7));
            runDiv();
            expectU64(t, ctx->lo, 1u, "div by zero of a negative dividend leaves LO = 1");
            expectU64(t, ctx->hi, 0xFFFFFFFFFFFFFFF9ull, "div by zero leaves HI = sign-extended dividend");

            setGpr32(*ctx, 1, 0x80000000u);
            setGpr32(*ctx, 3, 0xFFFFFFFFu);
            runDiv();
            expectU64(t, ctx->lo, 0xFFFFFFFF80000000ull, "div INT32_MIN / -1 leaves LO = INT32_MIN");
            expectU64(t, ctx->hi, 0u, "div INT32_MIN / -1 leaves HI = 0");
        });

        tc.Run("DIVU divides unsigned, sign-extends the results and defines division by zero", [](TestCase &t) {
            PS2X_GUEST_SCOPE();
            const Instruction divu = decode(encodeR(SPECIAL_DIVU, 1, 3, 0)); // divu $1, $3
            auto runDivu = [&] {
                PS2X_EXEC_EMITTED(t, divu, { uint32_t divisor = GPR_U32(ctx, 3); if (divisor != 0) { ctx->lo = (uint64_t)(int64_t)(int32_t)(GPR_U32(ctx, 1) / divisor); ctx->hi = (uint64_t)(int64_t)(int32_t)(GPR_U32(ctx, 1) % divisor); } else { ctx->lo = 0xFFFFFFFFFFFFFFFFull; ctx->hi = (uint64_t)(int64_t)(int32_t)GPR_U32(ctx,1); } });
            };

            setGpr32(*ctx, 1, 0xFFFFFFFFu);
            setGpr32(*ctx, 3, 2u);
            runDivu();
            expectU64(t, ctx->lo, 0x7FFFFFFFu, "divu 0xFFFFFFFF / 2 is an unsigned divide");
            expectU64(t, ctx->hi, 1u, "divu 0xFFFFFFFF / 2 remainder");

            setGpr32(*ctx, 3, 1u);
            runDivu();
            expectU64(t, ctx->lo, 0xFFFFFFFFFFFFFFFFull, "divu results are sign-extended into LO");
            expectU64(t, ctx->hi, 0u, "divu 0xFFFFFFFF / 1 remainder");

            setGpr32(*ctx, 1, 0x80000005u);
            setGpr32(*ctx, 3, 0u);
            runDivu();
            expectU64(t, ctx->lo, 0xFFFFFFFFFFFFFFFFull, "divu by zero leaves LO = -1");
            expectU64(t, ctx->hi, 0xFFFFFFFF80000005ull, "divu by zero leaves HI = sign-extended dividend");
        });

        tc.Run("ADD and SUB trap on signed overflow; ADDU and SUBU wrap", [](TestCase &t) {
            PS2X_GUEST_SCOPE();
            const Instruction add = decode(encodeR(SPECIAL_ADD, 1, 3, 2));   // add $2, $1, $3
            const Instruction addu = decode(encodeR(SPECIAL_ADDU, 1, 3, 2)); // addu $2, $1, $3
            const Instruction sub = decode(encodeR(SPECIAL_SUB, 1, 3, 2));   // sub $2, $1, $3
            const Instruction subu = decode(encodeR(SPECIAL_SUBU, 1, 3, 2)); // subu $2, $1, $3
            auto runAdd = [&] {
                PS2X_EXEC_EMITTED(t, add, { int32_t rs_val = GPR_S32(ctx, 1); int32_t rt_val = GPR_S32(ctx, 3); int64_t result = (int64_t)rs_val + (int64_t)rt_val; if (result > INT32_MAX || result < INT32_MIN) { runtime->SignalException(ctx, EXCEPTION_INTEGER_OVERFLOW); } else { SET_GPR_S32(ctx, 2, (int32_t)result); } });
            };
            auto runSub = [&] {
                PS2X_EXEC_EMITTED(t, sub, { uint32_t tmp; bool ov; SUB32_OV(GPR_U32(ctx, 1), GPR_U32(ctx, 3), tmp, ov); if (ov) runtime->SignalException(ctx, EXCEPTION_INTEGER_OVERFLOW); else SET_GPR_S32(ctx, 2, (int32_t)tmp); });
            };

            armTrapCheck(*ctx);
            setGpr32(*ctx, 1, 0x7FFFFFFEu);
            setGpr32(*ctx, 3, 1u);
            runAdd();
            expectGpr(t, *ctx, 2, 0x7FFFFFFFu, "add 0x7FFFFFFE + 1");
            t.IsTrue(noException(*ctx), "add without overflow must not raise an exception");

            armTrapCheck(*ctx);
            setGpr32(*ctx, 1, 0x7FFFFFFFu);
            setGpr(*ctx, 2, 0x1234u);
            runAdd();
            t.IsTrue(exceptionRaised(*ctx, EXCEPTION_INTEGER_OVERFLOW), "add INT32_MAX + 1 must raise an integer overflow exception");
            expectGpr(t, *ctx, 2, 0x1234u, "add overflow must leave rd unchanged");

            armTrapCheck(*ctx);
            PS2X_EXEC_EMITTED(t, addu, SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 1), GPR_U32(ctx, 3))););
            expectGpr(t, *ctx, 2, 0xFFFFFFFF80000000ull, "addu INT32_MAX + 1 wraps and sign-extends");
            t.IsTrue(noException(*ctx), "addu never raises an exception");

            armTrapCheck(*ctx);
            setGpr32(*ctx, 1, 0x80000000u);
            setGpr32(*ctx, 3, 1u);
            setGpr(*ctx, 2, 0x1234u);
            runSub();
            t.IsTrue(exceptionRaised(*ctx, EXCEPTION_INTEGER_OVERFLOW), "sub INT32_MIN - 1 must raise an integer overflow exception");
            expectGpr(t, *ctx, 2, 0x1234u, "sub overflow must leave rd unchanged");

            armTrapCheck(*ctx);
            setGpr32(*ctx, 1, 0x7FFFFFFFu);
            setGpr32(*ctx, 3, 0xFFFFFFFFu);
            runSub();
            t.IsTrue(exceptionRaised(*ctx, EXCEPTION_INTEGER_OVERFLOW), "sub INT32_MAX - (-1) must raise an integer overflow exception");

            armTrapCheck(*ctx);
            setGpr32(*ctx, 1, 5u);
            setGpr32(*ctx, 3, 7u);
            runSub();
            expectGpr(t, *ctx, 2, 0xFFFFFFFFFFFFFFFEull, "sub 5 - 7");
            t.IsTrue(noException(*ctx), "sub without overflow must not raise an exception");

            setGpr32(*ctx, 1, 0x80000000u);
            setGpr32(*ctx, 3, 1u);
            PS2X_EXEC_EMITTED(t, subu, SET_GPR_S32(ctx, 2, (int32_t)SUB32(GPR_U32(ctx, 1), GPR_U32(ctx, 3))););
            expectGpr(t, *ctx, 2, 0x7FFFFFFFu, "subu INT32_MIN - 1 wraps");
            t.IsTrue(noException(*ctx), "subu never raises an exception");
        });

        tc.Run("AND, OR, XOR and NOR operate on 64 bits", [](TestCase &t) {
            PS2X_GUEST_SCOPE();
            constexpr uint64_t kUpperHalf = 0x0123456789ABCDEFull;
            setGpr(*ctx, 1, 0xF0F0F0F0F0F0F0F0ull);
            setGpr(*ctx, 3, 0xFF00FF00FF00FF00ull);
            setGpr(*ctx, 2, 0u, kUpperHalf);

            PS2X_EXEC_EMITTED(t, decode(encodeR(SPECIAL_AND, 1, 3, 2)), SET_GPR_U64(ctx, 2, GPR_U64(ctx, 1) & GPR_U64(ctx, 3)););
            expectGpr(t, *ctx, 2, 0xF000F000F000F000ull, "and");
            PS2X_EXEC_EMITTED(t, decode(encodeR(SPECIAL_OR, 1, 3, 2)), SET_GPR_U64(ctx, 2, GPR_U64(ctx, 1) | GPR_U64(ctx, 3)););
            expectGpr(t, *ctx, 2, 0xFFF0FFF0FFF0FFF0ull, "or");
            PS2X_EXEC_EMITTED(t, decode(encodeR(SPECIAL_XOR, 1, 3, 2)), SET_GPR_U64(ctx, 2, GPR_U64(ctx, 1) ^ GPR_U64(ctx, 3)););
            expectGpr(t, *ctx, 2, 0x0FF00FF00FF00FF0ull, "xor");
            PS2X_EXEC_EMITTED(t, decode(encodeR(SPECIAL_NOR, 1, 3, 2)), SET_GPR_U64(ctx, 2, ~(GPR_U64(ctx, 1) | GPR_U64(ctx, 3))););
            expectGpr(t, *ctx, 2, 0x000F000F000F000Full, "nor");
            expectU64(t, gprHi(*ctx, 2), kUpperHalf, "logic ops preserve bits 64..127 of rd");
        });

        tc.Run("SLT and SLTU compare 64 bits signed and unsigned", [](TestCase &t) {
            PS2X_GUEST_SCOPE();
            const Instruction slt = decode(encodeR(SPECIAL_SLT, 1, 3, 2));   // slt $2, $1, $3
            const Instruction sltu = decode(encodeR(SPECIAL_SLTU, 1, 3, 2)); // sltu $2, $1, $3

            setGpr(*ctx, 1, 0xFFFFFFFFFFFFFFFFull); // -1 signed, max unsigned
            setGpr(*ctx, 3, 1u);
            PS2X_EXEC_EMITTED(t, slt, SET_GPR_U64(ctx, 2, ((int64_t)GPR_S64(ctx, 1) < (int64_t)GPR_S64(ctx, 3)) ? 1 : 0););
            expectGpr(t, *ctx, 2, 1u, "slt -1 < 1");
            PS2X_EXEC_EMITTED(t, sltu, SET_GPR_U64(ctx, 2, ((uint64_t)GPR_U64(ctx, 1) < (uint64_t)GPR_U64(ctx, 3)) ? 1 : 0););
            expectGpr(t, *ctx, 2, 0u, "sltu 0xFFFFFFFFFFFFFFFF < 1 is false");

            setGpr(*ctx, 1, 0x00000000FFFFFFFFull); // positive as 64 bits even though bit 31 is set
            setGpr(*ctx, 3, 0u);
            PS2X_EXEC_EMITTED(t, slt, SET_GPR_U64(ctx, 2, ((int64_t)GPR_S64(ctx, 1) < (int64_t)GPR_S64(ctx, 3)) ? 1 : 0););
            expectGpr(t, *ctx, 2, 0u, "slt compares 64 bits, not the low word");
            setGpr(*ctx, 3, 0x0000000100000000ull);
            PS2X_EXEC_EMITTED(t, sltu, SET_GPR_U64(ctx, 2, ((uint64_t)GPR_U64(ctx, 1) < (uint64_t)GPR_U64(ctx, 3)) ? 1 : 0););
            expectGpr(t, *ctx, 2, 1u, "sltu 0xFFFFFFFF < 0x100000000");
        });

        tc.Run("MTSA and MFSA access the shift amount register", [](TestCase &t) {
            PS2X_GUEST_SCOPE();
            const Instruction mtsa = decode(encodeR(SPECIAL_MTSA, 1, 0, 0)); // mtsa $1
            const Instruction mfsa = decode(encodeR(SPECIAL_MFSA, 0, 0, 2)); // mfsa $2
            constexpr uint64_t kUpperHalf = 0x0123456789ABCDEFull;

            setGpr(*ctx, 1, 0x30u);
            PS2X_EXEC_EMITTED(t, mtsa, ctx->sa = GPR_U32(ctx, 1) & 0x7F;);
            expectU64(t, ctx->sa, 0x30u, "mtsa stores the shift amount");

            setGpr(*ctx, 2, 0xFFFFFFFFFFFFFFFFull, kUpperHalf);
            PS2X_EXEC_EMITTED(t, mfsa, SET_GPR_U32(ctx, 2, ctx->sa););
            expectGpr(t, *ctx, 2, 0x30u, "mfsa reads the shift amount back");
            expectU64(t, gprHi(*ctx, 2), kUpperHalf, "mfsa preserves bits 64..127 of rd");
        });

        tc.Run("DADD and DSUB trap on 64-bit overflow; DADDU and DSUBU wrap", [](TestCase &t) {
            PS2X_GUEST_SCOPE();
            const Instruction dadd = decode(encodeR(SPECIAL_DADD, 1, 3, 2));   // dadd $2, $1, $3
            const Instruction daddu = decode(encodeR(SPECIAL_DADDU, 1, 3, 2)); // daddu $2, $1, $3
            const Instruction dsub = decode(encodeR(SPECIAL_DSUB, 1, 3, 2));   // dsub $2, $1, $3
            const Instruction dsubu = decode(encodeR(SPECIAL_DSUBU, 1, 3, 2)); // dsubu $2, $1, $3
            auto runDadd = [&] {
                PS2X_EXEC_EMITTED(t, dadd, { int64_t a = (int64_t)GPR_S64(ctx, 1); int64_t b = (int64_t)GPR_S64(ctx, 3); int64_t r = (int64_t)((uint64_t)a + (uint64_t)b); if (((a ^ b) >= 0) && ((a ^ r) < 0)) runtime->SignalException(ctx, EXCEPTION_INTEGER_OVERFLOW); else SET_GPR_S64(ctx, 2, r); });
            };
            auto runDsub = [&] {
                PS2X_EXEC_EMITTED(t, dsub, { int64_t a = (int64_t)GPR_S64(ctx, 1); int64_t b = (int64_t)GPR_S64(ctx, 3); int64_t r = (int64_t)((uint64_t)a - (uint64_t)b); if (((a ^ b) < 0) && ((a ^ r) < 0)) runtime->SignalException(ctx, EXCEPTION_INTEGER_OVERFLOW); else SET_GPR_S64(ctx, 2, r); });
            };

            armTrapCheck(*ctx);
            setGpr(*ctx, 1, 0x00000000FFFFFFFFull);
            setGpr(*ctx, 3, 1u);
            runDadd();
            expectGpr(t, *ctx, 2, 0x0000000100000000ull, "dadd carries past bit 31 without trapping");
            t.IsTrue(noException(*ctx), "dadd without overflow must not raise an exception");

            armTrapCheck(*ctx);
            setGpr(*ctx, 1, 0x7FFFFFFFFFFFFFFFull);
            setGpr(*ctx, 2, 0x1234u);
            runDadd();
            t.IsTrue(exceptionRaised(*ctx, EXCEPTION_INTEGER_OVERFLOW), "dadd INT64_MAX + 1 must raise an integer overflow exception");
            expectGpr(t, *ctx, 2, 0x1234u, "dadd overflow must leave rd unchanged");

            armTrapCheck(*ctx);
            PS2X_EXEC_EMITTED(t, daddu, SET_GPR_U64(ctx, 2, (uint64_t)GPR_U64(ctx, 1) + (uint64_t)GPR_U64(ctx, 3)););
            expectGpr(t, *ctx, 2, 0x8000000000000000ull, "daddu INT64_MAX + 1 wraps");
            t.IsTrue(noException(*ctx), "daddu never raises an exception");

            armTrapCheck(*ctx);
            setGpr(*ctx, 1, 0x8000000000000000ull);
            setGpr(*ctx, 3, 1u);
            setGpr(*ctx, 2, 0x1234u);
            runDsub();
            t.IsTrue(exceptionRaised(*ctx, EXCEPTION_INTEGER_OVERFLOW), "dsub INT64_MIN - 1 must raise an integer overflow exception");
            expectGpr(t, *ctx, 2, 0x1234u, "dsub overflow must leave rd unchanged");

            armTrapCheck(*ctx);
            setGpr(*ctx, 1, 0u);
            setGpr(*ctx, 3, 1u);
            runDsub();
            expectGpr(t, *ctx, 2, 0xFFFFFFFFFFFFFFFFull, "dsub 0 - 1");
            t.IsTrue(noException(*ctx), "dsub without overflow must not raise an exception");

            setGpr(*ctx, 1, 0x8000000000000000ull);
            PS2X_EXEC_EMITTED(t, dsubu, SET_GPR_U64(ctx, 2, GPR_U64(ctx, 1) - GPR_U64(ctx, 3)););
            expectGpr(t, *ctx, 2, 0x7FFFFFFFFFFFFFFFull, "dsubu INT64_MIN - 1 wraps");
            t.IsTrue(noException(*ctx), "dsubu never raises an exception");
        });

        tc.Run("TGE, TGEU, TLT, TLTU, TEQ and TNE trap exactly when their 64-bit condition holds", [](TestCase &t) {
            PS2X_GUEST_SCOPE();
            const Instruction tge = decode(encodeR(SPECIAL_TGE, 1, 3, 0));   // tge $1, $3
            const Instruction tgeu = decode(encodeR(SPECIAL_TGEU, 1, 3, 0)); // tgeu $1, $3
            const Instruction tlt = decode(encodeR(SPECIAL_TLT, 1, 3, 0));   // tlt $1, $3
            const Instruction tltu = decode(encodeR(SPECIAL_TLTU, 1, 3, 0)); // tltu $1, $3
            const Instruction teq = decode(encodeR(SPECIAL_TEQ, 1, 3, 0));   // teq $1, $3
            const Instruction tne = decode(encodeR(SPECIAL_TNE, 1, 3, 0));   // tne $1, $3

            // rs = -1 (signed) / max (unsigned), rt = 1.
            setGpr(*ctx, 1, 0xFFFFFFFFFFFFFFFFull);
            setGpr(*ctx, 3, 1u);

            armTrapCheck(*ctx);
            PS2X_EXEC_EMITTED(t, tge, if (GPR_S64(ctx, 1) >= GPR_S64(ctx, 3)) { runtime->handleTrap(rdram, ctx); });
            t.IsTrue(noException(*ctx), "tge: -1 >= 1 is false, no trap");
            armTrapCheck(*ctx);
            PS2X_EXEC_EMITTED(t, tgeu, if (GPR_U64(ctx, 1) >= GPR_U64(ctx, 3)) { runtime->handleTrap(rdram, ctx); });
            t.IsTrue(exceptionRaised(*ctx, EXCEPTION_TRAP), "tgeu: 0xFFFFFFFFFFFFFFFF >= 1 traps");
            armTrapCheck(*ctx);
            PS2X_EXEC_EMITTED(t, tlt, if (GPR_S64(ctx, 1) < GPR_S64(ctx, 3)) { runtime->handleTrap(rdram, ctx); });
            t.IsTrue(exceptionRaised(*ctx, EXCEPTION_TRAP), "tlt: -1 < 1 traps");
            armTrapCheck(*ctx);
            PS2X_EXEC_EMITTED(t, tltu, if (GPR_U64(ctx, 1) < GPR_U64(ctx, 3)) { runtime->handleTrap(rdram, ctx); });
            t.IsTrue(noException(*ctx), "tltu: 0xFFFFFFFFFFFFFFFF < 1 is false, no trap");
            armTrapCheck(*ctx);
            PS2X_EXEC_EMITTED(t, teq, if (GPR_U64(ctx, 1) == GPR_U64(ctx, 3)) { runtime->handleTrap(rdram, ctx); });
            t.IsTrue(noException(*ctx), "teq on different values does not trap");
            armTrapCheck(*ctx);
            PS2X_EXEC_EMITTED(t, tne, if (GPR_U64(ctx, 1) != GPR_U64(ctx, 3)) { runtime->handleTrap(rdram, ctx); });
            t.IsTrue(exceptionRaised(*ctx, EXCEPTION_TRAP), "tne on different values traps");

            // Equal low words, different high words: every compare must look at all 64 bits.
            setGpr(*ctx, 1, 0x0000000100000005ull);
            setGpr(*ctx, 3, 0x0000000000000005ull);
            armTrapCheck(*ctx);
            PS2X_EXEC_EMITTED(t, teq, if (GPR_U64(ctx, 1) == GPR_U64(ctx, 3)) { runtime->handleTrap(rdram, ctx); });
            t.IsTrue(noException(*ctx), "teq compares all 64 bits");
            armTrapCheck(*ctx);
            PS2X_EXEC_EMITTED(t, tge, if (GPR_S64(ctx, 1) >= GPR_S64(ctx, 3)) { runtime->handleTrap(rdram, ctx); });
            t.IsTrue(exceptionRaised(*ctx, EXCEPTION_TRAP), "tge compares all 64 bits");
            armTrapCheck(*ctx);
            PS2X_EXEC_EMITTED(t, tltu, if (GPR_U64(ctx, 1) < GPR_U64(ctx, 3)) { runtime->handleTrap(rdram, ctx); });
            t.IsTrue(noException(*ctx), "tltu compares all 64 bits");
        });

        tc.Run("DSLL, DSRL, DSRA and their +32 forms shift doublewords", [](TestCase &t) {
            PS2X_GUEST_SCOPE();
            setGpr(*ctx, 1, 0x8000000000000001ull);

            PS2X_EXEC_EMITTED(t, decode(encodeR(SPECIAL_DSLL, 0, 1, 2, 4)), SET_GPR_U64(ctx, 2, GPR_U64(ctx, 1) << 4););
            expectGpr(t, *ctx, 2, 0x0000000000000010ull, "dsll by 4");
            PS2X_EXEC_EMITTED(t, decode(encodeR(SPECIAL_DSRL, 0, 1, 2, 4)), SET_GPR_U64(ctx, 2, GPR_U64(ctx, 1) >> 4););
            expectGpr(t, *ctx, 2, 0x0800000000000000ull, "dsrl by 4 shifts in zeros");
            PS2X_EXEC_EMITTED(t, decode(encodeR(SPECIAL_DSRA, 0, 1, 2, 4)), SET_GPR_S64(ctx, 2, GPR_S64(ctx, 1) >> 4););
            expectGpr(t, *ctx, 2, 0xF800000000000000ull, "dsra by 4 shifts in the sign bit");
            PS2X_EXEC_EMITTED(t, decode(encodeR(SPECIAL_DSLL32, 0, 1, 2, 4)), SET_GPR_U64(ctx, 2, GPR_U64(ctx, 1) << (32 + 4)););
            expectGpr(t, *ctx, 2, 0x0000001000000000ull, "dsll32 by 4 shifts by 36");
            PS2X_EXEC_EMITTED(t, decode(encodeR(SPECIAL_DSRL32, 0, 1, 2, 4)), SET_GPR_U64(ctx, 2, GPR_U64(ctx, 1) >> (32 + 4)););
            expectGpr(t, *ctx, 2, 0x0000000008000000ull, "dsrl32 by 4 shifts by 36");
            PS2X_EXEC_EMITTED(t, decode(encodeR(SPECIAL_DSRA32, 0, 1, 2, 4)), SET_GPR_S64(ctx, 2, GPR_S64(ctx, 1) >> (32 + 4)););
            expectGpr(t, *ctx, 2, 0xFFFFFFFFF8000000ull, "dsra32 by 4 shifts by 36 and keeps the sign");
            PS2X_EXEC_EMITTED(t, decode(encodeR(SPECIAL_DSRA32, 0, 1, 2, 31)), SET_GPR_S64(ctx, 2, GPR_S64(ctx, 1) >> (32 + 31)););
            expectGpr(t, *ctx, 2, 0xFFFFFFFFFFFFFFFFull, "dsra32 by 31 of a negative value is -1");
        });

        tc.Run("every SpecialFunctions entry translates and only reserved function codes reject", [](TestCase &t) {
            // The R5900 has no DMULT/DMULTU/DDIV/DDIVU (0x1C..0x1F) and the other holes are reserved.
            const uint32_t implemented[] = {
                SPECIAL_SLL, SPECIAL_SRL, SPECIAL_SRA, SPECIAL_SLLV, SPECIAL_SRLV, SPECIAL_SRAV,
                SPECIAL_JR, SPECIAL_JALR, SPECIAL_MOVZ, SPECIAL_MOVN, SPECIAL_SYSCALL, SPECIAL_BREAK, SPECIAL_SYNC,
                SPECIAL_MFHI, SPECIAL_MTHI, SPECIAL_MFLO, SPECIAL_MTLO, SPECIAL_DSLLV, SPECIAL_DSRLV, SPECIAL_DSRAV,
                SPECIAL_MULT, SPECIAL_MULTU, SPECIAL_DIV, SPECIAL_DIVU,
                SPECIAL_ADD, SPECIAL_ADDU, SPECIAL_SUB, SPECIAL_SUBU, SPECIAL_AND, SPECIAL_OR, SPECIAL_XOR, SPECIAL_NOR,
                SPECIAL_MFSA, SPECIAL_MTSA, SPECIAL_SLT, SPECIAL_SLTU, SPECIAL_DADD, SPECIAL_DADDU, SPECIAL_DSUB, SPECIAL_DSUBU,
                SPECIAL_TGE, SPECIAL_TGEU, SPECIAL_TLT, SPECIAL_TLTU, SPECIAL_TEQ, SPECIAL_TNE,
                SPECIAL_DSLL, SPECIAL_DSRL, SPECIAL_DSRA, SPECIAL_DSLL32, SPECIAL_DSRL32, SPECIAL_DSRA32};
            t.Equals(std::size(implemented), static_cast<size_t>(52), "the manifest must list every SpecialFunctions enumerator");

            for (uint32_t function = 0; function < 64u; ++function)
            {
                bool isImplemented = false;
                for (uint32_t value : implemented)
                {
                    isImplemented = isImplemented || value == function;
                }

                const Instruction inst = decode(encodeR(function, 1, 3, 2, 4));
                const bool rejects = emitsRuntimeThrow(translate(inst));
                t.Equals(rejects, !isImplemented, describe(inst) + " (function " + hex(function) + "): " +
                                                      (isImplemented ? "an implemented SPECIAL function must not emit a runtime error"
                                                                     : "a reserved SPECIAL function must emit a runtime error"));
            }
        }); });

    MiniTest::Case("R5900Instructions.RegimmFunctions", [](TestCase &tc)
                   {
        tc.After(releaseGuest);

        tc.Run("BLTZ, BGEZ and their likely forms test the 64-bit sign", [](TestCase &t) {
            PS2X_GUEST_SCOPE();
            const uint32_t bltz = encodeRegimm(REGIMM_BLTZ, 1, kBranchOffsetWords);
            const uint32_t bgez = encodeRegimm(REGIMM_BGEZ, 1, kBranchOffsetWords);
            const uint32_t bltzl = encodeRegimm(REGIMM_BLTZL, 1, kBranchOffsetWords);
            const uint32_t bgezl = encodeRegimm(REGIMM_BGEZL, 1, kBranchOffsetWords);

            expectTranslation(t, decode(bltz), "// REGIMM branch instruction to 0x100C - Handled by branch logic");

            setGpr(*ctx, 1, 0x0000000080000000ull); // positive as a 64-bit value even though bit 31 is set
            t.IsFalse(PS2X_BRANCH_TAKEN(t, bltz, GPR_S64(ctx, 1) < 0), "bltz tests the 64-bit sign, not bit 31");
            t.IsTrue(PS2X_BRANCH_TAKEN(t, bgez, GPR_S64(ctx, 1) >= 0), "bgez tests the 64-bit sign, not bit 31");
            setGpr(*ctx, 1, 0xFFFFFFFF00000000ull); // negative as a 64-bit value even though bit 31 is clear
            t.IsTrue(PS2X_BRANCH_TAKEN(t, bltzl, GPR_S64(ctx, 1) < 0), "bltzl tests the 64-bit sign");
            t.IsFalse(PS2X_BRANCH_TAKEN(t, bgezl, GPR_S64(ctx, 1) >= 0), "bgezl tests the 64-bit sign");
            setGpr(*ctx, 1, 0u);
            t.IsFalse(PS2X_BRANCH_TAKEN(t, bltz, GPR_S64(ctx, 1) < 0), "bltz is not taken on zero");
            t.IsTrue(PS2X_BRANCH_TAKEN(t, bgez, GPR_S64(ctx, 1) >= 0), "bgez is taken on zero");

            expectConditionalBranchShape(t, bltz, false, false);
            expectConditionalBranchShape(t, bgez, false, false);
            expectConditionalBranchShape(t, bltzl, true, false);
            expectConditionalBranchShape(t, bgezl, true, false);
        });

        tc.Run("BLTZAL, BGEZAL, BLTZALL and BGEZALL link $ra unconditionally", [](TestCase &t) {
            PS2X_GUEST_SCOPE();
            const uint32_t bltzal = encodeRegimm(REGIMM_BLTZAL, 1, kBranchOffsetWords);
            const uint32_t bgezal = encodeRegimm(REGIMM_BGEZAL, 1, kBranchOffsetWords);
            const uint32_t bltzall = encodeRegimm(REGIMM_BLTZALL, 1, kBranchOffsetWords);
            const uint32_t bgezall = encodeRegimm(REGIMM_BGEZALL, 1, kBranchOffsetWords);

            setGpr(*ctx, 1, 0xFFFFFFFFFFFFFFFFull);
            t.IsTrue(PS2X_BRANCH_TAKEN(t, bltzal, GPR_S64(ctx, 1) < 0), "bltzal is taken on a negative value");
            t.IsFalse(PS2X_BRANCH_TAKEN(t, bgezal, GPR_S64(ctx, 1) >= 0), "bgezal is not taken on a negative value");
            t.IsTrue(PS2X_BRANCH_TAKEN(t, bltzall, GPR_S64(ctx, 1) < 0), "bltzall is taken on a negative value");
            t.IsFalse(PS2X_BRANCH_TAKEN(t, bgezall, GPR_S64(ctx, 1) >= 0), "bgezall is not taken on a negative value");

            expectConditionalBranchShape(t, bltzal, false, true);
            expectConditionalBranchShape(t, bgezal, false, true);
            expectConditionalBranchShape(t, bltzall, true, true);
            expectConditionalBranchShape(t, bgezall, true, true);

            // bgezal $zero is the canonical "bal": always taken, always linking.
            const uint32_t bal = encodeRegimm(REGIMM_BGEZAL, 0, kBranchOffsetWords);
            t.IsTrue(PS2X_BRANCH_TAKEN(t, bal, GPR_S64(ctx, 0) >= 0), "bal is always taken");
        });

        tc.Run("TGEI, TGEIU, TLTI, TLTIU, TEQI and TNEI trap against the sign-extended immediate", [](TestCase &t) {
            PS2X_GUEST_SCOPE();
            const Instruction tgei = decode(encodeRegimm(REGIMM_TGEI, 1, imm16(-1)));   // tgei $1, -1
            const Instruction tgeiu = decode(encodeRegimm(REGIMM_TGEIU, 1, imm16(-1))); // tgeiu $1, -1
            const Instruction tlti = decode(encodeRegimm(REGIMM_TLTI, 1, imm16(-1)));   // tlti $1, -1
            const Instruction tltiu = decode(encodeRegimm(REGIMM_TLTIU, 1, imm16(-1))); // tltiu $1, -1
            const Instruction teqi = decode(encodeRegimm(REGIMM_TEQI, 1, 5));           // teqi $1, 5
            const Instruction tnei = decode(encodeRegimm(REGIMM_TNEI, 1, 5));           // tnei $1, 5

            setGpr(*ctx, 1, 0xFFFFFFFFFFFFFFFEull); // -2 signed, just below max unsigned
            armTrapCheck(*ctx);
            PS2X_EXEC_EMITTED(t, tgei, if (GPR_S64(ctx, 1) >= (int64_t)(int32_t)4294967295) { runtime->handleTrap(rdram, ctx); });
            t.IsTrue(noException(*ctx), "tgei: -2 >= -1 is false, no trap");
            armTrapCheck(*ctx);
            PS2X_EXEC_EMITTED(t, tlti, if (GPR_S64(ctx, 1) < (int64_t)(int32_t)4294967295) { runtime->handleTrap(rdram, ctx); });
            t.IsTrue(exceptionRaised(*ctx, EXCEPTION_TRAP), "tlti: -2 < -1 traps");
            armTrapCheck(*ctx);
            PS2X_EXEC_EMITTED(t, tgeiu, if (GPR_U64(ctx, 1) >= (uint64_t)(int64_t)(int32_t)4294967295) { runtime->handleTrap(rdram, ctx); });
            t.IsTrue(noException(*ctx), "tgeiu: the immediate -1 is 0xFFFFFFFFFFFFFFFF unsigned, so 0x...FFFE is below it");
            armTrapCheck(*ctx);
            PS2X_EXEC_EMITTED(t, tltiu, if (GPR_U64(ctx, 1) < (uint64_t)(int64_t)(int32_t)4294967295) { runtime->handleTrap(rdram, ctx); });
            t.IsTrue(exceptionRaised(*ctx, EXCEPTION_TRAP), "tltiu: 0x...FFFE < 0xFFFFFFFFFFFFFFFF traps");

            setGpr(*ctx, 1, 5u);
            armTrapCheck(*ctx);
            PS2X_EXEC_EMITTED(t, teqi, if (GPR_S64(ctx, 1) == (int64_t)(int32_t)5) { runtime->handleTrap(rdram, ctx); });
            t.IsTrue(exceptionRaised(*ctx, EXCEPTION_TRAP), "teqi traps on an equal value");
            armTrapCheck(*ctx);
            PS2X_EXEC_EMITTED(t, tnei, if (GPR_S64(ctx, 1) != (int64_t)(int32_t)5) { runtime->handleTrap(rdram, ctx); });
            t.IsTrue(noException(*ctx), "tnei does not trap on an equal value");

            setGpr(*ctx, 1, 0x0000000100000005ull); // same low word, so the compare must use all 64 bits
            armTrapCheck(*ctx);
            PS2X_EXEC_EMITTED(t, teqi, if (GPR_S64(ctx, 1) == (int64_t)(int32_t)5) { runtime->handleTrap(rdram, ctx); });
            t.IsTrue(noException(*ctx), "teqi compares all 64 bits");
            armTrapCheck(*ctx);
            PS2X_EXEC_EMITTED(t, tnei, if (GPR_S64(ctx, 1) != (int64_t)(int32_t)5) { runtime->handleTrap(rdram, ctx); });
            t.IsTrue(exceptionRaised(*ctx, EXCEPTION_TRAP), "tnei compares all 64 bits");
        });

        tc.Run("MTSAB and MTSAH set the shift amount in bytes and halfwords", [](TestCase &t) {
            PS2X_GUEST_SCOPE();
            const Instruction mtsab = decode(encodeRegimm(REGIMM_MTSAB, 1, 3));          // mtsab $1, 3
            const Instruction mtsabNeg = decode(encodeRegimm(REGIMM_MTSAB, 1, imm16(-1))); // mtsab $1, -1
            const Instruction mtsah = decode(encodeRegimm(REGIMM_MTSAH, 1, 1));          // mtsah $1, 1

            setGpr(*ctx, 1, 1u);
            PS2X_EXEC_EMITTED(t, mtsab, ctx->sa = ((GPR_U32(ctx, 1) ^ (uint32_t)3) & 0xF) << 3;);
            expectU64(t, ctx->sa, 16u, "mtsab: (1 ^ 3) bytes = 2 bytes = 16 bits");

            setGpr(*ctx, 1, 0x10u); // bits above the byte index are ignored
            PS2X_EXEC_EMITTED(t, mtsabNeg, ctx->sa = ((GPR_U32(ctx, 1) ^ (uint32_t)4294967295) & 0xF) << 3;);
            expectU64(t, ctx->sa, 15u * 8u, "mtsab: (0x10 ^ -1) & 0xF = 15 bytes");

            setGpr(*ctx, 1, 2u);
            PS2X_EXEC_EMITTED(t, mtsah, ctx->sa = ((GPR_U32(ctx, 1) ^ (uint32_t)1) & 0x7) << 4;);
            expectU64(t, ctx->sa, 48u, "mtsah: (2 ^ 1) halfwords = 3 halfwords = 48 bits");
        });

        tc.Run("every RegimmFunctions entry translates and only reserved rt codes reject", [](TestCase &t) {
            const uint32_t implemented[] = {
                REGIMM_BLTZ, REGIMM_BGEZ, REGIMM_BLTZL, REGIMM_BGEZL,
                REGIMM_TGEI, REGIMM_TGEIU, REGIMM_TLTI, REGIMM_TLTIU, REGIMM_TEQI, REGIMM_TNEI,
                REGIMM_BLTZAL, REGIMM_BGEZAL, REGIMM_BLTZALL, REGIMM_BGEZALL, REGIMM_MTSAB, REGIMM_MTSAH};
            t.Equals(std::size(implemented), static_cast<size_t>(16), "the manifest must list every RegimmFunctions enumerator");

            for (uint32_t function = 0; function < 32u; ++function)
            {
                bool isImplemented = false;
                for (uint32_t value : implemented)
                {
                    isImplemented = isImplemented || value == function;
                }

                const Instruction inst = decode(encodeRegimm(function, 1, 4));
                const bool rejects = emitsRuntimeThrow(translate(inst));
                t.Equals(rejects, !isImplemented, describe(inst) + " (rt " + hex(function) + "): " +
                                                      (isImplemented ? "an implemented REGIMM function must not emit a runtime error"
                                                                     : "a reserved REGIMM function must emit a runtime error"));
            }
        }); });
}
