void sceCdRead(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t a0 = getRegU32(ctx, 4); // usually lbn
    const uint32_t a1 = getRegU32(ctx, 5); // usually sector count
    const uint32_t a2 = getRegU32(ctx, 6); // usually destination buffer

    struct CdReadArgs
    {
        uint32_t lbn = 0;
        uint32_t sectors = 0;
        uint32_t buf = 0;
        const char *tag = "";
    };

    auto clampReadBytes = [](uint32_t sectors, uint32_t offset) -> size_t
    {
        const uint64_t requested = static_cast<uint64_t>(sectors) * static_cast<uint64_t>(kCdSectorSize);
        if (requested == 0)
        {
            return 0;
        }

        const uint64_t maxBytes = static_cast<uint64_t>(PS2_RAM_SIZE - offset);
        const uint64_t clamped = std::min<uint64_t>(requested, maxBytes);
        return static_cast<size_t>(clamped);
    };

    auto tryRead = [&](const CdReadArgs &args) -> bool
    {
        const uint32_t offset = args.buf & PS2_RAM_MASK;
        const size_t bytes = clampReadBytes(args.sectors, offset);
        if (bytes == 0)
        {
            return true;
        }

        return readCdSectors(args.lbn, args.sectors, rdram + offset, bytes);
    };

    CdReadArgs selected{a0, a1, a2, "a0/a1/a2"};
    bool ok = tryRead(selected);

    if (!ok)
    {
        // Some game-side wrappers use a nonstandard register layout.
        // If primary decode does not resolve to a known LBN, try safe alternatives.
        constexpr uint32_t kMaxReasonableSectors = PS2_RAM_SIZE / kCdSectorSize;
        if (!isResolvableCdLbn(selected.lbn))
        {
            const std::array<CdReadArgs, 5> alternatives = {
                CdReadArgs{a2, a1, a0, "a2/a1/a0"},
                CdReadArgs{a0, a2, a1, "a0/a2/a1"},
                CdReadArgs{a1, a0, a2, "a1/a0/a2"},
                CdReadArgs{a1, a2, a0, "a1/a2/a0"},
                CdReadArgs{a2, a0, a1, "a2/a0/a1"}};

            for (const CdReadArgs &candidate : alternatives)
            {
                if (candidate.sectors > kMaxReasonableSectors)
                {
                    continue;
                }
                if (!isResolvableCdLbn(candidate.lbn))
                {
                    continue;
                }

                if (tryRead(candidate))
                {
                    static uint32_t recoverLogCount = 0;
                    if (recoverLogCount < 16)
                    {
                        std::cout << "[sceCdRead] recovered with alternate args " << candidate.tag
                                  << " (pc=0x" << std::hex << ctx->pc
                                  << " ra=0x" << getRegU32(ctx, 31)
                                  << " a0=0x" << a0
                                  << " a1=0x" << a1
                                  << " a2=0x" << a2 << std::dec << ")" << std::endl;
                        ++recoverLogCount;
                    }
                    selected = candidate;
                    ok = true;
                    break;
                }
            }
        }

        if (!ok)
        {
            const uint32_t offset = a2 & PS2_RAM_MASK;
            const size_t bytes = clampReadBytes(a1, offset);
            if (bytes > 0)
            {
                std::memset(rdram + offset, 0, bytes);
            }

            static uint32_t unresolvedLogCount = 0;
            if (unresolvedLogCount < 32)
            {
                std::cerr << "[sceCdRead] unresolved request pc=0x" << std::hex << ctx->pc
                          << " ra=0x" << getRegU32(ctx, 31)
                          << " a0=0x" << a0
                          << " a1=0x" << a1
                          << " a2=0x" << a2 << std::dec << std::endl;
                ++unresolvedLogCount;
            }
        }
    }

    if (ok)
    {
        g_cdStreamingLbn = selected.lbn + selected.sectors;
        setReturnS32(ctx, 1); // command accepted/success
        return;
    }

    setReturnS32(ctx, 0);
}

void sceCdSync(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    setReturnS32(ctx, 0); // 0 = completed/not busy
}

void sceCdGetError(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    setReturnS32(ctx, g_lastCdError);
}

void builtin_set_imask(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    static int logCount = 0;
    if (logCount < 8)
    {
        std::cout << "ps2_stub builtin_set_imask" << std::endl;
        ++logCount;
    }
    setReturnS32(ctx, 0);
}

void InitThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    static int logCount = 0;
    if (logCount < 8)
    {
        std::cout << "ps2_stub InitThread" << std::endl;
        ++logCount;
    }
    setReturnS32(ctx, 1); // success
}