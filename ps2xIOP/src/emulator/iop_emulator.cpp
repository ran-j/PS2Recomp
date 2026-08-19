#include "iop_emulator.h"
#include "iop_cdvd.h"
#include "iop_cpu.h"
#include "iop_imports.h"
#include "iop_kernel.h"
#include "iop_memory.h"
#include "iop_module_loader.h"
#include "iop_rpc.h"
#include "iop_sysclib.h"
#include "iop_emulator_const.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <optional>
#include <span>
#include <sstream>
#include <unordered_map>
#include <utility>

namespace ps2x::iop::detail
{
    namespace
    {
        constexpr uint32_t kRamSize = IopMemory::RamSize;
        constexpr uint32_t kKernelHeapBase = IopMemory::HeapBase;
        constexpr uint32_t kKernelHeapLimit = IopMemory::HeapLimit;
        constexpr uint32_t kCallStackBase = kKernelHeapLimit;
        constexpr uint32_t kCallStackLimit = 0x001FFF00u;
        constexpr uint32_t kCallStackSize = 0x2000u;
        constexpr uint32_t kCallStackCapacity = (kCallStackLimit - kCallStackBase) / kCallStackSize;
        constexpr uint64_t kCdvdCompletionCycles = 128u;

        uint32_t physicalAddress(uint32_t address)
        {
            return IopMemory::physicalAddress(address);
        }

        int32_t sign16(uint32_t value)
        {
            return static_cast<int16_t>(value & 0xFFFFu);
        }

        bool iequals(std::string_view lhs, std::string_view rhs)
        {
            if (lhs.size() != rhs.size())
                return false;
            for (size_t i = 0; i < lhs.size(); ++i)
            {
                if (std::tolower(static_cast<unsigned char>(lhs[i])) !=
                    std::tolower(static_cast<unsigned char>(rhs[i])))
                    return false;
            }
            return true;
        }

    }

    class IopEmulator::Impl final : public IopGuestExecutor
    {
    public:
        using CpuState = IopCpuState;

        struct Module
        {
            int id = 0;
            std::string path;
            std::string name;
            uint32_t base = 0;
            uint32_t size = 0;
            uint32_t entry = 0;
            uint32_t gp = 0;
            bool resident = false;
        };

        struct InterruptHandler
        {
            uint32_t function = 0;
            uint32_t argument = 0;
            uint32_t gp = 0;
        };

        struct GuestCallback
        {
            uint32_t function = 0;
            uint32_t gp = 0;
        };

        struct ScheduledGuestCallback
        {
            uint32_t function = 0u;
            uint32_t gp = 0u;
            uint32_t argument = 0u;
        };

        struct IomanDevice
        {
            uint32_t address = 0;
            uint32_t gp = 0;
            std::string name;
        };

        explicit Impl(IopHost &hostRef)
            : host(hostRef),
              cdvd(host, memory),
              kernel(memory),
              rpc(host, memory, kernel),
              sysclib(memory),
              cpuCore(memory),
              imports(memory)
        {
            reset();
        }

        void reset()
        {
            memory.reset();
            kernel.reset();
            modules.clear();
            imports.reset();
            rpc.reset();
            cdvd.reset();
            interruptHandlers.clear();
            iomanDevices.clear();
            interruptEnabled.clear();
            pendingDmaInterrupts.clear();
            pendingGuestCallbacks.clear();
            nextModuleId = 1;
            moduleCursor = kModuleLoadBase;
            totalCycles = 0;
            totalInstructions = 0;
            eeCycleCarry = 0;
            activeCpu = nullptr;
            lastError.clear();
            servicingDmaInterrupts = false;
            servicingGuestCallbacks = false;
            callDepth = 0u;
            secrMcCommandHandler = {};
            secrMcDevIdHandler = {};
            checkKelfPathCallback = {};
        }

        uint8_t read8(uint32_t address) const
        {
            return memory.read8(address);
        }

        uint16_t read16(uint32_t address) const
        {
            return memory.read16(address);
        }

        uint32_t read32(uint32_t address) const
        {
            return memory.read32(address);
        }

        void write8(uint32_t address, uint8_t value)
        {
            memory.write8(address, value);
            schedulePendingDma();
        }

        void write16(uint32_t address, uint16_t value)
        {
            memory.write16(address, value);
            schedulePendingDma();
        }

        void write32(uint32_t address, uint32_t value)
        {
            memory.write32(address, value);
            schedulePendingDma();
        }

