#include "GUI.hpp"
#include "ui/StyleManager.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_memory_editor.h"
#include "TextEditor.h"
#include "ImGuiFileDialog.h"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cmath>

static MemoryEditor mem_edit;
static TextEditor code_editor;
static TextEditor config_editor;
static TextEditor ghidra_editor;
static TextEditor log_editor;  // TextEditor for logs - supports text selection
static bool editors_initialized = false;
static bool show_settings_window = false;
static bool config_editor_needs_sync = false;
static size_t last_log_version = 0;
static bool s_wantsQuit = false;

// HEX view color highlighting
struct HexHighlightRange {
    size_t start;
    size_t end;
    ImU32 color;
    std::string label;
};
static std::vector<HexHighlightRange> hex_highlight_ranges;

// Global pointer for BgColorFn callback
static std::vector<HexHighlightRange>* g_hex_highlights = &hex_highlight_ranges;

std::string FormatHex(uint32_t val) {
    std::stringstream ss;
    ss << "0x" << std::hex << std::setw(8) << std::setfill('0') << val;
    return ss.str();
}

// BgColorFn callback for MemoryEditor - provides per-byte background color
static ImU32 HexBgColorCallback(const ImU8* /*mem*/, size_t off, void* /*user_data*/) {
    if (g_hex_highlights) {
        for (const auto& hl : *g_hex_highlights) {
            if (off >= hl.start && off < hl.end) {
                return hl.color;
            }
        }
    }
    return 0; // no color (transparent)
}

void GUI::ApplySettings(StudioState& state) {
    StyleManager::SetupFonts(state.settings);
    StyleManager::ApplyTheme(state.settings.theme, state.settings);
}

void GUI::RebuildFontsIfNeeded(StudioState& state) {
    if (state.pendingFontRebuild.exchange(false)) {
        StyleManager::SetupFonts(state.settings);
    }
}

void GUI::SyncConfigEditor(StudioState& state) {
    config_editor_needs_sync = true;
}

bool GUI::WantsQuit() {
    return s_wantsQuit;
}

