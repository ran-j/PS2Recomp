#include "game_overrides.h"
#include "ps2_runtime.h"
// TEMP: experimental test override for SLPS_204.14 (Taiko no Tatsujin JP)
// investigating an infinite busy-wait polling *(0x322880) at 0x1d2600.
// Force that getter to return 1 to see if the game proceeds past the wait.
// Remove or replace once the real signaling mechanism is understood.
namespace
{
    void ApplyTaikoInvestigationOverride(PS2Runtime &runtime)
    {
        // CONFIRMED still needed (tested 2026-07-21 session 4): even with the SIF
        // WaitSema completion fix in place, thread 2 (entry=0x1d0660) still never runs
        // and *(0x322880) is never set naturally. Disabling this override reproduces the
        // original hang, just slightly later (mid-way through the SIF handshake instead
        // of before it). Keep this in place.
        ps2_game_overrides::bindAddressHandler(runtime, 0x1D2600u, "ret1");

        // EXPERIMENTAL (2026-07-22 overnight session, attempt 2): overriding FUN_001894d0
        // (0x1894d0) to skip the whole "subsystem 7" chain did NOT stop the game from halting
        // at 0x104a40 -- live capture showed identical behavior, meaning the call chain
        // (gap_00100548 -> FUN_00100a68(a1=1) -> FUN_00189410(a0=7) -> FUN_001894d0) traced
        // this session is apparently NOT the actual path reaching sub_00104638 (or there's a
        // second, separate path). Reverted that attempt. Instead, directly override
        // sub_00104638 itself -- the function whose negative return (-1) is what the caller at
        // 0x104a1c checks (`bgezl $v0,0x104780`) before falling into the fatal `b $+0` loop at
        // 0x104a40. Forcing it to return 0 (success) unconditionally means whatever tried to
        // decompress a TIM2 texture from an empty/uninitialized source buffer will "succeed"
        // with garbage/zeroed output instead of halting -- likely a corrupted or blank texture
        // rather than correct graphics, but should let execution continue past this point
        // instead of hanging forever, which is strictly more informative for continued
        // investigation. Not a real fix -- purely a diagnostic probe to see what's downstream.
        ps2_game_overrides::bindAddressHandler(runtime, 0x104638u, "ret0");
    }
}
PS2_REGISTER_GAME_OVERRIDE("TaikoInvestigation_ForceFlag322880", "SLPS_204.14.elf", 0x100134u, 0u, ApplyTaikoInvestigationOverride);
#include "ps2_runtime_calls.h"
#include "ps2_stubs.h"
#include "ps2_syscalls.h"
#include "ps2_log.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <optional>
#include <vector>

namespace
{
    std::mutex &registryMutex()
    {
        static std::mutex mutex;
        return mutex;
    }

    std::vector<ps2_game_overrides::Descriptor> &descriptorRegistry()
    {
        static std::vector<ps2_game_overrides::Descriptor> registry;
        return registry;
    }

    bool equalsIgnoreCaseAscii(std::string_view lhs, std::string_view rhs)
    {
        if (lhs.size() != rhs.size())
        {
            return false;
        }

        for (size_t i = 0; i < lhs.size(); ++i)
        {
            const auto l = static_cast<unsigned char>(lhs[i]);
            const auto r = static_cast<unsigned char>(rhs[i]);
            if (std::tolower(l) != std::tolower(r))
            {
                return false;
            }
        }

        return true;
    }

    std::string basenameFromPath(const std::string &path)
    {
        std::error_code ec;
        const std::filesystem::path fsPath(path);
        const std::filesystem::path leaf = fsPath.filename();
        if (leaf.empty())
        {
            return path;
        }
        return leaf.string();
    }

    std::optional<PS2Runtime::RecompiledFunction> resolveHandlerByName(std::string_view handlerName)
    {
        const std::string_view resolvedSyscall = ps2_runtime_calls::resolveSyscallName(handlerName);
        if (!resolvedSyscall.empty())
        {
#define PS2_RESOLVE_SYSCALL(name)                   \
    if (resolvedSyscall == std::string_view{#name}) \
    {                                               \
        return &ps2_syscalls::name;                 \
    }
            PS2_SYSCALL_LIST(PS2_RESOLVE_SYSCALL)
#undef PS2_RESOLVE_SYSCALL
        }

        const std::string_view resolvedStub = ps2_runtime_calls::resolveStubName(handlerName);
        if (!resolvedStub.empty())
        {
#define PS2_RESOLVE_STUB(name)                   \
    if (resolvedStub == std::string_view{#name}) \
    {                                            \
        return &ps2_stubs::name;                 \
    }
            PS2_STUB_LIST(PS2_RESOLVE_STUB)
#undef PS2_RESOLVE_STUB
        }

        return std::nullopt;
    }
}
namespace ps2_game_overrides
{
    AutoRegister::AutoRegister(const Descriptor &descriptor)
    {
        registerDescriptor(descriptor);
    }

    void registerDescriptor(const Descriptor &descriptor)
    {
        if (!descriptor.apply)
        {
            std::cerr << "[game_overrides] ignoring descriptor with null apply callback." << std::endl;
            return;
        }

        std::lock_guard<std::mutex> lock(registryMutex());
        descriptorRegistry().push_back(descriptor);
    }

    bool bindAddressHandler(PS2Runtime &runtime, uint32_t address, std::string_view handlerName)
    {
        const auto resolved = resolveHandlerByName(handlerName);
        if (!resolved.has_value())
        {
            std::cerr << "[game_overrides] unresolved handler '" << handlerName
                      << "' for address 0x" << std::hex << address << std::dec << std::endl;
            return false;
        }

        return runtime.replaceFunction(address, resolved.value());
    }

    void applyMatching(PS2Runtime &runtime,
                       const std::string &elfPath,
                       uint32_t entry,
                       uint32_t fileCrc32,
                       bool fileCrcValid)
    {

        std::vector<Descriptor> descriptors;
        {
            std::lock_guard<std::mutex> lock(registryMutex());
            descriptors = descriptorRegistry();
        }

        if (descriptors.empty())
        {
            return;
        }

        const std::string elfName = basenameFromPath(elfPath);
        size_t appliedCount = 0;
        for (const Descriptor &descriptor : descriptors)
        {
            if (!descriptor.apply)
            {
                continue;
            }

            if (descriptor.elfName && descriptor.elfName[0] != '\0')
            {
                if (!equalsIgnoreCaseAscii(descriptor.elfName, elfName))
                {
                    continue;
                }
            }

            if (descriptor.entry != 0u && descriptor.entry != entry)
            {
                continue;
            }

            if (descriptor.crc32 != 0u)
            {
                if (!fileCrcValid || fileCrc32 != descriptor.crc32)
                {
                    continue;
                }
            }

            const char *name = (descriptor.name && descriptor.name[0] != '\0')
                                   ? descriptor.name
                                   : "unnamed";
            RUNTIME_LOG("[game_overrides] applying '" << name << "'");
            descriptor.apply(runtime);
            ++appliedCount;
        }

        if (appliedCount > 0)
        {
            RUNTIME_LOG("[game_overrides] applied " << appliedCount << " matching override(s).");
        }
    }
}
