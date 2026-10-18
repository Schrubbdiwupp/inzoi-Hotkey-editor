#pragma once
// German / English UI texts. Every text sits at its call site as T("deutsch", "english").
#include <string>

namespace I18n
{
    void SetEnglish(bool english);
    bool English();
    bool SystemIsGerman();   // Windows UI language, for the first start
}

inline const char* T(const char* de, const char* en) { return I18n::English() ? en : de; }

// Tooltip for the last ImGui item (shown after a short hover delay).
void Tip(const char* de, const char* en);
