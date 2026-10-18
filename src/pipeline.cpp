#include "pipeline.h"

#include <windows.h>
#include <shlobj.h>

#include <atomic>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <regex>
#include <sstream>
#include <thread>

#include "i18n.h"

namespace fs = std::filesystem;

namespace
{
    std::wstring Wide(const std::string& s)
    {
        if (s.empty()) return L"";
        const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
        std::wstring w(static_cast<size_t>(n), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
        return w;
    }

    fs::path P(const std::string& utf8) { return fs::u8path(utf8); }

    bool FileExists(const std::string& path)
    {
        std::error_code ec;
        return !path.empty() && fs::is_regular_file(P(path), ec);
    }

    bool DirExists(const std::string& path)
    {
        std::error_code ec;
        return !path.empty() && fs::is_directory(P(path), ec);
    }

    std::string Quote(const std::string& s) { return "\"" + s + "\""; }

    std::string Err(const char* de, const char* en) { return std::string(T("FEHLER: ", "ERROR: ")) + T(de, en); }

    // Removes ANSI colour codes (retoc prints them) and splits into lines.
    void AppendOutput(const std::string& raw, std::vector<std::string>& log)
    {
        static const std::regex ansi("\x1b\\[[0-9;]*[A-Za-z]");
        std::istringstream lines(std::regex_replace(raw, ansi, ""));
        std::string line;
        while (std::getline(lines, line))
        {
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
            if (!line.empty()) log.push_back("   " + line);
        }
    }

    int Run(const std::string& exe, const std::string& args, std::vector<std::string>& log)
    {
        SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };
        HANDLE readPipe = nullptr, writePipe = nullptr;
        if (!CreatePipe(&readPipe, &writePipe, &sa, 0)) return -1;
        SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

        STARTUPINFOW si = { sizeof(si) };
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdOutput = writePipe;
        si.hStdError = writePipe;
        si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        PROCESS_INFORMATION pi = {};
        std::wstring commandLine = Wide(Quote(exe) + " " + args);
        const std::wstring workDir = Wide(P(exe).parent_path().u8string());
        const BOOL started = CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                                            nullptr, workDir.empty() ? nullptr : workDir.c_str(), &si, &pi);
        CloseHandle(writePipe);
        if (!started)
        {
            CloseHandle(readPipe);
            log.push_back(std::string(T("FEHLER: Start fehlgeschlagen: ", "ERROR: could not start ")) + exe);
            return -1;
        }

