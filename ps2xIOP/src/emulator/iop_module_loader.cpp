#include "iop_module_loader.h"

#include "iop_memory.h"
#include "ps2x/iop/iop_subsystem.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <string>

namespace ps2x::iop::detail
{
    namespace
    {
        constexpr uint32_t kMaxImageSize = 64u * 1024u * 1024u;
        constexpr uint32_t kModuleLoadBase = 0x00010000u;

        constexpr uint16_t ET_EXEC = 2;
        constexpr uint16_t ET_SCE_IOPRELEXEC = 0xFF80u;
        constexpr uint16_t ET_SCE_IOPRELEXEC2 = 0xFF81u;
        constexpr uint16_t EM_MIPS = 8;
        constexpr uint32_t PT_LOAD = 1;
        constexpr uint32_t PT_SCE_IOPMOD = 0x70000080u;
        constexpr uint32_t PT_MIPS_REGINFO = 0x70000000u;
        constexpr uint32_t SHT_SYMTAB = 2;
        constexpr uint32_t SHT_MIPS_REGINFO = 0x70000006u;
        constexpr uint32_t SHT_RELA = 4;
        constexpr uint32_t SHT_NOBITS = 8;
        constexpr uint32_t SHT_REL = 9;
        constexpr uint32_t SHF_ALLOC = 0x2;
        constexpr uint32_t R_MIPS_NONE = 0;
        constexpr uint32_t R_MIPS_16 = 1;
        constexpr uint32_t R_MIPS_32 = 2;
        constexpr uint32_t R_MIPS_REL32 = 3;
        constexpr uint32_t R_MIPS_26 = 4;
        constexpr uint32_t R_MIPS_HI16 = 5;
        constexpr uint32_t R_MIPS_LO16 = 6;

#pragma pack(push, 1)
        struct Elf32Ehdr
        {
            unsigned char ident[16];
            uint16_t type;
            uint16_t machine;
            uint32_t version;
            uint32_t entry;
            uint32_t phoff;
            uint32_t shoff;
            uint32_t flags;
            uint16_t ehsize;
            uint16_t phentsize;
            uint16_t phnum;
            uint16_t shentsize;
            uint16_t shnum;
            uint16_t shstrndx;
        };

        struct Elf32Phdr
        {
            uint32_t type;
            uint32_t offset;
            uint32_t vaddr;
            uint32_t paddr;
            uint32_t filesz;
            uint32_t memsz;
            uint32_t flags;
            uint32_t align;
        };

        struct Elf32Shdr
        {
            uint32_t name;
            uint32_t type;
            uint32_t flags;
            uint32_t addr;
            uint32_t offset;
            uint32_t size;
            uint32_t link;
            uint32_t info;
            uint32_t addralign;
            uint32_t entsize;
        };

        struct Elf32Sym
        {
            uint32_t name;
            uint32_t value;
            uint32_t size;
            uint8_t info;
            uint8_t other;
            uint16_t shndx;
        };

        struct Elf32Rel
        {
            uint32_t offset;
            uint32_t info;
        };

        struct Elf32Rela
        {
            uint32_t offset;
            uint32_t info;
            int32_t addend;
        };
#pragma pack(pop)

        static_assert(sizeof(Elf32Ehdr) == 52);
        static_assert(sizeof(Elf32Phdr) == 32);
        static_assert(sizeof(Elf32Shdr) == 40);
        static_assert(sizeof(Elf32Sym) == 16);

        struct PendingHi16
        {
            uint32_t address = 0;
            uint32_t symbolValue = 0;
            uint32_t symbolIndex = 0;
        };

        uint32_t alignUp(uint32_t value, uint32_t alignment)
        {
            if (alignment <= 1u)
                return value;
            const uint32_t mask = alignment - 1u;
            return (value + mask) & ~mask;
        }

        bool checkedRange(size_t total, uint32_t offset, uint32_t size)
        {
            return offset <= total && size <= total - offset;
        }

        bool validElfHeader(const Elf32Ehdr &header)
        {
            return header.ident[0] == 0x7Fu &&
                   header.ident[1] == 'E' &&
                   header.ident[2] == 'L' &&
                   header.ident[3] == 'F' &&
                   header.ident[4] == 1 &&
                   header.ident[5] == 1 &&
                   header.machine == EM_MIPS &&
                   header.ehsize >= sizeof(Elf32Ehdr);
        }

