#include "widget_asset.h"

#include "i18n.h"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <functional>

namespace
{
    const std::map<std::string, std::pair<const char*, const char*>> PageTitles = {
        { "BW_VerticalBox", { "Schulteransicht", "Shoulder view" } },
        { "BW_VerticalBox_Drive", { "Fahrzeug", "Vehicle" } },
        { "BW_VerticalBox_Active", { "Aktivmodus", "Active mode" } },
        { "BW_VerticalBox_TopView", { "Draufsicht", "Top view" } },
        { "BW_VerticalBox_Dive", { "Tauchen", "Diving" } },
        { "BW_VerticalBox_Surfing", { "Surfen", "Surfing" } },
    };

    bool EqualsNoCase(const std::string& a, const std::string& b)
    {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i)
            if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
        return true;
    }

    bool ReadJsonFile(const std::string& path, Json& out, std::string& error)
    {
        std::ifstream in(std::filesystem::u8path(path), std::ios::binary);
        if (!in) { error = std::string(T("Datei fehlt: ", "file missing: ")) + path; return false; }
        try { in >> out; }
        catch (const std::exception& e) { error = std::string(T("JSON-Fehler in ", "JSON error in ")) + path + ": " + e.what(); return false; }
        return true;
    }

    // Read-only helpers on the UAssetGUI JSON.
    struct View
    {
        const Json& asset;

        const Json& Export(int index) const { return asset["Exports"][static_cast<size_t>(index - 1)]; }

        std::string ClassName(const Json& e) const
        {
            const int c = e.value("ClassIndex", 0);
            if (c < 0) return asset["Imports"][static_cast<size_t>(-c - 1)].value("ObjectName", "");
            if (c > 0) return Export(c).value("ObjectName", "");
            return "";
        }

        static const Json* Prop(const Json& e, const std::string& name)
        {
            if (!e.contains("Data") || !e["Data"].is_array()) return nullptr;
            for (const Json& p : e["Data"])
                if (p.contains("Name") && p["Name"].is_string() && EqualsNoCase(p["Name"].get<std::string>(), name)) return &p;
            return nullptr;
        }

        std::vector<int> Children(const Json& e) const
        {
            std::vector<int> result;
            const Json* slots = Prop(e, "Slots");
            if (!slots || !(*slots)["Value"].is_array()) return result;
            for (const Json& s : (*slots)["Value"])
            {
                const int slotIndex = s.value("Value", 0);
                if (slotIndex <= 0) continue;
                const Json* content = Prop(Export(slotIndex), "Content");
                if (content && (*content)["Value"].is_number_integer() && (*content)["Value"].get<int>() > 0)
                    result.push_back((*content)["Value"].get<int>());
            }
            return result;
        }

        int FindByClass(const std::string& cls) const
        {
            const Json& exports = asset["Exports"];
            for (size_t i = 0; i < exports.size(); ++i)
                if (ClassName(exports[i]) == cls) return static_cast<int>(i + 1);
            return 0;
        }
    };

    std::string ReadableMapping(std::string mapping)
    {
        // "ShoulderViewMode_CharacterMoveLeftAction" -> "CharacterMoveLeft"
        const size_t underscore = mapping.find('_');
        if (underscore != std::string::npos) mapping = mapping.substr(underscore + 1);
        if (mapping.size() > 6 && mapping.compare(mapping.size() - 6, 6, "Action") == 0) mapping.resize(mapping.size() - 6);
        return mapping;
    }

    // Literal text of a TextProperty, or "" if it comes from a string table.
    std::string LiteralText(const Json& textProp)
    {
        if (textProp.value("HistoryType", "") == "StringTableEntry") return "";
        if (textProp.contains("CultureInvariantString") && textProp["CultureInvariantString"].is_string())
            return textProp["CultureInvariantString"].get<std::string>();
        if (textProp.contains("SourceValue") && textProp["SourceValue"].is_string()) return textProp["SourceValue"].get<std::string>();
        if (textProp.contains("Value") && textProp["Value"].is_string()) return textProp["Value"].get<std::string>();
        return "";
    }

    Json VisibilityProperty(const std::string& value)
    {
        return Json{
            { "$type", "UAssetAPI.PropertyTypes.Objects.EnumPropertyData, UAssetAPI" },
            { "EnumType", "ESlateVisibility" },
            { "InnerType", "ByteProperty" },
            { "Name", "Visibility" },
            { "ArrayIndex", 0 },
            { "PropertyGuid", nullptr },
            { "IsZero", false },
            { "PropertyTagFlags", "None" },
            { "PropertyTypeName", nullptr },
            { "PropertyTagExtensions", "NoExtension" },
            { "Value", value },
        };
    }

    Json* MutableProp(Json& e, const std::string& name)
    {
        if (!e.contains("Data") || !e["Data"].is_array()) return nullptr;
        for (Json& p : e["Data"])
            if (p.contains("Name") && p["Name"].is_string() && EqualsNoCase(p["Name"].get<std::string>(), name)) return &p;
        return nullptr;
    }
}

