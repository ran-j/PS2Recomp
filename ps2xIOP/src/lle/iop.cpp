#include "iop.h"

#include <algorithm>
#include <cstring>

namespace ps2x::iop::lle
{
    namespace
    {
        constexpr uint32_t kTrapBase = 0x1000u;       // return traps, one word each
        constexpr uint32_t kCtypeBase = 0x1100u;      // sysclib's character class table
        constexpr uint32_t kInterruptStack = 0x8000u; // top of the interrupt context stack
        constexpr uint32_t kHeapBase = 0x10000u;
        constexpr uint32_t kSpuDmaLines[2] = {36u, 40u}; // INUM_DMA_4, INUM_DMA_7
        constexpr uint32_t kSpuLine = 9u;

        uint32_t syscallWord(uint32_t code) { return 0x0000000Cu | (code << 6); }
    }

    Iop::Iop(EeLink &ee) : m_ee(ee), m_bus(*this), m_cpu(m_bus), m_spu2(*this)
    {
        installKernel();
        uint8_t *ram = m_bus.ram();
        // Threads and handlers return here; the trap tells the kernel they are done.
        m_returnTrap = kTrapBase;
        const uint32_t returnWord = syscallWord(handlerFor("", 1));
        std::memcpy(ram + kTrapBase, &returnWord, 4);
        m_rpcTrap = kTrapBase + 8u;
        const uint32_t rpcWord = syscallWord(handlerFor("", 2));
        std::memcpy(ram + m_rpcTrap, &rpcWord, 4);
        // The ctype table sysclib hands out: bit 2 digit, 1 lower, 0 upper, 3 space, 6 hex.
        m_ctypeTable = kCtypeBase;
        for (uint32_t ch = 0; ch < 256u; ++ch)
        {
            uint8_t flags = 0;
            if (ch >= '0' && ch <= '9') flags |= 0x04u;
            if (ch >= 'a' && ch <= 'z') flags |= 0x02u;
            if (ch >= 'A' && ch <= 'Z') flags |= 0x01u;
            if (ch == ' ' || (ch >= 9u && ch <= 13u)) flags |= 0x08u;
            if ((ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F')) flags |= 0x40u;
            ram[kCtypeBase + 1u + ch] = flags;
        }
        m_allocations[0] = kHeapBase; // everything below the heap is the kernel's
    }

    Iop::~Iop() = default;

    std::string Iop::readString(uint32_t address, size_t limit)
    {
        std::string text;
        for (size_t index = 0; index < limit; ++index)
        {
            const uint8_t ch = m_bus.read8(address + static_cast<uint32_t>(index));
            if (ch == 0u)
                break;
            text.push_back(static_cast<char>(ch));
        }
        return text;
    }

    uint32_t Iop::arg(CpuContext &c, unsigned index)
    {
        return index < 4u ? c.gpr[4 + index] : m_bus.read32(c.gpr[29] + 16u + (index - 4u) * 4u);
    }

    uint32_t Iop::allocate(uint32_t size)
    {
        size = (size + 255u) & ~255u;
        uint32_t cursor = kHeapBase;
        for (const auto &[address, length] : m_allocations)
        {
            if (address >= cursor && address - cursor >= size)
                break;
            cursor = std::max(cursor, (address + length + 255u) & ~255u);
        }
        if (cursor + size > Bus::kRamBytes)
            return 0;
        m_allocations[cursor] = size;
        return cursor;
    }

    void Iop::release(uint32_t address)
    {
        if (address >= kHeapBase)
            m_allocations.erase(address);
    }

    void Iop::writeMemory(uint32_t address, const void *data, uint32_t size)
    {
        address &= Bus::kRamBytes - 1u;
        size = std::min(size, Bus::kRamBytes - address);
        std::memcpy(m_bus.ram() + address, data, size);
    }

    void Iop::readMemory(uint32_t address, void *data, uint32_t size)
    {
        address &= Bus::kRamBytes - 1u;
        size = std::min(size, Bus::kRamBytes - address);
        std::memcpy(data, m_bus.ram() + address, size);
    }

    // ------------------------------------------------------------------ hardware

    uint32_t Iop::ioRead(uint32_t physical, unsigned bytes)
    {
        if (physical >= 0x1F900000u && physical < 0x1F900800u)
        {
            uint32_t value = m_spu2.read(physical & 0x7FFu);
            if (bytes == 4u)
                value |= static_cast<uint32_t>(m_spu2.read((physical + 2u) & 0x7FFu)) << 16;
            return value;
        }
        int channel = 0;
        if (uint32_t *reg = dmaRegister(physical, channel))
            return *reg >> ((physical & 3u) * 8u);
        return 0;
    }

    uint32_t *Iop::dmaRegister(uint32_t physical, int &channel)
    {
        for (channel = 0; channel < 2; ++channel)
        {
            const uint32_t base = channel == 0 ? 0x1F8010C0u : 0x1F801500u;
            if (physical >= base && physical < base + 12u)
            {
                DmaChannel &dma = m_dma[channel];
                const uint32_t offset = physical - base;
                return offset < 4u ? &dma.madr : offset < 8u ? &dma.bcr : &dma.chcr;
            }
        }
        return nullptr;
    }

    void Iop::ioWrite(uint32_t physical, uint32_t value, unsigned bytes)
    {
        if (physical >= 0x1F900000u && physical < 0x1F900800u)
        {
            m_spu2.write(physical & 0x7FFu, static_cast<uint16_t>(value));
            if (bytes == 4u)
                m_spu2.write((physical + 2u) & 0x7FFu, static_cast<uint16_t>(value >> 16));
            return;
        }
        // libsd writes the block count and size as separate halfwords.
        int channel = 0;
        if (uint32_t *reg = dmaRegister(physical, channel))
        {
            const uint32_t shift = (physical & 3u) * 8u;
            const uint32_t mask = (bytes >= 4u ? 0xFFFFFFFFu : (1u << (bytes * 8u)) - 1u) << shift;
            *reg = (*reg & ~mask) | ((value << shift) & mask);
            DmaChannel &dma = m_dma[channel];
            dma.madr &= 0x00FFFFFFu;
            if (reg == &dma.chcr && (mask & value << shift & 0x01000000u) != 0u)
                dmaTransfer(channel);
        }
    }

    void Iop::dmaTransfer(int channel)
    {
        DmaChannel &dma = m_dma[channel];
        const uint32_t blockWords = dma.bcr & 0xFFFFu;
        const uint32_t blocks = std::max<uint32_t>(dma.bcr >> 16, 1u);
        const uint32_t bytes = blockWords * blocks * 4u;
        m_spu2.dmaStarted(channel);
        if ((dma.chcr & 1u) != 0u && m_spu2.autoDmaEnabled(channel))
        {
            // AutoDMA: the SPU2 pulls 1 KiB at a time as it plays.
            dma.remaining = bytes;
            return;
        }
        const uint32_t address = dma.madr & (Bus::kRamBytes - 1u);
        const uint32_t length = std::min(bytes, Bus::kRamBytes - address);
        auto *words = reinterpret_cast<uint16_t *>(m_bus.ram() + address);
        if ((dma.chcr & 1u) != 0u)
            m_spu2.dmaWrite(channel, words, length / 2u);
        else
            m_spu2.dmaRead(channel, words, length / 2u);
        dma.madr = (dma.madr + length) & 0x00FFFFFFu;
        dma.chcr &= ~0x01000000u;
        raiseInterrupt(kSpuDmaLines[channel]);
    }

    bool Iop::fetchAutoDma(int core, uint16_t *block)
    {
        DmaChannel &dma = m_dma[core];
        if ((dma.chcr & 0x01000000u) == 0u || dma.remaining < 1024u)
            return false;
        readMemory(dma.madr, block, 1024u);
        dma.madr = (dma.madr + 1024u) & 0x00FFFFFFu;
        dma.remaining -= 1024u;
        if (dma.remaining < 1024u)
        {
            dma.chcr &= ~0x01000000u;
            raiseInterrupt(kSpuDmaLines[core]);
        }
        return true;
    }

    void Iop::raiseSpuInterrupt() { raiseInterrupt(kSpuLine); }

    void Iop::raiseInterrupt(uint32_t line)
    {
        if (std::find(m_pendingInterrupts.begin(), m_pendingInterrupts.end(), line) == m_pendingInterrupts.end())
            m_pendingInterrupts.push_back(line);
    }

    // ------------------------------------------------------------------ execution

    void Iop::call(uint32_t function, std::initializer_list<uint32_t> arguments, uint32_t *result, uint32_t gp)
    {
        CpuContext context{};
        context.pc = function;
        unsigned index = 4;
        for (const uint32_t value : arguments)
            context.gpr[index++] = value;
        context.gpr[28] = gp;
        context.gpr[29] = kInterruptStack - 16u;
        context.gpr[31] = m_returnTrap;
        const bool nested = m_inInterrupt;
        m_inInterrupt = true;
        // Handlers run between samples, off the clock like RPC work.
        uint64_t spent = 0;
        const uint64_t limit = 4'000'000u;
        for (;;)
        {
            const StopReason reason = m_cpu.run(context, spent, limit);
            if (reason != StopReason::Syscall)
            {
                m_ee.log("[iop] handler at " + std::to_string(function) + " did not return");
                break;
            }
            const uint32_t code = m_cpu.syscallCode();
            if (code < m_handlers.size() && m_handlers[code] == &Iop::functionReturn)
                break;
            context.pc = context.gpr[31];
            if (code < m_handlers.size())
                (this->*m_handlers[code])(context);
        }
        m_inInterrupt = nested;
        if (result)
            *result = context.gpr[2];
    }

    void Iop::serviceInterrupts()
    {
        if (m_intrSuspend > 0)
            return;
        while (!m_pendingInterrupts.empty())
        {
            const uint32_t line = m_pendingInterrupts.front();
            m_pendingInterrupts.erase(m_pendingInterrupts.begin());
            const auto found = m_interrupts.find(line);
            if (found == m_interrupts.end() || !found->second.enabled || found->second.handler == 0u)
                continue;
            uint32_t result = 0;
            call(found->second.handler, {found->second.argument}, &result, found->second.gp);
            // A handler returning zero leaves its line disabled.
            if (result == 0u)
                found->second.enabled = false;
            reschedule();
        }
    }

    void Iop::serviceEvents()
    {
        // Threads sleeping in DelayThread.
        for (auto &[id, thread] : m_threads)
            if (thread.wait == Wait::Delay && thread.wakeCycle <= m_cycle)
                makeReady(thread, 0u);

        // Alarms: the handler returns the next interval, or zero to stop.
        for (auto it = m_alarms.begin(); it != m_alarms.end();)
        {
            if (it->second.cycle > m_cycle)
            {
                ++it;
                continue;
            }
            const Alarm alarm = it->second;
            it = m_alarms.erase(it);
            uint32_t next = 0;
            call(alarm.handler, {alarm.argument}, &next, alarm.gp);
            if (next != 0u)
                m_alarms.emplace(alarm.handler, Alarm{m_cycle + next, alarm.handler, alarm.argument, alarm.gp});
            reschedule();
        }

        // Hardware timers: the handler returns the next compare value, or zero to stop.
        for (auto &timer : m_timers)
        {
            if (!timer.running || timer.handler == 0u || timer.next > m_cycle)
                continue;
            uint32_t compare = 0;
            call(timer.handler, {timer.argument}, &compare, timer.gp);
            if (compare == 0u)
                timer.running = false;
            else
            {
                timer.compare = compare;
                timer.next = m_cycle + static_cast<uint64_t>(compare) * timer.prescale;
            }
            reschedule();
        }
    }

    Iop::Thread *Iop::pickThread()
    {
        Thread *best = nullptr;
        for (auto &[id, thread] : m_threads)
        {
            if (thread.dormant || thread.wait != Wait::None)
                continue;
            if (!best || thread.priority < best->priority ||
                (thread.priority == best->priority && thread.readySequence < best->readySequence))
                best = &thread;
        }
        return best;
    }

    void Iop::makeReady(Thread &thread, uint32_t result)
    {
        thread.wait = Wait::None;
        thread.context.gpr[2] = result;
        thread.readySequence = ++m_readySequence;
        reschedule();
    }

    void Iop::block(Wait wait, int32_t id)
    {
        Thread *thread = current();
        if (!thread || m_inInterrupt)
        {
            m_ee.log("[iop] blocking call outside a thread");
            return;
        }
        thread->wait = wait;
        thread->waitId = id;
        reschedule();
    }

    bool Iop::runThreads(uint64_t budget, bool advanceClock)
    {
        // Only the sample clock moves time. Work done off it, answering an
        // RPC the moment it arrives, would otherwise run the timers fast.
        uint64_t offClock = 0;
        uint64_t &clock = advanceClock ? m_cycle : offClock;
        bool ran = false;
        const uint64_t end = clock + budget;
        while (clock < end)
        {
            serviceInterrupts();
            Thread *thread = m_intrSuspend > 0 && current() && current()->wait == Wait::None ? current() : pickThread();
            if (!thread)
                break;
            m_current = thread->id;
            m_reschedule = false;
            ran = true;
            while (!m_reschedule && clock < end)
            {
                const StopReason reason = m_cpu.run(thread->context, clock, end);
                if (reason == StopReason::Budget)
                    break;
                if (reason == StopReason::Fault)
                {
                    m_ee.log("[iop] thread " + std::to_string(thread->id) + " stopped on an unsupported instruction");
                    thread->dormant = true;
                    reschedule();
                    break;
                }
                const uint32_t code = m_cpu.syscallCode();
                thread->context.pc = thread->context.gpr[31];
                if (code < m_handlers.size())
                    (this->*m_handlers[code])(thread->context);
                if (thread->dormant || thread->wait != Wait::None)
                    break;
            }
            deliverRequests();
        }
        m_current = 0;
        return ran;
    }

    void Iop::settle()
    {
        deliverRequests();
        // Enough for any command handler; threads spinning on hardware stop here.
        runThreads(2'000'000u, false);
    }

    void Iop::step(int16_t &left, int16_t &right)
    {
        const uint64_t target = (m_cycle / kCyclesPerSample + 1u) * kCyclesPerSample;
        serviceEvents();
        deliverRequests();
        runThreads(target > m_cycle ? target - m_cycle : 0u);
        m_cycle = std::max(m_cycle, target);
        m_spu2.tick(left, right);
        serviceInterrupts();
    }

    // ------------------------------------------------------------------ RPC

    bool Iop::hasServer(uint32_t sid) const { return m_servers.count(sid) != 0u; }

    bool Iop::server(uint32_t sid, uint32_t &record, uint32_t &buffer) const
    {
        const auto found = m_servers.find(sid);
        if (found == m_servers.end())
            return false;
        record = found->second.record;
        buffer = found->second.buffer;
        return true;
    }

    void Iop::submit(std::shared_ptr<Request> request) { m_requests.push_back(std::move(request)); }

    void Iop::deliverRequests()
    {
        for (auto it = m_requests.begin(); it != m_requests.end();)
        {
            const auto server = m_servers.find((*it)->sid);
            if (server == m_servers.end())
            {
                (*it)->done = true; // nobody serves it; the EE side falls back
                it = m_requests.erase(it);
                continue;
            }
            const auto owner = m_threads.find(server->second.thread);
            if (owner == m_threads.end() || owner->second.wait != Wait::Rpc || m_active.count(owner->first) != 0u)
            {
                ++it;
                continue;
            }
            Thread &thread = owner->second;
            const Request &request = **it;
            if (server->second.buffer != 0u && !request.send.empty())
                writeMemory(server->second.buffer, request.send.data(), static_cast<uint32_t>(request.send.size()));
            // Call the server function on top of the loop's own frame.
            m_saved[thread.id] = thread.context;
            CpuContext &c = thread.context;
            c.pc = server->second.function;
            c.gpr[4] = request.function;
            c.gpr[5] = server->second.buffer;
            c.gpr[6] = static_cast<uint32_t>(request.send.size());
            c.gpr[29] -= 64u;
            c.gpr[31] = m_rpcTrap;
            c.branchPending = false;
            c.loadRegister = 0;
            m_active[thread.id] = *it;
            makeReady(thread, 0u);
            it = m_requests.erase(it);
        }
    }

    void Iop::finishRequest(Thread &thread, uint32_t resultAddress)
    {
        const auto active = m_active.find(thread.id);
        if (active == m_active.end())
            return;
        Request &request = *active->second;
        if (request.receiveSize != 0u && request.receiveAddress != 0u && resultAddress != 0u)
        {
            std::vector<uint8_t> reply(request.receiveSize);
            readMemory(resultAddress, reply.data(), request.receiveSize);
            m_ee.writeEe(request.receiveAddress, reply.data(), request.receiveSize);
        }
        request.done = true;
        m_active.erase(active);
        thread.context = m_saved[thread.id];
        thread.wait = Wait::Rpc;
        reschedule();
    }

    // ------------------------------------------------------------------ modules

    void Iop::linkImports(const std::vector<IrxImport> &imports)
    {
        for (const IrxImport &import : imports)
        {
            uint32_t word = 0;
            const auto exported = m_exports.find(import.library);
            if (exported != m_exports.end() && import.ordinal < exported->second.size() &&
                exported->second[import.ordinal] != 0u)
                word = 0x08000000u | ((exported->second[import.ordinal] >> 2) & 0x03FFFFFFu); // j target
            else
                word = syscallWord(handlerFor(import.library, import.ordinal));
            m_bus.write32(import.stubAddress, word);
        }
    }

    int32_t Iop::loadModule(const std::vector<uint8_t> &file, const std::string &path,
                            const std::vector<std::string> &arguments)
    {
        IrxImage irx;
        std::string error;
        if (!parseIrx(file, irx, error))
        {
            m_ee.log("[iop] " + path + ": " + error);
            return -1;
        }
        const uint32_t size = static_cast<uint32_t>(irx.image.size()) + irx.bssSize;
        const uint32_t base = allocate(size);
        std::vector<IrxImport> imports;
        if (base == 0u || !relocateIrx(irx, m_bus.ram(), Bus::kRamBytes, base, imports, error))
        {
            m_ee.log("[iop] " + path + ": " + (base == 0u ? std::string("out of IOP memory") : error));
            release(base);
            return -1;
        }
        linkImports(imports);

        // argv: the path followed by the arguments, as C strings.
        std::vector<std::string> argv{path};
        argv.insert(argv.end(), arguments.begin(), arguments.end());
        uint32_t bytes = static_cast<uint32_t>(argv.size() + 1u) * 4u;
        for (const auto &text : argv)
            bytes += static_cast<uint32_t>(text.size()) + 1u;
        const uint32_t block = allocate(bytes);
        uint32_t strings = block + static_cast<uint32_t>(argv.size() + 1u) * 4u;
        for (size_t index = 0; index < argv.size(); ++index)
        {
            m_bus.write32(block + static_cast<uint32_t>(index) * 4u, strings);
            writeMemory(strings, argv[index].c_str(), static_cast<uint32_t>(argv[index].size()) + 1u);
            strings += static_cast<uint32_t>(argv[index].size()) + 1u;
        }
        m_bus.write32(block + static_cast<uint32_t>(argv.size()) * 4u, 0u);

        // The start function runs in a thread of its own, like MODLOAD's.
        Thread loader;
        loader.id = m_nextId++;
        loader.priority = loader.initialPriority = 8;
        loader.stackSize = 0x2000u;
        loader.stackBase = allocate(loader.stackSize);
        loader.gp = base + irx.gp;
        loader.dormant = false;
        loader.context.pc = base + irx.entry;
        loader.context.gpr[4] = static_cast<uint32_t>(argv.size());
        loader.context.gpr[5] = block;
        loader.context.gpr[28] = loader.gp;
        loader.context.gpr[29] = loader.stackBase + loader.stackSize - 16u;
        loader.context.gpr[31] = m_returnTrap;
        loader.readySequence = ++m_readySequence;
        const int32_t loaderId = loader.id;
        m_threads[loaderId] = loader;
        m_loaderResult = 0xFFFFFFFFu;
        m_loaderThread = loaderId;
        for (int round = 0; round < 64 && m_threads.count(loaderId) && !m_threads.at(loaderId).dormant; ++round)
            runThreads(2'000'000u);
        const uint32_t result = m_loaderResult;
        m_loaderThread = 0;
        if (m_threads.count(loaderId))
        {
            release(m_threads.at(loaderId).stackBase);
            m_threads.erase(loaderId);
        }
        release(block);

        Module module{m_nextId++, irx.name, base, size};
        if ((result & 3u) == 1u)
        {
            // NO_RESIDENT_END: the module is done and leaves nothing behind.
            release(base);
            m_ee.log("[iop] " + irx.name + " did not stay resident");
            return module.id;
        }
        m_modules.push_back(module);
        m_ee.log("[iop] started " + irx.name + " at " + std::to_string(base));
        return module.id;
    }
}
