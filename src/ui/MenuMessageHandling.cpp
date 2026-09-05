#include "ui/MenuMessageHandling.h"

#include <cwctype>
#include <string_view>

namespace hyperbrowse::ui
{
    wchar_t FindMenuMnemonic(std::wstring_view text)
    {
        for (std::size_t index = 0; index < text.size(); ++index)
        {
            if (text[index] != L'&')
            {
                continue;
            }

            if (index + 1 >= text.size())
            {
                break;
            }

            if (text[index + 1] == L'&')
            {
                ++index;
                continue;
            }

            return static_cast<wchar_t>(towupper(text[index + 1]));
        }

        return L'\0';
    }

    LRESULT HandleMenuCharMessage(WPARAM wParam, LPARAM lParam)
    {
        const HMENU menu = reinterpret_cast<HMENU>(lParam);
        if (!menu)
        {
            return MAKELRESULT(0, MNC_IGNORE);
        }

        const wchar_t pressed = static_cast<wchar_t>(towupper(static_cast<wchar_t>(LOWORD(wParam))));
        int matchedIndex = -1;
        bool duplicateMatch = false;
        const int itemCount = GetMenuItemCount(menu);
        for (int itemIndex = 0; itemIndex < itemCount; ++itemIndex)
        {
            MENUITEMINFOW itemInfo{};
            itemInfo.cbSize = sizeof(itemInfo);
            itemInfo.fMask = MIIM_FTYPE | MIIM_DATA;
            if (!GetMenuItemInfoW(menu, itemIndex, TRUE, &itemInfo) || (itemInfo.fType & MFT_SEPARATOR) != 0)
            {
                continue;
            }

            const auto* drawData = reinterpret_cast<const MenuDrawItemData*>(itemInfo.dwItemData);
            if (!drawData)
            {
                continue;
            }

            const wchar_t mnemonic = drawData->mnemonic != L'\0'
                ? drawData->mnemonic
                : FindMenuMnemonic(drawData->text);
            if (towupper(mnemonic) != pressed)
            {
                continue;
            }

            if (matchedIndex >= 0)
            {
                duplicateMatch = true;
                break;
            }

            matchedIndex = itemIndex;
        }

        if (matchedIndex >= 0)
        {
            return MAKELRESULT(matchedIndex, duplicateMatch ? MNC_SELECT : MNC_EXECUTE);
        }

        return MAKELRESULT(0, MNC_IGNORE);
    }
}
