#pragma once
// Hotkeys of all installed UI mods: read from ui/<app>/uimod_manifest.json (plain text, not part of the
// mod's checksum list), change them there and remount the mod live via gamelink.
#include <string>
#include <vector>

#include "pipeline.h"

struct HotkeyCombo
{
    std::string key;                       // inZOI/UE key name: "F2", "A", "One", "NumPadOne", ...
    bool ctrl = false, alt = false, shift = false;

    bool Empty() const { return key.empty(); }
    std::string Canonical() const;         // "Ctrl+Alt+Shift+F2" (fixed order, for comparing)
    std::string Display() const;           // "Strg+Alt+Shift+F2" with German key names
    bool operator==(const HotkeyCombo& o) const { return Canonical() == o.Canonical(); }
};

struct HotkeyEntry
{
    std::string modFolder;                 // plugin folder, e.g. "Liveslider_B0D3B2FB" (= mod_id for uimod.remount)
    std::string modName;                   // FriendlyName from mod_manifest.json
    std::string app;                       // folder below ui/
    std::string manifestPath;
    int triggerIndex = -1;                 // index in "triggers" (-1 = app has no keybind yet)
    std::string triggerId;
    HotkeyCombo combo;
    std::vector<std::string> commands;     // other triggers of the same app, readable ("/cmd", "immer an", ...)
    bool modEnabled = true;
    bool appEnabled = true;                // ui/uimod_apps.json "enabled" (a disabled app never reacts to its key)
    std::string mountId;                   // path below the Mods folder, e.g. "88849/1683442_9202268/LifeController_854E2A05";
                                           // uimod.remount needs it for nested (CurseForge) folders

    bool Active() const { return modEnabled && appEnabled; }

    std::string WishKey() const { return modFolder + "/" + app + "/" + (triggerId.empty() ? "keybind_hub" : triggerId); }
};

namespace Hotkeys
{
    std::vector<HotkeyEntry> Scan(const Settings& s, std::string& error);

    // Writes the combo into the manifest (first change backs up the original). Empty combo = remove the keybind.
    bool Apply(const Settings& s, const HotkeyEntry& entry, const HotkeyCombo& combo, std::string& error);
    bool RestoreOriginal(const Settings& s, const HotkeyEntry& entry, std::string& error);
    bool HasBackup(const Settings& s, const HotkeyEntry& entry);

    // Wishes survive mod updates: hotkeys.json in the data folder.
    bool LoadWishes(const Settings& s, std::vector<std::pair<std::string, HotkeyCombo>>& wishes);
    void SetWish(const Settings& s, const std::string& wishKey, const HotkeyCombo* combo);   // nullptr = forget

    bool GameRunning();
    bool CanRemount(const Settings& s);   // gamelink set and present
    // uimod.remount via gamelink (zoi-game.ps1); runs synchronously, appends output to log.
    bool Remount(const Settings& s, const std::string& mountId, std::vector<std::string>& log);

    // Key capture: ImGui key -> UE key name ("" = not allowed as hotkey).
    std::string KeyNameFromImGui(int imguiKey);
}

namespace HotkeysTab
{
    void Refresh();   // re-read on the next frame (after settings or language changed)
    void Draw();
}
