#include "hotkeys.h"

#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

#include "i18n.h"
#include "imgui.h"
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using Json = nlohmann::ordered_json;

namespace
{
    fs::path P(const std::string& utf8) { return fs::u8path(utf8); }

    struct KeyLabel { const char* de; const char* en; };
    const std::map<std::string, KeyLabel> KeyLabels = {
        { "Zero", { "0", "0" } }, { "One", { "1", "1" } }, { "Two", { "2", "2" } }, { "Three", { "3", "3" } },
        { "Four", { "4", "4" } }, { "Five", { "5", "5" } }, { "Six", { "6", "6" } }, { "Seven", { "7", "7" } },
        { "Eight", { "8", "8" } }, { "Nine", { "9", "9" } },
        { "NumPadZero", { "Num 0", "Num 0" } }, { "NumPadOne", { "Num 1", "Num 1" } }, { "NumPadTwo", { "Num 2", "Num 2" } },
        { "NumPadThree", { "Num 3", "Num 3" } }, { "NumPadFour", { "Num 4", "Num 4" } }, { "NumPadFive", { "Num 5", "Num 5" } },
        { "NumPadSix", { "Num 6", "Num 6" } }, { "NumPadSeven", { "Num 7", "Num 7" } }, { "NumPadEight", { "Num 8", "Num 8" } },
        { "NumPadNine", { "Num 9", "Num 9" } },
        { "Insert", { "Einfg", "Ins" } }, { "Delete", { "Entf", "Del" } }, { "Home", { "Pos1", "Home" } }, { "End", { "Ende", "End" } },
        { "PageUp", { "Bild auf", "PgUp" } }, { "PageDown", { "Bild ab", "PgDn" } }, { "Pause", { "Pause", "Pause" } },
        { "ScrollLock", { "Rollen", "ScrLk" } },
    };

    // Keys the game itself uses (shown as a warning). Only what is known for sure.
    const std::map<std::string, KeyLabel> GameKeys = {
        { "F6", { "inZOI: Spielhilfe (F6)", "inZOI: game help (F6)" } },
    };

    // Reads a JSON file that may start with a UTF-8 BOM. hadBom tells the writer to keep it.
    bool ReadJson(const fs::path& path, Json& out, bool* hadBom = nullptr)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in) return false;
        std::stringstream buffer;
        buffer << in.rdbuf();
        std::string text = buffer.str();
        const bool bom = text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
                         static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF;
        if (bom) text.erase(0, 3);
        if (hadBom) *hadBom = bom;
        try { out = Json::parse(text); }
        catch (...) { return false; }
        return true;
    }

    bool WriteJson(const fs::path& path, const Json& data, bool bom)
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        if (bom) out << "\xEF\xBB\xBF";
        out << data.dump(2);
        return static_cast<bool>(out);
    }

    HotkeyCombo ComboFromTrigger(const Json& t)
    {
        HotkeyCombo c;
        c.key = t.value("key", "");
        if (t.contains("modifiers") && t["modifiers"].is_array())
            for (const Json& m : t["modifiers"])
            {
                const std::string s = m.is_string() ? m.get<std::string>() : "";
                if (s == "Ctrl") c.ctrl = true;
                else if (s == "Alt") c.alt = true;
                else if (s == "Shift") c.shift = true;
            }
        return c;
    }

    Json ComboToJson(const HotkeyCombo& c)
    {
        Json j = { { "key", c.key } };
        Json mods = Json::array();
        if (c.ctrl) mods.push_back("Ctrl");
        if (c.alt) mods.push_back("Alt");
        if (c.shift) mods.push_back("Shift");
        j["modifiers"] = mods;
        return j;
    }

    fs::path BackupPath(const Settings& s, const HotkeyEntry& e)
    {
        return P(s.dataDir) / "hotkey_backups" / P(e.modFolder) / P(e.app) / "uimod_manifest.json";
    }

    fs::path WishesPath(const Settings& s) { return P(s.dataDir) / "hotkeys.json"; }

    std::string ModifierText(bool ctrl, bool alt, bool shift)
    {
        return std::string(ctrl ? T("Strg+", "Ctrl+") : "") + (alt ? "Alt+" : "") + (shift ? "Shift+" : "");
    }
}

std::string HotkeyCombo::Canonical() const
{
    if (key.empty()) return "";
    return std::string(ctrl ? "Ctrl+" : "") + (alt ? "Alt+" : "") + (shift ? "Shift+" : "") + key;
}

std::string HotkeyCombo::Display() const
{
    if (key.empty()) return "-";
    const auto label = KeyLabels.find(key);
    return ModifierText(ctrl, alt, shift) + (label != KeyLabels.end() ? T(label->second.de, label->second.en) : key);
}

