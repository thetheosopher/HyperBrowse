#pragma once

#include <windows.h>

#include <functional>
#include <string_view>

#include "browser/BrowserModel.h"

namespace hyperbrowse::ui
{
    struct ImageWorkflowState
    {
        bool nvJpegEnabled{};
        bool libRawOutOfProcessEnabled{true};
        bool rawJpegPairedOperationsEnabled{};
        browser::RawJpegDisplayPreference pairedRawJpegViewerPreference{browser::RawJpegDisplayPreference::Raw};
        bool defaultViewerToSecondaryMonitor{};
    };

    class ImageWorkflowPersistence
    {
    public:
        using ReadDword = std::function<bool(std::wstring_view valueName, DWORD* value)>;
        using WriteDword = std::function<void(std::wstring_view valueName, DWORD value)>;

        static ImageWorkflowState Load(const ReadDword& readDword,
                                       ImageWorkflowState state = {});
        static void Save(const ImageWorkflowState& state,
                         const WriteDword& writeDword);
    };
}
