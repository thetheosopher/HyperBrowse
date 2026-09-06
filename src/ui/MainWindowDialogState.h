#pragma once

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <array>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "browser/BrowserModel.h"
#include "ui/DialogTheme.h"
#include "util/ResourceSizing.h"
#include "util/UiTextSize.h"
#include "viewer/ViewerWindow.h"

namespace hyperbrowse::cache
{
    class CachedThumbnail;
}

namespace hyperbrowse::browser
{
    enum class ThumbnailSizePreset : int;
}

namespace hyperbrowse::ui::dialog_detail
{
    enum class ConsolidatedSettingsPage : std::size_t
    {
        Slideshow = 0,
        Viewer,
        Appearance,
        Performance,
        Behavior,
        Count,
    };

    enum class ConsolidatedSettingsControl : std::size_t
    {
        TransitionEnabled,
        TransitionStyle,
        SlideshowDuration,
        SlideshowDurationSpin,
        TransitionDuration,
        TransitionDurationSpin,
        ViewerWheelZoom,
        ViewerWheelNavigate,
        InvertKeyboardPanning,
        RawPairingEnabled,
        RawPreferJpeg,
        RawPreferRaw,
        SecondaryMonitor,
        InfoOverlays,
        FullMetadata,
        OverlayTextSize,
        ThemeLight,
        ThemeDark,
        AppTextSize,
        ThumbnailSize,
        ThumbnailDetails,
        CompactLayout,
        DetailsPanel,
        ResourceProfile,
        PersistentCache,
        ThumbnailCache,
        ThumbnailCacheAutomatic,
        MetadataCache,
        MetadataCacheAutomatic,
        PressureStatus,
        NvJpeg,
        LibRawOutOfProcess,
        RecursiveBrowsing,
        ShowSubfolders,
        CloseOnEscape,
        SingleInstance,
        EscapeKeyBehavior,
        WindowedFullMetadata,
        FullScreenFullMetadata,
        PrefetchDepth,
        PrefetchDepthAutomatic,
        Count,
    };

    struct AboutDialogState
    {
        HWND ownerWindow{};
        HWND githubButton{};
        HWND supportButton{};
        HWND okButton{};
        int githubButtonWidth{};
        int supportButtonWidth{};
        HINSTANCE instance{};
        HFONT titleFont{};
        HFONT subtitleFont{};
        HFONT bodyFont{};
        HFONT footerFont{};
        HICON heroIcon{};
        HICON windowIcon{};
        hyperbrowse::util::AppTextSize appTextSize{hyperbrowse::util::kDefaultAppTextSize};
        bool darkMode{};
        bool done{};
        COLORREF background{};
        COLORREF headerBackground{};
        COLORREF footerBackground{};
        COLORREF panelBackground{};
        COLORREF border{};
        COLORREF text{};
        COLORREF mutedText{};
        COLORREF accent{};
        std::wstring title;
        std::wstring subtitle;
        std::wstring intro;
        std::wstring bodyHeading;
        std::wstring bodyContent;
        std::wstring footer;
        std::shared_ptr<const hyperbrowse::cache::CachedThumbnail> brandArt;
        Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> d2dRenderTarget;
        Microsoft::WRL::ComPtr<ID2D1Bitmap> d2dBrandArtBitmap;
        Microsoft::WRL::ComPtr<IDWriteTextFormat> d2dTitleFormat;
        Microsoft::WRL::ComPtr<IDWriteTextFormat> d2dSubtitleFormat;
        Microsoft::WRL::ComPtr<IDWriteTextFormat> d2dBodyFormat;
        Microsoft::WRL::ComPtr<IDWriteTextFormat> d2dFooterFormat;
    };

    struct AboutDialogLinkPalette
    {
        COLORREF fill{};
        COLORREF border{};
        COLORREF text{};
    };