namespace Hotkeys
{
    std::vector<HotkeyEntry> Scan(const Settings& s, std::string& error)
    {
        std::vector<HotkeyEntry> entries;
        const fs::path root = P(s.modsDir);
        std::error_code ec;
        if (s.modsDir.empty() || !fs::exists(root, ec))
        {
            error = std::string(T("Mod-Ordner nicht gefunden: ", "Mods folder not found: ")) + s.modsDir +
                    T("  (Einstellungen prüfen)", "  (check the settings)");
            return entries;
        }

        for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec);
             it != fs::recursive_directory_iterator(); it.increment(ec))
        {
            if (ec) break;
            // InGame holds the mods' own saved data, never manifests that the game loads.
            if (it->is_directory(ec) && it->path().filename() == "InGame") { it.disable_recursion_pending(); continue; }
            if (it->path().filename() != "uimod_manifest.json") continue;
            const fs::path appDir = it->path().parent_path();
            if (appDir.parent_path().filename() != "ui") continue;
            const fs::path modRoot = appDir.parent_path().parent_path();

            Json manifest;
            if (!ReadJson(it->path(), manifest) || !manifest.is_object()) continue;

            HotkeyEntry base;
            base.modFolder = modRoot.filename().u8string();
            base.mountId = fs::relative(modRoot, root, ec).generic_u8string();
            if (base.mountId.empty()) base.mountId = base.modFolder;
            base.app = appDir.filename().u8string();
            Json apps;
            if (ReadJson(modRoot / "ui" / "uimod_apps.json", apps) && apps.contains("apps") && apps["apps"].is_array())
                for (const Json& a : apps["apps"])
                    if (a.value("name", "") == base.app) base.appEnabled = a.value("enabled", true);
            base.manifestPath = it->path().u8string();
            base.modName = base.modFolder;
            Json modManifest;
            if (ReadJson(modRoot / "mod_manifest.json", modManifest) && modManifest.is_object())
            {
                base.modName = modManifest.value("FriendlyName", base.modFolder);
                base.modEnabled = modManifest.value("bEnable", true);
            }

            std::vector<HotkeyEntry> keybinds;
            if (manifest.contains("triggers") && manifest["triggers"].is_array())
            {
                const Json& triggers = manifest["triggers"];
                for (size_t i = 0; i < triggers.size(); ++i)
                {
                    const std::string type = triggers[i].value("type", "");
                    if (type == "command") base.commands.push_back("/" + triggers[i].value("command", ""));
                    else if (type == "always_on") base.commands.push_back(T("immer an", "always on"));
                    else if (type == "interaction_menu") base.commands.push_back(T("Interaktionsmenü", "interaction menu"));
                    else if (type == "game_mode")
                    {
                        std::string modes;
                        const Json& m = triggers[i]["mode"];
                        if (m.is_string()) modes = m.get<std::string>();
                        else if (m.is_array()) for (const Json& x : m) if (x.is_string()) modes += (modes.empty() ? "" : ",") + x.get<std::string>();
                        base.commands.push_back(std::string(T("Modus ", "mode ")) + modes);
                    }
                    else if (type == "keybind")
                    {
                        HotkeyEntry e;
                        e.triggerIndex = static_cast<int>(i);
                        e.triggerId = triggers[i].value("id", "");
                        e.combo = ComboFromTrigger(triggers[i]);
                        keybinds.push_back(e);
                    }
                }
            }
            if (keybinds.empty()) keybinds.push_back(HotkeyEntry());   // app without hotkey: one row to add one
            for (HotkeyEntry& e : keybinds)
            {
                HotkeyEntry full = base;
                full.triggerIndex = e.triggerIndex;
                full.triggerId = e.triggerId;
                full.combo = e.combo;
                entries.push_back(full);
            }
        }
        return entries;
    }

    bool HasBackup(const Settings& s, const HotkeyEntry& entry)
    {
        std::error_code ec;
        return fs::exists(BackupPath(s, entry), ec);
    }

    bool Apply(const Settings& s, const HotkeyEntry& entry, const HotkeyCombo& combo, std::string& error)
    {
        Json manifest;
        bool bom = false;
        if (!ReadJson(P(entry.manifestPath), manifest, &bom))
        {
            error = std::string(T("Manifest nicht lesbar: ", "cannot read manifest: ")) + entry.manifestPath;
            return false;
        }

        std::error_code ec;
        const fs::path backup = BackupPath(s, entry);
        if (!fs::exists(backup, ec))
        {
            fs::create_directories(backup.parent_path(), ec);
            fs::copy_file(P(entry.manifestPath), backup, ec);
            if (ec) { error = std::string(T("Sicherung fehlgeschlagen: ", "backup failed: ")) + ec.message(); return false; }
        }

        if (!manifest.contains("triggers") || !manifest["triggers"].is_array()) manifest["triggers"] = Json::array();
        Json& triggers = manifest["triggers"];
        const bool exists = entry.triggerIndex >= 0 && entry.triggerIndex < static_cast<int>(triggers.size());

        if (combo.Empty())
        {
            if (exists) triggers.erase(static_cast<size_t>(entry.triggerIndex));
        }
        else
        {
            if (!exists) triggers.push_back(Json{ { "id", "keybind_hub" }, { "type", "keybind" } });
            Json& t = exists ? triggers[static_cast<size_t>(entry.triggerIndex)] : triggers.back();
            t["key"] = combo.key;
            const Json mods = ComboToJson(combo)["modifiers"];
            if (mods.empty()) t.erase("modifiers");
            else t["modifiers"] = mods;
        }
        if (!WriteJson(P(entry.manifestPath), manifest, bom))
        {
            error = std::string(T("Manifest nicht schreibbar: ", "cannot write manifest: ")) + entry.manifestPath;
            return false;
        }
        return true;
    }

    bool RestoreOriginal(const Settings& s, const HotkeyEntry& entry, std::string& error)
    {
        std::error_code ec;
        const fs::path backup = BackupPath(s, entry);
        if (!fs::exists(backup, ec)) { error = T("Keine Sicherung vorhanden.", "No backup found."); return false; }
        fs::copy_file(backup, P(entry.manifestPath), fs::copy_options::overwrite_existing, ec);
        if (ec) { error = std::string(T("Wiederherstellen fehlgeschlagen: ", "restore failed: ")) + ec.message(); return false; }
        fs::remove(backup, ec);
        return true;
    }

    bool LoadWishes(const Settings& s, std::vector<std::pair<std::string, HotkeyCombo>>& wishes)
    {
        wishes.clear();
        Json data;
        if (!ReadJson(WishesPath(s), data) || !data.contains("wuensche")) return false;
        for (auto it = data["wuensche"].begin(); it != data["wuensche"].end(); ++it)
            wishes.emplace_back(it.key(), ComboFromTrigger(it.value()));
        return true;
    }

    void SetWish(const Settings& s, const std::string& wishKey, const HotkeyCombo* combo)
    {
        Json data;
        if (!ReadJson(WishesPath(s), data) || !data.is_object()) data = Json::object();
        if (!data.contains("wuensche")) data["wuensche"] = Json::object();
        if (combo) data["wuensche"][wishKey] = ComboToJson(*combo);
        else data["wuensche"].erase(wishKey);
        std::error_code ec;
        fs::create_directories(P(s.dataDir), ec);
        WriteJson(WishesPath(s), data, false);
    }

    bool GameRunning()
    {
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE) return false;
        PROCESSENTRY32W entry = { sizeof(entry) };
        bool found = false;
        for (BOOL ok = Process32FirstW(snapshot, &entry); ok && !found; ok = Process32NextW(snapshot, &entry))
            found = _wcsicmp(entry.szExeFile, L"inZOI-Win64-Shipping.exe") == 0;
        CloseHandle(snapshot);
        return found;
    }

    bool CanRemount(const Settings& s)
    {
        std::error_code ec;
        return !s.gamelink.empty() && fs::is_regular_file(P(s.gamelink), ec);
    }

    bool Remount(const Settings& s, const std::string& mountId, std::vector<std::string>& log)
    {
        if (!CanRemount(s))
        {
            log.push_back(T("Kein gamelink eingestellt - die neue Taste gilt nach dem nächsten Spielstart.",
                            "No gamelink set - the new key works after the next game start."));
            return false;
        }
        std::vector<std::string> output;
        Pipeline::RunTool("C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe",
                          "-NoProfile -ExecutionPolicy Bypass -File \"" + s.gamelink + "\" uimod.remount mod_id=" + mountId + " -Wait 20",
                          output);
        bool ok = false;
        for (const std::string& line : output)
            if (line.find("\"success\"") != std::string::npos && line.find("true") != std::string::npos) ok = true;
        log.insert(log.end(), output.begin(), output.end());
        log.push_back(ok ? mountId + T(" im laufenden Spiel neu geladen - neue Taste gilt sofort.",
                                       " reloaded in the running game - the new key works right away.")
                         : std::string(T("FEHLER: Neu laden im Spiel hat nicht geklappt - der Mod ist evtl. bis zum nächsten Spielstart entladen. Nach dem Neustart gilt die neue Taste.",
                                         "ERROR: reloading in the game failed - the mod may be unloaded until the next game start. After a restart the new key works.")));
        return ok;
    }

    std::string KeyNameFromImGui(int imguiKey)
    {
        const ImGuiKey k = static_cast<ImGuiKey>(imguiKey);
        if (k >= ImGuiKey_F1 && k <= ImGuiKey_F12) return "F" + std::to_string(k - ImGuiKey_F1 + 1);
        if (k >= ImGuiKey_A && k <= ImGuiKey_Z) return std::string(1, static_cast<char>('A' + (k - ImGuiKey_A)));
        static const char* digits[] = { "Zero", "One", "Two", "Three", "Four", "Five", "Six", "Seven", "Eight", "Nine" };
        if (k >= ImGuiKey_0 && k <= ImGuiKey_9) return digits[k - ImGuiKey_0];
        if (k >= ImGuiKey_Keypad0 && k <= ImGuiKey_Keypad9) return std::string("NumPad") + digits[k - ImGuiKey_Keypad0];
        switch (k)
        {
        case ImGuiKey_Insert: return "Insert";
        case ImGuiKey_Delete: return "Delete";
        case ImGuiKey_Home: return "Home";
        case ImGuiKey_End: return "End";
        case ImGuiKey_PageUp: return "PageUp";
        case ImGuiKey_PageDown: return "PageDown";
        case ImGuiKey_Pause: return "Pause";
        case ImGuiKey_ScrollLock: return "ScrollLock";
        default: return "";
        }
    }
}

