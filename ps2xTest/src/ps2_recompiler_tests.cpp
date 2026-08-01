#include "MiniTest.h"
#include "ps2recomp/ps2_recompiler.h"
#include "ps2recomp/config_manager.h"
#include "ps2recomp/elf_parser.h"
#include "ps2recomp/instructions.h"
#include "ps2recomp/types.h"
#include "ps2_runtime_calls.h"
#include <elfio/elfio.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <vector>

using namespace ps2recomp;

static Instruction makeNopLike(uint32_t address)
{
    Instruction inst{};
    inst.address = address;
    inst.opcode = OPCODE_ADDIU;
    inst.rt = 0;
    inst.raw = 0;
    return inst;
}

static Instruction makeAbsJump(uint32_t address, uint32_t target, uint32_t opcode)
{
    Instruction inst{};
    inst.address = address;
    inst.opcode = opcode;
    inst.target = (target >> 2) & 0x03FFFFFFu;
    inst.hasDelaySlot = true;
    inst.raw = (opcode << 26) | inst.target;
    return inst;
}

// Mirrors R5900Decoder::decodeInstruction, which unconditionally populates
// rs/rt/rd/immediate from the raw instruction bits regardless of instruction
// format. For a J-type instruction (j/jal) those bit positions are actually
// part of the 26-bit jump target, not real register fields, so a naive "does
// this instruction write rt" check can alias against the jal's own encoded
// target. Used to reproduce that scenario in tests.
static Instruction makeAbsJumpDecoded(uint32_t address, uint32_t target, uint32_t opcode)
{
    Instruction inst{};
    inst.address = address;
    inst.opcode = opcode;
    inst.target = (target >> 2) & 0x03FFFFFFu;
    inst.hasDelaySlot = true;
    inst.raw = (opcode << 26) | inst.target;
    inst.rs = RS(inst.raw);
    inst.rt = RT(inst.raw);
    inst.rd = RD(inst.raw);
    inst.immediate = IMMEDIATE(inst.raw);
    inst.simmediate = static_cast<uint32_t>(SIMMEDIATE(inst.raw));
    return inst;
}

static Instruction makeJrRa(uint32_t address)
{
    Instruction inst{};
    inst.address = address;
    inst.opcode = OPCODE_SPECIAL;
    inst.function = SPECIAL_JR;
    inst.rs = 31;
    inst.hasDelaySlot = true;
    inst.raw = 0x03E00008u;
    return inst;
}

static Instruction makeLui(uint32_t address, uint32_t rt, uint32_t imm)
{
    Instruction inst{};
    inst.address = address;
    inst.opcode = OPCODE_LUI;
    inst.rt = rt;
    inst.immediate = imm & 0xFFFFu;
    inst.raw = (OPCODE_LUI << 26) | (rt << 16) | (imm & 0xFFFFu);
    return inst;
}

static Instruction makeAddiu(uint32_t address, uint32_t rt, uint32_t rs, uint32_t imm)
{
    Instruction inst{};
    inst.address = address;
    inst.opcode = OPCODE_ADDIU;
    inst.rt = rt;
    inst.rs = rs;
    inst.immediate = imm & 0xFFFFu;
    inst.simmediate = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(imm & 0xFFFFu)));
    inst.raw = (OPCODE_ADDIU << 26) | (rs << 21) | (rt << 16) | (imm & 0xFFFFu);
    return inst;
}

static Instruction makeOri(uint32_t address, uint32_t rt, uint32_t rs, uint32_t imm)
{
    Instruction inst{};
    inst.address = address;
    inst.opcode = OPCODE_ORI;
    inst.rt = rt;
    inst.rs = rs;
    inst.immediate = imm & 0xFFFFu;
    inst.raw = (OPCODE_ORI << 26) | (rs << 21) | (rt << 16) | (imm & 0xFFFFu);
    return inst;
}

static Instruction makeSyscall(uint32_t address)
{
    Instruction inst{};
    inst.address = address;
    inst.opcode = OPCODE_SPECIAL;
    inst.function = SPECIAL_SYSCALL;
    inst.raw = SPECIAL_SYSCALL;
    return inst;
}

static Instruction makeLw(uint32_t address, uint32_t rt, uint32_t rs)
{
    Instruction inst{};
    inst.address = address;
    inst.opcode = OPCODE_LW;
    inst.rt = rt;
    inst.rs = rs;
    inst.isLoad = true;
    inst.raw = (OPCODE_LW << 26) | (rs << 21) | (rt << 16);
    return inst;
}

static Instruction makeSw(uint32_t address, uint32_t rt, uint32_t rs)
{
    Instruction inst{};
    inst.address = address;
    inst.opcode = OPCODE_SW;
    inst.rt = rt;
    inst.rs = rs;
    inst.isStore = true;
    inst.raw = (OPCODE_SW << 26) | (rs << 21) | (rt << 16);
    return inst;
}

static Instruction makeBeq(uint32_t address, uint32_t rs, uint32_t rt, uint32_t target)
{
    Instruction inst{};
    inst.address = address;
    inst.opcode = OPCODE_BEQ;
    inst.rs = rs;
    inst.rt = rt;
    // Test-fixture convention (shared with makeAbsJump): store the branch target the
    // same way a J-type target is stored, since this scan's helpers never resolve a
    // conditional branch's real PC-relative target.
    inst.target = (target >> 2) & 0x03FFFFFFu;
    inst.isBranch = true;
    inst.hasDelaySlot = true;
    inst.raw = (OPCODE_BEQ << 26) | (rs << 21) | (rt << 16);
    return inst;
}

// fmt is the COP "sub-opcode" field (bits 25-21, decoded into rs); rt is the GPR
// operand. Used to build e.g. an `mtc1 $a0,$fN` (opcode=OPCODE_COP1, fmt=COP1_MT,
// rt=4), which only READS rt.
static Instruction makeCopMove(uint32_t address, uint32_t opcode, uint32_t fmt, uint32_t rt)
{
    Instruction inst{};
    inst.address = address;
    inst.opcode = opcode;
    inst.rs = fmt;
    inst.rt = rt;
    inst.raw = (opcode << 26) | (fmt << 21) | (rt << 16);
    return inst;
}

static Function makeFunction(const std::string &name, uint32_t start, uint32_t end)
{
    Function fn{};
    fn.name = name;
    fn.start = start;
    fn.end = end;
    fn.isRecompiled = true;
    fn.isStub = false;
    fn.isSkipped = false;
    return fn;
}

static bool writeMinimalMipsElfWithCodeAndDataFunctionSymbols(const std::filesystem::path &elfPath)
{
    ELFIO::elfio writer;
    writer.create(ELFIO::ELFCLASS32, ELFIO::ELFDATA2LSB);
    writer.set_os_abi(ELFIO::ELFOSABI_NONE);
    writer.set_type(ELFIO::ET_EXEC);
    writer.set_machine(ELFIO::EM_MIPS);
    writer.set_entry(0x00100000u);

    ELFIO::section *text = writer.sections.add(".text");
    text->set_type(ELFIO::SHT_PROGBITS);
    text->set_flags(ELFIO::SHF_ALLOC | ELFIO::SHF_EXECINSTR);
    text->set_addr_align(4);
    text->set_address(0x00100000u);
    const char textBytes[] = {0x08, 0x00, static_cast<char>(0xE0), 0x03, 0x00, 0x00, 0x00, 0x00};
    text->set_data(textBytes, sizeof(textBytes));

    ELFIO::section *data = writer.sections.add(".data");
    data->set_type(ELFIO::SHT_PROGBITS);
    data->set_flags(ELFIO::SHF_ALLOC | ELFIO::SHF_WRITE);
    data->set_addr_align(4);
    data->set_address(0x00200000u);
    const char dataBytes[] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, static_cast<char>(0x88)};
    data->set_data(dataBytes, sizeof(dataBytes));

    ELFIO::section *strtab = writer.sections.add(".strtab");
    strtab->set_type(ELFIO::SHT_STRTAB);
    strtab->set_addr_align(1);

    ELFIO::section *symtab = writer.sections.add(".symtab");
    symtab->set_type(ELFIO::SHT_SYMTAB);
    symtab->set_info(1);
    symtab->set_link(strtab->get_index());
    symtab->set_addr_align(4);
    symtab->set_entry_size(writer.get_default_entry_size(ELFIO::SHT_SYMTAB));

    ELFIO::symbol_section_accessor symbols(writer, symtab);
    ELFIO::string_section_accessor strings(strtab);
    symbols.add_symbol(strings, "", 0, 0, ELFIO::STB_LOCAL, ELFIO::STT_NOTYPE, 0, ELFIO::SHN_UNDEF);
    symbols.add_symbol(strings, "code_func", text->get_address(), text->get_size(),
                       ELFIO::STB_GLOBAL, ELFIO::STT_FUNC, 0, text->get_index());
    symbols.add_symbol(strings, "data_func", data->get_address(), data->get_size(),
                       ELFIO::STB_GLOBAL, ELFIO::STT_FUNC, 0, data->get_index());

    ELFIO::segment *textSegment = writer.segments.add();
    textSegment->set_type(ELFIO::PT_LOAD);
    textSegment->set_flags(ELFIO::PF_R | ELFIO::PF_X);
    textSegment->set_align(0x1000);
    textSegment->add_section_index(text->get_index(), text->get_addr_align());

    ELFIO::segment *dataSegment = writer.segments.add();
    dataSegment->set_type(ELFIO::PT_LOAD);
    dataSegment->set_flags(ELFIO::PF_R | ELFIO::PF_W);
    dataSegment->set_align(0x1000);
    dataSegment->add_section_index(data->get_index(), data->get_addr_align());

    return writer.save(elfPath.string());
}

static bool writeMinimalMipsElfWithJalFallbackTarget(const std::filesystem::path &elfPath)
{
    ELFIO::elfio writer;
    writer.create(ELFIO::ELFCLASS32, ELFIO::ELFDATA2LSB);
    writer.set_os_abi(ELFIO::ELFOSABI_NONE);
    writer.set_type(ELFIO::ET_EXEC);
    writer.set_machine(ELFIO::EM_MIPS);
    writer.set_entry(0x00100000u);

    ELFIO::section *text = writer.sections.add(".text");
    text->set_type(ELFIO::SHT_PROGBITS);
    text->set_flags(ELFIO::SHF_ALLOC | ELFIO::SHF_EXECINSTR);
    text->set_addr_align(4);
    text->set_address(0x00100000u);

    const std::array<uint32_t, 6> textWords = {
        0x0C040004u, // jal 0x00100010
        0x00000000u, // nop
        0x03E00008u, // jr $ra
        0x00000000u, // nop
        0x03E00008u, // jr $ra
        0x00000000u  // nop
    };
    text->set_data(reinterpret_cast<const char *>(textWords.data()),
                   static_cast<ELFIO::Elf_Word>(textWords.size() * sizeof(uint32_t)));

    ELFIO::segment *textSegment = writer.segments.add();
    textSegment->set_type(ELFIO::PT_LOAD);
    textSegment->set_flags(ELFIO::PF_R | ELFIO::PF_X);
    textSegment->set_align(0x1000);
    textSegment->add_section_index(text->get_index(), text->get_addr_align());

    return writer.save(elfPath.string());
}

// Builds a fixture ELF containing a single "container_fn" function
// [containerStart, containerEnd) that is NOP-filled and ends in jr $ra + delay-slot nop.
// No other code exists anywhere in the ELF, so no jal/j instruction anywhere can ever
// target a mid-body address of this function - the only way a mid-body address can be
// discovered as an entry point is via an ingested external-call-target manifest. The
// ELF entry point is set to containerStart, so bootstrap registration only covers the
// function head.
static bool writeContainerOnlyElf(const std::filesystem::path &elfPath,
                                   uint32_t containerStart,
                                   uint32_t containerEnd)
{
    ELFIO::elfio writer;
    writer.create(ELFIO::ELFCLASS32, ELFIO::ELFDATA2LSB);
    writer.set_os_abi(ELFIO::ELFOSABI_NONE);
    writer.set_type(ELFIO::ET_EXEC);
    writer.set_machine(ELFIO::EM_MIPS);
    writer.set_entry(containerStart);

    ELFIO::section *text = writer.sections.add(".text");
    text->set_type(ELFIO::SHT_PROGBITS);
    text->set_flags(ELFIO::SHF_ALLOC | ELFIO::SHF_EXECINSTR);
    text->set_addr_align(4);
    text->set_address(containerStart);

    const uint32_t size = containerEnd - containerStart;
    const size_t wordCount = size / sizeof(uint32_t);
    if (wordCount < 2)
    {
        return false;
    }
    std::vector<uint32_t> textWords(wordCount, 0x00000000u); // NOP-fill the whole body
    textWords[wordCount - 2] = 0x03E00008u;                  // jr $ra
    textWords[wordCount - 1] = 0x00000000u;                  // nop (delay slot)
    text->set_data(reinterpret_cast<const char *>(textWords.data()),
                   static_cast<ELFIO::Elf_Word>(textWords.size() * sizeof(uint32_t)));

    ELFIO::section *strtab = writer.sections.add(".strtab");
    strtab->set_type(ELFIO::SHT_STRTAB);
    strtab->set_addr_align(1);

    ELFIO::section *symtab = writer.sections.add(".symtab");
    symtab->set_type(ELFIO::SHT_SYMTAB);
    symtab->set_info(1);
    symtab->set_link(strtab->get_index());
    symtab->set_addr_align(4);
    symtab->set_entry_size(writer.get_default_entry_size(ELFIO::SHT_SYMTAB));

    ELFIO::symbol_section_accessor symbols(writer, symtab);
    ELFIO::string_section_accessor strings(strtab);
    symbols.add_symbol(strings, "", 0, 0, ELFIO::STB_LOCAL, ELFIO::STT_NOTYPE, 0, ELFIO::SHN_UNDEF);
    symbols.add_symbol(strings, "container_fn", containerStart, size,
                       ELFIO::STB_GLOBAL, ELFIO::STT_FUNC, 0, text->get_index());

    ELFIO::segment *textSegment = writer.segments.add();
    textSegment->set_type(ELFIO::PT_LOAD);
    textSegment->set_flags(ELFIO::PF_R | ELFIO::PF_X);
    textSegment->set_align(0x1000);
    textSegment->add_section_index(text->get_index(), text->get_addr_align());

    return writer.save(elfPath.string());
}

