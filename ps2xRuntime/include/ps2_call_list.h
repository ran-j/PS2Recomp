#pragma once

// I know ugly, but will work for now.

#define PS2_SYSCALL_LIST(X)   \
    X(FlushCache)             \
    X(ResetEE)                \
    X(SetMemoryMode)          \
                              \
    X(CreateThread)           \
    X(DeleteThread)           \
    X(StartThread)            \
    X(ExitThread)             \
    X(ExitDeleteThread)       \
    X(TerminateThread)        \
    X(SuspendThread)          \
    X(ResumeThread)           \
    X(GetThreadId)            \
    X(ReferThreadStatus)      \
    X(SleepThread)            \
    X(WakeupThread)           \
    X(iWakeupThread)          \
    X(ChangeThreadPriority)   \
    X(RotateThreadReadyQueue) \
    X(ReleaseWaitThread)      \
    X(iReleaseWaitThread)     \
                              \
    X(CreateSema)             \
    X(DeleteSema)             \
    X(SignalSema)             \
    X(iSignalSema)            \
    X(WaitSema)               \
    X(PollSema)               \
    X(iPollSema)              \
    X(ReferSemaStatus)        \
    X(iReferSemaStatus)       \
                              \
    X(CreateEventFlag)        \
    X(DeleteEventFlag)        \
    X(SetEventFlag)           \
    X(iSetEventFlag)          \
    X(ClearEventFlag)         \
    X(iClearEventFlag)        \
    X(WaitEventFlag)          \
    X(PollEventFlag)          \
    X(iPollEventFlag)         \
    X(ReferEventFlagStatus)   \
    X(iReferEventFlagStatus)  \
                              \
    X(SetAlarm)               \
    X(iSetAlarm)              \
    X(CancelAlarm)            \
    X(iCancelAlarm)           \
                              \
    X(EnableIntc)             \
    X(DisableIntc)            \
    X(EnableDmac)             \
    X(DisableDmac)            \
                              \
    X(SifStopModule)          \
    X(SifLoadModule)          \
    X(SifInitRpc)             \
    X(SifBindRpc)             \
    X(SifCallRpc)             \
    X(SifRegisterRpc)         \
    X(SifCheckStatRpc)        \
    X(SifSetRpcQueue)         \
    X(SifRemoveRpcQueue)      \
    X(SifRemoveRpc)           \
    X(sceSifCallRpc)          \
    X(sceSifSendCmd)          \
    X(_sceRpcGetPacket)       \
                              \
    X(fioOpen)                \
    X(fioClose)               \
    X(fioRead)                \
    X(fioWrite)               \
    X(fioLseek)               \
    X(fioMkdir)               \
    X(fioChdir)               \
    X(fioRmdir)               \
    X(fioGetstat)             \
    X(fioRemove)              \
                              \
    X(GsSetCrt)               \
    X(GsGetIMR)               \
    X(GsPutIMR)               \
    X(GsSetVideoMode)         \
                              \
    X(GetOsdConfigParam)      \
    X(SetOsdConfigParam)      \
    X(GetRomName)             \
    X(SifLoadElfPart)         \
    X(sceSifLoadModule)       \
                              \
    X(SetupThread)            \
    X(QueryBootMode)          \
    X(GetThreadTLS)           \
    X(RegisterExitHandler)

// Stubs
#define PS2_STUB_LIST(X) \
    X(malloc)            \
    X(free)              \
    X(calloc)            \
    X(realloc)           \
    X(memcpy)            \
    X(memset)            \
    X(memmove)           \
    X(memcmp)            \
                         \
    X(strcpy)            \
    X(strncpy)           \
    X(strlen)            \
    X(strcmp)            \
    X(strncmp)           \
    X(strcat)            \
    X(strncat)           \
    X(strchr)            \
    X(strrchr)           \
    X(strstr)            \
                         \
    X(printf)            \
    X(sprintf)           \
    X(snprintf)          \
    X(puts)              \
    X(fopen)             \
    X(fclose)            \
    X(fread)             \
    X(fwrite)            \
    X(fprintf)           \
    X(fseek)             \
    X(ftell)             \
    X(fflush)            \
                         \
    X(sqrt)              \
    X(sin)               \
    X(cos)               \
    X(tan)               \
    X(atan2)             \
    X(pow)               \
    X(exp)               \
    X(log)               \
    X(log10)             \
    X(ceil)              \
    X(floor)             \
    X(fabs)              \
    X(sceCdRead)         \
    X(sceCdSync)         \
    X(sceCdGetError)     \
    X(njSetBorderColor)  \
    X(njSetTextureMemorySize) \
    X(njInitVertexBuffer) \
    X(njTextureShadingMode) \
    X(njInitView)        \
    X(njSetAspect)       \
    X(njInitSystem)      \
    X(njInitPrint)       \
    X(njPolygonCullingMode) \
    X(njSetView)         \
    X(njGetMatrix)       \
    X(njInitTexture)     \
    X(njInitTextureBuffer) \
    X(njSetPaletteMode)  \
    X(njClipZ)           \
    X(syRtcInit)         \
    X(_builtin_set_imask) \
    X(syFree)            \
    X(InitSdcParameter)  \
    X(Ps2_pad_actuater)  \
    X(syMallocInit)      \
    X(syHwInit)          \
    X(syHwInit2)         \
    X(InitGdSystemEx)    \
    X(pdInitPeripheral)  \
    X(njSetVertexBuffer) \
    X(njPrintSize)       \
    X(pdGetPeripheral)   \
    X(Ps2SwapDBuff)      \
    X(InitReadKeyEx)     \
    X(SetRepeatKeyTimer) \
    X(StopFxProgram)     \
    X(sdSndStopAll)      \
    X(sdSysFinish)       \
    X(ADXT_Init)         \
    X(ADXT_SetNumRetry)  \
    X(cvFsSetDefDev)     \
    X(sndr_trans_func)    \
    X(sdDrvInit)         \
    X(ADXF_LoadPartitionNw)