// ---------------------------------------------------------------------------------------------- UI

namespace
{
    Settings& settings = AppSettings();
    std::vector<HotkeyEntry> entries;
    std::map<std::string, std::vector<int>> byCombo;   // canonical combo -> entry indices (active apps only)
    std::map<std::string, HotkeyCombo> wishes;
    std::string scanError;
    bool onlyProblems = false;
    int seenJobs = -1;

    bool needRescan = false;        // actions only flag it: rescanning mid-table would invalidate references
    int editIndex = -1;              // entry in the capture popup
    HotkeyCombo captured;
    bool openPopup = false;

    const ImVec4 Accent = ImVec4(0.96f, 0.55f, 0.20f, 1.0f);
    const ImVec4 Muted = ImVec4(0.60f, 0.62f, 0.68f, 1.0f);
    const ImVec4 Good = ImVec4(0.45f, 0.85f, 0.50f, 1.0f);
    const ImVec4 Bad = ImVec4(1.00f, 0.42f, 0.38f, 1.0f);

    // Current table sort: column 0 = key, 1 = mod, 2 = window; set by clicking the header.
    int sortColumn = 0;
    bool sortAscending = true;

    std::string Lower(std::string s)
    {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }

    // "F2" before "F10": compares digit runs as numbers.
    int NaturalCompare(const std::string& a, const std::string& b)
    {
        size_t i = 0, j = 0;
        while (i < a.size() && j < b.size())
        {
            if (std::isdigit(static_cast<unsigned char>(a[i])) && std::isdigit(static_cast<unsigned char>(b[j])))
            {
                size_t ei = i, ej = j;
                while (ei < a.size() && std::isdigit(static_cast<unsigned char>(a[ei]))) ++ei;
                while (ej < b.size() && std::isdigit(static_cast<unsigned char>(b[ej]))) ++ej;
                const long long na = std::stoll(a.substr(i, ei - i)), nb = std::stoll(b.substr(j, ej - j));
                if (na != nb) return na < nb ? -1 : 1;
                i = ei; j = ej;
                continue;
            }
            const char ca = static_cast<char>(std::tolower(static_cast<unsigned char>(a[i])));
            const char cb = static_cast<char>(std::tolower(static_cast<unsigned char>(b[j])));
            if (ca != cb) return ca < cb ? -1 : 1;
            ++i; ++j;
        }
        return (a.size() - i) == (b.size() - j) ? 0 : (a.size() - i) < (b.size() - j) ? -1 : 1;
    }