// Builds a fixture ELF exercising the data-embedded thread-entry discovery path through
// real ELF/.data plumbing:
//   caller @ 0x00100000: nop; lui $a0,hi(P); jal CreateThread; addiu $a0,$a0,lo(P) (delay
//                        slot); jr $ra; nop
//   CreateThread @ 0x00100018 (syscall 0x20 wrapper): addiu $v1,$zero,0x20; syscall;
//                        jr $ra; nop
//   worker_container @ 0x00100028: nop; nop; nop (this is E, the thread entry pointer,
//                        strictly inside and not the head); nop; jr $ra; nop
//   .data @ 0x00200000 (P, the ThreadParam struct): word0 = 0 (unused by this test),
//                        word1 (P+4) = E = 0x00100030 (PS2 ABI: entry fn ptr is the
//                        second word of ThreadParam).
static bool writeThreadEntryDataElf(const std::filesystem::path &elfPath)
{
    ELFIO::elfio writer;
    writer.create(ELFIO::ELFCLASS32, ELFIO::ELFDATA2LSB);
    writer.set_os_abi(ELFIO::ELFOSABI_NONE);
    writer.set_type(ELFIO::ET_EXEC);
    writer.set_machine(ELFIO::EM_MIPS);
    writer.set_entry(0x00100000u);

    ELFIO::section *text = writer.sections.add(".text");
    text->set_type(ELFIO::SHT_PROGBITS);
    text->set_flags(ELFIO::SHF_ALLOC | ELFIO::SHF_EXECINSTR);
    text->set_addr_align(4);
    text->set_address(0x00100000u);

    const std::array<uint32_t, 16> textWords = {
        // caller @ 0x00100000
        0x00000000u, // 0x00100000: nop
        0x3C040020u, // 0x00100004: lui $a0, 0x0020        -> $a0 = 0x00200000 (P)
        0x0C040006u, // 0x00100008: jal 0x00100018 (CreateThread)
        0x24840000u, // 0x0010000C: addiu $a0,$a0,0 (delay slot; lo(P) == 0)
        0x03E00008u, // 0x00100010: jr $ra
        0x00000000u, // 0x00100014: nop
        // CreateThread @ 0x00100018
        0x24030020u, // 0x00100018: addiu $v1,$zero,0x20
        0x0000000Cu, // 0x0010001C: syscall
        0x03E00008u, // 0x00100020: jr $ra
        0x00000000u, // 0x00100024: nop
        // worker_container @ 0x00100028
        0x00000000u, // 0x00100028: nop
        0x00000000u, // 0x0010002C: nop
        0x00000000u, // 0x00100030: nop  <- E, the data-embedded thread entry point
        0x00000000u, // 0x00100034: nop
        0x03E00008u, // 0x00100038: jr $ra
        0x00000000u  // 0x0010003C: nop
    };
    text->set_data(reinterpret_cast<const char *>(textWords.data()),
                   static_cast<ELFIO::Elf_Word>(textWords.size() * sizeof(uint32_t)));

    ELFIO::section *data = writer.sections.add(".data");
    data->set_type(ELFIO::SHT_PROGBITS);
    data->set_flags(ELFIO::SHF_ALLOC | ELFIO::SHF_WRITE);
    data->set_addr_align(4);
    data->set_address(0x00200000u);
    const std::array<uint32_t, 2> dataWords = {
        0x00000000u, // P + 0: unused by this test
        0x00100030u  // P + 4: thread entry function pointer == E
    };
    data->set_data(reinterpret_cast<const char *>(dataWords.data()),
                   static_cast<ELFIO::Elf_Word>(dataWords.size() * sizeof(uint32_t)));

    ELFIO::section *strtab = writer.sections.add(".strtab");
    strtab->set_type(ELFIO::SHT_STRTAB);
    strtab->set_addr_align(1);

    ELFIO::section *symtab = writer.sections.add(".symtab");
    symtab->set_type(ELFIO::SHT_SYMTAB);
    symtab->set_info(1);
    symtab->set_link(strtab->get_index());
    symtab->set_addr_align(4);
    symtab->set_entry_size(writer.get_default_entry_size(ELFIO::SHT_SYMTAB));

    ELFIO::symbol_section_accessor symbols(writer, symtab);
    ELFIO::string_section_accessor strings(strtab);
    symbols.add_symbol(strings, "", 0, 0, ELFIO::STB_LOCAL, ELFIO::STT_NOTYPE, 0, ELFIO::SHN_UNDEF);
    symbols.add_symbol(strings, "caller", 0x00100000u, 0x18u,
                       ELFIO::STB_GLOBAL, ELFIO::STT_FUNC, 0, text->get_index());
    symbols.add_symbol(strings, "CreateThread", 0x00100018u, 0x10u,
                       ELFIO::STB_GLOBAL, ELFIO::STT_FUNC, 0, text->get_index());
    symbols.add_symbol(strings, "worker_container", 0x00100028u, 0x18u,
                       ELFIO::STB_GLOBAL, ELFIO::STT_FUNC, 0, text->get_index());

    ELFIO::segment *textSegment = writer.segments.add();
    textSegment->set_type(ELFIO::PT_LOAD);
    textSegment->set_flags(ELFIO::PF_R | ELFIO::PF_X);
    textSegment->set_align(0x1000);
    textSegment->add_section_index(text->get_index(), text->get_addr_align());

    ELFIO::segment *dataSegment = writer.segments.add();
    dataSegment->set_type(ELFIO::PT_LOAD);
    dataSegment->set_flags(ELFIO::PF_R | ELFIO::PF_W);
    dataSegment->set_align(0x1000);
    dataSegment->add_section_index(data->get_index(), data->get_addr_align());

    return writer.save(elfPath.string());
}

// Builds a fixture ELF with a single "caller" function [callerStart, callerStart+0x10)
// that JALs to jalTarget - an address that lies entirely outside every section of
// this ELF (that is the point: it exists only in a sibling unit's ELF). Pairs with
// writeContainerOnlyElf (the callee side) to build a genuine two-ELF cross-unit
// regression: this unit's own CollectExternalCallTargets sees a jal landing outside
// every one of its own sections, which the pre-fix inclusion gate dropped.
static bool writeJalToForeignTargetElf(const std::filesystem::path &elfPath,
                                        uint32_t callerStart,
                                        uint32_t jalTarget)
{
    ELFIO::elfio writer;
    writer.create(ELFIO::ELFCLASS32, ELFIO::ELFDATA2LSB);
    writer.set_os_abi(ELFIO::ELFOSABI_NONE);
    writer.set_type(ELFIO::ET_EXEC);
    writer.set_machine(ELFIO::EM_MIPS);
    writer.set_entry(callerStart);

    ELFIO::section *text = writer.sections.add(".text");
    text->set_type(ELFIO::SHT_PROGBITS);
    text->set_flags(ELFIO::SHF_ALLOC | ELFIO::SHF_EXECINSTR);
    text->set_addr_align(4);
    text->set_address(callerStart);

    const uint32_t jalWord = (static_cast<uint32_t>(OPCODE_JAL) << 26) | ((jalTarget >> 2) & 0x03FFFFFFu);
    const std::array<uint32_t, 4> textWords = {
        jalWord,     // jal jalTarget
        0x00000000u, // nop (delay slot)
        0x03E00008u, // jr $ra
        0x00000000u  // nop (delay slot)
    };
    text->set_data(reinterpret_cast<const char *>(textWords.data()),
                   static_cast<ELFIO::Elf_Word>(textWords.size() * sizeof(uint32_t)));

    ELFIO::section *strtab = writer.sections.add(".strtab");
    strtab->set_type(ELFIO::SHT_STRTAB);
    strtab->set_addr_align(1);

    ELFIO::section *symtab = writer.sections.add(".symtab");
    symtab->set_type(ELFIO::SHT_SYMTAB);
    symtab->set_info(1);
    symtab->set_link(strtab->get_index());
    symtab->set_addr_align(4);
    symtab->set_entry_size(writer.get_default_entry_size(ELFIO::SHT_SYMTAB));

    ELFIO::symbol_section_accessor symbols(writer, symtab);
    ELFIO::string_section_accessor strings(strtab);
    symbols.add_symbol(strings, "", 0, 0, ELFIO::STB_LOCAL, ELFIO::STT_NOTYPE, 0, ELFIO::SHN_UNDEF);
    symbols.add_symbol(strings, "caller_fn", callerStart,
                       static_cast<ELFIO::Elf_Xword>(textWords.size() * sizeof(uint32_t)),
                       ELFIO::STB_GLOBAL, ELFIO::STT_FUNC, 0, text->get_index());

    ELFIO::segment *textSegment = writer.segments.add();
    textSegment->set_type(ELFIO::PT_LOAD);
    textSegment->set_flags(ELFIO::PF_R | ELFIO::PF_X);
    textSegment->set_align(0x1000);
    textSegment->add_section_index(text->get_index(), text->get_addr_align());

    return writer.save(elfPath.string());
}

// Builds a fixture ELF with a single container_fn [containerStart, containerEnd),
// NOP-filled and jr $ra-terminated like writeContainerOnlyElf, except word index 0
// is a jal to foreignJalTarget (assumed to land outside every section of this ELF,
// i.e. inside a sibling unit) instead of a nop. This unit therefore both exposes a
// mid-body target (containerStart+8, a real decoded instruction boundary that is not
// the function head) for a sibling to call into, AND itself calls into a sibling's
// mid-body target - used to build the mutually-calling two-phase multi-unit test.
static bool writeMutualCallElf(const std::filesystem::path &elfPath,
                                uint32_t containerStart,
                                uint32_t containerEnd,
                                uint32_t foreignJalTarget)
{
    ELFIO::elfio writer;
    writer.create(ELFIO::ELFCLASS32, ELFIO::ELFDATA2LSB);
    writer.set_os_abi(ELFIO::ELFOSABI_NONE);
    writer.set_type(ELFIO::ET_EXEC);
    writer.set_machine(ELFIO::EM_MIPS);
    writer.set_entry(containerStart);

    ELFIO::section *text = writer.sections.add(".text");
    text->set_type(ELFIO::SHT_PROGBITS);
    text->set_flags(ELFIO::SHF_ALLOC | ELFIO::SHF_EXECINSTR);
    text->set_addr_align(4);
    text->set_address(containerStart);

    const uint32_t size = containerEnd - containerStart;
    const size_t wordCount = size / sizeof(uint32_t);
    if (wordCount < 4)
    {
        return false;
    }
    const uint32_t jalWord = (static_cast<uint32_t>(OPCODE_JAL) << 26) | ((foreignJalTarget >> 2) & 0x03FFFFFFu);
    std::vector<uint32_t> textWords(wordCount, 0x00000000u); // NOP-fill the whole body
    textWords[0] = jalWord;                                  // jal foreignJalTarget
    // textWords[1] stays the delay-slot nop; textWords[2] (containerStart+8) is the
    // mid-body target this unit exposes to a sibling.
    textWords[wordCount - 2] = 0x03E00008u; // jr $ra
    textWords[wordCount - 1] = 0x00000000u; // nop (delay slot)
    text->set_data(reinterpret_cast<const char *>(textWords.data()),
                   static_cast<ELFIO::Elf_Word>(textWords.size() * sizeof(uint32_t)));

    ELFIO::section *strtab = writer.sections.add(".strtab");
    strtab->set_type(ELFIO::SHT_STRTAB);
    strtab->set_addr_align(1);

    ELFIO::section *symtab = writer.sections.add(".symtab");
    symtab->set_type(ELFIO::SHT_SYMTAB);
    symtab->set_info(1);
    symtab->set_link(strtab->get_index());
    symtab->set_addr_align(4);
    symtab->set_entry_size(writer.get_default_entry_size(ELFIO::SHT_SYMTAB));

    ELFIO::symbol_section_accessor symbols(writer, symtab);
    ELFIO::string_section_accessor strings(strtab);
    symbols.add_symbol(strings, "", 0, 0, ELFIO::STB_LOCAL, ELFIO::STT_NOTYPE, 0, ELFIO::SHN_UNDEF);
    symbols.add_symbol(strings, "container_fn", containerStart, size,
                       ELFIO::STB_GLOBAL, ELFIO::STT_FUNC, 0, text->get_index());

    ELFIO::segment *textSegment = writer.segments.add();
    textSegment->set_type(ELFIO::PT_LOAD);
    textSegment->set_flags(ELFIO::PF_R | ELFIO::PF_X);
    textSegment->set_align(0x1000);
    textSegment->add_section_index(text->get_index(), text->get_addr_align());

    return writer.save(elfPath.string());
}

// Returns every line of `content` containing `needle` - used to inspect the emitted
// register_functions.cpp function-table initializer, whose lines look like:
//   g_ps2RecompiledFunctionTable[<slot>] = <ownerName>; // 0x<address>
static std::vector<std::string> findLinesContaining(const std::string &content, const std::string &needle)
{
    std::vector<std::string> matches;
    std::istringstream iss(content);
    std::string line;
    while (std::getline(iss, line))
    {
        if (line.find(needle) != std::string::npos)
        {
            matches.push_back(line);
        }
    }
    return matches;
}

// Extracts <ownerName> from a "g_ps2RecompiledFunctionTable[<slot>] = <ownerName>; // 0x<addr>" line.
static std::string extractOwnerNameFromRegistrationLine(const std::string &line)
{
    const size_t eqPos = line.find("= ");
    if (eqPos == std::string::npos)
    {
        return {};
    }
    const size_t start = eqPos + 2;
    const size_t semiPos = line.find(';', start);
    if (semiPos == std::string::npos)
    {
        return {};
    }
    return line.substr(start, semiPos - start);
}

