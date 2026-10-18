#pragma once
// Settings (f6editor.ini next to the exe) and the tool chain around the widget:
// retoc (IoStore <-> legacy) and UAssetGUI (asset <-> JSON).
// All paths are UTF-8. Long steps run on a worker thread; the UI polls Busy()/Log().
#include <functional>
#include <string>
#include <vector>

struct Settings
{
    std::string dataDir;        // working files (original.json, changes, backups); default: <exe>\data
    std::string gameDir;        // inZOI install folder (contains BlueClient)
    std::string modsDir;        // Documents\inZOI\Mods
    std::string retocExe;
    std::string uassetGuiExe;
    std::string usmapFile;      // mapping file; copied into %LOCALAPPDATA%\UAssetGUI\Mappings
    std::string mapping;        // its name there (file name without .usmap), what UAssetGUI's command line wants
    std::string aesKey;         // 0x + 64 hex digits, or ...
    std::string keyFile;        // ... a text file that contains it
    std::string gamelink;       // optional: zoi-game.ps1 of the Zoi Terminal mod, for live remount of mods
    std::string engine = "UE5_6";
    std::string language;       // "de" / "en" ("" = like Windows)

    std::string OriginalJson() const { return dataDir + "\\original.json"; }
    std::string ChangesJson() const { return dataDir + "\\aenderungen.json"; }
    std::string EnglishTexts() const;   // texte_en.json in the data folder, else next to the exe
    std::string ModDir() const { return gameDir + "\\BlueClient\\Content\\Paks\\~mods\\F6Editor"; }
    std::string PakName() const { return "zzz_F6Editor_P"; }
};

constexpr const char* AssetName = "WBP_HUD_ShoulderViewControlGuide";
constexpr const char* AssetFolder = "BlueClient\\Content\\UI\\HUD\\Components";

Settings& AppSettings();                      // the one shared instance (UI thread)
void LoadSettings();                          // reads f6editor.ini into AppSettings()
bool SaveSettings();
std::string ExeDir();

// Path checks for the settings page: "" = ok, otherwise a short reason.
namespace Check
{
    std::string Game(const Settings& s);
    std::string Mods(const Settings& s);
    std::string Retoc(const Settings& s);
    std::string UAssetGui(const Settings& s);
    std::string Usmap(const Settings& s);
    std::string Key(const Settings& s);
    std::string Data(const Settings& s);
    std::string Gamelink(const Settings& s);   // optional, "" also when empty
    bool KeyGuideReady(const Settings& s);     // everything the key guide tab needs
}

// Fills empty or broken paths with what can be found (Steam libraries, Documents, next to the exe, ...).
// Returns the number of paths it filled in.
int AutoDetect(Settings& s);

namespace Pipeline
{
    // Runs a console program without a window; output lines go to log. Returns the exit code (-1 = start failed).
    int RunTool(const std::string& exe, const std::string& args, std::vector<std::string>& log);

    // Extracts the original widget from the game and converts it to original.json.
    bool ExtractOriginal(const Settings& s, std::vector<std::string>& log);
    // modified.json -> .uasset -> IoStore pak -> ~mods\F6Editor (old files go to backups first).
    bool BuildAndInstall(const Settings& s, const std::string& modifiedJson, std::vector<std::string>& log, bool install = true);
    // Moves the installed override to the backups (game uses the original again).
    bool RemoveOverride(const Settings& s, std::vector<std::string>& log);
    bool OverrideInstalled(const Settings& s);

    // Worker thread: one job at a time.
    void RunAsync(const std::string& title, std::function<bool(std::vector<std::string>&)> job);
    bool Busy();
    std::string CurrentTitle();
    std::vector<std::string> Log();          // copy of the log lines
    int FinishedJobs();                      // increments after each job; UI reloads on change
    bool LastJobOk();
}

// Log lines that start with this are shown red.
inline bool IsErrorLine(const std::string& line) { return line.rfind("FEHLER", 0) == 0 || line.rfind("ERROR", 0) == 0; }