    // Key column: by key name, then plain before Ctrl/Alt/Shift.
    int CompareCombo(const HotkeyCombo& a, const HotkeyCombo& b)
    {
        if (const int c = NaturalCompare(a.key, b.key)) return c;
        const int ma = (a.ctrl ? 4 : 0) + (a.alt ? 2 : 0) + (a.shift ? 1 : 0);
        const int mb = (b.ctrl ? 4 : 0) + (b.alt ? 2 : 0) + (b.shift ? 1 : 0);
        return ma == mb ? 0 : ma < mb ? -1 : 1;
    }

    void SortEntries()
    {
        std::stable_sort(entries.begin(), entries.end(), [](const HotkeyEntry& a, const HotkeyEntry& b) {
            // Rows without a hotkey always stay at the bottom when sorting by key.
            if (sortColumn == 0 && a.combo.Empty() != b.combo.Empty()) return !a.combo.Empty();
            int c = 0;
            if (sortColumn == 0) c = CompareCombo(a.combo, b.combo);
            else if (sortColumn == 1) c = NaturalCompare(Lower(a.modName), Lower(b.modName));
            else c = NaturalCompare(Lower(a.app), Lower(b.app));
            if (c == 0) c = NaturalCompare(Lower(a.modName + a.app), Lower(b.modName + b.app));   // stable tie-break
            return sortAscending ? c < 0 : c > 0;
        });
        byCombo.clear();
        for (int i = 0; i < static_cast<int>(entries.size()); ++i)
            if (entries[static_cast<size_t>(i)].Active() && !entries[static_cast<size_t>(i)].combo.Empty())
                byCombo[entries[static_cast<size_t>(i)].combo.Canonical()].push_back(i);
    }

