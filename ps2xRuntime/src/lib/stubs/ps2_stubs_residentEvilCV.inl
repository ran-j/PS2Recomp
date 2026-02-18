void syRtcInit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    static int logCount = 0;
    if (logCount < 8)
    {
        std::cout << "ps2_stub syRtcInit" << std::endl;
        ++logCount;
    }
    setReturnS32(ctx, 0);
}

void syFree(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    static int logCount = 0;
    if (logCount < 8)
    {
        std::cout << "ps2_stub syFree" << std::endl;
        ++logCount;
    }

    const uint32_t guestAddr = getRegU32(ctx, 4); // $a0
    if (runtime && guestAddr != 0u)
    {
        runtime->guestFree(guestAddr);
    }

    setReturnS32(ctx, 0);
}

void syMalloc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t requestedSize = getRegU32(ctx, 4); // $a0
    uint32_t resultAddr = 0u;

    if (runtime && requestedSize != 0u)
    {
        // Match game expectation for allocator alignment while keeping pointers in EE RAM.
        resultAddr = runtime->guestMalloc(requestedSize, 64u);
    }

    static int logCount = 0;
    if (logCount < 16)
    {
        std::cout << "ps2_stub syMalloc"
                  << " size=0x" << std::hex << requestedSize
                  << " -> 0x" << resultAddr
                  << std::dec << std::endl;
        ++logCount;
    }

    setReturnU32(ctx, resultAddr);
}

void InitSdcParameter(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    static int logCount = 0;
    if (logCount < 8)
    {
        std::cout << "ps2_stub InitSdcParameter" << std::endl;
        ++logCount;
    }
    setReturnS32(ctx, 0);
}

void Ps2_pad_actuater(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    static int logCount = 0;
    if (logCount < 8)
    {
        std::cout << "ps2_stub Ps2_pad_actuater" << std::endl;
        ++logCount;
    }
    setReturnS32(ctx, 0);
}

void syMallocInit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    static int logCount = 0;
    if (runtime)
    {
        const uint32_t heapBase = getRegU32(ctx, 4); // $a0
        const uint32_t heapSize = getRegU32(ctx, 5); // $a1 (optional size)

        constexpr uint32_t kHeapBaseFloor = 0x00100000u;
        uint32_t normalizedBase = heapBase;
        if (normalizedBase >= 0x80000000u && normalizedBase < 0xC0000000u)
        {
            normalizedBase &= 0x1FFFFFFFu;
        }
        else if (normalizedBase >= PS2_RAM_SIZE)
        {
            normalizedBase &= PS2_RAM_MASK;
        }

        const bool suspiciousKsegBase = (heapBase & 0xE0000000u) == 0x80000000u && normalizedBase < kHeapBaseFloor;
        if (normalizedBase == 0u || suspiciousKsegBase)
        {
            // Keep the ELF-driven suggestion instead of collapsing heap to low memory.
            normalizedBase = runtime->guestHeapBase();
        }

        // Treat absurd "size" values as unspecified limit.
        uint32_t heapLimit = 0u;
        if (heapSize != 0u && heapSize <= PS2_RAM_SIZE && normalizedBase < PS2_RAM_SIZE)
        {
            const uint64_t candidateLimit = static_cast<uint64_t>(normalizedBase) + static_cast<uint64_t>(heapSize);
            heapLimit = static_cast<uint32_t>(std::min<uint64_t>(candidateLimit, PS2_RAM_SIZE));
        }
        runtime->configureGuestHeap(normalizedBase, heapLimit);
        if (logCount < 8)
        {
            std::cout << "ps2_stub syMallocInit"
                      << " reqBase=0x" << std::hex << heapBase
                      << " reqSize=0x" << heapSize
                      << " normBase=0x" << normalizedBase
                      << " reqLimit=0x" << heapLimit
                      << " finalBase=0x" << runtime->guestHeapBase()
                      << " finalEnd=0x" << runtime->guestHeapEnd()
                      << std::dec << std::endl;
            ++logCount;
        }
    }
    else if (logCount < 8)
    {
        std::cout << "ps2_stub syMallocInit" << std::endl;
        ++logCount;
    }

    setReturnS32(ctx, 0);
}

void syHwInit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    static int logCount = 0;
    if (logCount < 8)
    {
        std::cout << "ps2_stub syHwInit" << std::endl;
        ++logCount;
    }
    setReturnS32(ctx, 0);
}

void syHwInit2(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    static int logCount = 0;
    if (logCount < 8)
    {
        std::cout << "ps2_stub syHwInit2" << std::endl;
        ++logCount;
    }
    setReturnS32(ctx, 0);
}

