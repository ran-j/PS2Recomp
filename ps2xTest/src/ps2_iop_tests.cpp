#include "MiniTest.h"
#include "ps2x/iop/iop_subsystem.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#elif defined(__linux__)
#include <dlfcn.h>
#endif

namespace
{
    using namespace ps2x::iop;

    constexpr uint32_t kSyntheticSid = 0xF00DCAFEu;
    constexpr uint32_t kCoreCollisionSid = 0x80001300u;
    constexpr uint32_t kSyntheticFunction = 0x42u;
    constexpr uint32_t kCoreCollisionFunction = 0x99u;
    constexpr uint32_t kSyntheticEntryPoint = 0x00123456u;
    constexpr uint32_t kSpecificRecvXEntryPoint = kSyntheticEntryPoint + 0x100u;
    constexpr uint32_t kSyntheticCrc32 = 0xA1B2C3D4u;
    constexpr uint32_t kResponseXor = 0xA5A55A5Au;
    constexpr uint32_t kCoreCollisionResponse = 0xC0DEF00Du;

    class FakeIopHost final : public IopHost
    {
    public:
        explicit FakeIopHost(size_t memorySize = 0x10000u)
            : memory(memorySize, 0u)
        {
        }

        bool readGuest(uint32_t address, void *destination, size_t size) const override
        {
            if ((!destination && size != 0u) || !contains(address, size))
            {
                return false;
            }
            if (size != 0u)
            {
                std::memcpy(destination, memory.data() + address, size);
            }
            return true;
        }

        bool writeGuest(uint32_t address, const void *source, size_t size) override
        {
            if ((!source && size != 0u) || !contains(address, size))
            {
                return false;
            }
            if (size != 0u)
            {
                std::memcpy(memory.data() + address, source, size);
            }
            return true;
        }

        bool zeroGuest(uint32_t address, size_t size) override
        {
            if (!contains(address, size))
            {
                return false;
            }
            std::fill(memory.begin() + address, memory.begin() + address + size, 0u);
            return true;
        }

        bool normalizeGuestAddress(uint32_t address, uint32_t &normalized) const override
        {
            normalized = address & 0x1FFFFFFFu;
            return normalized < memory.size();
        }

        uint32_t allocateIopHandle(IopHandleKind kind) override
        {
            const uint32_t value = nextHandle;
            nextHandle += (kind == IopHandleKind::RpcPacket) ? 0x40u : 0x80u;
            return value;
        }

        uint32_t allocateGuest(uint32_t size, uint32_t alignment) override
        {
            if (size == 0u)
            {
                return 0u;
            }
            const uint64_t effectiveAlignment = alignment == 0u ? 1u : alignment;
            const uint64_t aligned = ((static_cast<uint64_t>(nextGuestAddress) + effectiveAlignment - 1u) /
                                      effectiveAlignment) *
                                     effectiveAlignment;
            if (aligned + size > memory.size())
            {
                return 0u;
            }
            nextGuestAddress = static_cast<uint32_t>(aligned + size);
            guestAllocations.push_back(static_cast<uint32_t>(aligned));
            return static_cast<uint32_t>(aligned);
        }

        void freeGuest(uint32_t address) override
        {
            freedGuestAddresses.push_back(address);
        }

        void audioCommand(uint32_t sid,
                          uint32_t function,
                          GuestBuffer send,
                          GuestBuffer receive) override
        {
            lastAudioSid = sid;
            lastAudioFunction = function;
            lastAudioSend = send;
            lastAudioReceive = receive;
            ++audioCalls;
        }

        std::string hostPath(HostPathKind kind) const override
        {
            switch (kind)
            {
            case HostPathKind::CdRoot:
                return "fake/cd";
            case HostPathKind::CdImage:
                return "fake/disc.iso";
            case HostPathKind::HostRoot:
                return "fake/host";
            case HostPathKind::MemoryCardRoot:
                return "fake/mc0";
            default:
                return "fake/elf";
            }
        }

        std::string translateGuestPath(std::string_view path) const override
        {
            return "translated/" + std::string(path);
        }

        bool searchCdFile(std::string_view path, CdFileInfo &file) override
        {
            const auto found = cdFiles.find(std::string(path));
            if (found == cdFiles.end())
            {
                file = {};
                return false;
            }
            file = found->second;
            return true;
        }

        uint64_t openHostFile(std::string_view path) override
        {
            const auto file = hostFileContents.find(std::string(path));
            if (file == hostFileContents.end())
            {
                return 0u;
            }
            const uint64_t handle = nextHostFileHandle++;
            openHostFiles.emplace(handle, file->first);
            return handle;
        }

        bool hostFileSize(uint64_t handle, uint64_t &size) const override
        {
            size = 0u;
            const auto open = openHostFiles.find(handle);
            if (open == openHostFiles.end())
            {
                return false;
            }
            const auto file = hostFileContents.find(open->second);
            if (file == hostFileContents.end())
            {
                return false;
            }
            size = file->second.size();
            return true;
        }

        bool readHostFile(uint64_t handle,
                          uint64_t offset,
                          void *destination,
                          size_t size,
                          size_t &bytesRead) override
        {
            bytesRead = 0u;
            if (!destination && size != 0u)
            {
                return false;
            }
            const auto open = openHostFiles.find(handle);
            if (open == openHostFiles.end())
            {
                return false;
            }
            const auto file = hostFileContents.find(open->second);
            if (file == hostFileContents.end() || offset > file->second.size())
            {
                return false;
            }
            bytesRead = std::min<size_t>(size, file->second.size() - static_cast<size_t>(offset));
            if (bytesRead != 0u)
            {
                std::memcpy(destination,
                            file->second.data() + static_cast<size_t>(offset),
                            bytesRead);
            }
            return true;
        }

        void closeHostFile(uint64_t handle) override
        {
            if (openHostFiles.erase(handle) != 0u)
            {
                closedHostFileHandles.push_back(handle);
            }
        }

        int32_t memoryCard(const MemoryCardRequest &request) override
        {
            lastMemoryCardRequest = request;
            ++memoryCardCalls;
            return 0;
        }

        bool hasGuestFunction(uint32_t address) const override
        {
            return address == guestFunctionAddress;
        }

        bool invokeGuestFunction(uint64_t callToken,
                                 uint32_t address,
                                 uint32_t a0,
                                 uint32_t a1,
                                 uint32_t a2,
                                 uint32_t a3,
                                 uint32_t *resultAddress) override
        {
            if (!hasGuestFunction(address))
            {
                return false;
            }
            lastCallToken = callToken;
            lastGuestArguments = {a0, a1, a2, a3};
            if (resultAddress)
            {
                *resultAddress = guestFunctionResult;
            }
            return true;
        }

        void log(LogLevel level, std::string_view message) override
        {
            logs.emplace_back(level, std::string(message));
        }

        bool writeWord(uint32_t address, uint32_t value)
        {
            return writeGuest(address, &value, sizeof(value));
        }

        uint32_t readWord(uint32_t address) const
        {
            uint32_t value = 0u;
            (void)readGuest(address, &value, sizeof(value));
            return value;
        }

        bool hasLog(std::string_view expected) const
        {
            return std::any_of(logs.begin(), logs.end(), [&](const auto &entry)
                               { return entry.second == expected; });
        }

        std::vector<uint8_t> memory;
        uint32_t nextHandle = 0x8000u;
        uint32_t nextGuestAddress = 0x4000u;
        std::vector<uint32_t> guestAllocations;
        std::vector<uint32_t> freedGuestAddresses;
        uint32_t audioCalls = 0u;
        uint32_t lastAudioSid = 0u;
        uint32_t lastAudioFunction = 0u;
        GuestBuffer lastAudioSend{};
        GuestBuffer lastAudioReceive{};
        uint32_t memoryCardCalls = 0u;
        MemoryCardRequest lastMemoryCardRequest{};
        uint32_t guestFunctionAddress = 0x2000u;
        uint32_t guestFunctionResult = 0x3000u;
        uint64_t lastCallToken = 0u;
        std::vector<uint32_t> lastGuestArguments;
        std::vector<std::pair<LogLevel, std::string>> logs;
        std::unordered_map<std::string, CdFileInfo> cdFiles;
        std::unordered_map<std::string, std::vector<uint8_t>> hostFileContents;
        std::unordered_map<uint64_t, std::string> openHostFiles;
        std::vector<uint64_t> closedHostFileHandles;
        uint64_t nextHostFileHandle = 1u;

    private:
        bool contains(uint32_t address, size_t size) const
        {
            const uint64_t end = static_cast<uint64_t>(address) + static_cast<uint64_t>(size);
            return end <= memory.size();
        }
    };

    bool containsDiagnostic(const DebugSnapshot &snapshot, std::string_view text)
    {
        return std::any_of(snapshot.diagnostics.begin(), snapshot.diagnostics.end(), [&](const std::string &diagnostic)
                           { return diagnostic.find(text) != std::string::npos; });
    }

    const DebugService *findService(const DebugSnapshot &snapshot, std::string_view name)
    {
        const auto it = std::find_if(snapshot.services.begin(), snapshot.services.end(), [&](const DebugService &service)
                                     { return service.name == name; });
        return it == snapshot.services.end() ? nullptr : &*it;
    }

    uint64_t metricValue(const DebugService &service, std::string_view name)
    {
        const auto it = std::find_if(service.metrics.begin(), service.metrics.end(), [&](const DebugMetric &metric)
                                     { return metric.name == name; });
        return it == service.metrics.end() ? std::numeric_limits<uint64_t>::max() : it->value;
    }