        void schedulePendingDma()
        {
            if (const auto dma = memory.takeDmaStart())
                pendingDmaInterrupts[dma->irq] = totalCycles + dma->delayCycles;
        }

        bool readRam(uint32_t address, void *destination, size_t size) const
        {
            return memory.readRam(address, destination, size);
        }

        bool writeRam(uint32_t address, const void *source, size_t size)
        {
            return memory.writeRam(address, source, size);
        }

        bool zeroRam(uint32_t address, size_t size)
        {
            return memory.zeroRam(address, size);
        }

        bool isHardwareAddress(uint32_t phys) const
        {
            return memory.isHardwareAddress(phys);
        }

        uint32_t allocate(uint32_t size, uint32_t alignment = 16u, std::optional<uint32_t> fixed = std::nullopt)
        {
            return memory.allocate(size, alignment, fixed);
        }

        bool freeAllocation(uint32_t address)
        {
            return memory.freeAllocation(address);
        }

        uint32_t maxFreeMemory() const
        {
            return memory.maxFreeMemory();
        }

        std::string readString(uint32_t address, size_t limit = 1024) const
        {
            return memory.readString(address, limit);
        }

        void log(LogLevel level, std::string_view text)
        {
            host.log(level, text);
        }

        bool checkInterrupt(CpuState &cpu)
        {
            const uint32_t status = cpu.cop0[12];
            if ((status & 1u) == 0u)
                return false;
            if ((status & 0x2u) != 0u)
                return false;
            const bool pending = memory.interruptControl() != 0u && (memory.interruptStatus() & memory.interruptMask()) != 0u;
            if (!pending)
                return false;
            cpu.cop0[13] |= 0x400u;
            cpuCore.raiseException(cpu, 0u, cpu.pc, false);
            return true;
        }

        enum class ImportDisposition
        {
            Handled,
            JumpToGuest,
            Missing,
        };

