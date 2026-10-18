# inZOI Hotkey Editor (F6 Editor)

**English** · [Deutsch](#deutsch)

A small Windows tool for **inZOI** modders and players:

- **Mod hotkeys** – lists the hotkeys of **all installed UI mods**, shows **duplicates**, and lets you give any mod a new key combination **with Ctrl / Alt / Shift** (e.g. `Shift+F2`). Press the keys, click *Apply*, done.
- **Key guide (F6)** – edit the texts of the in-game key guide and hide rows. The tool builds an override pak for the `~mods` folder.

## Mod hotkeys – how it works

Every UI mod has a plain-text `ui/<app>/uimod_manifest.json` with its `keybind` trigger. This file is **not** part of the mod's checksum list, so it can be changed safely. The editor:

- scans `Documents\inZOI\Mods` (also nested CurseForge folders) and lists key, mod, window and other triggers,
- marks duplicates red – windows that a mod has switched off in `uimod_apps.json` don't count,
- writes the new combination (`"key": "F2", "modifiers": ["Shift"]`), backing up the original first (*Original* button restores it),
- remembers your keys: if a mod update overwrites them, *Re-apply my keys* puts them back,
- **optional:** with the Zoi Terminal mod's `zoi-game.ps1` (gamelink) the mod is reloaded in the running game (`uimod.remount`), so the new key works immediately. Without it, the key works after the next game start.

Only `Ctrl`, `Alt` and `Shift` are allowed as modifiers and the combination must match exactly – `Shift+F2` and `F2` can belong to two different mods.

## Key guide (F6)

Needs (not included – get them from their authors / the inZOI modding community):

| What | Where |
|---|---|
| `retoc.exe` | https://github.com/trumank/retoc/releases |
| `UAssetGUI.exe` | https://github.com/atenfyr/UAssetGUI/releases |
| mapping file `.usmap` matching your game version | inZOI modding community / UE4SS dump |
| the game's AES key | inZOI modding community |

Steps: *Load original from game* → edit texts / untick rows → *Install* → start the game. *Remove override* brings the original back. After an inZOI update just load the original again; your changes are kept.

## Settings

On the first start the app looks for the game (Steam libraries), the mods folder and the tools next to the exe, and opens the **Settings** tab if something is missing. Every path has a `...` button. Language: German / English (follows Windows, switch with the `EN`/`DE` tab). Settings are stored in `f6editor.ini` next to the exe, working files in `data\`.

## Building

Visual Studio 2022 (C++17, v143), `F6Editor.vcxproj`, Release x64. Uses [Dear ImGui](https://github.com/ocornut/imgui) 1.91.9b (DX11 + Win32) and [nlohmann/json](https://github.com/nlohmann/json) 3.11.3, both MIT, included in `external/`.

Command line (no window): `F6Editor.exe --load | --install | --remove | --hotkeys` → log in `data\letzter_lauf.log`.

---

## Deutsch

Ein kleines Windows-Werkzeug für **inZOI**:

- **Mod-Hotkeys** – listet die Tasten **aller installierten UI-Mods**, zeigt **doppelte Belegungen** und gibt jedem Mod per Tastendruck eine neue Kombination **mit Strg / Alt / Shift** (z. B. `Shift+F2`).
- **Tastenhilfe (F6)** – Texte der Tastenhilfe im Spiel ändern und Zeilen ausblenden. Das Werkzeug baut dafür ein Override-Pak für den `~mods`-Ordner.

### Mod-Hotkeys

Jeder UI-Mod hat eine Klartext-Datei `ui/<app>/uimod_manifest.json` mit seinem `keybind`. Sie steht **nicht** in der Prüfsummenliste des Mods und darf geändert werden. Der Editor liest alle Mods in `Dokumente\inZOI\Mods` (auch verschachtelte CurseForge-Ordner), markiert Doppelte rot, sichert vor dem Ändern das Original (Knopf *Original*) und merkt sich deine Tasten – überschreibt ein Mod-Update sie, trägt *Wünsche erneut anwenden* sie wieder ein. Optional lädt er den Mod mit dem `zoi-game.ps1` (gamelink) des Zoi-Terminal-Mods sofort im laufenden Spiel neu; sonst gilt die neue Taste ab dem nächsten Spielstart.

### Tastenhilfe (F6)

Braucht `retoc.exe`, `UAssetGUI.exe`, eine zur Spielversion passende `.usmap` und den AES-Schlüssel des Spiels (nicht enthalten, siehe Tabelle oben). Ablauf: *Original aus Spiel laden* → Texte ändern / Haken entfernen → *Ins Spiel* → Spiel starten. *Override entfernen* stellt das Original wieder her.

### Einstellungen

Beim ersten Start sucht die App Spiel (Steam), Mod-Ordner und Werkzeuge selbst und öffnet die **Einstellungen**, falls etwas fehlt. Jeder Pfad hat einen `...`-Knopf, die Sprache folgt Windows oder wird per `DE`/`EN` umgeschaltet. Überall gibt es Tooltips.

Made by **Schrubbi** (Schrubbdiwupp) · MIT License (see `LICENSE`, third-party notices in `THIRD_PARTY_LICENSES.txt`).

## Download

Ready-to-use exe: see [Releases](https://github.com/Schrubbdiwupp/inzoi-Hotkey-editor/releases). Unzip anywhere, start `F6Editor.exe`. / Fertige exe unter *Releases*: entpacken, `F6Editor.exe` starten.
