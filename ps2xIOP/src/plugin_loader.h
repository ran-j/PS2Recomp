#pragma once

#include "iop_service.h"

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace ps2x::iop::detail
{
    class PluginCatalog
    {
    public:
        explicit PluginCatalog(IopHost &host);
        ~PluginCatalog();

        PluginCatalog(const PluginCatalog &) = delete;
        PluginCatalog &operator=(const PluginCatalog &) = delete;

        bool load(const std::vector<std::filesystem::path> &searchPaths,
                  std::vector<ProfileDefinition> &profiles,
                  std::vector<std::string> &diagnostics,
                  std::string *error);

    private:
        class DynamicLibrary;

        IopHost &m_host;
        std::vector<std::shared_ptr<DynamicLibrary>> m_libraries;
        std::unordered_set<std::string> m_loadedPaths;
    };
}
