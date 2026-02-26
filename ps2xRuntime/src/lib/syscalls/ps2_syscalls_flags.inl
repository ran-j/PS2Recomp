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
    std::cout << "[CreateSema] id=" << id << " init=" << init << " max=" << max << std::endl;
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

    {
        std::lock_guard<std::mutex> lock(sema->m);
        sema->deleted = true;
    }
    sema->cv.notify_all();

    setReturnS32(ctx, KE_OK);
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

    int ret = KE_OK;
    int beforeCount = 0;
    int afterCount = 0;
    {
        std::lock_guard<std::mutex> lock(sema->m);
        beforeCount = sema->count;
        if (sema->count >= sema->maxCount)
        {
            ret = KE_SEMA_OVF;
        }
        else
        {
            sema->count++;
            sema->cv.notify_one();
        }
        afterCount = sema->count;
    }

    static std::atomic<uint32_t> s_signalSemaLogs{0};
    const uint32_t sigLog = s_signalSemaLogs.fetch_add(1, std::memory_order_relaxed);
    if (sigLog < 256u)
    {
        std::cout << "[SignalSema] tid=" << g_currentThreadId
                  << " sid=" << sid
                  << " count=" << beforeCount << "->" << afterCount
                  << " ret=" << ret
                  << std::endl;
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

    auto info = ensureCurrentThreadInfo(ctx);
    throwIfTerminated(info);
    std::unique_lock<std::mutex> lock(sema->m);
    int ret = 0;

    if (sema->count == 0)
    {
        static std::atomic<uint32_t> s_waitSemaBlockLogs{0};
        const uint32_t blockLog = s_waitSemaBlockLogs.fetch_add(1, std::memory_order_relaxed);
        if (blockLog < 256u)
        {
            std::cout << "[WaitSema:block] tid=" << g_currentThreadId
                      << " sid=" << sid
                      << " pc=0x" << std::hex << ctx->pc
                      << " ra=0x" << getRegU32(ctx, 31)
                      << std::dec
                      << std::endl;
        }

        if (info)
        {
            std::lock_guard<std::mutex> tLock(info->m);
            info->status = (info->suspendCount > 0) ? THS_WAITSUSPEND : THS_WAIT;
            info->waitType = TSW_SEMA;
            info->waitId = sid;
            info->forceRelease = false;
        }

        sema->waiters++;
        sema->cv.wait(lock, [&]()
                      {
                          bool forced = info ? info->forceRelease.load() : false;
                          bool terminated = info ? info->terminated.load() : false;
                          return sema->count > 0 || sema->deleted || forced || terminated; //
                      });
        sema->waiters--;
        if (sema->deleted)
        {
            ret = KE_WAIT_DELETE;
        }

        if (info)
        {
            std::lock_guard<std::mutex> tLock(info->m);
            info->status = (info->suspendCount > 0) ? THS_SUSPEND : THS_RUN;
            info->waitType = TSW_NONE;
            info->waitId = 0;
            if (info->forceRelease)
            {
                info->forceRelease = false;
                ret = KE_RELEASE_WAIT;
            }
        }

        if (info && info->terminated.load())
        {
            throw ThreadExitException();
        }
    }

    if (ret == 0 && sema->count > 0)
    {
        sema->count--;
    }

    static std::atomic<uint32_t> s_waitSemaWakeLogs{0};
    const uint32_t wakeLog = s_waitSemaWakeLogs.fetch_add(1, std::memory_order_relaxed);
    if (wakeLog < 256u)
    {
        std::cout << "[WaitSema:wake] tid=" << g_currentThreadId
                  << " sid=" << sid
                  << " ret=" << ret
                  << " count=" << sema->count
                  << std::endl;
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
        setReturnS32(ctx, KE_OK);
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
    const uint32_t *param = reinterpret_cast<const uint32_t *>(getConstMemPtr(rdram, paramAddr));

    auto info = std::make_shared<EventFlagInfo>();
    if (param)
    {
        info->attr = param[0];
        info->option = param[1];
        info->initBits = param[2];
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

    {
        std::lock_guard<std::mutex> lock(info->m);
        info->deleted = true;
    }
    info->cv.notify_all();
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

    uint32_t newBits = 0u;
    {
        std::lock_guard<std::mutex> lock(info->m);
        info->bits |= bits;
        newBits = info->bits;
    }

    static std::atomic<uint32_t> s_setEventFlagLogs{0};
    const uint32_t setLog = s_setEventFlagLogs.fetch_add(1, std::memory_order_relaxed);
    if (setLog < 256u)
    {
        std::cout << "[SetEventFlag] tid=" << g_currentThreadId
                  << " eid=" << eid
                  << " bits=0x" << std::hex << bits
                  << " newBits=0x" << newBits
                  << std::dec << std::endl;
    }
    info->cv.notify_all();
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
    info->cv.notify_all();
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

    auto tInfo = ensureCurrentThreadInfo(ctx);
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
        static std::atomic<uint32_t> s_waitEventBlockLogs{0};
        const uint32_t evBlockLog = s_waitEventBlockLogs.fetch_add(1, std::memory_order_relaxed);
        if (evBlockLog < 256u)
        {
            std::cout << "[WaitEventFlag:block] tid=" << g_currentThreadId
                      << " eid=" << eid
                      << " waitBits=0x" << std::hex << waitBits
                      << " mode=0x" << mode
                      << " bits=0x" << info->bits
                      << " pc=0x" << ctx->pc
                      << " ra=0x" << getRegU32(ctx, 31)
                      << std::dec
                      << std::endl;
        }

        if (tInfo)
        {
            std::lock_guard<std::mutex> tLock(tInfo->m);
            tInfo->status = THS_WAIT;
            tInfo->waitType = TSW_EVENT;
            tInfo->waitId = eid;
            tInfo->forceRelease = false;
        }

        info->waiters++;
        info->cv.wait(lock, satisfied);
        info->waiters--;

        if (tInfo)
        {
            std::lock_guard<std::mutex> tLock(tInfo->m);
            tInfo->status = THS_RUN;
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
    }

    if (ret == KE_OK && info->deleted)
    {
        ret = KE_WAIT_DELETE;
    }

    if (ret == KE_OK && resBitsPtr)
    {
        *resBitsPtr = info->bits;
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

    static std::atomic<uint32_t> s_waitEventWakeLogs{0};
    const uint32_t evWakeLog = s_waitEventWakeLogs.fetch_add(1, std::memory_order_relaxed);
    if (evWakeLog < 256u)
    {
        std::cout << "[WaitEventFlag:wake] tid=" << g_currentThreadId
                  << " eid=" << eid
                  << " ret=" << ret
                  << " bits=0x" << std::hex << info->bits
                  << std::dec
                  << std::endl;
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

void SetAlarm(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    uint16_t ticks = static_cast<uint16_t>(getRegU32(ctx, 4) & 0xFFFFu);
    uint32_t handler = getRegU32(ctx, 5);
    uint32_t arg = getRegU32(ctx, 6);

    static int logCount = 0;
    if (logCount < 5)
    {
        std::cout << "[SetAlarm] ticks=" << ticks
                  << " handler=0x" << std::hex << handler
                  << " arg=0x" << arg << std::dec << std::endl;
        ++logCount;
    }

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
