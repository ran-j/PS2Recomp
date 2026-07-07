#include "Common.h"
#include "Sync.h"
#include "ps2_fiber.h"

namespace ps2_syscalls
{
    static bool looksLikeGuestPointerOrNull(uint32_t value)
    {
        if (value == 0u)
        {
            return true;
        }
        const uint32_t normalized = value & 0x1FFFFFFFu;
        return normalized < PS2_RAM_SIZE;
    }

    static bool readGuestU32Safe(const uint8_t *rdram, uint32_t addr, uint32_t &out)
    {
        const uint8_t *b0 = getConstMemPtr(rdram, addr + 0u);
        const uint8_t *b1 = getConstMemPtr(rdram, addr + 1u);
        const uint8_t *b2 = getConstMemPtr(rdram, addr + 2u);
        const uint8_t *b3 = getConstMemPtr(rdram, addr + 3u);
        if (!b0 || !b1 || !b2 || !b3)
        {
            out = 0u;
            return false;
        }

        out = static_cast<uint32_t>(*b0) |
              (static_cast<uint32_t>(*b1) << 8) |
              (static_cast<uint32_t>(*b2) << 16) |
              (static_cast<uint32_t>(*b3) << 24);
        return true;
    }

    struct DecodedSemaParams
    {
        int init = 0;
        int max = 1;
        uint32_t attr = 0;
        uint32_t option = 0;
    };

    static DecodedSemaParams decodeCreateSemaParams(const uint32_t *param, uint32_t availableWords)
    {
        DecodedSemaParams out{};
        if (!param || availableWords == 0u)
        {
            return out;
        }

        // EE layout (kernel.h):
        // [0]=count [1]=max_count [2]=init_count [3]=wait_threads [4]=attr [5]=option
        const bool hasEeLayout = availableWords >= 3u;
        const int eeMax = hasEeLayout ? static_cast<int>(param[1]) : 1;
        const int eeInit = hasEeLayout ? static_cast<int>(param[2]) : 0;
        const uint32_t eeAttr = (availableWords >= 5u) ? param[4] : 0u;
        const uint32_t eeOption = (availableWords >= 6u) ? param[5] : 0u;

        // Legacy layout (IOP-style):
        // [0]=attr [1]=option [2]=init [3]=max
        const bool hasLegacyLayout = availableWords >= 4u;
        const int legacyMax = hasLegacyLayout ? static_cast<int>(param[3]) : 1;
        const int legacyInit = hasLegacyLayout ? static_cast<int>(param[2]) : 0;
        const uint32_t legacyAttr = hasLegacyLayout ? param[0] : 0u;
        const uint32_t legacyOption = hasLegacyLayout ? param[1] : 0u;

        auto countLooksPlausible = [](int value) -> bool
        {
            return value > 0 && value <= 0x10000;
        };

        bool useLegacyLayout = hasLegacyLayout && !hasEeLayout;
        if (hasLegacyLayout && hasEeLayout && countLooksPlausible(legacyMax) && !countLooksPlausible(eeMax))
        {
            useLegacyLayout = true;
        }
        else if (hasLegacyLayout && hasEeLayout && countLooksPlausible(legacyMax) && countLooksPlausible(eeMax))
        {
            // If both max values look valid, prefer the layout whose option field
            // looks like a pointer/NULL payload.
            const bool eeOptionLooksValid = looksLikeGuestPointerOrNull(eeOption);
            const bool legacyOptionLooksValid = looksLikeGuestPointerOrNull(legacyOption);
            if (!eeOptionLooksValid && legacyOptionLooksValid)
            {
                useLegacyLayout = true;
            }
        }

        if (useLegacyLayout && hasLegacyLayout)
        {
            out.max = legacyMax;
            out.init = legacyInit;
            out.attr = legacyAttr;
            out.option = legacyOption;
        }
        else
        {
            if (!hasEeLayout)
            {
                return out;
            }
            out.max = eeMax;
            out.init = eeInit;
            out.attr = eeAttr;
            out.option = eeOption;
        }

        return out;
    }

    void CreateSema(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t paramAddr = getRegU32(ctx, 4); // $a0
        if (paramAddr == 0u)
        {
            setReturnS32(ctx, KE_ERROR);
            return;
        }

        uint32_t rawParams[6] = {};
        uint32_t availableWords = 0u;
        for (uint32_t i = 0; i < 6u; ++i)
        {
            if (!readGuestU32Safe(rdram, paramAddr + (i * 4u), rawParams[i]))
            {
                break;
            }
            availableWords = i + 1u;
        }

        if (availableWords < 3u)
        {
            setReturnS32(ctx, KE_ERROR);
            return;
        }

        const DecodedSemaParams decoded = decodeCreateSemaParams(rawParams, availableWords);
        int init = decoded.init;
        int max = decoded.max;
        uint32_t attr = decoded.attr;
        uint32_t option = decoded.option;

        if (max <= 0)
        {
            max = 1;
        }
        if (init < 0)
        {
            init = 0;
        }
        if (init > max)
        {
            init = max;
        }

        int id = 0;
        auto info = std::make_shared<SemaInfo>();
        info->count = init;
        info->maxCount = max;
        info->initCount = init;
        info->attr = attr;
        info->option = option;

        {
            std::lock_guard<std::mutex> lock(g_sema_map_mutex);
            for (int attempts = 0; attempts < 0x7FFF; ++attempts)
            {
                if (g_nextSemaId <= 0)
                {
                    g_nextSemaId = 1;
                }

                const int candidate = g_nextSemaId++;
                if (candidate <= 0)
                {
                    continue;
                }

                if (g_semas.find(candidate) == g_semas.end())
                {
                    id = candidate;
                    break;
                }
            }

            if (id <= 0)
            {
                setReturnS32(ctx, KE_ERROR);
                return;
            }

            g_semas.emplace(id, info);
        }
        setReturnS32(ctx, id);
    }