    void Rescan()
    {
        scanError.clear();
        entries = Hotkeys::Scan(settings, scanError);
        SortEntries();
        wishes.clear();
        std::vector<std::pair<std::string, HotkeyCombo>> list;
        Hotkeys::LoadWishes(settings, list);
        for (auto& [key, combo] : list) wishes[key] = combo;
    }

    // Other active entries that use the same combo.
    std::vector<int> ConflictsOf(const HotkeyCombo& combo, int self)
    {
        std::vector<int> result;
        const auto it = byCombo.find(combo.Canonical());
        if (it != byCombo.end())
            for (const int i : it->second) if (i != self) result.push_back(i);
        return result;
    }

    std::string ConflictText(const std::vector<int>& others)
    {
        std::string text;
        for (const int i : others)
        {
            if (!text.empty()) text += ", ";
            const HotkeyEntry& o = entries[static_cast<size_t>(i)];
            text += o.modName + (o.app != o.modName ? " (" + o.app + ")" : "");
        }
        return text;
    }

    const HotkeyCombo* WishFor(const HotkeyEntry& e)
    {
        const auto it = wishes.find(e.WishKey());
        return it != wishes.end() ? &it->second : nullptr;
    }

    void ApplyAndRemount(const HotkeyEntry& entry, const HotkeyCombo& combo, bool rememberWish)
    {
        std::string error;
        if (!Hotkeys::Apply(settings, entry, combo, error))
        {
            const std::string message = error;
            Pipeline::RunAsync(T("Hotkey ändern", "Change hotkey"), [message](std::vector<std::string>& log) {
                log.push_back(std::string(T("FEHLER: ", "ERROR: ")) + message);
                return false;
            });
            return;
        }
        if (rememberWish) Hotkeys::SetWish(settings, entry.WishKey(), &combo);
        const Settings s = settings;
        const std::string folder = entry.mountId, title = entry.modName + ": " + combo.Display();
        Pipeline::RunAsync("Hotkey " + title, [s, folder](std::vector<std::string>& log) {
            log.push_back(T("Manifest geschrieben.", "Manifest written."));
            if (!Hotkeys::GameRunning())
            {
                log.push_back(T("Spiel läuft nicht - neue Taste gilt beim nächsten Start.", "Game is not running - the new key works on the next start."));
                return true;
            }
            Hotkeys::Remount(s, folder, log);
            return true;
        });
        needRescan = true;
    }

    void RestoreAndRemount(const HotkeyEntry& entry)
    {
        std::string error;
        const bool ok = Hotkeys::RestoreOriginal(settings, entry, error);
        Hotkeys::SetWish(settings, entry.WishKey(), nullptr);
        const Settings s = settings;
        const std::string folder = entry.mountId;
        Pipeline::RunAsync("Original: " + entry.modName, [s, folder, ok, error](std::vector<std::string>& log) {
            if (!ok) { log.push_back(std::string(T("FEHLER: ", "ERROR: ")) + error); return false; }
            log.push_back(T("Original-Manifest zurückgeschrieben.", "Original manifest restored."));
            if (Hotkeys::GameRunning()) Hotkeys::Remount(s, folder, log);
            return true;
        });
        needRescan = true;
    }

