// Exports PS2Recomp TOML config (+ optional CSV) from Ghidra
// @category PS2Recomp

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.address.AddressSetView;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.RefType;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;
import ghidra.program.model.symbol.SymbolType;

import java.io.File;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.Comparator;
import java.util.HashSet;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Set;
import java.util.regex.Pattern;

public class ExportPS2Functions extends GhidraScript {

    // Names and values mirror ps2recomp::MipsOpcodes/SpecialFunctions/GprRegisters.
    // would be amazing cmake create this script coping the register from the header file
    private static final int MIPS_INSTRUCTION_SIZE = 4;
    private static final int MIPS_IMMEDIATE_BITS = 16;
    private static final int MIPS_IMMEDIATE_SIGN_BIT = 0x8000;
    private static final long UINT32_MASK = 0xFFFFFFFFL;
    private static final long OPCODE_MASK = 0x3FL;
    private static final long REGISTER_MASK = 0x1FL;
    private static final long IMMEDIATE_MASK = 0xFFFFL;
    private static final int OPCODE_SHIFT = 26;
    private static final int RS_SHIFT = 21;
    private static final int RT_SHIFT = 16;
    private static final int RD_SHIFT = 11;

    private static final int GPR_ZERO = 0;
    private static final int GPR_A0 = 4;
    private static final int GPR_A3 = 7;
    private static final int GPR_SP = 29;
    private static final int GPR_RA = 31;

    private static final int OPCODE_SPECIAL = 0x00;
    private static final int OPCODE_REGIMM = 0x01;
    private static final int OPCODE_J = 0x02;
    private static final int OPCODE_JAL = 0x03;
    private static final int OPCODE_BEQ = 0x04;
    private static final int OPCODE_BNE = 0x05;
    private static final int OPCODE_BLEZ = 0x06;
    private static final int OPCODE_BGTZ = 0x07;
    private static final int OPCODE_ADDI = 0x08;
    private static final int OPCODE_ADDIU = 0x09;
    private static final int OPCODE_SLTI = 0x0A;
    private static final int OPCODE_SLTIU = 0x0B;
    private static final int OPCODE_ANDI = 0x0C;
    private static final int OPCODE_ORI = 0x0D;
    private static final int OPCODE_XORI = 0x0E;
    private static final int OPCODE_LUI = 0x0F;
    private static final int OPCODE_BEQL = 0x14;
    private static final int OPCODE_BNEL = 0x15;
    private static final int OPCODE_BLEZL = 0x16;
    private static final int OPCODE_BGTZL = 0x17;
    private static final int OPCODE_DADDI = 0x18;
    private static final int OPCODE_DADDIU = 0x19;
    private static final int OPCODE_LDL = 0x1A;
    private static final int OPCODE_LDR = 0x1B;
    private static final int OPCODE_MMI = 0x1C;
    private static final int OPCODE_LQ = 0x1E;
    private static final int OPCODE_SQ = 0x1F;
    private static final int OPCODE_LB = 0x20;
    private static final int OPCODE_LH = 0x21;
    private static final int OPCODE_LWL = 0x22;
    private static final int OPCODE_LW = 0x23;
    private static final int OPCODE_LBU = 0x24;
    private static final int OPCODE_LHU = 0x25;
    private static final int OPCODE_LWR = 0x26;
    private static final int OPCODE_LWU = 0x27;
    private static final int OPCODE_SW = 0x2B;
    private static final int OPCODE_LL = 0x30;
    private static final int OPCODE_LLD = 0x34;
    private static final int OPCODE_LD = 0x37;
    private static final int OPCODE_SD = 0x3F;

    private static final int SPECIAL_JR = 0x08;
    private static final int SPECIAL_JALR = 0x09;

