#include "i18n.h"

#include <windows.h>

#include <atomic>

#include "imgui.h"

namespace
{
    std::atomic<bool> english = false;   // read by the worker thread for log texts
}

namespace I18n
{
    void SetEnglish(bool value) { english = value; }
    bool English() { return english; }
    bool SystemIsGerman() { return PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_GERMAN; }
}

void Tip(const char* de, const char* en)
{
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled)) return;
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
    ImGui::TextUnformatted(T(de, en));
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}
