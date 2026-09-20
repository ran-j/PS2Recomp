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
#include <iterator>
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

static bool writeMinimalMipsElfWithVuMicroprogramSection(const std::filesystem::path &elfPath)
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
    const std::array<uint32_t, 2> textWords{0x03E00008u, 0x00000000u};
    text->set_data(reinterpret_cast<const char *>(textWords.data()), sizeof(textWords));

    ELFIO::section *vuText = writer.sections.add(".vutext");
    vuText->set_type(ELFIO::SHT_PROGBITS);
    vuText->set_flags(ELFIO::SHF_ALLOC | ELFIO::SHF_EXECINSTR);
    vuText->set_addr_align(16);
    vuText->set_address(0x00250000u);
    const std::array<uint32_t, 4> vuWords{0x01EC48BDu, 0u, 0x01FA717Du, 0u};
    vuText->set_data(reinterpret_cast<const char *>(vuWords.data()), sizeof(vuWords));

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
    symbols.add_symbol(strings, "ee_entry", text->get_address(), text->get_size(),
                       ELFIO::STB_GLOBAL, ELFIO::STT_FUNC, 0, text->get_index());
    symbols.add_symbol(strings, "vu_program", vuText->get_address(), vuText->get_size(),
                       ELFIO::STB_GLOBAL, ELFIO::STT_FUNC, 0, vuText->get_index());

    ELFIO::segment *textSegment = writer.segments.add();
    textSegment->set_type(ELFIO::PT_LOAD);
    textSegment->set_flags(ELFIO::PF_R | ELFIO::PF_X);
    textSegment->set_align(0x1000);
    textSegment->add_section_index(text->get_index(), text->get_addr_align());

    ELFIO::segment *vuSegment = writer.segments.add();
    vuSegment->set_type(ELFIO::PT_LOAD);
    vuSegment->set_flags(ELFIO::PF_R | ELFIO::PF_X);
    vuSegment->set_align(0x1000);
    vuSegment->add_section_index(vuText->get_index(), vuText->get_addr_align());

    return writer.save(elfPath.string());
}

static bool writeMinimalMipsElfWithUnmappedEntryHint(const std::filesystem::path &elfPath)
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
    const std::array<uint32_t, 10> textWords = {
        0x03E00008u, 0x00000000u, // known function at 0x00100000
        0x00000000u, 0x00000000u,
        0x03E00008u, 0x00000000u, // omitted entry at 0x00100010
        0x00000000u, 0x00000000u,
        0x03E00008u, 0x00000000u, // next known function at 0x00100020
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
    symbols.add_symbol(strings, "", 0, 0,
                       ELFIO::STB_LOCAL, ELFIO::STT_NOTYPE, 0, ELFIO::SHN_UNDEF);
    symbols.add_symbol(strings, "known_before", 0x00100000u, 8u,
                       ELFIO::STB_GLOBAL, ELFIO::STT_FUNC, 0, text->get_index());
    symbols.add_symbol(strings, "stubbed_owner", 0x00100008u, 0x18u,
                       ELFIO::STB_GLOBAL, ELFIO::STT_FUNC, 0, text->get_index());
    symbols.add_symbol(strings, "known_after", 0x00100020u, 8u,
                       ELFIO::STB_GLOBAL, ELFIO::STT_FUNC, 0, text->get_index());

    ELFIO::segment *textSegment = writer.segments.add();
    textSegment->set_type(ELFIO::PT_LOAD);
    textSegment->set_flags(ELFIO::PF_R | ELFIO::PF_X);
    textSegment->set_align(0x1000);
    textSegment->add_section_index(text->get_index(), text->get_addr_align());

    return writer.save(elfPath.string());
}

