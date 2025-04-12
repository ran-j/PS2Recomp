#ifndef PS2RECOMP_CONFIG_MANAGER_H
#define PS2RECOMP_CONFIG_MANAGER_H

#include "ps2recomp/types.h"
#include <string>

namespace ps2recomp
{

    class ConfigManager
    {
    public:
        ConfigManager(const std::string &configPath);
        ~ConfigManager();

        RecompilerConfig loadConfig();
        void saveConfig(const RecompilerConfig &config);

    private:
        std::string m_configPath;
    };

}

#endif // PS2RECOMP_CONFIG_MANAGER_H