        bool applyRelocations(std::span<const uint8_t> image,
                              const std::vector<Elf32Shdr> &sections,
                              int64_t delta,
                              uint32_t loadBase,
                              bool isIopRelocatable,
                              IopMemory &memory)
        {
            if (sections.empty())
                return true;

            bool allSupported = true;
            std::vector<PendingHi16> hi16;
            for (size_t sectionIndex = 0; sectionIndex < sections.size(); ++sectionIndex)
            {
                const Elf32Shdr &relsec = sections[sectionIndex];
                if (relsec.type != SHT_REL && relsec.type != SHT_RELA)
                    continue;
                if (relsec.info >= sections.size())
                    continue;

                const Elf32Shdr &targetSection = sections[relsec.info];
                const uint32_t targetBase = static_cast<uint32_t>(static_cast<int64_t>(targetSection.addr) + delta);

                std::span<const Elf32Sym> symbols;
                std::vector<Elf32Sym> symbolStorage;
                if (relsec.link < sections.size())
                {
                    const Elf32Shdr &symsec = sections[relsec.link];
                    if (symsec.type == SHT_SYMTAB &&
                        symsec.entsize >= sizeof(Elf32Sym) &&
                        checkedRange(image.size(), symsec.offset, symsec.size))
                    {
                        const size_t count = symsec.size / symsec.entsize;
                        symbolStorage.resize(count);
                        for (size_t i = 0; i < count; ++i)
                        {
                            std::memcpy(&symbolStorage[i], image.data() + symsec.offset + i * symsec.entsize, sizeof(Elf32Sym));
                        }
                        symbols = symbolStorage;
                    }
                }

                const uint32_t entrySize = relsec.type == SHT_RELA
                                               ? std::max<uint32_t>(relsec.entsize, sizeof(Elf32Rela))
                                               : std::max<uint32_t>(relsec.entsize, sizeof(Elf32Rel));
                if (entrySize == 0u || !checkedRange(image.size(), relsec.offset, relsec.size))
                    continue;

                for (uint32_t offset = 0; offset + entrySize <= relsec.size; offset += entrySize)
                {
                    uint32_t relocationOffset = 0u;
                    uint32_t relocationInfo = 0u;
                    int32_t explicitAddend = 0;
                    if (relsec.type == SHT_RELA)
                    {
                        Elf32Rela relocation{};
                        std::memcpy(&relocation,
                                    image.data() + relsec.offset + offset,
                                    sizeof(relocation));
                        relocationOffset = relocation.offset;
                        relocationInfo = relocation.info;
                        explicitAddend = relocation.addend;
                    }
                    else
                    {
                        Elf32Rel relocation{};
                        std::memcpy(&relocation, image.data() + relsec.offset + offset, sizeof(relocation));
                        relocationOffset = relocation.offset;
                        relocationInfo = relocation.info;
                    }

                    const uint32_t type = relocationInfo & 0xFFu;
                    const uint32_t symbolIndex = relocationInfo >> 8u;
                    uint32_t symbolValue = isIopRelocatable ? loadBase : 0u;
                    if (symbolIndex < symbols.size())
                    {
                        const Elf32Sym &symbol = symbols[symbolIndex];
                        if (!isIopRelocatable || symbolIndex != 0u)
                        {
                            symbolValue = symbol.value;
                            if (symbol.shndx != 0u)
                            {
                                symbolValue = static_cast<uint32_t>(
                                    static_cast<int64_t>(symbolValue) + delta);
                            }
                        }
                    }

                    // Sony IOP relocatable executables use absolute image offsets
                    // and symbol index zero. loadcore applies them as loadBase +
                    // r_offset; normal ELF REL sections use a section-relative offset.
                    const uint64_t place64 = isIopRelocatable
                                                 ? static_cast<uint64_t>(loadBase) + relocationOffset
                                                 : static_cast<uint64_t>(targetBase) + relocationOffset;
                    if (place64 > std::numeric_limits<uint32_t>::max())
                    {
                        allSupported = false;
                        continue;
                    }
                    const uint32_t place = static_cast<uint32_t>(place64);
                    if (place + 3u >= IopMemory::RamSize)
                    {
                        allSupported = false;
                        continue;
                    }

                    const uint32_t word = memory.read32(place);
                    const int32_t addend = relsec.type == SHT_RELA
                                               ? explicitAddend
                                               : static_cast<int32_t>(word);
                    switch (type)
                    {
                    case R_MIPS_NONE:
                        break;
                    case R_MIPS_32:
                    case R_MIPS_REL32:
                        memory.write32(place,
                                       static_cast<uint32_t>(
                                           static_cast<int64_t>(addend) + symbolValue));
                        break;
                    case R_MIPS_26:
                    {
                        const uint32_t target = ((word & 0x03FFFFFFu) << 2u) + symbolValue;
                        memory.write32(place,
                                       (word & 0xFC000000u) |
                                           ((target >> 2u) & 0x03FFFFFFu));
                        break;
                    }
                    case R_MIPS_HI16:
                        hi16.push_back({place, symbolValue, symbolIndex});
                        break;
                    case R_MIPS_LO16:
                    {
                        const int32_t lo = static_cast<int16_t>(word & 0xFFFFu);
                        for (auto pending = hi16.begin(); pending != hi16.end();)
                        {
                            if (pending->symbolIndex != symbolIndex)
                            {
                                ++pending;
                                continue;
                            }
                            const uint32_t hiWord = memory.read32(pending->address);
                            const int32_t hi = static_cast<int16_t>(hiWord & 0xFFFFu) << 16u;
                            const int64_t full = static_cast<int64_t>(hi) + lo + pending->symbolValue;
                            const uint32_t relocatedHi = static_cast<uint32_t>((full + 0x8000) >> 16u) & 0xFFFFu;
                            memory.write32(pending->address, (hiWord & 0xFFFF0000u) | relocatedHi);
                            pending = hi16.erase(pending);
                        }
                        const int64_t full = static_cast<int64_t>(lo) + symbolValue;
                        memory.write32(place, (word & 0xFFFF0000u) | (static_cast<uint32_t>(full) & 0xFFFFu));
                        break;
                    }
                    case R_MIPS_16:
                        memory.write32(place, (word & 0xFFFF0000u) | (static_cast<uint32_t>(addend + symbolValue) & 0xFFFFu));
                        break;
                    default:
                        allSupported = false;
                        break;
                    }
                }
            }
            return allSupported;
        }
    }

