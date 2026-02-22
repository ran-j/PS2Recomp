#ifndef PS2_GAME_OVERRIDES_H
#define PS2_GAME_OVERRIDES_H

#include <cstdint>
#include <string>
#include <string_view>

class PS2Runtime;

namespace ps2_game_overrides
{
    using ApplyFn = void (*)(PS2Runtime &runtime);

    struct Descriptor
    {
        const char *name = "";
        const char *elfName = "";
        uint32_t entry = 0;
        uint32_t crc32 = 0;
        ApplyFn apply = nullptr;
    };

    class AutoRegister
    {
    public:
        explicit AutoRegister(const Descriptor &descriptor);
    };

    void registerDescriptor(const Descriptor &descriptor);
    void applyMatching(PS2Runtime &runtime, const std::string &elfPath, uint32_t entry);

    // Runtime helper for game-specific modules:
    // bind an address to an existing syscall/stub handler by name.
    bool bindAddressHandler(PS2Runtime &runtime, uint32_t address, std::string_view handlerName);
}

#define PS2_GAME_OVERRIDE_JOIN2(a, b) a##b
#define PS2_GAME_OVERRIDE_JOIN(a, b) PS2_GAME_OVERRIDE_JOIN2(a, b)

#define PS2_REGISTER_GAME_OVERRIDE(NAME_LITERAL, ELF_NAME_LITERAL, ENTRY_U32, CRC32_U32, APPLY_FN)      \
    namespace                                                                                             \
    {                                                                                                     \
        static const ps2_game_overrides::Descriptor PS2_GAME_OVERRIDE_JOIN(kPs2OverrideDesc_, __LINE__) \
        {                                                                                                 \
            NAME_LITERAL,                                                                                 \
            ELF_NAME_LITERAL,                                                                             \
            static_cast<uint32_t>(ENTRY_U32),                                                             \
            static_cast<uint32_t>(CRC32_U32),                                                             \
            APPLY_FN                                                                                      \
        };                                                                                                \
        static const ps2_game_overrides::AutoRegister PS2_GAME_OVERRIDE_JOIN(kPs2OverrideReg_, __LINE__)\
            (PS2_GAME_OVERRIDE_JOIN(kPs2OverrideDesc_, __LINE__));                                       \
    }

#endif // PS2_GAME_OVERRIDES_H
