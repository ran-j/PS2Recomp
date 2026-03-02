#pragma once

#include <filesystem>
#include <string>
#include <vector>
#include <map>
#include <future>
#include <atomic>
#include <mutex>
#include <iostream>
#include <sstream>
#include <fstream>
#include <memory>

#include "ps2recomp/elf_analyzer.h"
#include "ps2recomp/config_manager.h"
#include "ps2recomp/ps2_recompiler.h"
#include "ps2recomp/types.h"

#include "../../ps2xTest/include/MiniTest.h"
extern void register_code_generator_tests();
extern void register_r5900_decoder_tests();

enum class OverrideStatus {
    Default, Stub, Skip, ForceRecompile
};

enum class ThemeMode {
    Dark, Light, Custom
};

struct AppSettings {
    ThemeMode theme = ThemeMode::Dark;
    float fontSize = 15.0f;
    float uiScale = 1.0f;
    std::string selectedFont = "Font_1.ttf";
    int windowWidth = 1280;
    int windowHeight = 720;
    bool maximized = true;

    // Custom theme colors
    float customBgBase[4] = {0.08f, 0.08f, 0.08f, 1.00f};
    float customAccent[4] = {0.00f, 0.48f, 0.80f, 1.00f};
};

class StreamRedirector : public std::stringbuf {
public:
    StreamRedirector(std::vector<std::string>& target, std::mutex& mtx, std::atomic<size_t>& ver)
        : logs(target), mutex(mtx), version(ver) {}
    int sync() override {
        std::lock_guard<std::mutex> lock(mutex);
        std::string line = this->str();
        if (!line.empty() && line.back() == '\n') line.pop_back();
        if (!line.empty()) {
            logs.push_back(line);
            version.fetch_add(1);
        }
        this->str("");
        return 0;
    }
private:
    std::vector<std::string>& logs;
    std::mutex& mutex;
    std::atomic<size_t>& version;
};

struct UIState {
    std::string elfPath;
    std::string outputPath = "output";
    std::string customOutputPath;
    std::string ghidraCSVPath;
    std::string ghidraCSVContent;
    std::string configTomlContent;
    std::vector<uint8_t> rawElfData;
    std::map<std::string, OverrideStatus> funcOverrides;
    std::shared_ptr<ps2recomp::ElfAnalyzer> analyzer;
    bool isAnalysisComplete = false;

    // NEW: Support for multiple imported CSV files
    std::vector<std::string> importedCSVFiles;
    int selectedCSVIndex = -1;
};

class StudioState {
public:
    UIState data;
    AppSettings settings;
    int selectedFuncIndex = -1;
    std::atomic<bool> isBusy = false;
    std::vector<std::string> logs;
    std::mutex stateMutex;
    std::streambuf* oldCout = nullptr;
    std::streambuf* oldCerr = nullptr;
    std::unique_ptr<StreamRedirector> redirector;
    std::future<void> workerThread;
    std::vector<std::string> availableFonts;

    // Deferred font rebuild flag - fonts are rebuilt in the main loop BEFORE NewFrame
    std::atomic<bool> pendingFontRebuild{false};

    // Resolved absolute path to config.toml
    std::string configTomlPath;

    // Track log changes for efficient UI updates
    std::atomic<size_t> logVersion{0};

    // Thread-safe status message
    void SetStatus(const std::string& msg) {
        std::lock_guard<std::mutex> lock(statusMutex_);
        statusMessage_ = msg;
    }
    std::string GetStatus() const {
        std::lock_guard<std::mutex> lock(statusMutex_);
        return statusMessage_;
    }

    StudioState() {
        redirector = std::make_unique<StreamRedirector>(logs, stateMutex, logVersion);
        oldCout = std::cout.rdbuf(redirector.get());
        oldCerr = std::cerr.rdbuf(redirector.get());

        try {
            if (!std::filesystem::exists("output")) {
                std::filesystem::create_directory("output");
            }
            data.outputPath = std::filesystem::absolute("output").string();
        } catch(...) {}

        LoadSettings();
        ScanFonts();

        // Load config.toml immediately and resolve its path
        LoadConfigToml();
    }

    ~StudioState() {
        std::cout.rdbuf(oldCout);
        std::cerr.rdbuf(oldCerr);
        SaveSettings();
    }

    void Log(const std::string& msg) {
        std::lock_guard<std::mutex> lock(stateMutex);
        logs.push_back("[Studio] " + msg);
        logVersion.fetch_add(1);
    }