    void DeleteSema(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int sid = static_cast<int>(getRegU32(ctx, 4));
        std::shared_ptr<SemaInfo> sema;

        {
            std::lock_guard<std::mutex> lock(g_sema_map_mutex);
            auto it = g_semas.find(sid);
            if (it == g_semas.end())
            {
                setReturnS32(ctx, KE_UNKNOWN_SEMID);
                return;
            }
            sema = it->second;
            g_semas.erase(it);
        }

        // Collect all waiting threads, then wake each with token validation.
        std::vector<std::pair<int, ps2sched::FiberToken>> waiters;
        {
            std::lock_guard<std::mutex> lk(sema->m);
            sema->deleted = true;
            waiters.swap(sema->waitList);
        }
        for (const auto& [tid, token] : waiters)
        {
            ps2sched::enqueue_external_wakeup_validated(tid, token);
        }
        if (!waiters.empty())
        {
            ps2sched::maybe_yield();
        }

        // PS2 EE BIOS returns sid on success.
        setReturnS32(ctx, sid);
    }

    void iDeleteSema(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        DeleteSema(rdram, ctx, runtime);
    }

    void SignalSema(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int sid = static_cast<int>(getRegU32(ctx, 4));
        auto sema = lookupSemaInfo(sid);
        if (!sema)
        {
            setReturnS32(ctx, KE_UNKNOWN_SEMID);
            return;
        }

        // PS2 EE BIOS returns sid on success; KE_SEMA_OVF overrides on overflow.
        int ret = sid;
        int wokenTid = 0;
        ps2sched::FiberToken wokenToken{};
        {
            std::unique_lock<std::mutex> lock(sema->m);
            if (sema->count >= sema->maxCount)
            {
                ret = KE_SEMA_OVF;
            }
            else
            {
                sema->count++;
                // Pop one waiter and wake it.
                if (!sema->waitList.empty())
                {
                    wokenTid   = sema->waitList.front().first;
                    wokenToken = sema->waitList.front().second;
                    sema->waitList.erase(sema->waitList.begin());
                }
            }
            lock.unlock();
        }
        if (wokenTid != 0)
        {
            ps2sched::enqueue_external_wakeup_validated(wokenTid, wokenToken);
            ps2sched::maybe_yield();
        }

        setReturnS32(ctx, ret);
    }

    void iSignalSema(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        SignalSema(rdram, ctx, runtime);
    }