        ImportDisposition dispatchImport(const IopImportCall &call, CpuState &cpu)
        {
            const uint32_t a0 = cpu.gpr[4];
            const uint32_t a1 = cpu.gpr[5];
            const uint32_t a2 = cpu.gpr[6];
            const uint32_t a3 = cpu.gpr[7];
            auto setV0 = [&](uint32_t value)
            {
                cpu.gpr[2] = value;
            };

            // TODO this will become a dispatch table once we have more imports implemented, but for now this is fine.
            if (iequals(call.library, "sysmem"))
            {
                switch (call.ordinal)
                {
                case 4:
                {
                    const uint32_t mode = a0;
                    const uint32_t size = a1;
                    const uint32_t ptr = a2;
                    const uint32_t address = mode == 2u ? allocate(size, 16u, ptr) : allocate(size, 16u);
                    setV0(address);
                    return ImportDisposition::Handled;
                }
                case 5:
                    setV0(freeAllocation(a0) ? 0u : 0xFFFFFFFFu);
                    return ImportDisposition::Handled;
                case 6:
                    setV0(kRamSize);
                    return ImportDisposition::Handled;
                case 7:
                    setV0(maxFreeMemory());
                    return ImportDisposition::Handled;
                case 8:
                    setV0(maxFreeMemory());
                    return ImportDisposition::Handled;
                case 9:
                {
                    if (const auto block = memory.allocationContaining(a0))
                    {
                        setV0(block->address);
                        return ImportDisposition::Handled;
                    }
                    setV0(0u);
                    return ImportDisposition::Handled;
                }
                case 10:
                {
                    if (const auto block = memory.allocationContaining(a0))
                    {
                        setV0(block->size);
                        return ImportDisposition::Handled;
                    }
                    setV0(0xFFFFFFFFu);
                    return ImportDisposition::Handled;
                }
                case 14:
                {
                    const std::string format = readString(a0, 512);
                    log(LogLevel::Info, std::string("[IOP Kprintf] ") + format);
                    setV0(static_cast<uint32_t>(format.size()));
                    return ImportDisposition::Handled;
                }
                case 15:
                    setV0(0);
                    return ImportDisposition::Handled;
                default:
                    break;
                }
            }

            if (iequals(call.library, "cdvdman") &&
                cdvd.dispatchImport(call.ordinal, cpu))
            {
                if (const auto callback = cdvd.takeCompletionCallback())
                {
                    pendingGuestCallbacks.emplace(
                        totalCycles + kCdvdCompletionCycles,
                        ScheduledGuestCallback{
                            callback->address,
                            callback->gp,
                            callback->reason,
                        });
                }
                return ImportDisposition::Handled;
            }

            if (iequals(call.library, "loadcore"))
            {
                switch (call.ordinal)
                {
                case 3:
                    setV0(0);
                    return ImportDisposition::Handled;
                case 4:
                case 5:
                    setV0(0);
                    return ImportDisposition::Handled;
                case 6:
                case 10:
                    setV0(imports.registerExportTable(a0) ? 0u : 0xFFFFFFFFu);
                    return ImportDisposition::Handled;
                case 7:
                    setV0(imports.releaseExportTable(a0) ? 0u : 0xFFFFFFFFu);
                    return ImportDisposition::Handled;
                case 8:
                case 9:
                    setV0(0);
                    return ImportDisposition::Handled;
                case 11:
                {
                    const std::string name = readString(a0 + 12u, 8u);
                    setV0(imports.findTable(name));
                    return ImportDisposition::Handled;
                }
                case 12:
                    setV0(0);
                    return ImportDisposition::Handled;
                case 13:
                case 14:
                case 15:
                case 16:
                case 17:
                case 20:
                case 21:
                    setV0(0);
                    return ImportDisposition::Handled;
                default:
                    break;
                }
            }

            if (iequals(call.library, "thbase") || iequals(call.library, "threadman"))
            {
                return kernel.dispatchThreadImport(call.ordinal, cpu, totalCycles)
                           ? ImportDisposition::Handled
                           : ImportDisposition::Missing;
            }
            if (iequals(call.library, "thsemap"))
            {
                return kernel.dispatchSemaphoreImport(call.ordinal, cpu)
                           ? ImportDisposition::Handled
                           : ImportDisposition::Missing;
            }
            if (iequals(call.library, "thevent"))
            {
                return kernel.dispatchEventImport(call.ordinal, cpu)
                           ? ImportDisposition::Handled
                           : ImportDisposition::Missing;
            }
            if (iequals(call.library, "sifcmd"))
            {
                return rpc.dispatchSifCmdImport(call.ordinal, cpu)
                           ? ImportDisposition::Handled
                           : ImportDisposition::Missing;
            }
            if (iequals(call.library, "intrman"))
            {
                switch (call.ordinal)
                {
                case 3:
                    setV0(0);
                    return ImportDisposition::Handled;
                case 4: // RegisterIntrHandler
                    interruptHandlers[static_cast<int>(a0)] = {a2, a3, cpu.gpr[28]};
                    setV0(0);
                    return ImportDisposition::Handled;
                case 5:
                    interruptHandlers.erase(static_cast<int>(a0));
                    setV0(0);
                    return ImportDisposition::Handled;
                case 6:
                    interruptEnabled[static_cast<int>(a0)] = true;
                    if (a0 < 32u)
                        memory.setInterruptMask(memory.interruptMask() | (1u << a0));
                    setV0(0);
                    return ImportDisposition::Handled;
                case 7:
                    if (a1)
                        write32(a1, a0);
                    if (a0 < 32u)
                        memory.setInterruptMask(memory.interruptMask() & ~(1u << a0));
                    interruptEnabled[static_cast<int>(a0)] = false;
                    setV0(0);
                    return ImportDisposition::Handled;
                case 8:
                    memory.setInterruptControl(0u);
                    setV0(0);
                    return ImportDisposition::Handled;
                case 9:
                    memory.setInterruptControl(1u);
                    setV0(0);
                    return ImportDisposition::Handled;
                case 14:
                    if (a0)
                    {
                        const uint32_t ret = callFunction(a0, a1, a2, a3, cpu.gpr[28], 100000u);
                        setV0(ret);
                    }
                    else
                        setV0(0);
                    return ImportDisposition::Handled;
                case 15:
                case 16:
                    setV0(0);
                    return ImportDisposition::Handled;
                case 17:
                    if (a0)
                        write32(a0, memory.interruptControl());
                    memory.setInterruptControl(0u);
                    setV0(0);
                    return ImportDisposition::Handled;
                case 18:
                    memory.setInterruptControl(a0 ? 1u : 0u);
                    setV0(0);
                    return ImportDisposition::Handled;
                case 23:
                    setV0(0);
                    return ImportDisposition::Handled;
                case 24:
                    setV0(0);
                    return ImportDisposition::Handled;
                case 25:
                    setV0(0);
                    return ImportDisposition::Handled;
                case 28:
                case 30:
                    setV0(0);
                    return ImportDisposition::Handled;
                default:
                    break;
                }
            }
            if (iequals(call.library, "secrman"))
            {
                switch (call.ordinal)
                {
                case 4: // SecrSetMcCommandHandler
                    secrMcCommandHandler = {a0, cpu.gpr[28]};
                    setV0(0);
                    return ImportDisposition::Handled;
                case 5: // SecrSetMcDevIDHandler
                    secrMcDevIdHandler = {a0, cpu.gpr[28]};
                    setV0(0);
                    return ImportDisposition::Handled;
                default:
                    break;
                }
            }
            if (iequals(call.library, "modload") && call.ordinal == 13u)
            {
                // SetCheckKelfPathCallback. MODLOAD owns this callback in the
                // real IOP; retain both the address and the registering
                // module's GP so a later KELF load can invoke it faithfully.
                checkKelfPathCallback = {a0, cpu.gpr[28]};
                setV0(0);
                return ImportDisposition::Handled;
            }
            if (iequals(call.library, "ioman"))
            {
                switch (call.ordinal)
                {
                case 20: // AddDrv
                {
                    constexpr size_t kMaxIomanDevices = 16u;
                    if (a0 == 0u || iomanDevices.size() >= kMaxIomanDevices)
                    {
                        setV0(0xFFFFFFFFu);
                        return ImportDisposition::Handled;
                    }

                    const uint32_t nameAddress = read32(a0 + 0u);
                    const uint32_t operations = read32(a0 + 16u);
                    const std::string name = readString(nameAddress, 64u);
                    if (nameAddress == 0u || operations == 0u || name.empty())
                    {
                        setV0(0xFFFFFFFFu);
                        return ImportDisposition::Handled;
                    }

                    // The original IOMAN exposes the device before invoking
                    // init, then removes it again if initialization fails.
                    iomanDevices.push_back({a0, cpu.gpr[28], name});
                    const uint32_t init = read32(operations + 0u);
                    if (init != 0u)
                    {
                        const int32_t initResult = static_cast<int32_t>(
                            callFunction(init, a0, 0u, 0u, 0u, cpu.gpr[28]));
                        if (initResult < 0)
                        {
                            iomanDevices.pop_back();
                            setV0(0xFFFFFFFFu);
                            return ImportDisposition::Handled;
                        }
                    }

                    setV0(0u);
                    return ImportDisposition::Handled;
                }
                case 21: // DelDrv
                {
                    const std::string name = readString(a0, 64u);
                    const auto device = std::find_if(
                        iomanDevices.begin(), iomanDevices.end(),
                        [&](const IomanDevice &candidate)
                        { return candidate.name == name; });
                    if (device == iomanDevices.end())
                    {
                        setV0(0xFFFFFFFFu);
                        return ImportDisposition::Handled;
                    }

                    const uint32_t operations = read32(device->address + 16u);
                    const uint32_t deinit = operations != 0u ? read32(operations + 4u) : 0u;
                    if (deinit != 0u)
                        (void)callFunction(deinit, device->address, 0u, 0u, 0u, device->gp);
                    iomanDevices.erase(device);
                    setV0(0u);
                    return ImportDisposition::Handled;
                }
                default:
                    break;
                }
            }
            if (iequals(call.library, "sifman"))
            {
                return rpc.dispatchSifManImport(call.ordinal, cpu)
                           ? ImportDisposition::Handled
                           : ImportDisposition::Missing;
            }
            if (iequals(call.library, "vblank"))
            {
                switch (call.ordinal)
                {
                case 4: // WaitVblankStart
                case 5: // WaitVblankEnd
                case 6: // WaitVblank
                case 7: // WaitNonVblank
                {
                    const bool waitForEnd = call.ordinal == 5u || call.ordinal == 7u;
                    const uint64_t phase = waitForEnd ? kVblankEndPhaseCycles : 0u;
                    const uint64_t fieldStart = totalCycles - (totalCycles % kVblankPeriodCycles);
                    uint64_t wakeCycle = fieldStart + phase;
                    if (wakeCycle <= totalCycles)
                        wakeCycle += kVblankPeriodCycles;
                    kernel.delayCurrentUntil(wakeCycle, cpu);
                }
                    setV0(0);
                    return ImportDisposition::Handled;
                case 8: // RegisterVblankHandler
                case 9: // ReleaseVblankHandler
                    // Callback delivery is not required by the scheduler wait
                    // ABI yet.
                    setV0(0);
                    return ImportDisposition::Handled;
                default:
                    break;
                }
            }
            if (iequals(call.library, "timrman") || iequals(call.library, "dmacman"))
            {
                setV0(0);
                return ImportDisposition::Handled;
            }
            if (iequals(call.library, "stdio"))
            {
                switch (call.ordinal)
                {
                case 4: // printf
                {
                    const std::string text = readString(a0, 2048);
                    log(LogLevel::Info, std::string("[IOP printf] ") + text);
                    setV0(static_cast<uint32_t>(text.size()));
                    return ImportDisposition::Handled;
                }
                case 5:
                    setV0(0xFFFFFFFFu);
                    return ImportDisposition::Handled; // getchar
                case 6:
                {
                    const char ch = static_cast<char>(a0 & 0xFFu);
                    log(LogLevel::Info, std::string("[IOP putchar] ") + ch);
                    setV0(a0 & 0xFFu);
                    return ImportDisposition::Handled;
                }
                case 7:
                {
                    const std::string text = readString(a0, 2048);
                    log(LogLevel::Info, std::string("[IOP puts] ") + text);
                    setV0(static_cast<uint32_t>(text.size() + 1u));
                    return ImportDisposition::Handled;
                }
                case 8:
                    setV0(0);
                    return ImportDisposition::Handled; // gets
                case 9:
                {
                    const std::string text = readString(a1, 2048);
                    log(LogLevel::Info, std::string("[IOP fdprintf] ") + text);
                    setV0(static_cast<uint32_t>(text.size()));
                    return ImportDisposition::Handled;
                }
                case 10:
                    setV0(0xFFFFFFFFu);
                    return ImportDisposition::Handled;
                case 11:
                    setV0(a0 & 0xFFu);
                    return ImportDisposition::Handled;
                case 12:
                {
                    const std::string text = readString(a0, 2048);
                    log(LogLevel::Info, std::string("[IOP fdputs] ") + text);
                    setV0(static_cast<uint32_t>(text.size()));
                    return ImportDisposition::Handled;
                }
                case 13:
                    setV0(0);
                    return ImportDisposition::Handled;
                case 14:
                {
                    const std::string text = readString(a1, 2048);
                    log(LogLevel::Info, std::string("[IOP vfdprintf] ") + text);
                    setV0(static_cast<uint32_t>(text.size()));
                    return ImportDisposition::Handled;
                }
                default:
                    break;
                }
            }
            if (iequals(call.library, "sysclib"))
            {
                return sysclib.dispatchImport(call.ordinal, cpu)
                           ? ImportDisposition::Handled
                           : ImportDisposition::Missing;
            }
            if (iequals(call.library, "heaplib"))
            {
                switch (call.ordinal)
                {
                case 4: // CreateHeap - heap identity is opaque to callers.
                    setV0(allocate(16u, 16u));
                    return ImportDisposition::Handled;
                case 5: // DeleteHeap
                    if (a0)
                        freeAllocation(a0);
                    setV0(0);
                    return ImportDisposition::Handled;
                case 6:
                    setV0(allocate(a1, 16u));
                    return ImportDisposition::Handled;
                case 7:
                    setV0(freeAllocation(a1) ? 0u : 0xFFFFFFFFu);
                    return ImportDisposition::Handled;
                case 8:
                    setV0(maxFreeMemory());
                    return ImportDisposition::Handled;
                case 11:
                    setV0(0);
                    return ImportDisposition::Handled;
                case 15:
                {
                    if (const auto block = memory.allocationContaining(a0))
                    {
                        setV0(block->size);
                        return ImportDisposition::Handled;
                    }
                    setV0(0xFFFFFFFFu);
                    return ImportDisposition::Handled;
                }
                default:
                    break;
                }
            }

            const uint32_t target = imports.resolve(call.library, call.ordinal);
            if (target != 0u)
            {
                cpu.pc = target;
                cpu.branchPending = false;
                return ImportDisposition::JumpToGuest;
            }

            std::ostringstream out;
            out << "[IOP] unhandled import " << call.library << ':' << call.ordinal << " pc=0x" << std::hex << cpu.pc;
            log(LogLevel::Warning, out.str());
            setV0(0);
            return ImportDisposition::Missing;
        }

