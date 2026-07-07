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

        tc.Run("collect external call targets: excludes targets outside any executable section", [](TestCase &t) {
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

            t.Equals(targets.size(), static_cast<size_t>(0),
                     "a jal target past the end of every code section should not be collected");
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
