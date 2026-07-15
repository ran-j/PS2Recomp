#include "plugin_loader.h"

#include "ps2x/iop/plugin_api.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <utility>

#if PS2X_IOP_ENABLE_PLUGINS && defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#elif PS2X_IOP_ENABLE_PLUGINS && defined(__linux__)
#include <dlfcn.h>
#endif

namespace ps2x::iop::detail
{
    namespace
    {
        constexpr size_t kMaxPluginProfiles = 256u;
        constexpr size_t kMaxPluginSids = 256u;
        constexpr size_t kMaxPluginStringBytes = 4096u;

        bool validStringView(ps2x_iop_string_view_v1 value)
        {
            return value.size <= kMaxPluginStringBytes && (value.size == 0u || value.data != nullptr);
        }

        std::string copyString(ps2x_iop_string_view_v1 value)
        {
            if (!validStringView(value) || value.size == 0u)
            {
                return {};
            }
            return std::string(value.data, value.size);
        }

        ps2x_iop_string_view_v1 makeStringView(std::string_view value)
        {
            return {value.data(), value.size()};
        }

        int32_t copyHostString(const std::string &value,
                               char *destination,
                               size_t capacity,
                               size_t *requiredSize)
        {
            const size_t required = value.size() + 1;
            if (requiredSize)
            {
                *requiredSize = required;
            }
            if (!destination || capacity < required)
            {
                return PS2X_IOP_STATUS_BUFFER_TOO_SMALL_V1;
            }
            std::memcpy(destination, value.c_str(), required);
            return PS2X_IOP_STATUS_OK_V1;
        }

        IopHandleKind toHandleKind(uint32_t kind)
        {
            return kind == PS2X_IOP_HANDLE_RPC_PACKET_V1
                       ? IopHandleKind::RpcPacket
                       : IopHandleKind::RpcServer;
        }

        HostPathKind toHostPathKind(uint32_t kind)
        {
            switch (kind)
            {
            case PS2X_IOP_PATH_CD_ROOT_V1:
                return HostPathKind::CdRoot;
            case PS2X_IOP_PATH_CD_IMAGE_V1:
                return HostPathKind::CdImage;
            case PS2X_IOP_PATH_HOST_ROOT_V1:
                return HostPathKind::HostRoot;
            case PS2X_IOP_PATH_MEMORY_CARD_ROOT_V1:
                return HostPathKind::MemoryCardRoot;
            default:
                return HostPathKind::ElfDirectory;
            }
        }

        MemoryCardOperation toMemoryCardOperation(uint32_t operation)
        {
            const uint32_t last = static_cast<uint32_t>(MemoryCardOperation::Mkdir);
            if (operation > last)
            {
                throw std::out_of_range("invalid memory-card operation");
            }
            return static_cast<MemoryCardOperation>(operation);
        }

        class HostApiBridge
        {
        public:
            explicit HostApiBridge(IopHost &hostRef)
                : host(hostRef)
            {
                api.abi_version = PS2X_IOP_ABI_VERSION_V1;
                api.struct_size = sizeof(api);
                api.userdata = this;
                api.read_guest = &readGuest;
                api.write_guest = &writeGuest;
                api.zero_guest = &zeroGuest;
                api.normalize_guest_address = &normalizeGuestAddress;
                api.allocate_iop_handle = &allocateIopHandle;
                api.allocate_guest = &allocateGuest;
                api.free_guest = &freeGuest;
                api.audio_command = &audioCommand;
                api.get_host_path = &getHostPath;
                api.translate_guest_path = &translateGuestPath;
                api.open_host_file = &openHostFile;
                api.host_file_size = &hostFileSize;
                api.read_host_file = &readHostFile;
                api.close_host_file = &closeHostFile;
                api.memory_card = &memoryCard;
                api.has_guest_function = &hasGuestFunction;
                api.invoke_guest_function = &invokeGuestFunction;
                api.log = &log;
            }

            ps2x_iop_host_api_v1 api{};
            IopHost &host;

        private:
            static HostApiBridge *self(void *userdata)
            {
                return static_cast<HostApiBridge *>(userdata);
            }

            template <typename Callback>
            static int32_t guardedStatus(Callback &&callback) noexcept
            {
                try
                {
                    return static_cast<int32_t>(
                        std::forward<Callback>(callback)());
                }
                catch (...)
                {
                    return PS2X_IOP_STATUS_FAILED_V1;
                }
            }