    void DrawCapturePopup()
    {
        const std::string title = std::string(T("Neue Tastenkombination", "New key combination")) + "###capture";
        if (openPopup) { ImGui::OpenPopup(title.c_str()); openPopup = false; }
        ImGui::SetNextWindowSize(ImVec2(580, 0));
        if (!ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings))
            return;
        if (editIndex < 0 || editIndex >= static_cast<int>(entries.size())) { ImGui::CloseCurrentPopup(); ImGui::EndPopup(); return; }
        const HotkeyEntry& e = entries[static_cast<size_t>(editIndex)];
        const ImGuiIO& io = ImGui::GetIO();

        ImGui::TextColored(Accent, "%s", e.modName.c_str());
        ImGui::SameLine();
        ImGui::TextColored(Muted, "(%s)  -  %s %s", e.app.c_str(), T("bisher:", "current:"), e.combo.Display().c_str());
        ImGui::Spacing();
        ImGui::TextWrapped("%s", T("Halte Strg / Alt / Shift und drück die Taste (F1-F12, A-Z, 0-9, Ziffernblock, Einfg/Entf/Pos1/Ende/Bild). Esc bricht ab.",
                                   "Hold Ctrl / Alt / Shift and press the key (F1-F12, A-Z, 0-9, numpad, Ins/Del/Home/End/PgUp/PgDn). Esc cancels."));