static bool writeMinimalMipsElfWithAddressTakenCallbacks(const std::filesystem::path &elfPath,
                                                         bool includePartialDwarf = false)
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

    std::array<uint32_t, 384> textWords{};
    textWords[0] = 0x3C040010u;  // lui a0,0x10
    textWords[1] = 0xAC800000u;  // sw zero,0(a0)
    textWords[2] = 0x0C040008u;  // jal 0x00100020 (callback registrar)
    textWords[3] = 0x24840040u;  // addiu a0,a0,0x40 (delay slot)
    textWords[4] = 0x03E00008u;  // jr ra
    textWords[5] = 0x00000000u;  // nop
    textWords[6] = 0x3C080010u;  // lui t0,0x10
    textWords[7] = 0x25080300u;  // addiu t0,t0,0x300 (code label, not a callback argument)

    textWords[8] = 0x03E00008u;  // registrar at 0x00100020
    textWords[9] = 0x00000000u;

    textWords[16] = 0x27BDFFF0u; // callback at 0x00100040: addiu sp,sp,-0x10
    textWords[17] = 0xFFBF0000u; // sd ra,0(sp)
    textWords[18] = 0xDFBF0000u; // ld ra,0(sp)
    textWords[19] = 0x03E00008u; // jr ra
    textWords[20] = 0x27BD0010u; // addiu sp,sp,0x10

    textWords[24] = 0x08040008u; // table leaf thunk at 0x00100060: j 0x00100020
    textWords[25] = 0x00000000u; // nop (delay slot)
    textWords[26] = 0x03E00008u; // table leaf at 0x00100068
    textWords[27] = 0x00000000u;
    textWords[28] = 0x03E00008u; // adjacent leaf thunk at 0x00100070
    textWords[29] = 0x00000000u;

    // Address-taken initializer at 0x00100080 with a long constant-setup
    // preamble before its stack frame, matching retail constructor tables.
    textWords[32] = 0x3C020010u; // lui v0,0x10
    textWords[33] = 0x3C030010u; // lui v1,0x10
    textWords[34] = 0x3C050010u; // lui a1,0x10
    textWords[35] = 0x3C060010u; // lui a2,0x10
    textWords[36] = 0x3C070010u; // lui a3,0x10
    textWords[37] = 0x3C080010u; // lui t0,0x10
    textWords[38] = 0x3C090010u; // lui t1,0x10
    textWords[39] = 0x3C0A0010u; // lui t2,0x10
    textWords[40] = 0x3C0B0010u; // lui t3,0x10
    textWords[41] = 0x27BDFFF0u; // addiu sp,sp,-0x10
    textWords[42] = 0x03E00008u; // jr ra
    textWords[43] = 0x27BD0010u; // addiu sp,sp,0x10 (delay slot)

    // A callback address completed three instructions before the registrar call.
    // Its leaf body is deliberately longer than a small thunk and begins after a
    // preceding function's return, matching stripped retail ELF callback ranges.
    textWords[44] = 0x3C060010u; // lui a2,0x10
    textWords[45] = 0x7FB00010u; // sq s0,0x10(sp)
    textWords[46] = 0xFFBF0000u; // sd ra,0(sp)
    textWords[47] = 0x24C600E0u; // addiu a2,a2,0xE0 (callback at 0x001000E0)
    textWords[48] = 0x24040008u; // addiu a0,zero,8
    textWords[49] = 0x2405040Fu; // addiu a1,zero,0x40F
    textWords[50] = 0x0C040008u; // jal 0x00100020 (callback registrar)
    textWords[51] = 0x00000000u; // nop (delay slot)

    textWords[56] = 0x3C020010u; // long leaf callback at 0x001000E0
    textWords[57] = 0x8C420200u;
    textWords[58] = 0x3C030020u;
    textWords[59] = 0x24630100u;
    textWords[60] = 0x3C068000u;
    textWords[61] = 0x24420001u;
    textWords[62] = 0x00A31821u;
    textWords[63] = 0x3C010010u;
    textWords[64] = 0xAC420200u;
    textWords[65] = 0xAC660004u;
    textWords[66] = 0x0080102Du;
    textWords[67] = 0x3C010010u;
    textWords[68] = 0xAC450204u;
    textWords[69] = 0x03E00008u; // jr ra, beyond the old eight-word leaf window
    textWords[70] = 0xAC600000u; // sw zero,0(v1) (delay slot)

    // A long leaf method referenced only by a clustered descriptor table. Its
    // return is deliberately beyond the materialized-callback probe distance.
    textWords[72] = 0x8C850014u; // lw a1,0x14(a0), method at 0x00100120
    for (size_t index = 73; index < 121; ++index)
    {
        textWords[index] = 0x24420001u; // addiu v0,v0,1
    }
    textWords[121] = 0x03E00008u; // jr ra at method instruction 49
    textWords[122] = 0x00000000u; // nop (delay slot)

    // Some retail callback registrars keep an address in a saved register while
    // assembling the remaining arguments, then copy it into a2 immediately before
    // the JAL. The call is deliberately well beyond any small lookahead window.
    textWords[128] = 0x3C140010u; // lui s4,0x10
    textWords[129] = 0x26940280u; // addiu s4,s4,0x280 (callback at 0x00100280)
    textWords[130] = 0x7FB00060u; // sq s0,0x60(sp)
    textWords[131] = 0x7FB10050u; // sq s1,0x50(sp)
    textWords[132] = 0x24070001u; // addiu a3,zero,1
    textWords[133] = 0x7FB20040u; // sq s2,0x40(sp)
    textWords[134] = 0x0000202Du; // daddu a0,zero,zero
    textWords[135] = 0x7FB30030u; // sq s3,0x30(sp)
    textWords[136] = 0xFFBF0000u; // sd ra,0(sp)
    textWords[137] = 0x2405011Fu; // addiu a1,zero,0x11F
    textWords[138] = 0x0C040008u; // setup call; s4 must preserve the incomplete address
    textWords[139] = 0x00000000u; // nop (delay slot)
    textWords[144] = 0x0280302Du; // daddu a2,s4,zero
    textWords[146] = 0x0C040008u; // jal 0x00100020 (callback registrar)
    textWords[147] = 0x00000000u; // nop (delay slot)

    // A retail-style conditional initializes a callback register in its delay
    // slot, then completes the address only in the taken successor block. A
    // linear lookahead cannot connect these two halves; CFG traversal must.
    textWords[148] = 0x04410004u; // bgez v0,0x00100264
    textWords[149] = 0x3C060010u; // lui a2,0x10 (delay slot)
    textWords[150] = 0x10000008u; // b 0x0010027C (not-taken path)
    textWords[151] = 0x00000000u; // nop (delay slot)
    textWords[153] = 0x24C602C0u; // addiu a2,a2,0x2C0
    textWords[154] = 0x0C040008u; // jal 0x00100020 (callback registrar)
    textWords[155] = 0x24040008u; // addiu a0,zero,8 (delay slot)

    textWords[160] = 0x3C030010u; // callback at 0x00100280
    textWords[161] = 0x8C630200u;
    textWords[162] = 0x24630001u;
    textWords[163] = 0xAC630200u;
    textWords[164] = 0x03E00008u; // jr ra
    textWords[165] = 0x0080102Du; // daddu v0,a0,zero (delay slot)

    textWords[176] = 0x8F830000u; // callback at 0x001002C0: lw v1,0(gp)
    textWords[177] = 0x0080102Du; // daddu v0,a0,zero
    textWords[178] = 0x2405FFFFu; // addiu a1,zero,-1
    textWords[179] = 0x00832021u; // addu a0,a0,v1
    textWords[180] = 0x03E00008u; // jr ra
    textWords[181] = 0xAC850000u; // sw a1,0(a0) (delay slot)

    // Retail class constructors often build their method tables in writable
    // memory instead of shipping literal function pointers in .rodata. The
    // materialized code address is never passed to a registrar; storing it in
    // the descriptor is the only address-taken evidence.
    textWords[184] = 0x3C040010u; // lui a0,0x10
    textWords[185] = 0x24840340u; // addiu a0,a0,0x340 (method at 0x00100340)
    textWords[186] = 0xAE44001Cu; // sw a0,0x1c(s2)
    textWords[187] = 0x0000202Du; // daddu a0,zero,zero (clobber)

    textWords[208] = 0x3C020020u; // stored leaf method at 0x00100340
    textWords[209] = 0x03E00008u; // jr ra
    textWords[210] = 0x24420100u; // addiu v0,v0,0x100 (delay slot)

    // Long leaf in an alternating (function pointer, numeric id) table. This
    // is a common stripped retail dispatch-table layout and provides strong
    // address evidence through the adjacent ordinary function pointer.
    textWords[224] = 0x3C010020u; // long leaf at 0x00100380
    for (size_t index = 225; index < 235; ++index)
    {
        textWords[index] = 0x24420001u;
    }
    textWords[235] = 0x03E00008u;
    textWords[236] = 0x00000000u;

    // A stripped function map may merge the middle member of a run of trivial
    // leaf accessors into its predecessor. Only the first accessor is reached by
    // a direct call; the second still needs its own callable entry.
    textWords[188] = 0x0C0400F0u; // jal 0x001003C0
    textWords[189] = 0x00000000u; // nop (delay slot)
    textWords[192] = 0x03E00008u; // isolated pointer target at 0x00100300
    textWords[193] = 0x00000000u; // nop (delay slot)
    textWords[240] = 0x03E00008u; // known leaf at 0x001003C0: jr ra
    textWords[241] = 0x0080102Du; // daddu v0,a0,zero (delay slot)
    textWords[242] = 0x03E00008u; // merged leaf at 0x001003C8: jr ra
    textWords[243] = 0x0080102Du; // daddu v0,a0,zero (delay slot)

    // Some retail registrars take more than four register arguments. The fifth
    // callback is passed in physical t0, followed by unrelated argument setup
    // before the call. It must remain distinguishable from the dead t0 code
    // materialization at 0x00100018 above.
    textWords[196] = 0x3C080010u; // lui t0,0x10
    textWords[197] = 0x250803E0u; // addiu t0,t0,0x3E0 (callback at 0x001003E0)
    textWords[198] = 0x24040014u; // addiu a0,zero,0x14
    textWords[199] = 0x0C040008u; // jal 0x00100020 (callback registrar)
    textWords[200] = 0x24050A0Bu; // addiu a1,zero,0xA0B (delay slot)

    textWords[248] = 0x03E00008u; // extended-argument leaf at 0x001003E0: jr ra
    textWords[249] = 0x0080102Du; // daddu v0,a0,zero (delay slot)

    // The fifth register argument can also reference a substantial leaf body.
    // Its return deliberately lies beyond the old fixed 64-instruction scan
    // window, so discovery must follow the candidate's reachable control flow.
    textWords[201] = 0x3C080010u; // lui t0,0x10
    textWords[202] = 0x25080400u; // addiu t0,t0,0x400 (callback at 0x00100400)
    textWords[203] = 0x24040030u; // addiu a0,zero,0x30
    textWords[204] = 0x0C040008u; // jal 0x00100020 (callback registrar)
    textWords[205] = 0x24050A06u; // addiu a1,zero,0xA06 (delay slot)

    textWords[256] = 0x3C080048u; // long extended-argument leaf at 0x00100400
    for (size_t index = 257; index < 336; ++index)
    {
        textWords[index] = 0x24420001u; // addiu v0,v0,1
    }
    textWords[336] = 0x03E00008u; // jr ra at instruction 80
    textWords[337] = 0x00000000u; // nop (delay slot)

    // Stripped function maps can merge a normal non-leaf function into the
    // preceding function even though the boundary is unambiguous in the bytes:
    // `jr ra`, its delay slot, then a fresh stack allocation.
    textWords[338] = 0x27BDFFF0u; // post-return function at 0x00100548
    textWords[339] = 0xFFBF0000u; // sd ra,0(sp)
    textWords[340] = 0x03E00008u; // jr ra
    textWords[341] = 0x27BD0010u; // addiu sp,sp,0x10 (delay slot)

    // The target at 0x00100580 has only a singleton initialized-data pointer.
    // A known function loads that slot and invokes it through JALR, matching
    // retail callback slots that are not large enough to look like a table.
    textWords[30] = 0x0C0400D4u;  // jal 0x00100350
    textWords[31] = 0x00000000u;  // nop (delay slot)
    textWords[212] = 0x27BDFFF0u; // indirect caller at 0x00100350
    textWords[213] = 0xFFBF0000u; // sd ra,0(sp)
    textWords[214] = 0x3C100020u; // lui s0,0x20
    textWords[215] = 0x8E021000u; // lw v0,0x1000(s0) -> [0x00201000]
    textWords[216] = 0x00000000u; // nop
    textWords[217] = 0x0040F809u; // jalr v0
    textWords[218] = 0x00000000u; // nop (delay slot)
    textWords[219] = 0xDFBF0000u; // ld ra,0(sp)
    textWords[220] = 0x03E00008u; // jr ra
    textWords[221] = 0x27BD0010u; // addiu sp,sp,0x10 (delay slot)
    textWords[352] = 0x27BDFFF0u; // singleton data target at 0x00100580
    textWords[353] = 0x0320F809u; // jalr t9
    textWords[354] = 0x0200202Du; // daddu a0,s0,zero (delay slot at 0x00100588)
    textWords[355] = 0x24420001u; // addiu v0,v0,1
    textWords[356] = 0x24420001u; // addiu v0,v0,1
    textWords[357] = 0x03E00008u; // jr ra
    textWords[358] = 0x27BD0010u; // addiu sp,sp,0x10 (delay slot)

    // A second initialized-data word deliberately points at 0x00100588, the
    // delay slot of the JALR above. Its following body can look callable to a
    // reachability probe, but splitting there would truncate the real owner.
    textWords[12] = 0x0C040170u;  // jal 0x001005C0
    textWords[13] = 0x00000000u;  // nop (delay slot)
    textWords[368] = 0x27BDFFF0u; // delay-slot pointer caller at 0x001005C0
    textWords[369] = 0x3C100020u; // lui s0,0x20
    textWords[370] = 0x8E021040u; // lw v0,0x1040(s0) -> [0x00201040]
    textWords[371] = 0x0040F809u; // jalr v0
    textWords[372] = 0x00000000u; // nop (delay slot)
    textWords[373] = 0x03E00008u; // jr ra
    textWords[374] = 0x27BD0010u; // addiu sp,sp,0x10 (delay slot)

    text->set_data(reinterpret_cast<const char *>(textWords.data()),
                   static_cast<ELFIO::Elf_Word>(textWords.size() * sizeof(uint32_t)));

    ELFIO::section *rodata = writer.sections.add(".rodata");
    rodata->set_type(ELFIO::SHT_PROGBITS);
    rodata->set_flags(ELFIO::SHF_ALLOC);
    rodata->set_addr_align(4);
    rodata->set_address(0x00200000u);

    std::array<uint32_t, 32> tableWords{};
    tableWords[1] = 0x00100060u;
    tableWords[3] = 0x00100068u;
    // Retail class descriptor: name pointer, ordinary method, two reserved
    // words, then a long leaf method.
    tableWords[5] = 0x0020004Cu;
    tableWords[6] = 0x00100080u;
    tableWords[7] = 0;
    tableWords[8] = 0;
    tableWords[9] = 0x00100120u;
    tableWords[16] = 0x00100300u; // plausible entry, but not part of a pointer cluster
    tableWords[28] = 0x00100080u; // ordinary function, followed by a numeric id
    tableWords[29] = 0x0000000Du;
    tableWords[30] = 0x00100380u; // long leaf, followed by a numeric id
    tableWords[31] = 0x0000000Bu;
    rodata->set_data(reinterpret_cast<const char *>(tableWords.data()),
                     static_cast<ELFIO::Elf_Word>(tableWords.size() * sizeof(uint32_t)));

    ELFIO::section *data = writer.sections.add(".data");
    data->set_type(ELFIO::SHT_PROGBITS);
    data->set_flags(ELFIO::SHF_ALLOC | ELFIO::SHF_WRITE);
    data->set_addr_align(4);
    data->set_address(0x00201000u);
    std::array<uint32_t, 17> singletonCallbacks{};
    singletonCallbacks[0] = 0x00100580u;
    singletonCallbacks[16] = 0x00100588u;
    data->set_data(reinterpret_cast<const char *>(singletonCallbacks.data()),
                   static_cast<ELFIO::Elf_Word>(singletonCallbacks.size() * sizeof(uint32_t)));

    if (includePartialDwarf)
    {
        // A retail ELF can retain debug information for only part of its code.
        // The parser must still supplement that incomplete map with static
        // address-taken discovery instead of treating any DWARF as exhaustive.
        const std::array<uint8_t, 19> abbrevBytes = {
            0x01, 0x11, 0x01, // abbrev 1: compile_unit, has children
            0x03, 0x08,       // DW_AT_name, DW_FORM_string
            0x00, 0x00,
            0x02, 0x2E, 0x00, // abbrev 2: subprogram, no children
            0x03, 0x08,       // DW_AT_name, DW_FORM_string
            0x11, 0x01,       // DW_AT_low_pc, DW_FORM_addr
            0x12, 0x06,       // DW_AT_high_pc, DW_FORM_data4
            0x00, 0x00,       // end of attribute list
            0x00};             // end of abbreviation table

        ELFIO::section *debugAbbrev = writer.sections.add(".debug_abbrev");
        debugAbbrev->set_type(ELFIO::SHT_PROGBITS);
        debugAbbrev->set_addr_align(1);
        debugAbbrev->set_data(reinterpret_cast<const char *>(abbrevBytes.data()),
                              static_cast<ELFIO::Elf_Word>(abbrevBytes.size()));

        std::vector<uint8_t> infoBytes(sizeof(uint32_t), 0);
        auto appendU8 = [&infoBytes](uint8_t value)
        { infoBytes.push_back(value); };
        auto appendU16 = [&infoBytes](uint16_t value)
        {
            const auto *bytes = reinterpret_cast<const uint8_t *>(&value);
            infoBytes.insert(infoBytes.end(), bytes, bytes + sizeof(value));
        };
        auto appendU32 = [&infoBytes](uint32_t value)
        {
            const auto *bytes = reinterpret_cast<const uint8_t *>(&value);
            infoBytes.insert(infoBytes.end(), bytes, bytes + sizeof(value));
        };
        auto appendString = [&infoBytes](std::string_view value)
        {
            infoBytes.insert(infoBytes.end(), value.begin(), value.end());
            infoBytes.push_back(0);
        };

        appendU16(4);          // DWARF version
        appendU32(0);          // abbreviation table offset
        appendU8(4);           // address size
        appendU8(1);           // compile-unit DIE
        appendString("partial-unit");
        appendU8(2);           // subprogram DIE
        appendString("known_partial_function");
        appendU32(0x00100000u);
        appendU32(0x20u);      // DWARF 4 high_pc offset
        appendU8(2);           // a later known subprogram bounds fallback ranges
        appendString("known_tail_function");
        appendU32(0x001003F0u);
        appendU32(0x10u);
        appendU8(0);           // end compile-unit children

        const uint32_t unitLength = static_cast<uint32_t>(infoBytes.size() - sizeof(uint32_t));
        std::memcpy(infoBytes.data(), &unitLength, sizeof(unitLength));

        ELFIO::section *debugInfo = writer.sections.add(".debug_info");
        debugInfo->set_type(ELFIO::SHT_PROGBITS);
        debugInfo->set_addr_align(1);
        debugInfo->set_data(reinterpret_cast<const char *>(infoBytes.data()),
                            static_cast<ELFIO::Elf_Word>(infoBytes.size()));
    }

    ELFIO::segment *textSegment = writer.segments.add();
    textSegment->set_type(ELFIO::PT_LOAD);
    textSegment->set_flags(ELFIO::PF_R | ELFIO::PF_X);
    textSegment->set_align(0x1000);
    textSegment->add_section_index(text->get_index(), text->get_addr_align());

    ELFIO::segment *dataSegment = writer.segments.add();
    dataSegment->set_type(ELFIO::PT_LOAD);
    dataSegment->set_flags(ELFIO::PF_R);
    dataSegment->set_align(0x1000);
    dataSegment->add_section_index(rodata->get_index(), rodata->get_addr_align());
    dataSegment->add_section_index(data->get_index(), data->get_addr_align());

    return writer.save(elfPath.string());
}