    void WaitSema(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int sid = static_cast<int>(getRegU32(ctx, 4));
        auto sema = lookupSemaInfo(sid);
        if (!sema)
        {
            setReturnS32(ctx, KE_UNKNOWN_SEMID);
            return;
        }

        // Borrowed host workers (g_currentThreadId == -1) are not PS2 threads;
        // they must never create or mutate a g_threads entry (all such workers
        // would alias tid -1 and race each other). Guest identity is keyed off
        // g_currentThreadId, NOT fiber-ness: a non-fiber host thread carrying a
        // real guest tid (e.g. a raw std::thread standing in for a guest thread
        // in tests, or the main thread — which the runtime ctor seeds with tid 1)
        // still needs ThreadInfo bookkeeping so ReleaseWaitThread/ReferSemaStatus
        // can observe and target it. onFiber remains the gate for the parts of
        // this call that are genuinely scheduler-only (arm_park / the fiber Mesa
        // wait below): a non-fiber, even with a valid info, cannot be parked on
        // the fiber scheduler and still takes the bounded-backoff retry loop
        // (block_current handles it). info == nullptr (borrowed worker) drives
        // the non-bookkeeping retry path below; all ThreadInfo accesses are
        // already guarded by `if (info)`.
        const bool onFiber = (ps2fiber_current() != nullptr);
        std::shared_ptr<ThreadInfo> info =
            (g_currentThreadId != -1) ? ensureCurrentThreadInfo(ctx) : nullptr;
        throwIfTerminated(info); // throwIfTerminated is null-safe
        std::unique_lock<std::mutex> lock(sema->m);

        // PS2 EE BIOS returns sid on success.
        int ret = sid;

        if (sema->count > 0)
        {
            sema->count--;
        }
        else
        {
            // Slow path: wait until we can consume a permit (Mesa monitor semantics).
            // Re-check count > 0 after each wake; re-block if stolen by PollSema.
            NonFiberBackoff nfBackoff; // unused for fibers; ramps for borrowed workers

            // Establish wait state ONCE, here, at the point the thread first
            // transitions into THS_WAIT/THS_WAITSUSPEND for this call. This is
            // also the only safe place to clear forceRelease: ReleaseWaitThread
            // only ever sets it while observing status == WAIT/WAITSUSPEND, and
            // that transition (plus the clear) happens atomically under info->m
            // here, so a stale true left over from a prior, unrelated wait on
            // this ThreadInfo cannot leak into this wait, and nothing can race
            // the clear itself.
            if (info)
            {
                std::lock_guard<std::mutex> tLock(info->m);
                info->status = (info->suspendCount > 0) ? THS_WAITSUSPEND : THS_WAIT;
                info->waitType = TSW_SEMA;
                info->waitId = sid;
                info->forceRelease = false;
            }

            for (;;)
            {
                // Retry-check: status stays WAIT/WAITSUSPEND for the *entire*
                // loop (it is only reset to RUN/SUSPEND once, after the loop,
                // below), so ReleaseWaitThread may legitimately fire and set
                // forceRelease at any point while we're spinning here -
                // including right after the post-wake check below found it
                // false and decided to re-block. Clearing forceRelease
                // unconditionally here would silently clobber that pending
                // release, so consume it and take the same released-exit path
                // as the post-wake check.
                if (info)
                {
                    bool release = false;
                    {
                        std::lock_guard<std::mutex> tLock(info->m);
                        if (info->forceRelease)
                        {
                            info->forceRelease = false;
                            release = true;
                        }
                    }
                    if (release)
                    {
                        ret = KE_RELEASE_WAIT;
                        break;
                    }
                }

                // sema->waiters is a plain count of every thread genuinely
                // blocked here — fiber, non-fiber-with-identity, or fully
                // borrowed (g_currentThreadId == -1) — so ReferSemaStatus's
                // wait_threads reports accurately for ALL of them (PS2 EE BIOS
                // semantics: any blocked thread counts, not just ones we can
                // individually target for wakeup). It carries no identity, so
                // unlike waitList it has no tid-aliasing hazard and is safe to
                // bump unconditionally.
                sema->waiters++;

                // Only a thread with a valid guest identity (info != nullptr,
                // i.e. g_currentThreadId != -1) publishes itself to the
                // *wait-list* (as opposed to the waiters count above), whether
                // or not it is a real fiber. This is what lets ReleaseWaitThread's
                // target lookup and SignalSema/DeleteSema's targeted wakeup see a
                // non-fiber host thread carrying a real guest tid (e.g. a raw
                // std::thread standing in for a guest thread in tests). Off-fiber,
                // current_fiber_token() is FiberToken{}; SignalSema/DeleteSema's
                // enqueue_external_wakeup_validated drops FiberToken{} entries, so
                // publishing here is harmless for such a waiter — it doesn't need
                // the wake, it re-polls via the backoff loop below. A fully
                // borrowed host worker (info==nullptr) would alias every other
                // borrowed worker as tid -1 if it published a waitList entry, so
                // it skips that (but is still counted in sema->waiters above) and
                // relies on the block_current() non-fiber retry loop (which
                // re-checks sema->count below).
                if (info)
                {
                    // Publish to the wait-list under sema->m so a SignalSema is
                    // serialized against our enqueue. arm_park() runs AFTER we
                    // drop sema->m, so g_sched_mutex is never nested under an
                    // object mutex. A SignalSema that fires in the publish/arm
                    // window sees g_running_fiber == this fiber and records
                    // wake_pending (consumed by block_current).
                    sema->waitList.emplace_back(g_currentThreadId, ps2sched::current_fiber_token());
                }

                // Drop sema->m BEFORE any scheduler operation.
                lock.unlock();

                // Non-fiber (borrowed host worker) path: bounded exponential backoff
                // so a never-satisfied condition cannot busy-spin the CPU.
                const ps2sched::BlockResult br = nfBackoff.wait(onFiber);

                // === Woke up here ===
                lock.lock();

                // Any thread that published a wait-list entry above (info !=
                // nullptr) must remove it here if still present (SignalSema/
                // DeleteSema may have already popped it). A borrowed host worker
                // (info == nullptr) never published a wait-list entry, so there
                // is nothing to remove there — but every thread (identified or
                // borrowed) decrements sema->waiters to match its unconditional
                // increment above.
                if (info)
                {
                    auto& wl = sema->waitList;
                    auto it = std::find_if(wl.begin(), wl.end(),
                        [](const std::pair<int, ps2sched::FiberToken>& e){ return e.first == g_currentThreadId; });
                    if (it != wl.end()) wl.erase(it);
                }
                sema->waiters--;

                // Wake reasons that abort the wait without consuming a permit:
                if (sema->deleted)
                {
                    ret = KE_WAIT_DELETE;
                    break;
                }

                if (info)
                {
                    bool release = false;
                    {
                        std::lock_guard<std::mutex> tLock(info->m);
                        if (info->forceRelease)
                        {
                            info->forceRelease = false;
                            release = true;
                        }
                    }
                    if (release)
                    {
                        ret = KE_RELEASE_WAIT;
                        break;
                    }
                }

                if (info && info->terminated.load())
                {
                    throw ThreadExitException();
                }

                // Mesa re-check: only consume if a permit is actually available.
                // If PollSema stole the count between SignalSema's unlock and our
                // re-lock, count == 0 and we loop to re-block.
                if (sema->count > 0)
                {
                    sema->count--;
                    // PS2 EE BIOS returns sid on success.
                    ret = sid;
                    break;
                }
                // Spurious wake or permit stolen — loop and block again.
            }
        }

        // Reset thread status on all non-exception exit paths (fast path, slow path success,
        // and error breaks). The throw-ThreadExitException path unwinds without reaching here.
        if (info)
        {
            std::lock_guard<std::mutex> tLock(info->m);
            info->status = (info->suspendCount > 0) ? THS_SUSPEND : THS_RUN;
            info->waitType = TSW_NONE;
            info->waitId = 0;
        }

        lock.unlock();
        waitWhileSuspended(info);
        setReturnS32(ctx, ret);
    }

