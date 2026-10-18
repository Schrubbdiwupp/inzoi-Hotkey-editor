#include "editor_ui.h"

#include <filesystem>

#include "hotkeys.h"
#include "i18n.h"
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"
#include "pipeline.h"
#include "settings_ui.h"
#include "widget_asset.h"

namespace
{
    HWND mainWindow = nullptr;
    Settings& settings = AppSettings();
    WidgetAsset asset;
    ChangeMap changes;
    std::string loadError;
    int selectedPage = 0;
    int seenJobs = 0;
    bool changesDirty = false;
    bool openSettingsTab = false;   // first start with missing paths
    bool lastEnglish = false;

    const ImVec4 Accent = ImVec4(0.96f, 0.55f, 0.20f, 1.0f);   // Klappkatz orange
    const ImVec4 Muted = ImVec4(0.60f, 0.62f, 0.68f, 1.0f);
    const ImVec4 Good = ImVec4(0.45f, 0.85f, 0.50f, 1.0f);
    const ImVec4 Bad = ImVec4(1.00f, 0.42f, 0.38f, 1.0f);

    void UpdateWindowTitle()
    {
        SetWindowTextW(mainWindow, I18n::English() ? L"F6 Editor  -  inZOI key guide & mod hotkeys"
                                                   : L"F6-Editor  -  inZOI Tastenhilfe & Mod-Hotkeys");
    }

    void Reload()
    {
        loadError.clear();
        std::error_code ec;
        if (!Check::KeyGuideReady(settings))
        {
            loadError = T("Für die Tastenhilfe fehlen noch Pfade - bitte im Reiter \"Einstellungen\" ergänzen.",
                          "Some paths for the key guide are still missing - please fill them in on the \"Settings\" tab.");
            return;
        }
        if (!std::filesystem::exists(std::filesystem::u8path(settings.OriginalJson()), ec))
        {
            loadError = T("Noch kein Original geladen - oben auf \"Original aus Spiel laden\" klicken.",
                          "No original loaded yet - click \"Load original from game\" above.");
            return;
        }
        if (!asset.Load(settings.OriginalJson(), settings.EnglishTexts(), loadError)) return;
        LoadChanges(settings.ChangesJson(), changes);
        if (selectedPage >= static_cast<int>(asset.Pages().size())) selectedPage = 0;
    }

    void SaveIfDirty()
    {
        if (!changesDirty) return;
        SaveChanges(settings.ChangesJson(), changes);
        changesDirty = false;
    }

    int CountChanges()
    {
        int n = 0;
        for (const auto& [id, c] : changes) if (!c.Empty()) ++n;
        return n;
    }

    void StartInstall()
    {
        SaveIfDirty();
        const Settings s = settings;
        const ChangeMap c = changes;
        WidgetAsset* a = &asset;
        Pipeline::RunAsync(T("Ins Spiel", "Install"), [s, c, a](std::vector<std::string>& log) {
            std::string error;
            const std::string modified = s.dataDir + "\\geaendert.json";
            const int n = a->WriteModified(c, modified, error);
            if (n < 0) { log.push_back(std::string(T("FEHLER: ", "ERROR: ")) + error); return false; }
            log.push_back(std::to_string(n) + T(" Zeile(n) geändert.", " row(s) changed."));
            return Pipeline::BuildAndInstall(s, modified, log);
        });
    }

