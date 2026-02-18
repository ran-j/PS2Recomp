#include "ps2_syscalls.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "ps2_stubs.h"
#include <iostream>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <fstream>
#include <vector>
#include <unordered_map>
#include <thread>
#include <condition_variable>
#include <atomic>
#include <filesystem>
#include <chrono>
#include <ctime>
#include <memory>
#include <string>

#ifndef _WIN32
#include <unistd.h>   // for unlink,rmdir,chdir
#include <sys/stat.h> // for mkdir
#endif
#include <ThreadNaming.h>

std::string translatePs2Path(const char *ps2Path);

#include "syscalls/helpers/ps2_syscalls_helpers_path.inl"
#include "syscalls/helpers/ps2_syscalls_helpers_state.inl"
#include "syscalls/helpers/ps2_syscalls_helpers_loader.inl"
#include "syscalls/helpers/ps2_syscalls_helpers_runtime.inl"

namespace ps2_syscalls
{
#include "syscalls/ps2_syscalls_interrupt.inl"
#include "syscalls/ps2_syscalls_system.inl"
    void iDeleteSema(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);

    bool dispatchNumericSyscall(uint32_t syscallNumber, uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
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
        case static_cast<uint32_t>(-0x2A):
            ChangeThreadPriority(rdram, ctx, runtime);
            return true;
        case 0x2B:
        case static_cast<uint32_t>(-0x2C):
            RotateThreadReadyQueue(rdram, ctx, runtime);
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
        case static_cast<uint32_t>(-0x31):
            ReferThreadStatus(rdram, ctx, runtime);
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
        case 0x53:
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
        case 0x64:
            FlushCache(rdram, ctx, runtime);
            return true;
        case 0x70:
        case static_cast<uint32_t>(-0x70):
            GsGetIMR(rdram, ctx, runtime);
            return true;
        case 0x71:
        case static_cast<uint32_t>(-0x71):
            GsPutIMR(rdram, ctx, runtime);
            return true;
        case 0x73:
            SetVSyncFlag(rdram, ctx, runtime);
            return true;
        case 0x74:
            RegisterExitHandler(rdram, ctx, runtime);
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
        case 0x85:
            SetMemoryMode(rdram, ctx, runtime);
            return true;
        default:
            return false;
        }
    }

#include "syscalls/ps2_syscalls_thread.inl"
#include "syscalls/ps2_syscalls_flags.inl"
#include "syscalls/ps2_syscalls_rpc.inl"
#include "syscalls/ps2_syscalls_fileio.inl"

    void notifyRuntimeStop()
    {
        stopInterruptWorker();
        {
            std::lock_guard<std::mutex> lock(g_irq_handler_mutex);
            g_intcHandlers.clear();
            g_dmacHandlers.clear();
            g_nextIntcHandlerId = 1;
            g_nextDmacHandlerId = 1;
            g_enabled_intc_mask = 0xFFFFFFFFu;
            g_enabled_dmac_mask = 0xFFFFFFFFu;
        }
        {
            std::lock_guard<std::mutex> lock(g_vsync_flag_mutex);
            g_vsync_registration = {};
            g_vsync_tick_counter = 0u;
        }

        std::vector<std::shared_ptr<ThreadInfo>> threads;
        threads.reserve(32);
        {
            std::lock_guard<std::mutex> lock(g_thread_map_mutex);
            for (const auto &entry : g_threads)
            {
                if (entry.second)
                {
                    threads.push_back(entry.second);
                }
            }
        }

        for (const auto &threadInfo : threads)
        {
            {
                std::lock_guard<std::mutex> lock(threadInfo->m);
                threadInfo->forceRelease = true;
                threadInfo->terminated = true;
            }
            threadInfo->cv.notify_all();
        }

        std::vector<std::shared_ptr<SemaInfo>> semas;
        {
            std::lock_guard<std::mutex> lock(g_sema_map_mutex);
            semas.reserve(g_semas.size());
            for (const auto &entry : g_semas)
            {
                if (entry.second)
                {
                    semas.push_back(entry.second);
                }
            }
        }
        for (const auto &sema : semas)
        {
            sema->cv.notify_all();
        }

        std::vector<std::shared_ptr<EventFlagInfo>> eventFlags;
        {
            std::lock_guard<std::mutex> lock(g_event_flag_map_mutex);
            eventFlags.reserve(g_eventFlags.size());
            for (const auto &entry : g_eventFlags)
            {
                if (entry.second)
                {
                    eventFlags.push_back(entry.second);
                }
            }
        }
        for (const auto &eventFlag : eventFlags)
        {
            eventFlag->cv.notify_all();
        }

        {
            std::lock_guard<std::mutex> lock(g_alarm_mutex);
            g_alarms.clear();
        }
        g_alarm_cv.notify_all();
    }
}