    // For now I have to copy all functions from the runtime handler list
    private static final Set<String> RUNTIME_HANDLER_NAMES = new HashSet<>(Arrays.asList(
        "FlushCache", "iFlushCache", "ResetEE", "SetMemoryMode", "InitThread", "CreateThread",
        "DeleteThread", "StartThread", "ExitThread", "ExitDeleteThread", "TerminateThread", "SuspendThread",
        "ResumeThread", "GetThreadId", "ReferThreadStatus", "iReferThreadStatus", "SleepThread", "WakeupThread",
        "iWakeupThread", "CancelWakeupThread", "iCancelWakeupThread", "ChangeThreadPriority", "iChangeThreadPriority", "RotateThreadReadyQueue",
        "iRotateThreadReadyQueue", "ReleaseWaitThread", "iReleaseWaitThread", "CreateSema", "DeleteSema", "SignalSema",
        "iSignalSema", "WaitSema", "PollSema", "iPollSema", "ReferSemaStatus", "iReferSemaStatus",
        "CreateEventFlag", "DeleteEventFlag", "SetEventFlag", "iSetEventFlag", "ClearEventFlag", "iClearEventFlag",
        "WaitEventFlag", "PollEventFlag", "iPollEventFlag", "ReferEventFlagStatus", "iReferEventFlagStatus", "InitAlarm",
        "SetAlarm", "iSetAlarm", "CancelAlarm", "iCancelAlarm", "ReleaseAlarm", "iReleaseAlarm",
        "AddIntcHandler", "AddIntcHandler2", "RemoveIntcHandler", "AddDmacHandler", "AddDmacHandler2", "RemoveDmacHandler",
        "EnableIntc", "iEnableIntc", "DisableIntc", "iDisableIntc", "EnableDmac", "iEnableDmac",
        "DisableDmac", "iDisableDmac", "SifStopModule", "SifLoadModule", "SifInitRpc", "SifBindRpc",
        "SifCallRpc", "SifRegisterRpc", "SifCheckStatRpc", "SifSetRpcQueue", "SifRemoveRpcQueue", "SifRemoveRpc",
        "sceSifCallRpc", "sceSifSendCmd", "sceRpcGetPacket", "fioOpen", "fioClose", "fioRead",
        "fioWrite", "fioLseek", "fioMkdir", "fioChdir", "fioRmdir", "fioGetstat",
        "fioRemove", "SetGsCrt", "GsSetCrt", "GsGetIMR", "iGsGetIMR", "GsPutIMR",
        "iGsPutIMR", "SetVSyncFlag", "SetSyscall", "GsSetVideoMode", "GetOsdConfigParam", "SetOsdConfigParam",
        "EnableCache", "DisableCache", "GetRomName", "SifLoadElfPart", "sceSifLoadElf", "sceSifLoadElfPart",
        "sceSifLoadModule", "sceSifLoadModuleBuffer", "SetupThread", "EndOfHeap", "GetMemorySize", "Deci2Call",
        "QueryBootMode", "GetThreadTLS", "Copy", "GetEntryAddress", "RegisterExitHandler", "ret0", "ret1", "reta0",
        "calloc_r", "free_r", "realloc_r", "memalign_r", "malloc_r", "malloc_extend_top", "malloc_trim_r", "mbtowc_r", "printf_r",
        "__malloc_lock", "__malloc_unlock",
        "abs", "__ieee754_rem_pio2f", "__kernel_cosf", "__kernel_sinf", "atan", "atan2",
        "calloc", "ceil", "close", "cos", "exit", "exp",
        "fabs", "fclose", "fflush", "floor", "fopen", "fprintf",
        "fread", "free", "fseek", "fstat", "ftell", "fwrite",
        "getpid", "log", "log10", "lseek", "malloc", "memalign", "memclr", "memchr",
        "memcmp", "memcpy", "memmove", "memset", "open", "pow",
        "printf", "puts", "rand", "read", "realloc", "sin",
        "snprintf", "sprintf", "sqrt", "srand", "stat", "strcasecmp",
        "strcat", "strchr", "strcmp", "strcpy", "strlen", "strncat",
        "strncmp", "strncpy", "strrchr", "strstr", "tan", "vfprintf",
        "vsprintf", "write", "__divdi3",
        "DmaAddr", "builtin_set_imask", "sceCdRI", "sceCdRM",
        "sceDevVif0Reset", "sceDevVu0Reset", "sceFsDbChk", "sceFsIntrSigSema", "sceFsSemExit", "sceFsSemInit",
        "sceFsSigSema", "sceIDC", "sceMpegFlush", "sceRpcFreePacket", "sceRpcGetFPacket", "sceRpcGetFPacket2",
        "sceSDC", "sceSifCmdIntrHdlr", "sceVu0ecossin", "mcCallMessageTypeSe", "mcCheckReadStartConfigFile", "mcCheckReadStartSaveFile",
        "mcCheckWriteStartConfigFile", "mcCheckWriteStartSaveFile", "mcCreateConfigInit", "mcCreateFileSelectWindow", "mcCreateIconInit", "mcCreateSaveFileInit",
        "mcDispFileName", "mcDispFileNumber", "mcDispWindowCurSol", "mcDispWindowFoundtion", "mcDisplayFileSelectWindow", "mcDisplaySelectFileInfo",
        "mcDisplaySelectFileInfoMesCount", "mcGetConfigCapacitySize", "mcGetFileSelectWindowCursol", "mcGetFreeCapacitySize", "mcGetIconCapacitySize", "mcGetIconFileCapacitySize",
        "mcGetPortSelectDirInfo", "mcGetSaveFileCapacitySize", "mcGetStringEnd", "mcMoveFileSelectWindowCursor", "mcNewCreateConfigFile", "mcNewCreateIcon",
        "mcNewCreateSaveFile", "mcReadIconData", "mcReadStartConfigFile", "mcReadStartSaveFile", "mcSelectFileInfoInit", "mcSelectSaveFileCheck",
        "mcSetFileSelectWindowCursol", "mcSetFileSelectWindowCursolInit", "mcSetStringSaveFile", "mcSetTyepWriteMode", "mcWriteIconData", "mcWriteStartConfigFile",
        "mcWriteStartSaveFile", "mceGetInfoApdx", "mceIntrReadFixAlign", "mceStorePwd", "sceCdApplyNCmd", "sceCdBreak",
        "sceCdCallback", "sceCdChangeThreadPriority", "sceCdDelayThread", "sceCdDiskReady", "sceCdGetDiskType", "sceCdGetError",
        "sceCdGetReadPos", "sceCdGetToc", "sceCdInit", "sceCdInitEeCB", "sceCdIntToPos", "sceCdMmode",
        "sceCdNcmdDiskReady", "sceCdPause", "sceCdPosToInt", "sceCdRead", "sceCdReadChain", "sceCdReadClock",
        "sceCdReadIOPm", "sceCdSearchFile", "sceCdSeek", "sceCdStInit", "sceCdStPause", "sceCdStRead",
        "sceCdStResume", "sceCdStSeek", "sceCdStSeekF", "sceCdStStart", "sceCdStStat", "sceCdStStop",
        "sceCdStandby", "sceCdStatus", "sceCdStop", "sceCdStream", "sceCdSync", "sceCdSyncS",
        "sceCdTrayReq", "sceClose", "sceDeci2Close", "sceDeci2ExLock", "sceDeci2ExRecv", "sceDeci2ExReqSend",
        "sceDeci2ExSend", "sceDeci2ExUnLock", "sceDeci2Open", "sceDeci2Poll", "sceDeci2ReqSend", "sceDmaCallback",
        "sceDmaDebug", "sceDmaGetChan", "sceDmaGetEnv", "sceDmaLastSyncTime", "sceDmaPause", "sceDmaPutEnv",
        "sceDmaPutStallAddr", "sceDmaRecv", "sceDmaRecvI", "sceDmaRecvN", "sceDmaReset", "sceDmaRestart",
        "sceDmaSend", "sceDmaSendI", "sceDmaSendM", "sceDmaSendN", "sceDmaSync", "sceDmaSyncN",
        "sceDmaWatch", "sceFsInit", "sceFsReset", "sceGifPkAddGsAD", "sceGifPkAddGsData", "sceGifPkCloseGifTag",
        "sceGifPkCnt", "sceGifPkEnd", "sceGifPkInit", "sceGifPkOpenGifTag", "sceGifPkRef", "sceGifPkRefLoadImage",
        "sceGifPkReset", "sceGifPkReserve", "sceGifPkTerminate", "sceGsExecLoadImage", "sceGsExecStoreImage", "sceGsGetGParam",
        "sceGsPutDispEnv", "sceGsPutDrawEnv", "sceGsResetGraph", "sceGsResetPath", "sceGsSetDefClear", "sceGsSetDefDBuffDc",
        "sceGsSetDefDBuff", "sceGsSetDefDispEnv", "sceGsSetDefDrawEnv", "sceGsSetDefDrawEnv2", "sceGsSetDefLoadImage", "sceGsSetDefStoreImage",
        "sceGsSwapDBuffDc", "sceGsSwapDBuff", "sceGsSyncPath", "sceGsSyncV", "sceGsSyncVCallback", "sceGszbufaddr",
        "sceVif1PkAddGsAD", "sceVif1PkAlign", "sceVif1PkCall", "sceVif1PkCloseDirectCode", "sceVif1PkCloseGifTag", "sceVif1PkCnt",
        "sceVif1PkEnd", "sceVif1PkInit", "sceVif1PkOpenDirectCode", "sceVif1PkOpenGifTag", "sceVif1PkReset", "sceVif1PkReserve",
        "sceVif1PkTerminate", "sceeFontInit", "sceeFontLoadFont", "sceeFontPrintfAt", "sceeFontPrintfAt2", "sceeFontGenerateString",
        "sceeFontClose", "sceeFontSetColour", "sceeFontSetMode", "sceeFontSetFont", "sceeFontSetScale", "sceIoctl",
        "sceIpuInit", "sceIpuRestartDMA", "sceIpuStopDMA", "sceIpuSync", "sceLseek", "sceMcChangeThreadPriority",
        "sceMcChdir", "sceMcClose", "sceMcDelete", "sceMcEnd", "sceMcFlush", "sceMcFormat",
        "sceMcGetDir", "sceMcGetEntSpace", "sceMcGetInfo", "sceMcGetSlotMax", "sceMcInit", "sceMcMkdir",
        "sceMcOpen", "sceMcRead", "sceMcRename", "sceMcSeek", "sceMcSetFileInfo", "sceMcSync",
        "sceMcUnformat", "sceMcWrite", "sceMpegAddBs", "sceMpegAddCallback", "sceMpegAddStrCallback", "sceMpegClearRefBuff",
        "sceMpegCreate", "sceMpegDelete", "sceMpegDemuxPss", "sceMpegDemuxPssRing", "sceMpegDispCenterOffX", "sceMpegDispCenterOffY",
        "sceMpegDispHeight", "sceMpegDispWidth", "sceMpegGetDecodeMode", "sceMpegGetPicture", "sceMpegGetPictureRAW8", "sceMpegGetPictureRAW8xy",
        "sceMpegInit", "sceMpegIsEnd", "sceMpegIsRefBuffEmpty", "sceMpegReset", "sceMpegResetDefaultPtsGap", "sceMpegSetDecodeMode",
        "sceMpegSetDefaultPtsGap", "sceMpegSetImageBuff", "sceOpen", "scePadEnd", "scePadEnterPressMode", "scePadExitPressMode",
        "scePadGetButtonMask", "scePadGetDmaStr", "scePadGetFrameCount", "scePadGetModVersion", "scePadGetPortMax", "scePadGetReqState",
        "scePadGetSlotMax", "scePadGetState", "scePadInfoAct", "scePadInfoComb", "scePadInfoMode", "scePadInfoPressMode",
        "scePadInit", "scePadInit2", "scePadPortClose", "scePadPortOpen", "scePadRead", "scePadReqIntToStr",
        "scePadSetActAlign", "scePadSetActDirect", "scePadSetButtonInfo", "scePadSetMainMode", "scePadSetReqState", "scePadSetVrefParam",
        "scePadSetWarningLevel", "scePadStateIntToStr", "scePrintf", "sceRead", "sceResetttyinit", "sceSSyn_BreakAtick",
        "sceSSyn_ClearBreakAtick", "sceSSyn_SendExcMsg", "sceSSyn_SendNrpnMsg", "sceSSyn_SendRpnMsg", "sceSSyn_SendShortMsg", "sceSSyn_SetChPriority",
        "sceSSyn_SetMasterVolume", "sceSSyn_SetOutPortVolume", "sceSSyn_SetOutputAssign", "sceSSyn_SetOutputMode", "sceSSyn_SetPortMaxPoly", "sceSSyn_SetPortVolume",
        "sceSSyn_SetTvaEnvMode", "sceSdCallBack", "sceSdRemote", "sceSdRemoteInit", "sceSdTransToIOP", "sceSetBrokenLink",
        "sceSetPtm", "sceSifAddCmdHandler", "sceSifAllocIopHeap", "sceSifAllocSysMemory", "sceSifBindRpc", "sceSifCheckStatRpc",
        "sceSifDmaStat", "sceSifExecRequest", "sceSifExitCmd", "sceSifExitRpc", "sceSifFreeIopHeap", "sceSifFreeSysMemory",
        "sceSifGetDataTable", "sceSifGetIopAddr", "sceSifGetNextRequest", "sceSifGetOtherData", "sceSifGetReg", "sceSifGetSreg",
        "sceSifInitCmd", "sceSifInitIopHeap", "sceSifInitRpc", "sceSifIsAliveIop", "sceSifLoadFileReset", "sceSifLoadIopHeap",
        "sceSifRebootIop", "sceSifRegisterRpc", "sceSifRemoveCmdHandler", "sceSifRemoveRpc", "sceSifRemoveRpcQueue", "sceSifResetIop",
        "sceSifRpcLoop", "sceSifSetCmdBuffer", "sceSifSetDChain", "sceSifSetDma", "isceSifSetDChain", "isceSifSetDma",
        "sceSifSetIopAddr", "sceSifSetReg", "sceSifSetRpcQueue", "sceSifSetSreg", "sceSifSetSysCmdBuffer", "sceSifStopDma",
        "sceSifSyncIop", "sceSifWriteBackDCache", "sceSynthSizerLfoTriangle", "sceSynthesizerAmpProcI", "sceSynthesizerAmpProcNI", "sceSynthesizerAssignAllNoteOff",
        "sceSynthesizerAssignAllSoundOff", "sceSynthesizerAssignHoldChange", "sceSynthesizerAssignNoteOff", "sceSynthesizerAssignNoteOn", "sceSynthesizerCalcEnv", "sceSynthesizerCalcPortamentPitch",
        "sceSynthesizerCalcTvfCoefAll", "sceSynthesizerCalcTvfCoefF0", "sceSynthesizerCent2PhaseInc", "sceSynthesizerChangeEffectSend", "sceSynthesizerChangeHsPanpot", "sceSynthesizerChangeNrpnCutOff",
        "sceSynthesizerChangeNrpnLfoDepth", "sceSynthesizerChangeNrpnLfoRate", "sceSynthesizerChangeOutAttrib", "sceSynthesizerChangeOutVol", "sceSynthesizerChangePanpot", "sceSynthesizerChangePartBendSens",
        "sceSynthesizerChangePartExpression", "sceSynthesizerChangePartHsExpression", "sceSynthesizerChangePartHsPitchBend", "sceSynthesizerChangePartModuration", "sceSynthesizerChangePartPitchBend", "sceSynthesizerChangePartVolume",
        "sceSynthesizerChangePortamento", "sceSynthesizerChangePortamentoTime", "sceSynthesizerClearKeyMap", "sceSynthesizerClearSpr", "sceSynthesizerCopyOutput", "sceSynthesizerDmaFromSPR",
        "sceSynthesizerDmaSpr", "sceSynthesizerDmaToSPR", "sceSynthesizerGetPartOutLevel", "sceSynthesizerGetPartial", "sceSynthesizerGetSampleParam", "sceSynthesizerHsMessage",
        "sceSynthesizerLfoNone", "sceSynthesizerLfoProc", "sceSynthesizerLfoSawDown", "sceSynthesizerLfoSawUp", "sceSynthesizerLfoSquare", "sceSynthesizerReadNoise",
        "sceSynthesizerReadNoiseAdd", "sceSynthesizerReadSample16", "sceSynthesizerReadSample16Add", "sceSynthesizerReadSample8", "sceSynthesizerReadSample8Add", "sceSynthesizerResetPart",
        "sceSynthesizerRestorDma", "sceSynthesizerSelectPatch", "sceSynthesizerSendShortMessage", "sceSynthesizerSetMasterVolume", "sceSynthesizerSetRVoice", "sceSynthesizerSetupDma",
        "sceSynthesizerSetupLfo", "sceSynthesizerSetupMidiModuration", "sceSynthesizerSetupMidiPanpot", "sceSynthesizerSetupNewNoise", "sceSynthesizerSetupReleaseEnv", "sceSynthesizerSetupTruncateTvaEnv",
        "sceSynthesizerSetupTruncateTvfPitchEnv", "sceSynthesizerSetuptEnv", "sceSynthesizerTonegenerator", "sceSynthesizerTransposeMatrix", "sceSynthesizerTvfProcI", "sceSynthesizerTvfProcNI",
        "sceSynthesizerWaitDmaFromSPR", "sceSynthesizerWaitDmaToSPR", "sceSynthsizerGetDrumPatch", "sceSynthsizerGetMeloPatch", "sceSynthsizerLfoNoise", "sceTtyHandler",
        "sceTtyInit", "sceTtyRead", "sceTtyWrite", "sceVpu0Reset", "sceVu0AddVector", "sceVu0ApplyMatrix",
        "sceVu0CameraMatrix", "sceVu0ClampVector", "sceVu0ClipAll", "sceVu0ClipScreen", "sceVu0ClipScreen3", "sceVu0CopyMatrix",
        "sceVu0CopyVector", "sceVu0CopyVectorXYZ", "sceVu0DivVector", "sceVu0DivVectorXYZ", "sceVu0DropShadowMatrix", "sceVu0FTOI0Vector",
        "sceVu0FTOI4Vector", "sceVu0ITOF0Vector", "sceVu0ITOF12Vector", "sceVu0ITOF4Vector", "sceVu0InnerProduct", "sceVu0InterVector",
        "sceVu0InterVectorXYZ", "sceVu0InversMatrix", "sceVu0LightColorMatrix", "sceVu0MulMatrix", "sceVu0MulVector", "sceVu0NormalLightMatrix",
        "sceVu0Normalize", "sceVu0OuterProduct", "sceVu0RotMatrix", "sceVu0RotMatrixX", "sceVu0RotMatrixY", "sceVu0RotMatrixZ",
        "sceVu0RotTransPers", "sceVu0RotTransPersN", "sceVu0ScaleVector", "sceVu0ScaleVectorXYZ", "sceVu0SubVector", "sceVu0TransMatrix",
        "sceVu0TransposeMatrix", "sceVu0UnitMatrix", "sceVu0ViewScreenMatrix", "sceWrite", "InitTLB"
    ));

