#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace hyperbrowse::ui
{
    class QuickAccessPathList
    {
    public:
        static bool Insert(std::vector<std::wstring>* paths,
                           std::wstring folderPath,
                           std::size_t maxCount,
                           bool moveToFront);
        static std::vector<std::wstring> Deserialize(std::wstring_view serialized,
                                                      std::size_t maxCount);
        static std::wstring Serialize(const std::vector<std::wstring>& paths);
    };
}