    void DrawToolbar()
    {
        const bool busy = Pipeline::Busy();
        const bool ready = Check::KeyGuideReady(settings);
        ImGui::BeginDisabled(busy || !ready);
        if (ImGui::Button(T("Original aus Spiel laden", "Load original from game")))
        {
            const Settings s = settings;
            Pipeline::RunAsync(T("Original laden", "Load original"), [s](std::vector<std::string>& log) { return Pipeline::ExtractOriginal(s, log); });
        }
        Tip("Holt die Tastenhilfe frisch aus den Spieldateien. Nötig beim ersten Mal und nach jedem inZOI-Update. Deine Änderungen bleiben erhalten.",
            "Extracts the key guide fresh from the game files. Needed the first time and after every inZOI update. Your changes are kept.");
        ImGui::SameLine();
        ImGui::BeginDisabled(!asset.Loaded());
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.38f, 0.10f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.90f, 0.48f, 0.15f, 1.0f));
        if (ImGui::Button(T("  Ins Spiel  ", "  Install  "))) StartInstall();
        ImGui::PopStyleColor(2);
        ImGui::EndDisabled();
        Tip("Baut aus deinen Änderungen ein Mod-Pak und legt es in den ~mods-Ordner des Spiels. Wirkt nach dem nächsten Spielstart. Am besten bei geschlossenem Spiel.",
            "Builds a mod pak from your changes and puts it into the game's ~mods folder. Takes effect on the next game start. Best done with the game closed.");
        ImGui::SameLine();
        if (ImGui::Button(T("Override entfernen", "Remove override")))
        {
            const Settings s = settings;
            Pipeline::RunAsync(T("Override entfernen", "Remove override"), [s](std::vector<std::string>& log) { return Pipeline::RemoveOverride(s, log); });
        }
        Tip("Nimmt das Mod-Pak wieder aus dem Spiel (es wird gesichert). Danach zeigt das Spiel wieder die Original-Tastenhilfe.",
            "Takes the mod pak out of the game again (it is backed up). The game then shows the original key guide again.");
        ImGui::EndDisabled();

        ImGui::SameLine(0, 24);
        if (busy)
        {
            const char spin[] = "|/-\\";
            ImGui::TextColored(Accent, "%s %s %c", Pipeline::CurrentTitle().c_str(), T("läuft ...", "running ..."),
                               spin[static_cast<int>(ImGui::GetTime() * 8) % 4]);
        }
        else if (ready && Pipeline::OverrideInstalled(settings))
            ImGui::TextColored(Good, "%s", T("Override ist installiert", "Override is installed"));
        else
            ImGui::TextColored(Muted, "%s", T("Kein Override installiert (Spiel zeigt das Original)", "No override installed (game shows the original)"));

        ImGui::SameLine();
        const int n = CountChanges();
        const std::string count = std::to_string(n) + " " + (n == 1 ? T("Änderung", "change") : T("Änderungen", "changes"));
        ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - ImGui::CalcTextSize(count.c_str()).x);
        ImGui::TextColored(n ? Accent : Muted, "%s", count.c_str());
        Tip("So viele Zeilen hast du geändert (Text oder sichtbar/unsichtbar). Gespeichert wird sofort.",
            "This many rows are changed (text or shown/hidden). Saved right away.");
    }

    void DrawPageList()
    {
        ImGui::BeginChild("pages", ImVec2(200, 0), ImGuiChildFlags_Borders);
        ImGui::TextColored(Muted, "%s", T("SEITEN", "PAGES"));
        Tip("Die Tastenhilfe hat eine Seite je Spielsituation. Das Spiel zeigt automatisch die passende.",
            "The key guide has one page per game situation. The game shows the right one by itself.");
        ImGui::Separator();
        const auto& pages = asset.Pages();
        for (int i = 0; i < static_cast<int>(pages.size()); ++i)
        {
            int changed = 0;
            for (const GuideRow& r : pages[static_cast<size_t>(i)].rows)
                if (auto it = changes.find(r.id); it != changes.end() && !it->second.Empty()) ++changed;
            const std::string& title = I18n::English() ? pages[static_cast<size_t>(i)].titleEn : pages[static_cast<size_t>(i)].titleDe;
            std::string label = title + (changed ? "  (" + std::to_string(changed) + ")" : "") + "##p" + std::to_string(i);
            if (ImGui::Selectable(label.c_str(), selectedPage == i, 0, ImVec2(0, 28))) selectedPage = i;
        }
        ImGui::EndChild();
    }

    void DrawRows()
    {
        ImGui::BeginChild("rows", ImVec2(0, 0), ImGuiChildFlags_Borders);
        const GuidePage& page = asset.Pages()[static_cast<size_t>(selectedPage)];
        ImGui::TextColored(Accent, "%s", (I18n::English() ? page.titleEn : page.titleDe).c_str());
        ImGui::SameLine();
        ImGui::TextColored(Muted, "  %d %s", static_cast<int>(page.rows.size()),
                           T("Zeilen  -  eigener Text leer = Spieltext bleibt", "rows  -  empty own text = game text stays"));
        ImGui::Spacing();

        if (ImGui::BeginTable("rows", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp |
                                             ImGuiTableFlags_ScrollY))
        {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn(T("Anzeigen", "Show"), ImGuiTableColumnFlags_WidthFixed, 70);
            ImGui::TableSetupColumn(T("Taste(n)", "Key(s)"), ImGuiTableColumnFlags_WidthStretch, 1.1f);
            ImGui::TableSetupColumn(T("Spieltext", "Game text"), ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn(T("Eigener Text", "Own text"), ImGuiTableColumnFlags_WidthStretch, 1.3f);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 74);
            ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
            const char* headerTips[][2] = {
                { "Haken weg = die Zeile wird im Spiel ausgeblendet.", "Untick = the row is hidden in the game." },
                { "Die Spielaktion hinter der Taste ([...] = feste Taste). Welche Taste das genau ist, legt das Spiel fest.",
                  "The game action behind the key ([...] = fixed key). Which key that is exactly is up to the game." },
                { "Der Text, den das Spiel normalerweise zeigt (englisch).", "The text the game normally shows (English)." },
                { "Dein Text für diese Zeile. Leer lassen = Spieltext bleibt (dann auch in der Spielsprache).",
                  "Your text for this row. Leave empty = game text stays (then also in the game's language)." },
                { "", "" },
            };
            for (int c = 0; c < 5; ++c)
            {
                ImGui::TableSetColumnIndex(c);
                ImGui::TableHeader(ImGui::TableGetColumnName(c));
                if (headerTips[c][0][0]) Tip(headerTips[c][0], headerTips[c][1]);
            }

            for (const GuideRow& row : page.rows)
            {
                ImGui::PushID(row.id.c_str());
                RowChange& change = changes[row.id];
                ImGui::TableNextRow(0, 34);

                ImGui::TableNextColumn();
                bool visible = change.overrideVisibility ? change.visible : row.originallyVisible;
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 20);
                if (ImGui::Checkbox("##vis", &visible))
                {
                    change.overrideVisibility = visible != row.originallyVisible;
                    change.visible = visible;
                    changesDirty = true;
                }
                Tip("Zeile im Spiel anzeigen oder ausblenden.", "Show or hide this row in the game.");

                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(visible ? ImVec4(1, 1, 1, 1) : Muted, "%s", row.keys.c_str());
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", row.id.c_str());

                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(Muted, "%s", row.originalText.c_str());
                if (!row.textKey.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", row.textKey.c_str());

                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-1);
                ImGui::BeginDisabled(!row.textExport);
                if (ImGui::InputTextWithHint("##text", row.originalText.c_str(), &change.text)) changesDirty = true;
                ImGui::EndDisabled();

                ImGui::TableNextColumn();
                if (!change.Empty())
                {
                    if (ImGui::SmallButton("Original"))
                    {
                        change = RowChange();
                        changesDirty = true;
                    }
                    Tip("Setzt diese Zeile auf den Spielstand zurück.", "Resets this row to the game's version.");
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::EndChild();
    }

    void DrawLog()
    {
        ImGui::BeginChild("log", ImVec2(0, 0), ImGuiChildFlags_Borders);
        for (const std::string& line : Pipeline::Log())
        {
            const ImVec4 color = IsErrorLine(line) ? Bad : line.rfind("==", 0) == 0 ? Accent :
                                 line.rfind("   ", 0) == 0 ? Muted : ImVec4(0.9f, 0.9f, 0.9f, 1);
            ImGui::TextColored(color, "%s", line.c_str());
        }
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4) ImGui::SetScrollHereY(1.0f);
        ImGui::EndChild();
    }

    void OnSettingsChanged()
    {
        Reload();
        HotkeysTab::Refresh();
    }
}