        bool step(CpuState &cpu)
        {
            if (cpu.stopped)
                return false;
            if (cpu.pc == kThreadReturnSentinel || cpu.pc == kCallReturnSentinel)
            {
                cpu.stopped = true;
                return false;
            }
            if (physicalAddress(cpu.pc) >= kRamSize)
            {
                std::ostringstream out;
                out << "[IOP] execution outside RAM pc=0x" << std::hex << cpu.pc;
                log(LogLevel::Error, out.str());
                cpu.stopped = true;
                return false;
            }
            if (checkInterrupt(cpu))
                return true;

            if (const auto import = imports.decode(cpu.pc))
            {
                const ImportDisposition disposition = dispatchImport(*import, cpu);
                ++totalInstructions;
                ++totalCycles;
                if (disposition == ImportDisposition::JumpToGuest)
                    return true;
                cpu.pc = cpu.gpr[31];
                cpu.branchPending = false;
                return !cpu.stopped;
            }

            const bool running = cpuCore.executeInstruction(cpu);
            schedulePendingDma();
            ++totalInstructions;
            ++totalCycles;
            return running;
        }

        uint32_t runCpu(CpuState &cpu, uint32_t instructionBudget)
        {
            CpuState *previous = activeCpu;
            activeCpu = &cpu;
            const uint64_t start = totalInstructions;
            while (!cpu.stopped && !cpu.yielded && totalInstructions - start < instructionBudget)
            {
                if (!step(cpu))
                    break;
                if (!servicingDmaInterrupts && !pendingDmaInterrupts.empty())
                    servicePendingDmaInterrupts();
                if (!servicingGuestCallbacks && !pendingGuestCallbacks.empty())
                    servicePendingGuestCallbacks();
            }
            activeCpu = previous;
            return static_cast<uint32_t>(totalInstructions - start);
        }

