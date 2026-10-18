#include "widget_asset.h"

#include "hotkeys.h"
#include "i18n.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <functional>
#include <set>
#include <stdexcept>

namespace
{
    const std::map<std::string, std::pair<const char*, const char*>> PageTitles = {
        { "BW_VerticalBox", { "Schulteransicht", "Shoulder view" } },
        { "BW_VerticalBox_Drive", { "Fahrzeug", "Vehicle" } },
        { "BW_VerticalBox_Active", { "Aktivmodus", "Active mode" } },
        { "BW_VerticalBox_TopView", { "Draufsicht (F6-Menü)", "Top view (F6 menu)" } },
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
        page.panelExport = panel;

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
    // The top view page is what F6 shows in the normal camera - list it first.
    std::stable_partition(pages.begin(), pages.end(), [](const GuidePage& p) { return p.id == "BW_VerticalBox_TopView"; });
    return true;
}

namespace
{
    const char* const DependencyLists[] = { "SerializationBeforeSerializationDependencies", "CreateBeforeSerializationDependencies",
                                            "SerializationBeforeCreateDependencies", "CreateBeforeCreateDependencies" };

    // Adds new rows by cloning existing exports (proof of concept tested in game 19.10.2026):
    // row = HorizontalBox + (key widget | "+" text)* + text, each child in a HorizontalBoxSlot,
    // the box in a VerticalBoxSlot of the page's Panel_Guide.
    class RowCloner
    {
    public:
        // Templates are read from the untouched original (src), so hidden or renamed rows never leak into clones.
        RowCloner(Json& target, const Json& original) : a(target), exports(target["Exports"]), src(original["Exports"])
        {
            for (const Json& e : exports) usedNames.insert(e.value("ObjectName", ""));
        }

        bool FindTemplates(std::string& error)
        {
            // Only objects directly in the WidgetTree have unique names; slots are found through the structure
            // (every box has its own HorizontalBoxSlot_1 ...).
            box = Unique("B1HorizontalBox_F_Select", error);    // simple row: [key, text]
            key = Unique("WBP_ActionKeyWidget_688", error);     // key widget with a fixed DirectKey
            plus = Unique("B1TextBlock_2", error);              // literal "+"
            if (!box || !key || !plus) return false;
            const Json& slots = Prop(src[box - 1], "Slots")["Value"];
            keySlot = slots[0]["Value"].get<int>();
            textSlot = slots[1]["Value"].get<int>();
            text = Prop(src[textSlot - 1], "content")["Value"].get<int>();
            boxSlot = Prop(src[box - 1], "Slot")["Value"].get<int>();
            plusSlot = Prop(src[plus - 1], "Slot")["Value"].get<int>();
            keyTemplateSlot = Prop(src[key - 1], "Slot")["Value"].get<int>();
            return true;
        }

