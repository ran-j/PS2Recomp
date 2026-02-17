void sceCdRead(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    uint32_t lbn = getRegU32(ctx, 4);     // $a0 - logical block number
    uint32_t sectors = getRegU32(ctx, 5); // $a1 - sector count
    uint32_t buf = getRegU32(ctx, 6);     // $a2 - destination buffer in RDRAM

    uint32_t offset = buf & PS2_RAM_MASK;
    size_t bytes = static_cast<size_t>(sectors) * kCdSectorSize;
    if (bytes > 0)
    {
        const size_t maxBytes = PS2_RAM_SIZE - offset;
        if (bytes > maxBytes)
        {
            bytes = maxBytes;
        }
    }

    uint8_t *dst = rdram + offset;
    bool ok = true;
    if (bytes > 0)
    {
        ok = readCdSectors(lbn, sectors, dst, bytes);
        if (!ok)
        {
            std::memset(dst, 0, bytes);
        }
    }

    if (ok)
    {
        g_cdStreamingLbn = lbn + sectors;
        setReturnS32(ctx, 1); // command accepted/success
    }
    else
    {
        setReturnS32(ctx, 0);
    }
}

void sceCdSync(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    setReturnS32(ctx, 0); // 0 = completed/not busy
}

void sceCdGetError(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    setReturnS32(ctx, g_lastCdError);
}

void _builtin_set_imask(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    static int logCount = 0;
    if (logCount < 8)
    {
        std::cout << "ps2_stub _builtin_set_imask" << std::endl;
        ++logCount;
    }
    setReturnS32(ctx, 0);
}
