#include "ps2recomp/config_manager.h"
#include <toml.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <sstream>
#include <filesystem>

namespace fs = std::filesystem;

namespace ps2recomp
{
    // Simple JSON parser for functions file
    static std::vector<ExternalFunction> loadFunctionsFromJson(const std::string &jsonPath, const std::string &configDir)
    {
        std::vector<ExternalFunction> functions;

        // Resolve path relative to config file
        fs::path fullPath = jsonPath;
        if (!fullPath.is_absolute()) {
            fullPath = fs::path(configDir) / jsonPath;
        }

        std::ifstream file(fullPath);
        if (!file.is_open()) {
            std::cerr << "Warning: Could not open functions file: " << fullPath << std::endl;
            return functions;
        }

        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string json = buffer.str();

        // Simple JSON array parser for our specific format
        // Format: [{"name": "func", "address": 123, "size": 456}, ...]
        size_t pos = 0;
        while ((pos = json.find("\"name\"", pos)) != std::string::npos) {
            ExternalFunction func;

            // Find name value
            size_t nameStart = json.find("\"", pos + 6) + 1;
            size_t nameEnd = json.find("\"", nameStart);
            func.name = json.substr(nameStart, nameEnd - nameStart);

            // Find address value
            size_t addrPos = json.find("\"address\"", nameEnd);
            size_t addrStart = json.find(":", addrPos) + 1;
            while (json[addrStart] == ' ') addrStart++;
            size_t addrEnd = addrStart;
            while (addrEnd < json.size() && (isdigit(json[addrEnd]) || json[addrEnd] == 'x' ||
                   (json[addrEnd] >= 'a' && json[addrEnd] <= 'f') ||
                   (json[addrEnd] >= 'A' && json[addrEnd] <= 'F'))) addrEnd++;
            func.address = std::stoul(json.substr(addrStart, addrEnd - addrStart), nullptr, 0);

            // Find size value
            size_t sizePos = json.find("\"size\"", addrEnd);
            size_t sizeStart = json.find(":", sizePos) + 1;
            while (json[sizeStart] == ' ') sizeStart++;
            size_t sizeEnd = sizeStart;
            while (sizeEnd < json.size() && (isdigit(json[sizeEnd]) || json[sizeEnd] == 'x' ||
                   (json[sizeEnd] >= 'a' && json[sizeEnd] <= 'f') ||
                   (json[sizeEnd] >= 'A' && json[sizeEnd] <= 'F'))) sizeEnd++;
            func.size = std::stoul(json.substr(sizeStart, sizeEnd - sizeStart), nullptr, 0);

            functions.push_back(func);
            pos = sizeEnd;
        }

        std::cout << "Loaded " << functions.size() << " external functions from " << fullPath << std::endl;
        return functions;
    }

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
            std::cout << "Parsing toml file: " << m_configPath << std::endl;
            auto data = toml::parse(m_configPath);

            config.inputPath = toml::find<std::string>(data, "general", "input");
            config.outputPath = toml::find<std::string>(data, "general", "output");
            config.singleFileOutput = toml::find<bool>(data, "general", "single_file_output");
            config.stubImplementations = toml::find<std::vector<std::string>>(data, "general", "stubs");

            config.skipFunctions = toml::find<std::vector<std::string>>(data, "general", "skip");

            // Load external functions file if specified
            if (data.at("general").contains("functions_file")) {
                config.functionsFile = toml::find<std::string>(data, "general", "functions_file");
                // Get directory of config file
                fs::path configPath(m_configPath);
                std::string configDir = configPath.parent_path().string();
                config.externalFunctions = loadFunctionsFromJson(config.functionsFile, configDir);
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
                            uint32_t address = std::stoul(toml::find<std::string>(patch, "address"), nullptr, 0);
                            std::string value = toml::find<std::string>(patch, "value");
                            config.patches[address] = value;
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
            data["stubs"] = config.stubImplementations;
        }

        std::ofstream file(m_configPath);
        if (!file)
        {
            throw std::runtime_error("Failed to open file for writing: " + m_configPath);
        }

        file << data;
    }

} // namespace ps2recomp