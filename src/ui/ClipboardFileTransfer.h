#pragma once

#include <windows.h>

#include <string>
#include <string_view>
#include <vector>

namespace hyperbrowse::ui
{
    bool CopyTextToClipboard(HWND ownerWindow, std::wstring_view text);
    bool CopyFilePathsToClipboard(HWND ownerWindow,
                                  const std::vector<std::wstring>& paths,
                                  bool movePreferred);
    std::vector<std::wstring> ReadClipboardFilePaths(HWND ownerWindow,
                                                     DWORD* preferredDropEffect = nullptr);
}