    void LogRaw(const std::string& msg) {
        std::lock_guard<std::mutex> lock(stateMutex);
        logs.push_back(msg);
        logVersion.fetch_add(1);
    }

    void ScanFonts() {
        availableFonts.clear();
        try {
            std::filesystem::path fontDir = "external/Font";
            if (std::filesystem::exists(fontDir)) {
                for (const auto& entry : std::filesystem::directory_iterator(fontDir)) {
                    if (entry.is_regular_file()) {
                        std::string ext = entry.path().extension().string();
                        if (ext == ".ttf" || ext == ".TTF") {
                            availableFonts.push_back(entry.path().filename().string());
                        }
                    }
                }
            }
        } catch(...) {}

        if (availableFonts.empty()) {
            availableFonts.push_back("Default");
        }
    }

    void LoadSettings() {
        try {
            std::ifstream file("studio_settings.ini");
            if (file) {
                std::string line;
                while (std::getline(file, line)) {
                    size_t pos = line.find('=');
                    if (pos != std::string::npos) {
                        std::string key = line.substr(0, pos);
                        std::string value = line.substr(pos + 1);

                        if (key == "theme") {
                            int t = std::stoi(value);
                            settings.theme = static_cast<ThemeMode>(t);
                        } else if (key == "fontSize") {
                            float newSize = std::stof(value);
                            // Validate font size
                            if (newSize >= 10.0f && newSize <= 48.0f) {
                                settings.fontSize = newSize;
                            }
                        } else if (key == "uiScale") {
                            float newScale = std::stof(value);
                            if (newScale >= 0.5f && newScale <= 3.0f) {
                                settings.uiScale = newScale;
                            }
                        } else if (key == "selectedFont") {
                            settings.selectedFont = value;
                        } else if (key == "windowWidth") {
                            settings.windowWidth = std::stoi(value);
                        } else if (key == "windowHeight") {
                            settings.windowHeight = std::stoi(value);
                        } else if (key == "maximized") {
                            settings.maximized = (value == "1");
                        } else if (key == "customOutputPath") {
                            data.customOutputPath = value;
                        }
                    }
                }
            }
        } catch(...) {}
    }

    void SaveSettings() {
        try {
            std::ofstream file("studio_settings.ini");
            file << "theme=" << static_cast<int>(settings.theme) << "\n";
            file << "fontSize=" << settings.fontSize << "\n";
            file << "uiScale=" << settings.uiScale << "\n";
            file << "selectedFont=" << settings.selectedFont << "\n";
            file << "windowWidth=" << settings.windowWidth << "\n";
            file << "windowHeight=" << settings.windowHeight << "\n";
            file << "maximized=" << (settings.maximized ? "1" : "0") << "\n";
            if (!data.customOutputPath.empty()) {
                file << "customOutputPath=" << data.customOutputPath << "\n";
            }
        } catch(...) {}
    }

    void LoadELF(const std::string& path) {
        data.elfPath = path;
        data.isAnalysisComplete = false;
        data.analyzer.reset();
        data.funcOverrides.clear();

        try {
            std::ifstream file(path, std::ios::binary);
            if (file) {
                data.rawElfData.assign((std::istreambuf_iterator<char>(file)), 
                                      (std::istreambuf_iterator<char>()));
            } else { 
                data.rawElfData.clear(); 
            }
        } catch (...) { 
            data.rawElfData.clear(); 
        }

        std::string filename = std::filesystem::path(path).filename().string();
        SetStatus("Loaded: " + filename);
        Log("File loaded: " + path);
    }

    void ImportGhidraCSV(const std::string& csvPath) {
        if (!data.analyzer) {
            Log("Error: Analyze ELF first before importing symbols.");
            return;
        }

        data.ghidraCSVPath = csvPath;

        try {
            std::ifstream file(csvPath);
            if (file) {
                std::stringstream buffer;
                buffer << file.rdbuf();
                data.ghidraCSVContent = buffer.str();
            }
        } catch(...) {}

        data.analyzer->importGhidraMap(csvPath);
        Log("Ghidra symbols imported successfully from: " + csvPath);
    }