void InitGdSystemEx(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    static int logCount = 0;
    if (logCount < 8)
    {
        std::cout << "ps2_stub InitGdSystemEx" << std::endl;
        ++logCount;
    }
    setReturnS32(ctx, 0);
}

void pdInitPeripheral(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    static int logCount = 0;
    if (logCount < 8)
    {
        std::cout << "ps2_stub pdInitPeripheral" << std::endl;
        ++logCount;
    }
    setReturnS32(ctx, 0);
}

void pdGetPeripheral(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    static int logCount = 0;
    if (logCount < 8)
    {
        std::cout << "ps2_stub pdGetPeripheral" << std::endl;
        ++logCount;
    }
    setReturnS32(ctx, 0);
}

void Ps2SwapDBuff(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    static int logCount = 0;
    if (logCount < 8)
    {
        std::cout << "ps2_stub Ps2SwapDBuff" << std::endl;
        ++logCount;
    }
    setReturnS32(ctx, 0);
}

void InitReadKeyEx(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    static int logCount = 0;
    if (logCount < 8)
    {
        std::cout << "ps2_stub InitReadKeyEx" << std::endl;
        ++logCount;
    }
    setReturnS32(ctx, 0);
}

void SetRepeatKeyTimer(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    static int logCount = 0;
    if (logCount < 8)
    {
        std::cout << "ps2_stub SetRepeatKeyTimer" << std::endl;
        ++logCount;
    }
    setReturnS32(ctx, 0);
}

void StopFxProgram(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    static int logCount = 0;
    if (logCount < 8)
    {
        std::cout << "ps2_stub StopFxProgram" << std::endl;
        ++logCount;
    }
    setReturnS32(ctx, 0);
}

void sndr_trans_func(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    static int logCount = 0;
    if (logCount < 8)
    {
        std::cout << "ps2_stub sndr_trans_func (noop)" << std::endl;
        ++logCount;
    }

    // For now just clear the snd busy flag used by sdMultiUnitDownload/SysServer loops.
    constexpr uint32_t kSndBusyAddr = 0x01E0E170;
    if (rdram)
    {
        uint32_t offset = kSndBusyAddr & PS2_RAM_MASK;
        if (offset + sizeof(uint32_t) <= PS2_RAM_SIZE)
        {
            *reinterpret_cast<uint32_t *>(rdram + offset) = 0;
        }
    }

    setReturnS32(ctx, 0);
}

void sdDrvInit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    constexpr uint32_t kSdrInitAddr = 0x2E9A20u;

    static int logCount = 0;
    if (logCount < 8)
    {
        std::cout << "ps2_stub sdDrvInit -> SdrInit_0x2e9a20" << std::endl;
        ++logCount;
    }

    if (!runtime || !ctx || !rdram || !runtime->hasFunction(kSdrInitAddr))
    {
        setReturnS32(ctx, 0);
        return;
    }

    const uint32_t returnPc = getRegU32(ctx, 31);
    PS2Runtime::RecompiledFunction sdrInit = runtime->lookupFunction(kSdrInitAddr);
    ctx->pc = kSdrInitAddr;
    sdrInit(rdram, ctx, runtime);

    if (ctx->pc == kSdrInitAddr || ctx->pc == 0u)
    {
        ctx->pc = returnPc;
    }
}

void ADXF_LoadPartitionNw(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    static int logCount = 0;
    if (logCount < 8)
    {
        std::cout << "ps2_stub ADXF_LoadPartitionNw (noop)" << std::endl;
        ++logCount;
    }
    // Return success to keep the ADX partition setup moving.
    setReturnS32(ctx, 0);
}

void sdSndStopAll(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    static int logCount = 0;
    if (logCount < 8)
    {
        std::cout << "ps2_stub sdSndStopAll" << std::endl;
        ++logCount;
    }
    setReturnS32(ctx, 0);
}

void sdSysFinish(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    static int logCount = 0;
    if (logCount < 8)
    {
        std::cout << "ps2_stub sdSysFinish" << std::endl;
        ++logCount;
    }
    setReturnS32(ctx, 0);
}

void ADXT_Init(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    static int logCount = 0;
    if (logCount < 8)
    {
        std::cout << "ps2_stub ADXT_Init" << std::endl;
        ++logCount;
    }
    setReturnS32(ctx, 0);
}

void ADXT_SetNumRetry(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    static int logCount = 0;
    if (logCount < 8)
    {
        std::cout << "ps2_stub ADXT_SetNumRetry" << std::endl;
        ++logCount;
    }
    setReturnS32(ctx, 0);
}

void cvFsSetDefDev(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    static int logCount = 0;
    if (logCount < 8)
    {
        std::cout << "ps2_stub cvFsSetDefDev" << std::endl;
        ++logCount;
    }
    setReturnS32(ctx, 0);
}