    private static final Set<String> PS2_API_PREFIXES = new HashSet<>(Arrays.asList(
        "sce", "Sce", "SCE",
        "sif", "Sif", "SIF",
        "gs", "Gs", "GS",
        "dma", "Dma", "DMA",
        "iop", "Iop", "IOP",
        "vif", "Vif", "VIF",
        "spu", "Spu", "SPU",
        "mc", "Mc", "MC",
        "libc", "Libc", "LIBC"
    ));

    private static final Set<String> KNOWN_STDLIB_NAMES = new HashSet<>(Arrays.asList(
        "printf", "sprintf", "snprintf", "fprintf", "vprintf", "vfprintf", "vsprintf", "vsnprintf",
        "puts", "putchar", "getchar", "gets", "fgets",
        "__mcmp", "__sbprintf", "__sprint", "__sprint_r",
        "malloc", "free", "calloc", "realloc", "memalign", "memclr", "aligned_alloc", "posix_memalign",
        "memcpy", "memset", "memmove", "memcmp", "memchr", "bcopy", "bzero",
        "strcpy", "strncpy", "strcat", "strncat", "strcmp", "strncmp", "strlen", "strstr",
        "strchr", "strrchr", "strdup", "strtok", "strtok_r", "strerror",
        "fopen", "fclose", "fread", "fwrite", "fseek", "ftell", "rewind", "fflush",
        "fgetc", "feof", "ferror", "clearerr", "fileno", "tmpfile", "remove", "rename",
        "open", "close", "read", "write", "lseek", "stat", "fstat",
        "atoi", "atol", "atoll", "atof", "strtol", "strtoul", "strtoll", "strtoull", "strtod", "strtof",
        "rand", "srand", "random", "srandom", "drand48", "sqrt", "pow", "exp", "log", "log10",
        "sin", "cos", "tan", "asin", "acos", "atan", "atan2", "sinh", "cosh", "tanh",
        "floor", "ceil", "fabs", "fmod", "frexp", "ldexp", "modf",
        "time", "ctime", "clock", "difftime", "mktime", "localtime", "gmtime", "asctime", "strftime",
        "gettimeofday", "nanosleep", "usleep",
        "atexit", "system", "getpid", "fork", "waitpid",
        "qsort", "bsearch", "abs", "div", "labs", "ldiv", "llabs", "lldiv",
        "isalnum", "isalpha", "isdigit", "islower", "isupper", "isspace", "tolower", "toupper",
        "setjmp", "longjmp", "getenv", "setenv", "unsetenv",
        "perror", "fputc", "getc", "ungetc", "freopen", "setvbuf", "setbuf",
        "strnlen", "strspn", "strcspn", "strcasecmp", "strncasecmp"
    ));

