#include "MiniTest.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "ps2_syscalls.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <vector>

using namespace ps2_syscalls;

namespace
{
    constexpr uint32_t kOpcodeJ = 2u;
    constexpr uint32_t kOpcodeJal = 3u;

    constexpr uint32_t encodeJump(uint32_t opcode, uint32_t target)
    {
        return (opcode << 26) | ((target >> 2) & 0x03FFFFFFu);
    }

    void setRegU32(R5900Context &ctx, int reg, uint32_t value)
    {
        ctx.r[reg] = _mm_set_epi64x(0, static_cast<int64_t>(value));
    }

    void writeWord(uint8_t *rdram, uint32_t address, uint32_t value)
    {
        std::memcpy(rdram + address, &value, sizeof(value));
    }

    // Runs action, which must throw ExceptionT whose message holds every fragment.
    template <typename ExceptionT>
    void expectThrow(TestCase &t, const std::function<void()> &action,
                     std::initializer_list<std::string> fragments, const std::string &what)
    {
        try
        {
            action();
        }
        catch (const ExceptionT &error)
        {
            const std::string message = error.what();
            for (const std::string &fragment : fragments)
            {
                t.IsTrue(message.find(fragment) != std::string::npos,
                         what + ": report should contain '" + fragment + "' but was: " + message);
            }
            return;
        }
        t.Fail(what + ": expected an exception");
    }

    // A one-segment MIPS executable whose code is words, loaded at base.
    std::vector<uint8_t> makeCodeElf(uint32_t base, const std::vector<uint32_t> &words)
    {
        constexpr uint32_t kHeaderSize = 52u;
        constexpr uint32_t kProgramHeaderSize = 32u;
        constexpr uint32_t kCodeOffset = 0x100u;
        const uint32_t codeSize = static_cast<uint32_t>(words.size() * sizeof(uint32_t));
        std::vector<uint8_t> elf(kCodeOffset + codeSize, 0u);
        const auto put16 = [&](size_t at, uint16_t value) { std::memcpy(elf.data() + at, &value, sizeof(value)); };
        const auto put32 = [&](size_t at, uint32_t value) { std::memcpy(elf.data() + at, &value, sizeof(value)); };
        elf[0] = 0x7Fu;
        elf[1] = 'E';
        elf[2] = 'L';
        elf[3] = 'F';
        elf[4] = 1u; // ELFCLASS32
        elf[5] = 1u; // ELFDATA2LSB
        elf[6] = 1u; // EV_CURRENT
        put16(16, 2u); // ET_EXEC
        put16(18, 8u); // EM_MIPS
        put32(20, 1u);
        put32(24, base); // e_entry
        put32(28, kHeaderSize);
        put16(40, kHeaderSize);
        put16(42, kProgramHeaderSize);
        put16(44, 1u);
        put32(kHeaderSize + 0u, 1u); // PT_LOAD
        put32(kHeaderSize + 4u, kCodeOffset);
        put32(kHeaderSize + 8u, base);
        put32(kHeaderSize + 12u, base);
        put32(kHeaderSize + 16u, codeSize);
        put32(kHeaderSize + 20u, codeSize);
        put32(kHeaderSize + 24u, 5u); // PF_R | PF_X
        put32(kHeaderSize + 28u, 0x10u);
        std::memcpy(elf.data() + kCodeOffset, words.data(), codeSize);
        return elf;
    }

    constexpr uint32_t kSitePc = 0x00101000u;
    constexpr uint32_t kElfTargetPc = 0x00102000u;
    constexpr uint32_t kPatchedTargetPc = 0x00103000u;
    constexpr uint32_t kNotAFunctionPc = 0x00104000u;

    uint32_t g_elfTargetRuns = 0u;
    uint32_t g_patchedTargetRuns = 0u;
    uint32_t g_patchedTargetA0 = 0u;

    void elfTargetFunction(uint8_t *, R5900Context *, PS2Runtime *)
    {
        ++g_elfTargetRuns;
    }

    void patchedTargetFunction(uint8_t *, R5900Context *ctx, PS2Runtime *)
    {
        ++g_patchedTargetRuns;
        g_patchedTargetA0 = ::getRegU32(ctx, 4);
    }