        std::string output;
        char buffer[4096];
        DWORD read = 0;
        while (ReadFile(readPipe, buffer, sizeof(buffer), &read, nullptr) && read > 0) output.append(buffer, read);
        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD code = 0;
        GetExitCodeProcess(pi.hProcess, &code);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        CloseHandle(readPipe);
        AppendOutput(output, log);
        return static_cast<int>(code);
    }

    std::string KeyFromText(const std::string& text)
    {
        std::smatch m;
        if (std::regex_search(text, m, std::regex("0x[0-9A-Fa-f]{64}"))) return m.str();
        return "";
    }

    std::string ReadTextFile(const std::string& path)
    {
        std::ifstream in(P(path), std::ios::binary);
        std::stringstream text;
        text << in.rdbuf();
        return text.str();
    }

    std::string AesKey(const Settings& s)
    {
        if (const std::string key = KeyFromText(s.aesKey); !key.empty()) return key;
        if (FileExists(s.keyFile)) return KeyFromText(ReadTextFile(s.keyFile));
        return "";
    }

    std::string LocalAppData()
    {
        wchar_t buffer[MAX_PATH] = L"";
        GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH);
        return fs::path(buffer).u8string();
    }

    std::string MappingsFolder() { return LocalAppData() + "\\UAssetGUI\\Mappings"; }

    // UAssetGUI's command line only takes the name of a mapping in its own Mappings folder.
    bool InstallMapping(const Settings& s, std::string& name, std::vector<std::string>& log)
    {
        name = P(s.usmapFile).stem().u8string();
        const fs::path target = P(MappingsFolder()) / P(s.usmapFile).filename();
        std::error_code ec;
        if (fs::exists(target, ec)) return true;
        fs::create_directories(target.parent_path(), ec);
        fs::copy_file(P(s.usmapFile), target, ec);
        if (ec)
        {
            log.push_back(Err("Mapping konnte nicht nach UAssetGUI kopiert werden: ", "could not copy the mapping to UAssetGUI: ") + ec.message());
            return false;
        }
        log.push_back(std::string(T("Mapping für UAssetGUI eingerichtet: ", "Mapping installed for UAssetGUI: ")) + target.u8string());
        return true;
    }

    std::string Timestamp()
    {
        const std::time_t now = std::time(nullptr);
        std::tm local = {};
        localtime_s(&local, &now);
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d_%H%M%S", &local);
        return buf;
    }

    bool BackupInstalled(const Settings& s, std::vector<std::string>& log)
    {
        std::error_code ec;
        if (!fs::exists(P(s.ModDir()), ec) || fs::is_empty(P(s.ModDir()), ec)) return true;
        const fs::path target = P(s.dataDir) / "backups" / Timestamp();
        fs::create_directories(target, ec);
        for (const auto& entry : fs::directory_iterator(P(s.ModDir()), ec))
        {
            fs::rename(entry.path(), target / entry.path().filename(), ec);
            if (ec)
            {
                log.push_back(std::string(T("FEHLER: Kann alte Datei nicht sichern (Spiel offen?): ",
                                            "ERROR: cannot back up the old file (game running?): ")) +
                              entry.path().u8string() + " - " + ec.message());
                return false;
            }
        }
        log.push_back(std::string(T("Alte Version gesichert: ", "Old version backed up: ")) + target.u8string());
        return true;
    }

    bool NewerThan(const fs::path& file, fs::file_time_type since)
    {
        std::error_code ec;
        return fs::exists(file, ec) && fs::last_write_time(file, ec) >= since;
    }

    // Steam: install path from the registry, libraries from libraryfolders.vdf.
    std::vector<std::string> SteamLibraries()
    {
        std::vector<std::string> result;
        wchar_t steam[MAX_PATH] = L"";
        DWORD size = sizeof(steam);
        if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath", RRF_RT_REG_SZ, nullptr, steam, &size) != ERROR_SUCCESS)
            return result;
        const std::string steamPath = fs::path(steam).make_preferred().u8string();
        result.push_back(steamPath);
        const std::string vdf = ReadTextFile(steamPath + "\\steamapps\\libraryfolders.vdf");
        static const std::regex pathLine("\"path\"\\s+\"([^\"]+)\"");
        for (auto it = std::sregex_iterator(vdf.begin(), vdf.end(), pathLine); it != std::sregex_iterator(); ++it)
            result.push_back(std::regex_replace((*it)[1].str(), std::regex("\\\\\\\\"), "\\"));
        return result;
    }

    // Looks for a file by name next to the exe, in its sub folders (2 levels) and one level up.
    std::string FindNearExe(const std::string& fileName)
    {
        const fs::path exeDir = P(ExeDir());
        std::error_code ec;
        for (const fs::path& start : { exeDir, exeDir.parent_path() })
        {
            if (fs::exists(start / fileName, ec)) return (start / fileName).u8string();
            for (auto it = fs::recursive_directory_iterator(start, fs::directory_options::skip_permission_denied, ec);
                 it != fs::recursive_directory_iterator(); it.increment(ec))
            {
                if (ec) break;
                if (it.depth() > 2) { it.disable_recursion_pending(); continue; }
                if (it->is_regular_file(ec) && _stricmp(it->path().filename().u8string().c_str(), fileName.c_str()) == 0)
                    return it->path().u8string();
            }
        }
        return "";
    }

    Settings appSettings;

    std::mutex mutex;
    std::vector<std::string> logLines;
    std::string currentTitle;
    std::atomic<bool> busy = false;
    std::atomic<int> finished = 0;
    std::atomic<bool> lastOk = true;
}

std::string ExeDir()
{
    wchar_t exePath[MAX_PATH] = L"";
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    return fs::path(exePath).parent_path().u8string();
}

std::string Settings::EnglishTexts() const
{
    const std::string inData = dataDir + "\\texte_en.json";
    return FileExists(inData) ? inData : ExeDir() + "\\texte_en.json";
}

Settings& AppSettings() { return appSettings; }

