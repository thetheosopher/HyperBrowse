#pragma once

#include <functional>
#include <string>
#include <string_view>

namespace hyperbrowse::ui
{
    struct SelectedPathState
    {
        std::wstring folderPath;
        std::wstring imagePath;
    };

    class SelectedPathPersistence
    {
    public:
        using ReadValue = std::function<bool(std::wstring_view valueName, std::wstring* value)>;
        using WriteValue = std::function<void(std::wstring_view valueName, std::wstring_view value)>;
        using DeleteValue = std::function<void(std::wstring_view valueName)>;

        static SelectedPathState Load(const ReadValue& readValue);
        static void Save(const SelectedPathState& state,
                          const WriteValue& writeValue,
                          const DeleteValue& deleteValue);
    };
}