static bool writeMinimalMipsElfWithInitializer(const std::filesystem::path &elfPath,
                                               const std::string &functionName,
                                               uint32_t initializerTarget)
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
    const std::array<uint32_t, 2> textWords = {
        0x03E00008u, // jr $ra
        0x00000000u, // nop
    };
    text->set_data(reinterpret_cast<const char *>(textWords.data()),
                   static_cast<ELFIO::Elf_Word>(textWords.size() * sizeof(uint32_t)));

    ELFIO::section *ctors = writer.sections.add(".ctors");
    ctors->set_type(ELFIO::SHT_PROGBITS);
    ctors->set_flags(ELFIO::SHF_ALLOC | ELFIO::SHF_WRITE);
    ctors->set_addr_align(4);
    ctors->set_address(0x00200000u);
    ctors->set_data(reinterpret_cast<const char *>(&initializerTarget),
                    static_cast<ELFIO::Elf_Word>(sizeof(initializerTarget)));

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
    symbols.add_symbol(strings, "", 0, 0,
                       ELFIO::STB_LOCAL, ELFIO::STT_NOTYPE, 0, ELFIO::SHN_UNDEF);
    symbols.add_symbol(strings, functionName.c_str(), text->get_address(), text->get_size(),
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
    dataSegment->add_section_index(ctors->get_index(), ctors->get_addr_align());

    return writer.save(elfPath.string());
}