    struct ShortcutReferenceState
    {
        HWND ownerWindow{};
        HWND* windowSlot{};
        HWND subtitleWindow{};
        HWND listWindow{};
        HWND closeButton{};
        HFONT bodyFont{};
        hyperbrowse::util::AppTextSize appTextSize{hyperbrowse::util::kDefaultAppTextSize};
        UINT dpi{96};
        bool darkMode{};
        COLORREF background{};
        COLORREF listBackground{};
        COLORREF text{};
        COLORREF mutedText{};
        COLORREF border{};
        HBRUSH backgroundBrush{};
        HBRUSH listBackgroundBrush{};
    };

    struct PerformanceSettingsDialogState
    {
        HWND ownerWindow{};
        HFONT titleFont{};
        HFONT bodyFont{};
        hyperbrowse::ui::DialogTheme theme{};
        HBRUSH backgroundBrush{};
        HBRUSH fieldBrush{};
        hyperbrowse::util::AppTextSize appTextSize{hyperbrowse::util::kDefaultAppTextSize};
        HWND instructionWindow{};
        HWND summaryWindow{};
        HWND thumbnailEditWindow{};
        HWND thumbnailAutoCheckWindow{};
        HWND metadataEditWindow{};
        HWND metadataAutoCheckWindow{};
        HWND pressureStatusCheckWindow{};
        HWND okButton{};
        std::wstring title;
        std::wstring instruction;
        std::wstring summary;
        std::wstring footnote;
        std::wstring thumbnailCacheText;
        std::wstring metadataCacheText;
        std::size_t thumbnailCacheCapacityOverrideBytes{};
        std::size_t metadataCacheCapacityOverrideEntries{};
        bool thumbnailCacheAutomatic{true};
        bool metadataCacheAutomatic{true};
        bool showPressureStateInStatusBar{};
        bool accepted{};
        bool done{};
    };

    struct FileAssociationsDialogState
    {
        HWND ownerWindow{};
        HFONT bodyFont{};
        hyperbrowse::ui::DialogTheme theme{};
        HBRUSH backgroundBrush{};
        hyperbrowse::util::AppTextSize appTextSize{hyperbrowse::util::kDefaultAppTextSize};
        HWND firstFormatWindow{};
        HWND okButton{};
        std::vector<HWND> formatCheckWindows;
        std::vector<HWND> formatDescriptionWindows;
        std::vector<bool> initialDefaults;
        std::vector<bool> checkedDefaults;
        std::vector<bool> selectedDefaults;
        std::wstring title;
        std::wstring instruction;
        std::wstring footnote;
        bool darkMode{};
        bool accepted{};
        bool done{};
    };

    struct FileAssociationsDialogLayoutMetrics
    {
        int margin{};
        int contentWidth{};
        int instructionTop{};
        int instructionHeight{};
        int actionTop{};
        int actionGap{};
        int selectAllWidth{};
        int clearAllWidth{};
        int defaultAppsButtonWidth{};
        int buttonHeight{};
        int defaultAppsButtonHeight{};
        int formatGroupTop{};
        int formatGroupHeight{};
        int formatRowHeight{};
        int formatGroupContentTop{};
        int footnoteTop{};
        int footnoteHeight{};
        int dividerTop{};
        int buttonTop{};
        int buttonRowHeight{};
        int minimumClientHeight{};
    };

    struct PerformanceSettingsDialogLayoutMetrics
    {
        int margin{};
        int contentLeft{};
        int contentWidth{};
        int sectionInset{};
        int controlGap{};
        int titleTop{};
        int titleHeight{};
        int instructionTop{};
        int instructionHeight{};
        int summaryGroupTop{};
        int summaryInnerWidth{};
        int summaryHeight{};
        int summaryGroupHeight{};
        int cacheGroupTop{};
        int cacheGroupHeight{};
        int rowLabelLeft{};
        int rowValueLeft{};
        int rowUnitLeft{};
        int rowCheckboxLeft{};
        int labelWidth{};
        int editWidth{};
        int unitWidth{};
        int checkboxWidth{};
        int rowHeight{};
        int checkboxHeight{};
        int firstRowTop{};
        int secondRowTop{};
        int pressureStatusTop{};
        int footnoteTop{};
        int minimumFootnoteHeight{};
        int buttonHeight{};
        int applyButtonWidth{};
        int cancelButtonWidth{};
        int minimumClientWidth{};
        int minimumClientHeight{};
    };

