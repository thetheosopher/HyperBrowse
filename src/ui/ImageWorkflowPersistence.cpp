#include "ui/ImageWorkflowPersistence.h"

namespace hyperbrowse::ui
{
    namespace
    {
        constexpr std::wstring_view kNvJpegEnabledValue = L"NvJpegEnabled";
        constexpr std::wstring_view kLibRawOutOfProcessEnabledValue = L"LibRawOutOfProcessEnabled";
        constexpr std::wstring_view kRawJpegPairedOperationsEnabledValue = L"RawJpegPairedOperationsEnabled";
        constexpr std::wstring_view kPairedRawJpegViewerPreferenceValue = L"PairedRawJpegViewerPreference";
        constexpr std::wstring_view kDefaultViewerToSecondaryMonitorValue = L"DefaultViewerToSecondaryMonitor";

        constexpr bool IsValidRawJpegDisplayPreference(DWORD value) noexcept
        {
            return value <= static_cast<DWORD>(browser::RawJpegDisplayPreference::Raw);
        }
    }

    ImageWorkflowState ImageWorkflowPersistence::Load(const ReadDword& readDword,
                                                      ImageWorkflowState state)
    {
        DWORD value = 0;
        if (readDword(kNvJpegEnabledValue, &value))
        {
            state.nvJpegEnabled = value != 0;
        }

        if (readDword(kLibRawOutOfProcessEnabledValue, &value))
        {
            state.libRawOutOfProcessEnabled = value != 0;
        }

        if (readDword(kRawJpegPairedOperationsEnabledValue, &value))
        {
            state.rawJpegPairedOperationsEnabled = value != 0;
        }

        if (readDword(kPairedRawJpegViewerPreferenceValue, &value)
            && IsValidRawJpegDisplayPreference(value))
        {
            state.pairedRawJpegViewerPreference = static_cast<browser::RawJpegDisplayPreference>(value);
        }

        if (readDword(kDefaultViewerToSecondaryMonitorValue, &value))
        {
            state.defaultViewerToSecondaryMonitor = value != 0;
        }

        return state;
    }

    void ImageWorkflowPersistence::Save(const ImageWorkflowState& state,
                                         const WriteDword& writeDword)
    {
        writeDword(kNvJpegEnabledValue, state.nvJpegEnabled ? 1UL : 0UL);
        writeDword(kLibRawOutOfProcessEnabledValue, state.libRawOutOfProcessEnabled ? 1UL : 0UL);
        writeDword(kRawJpegPairedOperationsEnabledValue, state.rawJpegPairedOperationsEnabled ? 1UL : 0UL);
        writeDword(kPairedRawJpegViewerPreferenceValue, static_cast<DWORD>(state.pairedRawJpegViewerPreference));
        writeDword(kDefaultViewerToSecondaryMonitorValue, state.defaultViewerToSecondaryMonitor ? 1UL : 0UL);
    }
}