static bool writeRecompilerTestConfig(const std::filesystem::path &configPath,
                                      const std::filesystem::path &elfPath,
                                      const std::filesystem::path &outputPath,
                                      const std::vector<std::string> &skip,
                                      const std::vector<std::string> &stubs = {},
                                      const std::vector<std::string> &entryPoints = {})
{
    std::ofstream config(configPath);
    if (!config)
        return false;

    config << "[general]\n";
    config << "input = \"" << elfPath.generic_string() << "\"\n";
    config << "output = \"" << outputPath.generic_string() << "\"\n";
    config << "skip = [";
    for (size_t i = 0; i < skip.size(); ++i)
    {
        if (i != 0u)
            config << ", ";
        config << '"' << skip[i] << '"';
    }
    config << "]\n";
    config << "stubs = [";
    for (size_t i = 0; i < stubs.size(); ++i)
    {
        if (i != 0u)
            config << ", ";
        config << '"' << stubs[i] << '"';
    }
    config << "]\n";
    config << "entry_points = [";
    for (size_t i = 0; i < entryPoints.size(); ++i)
    {
        if (i != 0u)
            config << ", ";
        config << '"' << entryPoints[i] << '"';
    }
    config << "]\n";
    return static_cast<bool>(config);
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

        tc.Run("config manager loads modern and legacy guest entry hints", [](TestCase &t) {
            const auto uniqueSuffix = std::to_string(
                static_cast<unsigned long long>(std::chrono::steady_clock::now().time_since_epoch().count()));
            const std::filesystem::path configPath =
                std::filesystem::temp_directory_path() / ("ps2recomp-entry-hints-" + uniqueSuffix + ".toml");

            std::ofstream configFile(configPath);
            t.IsTrue(static_cast<bool>(configFile), "temp config file should be writable");
            if (!configFile)
            {
                return;
            }

            configFile << "[general]\n";
            configFile << "input = \"dummy.elf\"\n";
            configFile << "output = \"out\"\n";
            configFile << "entry_points = [\"callback@0x7008\"]\n";
            configFile << "untracked_stubs = [\"legacy_callback@0x7018\"]\n";
            configFile.close();

            ConfigManager manager(configPath.string());
            const RecompilerConfig config = manager.loadConfig();

            t.Equals(config.entryPointHints.size(), static_cast<size_t>(2),
                     "modern and legacy entry metadata should be merged");
            t.IsTrue(std::find(config.entryPointHints.begin(), config.entryPointHints.end(),
                               "callback@0x7008") != config.entryPointHints.end(),
                     "modern entry_points metadata should load");
            t.IsTrue(std::find(config.entryPointHints.begin(), config.entryPointHints.end(),
                               "legacy_callback@0x7018") != config.entryPointHints.end(),
                     "legacy untracked_stubs metadata should remain compatible");

            std::error_code removeError;
            std::filesystem::remove(configPath, removeError);
        });

        tc.Run("configured entry hint synthesizes an omitted standalone function", [](TestCase &t) {
            const std::string uniqueSuffix =
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
            const std::filesystem::path tempRoot =
                std::filesystem::temp_directory_path() / ("ps2recomp-entry-synthesis-" + uniqueSuffix);
            const std::filesystem::path elfPath = tempRoot / "entry-hint.elf";
            const std::filesystem::path configPath = tempRoot / "entry-hint.toml";
            const std::filesystem::path outputPath = tempRoot / "output";
            std::filesystem::create_directories(tempRoot);

            const bool elfWritten = writeMinimalMipsElfWithUnmappedEntryHint(elfPath);
            const bool configWritten = writeRecompilerTestConfig(
                configPath,
                elfPath,
                outputPath,
                {},
                {"InitAlarm@0x00100008"},
                {"omitted_callback@0x00100010"});
            t.IsTrue(elfWritten && configWritten,
                     "standalone entry regression inputs should be generated");

            if (elfWritten && configWritten)
            {
                PS2Recompiler recompiler(configPath.string());
                t.IsTrue(recompiler.initialize(),
                         "standalone entry regression config should initialize");
                t.IsTrue(recompiler.recompile(),
                         "configured executable entry should be decoded even without a symbol");
                recompiler.generateOutput();

                const std::filesystem::path registrationPath = outputPath / "register_functions.cpp";
                std::ifstream registrationFile(registrationPath);
                const std::string registration{
                    std::istreambuf_iterator<char>(registrationFile),
                    std::istreambuf_iterator<char>()};
                t.IsTrue(registration.find("// 0x100010") != std::string::npos,
                         "synthesized entry address should be registered for guest dispatch");
                t.IsTrue(recompiler.reportCounters().additionalEntryPoints >= 1u,
                         "synthesized entry should be visible in the report");
            }

            std::error_code removeError;
            std::filesystem::remove_all(tempRoot, removeError);
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

        tc.Run("elf parser keeps VU microprograms out of EE code discovery", [](TestCase &t) {
            const auto uniqueSuffix = std::to_string(
                static_cast<unsigned long long>(std::chrono::steady_clock::now().time_since_epoch().count()));
            const std::filesystem::path elfPath =
                std::filesystem::temp_directory_path() / ("ps2recomp-vutext-" + uniqueSuffix + ".elf");

            const bool writeOk = writeMinimalMipsElfWithVuMicroprogramSection(elfPath);
            t.IsTrue(writeOk, "temporary ELF with .vutext should be generated");
            if (!writeOk)
                return;

            ElfParser parser(elfPath.string());
            const bool parseOk = parser.parse();
            t.IsTrue(parseOk, "ELF with .vutext should parse");
            if (parseOk)
            {
                const auto sections = parser.getSections();
                const auto vuSection = std::find_if(sections.begin(), sections.end(),
                                                    [](const Section &section)
                                                    { return section.name == ".vutext"; });
                t.IsTrue(vuSection != sections.end(), ".vutext bytes should remain available");
                if (vuSection != sections.end())
                    t.IsFalse(vuSection->isCode, ".vutext must not be decoded as R5900 code");

                const auto functions = parser.extractFunctions();
                const bool hasVuFunction = std::any_of(functions.begin(), functions.end(),
                                                       [](const Function &function)
                                                       { return function.start == 0x00250000u; });
                t.IsFalse(hasVuFunction, "VU symbol must not become an EE function");
            }

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

        tc.Run("elf parser discovers address-taken callbacks in stripped ELFs", [](TestCase &t) {
            const auto uniqueSuffix = std::to_string(
                static_cast<unsigned long long>(std::chrono::steady_clock::now().time_since_epoch().count()));
            const std::filesystem::path elfPath =
                std::filesystem::temp_directory_path() / ("ps2recomp-address-taken-" + uniqueSuffix + ".elf");

            const bool writeOk = writeMinimalMipsElfWithAddressTakenCallbacks(elfPath);
            t.IsTrue(writeOk, "temporary stripped ELF should be generated");
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
            auto hasStart = [&functions](uint32_t start)
            {
                return std::any_of(functions.begin(), functions.end(),
                                   [start](const Function &function)
                                   { return function.start == start; });
            };

            t.IsTrue(hasStart(0x00100040u),
                     "LUI plus delay-slot ADDIU should discover the callback entry");
            t.IsTrue(hasStart(0x00100060u),
                     "clustered rodata pointers should discover the first leaf callback");
            t.IsTrue(hasStart(0x00100068u),
                     "clustered rodata pointers should discover the second leaf callback");
            t.IsTrue(hasStart(0x00100080u),
                     "clustered pointers should discover an initializer with a delayed stack prologue");
            t.IsTrue(hasStart(0x001000E0u),
                     "a nearby registrar call should discover a longer leaf callback");
            t.IsTrue(hasStart(0x00100120u),
                     "a clustered descriptor should discover a long leaf method");
            t.IsTrue(hasStart(0x00100280u),
                     "a callback should flow through a saved register into a call argument");
            t.IsTrue(hasStart(0x001002C0u),
                     "a delay-slot LUI should flow into the taken branch successor");
            t.IsTrue(hasStart(0x00100340u),
                     "a materialized method stored into a runtime descriptor should be discovered");
            t.IsTrue(hasStart(0x00100380u),
                     "an alternating pointer/id table should discover a neighboring long leaf");
            t.IsTrue(hasStart(0x001003C8u),
                     "an adjacent two-instruction leaf thunk should be split from a known thunk");
            t.IsTrue(hasStart(0x001003E0u),
                     "a callback passed as the fifth register argument should be discovered");
            t.IsTrue(hasStart(0x00100400u),
                     "a long callback passed as the fifth register argument should be discovered");
            t.IsFalse(hasStart(0x00100548u),
                      "a post-return prologue without a cross-reference must remain only a hint");
            t.IsTrue(hasStart(0x00100580u),
                     "a singleton data pointer loaded and consumed by JALR should be discovered");
            t.IsFalse(hasStart(0x00100588u),
                      "a data pointer must not split a function at a control-transfer delay slot");
            t.IsFalse(hasStart(0x00100300u),
                      "an isolated data pointer or non-callback code materialization must not become a function");

            std::error_code removeError;
            std::filesystem::remove(elfPath, removeError);
        });

        tc.Run("elf parser supplements partial DWARF with address-taken callbacks", [](TestCase &t) {
            const auto uniqueSuffix = std::to_string(
                static_cast<unsigned long long>(std::chrono::steady_clock::now().time_since_epoch().count()));
            const std::filesystem::path elfPath =
                std::filesystem::temp_directory_path() / ("ps2recomp-partial-dwarf-" + uniqueSuffix + ".elf");

            const bool writeOk = writeMinimalMipsElfWithAddressTakenCallbacks(elfPath, true);
            t.IsTrue(writeOk, "temporary ELF with partial DWARF should be generated");
            if (!writeOk)
            {
                return;
            }

            ElfParser parser(elfPath.string());
            const bool parseOk = parser.parse();
            t.IsTrue(parseOk, "generated ELF with partial DWARF should parse");
            if (parseOk)
            {
                const auto functions = parser.extractFunctions();
                const auto callbackIt = std::find_if(
                    functions.begin(), functions.end(),
                    [](const Function &function)
                    { return function.start == 0x00100040u; });
                t.IsTrue(callbackIt != functions.end(),
                         "partial DWARF must not suppress static callback discovery");

                const auto lastFallbackIt = std::find_if(
                    functions.begin(), functions.end(),
                    [](const Function &function)
                    { return function.start == 0x001003E0u; });
                t.IsTrue(lastFallbackIt != functions.end(),
                         "the last inferred callback should still be discovered");
                if (lastFallbackIt != functions.end())
                {
                    t.Equals(0x001003F0u, lastFallbackIt->end,
                             "later partial DWARF should bound an inferred callback");
                }
            }

            std::error_code removeError;
            std::filesystem::remove(elfPath, removeError);
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

        tc.Run("initializer skips fall back to guest recompilation", [](TestCase &t) {
            const std::string uniqueSuffix =
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
            const std::filesystem::path tempRoot =
                std::filesystem::temp_directory_path() / ("ps2recomp-initializer-" + uniqueSuffix);
            const std::filesystem::path elfPath = tempRoot / "initializer.elf";
            const std::filesystem::path configPath = tempRoot / "initializer.toml";
            const std::filesystem::path outputPath = tempRoot / "output";
            std::filesystem::create_directories(tempRoot);

            const bool elfWritten =
                writeMinimalMipsElfWithInitializer(elfPath, "__sinit_test.cpp", 0x00100000u);
            const bool configWritten =
                writeRecompilerTestConfig(configPath, elfPath, outputPath, {"__sinit_test.cpp"});
            t.IsTrue(elfWritten && configWritten,
                     "initializer regression inputs should be generated");

            if (elfWritten && configWritten)
            {
                PS2Recompiler recompiler(configPath.string());
                t.IsTrue(recompiler.initialize(),
                         "initializer regression config should initialize");
                t.IsTrue(recompiler.recompile(),
                         "a decodable skipped initializer should use guest fallback");
                const RecompilerReporter::Counters &counters = recompiler.reportCounters();
                t.Equals(counters.correctnessCriticalGuestFallbacks, static_cast<size_t>(1u),
                         "the ignored initializer skip should be reported");
                t.Equals(counters.correctnessCriticalFailures, static_cast<size_t>(0u),
                         "guest fallback should avoid a correctness-critical failure");
                t.Equals(counters.functionsSkipped, static_cast<size_t>(0u),
                         "the initializer should not remain skipped");
                t.Equals(counters.functionsRecompiled, static_cast<size_t>(1u),
                         "the original initializer body should be recompiled");
            }

            std::error_code removeError;
            std::filesystem::remove_all(tempRoot, removeError);
        });

        tc.Run("missing constructor-table targets fail recompilation", [](TestCase &t) {
            const std::string uniqueSuffix =
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
            const std::filesystem::path tempRoot =
                std::filesystem::temp_directory_path() / ("ps2recomp-missing-initializer-" + uniqueSuffix);
            const std::filesystem::path elfPath = tempRoot / "initializer.elf";
            const std::filesystem::path configPath = tempRoot / "initializer.toml";
            const std::filesystem::path outputPath = tempRoot / "output";
            std::filesystem::create_directories(tempRoot);

            const bool elfWritten =
                writeMinimalMipsElfWithInitializer(elfPath, "ordinary_entry", 0x00100040u);
            const bool configWritten =
                writeRecompilerTestConfig(configPath, elfPath, outputPath, {});
            t.IsTrue(elfWritten && configWritten,
                     "missing-initializer regression inputs should be generated");

            if (elfWritten && configWritten)
            {
                {
                    PS2Recompiler recompiler(configPath.string());
                    t.IsTrue(recompiler.initialize(),
                             "missing-initializer regression config should initialize");
                    t.IsFalse(recompiler.recompile(),
                              "an unresolved .ctors target should be correctness-fatal");
                    t.Equals(recompiler.reportCounters().correctnessCriticalFailures,
                             static_cast<size_t>(1u),
                             "the unresolved constructor target should appear in the report");
                }

                const bool overrideWritten =
                    writeRecompilerTestConfig(
                        configPath, elfPath, outputPath, {},
                        {"memclr@0x00100040"});
                t.IsTrue(overrideWritten,
                         "manual initializer override config should be generated");
                if (overrideWritten)
                {
                    PS2Recompiler overridden(configPath.string());
                    t.IsTrue(overridden.initialize(),
                             "manual initializer override should initialize");
                    t.IsTrue(overridden.recompile(),
                             "a resolved address-bound handler should satisfy the constructor target");
                    t.Equals(overridden.reportCounters().functionsStubbed,
                             static_cast<size_t>(1u),
                             "the resolved manual initializer should be emitted as a stub binding");
                }
            }

            std::error_code removeError;
            std::filesystem::remove_all(tempRoot, removeError);
        });

        tc.Run("respect max length for .cpp filenames", [](TestCase& t) {
            
            t.IsTrue(PS2Recompiler::ClampFilenameLength("ReallyLongFunctionNameReallyLongFunctionNameReallyLongFunctionName_0x12345678",".cpp",50).length() <= 50,"Function name must be max 50 characters");

            t.IsTrue(PS2Recompiler::ClampFilenameLength("ReallyLongFunctionNameReallyLongFunctionNameReallyLongFunctionName_0x12345678", ".cpp", 50).rfind("0x12345678") != std::string::npos, "Function name must mantain the function address at the end, if present");
            
        });
    });
}
