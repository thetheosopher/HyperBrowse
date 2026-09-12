#include "ui/SettingsLayout.h"

#include <algorithm>
#include <array>

#include "util/UiTextSize.h"

namespace hyperbrowse::ui::dialog_detail
{
    namespace
    {
        int SafeMeasureText(const SettingsLayoutInput& input, std::wstring_view text, int fallbackWidth)
        {
            if (input.measureTextWidth)
            {
                return std::max(1, input.measureTextWidth(text));
            }
            return std::max(1, fallbackWidth * static_cast<int>(text.size()));
        }

        void AddLabel(SettingsLayoutResult& result,
                      int left,
                      int top,
                      int right,
                      int bottom,
                      std::wstring_view text,
                      bool muted = false,
                      ConsolidatedSettingsControl mnemonicControl = ConsolidatedSettingsControl::Count)
        {
            result.labels.push_back(SettingsLayoutLabel{{left, top, right, bottom}, std::wstring(text), muted, mnemonicControl});
        }

        void SetControlRect(SettingsLayoutResult& result,
                            ConsolidatedSettingsControl control,
                            int left,
                            int top,
                            int right,
                            int bottom)
        {
            result.controlRects[static_cast<std::size_t>(control)] = {left, top, right, bottom};
        }
    }

    SettingsLayoutResult MeasureSettingsLayout(const SettingsLayoutInput& input)
    {
        SettingsLayoutResult result;
        const auto scale = [textSize = input.appTextSize](int value)
        {
            return hyperbrowse::util::ScaleAppTextDimension(value, textSize);
        };
        const int width = std::max(1, input.availableWidth);
        const int height = std::max(1, input.availableHeight);
        const int margin = scale(28);
        const int tabTop = scale(14);
        const int tabHeight = scale(46);
        const int contentTop = tabTop + tabHeight + scale(18);
        const int rowHeight = scale(38);
        const int rowGap = scale(12);
        const int buttonWidth = scale(104);
        const int buttonHeight = scale(38);
        const int buttonGap = scale(12);
        const int left = margin;
        const int right = std::max(left + 1, width - margin);
        const int availableWidth = std::max(1, right - left);
        const int fallbackCharacterWidth = scale(8);
        const std::array<std::wstring_view, 9> measuredLabels{
            L"Transition duration (milliseconds)",
            L"Treat paired RAW+JPEG files as one operation",
            L"Open viewers on a secondary monitor when available",
            L"ESC key behavior in full screen",
            L"Keep the persistent thumbnail cache enabled",
            L"Show memory pressure state in the status bar",
            L"Use out-of-process LibRaw fallback",
            L"New Quick Actions shortcut order",
            L"Metadata cache cap (entries)",
        };
        int measuredLabelWidth = 0;
        for (const std::wstring_view label : measuredLabels)
        {
            measuredLabelWidth = std::max(measuredLabelWidth, SafeMeasureText(input, label, fallbackCharacterWidth));
        }
        const int minimumValueWidth = scale(170);
        const int labelGap = scale(20);
        const bool canUseColumns = availableWidth >= measuredLabelWidth + labelGap + minimumValueWidth;
        const int valueLeft = canUseColumns
            ? std::min(right - minimumValueWidth, left + measuredLabelWidth + labelGap)
            : left + availableWidth / 2;
        const int valueRight = right;
        const int radioGap = scale(16);
        const int radioWidth = std::max(1, (valueRight - valueLeft - radioGap) / 2);
        const int secondRadioLeft = valueLeft + radioWidth + radioGap;
        const int footerTop = std::max(contentTop, height - margin - buttonHeight);
        const int footerBottom = std::max(footerTop, height - margin);
        const RECT bodyViewport{left, contentTop, right, std::max(contentTop, footerTop - scale(18))};

        result.metrics = {
            width,
            height,
            margin,
            tabTop,
            tabHeight,
            contentTop,
            footerTop,
            buttonHeight,
            left,
            right,
            valueLeft,
            valueRight,
            rowHeight,
            rowGap,
            buttonWidth,
            buttonGap,
            radioGap,
            radioWidth,
            std::max(1, valueLeft - left - labelGap),
            bodyViewport,
            {left, footerTop, right, footerBottom},
        };
        result.metrics.footer.left = right - (buttonWidth * 3 + buttonGap * 2);
        result.metrics.footer.right = right;
        for (std::size_t index = 0; index < result.tabRects.size(); ++index)
        {
            const int tabLeft = margin + static_cast<int>(index) * ((width - (margin * 2)) / static_cast<int>(result.tabRects.size()));
            const int tabRight = margin + static_cast<int>(index + 1) * ((width - (margin * 2)) / static_cast<int>(result.tabRects.size()));
            result.tabRects[index] = {tabLeft, tabTop, tabRight, tabTop + tabHeight};
        }

        const auto addLabelValue = [&](std::wstring_view label,
                                       ConsolidatedSettingsControl control,
                                       int& y)
        {
            if (canUseColumns)
            {
                AddLabel(result, left, y, valueLeft - labelGap, y + rowHeight, label, false, control);
                SetControlRect(result, control, valueLeft, y, valueRight, y + rowHeight);
                y += rowHeight + rowGap;
                return;
            }
            AddLabel(result, left, y, right, y + rowHeight, label, false, control);
            y += rowHeight + rowGap;
            SetControlRect(result, control, left, y, right, y + rowHeight);
            y += rowHeight + rowGap;
        };
        const auto addNumeric = [&](std::wstring_view label,
                                    ConsolidatedSettingsControl control,
                                    int numericIndex,
                                    int& y)
        {
            if (canUseColumns)
            {
                AddLabel(result, left, y, valueLeft - labelGap, y + rowHeight, label, false, control);
            }
            else
            {
                AddLabel(result, left, y, right, y + rowHeight, label, false, control);
                y += rowHeight + rowGap;
            }
            const int editTop = y;
            const int editWidth = std::min(scale(170), std::max(1, valueRight - valueLeft));
            const int spinWidth = scale(28);
            const int editLeft = canUseColumns ? valueLeft : left;
            result.numericEditRects[static_cast<std::size_t>(numericIndex)] = {
                editLeft, editTop, editLeft + editWidth, editTop + scale(34)};
            result.numericSpinRects[static_cast<std::size_t>(numericIndex)] = {
                editLeft + editWidth - spinWidth, editTop, editLeft + editWidth, editTop + scale(34)};
            y += rowHeight + rowGap;
        };
        const auto addCheck = [&](ConsolidatedSettingsControl control, std::wstring_view text, int& y)
        {
            SetControlRect(result, control, left, y, right, y + rowHeight);
            AddLabel(result, left + scale(34), y, right, y + rowHeight, text, false, control);
            y += rowHeight + rowGap;
        };
        const auto addRadioPair = [&](std::wstring_view label,
                                      ConsolidatedSettingsControl firstControl,
                                      std::wstring_view firstText,
                                      ConsolidatedSettingsControl secondControl,
                                      std::wstring_view secondText,
                                      int& y)
        {
            AddLabel(result, left, y, valueLeft - labelGap, y + rowHeight, label);
            const int minimumRadioWidth = scale(132);
            if (valueRight - valueLeft >= minimumRadioWidth * 2 + radioGap)
            {
                SetControlRect(result, firstControl, valueLeft, y, valueLeft + radioWidth, y + rowHeight);
                AddLabel(result, valueLeft + scale(34), y, valueLeft + radioWidth, y + rowHeight, firstText, false, firstControl);
                SetControlRect(result, secondControl, secondRadioLeft, y, valueRight, y + rowHeight);
                AddLabel(result, secondRadioLeft + scale(34), y, valueRight, y + rowHeight, secondText, false, secondControl);
                y += rowHeight + rowGap;
                return;
            }
            y += rowHeight + rowGap;
            SetControlRect(result, firstControl, left, y, right, y + rowHeight);
            AddLabel(result, left + scale(34), y, right, y + rowHeight, firstText, false, firstControl);
            y += rowHeight + rowGap;
            SetControlRect(result, secondControl, left, y, right, y + rowHeight);
            AddLabel(result, left + scale(34), y, right, y + rowHeight, secondText, false, secondControl);
            y += rowHeight + rowGap;
        };

        int y = contentTop;
        switch (input.page)
        {
        case ConsolidatedSettingsPage::Slideshow:
            addLabelValue(L"Transition style", ConsolidatedSettingsControl::TransitionStyle, y);
            addNumeric(L"Slide duration (milliseconds)", ConsolidatedSettingsControl::SlideshowDuration, 0, y);
            addNumeric(L"Transition duration (milliseconds)", ConsolidatedSettingsControl::TransitionDuration, 1, y);
            AddLabel(result, left, y, right, y + rowHeight, L"Slides: 250-60000 ms   |   Transitions: 100-5000 ms", true);
            y += rowHeight;
            break;
        case ConsolidatedSettingsPage::Viewer:
            addCheck(ConsolidatedSettingsControl::TransitionEnabled, L"Use slideshow transitions", y);
            addRadioPair(L"Mouse wheel", ConsolidatedSettingsControl::ViewerWheelZoom, L"Zoom",
                         ConsolidatedSettingsControl::ViewerWheelNavigate, L"Navigate", y);
            addCheck(ConsolidatedSettingsControl::InvertKeyboardPanning, L"Invert keyboard panning", y);
            addCheck(ConsolidatedSettingsControl::RawPairingEnabled, L"Treat paired RAW+JPEG files as one operation", y);
            addRadioPair(L"Paired viewer preference", ConsolidatedSettingsControl::RawPreferRaw, L"Prefer RAW",
                         ConsolidatedSettingsControl::RawPreferJpeg, L"Prefer JPEG", y);
            addCheck(ConsolidatedSettingsControl::SecondaryMonitor, L"Open viewers on a secondary monitor when available", y);
            addCheck(ConsolidatedSettingsControl::InfoOverlays, L"Show viewer detail overlays", y);
            addCheck(ConsolidatedSettingsControl::WindowedFullMetadata, L"Show full metadata in windowed mode", y);
            addCheck(ConsolidatedSettingsControl::FullScreenFullMetadata, L"Show full metadata in full-screen mode", y);
            addLabelValue(L"Overlay text size", ConsolidatedSettingsControl::OverlayTextSize, y);
            addLabelValue(L"ESC key behavior in full screen", ConsolidatedSettingsControl::EscapeKeyBehavior, y);
            break;
        case ConsolidatedSettingsPage::Appearance:
            addRadioPair(L"Theme", ConsolidatedSettingsControl::ThemeLight, L"Light",
                         ConsolidatedSettingsControl::ThemeDark, L"Dark", y);
            addLabelValue(L"Application text size", ConsolidatedSettingsControl::AppTextSize, y);
            addLabelValue(L"Thumbnail size", ConsolidatedSettingsControl::ThumbnailSize, y);
            addCheck(ConsolidatedSettingsControl::ThumbnailDetails, L"Show thumbnail details", y);
            addCheck(ConsolidatedSettingsControl::CompactLayout, L"Use compact thumbnail layout", y);
            addCheck(ConsolidatedSettingsControl::DetailsPanel, L"Show the details panel", y);
            break;
        case ConsolidatedSettingsPage::Performance:
            addLabelValue(L"Resource profile", ConsolidatedSettingsControl::ResourceProfile, y);
            addCheck(ConsolidatedSettingsControl::PersistentCache, L"Keep the persistent thumbnail cache enabled", y);
            addNumeric(L"Thumbnail cache cap (MB)", ConsolidatedSettingsControl::ThumbnailCache, 2, y);
            addCheck(ConsolidatedSettingsControl::ThumbnailCacheAutomatic, L"Follow profile", y);
            addNumeric(L"Metadata cache cap (entries)", ConsolidatedSettingsControl::MetadataCache, 3, y);
            addCheck(ConsolidatedSettingsControl::MetadataCacheAutomatic, L"Follow profile", y);
            addNumeric(L"Prefetch depth (items)", ConsolidatedSettingsControl::PrefetchDepth, 4, y);
            addCheck(ConsolidatedSettingsControl::PrefetchDepthAutomatic, L"Follow profile", y);
            addCheck(ConsolidatedSettingsControl::PressureStatus, L"Show memory pressure state in the status bar", y);
            addCheck(ConsolidatedSettingsControl::NvJpeg, L"Use NVIDIA JPEG acceleration when available", y);
            addCheck(ConsolidatedSettingsControl::LibRawOutOfProcess, L"Use out-of-process LibRaw fallback", y);
            break;
        case ConsolidatedSettingsPage::Behavior:
            addCheck(ConsolidatedSettingsControl::RecursiveBrowsing, L"Browse folders recursively", y);
            addCheck(ConsolidatedSettingsControl::ShowSubfolders, L"Show subfolders in the browser", y);
            addCheck(ConsolidatedSettingsControl::CloseOnEscape, L"Close the main window when ESC is pressed", y);
            addCheck(ConsolidatedSettingsControl::SingleInstance, L"Use a single application instance", y);
            addLabelValue(L"New Quick Actions shortcut order", ConsolidatedSettingsControl::QuickSendShortcutOrder, y);
            break;
        default:
            break;
        }

        result.requiredContentHeight = std::max(contentTop, y + margin);
        result.scrollExtent = std::max(0, result.requiredContentHeight - static_cast<int>(bodyViewport.bottom));
        result.requiresScroll = result.scrollExtent > 0;
        return result;
    }
}
