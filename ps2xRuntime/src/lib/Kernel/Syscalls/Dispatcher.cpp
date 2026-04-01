#include "Common.h"
#include "Dispatcher.h"
#include "System.h"

namespace ps2_syscalls
{
    bool dispatchNumericSyscall(uint32_t syscallNumber, uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (dispatchSyscallOverride(syscallNumber, rdram, ctx, runtime))
        {
            return true;
        }

        switch (syscallNumber)
        {
        case 0x01:
            ResetEE(rdram, ctx, runtime);
            return true;
        case 0x02:
            GsSetCrt(rdram, ctx, runtime);
            return true;
        case 0x04:
            ExitThread(rdram, ctx, runtime);
            return true;
        case 0x10:
            AddIntcHandler(rdram, ctx, runtime);
            return true;
        case 0x11:
            RemoveIntcHandler(rdram, ctx, runtime);
            return true;
        case 0x12:
            AddDmacHandler(rdram, ctx, runtime);
            return true;
        case 0x13:
            RemoveDmacHandler(rdram, ctx, runtime);
            return true;
        case 0x14:
            EnableIntc(rdram, ctx, runtime);
            return true;
        case 0x15:
            DisableIntc(rdram, ctx, runtime);
            return true;
        case 0x16:
            EnableDmac(rdram, ctx, runtime);
            return true;
        case 0x17:
            DisableDmac(rdram, ctx, runtime);
            return true;
        case static_cast<uint32_t>(-0x1A):
            iEnableIntc(rdram, ctx, runtime);
            return true;
        case static_cast<uint32_t>(-0x1B):
            iDisableIntc(rdram, ctx, runtime);
            return true;
        case static_cast<uint32_t>(-0x1C):
            iEnableDmac(rdram, ctx, runtime);
            return true;
        case static_cast<uint32_t>(-0x1D):
            iDisableDmac(rdram, ctx, runtime);
            return true;
        case 0x18:
        case 0xFC:
            SetAlarm(rdram, ctx, runtime);
            return true;
        case 0x19:
        case 0xFE:
            CancelAlarm(rdram, ctx, runtime);
            return true;
        case static_cast<uint32_t>(-0x1E):
        case static_cast<uint32_t>(-0xFD):
            iSetAlarm(rdram, ctx, runtime);
            return true;
        case static_cast<uint32_t>(-0x1F):
        case static_cast<uint32_t>(-0xFF):
            iCancelAlarm(rdram, ctx, runtime);
            return true;
        case 0x20:
            CreateThread(rdram, ctx, runtime);
            return true;
        case 0x21:
            DeleteThread(rdram, ctx, runtime);
            return true;
        case 0x22:
            StartThread(rdram, ctx, runtime);
            return true;
        case 0x23:
            ExitThread(rdram, ctx, runtime);
            return true;
        case 0x24:
            ExitDeleteThread(rdram, ctx, runtime);
            return true;
        case 0x25:
        case static_cast<uint32_t>(-0x26):
            TerminateThread(rdram, ctx, runtime);
            return true;
        case 0x29:
            ChangeThreadPriority(rdram, ctx, runtime);
            return true;
        case static_cast<uint32_t>(-0x2A):
            iChangeThreadPriority(rdram, ctx, runtime);
            return true;
        case 0x2B:
            RotateThreadReadyQueue(rdram, ctx, runtime);
            return true;
        case static_cast<uint32_t>(-0x2C):
            iRotateThreadReadyQueue(rdram, ctx, runtime);
            return true;
        case 0x2D:
            ReleaseWaitThread(rdram, ctx, runtime);
            return true;
        case static_cast<uint32_t>(-0x2E):
            iReleaseWaitThread(rdram, ctx, runtime);
            return true;
        case 0x2F:
        case static_cast<uint32_t>(-0x2F):
            GetThreadId(rdram, ctx, runtime);
            return true;
        case 0x30:
            ReferThreadStatus(rdram, ctx, runtime);
            return true;
        case static_cast<uint32_t>(-0x31):
            iReferThreadStatus(rdram, ctx, runtime);
            return true;
        case 0x32:
            SleepThread(rdram, ctx, runtime);
            return true;
        case 0x33:
            WakeupThread(rdram, ctx, runtime);
            return true;
        case static_cast<uint32_t>(-0x34):
            iWakeupThread(rdram, ctx, runtime);
            return true;
        case 0x35:
            CancelWakeupThread(rdram, ctx, runtime);
            return true;
        case static_cast<uint32_t>(-0x36):
            iCancelWakeupThread(rdram, ctx, runtime);
            return true;
        case 0x37:
        case static_cast<uint32_t>(-0x38):
            SuspendThread(rdram, ctx, runtime);
            return true;
        case 0x39:
        case static_cast<uint32_t>(-0x3A):
            ResumeThread(rdram, ctx, runtime);
            return true;
        case 0x3C:
            SetupThread(rdram, ctx, runtime);
            return true;
        case 0x3D:
            SetupHeap(rdram, ctx, runtime);
            return true;
        case 0x3E:
            EndOfHeap(rdram, ctx, runtime);
            return true;
        case 0x40:
            CreateSema(rdram, ctx, runtime);
            return true;
        case 0x41:
            DeleteSema(rdram, ctx, runtime);
            return true;
        case static_cast<uint32_t>(-0x49):
            iDeleteSema(rdram, ctx, runtime);
            return true;
        case 0x42:
            SignalSema(rdram, ctx, runtime);
            return true;
        case static_cast<uint32_t>(-0x43):
            iSignalSema(rdram, ctx, runtime);
            return true;
        case 0x44:
            WaitSema(rdram, ctx, runtime);
            return true;
        case 0x45:
            PollSema(rdram, ctx, runtime);
            return true;
        case static_cast<uint32_t>(-0x46):
            iPollSema(rdram, ctx, runtime);
            return true;
        case 0x47:
            ReferSemaStatus(rdram, ctx, runtime);
            return true;
        case static_cast<uint32_t>(-0x48):
            iReferSemaStatus(rdram, ctx, runtime);
            return true;
        case 0x4A:
            SetOsdConfigParam(rdram, ctx, runtime);
            return true;
        case 0x4B:
            GetOsdConfigParam(rdram, ctx, runtime);
            return true;
        case 0x50:
            CreateEventFlag(rdram, ctx, runtime);
            return true;
        case 0x51:
            DeleteEventFlag(rdram, ctx, runtime);
            return true;
        case 0x52:
            SetEventFlag(rdram, ctx, runtime);
            return true;
        case static_cast<uint32_t>(-0x53):
            iSetEventFlag(rdram, ctx, runtime);
            return true;
        case 0x54:
            ClearEventFlag(rdram, ctx, runtime);
            return true;
        case static_cast<uint32_t>(-0x55):
            iClearEventFlag(rdram, ctx, runtime);
            return true;
        case 0x56:
            WaitEventFlag(rdram, ctx, runtime);
            return true;
        case 0x57:
            PollEventFlag(rdram, ctx, runtime);
            return true;
        case static_cast<uint32_t>(-0x58):
            iPollEventFlag(rdram, ctx, runtime);
            return true;
        case 0x59:
            ReferEventFlagStatus(rdram, ctx, runtime);
            return true;
        case static_cast<uint32_t>(-0x5A):
            iReferEventFlagStatus(rdram, ctx, runtime);
            return true;
        case 0x5A:
            QueryBootMode(rdram, ctx, runtime);
            return true;
        case 0x5B:
            GetThreadTLS(rdram, ctx, runtime);
            return true;
        case 0x5C:
        case static_cast<uint32_t>(-0x5C):
            EnableIntcHandler(rdram, ctx, runtime);
            return true;
        case 0x5D:
        case static_cast<uint32_t>(-0x5D):
            DisableIntcHandler(rdram, ctx, runtime);
            return true;
        case 0x5E:
        case static_cast<uint32_t>(-0x5E):
            EnableDmacHandler(rdram, ctx, runtime);
            return true;
        case 0x5F:
        case static_cast<uint32_t>(-0x5F):
            DisableDmacHandler(rdram, ctx, runtime);
            return true;
        case 0x61:
            EnableCache(rdram, ctx, runtime);
            return true;
        case 0x62:
            DisableCache(rdram, ctx, runtime);
            return true;
        case 0x64:
            FlushCache(rdram, ctx, runtime);
            return true;
        case 0x70:
            GsGetIMR(rdram, ctx, runtime);
            return true;
        case static_cast<uint32_t>(-0x70):
            iGsGetIMR(rdram, ctx, runtime);
            return true;
        case 0x71:
            GsPutIMR(rdram, ctx, runtime);
            return true;
        case static_cast<uint32_t>(-0x71):
            iGsPutIMR(rdram, ctx, runtime);
            return true;
        case 0x73:
            SetVSyncFlag(rdram, ctx, runtime);
            return true;
        case 0x74:
            SetSyscall(rdram, ctx, runtime);
            return true;
        case 0x76:
        case static_cast<uint32_t>(-0x76):
            ps2_stubs::sceSifDmaStat(rdram, ctx, runtime);
            return true;
        case 0x77:
        case static_cast<uint32_t>(-0x77):
            ps2_stubs::sceSifSetDma(rdram, ctx, runtime);
            return true;
        case 0x78:
        case static_cast<uint32_t>(-0x78):
            ps2_stubs::sceSifSetDChain(rdram, ctx, runtime);
            return true;
        case 0x83:
            FindAddress(rdram, ctx, runtime);
            return true;
        case 0x85:
            SetMemoryMode(rdram, ctx, runtime);
            return true;
        default:
            return false;
        }
    }
}