        void AddRow(int panel, const std::vector<std::string>& keys, const std::string& rowText)
        {
            const int boxIdx = Next();
            std::vector<Json> pending(1);   // [0] = the box, filled in at the end
            std::vector<int> childSlots;
            auto nextIdx = [&]() { return boxIdx + static_cast<int>(pending.size()); };

            for (size_t i = 0; i < keys.size(); ++i)
            {
                if (i > 0)   // " + " between keys
                {
                    const int slotIdx = nextIdx(), plusIdx = slotIdx + 1;
                    const int oldParent = Prop(src[plusSlot - 1], "Parent")["Value"].get<int>();
                    pending.push_back(Clone(plusSlot, { { oldParent, boxIdx }, { plus, plusIdx } }, boxIdx));
                    pending.push_back(Clone(plus, { { plusSlot, slotIdx } }, src[plus - 1]["OuterIndex"].get<int>()));
                    childSlots.push_back(slotIdx);
                }
                const int slotIdx = nextIdx(), keyIdx = slotIdx + 1;
                const int oldContent = Prop(src[keySlot - 1], "content")["Value"].get<int>();
                pending.push_back(Clone(keySlot, { { box, boxIdx }, { oldContent, keyIdx } }, boxIdx));
                Json k = Clone(key, { { keyTemplateSlot, slotIdx } }, src[key - 1]["OuterIndex"].get<int>());
                MutableProp(k, "DirectKey")["Value"][0]["Value"] = keys[i];
                AddExportDataName(keys[i]);
                pending.push_back(k);
                childSlots.push_back(slotIdx);
            }

            const int tSlotIdx = nextIdx(), textIdx = tSlotIdx + 1;
            pending.push_back(Clone(textSlot, { { box, boxIdx }, { text, textIdx } }, boxIdx));
            Json t = Clone(text, { { textSlot, tSlotIdx } }, src[text - 1]["OuterIndex"].get<int>());
            Json& tp = MutableProp(t, "Text");
            tp["HistoryType"] = "None";
            tp["TableId"] = nullptr;
            tp["Value"] = nullptr;
            tp["CultureInvariantString"] = rowText;
            pending.push_back(t);
            childSlots.push_back(tSlotIdx);

            const int vSlotIdx = nextIdx();
            pending.push_back(Clone(boxSlot, { { box, boxIdx }, { src[boxSlot - 1]["OuterIndex"].get<int>(), panel } }, panel));

            Json b = Clone(box, { { boxSlot, vSlotIdx } }, src[box - 1]["OuterIndex"].get<int>());
            // new rows are always visible, whatever the template row is set to
            Json& bData = b["Data"];
            bData.erase(std::remove_if(bData.begin(), bData.end(), [](const Json& p) { return p.value("Name", "") == "Visibility"; }), bData.end());
            Json& slotList = MutableProp(b, "Slots")["Value"];
            const Json slotTemplate = slotList[0];
            slotList = Json::array();
            for (size_t i = 0; i < childSlots.size(); ++i)
            {
                Json s = slotTemplate;
                s["Name"] = std::to_string(i);
                s["Value"] = childSlots[i];
                slotList.push_back(s);
            }
            b["CreateBeforeSerializationDependencies"] = Json::array();
            for (const int s : childSlots) b["CreateBeforeSerializationDependencies"].push_back(s);
            b["CreateBeforeSerializationDependencies"].push_back(vSlotIdx);
            MutableProp(b, "bIsVariable")["Value"] = false;
            pending[0] = b;

            // The page panel gets the new slot at the end.
            Json& p = exports[static_cast<size_t>(panel - 1)];
            Json& panelSlots = MutableProp(p, "Slots")["Value"];
            Json s = panelSlots[0];
            s["Name"] = std::to_string(panelSlots.size());
            s["Value"] = vSlotIdx;
            panelSlots.push_back(s);
            p["CreateBeforeSerializationDependencies"].push_back(vSlotIdx);

            for (Json& e : pending) exports.push_back(std::move(e));
        }

        // Safety net for the crash of 18.10. ("Bad name index 291/223"): every name used as a value inside
        // export data must be among the first NamesReferencedFromExportDataCount names, or the zen package loses it.
        bool CheckNames(std::string& error) const
        {
            const int limit = a.value("NamesReferencedFromExportDataCount", 0);
            for (const std::string& n : addedNames)
            {
                const auto& names = a["NameMap"];
                int index = -1;
                for (size_t i = 0; i < names.size(); ++i) if (names[i] == n) { index = static_cast<int>(i); break; }
                if (index < 0 || index >= limit)
                {
                    error = std::string(T("Interner Fehler: Tastenname außerhalb des Namensbereichs: ", "internal error: key name outside the name range: ")) + n;
                    return false;
                }
            }
            return true;
        }

    private:
        Json& a;
        Json& exports;
        const Json& src;
        std::set<std::string> usedNames;
        std::vector<std::string> addedNames;
        int box = 0, key = 0, plus = 0, keySlot = 0, textSlot = 0, text = 0, boxSlot = 0, plusSlot = 0, keyTemplateSlot = 0;
        int counter = 900;

        int Next() const { return static_cast<int>(exports.size()) + 1; }

        int Unique(const std::string& name, std::string& error)
        {
            int found = 0, hits = 0;
            for (size_t i = 0; i < src.size(); ++i)
                if (src[i].value("ObjectName", "") == name) { found = static_cast<int>(i + 1); ++hits; }
            if (hits != 1)
            {
                error = std::string(T("Vorlage für neue Zeilen nicht gefunden (Spiel-Update?): ", "template for new rows not found (game update?): ")) + name;
                return 0;
            }
            return found;
        }

        static const Json& Prop(const Json& e, const char* name)
        {
            for (const Json& p : e["Data"])
                if (EqualsNoCase(p.value("Name", ""), name)) return p;
            throw std::runtime_error(std::string("property missing: ") + name);
        }

        static Json& MutableProp(Json& e, const char* name)
        {
            for (Json& p : e["Data"])
                if (EqualsNoCase(p.value("Name", ""), name)) return p;
            throw std::runtime_error(std::string("property missing: ") + name);
        }

        // Unique object name from the template's base name ("B1TextBlock_6" -> "B1TextBlock_900").
        // The base name is already in the name map, only the number is new.
        std::string NewName(const std::string& templateName)
        {
            std::string base = templateName;
            const size_t underscore = base.find_last_of('_');
            if (underscore != std::string::npos && underscore + 1 < base.size() &&
                base.find_first_not_of("0123456789", underscore + 1) == std::string::npos)
                base.resize(underscore);
            std::string name;
            do name = base + "_" + std::to_string(counter++);
            while (usedNames.count(name));
            usedNames.insert(name);
            return name;
        }

