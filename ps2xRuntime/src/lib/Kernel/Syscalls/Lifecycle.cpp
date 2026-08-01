#include "Common.h"
#include "Interrupt.h"
#include "Lifecycle.h"
#include "Sync.h"

namespace ps2_syscalls
{
    using namespace interrupt_state;

    void notifyRuntimeStop()
    {
        // requestStop can be invoked from GUEST context (e.g. the
        // unimplemented-function default handler runs on the guest executor
        // thread, inside a fiber). Joining a host worker from there can
        // deadlock: a worker blocked in async_guest_begin() waits for
        // g_running_fiber == nullptr, which cannot happen while the joining
        // fiber IS the running fiber. In that case only signal the workers to
        // stop; scheduler_shutdown() joins them later on the main thread
        // (both stop functions are idempotent).
        const bool fromGuestExecutor = ps2sched::is_guest_thread();
        if (fromGuestExecutor)
        {
            signalInterruptWorkerStop();
        }
        else
        {
            stopInterruptWorker();
        }
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

        std::vector<std::pair<int, std::shared_ptr<ThreadInfo>>> threads;
        threads.reserve(32);
        {
            std::lock_guard<std::mutex> lock(g_thread_map_mutex);
            for (const auto &entry : g_threads)
            {
                if (entry.second)
                {
                    threads.emplace_back(entry.first, entry.second);
                }
            }
            g_threads.clear();
            g_nextThreadId = 2; // Reserve id 1 for main thread.
        }
        // Consume each reaped thread's activeCounted token rather than
        // force-storing g_activeThreads to 0: a worker fiber that is still
        // genuinely running is a concurrent, legitimate consumer of that SAME
        // token through on_fiber_exit, since it holds the same ThreadInfo
        // shared_ptr as this snapshot. The atomic exchange arbitrates the two
        // sides so only the winner performs the matching fetch_sub. If
        // on_fiber_exit wins, it decrements and this loop's exchange finds the
        // token already false (no-op); if this loop wins, on_fiber_exit later
        // sees the token gone (or a null ThreadInfo, since g_threads was
        // cleared above) and skips its own decrement.
        for (const auto &[tid, threadInfo] : threads)
        {
            consumeActiveToken(threadInfo);
        }
        // -1 is the "not a guest fiber" sentinel: a host thread running
        // notifyRuntimeStop must never be mistaken for a real guest thread id
        // by arm_park / wait-list operations. Do NOT clobber it when called
        // from guest context, though — there g_currentThreadId identifies the
        // still-running fiber on the executor thread, and zapping it would
        // corrupt that fiber's identity for the rest of its unwind.
        if (!fromGuestExecutor)
        {
            g_currentThreadId = -1;
        }

        for (const auto &[tid, threadInfo] : threads)
        {
            {
                std::lock_guard<std::mutex> lock(threadInfo->m);
                threadInfo->forceRelease = true;
                threadInfo->terminated = true;
            }
            ps2sched::request_terminate(tid);
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
            g_semas.clear();
            g_nextSemaId = 1;
        }
        for (const auto &sema : semas)
        {
            wakeWaiters(sema->m, sema->waitList);
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
            g_eventFlags.clear();
            g_nextEventFlagId = 1;
        }
        for (const auto &eventFlag : eventFlags)
        {
            wakeWaiters(eventFlag->m, eventFlag->waitList);
        }

        // Stop and JOIN the alarm worker BEFORE clearing alarms, so no callback can
        // fire against rdram/runtime that is about to be destroyed. From guest
        // context, signal-only (see the comment at the top of this function):
        // the worker re-checks the stop flag before invoking any callback, and
        // scheduler_shutdown() performs the join on the main thread.
        if (fromGuestExecutor)
        {
            ps2_syscalls::signalAlarmWorkerStop();
        }
        else
        {
            ps2_syscalls::stopAlarmWorker();
        }
        {
            std::lock_guard<std::mutex> lock(g_alarm_mutex);
            g_alarms.clear();
        }
        g_alarm_cv.notify_all();

        {
            std::lock_guard<std::mutex> lock(g_exit_handler_mutex);
            g_exit_handlers.clear();
        }
        {
            std::lock_guard<std::mutex> lock(g_syscall_override_mutex);
            g_syscall_overrides.clear();
        }
    }

}