namespace EditorUi
{
    void Init(HWND window)
    {
        mainWindow = window;
        // First start (or moved PC): fill what can be found, then show the settings if something is still missing.
        if (!Check::KeyGuideReady(settings) || !Check::Mods(settings).empty())
            if (AutoDetect(settings) > 0) SaveSettings();
        openSettingsTab = !Check::Mods(settings).empty() || !Check::KeyGuideReady(settings);
        Reload();
        seenJobs = Pipeline::FinishedJobs();
        lastEnglish = I18n::English();
        UpdateWindowTitle();
        HotkeysTab::Refresh();
    }

    void ApplyTheme()
    {
        ImGuiStyle& style = ImGui::GetStyle();
        ImGui::StyleColorsDark();
        style.WindowRounding = 0;
        style.ChildRounding = 6;
        style.FrameRounding = 5;
        style.GrabRounding = 5;
        style.PopupRounding = 6;
        style.FramePadding = ImVec2(10, 6);
        style.ItemSpacing = ImVec2(10, 8);
        style.CellPadding = ImVec2(8, 5);
        ImVec4* c = style.Colors;
        c[ImGuiCol_WindowBg] = ImVec4(0.055f, 0.060f, 0.078f, 1);
        c[ImGuiCol_ChildBg] = ImVec4(0.075f, 0.080f, 0.100f, 1);
        c[ImGuiCol_PopupBg] = ImVec4(0.09f, 0.095f, 0.12f, 0.98f);
        c[ImGuiCol_FrameBg] = ImVec4(0.13f, 0.14f, 0.17f, 1);
        c[ImGuiCol_FrameBgHovered] = ImVec4(0.18f, 0.19f, 0.23f, 1);
        c[ImGuiCol_Button] = ImVec4(0.17f, 0.18f, 0.22f, 1);
        c[ImGuiCol_ButtonHovered] = ImVec4(0.26f, 0.27f, 0.32f, 1);
        c[ImGuiCol_ButtonActive] = ImVec4(0.96f, 0.55f, 0.20f, 1);
        c[ImGuiCol_Header] = ImVec4(0.96f, 0.55f, 0.20f, 0.35f);
        c[ImGuiCol_HeaderHovered] = ImVec4(0.96f, 0.55f, 0.20f, 0.25f);
        c[ImGuiCol_HeaderActive] = ImVec4(0.96f, 0.55f, 0.20f, 0.45f);
        c[ImGuiCol_Tab] = ImVec4(0.13f, 0.14f, 0.17f, 1);
        c[ImGuiCol_TabHovered] = ImVec4(0.96f, 0.55f, 0.20f, 0.55f);
        c[ImGuiCol_TabSelected] = ImVec4(0.75f, 0.38f, 0.10f, 1);
        c[ImGuiCol_CheckMark] = Accent;
        c[ImGuiCol_TableRowBgAlt] = ImVec4(1, 1, 1, 0.025f);
        c[ImGuiCol_Border] = ImVec4(0.20f, 0.21f, 0.25f, 1);
    }