        Json Clone(int templ, const std::map<int, int>& map, int outer)
        {
            Json e = src[static_cast<size_t>(templ - 1)];
            e["ObjectName"] = NewName(e.value("ObjectName", ""));
            e["OuterIndex"] = outer;
            auto remap = [&](int v) { const auto it = map.find(v); return it != map.end() ? it->second : v; };
            for (const char* list : DependencyLists)
                for (Json& v : e[list]) v = remap(v.get<int>());
            std::function<void(Json&)> walk = [&](Json& node) {
                if (node.is_array()) { for (Json& c : node) walk(c); return; }
                if (!node.is_object()) return;
                if (node.value("$type", "").rfind("UAssetAPI.PropertyTypes.Objects.ObjectPropertyData", 0) == 0 && node["Value"].is_number_integer())
                    node["Value"] = remap(node["Value"].get<int>());
                for (auto it = node.begin(); it != node.end(); ++it) walk(it.value());
            };
            walk(e["Data"]);
            return e;
        }

        // Names inside export data must sit in the first NamesReferencedFromExportDataCount entries.
        // UAssetAPI refers to names by string, so moving one is safe.
        void AddExportDataName(const std::string& n)
        {
            addedNames.push_back(n);
            Json& names = a["NameMap"];
            int count = a.value("NamesReferencedFromExportDataCount", 0);
            for (size_t i = 0; i < names.size(); ++i)
                if (names[i] == n)
                {
                    if (static_cast<int>(i) < count) return;
                    names.erase(i);
                    break;
                }
            names.insert(names.begin() + count, n);
            a["NamesReferencedFromExportDataCount"] = count + 1;
        }
    };
}

int WidgetAsset::WriteModified(const ChangeMap& changes, const NewRows& newRows, const std::string& outPath, std::string& error) const
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

    if (!newRows.empty())
    {
        try
        {
            RowCloner cloner(copy, asset);
            if (!cloner.FindTemplates(error)) return -1;
            for (const NewRow& row : newRows)
            {
                if (row.keys.empty() && row.text.empty()) continue;
                const auto page = std::find_if(pages.begin(), pages.end(), [&](const GuidePage& p) { return p.id == row.page; });
                if (page == pages.end() || !page->panelExport) continue;
                cloner.AddRow(page->panelExport, row.keys, row.text);
                ++changed;
            }
            if (!cloner.CheckNames(error)) return -1;
        }
        catch (const std::exception& e)
        {
            error = std::string(T("Neue Zeilen konnten nicht gebaut werden: ", "could not build the new rows: ")) + e.what();
            return -1;
        }
    }

    std::ofstream out(std::filesystem::u8path(outPath), std::ios::binary | std::ios::trunc);
    if (!out) { error = std::string(T("Kann nicht schreiben: ", "cannot write: ")) + outPath; return -1; }
    out << copy.dump(2);
    return changed;
}

bool LoadChanges(const std::string& path, ChangeMap& changes, NewRows& newRows)
{
    changes.clear();
    newRows.clear();
    Json data;
    std::string error;
    if (!ReadJsonFile(path, data, error)) return false;
    if (data.contains("neue_zeilen") && data["neue_zeilen"].is_array())
        for (const Json& r : data["neue_zeilen"])
        {
            NewRow row;
            row.page = r.value("seite", "");
            row.text = r.value("text", "");
            if (r.contains("tasten") && r["tasten"].is_array())
                for (const Json& k : r["tasten"]) if (k.is_string()) row.keys.push_back(k.get<std::string>());
            newRows.push_back(row);
        }
    if (!data.contains("zeilen")) return true;
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

std::string KeysDisplay(const std::vector<std::string>& keys)
{
    std::string text;
    for (const std::string& k : keys)
    {
        if (!text.empty()) text += " + ";
        if (k == "LeftShift" || k == "RightShift") text += "Shift";
        else if (k == "LeftControl" || k == "RightControl") text += T("Strg", "Ctrl");
        else if (k == "LeftAlt" || k == "RightAlt") text += "Alt";
        else text += Hotkeys::KeyLabel(k);
    }
    return text;
}

bool SaveChanges(const std::string& path, const ChangeMap& changes, const NewRows& newRows)
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
    data["neue_zeilen"] = Json::array();
    for (const NewRow& r : newRows)
        data["neue_zeilen"].push_back(Json{ { "seite", r.page }, { "tasten", r.keys }, { "text", r.text } });
    std::ofstream out(std::filesystem::u8path(path), std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << data.dump(2);
    return true;
}