    bool IopModuleLoader::readWholeHostFile(IopHost &host, std::string_view guestPath, std::vector<uint8_t> &bytes)
    {
        const std::string translated = host.translateGuestPath(guestPath);
        const std::string_view path = translated.empty() ? guestPath : std::string_view(translated);
        const uint64_t handle = host.openHostFile(path);
        if (handle == 0u)
            return false;

        uint64_t size = 0u;
        if (!host.hostFileSize(handle, size) || size == 0u || size > kMaxImageSize)
        {
            host.closeHostFile(handle);
            return false;
        }

        bytes.resize(static_cast<size_t>(size));
        size_t bytesRead = 0u;
        const bool ok = host.readHostFile(handle, 0u, bytes.data(), bytes.size(), bytesRead) && bytesRead == bytes.size();
        host.closeHostFile(handle);
        return ok;
    }

    bool IopModuleLoader::readElfFromGuest(IopHost &host, uint32_t guestAddress, std::vector<uint8_t> &bytes)
    {
        Elf32Ehdr header{};
        if (!host.readGuest(guestAddress, &header, sizeof(header)) || !validElfHeader(header))
            return false;

        uint64_t required = sizeof(header);
        required = std::max<uint64_t>(required, static_cast<uint64_t>(header.phoff) + static_cast<uint64_t>(header.phentsize) * header.phnum);
        required = std::max<uint64_t>(required, static_cast<uint64_t>(header.shoff) + static_cast<uint64_t>(header.shentsize) * header.shnum);

        if (required > kMaxImageSize)
            return false; // Should we log an error here? TODO check later

        bytes.resize(static_cast<size_t>(required));
        if (!host.readGuest(guestAddress, bytes.data(), bytes.size()))
            return false;

        if (header.shnum != 0u && header.shentsize >= sizeof(Elf32Shdr))
        {
            for (uint16_t i = 0; i < header.shnum; ++i)
            {
                Elf32Shdr section{};
                const size_t offset = static_cast<size_t>(header.shoff) + static_cast<size_t>(i) * header.shentsize;
                std::memcpy(&section, bytes.data() + offset, sizeof(section));
                if (section.type != SHT_NOBITS)
                {
                    required = std::max<uint64_t>(required, static_cast<uint64_t>(section.offset) + section.size);
                }
            }
        }
        if (header.phnum != 0u && header.phentsize >= sizeof(Elf32Phdr))
        {
            for (uint16_t i = 0; i < header.phnum; ++i)
            {
                Elf32Phdr program{};
                const size_t offset = static_cast<size_t>(header.phoff) + static_cast<size_t>(i) * header.phentsize;
                std::memcpy(&program, bytes.data() + offset, sizeof(program));
                required = std::max<uint64_t>(required, static_cast<uint64_t>(program.offset) + program.filesz);
            }
        }
        if (required > kMaxImageSize)
            return false;

        bytes.resize(static_cast<size_t>(required));
        return host.readGuest(guestAddress, bytes.data(), bytes.size());
    }