    void Draw()
    {
        // A finished job may have produced a new original.json.
        if (Pipeline::FinishedJobs() != seenJobs)
        {
            seenJobs = Pipeline::FinishedJobs();
            Reload();
        }
        if (lastEnglish != I18n::English())
        {
            lastEnglish = I18n::English();
            UpdateWindowTitle();
            OnSettingsChanged();   // texts built while loading (errors, trigger names) follow the language
        }

        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos);
        ImGui::SetNextWindowSize(vp->WorkSize);
        ImGui::Begin("F6Editor", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                           ImGuiWindowFlags_NoBringToFrontOnFocus);

        const float logHeight = 150;
        if (ImGui::BeginTabBar("tabs"))
        {
            // ### keeps the tab identity when the language (label) changes.
            if (ImGui::BeginTabItem((std::string(T("  Tastenhilfe (F6)  ", "  Key guide (F6)  ")) + "###guide").c_str()))
            {
                DrawToolbar();
                ImGui::Separator();
                const float mainHeight = ImGui::GetContentRegionAvail().y - logHeight - ImGui::GetStyle().ItemSpacing.y;
                ImGui::BeginChild("main", ImVec2(0, mainHeight));
                if (asset.Loaded())
                {
                    DrawPageList();
                    ImGui::SameLine();
                    DrawRows();
                }
                else
                    ImGui::TextColored(loadError.empty() ? Muted : Bad, "%s", loadError.c_str());
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            Tip("Texte der Tastenhilfe ändern und Zeilen ausblenden.", "Change the key guide's texts and hide rows.");

            if (ImGui::BeginTabItem((std::string(T("  Mod-Hotkeys  ", "  Mod hotkeys  ")) + "###hotkeys").c_str()))
            {
                const float mainHeight = ImGui::GetContentRegionAvail().y - logHeight - ImGui::GetStyle().ItemSpacing.y;
                ImGui::BeginChild("hotkeys", ImVec2(0, mainHeight));
                HotkeysTab::Draw();
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            Tip("Alle Tasten deiner UI-Mods: doppelte finden und neu belegen (auch mit Strg / Alt / Shift).",
                "All keys of your UI mods: find duplicates and reassign them (also with Ctrl / Alt / Shift).");

            const ImGuiTabItemFlags settingsFlags = openSettingsTab ? ImGuiTabItemFlags_SetSelected : 0;
            openSettingsTab = false;
            if (ImGui::BeginTabItem((std::string(T("  Einstellungen  ", "  Settings  ")) + "###settings").c_str(), nullptr, settingsFlags))
            {
                const float mainHeight = ImGui::GetContentRegionAvail().y - logHeight - ImGui::GetStyle().ItemSpacing.y;
                ImGui::BeginChild("settings", ImVec2(0, mainHeight));
                if (SettingsTab::Draw()) OnSettingsChanged();
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            Tip("Pfade zu Spiel, Mods und Werkzeugen, Sprache.", "Paths to game, mods and tools, language.");

            // Quick language switch at the right end of the tab bar.
            if (ImGui::TabItemButton(I18n::English() ? "DE###lang" : "EN###lang", ImGuiTabItemFlags_Trailing))
            {
                settings.language = I18n::English() ? "de" : "en";
                I18n::SetEnglish(settings.language == "en");
                SaveSettings();
            }
            Tip("Sprache umschalten (Deutsch / English)", "Switch language (Deutsch / English)");
            ImGui::EndTabBar();
        }

        DrawLog();
        ImGui::End();

        // Changes are kept in aenderungen.json right away (the file is tiny).
        if (!ImGui::IsAnyItemActive()) SaveIfDirty();
    }
}
