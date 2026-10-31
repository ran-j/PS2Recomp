#include "emulator/core/iop_cpu.h"
#include "emulator/core/iop_kernel.h"
#include "emulator/iop_emulator_const.h"
#include "emulator/core/iop_memory.h"
#include "emulator/imports/iop_cdvd.h"
#include "emulator/imports/iop_dmacman.h"
#include "emulator/imports/iop_imports.h"
#include "emulator/imports/iop_loadcore.h"
#include "emulator/imports/iop_timrman.h"
#include "emulator/services/iop_rpc.h"
#include "ps2x/iop/iop_host.h"

#include <cstdint>
#include <chrono>
#include <cstring>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    using namespace ps2x::iop;
    using namespace ps2x::iop::detail;

    constexpr uint32_t kExportMagic = 0x41C00000u;
    constexpr int32_t kLibraryNotFound = -213;
    constexpr int32_t kIllegalLibrary = -214;

    class NullHost : public IopHost
    {
    public:
        bool readGuest(uint32_t, void *, size_t) const override { return false; }
        bool writeGuest(uint32_t, const void *, size_t) override { return false; }
        bool zeroGuest(uint32_t, size_t) override { return false; }
        bool normalizeGuestAddress(uint32_t, uint32_t &) const override { return false; }
        uint32_t allocateIopHandle(IopHandleKind) override { return 1u; }
        uint32_t allocateGuest(uint32_t, uint32_t) override { return 0u; }
        void freeGuest(uint32_t) override {}
        void audioCommand(uint32_t, uint32_t, GuestBuffer, GuestBuffer) override {}
        std::string hostPath(HostPathKind) const override { return {}; }
        std::string translateGuestPath(std::string_view path) const override { return std::string(path); }
        uint64_t openHostFile(std::string_view) override { return 0u; }
        bool hostFileSize(uint64_t, uint64_t &) const override { return false; }
        bool readHostFile(uint64_t, uint64_t, void *, size_t, size_t &) override { return false; }
        void closeHostFile(uint64_t) override {}
        int32_t memoryCard(const MemoryCardRequest &) override { return 0; }
        bool hasGuestFunction(uint32_t) const override { return false; }
        bool invokeGuestFunction(uint64_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t *) override { return false; }
        void log(LogLevel, std::string_view) override {}
    };

    class CdRootHost final : public NullHost
    {
    public:
        explicit CdRootHost(std::filesystem::path rootPath)
            : root(std::move(rootPath))
        {
        }

        std::string hostPath(HostPathKind kind) const override
        {
            return kind == HostPathKind::CdRoot ? root.string() : std::string{};
        }

    private:
        std::filesystem::path root;
    };

    class RecordingExecutor final : public IopGuestExecutor
    {
    public:
        uint32_t executeGuestFunction(uint32_t address,
                                      uint32_t a0,
                                      uint32_t,
                                      uint32_t,
                                      uint32_t,
                                      uint32_t gp) override
        {
            ++calls;
            lastAddress = address;
            lastArgument = a0;
            lastGp = gp;
            return callbackResult;
        }

        uint32_t callbackResult = 0u;
        uint32_t calls = 0u;
        uint32_t lastAddress = 0u;
        uint32_t lastArgument = 0u;
        uint32_t lastGp = 0u;
    };

    bool expect(bool condition, std::string_view message)
    {
        if (condition)
            return true;
        std::cerr << "FAIL: " << message << '\n';
        return false;
    }

    bool testLoadcoreRebootLibraryMode()
    {
        IopMemory memory;
        IopImportRegistry imports(memory);
        IopLoadcore loadcore(memory, imports);

        IopCpuState cpu{};
        cpu.gpr[4] = 0u;
        cpu.gpr[5] = 2u;
        if (!expect(loadcore.dispatchImport(27u, cpu), "loadcore:27 was not handled") ||
            !expect(static_cast<int32_t>(cpu.gpr[2]) == kIllegalLibrary,
                    "loadcore:27 did not reject a null export table"))
            return false;

        constexpr uint32_t table = 0x1000u;
        memory.write32(table, kExportMagic);
        memory.write16(table + 8u, 0x0101u);
        memory.write16(table + 10u, 0x1234u);
        const char name[8] = {'t', 'e', 's', 't', 'l', 'i', 'b', '\0'};
        (void)memory.writeRam(table + 12u, name, sizeof(name));
        memory.write32(table + 20u, 0u);

        cpu = {};
        cpu.gpr[4] = table;
        cpu.gpr[5] = 2u;
        if (!expect(loadcore.dispatchImport(27u, cpu), "loadcore:27 rejected a valid export table") ||
            !expect(cpu.gpr[2] == 0u, "loadcore:27 returned an error for a valid export table") ||
            !expect(memory.read16(table + 10u) == 0x1232u,
                    "loadcore:27 did not replace only export mode bits 1 and 2"))
            return false;

        constexpr uint32_t invalidTable = 0x1100u;
        memory.write32(invalidTable, 0xDEADBEEFu);
        cpu = {};
        cpu.gpr[4] = invalidTable;
        cpu.gpr[5] = 6u;
        if (!expect(loadcore.dispatchImport(27u, cpu), "loadcore:27 did not consume an invalid-table call") ||
            !expect(static_cast<int32_t>(cpu.gpr[2]) == kLibraryNotFound,
                    "loadcore:27 returned the wrong invalid-table error"))
            return false;

        if (!expect(imports.registerExportTable(table), "test export table did not register"))
            return false;
        memory.write32(table, 0u);
        cpu = {};
        cpu.gpr[4] = table;
        cpu.gpr[5] = 6u;
        return expect(loadcore.dispatchImport(27u, cpu), "loadcore:27 rejected a registered table") &&
               expect(cpu.gpr[2] == 0u, "loadcore:27 returned an error for a registered table") &&
               expect(memory.read16(table + 10u) == 0x1236u,
                      "loadcore:27 did not update a registered table's mode");
    }

    bool pollEvent(IopKernel &kernel, int eventId, uint32_t bits, uint32_t resultAddress, int32_t expected)
    {
        IopCpuState cpu{};
        cpu.gpr[4] = static_cast<uint32_t>(eventId);
        cpu.gpr[5] = bits;
        cpu.gpr[6] = 0u; // WEF_AND
        cpu.gpr[7] = resultAddress;
        return expect(kernel.dispatchEventImport(11u, cpu), "PollEventFlag was not handled") &&
               expect(static_cast<int32_t>(cpu.gpr[2]) == expected, "PollEventFlag returned an unexpected result");
    }

    bool testCdvdSpecialControl()
    {
        NullHost host;
        IopMemory memory;
        IopKernel kernel(memory);
        kernel.reset();
        IopCdvd cdvd(host, memory, kernel);
        cdvd.reset();

        constexpr uint32_t param = 0x2000u;
        constexpr uint32_t eventResult = 0x2010u;
        IopCpuState cpu{};
        cpu.gpr[4] = static_cast<uint32_t>(-11); // sceCdSC: return cdvdman interrupt event flag
        cpu.gpr[5] = param;
        if (!expect(cdvd.dispatchImport(50u, cpu), "cdvdman:50 was not handled") ||
            !expect(static_cast<int32_t>(cpu.gpr[2]) > 0, "sceCdSC(-11) did not return a valid event flag"))
            return false;
        const int eventId = static_cast<int>(cpu.gpr[2]);

        if (!pollEvent(kernel, eventId, 0x29u, eventResult, 0) ||
            !expect(memory.read32(eventResult) == 0x29u, "cdvdman event flag did not start with bits 0x29"))
            return false;

        IopCpuState clear{};
        clear.gpr[4] = static_cast<uint32_t>(eventId);
        clear.gpr[5] = ~0x29u;
        if (!expect(kernel.dispatchEventImport(8u, clear), "ClearEventFlag was not handled") ||
            !pollEvent(kernel, eventId, 0x29u, eventResult, -418))
            return false;

        cpu = {};
        cpu.gpr[4] = 0x12345u;
        if (!expect(cdvd.dispatchImport(7u, cpu), "sceCdSeek was not handled") ||
            !pollEvent(kernel, eventId, 0x29u, eventResult, 0))
            return false;

        memory.write8(param, 0x30u);
        cpu = {};
        cpu.gpr[4] = static_cast<uint32_t>(-2);
        cpu.gpr[5] = param;
        if (!expect(cdvd.dispatchImport(50u, cpu), "sceCdSC(-2) was not handled") ||
            !expect(cpu.gpr[2] == 0x30u, "sceCdSC(-2) did not store the low-byte error"))
            return false;

        memory.write32(param, 0u);
        cpu = {};
        cpu.gpr[4] = static_cast<uint32_t>(-1);
        cpu.gpr[5] = param;
        if (!expect(cdvd.dispatchImport(50u, cpu), "sceCdSC(-1) was not handled") ||
            !expect(cpu.gpr[2] == 0u, "sceCdSC(-1) returned the wrong initial stream state") ||
            !expect(memory.read32(param) == 0x30u, "sceCdSC(-1) did not publish the last error"))
            return false;

        cpu = {};
        cpu.gpr[4] = 2u;
        cpu.gpr[5] = param;
        if (!expect(cdvd.dispatchImport(50u, cpu), "sceCdSC(2) was not handled") ||
            !expect(cpu.gpr[2] == 2u, "sceCdSC(2) did not update the stream state"))
            return false;

        cpu = {};
        cpu.gpr[4] = static_cast<uint32_t>(-1);
        cpu.gpr[5] = param;
        return expect(cdvd.dispatchImport(50u, cpu), "second sceCdSC(-1) was not handled") &&
               expect(cpu.gpr[2] == 2u, "sceCdSC(-1) did not preserve the stream state");
    }

    bool testCdvdSearchFile()
    {
        const auto suffix = std::to_string(
            static_cast<unsigned long long>(std::chrono::steady_clock::now().time_since_epoch().count()));
        const std::filesystem::path root =
            std::filesystem::temp_directory_path() / ("ps2x-iop-cdvd-search-" + suffix);
        const std::filesystem::path movieDirectory = root / "MOVIE";
        const std::filesystem::path moviePath = movieDirectory / "OPENING.PSS";
        std::error_code error;
        std::filesystem::create_directories(movieDirectory, error);
        if (!expect(!error, "could not create the temporary CD root"))
            return false;
        {
            std::ofstream movie(moviePath, std::ios::binary);
            movie.write("PSS!", 4);
        }

        CdRootHost host(root);
        IopMemory memory;
        IopKernel kernel(memory);
        kernel.reset();
        IopCdvd cdvd(host, memory, kernel);
        cdvd.reset();

        constexpr uint32_t resultAddress = 0x2400u;
        constexpr uint32_t pathAddress = 0x2480u;
        const char path[] = "cdrom0:\\movie\\opening.pss;1";
        (void)memory.writeRam(pathAddress, path, sizeof(path));

        IopCpuState cpu{};
        cpu.gpr[4] = resultAddress;
        cpu.gpr[5] = pathAddress;
        const bool handled = cdvd.dispatchImport(10u, cpu);
        const bool passed =
            expect(handled, "cdvdman:10 was not handled") &&
            expect(cpu.gpr[2] == 1u, "sceCdSearchFile did not find a case-insensitive ISO path") &&
            expect(memory.read32(resultAddress) >= 20u, "sceCdSearchFile returned an invalid LSN") &&
            expect(memory.read32(resultAddress + 4u) == 4u, "sceCdSearchFile returned the wrong size") &&
            expect(memory.readString(resultAddress + 8u, 16u) == "OPENING.PSS",
                   "sceCdSearchFile returned the wrong file name");

        std::filesystem::remove_all(root, error);
        return passed;
    }

    int startTestThread(IopKernel &kernel, IopMemory &memory, uint32_t descriptor, uint32_t entry, uint32_t priority)
    {
        memory.write32(descriptor + 0u, 0x02000000u); // TH_C
        memory.write32(descriptor + 4u, 0u);
        memory.write32(descriptor + 8u, entry);
        memory.write32(descriptor + 12u, 0x800u);
        memory.write32(descriptor + 16u, priority);
        IopCpuState cpu{};
        cpu.gpr[4] = descriptor;
        if (!kernel.dispatchThreadImport(4u, cpu, 0u)) // CreateThread
            return -1;
        const int id = static_cast<int>(cpu.gpr[2]);
        cpu = {};
        cpu.gpr[4] = static_cast<uint32_t>(id);
        if (!kernel.dispatchThreadImport(6u, cpu, 0u)) // StartThread
            return -1;
        return id;
    }

    bool testSliceEndDoesNotHandTheCpuToAnEqualPriorityThread()
    {
        // CRI's ADX driver has a mixer and a command thread at priority 39. The command thread stops a
        // stream by clearing fields the mixer divides by; if it runs when the emulator ends the mixer's
        // slice mid-stream, the mixer hits its divide-by-zero break and runs into the exception vector.
        IopMemory memory;
        IopKernel kernel(memory);
        kernel.reset();

        const int stopper = startTestThread(kernel, memory, 0x3000u, 0x10000u, 39u);
        const int mixer = startTestThread(kernel, memory, 0x3040u, 0x20000u, 39u);
        if (!expect(stopper > 0 && mixer > stopper, "could not create the two equal-priority threads"))
            return false;

        IopThread *running = kernel.beginNextReady(0u);
        if (!expect(running != nullptr && running->id == stopper, "the lower-id thread should run first"))
            return false;
        IopCpuState delay{};
        delay.gpr[4] = 100u; // DelayThread(100 us)
        if (!expect(kernel.dispatchThreadImport(33u, delay, 0u), "DelayThread was not handled"))
            return false;
        running->cpu.yielded = delay.yielded;
        kernel.endTimeslice(*running, kThreadReturnSentinel);

        running = kernel.beginNextReady(10u);
        if (!expect(running != nullptr && running->id == mixer, "the mixer should run while the stopper delays"))
            return false;
        kernel.endTimeslice(*running, kThreadReturnSentinel); // emulator budget ran out mid-stream

        running = kernel.beginNextReady(1'000'000u); // the stopper's delay has long expired
        if (!expect(running != nullptr && running->id == mixer,
                    "an equal-priority thread that became ready must not take over at a slice boundary"))
            return false;

        // A yield hands the CPU over as usual.
        running->cpu.yielded = true;
        kernel.endTimeslice(*running, kThreadReturnSentinel);
        running = kernel.beginNextReady(1'000'001u);
        return expect(running != nullptr && running->id == stopper, "after a yield the waiting thread should run");
    }

    bool testSliceEndInsideCpuSuspendIntrResumesSameThread()
    {
        IopMemory memory;
        IopKernel kernel(memory);
        kernel.reset();

        const int worker = startTestThread(kernel, memory, 0x3000u, 0x10000u, 39u);
        IopThread *running = kernel.beginNextReady(0u);
        if (!expect(running != nullptr && running->id == worker, "the worker should run"))
            return false;
        memory.setInterruptControl(0u); // CpuSuspendIntr
        kernel.endTimeslice(*running, kThreadReturnSentinel);

        const int urgent = startTestThread(kernel, memory, 0x3040u, 0x20000u, 10u);
        running = kernel.beginNextReady(1u);
        if (!expect(running != nullptr && running->id == worker,
                    "nothing may preempt a thread that has interrupts suspended"))
            return false;
        memory.setInterruptControl(1u); // CpuResumeIntr
        kernel.endTimeslice(*running, kThreadReturnSentinel);

        running = kernel.beginNextReady(2u);
        return expect(running != nullptr && running->id == urgent,
                      "once interrupts are back on, the higher-priority thread must run");
    }

    uint64_t startSpu2Dma(IopMemory &memory, bool autoDma)
    {
        memory.write16(0x1F9001B0u, static_cast<uint16_t>(autoDma ? 1u : 0u)); // core 0 ADMAS
        memory.write32(0x1F8010C4u, (2u << 16u) | 0x100u);                     // 2 blocks of 0x100 words
        memory.write32(0x1F8010C8u, 0x01000201u);                               // start, from memory
        const auto start = memory.takeDmaStart();
        return start ? start->delayCycles : 0u;
    }

    bool testSpu2AutoDmaCompletesAtPlaybackRate()
    {
        // ADX streaming waits for this interrupt between PCM blocks; finishing it at bus speed made the
        // mixer thread spin and starve everything else at its priority. The SPU2 pulls one 1 KB block
        // (256 stereo frames) per 256 output frames, and the transfer ends when its last block is pulled.
        IopMemory memory;
        if (!expect(startSpu2Dma(memory, true) == 0u && !memory.takeDmaStart(), "AutoDMA must not complete at bus speed"))
            return false;
        std::vector<int16_t> out;
        memory.mixSpu2(256u, out);
        if (!expect(!memory.takeDmaStart(), "AutoDMA completed before playback reached its last block"))
            return false;
        memory.mixSpu2(1u, out);
        const auto done = memory.takeDmaStart();
        if (!expect(done && done->irq == 0x24, "AutoDMA should complete once playback pulls its last block"))
            return false;
        const uint64_t plainDelay = startSpu2Dma(memory, false);
        return expect(plainDelay == 0x200u * 2u, "a plain SPU2 DMA should keep its bus-speed delay");
    }

    bool testTimrmanPeriodicCallback()
    {
        IopTimrman timrman;
        timrman.reset();
        IopCpuState cpu{};

        cpu.gpr[4] = 1u; // SYSCLK
        cpu.gpr[5] = 32u;
        cpu.gpr[6] = 1u;
        if (!expect(timrman.dispatchImport(4u, cpu, 100u), "AllocHardTimer was not handled") ||
            !expect(static_cast<int32_t>(cpu.gpr[2]) > 0, "AllocHardTimer did not allocate a 32-bit timer"))
            return false;
        const uint32_t timerId = cpu.gpr[2];

        cpu = {};
        cpu.gpr[4] = timerId;
        cpu.gpr[5] = 100u;
        cpu.gpr[6] = 0x12340u;
        cpu.gpr[7] = 0x45670u;
        cpu.gpr[28] = 0x89AB0u;
        if (!expect(timrman.dispatchImport(20u, cpu, 100u), "SetTimerHandler was not handled") ||
            !expect(cpu.gpr[2] == 0u, "SetTimerHandler failed"))
            return false;

        cpu = {};
        cpu.gpr[4] = timerId;
        cpu.gpr[5] = 1u;
        cpu.gpr[6] = 0u;
        cpu.gpr[7] = 1u;
        if (!expect(timrman.dispatchImport(22u, cpu, 100u), "SetupHardTimer was not handled") ||
            !expect(cpu.gpr[2] == 0u, "SetupHardTimer failed"))
            return false;

        cpu = {};
        cpu.gpr[4] = timerId;
        if (!expect(timrman.dispatchImport(23u, cpu, 100u), "StartHardTimer was not handled") ||
            !expect(cpu.gpr[2] == 0u, "StartHardTimer failed") ||
            !expect(timrman.nextEventCycle(1000u) == 200u, "timer compare was scheduled at the wrong cycle"))
            return false;

        RecordingExecutor executor;
        executor.callbackResult = 100u;
        timrman.serviceDue(199u, executor);
        if (!expect(executor.calls == 0u, "timer callback ran too early"))
            return false;
        timrman.serviceDue(200u, executor);
        return expect(executor.calls == 1u, "timer callback did not run") &&
               expect(executor.lastAddress == 0x12340u, "timer called the wrong handler") &&
               expect(executor.lastArgument == 0x45670u, "timer passed the wrong common argument") &&
               expect(executor.lastGp == 0x89AB0u, "timer callback lost the registering module GP") &&
               expect(timrman.nextEventCycle(1000u) == 300u, "timer callback return did not rearm compare");
    }
}