    void PollSema(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int sid = static_cast<int>(getRegU32(ctx, 4));
        auto sema = lookupSemaInfo(sid);
        if (!sema)
        {
            setReturnS32(ctx, KE_UNKNOWN_SEMID);
            return;
        }

        std::lock_guard<std::mutex> lock(sema->m);
        if (sema->count > 0)
        {
            sema->count--;
            setReturnS32(ctx, sid);  // PS2 EE BIOS returns sid on success.
            return;
        }

        setReturnS32(ctx, KE_SEMA_ZERO);
    }

    void iPollSema(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        PollSema(rdram, ctx, runtime);
    }

    void ReferSemaStatus(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int sid = static_cast<int>(getRegU32(ctx, 4));
        uint32_t statusAddr = getRegU32(ctx, 5);

        auto sema = lookupSemaInfo(sid);
        if (!sema)
        {
            setReturnS32(ctx, KE_UNKNOWN_SEMID);
            return;
        }

        ee_sema_t *status = reinterpret_cast<ee_sema_t *>(getMemPtr(rdram, statusAddr));
        if (!status)
        {
            setReturnS32(ctx, KE_ERROR);
            return;
        }

        std::lock_guard<std::mutex> lock(sema->m);
        status->count = sema->count;
        status->max_count = sema->maxCount;
        status->init_count = sema->initCount;
        status->wait_threads = sema->waiters;
        status->attr = sema->attr;
        status->option = sema->option;
        setReturnS32(ctx, KE_OK);
    }

    void iReferSemaStatus(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        ReferSemaStatus(rdram, ctx, runtime);
    }

    constexpr uint32_t WEF_OR = 1;
    constexpr uint32_t WEF_CLEAR = 0x10;
    constexpr uint32_t WEF_CLEAR_ALL = 0x20;
    constexpr uint32_t WEF_MODE_MASK = WEF_OR | WEF_CLEAR | WEF_CLEAR_ALL;
    constexpr uint32_t EA_MULTI = 0x2;

    void CreateEventFlag(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t paramAddr = getRegU32(ctx, 4); // $a0

        auto info = std::make_shared<EventFlagInfo>();
        if (paramAddr != 0u)
        {
            // Read attr / option / initBits with full RDRAM range checks (matching
            // CreateSema), instead of dereferencing a raw guest pointer. A field
            // whose word falls outside RDRAM stays at its default of 0.
            uint32_t attr = 0u, option = 0u, initBits = 0u;
            readGuestU32Safe(rdram, paramAddr + 0u, attr);
            readGuestU32Safe(rdram, paramAddr + 4u, option);
            readGuestU32Safe(rdram, paramAddr + 8u, initBits);
            info->attr = attr;
            info->option = option;
            info->initBits = initBits;
            info->bits = info->initBits;
        }

        int id = 0;
        {
            std::lock_guard<std::mutex> mapLock(g_event_flag_map_mutex);
            id = g_nextEventFlagId++;
            g_eventFlags[id] = info;
        }
        setReturnS32(ctx, id);
    }

    void DeleteEventFlag(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int eid = static_cast<int>(getRegU32(ctx, 4));
        std::shared_ptr<EventFlagInfo> info;
        {
            std::lock_guard<std::mutex> mapLock(g_event_flag_map_mutex);
            auto it = g_eventFlags.find(eid);
            if (it == g_eventFlags.end())
            {
                setReturnS32(ctx, KE_UNKNOWN_EVFID);
                return;
            }
            info = it->second;
            g_eventFlags.erase(it);
        }

        if (!info)
        {
            setReturnS32(ctx, KE_UNKNOWN_EVFID);
            return;
        }

        std::vector<std::pair<int, ps2sched::FiberToken>> evfWaiters;
        {
            std::lock_guard<std::mutex> lk(info->m);
            info->deleted = true;
            evfWaiters.swap(info->waitList);
        }
        for (const auto& [tid, token] : evfWaiters)
        {
            ps2sched::enqueue_external_wakeup_validated(tid, token);
        }
        if (!evfWaiters.empty())
        {
            ps2sched::maybe_yield();
        }
        setReturnS32(ctx, 0);
    }

    void SetEventFlag(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int eid = static_cast<int>(getRegU32(ctx, 4));
        uint32_t bits = getRegU32(ctx, 5);
        auto info = lookupEventFlagInfo(eid);
        if (!info)
        {
            setReturnS32(ctx, KE_UNKNOWN_EVFID);
            return;
        }

        if (bits == 0)
        {
            setReturnS32(ctx, KE_OK);
            return;
        }

        std::vector<std::pair<int, ps2sched::FiberToken>> setEvfWaiters;
        {
            std::unique_lock<std::mutex> lock(info->m);
            info->bits |= bits;
            // Collect all waiting threads (they'll re-evaluate the condition on wake).
            // Don't clear waitList yet — each waiter removes itself on wake.
            setEvfWaiters = info->waitList;
            lock.unlock();
        }

        for (const auto& [tid, token] : setEvfWaiters)
        {
            ps2sched::enqueue_external_wakeup_validated(tid, token);
        }
        if (!setEvfWaiters.empty())
        {
            ps2sched::maybe_yield();
        }
        setReturnS32(ctx, 0);
    }

