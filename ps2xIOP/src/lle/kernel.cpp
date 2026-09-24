#include "iop.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace ps2x::iop::lle
{
    namespace
    {
        // About 100 us, a SIF command to the EE and its answer.
        constexpr uint64_t kSifRoundTrip = Iop::kClock / 10'000u;

        struct Binding
        {
            const char *library;
            uint16_t ordinal;
            const char *handler;
        };

        // Library ordinals as the IOP kernel exports them.
        constexpr Binding kBindings[] = {
            {"sysmem", 4, "allocSysMemory"}, {"sysmem", 5, "freeSysMemory"},
            {"sysmem", 7, "queryMaxFreeMemSize"}, {"sysmem", 8, "queryTotalFreeMemSize"},
            {"sysmem", 14, "kprintf"},
            {"loadcore", 4, "noop"}, {"loadcore", 5, "noop"},
            {"loadcore", 6, "registerLibraryEntries"}, {"loadcore", 7, "releaseLibraryEntries"},
            {"intrman", 4, "registerIntrHandler"}, {"intrman", 5, "releaseIntrHandler"},
            {"intrman", 6, "enableIntr"}, {"intrman", 7, "disableIntr"},
            {"intrman", 17, "cpuSuspendIntr"}, {"intrman", 18, "cpuResumeIntr"},
            {"intrman", 23, "queryIntrContext"},
            {"stdio", 4, "kprintf"},
            {"sysclib", 8, "lookCtypeTable"}, {"sysclib", 11, "memcmpHandler"},
            {"sysclib", 12, "memcpyHandler"}, {"sysclib", 13, "memmoveHandler"},
            {"sysclib", 14, "memsetHandler"}, {"sysclib", 17, "bzeroHandler"},
            {"sysclib", 22, "strcmpHandler"}, {"sysclib", 23, "strcpyHandler"},
            {"sysclib", 27, "strlenHandler"}, {"sysclib", 29, "strncmpHandler"},
            {"sysclib", 30, "strncpyHandler"}, {"sysclib", 36, "strtolHandler"},
            {"thbase", 4, "createThread"}, {"thbase", 5, "deleteThread"}, {"thbase", 6, "startThread"},
            {"thbase", 8, "exitThread"}, {"thbase", 10, "terminateThread"}, {"thbase", 11, "terminateThread"},
            {"thbase", 14, "changeThreadPriority"}, {"thbase", 15, "changeThreadPriority"},
            {"thbase", 16, "rotateThreadReadyQueue"}, {"thbase", 17, "rotateThreadReadyQueue"},
            {"thbase", 20, "getThreadId"}, {"thbase", 22, "referThreadStatus"},
            {"thbase", 23, "referThreadStatus"}, {"thbase", 24, "sleepThread"},
            {"thbase", 25, "wakeupThread"}, {"thbase", 26, "wakeupThread"},
            {"thbase", 27, "cancelWakeupThread"}, {"thbase", 28, "cancelWakeupThread"},
            {"thbase", 33, "delayThread"}, {"thbase", 34, "getSystemTime"},
            {"thbase", 35, "setAlarm"}, {"thbase", 36, "setAlarm"},
            {"thbase", 37, "cancelAlarm"}, {"thbase", 38, "cancelAlarm"},
            {"thbase", 39, "usecToSysClock"}, {"thbase", 40, "sysClockToUsec"},
            {"thsemap", 4, "createSema"}, {"thsemap", 5, "deleteSema"}, {"thsemap", 6, "signalSema"},
            {"thsemap", 7, "signalSema"}, {"thsemap", 8, "waitSema"}, {"thsemap", 9, "pollSema"},
            {"thsemap", 10, "pollSema"}, {"thsemap", 11, "referSemaStatus"}, {"thsemap", 12, "referSemaStatus"},
            {"thevent", 4, "createEventFlag"}, {"thevent", 5, "deleteEventFlag"},
            {"thevent", 6, "setEventFlag"}, {"thevent", 7, "setEventFlag"},
            {"thevent", 8, "clearEventFlag"}, {"thevent", 9, "clearEventFlag"},
            {"thevent", 10, "waitEventFlag"}, {"thevent", 11, "pollEventFlag"},
            {"thmsgbx", 4, "createMbx"}, {"thmsgbx", 5, "deleteMbx"}, {"thmsgbx", 6, "sendMbx"},
            {"thmsgbx", 7, "sendMbx"}, {"thmsgbx", 8, "receiveMbx"}, {"thmsgbx", 9, "pollMbx"},
            {"sifman", 5, "noop"}, {"sifman", 7, "sifSetDma"}, {"sifman", 8, "sifDmaStat"},
            {"sifman", 29, "sifCheckInit"},
            {"sifcmd", 4, "noop"}, {"sifcmd", 14, "noop"}, {"sifcmd", 15, "sifBindRpc"},
            {"sifcmd", 16, "sifCallRpc"}, {"sifcmd", 17, "sifRegisterRpc"}, {"sifcmd", 19, "sifSetRpcQueue"},
            {"sifcmd", 22, "sifRpcLoop"}, {"sifcmd", 23, "sifGetOtherData"}, {"sifcmd", 24, "sifRemoveRpc"},
            {"sifcmd", 25, "noop"},
            {"timrman", 4, "allocHardTimer"}, {"timrman", 6, "freeHardTimer"},
            {"timrman", 10, "getTimerCounter"}, {"timrman", 20, "setTimerHandler"},
            {"timrman", 22, "setupHardTimer"}, {"timrman", 23, "startHardTimer"},
            {"timrman", 24, "stopHardTimer"},
        };

        struct Named
        {
            const char *name;
            void (Iop::*handler)(CpuContext &);
        };
    }

    void Iop::installKernel()
    {
#define PS2X_IOP_HANDLER(name) m_handlers.push_back(&Iop::name);
#include "kernel_handlers.inc"
#undef PS2X_IOP_HANDLER
    }

    uint32_t Iop::handlerFor(const std::string &library, uint16_t ordinal)
    {
        static const std::vector<std::string> names = {
#define PS2X_IOP_HANDLER(name) #name,
#include "kernel_handlers.inc"
#undef PS2X_IOP_HANDLER
        };
        const auto index = [&](const char *name) {
            return static_cast<uint32_t>(std::find(names.begin(), names.end(), name) - names.begin());
        };
        if (library.empty())
            return index(ordinal == 1u ? "functionReturn" : "rpcReturn");
        for (const Binding &binding : kBindings)
            if (library == binding.library && ordinal == binding.ordinal)
                return index(binding.handler);
        m_ee.log("[iop] no kernel service for " + library + " #" + std::to_string(ordinal));
        return index("unknownImport");
    }

    void Iop::unknownImport(CpuContext &c) { ret(c, 0); }
    void Iop::noop(CpuContext &c) { ret(c, 0); }

    void Iop::functionReturn(CpuContext &c)
    {
        // A thread's entry function returned: that is ExitThread.
        Thread *thread = current();
        if (!thread)
            return;
        if (thread->id == m_loaderThread)
            m_loaderResult = c.gpr[2];
        thread->dormant = true;
        reschedule();
    }

    void Iop::rpcReturn(CpuContext &c)
    {
        if (Thread *thread = current())
            finishRequest(*thread, c.gpr[2]);
    }

    // ---------------------------------------------------------------- sysmem, loadcore, stdio

    void Iop::allocSysMemory(CpuContext &c) { ret(c, allocate(arg(c, 1))); }

    void Iop::freeSysMemory(CpuContext &c)
    {
        release(arg(c, 0));
        ret(c, 0);
    }

    void Iop::queryMaxFreeMemSize(CpuContext &c)
    {
        uint32_t largest = 0, cursor = 0;
        for (const auto &[address, length] : m_allocations)
        {
            largest = std::max(largest, address > cursor ? address - cursor : 0u);
            cursor = std::max(cursor, address + length);
        }
        ret(c, std::max(largest, Bus::kRamBytes - cursor));
    }

    void Iop::queryTotalFreeMemSize(CpuContext &c)
    {
        uint32_t used = 0;
        for (const auto &[address, length] : m_allocations)
            used += length;
        ret(c, Bus::kRamBytes - used);
    }

    void Iop::kprintf(CpuContext &c)
    {
        // Only the format string; enough to follow what the drivers report.
        std::string text = readString(arg(c, 0));
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
            text.pop_back();
        if (!text.empty())
            m_ee.log("[iop] " + text);
        ret(c, 0);
    }

    void Iop::registerLibraryEntries(CpuContext &c)
    {
        const uint32_t table = arg(c, 0);
        std::string name;
        for (uint32_t index = 0; index < 8u; ++index)
        {
            const uint8_t ch = m_bus.read8(table + 12u + index);
            if (ch == 0u)
                break;
            name.push_back(static_cast<char>(ch));
        }
        std::vector<uint32_t> functions;
        for (uint32_t entry = table + 20u; entry < table + 20u + 1024u; entry += 4u)
        {
            const uint32_t address = m_bus.read32(entry);
            if (address == 0u)
                break;
            functions.push_back(address);
        }
        m_exports[name] = std::move(functions);
        ret(c, 0);
    }

    void Iop::releaseLibraryEntries(CpuContext &c) { ret(c, 0); }

    // ---------------------------------------------------------------- intrman

    void Iop::registerIntrHandler(CpuContext &c)
    {
        Interrupt &line = m_interrupts[arg(c, 0)];
        line.mode = arg(c, 1);
        line.handler = arg(c, 2);
        line.argument = arg(c, 3);
        line.gp = c.gpr[28];
        ret(c, 0);
    }

    void Iop::releaseIntrHandler(CpuContext &c)
    {
        m_interrupts.erase(arg(c, 0));
        ret(c, 0);
    }

    void Iop::enableIntr(CpuContext &c)
    {
        m_interrupts[arg(c, 0) & 0xFFu].enabled = true;
        ret(c, 0);
    }

    void Iop::disableIntr(CpuContext &c)
    {
        auto &line = m_interrupts[arg(c, 0) & 0xFFu];
        if (arg(c, 1) != 0u)
            m_bus.write32(arg(c, 1), line.enabled ? arg(c, 0) : 0u);
        line.enabled = false;
        ret(c, 0);
    }

    void Iop::cpuSuspendIntr(CpuContext &c)
    {
        if (arg(c, 0) != 0u)
            m_bus.write32(arg(c, 0), m_intrSuspend > 0 ? 0u : 1u);
        ++m_intrSuspend;
        ret(c, m_intrSuspend > 1 ? static_cast<uint32_t>(-102) : 0u); // KE_CPUDI when already off
    }

    void Iop::cpuResumeIntr(CpuContext &c)
    {
        if (arg(c, 0) != 0u && m_intrSuspend > 0)
            m_intrSuspend = 0;
        else if (m_intrSuspend > 0)
            --m_intrSuspend;
        ret(c, 0);
    }

    void Iop::queryIntrContext(CpuContext &c) { ret(c, m_inInterrupt ? 1u : 0u); }

    // ---------------------------------------------------------------- sysclib

    void Iop::lookCtypeTable(CpuContext &c)
    {
        // Returns the class of one character, as the table lookup would.
        ret(c, m_bus.read8(m_ctypeTable + 1u + (arg(c, 0) & 0xFFu)));
    }

    void Iop::memcpyHandler(CpuContext &c)
    {
        const uint32_t destination = arg(c, 0), source = arg(c, 1), size = arg(c, 2);
        for (uint32_t index = 0; index < size; ++index)
            m_bus.write8(destination + index, m_bus.read8(source + index));
        ret(c, destination);
    }

    void Iop::memmoveHandler(CpuContext &c)
    {
        const uint32_t destination = arg(c, 0), source = arg(c, 1), size = arg(c, 2);
        std::vector<uint8_t> copy(size);
        for (uint32_t index = 0; index < size; ++index)
            copy[index] = m_bus.read8(source + index);
        for (uint32_t index = 0; index < size; ++index)
            m_bus.write8(destination + index, copy[index]);
        ret(c, destination);
    }

    void Iop::memsetHandler(CpuContext &c)
    {
        const uint32_t destination = arg(c, 0), size = arg(c, 2);
        for (uint32_t index = 0; index < size; ++index)
            m_bus.write8(destination + index, static_cast<uint8_t>(arg(c, 1)));
        ret(c, destination);
    }

    void Iop::memcmpHandler(CpuContext &c)
    {
        int32_t result = 0;
        for (uint32_t index = 0; index < arg(c, 2) && result == 0; ++index)
            result = static_cast<int32_t>(m_bus.read8(arg(c, 0) + index)) - m_bus.read8(arg(c, 1) + index);
        ret(c, static_cast<uint32_t>(result));
    }

    void Iop::bzeroHandler(CpuContext &c)
    {
        for (uint32_t index = 0; index < arg(c, 1); ++index)
            m_bus.write8(arg(c, 0) + index, 0u);
        ret(c, 0);
    }

    void Iop::strlenHandler(CpuContext &c) { ret(c, static_cast<uint32_t>(readString(arg(c, 0), 65536).size())); }

    void Iop::strcmpHandler(CpuContext &c)
    {
        ret(c, static_cast<uint32_t>(readString(arg(c, 0), 65536).compare(readString(arg(c, 1), 65536))));
    }

    void Iop::strncmpHandler(CpuContext &c)
    {
        const uint32_t count = arg(c, 2);
        const std::string a = readString(arg(c, 0), count), b = readString(arg(c, 1), count);
        ret(c, static_cast<uint32_t>(a.compare(b)));
    }

    void Iop::strcpyHandler(CpuContext &c)
    {
        const std::string text = readString(arg(c, 1), 65536);
        writeMemory(arg(c, 0), text.c_str(), static_cast<uint32_t>(text.size()) + 1u);
        ret(c, arg(c, 0));
    }

    void Iop::strncpyHandler(CpuContext &c)
    {
        const uint32_t count = arg(c, 2);
        std::string text = readString(arg(c, 1), count);
        text.resize(count, '\0');
        writeMemory(arg(c, 0), text.data(), count);
        ret(c, arg(c, 0));
    }

    void Iop::strtolHandler(CpuContext &c)
    {
        const uint32_t start = arg(c, 0);
        const std::string text = readString(start, 64);
        char *end = nullptr;
        const long value = std::strtol(text.c_str(), &end, static_cast<int>(arg(c, 2)));
        if (arg(c, 1) != 0u)
            m_bus.write32(arg(c, 1), start + static_cast<uint32_t>(end - text.c_str()));
        ret(c, static_cast<uint32_t>(value));
    }

    // ---------------------------------------------------------------- threads

    void Iop::createThread(CpuContext &c)
    {
        const uint32_t param = arg(c, 0);
        Thread thread;
        thread.id = m_nextId++;
        thread.attr = m_bus.read32(param);
        thread.option = m_bus.read32(param + 4u);
        thread.entry = m_bus.read32(param + 8u);
        thread.stackSize = (m_bus.read32(param + 12u) + 15u) & ~15u;
        thread.priority = thread.initialPriority = static_cast<int32_t>(m_bus.read32(param + 16u));
        thread.stackBase = allocate(thread.stackSize);
        thread.gp = c.gpr[28];
        if (thread.stackBase == 0u)
        {
            ret(c, static_cast<uint32_t>(-400)); // KE_NO_MEMORY
            return;
        }
        m_threads[thread.id] = thread;
        ret(c, static_cast<uint32_t>(thread.id));
    }

    void Iop::deleteThread(CpuContext &c)
    {
        const auto found = m_threads.find(static_cast<int32_t>(arg(c, 0)));
        if (found == m_threads.end() || !found->second.dormant)
        {
            ret(c, static_cast<uint32_t>(-407));
            return;
        }
        release(found->second.stackBase);
        m_threads.erase(found);
        ret(c, 0);
    }

    void Iop::startThread(CpuContext &c)
    {
        const auto found = m_threads.find(static_cast<int32_t>(arg(c, 0)));
        if (found == m_threads.end())
        {
            ret(c, static_cast<uint32_t>(-407));
            return;
        }
        Thread &thread = found->second;
        thread.context = {};
        thread.context.pc = thread.entry;
        thread.context.gpr[4] = arg(c, 1);
        thread.context.gpr[28] = thread.gp;
        thread.context.gpr[29] = thread.stackBase + thread.stackSize - 16u;
        thread.context.gpr[31] = m_returnTrap;
        thread.priority = thread.initialPriority;
        thread.dormant = false;
        thread.wakeupCount = 0;
        makeReady(thread, 0u);
        ret(c, 0);
    }

    void Iop::exitThread(CpuContext &c)
    {
        (void)c;
        if (Thread *thread = current())
        {
            thread->dormant = true;
            reschedule();
        }
    }

    void Iop::terminateThread(CpuContext &c)
    {
        const auto found = m_threads.find(static_cast<int32_t>(arg(c, 0)));
        if (found != m_threads.end())
        {
            found->second.dormant = true;
            found->second.wait = Wait::None;
            m_active.erase(found->first);
        }
        ret(c, 0);
    }

    void Iop::changeThreadPriority(CpuContext &c)
    {
        const int32_t id = static_cast<int32_t>(arg(c, 0));
        const auto found = m_threads.find(id == 0 ? m_current : id);
        if (found != m_threads.end())
        {
            const int32_t priority = static_cast<int32_t>(arg(c, 1));
            found->second.priority = priority == 0 ? found->second.initialPriority : priority;
            reschedule();
        }
        ret(c, 0);
    }

    void Iop::rotateThreadReadyQueue(CpuContext &c)
    {
        if (Thread *thread = current())
            thread->readySequence = ++m_readySequence;
        reschedule();
        ret(c, 0);
    }

    void Iop::getThreadId(CpuContext &c) { ret(c, static_cast<uint32_t>(m_current)); }

    void Iop::referThreadStatus(CpuContext &c)
    {
        const int32_t id = static_cast<int32_t>(arg(c, 0));
        const auto found = m_threads.find(id == 0 ? m_current : id);
        const uint32_t info = arg(c, 1);
        if (found == m_threads.end() || info == 0u)
        {
            ret(c, static_cast<uint32_t>(-407));
            return;
        }
        const Thread &thread = found->second;
        // struct _iop_thread_status: attr, option, status, entry, stack, stackSize,
        // gpReg, initPriority, currentPriority, waitType, waitId, wakeupCount, ...
        const uint32_t status = thread.dormant ? 0x10u : thread.wait != Wait::None ? 0x04u
                                                : thread.id == m_current ? 0x01u : 0x02u;
        const uint32_t fields[12] = {thread.attr, thread.option, status, thread.entry, thread.stackBase,
                                     thread.stackSize, thread.gp, static_cast<uint32_t>(thread.initialPriority),
                                     static_cast<uint32_t>(thread.priority), 0u, static_cast<uint32_t>(thread.waitId),
                                     static_cast<uint32_t>(thread.wakeupCount)};
        for (uint32_t index = 0; index < 12u; ++index)
            m_bus.write32(info + index * 4u, fields[index]);
        ret(c, 0);
    }

    void Iop::sleepThread(CpuContext &c)
    {
        Thread *thread = current();
        if (thread && thread->wakeupCount > 0)
        {
            --thread->wakeupCount;
            ret(c, 0);
            return;
        }
        ret(c, 0);
        block(Wait::Sleep, 0);
    }

    void Iop::wakeupThread(CpuContext &c)
    {
        const auto found = m_threads.find(static_cast<int32_t>(arg(c, 0)));
        if (found == m_threads.end())
        {
            ret(c, static_cast<uint32_t>(-407));
            return;
        }
        if (found->second.wait == Wait::Sleep)
            makeReady(found->second, 0u);
        else
            ++found->second.wakeupCount;
        ret(c, 0);
    }

    void Iop::cancelWakeupThread(CpuContext &c)
    {
        const int32_t id = static_cast<int32_t>(arg(c, 0));
        const auto found = m_threads.find(id == 0 ? m_current : id);
        int32_t count = 0;
        if (found != m_threads.end())
        {
            count = found->second.wakeupCount;
            found->second.wakeupCount = 0;
        }
        ret(c, static_cast<uint32_t>(count));
    }

    void Iop::delayThread(CpuContext &c)
    {
        Thread *thread = current();
        if (!thread)
            return;
        thread->wakeCycle = m_cycle + static_cast<uint64_t>(arg(c, 0)) * kClock / 1'000'000u;
        ret(c, 0);
        block(Wait::Delay, 0);
    }

    void Iop::getSystemTime(CpuContext &c)
    {
        m_bus.write32(arg(c, 0), static_cast<uint32_t>(m_cycle));
        m_bus.write32(arg(c, 0) + 4u, static_cast<uint32_t>(m_cycle >> 32));
        ret(c, 0);
    }

    void Iop::setAlarm(CpuContext &c)
    {
        // The clock argument points at an iop_sys_clock_t.
        const uint64_t clock = m_bus.read32(arg(c, 0)) | (static_cast<uint64_t>(m_bus.read32(arg(c, 0) + 4u)) << 32);
        m_alarms.emplace(arg(c, 1), Alarm{m_cycle + clock, arg(c, 1), arg(c, 2), c.gpr[28]});
        ret(c, 0);
    }

    void Iop::cancelAlarm(CpuContext &c)
    {
        const auto range = m_alarms.equal_range(arg(c, 0));
        for (auto it = range.first; it != range.second;)
            it = it->second.argument == arg(c, 1) ? m_alarms.erase(it) : std::next(it);
        ret(c, 0);
    }

    void Iop::usecToSysClock(CpuContext &c)
    {
        const uint64_t clock = static_cast<uint64_t>(arg(c, 0)) * kClock / 1'000'000u;
        m_bus.write32(arg(c, 1), static_cast<uint32_t>(clock));
        m_bus.write32(arg(c, 1) + 4u, static_cast<uint32_t>(clock >> 32));
        ret(c, 0);
    }

    void Iop::sysClockToUsec(CpuContext &c)
    {
        const uint64_t clock = m_bus.read32(arg(c, 0)) | (static_cast<uint64_t>(m_bus.read32(arg(c, 0) + 4u)) << 32);
        const uint64_t usec = clock * 1'000'000u / kClock;
        m_bus.write32(arg(c, 1), static_cast<uint32_t>(usec / 1'000'000u));
        m_bus.write32(arg(c, 2), static_cast<uint32_t>(usec % 1'000'000u));
        ret(c, 0);
    }

    // ---------------------------------------------------------------- semaphores

    void Iop::createSema(CpuContext &c)
    {
        const uint32_t param = arg(c, 0);
        Semaphore semaphore;
        semaphore.attr = m_bus.read32(param);
        semaphore.option = m_bus.read32(param + 4u);
        semaphore.count = static_cast<int32_t>(m_bus.read32(param + 8u));
        semaphore.maximum = static_cast<int32_t>(m_bus.read32(param + 12u));
        const int32_t id = m_nextId++;
        m_semaphores[id] = semaphore;
        ret(c, static_cast<uint32_t>(id));
    }

    void Iop::deleteSema(CpuContext &c)
    {
        const int32_t id = static_cast<int32_t>(arg(c, 0));
        for (auto &[threadId, thread] : m_threads)
            if (thread.wait == Wait::Semaphore && thread.waitId == id)
                makeReady(thread, static_cast<uint32_t>(-425)); // KE_WAIT_DELETE
        m_semaphores.erase(id);
        ret(c, 0);
    }

    void Iop::signalSema(CpuContext &c)
    {
        const int32_t id = static_cast<int32_t>(arg(c, 0));
        const auto found = m_semaphores.find(id);
        if (found == m_semaphores.end())
        {
            ret(c, static_cast<uint32_t>(-408));
            return;
        }
        // Wake the longest-waiting thread, or count the signal.
        Thread *waiter = nullptr;
        for (auto &[threadId, thread] : m_threads)
            if (thread.wait == Wait::Semaphore && thread.waitId == id &&
                (!waiter || thread.readySequence < waiter->readySequence))
                waiter = &thread;
        if (waiter)
            makeReady(*waiter, 0u);
        else if (found->second.count < found->second.maximum || found->second.maximum == 0)
            ++found->second.count;
        ret(c, 0);
    }

    void Iop::waitSema(CpuContext &c)
    {
        const int32_t id = static_cast<int32_t>(arg(c, 0));
        const auto found = m_semaphores.find(id);
        if (found == m_semaphores.end())
        {
            ret(c, static_cast<uint32_t>(-408));
            return;
        }
        if (found->second.count > 0)
        {
            --found->second.count;
            ret(c, 0);
            return;
        }
        if (Thread *thread = current())
            thread->readySequence = ++m_readySequence;
        ret(c, 0);
        block(Wait::Semaphore, id);
    }

    void Iop::pollSema(CpuContext &c)
    {
        const auto found = m_semaphores.find(static_cast<int32_t>(arg(c, 0)));
        if (found == m_semaphores.end() || found->second.count <= 0)
        {
            ret(c, static_cast<uint32_t>(-419)); // KE_SEMA_ZERO
            return;
        }
        --found->second.count;
        ret(c, 0);
    }

    void Iop::referSemaStatus(CpuContext &c)
    {
        const auto found = m_semaphores.find(static_cast<int32_t>(arg(c, 0)));
        if (found == m_semaphores.end())
        {
            ret(c, static_cast<uint32_t>(-408));
            return;
        }
        int32_t waiting = 0;
        for (const auto &[threadId, thread] : m_threads)
            waiting += thread.wait == Wait::Semaphore && thread.waitId == found->first ? 1 : 0;
        const uint32_t info = arg(c, 1);
        const uint32_t fields[6] = {found->second.attr, found->second.option, 0u,
                                    static_cast<uint32_t>(found->second.maximum),
                                    static_cast<uint32_t>(found->second.count), static_cast<uint32_t>(waiting)};
        for (uint32_t index = 0; index < 6u; ++index)
            m_bus.write32(info + index * 4u, fields[index]);
        ret(c, 0);
    }

    // ---------------------------------------------------------------- event flags

    void Iop::createEventFlag(CpuContext &c)
    {
        const uint32_t param = arg(c, 0);
        EventFlag flag;
        flag.attr = m_bus.read32(param);
        flag.option = m_bus.read32(param + 4u);
        flag.bits = m_bus.read32(param + 8u);
        const int32_t id = m_nextId++;
        m_eventFlags[id] = flag;
        ret(c, static_cast<uint32_t>(id));
    }

    void Iop::deleteEventFlag(CpuContext &c)
    {
        const int32_t id = static_cast<int32_t>(arg(c, 0));
        for (auto &[threadId, thread] : m_threads)
            if (thread.wait == Wait::EventFlag && thread.waitId == id)
                makeReady(thread, static_cast<uint32_t>(-425));
        m_eventFlags.erase(id);
        ret(c, 0);
    }

    void Iop::setEventFlag(CpuContext &c)
    {
        const int32_t id = static_cast<int32_t>(arg(c, 0));
        const auto found = m_eventFlags.find(id);
        if (found == m_eventFlags.end())
        {
            ret(c, static_cast<uint32_t>(-409));
            return;
        }
        EventFlag &flag = found->second;
        flag.bits |= arg(c, 1);
        for (auto &[threadId, thread] : m_threads)
        {
            if (thread.wait != Wait::EventFlag || thread.waitId != id)
                continue;
            // Mode bit 0: OR (any bit) instead of AND; 0x10 clear all, 0x20 clear pattern.
            const bool satisfied = (thread.waitMode & 1u) ? (flag.bits & thread.waitBits) != 0u
                                                          : (flag.bits & thread.waitBits) == thread.waitBits;
            if (!satisfied)
                continue;
            if (thread.waitResult != 0u)
                m_bus.write32(thread.waitResult, flag.bits);
            if (thread.waitMode & 0x10u)
                flag.bits = 0;
            else if (thread.waitMode & 0x20u)
                flag.bits &= ~thread.waitBits;
            makeReady(thread, 0u);
        }
        ret(c, 0);
    }

    void Iop::clearEventFlag(CpuContext &c)
    {
        const auto found = m_eventFlags.find(static_cast<int32_t>(arg(c, 0)));
        if (found != m_eventFlags.end())
            found->second.bits &= arg(c, 1);
        ret(c, 0);
    }

    void Iop::waitEventFlag(CpuContext &c)
    {
        const int32_t id = static_cast<int32_t>(arg(c, 0));
        const auto found = m_eventFlags.find(id);
        if (found == m_eventFlags.end())
        {
            ret(c, static_cast<uint32_t>(-409));
            return;
        }
        EventFlag &flag = found->second;
        const uint32_t bits = arg(c, 1), mode = arg(c, 2), result = arg(c, 3);
        const bool satisfied = (mode & 1u) ? (flag.bits & bits) != 0u : (flag.bits & bits) == bits;
        if (satisfied)
        {
            if (result != 0u)
                m_bus.write32(result, flag.bits);
            if (mode & 0x10u)
                flag.bits = 0;
            else if (mode & 0x20u)
                flag.bits &= ~bits;
            ret(c, 0);
            return;
        }
        if (Thread *thread = current())
        {
            thread->waitBits = bits;
            thread->waitMode = mode;
            thread->waitResult = result;
        }
        ret(c, 0);
        block(Wait::EventFlag, id);
    }

    void Iop::pollEventFlag(CpuContext &c)
    {
        const auto found = m_eventFlags.find(static_cast<int32_t>(arg(c, 0)));
        if (found == m_eventFlags.end())
        {
            ret(c, static_cast<uint32_t>(-409));
            return;
        }
        EventFlag &flag = found->second;
        const uint32_t bits = arg(c, 1), mode = arg(c, 2), result = arg(c, 3);
        const bool satisfied = (mode & 1u) ? (flag.bits & bits) != 0u : (flag.bits & bits) == bits;
        if (!satisfied)
        {
            ret(c, static_cast<uint32_t>(-421)); // KE_EVF_COND
            return;
        }
        if (result != 0u)
            m_bus.write32(result, flag.bits);
        if (mode & 0x10u)
            flag.bits = 0;
        else if (mode & 0x20u)
            flag.bits &= ~bits;
        ret(c, 0);
    }

    // ---------------------------------------------------------------- message boxes

    void Iop::createMbx(CpuContext &c)
    {
        Mailbox mailbox;
        mailbox.attr = m_bus.read32(arg(c, 0));
        mailbox.option = m_bus.read32(arg(c, 0) + 4u);
        const int32_t id = m_nextId++;
        m_mailboxes[id] = mailbox;
        ret(c, static_cast<uint32_t>(id));
    }

    void Iop::deleteMbx(CpuContext &c)
    {
        m_mailboxes.erase(static_cast<int32_t>(arg(c, 0)));
        ret(c, 0);
    }

    void Iop::sendMbx(CpuContext &c)
    {
        const int32_t id = static_cast<int32_t>(arg(c, 0));
        const auto found = m_mailboxes.find(id);
        if (found == m_mailboxes.end())
        {
            ret(c, static_cast<uint32_t>(-410));
            return;
        }
        for (auto &[threadId, thread] : m_threads)
            if (thread.wait == Wait::Mailbox && thread.waitId == id)
            {
                m_bus.write32(thread.waitResult, arg(c, 1));
                makeReady(thread, 0u);
                ret(c, 0);
                return;
            }
        found->second.messages.push_back(arg(c, 1));
        ret(c, 0);
    }

    void Iop::receiveMbx(CpuContext &c)
    {
        const int32_t id = static_cast<int32_t>(arg(c, 1));
        const auto found = m_mailboxes.find(id);
        if (found == m_mailboxes.end())
        {
            ret(c, static_cast<uint32_t>(-410));
            return;
        }
        if (!found->second.messages.empty())
        {
            m_bus.write32(arg(c, 0), found->second.messages.front());
            found->second.messages.pop_front();
            ret(c, 0);
            return;
        }
        if (Thread *thread = current())
            thread->waitResult = arg(c, 0);
        ret(c, 0);
        block(Wait::Mailbox, id);
    }

    void Iop::pollMbx(CpuContext &c)
    {
        const auto found = m_mailboxes.find(static_cast<int32_t>(arg(c, 1)));
        if (found == m_mailboxes.end() || found->second.messages.empty())
        {
            ret(c, static_cast<uint32_t>(-424)); // KE_MBOX_NOMSG
            return;
        }
        m_bus.write32(arg(c, 0), found->second.messages.front());
        found->second.messages.pop_front();
        ret(c, 0);
    }

    // ---------------------------------------------------------------- SIF

    void Iop::sifSetDma(CpuContext &c)
    {
        // Each descriptor: IOP source, EE destination, size, attributes.
        const uint32_t list = arg(c, 0), count = arg(c, 1);
        for (uint32_t index = 0; index < count; ++index)
        {
            const uint32_t entry = list + index * 16u;
            const uint32_t source = m_bus.read32(entry), destination = m_bus.read32(entry + 4u);
            const uint32_t size = m_bus.read32(entry + 8u);
            std::vector<uint8_t> data(size);
            readMemory(source, data.data(), size);
            m_ee.writeEe(destination, data.data(), size);
        }
        ret(c, ++m_sifDmaId);
    }

    void Iop::sifDmaStat(CpuContext &c) { ret(c, static_cast<uint32_t>(-1)); } // always complete

    void Iop::sifCheckInit(CpuContext &c) { ret(c, 1u); }

    void Iop::sifRegisterRpc(CpuContext &c)
    {
        Server server;
        server.record = arg(c, 0);
        server.sid = arg(c, 1);
        server.function = arg(c, 2);
        server.buffer = arg(c, 3);
        server.queue = arg(c, 6);
        for (const auto &[queue, owner] : m_queues)
            if (queue == server.queue)
                server.thread = owner;
        m_servers[server.sid] = server;
        m_ee.log("[iop] RPC server " + std::to_string(server.sid) + " registered");
        ret(c, 0);
    }

    void Iop::sifSetRpcQueue(CpuContext &c)
    {
        m_queues[arg(c, 0)] = static_cast<int32_t>(arg(c, 1));
        ret(c, 0);
    }

    void Iop::sifRpcLoop(CpuContext &c)
    {
        // The loop never returns; requests are delivered to the waiting thread.
        (void)c;
        block(Wait::Rpc, static_cast<int32_t>(arg(c, 0)));
    }

    void Iop::sifRemoveRpc(CpuContext &c)
    {
        for (auto it = m_servers.begin(); it != m_servers.end();)
            it = it->second.record == arg(c, 0) ? m_servers.erase(it) : std::next(it);
        ret(c, 0);
    }

    void Iop::sifGetOtherData(CpuContext &c)
    {
        // (record, EE source, IOP destination, size, mode)
        const uint32_t source = arg(c, 1), destination = arg(c, 2), size = arg(c, 3);
        std::vector<uint8_t> data(size);
        m_ee.readEe(source, data.data(), size);
        writeMemory(destination, data.data(), size);
        ret(c, 0);
    }

    void Iop::sifBindRpc(CpuContext &c)
    {
        // Callers poll the server field until the EE has registered the id,
        // some in a busy loop, so the call has to wait like the real round
        // trip does or it starves every lower priority thread.
        const uint32_t client = arg(c, 0);
        for (uint32_t offset = 0; offset < 0x28u; offset += 4u)
            m_bus.write32(client + offset, 0u);
        // Only tested against zero here; it names EE memory the IOP never reads.
        if (m_ee.eeServer(arg(c, 1)))
            m_bus.write32(client + 0x24u, 1u);
        ret(c, 0);
        if (Thread *thread = current())
        {
            thread->wakeCycle = m_cycle + kSifRoundTrip;
            block(Wait::Delay, 0);
        }
    }

    void Iop::sifCallRpc(CpuContext &c) { ret(c, static_cast<uint32_t>(-1)); }

    // ---------------------------------------------------------------- hardware timers

    void Iop::allocHardTimer(CpuContext &c)
    {
        // Timers 4 and 5 are the 32-bit ones drivers ask for; hand out any free one.
        for (uint32_t index = 0; index < m_timers.size(); ++index)
            if (!m_timers[index].allocated)
            {
                m_timers[index] = {};
                m_timers[index].allocated = true;
                ret(c, index + 1u);
                return;
            }
        ret(c, static_cast<uint32_t>(-150));
    }

    void Iop::freeHardTimer(CpuContext &c)
    {
        const uint32_t index = arg(c, 0) - 1u;
        if (index < m_timers.size())
            m_timers[index] = {};
        ret(c, 0);
    }

    void Iop::setTimerHandler(CpuContext &c)
    {
        const uint32_t index = arg(c, 0) - 1u;
        if (index < m_timers.size())
        {
            m_timers[index].compare = arg(c, 1);
            m_timers[index].handler = arg(c, 2);
            m_timers[index].argument = arg(c, 3);
            m_timers[index].gp = c.gpr[28];
        }
        ret(c, 0);
    }

    void Iop::setupHardTimer(CpuContext &c)
    {
        // (timer, source, mode, prescale): only the system clock source is used by drivers.
        const uint32_t index = arg(c, 0) - 1u;
        if (index < m_timers.size())
        {
            m_timers[index].source = arg(c, 1);
            m_timers[index].prescale = std::max<uint32_t>(arg(c, 3), 1u);
        }
        ret(c, 0);
    }

    void Iop::startHardTimer(CpuContext &c)
    {
        const uint32_t index = arg(c, 0) - 1u;
        if (index < m_timers.size())
        {
            HardTimer &timer = m_timers[index];
            timer.running = true;
            timer.start = m_cycle;
            timer.next = m_cycle + static_cast<uint64_t>(std::max<uint32_t>(timer.compare, 1u)) * timer.prescale;
        }
        ret(c, 0);
    }

    void Iop::stopHardTimer(CpuContext &c)
    {
        const uint32_t index = arg(c, 0) - 1u;
        if (index < m_timers.size())
            m_timers[index].running = false;
        ret(c, 0);
    }

    void Iop::getTimerCounter(CpuContext &c)
    {
        const uint32_t index = arg(c, 0) - 1u;
        ret(c, index < m_timers.size() ? static_cast<uint32_t>((m_cycle - m_timers[index].start) /
                                                               std::max<uint32_t>(m_timers[index].prescale, 1u))
                                       : 0u);
    }
}