void LoadSettings()
{
    Settings& s = appSettings;
    std::ifstream in(P(ExeDir() + "\\f6editor.ini"));
    std::string line, toolsDir;
    while (in && std::getline(in, line))
    {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t eq = line.find('=');
        if (line.empty() || line[0] == ';' || eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq), value = line.substr(eq + 1);
        if (key == "data" || key == "daten") s.dataDir = value;
        else if (key == "game" || key == "spiel") s.gameDir = value;
        else if (key == "mods") s.modsDir = value;
        else if (key == "retoc") s.retocExe = value;
        else if (key == "uassetgui") s.uassetGuiExe = value;
        else if (key == "werkzeuge") toolsDir = value;   // first version: one folder for both tools
        else if (key == "usmap") s.usmapFile = value;
        else if (key == "mapping") s.mapping = value;
        else if (key == "aes_key") s.aesKey = value;
        else if (key == "key_file" || key == "schluessel") s.keyFile = value;
        else if (key == "gamelink") s.gamelink = value;
        else if (key == "engine") s.engine = value;
        else if (key == "language") s.language = value;
    }
    if (!toolsDir.empty())
    {
        if (s.retocExe.empty()) s.retocExe = toolsDir + "\\retoc.exe";
        if (s.uassetGuiExe.empty()) s.uassetGuiExe = toolsDir + "\\UAssetGUI.exe";
    }
    if (s.usmapFile.empty() && !s.mapping.empty()) s.usmapFile = MappingsFolder() + "\\" + s.mapping + ".usmap";
    if (s.dataDir.empty()) s.dataDir = ExeDir() + "\\data";
    if (s.modsDir.empty())
    {
        PWSTR documents = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &documents)))
            s.modsDir = (fs::path(documents) / "inZOI" / "Mods").u8string();
        CoTaskMemFree(documents);
    }
    I18n::SetEnglish(s.language.empty() ? !I18n::SystemIsGerman() : s.language == "en");
}

bool SaveSettings()
{
    Settings& s = appSettings;
    if (!s.usmapFile.empty()) s.mapping = P(s.usmapFile).stem().u8string();
    std::ofstream out(P(ExeDir() + "\\f6editor.ini"), std::ios::trunc);
    if (!out) return false;
    out << "; F6 Editor settings (UTF-8) - easier to change on the settings page of the app\n"
        << "language=" << s.language << "\n"
        << "data=" << s.dataDir << "\n"
        << "game=" << s.gameDir << "\n"
        << "mods=" << s.modsDir << "\n"
        << "retoc=" << s.retocExe << "\n"
        << "uassetgui=" << s.uassetGuiExe << "\n"
        << "usmap=" << s.usmapFile << "\n"
        << "mapping=" << s.mapping << "\n"
        << "aes_key=" << s.aesKey << "\n"
        << "key_file=" << s.keyFile << "\n"
        << "gamelink=" << s.gamelink << "\n"
        << "engine=" << s.engine << "\n";
    return static_cast<bool>(out);
}

namespace Check
{
    std::string Game(const Settings& s)
    {
        if (s.gameDir.empty()) return T("nicht gesetzt", "not set");
        if (!FileExists(s.gameDir + "\\BlueClient\\Content\\Paks\\global.utoc"))
            return T("kein inZOI-Ordner (BlueClient\\Content\\Paks fehlt)", "not an inZOI folder (BlueClient\\Content\\Paks missing)");
        return "";
    }
    std::string Mods(const Settings& s)
    {
        if (!DirExists(s.modsDir)) return T("Ordner fehlt", "folder missing");
        return "";
    }
    std::string Retoc(const Settings& s)
    {
        if (!FileExists(s.retocExe)) return T("retoc.exe nicht gefunden", "retoc.exe not found");
        return "";
    }
    std::string UAssetGui(const Settings& s)
    {
        if (!FileExists(s.uassetGuiExe)) return T("UAssetGUI.exe nicht gefunden", "UAssetGUI.exe not found");
        return "";
    }
    std::string Usmap(const Settings& s)
    {
        if (!FileExists(s.usmapFile)) return T("Mapping-Datei (.usmap) fehlt", "mapping file (.usmap) missing");
        return "";
    }
    std::string Key(const Settings& s)
    {
        if (!AesKey(s).empty()) return "";
        if (!s.aesKey.empty()) return T("Schlüssel muss 0x + 64 Hex-Zeichen sein", "key must be 0x + 64 hex digits");
        if (!s.keyFile.empty()) return T("in der Datei steht kein Schlüssel (0x + 64 Hex-Zeichen)", "no key (0x + 64 hex digits) in that file");
        return T("nicht gesetzt", "not set");
    }
    std::string Data(const Settings& s)
    {
        std::error_code ec;
        fs::create_directories(P(s.dataDir), ec);
        if (!DirExists(s.dataDir)) return T("Ordner kann nicht angelegt werden", "folder cannot be created");
        return "";
    }
    std::string Gamelink(const Settings& s)
    {
        if (s.gamelink.empty() || FileExists(s.gamelink)) return "";
        return T("Datei nicht gefunden", "file not found");
    }
    bool KeyGuideReady(const Settings& s)
    {
        return Game(s).empty() && Retoc(s).empty() && UAssetGui(s).empty() && Usmap(s).empty() && Key(s).empty() && Data(s).empty();
    }
}

