#include "ui/ViewerSettingsPersistence.h"

namespace hyperbrowse::ui
{
    namespace
    {
        constexpr std::wstring_view kSlideshowIntervalValue = L"SlideshowIntervalMs";
        constexpr std::wstring_view kSlideshowTransitionStyleValue = L"SlideshowTransitionStyle";
        constexpr std::wstring_view kSlideshowTransitionDurationValue = L"SlideshowTransitionDurationMs";
        constexpr std::wstring_view kUseSlideshowTransitionValue = L"UseSlideshowTransition";
        constexpr std::wstring_view kMouseWheelBehaviorValue = L"ViewerMouseWheelBehavior";
        constexpr std::wstring_view kEscapeKeyBehaviorValue = L"ViewerEscapeKeyBehavior";
        constexpr std::wstring_view kInvertKeyboardPanningValue = L"InvertKeyboardPanning";
        constexpr UINT kMinimumSlideshowDurationMs = 250U;
        constexpr UINT kMaximumSlideshowDurationMs = 60000U;
        constexpr UINT kDefaultSlideshowDurationMs = 3000U;
        constexpr UINT kMinimumTransitionDurationMs = 100U;
        constexpr UINT kMaximumTransitionDurationMs = 5000U;
        constexpr UINT kDefaultTransitionDurationMs = 350U;

        UINT NormalizeSlideshowDuration(UINT value)
        {
            return value >= kMinimumSlideshowDurationMs && value <= kMaximumSlideshowDurationMs
                ? value
                : kDefaultSlideshowDurationMs;
        }

        UINT NormalizeTransitionDuration(UINT value)
        {
            return value >= kMinimumTransitionDurationMs && value <= kMaximumTransitionDurationMs
                ? value
                : kDefaultTransitionDurationMs;
        }
    }

    ViewerSettingsState ViewerSettingsPersistence::Load(const ReadDword& readDword,
                                                        ViewerSettingsState state)
    {
        DWORD value = 0;
        if (readDword(kSlideshowIntervalValue, &value))
        {
            state.slideshowIntervalMs = NormalizeSlideshowDuration(value);
        }

        if (readDword(kSlideshowTransitionStyleValue, &value)
            && value <= static_cast<DWORD>(viewer::TransitionStyle::MonochromeReveal))
        {
            state.slideshowTransitionStyle = static_cast<viewer::TransitionStyle>(value);
        }

        if (readDword(kSlideshowTransitionDurationValue, &value))
        {
            state.slideshowTransitionDurationMs = NormalizeTransitionDuration(value);
        }

        if (readDword(kUseSlideshowTransitionValue, &value))
        {
            state.useSlideshowTransition = value != 0;
        }

        if (readDword(kMouseWheelBehaviorValue, &value)
            && value <= static_cast<DWORD>(viewer::MouseWheelBehavior::Navigate))
        {
            state.mouseWheelBehavior = static_cast<viewer::MouseWheelBehavior>(value);
        }

        if (readDword(kEscapeKeyBehaviorValue, &value)
            && value <= static_cast<DWORD>(viewer::EscapeKeyBehavior::ActualSize))
        {
            state.escapeKeyBehavior = static_cast<viewer::EscapeKeyBehavior>(value);
        }

        if (readDword(kInvertKeyboardPanningValue, &value))
        {
            state.invertKeyboardPanning = value != 0;
        }

        return state;
    }

    void ViewerSettingsPersistence::Save(const ViewerSettingsState& state,
                                         const WriteDword& writeDword)
    {
        writeDword(kSlideshowIntervalValue, state.slideshowIntervalMs);
        writeDword(kSlideshowTransitionStyleValue, static_cast<DWORD>(state.slideshowTransitionStyle));
        writeDword(kSlideshowTransitionDurationValue, state.slideshowTransitionDurationMs);
        writeDword(kUseSlideshowTransitionValue, state.useSlideshowTransition ? 1UL : 0UL);
        writeDword(kMouseWheelBehaviorValue, static_cast<DWORD>(state.mouseWheelBehavior));
        writeDword(kEscapeKeyBehaviorValue, static_cast<DWORD>(state.escapeKeyBehavior));
        writeDword(kInvertKeyboardPanningValue, state.invertKeyboardPanning ? 1UL : 0UL);
    }
}