    IopImageLoadResult IopModuleLoader::load(std::span<const uint8_t> image, IopMemory &memory, uint32_t moduleCursor)
    {
        IopImageLoadResult result;
        result.nextModuleCursor = moduleCursor;
        if (image.size() < sizeof(Elf32Ehdr))
            return result;

        Elf32Ehdr header{};
        std::memcpy(&header, image.data(), sizeof(header));
        if (!validElfHeader(header))
        {
            result.error = IopImageLoadError::InvalidElf;
            return result;
        }

        uint32_t minVaddr = std::numeric_limits<uint32_t>::max();
        uint32_t maxVaddr = 0u;
        bool hasLoad = false;
        std::vector<Elf32Phdr> programHeaders;
        if (header.phnum != 0u && header.phentsize >= sizeof(Elf32Phdr) && checkedRange(image.size(), header.phoff, static_cast<uint32_t>(header.phentsize) * header.phnum))
        {
            programHeaders.reserve(header.phnum);
            for (uint16_t i = 0; i < header.phnum; ++i)
            {
                Elf32Phdr program{};
                std::memcpy(&program, image.data() + header.phoff + static_cast<size_t>(i) * header.phentsize, sizeof(program));
                programHeaders.push_back(program);
                if (program.type == PT_LOAD && program.memsz != 0u)
                {
                    hasLoad = true;
                    minVaddr = std::min(minVaddr, program.vaddr);
                    maxVaddr = std::max(maxVaddr, program.vaddr + program.memsz);
                }
            }
        }

        std::vector<Elf32Shdr> sectionHeaders;
        if (header.shnum != 0u && header.shentsize >= sizeof(Elf32Shdr) && checkedRange(image.size(), header.shoff, static_cast<uint32_t>(header.shentsize) * header.shnum))
        {
            sectionHeaders.reserve(header.shnum);
            for (uint16_t i = 0; i < header.shnum; ++i)
            {
                Elf32Shdr section{};
                std::memcpy(&section, image.data() + header.shoff + static_cast<size_t>(i) * header.shentsize, sizeof(section));
                sectionHeaders.push_back(section);
                if (!hasLoad && (section.flags & SHF_ALLOC) != 0u && section.size != 0u)
                {
                    minVaddr = std::min(minVaddr, section.addr);
                    maxVaddr = std::max(maxVaddr, section.addr + section.size);
                }
            }
        }

        if (minVaddr == std::numeric_limits<uint32_t>::max())
            minVaddr = 0u;
        uint32_t span = maxVaddr > minVaddr ? maxVaddr - minVaddr : 0x1000u;
        span = alignUp(span, 0x100u);
        const bool relocate = header.type != ET_EXEC ||
                              maxVaddr > IopMemory::RamSize ||
                              (minVaddr < kModuleLoadBase && minVaddr != 0u);
        uint32_t base = 0u;
        int64_t delta = 0;
        if (relocate)
        {
            base = alignUp(moduleCursor, 0x100u);
            if (base + span >= IopMemory::HeapBase)
            {
                result.error = IopImageLoadError::ArenaExhausted;
                return result;
            }
            delta = static_cast<int64_t>(base) - minVaddr;
            result.nextModuleCursor = base + span;
        }
        else
        {
            base = minVaddr;
        }

        if (hasLoad)
        {
            for (const auto &program : programHeaders)
            {
                if (program.type != PT_LOAD || program.memsz == 0u)
                    continue;
                if (!checkedRange(image.size(), program.offset, program.filesz) ||
                    program.memsz < program.filesz)
                    return result;
                const uint32_t destination = static_cast<uint32_t>(static_cast<int64_t>(program.vaddr) + delta);
                if (destination >= IopMemory::RamSize || program.memsz > IopMemory::RamSize - destination)
                    return result;
                if (!memory.writeRam(destination, image.data() + program.offset, program.filesz))
                    return result;
                if (program.memsz > program.filesz && !memory.zeroRam(destination + program.filesz, program.memsz - program.filesz))
                    return result;
            }
        }
        else
        {
            uint32_t sectionCursor = base;
            for (auto &section : sectionHeaders)
            {
                if ((section.flags & SHF_ALLOC) == 0u || section.size == 0u)
                    continue;
                uint32_t destination = 0u;
                if (section.addr != 0u)
                {
                    destination = static_cast<uint32_t>(static_cast<int64_t>(section.addr) + delta);
                }
                else
                {
                    sectionCursor = alignUp(sectionCursor, std::max<uint32_t>(section.addralign, 4u));
                    destination = sectionCursor;
                    section.addr = static_cast<uint32_t>(static_cast<int64_t>(destination) - delta);
                    sectionCursor += section.size;
                }
                if (destination >= IopMemory::RamSize || section.size > IopMemory::RamSize - destination)
                    return result;
                if (section.type == SHT_NOBITS)
                {
                    if (!memory.zeroRam(destination, section.size))
                        return result;
                }
                else
                {
                    if (!checkedRange(image.size(), section.offset, section.size) ||
                        !memory.writeRam(destination,
                                         image.data() + section.offset,
                                         section.size))
                        return result;
                }
            }
        }

        const bool isIopRelocatable = header.type == ET_SCE_IOPRELEXEC || header.type == ET_SCE_IOPRELEXEC2;
        result.relocationsComplete = applyRelocations(image,
                                                      sectionHeaders,
                                                      delta,
                                                      base,
                                                      isIopRelocatable,
                                                      memory);

        result.base = base;
        result.size = span;
        result.entry = static_cast<uint32_t>(static_cast<int64_t>(header.entry) + delta);
        result.gp = 0u;
        for (const auto &program : programHeaders)
        {
            if (program.type == PT_SCE_IOPMOD && program.filesz >= 12u && checkedRange(image.size(), program.offset, 12u))
            {
                uint32_t entry = 0u;
                uint32_t gp = 0u;
                std::memcpy(&entry, image.data() + program.offset + 4u, sizeof(entry));
                std::memcpy(&gp, image.data() + program.offset + 8u, sizeof(gp));
                result.entry = static_cast<uint32_t>(static_cast<int64_t>(entry) + delta);
                result.gp = gp != 0u
                                ? static_cast<uint32_t>(static_cast<int64_t>(gp) + delta)
                                : 0u;
                break;
            }
        }
        for (const auto &program : programHeaders)
        {
            if (result.gp != 0u)
                break;
            if (program.type == PT_MIPS_REGINFO && program.filesz >= 24u && checkedRange(image.size(), program.offset, 24u))
            {
                uint32_t gp = 0u;
                std::memcpy(&gp, image.data() + program.offset + 20u, sizeof(gp));
                result.gp = gp != 0u
                                ? static_cast<uint32_t>(static_cast<int64_t>(gp) + delta)
                                : 0u;
                break;
            }
        }
        if (result.gp == 0u)
        {
            for (const auto &section : sectionHeaders)
            {
                if (section.type == SHT_MIPS_REGINFO && section.size >= 24u && checkedRange(image.size(), section.offset, 24u))
                {
                    uint32_t gp = 0u;
                    std::memcpy(&gp, image.data() + section.offset + 20u, sizeof(gp));
                    result.gp = gp != 0u
                                    ? static_cast<uint32_t>(static_cast<int64_t>(gp) + delta)
                                    : 0u;
                    break;
                }
            }
        }

        result.error = IopImageLoadError::None;
        return result;
    }
}