    bool pluginModuleIsLoaded(const std::filesystem::path &path)
    {
#if defined(_WIN32)
        return GetModuleHandleW(path.c_str()) != nullptr;
#elif defined(__linux__)
        void *handle = dlopen(path.c_str(), RTLD_NOW | RTLD_NOLOAD);
        if (!handle)
        {
            return false;
        }
        dlclose(handle);
        return true;
#else
        (void)path;
        return false;
#endif
    }
}

void register_ps2_iop_tests()
{
    MiniTest::Case("PS2IopSubsystem", [](TestCase &tc)
    {
        tc.Run("unknown SID remains unhandled without a matching profile", [](TestCase &t)
        {
            FakeIopHost host;
            ps2x::iop::IopSubsystem subsystem(host);

            std::string error;
            const bool configured = subsystem.configure({"unmatched.elf", 0x100000u, 0x12345678u}, &error);
            t.IsTrue(configured, "configuring an unmatched game should keep core-only IOP services available");

            ps2x::iop::RpcRequest request{};
            request.sid = 0xDEADC0DEu;
            request.function = 0x99u;
            const ps2x::iop::RpcResult result = subsystem.handleRpc(request);
            t.IsFalse(result.handled, "an unknown SID should not be claimed by the IOP subsystem");
            t.Equals(result.resultAddress, 0u, "an unknown SID should not return a guest result address");
            t.IsFalse(result.signalNowaitCompletion, "an unknown SID should not signal nowait completion");
            t.Equals(result.callbackPolicy, ps2x::iop::CallbackPolicy::RuntimeDefault,
                     "an unknown SID should preserve runtime callback handling");

            const ps2x::iop::DebugSnapshot snapshot = subsystem.debugSnapshot();
            t.IsTrue(snapshot.activeProfile.empty(), "an unmatched game should not activate a profile");
            t.IsTrue(snapshot.activeProvider.empty(), "an unmatched game should not report a profile provider");
        });

        tc.Run("PADMAN core service advertises both XPAD endpoints without fabricating calls", [](TestCase &t)
        {
            constexpr uint32_t kCommandSid = 0x80000100u;
            constexpr uint32_t kConnectionSid = 0x80000101u;
            constexpr uint32_t kSend = 0x100u;
            constexpr uint32_t kReceive = 0x200u;

            FakeIopHost host;
            ps2x::iop::IopSubsystem subsystem(host);
            std::string error;
            t.IsTrue(subsystem.configure({"unmatched.elf", 0u, 0u}, &error),
                     "PADMAN should be available as a core service");
            t.IsTrue(subsystem.handlesSid(kCommandSid),
                     "PADMAN command endpoint should be bindable");
            t.IsTrue(subsystem.handlesSid(kConnectionSid),
                     "PADMAN connection endpoint should be bindable");

            const DebugSnapshot initialSnapshot = subsystem.debugSnapshot();
            const DebugService *padman = findService(initialSnapshot, "PADMAN");
            t.IsTrue(padman != nullptr, "PADMAN should have one shared debug service identity");
            if (padman)
            {
                t.Equals(padman->sids.size(), static_cast<size_t>(2u),
                         "shared PADMAN service should advertise exactly two SIDs");
                t.IsTrue(std::find(padman->sids.begin(), padman->sids.end(), kCommandSid) != padman->sids.end(),
                         "shared PADMAN service should advertise the command SID");
                t.IsTrue(std::find(padman->sids.begin(), padman->sids.end(), kConnectionSid) != padman->sids.end(),
                         "shared PADMAN service should advertise the connection SID");
            }

            t.IsTrue(host.writeWord(kSend, 0x11u), "test should write an unsupported PADMAN command");
            t.IsTrue(host.writeWord(kReceive, 0xA5A5A5A5u), "test should seed the receive buffer");

            RpcRequest unsupported{};
            unsupported.sid = kCommandSid;
            unsupported.function = 1u;
            unsupported.send = {kSend, 128u};
            unsupported.receive = {kReceive, 128u};
            t.IsFalse(subsystem.handleRpc(unsupported).handled,
                      "PADMAN must not fabricate success for an unsupported command");
            t.Equals(host.readWord(kReceive), 0xA5A5A5A5u,
                     "rejected PADMAN command must leave the receive buffer unchanged");

            RpcRequest malformed = unsupported;
            malformed.sid = kConnectionSid;
            malformed.function = 2u;
            malformed.send = {kSend, 0u};
            t.IsFalse(subsystem.handleRpc(malformed).handled,
                      "malformed PADMAN call should remain visibly unhandled");
            t.Equals(host.audioCalls, 0u,
                     "PADMAN endpoints must not be routed to the audio backend");

            const DebugSnapshot finalSnapshot = subsystem.debugSnapshot();
            padman = findService(finalSnapshot, "PADMAN");
            t.IsTrue(padman != nullptr, "PADMAN debug service should remain present");
            if (padman)
            {
                t.Equals(metricValue(*padman, "rejected_calls"), uint64_t{2},
                         "both calls should share one PADMAN rejection counter");
                t.Equals(metricValue(*padman, "malformed_calls"), uint64_t{1},
                         "wrong function and empty send buffer should count as malformed");
                t.Equals(metricValue(*padman, "last_sid"), static_cast<uint64_t>(kConnectionSid),
                         "shared PADMAN state should record calls from the companion SID");
            }
            t.Equals(host.logs.size(), static_cast<size_t>(2u),
                     "each rejected PADMAN call should produce a visible diagnostic");

            t.IsTrue(host.writeWord(kSend, 0x10u), "test should write PADMAN INIT");
            t.IsTrue(host.writeWord(kReceive, 0xA5A5A5A5u), "test should reseed the INIT response");
            RpcRequest initialize{};
            initialize.sid = kCommandSid;
            initialize.function = 1u;
            initialize.send = {kSend, 0x80u};
            initialize.receive = {kReceive, 0x80u};
            const RpcResult initResult = subsystem.handleRpc(initialize);
            t.IsTrue(initResult.handled, "typed PADMAN INIT should be handled");
            t.Equals(initResult.resultAddress, kReceive,
                     "INIT should return its receive buffer");
            t.Equals(host.readWord(kReceive + 0x0Cu), 1u,
                     "INIT should report successful PADMAN initialization");

            t.IsTrue(host.writeWord(kSend, 0x12u), "test should write PADMAN GET_MODVER");
            t.IsTrue(host.writeWord(kReceive, 0xA5A5A5A5u), "test should reseed the response");
            RpcRequest getVersion{};
            getVersion.sid = kCommandSid;
            getVersion.function = 1u;
            getVersion.send = {kSend, 0x80u};
            getVersion.receive = {kReceive, 0x80u};
            const RpcResult versionResult = subsystem.handleRpc(getVersion);
            t.IsTrue(versionResult.handled, "typed PADMAN GET_MODVER should be handled");
            t.Equals(versionResult.resultAddress, kReceive,
                     "GET_MODVER should return its receive buffer");
            t.Equals(host.readWord(kReceive + 0x0Cu), 0x00000422u,
                     "GET_MODVER should report a PADMAN 4.22-compatible version");

            RpcRequest wrongSize = getVersion;
            wrongSize.receive.size = 0x7Cu;
            t.IsFalse(subsystem.handleRpc(wrongSize).handled,
                      "GET_MODVER with a non-128-byte response must remain rejected");

            constexpr uint32_t kPadArea = 0x400u;
            t.IsTrue(host.writeWord(kSend + 0x00u, 0x01u), "test should write PADMAN OPEN");
            t.IsTrue(host.writeWord(kSend + 0x04u, 0u), "test should select port zero");
            t.IsTrue(host.writeWord(kSend + 0x08u, 0u), "test should select slot zero");
            t.IsTrue(host.writeWord(kSend + 0x10u, kPadArea), "test should provide an aligned pad area");
            RpcRequest open = getVersion;
            const RpcResult openResult = subsystem.handleRpc(open);
            t.IsTrue(openResult.handled, "valid typed PADMAN OPEN should be handled");
            t.Equals(host.readWord(kReceive + 0x0Cu), 1u,
                     "OPEN should report success");
            t.Equals(host.readWord(kReceive + 0x14u), kPadArea,
                     "OPEN should return the initialized pad area");
            t.Equals(static_cast<uint32_t>(host.memory[kPadArea + 1u]), 0x41u,
                     "OPEN should initialize a digital pad packet");
            t.Equals(static_cast<uint32_t>(host.memory[kPadArea + 2u]), 0xFFu,
                     "OPEN should initialize all buttons released");
            t.Equals(static_cast<uint32_t>(host.memory[kPadArea + 112u]), 6u,
                     "OPEN should initialize the first XPAD half stable");
            t.Equals(static_cast<uint32_t>(host.memory[kPadArea + 0x80u + 112u]), 6u,
                     "OPEN should initialize the second XPAD half stable");

            RpcRequest invalidOpen = open;
            t.IsTrue(host.writeWord(kSend + 0x10u, kPadArea + 1u),
                     "test should provide a misaligned pad area");
            t.IsFalse(subsystem.handleRpc(invalidOpen).handled,
                      "misaligned PADMAN OPEN must remain rejected");

            subsystem.reset();
            const DebugSnapshot resetSnapshot = subsystem.debugSnapshot();
            const DebugService *resetPadman = findService(resetSnapshot, "PADMAN");
            t.IsTrue(resetPadman != nullptr, "PADMAN should remain registered after reset");
            if (resetPadman)
            {
                t.Equals(metricValue(*resetPadman, "open_count"), uint64_t{0},
                         "PADMAN reset should clear session counters");
                t.Equals(metricValue(*resetPadman, "port0_open"), uint64_t{0},
                         "PADMAN reset should close tracked port sessions");
            }
        });

        tc.Run("FILEIO core service binds without fabricating uncharacterized calls", [](TestCase &t)
        {
            constexpr uint32_t kSid = 0x80000001u;
            constexpr uint32_t kReceive = 0x300u;
            constexpr uint32_t kSentinel = 0xA55AA55Au;

            FakeIopHost host;
            ps2x::iop::IopSubsystem subsystem(host);
            std::string error;
            t.IsTrue(subsystem.configure({"unmatched.elf", 0u, 0u}, &error),
                     "FILEIO should be available without a game profile");
            t.IsTrue(subsystem.handlesSid(kSid),
                     "the core FILEIO endpoint should be bindable");
            t.IsTrue(host.writeWord(kReceive, kSentinel),
                     "FILEIO receive sentinel should be writable");

            RpcRequest request{};
            request.sid = kSid;
            request.function = 0x1234u;
            request.send = {0x200u, 0x20u};
            request.receive = {kReceive, sizeof(uint32_t)};
            t.IsFalse(subsystem.handleRpc(request).handled,
                      "uncharacterized FILEIO calls must remain rejected");
            t.Equals(host.readWord(kReceive), kSentinel,
                     "bind-only FILEIO must leave receive memory untouched");

            constexpr uint32_t kInitPointer = 0x400u;
            t.IsTrue(host.writeWord(request.send.address, kInitPointer),
                     "FILEIO init pointer should be writable");
            request.function = 0xFFu;
            request.send.size = sizeof(uint32_t);
            t.IsTrue(subsystem.handleRpc(request).handled,
                     "the exact FILEIO init envelope should be handled");
            t.Equals(host.readWord(kReceive), 0u,
                     "FILEIO init should return zero");

            t.IsTrue(host.writeWord(request.send.address, kInitPointer + 1u),
                     "misaligned FILEIO init pointer should be writable");
            t.IsTrue(host.writeWord(kReceive, kSentinel),
                     "malformed FILEIO receive sentinel should be writable");
            t.IsFalse(subsystem.handleRpc(request).handled,
                      "FILEIO init must reject a misaligned guest pointer");
            t.Equals(host.readWord(kReceive), kSentinel,
                     "malformed FILEIO init must leave receive memory untouched");

            DebugSnapshot snapshot = subsystem.debugSnapshot();
            const DebugService *fileio = findService(snapshot, "FILEIO");
            t.IsTrue(fileio != nullptr, "FILEIO should appear in core diagnostics");
            if (fileio)
            {
                t.Equals(fileio->sids.size(), static_cast<size_t>(1u),
                         "FILEIO should advertise one endpoint");
                t.Equals(metricValue(*fileio, "init_calls"), uint64_t{1},
                         "FILEIO should count typed initialization");
                t.Equals(metricValue(*fileio, "rejected_calls"), uint64_t{2},
                         "FILEIO should count observed calls");
                t.Equals(metricValue(*fileio, "last_function"), uint64_t{0xFFu},
                         "FILEIO should retain the last rejected function");
                t.Equals(metricValue(*fileio, "last_send_size"), uint64_t{4u},
                         "FILEIO should retain the send size");
                t.Equals(metricValue(*fileio, "last_receive_size"), uint64_t{4u},
                         "FILEIO should retain the receive size");
            }

            subsystem.reset();
            snapshot = subsystem.debugSnapshot();
            fileio = findService(snapshot, "FILEIO");
            if (fileio)
            {
                t.Equals(metricValue(*fileio, "rejected_calls"), uint64_t{0},
                         "FILEIO reset should clear diagnostics");
                t.Equals(metricValue(*fileio, "init_calls"), uint64_t{0},
                         "FILEIO reset should clear typed call counters");
            }
        });

        tc.Run("LOADFILE reports the compatible version and tracks HLE modules", [](TestCase &t)
        {
            FakeIopHost host;
            ps2x::iop::IopSubsystem subsystem(host);
            std::string error;
            t.IsTrue(subsystem.configure({"unmatched.elf", 0u, 0u}, &error),
                     "LOADFILE should be available as a core service");

            constexpr uint32_t kReceive = 0x100u;
            ps2x::iop::RpcRequest request{};
            request.sid = 0x80000006u;
            request.function = 0xFFu;
            request.receive = {kReceive, 4u};
            t.IsTrue(subsystem.handleRpc(request).handled,
                     "LOADFILE GET_VERSION should be handled");
            t.Equals(host.readWord(kReceive), 0x30333432u,
                     "LOADFILE should report the compatible ROM version");

            constexpr uint32_t kSend = 0x200u;
            constexpr std::string_view kPath = "cdrom0:\\IOP\\SIO2MAN.IRX;1";
            t.IsTrue(host.writeGuest(kSend + 8u, kPath.data(), kPath.size() + 1u),
                     "the module path should fit in fake guest memory");
            request.function = 0u;
            request.send = {kSend, 0x200u};
            request.receive = {kReceive, 8u};
            t.IsTrue(subsystem.handleRpc(request).handled,
                     "LOADFILE MOD_LOAD should be handled");
            t.Equals(host.readWord(kReceive), 1u,
                     "the first known HLE module should receive module ID one");
            t.Equals(host.readWord(kReceive + 4u), 0u,
                     "a known HLE module should report successful startup");

            t.IsTrue(subsystem.handleRpc(request).handled,
                     "loading an already active HLE module should remain successful");
            t.Equals(host.readWord(kReceive), 1u,
                     "an already active module should preserve its module ID");

            constexpr std::string_view kOptionalPath = "cdrom0:\\IOP\\KCEJEAST.IRX;1";
            t.IsTrue(host.writeGuest(kSend + 8u,
                                     kOptionalPath.data(),
                                     kOptionalPath.size() + 1u),
                     "the optional module path should fit in fake guest memory");
            t.IsTrue(subsystem.handleRpc(request).handled,
                     "LOADFILE should handle the game's optional HLE module");
            t.Equals(host.readWord(kReceive), 2u,
                     "the optional HLE module should receive the next module ID");
            t.Equals(host.readWord(kReceive + 4u), 0u,
                     "the optional HLE module should report successful startup");

            const ps2x::iop::DebugSnapshot snapshot = subsystem.debugSnapshot();
            const ps2x::iop::DebugService *service = findService(snapshot, "LOADFILE");
            t.IsNotNull(service, "the core service snapshot should include LOADFILE");
            if (service)
            {
                t.Equals(metricValue(*service, "loaded_modules"), uint64_t{2},
                         "LOADFILE should count unique loaded modules");
                t.Equals(metricValue(*service, "load_calls"), uint64_t{3},
                         "LOADFILE should count successful load calls");
            }
        });

        tc.Run("CD/DVD Disk Ready reports complete across repeated polls", [](TestCase &t)
        {
            FakeIopHost host;
            ps2x::iop::IopSubsystem subsystem(host);
            std::string error;
            t.IsTrue(subsystem.configure({"unmatched.elf", 0u, 0u}, &error),
                     "the CD/DVD Disk Ready service should be available as a core service");

            constexpr uint32_t kSend = 0x300u;
            constexpr uint32_t kReceive = 0x400u;
            constexpr uint32_t kDiskReadySid = 0x8000059Au;
            constexpr uint32_t kDiskReadyComplete = 2u;
            t.IsTrue(host.writeWord(kSend, 0u),
                     "the zero-mode Disk Ready request should fit in fake guest memory");

            ps2x::iop::RpcRequest request{};
            request.sid = kDiskReadySid;
            request.function = 0u;
            request.send = {kSend, 4u};
            request.receive = {kReceive, 4u};

            for (uint32_t poll = 0u; poll < 3u; ++poll)
            {
                host.writeWord(kReceive, 0xFFFFFFFFu);
                const ps2x::iop::RpcResult result = subsystem.handleRpc(request);
                t.IsTrue(result.handled,
                         "CD/DVD Disk Ready function zero should handle every poll");
                t.Equals(host.readWord(kReceive), kDiskReadyComplete,
                         "CD/DVD Disk Ready should report the standard complete status");
            }

            const ps2x::iop::DebugSnapshot snapshot = subsystem.debugSnapshot();
            const ps2x::iop::DebugService *service = findService(snapshot, "CD/DVD Disk Ready");
            t.IsNotNull(service, "the core service snapshot should include CD/DVD Disk Ready");
            if (service)
            {
                t.Equals(metricValue(*service, "disk_ready_calls"), uint64_t{3},
                         "CD/DVD Disk Ready should count repeated polls");
            }
        });

        tc.Run("CD/DVD Search File resolves the standard legacy request packet", [](TestCase &t)
        {
            FakeIopHost host;
            ps2x::iop::IopSubsystem subsystem(host);
            std::string error;
            t.IsTrue(subsystem.configure({"unmatched.elf", 0u, 0u}, &error),
                     "the CD/DVD Search File service should be available as a core service");

            constexpr uint32_t kSend = 0x500u;
            constexpr uint32_t kReceive = 0x700u;
            constexpr uint32_t kFile = 0x800u;
            constexpr uint32_t kSearchFileSid = 0x80000597u;
            constexpr uint32_t kLegacyPacketSize = 0x124u;
            constexpr std::string_view kPath = "cdrom0:\\SYSTEM.CNF;1";
            host.cdFiles.emplace(std::string(kPath),
                                 CdFileInfo{0x00123456u,
                                            4097u,
                                            {1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u},
                                            "SYSTEM.CNF"});

            t.IsTrue(host.writeGuest(kSend + 0x20u, kPath.data(), kPath.size() + 1u),
                     "the Search File path should fit in the standard request packet");
            t.IsTrue(host.writeWord(kSend + 0x120u, kFile),
                     "the Search File destination should fit in the standard request packet");
            t.IsTrue(host.writeWord(kReceive, 0xFFFFFFFFu),
                     "the Search File result should fit in fake guest memory");

            ps2x::iop::RpcRequest request{};
            request.sid = kSearchFileSid;
            request.function = 0u;
            request.send = {kSend, kLegacyPacketSize};
            request.receive = {kReceive, 4u};

            const ps2x::iop::RpcResult result = subsystem.handleRpc(request);
            t.IsTrue(result.handled, "CD/DVD Search File function zero should be handled");
            t.Equals(host.readWord(kReceive), 1u,
                     "CD/DVD Search File should report a found extracted-disc file");
            t.Equals(host.readWord(kFile), 0x00123456u,
                     "the returned sceCdlFILE should preserve the host resolver LSN");
            t.Equals(host.readWord(kFile + 4u), 4097u,
                     "the returned sceCdlFILE should contain the host byte size");

            std::array<char, 16> returnedName{};
            t.IsTrue(host.readGuest(kFile + 8u, returnedName.data(), returnedName.size()),
                     "the returned sceCdlFILE name should fit in fake guest memory");
            t.Equals(std::string(returnedName.data()), std::string("SYSTEM.CNF"),
                     "the returned filename should omit the ISO version suffix");

            host.writeWord(kReceive, 0xFFFFFFFFu);
            constexpr std::string_view kMissingPath = "cdrom0:\\MISSING.BIN;1";
            t.IsTrue(host.writeGuest(kSend + 0x20u,
                                     kMissingPath.data(),
                                     kMissingPath.size() + 1u),
                     "the missing Search File path should fit in the request packet");
            t.IsTrue(subsystem.handleRpc(request).handled,
                     "a missing file should still produce a completed Search File RPC");
            t.Equals(host.readWord(kReceive), 0u,
                     "CD/DVD Search File should report zero for a missing file");

            constexpr uint32_t kLayeredPacketSize = 0x128u;
            constexpr uint32_t kLayeredFile = 0x900u;
            host.cdFiles[std::string(kPath)] =
                CdFileInfo{0x00123457u, 4097u, {}, "SYSTEM.CNF"};
            t.IsTrue(host.writeGuest(kSend + 0x20u,
                                     kPath.data(),
                                     kPath.size() + 1u),
                     "the layered Search File path should remain at offset 0x20");
            t.IsTrue(host.writeWord(kSend + 0x120u, 0u),
                     "the layered Search File request should include a layer word");
            t.IsTrue(host.writeWord(kSend + 0x124u, kLayeredFile),
                     "the layered Search File destination should follow the layer word");
            request.send.size = kLayeredPacketSize;
            t.IsTrue(subsystem.handleRpc(request).handled,
                     "the layered Search File request should be handled");
            t.Equals(host.readWord(kReceive), 1u,
                     "the layered Search File request should resolve its path");
            t.Equals(host.readWord(kLayeredFile), 0x00123457u,
                     "the layered Search File request should write to its destination");

            const ps2x::iop::DebugSnapshot snapshot = subsystem.debugSnapshot();
            const ps2x::iop::DebugService *service =
                findService(snapshot, "CD/DVD Search File");
            t.IsNotNull(service, "the core service snapshot should include CD/DVD Search File");
            if (service)
            {
                t.Equals(metricValue(*service, "search_calls"), uint64_t{3},
                         "CD/DVD Search File should count found and missing requests");
                t.Equals(metricValue(*service, "search_hits"), uint64_t{2},
                         "CD/DVD Search File should count resolved files");
            }
        });

        tc.Run("built-in profiles select by ELF basename and keep core services active", [](TestCase &t)
        {
            FakeIopHost host;
            ps2x::iop::IopSubsystem subsystem(host);
            std::string error;

            t.IsTrue(subsystem.configure({"SLUS_201.84", 0u, 0u}, &error),
                     "RECVX profile should match case-insensitively by basename");
            ps2x::iop::DebugSnapshot snapshot = subsystem.debugSnapshot();
            t.Equals(snapshot.activeProfile, std::string("recvx-us"),
                     "RECVX ELF should select its built-in profile");
            t.IsNotNull(findService(snapshot, "TSNDDRV"),
                        "RECVX profile should register TSNDDRV");
            t.IsNotNull(findService(snapshot, "CRI DTX"),
                        "RECVX profile should register CRI DTX");
            t.IsNotNull(findService(snapshot, "dbcman"),
                        "core DBCMAN should remain active with a game profile");
            t.IsNotNull(findService(snapshot, "libsd"),
                        "core LIBSD should remain active with a game profile");
            t.IsNotNull(findService(snapshot, "MCSERV"),
                        "core MCSERV should remain active with a game profile");

            error.clear();
            t.IsTrue(subsystem.configure({"slus_203.88", 0u, 0u}, &error),
                     "Fatal Frame profile should configure after a different game");
            snapshot = subsystem.debugSnapshot();
            t.Equals(snapshot.activeProfile, std::string("fatal-frame-us"),
                     "reload should replace the active profile");
            t.IsNull(findService(snapshot, "CRI DTX"),
                     "reload should destroy services from the previous profile");
            t.IsNotNull(findService(snapshot, "SDRDRV"),
                        "Fatal Frame profile should expose SDRDRV");
        });

        tc.Run("Duelists profile binds its observed custom RPC without fabricating behavior", [](TestCase &t)
        {
            constexpr uint32_t kSid = 0x05730601u;
            constexpr uint32_t kSend = 0x1200u;
            constexpr uint32_t kReceive = 0x1300u;
            constexpr uint32_t kSentinel = 0xA5A55A5Au;

            FakeIopHost host;
            ps2x::iop::IopSubsystem subsystem(host);
            std::string error;
            t.IsTrue(subsystem.configure({"SLUS_205.15", 0u, 0u}, &error),
                     "Duelists profile should configure by ELF basename");
            t.Equals(subsystem.debugSnapshot().activeProfile,
                     std::string("duelists-of-the-roses-us"),
                     "Duelists ELF should select its isolated profile");
            t.IsTrue(subsystem.handlesSid(kSid),
                     "the observed Duelists custom service should be bindable");

            t.IsTrue(host.writeWord(kSend + 0u, 0x11111111u),
                     "first diagnostic request word should be writable");
            t.IsTrue(host.writeWord(kSend + 4u, 0x22222222u),
                     "second diagnostic request word should be writable");
            t.IsTrue(host.writeWord(kReceive, kSentinel),
                     "receive sentinel should be writable");

            ps2x::iop::RpcRequest request{};
            request.sid = kSid;
            request.function = 7u;
            request.send = {kSend, 8u};
            request.receive = {kReceive, sizeof(uint32_t)};
            t.IsFalse(subsystem.handleRpc(request).handled,
                      "uncharacterized Duelists calls must remain rejected");
            t.Equals(host.readWord(kReceive), kSentinel,
                     "the bind-only service must leave receive memory unchanged");

            const ps2x::iop::DebugSnapshot snapshot = subsystem.debugSnapshot();
            const ps2x::iop::DebugService *service =
                findService(snapshot, "Duelists custom RPC probe");
            t.IsNotNull(service, "Duelists diagnostics should appear in the debug snapshot");
            if (service)
            {
                t.Equals(metricValue(*service, "rejected_calls"), uint64_t{1},
                         "the probe should count observed calls");
                t.Equals(metricValue(*service, "last_function"), uint64_t{7},
                         "the probe should capture the function");
                t.Equals(metricValue(*service, "last_send_size"), uint64_t{8},
                         "the probe should capture the send size");
                t.Equals(metricValue(*service, "last_receive_size"), uint64_t{4},
                         "the probe should capture the receive size");
                t.Equals(metricValue(*service, "last_word_0"), uint64_t{0x11111111u},
                         "the probe should capture the first request word");
                t.Equals(metricValue(*service, "last_word_1"), uint64_t{0x22222222u},
                         "the probe should capture the second request word");
            }

            for (uint32_t offset = 0u; offset < 0x10u; offset += sizeof(uint32_t))
            {
                t.IsTrue(host.writeWord(kReceive + offset, kSentinel),
                         "typed response sentinel should be writable");
            }
            request.function = 0xF005u;
            request.send.size = 0x40u;
            request.receive.size = 0x10u;
            t.IsTrue(subsystem.handleRpc(request).handled,
                     "the observed F005 envelope should be handled");
            for (uint32_t offset = 0u; offset < 0x10u; offset += sizeof(uint32_t))
            {
                t.Equals(host.readWord(kReceive + offset), 0u,
                         "F005 should zero exactly its typed response");
            }

            t.IsTrue(host.writeWord(kReceive + 0x10u, kSentinel),
                     "memory after the typed response should be writable");
            request.receive.size = 0x0Cu;
            t.IsFalse(subsystem.handleRpc(request).handled,
                      "F005 with the wrong receive size must remain rejected");
            t.Equals(host.readWord(kReceive + 0x10u), kSentinel,
                     "F005 must not write beyond its exact response");

            constexpr uint32_t kArenaToken = 0x000C1800u;
            t.IsTrue(host.writeWord(kSend + 4u, kArenaToken),
                     "F002 arena token should be writable");
            for (uint32_t offset = 0u; offset < 0x10u; offset += sizeof(uint32_t))
            {
                t.IsTrue(host.writeWord(kReceive + offset, kSentinel),
                         "F002 response sentinel should be writable");
            }
            request.function = 0xF002u;
            request.receive.size = 0x10u;
            t.IsTrue(subsystem.handleRpc(request).handled,
                     "the observed F002 envelope should be handled");
            t.Equals(host.readWord(kReceive), kArenaToken,
                     "F002 should return its nonzero arena token");
            t.Equals(host.readWord(kReceive + 4u), 0u,
                     "F002 should zero the remainder of its response");

            t.IsTrue(host.writeWord(kSend + 4u, 0u),
                     "zero F002 arena token should be writable");
            t.IsFalse(subsystem.handleRpc(request).handled,
                      "F002 must reject a zero arena token");

            constexpr uint32_t kSelfToken = 0x00400080u;
            constexpr uint32_t kRegistrationPointer = 0x00005010u;
            t.IsTrue(host.writeWord(kSend + 0u, kSelfToken),
                     "5F10 self token should be writable");
            t.IsTrue(host.writeWord(kSend + 4u, 11u),
                     "5F10 registration index should be writable");
            t.IsTrue(host.writeWord(kSend + 8u, kRegistrationPointer),
                     "5F10 registration pointer should be writable");
            request.function = 0x5F10u;
            t.IsTrue(subsystem.handleRpc(request).handled,
                     "the observed 5F10 registration envelope should be handled");
            t.Equals(host.readWord(kReceive), 0u,
                     "5F10 should return a zeroed response");

            t.IsTrue(host.writeWord(kSend + 4u, 12u),
                     "out-of-range 5F10 index should be writable");
            t.IsFalse(subsystem.handleRpc(request).handled,
                      "5F10 must reject indexes outside its 12-entry registry");
            t.IsTrue(host.writeWord(kSend + 0u, kSelfToken + 4u),
                     "mismatched 5F10 self token should be writable");
            t.IsTrue(host.writeWord(kSend + 4u, 0u),
                     "valid 5F10 index should be writable");
            t.IsFalse(subsystem.handleRpc(request).handled,
                      "5F10 must reject a mismatched established self token");
            t.IsTrue(host.writeWord(kSend + 0u, kSelfToken),
                     "valid 5F10 self token should be restorable");
            t.IsTrue(host.writeWord(kSend + 8u, 0u),
                     "zero 5F10 pointer should be writable");
            t.IsFalse(subsystem.handleRpc(request).handled,
                      "5F10 must reject a zero registration pointer");

            constexpr uint32_t kRangeBase = 0x00150000u;
            constexpr uint32_t kRangeSize = 0x00038000u;
            t.IsTrue(host.writeWord(kSend + 0u, kSelfToken),
                     "5F12 self token should be writable");
            t.IsTrue(host.writeWord(kSend + 4u, kRangeBase),
                     "5F12 range base should be writable");
            t.IsTrue(host.writeWord(kSend + 8u, kRangeSize),
                     "5F12 range size should be writable");
            request.function = 0x5F12u;
            t.IsTrue(subsystem.handleRpc(request).handled,
                     "the observed 5F12 range envelope should be handled");
            t.Equals(host.readWord(kReceive), 0u,
                     "5F12 should return a zeroed response");

            t.IsTrue(host.writeWord(kSend + 4u, 0xFFFFFFF0u),
                     "overflowing 5F12 base should be writable");
            t.IsTrue(host.writeWord(kSend + 8u, 0x20u),
                     "overflowing 5F12 size should be writable");
            t.IsFalse(subsystem.handleRpc(request).handled,
                      "5F12 must reject a range that overflows uint32");
            t.IsTrue(host.writeWord(kSend + 4u, kRangeBase),
                     "valid 5F12 base should be restorable");
            t.IsTrue(host.writeWord(kSend + 8u, 0u),
                     "zero 5F12 size should be writable");
            t.IsFalse(subsystem.handleRpc(request).handled,
                      "5F12 must reject a zero-size range");
            t.IsTrue(host.writeWord(kSend + 4u, 0u),
                     "zero 5F12 base should be writable");
            t.IsTrue(host.writeWord(kSend + 8u, kRangeSize),
                     "valid 5F12 size should be restorable");
            t.IsFalse(subsystem.handleRpc(request).handled,
                      "5F12 must reject a zero-base range");

            t.IsTrue(host.writeWord(kSend + 0u, kSelfToken),
                     "5000 self token should be writable");
            t.IsTrue(host.writeWord(kSend + 4u, 0x00400000u),
                     "5000 stale trailing word should be writable");
            t.IsTrue(host.writeWord(kSend + 8u, 0x21u),
                     "5000 second stale trailing word should be writable");
            t.IsTrue(host.writeWord(kSend + 12u, 0x00911EE8u),
                     "5000 third stale trailing word should be writable");
            for (uint32_t offset = 0u; offset < 0x10u; offset += sizeof(uint32_t))
            {
                t.IsTrue(host.writeWord(kReceive + offset, kSentinel),
                         "5000 response sentinel should be writable");
            }
            t.IsTrue(host.writeWord(kReceive + 0x10u, kSentinel),
                     "memory after the 5000 response should be writable");
            request.function = 0x5000u;
            request.mode = 1u;
            const RpcResult function5000Result = subsystem.handleRpc(request);
            t.IsTrue(function5000Result.handled,
                     "the exact NOWAIT 5000 envelope should be handled");
            t.IsTrue(function5000Result.signalNowaitCompletion,
                     "5000 should request NOWAIT completion signaling");
            t.Equals(function5000Result.resultAddress, kReceive,
                     "5000 should return its receive buffer");
            for (uint32_t offset = 0u; offset < 0x10u; offset += sizeof(uint32_t))
            {
                t.Equals(host.readWord(kReceive + offset), 0u,
                         "5000 should zero its characterized response");
            }
            t.Equals(host.readWord(kReceive + 0x10u), kSentinel,
                     "5000 must not write beyond its response");

            t.IsTrue(host.writeWord(kReceive, kSentinel),
                     "malformed 5000 receive sentinel should be writable");
            request.mode = 0u;
            t.IsFalse(subsystem.handleRpc(request).handled,
                      "5000 must reject a non-NOWAIT call");
            t.Equals(host.readWord(kReceive), kSentinel,
                     "malformed 5000 must leave receive memory untouched");
            request.mode = 1u;
            t.IsTrue(host.writeWord(kSend, kSelfToken + 0x40u),
                     "mismatched aligned 5000 self token should be writable");
            t.IsFalse(subsystem.handleRpc(request).handled,
                      "5000 must reject a mismatched stable self token");
            t.Equals(host.readWord(kReceive), kSentinel,
                     "mismatched 5000 must leave receive memory untouched");

            t.IsTrue(host.writeWord(kSend + 0u, kSelfToken),
                     "5002 self token should be writable");
            t.IsTrue(host.writeWord(kSend + 4u, 0x00000002u),
                     "5002 stale first trailing word should be writable");
            t.IsTrue(host.writeWord(kSend + 8u, 0x00001000u),
                     "5002 stale second trailing word should be writable");
            t.IsTrue(host.writeWord(kSend + 12u, 0xFFFFF000u),
                     "5002 stale third trailing word should be writable");
            for (uint32_t offset = 0u; offset < 0x10u; offset += sizeof(uint32_t))
            {
                t.IsTrue(host.writeWord(kReceive + offset, kSentinel),
                         "5002 response sentinel should be writable");
            }
            request.function = 0x5002u;
            request.mode = 1u;
            const RpcResult function5002Result = subsystem.handleRpc(request);
            t.IsTrue(function5002Result.handled,
                     "the exact NOWAIT 5002 envelope should be handled");
            t.IsTrue(function5002Result.signalNowaitCompletion,
                     "5002 should request NOWAIT completion signaling");
            t.Equals(function5002Result.resultAddress, kReceive,
                     "5002 should return its receive buffer");
            for (uint32_t offset = 0u; offset < 0x10u; offset += sizeof(uint32_t))
            {
                t.Equals(host.readWord(kReceive + offset), 0u,
                         "5002 should return the characterized idle audio status");
            }
            t.Equals(host.readWord(kReceive + 0x10u), kSentinel,
                     "5002 must not write beyond its response");

            t.IsTrue(host.writeWord(kReceive, kSentinel),
                     "malformed 5002 receive sentinel should be writable");
            request.mode = 0u;
            t.IsFalse(subsystem.handleRpc(request).handled,
                      "5002 must reject a non-NOWAIT call");
            t.Equals(host.readWord(kReceive), kSentinel,
                     "malformed 5002 must leave receive memory untouched");
            request.mode = 1u;
            t.IsTrue(host.writeWord(kSend, kSelfToken + 0x40u),
                     "mismatched aligned 5002 self token should be writable");
            t.IsFalse(subsystem.handleRpc(request).handled,
                      "5002 must reject a mismatched stable self token");
            t.Equals(host.readWord(kReceive), kSentinel,
                     "mismatched 5002 must leave receive memory untouched");

            t.IsTrue(host.writeWord(kSend + 0u, kSelfToken),
                     "5005 self token should be writable");
            t.IsTrue(host.writeWord(kSend + 4u, 0x80u),
                     "5005 first opaque argument should be writable");
            t.IsTrue(host.writeWord(kSend + 8u, 0x80u),
                     "5005 second opaque argument should be writable");
            t.IsTrue(host.writeWord(kSend + 12u, 0xDEADBEEFu),
                     "5005 ignored trailing word should be writable");
            for (uint32_t offset = 0u; offset < 0x10u; offset += sizeof(uint32_t))
            {
                t.IsTrue(host.writeWord(kReceive + offset, kSentinel),
                         "5005 response sentinel should be writable");
            }
            request.function = 0x5005u;
            const RpcResult function5005Result = subsystem.handleRpc(request);
            t.IsTrue(function5005Result.handled,
                     "the exact NOWAIT 5005 envelope should be handled");
            t.IsTrue(function5005Result.signalNowaitCompletion,
                     "5005 should request NOWAIT completion signaling");
            t.Equals(function5005Result.resultAddress, kReceive,
                     "5005 should return its receive buffer");
            for (uint32_t offset = 0u; offset < 0x10u; offset += sizeof(uint32_t))
            {
                t.Equals(host.readWord(kReceive + offset), 0u,
                         "5005 should zero its characterized response");
            }

            t.IsTrue(host.writeWord(kSend + 4u, 0u),
                     "zero 5005 first argument should be writable");
            t.IsTrue(host.writeWord(kSend + 8u, 0u),
                     "zero 5005 second argument should be writable");
            t.IsTrue(subsystem.handleRpc(request).handled,
                     "5005 should accept characterized zero opaque arguments");

            t.IsTrue(host.writeWord(kReceive, kSentinel),
                     "malformed 5005 receive sentinel should be writable");
            request.mode = 0u;
            t.IsFalse(subsystem.handleRpc(request).handled,
                      "5005 must reject a non-NOWAIT call");
            t.Equals(host.readWord(kReceive), kSentinel,
                     "malformed 5005 must leave receive memory untouched");
            request.mode = 1u;
            t.IsTrue(host.writeWord(kSend, kSelfToken + 0x40u),
                     "mismatched aligned 5005 self token should be writable");
            t.IsFalse(subsystem.handleRpc(request).handled,
                      "5005 must reject a mismatched stable self token");
            t.Equals(host.readWord(kReceive), kSentinel,
                     "mismatched 5005 must leave receive memory untouched");

            t.IsTrue(host.writeWord(kSend + 0u, kSelfToken),
                     "F003 self token should be writable");
            t.IsTrue(host.writeWord(kSend + 4u, 0u),
                     "disabled F003 boolean should be writable");
            t.IsTrue(host.writeWord(kSend + 8u, 0x0034F6E0u),
                     "F003 ignored trailing word should be writable");
            for (uint32_t offset = 0u; offset < 0x10u; offset += sizeof(uint32_t))
            {
                t.IsTrue(host.writeWord(kReceive + offset, kSentinel),
                         "F003 response sentinel should be writable");
            }
            request.function = 0xF003u;
            const RpcResult disabledF003Result = subsystem.handleRpc(request);
            t.IsTrue(disabledF003Result.handled,
                     "the exact disabled F003 envelope should be handled");
            t.IsTrue(disabledF003Result.signalNowaitCompletion,
                     "F003 should request NOWAIT completion signaling");
            t.Equals(disabledF003Result.resultAddress, kReceive,
                     "F003 should return its receive buffer");
            for (uint32_t offset = 0u; offset < 0x10u; offset += sizeof(uint32_t))
            {
                t.Equals(host.readWord(kReceive + offset), 0u,
                         "F003 should zero its characterized response");
            }

            t.IsTrue(host.writeWord(kSend + 4u, 1u),
                     "enabled F003 boolean should be writable");
            t.IsTrue(subsystem.handleRpc(request).handled,
                     "F003 should accept the characterized enabled boolean");

            t.IsTrue(host.writeWord(kReceive, kSentinel),
                     "malformed F003 receive sentinel should be writable");
            t.IsTrue(host.writeWord(kSend + 4u, 2u),
                     "invalid F003 boolean should be writable");
            t.IsFalse(subsystem.handleRpc(request).handled,
                      "F003 must reject values outside its Boolean domain");
            t.Equals(host.readWord(kReceive), kSentinel,
                     "invalid F003 must leave receive memory untouched");
            t.IsTrue(host.writeWord(kSend + 4u, 1u),
                     "valid F003 boolean should be restorable");
            request.mode = 0u;
            t.IsFalse(subsystem.handleRpc(request).handled,
                      "F003 must reject a non-NOWAIT call");
            t.Equals(host.readWord(kReceive), kSentinel,
                     "non-NOWAIT F003 must leave receive memory untouched");
            request.mode = 1u;

            const ps2x::iop::DebugSnapshot handledSnapshot = subsystem.debugSnapshot();
            service = findService(handledSnapshot, "Duelists custom RPC probe");
            if (service)
            {
                t.Equals(metricValue(*service, "handled_calls"), uint64_t{10},
                         "the probe should count all typed calls");
                t.Equals(metricValue(*service, "f002_calls"), uint64_t{1},
                         "the probe should count typed F002 calls");
                t.Equals(metricValue(*service, "registration_count"), uint64_t{1},
                         "the probe should count valid 5F10 registrations");
                t.Equals(metricValue(*service, "last_registration_index"), uint64_t{11},
                         "the probe should retain the last valid registration index");
                t.Equals(metricValue(*service, "last_registration_pointer"),
                         uint64_t{kRegistrationPointer},
                         "the probe should retain the last valid registration pointer");
                t.Equals(metricValue(*service, "range_registration_count"), uint64_t{1},
                         "the probe should count valid 5F12 range registrations");
                t.Equals(metricValue(*service, "registered_range_base"), uint64_t{kRangeBase},
                         "the probe should retain the registered range base");
                t.Equals(metricValue(*service, "registered_range_size"), uint64_t{kRangeSize},
                         "the probe should retain the registered range size");
                t.Equals(metricValue(*service, "function_5000_calls"), uint64_t{1},
                         "the probe should count typed 5000 calls");
                t.Equals(metricValue(*service, "function_5002_calls"), uint64_t{1},
                         "the probe should count typed 5002 calls");
                t.Equals(metricValue(*service, "function_5005_calls"), uint64_t{2},
                         "the probe should count typed 5005 calls");
                t.Equals(metricValue(*service, "last_5005_argument_0"), uint64_t{0},
                         "the probe should retain the last 5005 first argument");
                t.Equals(metricValue(*service, "last_5005_argument_1"), uint64_t{0},
                         "the probe should retain the last 5005 second argument");
                t.Equals(metricValue(*service, "f003_calls"), uint64_t{2},
                         "the probe should count typed F003 calls");
                t.Equals(metricValue(*service, "last_f003_enabled"), uint64_t{1},
                         "the probe should retain the last F003 Boolean");
                t.Equals(metricValue(*service, "rejected_calls"), uint64_t{17},
                         "the probe should retain rejected-call diagnostics");
            }

            subsystem.reset();
            const ps2x::iop::DebugSnapshot resetSnapshot = subsystem.debugSnapshot();
            service = findService(resetSnapshot, "Duelists custom RPC probe");
            if (service)
            {
                t.Equals(metricValue(*service, "handled_calls"), uint64_t{0},
                         "reset should clear typed call counters");
                t.Equals(metricValue(*service, "registration_count"), uint64_t{0},
                         "reset should clear registration counters");
                t.Equals(metricValue(*service, "range_registration_count"), uint64_t{0},
                         "reset should clear range counters");
                t.Equals(metricValue(*service, "function_5000_calls"), uint64_t{0},
                         "reset should clear 5000 counters");
                t.Equals(metricValue(*service, "function_5002_calls"), uint64_t{0},
                         "reset should clear 5002 counters");
                t.Equals(metricValue(*service, "function_5005_calls"), uint64_t{0},
                         "reset should clear 5005 counters");
                t.Equals(metricValue(*service, "f003_calls"), uint64_t{0},
                         "reset should clear F003 counters");
                t.Equals(metricValue(*service, "self_token"), uint64_t{0},
                         "reset should clear the established self token");
                t.Equals(metricValue(*service, "registered_range_size"), uint64_t{0},
                         "reset should clear the registered range");
            }

            FakeIopHost otherHost;
            ps2x::iop::IopSubsystem otherSubsystem(otherHost);
            error.clear();
            t.IsTrue(otherSubsystem.configure({"SLUS_203.88", 0u, 0u}, &error),
                     "another built-in profile should configure");
            t.IsFalse(otherSubsystem.handlesSid(kSid),
                      "the Duelists custom SID must not leak into other profiles");
        });

        tc.Run("two subsystem instances isolate profile state and reset deterministically", [](TestCase &t)
        {
            FakeIopHost hostA;
            FakeIopHost hostB;
            ps2x::iop::IopSubsystem subsystemA(hostA);
            ps2x::iop::IopSubsystem subsystemB(hostB);
            std::string error;
            t.IsTrue(subsystemA.configure({"SLUS_205.78", 0u, 0u}, &error),
                     "first LotR instance should configure");
            t.IsTrue(subsystemB.configure({"SLUS_205.78", 0u, 0u}, &error),
                     "second LotR instance should configure");

            ps2x::iop::RpcRequest request{};
            request.sid = 0x00012345u;
            request.receive = {0x1000u, 8u};

            t.IsTrue(subsystemA.handleRpc(request).handled,
                     "first instance should handle LotR sound RPC");
            t.Equals(hostA.readWord(0x1004u), 1u,
                     "first instance should start its counter at one");
            (void)subsystemA.handleRpc(request);
            t.Equals(hostA.readWord(0x1004u), 2u,
                     "first instance should advance independently");

            t.IsTrue(subsystemB.handleRpc(request).handled,
                     "second instance should handle LotR sound RPC");
            t.Equals(hostB.readWord(0x1004u), 1u,
                     "second instance must not inherit the first counter");

            subsystemA.reset();
            (void)subsystemA.handleRpc(request);
            t.Equals(hostA.readWord(0x1004u), 1u,
                     "reset should restore per-instance service state");
        });

        tc.Run("TSNDDRV uses profile checksum bindings without writing invalid ports", [](TestCase &t)
        {
            FakeIopHost host(0x02000000u);
            ps2x::iop::IopSubsystem subsystem(host);
            std::string error;
            t.IsTrue(subsystem.configure({"slus_201.84", 0u, 0u}, &error),
                     "RECVX profile should configure for TSNDDRV command testing");

            constexpr uint32_t kResponseAddress = 0x1000u;
            ps2x::iop::RpcRequest stateRequest{};
            stateRequest.sid = 1u;
            stateRequest.function = 0x12u;
            stateRequest.receive = {kResponseAddress, sizeof(uint32_t)};
            t.IsTrue(subsystem.handleRpc(stateRequest).handled,
                     "TSNDDRV should return its configured status buffer");
            const uint32_t statusAddress = host.readWord(kResponseAddress);
            t.IsTrue(statusAddress != 0u, "TSNDDRV status buffer should be allocated");

            constexpr int16_t kChecksum = 0x1234;
            t.IsTrue(host.writeGuest(0x01E0EF10u, &kChecksum, sizeof(kChecksum)),
                     "RECVX primary checksum binding should be writable in the fake guest");

            constexpr uint32_t kCommandAddress = 0x2000u;
            std::array<uint8_t, 8> command{};
            command[0] = 0x29u;
            command[1] = 0u;
            t.IsTrue(host.writeGuest(kCommandAddress, command.data(), command.size()),
                     "valid TSNDDRV command should be writable");

            ps2x::iop::RpcRequest commandRequest{};
            commandRequest.sid = 0u;
            commandRequest.function = 0u;
            commandRequest.send = {kCommandAddress, static_cast<uint32_t>(command.size())};
            t.IsTrue(subsystem.handleRpc(commandRequest).handled,
                     "TSNDDRV should handle the characterized command queue");

            int16_t writtenChecksum = 0;
            t.IsTrue(host.readGuest(statusAddress + 0x26u,
                                    &writtenChecksum,
                                    sizeof(writtenChecksum)),
                     "TSNDDRV SE checksum slot should be readable");
            t.Equals(writtenChecksum, kChecksum,
                     "valid port should mirror the profile-bound checksum table");

            constexpr uint32_t kPastStatusAddress = 0x44u;
            constexpr uint16_t kSentinel = 0xBEEFu;
            t.IsTrue(host.writeGuest(statusAddress + kPastStatusAddress,
                                     &kSentinel,
                                     sizeof(kSentinel)),
                     "sentinel after the status structure should be writable");
            command[1] = 0x0Fu;
            (void)host.writeGuest(kCommandAddress, command.data(), command.size());
            (void)subsystem.handleRpc(commandRequest);

            uint16_t sentinelAfter = 0u;
            (void)host.readGuest(statusAddress + kPastStatusAddress,
                                 &sentinelAfter,
                                 sizeof(sentinelAfter));
            t.Equals(sentinelAfter, kSentinel,
                     "invalid port must not overwrite memory past the 0x42-byte status structure");
        });

        tc.Run("RECVX reset clears CRI object maps without global state", [](TestCase &t)
        {
            FakeIopHost host(0x02000000u);
            ps2x::iop::IopSubsystem subsystem(host);
            std::string error;
            t.IsTrue(subsystem.configure({"slus_201.84", 0u, 0u}, &error),
                     "RECVX profile should configure");

            constexpr uint32_t kSendAddress = 0x2000u;
            constexpr uint32_t kReceiveAddress = 0x2100u;
            host.writeWord(kSendAddress + 0u, 0u);
            host.writeWord(kSendAddress + 4u, 0x4000u);
            host.writeWord(kSendAddress + 8u, 0x100u);

            ps2x::iop::RpcRequest request{};
            request.sid = 0x7D000000u;
            request.function = 0x422u;
            request.send = {kSendAddress, 12u};
            request.receive = {kReceiveAddress, 4u};
            t.IsTrue(subsystem.handleRpc(request).handled,
                     "SJRMT create should be emulated by the RECVX profile");

            ps2x::iop::DebugSnapshot snapshot = subsystem.debugSnapshot();
            const ps2x::iop::DebugService *service =
                findService(snapshot, "CRI DTX");
            if (!service)
            {
                t.Fail("CRI DTX service should be visible in the debug snapshot");
                return;
            }
            t.Equals(metricValue(*service, "sjrmt_objects"), uint64_t{1},
                     "created CRI object should be tracked by this instance");

            subsystem.reset();
            snapshot = subsystem.debugSnapshot();
            service = findService(snapshot, "CRI DTX");
            if (!service)
            {
                t.Fail("CRI DTX service should survive reset");
                return;
            }
            t.Equals(metricValue(*service, "sjrmt_objects"), uint64_t{0},
                     "reset should clear CRI object maps");
        });

        tc.Run("reset closes profile-owned host file handles", [](TestCase &t)
        {
            FakeIopHost host;
            host.hostFileContents["translated/test.bin"] = {0x10u, 0x20u, 0x30u};

            ps2x::iop::IopSubsystem subsystem(host);
            std::string error;
            t.IsTrue(subsystem.configure({"SLUS_205.78", 0u, 0u}, &error),
                     "LotR profile should configure for file lifecycle testing");

            constexpr uint32_t kPathAddress = 0x1000u;
            constexpr uint32_t kReceiveAddress = 0x1100u;
            constexpr char kPath[] = "test.bin";
            t.IsTrue(host.writeGuest(kPathAddress, kPath, sizeof(kPath)),
                     "fake guest path should be writable");

            ps2x::iop::RpcRequest request{};
            request.sid = 0x0000FF01u;
            request.function = 0x08u;
            request.send = {kPathAddress, sizeof(kPath)};
            request.receive = {kReceiveAddress, 8u};
            t.IsTrue(subsystem.handleRpc(request).handled,
                     "LotR CLFILE open should be handled");
            t.Equals(host.openHostFiles.size(), size_t{1},
                     "open RPC should retain one opaque host file handle");

            ps2x::iop::DebugSnapshot snapshot = subsystem.debugSnapshot();
            const ps2x::iop::DebugService *service =
                findService(snapshot, "CLFILE");
            if (!service)
            {
                t.Fail("LotR CLFILE service should be visible before reset");
                return;
            }
            t.Equals(metricValue(*service, "open_files"), uint64_t{1},
                     "debug state should report the open file");

            subsystem.reset();
            t.IsTrue(host.openHostFiles.empty(),
                     "reset should release every retained host file handle");
            t.Equals(host.closedHostFileHandles.size(), size_t{1},
                     "host close callback should run exactly once");
            snapshot = subsystem.debugSnapshot();
            service = findService(snapshot, "CLFILE");
            if (!service)
            {
                t.Fail("LotR CLFILE service should survive reset");
                return;
            }
            t.Equals(metricValue(*service, "open_files"), uint64_t{0},
                     "reset should clear the CLFILE handle registry");
        });

#if defined(PS2X_TEST_IOP_PLUGIN_DIR)
        tc.Run("plugin module remains loaded through instances and unloads after subsystem destruction", [](TestCase &t)
        {
            const std::filesystem::path pluginDirectory(PS2X_TEST_IOP_PLUGIN_DIR);
#if defined(_WIN32)
            const std::filesystem::path pluginPath =
                pluginDirectory / "ps2_iop_fake_plugin.dll";
#else
            const std::filesystem::path pluginPath =
                pluginDirectory / "ps2_iop_fake_plugin.so";
#endif
            t.IsFalse(pluginModuleIsLoaded(pluginPath),
                      "synthetic plugin should not be loaded before discovery");
            {
                FakeIopHost host;
                ps2x::iop::IopSubsystem subsystem(host);
                subsystem.setPluginSearchPaths({pluginDirectory});
                std::string error;
                t.IsTrue(subsystem.loadPlugins(&error),
                         "synthetic plugins should load for lifetime testing");
                t.IsTrue(subsystem.configure({"synthetic_iop_test.elf",
                                              kSyntheticEntryPoint,
                                              kSyntheticCrc32},
                                             &error),
                         "synthetic plugin instance should be created");
                t.IsTrue(pluginModuleIsLoaded(pluginPath),
                         "module must stay loaded while a profile instance exists");
            }
            t.IsFalse(pluginModuleIsLoaded(pluginPath),
                      "module should unload after profile destruction and catalog teardown");
        });

        tc.Run("plugin discovery matches all identity fields and dispatches through the host bridge", [](TestCase &t)
        {
            FakeIopHost host;
            ps2x::iop::IopSubsystem subsystem(host);
            const std::filesystem::path pluginDirectory(PS2X_TEST_IOP_PLUGIN_DIR);

            t.IsTrue(std::filesystem::is_directory(pluginDirectory),
                     "the synthetic IOP plugin directory should be staged by the test build");
            subsystem.setPluginSearchPaths({pluginDirectory});

            std::string error;
            t.IsTrue(subsystem.loadPlugins(&error), "synthetic IOP plugin discovery should succeed");
            ps2x::iop::DebugSnapshot discoverySnapshot = subsystem.debugSnapshot();
            t.IsTrue(containsDiagnostic(discoverySnapshot, "loaded 4 profile(s)"),
                     "plugin discovery diagnostics should report all accepted synthetic profiles");
            t.IsTrue(containsDiagnostic(discoverySnapshot, "too many SIDs"),
                     "an invalid profile descriptor should be ignored with a diagnostic");
            t.IsTrue(containsDiagnostic(discoverySnapshot, "bad_abi"),
                     "an ABI-incompatible plugin should be ignored with a diagnostic");
            t.IsTrue(containsDiagnostic(discoverySnapshot, "incompatible ABI"),
                     "the incompatible-plugin diagnostic should explain the ABI failure");
            t.IsTrue(containsDiagnostic(discoverySnapshot, "missing_symbol"),
                     "a plugin without the query symbol should be ignored with a diagnostic");
            t.IsTrue(containsDiagnostic(discoverySnapshot, "missing ps2x_iop_query_v1"),
                     "the missing-symbol diagnostic should name the required entry point");

            auto expectNoProfile = [&](const ps2x::iop::GameIdentity &identity, const std::string &reason) {
                error.clear();
                t.IsTrue(subsystem.configure(identity, &error), "mismatching plugin identity should configure core-only services");
                const ps2x::iop::DebugSnapshot snapshot = subsystem.debugSnapshot();
                t.IsTrue(snapshot.activeProfile.empty(), reason);

                ps2x::iop::RpcRequest request{};
                request.sid = kSyntheticSid;
                request.function = kSyntheticFunction;
                t.IsFalse(subsystem.handleRpc(request).handled,
                          "a mismatching profile must not expose its synthetic SID");
            };

            expectNoProfile({"different.elf", kSyntheticEntryPoint, kSyntheticCrc32},
                            "a different ELF basename should not match the plugin profile");
            expectNoProfile({"synthetic_iop_test.elf", kSyntheticEntryPoint + 4u, kSyntheticCrc32},
                            "a different entry point should not match the plugin profile");
            expectNoProfile({"synthetic_iop_test.elf", kSyntheticEntryPoint, kSyntheticCrc32 ^ 1u},
                            "a different CRC32 should not match the plugin profile");

            error.clear();
            t.IsTrue(subsystem.configure({"synthetic_iop_test.elf", kSyntheticEntryPoint, kSyntheticCrc32}, &error),
                     "the synthetic ELF identity should activate the plugin profile");

            ps2x::iop::DebugSnapshot snapshot = subsystem.debugSnapshot();
            t.Equals(snapshot.activeProfile, std::string("synthetic-test-profile"),
                     "debug snapshot should expose the active plugin profile id");
            t.Equals(snapshot.activeProvider, std::string("ps2x-test-plugin"),
                     "debug snapshot should expose the plugin provider name");
            const ps2x::iop::DebugService *service = findService(snapshot, "synthetic-test-profile");
            if (!service)
            {
                t.Fail("debug snapshot should include the synthetic profile service");
                return;
            }
            t.IsTrue(service->profileSpecific, "plugin service should be marked profile-specific");
            t.IsTrue(std::find(service->sids.begin(), service->sids.end(), kSyntheticSid) != service->sids.end(),
                     "plugin service should advertise its synthetic SID");
            t.Equals(metricValue(*service, "reset_generation"), uint64_t{1},
                     "profile configuration should reset a new plugin instance once");

            ps2x::iop::RpcAbiRequest abiRequest{};
            abiRequest.boundSid = kSyntheticSid;
            abiRequest.function = kSyntheticFunction;
            abiRequest.registers.plausible = true;
            abiRequest.stack.plausible = true;
            t.Equals(subsystem.selectRpcAbi(abiRequest), ps2x::iop::RpcAbi::Stack,
                     "plugin should be able to select the stack RPC ABI");
            abiRequest.function = kSyntheticFunction + 1u;
            t.Equals(subsystem.selectRpcAbi(abiRequest), ps2x::iop::RpcAbi::RuntimeDefault,
                     "plugin ABI selection should fall back for unrelated functions");

            constexpr uint32_t kSendAddress = 0x1000u;
            constexpr uint32_t kReceiveAddress = 0x1100u;
            constexpr uint32_t kInput = 0x1234ABCDu;
            t.IsTrue(host.writeWord(kSendAddress, kInput), "fake host should seed the plugin send buffer");
            t.IsTrue(host.writeWord(kReceiveAddress, 0u), "fake host should clear the plugin receive buffer");

            ps2x::iop::RpcRequest request{};
            request.callToken = 0x1122334455667788ull;
            request.sid = kSyntheticSid;
            request.function = kSyntheticFunction;
            request.send = {kSendAddress, sizeof(uint32_t)};
            request.receive = {kReceiveAddress, sizeof(uint32_t)};
            const ps2x::iop::RpcResult result = subsystem.handleRpc(request);

            t.IsTrue(result.handled, "matching synthetic SID/function should dispatch to the plugin");
            t.Equals(result.resultAddress, kReceiveAddress, "plugin should return its receive-buffer address");
            t.IsTrue(result.signalNowaitCompletion, "plugin should request nowait completion signaling");
            t.Equals(result.callbackPolicy, ps2x::iop::CallbackPolicy::Suppress,
                     "plugin should be able to suppress the runtime callback");
            t.Equals(host.readWord(kReceiveAddress), kInput ^ kResponseXor,
                     "plugin should read and write guest memory through the IopHost bridge");

            ps2x::iop::RpcRequest unknownRequest{};
            unknownRequest.sid = 0xDEADC0DEu;
            unknownRequest.function = kSyntheticFunction;
            t.IsFalse(subsystem.handleRpc(unknownRequest).handled,
                      "unknown SID should remain unhandled while a plugin profile is active");

            constexpr uint32_t kCoreCollisionReceiveAddress = 0x1200u;
            ps2x::iop::RpcRequest collisionRequest{};
            collisionRequest.sid = kCoreCollisionSid;
            collisionRequest.function = kCoreCollisionFunction;
            collisionRequest.receive = {kCoreCollisionReceiveAddress, sizeof(uint32_t)};
            const ps2x::iop::RpcResult collisionResult = subsystem.handleRpc(collisionRequest);
            t.IsTrue(collisionResult.handled,
                     "a profile service should take precedence over a core service for the same SID");
            t.Equals(host.readWord(kCoreCollisionReceiveAddress), kCoreCollisionResponse,
                     "the profile collision route should reach the plugin implementation");

            subsystem.onSifTransfer({ps2x::iop::SifTransferKind::SetDma,
                                     ps2x::iop::SifTransferPhase::AfterCopy,
                                     kSendAddress,
                                     kReceiveAddress,
                                     sizeof(uint32_t)});
            snapshot = subsystem.debugSnapshot();
            service = findService(snapshot, "synthetic-test-profile");
            if (!service)
            {
                t.Fail("synthetic profile service should remain visible after dispatch");
                return;
            }
            t.Equals(metricValue(*service, "rpc_calls"), uint64_t{2},
                     "plugin debug metrics should count dispatched RPCs");
            t.Equals(metricValue(*service, "sif_transfers"), uint64_t{1},
                     "plugin debug metrics should count SIF transfer hooks");

            subsystem.reset();
            snapshot = subsystem.debugSnapshot();
            service = findService(snapshot, "synthetic-test-profile");
            if (!service)
            {
                t.Fail("synthetic profile service should remain visible after reset");
                return;
            }
            t.Equals(metricValue(*service, "reset_generation"), uint64_t{2},
                     "explicit subsystem reset should reach the plugin instance");
            t.Equals(metricValue(*service, "rpc_calls"), uint64_t{0},
                     "plugin reset should clear per-instance RPC state");
            t.Equals(metricValue(*service, "sif_transfers"), uint64_t{0},
                     "plugin reset should clear per-instance transfer state");

            error.clear();
            t.IsFalse(subsystem.configure({"synthetic_duplicate.elf", kSyntheticEntryPoint, kSyntheticCrc32}, &error),
                      "duplicate SIDs inside one profile layer should reject configuration");
            t.IsTrue(error.find("duplicate IOP SID") != std::string::npos,
                     "duplicate-SID failure should clearly identify the registry conflict");

            error.clear();
            t.IsFalse(subsystem.configure({"slus_201.84", kSyntheticEntryPoint, kSyntheticCrc32}, &error),
                      "equally specific built-in and plugin matchers should be ambiguous");
            t.IsTrue(error.find("ambiguous IOP profiles") != std::string::npos,
                     "ambiguous profile selection should fail clearly");

            error.clear();
            t.IsTrue(subsystem.configure({"slus_201.84",
                                          kSpecificRecvXEntryPoint,
                                          kSyntheticCrc32},
                                         &error),
                     "a more-specific matcher should win over a lower-specificity tie");
            t.Equals(subsystem.debugSnapshot().activeProfile,
                     std::string("synthetic-specific-recvx-profile"),
                     "the most specific plugin profile should be selected");

            error.clear();
            t.IsTrue(subsystem.configure({"different.elf", kSyntheticEntryPoint, kSyntheticCrc32}, &error),
                     "switching to an unmatched ELF should destroy the active plugin profile");
            t.IsTrue(host.hasLog("fake-plugin-destroy"),
                     "plugin profile destroy callback should run when the active profile is replaced");
            t.IsTrue(subsystem.debugSnapshot().activeProfile.empty(),
                     "switching to an unmatched ELF should leave no active profile");
        });
#endif
    });
}