    void resetBranchCounters()
    {
        g_elfTargetRuns = 0u;
        g_patchedTargetRuns = 0u;
        g_patchedTargetA0 = 0u;
    }
}

void register_ps2_guest_code_tests()
{
    MiniTest::Case("PS2GuestCode", [](TestCase &tc)
    {
        tc.Run("a rewritten JAL calls the function the guest now names", [](TestCase &t)
        {
            std::vector<uint8_t> rdram(PS2_RAM_SIZE, 0u);
            PS2Runtime runtime;
            runtime.registerFunction(kElfTargetPc, &elfTargetFunction);
            runtime.registerFunction(kPatchedTargetPc, &patchedTargetFunction);
            resetBranchCounters();

            writeWord(rdram.data(), kSitePc, encodeJump(kOpcodeJal, kElfTargetPc));
            R5900Context ctx{};
            t.IsTrue(runtime.dispatchGuestBranch(rdram.data(), &ctx, kElfTargetPc, kSitePc, kSitePc + 8u,
                                                 PS2Runtime::GuestBranchKind::DirectCall, "JAL"),
                     "an unmodified JAL should return to its fallthrough");
            t.Equals(g_elfTargetRuns, 1u, "an unmodified JAL should call the ELF's target");

            // malloc's `jal _malloc_r`, pointed at the title's own allocator.
            writeWord(rdram.data(), kSitePc, encodeJump(kOpcodeJal, kPatchedTargetPc));
            setRegU32(ctx, 4, 0x1234u);
            t.IsTrue(runtime.dispatchGuestBranch(rdram.data(), &ctx, kElfTargetPc, kSitePc, kSitePc + 8u,
                                                 PS2Runtime::GuestBranchKind::DirectCall, "JAL"),
                     "a rewritten JAL should return to its fallthrough");
            t.Equals(g_patchedTargetRuns, 1u, "a rewritten JAL should call the target now in guest RAM");
            t.Equals(g_elfTargetRuns, 1u, "a rewritten JAL must not call the ELF's target");
            t.Equals(g_patchedTargetA0, 0x1234u, "the new target should get the caller's arguments");
            t.Equals(ctx.pc, kSitePc + 8u, "the call should resume at the fallthrough");
        });

        tc.Run("a rewritten J transfers to the function the guest now names", [](TestCase &t)
        {
            std::vector<uint8_t> rdram(PS2_RAM_SIZE, 0u);
            PS2Runtime runtime;
            runtime.registerFunction(kElfTargetPc, &elfTargetFunction);
            runtime.registerFunction(kPatchedTargetPc, &patchedTargetFunction);
            resetBranchCounters();

            // free's `j _free_r`, pointed at the title's own free.
            writeWord(rdram.data(), kSitePc, encodeJump(kOpcodeJ, kPatchedTargetPc));
            R5900Context ctx{};
            t.IsFalse(runtime.dispatchGuestBranch(rdram.data(), &ctx, kElfTargetPc, kSitePc, 0u,
                                                  PS2Runtime::GuestBranchKind::DirectJump, "J"),
                      "a J should hand the transfer to the dispatcher");
            t.Equals(ctx.pc, kPatchedTargetPc, "the dispatcher should continue at the target now in guest RAM");
            t.Equals(g_elfTargetRuns + g_patchedTargetRuns, 0u, "a J must not nest a call");
        });

        tc.Run("a rewrite recompiled code cannot follow stops the run", [](TestCase &t)
        {
            std::vector<uint8_t> rdram(PS2_RAM_SIZE, 0u);
            PS2Runtime runtime;
            runtime.registerFunction(kElfTargetPc, &elfTargetFunction);
            resetBranchCounters();

            const auto dispatchCall = [&]
            {
                R5900Context ctx{};
                (void)runtime.dispatchGuestBranch(rdram.data(), &ctx, kElfTargetPc, kSitePc, kSitePc + 8u,
                                                  PS2Runtime::GuestBranchKind::DirectCall, "JAL");
            };

            writeWord(rdram.data(), kSitePc, 0u);
            expectThrow<std::logic_error>(
                t, dispatchCall,
                {"[smc:unhonoured] JAL at 0x00101000", "was 0x0C040800 in the ELF and is now 0x00000000"},
                "a JAL rewritten to a NOP");
            writeWord(rdram.data(), kSitePc, encodeJump(kOpcodeJal, kNotAFunctionPc));
            expectThrow<std::logic_error>(
                t, dispatchCall,
                {"[smc:unhonoured] JAL at 0x00101000", "is now 0x0C041000"},
                "a JAL rewritten to an address that starts no recompiled function");
            writeWord(rdram.data(), kSitePc, encodeJump(kOpcodeJ, kElfTargetPc));
            expectThrow<std::logic_error>(
                t, dispatchCall,
                {"[smc:unhonoured] JAL at 0x00101000", "is now 0x08040800"},
                "a JAL rewritten to a J");
            t.Equals(g_elfTargetRuns, 0u, "no stale code may run after a rewrite that cannot be followed");
        });

        tc.Run("FlushCache that invalidates the I-cache reports each rewritten code word once", [](TestCase &t)
        {
            constexpr uint32_t kCodeBase = 0x00100000u;
            std::vector<uint32_t> code(0x40u, 0u);
            code[0] = encodeJump(kOpcodeJal, kCodeBase + 0x80u);
            code[2] = 0x24020001u; // addiu $v0, $zero, 1
            code[3] = 0x03E00008u; // jr $ra
            const std::vector<uint8_t> elf = makeCodeElf(kCodeBase, code);

            const std::filesystem::path elfPath =
                std::filesystem::temp_directory_path() /
                ("ps2recomp_guest_code_" + std::to_string(reinterpret_cast<std::uintptr_t>(&t)) + ".elf");
            {
                std::ofstream file(elfPath, std::ios::binary);
                file.write(reinterpret_cast<const char *>(elf.data()), static_cast<std::streamsize>(elf.size()));
            }

            const PS2Runtime::IoPaths oldPaths = PS2Runtime::getIoPaths();
            PS2Runtime runtime;
            t.IsTrue(runtime.memory().initialize(), "runtime memory initialize should succeed");
            const bool loaded = runtime.loadELF(elfPath.string());
            PS2Runtime::setIoPaths(oldPaths);
            std::error_code removeError;
            std::filesystem::remove(elfPath, removeError);
            t.IsTrue(loaded, "the test ELF should load");

            uint8_t *rdram = runtime.memory().getRDRAM();
            R5900Context ctx{};
            const auto flushCache = [&](uint32_t mode)
            {
                setRegU32(ctx, 4, mode);
                FlushCache(rdram, &ctx, &runtime);
            };
            t.Equals(runtime.auditGuestCode(rdram, "test"), size_t{0u}, "the loaded image has no rewritten words");

            writeWord(rdram, kCodeBase, encodeJump(kOpcodeJal, kCodeBase + 0xC0u));
            flushCache(0u); // WRITEBACK_DCACHE
            t.Equals(runtime.auditGuestCode(rdram, "test"), size_t{1u},
                     "writing back the D-cache should not audit, so the rewrite is still new");
            t.Equals(runtime.auditGuestCode(rdram, "test"), size_t{0u}, "a rewrite should be reported once");

            writeWord(rdram, kCodeBase + 8u, 0x24020002u);
            flushCache(2u); // INVALIDATE_ICACHE
            t.Equals(runtime.auditGuestCode(rdram, "test"), size_t{0u},
                     "invalidating the I-cache should already have reported the rewrite");

            writeWord(rdram, kCodeBase + 12u, 0x00000000u);
            flushCache(3u); // INVALIDATE_CACHE
            t.Equals(runtime.auditGuestCode(rdram, "test"), size_t{0u},
                     "invalidating every cache should already have reported the rewrite");

            writeWord(rdram, kCodeBase, encodeJump(kOpcodeJal, kCodeBase + 0xE0u));
            t.Equals(runtime.auditGuestCode(rdram, "test"), size_t{1u}, "a word rewritten again should be reported again");
        });
    });
}