    void iSetEventFlag(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        SetEventFlag(rdram, ctx, runtime);
    }

    void ClearEventFlag(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int eid = static_cast<int>(getRegU32(ctx, 4));
        uint32_t bits = getRegU32(ctx, 5);
        auto info = lookupEventFlagInfo(eid);
        if (!info)
        {
            setReturnS32(ctx, KE_UNKNOWN_EVFID);
            return;
        }

        {
            std::lock_guard<std::mutex> lock(info->m);
            info->bits &= bits;
        }
        setReturnS32(ctx, KE_OK);
    }

    void iClearEventFlag(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        ClearEventFlag(rdram, ctx, runtime);
    }

    void WaitEventFlag(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int eid = static_cast<int>(getRegU32(ctx, 4));
        uint32_t waitBits = getRegU32(ctx, 5);
        uint32_t mode = getRegU32(ctx, 6);
        uint32_t resBitsAddr = getRegU32(ctx, 7);

        if ((mode & ~WEF_MODE_MASK) != 0)
        {
            setReturnS32(ctx, KE_ILLEGAL_MODE);
            return;
        }

        if (waitBits == 0)
        {
            setReturnS32(ctx, KE_EVF_ILPAT);
            return;
        }

        auto info = lookupEventFlagInfo(eid);
        if (!info)
        {
            setReturnS32(ctx, KE_UNKNOWN_EVFID);
            return;
        }

        uint32_t *resBitsPtr = resBitsAddr ? reinterpret_cast<uint32_t *>(getMemPtr(rdram, resBitsAddr)) : nullptr;

        std::unique_lock<std::mutex> lock(info->m);
        if ((info->attr & EA_MULTI) == 0 && info->waiters > 0)
        {
            setReturnS32(ctx, KE_EVF_MULTI);
            return;
        }

        // Guest identity is keyed off g_currentThreadId, NOT fiber-ness (see
        // WaitSema for the full rationale). onFiber remains the gate only for
        // the genuinely scheduler-only parts below (arm_park / the fiber Mesa
        // loop); a non-fiber thread with a valid tInfo still takes the
        // bounded-backoff retry loop.
        const bool onFiber = (ps2fiber_current() != nullptr);
        std::shared_ptr<ThreadInfo> tInfo =
            (g_currentThreadId != -1) ? ensureCurrentThreadInfo(ctx) : nullptr;
        throwIfTerminated(tInfo);
        int ret = KE_OK;

        auto satisfied = [&]()
        {
            if (tInfo && tInfo->forceRelease.load())
                return true;
            if (tInfo && tInfo->terminated.load())
                return true;
            if (info->deleted)
            {
                return true;
            }
            if (mode & WEF_OR)
            {
                return (info->bits & waitBits) != 0;
            }
            return (info->bits & waitBits) == waitBits;
        };

        if (!satisfied())
        {
            if (!onFiber)
            {
                // Non-fiber path (IRQ/alarm worker, or — since tInfo is now keyed
                // off g_currentThreadId rather than onFiber — any host thread
                // carrying a real guest tid, e.g. a raw std::thread standing in
                // for a guest thread in tests): Mesa-style re-block loop,
                // mirroring WaitSema's non-fiber handling. We cannot park a host
                // worker on the fiber scheduler, so bounded exponential backoff
                // (NonFiberBackoff) stands in for arm_park/block_current's real
                // parking, re-checking satisfied() under info->m every iteration.
                //
                // A thread with a valid guest identity (tInfo != nullptr) also
                // publishes itself to info->waitList and keeps its ThreadInfo
                // status in THS_WAIT/THS_WAITSUSPEND for the duration, so
                // ReferEventFlagStatus's numThreads count and ReleaseWaitThread's
                // target lookup both see it — matching WaitSema. Off-fiber,
                // current_fiber_token() is FiberToken{}; SetEventFlag's wake fan-out
                // tolerates FiberToken{} (enqueue_external_wakeup_validated drops it):
                // this waiter doesn't need the wake, it re-polls every backoff
                // step. A true borrowed worker (tInfo == nullptr) never publishes
                // and relies solely on satisfied() re-checks, same as before.
                if (tInfo)
                {
                    std::lock_guard<std::mutex> tLock(tInfo->m);
                    tInfo->status = (tInfo->suspendCount > 0) ? THS_WAITSUSPEND : THS_WAIT;
                    tInfo->waitType = TSW_EVENT;
                    tInfo->waitId = eid;
                    tInfo->forceRelease = false;
                }

                NonFiberBackoff nfBackoff;
                for (;;)
                {
                    if (tInfo)
                    {
                        bool release = false;
                        {
                            std::lock_guard<std::mutex> tLock(tInfo->m);
                            if (tInfo->forceRelease)
                            {
                                tInfo->forceRelease = false;
                                release = true;
                            }
                        }
                        if (release)
                        {
                            ret = KE_RELEASE_WAIT;
                            break;
                        }
                    }

                    // info->waiters counts every thread genuinely blocked here —
                    // fiber, non-fiber-with-identity, or fully borrowed
                    // (g_currentThreadId == -1) — so ReferEventFlagStatus's
                    // numThreads and the EA_MULTI exclusivity gate above are
                    // accurate for all of them. It carries no identity, so unlike
                    // waitList it has no tid-aliasing hazard and is safe to bump
                    // unconditionally (mirrors WaitSema's sema->waiters).
                    info->waiters++;
                    if (tInfo)
                    {
                        info->waitList.emplace_back(g_currentThreadId, ps2sched::current_fiber_token());
                    }

                    lock.unlock();
                    const ps2sched::BlockResult br = nfBackoff.wait(false);
                    lock.lock();

                    if (tInfo)
                    {
                        auto &wl = info->waitList;
                        auto it = std::find_if(wl.begin(), wl.end(),
                            [](const std::pair<int, ps2sched::FiberToken>& e){ return e.first == g_currentThreadId; });
                        if (it != wl.end()) wl.erase(it);
                    }
                    info->waiters--;

                    if (tInfo)
                    {
                        std::lock_guard<std::mutex> tLock(tInfo->m);
                        if (tInfo->forceRelease)
                        {
                            tInfo->forceRelease = false;
                            ret = KE_RELEASE_WAIT;
                        }
                    }

                    if (tInfo && tInfo->terminated.load())
                    {
                        throw ThreadExitException();
                    }

                    if (ret != KE_OK || info->deleted || satisfied())
                        break;
                }

                if (tInfo)
                {
                    std::lock_guard<std::mutex> tLock(tInfo->m);
                    tInfo->status = (tInfo->suspendCount > 0) ? THS_SUSPEND : THS_RUN;
                    tInfo->waitType = TSW_NONE;
                    tInfo->waitId = 0;
                }
            }
            else
            {
                // Mesa-style re-block loop for fibers. SetEventFlag wakes ALL
                // waiters including those that waited AND bits that were only
                // partially satisfied; re-check the condition on every wake and
                // re-publish to the wait-list if still unsatisfied.
                for (;;)
                {
                    // Update thread wait state, unless a ReleaseWaitThread already
                    // raced in since our last check. Status flips back to RUN/SUSPEND
                    // at the bottom of every iteration (below) and only becomes WAIT
                    // again right here, so in principle no legitimate forceRelease
                    // can be pending at this exact point (see WaitSema for the
                    // general hazard this guards against). Check-and-consume first
                    // anyway, defensively: if it is somehow already set, take the
                    // same released-exit path as the post-wake check below instead
                    // of blindly clearing it and re-parking.
                    bool release = false;
                    if (tInfo)
                    {
                        std::lock_guard<std::mutex> tLock(tInfo->m);
                        if (tInfo->forceRelease)
                        {
                            tInfo->forceRelease = false;
                            release = true;
                        }
                        else
                        {
                            tInfo->status = (tInfo->suspendCount > 0) ? THS_WAITSUSPEND : THS_WAIT;
                            tInfo->waitType = TSW_EVENT;
                            tInfo->waitId = eid;
                        }
                    }
                    if (release)
                    {
                        ret = KE_RELEASE_WAIT;
                        break;
                    }

                    // Publish under info->m; arm_park after unlock (no nested locks).
                    info->waiters++;
                    info->waitList.emplace_back(g_currentThreadId, ps2sched::current_fiber_token());

                    // UNLOCK before any scheduler operation.
                    lock.unlock();
                    ps2sched::arm_park();
                    const ps2sched::BlockResult br = ps2sched::block_current();
                    if (br != ps2sched::BlockResult::Parked &&
                        br != ps2sched::BlockResult::WokenInWindow)
                    {
                        std::fprintf(stderr,
                            "FATAL [WaitEventFlag]: unexpected NonFiber result in fiber Mesa loop\n");
                        std::terminate();
                    }

                    lock.lock();

                    // Remove self from wait-list (re-added at top of loop if not satisfied).
                    {
                        auto &wl = info->waitList;
                        auto it = std::find_if(wl.begin(), wl.end(),
                            [](const std::pair<int, ps2sched::FiberToken>& e){ return e.first == g_currentThreadId; });
                        if (it != wl.end()) wl.erase(it);
                        info->waiters--;
                    }

                    if (tInfo)
                    {
                        std::lock_guard<std::mutex> tLock(tInfo->m);
                        tInfo->status = (tInfo->suspendCount > 0) ? THS_SUSPEND : THS_RUN;
                        tInfo->waitType = TSW_NONE;
                        tInfo->waitId = 0;
                        if (tInfo->forceRelease)
                        {
                            tInfo->forceRelease = false;
                            ret = KE_RELEASE_WAIT;
                        }
                    }

                    if (tInfo && tInfo->terminated.load())
                    {
                        throw ThreadExitException();
                    }

                    // Exit if forceRelease/terminate/delete broke us out, OR
                    // if the condition is now satisfied.
                    if (ret != KE_OK || info->deleted || satisfied())
                        break;

                    // Spurious wake (SetEventFlag set only a subset of AND bits):
                    // loop back to re-publish and re-block.
                }
            }
        }

        if (ret == KE_OK && info->deleted)
        {
            ret = KE_WAIT_DELETE;
        }

        if (ret == KE_OK)
        {
            if (resBitsPtr)
            {
                *resBitsPtr = info->bits;
            }

            if (mode & WEF_CLEAR_ALL)
            {
                info->bits = 0;
            }
            else if (mode & WEF_CLEAR)
            {
                info->bits &= ~waitBits;
            }
        }

        lock.unlock();
        waitWhileSuspended(tInfo);
        setReturnS32(ctx, ret);
    }