    struct SlideshowTransitionOption
    {
        hyperbrowse::viewer::TransitionStyle style;
        const wchar_t* label;
    };

    struct EscapeKeyBehaviorOption
    {
        hyperbrowse::viewer::EscapeKeyBehavior behavior;
        const wchar_t* label;
    };

    struct SlideshowSettingsDialogState
    {
        HWND ownerWindow{};
        HWND transitionComboWindow{};
        HWND durationEditWindow{};
        HWND durationSpinWindow{};
        HWND transitionDurationEditWindow{};
        HWND transitionDurationSpinWindow{};
        HWND okButton{};
        HFONT bodyFont{};
        hyperbrowse::ui::DialogTheme theme{};
        HBRUSH backgroundBrush{};
        HBRUSH fieldBrush{};
        hyperbrowse::util::AppTextSize appTextSize{hyperbrowse::util::kDefaultAppTextSize};
        std::wstring title;
        std::wstring instruction;
        std::wstring footnote;
        UINT slideshowDurationMs{3000};
        UINT transitionDurationMs{350};
        hyperbrowse::viewer::TransitionStyle transitionStyle{hyperbrowse::viewer::TransitionStyle::Crossfade};
        int dialogHeight{};
        bool accepted{};
        bool done{};
    };

    struct ConsolidatedSettingsDialogState
    {
        HWND ownerWindow{};
        HINSTANCE instance{};
        HWND dialogWindow{};
        HWND tabWindow{};
        HFONT bodyFont{};
        hyperbrowse::ui::DialogTheme theme{};
        HBRUSH backgroundBrush{};
        HBRUSH fieldBrush{};
        std::wstring title;
        std::array<std::vector<HWND>, static_cast<std::size_t>(ConsolidatedSettingsPage::Count)> pageControls;
        std::array<HWND, static_cast<std::size_t>(ConsolidatedSettingsControl::Count)> controls{};
        hyperbrowse::util::AppTextSize appTextSize{hyperbrowse::util::kDefaultAppTextSize};
        bool darkTheme{};
        hyperbrowse::util::ResourceProfile resourceProfile{hyperbrowse::util::ResourceProfile::Balanced};
        int prefetchDepthOverride{hyperbrowse::util::kAutomaticPrefetchDepth};
        hyperbrowse::browser::ThumbnailSizePreset thumbnailSizePreset{static_cast<hyperbrowse::browser::ThumbnailSizePreset>(192)};
        hyperbrowse::viewer::MouseWheelBehavior viewerMouseWheelBehavior{hyperbrowse::viewer::MouseWheelBehavior::Zoom};
        hyperbrowse::viewer::EscapeKeyBehavior viewerEscapeKeyBehavior{hyperbrowse::viewer::EscapeKeyBehavior::Close};
        bool invertKeyboardPanning{};
        hyperbrowse::viewer::TransitionStyle slideshowTransitionStyle{hyperbrowse::viewer::TransitionStyle::Crossfade};
        hyperbrowse::viewer::InfoOverlayTextSize overlayTextSize{hyperbrowse::viewer::InfoOverlayTextSize::Small};
        hyperbrowse::browser::RawJpegDisplayPreference pairedRawJpegViewerPreference{hyperbrowse::browser::RawJpegDisplayPreference::Raw};
        UINT slideshowIntervalMs{3000};
        UINT slideshowTransitionDurationMs{350};
        std::size_t thumbnailCacheCapacityOverrideBytes{};
        std::size_t metadataCacheCapacityOverrideEntries{};
        bool useSlideshowTransition{};
        bool infoOverlaysVisible{};
        bool windowedFullMetadataVisible{};
        bool fullScreenFullMetadataVisible{};
        bool compactThumbnailLayout{true};
        bool thumbnailDetailsVisible{true};
        bool detailsStripVisible{true};
        bool recursiveBrowsingEnabled{};
        bool showSubfoldersInBrowser{};
        bool rawJpegPairedOperationsEnabled{};
        bool defaultViewerToSecondaryMonitor{};
        bool persistentThumbnailCacheEnabled{true};
        bool showPressureStateInStatusBar{};
        bool nvJpegEnabled{};
        bool libRawOutOfProcessEnabled{true};
        bool closeMainWindowOnEscape{};
        bool singleInstanceEnabled{};
        bool secondaryMonitorAvailable{true};
        bool nvJpegAvailable{};
        bool libRawAvailable{};
        std::function<void(const ConsolidatedSettingsDialogState&)> apply;
        bool accepted{};
        bool done{};
    };

