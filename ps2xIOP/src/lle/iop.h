#pragma once

#include "bus.h"
#include "cpu.h"
#include "irx.h"
#include "spu2.h"

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace ps2x::iop::lle
{
    // What the emulated IOP needs from the EE side.
    class EeLink
    {
    public:
        virtual ~EeLink() = default;
        virtual bool readEe(uint32_t address, void *destination, uint32_t size) = 0;
        virtual bool writeEe(uint32_t address, const void *source, uint32_t size) = 0;
        // An RPC server the EE has registered, for IOP modules that bind back.
        virtual bool eeServer(uint32_t sid) = 0;
        virtual void log(const std::string &message) = 0;
    };

    // An IOP running original IRX modules on an R3000A interpreter with a
    // small high-level kernel underneath. Time is counted in IOP cycles
    // (36.864 MHz) and advances as the SPU2 produces 48 kHz samples.
    class Iop final : public IoDevice, public Spu2Host
    {
    public:
        static constexpr uint64_t kClock = 36'864'000u;
        static constexpr uint32_t kCyclesPerSample = 768u;

        explicit Iop(EeLink &ee);
        ~Iop() override;

        // Load and start an IRX. Returns a positive module id, or a negative error.
        int32_t loadModule(const std::vector<uint8_t> &file, const std::string &path,
                           const std::vector<std::string> &arguments);

        // EE-side SIF RPC into a server registered by an IOP module.
        bool hasServer(uint32_t sid) const;
        bool server(uint32_t sid, uint32_t &record, uint32_t &buffer) const;
        struct Request
        {
            uint32_t sid = 0;
            uint32_t function = 0;
            std::vector<uint8_t> send;
            uint32_t receiveAddress = 0; // EE address the reply goes to
            uint32_t receiveSize = 0;
            bool done = false;
        };
        // Queue a request; the server thread handles it as the IOP runs.
        void submit(std::shared_ptr<Request> request);

        // EE DMA into IOP memory (sceSifSetDma with an IOP destination).
        void writeMemory(uint32_t address, const void *data, uint32_t size);
        void readMemory(uint32_t address, void *data, uint32_t size);
        uint32_t allocate(uint32_t size);
        void release(uint32_t address);

        // Run until every ready thread has blocked, without advancing time.
        void settle();
        // Advance time by one output sample and produce it.
        void step(int16_t &left, int16_t &right);

        // IoDevice
        uint32_t ioRead(uint32_t physical, unsigned bytes) override;
        void ioWrite(uint32_t physical, uint32_t value, unsigned bytes) override;
        // Spu2Host
        bool fetchAutoDma(int core, uint16_t *block) override;
        void raiseSpuInterrupt() override;

    private:
        enum class Wait : uint8_t
        {
            None,
            Sleep,
            Delay,
            Semaphore,
            EventFlag,
            Mailbox,
            Rpc,
        };

        struct Thread
        {
            int32_t id = 0;
            uint32_t entry = 0, stackBase = 0, stackSize = 0, gp = 0, attr = 0, option = 0;
            int32_t priority = 0, initialPriority = 0;
            CpuContext context;
            bool dormant = true;
            Wait wait = Wait::None;
            int32_t waitId = 0;
            uint32_t waitBits = 0, waitMode = 0, waitResult = 0;
            uint64_t wakeCycle = 0;
            int32_t wakeupCount = 0;
            uint64_t readySequence = 0;
        };

        struct Semaphore
        {
            int32_t count = 0, maximum = 0;
            uint32_t attr = 0, option = 0;
        };

        struct EventFlag
        {
            uint32_t bits = 0, attr = 0, option = 0;
        };

        struct Mailbox
        {
            uint32_t attr = 0, option = 0;
            std::deque<uint32_t> messages;
        };

        struct Alarm
        {
            uint64_t cycle = 0;
            uint32_t handler = 0, argument = 0, gp = 0;
        };

        struct HardTimer
        {
            bool allocated = false, running = false;
            uint32_t source = 0, prescale = 1, compare = 0, handler = 0, argument = 0, gp = 0;
            uint64_t start = 0, next = 0;
        };

        struct Interrupt
        {
            uint32_t handler = 0, argument = 0, mode = 0, gp = 0;
            bool enabled = false;
        };

        struct Server
        {
            uint32_t sid = 0, function = 0, buffer = 0, queue = 0, record = 0;
            int32_t thread = 0;
        };

        struct DmaChannel
        {
            uint32_t madr = 0, bcr = 0, chcr = 0;
            uint32_t remaining = 0; // bytes left for AutoDMA
        };

        struct Module
        {
            int32_t id = 0;
            std::string name;
            uint32_t base = 0, size = 0;
        };

        using Handler = void (Iop::*)(CpuContext &);

        // Kernel
        void installKernel();
        uint32_t handlerFor(const std::string &library, uint16_t ordinal);
        void linkImports(const std::vector<IrxImport> &imports);
        void call(uint32_t function, std::initializer_list<uint32_t> arguments, uint32_t *result, uint32_t gp);
        bool runThreads(uint64_t budget, bool advanceClock = true);
        Thread *current() { return m_current ? &m_threads.at(m_current) : nullptr; }
        void makeReady(Thread &thread, uint32_t result);
        void block(Wait wait, int32_t id);
        void reschedule() { m_reschedule = true; }
        Thread *pickThread();
        void deliverRequests();
        void finishRequest(Thread &thread, uint32_t resultAddress);
        void raiseInterrupt(uint32_t line);
        void serviceEvents();
        void serviceInterrupts();
        void dmaTransfer(int channel);
        uint32_t *dmaRegister(uint32_t physical, int &channel);
        uint32_t arg(CpuContext &c, unsigned index);
        void ret(CpuContext &c, uint32_t value) { c.gpr[2] = value; }
        std::string readString(uint32_t address, size_t limit = 256);

#define PS2X_IOP_HANDLER(name) void name(CpuContext &c);
#include "kernel_handlers.inc"
#undef PS2X_IOP_HANDLER

        EeLink &m_ee;
        Bus m_bus;
        Cpu m_cpu;
        Spu2 m_spu2;
        uint64_t m_cycle = 0;

        std::vector<Handler> m_handlers;
        std::unordered_map<std::string, std::vector<uint32_t>> m_exports;
        std::map<int32_t, Thread> m_threads;
        std::map<int32_t, Semaphore> m_semaphores;
        std::map<int32_t, EventFlag> m_eventFlags;
        std::map<int32_t, Mailbox> m_mailboxes;
        std::multimap<uint32_t, Alarm> m_alarms; // keyed by handler
        std::array<HardTimer, 6> m_timers{};
        std::map<uint32_t, Interrupt> m_interrupts;
        std::vector<uint32_t> m_pendingInterrupts;
        std::map<uint32_t, Server> m_servers;
        std::map<uint32_t, int32_t> m_queues; // RPC queue record -> owning thread
        std::deque<std::shared_ptr<Request>> m_requests;
        std::map<int32_t, std::shared_ptr<Request>> m_active; // by server thread
        std::map<int32_t, CpuContext> m_saved;                 // server loop frames under a call
        std::array<DmaChannel, 2> m_dma{};                    // channels 4 and 7
        std::vector<Module> m_modules;
        std::map<uint32_t, uint32_t> m_allocations; // address -> size
        int32_t m_nextId = 1;
        int32_t m_current = 0;
        uint64_t m_readySequence = 0;
        bool m_reschedule = false;
        bool m_inInterrupt = false;
        int32_t m_intrSuspend = 0;
        uint32_t m_ctypeTable = 0;
        uint32_t m_returnTrap = 0;
        uint32_t m_rpcTrap = 0;
        int32_t m_loaderThread = 0;
        uint32_t m_loaderResult = 0;
        uint32_t m_sifDmaId = 0;
    };
}
