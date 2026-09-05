#pragma once

#include <windows.h>

#include <string>
#include <string_view>

namespace hyperbrowse::ui
{
    struct MenuDrawItemData
    {
        std::wstring text;
        wchar_t mnemonic{};
        int mnemonicDisplayIndex{-1};
        bool separator{};
        bool hasSubmenu{};
    };

    wchar_t FindMenuMnemonic(std::wstring_view text);
    LRESULT HandleMenuCharMessage(WPARAM wParam, LPARAM lParam);
}