    void SetOutputDir(const std::string& path) {
        if (path.empty()) return;

        try {
            data.customOutputPath = std::filesystem::absolute(path).string();
            if (!std::filesystem::exists(data.customOutputPath)) {
                std::filesystem::create_directories(data.customOutputPath);
            }
            Log("Custom output directory set to: " + data.customOutputPath);
        } catch (const std::exception& e) {
            Log(std::string("Error setting output dir: ") + e.what());
            data.customOutputPath = std::filesystem::absolute("output").string();
        }
    }

    void StartAnalysis(std::string path = "") {
        if (isBusy) return;
        if (!path.empty()) data.elfPath = path;
        if (data.elfPath.empty()) { 
            Log("Error: No file selected."); 
            return; 
        }

        isBusy = true;
        SetStatus("Analyzing...");
        std::string currentPath = data.elfPath;

        workerThread = std::async(std::launch::async, [this, currentPath]() {
            try {
                auto newAnalyzer = std::make_shared<ps2recomp::ElfAnalyzer>(currentPath);
                if (newAnalyzer->analyze()) {
                    std::lock_guard<std::mutex> lock(stateMutex);
                    data.analyzer = newAnalyzer;
                    data.isAnalysisComplete = true;
                    SetStatus("Analysis Complete");
                    logVersion.fetch_add(1);
                } else {
                    SetStatus("Analysis Failed");
                    Log("Analysis failed for: " + currentPath);
                }
            } catch (const std::exception& e) {
                SetStatus("Error"); 
                Log(std::string("Analysis exception: ") + e.what());
            }
            isBusy = false;
        });
    }

    void MoveOutputFiles() {
        if (data.customOutputPath.empty() || data.customOutputPath == data.outputPath) {
            return;
        }

        try {
            std::filesystem::path srcDir = data.outputPath;
            std::filesystem::path dstDir = data.customOutputPath;

            if (!std::filesystem::exists(dstDir)) {
                std::filesystem::create_directories(dstDir);
            }

            for (const auto& entry : std::filesystem::directory_iterator(srcDir)) {
                if (entry.is_regular_file()) {
                    std::filesystem::path dst = dstDir / entry.path().filename();
                    std::filesystem::copy_file(entry.path(), dst, 
                                             std::filesystem::copy_options::overwrite_existing);
                    Log("Moved: " + entry.path().filename().string());
                }
            }

            Log("All output files moved to: " + data.customOutputPath);
        } catch (const std::exception& e) {
            Log(std::string("Error moving files: ") + e.what());
        }
    }

    void StartRecompilation() {
        if (isBusy || !data.isAnalysisComplete) return;

        // Set the effective output path BEFORE saving config
        std::string effectiveOutputPath = data.customOutputPath.empty() 
            ? data.outputPath : data.customOutputPath;

        if (effectiveOutputPath.empty()) {
            SetOutputDir("output");
            effectiveOutputPath = data.outputPath;
        }

        // Ensure output directory exists
        try {
            if (!std::filesystem::exists(effectiveOutputPath)) {
                std::filesystem::create_directories(effectiveOutputPath);
            }
        } catch (const std::exception& e) {
            Log(std::string("Error creating output dir: ") + e.what());
            return;
        }

        SaveConfigTOML();
        isBusy = true;
        SetStatus("Recompiling...");

        // Use the resolved config path
        std::string resolvedConfigPath = configTomlPath.empty() ? "config.toml" : configTomlPath;

        workerThread = std::async(std::launch::async, [this, resolvedConfigPath]() {
            try {
                ps2recomp::PS2Recompiler recompiler(resolvedConfigPath);
                if (recompiler.initialize() && recompiler.recompile()) {
                    recompiler.generateOutput();
                    SetStatus("Recompilation Success");
                    Log("Recompilation completed successfully");
                    LoadConfigToml();
                } else {
                    SetStatus("Recompilation Failed");
                    Log("Recompilation failed");
                }
            } catch (const std::exception& e) {
                SetStatus("Recompilation Error");
                Log(std::string("Recompilation error: ") + e.what());
            }
            isBusy = false;
        });
    }

    void RunDiagnostics() {
        if (isBusy) return;
        isBusy = true;
        SetStatus("Running Diagnostics...");

        workerThread = std::async(std::launch::async, [this]() {
            try {
                register_code_generator_tests();
                register_r5900_decoder_tests();
                std::cout << "\n--- DIAGNOSTICS ---\n" << std::endl;
                int failed = MiniTest::Run();
                if (failed == 0) {
                    SetStatus("System Healthy");
                    std::cout << "\n[SUCCESS] All systems operational." << std::endl;
                } else {
                    SetStatus("Issues Found");
                    std::cerr << "\n[WARNING] " << failed << " tests failed!" << std::endl;
                }
            } catch (const std::exception& e) {
                SetStatus("Diagnostics Error");
                std::cerr << "Critical failure: " << e.what() << std::endl;
            }
            isBusy = false;
        });
    }