int AutoDetect(Settings& s)
{
    int found = 0;
    if (!Check::Game(s).empty())
        for (const std::string& lib : SteamLibraries())
        {
            const std::string candidate = lib + "\\steamapps\\common\\inZOI";
            if (FileExists(candidate + "\\BlueClient\\Content\\Paks\\global.utoc")) { s.gameDir = candidate; ++found; break; }
        }
    if (!Check::Mods(s).empty())
    {
        PWSTR documents = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &documents)))
        {
            const std::string candidate = (fs::path(documents) / "inZOI" / "Mods").u8string();
            if (DirExists(candidate)) { s.modsDir = candidate; ++found; }
        }
        CoTaskMemFree(documents);
    }
    if (!Check::Retoc(s).empty())
        if (std::string f = FindNearExe("retoc.exe"); !f.empty()) { s.retocExe = f; ++found; }
    if (!Check::UAssetGui(s).empty())
        if (std::string f = FindNearExe("UAssetGUI.exe"); !f.empty()) { s.uassetGuiExe = f; ++found; }
    if (!Check::Usmap(s).empty())
    {
        // Already installed in UAssetGUI, or lying next to one of the tools.
        std::error_code ec;
        std::vector<fs::path> folders = { P(MappingsFolder()) };
        if (FileExists(s.uassetGuiExe)) folders.push_back(P(s.uassetGuiExe).parent_path());
        if (FileExists(s.retocExe)) folders.push_back(P(s.retocExe).parent_path());
        for (const fs::path& folder : folders)
        {
            for (const auto& entry : fs::directory_iterator(folder, ec))
            {
                const std::string name = entry.path().filename().u8string();
                if (entry.path().extension() == ".usmap" && name.find("BlueClient") != std::string::npos)
                {
                    s.usmapFile = entry.path().u8string();
                    ++found;
                    break;
                }
            }
            if (Check::Usmap(s).empty()) break;
        }
    }
    return found;
}

namespace Pipeline
{
    int RunTool(const std::string& exe, const std::string& args, std::vector<std::string>& log) { return Run(exe, args, log); }

    bool ExtractOriginal(const Settings& s, std::vector<std::string>& log)
    {
        const std::string key = AesKey(s);
        if (key.empty()) { log.push_back(Err("Kein AES-Schlüssel (Einstellungen).", "no AES key (settings).")); return false; }
        std::string mapping;
        if (!InstallMapping(s, mapping, log)) return false;
        const std::string outDir = s.dataDir + "\\original";
        std::error_code ec;
        fs::create_directories(P(s.dataDir), ec);
        fs::remove_all(P(outDir), ec);

        log.push_back(T("1/2 Widget aus den Spieldateien holen (retoc) ...", "1/2 Extracting the widget from the game files (retoc) ..."));
        // No trailing backslash: before the closing quote it would escape it.
        const std::string paks = s.gameDir + "\\BlueClient\\Content\\Paks";
        Run(s.retocExe, "-a " + key + " to-legacy --version " + s.engine + " --no-shaders --no-script-objects -f " + AssetName + " " +
            Quote(paks) + " " + Quote(outDir), log);
        const std::string uasset = outDir + "\\" + AssetFolder + "\\" + AssetName + ".uasset";
        if (!fs::exists(P(uasset), ec)) { log.push_back(Err("Widget wurde nicht extrahiert.", "widget was not extracted.")); return false; }

        log.push_back(T("2/2 In JSON umwandeln (UAssetGUI) ...", "2/2 Converting to JSON (UAssetGUI) ..."));
        const auto start = fs::file_time_type::clock::now() - std::chrono::seconds(2);
        Run(s.uassetGuiExe, "tojson " + Quote(uasset) + " " + Quote(s.OriginalJson()) + " VER_" + s.engine + " " + Quote(mapping), log);
        if (!NewerThan(P(s.OriginalJson()), start)) { log.push_back(Err("UAssetGUI hat kein JSON geschrieben.", "UAssetGUI wrote no JSON.")); return false; }

        // With a wrong mapping UAssetGUI only produces RawExport entries.
        if (ReadTextFile(s.OriginalJson()).find("NormalExport") == std::string::npos)
        {
            log.push_back(Err("Nur Rohdaten (RawExport) - die Mapping-Datei passt nicht zur Spielversion.",
                              "raw data only (RawExport) - the mapping file does not match the game version."));
            return false;
        }
        log.push_back(T("Original geladen.", "Original loaded."));
        return true;
    }