    void PollEventFlag(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int eid = static_cast<int>(getRegU32(ctx, 4));
        uint32_t waitBits = getRegU32(ctx, 5);
        uint32_t mode = getRegU32(ctx, 6);
        uint32_t resBitsAddr = getRegU32(ctx, 7);

        if ((mode & ~WEF_MODE_MASK) != 0)
        {
            setReturnS32(ctx, KE_ILLEGAL_MODE);
            return;
        }

        if (waitBits == 0)
        {
            setReturnS32(ctx, KE_EVF_ILPAT);
            return;
        }

        auto info = lookupEventFlagInfo(eid);
        if (!info)
        {
            setReturnS32(ctx, KE_UNKNOWN_EVFID);
            return;
        }

        uint32_t *resBitsPtr = resBitsAddr ? reinterpret_cast<uint32_t *>(getMemPtr(rdram, resBitsAddr)) : nullptr;

        std::lock_guard<std::mutex> lock(info->m);
        if ((info->attr & EA_MULTI) == 0 && info->waiters > 0)
        {
            setReturnS32(ctx, KE_EVF_MULTI);
            return;
        }

        bool ok = false;
        if (mode & WEF_OR)
        {
            ok = (info->bits & waitBits) != 0;
        }
        else
        {
            ok = (info->bits & waitBits) == waitBits;
        }

        if (!ok)
        {
            setReturnS32(ctx, KE_EVF_COND);
            return;
        }

        if (resBitsPtr)
        {
            *resBitsPtr = info->bits;
        }

        if (mode & WEF_CLEAR_ALL)
        {
            info->bits = 0;
        }
        else if (mode & WEF_CLEAR)
        {
            info->bits &= ~waitBits;
        }

        setReturnS32(ctx, KE_OK);
    }