    private static final Pattern C_LIB_PATTERN = Pattern.compile(
        "^_*(mem|str|time|f?printf|f?scanf|malloc|free|calloc|realloc|atoi|itoa|rand|srand|abort|exit|atexit|getenv|system|bsearch|qsort|abs|labs|div|ldiv|mblen|mbtowc|wctomb|mbstowcs|wcstombs).*"
    );

    private static final Pattern KERNEL_RUNTIME_NAME_PATTERN = Pattern.compile(
        "^(?:"
            + "(?:Create|Delete|Start|ExitDelete|Exit|Terminate|Suspend|Resume|Sleep|Wakeup|CancelWakeup|Change|Rotate|Release|Setup|Register|Query|Get|Set|Refer|Poll|Wait|Signal|Enable|Disable|Flush|Reset|Add|Init)"
            + "(?:Thread|Sema|EventFlag|Alarm|Intc|IntcHandler2|Dmac|DmacHandler2|OsdConfigParam|MemorySize|VSyncFlag|Heap|TLS|Status|Cache|Syscall|TLB|TLBEntry|GsCrt)"
            + "|EndOfHeap"
            + "|GsGetIMR|GsPutIMR"
            + "|Deci2Call"
            + "|Sif[A-Za-z0-9_]+"
            + "|i(?:SignalSema|PollSema|ReferSemaStatus|SetEventFlag|ClearEventFlag|PollEventFlag|ReferEventFlagStatus|WakeupThread|CancelWakeupThread|ReleaseWaitThread|SetAlarm|CancelAlarm|FlushCache|sceSifSetDma|sceSifSetDChain)"
        + ")$"
    );