// ---- Settings Window ----
static void DrawSettingsWindow(StudioState& state) {
    if (!show_settings_window) return;

    ImGui::SetNextWindowSize(ImVec2(600, 500), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Settings", &show_settings_window)) {

        if (ImGui::CollapsingHeader("Theme", ImGuiTreeNodeFlags_DefaultOpen)) {
            const char* themes[] = { "Dark", "Light", "Custom" };
            int current = static_cast<int>(state.settings.theme);
            if (ImGui::Combo("Theme Mode", &current, themes, 3)) {
                state.settings.theme = static_cast<ThemeMode>(current);
                StyleManager::ApplyTheme(state.settings.theme, state.settings);
            }

            if (state.settings.theme == ThemeMode::Custom) {
                ImGui::Spacing();
                ImGui::ColorEdit4("Background", state.settings.customBgBase);
                ImGui::ColorEdit4("Accent", state.settings.customAccent);
                if (ImGui::Button("Apply Custom Colors")) {
                    StyleManager::ApplyTheme(state.settings.theme, state.settings);
                }
            }
        }

        if (ImGui::CollapsingHeader("Font & Size", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::BeginCombo("Font", state.settings.selectedFont.c_str())) {
                for (const auto& font : state.availableFonts) {
                    bool selected = (state.settings.selectedFont == font);
                    if (ImGui::Selectable(font.c_str(), selected)) {
                        state.settings.selectedFont = font;
                        // Defer font rebuild to main loop (prevents crash)
                        state.pendingFontRebuild = true;
                    }
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            // Font size slider - changes are deferred to prevent crash
            if (ImGui::SliderFloat("Font Size", &state.settings.fontSize, 10.0f, 24.0f, "%.1f")) {
                // Clamp to safe range
                state.settings.fontSize = std::clamp(state.settings.fontSize, 10.0f, 48.0f);
                // Defer font rebuild to main loop - NOT during frame rendering
                state.pendingFontRebuild = true;
            }
        }

        if (ImGui::CollapsingHeader("UI Scale", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::SliderFloat("Scale", &state.settings.uiScale, 0.5f, 2.0f, "%.2f")) {
                state.settings.uiScale = std::clamp(state.settings.uiScale, 0.5f, 3.0f);
                StyleManager::ApplyTheme(state.settings.theme, state.settings);
            }
        }

        if (ImGui::CollapsingHeader("Window", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Start Maximized", &state.settings.maximized);
            ImGui::SliderInt("Default Width", &state.settings.windowWidth, 800, 3840);
            ImGui::SliderInt("Default Height", &state.settings.windowHeight, 600, 2160);
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("Rescan Fonts", ImVec2(150, 0))) {
            state.ScanFonts();
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset to Defaults", ImVec2(150, 0))) {
            state.settings = AppSettings();
            state.pendingFontRebuild = true;
        }

        ImGui::SameLine();
        if (ImGui::Button("Close", ImVec2(150, 0))) {
            show_settings_window = false;
        }
    }
    ImGui::End();
}

// ---- Main Draw ----
void GUI::DrawStudio(StudioState& state) {
    if (!editors_initialized) {
        code_editor.SetLanguageDefinition(TextEditor::LanguageDefinition::CPlusPlus());
        code_editor.SetPalette(TextEditor::GetDarkPalette());
        code_editor.SetReadOnly(true);

        config_editor.SetLanguageDefinition(TextEditor::LanguageDefinition::CPlusPlus());
        config_editor.SetPalette(TextEditor::GetDarkPalette());
        config_editor.SetReadOnly(false);
        config_editor.SetText(state.data.configTomlContent);

        ghidra_editor.SetLanguageDefinition(TextEditor::LanguageDefinition::CPlusPlus());
        ghidra_editor.SetPalette(TextEditor::GetDarkPalette());
        ghidra_editor.SetReadOnly(true);

        // Log editor: read-only TextEditor that supports text selection (Ctrl+A, Ctrl+C, mouse drag)
        log_editor.SetPalette(TextEditor::GetDarkPalette());
        log_editor.SetReadOnly(true);
        log_editor.SetShowWhitespaces(false);

        mem_edit.ReadOnly = true;
        mem_edit.OptShowAscii = true;
        mem_edit.OptGreyOutZeroes = true;
        mem_edit.OptUpperCaseHex = false;
        // Set BgColorFn for highlighting selected functions in hex view
        mem_edit.BgColorFn = HexBgColorCallback;
        mem_edit.UserData = nullptr;

        editors_initialized = true;
    }

    // Sync config editor if requested
    if (config_editor_needs_sync) {
        config_editor.SetText(state.data.configTomlContent);
        config_editor_needs_sync = false;
    }

    // Update log editor when logs change
    {
        size_t currentVersion = state.logVersion.load();
        if (currentVersion != last_log_version) {
            std::lock_guard<std::mutex> lock(state.stateMutex);
            std::stringstream ss;
            for (const auto& logLine : state.logs) {
                ss << logLine << "\n";
            }
            log_editor.SetText(ss.str());

            // Set error markers for lines containing Error/Failed
            TextEditor::ErrorMarkers markers;
            for (size_t i = 0; i < state.logs.size(); i++) {
                const auto& line = state.logs[i];
                if (line.find("Error") != std::string::npos ||
                    line.find("Failed") != std::string::npos ||
                    line.find("error") != std::string::npos) {
                    markers.insert(std::make_pair(static_cast<int>(i + 1), line));
                }
            }
            log_editor.SetErrorMarkers(markers);

            last_log_version = currentVersion;
        }
    }

    // Dockspace
    ImGuiID dockspace_id = ImGui::GetID("StudioDock");
    ImGui::DockSpaceOverViewport(dockspace_id, ImGui::GetMainViewport());

    static bool first_run = true;
    if (first_run) {
        first_run = false;
        ImGui::DockBuilderRemoveNode(dockspace_id);
        ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspace_id, ImGui::GetMainViewport()->Size);

        ImGuiID dock_left = ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Left, 0.22f, nullptr, &dockspace_id);
        ImGuiID dock_right = ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Right, 0.28f, nullptr, &dockspace_id);
        ImGuiID dock_down = ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Down, 0.25f, nullptr, &dockspace_id);

        ImGui::DockBuilderDockWindow("Explorer", dock_left);
        ImGui::DockBuilderDockWindow("Inspector", dock_right);
        ImGui::DockBuilderDockWindow("Logs", dock_down);
        ImGui::DockBuilderDockWindow("Workspace", dockspace_id);
        ImGui::DockBuilderFinish(dockspace_id);
    }

    // File dialogs
    ImGui::SetNextWindowSize(ImVec2(900, 600), ImGuiCond_FirstUseEver);

    if (ImGuiFileDialog::Instance()->Display("ChooseFileDlgKey")) {
        if (ImGuiFileDialog::Instance()->IsOk()) {
            state.LoadELF(ImGuiFileDialog::Instance()->GetFilePathName());
        }
        ImGuiFileDialog::Instance()->Close();
    }

    if (ImGuiFileDialog::Instance()->Display("ImportGhidraKey")) {
        if (ImGuiFileDialog::Instance()->IsOk()) {
            std::string csvPath = ImGuiFileDialog::Instance()->GetFilePathName();
            state.ImportGhidraCSV(csvPath);
            // Add to imported CSV list (avoid duplicates)
            bool alreadyImported = false;
            for (const auto& existing : state.data.importedCSVFiles) {
                if (existing == csvPath) { alreadyImported = true; break; }
            }
            if (!alreadyImported) {
                state.data.importedCSVFiles.push_back(csvPath);
            }
            state.data.selectedCSVIndex = static_cast<int>(state.data.importedCSVFiles.size()) - 1;
            ghidra_editor.SetText(state.data.ghidraCSVContent);
        }
        ImGuiFileDialog::Instance()->Close();
    }

    if (ImGuiFileDialog::Instance()->Display("ChooseDirDlgKey")) {
        if (ImGuiFileDialog::Instance()->IsOk()) {
            state.SetOutputDir(ImGuiFileDialog::Instance()->GetFilePathName());
        }
        ImGuiFileDialog::Instance()->Close();
    }

    // ---- Main Menu Bar ----
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Open ELF...", "Ctrl+O")) {
                IGFD::FileDialogConfig config;
                config.path = ".";
                ImGuiFileDialog::Instance()->OpenDialog("ChooseFileDlgKey", "Choose File", ".*,.elf", config);
            }
            if (ImGui::MenuItem("Import Ghidra CSV...")) {
                IGFD::FileDialogConfig config;
                config.path = ".";
                ImGuiFileDialog::Instance()->OpenDialog("ImportGhidraKey", "Choose CSV", ".csv", config);
            }
            if (ImGui::MenuItem("Set Output Directory...")) {
                IGFD::FileDialogConfig config;
                config.path = ".";
                ImGuiFileDialog::Instance()->OpenDialog("ChooseDirDlgKey", "Choose Output Dir", nullptr, config);
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Save Config", "Ctrl+S", false, state.data.isAnalysisComplete)) {
                state.SaveConfigTOML();
                config_editor_needs_sync = true;
            }
            if (ImGui::MenuItem("Reload Config from Disk")) {
                state.LoadConfigToml();
                config_editor_needs_sync = true;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit")) s_wantsQuit = true;
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Tools")) {
            bool busy = state.isBusy.load();
            if (ImGui::MenuItem("Analyze", "F5", false, !busy && !state.data.elfPath.empty())) {
                state.StartAnalysis();
            }
            if (ImGui::MenuItem("Recompile", "F7", false, !busy && state.data.isAnalysisComplete)) {
                state.StartRecompilation();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Run System Diagnostics", nullptr, false, !busy)) {
                state.RunDiagnostics();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Settings")) {
            if (ImGui::MenuItem("Open Settings Window")) {
                show_settings_window = true;
            }
            ImGui::EndMenu();
        }

        // Show output path in menu bar
        std::string pathDisplay = state.GetEffectiveOutputPath();
        float pathWidth = ImGui::CalcTextSize(pathDisplay.c_str()).x;
        if (ImGui::GetWindowWidth() > pathWidth + 300) {
            ImGui::SameLine(ImGui::GetWindowWidth() - pathWidth - 20);
            ImGui::TextDisabled("Output: %s", pathDisplay.c_str());
        }

        ImGui::EndMainMenuBar();
    }

    // ---- Keyboard Shortcuts ----
    {
        ImGuiIO& io = ImGui::GetIO();
        bool noPopup = !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
        if (noPopup) {
            if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O)) {
                IGFD::FileDialogConfig config;
                config.path = ".";
                ImGuiFileDialog::Instance()->OpenDialog("ChooseFileDlgKey", "Choose File", ".*,.elf", config);
            }
            if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S) && state.data.isAnalysisComplete) {
                state.SaveConfigTOML();
                config_editor_needs_sync = true;
            }
            bool busy = state.isBusy.load();
            if (ImGui::IsKeyPressed(ImGuiKey_F5) && !busy && !state.data.elfPath.empty()) {
                state.StartAnalysis();
            }
            if (ImGui::IsKeyPressed(ImGuiKey_F7) && !busy && state.data.isAnalysisComplete) {
                state.StartRecompilation();
            }
        }
    }

    DrawSettingsWindow(state);

    // ---- Explorer Panel ----
    ImGui::Begin("Explorer");
    ImGui::SetNextItemWidth(-1);
    static char filterBuf[128] = "";
    ImGui::InputTextWithHint("##Search", "Search functions...", filterBuf, 128);
    ImGui::Separator();

    if (state.data.isAnalysisComplete && state.data.analyzer) {
        ImGui::BeginChild("FuncList", ImVec2(0, 0), false);
        const auto& funcs = state.data.analyzer->getFunctions();

        // Build filtered index list so clipper works correctly
        std::string filterStr(filterBuf);
        static std::vector<int> filteredIndices;
        filteredIndices.clear();
        filteredIndices.reserve(funcs.size());
        for (int idx = 0; idx < static_cast<int>(funcs.size()); idx++) {
            if (filterStr.empty() || funcs[idx].name.find(filterStr) != std::string::npos) {
                filteredIndices.push_back(idx);
            }
        }

        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(filteredIndices.size()));
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++) {
                if (row < 0 || row >= static_cast<int>(filteredIndices.size())) continue;
                int i = filteredIndices[row];

                const auto& func = funcs[i];

                OverrideStatus status = OverrideStatus::Default;
                auto it = state.data.funcOverrides.find(func.name);
                if (it != state.data.funcOverrides.end()) {
                    status = it->second;
                }

                ImVec4 color = ImGui::GetStyle().Colors[ImGuiCol_Text];
                if (status == OverrideStatus::Stub) {
                    color = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
                } else if (status == OverrideStatus::Skip) {
                    color = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
                } else if (status == OverrideStatus::ForceRecompile) {
                    color = ImVec4(0.4f, 1.0f, 0.4f, 1.0f);
                }

                ImGui::PushStyleColor(ImGuiCol_Text, color);
                bool isSelected = state.selectedFuncIndex == i;
                if (ImGui::Selectable(func.name.c_str(), isSelected)) {
                    state.selectedFuncIndex = i;
                    std::stringstream codeSS;
                    codeSS << "// Function: " << func.name << "\n";
                    codeSS << "// Address:  " << FormatHex(func.start) << "\n";
                    codeSS << "// Size:     " << (func.end - func.start) << " bytes\n\n";
                    codeSS << "void " << func.name << "() {\n";
                    for (const auto& inst : func.instructions) {
                        codeSS << "    // " << FormatHex(inst.address)
                               << ": [0x" << std::hex << inst.opcode << "]\n";
                    }
                    codeSS << "}\n";
                    code_editor.SetText(codeSS.str());

                    // Update hex view to highlight and jump to this function
                    if (!state.data.rawElfData.empty() &&
                        func.start < state.data.rawElfData.size()) {
                        mem_edit.GotoAddrAndHighlight(func.start,
                            std::min(static_cast<size_t>(func.end), state.data.rawElfData.size()));
                    }
                }
                ImGui::PopStyleColor();

                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::Text("Start: %s", FormatHex(func.start).c_str());
                    ImGui::Text("Size:  %d bytes", (func.end - func.start));
                    ImGui::EndTooltip();
                }
            }
        }
        ImGui::EndChild();
    } else {
        float winW = ImGui::GetWindowSize().x;
        float winH = ImGui::GetWindowSize().y;
        if (state.data.elfPath.empty()) {
            const char* txt = "No ELF loaded";
            ImGui::SetCursorPos(ImVec2((winW - ImGui::CalcTextSize(txt).x) * 0.5f, winH * 0.4f));
            ImGui::TextDisabled("%s", txt);
        } else {
            const char* txt = "Ready to Analyze";
            ImGui::SetCursorPos(ImVec2((winW - ImGui::CalcTextSize(txt).x) * 0.5f, winH * 0.4f));
            ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "%s", txt);
            ImGui::SetCursorPosX((winW - 120) * 0.5f);
            if (ImGui::Button("Run Analysis", ImVec2(120, 30))) {
                state.StartAnalysis();
            }
        }
    }
    ImGui::End();

    // ---- Inspector Panel ----
    ImGui::Begin("Inspector");
    if (state.data.analyzer && state.selectedFuncIndex >= 0) {
        const auto& funcs = state.data.analyzer->getFunctions();
        if (state.selectedFuncIndex < static_cast<int>(funcs.size())) {
            const auto& func = funcs[state.selectedFuncIndex];

            if (ImGui::GetIO().Fonts->Fonts.Size > 0) {
                ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]);
                ImGui::TextColored(ImVec4(0.0f, 0.6f, 1.0f, 1.0f), "%s", func.name.c_str());
                ImGui::PopFont();
            } else {
                ImGui::TextColored(ImVec4(0.0f, 0.6f, 1.0f, 1.0f), "%s", func.name.c_str());
            }
            ImGui::Separator();
            ImGui::Spacing();

            OverrideStatus current = OverrideStatus::Default;
            auto it = state.data.funcOverrides.find(func.name);
            if (it != state.data.funcOverrides.end()) {
                current = it->second;
            }

            const char* options[] = { "Auto (Default)", "Stub (Plug)", "Skip (Ignore)", "Force Recompile" };
            int idx = static_cast<int>(current);
            ImGui::TextDisabled("Strategy:");
            ImGui::SetNextItemWidth(-1);
            if (ImGui::Combo("##Action", &idx, options, 4)) {
                state.data.funcOverrides[func.name] = static_cast<OverrideStatus>(idx);
                state.Log("Strategy changed for " + func.name);
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            if (ImGui::BeginTable("MetaTable", 2)) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextDisabled("Address:");
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%s", FormatHex(func.start).c_str());

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextDisabled("Size:");
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%d bytes", (func.end - func.start));

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextDisabled("Instructions:");
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%zu", func.instructions.size());

                ImGui::EndTable();
            }
        } else {
            state.selectedFuncIndex = -1;
            ImGui::TextDisabled("Select a function to inspect");
        }
    } else {
        ImGui::TextDisabled("Select a function to inspect");
    }
    ImGui::End();

    // ---- Workspace Panel (Tabs) ----
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.08f, 0.08f, 1.00f));
    ImGui::Begin("Workspace", nullptr, ImGuiWindowFlags_NoTitleBar);

    if (ImGui::BeginTabBar("Tabs", ImGuiTabBarFlags_Reorderable)) {
        // C++ Preview Tab
        if (ImGui::BeginTabItem("  C++ Preview  ")) {
            if (ImGui::BeginPopupContextItem("CppContext")) {
                if (ImGui::MenuItem("Copy All")) {
                    ImGui::SetClipboardText(code_editor.GetText().c_str());
                    state.Log("C++ code copied to clipboard");
                }
                ImGui::EndPopup();
            }
            code_editor.Render("Editor");
            ImGui::EndTabItem();
        }

        // Hex View Tab - with highlighting
        if (ImGui::BeginTabItem("  Hex View  ")) {
            if (state.data.rawElfData.empty()) {
                ImGui::TextDisabled("No binary data available (File not loaded or read error)");
            } else {
                // Setup hex highlights based on selected function
                hex_highlight_ranges.clear();
                if (state.data.analyzer && state.selectedFuncIndex >= 0) {
                    const auto& funcs = state.data.analyzer->getFunctions();
                    if (state.selectedFuncIndex < static_cast<int>(funcs.size())) {
                        const auto& func = funcs[state.selectedFuncIndex];
                        if (func.start < state.data.rawElfData.size()) {
                            HexHighlightRange hl;
                            hl.start = func.start;
                            hl.end = std::min(static_cast<size_t>(func.end), state.data.rawElfData.size());
                            hl.color = IM_COL32(0, 120, 215, 80);
                            hl.label = "Function: " + func.name;
                            hex_highlight_ranges.push_back(hl);
                        }
                    }
                }

                // Also highlight ELF header (first 52 bytes for 32-bit ELF)
                if (state.data.rawElfData.size() >= 52) {
                    HexHighlightRange elfHeader;
                    elfHeader.start = 0;
                    elfHeader.end = 52;
                    elfHeader.color = IM_COL32(100, 180, 80, 40);
                    elfHeader.label = "ELF Header";
                    hex_highlight_ranges.push_back(elfHeader);
                }

                mem_edit.DrawContents(state.data.rawElfData.data(), state.data.rawElfData.size());
            }
            ImGui::EndTabItem();
        }

        // config.toml Tab
        if (ImGui::BeginTabItem("  config.toml  ")) {
            // Toolbar for config editor
            if (ImGui::Button("Save")) {
                state.SaveConfigTomlFromEditor(config_editor.GetText());
            }
            ImGui::SameLine();
            if (ImGui::Button("Reload from Disk")) {
                state.LoadConfigToml();
                config_editor.SetText(state.data.configTomlContent);
                state.Log("config.toml reloaded from disk");
            }
            ImGui::SameLine();
            if (ImGui::Button("Copy All")) {
                ImGui::SetClipboardText(config_editor.GetText().c_str());
                state.Log("config.toml copied to clipboard");
            }
            ImGui::SameLine();
            if (!state.configTomlPath.empty()) {
                ImGui::TextDisabled("Path: %s", state.configTomlPath.c_str());
            }
            ImGui::Separator();

            config_editor.Render("ConfigEditor");
            ImGui::EndTabItem();
        }

        // Imported CSV Files Tab - ALWAYS visible
        if (ImGui::BeginTabItem("  Ghidra CSV  ")) {
            // Import button always available
            if (ImGui::Button("Import CSV...")) {
                IGFD::FileDialogConfig config;
                config.path = ".";
                ImGuiFileDialog::Instance()->OpenDialog("ImportGhidraKey", "Choose CSV", ".csv", config);
            }

            if (state.data.importedCSVFiles.empty()) {
                ImGui::SameLine();
                ImGui::TextDisabled("No CSV files imported yet");
            } else {
                ImGui::SameLine();
                ImGui::TextDisabled("(%zu files)", state.data.importedCSVFiles.size());
            }
            ImGui::Separator();

            // File list
            if (!state.data.importedCSVFiles.empty()) {
                ImGui::BeginChild("CSVList", ImVec2(0, 120), true);
                for (size_t i = 0; i < state.data.importedCSVFiles.size(); i++) {
                    const auto& csvPath = state.data.importedCSVFiles[i];
                    std::string filename = std::filesystem::path(csvPath).filename().string();

                    ImGui::PushID(static_cast<int>(i));
                    bool isSelected = state.data.selectedCSVIndex == static_cast<int>(i);
                    if (ImGui::Selectable(filename.c_str(), isSelected)) {
                        state.data.selectedCSVIndex = static_cast<int>(i);
                        try {
                            std::ifstream file(csvPath);
                            if (file) {
                                std::stringstream buffer;
                                buffer << file.rdbuf();
                                std::string content = buffer.str();
                                ghidra_editor.SetText(content);
                                state.data.ghidraCSVPath = csvPath;
                                state.data.ghidraCSVContent = content;
                            }
                        } catch (...) {
                            state.Log("Error reading CSV: " + csvPath);
                        }
                    }

                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s", csvPath.c_str());
                    }

                    ImGui::SameLine();
                    if (ImGui::SmallButton("X")) {
                        state.data.importedCSVFiles.erase(state.data.importedCSVFiles.begin() + i);
                        if (state.data.selectedCSVIndex == static_cast<int>(i)) {
                            state.data.selectedCSVIndex = -1;
                            ghidra_editor.SetText("");
                        } else if (state.data.selectedCSVIndex > static_cast<int>(i)) {
                            state.data.selectedCSVIndex--;
                        }
                        ImGui::PopID();
                        break; // restart iteration next frame
                    }
                    ImGui::PopID();
                }
                ImGui::EndChild();
                ImGui::Separator();

                // Show content of selected CSV
                if (state.data.selectedCSVIndex >= 0 &&
                    state.data.selectedCSVIndex < static_cast<int>(state.data.importedCSVFiles.size())) {
                    std::string selFileName = std::filesystem::path(
                        state.data.importedCSVFiles[state.data.selectedCSVIndex]).filename().string();
                    ImGui::TextDisabled("Content: %s", selFileName.c_str());
                    ImGui::Separator();
                    ghidra_editor.Render("GhidraEditor");
                }
            }

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }
    ImGui::End();
    ImGui::PopStyleColor();

    // ---- Logs Panel ----
    ImGui::Begin("Logs");

    // Status bar
    {
        std::string currentStatus = state.GetStatus();
        if (state.isBusy.load()) {
            float progress = std::fmod((float)ImGui::GetTime(), 1.0f);
            ImGui::ProgressBar(progress, ImVec2(-1, 6), "");
            ImGui::SameLine();
            ImGui::Text("%s", currentStatus.c_str());
        } else {
            ImGui::TextDisabled("Status: %s", currentStatus.c_str());
        }
    }

    // Log toolbar - right-aligned
    {
        float btnWidth = ImGui::CalcTextSize("Copy All").x + ImGui::CalcTextSize("Clear").x
                       + ImGui::GetStyle().FramePadding.x * 4 + ImGui::GetStyle().ItemSpacing.x * 2 + 20;
        ImGui::SameLine(std::max(0.0f, ImGui::GetWindowWidth() - btnWidth));
    }
    if (ImGui::SmallButton("Copy All")) {
        std::lock_guard<std::mutex> lock(state.stateMutex);
        std::stringstream allLogs;
        for (const auto& log : state.logs) {
            allLogs << log << "\n";
        }
        ImGui::SetClipboardText(allLogs.str().c_str());
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear")) {
        std::lock_guard<std::mutex> lock(state.stateMutex);
        state.logs.clear();
        state.logVersion.fetch_add(1);
    }

    ImGui::Separator();

    // Log viewer using TextEditor - supports text selection, Ctrl+A, Ctrl+C, mouse drag
    log_editor.Render("LogEditor");

    ImGui::End();
}