    void iPollEventFlag(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        PollEventFlag(rdram, ctx, runtime);
    }

    void ReferEventFlagStatus(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int eid = static_cast<int>(getRegU32(ctx, 4));
        uint32_t infoAddr = getRegU32(ctx, 5);

        struct Ps2EventFlagInfo
        {
            uint32_t attr;
            uint32_t option;
            uint32_t initBits;
            uint32_t currBits;
            int32_t numThreads;
            int32_t reserved1;
            int32_t reserved2;
        };

        auto info = lookupEventFlagInfo(eid);
        if (!info)
        {
            setReturnS32(ctx, KE_UNKNOWN_EVFID);
            return;
        }

        Ps2EventFlagInfo *out = infoAddr ? reinterpret_cast<Ps2EventFlagInfo *>(getMemPtr(rdram, infoAddr)) : nullptr;
        if (!out)
        {
            setReturnS32(ctx, -1);
            return;
        }

        std::lock_guard<std::mutex> lock(info->m);
        out->attr = info->attr;
        out->option = info->option;
        out->initBits = info->initBits;
        out->currBits = info->bits;
        out->numThreads = info->waiters;
        out->reserved1 = 0;
        out->reserved2 = 0;
        setReturnS32(ctx, 0);
    }

    void iReferEventFlagStatus(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        ReferEventFlagStatus(rdram, ctx, runtime);
    }

    static void alarmWorkerMain()
    {
        g_currentThreadId = -1; // host worker, not a fiber
        for (;;)
        {
            std::shared_ptr<AlarmInfo> readyAlarm;
            {
                std::unique_lock<std::mutex> lock(g_alarm_mutex);
                while (!readyAlarm)
                {
                    if (g_alarm_stop_flag.load(std::memory_order_acquire))
                        return; // stop requested -> exit, fire nothing more
                    if (g_alarms.empty())
                    {
                        g_alarm_cv.wait(lock);
                        continue;
                    }

                    auto nextIt = std::min_element(
                        g_alarms.begin(), g_alarms.end(),
                        [](const auto &a, const auto &b)
                        { return a.second->dueAt < b.second->dueAt; });
                    if (nextIt == g_alarms.end())
                    {
                        g_alarm_cv.wait(lock);
                        continue;
                    }

                    const auto now = std::chrono::steady_clock::now();
                    // Copy the deadline out of the AlarmInfo BEFORE waiting.
                    // condition_variable::wait_until() releases `lock` internally
                    // while it sleeps, and CancelAlarm/ReleaseAlarm only need
                    // g_alarm_mutex to erase (and destroy) this same AlarmInfo. A
                    // reference into `nextIt->second->dueAt` held across that
                    // unlocked window would dangle the instant CancelAlarm's erase
                    // runs concurrently — this local copy is ours, not the map's.
                    const auto deadline = nextIt->second->dueAt;
                    if (deadline > now)
                    {
                        g_alarm_cv.wait_until(lock, deadline);
                        continue;
                    }

                    readyAlarm = nextIt->second;
                    g_alarms.erase(nextIt);
                }
            }

            // After dropping the lock, re-check stop before touching rdram.
            // If stop was requested we must NOT invoke the callback (rdram /
            // runtime may be torn down).
            if (g_alarm_stop_flag.load(std::memory_order_acquire))
                return;

            if (!readyAlarm || !readyAlarm->runtime || !readyAlarm->rdram ||
                !readyAlarm->handler)
                continue;
            if (!readyAlarm->runtime->hasFunction(readyAlarm->handler))
                continue;

            try
            {
                constexpr uint32_t kAlarmCallbackStackSize = 0x4000u;
                thread_local PS2Runtime *s_alarmStackRuntime = nullptr;
                thread_local uint32_t s_alarmStackTop = 0u;
                if (s_alarmStackRuntime != readyAlarm->runtime || s_alarmStackTop == 0u)
                {
                    s_alarmStackRuntime = readyAlarm->runtime;
                    s_alarmStackTop = readyAlarm->runtime->reserveAsyncCallbackStack(
                        kAlarmCallbackStackSize, 16u);
                }

                R5900Context callbackCtx{};
                setRegU32(&callbackCtx, 28, readyAlarm->gp);
                // Failure fallback: see kAsyncCallbackFallbackSp.
                setRegU32(&callbackCtx, 29,
                          (s_alarmStackTop != 0u) ? s_alarmStackTop
                                                  : kAsyncCallbackFallbackSp);
                setRegU32(&callbackCtx, 31, 0);
                setRegU32(&callbackCtx, 4, static_cast<uint32_t>(readyAlarm->id));
                setRegU32(&callbackCtx, 5, static_cast<uint32_t>(readyAlarm->ticks));
                setRegU32(&callbackCtx, 6, readyAlarm->commonArg);
                setRegU32(&callbackCtx, 7, 0);
                callbackCtx.pc = readyAlarm->handler;

                PS2Runtime::RecompiledFunction func =
                    readyAlarm->runtime->lookupFunction(readyAlarm->handler);
                {
                    AsyncGuestScope guestScope; // token released even if func throws
                    func(readyAlarm->rdram, &callbackCtx, readyAlarm->runtime);
                }
            }
            catch (const ThreadExitException &)
            {
            }
            catch (const std::exception &e)
            {
                static int alarmExceptionLogs = 0;
                if (alarmExceptionLogs < 8)
                {
                    std::cerr << "[SetAlarm] callback exception: " << e.what() << std::endl;
                    ++alarmExceptionLogs;
                }
            }
        }
    }