    private static final class FunctionRecord {
        String name;
        long start;
        long endExclusive;
        long size;
        boolean syntheticEntry = false;
    }

    private static final class AddressTakenCandidate {
        long sourceOffset;
        long target;

        AddressTakenCandidate(long sourceOffset, long target) {
            this.sourceOffset = sourceOffset;
            this.target = target;
        }
    }

    private enum ClassificationKind {
        STUB,
        UNTRACKED_STUB,
        NONE
    }

    private static final class ClassificationResult {
        final ClassificationKind kind;
        final String name;

        ClassificationResult(ClassificationKind kind, String name) {
            this.kind = kind;
            this.name = name;
        }
    }

    private static String hex(long value) {
        return String.format("0x%08X", value & UINT32_MASK);
    }

    private static int opcode(long raw) {
        return (int) ((raw >>> OPCODE_SHIFT) & OPCODE_MASK);
    }

    private static int rs(long raw) {
        return (int) ((raw >>> RS_SHIFT) & REGISTER_MASK);
    }

    private static int rt(long raw) {
        return (int) ((raw >>> RT_SHIFT) & REGISTER_MASK);
    }

    private static int rd(long raw) {
        return (int) ((raw >>> RD_SHIFT) & REGISTER_MASK);
    }

    private static int function(long raw) {
        return (int) (raw & OPCODE_MASK);
    }

    private static int immediate(long raw) {
        return (int) (raw & IMMEDIATE_MASK);
    }

    private static String tomlString(String value) {
        if (value == null) {
            return "\"\"";
        }
        return "\"" + value.replace("\\", "\\\\").replace("\"", "\\\"") + "\"";
    }

    // Ghidra on Windows can return executable paths like "/D:/path/to/elf".
    // TOML expects "D:/path/to/elf" for this field.
    private static String normalizeWindowsDrivePath(String value) {
        if (value == null || value.length() < 4) {
            return value;
        }

        if (value.charAt(0) == '/' &&
            Character.isLetter(value.charAt(1)) &&
            value.charAt(2) == ':' &&
            (value.charAt(3) == '/' || value.charAt(3) == '\\')) {
            return value.substring(1);
        }

        return value;
    }

    private static String normalizeOptionalLeadingUnderscore(String value) {
        if (value == null || value.isEmpty()) {
            return "";
        }
        return value.startsWith("_") && value.length() > 1 ? value.substring(1) : value;
    }

    private static String resolveRuntimeHandlerName(String name) {
        if (name == null || name.isEmpty()) {
            return "";
        }

        if (RUNTIME_HANDLER_NAMES.contains(name)) {
            return name;
        }

        String normalized = normalizeOptionalLeadingUnderscore(name);
        if (!normalized.equals(name) && RUNTIME_HANDLER_NAMES.contains(normalized)) {
            return normalized;
        }

        String underscored = "_" + name;
        if (!name.startsWith("_") && RUNTIME_HANDLER_NAMES.contains(underscored)) {
            return underscored;
        }

        return "";
    }

    private static boolean hasRuntimeHandler(String name) {
        return !resolveRuntimeHandlerName(name).isEmpty();
    }

    private static boolean hasReliableSymbolName(String name) {
        if (name == null || name.isEmpty()) {
            return false;
        }

        if (name.startsWith("sub_") || name.startsWith("FUN_") || name.startsWith("func_") ||
            name.startsWith("entry_") || name.startsWith("function_") || name.startsWith("LAB_")) {
            return false;
        }

        boolean hasAlpha = false;
        boolean allHexOrPrefix = true;
        for (int i = 0; i < name.length(); ++i) {
            char c = name.charAt(i);
            if (Character.isAlphabetic(c)) {
                hasAlpha = true;
            }
            if (!(Character.digit(c, 16) >= 0 || c == 'x' || c == 'X' || c == '_')) {
                allHexOrPrefix = false;
            }
        }

        if (!hasAlpha) {
            return false;
        }

        if ((name.startsWith("0x") || name.startsWith("0X")) && allHexOrPrefix) {
            return false;
        }

        return true;
    }

    private static boolean hasPs2ApiPrefix(String name) {
        if (name == null || name.isEmpty()) {
            return false;
        }

        String base = normalizeOptionalLeadingUnderscore(name);
        for (String prefix : PS2_API_PREFIXES) {
            if (!base.startsWith(prefix)) {
                continue;
            }

            if (base.length() == prefix.length()) {
                return true;
            }

            if (!Character.isLowerCase(base.charAt(prefix.length()))) {
                return true;
            }
        }

        return false;
    }

    private static boolean matchesWithOptionalLeadingUnderscoreAlias(String candidate, Set<String> names) {
        if (candidate == null || candidate.isEmpty() || names == null || names.isEmpty()) {
            return false;
        }

        if (names.contains(candidate)) {
            return true;
        }

        String normalized = normalizeOptionalLeadingUnderscore(candidate);
        if (!normalized.equals(candidate) && names.contains(normalized)) {
            return true;
        }

        if (!candidate.startsWith("_") && names.contains("_" + candidate)) {
            return true;
        }

        return false;
    }

    private static boolean isLibraryFunctionName(String name) {
        if (name == null || name.isEmpty() || !hasReliableSymbolName(name)) {
            return false;
        }

        if (hasRuntimeHandler(name)) {
            return true;
        }

        String normalized = normalizeOptionalLeadingUnderscore(name);
        if (hasRuntimeHandler(normalized)) {
            return true;
        }

        if (KERNEL_RUNTIME_NAME_PATTERN.matcher(normalized).matches()) {
            return true;
        }

        if (matchesWithOptionalLeadingUnderscoreAlias(normalized, KNOWN_STDLIB_NAMES)) {
            return true;
        }

        if (hasPs2ApiPrefix(name)) {
            return true;
        }

        return C_LIB_PATTERN.matcher(normalized).matches();
    }