void register_ps2_recompiler_tests()
{
    MiniTest::Case("PS2Recompiler", [](TestCase &tc)
                   {
        tc.Run("game helpers are not classified as runtime stubs", [](TestCase &t) {
            t.IsFalse(ps2_runtime_calls::isStubName("Pad_init"),
                      "Pad_init should be recompiled as game code");
            t.IsFalse(ps2_runtime_calls::isStubName("Pad_set"),
                      "Pad_set should be recompiled as game code");
            t.IsFalse(ps2_runtime_calls::isStubName("pdInitPeripheral"),
                      "pdInitPeripheral should be recompiled as game code");
            t.IsFalse(ps2_runtime_calls::isStubName("pdGetPeripheral"),
                      "pdGetPeripheral should be recompiled as game code");
            t.IsFalse(ps2_runtime_calls::isStubName("InitThread"),
                      "InitThread should be recompiled as game code");
            t.IsFalse(ps2_runtime_calls::isStubName("syFree"),
                      "syFree should be recompiled as game code");
            t.IsFalse(ps2_runtime_calls::isStubName("syMallocInit"),
                      "syMallocInit should be recompiled as game code");
            t.IsFalse(ps2_runtime_calls::isStubName("syHwInit"),
                      "syHwInit should be recompiled as game code");
            t.IsFalse(ps2_runtime_calls::isStubName("syHwInit2"),
                      "syHwInit2 should be recompiled as game code");
            t.IsFalse(ps2_runtime_calls::isStubName("syRtcInit"),
                      "syRtcInit should be recompiled as game code");
            t.IsFalse(ps2_runtime_calls::isStubName("sdDrvInit"),
                      "sdDrvInit should be recompiled as game code");
            t.IsFalse(ps2_runtime_calls::isStubName("sdSndStopAll"),
                      "sdSndStopAll should be recompiled as game code");
            t.IsFalse(ps2_runtime_calls::isStubName("sdSysFinish"),
                      "sdSysFinish should be recompiled as game code");
            t.IsFalse(ps2_runtime_calls::isStubName("iopGetArea"),
                      "iopGetArea should be recompiled as game code");
            t.IsTrue(ps2_runtime_calls::isStubName("builtin_set_imask"),
                     "builtin_set_imask should remain a runtime helper");
            t.IsTrue(ps2_runtime_calls::isStubName("getpid"),
                     "getpid should remain a runtime helper");
            t.IsTrue(ps2_runtime_calls::isStubName("scePadRead"),
                     "scePadRead should remain a runtime pad stub");
        });

        tc.Run("additional entries split at nearest discovered boundary", [](TestCase &t) {
            std::vector<Section> sections = {
                {".text", 0x1000u, 0x3000u, 0u, true, false, false, true, nullptr}
            };

            std::vector<Function> functions = {
                makeFunction("container", 0x1000u, 0x1018u),
                makeFunction("caller", 0x2000u, 0x2010u)
            };

            std::unordered_map<uint32_t, std::vector<Instruction>> decodedFunctions;
            decodedFunctions[0x1000u] = {
                makeNopLike(0x1000u),
                makeNopLike(0x1004u),
                makeNopLike(0x1008u),
                makeNopLike(0x100Cu),
                makeNopLike(0x1010u),
                makeNopLike(0x1014u)
            };
            decodedFunctions[0x2000u] = {
                makeAbsJump(0x2000u, 0x1008u, OPCODE_JAL),
                makeNopLike(0x2004u),
                makeAbsJump(0x2008u, 0x100Cu, OPCODE_J),
                makeNopLike(0x200Cu)
            };

            size_t discovered = PS2Recompiler::DiscoverAdditionalEntryPoints(
                functions, decodedFunctions, sections);

            t.Equals(discovered, static_cast<size_t>(3),
                     "expected two mid-function targets plus the JAL return entry to be discovered");

            auto findByStart = [&](uint32_t start) -> const Function* {
                auto it = std::find_if(functions.begin(), functions.end(),
                                       [&](const Function &fn) { return fn.start == start; });
                if (it == functions.end())
                {
                    return nullptr;
                }
                return &(*it);
            };

            const Function *entry1008 = findByStart(0x1008u);
            const Function *entry100C = findByStart(0x100Cu);
            const Function *entry2008 = findByStart(0x2008u);
            t.IsNotNull(entry1008, "entry at 0x1008 should exist");
            t.IsNotNull(entry100C, "entry at 0x100C should exist");
            t.IsNotNull(entry2008, "JAL return address entry at 0x2008 should exist");
            if (entry1008 && entry100C)
            {
                t.Equals(entry1008->end, 0x100Cu,
                         "entry 0x1008 should end at nearest discovered start 0x100C");
                t.Equals(entry100C->end, 0x1018u,
                         "entry 0x100C should end at containing function end");
            }
            if (entry2008)
            {
                t.Equals(entry2008->end, 0x2010u,
                         "return entry 0x2008 should slice through the caller tail");
            }

            auto decoded1008It = decodedFunctions.find(0x1008u);
            auto decoded100CIt = decodedFunctions.find(0x100Cu);
            auto decoded2008It = decodedFunctions.find(0x2008u);
            t.IsTrue(decoded1008It != decodedFunctions.end(), "decoded slice for 0x1008 should exist");
            t.IsTrue(decoded100CIt != decodedFunctions.end(), "decoded slice for 0x100C should exist");
            t.IsTrue(decoded2008It != decodedFunctions.end(), "decoded slice for 0x2008 should exist");
            if (decoded1008It != decodedFunctions.end())
            {
                t.Equals(decoded1008It->second.size(), static_cast<size_t>(1),
                         "entry 0x1008 slice should stop before 0x100C");
                if (!decoded1008It->second.empty())
                {
                    t.Equals(decoded1008It->second.front().address, 0x1008u,
                             "entry 0x1008 slice should begin at 0x1008");
                }
            }
            if (decoded100CIt != decodedFunctions.end() && !decoded100CIt->second.empty())
            {
                t.Equals(decoded100CIt->second.front().address, 0x100Cu,
                         "entry 0x100C slice should begin at 0x100C");
            }
            if (decoded2008It != decodedFunctions.end())
            {
                t.Equals(decoded2008It->second.size(), static_cast<size_t>(2),
                         "return entry 0x2008 slice should keep the jump and its delay slot");
                if (!decoded2008It->second.empty())
                {
                    t.Equals(decoded2008It->second.front().address, 0x2008u,
                             "return entry 0x2008 slice should begin at the JAL fallthrough");
                }
            }
        });

        tc.Run("entry reslice trims earlier entries after late discovery", [](TestCase &t) {
            std::vector<Function> functions = {
                makeFunction("container", 0x1000u, 0x1018u),
                makeFunction("entry_1008", 0x1008u, 0x1018u),
                makeFunction("entry_100c", 0x100Cu, 0x1018u)
            };

            std::unordered_map<uint32_t, std::vector<Instruction>> decodedFunctions;
            decodedFunctions[0x1000u] = {
                makeNopLike(0x1000u),
                makeNopLike(0x1004u),
                makeNopLike(0x1008u),
                makeNopLike(0x100Cu),
                makeNopLike(0x1010u),
                makeNopLike(0x1014u)
            };
            decodedFunctions[0x1008u] = {
                makeNopLike(0x1008u),
                makeNopLike(0x100Cu),
                makeNopLike(0x1010u),
                makeNopLike(0x1014u)
            };
            decodedFunctions[0x100Cu] = {
                makeNopLike(0x100Cu),
                makeNopLike(0x1010u),
                makeNopLike(0x1014u)
            };

            size_t resliced = PS2Recompiler::ResliceEntryFunctions(functions, decodedFunctions);
            t.Equals(resliced, static_cast<size_t>(1),
                     "expected only the earlier entry to be resliced");

            auto findByStart = [&](uint32_t start) -> const Function* {
                auto it = std::find_if(functions.begin(), functions.end(),
                                       [&](const Function &fn) { return fn.start == start; });
                if (it == functions.end())
                {
                    return nullptr;
                }
                return &(*it);
            };

            const Function *entry1008 = findByStart(0x1008u);
            const Function *entry100C = findByStart(0x100Cu);
            t.IsNotNull(entry1008, "entry at 0x1008 should exist");
            t.IsNotNull(entry100C, "entry at 0x100C should exist");
            if (entry1008)
            {
                t.Equals(entry1008->end, 0x100Cu,
                         "entry 0x1008 should be trimmed to next entry start");
            }
            if (entry100C)
            {
                t.Equals(entry100C->end, 0x1018u,
                         "entry 0x100C should still end at containing end");
            }

            auto decoded1008It = decodedFunctions.find(0x1008u);
            auto decoded100CIt = decodedFunctions.find(0x100Cu);
            t.IsTrue(decoded1008It != decodedFunctions.end(), "decoded slice for 0x1008 should exist");
            t.IsTrue(decoded100CIt != decodedFunctions.end(), "decoded slice for 0x100C should exist");
            if (decoded1008It != decodedFunctions.end())
            {
                t.Equals(decoded1008It->second.size(), static_cast<size_t>(1),
                         "entry 0x1008 slice should stop before 0x100C");
                if (!decoded1008It->second.empty())
                {
                    t.Equals(decoded1008It->second.front().address, 0x1008u,
                             "entry 0x1008 slice should begin at 0x1008");
                }
            }
            if (decoded100CIt != decodedFunctions.end())
            {
                t.Equals(decoded100CIt->second.size(), static_cast<size_t>(3),
                         "entry 0x100C slice should keep remaining instructions");
            }
        });

        tc.Run("same-function JAL return addresses get entry wrappers but targets stay labels", [](TestCase &t) {
            std::vector<Section> sections = {
                {".text", 0x1000u, 0x40u, 0u, true, false, false, true, nullptr}
            };

            std::vector<Function> functions = {
                makeFunction("container", 0x1000u, 0x101Cu)
            };

            std::unordered_map<uint32_t, std::vector<Instruction>> decodedFunctions;
            decodedFunctions[0x1000u] = {
                makeAbsJump(0x1000u, 0x100Cu, OPCODE_JAL),
                makeNopLike(0x1004u),
                makeAbsJump(0x1008u, 0x1014u, OPCODE_J),
                makeNopLike(0x100Cu),
                makeNopLike(0x1010u),
                makeNopLike(0x1014u),
                makeJrRa(0x1018u)
            };

            size_t discovered = PS2Recompiler::DiscoverAdditionalEntryPoints(
                functions, decodedFunctions, sections);

            t.Equals(discovered, static_cast<size_t>(1),
                     "same-function JAL should create only the resume entry while plain J stays internal");

            const bool hasResumeEntry = std::any_of(
                functions.begin(), functions.end(),
                [](const Function &fn) { return fn.start == 0x1008u; });
            const bool hasCallEntry = std::any_of(
                functions.begin(), functions.end(),
                [](const Function &fn) { return fn.start == 0x100Cu; });
            const bool hasJumpEntry = std::any_of(
                functions.begin(), functions.end(),
                [](const Function &fn) { return fn.start == 0x1014u && fn.name.rfind("entry_", 0) == 0; });

            t.IsTrue(hasResumeEntry, "same-function JAL return address should be promoted to a resumable entry");
            t.IsFalse(hasCallEntry, "same-function JAL target should remain an internal label");
            t.IsFalse(hasJumpEntry, "same-function J target should remain an internal label only");
        });

        tc.Run("JAL return addresses get resumable entry wrappers", [](TestCase &t) {
            std::vector<Section> sections = {
                {".text", 0x1000u, 0x2000u, 0u, true, false, false, true, nullptr}
            };

            std::vector<Function> functions = {
                makeFunction("caller", 0x1000u, 0x1018u),
                makeFunction("callee", 0x2000u, 0x2008u)
            };

            std::unordered_map<uint32_t, std::vector<Instruction>> decodedFunctions;
            decodedFunctions[0x1000u] = {
                makeAbsJump(0x1000u, 0x2000u, OPCODE_JAL),
                makeNopLike(0x1004u),
                makeNopLike(0x1008u),
                makeNopLike(0x100Cu),
                makeJrRa(0x1010u),
                makeNopLike(0x1014u)
            };
            decodedFunctions[0x2000u] = {
                makeJrRa(0x2000u),
                makeNopLike(0x2004u)
            };

            size_t discovered = PS2Recompiler::DiscoverAdditionalEntryPoints(
                functions, decodedFunctions, sections);
            t.Equals(discovered, static_cast<size_t>(1),
                     "external JAL should create one resumable entry at the caller return address");

            auto entryIt = std::find_if(functions.begin(), functions.end(),
                                        [](const Function &fn) { return fn.start == 0x1008u; });
            t.IsTrue(entryIt != functions.end(), "return address 0x1008 should be promoted to an entry wrapper");
            if (entryIt != functions.end())
            {
                t.Equals(entryIt->end, 0x1018u,
                         "return-address entry should slice through the remainder of the caller");
            }

            auto decodedEntryIt = decodedFunctions.find(0x1008u);
            t.IsTrue(decodedEntryIt != decodedFunctions.end(),
                     "decoded entry slice for the caller return address should exist");
            if (decodedEntryIt != decodedFunctions.end())
            {
                t.Equals(decodedEntryIt->second.size(), static_cast<size_t>(4),
                         "return-address entry slice should keep the caller tail");
                if (!decodedEntryIt->second.empty())
                {
                    t.Equals(decodedEntryIt->second.front().address, 0x1008u,
                             "return-address entry slice should begin at the JAL fallthrough");
                }
            }
        });

        tc.Run("JAL to an already-known function still discovers the return entry", [](TestCase &t) {
            std::vector<Section> sections = {
                {".text", 0x1000u, 0x2000u, 0u, true, false, false, true, nullptr}
            };

            std::vector<Function> functions = {
                makeFunction("caller", 0x1000u, 0x1020u),
                makeFunction("callee", 0x1100u, 0x1108u)
            };

            std::unordered_map<uint32_t, std::vector<Instruction>> decodedFunctions;
            decodedFunctions[0x1000u] = {
                makeNopLike(0x1000u),
                makeNopLike(0x1004u),
                makeAbsJump(0x1008u, 0x1100u, OPCODE_JAL),
                makeNopLike(0x100Cu),
                makeNopLike(0x1010u),
                makeNopLike(0x1014u),
                makeJrRa(0x1018u),
                makeNopLike(0x101Cu)
            };
            decodedFunctions[0x1100u] = {
                makeJrRa(0x1100u),
                makeNopLike(0x1104u)
            };

            size_t discovered = PS2Recompiler::DiscoverAdditionalEntryPoints(
                functions, decodedFunctions, sections);
            t.Equals(discovered, static_cast<size_t>(1),
                     "return entry should still be discovered even when the JAL target is already registered");

            auto entryIt = std::find_if(functions.begin(), functions.end(),
                                        [](const Function &fn) { return fn.start == 0x1010u; });
            t.IsTrue(entryIt != functions.end(),
                     "return address 0x1010 should be emitted as a resumable entry");
            if (entryIt != functions.end())
            {
                t.Equals(entryIt->end, 0x1020u,
                         "return entry should cover the remaining caller tail");
            }
        });

        tc.Run("discovery ignores synthetic entry wrappers", [](TestCase &t) {
            std::vector<Section> sections = {
                {".text", 0x1000u, 0x2000u, 0u, true, false, false, true, nullptr}
            };

            std::vector<Function> functions = {
                makeFunction("entry_1008", 0x1008u, 0x1020u),
                makeFunction("callee", 0x1100u, 0x1108u)
            };

            std::unordered_map<uint32_t, std::vector<Instruction>> decodedFunctions;
            decodedFunctions[0x1008u] = {
                makeAbsJump(0x1008u, 0x1100u, OPCODE_JAL),
                makeNopLike(0x100Cu),
                makeNopLike(0x1010u),
                makeNopLike(0x1014u),
                makeJrRa(0x1018u),
                makeNopLike(0x101Cu)
            };
            decodedFunctions[0x1100u] = {
                makeJrRa(0x1100u),
                makeNopLike(0x1104u)
            };

            size_t discovered = PS2Recompiler::DiscoverAdditionalEntryPoints(
                functions, decodedFunctions, sections);
            t.Equals(discovered, static_cast<size_t>(0),
                     "synthetic entry wrappers should not recursively produce more entries");

            const bool hasRecursiveResumeEntry = std::any_of(
                functions.begin(), functions.end(),
                [](const Function &fn) { return fn.start == 0x1010u; });
            t.IsFalse(hasRecursiveResumeEntry,
                      "discovery should not promote a return entry out of an existing entry wrapper");
        });

        tc.Run("entry reslice handles entries without containing function", [](TestCase &t) {
            std::vector<Function> functions = {
                makeFunction("entry_1008", 0x1008u, 0x1018u),
                makeFunction("entry_100c", 0x100Cu, 0x1018u)
            };

            std::unordered_map<uint32_t, std::vector<Instruction>> decodedFunctions;
            decodedFunctions[0x1008u] = {
                makeNopLike(0x1008u),
                makeNopLike(0x100Cu),
                makeNopLike(0x1010u),
                makeNopLike(0x1014u)
            };
            decodedFunctions[0x100Cu] = {
                makeNopLike(0x100Cu),
                makeNopLike(0x1010u),
                makeNopLike(0x1014u)
            };

            size_t resliced = PS2Recompiler::ResliceEntryFunctions(functions, decodedFunctions);
            t.Equals(resliced, static_cast<size_t>(1),
                     "expected only the earlier entry to be resliced");

            auto findByStart = [&](uint32_t start) -> const Function* {
                auto it = std::find_if(functions.begin(), functions.end(),
                                       [&](const Function &fn) { return fn.start == start; });
                if (it == functions.end())
                {
                    return nullptr;
                }
                return &(*it);
            };

            const Function *entry1008 = findByStart(0x1008u);
            const Function *entry100C = findByStart(0x100Cu);
            t.IsNotNull(entry1008, "entry at 0x1008 should exist");
            t.IsNotNull(entry100C, "entry at 0x100C should exist");
            if (entry1008)
            {
                t.Equals(entry1008->end, 0x100Cu,
                         "entry 0x1008 should be trimmed to next entry start");
            }
            if (entry100C)
            {
                t.Equals(entry100C->end, 0x1018u,
                         "entry 0x100C should keep original end");
            }

            auto decoded1008It = decodedFunctions.find(0x1008u);
            auto decoded100CIt = decodedFunctions.find(0x100Cu);
            t.IsTrue(decoded1008It != decodedFunctions.end(), "decoded slice for 0x1008 should exist");
            t.IsTrue(decoded100CIt != decodedFunctions.end(), "decoded slice for 0x100C should exist");
            if (decoded1008It != decodedFunctions.end())
            {
                t.Equals(decoded1008It->second.size(), static_cast<size_t>(1),
                         "entry 0x1008 slice should stop before 0x100C");
            }
            if (decoded100CIt != decodedFunctions.end())
            {
                t.Equals(decoded100CIt->second.size(), static_cast<size_t>(3),
                         "entry 0x100C slice should keep remaining instructions");
            }
        });

        tc.Run("non-executable section targets are ignored", [](TestCase &t) {
            std::vector<Section> sections = {
                {".text", 0x1000u, 0x2000u, 0u, true, false, false, true, nullptr},
                {".data", 0x3000u, 0x1000u, 0u, false, true, false, false, nullptr}
            };

            std::vector<Function> functions = {
                makeFunction("data_container", 0x3000u, 0x3010u),
                makeFunction("caller", 0x1800u, 0x1810u)
            };

            std::unordered_map<uint32_t, std::vector<Instruction>> decodedFunctions;
            decodedFunctions[0x3000u] = {
                makeNopLike(0x3000u),
                makeNopLike(0x3004u),
                makeNopLike(0x3008u),
                makeNopLike(0x300Cu)
            };
            decodedFunctions[0x1800u] = {
                makeAbsJump(0x1800u, 0x3004u, OPCODE_J),
                makeNopLike(0x1804u)
            };

            size_t discovered = PS2Recompiler::DiscoverAdditionalEntryPoints(
                functions, decodedFunctions, sections);
            t.Equals(discovered, static_cast<size_t>(0),
                     "non-executable targets should not produce additional entries");

            const bool hasDataEntry = std::any_of(functions.begin(), functions.end(),
                                                  [](const Function &fn) { return fn.start == 0x3004u; });
            t.IsFalse(hasDataEntry, "target in data section must not produce entry wrapper");
        });

        tc.Run("entry starting at jr ra is capped to return thunk", [](TestCase &t) {
            std::vector<Section> sections = {
                {".text", 0x1000u, 0x2000u, 0u, true, false, false, true, nullptr}
            };

            std::vector<Function> functions = {
                makeFunction("container", 0x1000u, 0x1200u),
                makeFunction("caller", 0x1300u, 0x1310u)
            };

            std::unordered_map<uint32_t, std::vector<Instruction>> decodedFunctions;
            decodedFunctions[0x1000u] = {
                makeNopLike(0x1000u),
                makeNopLike(0x1004u),
                makeNopLike(0x1008u),
                makeJrRa(0x10A0u),
                makeNopLike(0x10A4u),
                makeNopLike(0x10A8u),
                makeNopLike(0x10ACu)
            };
            decodedFunctions[0x1300u] = {
                makeAbsJump(0x1300u, 0x10A0u, OPCODE_J),
                makeNopLike(0x1304u)
            };

            size_t discovered = PS2Recompiler::DiscoverAdditionalEntryPoints(
                functions, decodedFunctions, sections);
            t.Equals(discovered, static_cast<size_t>(1),
                     "expected one additional entry from cross-function jump");

            auto entryIt = std::find_if(functions.begin(), functions.end(),
                                        [](const Function &fn) { return fn.start == 0x10A0u; });
            t.IsTrue(entryIt != functions.end(), "entry wrapper at 0x10A0 should exist");
            if (entryIt != functions.end())
            {
                t.Equals(entryIt->end, 0x10A8u,
                         "jr ra entry should end after delay slot, not at container end");
            }

            auto decodedEntryIt = decodedFunctions.find(0x10A0u);
            t.IsTrue(decodedEntryIt != decodedFunctions.end(),
                     "decoded entry slice for 0x10A0 should exist");
            if (decodedEntryIt != decodedFunctions.end())
            {
                t.Equals(decodedEntryIt->second.size(), static_cast<size_t>(2),
                         "jr ra entry slice should contain exactly jr+delay");
                if (!decodedEntryIt->second.empty())
                {
                    t.Equals(decodedEntryIt->second.front().address, 0x10A0u,
                             "entry slice should start at 0x10A0");
                }
            }
        });

        tc.Run("config manager parses jump_tables table entries", [](TestCase &t) {
            const auto uniqueSuffix = std::to_string(
                static_cast<unsigned long long>(std::chrono::steady_clock::now().time_since_epoch().count()));
            const std::filesystem::path configPath =
                std::filesystem::temp_directory_path() / ("ps2recomp-jump-table-" + uniqueSuffix + ".toml");

            std::ofstream configFile(configPath);
            t.IsTrue(static_cast<bool>(configFile), "temp config file should be writable");
            if (!configFile)
            {
                return;
            }

            configFile << "[general]\n";
            configFile << "input = \"dummy.elf\"\n";
            configFile << "output = \"out\"\n\n";
            configFile << "[jump_tables]\n";
            configFile << "[[jump_tables.table]]\n";
            configFile << "address = \"0x200000\"\n";
            configFile << "base_register = 9\n";
            configFile << "entries = [\n";
            configFile << "  { index = 0, target = \"0x1620\" },\n";
            configFile << "  { index = 1, target = \"0x1630\" },\n";
            configFile << "]\n";
            configFile.close();

            ConfigManager manager(configPath.string());
            RecompilerConfig config = manager.loadConfig();

            t.Equals(config.jumpTables.size(), static_cast<size_t>(1),
                     "one configured jump table should be loaded");
            if (!config.jumpTables.empty())
            {
                const JumpTable &table = config.jumpTables.front();
                t.Equals(table.address, 0x200000u, "table address should parse from hex string");
                t.Equals(table.baseRegister, 9u, "base register should parse");
                t.Equals(table.entries.size(), static_cast<size_t>(2),
                         "two jump table entries should parse");
                if (table.entries.size() >= 2)
                {
                    t.Equals(table.entries[0].index, 0u, "first entry index should parse");
                    t.Equals(table.entries[0].target, 0x1620u, "first entry target should parse");
                    t.Equals(table.entries[1].index, 1u, "second entry index should parse");
                    t.Equals(table.entries[1].target, 0x1630u, "second entry target should parse");
                }
            }

            std::error_code removeError;
            std::filesystem::remove(configPath, removeError);
        });

        tc.Run("elf parser ignores STT_FUNC symbols in non-executable sections", [](TestCase &t) {
            const auto uniqueSuffix = std::to_string(
                static_cast<unsigned long long>(std::chrono::steady_clock::now().time_since_epoch().count()));
            const std::filesystem::path elfPath =
                std::filesystem::temp_directory_path() / ("ps2recomp-parser-" + uniqueSuffix + ".elf");

            const bool writeOk = writeMinimalMipsElfWithCodeAndDataFunctionSymbols(elfPath);
            t.IsTrue(writeOk, "temporary ELF should be generated");
            if (!writeOk)
            {
                return;
            }

            ElfParser parser(elfPath.string());
            const bool parseOk = parser.parse();
            t.IsTrue(parseOk, "generated ELF should parse");
            if (!parseOk)
            {
                std::error_code removeError;
                std::filesystem::remove(elfPath, removeError);
                return;
            }

            const auto functions = parser.extractFunctions();
            const bool hasCodeFunction = std::any_of(functions.begin(), functions.end(),
                                                     [](const Function &fn)
                                                     { return fn.start == 0x00100000u; });
            const bool hasDataFunction = std::any_of(functions.begin(), functions.end(),
                                                     [](const Function &fn)
                                                     { return fn.start == 0x00200000u; });

            t.IsTrue(hasCodeFunction, "function in executable section should be retained");
            t.IsFalse(hasDataFunction, "STT_FUNC symbol in .data must be ignored");

            std::error_code removeError;
            std::filesystem::remove(elfPath, removeError);
        });

        tc.Run("ghidra map replaces JAL fallback-only auto starts", [](TestCase &t) {
            const auto uniqueSuffix = std::to_string(
                static_cast<unsigned long long>(std::chrono::steady_clock::now().time_since_epoch().count()));
            const std::filesystem::path elfPath =
                std::filesystem::temp_directory_path() / ("ps2recomp-ghidra-merge-" + uniqueSuffix + ".elf");
            const std::filesystem::path mapPath =
                std::filesystem::temp_directory_path() / ("ps2recomp-ghidra-merge-" + uniqueSuffix + ".csv");

            const bool writeOk = writeMinimalMipsElfWithJalFallbackTarget(elfPath);
            t.IsTrue(writeOk, "temporary ELF should be generated");
            if (!writeOk)
            {
                return;
            }

            ElfParser parser(elfPath.string());
            const bool parseOk = parser.parse();
            t.IsTrue(parseOk, "generated ELF should parse");
            if (!parseOk)
            {
                std::error_code removeError;
                std::filesystem::remove(elfPath, removeError);
                return;
            }

            const auto fallbackExtras = parser.extractExtraFunctions();
            const bool hasFallbackStart = std::any_of(
                fallbackExtras.begin(), fallbackExtras.end(),
                [](const Function &fn)
                { return fn.start == 0x00100010u; });
            t.IsTrue(hasFallbackStart, "JAL fallback should discover secondary start before map load");

            std::ofstream mapFile(mapPath);
            t.IsTrue(static_cast<bool>(mapFile), "ghidra map file should be writable");
            if (!mapFile)
            {
                std::error_code removeError;
                std::filesystem::remove(elfPath, removeError);
                return;
            }
            mapFile << "name,start,end,size\n";
            mapFile << "FUN_00100000,0x00100000,0x00100010,0x10\n";
            mapFile.close();

            const bool mapLoaded = parser.loadGhidraFunctionMap(mapPath.string());
            t.IsTrue(mapLoaded, "ghidra map should load");

            const auto functions = parser.extractFunctions();
            const auto entryIt = std::find_if(
                functions.begin(), functions.end(),
                [](const Function &fn)
                { return fn.start == 0x00100000u; });
            t.IsTrue(entryIt != functions.end(), "ghidra entry should exist");
            if (entryIt != functions.end())
            {
                t.Equals(entryIt->name, std::string("FUN_00100000"),
                         "ghidra name should win over fallback auto-name");
            }

            const bool stillHasFallbackOnlyStart = std::any_of(
                functions.begin(), functions.end(),
                [](const Function &fn)
                { return fn.start == 0x00100010u; });
            t.IsFalse(stillHasFallbackOnlyStart,
                      "fallback-only function starts should be removed once ghidra map is loaded");

            std::error_code removeError;
            std::filesystem::remove(elfPath, removeError);
            std::filesystem::remove(mapPath, removeError);
        });

        tc.Run("runtime call resolution includes Veronica compatibility aliases", [](TestCase &t) {
            t.Equals(ps2_runtime_calls::resolveSyscallName("ReleaseAlarm"), std::string_view{"ReleaseAlarm"},
                     "ReleaseAlarm should resolve as a syscall name");
            t.Equals(ps2_runtime_calls::resolveSyscallName("_ReleaseAlarm"), std::string_view{"ReleaseAlarm"},
                     "underscore ReleaseAlarm alias should resolve to ReleaseAlarm");
            t.Equals(ps2_runtime_calls::resolveSyscallName("EnableCache"), std::string_view{"EnableCache"},
                     "EnableCache should resolve as a syscall name");
            t.Equals(ps2_runtime_calls::resolveSyscallName("DisableCache"), std::string_view{"DisableCache"},
                     "DisableCache should resolve as a syscall name");
            t.Equals(ps2_runtime_calls::resolveStubName("isceSifSetDma"), std::string_view{"isceSifSetDma"},
                     "isceSifSetDma should resolve as a stub name");
            t.Equals(ps2_runtime_calls::resolveStubName("isceSifSetDChain"), std::string_view{"isceSifSetDChain"},
                     "isceSifSetDChain should resolve as a stub name");
            t.Equals(ps2_runtime_calls::resolveStubName("memalign"), std::string_view{"memalign"},
                     "memalign should resolve as a stub name");
            t.Equals(ps2_runtime_calls::resolveStubName("_memalign_r"), std::string_view{"memalign_r"},
                     "_memalign_r should resolve to the memalign_r stub");
            t.Equals(ps2_runtime_calls::resolveStubName("_realloc_r"), std::string_view{"realloc_r"},
                     "_realloc_r should resolve to the realloc_r stub");
            t.Equals(ps2_runtime_calls::resolveStubName("malloc_extend_top"), std::string_view{"malloc_extend_top"},
                     "malloc_extend_top should resolve as an allocator compatibility stub");
            t.Equals(ps2_runtime_calls::resolveStubName("__malloc_lock"), std::string_view{"__malloc_lock"},
                     "__malloc_lock should resolve as an allocator compatibility stub");
            t.Equals(ps2_runtime_calls::resolveStubName("__malloc_unlock"), std::string_view{"__malloc_unlock"},
                     "__malloc_unlock should resolve as an allocator compatibility stub");
            t.Equals(ps2_runtime_calls::resolveStubName("memclr"), std::string_view{"memclr"},
                     "memclr should resolve as a runtime stub");
            t.Equals(ps2_runtime_calls::resolveStubName("__divdi3"), std::string_view{"__divdi3"},
                     "__divdi3 should resolve as a runtime stub");
            t.Equals(ps2_runtime_calls::resolveStubName("__mcmp"), std::string_view{},
                     "__mcmp should be left for recompilation");
            t.Equals(ps2_runtime_calls::resolveStubName("__sprint"), std::string_view{},
                     "__sprint should be left for recompilation");
            t.Equals(ps2_runtime_calls::resolveStubName("__sprint_r"), std::string_view{},
                     "__sprint_r should be left for recompilation");
            t.Equals(ps2_runtime_calls::resolveStubName("__sbprintf"), std::string_view{},
                     "__sbprintf should be left for recompilation");
        });

        tc.Run("respect max length for .cpp filenames", [](TestCase& t) {

            t.IsTrue(PS2Recompiler::ClampFilenameLength("ReallyLongFunctionNameReallyLongFunctionNameReallyLongFunctionName_0x12345678",".cpp",50).length() <= 50,"Function name must be max 50 characters");

            t.IsTrue(PS2Recompiler::ClampFilenameLength("ReallyLongFunctionNameReallyLongFunctionNameReallyLongFunctionName_0x12345678", ".cpp", 50).rfind("0x12345678") != std::string::npos, "Function name must mantain the function address at the end, if present");

        });

        tc.Run("external call target manifest parsing sorts, dedupes, and ignores comments/blanks", [](TestCase &t) {
            std::istringstream manifest(
                "0x00462df4\n"
                "\n"
                "# a comment line\n"
                "0x001000A0\n"
                "0x00462df4\n"
                "   \n"
                "0x1000a0\n"
                "  # indented comment\n"
                "0x00500000\n");

            const std::vector<uint32_t> parsed = PS2Recompiler::ParseCallTargetManifest(manifest);

            t.Equals(parsed.size(), static_cast<size_t>(3),
                     "duplicate and case-variant addresses should collapse to unique targets");
            if (parsed.size() == 3)
            {
                t.Equals(parsed[0], 0x001000A0u, "targets should be sorted ascending");
                t.Equals(parsed[1], 0x00462df4u, "second target should be the mid-range address");
                t.Equals(parsed[2], 0x00500000u, "third target should be the highest address");
            }
        });

        tc.Run("external call target manifest parsing handles empty input", [](TestCase &t) {
            std::istringstream manifest("");
            const std::vector<uint32_t> parsed = PS2Recompiler::ParseCallTargetManifest(manifest);
            t.Equals(parsed.size(), static_cast<size_t>(0), "empty manifest should parse to an empty target list");
        });

        // Tier 1 end-to-end regression test: drives a REAL PS2Recompiler instance through
        // initialize() -> recompile() -> generateOutput() over a real ELF fixture and a real
        // manifest file, and inspects the actual register_functions.cpp produced by the
        // production FunctionTableEmitter. This exercises the full ingestion path -
        // loadExternalCallTargetManifests() -> discoverAdditionalEntryPoints()'s
        // m_ingestedExternalCallTargets -> owner mapping -> resume-target push (the code
        // at ps2_recompiler.cpp around lines 1938-1953) - and nothing else in this fixture
        // is capable of discovering the mid-body target, so this test fails if that code
        // path is disabled or removed.
        tc.Run("full pipeline: manifest-ingested packed jal-only entry registers into its owning unit", [](TestCase &t) {
            const auto uniqueSuffix = std::to_string(
                static_cast<unsigned long long>(std::chrono::steady_clock::now().time_since_epoch().count()));
            const std::filesystem::path workDir =
                std::filesystem::temp_directory_path() / ("ps2recomp-packed-jal-" + uniqueSuffix);
            std::error_code mkdirError;
            std::filesystem::create_directories(workDir, mkdirError);
            t.IsTrue(!mkdirError, "work directory should be created");

            const std::filesystem::path elfPath = workDir / "fixture.elf";
            constexpr uint32_t containerStart = 0x00100000u;
            constexpr uint32_t containerEnd = 0x00100018u;   // container_fn: 6 NOP-filled words, jr $ra tail
            constexpr uint32_t midBodyTarget = 0x00100008u;  // T: not the head, not any jal's target anywhere

            const bool wroteElf = writeContainerOnlyElf(elfPath, containerStart, containerEnd);
            t.IsTrue(wroteElf, "container-only fixture ELF should be generated");
            if (!wroteElf)
            {
                std::error_code cleanupError;
                std::filesystem::remove_all(workDir, cleanupError);
                return;
            }

            const std::filesystem::path manifestPath = workDir / "manifest.txt";
            {
                std::ofstream manifestFile(manifestPath);
                t.IsTrue(static_cast<bool>(manifestFile), "manifest file should be writable");
                manifestFile << "0x00100008\n";
            }

            const std::filesystem::path outWithManifest = workDir / "out_with";
            const std::filesystem::path outWithoutManifest = workDir / "out_without";

            const std::filesystem::path configWithManifestPath = workDir / "with_manifest.toml";
            {
                std::ofstream cfg(configWithManifestPath);
                t.IsTrue(static_cast<bool>(cfg), "config (with manifest) should be writable");
                cfg << "[general]\n";
                // generic_string(): backslashes from path::string() on Windows are
                // escape introducers inside a TOML basic string and break the parse.
                cfg << "input = \"" << elfPath.generic_string() << "\"\n";
                cfg << "output = \"" << outWithManifest.generic_string() << "\"\n";
                cfg << "external_call_target_manifests = [\"" << manifestPath.generic_string() << "\"]\n";
            }

            // "Without manifest" case omits external_call_target_manifests entirely.
            const std::filesystem::path configWithoutManifestPath = workDir / "without_manifest.toml";
            {
                std::ofstream cfg(configWithoutManifestPath);
                t.IsTrue(static_cast<bool>(cfg), "config (without manifest) should be writable");
                cfg << "[general]\n";
                cfg << "input = \"" << elfPath.generic_string() << "\"\n";
                cfg << "output = \"" << outWithoutManifest.generic_string() << "\"\n";
            }

            std::string headOwnerWithManifest;
            std::string targetOwnerWithManifest;

            // --- Run WITH the manifest ---
            {
                PS2Recompiler recompiler(configWithManifestPath.string());
                t.IsTrue(recompiler.initialize(), "initialize() should succeed for the with-manifest run");
                t.IsTrue(recompiler.recompile(), "recompile() should succeed for the with-manifest run");
                recompiler.generateOutput();

                const std::filesystem::path registerPath = outWithManifest / "register_functions.cpp";
                std::ifstream registerFile(registerPath);
                t.IsTrue(static_cast<bool>(registerFile),
                         "register_functions.cpp should be written for the with-manifest run");
                std::ostringstream contentStream;
                contentStream << registerFile.rdbuf();
                const std::string content = contentStream.str();

                const auto headLines = findLinesContaining(content, "// 0x100000");
                const auto targetLines = findLinesContaining(content, "// 0x100008");

                t.IsTrue(!headLines.empty(), "container head 0x100000 should be registered (sanity)");
                t.IsTrue(!targetLines.empty(),
                         "manifest-ingested target 0x100008 must be registered when the manifest is supplied - "
                         "this is the line that disappears if the ingestion->owner-mapping->resume-target push "
                         "(ps2_recompiler.cpp ~1938-1953) is disabled");

                if (!headLines.empty())
                {
                    headOwnerWithManifest = extractOwnerNameFromRegistrationLine(headLines.front());
                    t.IsTrue(headOwnerWithManifest.find("container_fn") != std::string::npos,
                             "container head should register under a container_fn-derived name");
                }
                if (!targetLines.empty())
                {
                    targetOwnerWithManifest = extractOwnerNameFromRegistrationLine(targetLines.front());
                    t.IsTrue(targetOwnerWithManifest.find("container_fn") != std::string::npos,
                             "0x100008 must be mapped to the container_fn owner, not left unresolved");
                }

                // Test C (boundary/owner-integrity invariant), folded in here so it shares
                // this fixture and this pipeline run: the manifest-ingested entry must
                // dispatch INTO the owning unit - i.e. resolve to the exact same generated
                // owner name as the container's own head - rather than being sliced into a
                // truncated standalone function ending at an interior label. The
                // resume-mapping path (ps2_recompiler.cpp ~1938-1953) pushes into
                // m_resumeEntryTargetsByOwner and creates no per-entry Function::end, so
                // there is no "->end" boundary to assert here the way there is for the
                // slicer path. That slicer-path End boundary is already covered by the
                // existing tests "additional entries split at nearest discovered boundary"
                // (~line 280) and "entry reslice trims earlier entries after late
                // discovery" (~line 374).
                if (!headOwnerWithManifest.empty() && !targetOwnerWithManifest.empty())
                {
                    t.Equals(targetOwnerWithManifest, headOwnerWithManifest,
                             "0x100008 must resolve to the SAME owner name as the container head, proving "
                             "dispatch resumes into the owning unit (which retains its full declared range) "
                             "rather than being sliced into a separate standalone function");
                }
            }

            // --- Run WITHOUT the manifest: reproduces the original bug. Nothing else in
            // this fixture can discover 0x100008 (no jal/branch anywhere targets it), so it
            // must be left unregistered.
            {
                PS2Recompiler recompiler(configWithoutManifestPath.string());
                t.IsTrue(recompiler.initialize(), "initialize() should succeed for the without-manifest run");
                t.IsTrue(recompiler.recompile(), "recompile() should succeed for the without-manifest run");
                recompiler.generateOutput();

                const std::filesystem::path registerPath = outWithoutManifest / "register_functions.cpp";
                std::ifstream registerFile(registerPath);
                t.IsTrue(static_cast<bool>(registerFile),
                         "register_functions.cpp should be written for the without-manifest run");
                std::ostringstream contentStream;
                contentStream << registerFile.rdbuf();
                const std::string content = contentStream.str();

                const auto headLines = findLinesContaining(content, "// 0x100000");
                const auto targetLines = findLinesContaining(content, "// 0x100008");

                t.IsTrue(!headLines.empty(),
                         "container head 0x100000 should still be registered without a manifest (sanity)");
                t.IsTrue(targetLines.empty(),
                         "without a manifest, 0x100008 must NOT be registered - this reproduces the original bug");
            }

            std::error_code removeError;
            std::filesystem::remove_all(workDir, removeError);
        });

        // Headline two-ELF cross-unit regression: drives two REAL, independent
        // PS2Recompiler instances (A and B) over two separate ELF fixtures with no
        // shared address space overlap in intent - A's jal target T exists only
        // inside B and lies entirely outside every section of A. Proves the full
        // chain: A's analysis phase emits T (Fix 1, the permissive collector) into
        // A's manifest, independent of build order relative to B; B then ingests
        // A's manifest and registers T into its own owning function.
        tc.Run("two-ELF: A emits T, B ingests T", [](TestCase &t) {
            const auto uniqueSuffix = std::to_string(
                static_cast<unsigned long long>(std::chrono::steady_clock::now().time_since_epoch().count()));
            const std::filesystem::path workDir =
                std::filesystem::temp_directory_path() / ("ps2recomp-two-elf-" + uniqueSuffix);
            std::error_code mkdirError;
            std::filesystem::create_directories(workDir, mkdirError);
            t.IsTrue(!mkdirError, "work directory should be created");

            constexpr uint32_t callerStart = 0x00300000u;    // A's own base range
            constexpr uint32_t containerStart = 0x00100000u; // B's own base range
            constexpr uint32_t containerEnd = 0x00100018u;   // container_fn: 6 NOP-filled words, jr $ra tail
            constexpr uint32_t targetT = 0x00100008u;        // T: inside B, not B's head, not any of A's own sections

            const std::filesystem::path elfAPath = workDir / "a.elf";
            const std::filesystem::path elfBPath = workDir / "b.elf";
            const bool wroteA = writeJalToForeignTargetElf(elfAPath, callerStart, targetT);
            const bool wroteB = writeContainerOnlyElf(elfBPath, containerStart, containerEnd);
            t.IsTrue(wroteA, "ELF A fixture should be generated");
            t.IsTrue(wroteB, "ELF B fixture should be generated");
            if (!wroteA || !wroteB)
            {
                std::error_code cleanupError;
                std::filesystem::remove_all(workDir, cleanupError);
                return;
            }

            const std::filesystem::path outA = workDir / "out_a";
            const std::filesystem::path outAAgain = workDir / "out_a_again";
            const std::filesystem::path outB = workDir / "out_b";
            const std::filesystem::path outBNoManifest = workDir / "out_b_no_manifest";

            const std::filesystem::path configAPath = workDir / "a.toml";
            {
                std::ofstream cfg(configAPath);
                t.IsTrue(static_cast<bool>(cfg), "config A should be writable");
                cfg << "[general]\n";
                cfg << "input = \"" << elfAPath.generic_string() << "\"\n";
                cfg << "output = \"" << outA.generic_string() << "\"\n";
            }
            const std::filesystem::path configAAgainPath = workDir / "a_again.toml";
            {
                std::ofstream cfg(configAAgainPath);
                t.IsTrue(static_cast<bool>(cfg), "config A (again) should be writable");
                cfg << "[general]\n";
                cfg << "input = \"" << elfAPath.generic_string() << "\"\n";
                cfg << "output = \"" << outAAgain.generic_string() << "\"\n";
            }

            // --- Analysis phase for A, run once, then again after B's own analysis
            // phase runs in between - proves emission is order-independent (it depends
            // only on A's own decoded functions/sections, never on any sibling).
            {
                PS2Recompiler recompilerA(configAPath.string());
                t.IsTrue(recompilerA.initialize(), "A: initialize() should succeed");
                t.IsTrue(recompilerA.recompile(true), "A: analysis-phase recompile(true) should succeed");
            }

            const std::filesystem::path configBAnalysisPath = workDir / "b_analysis.toml";
            const std::filesystem::path outBAnalysis = workDir / "out_b_analysis";
            {
                std::ofstream cfg(configBAnalysisPath);
                t.IsTrue(static_cast<bool>(cfg), "config B (analysis) should be writable");
                cfg << "[general]\n";
                cfg << "input = \"" << elfBPath.generic_string() << "\"\n";
                cfg << "output = \"" << outBAnalysis.generic_string() << "\"\n";
            }
            {
                PS2Recompiler recompilerBAnalysis(configBAnalysisPath.string());
                t.IsTrue(recompilerBAnalysis.initialize(), "B: initialize() should succeed for its own analysis phase");
                t.IsTrue(recompilerBAnalysis.recompile(true), "B: analysis-phase recompile(true) should succeed");
            }

            {
                PS2Recompiler recompilerAAgain(configAAgainPath.string());
                t.IsTrue(recompilerAAgain.initialize(), "A (again): initialize() should succeed");
                t.IsTrue(recompilerAAgain.recompile(true), "A (again): analysis-phase recompile(true) should succeed");
            }

            const std::filesystem::path manifestAPath = outA / "external_call_targets.txt";
            const std::filesystem::path manifestAAgainPath = outAAgain / "external_call_targets.txt";
            std::ifstream manifestAFile(manifestAPath, std::ios::binary);
            std::ifstream manifestAAgainFile(manifestAAgainPath, std::ios::binary);
            t.IsTrue(static_cast<bool>(manifestAFile), "A's manifest should be written");
            t.IsTrue(static_cast<bool>(manifestAAgainFile), "A's manifest (again) should be written");
            std::ostringstream manifestAStream;
            std::ostringstream manifestAAgainStream;
            manifestAStream << manifestAFile.rdbuf();
            manifestAAgainStream << manifestAAgainFile.rdbuf();
            const std::string manifestAContent = manifestAStream.str();
            const std::string manifestAAgainContent = manifestAAgainStream.str();

            t.IsTrue(manifestAContent.find("0x00100008") != std::string::npos,
                     "A's emitted manifest must contain T (0x00100008), which lies outside every section of A - "
                     "this is the case Fix 1's permissive collector restores");
            t.Equals(manifestAContent, manifestAAgainContent,
                     "A's emitted manifest must be byte-identical whether A's analysis runs before or after "
                     "B's analysis - emission depends only on A's own decoded functions/sections");

            // --- B ingests A's manifest and registers T into its own owning unit.
            const std::filesystem::path configBPath = workDir / "b.toml";
            {
                std::ofstream cfg(configBPath);
                t.IsTrue(static_cast<bool>(cfg), "config B should be writable");
                cfg << "[general]\n";
                cfg << "input = \"" << elfBPath.generic_string() << "\"\n";
                cfg << "output = \"" << outB.generic_string() << "\"\n";
                cfg << "external_call_target_manifests = [\"" << manifestAPath.generic_string() << "\"]\n";
            }
            {
                PS2Recompiler recompilerB(configBPath.string());
                t.IsTrue(recompilerB.initialize(), "B: initialize() should succeed");
                t.IsTrue(recompilerB.recompile(false), "B: generate-phase recompile(false) should succeed");
                recompilerB.generateOutput();

                const std::filesystem::path registerPath = outB / "register_functions.cpp";
                std::ifstream registerFile(registerPath);
                t.IsTrue(static_cast<bool>(registerFile), "B's register_functions.cpp should be written");
                std::ostringstream contentStream;
                contentStream << registerFile.rdbuf();
                const std::string content = contentStream.str();

                const auto headLines = findLinesContaining(content, "// 0x100000");
                const auto targetLines = findLinesContaining(content, "// 0x100008");

                t.IsTrue(!headLines.empty(), "B's container head 0x100000 should be registered (sanity)");
                t.IsTrue(!targetLines.empty(),
                         "T (0x100008) must be registered in B once B ingests A's manifest");

                if (!headLines.empty() && !targetLines.empty())
                {
                    const std::string headOwner = extractOwnerNameFromRegistrationLine(headLines.front());
                    const std::string targetOwner = extractOwnerNameFromRegistrationLine(targetLines.front());
                    t.Equals(targetOwner, headOwner,
                             "T must resolve to the SAME owner name as B's container head, proving dispatch "
                             "resumes into B's owning unit");
                }
            }

            // --- Negative arm: B with no manifest configured must not discover T -
            // nothing in B's own fixture can discover a mid-body target on its own.
            const std::filesystem::path configBNoManifestPath = workDir / "b_no_manifest.toml";
            {
                std::ofstream cfg(configBNoManifestPath);
                t.IsTrue(static_cast<bool>(cfg), "config B (no manifest) should be writable");
                cfg << "[general]\n";
                cfg << "input = \"" << elfBPath.generic_string() << "\"\n";
                cfg << "output = \"" << outBNoManifest.generic_string() << "\"\n";
            }
            {
                PS2Recompiler recompilerBNoManifest(configBNoManifestPath.string());
                t.IsTrue(recompilerBNoManifest.initialize(), "B (no manifest): initialize() should succeed");
                t.IsTrue(recompilerBNoManifest.recompile(false), "B (no manifest): recompile(false) should succeed");
                recompilerBNoManifest.generateOutput();

                const std::filesystem::path registerPath = outBNoManifest / "register_functions.cpp";
                std::ifstream registerFile(registerPath);
                t.IsTrue(static_cast<bool>(registerFile), "B's register_functions.cpp should be written");
                std::ostringstream contentStream;
                contentStream << registerFile.rdbuf();
                const std::string content = contentStream.str();

                const auto targetLines = findLinesContaining(content, "// 0x100008");
                t.IsTrue(targetLines.empty(),
                         "without A's manifest, T (0x100008) must NOT be registered - reproduces the original "
                         "cross-unit gap");
            }

            std::error_code removeError;
            std::filesystem::remove_all(workDir, removeError);
        });

        tc.Run("missing configured manifest hard-fails at generate time", [](TestCase &t) {
            const auto uniqueSuffix = std::to_string(
                static_cast<unsigned long long>(std::chrono::steady_clock::now().time_since_epoch().count()));
            const std::filesystem::path workDir =
                std::filesystem::temp_directory_path() / ("ps2recomp-missing-manifest-" + uniqueSuffix);
            std::error_code mkdirError;
            std::filesystem::create_directories(workDir, mkdirError);
            t.IsTrue(!mkdirError, "work directory should be created");

            const std::filesystem::path elfPath = workDir / "fixture.elf";
            const bool wroteElf = writeContainerOnlyElf(elfPath, 0x00100000u, 0x00100018u);
            t.IsTrue(wroteElf, "fixture ELF should be generated");
            if (!wroteElf)
            {
                std::error_code cleanupError;
                std::filesystem::remove_all(workDir, cleanupError);
                return;
            }

            const std::filesystem::path missingManifestPath = workDir / "does_not_exist.txt";
            const std::filesystem::path outDir = workDir / "out";
            const std::filesystem::path configPath = workDir / "config.toml";
            {
                std::ofstream cfg(configPath);
                t.IsTrue(static_cast<bool>(cfg), "config should be writable");
                cfg << "[general]\n";
                cfg << "input = \"" << elfPath.generic_string() << "\"\n";
                cfg << "output = \"" << outDir.generic_string() << "\"\n";
                cfg << "external_call_target_manifests = [\"" << missingManifestPath.generic_string() << "\"]\n";
            }

            PS2Recompiler recompiler(configPath.string());
            t.IsTrue(recompiler.initialize(), "initialize() should succeed");
            t.IsFalse(recompiler.recompile(false),
                      "recompile(false) must hard-fail when a configured manifest cannot be opened");

            std::error_code removeError;
            std::filesystem::remove_all(workDir, removeError);
        });

        tc.Run("existing (even empty) configured manifest does not hard-fail", [](TestCase &t) {
            const auto uniqueSuffix = std::to_string(
                static_cast<unsigned long long>(std::chrono::steady_clock::now().time_since_epoch().count()));
            const std::filesystem::path workDir =
                std::filesystem::temp_directory_path() / ("ps2recomp-empty-manifest-" + uniqueSuffix);
            std::error_code mkdirError;
            std::filesystem::create_directories(workDir, mkdirError);
            t.IsTrue(!mkdirError, "work directory should be created");

            const std::filesystem::path elfPath = workDir / "fixture.elf";
            const bool wroteElf = writeContainerOnlyElf(elfPath, 0x00100000u, 0x00100018u);
            t.IsTrue(wroteElf, "fixture ELF should be generated");
            if (!wroteElf)
            {
                std::error_code cleanupError;
                std::filesystem::remove_all(workDir, cleanupError);
                return;
            }

            const std::filesystem::path emptyManifestPath = workDir / "empty_manifest.txt";
            {
                std::ofstream manifestFile(emptyManifestPath);
                t.IsTrue(static_cast<bool>(manifestFile), "empty manifest file should be writable");
            }

            const std::filesystem::path outDir = workDir / "out";
            const std::filesystem::path configPath = workDir / "config.toml";
            {
                std::ofstream cfg(configPath);
                t.IsTrue(static_cast<bool>(cfg), "config should be writable");
                cfg << "[general]\n";
                cfg << "input = \"" << elfPath.generic_string() << "\"\n";
                cfg << "output = \"" << outDir.generic_string() << "\"\n";
                cfg << "external_call_target_manifests = [\"" << emptyManifestPath.generic_string() << "\"]\n";
            }

            PS2Recompiler recompiler(configPath.string());
            t.IsTrue(recompiler.initialize(), "initialize() should succeed");
            t.IsTrue(recompiler.recompile(false),
                     "recompile(false) must NOT hard-fail when a configured manifest exists (even if empty) - "
                     "this half-guard catches a mutation that makes the hard-fail unconditional");

            std::error_code removeError;
            std::filesystem::remove_all(workDir, removeError);
        });

        tc.Run("analysis phase never hard-fails on a missing sibling manifest", [](TestCase &t) {
            const auto uniqueSuffix = std::to_string(
                static_cast<unsigned long long>(std::chrono::steady_clock::now().time_since_epoch().count()));
            const std::filesystem::path workDir =
                std::filesystem::temp_directory_path() / ("ps2recomp-analysis-safe-" + uniqueSuffix);
            std::error_code mkdirError;
            std::filesystem::create_directories(workDir, mkdirError);
            t.IsTrue(!mkdirError, "work directory should be created");

            const std::filesystem::path elfAPath = workDir / "a.elf";
            const bool wroteA = writeContainerOnlyElf(elfAPath, 0x00100000u, 0x00100018u);
            t.IsTrue(wroteA, "ELF A fixture should be generated");
            if (!wroteA)
            {
                std::error_code cleanupError;
                std::filesystem::remove_all(workDir, cleanupError);
                return;
            }

            // B's manifest has not been emitted anywhere in this test - it is a sibling
            // that simply has not run its own analysis phase yet.
            const std::filesystem::path notYetEmittedBManifestPath = workDir / "b_out" / "external_call_targets.txt";

            const std::filesystem::path outA = workDir / "out_a";
            const std::filesystem::path configAPath = workDir / "a.toml";
            {
                std::ofstream cfg(configAPath);
                t.IsTrue(static_cast<bool>(cfg), "config A should be writable");
                cfg << "[general]\n";
                cfg << "input = \"" << elfAPath.generic_string() << "\"\n";
                cfg << "output = \"" << outA.generic_string() << "\"\n";
                cfg << "external_call_target_manifests = [\"" << notYetEmittedBManifestPath.generic_string() << "\"]\n";
            }

            PS2Recompiler recompilerA(configAPath.string());
            t.IsTrue(recompilerA.initialize(), "A: initialize() should succeed");
            t.IsTrue(recompilerA.recompile(true),
                     "A: analysis-phase recompile(true) must succeed even though its configured sibling "
                     "manifest does not exist yet - the split makes the hard-fail safe");

            const std::filesystem::path manifestAPath = outA / "external_call_targets.txt";
            std::ifstream manifestAFile(manifestAPath);
            t.IsTrue(static_cast<bool>(manifestAFile), "A's own manifest should still be emitted");

            std::error_code removeError;
            std::filesystem::remove_all(workDir, removeError);
        });

        tc.Run("two-phase clean build of mutually-calling units", [](TestCase &t) {
            const auto uniqueSuffix = std::to_string(
                static_cast<unsigned long long>(std::chrono::steady_clock::now().time_since_epoch().count()));
            const std::filesystem::path workDir =
                std::filesystem::temp_directory_path() / ("ps2recomp-mutual-two-phase-" + uniqueSuffix);
            std::error_code mkdirError;
            std::filesystem::create_directories(workDir, mkdirError);
            t.IsTrue(!mkdirError, "work directory should be created");

            constexpr uint32_t containerStartA = 0x00100000u;
            constexpr uint32_t containerEndA = 0x00100020u;
            constexpr uint32_t targetA = 0x00100008u; // A's mid-body target, exposed to B

            constexpr uint32_t containerStartB = 0x00200000u;
            constexpr uint32_t containerEndB = 0x00200020u;
            constexpr uint32_t targetB = 0x00200008u; // B's mid-body target, exposed to A

            const std::filesystem::path elfAPath = workDir / "a.elf";
            const std::filesystem::path elfBPath = workDir / "b.elf";
            const bool wroteA = writeMutualCallElf(elfAPath, containerStartA, containerEndA, targetB);
            const bool wroteB = writeMutualCallElf(elfBPath, containerStartB, containerEndB, targetA);
            t.IsTrue(wroteA, "ELF A fixture should be generated");
            t.IsTrue(wroteB, "ELF B fixture should be generated");
            if (!wroteA || !wroteB)
            {
                std::error_code cleanupError;
                std::filesystem::remove_all(workDir, cleanupError);
                return;
            }

            const std::filesystem::path outA = workDir / "out_a";
            const std::filesystem::path outB = workDir / "out_b";
            const std::filesystem::path manifestAPath = outA / "external_call_targets.txt";
            const std::filesystem::path manifestBPath = outB / "external_call_targets.txt";

            const std::filesystem::path configAPath = workDir / "a.toml";
            {
                std::ofstream cfg(configAPath);
                t.IsTrue(static_cast<bool>(cfg), "config A should be writable");
                cfg << "[general]\n";
                cfg << "input = \"" << elfAPath.generic_string() << "\"\n";
                cfg << "output = \"" << outA.generic_string() << "\"\n";
                cfg << "external_call_target_manifests = [\"" << manifestBPath.generic_string() << "\"]\n";
            }
            const std::filesystem::path configBPath = workDir / "b.toml";
            {
                std::ofstream cfg(configBPath);
                t.IsTrue(static_cast<bool>(cfg), "config B should be writable");
                cfg << "[general]\n";
                cfg << "input = \"" << elfBPath.generic_string() << "\"\n";
                cfg << "output = \"" << outB.generic_string() << "\"\n";
                cfg << "external_call_target_manifests = [\"" << manifestAPath.generic_string() << "\"]\n";
            }

            // --- Phase 1: analysis phase for both units, neither sibling manifest
            // exists yet at the time either analysis phase runs.
            {
                PS2Recompiler recompilerA(configAPath.string());
                t.IsTrue(recompilerA.initialize(), "A: initialize() should succeed");
                t.IsTrue(recompilerA.recompile(true), "A: phase 1 recompile(true) should succeed");
            }
            {
                PS2Recompiler recompilerB(configBPath.string());
                t.IsTrue(recompilerB.initialize(), "B: initialize() should succeed");
                t.IsTrue(recompilerB.recompile(true), "B: phase 1 recompile(true) should succeed");
            }

            std::ifstream manifestAFile(manifestAPath);
            std::ifstream manifestBFile(manifestBPath);
            t.IsTrue(static_cast<bool>(manifestAFile), "A's manifest should exist after phase 1");
            t.IsTrue(static_cast<bool>(manifestBFile), "B's manifest should exist after phase 1");

            // --- Phase 2: generate phase for both units, each now ingesting the
            // other's phase-1 manifest with no missing-manifest failure.
            {
                PS2Recompiler recompilerA(configAPath.string());
                t.IsTrue(recompilerA.initialize(), "A: initialize() should succeed for phase 2");
                t.IsTrue(recompilerA.recompile(false), "A: phase 2 recompile(false) should succeed");
                recompilerA.generateOutput();

                std::ifstream registerFile(outA / "register_functions.cpp");
                t.IsTrue(static_cast<bool>(registerFile), "A's register_functions.cpp should be written");
                std::ostringstream contentStream;
                contentStream << registerFile.rdbuf();
                const auto targetLines = findLinesContaining(contentStream.str(), "// 0x100008");
                t.IsTrue(!targetLines.empty(),
                         "A must register its own mid-body target 0x100008 once it ingests B's manifest");
            }
            {
                PS2Recompiler recompilerB(configBPath.string());
                t.IsTrue(recompilerB.initialize(), "B: initialize() should succeed for phase 2");
                t.IsTrue(recompilerB.recompile(false), "B: phase 2 recompile(false) should succeed");
                recompilerB.generateOutput();

                std::ifstream registerFile(outB / "register_functions.cpp");
                t.IsTrue(static_cast<bool>(registerFile), "B's register_functions.cpp should be written");
                std::ostringstream contentStream;
                contentStream << registerFile.rdbuf();
                const auto targetLines = findLinesContaining(contentStream.str(), "// 0x200008");
                t.IsTrue(!targetLines.empty(),
                         "B must register its own mid-body target 0x200008 once it ingests A's manifest");
            }

            std::error_code removeError;
            std::filesystem::remove_all(workDir, removeError);
        });

        // Tier 1 end-to-end regression test for data-embedded thread entry discovery: drives
        // a REAL PS2Recompiler instance over a real ELF whose .data section contains a
        // ThreadParam struct read via the production m_elfParser->readWord/isValidAddress
        // path, proving both that the analyzer reads the pointer from real ELF data AND
        // that the resulting entry gets registered into its owning unit through the full
        // recompile()/generateOutput() pipeline.
        tc.Run("full pipeline: data-embedded thread entry (CreateThread ThreadParam) registers into its owning unit", [](TestCase &t) {
            const auto uniqueSuffix = std::to_string(
                static_cast<unsigned long long>(std::chrono::steady_clock::now().time_since_epoch().count()));
            const std::filesystem::path workDir =
                std::filesystem::temp_directory_path() / ("ps2recomp-thread-entry-" + uniqueSuffix);
            std::error_code mkdirError;
            std::filesystem::create_directories(workDir, mkdirError);
            t.IsTrue(!mkdirError, "work directory should be created");

            const std::filesystem::path elfPath = workDir / "fixture.elf";
            const bool wroteElf = writeThreadEntryDataElf(elfPath);
            t.IsTrue(wroteElf, "thread-entry fixture ELF should be generated");
            if (!wroteElf)
            {
                std::error_code cleanupError;
                std::filesystem::remove_all(workDir, cleanupError);
                return;
            }

            const std::filesystem::path outDir = workDir / "out";
            const std::filesystem::path configPath = workDir / "config.toml";
            {
                std::ofstream cfg(configPath);
                t.IsTrue(static_cast<bool>(cfg), "config should be writable");
                cfg << "[general]\n";
                // generic_string(): see the manifest test above - native Windows
                // separators are invalid escapes inside a TOML basic string.
                cfg << "input = \"" << elfPath.generic_string() << "\"\n";
                cfg << "output = \"" << outDir.generic_string() << "\"\n";
            }

            PS2Recompiler recompiler(configPath.string());
            t.IsTrue(recompiler.initialize(), "initialize() should succeed");
            t.IsTrue(recompiler.recompile(), "recompile() should succeed");
            recompiler.generateOutput();

            const std::filesystem::path registerPath = outDir / "register_functions.cpp";
            std::ifstream registerFile(registerPath);
            t.IsTrue(static_cast<bool>(registerFile), "register_functions.cpp should be written");
            std::ostringstream contentStream;
            contentStream << registerFile.rdbuf();
            const std::string content = contentStream.str();

            const auto entryLines = findLinesContaining(content, "// 0x100030");
            t.IsTrue(!entryLines.empty(),
                     "data-embedded thread entry 0x100030 (read from the .data ThreadParam struct via "
                     "the real ELF parser) must be registered");
            if (!entryLines.empty())
            {
                const std::string owner = extractOwnerNameFromRegistrationLine(entryLines.front());
                t.IsTrue(owner.find("worker_container") != std::string::npos,
                         "0x100030 must be mapped to the worker_container owner that actually contains it");
            }

            std::error_code removeError;
            std::filesystem::remove_all(workDir, removeError);
        });

        tc.Run("collect external call targets: excludes jal into a local recompiled function, includes jal outside all local functions", [](TestCase &t) {
            std::vector<Section> sections = {
                {".text", 0x1000u, 0x100000u - 0x1000u, 0u, true, false, false, true, nullptr}
            };

            std::vector<Function> functions = {
                makeFunction("functionA", 0x1000u, 0x1020u),
                makeFunction("functionB", 0x2000u, 0x2020u)
            };

            std::unordered_map<uint32_t, std::vector<Instruction>> decodedFunctions;
            decodedFunctions[0x1000u] = {
                makeAbsJump(0x1000u, 0x2010u, OPCODE_JAL),
                makeNopLike(0x1004u),
                makeAbsJump(0x1008u, 0x50000u, OPCODE_JAL),
                makeNopLike(0x100Cu)
            };

            const std::vector<uint32_t> targets =
                PS2Recompiler::CollectExternalCallTargets(decodedFunctions, functions, sections);

            t.Equals(targets.size(), static_cast<size_t>(1),
                     "only the target outside every local function range should be collected");
            if (!targets.empty())
            {
                t.Equals(targets[0], 0x50000u,
                         "jal into functionB's range should be excluded; jal to 0x50000 should be included");
            }
        });

        tc.Run("collect external call targets: foreign/overlay target outside all sections IS collected", [](TestCase &t) {
            std::vector<Section> sections = {
                {".text", 0x1000u, 0x100000u - 0x1000u, 0u, true, false, false, true, nullptr}
            };

            std::vector<Function> functions = {
                makeFunction("functionA", 0x1000u, 0x1020u)
            };

            std::unordered_map<uint32_t, std::vector<Instruction>> decodedFunctions;
            decodedFunctions[0x1000u] = {
                makeAbsJump(0x1000u, 0x300000u, OPCODE_JAL),
                makeNopLike(0x1004u)
            };

            const std::vector<uint32_t> targets =
                PS2Recompiler::CollectExternalCallTargets(decodedFunctions, functions, sections);

            t.Equals(targets.size(), static_cast<size_t>(1),
                     "a jal target past the end of every section of this unit is a candidate cross-unit/overlay "
                     "target and must be collected - the emitting unit cannot know the callee unit's layout");
            if (!targets.empty())
            {
                t.Equals(targets[0], 0x300000u,
                         "the collected target should be the foreign/overlay address itself");
            }
        });

        tc.Run("collect external call targets: target inside the caller's own data section is excluded", [](TestCase &t) {
            std::vector<Section> sections = {
                {".text", 0x1000u, 0x100000u - 0x1000u, 0u, true, false, false, true, nullptr},
                {".data", 0x200000u, 0x10000u, 0u, false, true, false, false, nullptr}
            };

            std::vector<Function> functions = {
                makeFunction("functionA", 0x1000u, 0x1020u)
            };

            std::unordered_map<uint32_t, std::vector<Instruction>> decodedFunctions;
            decodedFunctions[0x1000u] = {
                makeAbsJump(0x1000u, 0x200008u, OPCODE_JAL),
                makeNopLike(0x1004u)
            };

            const std::vector<uint32_t> targets =
                PS2Recompiler::CollectExternalCallTargets(decodedFunctions, functions, sections);

            t.Equals(targets.size(), static_cast<size_t>(0),
                     "a jal landing inside the caller's own data section is garbage and must be dropped");
        });

        tc.Run("collect external call targets: duplicates collapse and results are sorted", [](TestCase &t) {
            std::vector<Section> sections = {
                {".text", 0x1000u, 0x100000u - 0x1000u, 0u, true, false, false, true, nullptr}
            };

            std::vector<Function> functions = {
                makeFunction("functionA", 0x1000u, 0x1020u)
            };

            std::unordered_map<uint32_t, std::vector<Instruction>> decodedFunctions;
            decodedFunctions[0x1000u] = {
                makeAbsJump(0x1000u, 0x50000u, OPCODE_JAL),
                makeNopLike(0x1004u),
                makeAbsJump(0x1008u, 0x50000u, OPCODE_JAL),
                makeNopLike(0x100Cu),
                makeAbsJump(0x1010u, 0x50000u, OPCODE_J),
                makeNopLike(0x1014u),
                makeAbsJump(0x1018u, 0x60000u, OPCODE_JAL),
                makeNopLike(0x101Cu),
                makeAbsJump(0x1020u, 0x40000u, OPCODE_JAL),
                makeNopLike(0x1024u)
            };

            const std::vector<uint32_t> targets =
                PS2Recompiler::CollectExternalCallTargets(decodedFunctions, functions, sections);

            t.Equals(targets.size(), static_cast<size_t>(3),
                     "duplicate targets should collapse to unique entries");
            if (targets.size() == 3)
            {
                t.Equals(targets[0], 0x40000u, "targets should be sorted ascending");
                t.Equals(targets[1], 0x50000u, "second target should be the mid-range address");
                t.Equals(targets[2], 0x60000u, "third target should be the highest address");
            }
        });

        tc.Run("collect external call targets: targets inside skipped or stub local functions are still emitted", [](TestCase &t) {
            std::vector<Section> sections = {
                {".text", 0x1000u, 0x100000u - 0x1000u, 0u, true, false, false, true, nullptr}
            };

            Function skippedFunction = makeFunction("functionB", 0x2000u, 0x2020u);
            skippedFunction.isSkipped = true;

            std::vector<Function> functions = {
                makeFunction("functionA", 0x1000u, 0x1020u),
                skippedFunction
            };

            std::unordered_map<uint32_t, std::vector<Instruction>> decodedFunctions;
            decodedFunctions[0x1000u] = {
                makeAbsJump(0x1000u, 0x2010u, OPCODE_JAL),
                makeNopLike(0x1004u)
            };

            const std::vector<uint32_t> targets =
                PS2Recompiler::CollectExternalCallTargets(decodedFunctions, functions, sections);

            t.Equals(targets.size(), static_cast<size_t>(1),
                     "a jal landing inside a skipped local function should still be collected");
            if (!targets.empty())
            {
                t.Equals(targets[0], 0x2010u,
                         "only recompiled, non-stub, non-skipped functions should suppress emission");
            }
        });

        tc.Run("collect external call targets: conditional branches are not collected", [](TestCase &t) {
            std::vector<Section> sections = {
                {".text", 0x1000u, 0x100000u - 0x1000u, 0u, true, false, false, true, nullptr}
            };

            std::vector<Function> functions = {
                makeFunction("functionA", 0x1000u, 0x1020u)
            };

            // Target (0x50000) is inside the code section and outside every local function
            // range, i.e. it would be collected if this were a jal/j - isolating that the
            // opcode check, not the address itself, is what excludes it.
            Instruction branch = makeAbsJump(0x1000u, 0x50000u, OPCODE_BEQ);
            branch.isBranch = true;

            std::unordered_map<uint32_t, std::vector<Instruction>> decodedFunctions;
            decodedFunctions[0x1000u] = {
                branch,
                makeNopLike(0x1004u)
            };

            const std::vector<uint32_t> targets =
                PS2Recompiler::CollectExternalCallTargets(decodedFunctions, functions, sections);

            t.Equals(targets.size(), static_cast<size_t>(0),
                     "conditional branches are not jal/j and should not be collected even though the address would otherwise qualify");
        });

        tc.Run("data-embedded thread entries: jal to CreateThread wrapper resolves delay-slot $a0", [](TestCase &t) {
            constexpr uint32_t wrapperStart = 0x00100200u;
            constexpr uint32_t callerStart = 0x00100000u;
            constexpr uint32_t jalAddr = callerStart + 8u;

            std::unordered_map<uint32_t, std::vector<Instruction>> decoded = {
                {wrapperStart, {
                    makeAddiu(wrapperStart, 3, 0, 0x20),
                    makeSyscall(wrapperStart + 4u),
                    makeJrRa(wrapperStart + 8u),
                }},
                {callerStart, {
                    makeNopLike(callerStart),
                    makeLui(callerStart + 4u, 4, 0x0030),
                    makeAbsJump(jalAddr, wrapperStart, OPCODE_JAL),
                    makeAddiu(jalAddr + 4u, 4, 4, 0x1234),
                }},
            };

            const uint32_t paramAddress = 0x00301234u;
            std::unordered_map<uint32_t, uint32_t> fakeMemory = {
                {paramAddress, 0u},
                {paramAddress + 4u, 0x00280000u},
            };
            auto isValid = [&](uint32_t addr) { return fakeMemory.count(addr) != 0u; };
            auto readWord = [&](uint32_t addr) { return fakeMemory.at(addr); };

            const std::vector<uint32_t> result =
                PS2Recompiler::DiscoverDataEmbeddedThreadEntries(decoded, isValid, readWord);

            t.Equals(result.size(), static_cast<size_t>(1), "expected exactly one discovered thread entry");
            if (result.size() == 1)
            {
                t.Equals(result[0], 0x00280000u, "thread entry pointer should be read from the ThreadParam struct");
            }
        });

        tc.Run("data-embedded thread entries: jal's decoder-populated rt field must not clobber $a0", [](TestCase &t) {
            // R5900Decoder::decodeInstruction populates rs/rt/rd unconditionally from the
            // raw instruction bits, even for J-type (j/jal) instructions where those bit
            // positions are actually part of the 26-bit jump target rather than a real
            // register field. For wrapperStart = 0x00100200, the jal's encoded target bits
            // happen to alias rt = 4 ($a0). If the constant-propagation walk treated that
            // as a real write to $a0, it would wrongly erase the $a0 the lui just set,
            // causing the delay-slot addiu to fail to resolve. This must still resolve.
            constexpr uint32_t wrapperStart = 0x00100200u;
            constexpr uint32_t callerStart = 0x00100000u;
            constexpr uint32_t jalAddr = callerStart + 8u;

            std::unordered_map<uint32_t, std::vector<Instruction>> decoded = {
                {wrapperStart, {
                    makeAddiu(wrapperStart, 3, 0, 0x20),
                    makeSyscall(wrapperStart + 4u),
                    makeJrRa(wrapperStart + 8u),
                }},
                {callerStart, {
                    makeNopLike(callerStart),
                    makeLui(callerStart + 4u, 4, 0x0030),
                    makeAbsJumpDecoded(jalAddr, wrapperStart, OPCODE_JAL),
                    makeAddiu(jalAddr + 4u, 4, 4, 0x1234),
                }},
            };

            t.Equals(static_cast<uint32_t>(RT(decoded.at(callerStart)[2].raw)), 4u,
                     "test setup sanity check: this jal's decoder-populated rt must alias $a0");

            const uint32_t paramAddress = 0x00301234u;
            std::unordered_map<uint32_t, uint32_t> fakeMemory = {
                {paramAddress, 0u},
                {paramAddress + 4u, 0x00280000u},
            };
            auto isValid = [&](uint32_t addr) { return fakeMemory.count(addr) != 0u; };
            auto readWord = [&](uint32_t addr) { return fakeMemory.at(addr); };

            const std::vector<uint32_t> result =
                PS2Recompiler::DiscoverDataEmbeddedThreadEntries(decoded, isValid, readWord);

            t.Equals(result.size(), static_cast<size_t>(1),
                     "a jal's incidental rt bits must not be treated as a register write");
            if (result.size() == 1)
            {
                t.Equals(result[0], 0x00280000u, "thread entry pointer should still be read from the ThreadParam struct");
            }
        });

        tc.Run("data-embedded thread entries: direct inline syscall resolves static $a0", [](TestCase &t) {
            constexpr uint32_t callerStart = 0x00101000u;
            std::unordered_map<uint32_t, std::vector<Instruction>> decoded = {
                {callerStart, {
                    makeLui(callerStart, 4, 0x0041),
                    makeAddiu(callerStart + 4u, 4, 4, 0x0100),
                    makeAddiu(callerStart + 8u, 3, 0, 0x20),
                    makeSyscall(callerStart + 12u),
                }},
            };

            const uint32_t paramAddress = 0x00410100u;
            std::unordered_map<uint32_t, uint32_t> fakeMemory = {
                {paramAddress, 0u},
                {paramAddress + 4u, 0x00420000u},
            };
            auto isValid = [&](uint32_t addr) { return fakeMemory.count(addr) != 0u; };
            auto readWord = [&](uint32_t addr) { return fakeMemory.at(addr); };

            const std::vector<uint32_t> result =
                PS2Recompiler::DiscoverDataEmbeddedThreadEntries(decoded, isValid, readWord);

            t.Equals(result.size(), static_cast<size_t>(1), "expected the inline syscall call site to resolve");
            if (result.size() == 1)
            {
                t.Equals(result[0], 0x00420000u, "thread entry pointer should be read via the direct syscall path");
            }
        });

        tc.Run("data-embedded thread entries: clobbered $a0 before call yields no result", [](TestCase &t) {
            constexpr uint32_t wrapperStart = 0x00100200u;
            constexpr uint32_t callerStart = 0x00100000u;
            constexpr uint32_t jalAddr = callerStart + 12u;

            std::unordered_map<uint32_t, std::vector<Instruction>> decoded = {
                {wrapperStart, {
                    makeAddiu(wrapperStart, 3, 0, 0x20),
                    makeSyscall(wrapperStart + 4u),
                    makeJrRa(wrapperStart + 8u),
                }},
                {callerStart, {
                    makeNopLike(callerStart),
                    makeLui(callerStart + 4u, 4, 0x0030),
                    makeLw(callerStart + 8u, 4, 5), // clobbers $a0 with an unresolved load
                    makeAbsJump(jalAddr, wrapperStart, OPCODE_JAL),
                    makeNopLike(jalAddr + 4u), // delay slot does not re-materialize $a0
                }},
            };

            std::unordered_map<uint32_t, uint32_t> fakeMemory = {
                {0x00301234u, 0u},
                {0x00301238u, 0x00280000u},
            };
            auto isValid = [&](uint32_t addr) { return fakeMemory.count(addr) != 0u; };
            auto readWord = [&](uint32_t addr) { return fakeMemory.at(addr); };

            const std::vector<uint32_t> result =
                PS2Recompiler::DiscoverDataEmbeddedThreadEntries(decoded, isValid, readWord);

            t.Equals(result.size(), static_cast<size_t>(0), "a clobbered $a0 with no re-materialization should not resolve");
        });

        tc.Run("data-embedded thread entries: store between materialization and call does not clobber $a0", [](TestCase &t) {
            // Maintainer's exact sequence: lui $a0,hi(P); sw $a0,0($sp); jal
            // CreateThread; addiu $a0,$a0,lo(P) (delay slot). A store reads its rt
            // operand, it does not write it - sw $a0 must not erase the $a0 the lui
            // just resolved. Strong pin for "stores don't clobber rt": mutating SW to
            // {rt} in mayWriteGprs makes this test fail.
            constexpr uint32_t wrapperStart = 0x00100200u;
            constexpr uint32_t callerStart = 0x00100000u;
            constexpr uint32_t jalAddr = callerStart + 12u;

            std::unordered_map<uint32_t, std::vector<Instruction>> decoded = {
                {wrapperStart, {
                    makeAddiu(wrapperStart, 3, 0, 0x20),
                    makeSyscall(wrapperStart + 4u),
                    makeJrRa(wrapperStart + 8u),
                }},
                {callerStart, {
                    makeNopLike(callerStart),
                    makeLui(callerStart + 4u, 4, 0x0030),
                    makeSw(callerStart + 8u, 4, 29), // sw $a0, 0($sp) - reads $a0, does not clobber it
                    makeAbsJump(jalAddr, wrapperStart, OPCODE_JAL),
                    makeAddiu(jalAddr + 4u, 4, 4, 0x1234), // delay slot: addiu $a0,$a0,lo(P)
                }},
            };

            const uint32_t paramAddress = 0x00301234u;
            std::unordered_map<uint32_t, uint32_t> fakeMemory = {
                {paramAddress, 0u},
                {paramAddress + 4u, 0x00280000u},
            };
            auto isValid = [&](uint32_t addr) { return fakeMemory.count(addr) != 0u; };
            auto readWord = [&](uint32_t addr) { return fakeMemory.at(addr); };

            const std::vector<uint32_t> result =
                PS2Recompiler::DiscoverDataEmbeddedThreadEntries(decoded, isValid, readWord);

            t.Equals(result.size(), static_cast<size_t>(1),
                     "a store of $a0 between materialization and the call must not erase the resolved $a0");
            if (result.size() == 1)
            {
                t.Equals(result[0], 0x00280000u, "thread entry pointer should still resolve through the store");
            }
        });

        tc.Run("data-embedded thread entries: unrelated jal between materialization and CreateThread is not resolved", [](TestCase &t) {
            // Negative case: lui $a0,hi(P); addiu $a0,$a0,lo(P); jal unrelated_fn;
            // nop; addiu $v1,$zero,0x20; syscall. $a0 is fully resolved before the
            // unrelated call, but the unrelated jal is a control transfer sitting
            // between the materialization and the CreateThread invocation, so it
            // cannot dominate - the basic-block restriction, not mayWriteGprs
            // (a jal writes only $ra=31, untracked), is what must drop this. Pin:
            // removing the isControlTransfer backward-scan (walking the full window
            // instead) makes this test fail.
            constexpr uint32_t callerStart = 0x00101000u;
            constexpr uint32_t unrelatedFn = 0x00109000u;

            std::unordered_map<uint32_t, std::vector<Instruction>> decoded = {
                {callerStart, {
                    makeLui(callerStart, 4, 0x0040),               // lui $a0, 0x0040
                    makeAddiu(callerStart + 4u, 4, 4, 0x0100),     // addiu $a0,$a0,0x0100 -> $a0 = P (fully resolved)
                    makeAbsJump(callerStart + 8u, unrelatedFn, OPCODE_JAL), // jal unrelated_fn
                    makeNopLike(callerStart + 12u),                // delay slot
                    makeAddiu(callerStart + 16u, 3, 0, 0x20),      // addiu $v1,$zero,0x20
                    makeSyscall(callerStart + 20u),
                }},
            };

            const uint32_t paramAddress = 0x00400100u;
            std::unordered_map<uint32_t, uint32_t> fakeMemory = {
                {paramAddress, 0u},
                {paramAddress + 4u, 0x00450000u},
            };
            auto isValid = [&](uint32_t addr) { return fakeMemory.count(addr) != 0u; };
            auto readWord = [&](uint32_t addr) { return fakeMemory.at(addr); };

            const std::vector<uint32_t> result =
                PS2Recompiler::DiscoverDataEmbeddedThreadEntries(decoded, isValid, readWord);

            t.Equals(result.size(), static_cast<size_t>(0),
                     "an unrelated call sitting between the materialization and CreateThread must not let the "
                     "stale $a0 survive - the basic-block restriction must drop it");
        });

        tc.Run("data-embedded thread entries: strong load-clobber pin", [](TestCase &t) {
            // lui $a0,hi(P); lw $a0,0($t0); jal CreateThread; addiu $a0,$a0,0 (delay
            // slot). hi(P) alone (0x00300000) IS a valid param address in fakeMemory
            // and the delay-slot addiu's immediate is 0, so if LW failed to
            // invalidate $a0 the stale lui-only value would survive unchanged and
            // still resolve - unlike the existing weak "clobbered $a0" test (whose
            // lui-only value is not a valid param either way), this assertion truly
            // depends on LW invalidating $a0. Pin: mutating LW to {} in mayWriteGprs
            // makes this test fail.
            constexpr uint32_t wrapperStart = 0x00100200u;
            constexpr uint32_t callerStart = 0x00102000u;
            constexpr uint32_t jalAddr = callerStart + 12u;

            std::unordered_map<uint32_t, std::vector<Instruction>> decoded = {
                {wrapperStart, {
                    makeAddiu(wrapperStart, 3, 0, 0x20),
                    makeSyscall(wrapperStart + 4u),
                    makeJrRa(wrapperStart + 8u),
                }},
                {callerStart, {
                    makeNopLike(callerStart),
                    makeLui(callerStart + 4u, 4, 0x0030),     // lui $a0, 0x0030 -> $a0 = 0x00300000 (a valid param on its own)
                    makeLw(callerStart + 8u, 4, 6),           // lw $a0, 0($t0) - must invalidate $a0
                    makeAbsJump(jalAddr, wrapperStart, OPCODE_JAL),
                    makeAddiu(jalAddr + 4u, 4, 4, 0x0000),    // delay slot: addiu $a0,$a0,0
                }},
            };

            const uint32_t paramAddress = 0x00300000u;
            std::unordered_map<uint32_t, uint32_t> fakeMemory = {
                {paramAddress, 0u},
                {paramAddress + 4u, 0x00280000u},
            };
            auto isValid = [&](uint32_t addr) { return fakeMemory.count(addr) != 0u; };
            auto readWord = [&](uint32_t addr) { return fakeMemory.at(addr); };

            const std::vector<uint32_t> result =
                PS2Recompiler::DiscoverDataEmbeddedThreadEntries(decoded, isValid, readWord);

            t.Equals(result.size(), static_cast<size_t>(0),
                     "the reloaded $a0 must not resolve through a load that invalidated it, even though the "
                     "pre-load lui-only value happens to be a valid param address on its own");
        });

        tc.Run("data-embedded thread entries: mtc1 reading $a0 is a documented safe-direction false negative", [](TestCase &t) {
            // The conservative COP0/COP1/COP2/MMI {rt,rd} invalidation intentionally
            // reproduces a false negative here: mtc1 $a0,$f0 only READS $a0, but the
            // helper cannot distinguish that from an MFC1-style GPR write without
            // sub-decoding fmt, so it invalidates $a0 anyway. This is the accepted,
            // safe-direction tradeoff (over-invalidation only costs a missed entry,
            // never a false survival) - documented here as a guard on that class of
            // mayWriteGprs, not one of the strong pins.
            constexpr uint32_t callerStart = 0x00103000u;

            std::unordered_map<uint32_t, std::vector<Instruction>> decoded = {
                {callerStart, {
                    makeLui(callerStart, 4, 0x0050),               // lui $a0, 0x0050
                    makeAddiu(callerStart + 4u, 4, 4, 0x0060),     // addiu $a0,$a0,0x0060 -> $a0 = P (fully resolved)
                    makeCopMove(callerStart + 8u, OPCODE_COP1, COP1_MT, 4), // mtc1 $a0, $f0 - reads $a0 only
                    makeAddiu(callerStart + 12u, 3, 0, 0x20),      // addiu $v1,$zero,0x20
                    makeSyscall(callerStart + 16u),
                }},
            };

            const uint32_t paramAddress = 0x00500060u;
            std::unordered_map<uint32_t, uint32_t> fakeMemory = {
                {paramAddress, 0u},
                {paramAddress + 4u, 0x00550000u},
            };
            auto isValid = [&](uint32_t addr) { return fakeMemory.count(addr) != 0u; };
            auto readWord = [&](uint32_t addr) { return fakeMemory.at(addr); };

            const std::vector<uint32_t> result =
                PS2Recompiler::DiscoverDataEmbeddedThreadEntries(decoded, isValid, readWord);

            t.Equals(result.size(), static_cast<size_t>(0),
                     "the conservative COP invalidation is expected to miss this entry - documented false negative, "
                     "the safe direction");
        });

        tc.Run("data-embedded thread entries: branch between materialization and inline CreateThread is a block boundary", [](TestCase &t) {
            // A real branch is itself a block terminator: even though a branch writes
            // nothing tracked, positioning one between the materialization and a
            // same-block inline CreateThread must still fail to resolve, because the
            // basic-block restriction stops the backward scan at the branch. Doubles
            // as a block-boundary check distinct from the unrelated-jal negative
            // above (which pins the mechanism via an unconditional jal).
            constexpr uint32_t callerStart = 0x00104000u;

            std::unordered_map<uint32_t, std::vector<Instruction>> decoded = {
                {callerStart, {
                    makeLui(callerStart, 4, 0x0060),                 // lui $a0, 0x0060
                    makeAddiu(callerStart + 4u, 4, 4, 0x0070),       // addiu $a0,$a0,0x0070 -> $a0 = P (fully resolved)
                    makeBeq(callerStart + 8u, 0, 0, callerStart + 0x100), // beq $zero,$zero,elsewhere
                    makeNopLike(callerStart + 12u),                  // delay slot
                    makeAddiu(callerStart + 16u, 3, 0, 0x20),        // addiu $v1,$zero,0x20
                    makeSyscall(callerStart + 20u),
                }},
            };

            const uint32_t paramAddress = 0x00600070u;
            std::unordered_map<uint32_t, uint32_t> fakeMemory = {
                {paramAddress, 0u},
                {paramAddress + 4u, 0x00650000u},
            };
            auto isValid = [&](uint32_t addr) { return fakeMemory.count(addr) != 0u; };
            auto readWord = [&](uint32_t addr) { return fakeMemory.at(addr); };

            const std::vector<uint32_t> result =
                PS2Recompiler::DiscoverDataEmbeddedThreadEntries(decoded, isValid, readWord);

            t.Equals(result.size(), static_cast<size_t>(0),
                     "a branch between the materialization and the call is a block boundary, even inline "
                     "within the same function");
        });

        tc.Run("data-embedded thread entries: wrapper with wrong syscall number is not registered", [](TestCase &t) {
            constexpr uint32_t wrapperStart = 0x00100200u;
            constexpr uint32_t callerStart = 0x00100000u;
            constexpr uint32_t jalAddr = callerStart + 8u;

            std::unordered_map<uint32_t, std::vector<Instruction>> decoded = {
                {wrapperStart, {
                    makeAddiu(wrapperStart, 3, 0, 0x21), // wrong syscall number
                    makeSyscall(wrapperStart + 4u),
                    makeJrRa(wrapperStart + 8u),
                }},
                {callerStart, {
                    makeNopLike(callerStart),
                    makeLui(callerStart + 4u, 4, 0x0030),
                    makeAbsJump(jalAddr, wrapperStart, OPCODE_JAL),
                    makeAddiu(jalAddr + 4u, 4, 4, 0x1234),
                }},
            };

            std::unordered_map<uint32_t, uint32_t> fakeMemory = {
                {0x00301234u, 0u},
                {0x00301238u, 0x00280000u},
            };
            auto isValid = [&](uint32_t addr) { return fakeMemory.count(addr) != 0u; };
            auto readWord = [&](uint32_t addr) { return fakeMemory.at(addr); };

            const std::vector<uint32_t> result =
                PS2Recompiler::DiscoverDataEmbeddedThreadEntries(decoded, isValid, readWord);

            t.Equals(result.size(), static_cast<size_t>(0), "a wrapper using a non-CreateThread syscall number should be ignored");
        });

        tc.Run("data-embedded thread entries: wrapper scan resets on a later $v1 clobber", [](TestCase &t) {
            // Negative case (maintainer's exact sequence): addiu $v1,$zero,0x20;
            // addiu $v1,$zero,0x21; syscall. The second addiu overwrites $v1 with
            // 0x21 before the syscall runs, so this is not a CreateThread wrapper -
            // the actual syscall number is 0x21. Pin: removing the
            // sawAddiuV1Syscall = false reset makes this test fail (it would then be
            // misclassified as a wrapper and the jal below would produce a result).
            constexpr uint32_t wrapperStart = 0x00100200u;
            constexpr uint32_t callerStart = 0x00100000u;
            constexpr uint32_t jalAddr = callerStart + 8u;

            std::unordered_map<uint32_t, std::vector<Instruction>> decoded = {
                {wrapperStart, {
                    makeAddiu(wrapperStart, 3, 0, 0x20),
                    makeAddiu(wrapperStart + 4u, 3, 0, 0x21), // clobbers $v1 with a different immediate
                    makeSyscall(wrapperStart + 8u),
                    makeJrRa(wrapperStart + 12u),
                }},
                {callerStart, {
                    makeNopLike(callerStart),
                    makeLui(callerStart + 4u, 4, 0x0030),
                    makeAbsJump(jalAddr, wrapperStart, OPCODE_JAL),
                    makeAddiu(jalAddr + 4u, 4, 4, 0x1234),
                }},
            };

            std::unordered_map<uint32_t, uint32_t> fakeMemory = {
                {0x00301234u, 0u},
                {0x00301238u, 0x00280000u},
            };
            auto isValid = [&](uint32_t addr) { return fakeMemory.count(addr) != 0u; };
            auto readWord = [&](uint32_t addr) { return fakeMemory.at(addr); };

            const std::vector<uint32_t> result =
                PS2Recompiler::DiscoverDataEmbeddedThreadEntries(decoded, isValid, readWord);

            t.Equals(result.size(), static_cast<size_t>(0),
                     "a $v1 clobber between the 0x20 materialization and the syscall must not be classified as "
                     "a CreateThread wrapper");
        });

        tc.Run("data-embedded thread entries: wrapper scan reset is conditional on a $v1 write (positive half-guard)", [](TestCase &t) {
            // Positive half-guard: addiu $v1,$zero,0x20; addiu $a1,$zero,1; syscall.
            // The intervening instruction writes $a1, not $v1, so the 0x20
            // materialization is still live and this IS a legitimate wrapper. Catches
            // a mutation that makes the reset unconditional (fires on every
            // non-materialization instruction, not just a $v1 write), which would
            // drop this wrapper too.
            constexpr uint32_t wrapperStart = 0x00100200u;
            constexpr uint32_t callerStart = 0x00100000u;
            constexpr uint32_t jalAddr = callerStart + 8u;

            std::unordered_map<uint32_t, std::vector<Instruction>> decoded = {
                {wrapperStart, {
                    makeAddiu(wrapperStart, 3, 0, 0x20),
                    makeAddiu(wrapperStart + 4u, 5, 0, 1), // writes $a1, not $v1 - must not reset
                    makeSyscall(wrapperStart + 8u),
                    makeJrRa(wrapperStart + 12u),
                }},
                {callerStart, {
                    makeNopLike(callerStart),
                    makeLui(callerStart + 4u, 4, 0x0030),
                    makeAbsJump(jalAddr, wrapperStart, OPCODE_JAL),
                    makeAddiu(jalAddr + 4u, 4, 4, 0x1234),
                }},
            };

            const uint32_t paramAddress = 0x00301234u;
            std::unordered_map<uint32_t, uint32_t> fakeMemory = {
                {paramAddress, 0u},
                {paramAddress + 4u, 0x00280000u},
            };
            auto isValid = [&](uint32_t addr) { return fakeMemory.count(addr) != 0u; };
            auto readWord = [&](uint32_t addr) { return fakeMemory.at(addr); };

            const std::vector<uint32_t> result =
                PS2Recompiler::DiscoverDataEmbeddedThreadEntries(decoded, isValid, readWord);

            t.Equals(result.size(), static_cast<size_t>(1),
                     "an intervening write to a register other than $v1 must not reset the wrapper scan");
            if (result.size() == 1)
            {
                t.Equals(result[0], 0x00280000u, "thread entry pointer should resolve through the legitimate wrapper");
            }
        });

        tc.Run("data-embedded thread entries: zero entry pointer is filtered out", [](TestCase &t) {
            constexpr uint32_t wrapperStart = 0x00100200u;
            constexpr uint32_t callerStart = 0x00100000u;
            constexpr uint32_t jalAddr = callerStart + 8u;

            std::unordered_map<uint32_t, std::vector<Instruction>> decoded = {
                {wrapperStart, {
                    makeAddiu(wrapperStart, 3, 0, 0x20),
                    makeSyscall(wrapperStart + 4u),
                    makeJrRa(wrapperStart + 8u),
                }},
                {callerStart, {
                    makeNopLike(callerStart),
                    makeLui(callerStart + 4u, 4, 0x0030),
                    makeAbsJump(jalAddr, wrapperStart, OPCODE_JAL),
                    makeAddiu(jalAddr + 4u, 4, 4, 0x1234),
                }},
            };

            std::unordered_map<uint32_t, uint32_t> fakeMemory = {
                {0x00301234u, 0u},
                {0x00301238u, 0u}, // entry pointer is zero, should be excluded
            };
            auto isValid = [&](uint32_t addr) { return fakeMemory.count(addr) != 0u; };
            auto readWord = [&](uint32_t addr) { return fakeMemory.at(addr); };

            const std::vector<uint32_t> result =
                PS2Recompiler::DiscoverDataEmbeddedThreadEntries(decoded, isValid, readWord);

            t.Equals(result.size(), static_cast<size_t>(0), "a zero entry pointer should never be reported as a thread entry");
        });

        tc.Run("data-embedded thread entries: multiple call sites to the same struct dedupe", [](TestCase &t) {
            constexpr uint32_t wrapperStart = 0x00100200u;
            constexpr uint32_t callerAStart = 0x00100000u;
            constexpr uint32_t callerBStart = 0x00100100u;
            constexpr uint32_t jalAddrA = callerAStart + 8u;
            constexpr uint32_t jalAddrB = callerBStart + 8u;

            std::unordered_map<uint32_t, std::vector<Instruction>> decoded = {
                {wrapperStart, {
                    makeAddiu(wrapperStart, 3, 0, 0x20),
                    makeSyscall(wrapperStart + 4u),
                    makeJrRa(wrapperStart + 8u),
                }},
                {callerAStart, {
                    makeNopLike(callerAStart),
                    makeLui(callerAStart + 4u, 4, 0x0030),
                    makeAbsJump(jalAddrA, wrapperStart, OPCODE_JAL),
                    makeAddiu(jalAddrA + 4u, 4, 4, 0x1234),
                }},
                {callerBStart, {
                    makeNopLike(callerBStart),
                    makeLui(callerBStart + 4u, 4, 0x0030),
                    makeAbsJump(jalAddrB, wrapperStart, OPCODE_JAL),
                    makeAddiu(jalAddrB + 4u, 4, 4, 0x1234),
                }},
            };

            std::unordered_map<uint32_t, uint32_t> fakeMemory = {
                {0x00301234u, 0u},
                {0x00301238u, 0x00280000u},
            };
            auto isValid = [&](uint32_t addr) { return fakeMemory.count(addr) != 0u; };
            auto readWord = [&](uint32_t addr) { return fakeMemory.at(addr); };

            const std::vector<uint32_t> result =
                PS2Recompiler::DiscoverDataEmbeddedThreadEntries(decoded, isValid, readWord);

            t.Equals(result.size(), static_cast<size_t>(1),
                     "two call sites referencing the same ThreadParam struct should dedupe to one entry");
            if (result.size() == 1)
            {
                t.Equals(result[0], 0x00280000u, "the deduped entry should still be the correct thread entry pointer");
            }
        });

        tc.Run("data-embedded thread entries: addiu LO with sign bit set is sign-extended, not OR'd", [](TestCase &t) {
            constexpr uint32_t wrapperStart = 0x00100200u;
            constexpr uint32_t callerStart = 0x00100000u;
            constexpr uint32_t jalAddr = callerStart + 12u;

            std::unordered_map<uint32_t, std::vector<Instruction>> decoded = {
                {wrapperStart, {
                    makeAddiu(wrapperStart, 3, 0, 0x20),
                    makeSyscall(wrapperStart + 4u),
                    makeJrRa(wrapperStart + 8u),
                }},
                {callerStart, {
                    makeNopLike(callerStart),
                    makeLui(callerStart + 4u, 4, 0x0031),
                    // sign-extended LO: address = 0x00310000 + sign_ext16(0x8000) = 0x00308000
                    makeAddiu(callerStart + 8u, 4, 4, 0x8000),
                    makeAbsJump(jalAddr, wrapperStart, OPCODE_JAL),
                    makeNopLike(jalAddr + 4u),
                }},
            };

            const uint32_t paramAddress = 0x00308000u;
            std::unordered_map<uint32_t, uint32_t> fakeMemory = {
                {paramAddress, 0u},
                {paramAddress + 4u, 0x00290000u},
            };
            auto isValid = [&](uint32_t addr) { return fakeMemory.count(addr) != 0u; };
            auto readWord = [&](uint32_t addr) { return fakeMemory.at(addr); };

            const std::vector<uint32_t> result =
                PS2Recompiler::DiscoverDataEmbeddedThreadEntries(decoded, isValid, readWord);

            t.Equals(result.size(), static_cast<size_t>(1),
                     "the sign-extended address should resolve to the correct ThreadParam struct");
            if (result.size() == 1)
            {
                t.Equals(result[0], 0x00290000u, "entry pointer should come from address 0x00308000, not the OR'd 0x00318000");
            }
        });
    });
}
