#include "ps2recomp/config_manager.h"
#include <toml.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace ps2recomp
{

    ConfigManager::ConfigManager(const std::string &configPath)
        : m_configPath(configPath)
    {
    }

    ConfigManager::~ConfigManager() = default;

    RecompilerConfig ConfigManager::loadConfig()
    {
        RecompilerConfig config;

        try
        {
            auto data = toml::parse(m_configPath);

            config.inputPath = toml::find<std::string>(data, "general", "input");
            config.outputPath = toml::find<std::string>(data, "general", "output");
            config.singleFileOutput = toml::find<bool>(data, "general", "single_file_output");

            config.skipFunctions = toml::find<std::vector<std::string>>(data, "general", "skip");

            if (data.contains("patches") && data.at("patches").is_table())
            {
                const auto &patches = toml::find(data, "patches");

                if (patches.contains("instructions") && patches.at("instructions").is_array())
                {
                    const auto &instPatches = toml::find(patches, "instructions").as_array();
                    for (const auto &patch : instPatches)
                    {
                        if (patch.contains("address") && patch.contains("value"))
                        {
                            uint32_t address = std::stoul(toml::find<std::string>(patch, "address"), nullptr, 0);
                            std::string value = toml::find<std::string>(patch, "value");
                            config.patches[address] = value;
                        }
                    }
                }
            }

            if (data.contains("stub_implementations") && data.at("stub_implementations").is_table())
            {
                const auto &stubImpls = toml::find(data, "stub_implementations");
                for (const auto &item : stubImpls.as_table())
                {
                    const std::string &funcName = item.first;
                    const std::string &implementation = toml::find<std::string>(stubImpls, funcName);
                    config.stubImplementations[funcName] = implementation;
                }
            }
        }
        catch (const std::exception &e)
        {
            std::cerr << "Error parsing configuration file: " << e.what() << std::endl;
            throw;
        }

        return config;
    }

    void ConfigManager::saveConfig(const RecompilerConfig &config)
    {
        toml::value data;

        toml::table general;
        general["input"] = config.inputPath;
        general["output"] = config.outputPath;
        general["single_file_output"] = config.singleFileOutput;
        data["general"] = general;

        toml::array skips;
        for (const auto &skip : config.skipFunctions)
        {
            skips.push_back(skip);
        }
        data["skip"] = skips;

        toml::table patches;
        toml::array instPatches;
        for (const auto &patch : config.patches)
        {
            toml::table p;
            p["address"] = "0x" + std::to_string(patch.first);
            p["value"] = patch.second;
            instPatches.push_back(p);
        }
        patches["instructions"] = instPatches;
        data["patches"] = patches;

        if (!config.stubImplementations.empty())
        {
            toml::table stubImpls;
            for (const auto &impl : config.stubImplementations)
            {
                stubImpls[impl.first] = impl.second;
            }
            data["stub_implementations"] = stubImpls;
        }

        std::ofstream file(m_configPath);
        if (!file)
        {
            throw std::runtime_error("Failed to open file for writing: " + m_configPath);
        }

        file << data;
    }

} // namespace ps2recomp