    private static ClassificationResult classifyFunction(Function function) {
        if (function == null) {
            return new ClassificationResult(ClassificationKind.NONE, "");
        }

        String name = function.getName();
        if (name == null || name.isEmpty()) {
            return new ClassificationResult(ClassificationKind.NONE, "");
        }

        String runtimeName = resolveRuntimeHandlerName(name);
        if (!runtimeName.isEmpty()) {
            return new ClassificationResult(ClassificationKind.STUB, runtimeName);
        }

        if (function.isThunk()) {
            Function target = function.getThunkedFunction(true);
            if (target != null) {
                String targetName = target.getName();
                String targetRuntimeName = resolveRuntimeHandlerName(targetName);
                if (!targetRuntimeName.isEmpty()) {
                    return new ClassificationResult(ClassificationKind.STUB, targetRuntimeName);
                }

                if (isLibraryFunctionName(targetName)) {
                    return new ClassificationResult(ClassificationKind.UNTRACKED_STUB, targetName);
                }
            }
        }

        if (isLibraryFunctionName(name)) {
            return new ClassificationResult(ClassificationKind.UNTRACKED_STUB, name);
        }

        return new ClassificationResult(ClassificationKind.NONE, name);
    }

    private static String makeSelector(String name, long start, boolean includeAddress) {
        if (includeAddress) {
            return name + "@" + hex(start);
        }
        return name;
    }

    private boolean isExecutableAddress(Address address) {
        if (address == null) {
            return false;
        }

        MemoryBlock block = currentProgram.getMemory().getBlock(address);
        return block != null && block.isExecute();
    }

    private boolean hasCallableLabelReference(Address address) {
        if (address == null) {
            return false;
        }

        ReferenceIterator refs = currentProgram.getReferenceManager().getReferencesTo(address);
        while (refs.hasNext()) {
            Reference ref = refs.next();
            if (ref == null) {
                continue;
            }

            RefType type = ref.getReferenceType();
            if (type != null && type.isCall()) {
                return true;
            }

            Address from = ref.getFromAddress();
            if (from == null) {
                continue;
            }

            MemoryBlock fromBlock = currentProgram.getMemory().getBlock(from);
            if (fromBlock != null && fromBlock.isExecute()) {
                return true;
            }
        }

        return false;
    }

    private static String makeAnonymousEntryName(long start) {
        return String.format("entry_%08x", start & UINT32_MASK);
    }

    private Long readWord(Address address) {
        if (address == null) {
            return null;
        }

        try {
            return ((long) currentProgram.getMemory().getInt(address)) & UINT32_MASK;
        } catch (Exception ignored) {
            return null;
        }
    }

    private Address addressFromOffset(long offset) {
        try {
            return currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(offset & UINT32_MASK);
        } catch (Exception ignored) {
            return null;
        }
    }

    private boolean looksLikeCallableEntry(long target, boolean allowLeafThunk) {
        if ((target % MIPS_INSTRUCTION_SIZE) != 0L) {
            return false;
        }

        Address address = addressFromOffset(target);
        if (!isExecutableAddress(address) || currentProgram.getListing().getInstructionAt(address) == null) {
            return false;
        }

        for (int index = 0; index < 8; ++index) {
            Address probe;
            try {
                probe = address.add(index * (long) MIPS_INSTRUCTION_SIZE);
            } catch (Exception ignored) {
                break;
            }

            Long rawValue = readWord(probe);
            if (rawValue == null) {
                break;
            }

            long raw = rawValue;
            int opcode = opcode(raw);
            int rs = rs(raw);
            int rt = rt(raw);
            int immediate = immediate(raw);

            if (index < 4 && (opcode == OPCODE_ADDIU || opcode == OPCODE_DADDIU) && rs == GPR_SP && rt == GPR_SP && (immediate & MIPS_IMMEDIATE_SIGN_BIT) != 0) {
                return true;
            }

            if ((opcode == OPCODE_SW || opcode == OPCODE_SD || opcode == OPCODE_SQ) &&
                rs == GPR_SP && rt == GPR_RA) {
                return true;
            }

            if (allowLeafThunk && opcode == OPCODE_SPECIAL &&
                function(raw) == SPECIAL_JR && rs == GPR_RA) {
                return true;
            }
        }

        return false;
    }

    private static boolean writesGpr(long raw, int register) {
        if (register == GPR_ZERO) {
            return false;
        }

        int opcode = opcode(raw);
        int rt = rt(raw);
        int rd = rd(raw);

        if (opcode == OPCODE_SPECIAL || opcode == OPCODE_MMI) {
            return rd == register;
        }
        if (opcode == OPCODE_JAL) {
            return register == GPR_RA;
        }

        boolean writesRt;
        switch (opcode) {
        case OPCODE_ADDI:
        case OPCODE_ADDIU:
        case OPCODE_SLTI:
        case OPCODE_SLTIU:
        case OPCODE_ANDI:
        case OPCODE_ORI:
        case OPCODE_XORI:
        case OPCODE_LUI:
        case OPCODE_DADDI:
        case OPCODE_DADDIU:
        case OPCODE_LDL:
        case OPCODE_LDR:
        case OPCODE_LQ:
        case OPCODE_LB:
        case OPCODE_LH:
        case OPCODE_LWL:
        case OPCODE_LW:
        case OPCODE_LBU:
        case OPCODE_LHU:
        case OPCODE_LWR:
        case OPCODE_LWU:
        case OPCODE_LL:
        case OPCODE_LLD:
        case OPCODE_LD:
            writesRt = true;
            break;
        default:
            writesRt = false;
            break;
        }
        return writesRt && rt == register;
    }

    private static boolean isControlTransfer(long raw) {
        int opcode = opcode(raw);
        switch (opcode) {
        case OPCODE_REGIMM:
        case OPCODE_J:
        case OPCODE_JAL:
        case OPCODE_BEQ:
        case OPCODE_BNE:
        case OPCODE_BLEZ:
        case OPCODE_BGTZ:
        case OPCODE_BEQL:
        case OPCODE_BNEL:
        case OPCODE_BLEZL:
        case OPCODE_BGTZL:
            return true;
        default:
            break;
        }

        if (opcode != OPCODE_SPECIAL) {
            return false;
        }

        int function = function(raw);
        return function == SPECIAL_JR || function == SPECIAL_JALR;
    }

    private static boolean isCallInstruction(long raw) {
        int opcode = opcode(raw);
        return opcode == OPCODE_JAL ||
               (opcode == OPCODE_SPECIAL && function(raw) == SPECIAL_JALR);
    }

    private void addSyntheticEntry(List<FunctionRecord> records, Set<Long> existingStarts, long target) {
        target &= UINT32_MASK;
        if (!existingStarts.add(target)) {
            return;
        }

        FunctionRecord record = new FunctionRecord();
        record.name = makeAnonymousEntryName(target);
        record.start = target;
        record.syntheticEntry = true;
        records.add(record);
    }

