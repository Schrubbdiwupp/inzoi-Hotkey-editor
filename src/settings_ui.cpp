#include "settings_ui.h"

#include <windows.h>
#include <shellapi.h>
#include <shobjidl.h>

#include <filesystem>
#include <functional>
#include <string>

#include "i18n.h"
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"
#include "pipeline.h"

namespace
{
    const ImVec4 Accent = ImVec4(0.96f, 0.55f, 0.20f, 1.0f);
    const ImVec4 Muted = ImVec4(0.60f, 0.62f, 0.68f, 1.0f);
    const ImVec4 Good = ImVec4(0.45f, 0.85f, 0.50f, 1.0f);
    const ImVec4 Bad = ImVec4(1.00f, 0.42f, 0.38f, 1.0f);

    std::string autoDetectResult;

    std::wstring Wide(const std::string& s)
    {
        if (s.empty()) return L"";
        const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
        std::wstring w(static_cast<size_t>(n), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
        return w;
    }

    std::string Utf8(const wchar_t* w) { return std::filesystem::path(w).u8string(); }

    // Windows file/folder dialog. filterSpec like L"*.exe"; empty = folder dialog.
    bool PickPath(const wchar_t* filterName, const wchar_t* filterSpec, std::string& path)
    {
        IFileOpenDialog* dialog = nullptr;
        if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) return false;
        DWORD options = 0;
        dialog->GetOptions(&options);
        if (!filterSpec) dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
        else
        {
            dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST);
            const COMDLG_FILTERSPEC filters[] = { { filterName, filterSpec }, { L"*.*", L"*.*" } };
            dialog->SetFileTypes(2, filters);
        }
        // Start where the current value points to.
        if (!path.empty())
        {
            std::filesystem::path start = std::filesystem::u8path(path);
            std::error_code ec;
            if (!std::filesystem::is_directory(start, ec)) start = start.parent_path();
            IShellItem* folder = nullptr;
            if (!start.empty() && SUCCEEDED(SHCreateItemFromParsingName(start.wstring().c_str(), nullptr, IID_PPV_ARGS(&folder))))
            {
                dialog->SetFolder(folder);
                folder->Release();
            }
        }
        bool picked = false;
        if (SUCCEEDED(dialog->Show(GetActiveWindow())))
        {
            IShellItem* item = nullptr;
            if (SUCCEEDED(dialog->GetResult(&item)))
            {
                PWSTR name = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &name)))
                {
                    path = Utf8(name);
                    picked = true;
                    CoTaskMemFree(name);
                }
                item->Release();
            }
        }
        dialog->Release();
        return picked;
    }

    void OpenUrl(const char* url) { ShellExecuteW(nullptr, L"open", Wide(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL); }

    // One settings row: label, text field, "..." button, status. Returns true if the value changed.
    bool PathRow(const char* id, const char* labelDe, const char* labelEn, std::string& value, const wchar_t* filterName,
                 const wchar_t* filterSpec, const std::string& problem, bool optional, const char* tipDe, const char* tipEn)
    {
        bool changed = false;
        ImGui::PushID(id);
        ImGui::TableNextRow(0, 36);
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(T(labelDe, labelEn));
        if (optional) { ImGui::SameLine(); ImGui::TextColored(Muted, "%s", T("(optional)", "(optional)")); }
        Tip(tipDe, tipEn);

        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##v", &value);
        if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
        Tip(tipDe, tipEn);

        ImGui::TableNextColumn();
        if (ImGui::Button("...", ImVec2(36, 0)) && PickPath(filterName, filterSpec, value)) changed = true;
        Tip(filterSpec ? "Datei auswählen" : "Ordner auswählen", filterSpec ? "Choose file" : "Choose folder");

        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        if (problem.empty())
            ImGui::TextColored(value.empty() && optional ? Muted : Good, "%s", value.empty() && optional ? T("nicht genutzt", "not used") : "OK");
        else
            ImGui::TextColored(Bad, "%s", problem.c_str());
        ImGui::PopID();
        return changed;
    }
}

