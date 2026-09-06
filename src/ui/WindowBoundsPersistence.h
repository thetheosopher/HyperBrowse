#pragma once

#include <windows.h>

#include <functional>
#include <optional>
#include <string_view>

namespace hyperbrowse::ui
{
    class WindowBoundsPersistence
    {
    public:
        using ReadDword = std::function<bool(std::wstring_view valueName, DWORD* value)>;
        using WriteDword = std::function<void(std::wstring_view valueName, DWORD value)>;

        static std::optional<RECT> Load(const ReadDword& readDword);
        static bool IsWithinWorkArea(const RECT& bounds,
                                     LONG minimumWidth,
                                     LONG minimumHeight,
                                     const RECT& workArea);
        static bool Save(const RECT& bounds,
                         LONG minimumWidth,
                         LONG minimumHeight,
                         const WriteDword& writeDword);
    };
}