    private void collectMaterializedCodeEntries(List<FunctionRecord> records, Set<Long> existingStarts) {
        AddressSet executableAddresses = new AddressSet();
        for (MemoryBlock block : currentProgram.getMemory().getBlocks()) {
            if (block != null && block.isExecute()) {
                executableAddresses.addRange(block.getStart(), block.getEnd());
            }
        }

        InstructionIterator instructions = currentProgram.getListing().getInstructions(executableAddresses, true);
        while (instructions.hasNext() && !monitor.isCancelled()) {
            Instruction instruction = instructions.next();
            if (instruction == null) {
                continue;
            }

            Long upperRawValue = readWord(instruction.getAddress());
            if (upperRawValue == null) {
                continue;
            }

            long upperRaw = upperRawValue;
            if (opcode(upperRaw) != OPCODE_LUI) {
                continue;
            }

            int upperRegister = rt(upperRaw);
            if (upperRegister == GPR_ZERO) {
                continue;
            }

            long upperValue = ((long) immediate(upperRaw)) << MIPS_IMMEDIATE_BITS;
            boolean sawControlTransfer = false;
            boolean sawCallTransfer = false;

            for (int lookahead = 1; lookahead <= 4; ++lookahead) {
                Address lowAddress;
                try {
                    lowAddress = instruction.getAddress().add(lookahead * (long) MIPS_INSTRUCTION_SIZE);
                } catch (Exception ignored) {
                    break;
                }

                Long lowRawValue = readWord(lowAddress);
                if (lowRawValue == null) {
                    break;
                }

                long lowRaw = lowRawValue;
                int opcode = opcode(lowRaw);
                int rs = rs(lowRaw);
                int rt = rt(lowRaw);

                if ((opcode == OPCODE_ADDIU || opcode == OPCODE_ORI || opcode == OPCODE_DADDIU) &&
                    rs == upperRegister) {
                    int immediate = immediate(lowRaw);
                    long target;
                    if (opcode == OPCODE_ORI) {
                        target = upperValue | immediate;
                    } else {
                        target = (upperValue + (short) immediate) & UINT32_MASK;
                    }

                    Long nextRaw = readWord(addressFromOffset(
                        lowAddress.getOffset() + MIPS_INSTRUCTION_SIZE));
                    boolean followedByCall = nextRaw != null && isCallInstruction(nextRaw);
                    boolean materializedAsCallArgument =
                        rt >= GPR_A0 && rt <= GPR_A3 && (sawCallTransfer || followedByCall);

                    if (looksLikeCallableEntry(target, materializedAsCallArgument)) {
                        addSyntheticEntry(records, existingStarts, target);
                    }
                    break;
                }

                if (sawControlTransfer) {
                    break;
                }
                if (writesGpr(lowRaw, upperRegister)) {
                    break;
                }
                if (isControlTransfer(lowRaw)) {
                    sawControlTransfer = true;
                    sawCallTransfer = isCallInstruction(lowRaw);
                }
            }
        }
    }

    private static boolean isDedicatedFunctionPointerBlock(String name) {
        return ".ctors".equals(name) || ".dtors".equals(name) ||
               ".init_array".equals(name) || ".fini_array".equals(name);
    }

    private void collectDataFunctionPointerEntries(List<FunctionRecord> records, Set<Long> existingStarts) {
        final long clusterDistanceBytes = 32L;

        for (MemoryBlock block : currentProgram.getMemory().getBlocks()) {
            if (block == null || block.isExecute() || !block.isInitialized() ||
                block.getSize() < MIPS_INSTRUCTION_SIZE) {
                continue;
            }

            List<AddressTakenCandidate> candidates = new ArrayList<>();
            for (long offset = 0;
                 offset + MIPS_INSTRUCTION_SIZE <= block.getSize() && !monitor.isCancelled();
                 offset += MIPS_INSTRUCTION_SIZE) {
                Address source;
                try {
                    source = block.getStart().add(offset);
                } catch (Exception ignored) {
                    break;
                }

                Long target = readWord(source);
                if (target != null && looksLikeCallableEntry(target, true)) {
                    candidates.add(new AddressTakenCandidate(offset, target));
                }
            }

            boolean dedicatedPointerBlock = isDedicatedFunctionPointerBlock(block.getName());
            for (int index = 0; index < candidates.size(); ++index) {
                AddressTakenCandidate candidate = candidates.get(index);
                boolean clustered = dedicatedPointerBlock;

                if (index > 0 &&
                    candidate.sourceOffset - candidates.get(index - 1).sourceOffset <= clusterDistanceBytes) {
                    clustered = true;
                }
                if (index + 1 < candidates.size() &&
                    candidates.get(index + 1).sourceOffset - candidate.sourceOffset <= clusterDistanceBytes) {
                    clustered = true;
                }

                if (clustered) {
                    addSyntheticEntry(records, existingStarts, candidate.target);
                }
            }
        }
    }

    private List<FunctionRecord> collectExecutableLabelRecords(List<FunctionRecord> functionRecords) {
        List<FunctionRecord> labelRecords = new ArrayList<>();
        Set<Long> existingStarts = new HashSet<>();
        for (FunctionRecord record : functionRecords) {
            existingStarts.add(record.start);
        }

        SymbolIterator symbols = currentProgram.getSymbolTable().getSymbolIterator(true);
        while (symbols.hasNext() && !monitor.isCancelled()) {
            Symbol symbol = symbols.next();
            if (symbol == null || !symbol.isPrimary()) {
                continue;
            }

            if (symbol.getSymbolType() == SymbolType.FUNCTION) {
                continue;
            }

            Address address = symbol.getAddress();
            if (!isExecutableAddress(address)) {
                continue;
            }

            long start = address.getOffset();
            if (existingStarts.contains(start)) {
                continue;
            }

            Instruction instruction = currentProgram.getListing().getInstructionAt(address);
            if (instruction == null) {
                continue;
            }

            if (!hasCallableLabelReference(address)) {
                continue;
            }

            FunctionRecord record = new FunctionRecord();
            record.name = symbol.getName();
            record.start = start;
            record.syntheticEntry = true;
            labelRecords.add(record);
            existingStarts.add(start);
        }

        AddressSet executableAddresses = new AddressSet();
        for (MemoryBlock block : currentProgram.getMemory().getBlocks()) {
            if (block != null && block.isExecute()) {
                executableAddresses.addRange(block.getStart(), block.getEnd());
            }
        }

        InstructionIterator instructions = currentProgram.getListing().getInstructions(executableAddresses, true);
        while (instructions.hasNext() && !monitor.isCancelled()) {
            Instruction instruction = instructions.next();
            if (instruction == null) {
                continue;
            }

            Address address = instruction.getAddress();
            long start = address.getOffset();
            if (existingStarts.contains(start)) {
                continue;
            }

            if (!hasCallableLabelReference(address)) {
                continue;
            }

            FunctionRecord record = new FunctionRecord();
            record.name = makeAnonymousEntryName(start);
            record.start = start;
            record.syntheticEntry = true;
            labelRecords.add(record);
            existingStarts.add(start);
        }

        // Ghidra does not always promote function pointers to CALL references,
        // especially when the low half is produced in a MIPS delay slot. Mirror
        // the stripped-ELF fallback used by ElfParser so the CSV still contains
        // callback and vtable entries that are only address-taken.
        collectMaterializedCodeEntries(labelRecords, existingStarts);
        collectDataFunctionPointerEntries(labelRecords, existingStarts);

        if (labelRecords.isEmpty()) {
            return labelRecords;
        }

        List<Long> boundaries = new ArrayList<>();
        for (FunctionRecord record : functionRecords) {
            boundaries.add(record.start);
        }
        for (FunctionRecord record : labelRecords) {
            boundaries.add(record.start);
        }
        Collections.sort(boundaries);

        functionRecords.sort(Comparator.comparingLong(r -> r.start));
        for (FunctionRecord record : labelRecords) {
            long endExclusive = 0L;

            for (FunctionRecord functionRecord : functionRecords) {
                if (record.start > functionRecord.start && record.start < functionRecord.endExclusive) {
                    endExclusive = functionRecord.endExclusive;
                    break;
                }
            }

            if (endExclusive == 0L) {
                Address startAddress = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(record.start);
                MemoryBlock block = currentProgram.getMemory().getBlock(startAddress);
                if (block != null) {
                    endExclusive = block.getEnd().getOffset() + 1L;
                }
            }

            for (Long boundary : boundaries) {
                if (boundary > record.start && (endExclusive == 0L || boundary < endExclusive)) {
                    endExclusive = boundary;
                    break;
                }
            }

            if (endExclusive <= record.start) {
                endExclusive = record.start + 4L;
            }

            record.endExclusive = endExclusive;
            record.size = record.endExclusive - record.start;
        }

        labelRecords.sort(Comparator.comparingLong(r -> r.start));
        return labelRecords;
    }

