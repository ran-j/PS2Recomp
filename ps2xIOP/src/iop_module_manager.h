#pragma once

#include "ps2x/iop/iop_types.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ps2x::iop::detail
{
    class IopModuleManager
    {
    public:
        IopModuleManager();

        void reset();
        void setServiceModuleKeys(std::vector<std::string> keys);

        [[nodiscard]] ModuleLoadResult loadHle(std::string_view path);
        void observePhysicalLoad(int32_t moduleId, std::string_view path);
        [[nodiscard]] bool stopHle(int32_t moduleId, int32_t *result);
        void observePhysicalStop(int32_t moduleId);

        [[nodiscard]] bool isLoaded(std::span<const std::string_view> aliases) const;
        [[nodiscard]] bool recognizes(std::string_view path) const;

    private:
        struct Record
        {
            std::string key;
            uint32_t references = 0u;
            bool physical = false;
        };

        void addLoadedKey(std::string_view key);
        void removeLoadedKey(std::string_view key);

        std::unordered_set<std::string> m_builtinKeys;
        std::unordered_set<std::string> m_serviceKeys;
        std::unordered_map<int32_t, Record> m_records;
        std::unordered_map<std::string, int32_t> m_hleIdsByKey;
        std::unordered_map<std::string, uint32_t> m_loadedKeyReferences;
        int32_t m_nextHleId = 0x40000000;
    };
}