        uint32_t callFunction(uint32_t address,
                              uint32_t a0,
                              uint32_t a1,
                              uint32_t a2,
                              uint32_t a3,
                              uint32_t gp,
                              uint32_t budget = kMaxCallInstructions)
        {
            struct CallDepthGuard
            {
                uint32_t &depth;
                ~CallDepthGuard() { --depth; }
            };

            const uint32_t depth = callDepth++;
            const CallDepthGuard depthGuard{callDepth};
            CpuState cpu{};
            cpu.pc = address;
            cpu.gpr[4] = a0;
            cpu.gpr[5] = a1;
            cpu.gpr[6] = a2;
            cpu.gpr[7] = a3;
            cpu.gpr[28] = gp;
            if (depth < kCallStackCapacity)
            {
                const uint32_t stackTop = kCallStackLimit - depth * kCallStackSize;
                cpu.gpr[29] = stackTop - 32u;
            }
            else if (activeCpu && activeCpu->gpr[29] > kCallStackBase + kStackGuardBytes)
            {
                // Extremely deep re-entrancy borrows unused space below the
                // suspended caller's live frame. Stack growth remains away
                // from the caller, so its saved registers stay intact.
                cpu.gpr[29] = (activeCpu->gpr[29] - kStackGuardBytes) & ~15u;
            }
            else
            {
                cpu.gpr[29] = kCallStackBase - 32u;
            }
            cpu.gpr[31] = kCallReturnSentinel;
            runCpu(cpu, budget);
            return cpu.gpr[2];
        }