namespace
{
    // Sends one SIO2 packet the way sio2man does in PIO mode and returns the reply bytes.
    std::vector<uint8_t> sio2Transfer(IopMemory &memory, const std::vector<uint8_t> &command)
    {
        memory.write32(0x1F808200u, static_cast<uint32_t>(command.size()) << 8u); // SEND3[0]: port 0, length
        memory.write32(0x1F808204u, 0u);
        for (const uint8_t value : command)
            memory.write8(0x1F808260u, value);
        memory.write32(0x1F808268u, 1u); // start
        std::vector<uint8_t> reply(command.size());
        for (uint8_t &value : reply)
            value = memory.read8(0x1F808264u);
        memory.write32(0x1F808280u, 1u); // acknowledge
        return reply;
    }

    bool testSio2MemoryCard()
    {
        const std::filesystem::path image = std::filesystem::temp_directory_path() / "ps2x_sio2_test_card.ps2";
        std::error_code ec;
        std::filesystem::remove(image, ec);

        IopMemory memory;
        memory.attachMemoryCard(0, image.string());
        if (!expect(!std::filesystem::exists(image), "the card image should not be created before SIO2 touches it"))
            return false;

        const std::vector<uint8_t> probe = sio2Transfer(memory, {0x81, 0x11, 0, 0});
        bool irq17 = false;
        while (const auto start = memory.takeDmaStart())
            irq17 = irq17 || start->irq == 17;
        if (!expect(irq17, "starting a SIO2 transfer should raise IRQ 17") ||
            !expect(std::filesystem::file_size(image, ec) == 0x4000u * 528u, "a blank 8 MB card image with ECC should be created") ||
            !expect(probe[2] == 0x2B && probe[3] == 0x55, "probe should answer 0x2B and the terminator") ||
            !expect(memory.read32(0x1F80826Cu) == 0x1100u, "RECV1 should report a connected card"))
            return false;

        const std::vector<uint8_t> specs = sio2Transfer(memory, {0x81, 0x26, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
        if (!expect(specs[3] == 0x00 && specs[4] == 0x02 && specs[5] == 0x10 && specs[8] == 0x40,
                    "specs should report 512-byte pages, 16-page blocks and 0x4000 pages") ||
            !expect(specs[11] == 0x52 && specs[12] == 0x55, "the spec checksum should XOR the eight spec bytes, then the terminator"))
            return false;

        // Program page 3, then read it back: flash writes only clear bits.
        (void)sio2Transfer(memory, {0x81, 0x22, 3, 0, 0, 0, 3, 0, 0});
        std::vector<uint8_t> write = {0x81, 0x42, 4, 0x12, 0x34, 0x56, 0x78, 0, 0, 0};
        // Replies are as long as the request: two echo bytes, 0x00, 0x2B, the data, XOR and terminator.
        (void)sio2Transfer(memory, write);
        (void)sio2Transfer(memory, {0x81, 0x23, 3, 0, 0, 0, 3, 0, 0});
        const std::vector<uint8_t> read = sio2Transfer(memory, {0x81, 0x43, 4, 0, 0, 0, 0, 0, 0, 0});
        if (!expect(read[3] == 0x2B && read[4] == 0x12 && read[7] == 0x78 && read[9] == 0x55, "read data should return the programmed bytes") ||
            !expect(read[8] == (0x12 ^ 0x34 ^ 0x56 ^ 0x78), "read data should end with the XOR checksum"))
            return false;

        // The write must survive reopening the image.
        IopMemory reopened;
        reopened.attachMemoryCard(0, image.string());
        (void)sio2Transfer(reopened, {0x81, 0x23, 3, 0, 0, 0, 3, 0, 0});
        const std::vector<uint8_t> persisted = sio2Transfer(reopened, {0x81, 0x43, 4, 0, 0, 0, 0, 0, 0, 0});
        (void)sio2Transfer(reopened, {0x81, 0x21, 0, 0, 0, 0, 0, 0, 0});
        (void)sio2Transfer(reopened, {0x81, 0x82, 0, 0});
        (void)sio2Transfer(reopened, {0x81, 0x23, 3, 0, 0, 0, 3, 0, 0});
        const std::vector<uint8_t> erased = sio2Transfer(reopened, {0x81, 0x43, 4, 0, 0, 0, 0, 0, 0, 0});
        std::filesystem::remove(image, ec);
        return expect(persisted[4] == 0x12, "programmed pages should persist in the image file") &&
               expect(erased[4] == 0xFF && erased[7] == 0xFF, "erasing block 0 should reset page 3 to 0xFF");
    }
    // Server modules pull EE payloads (memory card writes) with sceSifGetOtherData.
    bool testSifGetOtherData()
    {
        class GuestHost final : public NullHost
        {
        public:
            bool readGuest(uint32_t address, void *destination, size_t size) const override
            {
                if (address > guest.size() || size > guest.size() - address)
                    return false;
                std::memcpy(destination, guest.data() + address, size);
                return true;
            }
            std::vector<uint8_t> guest = std::vector<uint8_t>(0x1000u, 0u);
        };

        GuestHost host;
        IopMemory memory;
        IopKernel kernel(memory);
        IopRpcBridge rpc(host, memory, kernel);
        const uint8_t payload[8] = {'M', 'C', '2', 'S', 'A', 'V', 'E', '!'};
        std::memcpy(host.guest.data() + 0x400u, payload, sizeof(payload));

        IopCpuState cpu{};
        cpu.gpr[4] = 0x2000u;   // receive data
        cpu.gpr[5] = 0x400u;    // EE source
        cpu.gpr[6] = 0x30000u;  // IOP destination
        cpu.gpr[7] = sizeof(payload);
        cpu.gpr[2] = 0xFFFFu;
        uint8_t copied[8] = {};
        if (!expect(rpc.dispatchSifCmdImport(23u, cpu), "sceSifGetOtherData was not handled") ||
            !expect(cpu.gpr[2] == 0u, "sceSifGetOtherData should succeed") ||
            !expect(memory.readRam(0x30000u, copied, sizeof(copied)) && std::memcmp(copied, payload, sizeof(payload)) == 0,
                    "sceSifGetOtherData should copy EE memory into IOP RAM"))
            return false;

        cpu.gpr[5] = 0xFFF0u; // outside EE memory
        return expect(rpc.dispatchSifCmdImport(23u, cpu) && static_cast<int32_t>(cpu.gpr[2]) < 0,
                      "sceSifGetOtherData should fail on an unreadable EE source");
    }

    // mc2_s1 copies card specs with back-to-back lwl/lwr; the second must merge into the first's pending load.
    bool testUnalignedLoadPair()
    {
        IopMemory memory;
        IopCpuCore core(memory);
        const uint32_t program[] = {
            0x88A70003u, // lwl a3, 3(a1)
            0x98A70000u, // lwr a3, 0(a1)
            0x00000000u,
            0x00000000u,
        };
        const uint8_t data[8] = {0xAA, 0xAA, 0xAA, 0x11, 0x22, 0x33, 0x44, 0xAA};
        (void)memory.writeRam(0x1000u, program, sizeof(program));
        (void)memory.writeRam(0x2000u, data, sizeof(data));

        IopCpuState cpu{};
        cpu.pc = 0x1000u;
        cpu.gpr[5] = 0x2003u;
        cpu.gpr[7] = 0xDEADBEEFu;
        for (int i = 0; i < 4; ++i)
            (void)core.executeInstruction(cpu);
        return expect(cpu.gpr[7] == 0x44332211u, "an lwl/lwr pair should load the unaligned word");
    }

    // IOP pad drivers (and sio2d's memory card path) expect a controller in port 1 to answer on SIO2.
    bool testSio2DualShock2()
    {
        IopMemory memory;
        const auto transfer = [&memory](uint32_t port, const std::vector<uint8_t> &command)
        {
            memory.write32(0x1F808200u, port | (static_cast<uint32_t>(command.size()) << 8u));
            memory.write32(0x1F808204u, 0u);
            for (const uint8_t value : command)
                memory.write8(0x1F808260u, value);
            memory.write32(0x1F808268u, 1u);
            std::vector<uint8_t> reply(command.size());
            for (uint8_t &value : reply)
                value = memory.read8(0x1F808264u);
            memory.write32(0x1F808280u, 1u);
            return reply;
        };

        const std::vector<uint8_t> poll = transfer(0u, {0x01, 0x42, 0x00, 0x00, 0x00});
        if (!expect(poll == std::vector<uint8_t>{0xFF, 0x41, 0x5A, 0xFF, 0xFF}, "port 0 should answer a poll as a digital pad at rest") ||
            !expect(memory.read32(0x1F80826Cu) == 0x1100u, "RECV1 should report the port 0 controller as connected"))
            return false;

        const std::vector<uint8_t> empty = transfer(1u, {0x01, 0x42, 0x00, 0x00, 0x00});
        if (!expect(empty[1] == 0xFF && memory.read32(0x1F80826Cu) == 0x2D100u, "port 1 should read as an empty socket"))
            return false;

        // Enter config mode, switch to analog, leave: polls then carry the sticks at centre.
        (void)transfer(0u, {0x01, 0x43, 0x00, 0x01, 0x00});
        const std::vector<uint8_t> status = transfer(0u, {0x01, 0x45, 0x00, 0x5A, 0x5A, 0x5A, 0x5A, 0x5A, 0x5A});
        (void)transfer(0u, {0x01, 0x44, 0x00, 0x01, 0x03, 0x00, 0x00, 0x00, 0x00});
        (void)transfer(0u, {0x01, 0x43, 0x00, 0x00, 0x5A, 0x5A, 0x5A, 0x5A, 0x5A});
        const std::vector<uint8_t> analog = transfer(0u, {0x01, 0x42, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
        return expect(status[1] == 0xF3 && status[3] == 0x03, "config mode should identify as 0xF3 and report a DualShock 2") &&
               expect(analog[1] == 0x73 && analog[5] == 0x7F && analog[8] == 0x7F, "analog polls should report centred sticks");
    }

    // sio2man sends memory card packets by DMA through dmacman, so those calls must drive SIO2.
    bool testDmacmanDrivesSio2Dma()
    {
        const std::filesystem::path image = std::filesystem::temp_directory_path() / "ps2x_dmacman_test_card.ps2";
        std::error_code ec;
        std::filesystem::remove(image, ec);

        IopMemory memory;
        IopDmacman dmacman(memory);
        memory.attachMemoryCard(0, image.string());

        constexpr uint32_t kStack = 0x1000u;
        constexpr uint32_t kSendBuffer = 0x17000u;
        constexpr uint32_t kReplyBuffer = 0x17630u;
        constexpr uint32_t kChannelSio2In = 11u;
        constexpr uint32_t kChannelSio2Out = 12u;
        const auto call = [&](uint16_t ordinal, uint32_t a0, uint32_t a1 = 0u, uint32_t a2 = 0u, uint32_t a3 = 0u)
        {
            IopCpuState cpu{};
            cpu.gpr[4] = a0;
            cpu.gpr[5] = a1;
            cpu.gpr[6] = a2;
            cpu.gpr[7] = a3;
            cpu.gpr[29] = kStack;
            const bool handled = dmacman.dispatchImport(ordinal, cpu);
            return handled ? cpu.gpr[2] : 0xDEADBEEFu;
        };

        (void)call(33u, kChannelSio2In, 3u);
        (void)call(34u, kChannelSio2In);
        (void)call(34u, kChannelSio2Out);
        if (!expect(memory.read32(0x1F801570u) == 0x008B0000u, "DPCR2 should hold SIO2in priority 3 and both SIO2 enable bits"))
            return false;

        // One probe packet: port 0, four bytes, sent as one four-byte DMA block each way.
        memory.write32(0x1F808200u, 4u << 8u);
        const uint8_t probe[4] = {0x81, 0x11, 0x00, 0x00};
        (void)memory.writeRam(kSendBuffer, probe, sizeof(probe));

        memory.write32(kStack + 16u, 1u); // direction: from memory
        if (!expect(call(28u, kChannelSio2In, kSendBuffer, 1u, 1u) == 1u, "sceSetSliceDMA should accept SIO2in"))
            return false;
        if (!expect(memory.read32(0x1F801540u) == kSendBuffer && memory.read32(0x1F801544u) == 0x10001u &&
                        memory.read32(0x1F801548u) == 0x201u,
                    "sceSetSliceDMA should program MADR, BCR and CHCR without starting the channel") ||
            !expect(!std::filesystem::exists(image), "no SIO2 byte should move before sceStartDMA"))
            return false;
        (void)call(32u, kChannelSio2In);

        memory.write32(kStack + 16u, 0u); // direction: to memory
        (void)call(28u, kChannelSio2Out, kReplyBuffer, 1u, 1u);
        (void)call(32u, kChannelSio2Out);

        uint8_t reply[4] = {};
        (void)memory.readRam(kReplyBuffer, reply, sizeof(reply));
        bool sio2InIrq = false;
        bool sio2OutIrq = false;
        while (const auto start = memory.takeDmaStart())
        {
            sio2InIrq = sio2InIrq || start->irq == 0x2C;
            sio2OutIrq = sio2OutIrq || start->irq == 0x2D;
        }
        const bool created = std::filesystem::exists(image);
        std::filesystem::remove(image, ec);
        return expect(created, "the DMA-fed probe should reach the memory card") &&
               expect(reply[2] == 0x2B && reply[3] == 0x55, "the SIO2out DMA should deliver the probe reply to IOP RAM") &&
               expect((memory.read32(0x1F801548u) & 0x01000000u) == 0u && (memory.read32(0x1F801558u) & 0x01000000u) == 0u,
                      "finished channels should clear their CHCR start bit") &&
               expect(sio2InIrq && sio2OutIrq, "both SIO2 DMA channels should raise their completion interrupts") &&
               expect(call(9u, kChannelSio2Out) == memory.read32(0x1F801558u), "dmac_ch_get_chcr should read the channel register") &&
               expect(call(28u, 6u, kSendBuffer, 1u, 1u) == 0u, "sceSetSliceDMA must reject the OTC channel");
    }
}

int main()
{
    if (!testLoadcoreRebootLibraryMode() || !testCdvdSpecialControl() || !testCdvdSearchFile() ||
        !testTimrmanPeriodicCallback() || !testSio2MemoryCard() || !testSliceEndDoesNotHandTheCpuToAnEqualPriorityThread() ||
        !testSliceEndInsideCpuSuspendIntrResumesSameThread() || !testSpu2AutoDmaCompletesAtPlaybackRate() ||
        !testDmacmanDrivesSio2Dma() || !testSio2DualShock2() || !testSifGetOtherData() || !testUnalignedLoadPair())
        return 1;
    std::cout << "ps2xIOP import tests passed\n";
    return 0;
}