            template <typename Value, typename Callback>
            static Value guardedValue(Value fallback, Callback &&callback) noexcept
            {
                try
                {
                    return static_cast<Value>(
                        std::forward<Callback>(callback)());
                }
                catch (...)
                {
                    return fallback;
                }
            }

            template <typename Callback>
            static void guardedVoid(Callback &&callback) noexcept
            {
                try
                {
                    std::forward<Callback>(callback)();
                }
                catch (...)
                {
                }
            }

            static int32_t readGuest(void *userdata, uint32_t address, void *destination, size_t size)
            {
                if (!userdata || (!destination && size != 0))
                {
                    return PS2X_IOP_STATUS_INVALID_ARGUMENT_V1;
                }
                return guardedStatus([&]()
                                     { return self(userdata)->host.readGuest(address, destination, size)
                                                  ? PS2X_IOP_STATUS_OK_V1
                                                  : PS2X_IOP_STATUS_FAILED_V1; });
            }

            static int32_t writeGuest(void *userdata, uint32_t address, const void *source, size_t size)
            {
                if (!userdata || (!source && size != 0))
                {
                    return PS2X_IOP_STATUS_INVALID_ARGUMENT_V1;
                }
                return guardedStatus([&]()
                                     { return self(userdata)->host.writeGuest(address, source, size)
                                                  ? PS2X_IOP_STATUS_OK_V1
                                                  : PS2X_IOP_STATUS_FAILED_V1; });
            }

            static int32_t zeroGuest(void *userdata, uint32_t address, size_t size)
            {
                if (!userdata)
                {
                    return PS2X_IOP_STATUS_INVALID_ARGUMENT_V1;
                }
                return guardedStatus([&]()
                                     { return self(userdata)->host.zeroGuest(address, size)
                                                  ? PS2X_IOP_STATUS_OK_V1
                                                  : PS2X_IOP_STATUS_FAILED_V1; });
            }

            static int32_t normalizeGuestAddress(void *userdata, uint32_t address, uint32_t *normalized)
            {
                if (!userdata || !normalized)
                {
                    return PS2X_IOP_STATUS_INVALID_ARGUMENT_V1;
                }
                return guardedStatus([&]()
                                     { return self(userdata)->host.normalizeGuestAddress(address, *normalized)
                                                  ? PS2X_IOP_STATUS_OK_V1
                                                  : PS2X_IOP_STATUS_FAILED_V1; });
            }

            static uint32_t allocateIopHandle(void *userdata, uint32_t kind)
            {
                if (!userdata)
                {
                    return 0;
                }
                return guardedValue<uint32_t>(0u, [&]()
                                              { return self(userdata)->host.allocateIopHandle(toHandleKind(kind)); });
            }

            static uint32_t allocateGuest(void *userdata, uint32_t size, uint32_t alignment)
            {
                if (!userdata)
                {
                    return 0;
                }
                return guardedValue<uint32_t>(0u, [&]()
                                              { return self(userdata)->host.allocateGuest(size, alignment); });
            }

            static void freeGuest(void *userdata, uint32_t address)
            {
                if (userdata && address)
                {
                    guardedVoid([&]()
                                { self(userdata)->host.freeGuest(address); });
                }
            }

            static int32_t audioCommand(void *userdata,
                                        uint32_t sid,
                                        uint32_t function,
                                        ps2x_iop_guest_buffer_v1 send,
                                        ps2x_iop_guest_buffer_v1 receive)
            {
                if (!userdata)
                {
                    return PS2X_IOP_STATUS_INVALID_ARGUMENT_V1;
                }
                return guardedStatus([&]()
                                     {
                                         self(userdata)->host.audioCommand(sid,
                                                                           function,
                                                                           {send.address, send.size},
                                                                           {receive.address, receive.size});
                                         return PS2X_IOP_STATUS_OK_V1; });
            }

            static int32_t getHostPath(void *userdata,
                                       uint32_t kind,
                                       char *destination,
                                       size_t capacity,
                                       size_t *requiredSize)
            {
                if (!userdata)
                {
                    return PS2X_IOP_STATUS_INVALID_ARGUMENT_V1;
                }
                return guardedStatus([&]()
                                     { return copyHostString(self(userdata)->host.hostPath(toHostPathKind(kind)),
                                                             destination,
                                                             capacity,
                                                             requiredSize); });
            }

