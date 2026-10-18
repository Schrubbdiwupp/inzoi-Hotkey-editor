#pragma once
// The key guide widget (WBP_HUD_ShoulderViewControlGuide) as UAssetGUI JSON:
// reads pages and rows, applies the user's changes for a new override.
#include <map>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

using Json = nlohmann::ordered_json;   // keeps "$type" first, UAssetGUI needs that

struct GuideRow
{
    std::string id;            // ObjectName of the row's HorizontalBox, stable across game updates
    std::string keys;          // readable key list, e.g. "CharacterMoveLeft + LeftControl"
    std::string textKey;       // string table key of the row text ("" if literal)
    std::string originalText;  // English text (from texte_en.json) or the literal text
    bool originallyVisible = true;
    int boxExport = 0;         // 1-based export indices in the asset
    int textExport = 0;
};

struct GuidePage
{
    std::string id;            // ObjectName of the page's VerticalBox
    std::string titleDe, titleEn;   // display names
    std::vector<GuideRow> rows;
    int panelExport = 0;       // its Panel_Guide* VerticalBox (new rows go there)
};

// An added row: key symbols (fixed keys, UE names like "LeftShift", "F2") followed by a text.
struct NewRow
{
    std::string page;          // GuidePage::id
    std::vector<std::string> keys;
    std::string text;
};
using NewRows = std::vector<NewRow>;

struct RowChange
{
    std::string text;          // empty = keep the game's text
    bool overrideVisibility = false;
    bool visible = true;

    bool Empty() const { return text.empty() && !overrideVisibility; }
};

using ChangeMap = std::map<std::string, RowChange>;

class WidgetAsset
{
public:
    bool Load(const std::string& jsonPath, const std::string& englishTextsPath, std::string& error);
    const std::vector<GuidePage>& Pages() const { return pages; }
    bool Loaded() const { return !pages.empty(); }

    // Writes a copy of the original with the changes applied and the new rows added.
    // Returns the number of changed + added rows, -1 on error.
    int WriteModified(const ChangeMap& changes, const NewRows& newRows, const std::string& outPath, std::string& error) const;

private:
    Json asset;
    std::vector<GuidePage> pages;
    std::map<std::string, std::string> englishTexts;
};

bool LoadChanges(const std::string& path, ChangeMap& changes, NewRows& newRows);
bool SaveChanges(const std::string& path, const ChangeMap& changes, const NewRows& newRows);

// "Shift + F2" style text for a key list, in the UI language.
std::string KeysDisplay(const std::vector<std::string>& keys);
