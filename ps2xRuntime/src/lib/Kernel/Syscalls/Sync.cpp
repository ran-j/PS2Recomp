#include "Common.h"
#include "Sync.h"
#include "runtime/ee_scheduler.h"

namespace ps2_syscalls
{
    namespace
    {
        constexpr uint32_t WEF_OR = 0x01u;
        constexpr uint32_t WEF_CLEAR = 0x10u;
        constexpr uint32_t WEF_CLEAR_ALL = 0x20u;
        constexpr uint32_t WEF_MODE_MASK = WEF_OR | WEF_CLEAR | WEF_CLEAR_ALL;
        constexpr uint32_t EA_MULTI = 0x02u;

        EeScheduler &scheduler(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            EeScheduler &result = runtime->eeScheduler();
            result.bindMainContextForSyscall(*ctx, rdram);
            return result;
        }

        void deleteSemaphoreImpl(uint8_t *rdram,
                                 R5900Context *ctx,
                                 PS2Runtime *runtime,
                                 bool interruptSafe)
        {
            EeScheduler &ee = scheduler(rdram, ctx, runtime);
            const int result = ee.deleteSemaphore(static_cast<int>(getRegU32(ctx, 4)), interruptSafe);
            setReturnS32(ctx, result);
            ee.transferIfRequested(interruptSafe);
        }

        void signalSemaphoreImpl(uint8_t *rdram,
                                 R5900Context *ctx,
                                 PS2Runtime *runtime,
                                 bool interruptSafe)
        {
            EeScheduler &ee = scheduler(rdram, ctx, runtime);
            const int result = ee.signalSemaphore(static_cast<int>(getRegU32(ctx, 4)), interruptSafe);
            setReturnS32(ctx, result);
            ee.transferIfRequested(interruptSafe);
        }

        void deleteEventFlagImpl(uint8_t *rdram,
                                 R5900Context *ctx,
                                 PS2Runtime *runtime,
                                 bool interruptSafe)
        {
            EeScheduler &ee = scheduler(rdram, ctx, runtime);
            const int result = ee.deleteEventFlag(static_cast<int>(getRegU32(ctx, 4)), interruptSafe);
            setReturnS32(ctx, result);
            ee.transferIfRequested(interruptSafe);
        }

        void setEventFlagImpl(uint8_t *rdram,
                              R5900Context *ctx,
                              PS2Runtime *runtime,
                              bool interruptSafe)
        {
            EeScheduler &ee = scheduler(rdram, ctx, runtime);
            const int result = ee.setEventFlag(static_cast<int>(getRegU32(ctx, 4)),
                                               getRegU32(ctx, 5),
                                               interruptSafe);
            setReturnS32(ctx, result);
            ee.transferIfRequested(interruptSafe);
        }
    }

    void CreateSema(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t address = getRegU32(ctx, 4);
        const auto *param = address == 0u ? nullptr : getEeGuestStruct<ee_sema_t>(rdram, address);
        if (!param)
        {
            setReturnS32(ctx, KE_ERROR);
            return;
        }

        // ee_sema_t from ps2sdk. The IOP attr/option/init/max ordering is not
        // accepted by the EE runtime.
        setReturnS32(ctx,
                     scheduler(rdram, ctx, runtime)
                         .createSemaphore(param->init_count,
                                          param->max_count,
                                          param->attr,
                                          param->option));
    }

    void DeleteSema(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        deleteSemaphoreImpl(rdram, ctx, runtime, false);
    }

    void iDeleteSema(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        deleteSemaphoreImpl(rdram, ctx, runtime, true);
    }

    void SignalSema(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        signalSemaphoreImpl(rdram, ctx, runtime, false);
    }

    void iSignalSema(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        signalSemaphoreImpl(rdram, ctx, runtime, true);
    }

    void WaitSema(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        scheduler(rdram, ctx, runtime).waitSemaphore(static_cast<int>(getRegU32(ctx, 4)));
    }