    enum class ExperimentalSettingsDialogResult
    {
        Unavailable,
        Cancelled,
        Accepted,
    };

    struct ExperimentalSettingsLabel
    {
        RECT bounds{};
        std::wstring text;
        bool muted{};
    };

    struct ExperimentalSettingsDialogState
    {
        HWND ownerWindow{};
        HINSTANCE instance{};
        HWND dialogWindow{};
        ConsolidatedSettingsDialogState* settings{};
        Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> renderTarget;
        Microsoft::WRL::ComPtr<IDWriteTextFormat> bodyFormat;
        Microsoft::WRL::ComPtr<IDWriteTextFormat> smallFormat;
        Microsoft::WRL::ComPtr<IDWriteTextFormat> buttonFormat;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> panelBrush;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> fieldBrush;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> borderBrush;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> textBrush;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> mutedTextBrush;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> accentBrush;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> accentFillBrush;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> buttonTextBrush;
        HBRUSH editBackgroundBrush{};
        HFONT controlFont{};
        std::array<HWND, static_cast<std::size_t>(ConsolidatedSettingsControl::Count)> nativeControls{};
        std::array<RECT, static_cast<std::size_t>(ConsolidatedSettingsPage::Count)> tabRects{};
        std::array<RECT, static_cast<std::size_t>(ConsolidatedSettingsControl::Count)> controlRects{};
        std::array<HWND, 5> numericEdits{};
        std::array<HWND, 5> numericSpins{};
        std::vector<ExperimentalSettingsLabel> labels;
        RECT applyButtonRect{};
        RECT okButtonRect{};
        RECT cancelButtonRect{};
        ConsolidatedSettingsPage page{ConsolidatedSettingsPage::Slideshow};
        int hoveredControl{-1};
        int hoveredTab{-1};
        int pressedControl{-1};
        bool done{};
        bool accepted{};
    };

    struct SlideshowSettingsDialogLayoutMetrics
    {
        int margin{};
        int contentWidth{};
        int lineHeight{};
        int instructionHeight{};
        int labelWidth{};
        int valueWidth{};
        int numericEditWidth{};
        int spinWidth{};
        int controlHeight{};
        int rowGap{};
        int transitionTop{};
        int durationTop{};
        int transitionDurationTop{};
        int footnoteTop{};
        int footnoteHeight{};
        int dividerTop{};
        int buttonTop{};
        int buttonHeight{};
        int applyButtonWidth{};
        int cancelButtonWidth{};
        int minimumClientHeight{};
    };

    struct ImageInformationDialogState
    {
        HWND ownerWindow{};
        HINSTANCE instance{};
        HWND filenameWindow{};
        HWND contentWindow{};
        HWND metadataWindow{};
        HWND metadataToggleButton{};
        HWND okButton{};
        HFONT titleFont{};
        HFONT bodyFont{};
        hyperbrowse::ui::DialogTheme theme{};
        HBRUSH backgroundBrush{};
        HBRUSH fieldBrush{};
        hyperbrowse::util::AppTextSize appTextSize{hyperbrowse::util::kDefaultAppTextSize};
        std::wstring filename;
        std::wstring content;
        std::wstring metadata;
        bool expanded{};
        bool done{};
    };
}