            static int32_t translateGuestPath(void *userdata,
                                              ps2x_iop_string_view_v1 path,
                                              char *destination,
                                              size_t capacity,
                                              size_t *requiredSize)
            {
                if (!userdata || (!path.data && path.size != 0))
                {
                    return PS2X_IOP_STATUS_INVALID_ARGUMENT_V1;
                }
                return guardedStatus([&]()
                                     {
                                         const std::string translated = self(userdata)->host.translateGuestPath(
                                             std::string_view(path.data ? path.data : "", path.size));
                                         return copyHostString(translated, destination, capacity, requiredSize); });
            }

            static uint64_t openHostFile(void *userdata,
                                         ps2x_iop_string_view_v1 path)
            {
                if (!userdata || (!path.data && path.size != 0u))
                {
                    return 0u;
                }
                return guardedValue<uint64_t>(0u, [&]()
                                              { return self(userdata)->host.openHostFile(
                                                    std::string_view(path.data ? path.data : "", path.size)); });
            }

            static int32_t hostFileSize(void *userdata,
                                        uint64_t handle,
                                        uint64_t *size)
            {
                if (!userdata || handle == 0u || !size)
                {
                    return PS2X_IOP_STATUS_INVALID_ARGUMENT_V1;
                }
                return guardedStatus([&]()
                                     { return self(userdata)->host.hostFileSize(handle, *size)
                                                  ? PS2X_IOP_STATUS_OK_V1
                                                  : PS2X_IOP_STATUS_FAILED_V1; });
            }

            static int32_t readHostFile(void *userdata,
                                        uint64_t handle,
                                        uint64_t offset,
                                        void *destination,
                                        size_t size,
                                        size_t *bytesRead)
            {
                if (!userdata || handle == 0u || !bytesRead ||
                    (!destination && size != 0u))
                {
                    return PS2X_IOP_STATUS_INVALID_ARGUMENT_V1;
                }
                return guardedStatus([&]()
                                     { return self(userdata)->host.readHostFile(handle,
                                                                                offset,
                                                                                destination,
                                                                                size,
                                                                                *bytesRead)
                                                  ? PS2X_IOP_STATUS_OK_V1
                                                  : PS2X_IOP_STATUS_FAILED_V1; });
            }

            static void closeHostFile(void *userdata, uint64_t handle)
            {
                if (userdata && handle != 0u)
                {
                    guardedVoid([&]()
                                { self(userdata)->host.closeHostFile(handle); });
                }
            }

            static int32_t memoryCard(void *userdata,
                                      const ps2x_iop_memory_card_request_v1 *request,
                                      int32_t *result)
            {
                if (!userdata || !request || !result || request->struct_size < sizeof(*request))
                {
                    return PS2X_IOP_STATUS_INVALID_ARGUMENT_V1;
                }
                try
                {
                    MemoryCardRequest converted;
                    converted.operation = toMemoryCardOperation(request->operation);
                    std::copy(std::begin(request->arguments), std::end(request->arguments), converted.arguments.begin());
                    *result = self(userdata)->host.memoryCard(converted);
                    return PS2X_IOP_STATUS_OK_V1;
                }
                catch (...)
                {
                    return PS2X_IOP_STATUS_FAILED_V1;
                }
            }

            static int32_t hasGuestFunction(void *userdata, uint32_t address)
            {
                if (!userdata)
                {
                    return PS2X_IOP_STATUS_INVALID_ARGUMENT_V1;
                }
                return guardedStatus([&]()
                                     { return self(userdata)->host.hasGuestFunction(address) ? 1 : 0; });
            }

            static int32_t invokeGuestFunction(void *userdata,
                                               uint64_t callToken,
                                               uint32_t address,
                                               uint32_t a0,
                                               uint32_t a1,
                                               uint32_t a2,
                                               uint32_t a3,
                                               uint32_t *resultAddress)
            {
                if (!userdata)
                {
                    return PS2X_IOP_STATUS_INVALID_ARGUMENT_V1;
                }
                return guardedStatus([&]()
                                     { return self(userdata)->host.invokeGuestFunction(callToken,
                                                                                       address,
                                                                                       a0,
                                                                                       a1,
                                                                                       a2,
                                                                                       a3,
                                                                                       resultAddress)
                                                  ? 1
                                                  : 0; });
            }