    @Override
    public void run() throws Exception {
        File tomlFile = askFile("Choose output TOML config file", "Save");
        if (tomlFile == null) {
            return;
        }

        File csvFile = askFile("Choose output CSV file", "Save");
        if (csvFile == null) {
            return;
        }

        FunctionManager fm = currentProgram.getFunctionManager();
        FunctionIterator it = fm.getFunctions(true);

        List<FunctionRecord> functionRecords = new ArrayList<>();
        Set<String> stubSelectors = new LinkedHashSet<>();
        Set<String> untrackedStubSelectors = new LinkedHashSet<>();
        int uncategorizedCount = 0;

        while (it.hasNext() && !monitor.isCancelled()) {
            Function func = it.next();

            AddressSetView body = func.getBody();
            if (body == null || body.getNumAddresses() == 0) {
                continue;
            }

            FunctionRecord record = new FunctionRecord();
            record.name = func.getName();
            record.start = func.getEntryPoint().getOffset();
            record.endExclusive = body.getMaxAddress().getOffset() + 1L;
            record.size = body.getNumAddresses();
            functionRecords.add(record);

            ClassificationResult classification = classifyFunction(func);
            if (classification.kind == ClassificationKind.STUB) {
                stubSelectors.add(makeSelector(classification.name, record.start, true));
            } else if (classification.kind == ClassificationKind.UNTRACKED_STUB) {
                untrackedStubSelectors.add(makeSelector(classification.name, record.start, true));
            } else {
                uncategorizedCount++;
            }
        }

        final int functionCount = functionRecords.size();
        List<FunctionRecord> labelRecords = collectExecutableLabelRecords(functionRecords);
        List<FunctionRecord> exportRecords = new ArrayList<>(functionRecords);
        exportRecords.addAll(labelRecords);
        exportRecords.sort(Comparator.comparingLong(r -> r.start));

        try (PrintWriter writer = new PrintWriter(csvFile)) {
            writer.println("Name,Start,End,Size");
            for (FunctionRecord record : exportRecords) {
                writer.printf("%s,0x%08X,0x%08X,%d%n",
                    record.name,
                    record.start,
                    record.endExclusive,
                    record.size
                );
            }
        } 

        String programPath = currentProgram.getExecutablePath();
        if (programPath == null) {
            programPath = "";
        }
        programPath = normalizeWindowsDrivePath(programPath);

        File outputDir = tomlFile.getParentFile() == null ? new File("output") : new File(tomlFile.getParentFile(), "output");
        String ghidraCsvPath = csvFile.getAbsolutePath();

        try (PrintWriter writer = new PrintWriter(tomlFile)) {
            writer.println("# Auto-generated by ExportPS2Functions.java");
            writer.println("#");
            writer.println("# Classification policy (aligned with analyzer intent):");
            writer.println("# - runtime-known names -> [general].stubs");
            writer.println("# - library-like names without runtime handlers -> [general].untracked_stubs");
            writer.println("# - [general].skip is retained empty for legacy compatibility");
            writer.println("# - no SCE symbol database is used by this Ghidra script");
            writer.println();

            writer.println("[general]");
            writer.println("input = " + tomlString(programPath));
            writer.println("output = " + tomlString(outputDir.getAbsolutePath()));
            writer.println("ghidra_output = " + tomlString(ghidraCsvPath));
            writer.println("single_file_output = false");
            writer.println("patch_syscalls = false");
            writer.println("patch_cop0 = true");
            writer.println("patch_cache = true");
            writer.println("stubs = [");
            for (String selector : stubSelectors) {
                writer.println("  " + tomlString(selector) + ",");
            }
            writer.println("]");
            writer.println("untracked_stubs = [");
            for (String selector : untrackedStubSelectors) {
                writer.println("  " + tomlString(selector) + ",");
            }
            writer.println("]");
            writer.println("skip = []");
            writer.println();

            writer.println("[ghidra_export]");
            writer.println("function_count = " + functionCount);
            writer.println("code_label_count = " + labelRecords.size());
            writer.println("csv_record_count = " + exportRecords.size());
            writer.println("stub_count = " + stubSelectors.size());
            writer.println("untracked_stub_count = " + untrackedStubSelectors.size());
            writer.println("skip_count = 0");
            writer.println("uncategorized_count = " + uncategorizedCount);
            writer.println("runtime_call_name_count = " + RUNTIME_HANDLER_NAMES.size());
            writer.println("runtime_call_source = \"embedded_ps2_call_list_snapshot\"");
        }

         println(String.format("Exported %d functions and %d executable labels to %s", functionCount, labelRecords.size(), csvFile.getAbsolutePath()));

        println(String.format("Using %d embedded runtime handler names from ps2_call_list.h snapshot.", RUNTIME_HANDLER_NAMES.size()));
        println(String.format("Exported TOML config to %s", tomlFile.getAbsolutePath()));
    }
}