void mcCallMessageTypeSe(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("mcCallMessageTypeSe", rdram, ctx, runtime);
}

void mcCheckReadStartConfigFile(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("mcCheckReadStartConfigFile", rdram, ctx, runtime);
}

void mcCheckReadStartSaveFile(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("mcCheckReadStartSaveFile", rdram, ctx, runtime);
}

void mcCheckWriteStartConfigFile(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("mcCheckWriteStartConfigFile", rdram, ctx, runtime);
}

void mcCheckWriteStartSaveFile(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("mcCheckWriteStartSaveFile", rdram, ctx, runtime);
}

void mcCreateConfigInit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("mcCreateConfigInit", rdram, ctx, runtime);
}

void mcCreateFileSelectWindow(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("mcCreateFileSelectWindow", rdram, ctx, runtime);
}

void mcCreateIconInit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("mcCreateIconInit", rdram, ctx, runtime);
}

void mcCreateSaveFileInit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("mcCreateSaveFileInit", rdram, ctx, runtime);
}

void mcDispFileName(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("mcDispFileName", rdram, ctx, runtime);
}

void mcDispFileNumber(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("mcDispFileNumber", rdram, ctx, runtime);
}

void mcDisplayFileSelectWindow(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("mcDisplayFileSelectWindow", rdram, ctx, runtime);
}

void mcDisplaySelectFileInfo(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("mcDisplaySelectFileInfo", rdram, ctx, runtime);
}

void mcDisplaySelectFileInfoMesCount(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("mcDisplaySelectFileInfoMesCount", rdram, ctx, runtime);
}

void mcDispWindowCurSol(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("mcDispWindowCurSol", rdram, ctx, runtime);
}

void mcDispWindowFoundtion(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("mcDispWindowFoundtion", rdram, ctx, runtime);
}

void mceGetInfoApdx(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("mceGetInfoApdx", rdram, ctx, runtime);
}

void mceIntrReadFixAlign(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("mceIntrReadFixAlign", rdram, ctx, runtime);
}

void mceStorePwd(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("mceStorePwd", rdram, ctx, runtime);
}

void mcGetConfigCapacitySize(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("mcGetConfigCapacitySize", rdram, ctx, runtime);
}

void mcGetFileSelectWindowCursol(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("mcGetFileSelectWindowCursol", rdram, ctx, runtime);
}

void mcGetFreeCapacitySize(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("mcGetFreeCapacitySize", rdram, ctx, runtime);
}

void mcGetIconCapacitySize(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("mcGetIconCapacitySize", rdram, ctx, runtime);
}

void mcGetIconFileCapacitySize(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("mcGetIconFileCapacitySize", rdram, ctx, runtime);
}

void mcGetPortSelectDirInfo(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("mcGetPortSelectDirInfo", rdram, ctx, runtime);
}

void mcGetSaveFileCapacitySize(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("mcGetSaveFileCapacitySize", rdram, ctx, runtime);
}

void mcGetStringEnd(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("mcGetStringEnd", rdram, ctx, runtime);
}

void mcMoveFileSelectWindowCursor(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("mcMoveFileSelectWindowCursor", rdram, ctx, runtime);
}

void mcNewCreateConfigFile(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("mcNewCreateConfigFile", rdram, ctx, runtime);
}

void mcNewCreateIcon(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("mcNewCreateIcon", rdram, ctx, runtime);
}

void mcNewCreateSaveFile(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("mcNewCreateSaveFile", rdram, ctx, runtime);
}

void mcReadIconData(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("mcReadIconData", rdram, ctx, runtime);
}

void mcReadStartConfigFile(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("mcReadStartConfigFile", rdram, ctx, runtime);
}

void mcReadStartSaveFile(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("mcReadStartSaveFile", rdram, ctx, runtime);
}

void mcSelectFileInfoInit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("mcSelectFileInfoInit", rdram, ctx, runtime);
}

void mcSelectSaveFileCheck(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("mcSelectSaveFileCheck", rdram, ctx, runtime);
}

void mcSetFileSelectWindowCursol(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("mcSetFileSelectWindowCursol", rdram, ctx, runtime);
}

void mcSetFileSelectWindowCursolInit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("mcSetFileSelectWindowCursolInit", rdram, ctx, runtime);
}

void mcSetStringSaveFile(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("mcSetStringSaveFile", rdram, ctx, runtime);
}

void mcSetTyepWriteMode(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("mcSetTyepWriteMode", rdram, ctx, runtime);
}

void mcWriteIconData(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("mcWriteIconData", rdram, ctx, runtime);
}

void mcWriteStartConfigFile(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("mcWriteStartConfigFile", rdram, ctx, runtime);
}

void mcWriteStartSaveFile(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("mcWriteStartSaveFile", rdram, ctx, runtime);
}