    // Resolve and load config.toml with proper path tracking
    void LoadConfigToml() {
        bool loaded = false;

        // Build search paths - prioritize custom output path and previously resolved path
        std::vector<std::string> configPaths;
        if (!configTomlPath.empty()) {
            configPaths.push_back(configTomlPath); // previously resolved
        }
        configPaths.push_back("config.toml");
        if (!data.customOutputPath.empty()) {
            configPaths.push_back(data.customOutputPath + "/config.toml");
        }
        if (!data.outputPath.empty()) {
            configPaths.push_back(data.outputPath + "/config.toml");
        }
        configPaths.push_back("../config.toml");

        for (const auto& path : configPaths) {
            if (path.empty()) continue;

            try {
                std::ifstream file(path);
                if (file && file.good()) {
                    std::stringstream buffer;
                    buffer << file.rdbuf();
                    std::string content = buffer.str();
                    if (!content.empty()) {
                        data.configTomlContent = content;
                        configTomlPath = std::filesystem::absolute(path).string();
                        Log("Config loaded from: " + configTomlPath);
                        loaded = true;
                        break;
                    }
                }
            } catch(...) {
                continue;
            }
        }

        if (!loaded) {
            // Create default config in CWD
            configTomlPath = std::filesystem::absolute("config.toml").string();
            CreateDefaultConfig();
        }
    }

    // NEW: Create default config if none exists
    void CreateDefaultConfig() {
        try {
            std::string defaultConfig = R"(# PS2Recomp Configuration File
# Generated by PS2Recomp Studio

[input]
path = ""

[output]
path = "output"
single_file = false

[functions]
stub = []
skip = []
force_recompile = []

[settings]
optimize = true
debug_info = false
)";

            std::string savePath = configTomlPath.empty() ? "config.toml" : configTomlPath;
            std::ofstream file(savePath);
            if (file) {
                file << defaultConfig;
                file.flush();
                data.configTomlContent = defaultConfig;
                if (configTomlPath.empty()) {
                    configTomlPath = std::filesystem::absolute(savePath).string();
                }
                Log("Created default config at: " + configTomlPath);
            }
        } catch (const std::exception& e) {
            Log(std::string("Error creating default config: ") + e.what());
        }
    }

    void SaveConfigTOML() {
        if (!data.isAnalysisComplete) return;

        try {
            ps2recomp::RecompilerConfig config;
            config.inputPath = data.elfPath;
            // Use the effective output path (custom if set, default otherwise)
            config.outputPath = data.customOutputPath.empty() 
                ? data.outputPath : data.customOutputPath;
            config.singleFileOutput = false;

            for (const auto& [name, status] : data.funcOverrides) {
                if (status == OverrideStatus::Stub) {
                    config.stubImplementations.push_back(name);
                } else if (status == OverrideStatus::Skip) {
                    config.skipFunctions.push_back(name);
                }
            }

            std::string savePath = configTomlPath.empty() ? "config.toml" : configTomlPath;
            ps2recomp::ConfigManager mgr(savePath);
            mgr.saveConfig(config);
            Log("Config saved to " + savePath);

            LoadConfigToml();
        } catch (const std::exception& e) {
            Log(std::string("Error saving config: ") + e.what());
        }
    }

    void SaveConfigTomlFromEditor(const std::string& newContent) {
        try {
            std::string savePath = configTomlPath.empty() ? "config.toml" : configTomlPath;
            std::ofstream file(savePath);
            if (file) {
                file << newContent;
                file.flush();
                data.configTomlContent = newContent;
                Log("config.toml saved to: " + savePath);
            } else {
                Log("Error: Cannot open " + savePath + " for writing");
            }
        } catch (const std::exception& e) {
            Log(std::string("Error saving config.toml: ") + e.what());
        }
    }

    // Get the effective output directory
    std::string GetEffectiveOutputPath() const {
        if (!data.customOutputPath.empty()) return data.customOutputPath;
        return data.outputPath;
    }

private:
    mutable std::mutex statusMutex_;
    std::string statusMessage_ = "Ready";
};