            static void log(void *userdata, uint32_t level, ps2x_iop_string_view_v1 message)
            {
                if (!userdata || (!message.data && message.size != 0))
                {
                    return;
                }
                const uint32_t maxLevel = static_cast<uint32_t>(LogLevel::Error);
                const auto converted = static_cast<LogLevel>(std::min(level, maxLevel));
                guardedVoid([&]()
                            { self(userdata)->host.log(
                                  converted,
                                  std::string_view(message.data ? message.data : "", message.size)); });
            }
        };

        ps2x_iop_rpc_candidate_v1 toPluginCandidate(const RpcCallCandidate &candidate)
        {
            return {
                candidate.sendSize,
                candidate.receiveAddress,
                candidate.receiveSize,
                candidate.endFunction,
                candidate.endParameter,
                candidate.plausible ? 1u : 0u,
            };
        }

        class PluginService final : public IopService
        {
        public:
            PluginService(IopHost &host,
                          std::shared_ptr<void> libraryKeepAlive,
                          ps2x_iop_profile_api_v1 profileApi,
                          std::string serviceName,
                          std::vector<uint32_t> serviceSids,
                          const GameIdentity &identity)
                : m_libraryKeepAlive(std::move(libraryKeepAlive)),
                  m_api(profileApi),
                  m_name(std::move(serviceName)),
                  m_sids(std::move(serviceSids)),
                  m_host(host)
            {
                const ps2x_iop_game_identity_v1 pluginIdentity{
                    sizeof(ps2x_iop_game_identity_v1),
                    makeStringView(identity.elfName),
                    identity.entryPoint,
                    identity.crc32,
                };
                try
                {
                    m_instance = m_api.create(&m_host.api, &pluginIdentity);
                }
                catch (...)
                {
                    throw std::runtime_error("plugin profile create threw an exception");
                }
                if (!m_instance)
                {
                    throw std::runtime_error("plugin profile create returned null");
                }
            }

            ~PluginService() override
            {
                if (m_instance && m_api.destroy)
                {
                    try
                    {
                        m_api.destroy(m_instance);
                    }
                    catch (...)
                    {
                        m_host.host.log(LogLevel::Error,
                                        "IOP plugin destroy threw for " + m_name);
                    }
                }
                m_instance = nullptr;
            }

            std::string_view name() const override
            {
                return m_name;
            }

            std::span<const uint32_t> sids() const override
            {
                return m_sids;
            }

            void reset() override
            {
                if (!m_api.reset)
                {
                    return;
                }
                int32_t status = PS2X_IOP_STATUS_FAILED_V1;
                try
                {
                    status = m_api.reset(m_instance);
                }
                catch (...)
                {
                }
                if (status != PS2X_IOP_STATUS_OK_V1)
                {
                    m_host.host.log(LogLevel::Warning, "IOP plugin reset failed for " + m_name);
                }
            }

            RpcAbi selectRpcAbi(const RpcAbiRequest &request) const override
            {
                if (!m_api.select_rpc_abi)
                {
                    return RpcAbi::RuntimeDefault;
                }
                const ps2x_iop_rpc_abi_request_v1 converted{
                    sizeof(ps2x_iop_rpc_abi_request_v1),
                    request.boundSid,
                    request.function,
                    toPluginCandidate(request.registers),
                    toPluginCandidate(request.stack),
                };
                uint32_t result = PS2X_IOP_RPC_ABI_DEFAULT_V1;
                try
                {
                    result = m_api.select_rpc_abi(m_instance, &converted);
                }
                catch (...)
                {
                    m_host.host.log(LogLevel::Warning,
                                    "IOP plugin ABI selector threw for " + m_name);
                }
                if (result == PS2X_IOP_RPC_ABI_REGISTERS_V1)
                {
                    return RpcAbi::Registers;
                }
                if (result == PS2X_IOP_RPC_ABI_STACK_V1)
                {
                    return RpcAbi::Stack;
                }
                return RpcAbi::RuntimeDefault;
            }

