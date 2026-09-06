#pragma once

#include <windows.h>

#include <functional>
#include <string_view>

#include "viewer/ViewerWindow.h"

namespace hyperbrowse::ui
{
    struct ViewerSettingsState
    {
        UINT slideshowIntervalMs{3000};
        viewer::TransitionStyle slideshowTransitionStyle{viewer::TransitionStyle::Crossfade};
        UINT slideshowTransitionDurationMs{350};
        bool useSlideshowTransition{};
        viewer::MouseWheelBehavior mouseWheelBehavior{};
        bool invertKeyboardPanning{};
        viewer::EscapeKeyBehavior escapeKeyBehavior{};
    };

    class ViewerSettingsPersistence
    {
    public:
        using ReadDword = std::function<bool(std::wstring_view valueName, DWORD* value)>;
        using WriteDword = std::function<void(std::wstring_view valueName, DWORD value)>;

        static ViewerSettingsState Load(const ReadDword& readDword,
                                        ViewerSettingsState state = {});
        static void Save(const ViewerSettingsState& state,
                          const WriteDword& writeDword);
    };
}
