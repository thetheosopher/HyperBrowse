#pragma once

#include <string_view>

namespace hyperbrowse::ui
{
    class FolderTreeDropPolicy final
    {
    public:
        struct Input
        {
            std::wstring_view sourcePath;
            std::wstring_view destinationPath;
            std::wstring_view sourceParentPath;
            bool destinationExists{};
            bool sameDrive{};
        };

        static bool IsValid(const Input& input);
    };
}