        uint32_t executeGuestFunction(uint32_t address,
                                      uint32_t a0,
                                      uint32_t a1,
                                      uint32_t a2,
                                      uint32_t a3,
                                      uint32_t gp) override
        {
            return callFunction(address, a0, a1, a2, a3, gp);
        }

        // Not that good to use exception handling for control flow but will do for now
        void servicePendingDmaInterrupts()
        {
            if (servicingDmaInterrupts || pendingDmaInterrupts.empty())
                return;

            servicingDmaInterrupts = true;

            std::vector<int> completed;
            for (auto it = pendingDmaInterrupts.begin(); it != pendingDmaInterrupts.end();)
            {
                if (it->second > totalCycles)
                {
                    ++it;
                    continue;
                }
                completed.push_back(it->first);
                it = pendingDmaInterrupts.erase(it);
            }
            try
            {
                for (const int irq : completed)
                {
                    const auto enabled = interruptEnabled.find(irq);
                    if (enabled == interruptEnabled.end() || !enabled->second)
                        continue;
                    const auto handler = interruptHandlers.find(irq);
                    if (handler == interruptHandlers.end() || handler->second.function == 0u)
                        continue;

                    (void)callFunction(handler->second.function,
                                       handler->second.argument,
                                       0u,
                                       0u,
                                       0u,
                                       handler->second.gp,
                                       100000u);
                }
            }
            catch (...)
            {
                servicingDmaInterrupts = false;
                throw;
            }
            servicingDmaInterrupts = false;
        }