    bool BuildAndInstall(const Settings& s, const std::string& modifiedJson, std::vector<std::string>& log, bool install)
    {
        std::string mapping;
        if (!InstallMapping(s, mapping, log)) return false;
        std::error_code ec;
        const std::string staging = s.dataDir + "\\staging";
        const std::string outDir = s.dataDir + "\\out";
        fs::remove_all(P(staging), ec);
        fs::remove_all(P(outDir), ec);
        fs::create_directories(P(staging + "\\" + AssetFolder), ec);
        fs::create_directories(P(outDir), ec);

        log.push_back(T("1/3 JSON in Asset umwandeln (UAssetGUI) ...", "1/3 Converting JSON to asset (UAssetGUI) ..."));
        const std::string uasset = staging + "\\" + AssetFolder + "\\" + AssetName + ".uasset";
        Run(s.uassetGuiExe, "fromjson " + Quote(modifiedJson) + " " + Quote(uasset) + " " + Quote(mapping), log);
        if (!fs::exists(P(uasset), ec)) { log.push_back(Err("Asset wurde nicht geschrieben.", "asset was not written.")); return false; }

        log.push_back(T("2/3 Mod-Pak bauen (retoc) ...", "2/3 Building the mod pak (retoc) ..."));
        const std::string utoc = outDir + "\\" + s.PakName() + ".utoc";
        Run(s.retocExe, "to-zen --version " + s.engine + " " + Quote(staging) + " " + Quote(utoc), log);
        for (const char* ext : { ".utoc", ".ucas", ".pak" })
            if (!fs::exists(P(outDir + "\\" + s.PakName() + ext), ec))
            {
                log.push_back(Err("Pak-Datei fehlt: ", "pak file missing: ") + ext);
                return false;
            }

        if (!install)
        {
            log.push_back(std::string(T("Gebaut (nicht installiert): ", "Built (not installed): ")) + outDir);
            return true;
        }
        log.push_back(T("3/3 Ins Spiel legen ...", "3/3 Installing into the game ..."));
        if (!BackupInstalled(s, log)) return false;
        fs::create_directories(P(s.ModDir()), ec);
        for (const char* ext : { ".utoc", ".ucas", ".pak" })
        {
            fs::copy_file(P(outDir + "\\" + s.PakName() + ext), P(s.ModDir() + "\\" + s.PakName() + ext),
                          fs::copy_options::overwrite_existing, ec);
            if (ec) { log.push_back(Err("Kopieren fehlgeschlagen: ", "copy failed: ") + ec.message()); return false; }
        }
        log.push_back(std::string(T("Fertig: ", "Done: ")) + s.ModDir() +
                      T(" - wirkt nach dem nächsten Spielstart.", " - takes effect on the next game start."));
        return true;
    }

    bool RemoveOverride(const Settings& s, std::vector<std::string>& log)
    {
        if (!OverrideInstalled(s)) { log.push_back(T("Kein Override installiert.", "No override installed.")); return true; }
        if (!BackupInstalled(s, log)) return false;
        log.push_back(T("Override entfernt - nach dem nächsten Spielstart gilt wieder das Original.",
                        "Override removed - the original is back after the next game start."));
        return true;
    }

    bool OverrideInstalled(const Settings& s)
    {
        return FileExists(s.ModDir() + "\\" + s.PakName() + ".utoc");
    }

    void RunAsync(const std::string& title, std::function<bool(std::vector<std::string>&)> job)
    {
        if (busy.exchange(true)) return;
        {
            std::lock_guard<std::mutex> lock(mutex);
            currentTitle = title;
            logLines.push_back("== " + title);
        }
        std::thread([job]() {
            std::vector<std::string> lines;
            bool ok = false;
            try { ok = job(lines); }
            catch (const std::exception& e) { lines.push_back(std::string(T("FEHLER: ", "ERROR: ")) + e.what()); }
            {
                std::lock_guard<std::mutex> lock(mutex);
                logLines.insert(logLines.end(), lines.begin(), lines.end());
                if (logLines.size() > 400) logLines.erase(logLines.begin(), logLines.end() - 400);
                currentTitle.clear();
            }
            lastOk = ok;
            ++finished;
            busy = false;
        }).detach();
    }

    bool Busy() { return busy; }
    std::string CurrentTitle() { std::lock_guard<std::mutex> lock(mutex); return currentTitle; }
    std::vector<std::string> Log() { std::lock_guard<std::mutex> lock(mutex); return logLines; }
    int FinishedJobs() { return finished; }
    bool LastJobOk() { return lastOk; }
}
