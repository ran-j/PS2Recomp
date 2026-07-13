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