namespace SettingsTab
{
    bool Draw()
    {
        Settings& s = AppSettings();
        bool changed = false;

        ImGui::TextColored(Accent, "%s", T("Pfade und Werkzeuge", "Paths and tools"));
        ImGui::SameLine();
        ImGui::TextColored(Muted, "%s", T("  -  Änderungen werden sofort gespeichert (f6editor.ini neben der exe)",
                                          "  -  changes are saved right away (f6editor.ini next to the exe)"));

        if (ImGui::Button(T("Automatisch suchen", "Find automatically")))
        {
            const int n = AutoDetect(s);
            autoDetectResult = std::to_string(n) + T(" Pfad(e) gefunden.", " path(s) found.");
            if (n) changed = true;
        }
        Tip("Sucht das Spiel in deinen Steam-Bibliotheken, den Mod-Ordner unter Dokumente und retoc/UAssetGUI/Mapping neben dieser exe. Schon richtige Pfade bleiben, wie sie sind.",
            "Looks for the game in your Steam libraries, the mods folder in Documents and retoc/UAssetGUI/mapping next to this exe. Paths that are already right stay as they are.");
        if (!autoDetectResult.empty()) { ImGui::SameLine(); ImGui::TextColored(Muted, "%s", autoDetectResult.c_str()); }
        ImGui::Spacing();

        if (ImGui::BeginTable("settings", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableSetupColumn(T("Was", "What"), ImGuiTableColumnFlags_WidthFixed, 260);
            ImGui::TableSetupColumn(T("Pfad", "Path"), ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 40);
            ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 300);
            ImGui::TableHeadersRow();

            changed |= PathRow("game", "inZOI-Spielordner", "inZOI game folder", s.gameDir, nullptr, nullptr, Check::Game(s), false,
                               "Der Installationsordner von inZOI (darin liegt BlueClient), z. B. ...\\steamapps\\common\\inZOI.",
                               "The inZOI install folder (contains BlueClient), e.g. ...\\steamapps\\common\\inZOI.");
            changed |= PathRow("mods", "Mod-Ordner", "Mods folder", s.modsDir, nullptr, nullptr, Check::Mods(s), false,
                               "Wo inZOI die Mods ablegt: Dokumente\\inZOI\\Mods. Daraus liest der Reiter Mod-Hotkeys.",
                               "Where inZOI keeps mods: Documents\\inZOI\\Mods. The mod hotkeys tab reads from here.");
            changed |= PathRow("retoc", "retoc.exe", "retoc.exe", s.retocExe, L"retoc.exe", L"*.exe", Check::Retoc(s), false,
                               "Werkzeug zum Auspacken und Packen der Spieldateien (IoStore). Nur für die Tastenhilfe nötig.",
                               "Tool that unpacks and packs the game files (IoStore). Only needed for the key guide.");
            changed |= PathRow("uassetgui", "UAssetGUI.exe", "UAssetGUI.exe", s.uassetGuiExe, L"UAssetGUI.exe", L"*.exe", Check::UAssetGui(s), false,
                               "Werkzeug, das das Widget in lesbares JSON umwandelt und zurück. Nur für die Tastenhilfe nötig.",
                               "Tool that turns the widget into readable JSON and back. Only needed for the key guide.");
            changed |= PathRow("usmap", "Mapping-Datei (.usmap)", "Mapping file (.usmap)", s.usmapFile, L"Mappings (*.usmap)", L"*.usmap", Check::Usmap(s), false,
                               "Beschreibt den Aufbau der Spieldaten (z. B. BlueClient-5.6.1-...usmap). Muss zur Spielversion passen. Die App kopiert sie automatisch in den Mappings-Ordner von UAssetGUI.",
                               "Describes the layout of the game data (e.g. BlueClient-5.6.1-...usmap). Must match the game version. The app copies it into UAssetGUI's Mappings folder by itself.");

            // AES key: either pasted or read from a file.
            ImGui::PushID("aes");
            ImGui::TableNextRow(0, 36);
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(T("AES-Schlüssel", "AES key"));
            const char* keyTipDe = "Schlüssel der verschlüsselten Spieldateien (0x + 64 Hex-Zeichen). Entweder hier einfügen oder mit ... eine Textdatei wählen, in der er steht.";
            const char* keyTipEn = "Key of the encrypted game files (0x + 64 hex digits). Paste it here, or pick a text file that contains it with ...";
            Tip(keyTipDe, keyTipEn);
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextWithHint("##key", s.keyFile.empty() ? "0x..." : (std::string(T("aus Datei: ", "from file: ")) + s.keyFile).c_str(), &s.aesKey);
            if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
            Tip(keyTipDe, keyTipEn);
            ImGui::TableNextColumn();
            if (ImGui::Button("...", ImVec2(36, 0)) && PickPath(L"Text (*.txt)", L"*.txt", s.keyFile)) { s.aesKey.clear(); changed = true; }
            Tip("Textdatei mit dem Schlüssel wählen", "Choose a text file with the key");
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            if (const std::string p = Check::Key(s); p.empty()) ImGui::TextColored(Good, "OK");
            else ImGui::TextColored(Bad, "%s", p.c_str());
            ImGui::PopID();

            changed |= PathRow("data", "Datenordner", "Data folder", s.dataDir, nullptr, nullptr, Check::Data(s), false,
                               "Hier speichert die App ihre Arbeitsdateien: Original, deine Änderungen, Sicherungen.",
                               "Where the app keeps its working files: original, your changes, backups.");
            changed |= PathRow("gamelink", "gamelink (zoi-game.ps1)", "gamelink (zoi-game.ps1)", s.gamelink, L"PowerShell (*.ps1)", L"*.ps1",
                               Check::Gamelink(s), true,
                               "Nur mit dem Zoi-Terminal-Mod: lädt einen Mod nach dem Umbelegen sofort im laufenden Spiel neu. Ohne gilt die neue Taste ab dem nächsten Spielstart.",
                               "Only with the Zoi Terminal mod: reloads a mod in the running game right after you change its key. Without it the new key works from the next game start.");
            ImGui::EndTable();
        }

        ImGui::Spacing();
        ImGui::TextColored(Accent, "%s", T("Sprache", "Language"));
        int lang = s.language == "de" ? 1 : s.language == "en" ? 2 : 0;
        const int before = lang;
        ImGui::RadioButton(T("Wie Windows", "Like Windows"), &lang, 0);
        ImGui::SameLine();
        ImGui::RadioButton("Deutsch", &lang, 1);
        ImGui::SameLine();
        ImGui::RadioButton("English", &lang, 2);
        if (lang != before)
        {
            s.language = lang == 1 ? "de" : lang == 2 ? "en" : "";
            I18n::SetEnglish(lang == 0 ? !I18n::SystemIsGerman() : lang == 2);
            changed = true;
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextColored(Accent, "%s", T("Woher bekomme ich die Werkzeuge?", "Where do I get the tools?"));
        ImGui::TextWrapped("%s", T("Für den Reiter Mod-Hotkeys brauchst du nur den Mod-Ordner. Für die Tastenhilfe (F6) zusätzlich retoc, UAssetGUI, eine passende Mapping-Datei und den AES-Schlüssel des Spiels - beides gibt es in der inZOI-Modding-Community.",
                                   "The mod hotkeys tab only needs the mods folder. The key guide (F6) also needs retoc, UAssetGUI, a matching mapping file and the game's AES key - both are available in the inZOI modding community."));
        if (ImGui::SmallButton("retoc (GitHub)")) OpenUrl("https://github.com/trumank/retoc/releases");
        Tip("Öffnet die Download-Seite im Browser.", "Opens the download page in your browser.");
        ImGui::SameLine();
        if (ImGui::SmallButton("UAssetGUI (GitHub)")) OpenUrl("https://github.com/atenfyr/UAssetGUI/releases");
        Tip("Öffnet die Download-Seite im Browser.", "Opens the download page in your browser.");

        if (changed) SaveSettings();
        return changed;
    }
}
