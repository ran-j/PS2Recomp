#ifndef PS2RECOMP_CONFIG_MANAGER_H
#define PS2RECOMP_CONFIG_MANAGER_H

#include "ps2recomp/types.h"
#include <string>

namespace ps2recomp
{
    class RecompilerReporter;

    class ConfigManager
    {
    public:
        explicit ConfigManager(const std::string &configPath);
        ~ConfigManager();

        RecompilerConfig loadConfig() const;
        void saveConfig(const RecompilerConfig &config) const;
        void setReporter(RecompilerReporter *reporter);

    private:
        std::string m_configPath;
        RecompilerReporter *m_reporter = nullptr;
    };

}

#endif // PS2RECOMP_CONFIG_MANAGER_H