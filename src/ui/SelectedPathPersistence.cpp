#include "ui/SelectedPathPersistence.h"

#include <string>

namespace hyperbrowse::ui
{
    namespace
    {
        constexpr std::wstring_view kSelectedFolderPathValue = L"SelectedFolderPath";
        constexpr std::wstring_view kSelectedImagePathValue = L"SelectedImagePath";
    }

    SelectedPathState SelectedPathPersistence::Load(const ReadValue& readValue)
    {
        SelectedPathState state;
        readValue(kSelectedFolderPathValue, &state.folderPath);
        readValue(kSelectedImagePathValue, &state.imagePath);
        return state;
    }

    void SelectedPathPersistence::Save(const SelectedPathState& state,
                                       const WriteValue& writeValue,
                                       const DeleteValue& deleteValue)
    {
        if (!state.folderPath.empty())
        {
            writeValue(kSelectedFolderPathValue, state.folderPath);
        }

        if (!state.imagePath.empty())
        {
            writeValue(kSelectedImagePathValue, state.imagePath);
        }
        else
        {
            deleteValue(kSelectedImagePathValue);
        }
    }
}