        void servicePendingGuestCallbacks()
        {
            if (servicingGuestCallbacks || pendingGuestCallbacks.empty())
                return;

            std::vector<ScheduledGuestCallback> callbacks;
            for (auto it = pendingGuestCallbacks.begin(); it != pendingGuestCallbacks.end();)
            {
                if (it->first > totalCycles)
                    break;
                callbacks.push_back(it->second);
                it = pendingGuestCallbacks.erase(it);
            }
            if (callbacks.empty())
                return;

            servicingGuestCallbacks = true;
            try
            {
                for (const ScheduledGuestCallback &callback : callbacks)
                {
                    if (callback.function != 0u)
                    {
                        (void)callFunction(callback.function,
                                           callback.argument,
                                           0u,
                                           0u,
                                           0u,
                                           callback.gp,
                                           100000u);
                    }
                }
            }
            catch (...)
            {
                servicingGuestCallbacks = false;
                throw;
            }
            servicingGuestCallbacks = false;
        }

        void runCycles(uint64_t cycles) noexcept
        {
            try
            {
                const uint64_t target = totalCycles + cycles;
                while (totalCycles < target)
                {
                    servicePendingDmaInterrupts();
                    servicePendingGuestCallbacks();
                    IopThread *next = kernel.beginNextReady(totalCycles);
                    if (!next)
                    {
                        uint64_t nextWake = kernel.nextWakeCycle(target);
                        for (const auto &[irq, completionCycle] : pendingDmaInterrupts)
                            nextWake = std::min(nextWake, completionCycle);
                        if (!pendingGuestCallbacks.empty())
                            nextWake = std::min(nextWake, pendingGuestCallbacks.begin()->first);
                        totalCycles = std::max(totalCycles + 1u, std::min(target, nextWake));
                        continue;
                    }
                    const uint64_t before = totalCycles;
                    runCpu(next->cpu, static_cast<uint32_t>(std::min<uint64_t>(kDefaultSlice, target - totalCycles)));
                    kernel.endTimeslice(*next, kThreadReturnSentinel);
                    if (totalCycles == before)
                        ++totalCycles;
                }
            }
            catch (...)
            {
                // Runtime scheduling must never throw through EeScheduler::accountCycles().
            }
        }

        ModuleLoadResult loadImage(std::string path, std::span<const uint8_t> image, const void *arguments, uint32_t argumentSize)
        {
            ModuleLoadResult result{true, -1, -1};
            const IopImageLoadResult loaded = IopModuleLoader::load(image, memory, moduleCursor);
            moduleCursor = loaded.nextModuleCursor;
            if (!loaded)
            {
                if (loaded.error == IopImageLoadError::InvalidElf)
                    log(LogLevel::Error, "[IOP] rejected invalid/non-MIPS IRX ELF");
                else if (loaded.error == IopImageLoadError::ArenaExhausted)
                    log(LogLevel::Error, "[IOP] module arena exhausted");
                return result;
            }
            if (!loaded.relocationsComplete)
                log(LogLevel::Warning, "[IOP] one or more IRX relocations were unsupported");

            Module module;
            module.id = nextModuleId++;
            module.path = std::move(path);
            const size_t slash = module.path.find_last_of("/\\:");
            module.name = slash == std::string::npos ? module.path : module.path.substr(slash + 1u);
            module.base = loaded.base;
            module.size = loaded.size;
            module.entry = loaded.entry;
            module.gp = loaded.gp;

            uint32_t args = 0u;
            if (arguments && argumentSize)
            {
                args = allocate(argumentSize + 1u, 16u);
                if (args)
                {
                    writeRam(args, arguments, argumentSize);
                    write8(args + argumentSize, 0u);
                }
            }
            const uint32_t startResult = callFunction(module.entry, argumentSize, args, 0u, 0u, module.gp);
            if (args)
                freeAllocation(args);
            module.resident = startResult == 0u || startResult == 2u;
            result.moduleId = module.id;
            result.startResult = static_cast<int32_t>(startResult);
            modules[module.id] = std::move(module);

            std::ostringstream out;
            out << "[IOP] loaded IRX id=" << result.moduleId
                << " entry=0x" << std::hex << modules[result.moduleId].entry
                << " base=0x" << modules[result.moduleId].base
                << " start=" << std::dec << result.startResult;
            log(LogLevel::Info, out.str());
            return result;
        }

        ModuleLoadResult loadModule(std::string_view path, const void *arguments, uint32_t argumentSize)
        {
            std::vector<uint8_t> image;
            if (!IopModuleLoader::readWholeHostFile(host, path, image))
            {
                log(LogLevel::Warning, std::string("[IOP] failed to open IRX '") + std::string(path) + "'");
                return {true, -1, -1};
            }
            return loadImage(std::string(path), image, arguments, argumentSize);
        }

