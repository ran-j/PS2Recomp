#include "ps2x/iop/iop_subsystem.h"

#include "iop_service.h"
#include "iop_module_manager.h"
#include "emulator/iop_emulator.h"
#include "plugin_loader.h"
#include "ps2x/iop/ps2_path.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace ps2x::iop
{
    namespace
    {
        bool equalsIgnoreCaseAscii(std::string_view lhs, std::string_view rhs)
        {
            if (lhs.size() != rhs.size())
            {
                return false;
            }

            for (size_t i = 0; i < lhs.size(); ++i)
            {
                const auto left = static_cast<unsigned char>(lhs[i]);
                const auto right = static_cast<unsigned char>(rhs[i]);
                if (std::tolower(left) != std::tolower(right))
                {
                    return false;
                }
            }
            return true;
        }

        int matchSpecificity(const GameMatcher &matcher, const GameIdentity &identity)
        {
            int specificity = 0;
            if (!matcher.elfName.empty())
            {
                if (!equalsIgnoreCaseAscii(matcher.elfName, identity.elfName))
                {
                    return -1;
                }
                ++specificity;
            }
            if (matcher.entryPoint != 0)
            {
                if (matcher.entryPoint != identity.entryPoint)
                {
                    return -1;
                }
                ++specificity;
            }
            if (matcher.crc32 != 0)
            {
                if (matcher.crc32 != identity.crc32)
                {
                    return -1;
                }
                ++specificity;
            }
            return specificity;
        }
    }

    class IopSubsystem::Impl
    {
    public:
        explicit Impl(IopHost &hostRef)
            : host(hostRef),
              pluginCatalog(hostRef),
              coreServices(detail::createCoreServices(hostRef)),
              profiles(detail::createBuiltinProfiles()),
              emulator(hostRef)
        {
            refreshServiceModuleKeys();
            rebuildRoutes();
        }

        bool serviceActive(const detail::IopService &service) const
        {
            return moduleManager.isLoaded(service.moduleAliases());
        }

        void refreshServiceModuleKeys()
        {
            std::vector<std::string> keys;
            auto collect = [&](const detail::ServiceList &services)
            {
                for (const auto &service : services)
                {
                    if (!service)
                        continue;
                    for (std::string_view alias : service->moduleAliases())
                        keys.emplace_back(alias);
                }
            };
            collect(coreServices);
            collect(profileServices);
            moduleManager.setServiceModuleKeys(std::move(keys));
        }

        void rebuildRoutes()
        {
            routes.clear();
            auto addLayer = [&](detail::ServiceList &services, bool profileSpecific) -> bool
            {
                std::unordered_map<uint32_t, detail::IopService *> layer;
                for (const auto &service : services)
                {
                    if (!service || !serviceActive(*service))
                    {
                        continue;
                    }
                    for (const uint32_t sid : service->sids())
                    {
                        if (!layer.emplace(sid, service.get()).second)
                        {
                            std::ostringstream out;
                            out << "duplicate IOP SID 0x" << std::hex << sid << " in " << (profileSpecific ? "profile" : "core") << " layer";
                            lastError = out.str();
                            return false;
                        }
                    }
                }
                for (const auto &[sid, service] : layer)
                {
                    routes[sid] = service;
                }
                return true;
            };

            routesValid = addLayer(coreServices, false) && addLayer(profileServices, true);
        }

        void recordLoadOutcome(std::string_view path, bool hle)
        {
            constexpr size_t maxOutcomes = 32u;
            if (loadOutcomes.size() >= maxOutcomes || !loggedLoadPaths.emplace(path).second)
                return;
            std::string message = hle ? "[IOP:HLE] fallback module='" : "[IOP:load-failed] module='";
            message.append(path);
            message += hle ? "' physical IRX unavailable; using registered HLE provider"
                           : "' no HLE provider accepted the module; physical IRX was not loaded";
            loadOutcomes.push_back(message);
            host.log(hle ? LogLevel::Info : LogLevel::Warning, message);
        }

        IopHost &host;
        detail::PluginCatalog pluginCatalog;
        detail::ServiceList coreServices;
        detail::ServiceList profileServices;
        std::vector<detail::ProfileDefinition> profiles;
        std::unordered_map<uint32_t, detail::IopService *> routes;
        std::vector<std::filesystem::path> pluginSearchPaths;
        std::vector<std::string> diagnostics;
        std::vector<std::string> loadOutcomes;
        std::unordered_set<std::string> loggedLoadPaths;
        std::string activeProfile;
        std::string activeProvider;
        std::string lastError;
        bool routesValid = true;
        detail::IopModuleManager moduleManager;
        detail::IopEmulator emulator;
    };

    IopSubsystem::IopSubsystem(IopHost &host)
        : m_impl(std::make_unique<Impl>(host))
    {
    }

    IopSubsystem::~IopSubsystem() = default;
    IopSubsystem::IopSubsystem(IopSubsystem &&) noexcept = default;
    IopSubsystem &IopSubsystem::operator=(IopSubsystem &&) noexcept = default;

    void IopSubsystem::setPluginSearchPaths(std::vector<std::filesystem::path> paths)
    {
        m_impl->pluginSearchPaths = std::move(paths);
    }

    bool IopSubsystem::loadPlugins(std::string *error)
    {
        return m_impl->pluginCatalog.load(m_impl->pluginSearchPaths, m_impl->profiles, m_impl->diagnostics, error);
    }

    bool IopSubsystem::configure(const GameIdentity &identity, std::string *error)
    {
        if (error)
            error->clear();
        m_impl->profileServices.clear();
        m_impl->activeProfile.clear();
        m_impl->activeProvider.clear();
        m_impl->lastError.clear();

        const detail::ProfileDefinition *selected = nullptr;
        const detail::ProfileDefinition *selectedTie = nullptr;
        int selectedSpecificity = -1;
        for (const auto &profile : m_impl->profiles)
        {
            const int specificity = matchSpecificity(profile.matcher, identity);
            if (specificity < 0)
            {
                continue;
            }
            if (specificity > selectedSpecificity)
            {
                selected = &profile;
                selectedTie = nullptr;
                selectedSpecificity = specificity;
                continue;
            }
            if (specificity == selectedSpecificity && selected)
            {
                selectedTie = &profile;
            }
        }

        if (selected && selectedTie)
        {
            m_impl->lastError = "ambiguous IOP profiles '" + selected->provider + ":" +
                                selected->id + "' and '" + selectedTie->provider + ":" +
                                selectedTie->id + "'";
            if (error)
            {
                *error = m_impl->lastError;
            }
            m_impl->refreshServiceModuleKeys();
            m_impl->rebuildRoutes();
            return false;
        }

        if (selected)
        {
            try
            {
                m_impl->profileServices = selected->factory(m_impl->host, identity);
                m_impl->activeProfile = selected->id;
                m_impl->activeProvider = selected->provider;
            }
            catch (const std::exception &exception)
            {
                m_impl->lastError = "failed to create IOP profile '" + selected->id + "': " + exception.what();
                if (error)
                {
                    *error = m_impl->lastError;
                }
                m_impl->refreshServiceModuleKeys();
                m_impl->rebuildRoutes();
                return false;
            }
            catch (...)
            {
                m_impl->lastError = "failed to create IOP profile '" + selected->id + "': unknown plugin exception";
                if (error)
                {
                    *error = m_impl->lastError;
                }
                m_impl->refreshServiceModuleKeys();
                m_impl->rebuildRoutes();
                return false;
            }
        }

        m_impl->refreshServiceModuleKeys();
        m_impl->rebuildRoutes();
        if (!m_impl->routesValid)
        {
            const std::string routeError = m_impl->lastError;
            m_impl->profileServices.clear();
            m_impl->activeProfile.clear();
            m_impl->activeProvider.clear();
            m_impl->refreshServiceModuleKeys();
            m_impl->rebuildRoutes();
            m_impl->lastError = routeError;
            if (error)
            {
                *error = m_impl->lastError;
            }
            return false;
        }

        reset();
        return true;
    }

    void IopSubsystem::reset()
    {
        m_impl->moduleManager.reset();
        m_impl->loadOutcomes.clear();
        m_impl->loggedLoadPaths.clear();
        for (auto &service : m_impl->coreServices)
        {
            if (service)
            {
                service->reset();
            }
        }
        for (auto &service : m_impl->profileServices)
        {
            if (service)
            {
                service->reset();
            }
        }
        m_impl->emulator.reset();
        m_impl->refreshServiceModuleKeys();
        m_impl->rebuildRoutes();
    }

    ModuleLoadResult IopSubsystem::loadModule(std::string_view path, const void *arguments, uint32_t argumentSize)
    {
        const ParsedPs2Path parsed = parsePs2Path(path);
        if (!parsed)
            return {true, -1, -1};

        if (parsed.device != Ps2PathDevice::Rom0)
        {
            ModuleLoadResult physical = m_impl->emulator.loadModule(path, arguments, argumentSize);
            if (physical.moduleId > 0)
            {
                m_impl->moduleManager.observePhysicalLoad(physical.moduleId, path);
                m_impl->rebuildRoutes();
                return physical;
            }
        }

        ModuleLoadResult hle = m_impl->moduleManager.loadHle(path);
        if (hle.moduleId > 0)
        {
            m_impl->rebuildRoutes();
            if (parsed.device != Ps2PathDevice::Rom0)
                m_impl->recordLoadOutcome(path, true);
        }
        else
        {
            m_impl->recordLoadOutcome(path, false);
        }
        return hle;
    }

    ModuleLoadResult IopSubsystem::loadModuleBuffer(uint32_t guestAddress, const void *arguments, uint32_t argumentSize)
    {
        return m_impl->emulator.loadModuleBuffer(guestAddress, arguments, argumentSize);
    }

    bool IopSubsystem::stopModule(int32_t moduleId, int32_t *result)
    {
        if (m_impl->moduleManager.stopHle(moduleId, result))
        {
            m_impl->rebuildRoutes();
            return true;
        }
        if (!m_impl->emulator.stopModule(moduleId, result))
            return false;
        m_impl->moduleManager.observePhysicalStop(moduleId);
        m_impl->rebuildRoutes();
        return true;
    }

    void IopSubsystem::runEeCycles(uint64_t eeCycles) noexcept
    {
        m_impl->emulator.runEeCycles(eeCycles);
    }

    RpcAbi IopSubsystem::selectRpcAbi(const RpcAbiRequest &request) const
    {
        for (const auto &service : m_impl->profileServices)
        {
            if (service && m_impl->serviceActive(*service))
            {
                const RpcAbi selected = service->selectRpcAbi(request);
                if (selected != RpcAbi::RuntimeDefault)
                {
                    return selected;
                }
            }
        }
        for (const auto &service : m_impl->coreServices)
        {
            if (service && m_impl->serviceActive(*service))
            {
                const RpcAbi selected = service->selectRpcAbi(request);
                if (selected != RpcAbi::RuntimeDefault)
                {
                    return selected;
                }
            }
        }
        return RpcAbi::RuntimeDefault;
    }

    bool IopSubsystem::canBindRpc(uint32_t sid) const noexcept
    {
        if (m_impl->routes.find(sid) != m_impl->routes.end())
        {
            return true;
        }
        return m_impl->emulator.hasRpcServer(sid);
    }

    RpcResult IopSubsystem::handleRpc(const RpcRequest &request)
    {
        const auto route = m_impl->routes.find(request.sid);
        detail::IopService *hle = route != m_impl->routes.end() ? route->second : nullptr;

        // A profile can deliberately replace a physical endpoint when running
        // that IRX is outside the selected compatibility scope (for example,
        // disabling a game's audio driver while keeping the rest of its IOP
        // modules physical).
        if (hle && hle->overridesPhysicalRpcServer())
        {
            RpcResult overridden = hle->handleRpc(request);
            if (overridden.handled)
            {
                return overridden;
            }
        }

        // Otherwise physical servers are authoritative and HLE remains a
        // compatibility fallback for endpoints no loaded IRX provides.
        RpcResult emulated = m_impl->emulator.handleRpc(request);
        if (emulated.handled || !hle || hle->overridesPhysicalRpcServer())
        {
            return emulated;
        }
        return hle->handleRpc(request);
    }

    void IopSubsystem::onSifTransfer(const SifTransfer &transfer)
    {
        for (auto &service : m_impl->coreServices)
        {
            if (service && m_impl->serviceActive(*service))
            {
                service->onSifTransfer(transfer);
            }
        }
        for (auto &service : m_impl->profileServices)
        {
            if (service && m_impl->serviceActive(*service))
            {
                service->onSifTransfer(transfer);
            }
        }
        m_impl->emulator.onSifTransfer(transfer);
    }

    uint32_t IopSubsystem::allocateMemory(uint32_t size, uint32_t alignment)
    {
        return m_impl->emulator.allocateMemory(size, alignment);
    }

    bool IopSubsystem::freeMemory(uint32_t address)
    {
        return m_impl->emulator.freeMemory(address);
    }

    bool IopSubsystem::readMemory(uint32_t address, void *destination, size_t size) const
    {
        return m_impl->emulator.readMemory(address, destination, size);
    }

    bool IopSubsystem::writeMemory(uint32_t address, const void *source, size_t size)
    {
        return m_impl->emulator.writeMemory(address, source, size);
    }

    bool IopSubsystem::zeroMemory(uint32_t address, size_t size)
    {
        return m_impl->emulator.zeroMemory(address, size);
    }

    bool IopSubsystem::isMemoryRange(uint32_t address, size_t size) const
    {
        return m_impl->emulator.isMemoryRange(address, size);
    }

    DebugSnapshot IopSubsystem::debugSnapshot() const
    {
        DebugSnapshot snapshot;
        snapshot.emulatorCycles = m_impl->emulator.cycles();
        snapshot.emulatorInstructions = m_impl->emulator.instructions();
        snapshot.emulatorLoadedModules = m_impl->emulator.loadedModuleCount();
        snapshot.emulatorThreads = m_impl->emulator.threadCount();
        snapshot.emulatorRpcServers = m_impl->emulator.rpcServerCount();
        snapshot.activeProfile = m_impl->activeProfile;
        snapshot.activeProvider = m_impl->activeProvider;
        snapshot.diagnostics = m_impl->diagnostics;
        snapshot.diagnostics.insert(snapshot.diagnostics.end(), m_impl->loadOutcomes.begin(), m_impl->loadOutcomes.end());
        if (!m_impl->lastError.empty())
        {
            snapshot.diagnostics.push_back(m_impl->lastError);
        }

        auto append = [&](const detail::ServiceList &services, bool profileSpecific)
        {
            for (const auto &service : services)
            {
                if (!service)
                {
                    continue;
                }
                DebugService row;
                row.name = service->name();
                row.sids.assign(service->sids().begin(), service->sids().end());
                row.profileSpecific = profileSpecific;
                row.active = m_impl->serviceActive(*service);
                service->appendDebugMetrics(row.metrics);
                snapshot.services.push_back(std::move(row));
            }
        };
        append(m_impl->coreServices, false);
        append(m_impl->profileServices, true);
        return snapshot;
    }
}