bool WidgetAsset::Load(const std::string& jsonPath, const std::string& englishTextsPath, std::string& error)
{
    pages.clear();
    englishTexts.clear();
    if (!ReadJsonFile(jsonPath, asset, error)) return false;

    Json texts;
    std::string ignored;
    if (ReadJsonFile(englishTextsPath, texts, ignored) && texts.is_object())
        for (auto it = texts.begin(); it != texts.end(); ++it)
            if (it.value().is_string()) englishTexts[it.key()] = it.value().get<std::string>();

    if (!asset.contains("Exports") || !asset["Exports"].is_array()) { error = T("Keine Exports im JSON.", "No exports in the JSON."); return false; }
    const View view{ asset };

    const int switcher = view.FindByClass("B1WidgetSwitcher");
    if (!switcher) { error = T("WidgetSwitcher nicht gefunden - falsches Asset?", "WidgetSwitcher not found - wrong asset?"); return false; }

    for (const int pageIndex : view.Children(view.Export(switcher)))
    {
        GuidePage page;
        page.id = view.Export(pageIndex).value("ObjectName", "");
        const auto title = PageTitles.find(page.id);
        page.titleDe = title != PageTitles.end() ? title->second.first : page.id;
        page.titleEn = title != PageTitles.end() ? title->second.second : page.id;

        // The rows live in a VerticalBox named Panel_Guide* somewhere below the page.
        int panel = 0;
        std::function<void(int)> findPanel = [&](int index) {
            if (panel) return;
            const Json& e = view.Export(index);
            if (e.value("ObjectName", "").rfind("Panel_Guide", 0) == 0) { panel = index; return; }
            for (const int child : view.Children(e)) findPanel(child);
        };
        findPanel(pageIndex);
        if (!panel) continue;

        for (const int rowIndex : view.Children(view.Export(panel)))
        {
            const Json& box = view.Export(rowIndex);
            if (view.ClassName(box) != "B1HorizontalBox") continue;

            GuideRow row;
            row.id = box.value("ObjectName", "");
            row.boxExport = rowIndex;
            if (const Json* vis = View::Prop(box, "Visibility"))
            {
                const std::string v = (*vis).value("Value", "");
                row.originallyVisible = v != "Collapsed" && v != "Hidden";
            }

            std::function<void(int)> walk = [&](int index) {
                const Json& e = view.Export(index);
                const std::string cls = view.ClassName(e);
                if (cls.find("ActionKeyWidget") != std::string::npos)
                {
                    std::string key;
                    if (const Json* mapping = View::Prop(e, "MappingName"); mapping && (*mapping)["Value"].is_string())
                        key = ReadableMapping((*mapping)["Value"].get<std::string>());
                    else if (const Json* direct = View::Prop(e, "DirectKey"); direct && (*direct)["Value"].is_array())
                        for (const Json& p : (*direct)["Value"])
                            if (p.value("Name", "") == "KeyName" && p["Value"].is_string()) key = "[" + p["Value"].get<std::string>() + "]";
                    if (key.empty()) key = "?";
                    if (!row.keys.empty() && row.keys.back() != ' ') row.keys += "  ";
                    row.keys += key;
                }
                else if (cls == "B1TextBlock")
                {
                    if (const Json* text = View::Prop(e, "Text"))
                    {
                        const std::string literal = LiteralText(*text);
                        if (literal == "+") { row.keys += " + "; }
                        else
                        {
                            row.textExport = index;
                            if ((*text).value("HistoryType", "") == "StringTableEntry" && (*text)["Value"].is_string())
                            {
                                row.textKey = (*text)["Value"].get<std::string>();
                                const auto en = englishTexts.find(row.textKey);
                                row.originalText = en != englishTexts.end() ? en->second : row.textKey;
                            }
                            else
                                row.originalText = literal;
                        }
                    }
                }
                for (const int child : view.Children(e)) walk(child);
            };
            for (const int child : view.Children(box)) walk(child);
            page.rows.push_back(row);
        }
        pages.push_back(page);
    }
    if (pages.empty()) { error = T("Keine Seiten gefunden.", "No pages found."); return false; }
    return true;
}