    void PollSema(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx,
                     scheduler(rdram, ctx, runtime).pollSemaphore(static_cast<int>(getRegU32(ctx, 4))));
    }

    void iPollSema(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        PollSema(rdram, ctx, runtime);
    }

    void ReferSemaStatus(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        EeScheduler &ee = scheduler(rdram, ctx, runtime);
        const EeSemaphore *semaphore = ee.semaphore(static_cast<int>(getRegU32(ctx, 4)));
        if (!semaphore)
        {
            setReturnS32(ctx, KE_UNKNOWN_SEMID);
            return;
        }
        auto *status = getEeGuestStruct<ee_sema_t>(rdram, getRegU32(ctx, 5));
        if (!status)
        {
            setReturnS32(ctx, KE_ERROR);
            return;
        }
        status->count = semaphore->count;
        status->max_count = semaphore->maxCount;
        status->init_count = semaphore->initCount;
        status->wait_threads = static_cast<int>(semaphore->waiters.size());
        status->attr = semaphore->attr;
        status->option = semaphore->option;
        setReturnS32(ctx, KE_OK);
    }

    void iReferSemaStatus(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        ReferSemaStatus(rdram, ctx, runtime);
    }

    void CreateEventFlag(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        struct EeEventFlagParam
        {
            uint32_t attr;
            uint32_t option;
            uint32_t bits;
        };
        const uint32_t address = getRegU32(ctx, 4);
        const auto *param = address == 0u ? nullptr : getEeGuestStruct<EeEventFlagParam>(rdram, address);
        if (!param)
        {
            setReturnS32(ctx, KE_ERROR);
            return;
        }
        setReturnS32(ctx,
                     scheduler(rdram, ctx, runtime).createEventFlag(param->bits, param->attr, param->option));
    }

    void DeleteEventFlag(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        deleteEventFlagImpl(rdram, ctx, runtime, false);
    }

    void iDeleteEventFlag(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        deleteEventFlagImpl(rdram, ctx, runtime, true);
    }

    void SetEventFlag(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setEventFlagImpl(rdram, ctx, runtime, false);
    }

    void iSetEventFlag(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setEventFlagImpl(rdram, ctx, runtime, true);
    }

    void ClearEventFlag(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx,
                     scheduler(rdram, ctx, runtime)
                         .clearEventFlag(static_cast<int>(getRegU32(ctx, 4)), getRegU32(ctx, 5)));
    }

    void iClearEventFlag(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        ClearEventFlag(rdram, ctx, runtime);
    }

    void WaitEventFlag(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const int id = static_cast<int>(getRegU32(ctx, 4));
        const uint32_t bits = getRegU32(ctx, 5);
        const uint32_t mode = getRegU32(ctx, 6);
        if ((mode & ~WEF_MODE_MASK) != 0u)
        {
            setReturnS32(ctx, KE_ILLEGAL_MODE);
            return;
        }
        if (bits == 0u)
        {
            setReturnS32(ctx, KE_EVF_ILPAT);
            return;
        }
        EeScheduler &ee = scheduler(rdram, ctx, runtime);
        const EeEventFlag *flag = ee.eventFlag(id);
        if (!flag)
        {
            setReturnS32(ctx, KE_UNKNOWN_EVFID);
            return;
        }
        if ((flag->attr & EA_MULTI) == 0u && !flag->waiters.empty())
        {
            setReturnS32(ctx, KE_EVF_MULTI);
            return;
        }
        const uint32_t resultAddress = getRegU32(ctx, 7);
        if (resultAddress != 0u && !getEeGuestStruct<uint32_t>(rdram, resultAddress))
        {
            setReturnS32(ctx, KE_ERROR);
            return;
        }
        ee.waitEventFlag(id, bits, mode, resultAddress);
    }

    void PollEventFlag(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const int id = static_cast<int>(getRegU32(ctx, 4));
        const uint32_t bits = getRegU32(ctx, 5);
        const uint32_t mode = getRegU32(ctx, 6);
        if ((mode & ~WEF_MODE_MASK) != 0u)
        {
            setReturnS32(ctx, KE_ILLEGAL_MODE);
            return;
        }
        if (bits == 0u)
        {
            setReturnS32(ctx, KE_EVF_ILPAT);
            return;
        }
        EeScheduler &ee = scheduler(rdram, ctx, runtime);
        const EeEventFlag *flag = ee.eventFlag(id);
        if (!flag)
        {
            setReturnS32(ctx, KE_UNKNOWN_EVFID);
            return;
        }
        if ((flag->attr & EA_MULTI) == 0u && !flag->waiters.empty())
        {
            setReturnS32(ctx, KE_EVF_MULTI);
            return;
        }
        uint32_t *output = nullptr;
        if (getRegU32(ctx, 7) != 0u)
        {
            output = getEeGuestStruct<uint32_t>(rdram, getRegU32(ctx, 7));
            if (!output)
            {
                setReturnS32(ctx, KE_ERROR);
                return;
            }
        }
        uint32_t observed = 0;
        const int result = ee.pollEventFlag(id, bits, mode, observed);
        if (result == KE_OK && output)
        {
            *output = observed;
        }
        setReturnS32(ctx, result);
    }

    void iPollEventFlag(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        PollEventFlag(rdram, ctx, runtime);
    }

    void ReferEventFlagStatus(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        struct EeEventFlagStatus
        {
            uint32_t attr;
            uint32_t option;
            uint32_t initBits;
            uint32_t currentBits;
            int32_t waitThreads;
            int32_t reserved1;
            int32_t reserved2;
        };

        EeScheduler &ee = scheduler(rdram, ctx, runtime);
        const EeEventFlag *flag = ee.eventFlag(static_cast<int>(getRegU32(ctx, 4)));
        if (!flag)
        {
            setReturnS32(ctx, KE_UNKNOWN_EVFID);
            return;
        }
        auto *status = getEeGuestStruct<EeEventFlagStatus>(rdram, getRegU32(ctx, 5));
        if (!status)
        {
            setReturnS32(ctx, KE_ERROR);
            return;
        }
        *status = {flag->attr,
                   flag->option,
                   flag->initBits,
                   flag->bits,
                   static_cast<int32_t>(flag->waiters.size()),
                   0,
                   0};
        setReturnS32(ctx, KE_OK);
    }

    void iReferEventFlagStatus(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        ReferEventFlagStatus(rdram, ctx, runtime);
    }

    void SetAlarm(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx,
                     scheduler(rdram, ctx, runtime)
                         .setAlarm(static_cast<uint16_t>(getRegU32(ctx, 4)),
                                   getRegU32(ctx, 5),
                                   getRegU32(ctx, 6),
                                   getRegU32(ctx, 28),
                                   getRegU32(ctx, 29)));
    }

    void InitAlarm(uint8_t *, R5900Context *ctx, PS2Runtime *)
    {
        setReturnS32(ctx, KE_OK);
    }

    void iSetAlarm(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        SetAlarm(rdram, ctx, runtime);
    }

    void CancelAlarm(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx,
                     scheduler(rdram, ctx, runtime).cancelAlarm(static_cast<int>(getRegU32(ctx, 4))));
    }

    void iCancelAlarm(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        CancelAlarm(rdram, ctx, runtime);
    }

    void ReleaseAlarm(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        CancelAlarm(rdram, ctx, runtime);
    }

    void iReleaseAlarm(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        CancelAlarm(rdram, ctx, runtime);
    }
}