        for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k)
        {
            if (!ImGui::IsKeyPressed(static_cast<ImGuiKey>(k), false)) continue;
            const std::string name = Hotkeys::KeyNameFromImGui(k);
            if (name.empty()) continue;
            captured.key = name;
            captured.ctrl = io.KeyCtrl;
            captured.alt = io.KeyAlt;
            captured.shift = io.KeyShift;
        }

        ImGui::Spacing();
        const std::string live = ModifierText(io.KeyCtrl, io.KeyAlt, io.KeyShift);
        ImGui::SetWindowFontScale(1.8f);
        ImGui::TextColored(captured.Empty() ? Muted : ImVec4(1, 1, 1, 1), "%s",
                           captured.Empty() ? (live.empty() ? "..." : live.c_str()) : captured.Display().c_str());
        ImGui::SetWindowFontScale(1.0f);

        bool canApply = !captured.Empty();
        if (!captured.Empty())
        {
            const std::vector<int> others = ConflictsOf(captured, editIndex);
            if (!others.empty()) ImGui::TextColored(Bad, "%s %s", T("Schon belegt von:", "Already used by:"), ConflictText(others).c_str());
            else ImGui::TextColored(Good, "%s", T("Frei.", "Free."));
            if (const auto game = GameKeys.find(captured.Canonical()); game != GameKeys.end())
                ImGui::TextColored(Accent, "%s %s", T("Achtung:", "Note:"), T(game->second.de, game->second.en));
            if (captured.alt && captured.key == "F4")
            {
                ImGui::TextColored(Bad, "%s", T("Alt+F4 schließt das Spiel - nicht erlaubt.", "Alt+F4 closes the game - not allowed."));
                canApply = false;
            }
            if (!captured.ctrl && !captured.alt && !captured.shift && captured.key.size() == 1)
                ImGui::TextColored(Accent, "%s", T("Achtung: Einzelne Buchstaben nutzt das Spiel oft selbst (z. B. WASD).",
                                                   "Note: the game often uses single letters itself (e.g. WASD)."));
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::BeginDisabled(!canApply);
        if (ImGui::Button(T("Übernehmen", "Apply"), ImVec2(140, 0)))
        {
            ApplyAndRemount(entries[static_cast<size_t>(editIndex)], captured, true);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        Tip("Schreibt die Kombination in die Mod-Datei. Läuft das Spiel, wird der Mod sofort neu geladen.",
            "Writes the combination into the mod's file. If the game is running, the mod is reloaded right away.");
        ImGui::SameLine();
        ImGui::BeginDisabled(e.combo.Empty());
        if (ImGui::Button(T("Taste entfernen", "Remove key"), ImVec2(150, 0)))
        {
            ApplyAndRemount(e, HotkeyCombo(), true);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        Tip("Der Mod bekommt gar keine Taste mehr (z. B. wenn du ihn nur per Befehl öffnest).",
            "The mod gets no key at all (e.g. if you only open it with a command).");
        ImGui::SameLine();
        if (ImGui::Button(T("Abbrechen", "Cancel"), ImVec2(120, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

namespace HotkeysTab
{
    void Refresh() { needRescan = true; }

    void Draw()
    {
        // A finished job or new settings: re-read so the list shows the state on disk.
        if (seenJobs != Pipeline::FinishedJobs()) { seenJobs = Pipeline::FinishedJobs(); needRescan = true; }
        if (needRescan && !ImGui::IsPopupOpen("###capture", ImGuiPopupFlags_AnyPopupId)) { needRescan = false; Rescan(); }

        int conflicts = 0, drifted = 0;
        for (const auto& [combo, list] : byCombo) if (list.size() > 1) conflicts += static_cast<int>(list.size());
        for (const HotkeyEntry& e : entries)
            if (const HotkeyCombo* w = WishFor(e); w && !(*w == e.combo)) ++drifted;

        ImGui::BeginDisabled(Pipeline::Busy());
        if (ImGui::Button(T("Neu einlesen", "Rescan"))) Rescan();
        Tip("Liest die Hotkeys aller installierten Mods neu ein (z. B. nach einer Mod-Installation).",
            "Reads the hotkeys of all installed mods again (e.g. after installing a mod).");
        ImGui::SameLine();
        ImGui::BeginDisabled(drifted == 0);
        if (ImGui::Button((std::string(T("Wünsche erneut anwenden (", "Re-apply my keys (")) + std::to_string(drifted) + ")").c_str()))
        {
            std::vector<std::pair<HotkeyEntry, HotkeyCombo>> todo;
            for (const HotkeyEntry& e : entries)
                if (const HotkeyCombo* w = WishFor(e); w && !(*w == e.combo)) todo.emplace_back(e, *w);
            std::vector<std::string> folders;
            for (const auto& [e, w] : todo)
            {
                std::string error;
                if (Hotkeys::Apply(settings, e, w, error)) folders.push_back(e.mountId);
            }
            const Settings s = settings;
            Pipeline::RunAsync(T("Wünsche anwenden", "Re-apply keys"), [s, folders](std::vector<std::string>& log) {
                log.push_back(std::to_string(folders.size()) + T(" Hotkey(s) wieder eingetragen.", " hotkey(s) written again."));
                if (Hotkeys::GameRunning()) for (const std::string& f : folders) Hotkeys::Remount(s, f, log);
                return true;
            });
            needRescan = true;
        }
        ImGui::EndDisabled();
        Tip("Ein Mod-Update überschreibt seine Datei und damit deine Taste. Dieser Knopf trägt alle deine Tasten wieder ein.",
            "A mod update overwrites its file and with it your key. This button writes all your keys back in.");
        ImGui::EndDisabled();
        ImGui::SameLine(0, 20);
        ImGui::Checkbox(T("Nur Probleme zeigen", "Show problems only"), &onlyProblems);
        Tip("Zeigt nur doppelte Tasten, Spieltasten und von Updates zurückgesetzte Tasten.",
            "Shows only duplicate keys, game keys and keys reset by updates.");
        ImGui::SameLine(0, 24);
        ImGui::TextColored(Muted, "%d %s", static_cast<int>(entries.size()), T("Einträge", "entries"));
        ImGui::SameLine();
        if (conflicts) ImGui::TextColored(Bad, "  %d %s", conflicts, T("doppelt belegt", "duplicates"));
        else ImGui::TextColored(Good, "  %s", T("keine Doppelten", "no duplicates"));
        ImGui::SameLine();
        const bool running = Hotkeys::GameRunning();
        const bool live = running && Hotkeys::CanRemount(settings);
        ImGui::TextColored(live ? Good : Muted, "  %s",
                           live ? T("Spiel läuft: Änderungen gelten sofort", "Game running: changes apply right away")
                                : running ? T("Spiel läuft: Änderungen gelten nach Neustart (kein gamelink)", "Game running: changes apply after a restart (no gamelink)")
                                          : T("Spiel aus: Änderungen gelten beim Start", "Game closed: changes apply on start"));
        Tip("Sofort geht es nur mit dem Zoi-Terminal-Mod (gamelink, siehe Einstellungen). Sonst gilt die neue Taste ab dem nächsten Spielstart.",
            "Instant changes need the Zoi Terminal mod (gamelink, see settings). Otherwise the new key works from the next game start.");
        if (!scanError.empty()) ImGui::TextColored(Bad, "%s", scanError.c_str());

        if (ImGui::BeginTable("hotkeys", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY |
                                                ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Sortable,
                              ImVec2(0, ImGui::GetContentRegionAvail().y)))
        {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn(T("Taste", "Key"), ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_DefaultSort, 170);
            ImGui::TableSetupColumn("Mod", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn(T("Fenster / Auslöser", "Window / trigger"), ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoSort, 1.3f);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort, 200);
            ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
            const char* headerTips[][2] = {
                { "Klick sortiert nach Taste (F2 vor F10), nochmal klicken dreht die Reihenfolge um.", "Click to sort by key (F2 before F10), click again to reverse." },
                { "Klick sortiert nach Mod-Name (A-Z / Z-A).", "Click to sort by mod name (A-Z / Z-A)." },
                { "Fenster (App) des Mods und womit es sich sonst noch öffnet. Klick sortiert.", "The mod's window (app) and what else opens it. Click to sort." },
                { "Doppelt belegt, Spieltaste, von einem Update zurückgesetzt oder geändert.", "Duplicate, game key, reset by an update, or changed." },
                { "", "" },
            };
            for (int c = 0; c < 5; ++c)
            {
                ImGui::TableSetColumnIndex(c);
                ImGui::TableHeader(ImGui::TableGetColumnName(c));
                if (headerTips[c][0][0]) Tip(headerTips[c][0], headerTips[c][1]);
            }

            // Header click: sort by that column, second click reverses (A-Z / Z-A).
            if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs(); specs && specs->SpecsDirty)
            {
                if (specs->SpecsCount > 0)
                {
                    sortColumn = specs->Specs[0].ColumnIndex;
                    sortAscending = specs->Specs[0].SortDirection != ImGuiSortDirection_Descending;
                    SortEntries();
                }
                specs->SpecsDirty = false;
            }

            for (int i = 0; i < static_cast<int>(entries.size()); ++i)
            {
                const HotkeyEntry& e = entries[static_cast<size_t>(i)];
                const std::vector<int> others = e.Active() && !e.combo.Empty() ? ConflictsOf(e.combo, i) : std::vector<int>();
                const HotkeyCombo* wish = WishFor(e);
                const bool drift = wish && !(*wish == e.combo);
                const auto gameKey = GameKeys.find(e.combo.Canonical());
                const bool gameClash = e.Active() && gameKey != GameKeys.end();
                if (onlyProblems && others.empty() && !drift && !gameClash) continue;

                ImGui::PushID(i);
                ImGui::TableNextRow(0, 34);
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(!e.Active() ? Muted : !others.empty() ? Bad : e.combo.Empty() ? Muted : ImVec4(1, 1, 1, 1), "%s",
                                   e.combo.Display().c_str());

                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(e.Active() ? ImVec4(0.9f, 0.9f, 0.9f, 1) : Muted, "%s%s", e.modName.c_str(),
                                   !e.modEnabled ? T("  (Mod aus)", "  (mod off)")
                                   : !e.appEnabled ? T("  (Fenster im Mod abgeschaltet)", "  (window disabled by the mod)") : "");
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", e.manifestPath.c_str());

                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                std::string info = e.app;
                for (const std::string& c : e.commands) info += "  " + c;
                ImGui::TextColored(Muted, "%s", info.c_str());

                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                if (!others.empty()) ImGui::TextColored(Bad, "%s %s", T("doppelt mit", "same as"), ConflictText(others).c_str());
                else if (drift) ImGui::TextColored(Accent, "%s %s", T("Update hat zurückgesetzt:", "reset by an update:"), wish->Display().c_str());
                else if (gameClash) ImGui::TextColored(Accent, "%s", T(gameKey->second.de, gameKey->second.en));
                else if (Hotkeys::HasBackup(settings, e)) ImGui::TextColored(Good, "%s", T("geändert (Original gesichert)", "changed (original backed up)"));
                else if (e.combo.Empty()) ImGui::TextColored(Muted, "%s", T("kein Hotkey", "no hotkey"));
                else ImGui::TextColored(Muted, "ok");

                ImGui::TableNextColumn();
                ImGui::BeginDisabled(Pipeline::Busy());
                if (ImGui::SmallButton(e.combo.Empty() ? T("Hinzufügen", "Add") : T("Ändern", "Change")))
                {
                    editIndex = i;
                    captured = HotkeyCombo();
                    openPopup = true;
                }
                if (e.combo.Empty()) Tip("Gibt diesem Fenster eine eigene Taste.", "Gives this window its own key.");
                else Tip("Neue Tastenkombination festlegen (mit Strg / Alt / Shift).", "Set a new key combination (with Ctrl / Alt / Shift).");
                if (drift)
                {
                    ImGui::SameLine();
                    if (ImGui::SmallButton(T("Anwenden", "Apply"))) ApplyAndRemount(e, *wish, false);
                    Tip("Trägt deine Taste nach dem Mod-Update wieder ein.", "Writes your key back in after the mod update.");
                }
                else if (Hotkeys::HasBackup(settings, e))
                {
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Original")) RestoreAndRemount(e);
                    Tip("Stellt die Original-Taste des Mods wieder her.", "Restores the mod's original key.");
                }
                ImGui::EndDisabled();
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        DrawCapturePopup();
    }
}