int WidgetAsset::WriteModified(const ChangeMap& changes, const std::string& outPath, std::string& error) const
{
    Json copy = asset;
    int changed = 0;
    for (const GuidePage& page : pages)
        for (const GuideRow& row : page.rows)
        {
            const auto change = changes.find(row.id);
            if (change == changes.end() || change->second.Empty()) continue;
            bool any = false;

            if (!change->second.text.empty() && row.textExport)
            {
                Json& textBlock = copy["Exports"][static_cast<size_t>(row.textExport - 1)];
                if (Json* text = MutableProp(textBlock, "Text"))
                {
                    (*text)["HistoryType"] = "None";
                    (*text)["TableId"] = nullptr;
                    (*text)["Value"] = nullptr;
                    (*text)["CultureInvariantString"] = change->second.text;
                    any = true;
                }
            }

            if (change->second.overrideVisibility && change->second.visible != row.originallyVisible)
            {
                Json& box = copy["Exports"][static_cast<size_t>(row.boxExport - 1)];
                const std::string value = change->second.visible ? "Visible" : "Collapsed";
                if (Json* vis = MutableProp(box, "Visibility")) (*vis)["Value"] = value;
                else box["Data"].push_back(VisibilityProperty(value));   // Visibility is the last UWidget property, so appending keeps the order
                any = true;
            }
            if (any) ++changed;
        }

    std::ofstream out(std::filesystem::u8path(outPath), std::ios::binary | std::ios::trunc);
    if (!out) { error = std::string(T("Kann nicht schreiben: ", "cannot write: ")) + outPath; return -1; }
    out << copy.dump(2);
    return changed;
}

bool LoadChanges(const std::string& path, ChangeMap& changes)
{
    changes.clear();
    Json data;
    std::string error;
    if (!ReadJsonFile(path, data, error) || !data.contains("zeilen")) return false;
    for (auto it = data["zeilen"].begin(); it != data["zeilen"].end(); ++it)
    {
        RowChange change;
        change.text = it.value().value("text", "");
        if (it.value().contains("anzeigen"))
        {
            change.overrideVisibility = true;
            change.visible = it.value()["anzeigen"].get<bool>();
        }
        changes[it.key()] = change;
    }
    return true;
}

bool SaveChanges(const std::string& path, const ChangeMap& changes)
{
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::u8path(path).parent_path(), ec);
    Json data;
    data["zeilen"] = Json::object();
    for (const auto& [id, change] : changes)
    {
        if (change.Empty()) continue;
        Json row = Json::object();
        if (!change.text.empty()) row["text"] = change.text;
        if (change.overrideVisibility) row["anzeigen"] = change.visible;
        data["zeilen"][id] = row;
    }
    std::ofstream out(std::filesystem::u8path(path), std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << data.dump(2);
    return true;
}
