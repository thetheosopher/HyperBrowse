#pragma once

#include <windows.h>

#include <string>
#include <string_view>
#include <vector>

#include "browser/BrowserModel.h"
#include "util/UiTextSize.h"

namespace hyperbrowse::ui
{
    bool PromptForSingleLineText(HWND ownerWindow,
                                 HINSTANCE instance,
                                 hyperbrowse::util::AppTextSize appTextSize,
                                 bool darkTheme,
                                 const std::wstring& title,
                                 const std::wstring& instruction,
                                 const std::wstring& confirmLabel,
                                 const std::wstring& initialText,
                                 int selectionStart,
                                 int selectionEnd,
                                 std::wstring* resultText);

    bool IsValidRenameLeafName(std::wstring_view leafName, std::wstring* errorMessage);

    bool PromptForRenameLeafName(HWND ownerWindow,
                                 HINSTANCE instance,
                                 hyperbrowse::util::AppTextSize appTextSize,
                                 bool darkTheme,
                                 const std::wstring& title,
                                 const std::wstring& instruction,
                                 const std::wstring& currentLeafName,
                                 bool isFile,
                                 std::wstring* renamedLeafName);

    bool PromptForBatchRenamePattern(HWND ownerWindow,
                                     HINSTANCE instance,
                                     hyperbrowse::util::AppTextSize appTextSize,
                                     bool darkTheme,
                                     std::wstring initialPattern,
                                     std::vector<hyperbrowse::browser::BrowserItem> items,
                                     std::vector<std::wstring>* resultLeafNames);
}