            RpcResult handleRpc(const RpcRequest &request) override
            {
                const ps2x_iop_rpc_request_v1 converted{
                    sizeof(ps2x_iop_rpc_request_v1),
                    request.callToken,
                    request.clientAddress,
                    request.serverAddress,
                    request.serverFunction,
                    request.serverBuffer,
                    request.sid,
                    request.function,
                    request.mode,
                    {request.send.address, request.send.size},
                    {request.receive.address, request.receive.size},
                    request.endFunction,
                    request.endParameter,
                };
                ps2x_iop_rpc_result_v1 result{};
                result.struct_size = sizeof(result);
                int32_t status = PS2X_IOP_STATUS_FAILED_V1;
                try
                {
                    status = m_api.handle_rpc(m_instance, &converted, &result);
                }
                catch (...)
                {
                }
                if (status != PS2X_IOP_STATUS_OK_V1 ||
                    result.struct_size < sizeof(result))
                {
                    m_host.host.log(LogLevel::Warning, "IOP plugin RPC failed for " + m_name);
                    return {};
                }
                return {
                    result.handled != 0,
                    result.result_address,
                    result.signal_nowait_completion != 0,
                    result.signal_completion != 0,
                    result.callback_policy == PS2X_IOP_CALLBACK_SUPPRESS_V1
                        ? CallbackPolicy::Suppress
                        : CallbackPolicy::RuntimeDefault,
                    result.server_dispatch_policy == PS2X_IOP_SERVER_DISPATCH_SUPPRESS_V1
                        ? ServerDispatchPolicy::Suppress
                        : ServerDispatchPolicy::RuntimeDefault,
                };
            }

            void onSifTransfer(const SifTransfer &transfer) override
            {
                if (!m_api.on_sif_transfer)
                {
                    return;
                }
                const ps2x_iop_sif_transfer_v1 converted{
                    sizeof(ps2x_iop_sif_transfer_v1),
                    static_cast<uint32_t>(transfer.kind),
                    static_cast<uint32_t>(transfer.phase),
                    transfer.sourceAddress,
                    transfer.destinationAddress,
                    transfer.size,
                };
                int32_t status = PS2X_IOP_STATUS_FAILED_V1;
                try
                {
                    status = m_api.on_sif_transfer(m_instance, &converted);
                }
                catch (...)
                {
                }
                if (status != PS2X_IOP_STATUS_OK_V1)
                {
                    m_host.host.log(LogLevel::Warning, "IOP plugin transfer hook failed for " + m_name);
                }
            }

            void appendDebugMetrics(std::vector<DebugMetric> &metrics) const override
            {
                if (!m_api.debug_metric_count || !m_api.debug_metric)
                {
                    return;
                }
                size_t rawCount = 0u;
                try
                {
                    rawCount = m_api.debug_metric_count(m_instance);
                }
                catch (...)
                {
                    return;
                }
                const size_t count = std::min<size_t>(rawCount, 256);
                for (size_t i = 0; i < count; ++i)
                {
                    ps2x_iop_debug_metric_v1 metric{};
                    metric.struct_size = sizeof(metric);
                    int32_t status = PS2X_IOP_STATUS_FAILED_V1;
                    try
                    {
                        status = m_api.debug_metric(m_instance, i, &metric);
                    }
                    catch (...)
                    {
                    }
                    if (status != PS2X_IOP_STATUS_OK_V1 ||
                        metric.struct_size < sizeof(metric))
                    {
                        continue;
                    }
                    metrics.push_back({copyString(metric.name), metric.value, metric.hexadecimal != 0});
                }
            }

        private:
            std::shared_ptr<void> m_libraryKeepAlive;
            ps2x_iop_profile_api_v1 m_api{};
            std::string m_name;
            std::vector<uint32_t> m_sids;
            HostApiBridge m_host;
            void *m_instance = nullptr;
        };