    void ensureAlarmWorkerRunning()
    {
        std::lock_guard<std::mutex> lock(g_alarm_mutex);
        if (!g_alarm_worker_running)
        {
            g_alarm_stop_flag.store(false, std::memory_order_release);
            g_alarm_thread = std::thread(alarmWorkerMain);
            g_alarm_worker_running = true;
        }
    }

    void signalAlarmWorkerStop()
    {
        {
            std::lock_guard<std::mutex> lock(g_alarm_mutex);
            if (!g_alarm_worker_running)
                return; // never started or already stopped
        }
        g_alarm_stop_flag.store(true, std::memory_order_release);
        g_alarm_cv.notify_all();
        // No join, and g_alarm_worker_running stays true: the worker re-checks
        // the stop flag before invoking any callback, and scheduler_shutdown()'s
        // stopAlarmWorker() performs the join on the main thread (see Sync.h).
    }

    void stopAlarmWorker()
    {
        {
            std::lock_guard<std::mutex> lock(g_alarm_mutex);
            if (!g_alarm_worker_running)
                return; // never started or already stopped
        }
        g_alarm_stop_flag.store(true, std::memory_order_release);
        g_alarm_cv.notify_all();
        if (g_alarm_thread.joinable())
            g_alarm_thread.join(); // wait for the worker to fully exit
        std::lock_guard<std::mutex> lock(g_alarm_mutex);
        g_alarm_worker_running = false;
    }

    void SetAlarm(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint16_t ticks = static_cast<uint16_t>(getRegU32(ctx, 4) & 0xFFFFu);
        uint32_t handler = getRegU32(ctx, 5);
        uint32_t arg = getRegU32(ctx, 6);

        if (!runtime || !handler || !runtime->hasFunction(handler))
        {
            setReturnS32(ctx, KE_ERROR);
            return;
        }

        auto info = std::make_shared<AlarmInfo>();
        info->ticks = ticks;
        info->handler = handler;
        info->commonArg = arg;
        info->gp = getRegU32(ctx, 28);
        info->sp = getRegU32(ctx, 29);
        info->rdram = rdram;
        info->runtime = runtime;
        info->dueAt = std::chrono::steady_clock::now() + alarmTicksToDuration(ticks);

        int alarmId = 0;
        {
            std::lock_guard<std::mutex> lock(g_alarm_mutex);
            alarmId = g_nextAlarmId++;
            if (g_nextAlarmId <= 0)
            {
                g_nextAlarmId = 1;
            }
            info->id = alarmId;
            g_alarms[alarmId] = info;
        }

        ensureAlarmWorkerRunning();
        g_alarm_cv.notify_all();
        setReturnS32(ctx, alarmId);
    }

    void InitAlarm(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 0);
    }

    void iSetAlarm(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        SetAlarm(rdram, ctx, runtime);
    }

    void CancelAlarm(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int alarmId = static_cast<int>(getRegU32(ctx, 4));
        if (alarmId <= 0)
        {
            setReturnS32(ctx, KE_ERROR);
            return;
        }

        bool removed = false;
        {
            std::lock_guard<std::mutex> lock(g_alarm_mutex);
            removed = g_alarms.erase(alarmId) != 0;
        }

        if (removed)
        {
            g_alarm_cv.notify_all();
            setReturnS32(ctx, KE_OK);
            return;
        }

        setReturnS32(ctx, KE_ERROR);
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
        iCancelAlarm(rdram, ctx, runtime);
    }
}
