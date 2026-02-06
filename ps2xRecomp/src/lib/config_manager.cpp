#include "ps2recomp/config_manager.h"
#include <toml.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <sstream>

namespace ps2recomp
{

    ConfigManager::ConfigManager(const std::string &configPath)
        : m_configPath(configPath)
    {
    }

    ConfigManager::~ConfigManager() = default;

    RecompilerConfig ConfigManager::loadConfig() const
    {
        RecompilerConfig config;

        try
        {
            std::cout << "Parsing toml file: " << m_configPath << std::endl;
            auto data = toml::parse(m_configPath);
            const auto &general = toml::find(data, "general");

            config.inputPath = toml::find<std::string>(general, "input");
            config.ghidraMapPath = toml::find_or<std::string>(general, "ghidra_output", "");
            config.outputPath = toml::find<std::string>(general, "output");
            config.singleFileOutput = toml::find_or<bool>(general, "single_file_output", false);

            if (general.contains("stubs") && general.at("stubs").is_array())
            {
                config.stubImplementations = toml::find<std::vector<std::string>>(general, "stubs");
            }
            else if (data.contains("stubs") && data.at("stubs").is_array())
            {
                config.stubImplementations = toml::find<std::vector<std::string>>(data, "stubs");
            }

            if (general.contains("skip") && general.at("skip").is_array())
            {
                config.skipFunctions = toml::find<std::vector<std::string>>(general, "skip");
            }
            else if (data.contains("skip") && data.at("skip").is_array())
            {
                config.skipFunctions = toml::find<std::vector<std::string>>(data, "skip");
            }

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
                            uint32_t address = 0;
                            const auto &addressValue = patch.at("address");
                            if (addressValue.is_string())
                            {
                                address = std::stoul(toml::find<std::string>(patch, "address"), nullptr, 0);
                            }
                            else if (addressValue.is_integer())
                            {
                                address = static_cast<uint32_t>(toml::find<int64_t>(patch, "address"));
                            }
                            else
                            {
                                continue;
                            }

                            const auto &valueField = patch.at("value");
                            if (valueField.is_string())
                            {
                                config.patches[address] = toml::find<std::string>(patch, "value");
                            }
                            else if (valueField.is_integer())
                            {
                                std::ostringstream valueStream;
                                valueStream << "0x" << std::hex
                                            << static_cast<uint32_t>(toml::find<int64_t>(patch, "value"));
                                config.patches[address] = valueStream.str();
                            }
                        }
                    }
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

    void ConfigManager::saveConfig(const RecompilerConfig &config) const
    {
        toml::value data;

        toml::table general;
        general["input"] = config.inputPath;
        general["ghidra_output"] = config.ghidraMapPath;
        general["output"] = config.outputPath;
        general["single_file_output"] = config.singleFileOutput;
        general["skip"] = config.skipFunctions;
        general["stubs"] = config.stubImplementations;
        data["general"] = general;

        toml::table patches;
        toml::array instPatches;
        for (const auto &[addr, value] : config.patches)
        {
            std::ostringstream addrStream;
            addrStream << "0x" << std::hex << addr;

            toml::table p;
            p["address"] = addrStream.str();
            p["value"] = value;
            instPatches.push_back(p);
        }
        patches["instructions"] = instPatches;
        data["patches"] = patches;

        std::ofstream file(m_configPath);
        if (!file)
        {
            throw std::runtime_error("Failed to open file for writing: " + m_configPath);
        }

        file << data;
    }

} // namespace ps2recomp
