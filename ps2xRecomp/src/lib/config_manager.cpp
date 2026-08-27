#include "ps2recomp/config_manager.h"
#include "ps2recomp/recompiler_reporter.h"
#include <toml.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <sstream>
#include <algorithm>
#include <limits>
#include <thread>

namespace ps2recomp
{

    ConfigManager::ConfigManager(const std::string &configPath)
        : m_configPath(configPath)
    {
    }

    ConfigManager::~ConfigManager() = default;

    void ConfigManager::setReporter(RecompilerReporter *reporter)
    {
        m_reporter = reporter;
    }

    RecompilerConfig ConfigManager::loadConfig() const
    {
        RecompilerConfig config;

        try
        {
            if (m_reporter)
            {
                m_reporter->info("config", "Parsing toml file: " + m_configPath);
            }
            auto data = toml::parse(m_configPath);
            const auto &general = toml::find(data, "general");

            config.inputPath = toml::find<std::string>(general, "input");
            config.ghidraMapPath = toml::find_or<std::string>(general, "ghidra_output", "");
            config.outputPath = toml::find<std::string>(general, "output");
            config.singleFileOutput = toml::find_or<bool>(general, "single_file_output", false);
            config.lowMemoryMode = toml::find_or<bool>(general, "low_memory_mode", config.lowMemoryMode);
            const int64_t configuredOutputWorkers = toml::find_or<int64_t>(
                general,
                "output_worker_threads",
                toml::find_or<int64_t>(general, "output_worker_thread", config.outputWorkerThreads));
            const int64_t clampedOutputWorkers = std::clamp<int64_t>(
                configuredOutputWorkers,
                0,
                std::thread::hardware_concurrency() * 2);
            if (configuredOutputWorkers != clampedOutputWorkers)
            {
                if (m_reporter)
                {
                    std::ostringstream msg;
                    msg << "output_worker_threads value " << configuredOutputWorkers
                        << " is out of range; clamped to " << clampedOutputWorkers << ".";
                    m_reporter->warning("config", msg.str());
                }
            }
            config.outputWorkerThreads = static_cast<uint32_t>(clampedOutputWorkers);
            config.patchSyscalls = toml::find_or<bool>(general, "patch_syscalls", config.patchSyscalls);
            config.patchCop0 = toml::find_or<bool>(general, "patch_cop0", config.patchCop0);
            config.patchCache = toml::find_or<bool>(general, "patch_cache", config.patchCache);
            config.resumeEntryPointsPath = toml::find_or<std::string>(general, "resume_entry_points_file", "");

            if (general.contains("entry_point_pointer_ranges") &&
                general.at("entry_point_pointer_ranges").is_array())
            {
                const auto parseAddress = [](const toml::value &node, const std::string &field)
                {
                    const auto &value = node.at(field);
                    if (value.is_string())
                    {
                        const std::string text = toml::find<std::string>(node, field);
                        size_t consumed = 0;
                        const unsigned long parsed = std::stoul(text, &consumed, 0);
                        if (consumed != text.size() || parsed > std::numeric_limits<uint32_t>::max())
                        {
                            throw std::out_of_range(field + " is not a 32-bit address");
                        }
                        return static_cast<uint32_t>(parsed);
                    }
                    if (value.is_integer())
                    {
                        const int64_t parsed = toml::find<int64_t>(node, field);
                        if (parsed < 0 || static_cast<uint64_t>(parsed) > std::numeric_limits<uint32_t>::max())
                        {
                            throw std::out_of_range(field + " is not a 32-bit address");
                        }
                        return static_cast<uint32_t>(parsed);
                    }
                    throw std::invalid_argument(field + " must be an integer or address string");
                };

                for (const auto &rangeNode : general.at("entry_point_pointer_ranges").as_array())
                {
                    if (!rangeNode.is_table() || !rangeNode.contains("source_start") ||
                        !rangeNode.contains("source_end"))
                    {
                        throw std::invalid_argument(
                            "entry_point_pointer_ranges entries require source_start and source_end");
                    }

                    EntryPointPointerRange range;
                    range.sourceStart = parseAddress(rangeNode, "source_start");
                    range.sourceEnd = parseAddress(rangeNode, "source_end");
                    if (rangeNode.contains("target_start"))
                    {
                        range.targetStart = parseAddress(rangeNode, "target_start");
                    }
                    if (rangeNode.contains("target_end"))
                    {
                        range.targetEnd = parseAddress(rangeNode, "target_end");
                    }
                    range.windowWords = static_cast<uint32_t>(std::clamp<int64_t>(
                        toml::find_or<int64_t>(rangeNode, "window_words", range.windowWords), 1, 4096));
                    range.minimumCodePointers = static_cast<uint32_t>(std::clamp<int64_t>(
                        toml::find_or<int64_t>(
                            rangeNode, "minimum_code_pointers", range.minimumCodePointers),
                        1,
                        range.windowWords));

                    if (range.sourceStart >= range.sourceEnd || range.targetStart >= range.targetEnd ||
                        (range.sourceStart & 3u) != 0u || (range.sourceEnd & 3u) != 0u)
                    {
                        throw std::invalid_argument(
                            "entry_point_pointer_ranges addresses must form aligned, non-empty ranges");
                    }
                    config.entryPointPointerRanges.push_back(range);
                }
            }

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

            if (data.contains("mmio") && data.at("mmio").is_table())
            {
                const auto &mmioTable = toml::find(data, "mmio").as_table();
                for (const auto &[key, value] : mmioTable)
                {
                    uint32_t instAddr = std::stoul(key, nullptr, 0);
                    uint32_t mmioAddr = 0;
                    if (value.is_string())
                    {
                        mmioAddr = std::stoul(value.as_string(), nullptr, 0);
                    }
                    else if (value.is_integer())
                    {
                        mmioAddr = static_cast<uint32_t>(value.as_integer());
                    }
                    config.mmioByInstructionAddress[instAddr] = mmioAddr;
                }
            }

            if (data.contains("jump_tables") && data.at("jump_tables").is_table())
            {
                const auto &jumpTablesNode = data.at("jump_tables");
                if (jumpTablesNode.contains("table") && jumpTablesNode.at("table").is_array())
                {
                    const auto &tables = jumpTablesNode.at("table").as_array();
                    for (const auto &tableNode : tables)
                    {
                        if (!tableNode.is_table())
                        {
                            continue;
                        }

                        JumpTable table{};

                        if (tableNode.contains("address"))
                        {
                            const auto &addressValue = tableNode.at("address");
                            if (addressValue.is_string())
                            {
                                table.address = std::stoul(addressValue.as_string(), nullptr, 0);
                            }
                            else if (addressValue.is_integer())
                            {
                                table.address = static_cast<uint32_t>(addressValue.as_integer());
                            }
                        }

                        if (tableNode.contains("base_register"))
                        {
                            const auto &baseRegisterValue = tableNode.at("base_register");
                            if (baseRegisterValue.is_string())
                            {
                                table.baseRegister = std::stoul(baseRegisterValue.as_string(), nullptr, 0);
                            }
                            else if (baseRegisterValue.is_integer())
                            {
                                table.baseRegister = static_cast<uint32_t>(baseRegisterValue.as_integer());
                            }
                        }

                        if (table.address == 0u || !tableNode.contains("entries") || !tableNode.at("entries").is_array())
                        {
                            continue;
                        }

                        const auto &entries = tableNode.at("entries").as_array();
                        uint32_t fallbackIndex = 0u;
                        for (const auto &entryNode : entries)
                        {
                            if (!entryNode.is_table())
                            {
                                ++fallbackIndex;
                                continue;
                            }

                            JumpTableEntry entry{};
                            entry.index = fallbackIndex;

                            if (entryNode.contains("index"))
                            {
                                const auto &indexValue = entryNode.at("index");
                                if (indexValue.is_string())
                                {
                                    entry.index = std::stoul(indexValue.as_string(), nullptr, 0);
                                }
                                else if (indexValue.is_integer())
                                {
                                    entry.index = static_cast<uint32_t>(indexValue.as_integer());
                                }
                            }

                            bool hasTarget = false;
                            if (entryNode.contains("target"))
                            {
                                const auto &targetValue = entryNode.at("target");
                                if (targetValue.is_string())
                                {
                                    entry.target = std::stoul(targetValue.as_string(), nullptr, 0);
                                    hasTarget = true;
                                }
                                else if (targetValue.is_integer())
                                {
                                    entry.target = static_cast<uint32_t>(targetValue.as_integer());
                                    hasTarget = true;
                                }
                            }

                            if (hasTarget)
                            {
                                table.entries.push_back(entry);
                            }
                            ++fallbackIndex;
                        }

                        if (!table.entries.empty())
                        {
                            config.jumpTables.push_back(std::move(table));
                        }
                    }
                }
            }
        }
        catch (const std::exception &e)
        {
            if (m_reporter)
            {
                m_reporter->error("config", std::string("Error parsing configuration file: ") + e.what());
            }
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
        general["low_memory_mode"] = config.lowMemoryMode;
        general["output_worker_threads"] = static_cast<int64_t>(config.outputWorkerThreads);
        general["patch_syscalls"] = config.patchSyscalls;
        general["patch_cop0"] = config.patchCop0;
        general["patch_cache"] = config.patchCache;
        general["resume_entry_points_file"] = config.resumeEntryPointsPath;
        if (!config.entryPointPointerRanges.empty())
        {
            toml::array pointerRanges;
            for (const auto &range : config.entryPointPointerRanges)
            {
                const auto addressString = [](uint32_t address)
                {
                    std::ostringstream stream;
                    stream << "0x" << std::hex << address;
                    return stream.str();
                };

                toml::table rangeNode;
                rangeNode["source_start"] = addressString(range.sourceStart);
                rangeNode["source_end"] = addressString(range.sourceEnd);
                rangeNode["target_start"] = addressString(range.targetStart);
                rangeNode["target_end"] = addressString(range.targetEnd);
                rangeNode["window_words"] = static_cast<int64_t>(range.windowWords);
                rangeNode["minimum_code_pointers"] = static_cast<int64_t>(range.minimumCodePointers);
                pointerRanges.push_back(rangeNode);
            }
            general["entry_point_pointer_ranges"] = pointerRanges;
        }
        general["skip"] = config.skipFunctions;
        general["stubs"] = config.stubImplementations;
        data["general"] = general;

        if (!config.mmioByInstructionAddress.empty())
        {
            toml::table mmioTable;
            for (const auto &[instAddr, mmioAddr] : config.mmioByInstructionAddress)
            {
                std::ostringstream keyStream;
                keyStream << "0x" << std::hex << instAddr;
                std::ostringstream valStream;
                valStream << "0x" << std::hex << mmioAddr;
                mmioTable[keyStream.str()] = valStream.str();
            }
            data["mmio"] = mmioTable;
        }

        if (!config.jumpTables.empty())
        {
            toml::table jumpTables;
            toml::array tableArray;
            for (const auto &table : config.jumpTables)
            {
                toml::table tableNode;
                std::ostringstream addressStream;
                addressStream << "0x" << std::hex << table.address;
                tableNode["address"] = addressStream.str();
                tableNode["base_register"] = static_cast<int64_t>(table.baseRegister);

                toml::array entries;
                for (const auto &entry : table.entries)
                {
                    toml::table entryNode;
                    entryNode["index"] = static_cast<int64_t>(entry.index);
                    std::ostringstream targetStream;
                    targetStream << "0x" << std::hex << entry.target;
                    entryNode["target"] = targetStream.str();
                    entries.push_back(entryNode);
                }
                tableNode["entries"] = entries;
                tableArray.push_back(tableNode);
            }
            jumpTables["table"] = tableArray;
            data["jump_tables"] = jumpTables;
        }

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