        ModuleLoadResult loadModuleBuffer(uint32_t guestAddress, const void *arguments, uint32_t argumentSize)
        {
            std::vector<uint8_t> image;
            if (!IopModuleLoader::readElfFromGuest(host, guestAddress, image))
                return {true, -1, -1};
            std::ostringstream tag;
            tag << "buffer@0x" << std::hex << guestAddress;
            return loadImage(tag.str(), image, arguments, argumentSize);
        }

        bool stopModule(int32_t moduleId, int32_t *result)
        {
            auto it = modules.find(moduleId);
            if (it == modules.end())
                return false;
            // A removable IRX normally exposes a stop entry through module metadata. We do not guess it; terminate owned execution and release the image cleanly.
            kernel.terminateThreadsInRange(it->second.base, it->second.size);
            rpc.removeServersInRange(it->second.base, it->second.size);
            imports.eraseRange(it->second.base, it->second.size);
            modules.erase(it);
            kernel.cleanupDeadThreads();
            if (result)
                *result = 0;
            return true;
        }

        IopHost &host;
        IopMemory memory;
        IopCdvd cdvd;
        IopKernel kernel;
        IopRpcBridge rpc;
        IopSysclib sysclib;
        IopCpuCore cpuCore;
        IopImportRegistry imports;
        std::map<int, Module> modules;
        std::unordered_map<int, InterruptHandler> interruptHandlers;
        std::vector<IomanDevice> iomanDevices;
        std::map<int, bool> interruptEnabled;
        std::map<int, uint64_t> pendingDmaInterrupts;
        std::multimap<uint64_t, ScheduledGuestCallback> pendingGuestCallbacks;
        uint32_t nextModuleId = 1;
        uint32_t moduleCursor = kModuleLoadBase;
        uint64_t totalCycles = 0;
        uint64_t totalInstructions = 0;
        uint64_t eeCycleCarry = 0;
        CpuState *activeCpu = nullptr;
        std::string lastError;
        bool servicingDmaInterrupts = false;
        bool servicingGuestCallbacks = false;
        uint32_t callDepth = 0u;
        GuestCallback secrMcCommandHandler;
        GuestCallback secrMcDevIdHandler;
        GuestCallback checkKelfPathCallback;
    };

    IopEmulator::IopEmulator(IopHost &host)
        : m_impl(std::make_unique<Impl>(host))
    {
    }

    IopEmulator::~IopEmulator() = default;

    void IopEmulator::reset()
    {
        m_impl->reset();
    }

    ModuleLoadResult IopEmulator::loadModule(std::string_view path, const void *arguments, uint32_t argumentSize)
    {
        return m_impl->loadModule(path, arguments, argumentSize);
    }

    ModuleLoadResult IopEmulator::loadModuleBuffer(uint32_t guestAddress, const void *arguments, uint32_t argumentSize)
    {
        return m_impl->loadModuleBuffer(guestAddress, arguments, argumentSize);
    }

    bool IopEmulator::stopModule(int32_t moduleId, int32_t *result)
    {
        return m_impl->stopModule(moduleId, result);
    }

    void IopEmulator::runEeCycles(uint64_t eeCycles) noexcept
    {
        const uint64_t total = m_impl->eeCycleCarry + eeCycles;
        const uint64_t iopCycles = total / 8u;
        m_impl->eeCycleCarry = total % 8u;
        if (iopCycles)
            m_impl->runCycles(iopCycles);
    }

    RpcResult IopEmulator::handleRpc(const RpcRequest &request)
    {
        return m_impl->rpc.handleRpc(request, *m_impl);
    }

    bool IopEmulator::hasRpcServer(uint32_t sid) const noexcept
    {
        return m_impl->rpc.hasServer(sid);
    }

    void IopEmulator::onSifTransfer(const SifTransfer &transfer)
    {
        m_impl->rpc.onSifTransfer(transfer);
    }

    uint64_t IopEmulator::cycles() const noexcept
    {
        return m_impl->totalCycles;
    }

    uint64_t IopEmulator::instructions() const noexcept
    {
        return m_impl->totalInstructions;
    }

    uint32_t IopEmulator::loadedModuleCount() const noexcept
    {
        return static_cast<uint32_t>(m_impl->modules.size());
    }

    uint32_t IopEmulator::threadCount() const noexcept
    {
        return static_cast<uint32_t>(m_impl->kernel.threadCount());
    }

    uint32_t IopEmulator::rpcServerCount() const noexcept
    {
        return static_cast<uint32_t>(m_impl->rpc.serverCount());
    }

}