        bool hasPluginExtension(const std::filesystem::path &path)
        {
            std::string extension = path.extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char value)
                           { return static_cast<char>(std::tolower(value)); });
#if defined(_WIN32)
            return extension == ".dll";
#elif defined(__linux__)
            return extension == ".so";
#else
            (void)extension;
            return false;
#endif
        }

        std::string formatPluginDiagnostic(const std::filesystem::path &path, std::string_view reason)
        {
            return "IOP plugin '" + path.string() + "': " + std::string(reason);
        }
    }

    class PluginCatalog::DynamicLibrary
    {
    public:
        explicit DynamicLibrary(std::filesystem::path sourcePath)
            : path(std::move(sourcePath))
        {
        }

        ~DynamicLibrary()
        {
#if PS2X_IOP_ENABLE_PLUGINS && defined(_WIN32)
            if (handle)
            {
                FreeLibrary(static_cast<HMODULE>(handle));
            }
#elif PS2X_IOP_ENABLE_PLUGINS && defined(__linux__)
            if (handle)
            {
                dlclose(handle);
            }
#endif
        }

        bool open(std::string &error)
        {
#if PS2X_IOP_ENABLE_PLUGINS && defined(_WIN32)
            handle = LoadLibraryW(path.c_str());
            if (!handle)
            {
                error = "LoadLibraryW failed with code " + std::to_string(GetLastError());
                return false;
            }
            return true;
#elif PS2X_IOP_ENABLE_PLUGINS && defined(__linux__)
            handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
            if (!handle)
            {
                const char *message = dlerror();
                error = message ? message : "dlopen failed";
                return false;
            }
            return true;
#else
            error = "dynamic IOP plugins are disabled on this platform";
            return false;
#endif
        }

        void *symbol(const char *name) const
        {
#if PS2X_IOP_ENABLE_PLUGINS && defined(_WIN32)
            return handle ? reinterpret_cast<void *>(GetProcAddress(static_cast<HMODULE>(handle), name)) : nullptr;
#elif PS2X_IOP_ENABLE_PLUGINS && defined(__linux__)
            return handle ? dlsym(handle, name) : nullptr;
#else
            (void)name;
            return nullptr;
#endif
        }

        std::filesystem::path path;
        void *handle = nullptr;
    };

    PluginCatalog::PluginCatalog(IopHost &host)
        : m_host(host)
    {
    }

    PluginCatalog::~PluginCatalog() = default;

    // TODO I never test this one
    bool PluginCatalog::load(const std::vector<std::filesystem::path> &searchPaths,
                             std::vector<ProfileDefinition> &profiles,
                             std::vector<std::string> &diagnostics,
                             std::string *error)
    {
        (void)error;
#if !PS2X_IOP_ENABLE_PLUGINS
        if (!searchPaths.empty())
        {
            diagnostics.push_back("dynamic IOP plugins are disabled on this platform");
        }
        return true;
#else
        for (const auto &searchPath : searchPaths)
        {
            std::error_code ec;
            if (!std::filesystem::exists(searchPath, ec) || ec)
            {
                continue;
            }
            if (!std::filesystem::is_directory(searchPath, ec) || ec)
            {
                diagnostics.push_back(formatPluginDiagnostic(searchPath, "search path is not a directory"));
                continue;
            }

            for (std::filesystem::directory_iterator iterator(searchPath, ec), end; !ec && iterator != end; iterator.increment(ec))
            {
                const std::filesystem::directory_entry &entry = *iterator;
                if (!entry.is_regular_file(ec) || ec || !hasPluginExtension(entry.path()))
                {
                    ec.clear();
                    continue;
                }

                std::filesystem::path canonicalPath = std::filesystem::weakly_canonical(entry.path(), ec);
                if (ec)
                {
                    ec.clear();
                    canonicalPath = entry.path().lexically_normal();
                }
                const std::string pathKey = canonicalPath.generic_string();
                if (!m_loadedPaths.insert(pathKey).second)
                {
                    continue;
                }

                auto library = std::make_shared<DynamicLibrary>(canonicalPath);
                std::string openError;
                if (!library->open(openError))
                {
                    diagnostics.push_back(formatPluginDiagnostic(canonicalPath, openError));
                    continue;
                }

                const auto query = reinterpret_cast<ps2x_iop_query_v1_fn>(
                    library->symbol(PS2X_IOP_QUERY_SYMBOL_V1));
                if (!query)
                {
                    diagnostics.push_back(formatPluginDiagnostic(canonicalPath, "missing " PS2X_IOP_QUERY_SYMBOL_V1));
                    continue;
                }

                ps2x_iop_plugin_api_v1 plugin{};
                plugin.struct_size = sizeof(plugin);
                int32_t queryStatus = PS2X_IOP_STATUS_FAILED_V1;
                try
                {
                    queryStatus = query(PS2X_IOP_ABI_VERSION_V1, &plugin);
                }
                catch (...)
                {
                    diagnostics.push_back(formatPluginDiagnostic(canonicalPath,
                                                                 "query entry threw an exception"));
                    continue;
                }
                if (queryStatus != PS2X_IOP_STATUS_OK_V1 ||
                    plugin.abi_version != PS2X_IOP_ABI_VERSION_V1 ||
                    plugin.struct_size < sizeof(plugin))
                {
                    diagnostics.push_back(formatPluginDiagnostic(canonicalPath, "incompatible ABI or invalid descriptor"));
                    continue;
                }
                if (plugin.profile_count > 0 && !plugin.profiles)
                {
                    diagnostics.push_back(formatPluginDiagnostic(canonicalPath, "profile table is null"));
                    continue;
                }
                if (plugin.profile_count > kMaxPluginProfiles)
                {
                    diagnostics.push_back(formatPluginDiagnostic(canonicalPath, "too many profiles"));
                    continue;
                }

                const std::string provider = copyString(plugin.name).empty()
                                                 ? canonicalPath.filename().string()
                                                 : copyString(plugin.name);
                size_t acceptedProfiles = 0;
                for (size_t index = 0; index < plugin.profile_count; ++index)
                {
                    const ps2x_iop_profile_api_v1 &profile = plugin.profiles[index];
                    if (profile.abi_version != PS2X_IOP_ABI_VERSION_V1 ||
                        profile.struct_size < sizeof(profile) ||
                        profile.matcher.struct_size < sizeof(profile.matcher))
                    {
                        diagnostics.push_back(formatPluginDiagnostic(canonicalPath,
                                                                     "ignored invalid profile at index " + std::to_string(index)));
                        continue;
                    }

                    const std::string profileId = copyString(profile.id);
                    const bool validMatcherName = validStringView(profile.matcher.elf_name);
                    const bool matcherPresent = profile.matcher.elf_name.size != 0 ||
                                                profile.matcher.entry_point != 0 ||
                                                profile.matcher.crc32 != 0;
                    if (!validStringView(profile.id) || !validMatcherName ||
                        profileId.empty() || !matcherPresent ||
                        profile.sid_count == 0 || !profile.sids ||
                        !profile.create || !profile.destroy || !profile.reset ||
                        !profile.handle_rpc)
                    {
                        diagnostics.push_back(formatPluginDiagnostic(canonicalPath,
                                                                     "ignored invalid profile at index " + std::to_string(index)));
                        continue;
                    }

                    if (profile.sid_count > kMaxPluginSids)
                    {
                        diagnostics.push_back(formatPluginDiagnostic(canonicalPath,
                                                                     "ignored profile with too many SIDs: " + profileId));
                        continue;
                    }
                    std::vector<uint32_t> sids(profile.sids, profile.sids + profile.sid_count);

                    ProfileDefinition definition;
                    definition.id = profileId;
                    definition.provider = provider;
                    definition.matcher.elfName = copyString(profile.matcher.elf_name);
                    definition.matcher.entryPoint = profile.matcher.entry_point;
                    definition.matcher.crc32 = profile.matcher.crc32;
                    const std::shared_ptr<void> keepAlive = library;
                    definition.factory = [keepAlive, profile, profileId, sids = std::move(sids)](
                                             IopHost &host,
                                             const GameIdentity &identity) mutable
                    {
                        ServiceList services;
                        services.push_back(std::make_unique<PluginService>(host,
                                                                           keepAlive,
                                                                           profile,
                                                                           profileId,
                                                                           sids,
                                                                           identity));
                        return services;
                    };
                    profiles.push_back(std::move(definition));
                    ++acceptedProfiles;
                }

                if (acceptedProfiles == 0)
                {
                    diagnostics.push_back(formatPluginDiagnostic(canonicalPath, "no valid profiles"));
                    continue;
                }
                diagnostics.push_back(formatPluginDiagnostic(canonicalPath,
                                                             "loaded " + std::to_string(acceptedProfiles) + " profile(s)"));
                m_libraries.push_back(std::move(library));
            }

            if (ec)
            {
                diagnostics.push_back(formatPluginDiagnostic(searchPath, ec.message()));
            }
        }
        return true;
#endif
    }
}
