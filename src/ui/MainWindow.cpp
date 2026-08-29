#include "ui/MainWindow.h"

#include <commctrl.h>
#include <dwmapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <windowsx.h>
#include <richedit.h>
#include <wtsapi32.h>
#include <wrl/client.h>

#include <algorithm>
#include <cerrno>
#include <cwchar>
#include <cwctype>
#include <filesystem>
#include <functional>
#include <limits>
#include <mutex>
#include <new>
#include <string>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "app/Application.h"
#include "browser/BrowserModel.h"
#include "browser/BrowserPane.h"
#include "cache/DiskThumbnailCache.h"
#include "decode/ImageDecoder.h"
#include "services/BatchConvertService.h"
#include "services/FileAssociationService.h"
#include "services/FileOperationService.h"
#include "services/FolderEnumerationService.h"
#include "services/FolderTreeEnumerationService.h"
#include "services/FolderWatchService.h"
#include "services/ImageMetadataService.h"
#include "services/JpegTransformService.h"
#include "services/ThumbnailScheduler.h"
#include "services/UserMetadataStore.h"
#include "ui/DiagnosticsWindow.h"
#include "ui/CommandIds.h"
#include "ui/ShortcutCatalog.h"
#include "ui/ToolbarIconLibrary.h"
#include "util/BackgroundExecutor.h"
#include "util/Diagnostics.h"
#include "util/Log.h"
#include "util/ResourcePng.h"
#include "util/ResourceSizing.h"
#include "util/StringConvert.h"
#include "util/Timing.h"
#include "util/UiTextSize.h"
#include "render/GdiText.h"
#include "viewer/ViewerWindow.h"

#include "app/resource.h"
#include "HyperBrowseBuildInfo.h"

namespace fs = std::filesystem;

using namespace hyperbrowse::ui::command_ids;

namespace
{
    constexpr wchar_t kRegistryPath[] = L"Software\\HyperBrowse";
    constexpr wchar_t kRegistryValueLeftPaneWidth[] = L"LeftPaneWidth";
    constexpr wchar_t kRegistryValueBrowserMode[] = L"BrowserMode";
    constexpr wchar_t kRegistryValueThemeMode[] = L"ThemeMode";
    constexpr wchar_t kRegistryValueAppTextSize[] = L"AppTextSize";
    constexpr wchar_t kRegistryValueNvJpegEnabled[] = L"NvJpegEnabled";
    constexpr wchar_t kRegistryValueLibRawOutOfProcessEnabled[] = L"LibRawOutOfProcessEnabled";
    constexpr wchar_t kRegistryValueThumbnailSizePreset[] = L"ThumbnailSizePreset";
    constexpr wchar_t kRegistryValueCompactThumbnailLayout[] = L"CompactThumbnailLayout";
    constexpr wchar_t kRegistryValueThumbnailDetailsVisible[] = L"ThumbnailDetailsVisible";
    constexpr wchar_t kRegistryValueShowSubfoldersInBrowser[] = L"ShowSubfoldersInBrowser";
    constexpr wchar_t kRegistryValueSelectedFolderPath[] = L"SelectedFolderPath";
    constexpr wchar_t kRegistryValueSelectedImagePath[] = L"SelectedImagePath";
    constexpr wchar_t kRegistryValueWindowLeft[] = L"WindowLeft";
    constexpr wchar_t kRegistryValueWindowTop[] = L"WindowTop";
    constexpr wchar_t kRegistryValueWindowWidth[] = L"WindowWidth";
    constexpr wchar_t kRegistryValueWindowHeight[] = L"WindowHeight";
    constexpr wchar_t kRegistryValueSortMode[] = L"SortMode";
    constexpr wchar_t kRegistryValueSortAscending[] = L"SortAscending";
    constexpr wchar_t kRegistryValueSlideshowInterval[] = L"SlideshowIntervalMs";
    constexpr wchar_t kRegistryValueSlideshowTransitionStyle[] = L"SlideshowTransitionStyle";
    constexpr wchar_t kRegistryValueSlideshowTransitionDuration[] = L"SlideshowTransitionDurationMs";
    constexpr wchar_t kRegistryValueUseSlideshowTransition[] = L"UseSlideshowTransition";
    constexpr wchar_t kRegistryValueDetailsStripVisible[] = L"DetailsStripVisible";
    constexpr wchar_t kRegistryValueDetailsPanelWidth[] = L"DetailsPanelWidth";
    constexpr wchar_t kRegistryValueViewerMouseWheelBehavior[] = L"ViewerMouseWheelBehavior";
        constexpr wchar_t kRegistryValueInvertKeyboardPanning[] = L"InvertKeyboardPanning";
    constexpr wchar_t kRegistryValueRecentFolders[] = L"RecentFolders";
    constexpr wchar_t kRegistryValueRecentDestinationFolders[] = L"RecentDestinationFolders";
    constexpr wchar_t kRegistryValueFavoriteDestinationFolders[] = L"FavoriteDestinationFolders";
    constexpr wchar_t kRegistryValueLastQuickSendDestination[] = L"LastQuickSendDestination";
    constexpr wchar_t kRegistryValueQuickSendShortcutPrefix[] = L"QuickSendShortcut";
    constexpr wchar_t kRegistryValueRawJpegPairedOperationsEnabled[] = L"RawJpegPairedOperationsEnabled";
    constexpr wchar_t kRegistryValuePairedRawJpegViewerPreference[] = L"PairedRawJpegViewerPreference";
    constexpr wchar_t kRegistryValueDefaultViewerToSecondaryMonitor[] = L"DefaultViewerToSecondaryMonitor";
    constexpr wchar_t kRegistryValuePersistentThumbnailCacheEnabled[] = L"PersistentThumbnailCacheEnabled";
    constexpr wchar_t kRegistryValueResourceProfile[] = L"ResourceProfile";
    constexpr wchar_t kRegistryValueThumbnailCacheCapacityOverrideBytes[] = L"ThumbnailCacheCapacityOverrideBytes";
    constexpr wchar_t kRegistryValueMetadataCacheCapacityOverrideEntries[] = L"MetadataCacheCapacityOverrideEntries";
    constexpr wchar_t kRegistryValueShowPressureStateInStatusBar[] = L"ShowPressureStateInStatusBar";
    constexpr wchar_t kRegistryValueCloseMainWindowOnEscape[] = L"CloseMainWindowOnEscape";

    constexpr DWORD kDwmUseImmersiveDarkModeAttribute = 20;
    constexpr DWORD kDwmUseImmersiveDarkModeLegacyAttribute = 19;
    constexpr DWORD kDwmBorderColorAttribute = 34;
    constexpr DWORD kDwmCaptionColorAttribute = 35;
    constexpr DWORD kDwmTextColorAttribute = 36;

    std::wstring ToLowercaseCopy(std::wstring value);
    int CountDecimalDigits(std::size_t value);
    bool ShouldShowFolderInTree(const std::wstring& folderPath)
    {
        if (folderPath.size() == 3 && folderPath[1] == L':' && folderPath[2] == L'\\')
        {
            return true;
        }

        const DWORD attributes = GetFileAttributesW(folderPath.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_HIDDEN) == 0)
        {
            return true;
        }

        SHELLFLAGSTATE shellState{};
        SHGetSettings(&shellState, SSF_SHOWALLOBJECTS);
        return shellState.fShowAllObjects != FALSE;
    }

    bool TryBuildBatchRenamePatternLeafName(std::wstring_view pattern,
                                            const hyperbrowse::browser::BrowserItem& item,
                                            std::size_t ordinal,
                                            std::size_t selectionCount,
                                            int defaultNumberWidth,
                                            std::wstring* leafName,
                                            std::wstring* errorMessage);
    constexpr int kDetailsPanelCloseButtonSize = 18;
    constexpr int kDetailsPanelCloseButtonMargin = 8;
    constexpr int kDetailsPanelCloseButtonGap = 8;
    constexpr UINT kMemoryPressureSampledMessage = WM_APP + 72;
    constexpr UINT kPersistentThumbnailCacheMaintenanceMessage = WM_APP + 75;
    enum class PersistentThumbnailCacheMaintenanceOperation : unsigned int
    {
        Statistics = 0,
        Compact = 1,
        Purge = 2,
    };
    constexpr unsigned int kPersistentThumbnailCacheMaintenanceSuccessFlag = 4;
    constexpr std::size_t kOpenedFolderHistoryLimit = 256;
    constexpr std::size_t kInvalidHistoryIndex = static_cast<std::size_t>(-1);
    constexpr UINT_PTR kMemoryPressureTimerId = 9101;
    constexpr UINT kMemoryPressureIntervalMs = 1500;
    constexpr GUID kConsoleDisplayStateGuid{
        0x6fe69556, 0x704a, 0x47a0, {0x8f, 0x24, 0xc2, 0x8d, 0x93, 0x6f, 0xda, 0x47}};
    constexpr GUID kMonitorPowerOnGuid{
        0x02731015, 0x4510, 0x4526, {0x99, 0xe6, 0xe5, 0xa1, 0x7e, 0xbd, 0x1a, 0xea}};
    constexpr std::uint64_t kMemoryPressureAvailableBytesThreshold = 1024ULL * 1024ULL * 1024ULL;
    constexpr std::uint64_t kMemoryPressureActivateUsedPercent = 85ULL;
    constexpr std::uint64_t kMemoryPressureRecoverUsedPercent = 70ULL;
    constexpr unsigned int kMemoryPressureRecoverySamplesRequired = 2;

    constexpr int kActionStripPaddingX = 8;
    constexpr int kActionStripPaddingY = 6;
    constexpr int kToolbarItemSize = 32;
    constexpr int kToolbarIconSize = 18;
    constexpr int kToolbarDropdownChevronSize = 10;
    constexpr int kToolbarTooltipMaxWidth = 300;
    constexpr int kToolbarSeparatorWidth = 9;
    constexpr int kToolbarSeparatorGap = 4;
    constexpr int kToolbarFilterEditHeight = 24;
    constexpr int kFilterEditMinWidth = 160;
    constexpr int kDetailsStripHeight = 22;
    constexpr UINT kStatusStripControlId = 5001;
    constexpr int kStatusStripHeight = 28;
    constexpr int kStatusStripHorizontalPadding = 12;
    constexpr int kMenuPopupItemHeight = 28;
    constexpr int kMenuPopupSeparatorHeight = 10;
    constexpr int kMenuPopupCheckColumnWidth = 24;
    constexpr int kMenuPopupTextPadding = 12;
    constexpr int kMenuPopupShortcutGap = 24;
    constexpr int kMenuPopupMeasurementAllowance = 8;
    constexpr int kMenuPopupArrowWidth = 12;
    constexpr int kCommandBarMenuButtonGap = 4;
    constexpr int kCommandBarMenuButtonPadding = 12;
    constexpr int kCommandBarMenuButtonMinWidth = 56;
    constexpr int kCommandBarMenuChevronWidth = 8;
    constexpr int kDetailsPanelPreferredWidth = 340;
    constexpr int kDetailsPanelMinWidth = 250;
    constexpr int kDetailsPanelMargin = 14;
    constexpr int kDetailsPanelTabHeight = 30;
    constexpr int kDetailsPanelTabGap = 10;
    constexpr int kDetailsPanelTabButtonGap = 8;
    constexpr int kDetailsPanelTabButtonHorizontalPadding = 16;
    constexpr int kDetailsPanelTabMinButtonWidth = 96;
    constexpr int kDetailsPanelHistogramHeight = 88;
    constexpr int kDetailsPanelSectionGap = 12;
    constexpr int kDetailsPanelTextTopGap = 14;
    constexpr int kDetailsPanelHistogramBins = 64;
    constexpr int kQuickAccessPanelHeaderHeight = 18;
    constexpr int kQuickAccessPanelTopGap = 12;
    constexpr int kQuickAccessPanelRowHeight = 40;
    constexpr int kQuickAccessPanelRowGap = 6;
    constexpr int kQuickAccessPanelButtonWidth = 56;
    constexpr int kQuickAccessPanelButtonGap = 8;
    constexpr int kQuickAccessPanelButtonRightInset = 8;
    constexpr int kQuickAccessPanelRemoveButtonWidth = 24;
    constexpr int kQuickAccessPanelHeaderVerticalPadding = 4;
    constexpr int kQuickAccessPanelRowTextTopInset = 5;
    constexpr int kQuickAccessPanelRowTextGap = 4;
    constexpr int kQuickAccessPanelRowBottomInset = 6;
    constexpr int kQuickAccessPanelButtonVerticalInset = 6;
    constexpr int kQuickAccessPanelShortcutWidth = 24;
    constexpr int kQuickAccessPanelShortcutGap = 8;
    constexpr int kQuickAccessPanelSortButtonSize = 18;
    constexpr int kQuickAccessPanelSortButtonGap = 6;
    constexpr int kQuickAccessPanelScrollBarGap = 6;
    constexpr UINT kQuickAccessShortcutEditBaseId = 5200;
    constexpr UINT_PTR kQuickAccessSortTooltipId = static_cast<UINT_PTR>(-1);
    constexpr UINT_PTR kDetailsPanelHistogramTooltipId = static_cast<UINT_PTR>(-2);
    constexpr UINT kQuickSendPopupCommandBase = 5300;
    constexpr UINT kQuickSendPopupBrowseCommand = kQuickSendPopupCommandBase;
    constexpr UINT kQuickSendPopupDestinationBase = kQuickSendPopupCommandBase + 1;
    constexpr std::size_t kIncrementalFolderWatchEventLimit = 64;
    constexpr std::size_t kIncrementalFileOperationPathLimit = 64;
    constexpr UINT_PTR kFolderEnumerationPresentationTimerId = 9102;
    constexpr UINT kFolderEnumerationPresentationIntervalMs = 50;
    HWND FindPopupMenuWindow(HMENU menu)
    {
        struct SearchContext
        {
            HMENU menu{};
            HWND window{};
        } context{menu};

        EnumThreadWindows(GetCurrentThreadId(), [](HWND window, LPARAM lParam)
        {
            auto* context = reinterpret_cast<SearchContext*>(lParam);
            if (!context || GetMenu(window) != context->menu)
            {
                return TRUE;
            }

            wchar_t className[32]{};
            if (GetClassNameW(window, className, static_cast<int>(std::size(className))) > 0
                && _wcsicmp(className, L"#32768") == 0)
            {
                context->window = window;
                return FALSE;
            }

            return TRUE;
        }, reinterpret_cast<LPARAM>(&context));
        return context.window;
    }

    constexpr wchar_t kTextInputDialogClassName[] = L"HyperBrowseTextInputDialog";
    constexpr std::array kThumbnailSizePresets{
        hyperbrowse::browser::ThumbnailSizePreset::Pixels96,
        hyperbrowse::browser::ThumbnailSizePreset::Pixels128,
        hyperbrowse::browser::ThumbnailSizePreset::Pixels160,
        hyperbrowse::browser::ThumbnailSizePreset::Pixels192,
        hyperbrowse::browser::ThumbnailSizePreset::Pixels256,
        hyperbrowse::browser::ThumbnailSizePreset::Pixels320,
        hyperbrowse::browser::ThumbnailSizePreset::Pixels360,
        hyperbrowse::browser::ThumbnailSizePreset::Pixels420,
        hyperbrowse::browser::ThumbnailSizePreset::Pixels480,
        hyperbrowse::browser::ThumbnailSizePreset::Pixels560,
        hyperbrowse::browser::ThumbnailSizePreset::Pixels640,
    };
    constexpr int kTextInputDialogWidth = 440;
    constexpr int kTextInputDialogHeight = 160;
    constexpr int kTextInputDialogMargin = 14;
    constexpr int kTextInputDialogInstructionMinHeight = 42;
    constexpr int kTextInputDialogEditTopGap = 8;
    constexpr int kTextInputDialogDividerTopGap = 14;
    constexpr int kTextInputDialogButtonTopGap = 10;
    constexpr int kTextInputEditHeight = 24;
    constexpr int kTextInputButtonWidth = 88;
    constexpr int kTextInputButtonHeight = 28;
    constexpr int kTextInputEditControlId = 100;
    constexpr wchar_t kBatchRenameDialogClassName[] = L"HyperBrowseBatchRenameDialog";
    constexpr int kBatchRenameDialogWidth = 760;
    constexpr int kBatchRenameDialogHeight = 460;
    constexpr int kBatchRenamePatternEditControlId = 200;
    constexpr int kBatchRenamePreviewListControlId = 201;
    constexpr int kBatchRenameInstructionControlId = 202;
    constexpr int kBatchRenameHelpControlId = 203;
    constexpr wchar_t kAboutDialogClassName[] = L"HyperBrowseAboutDialog";
    constexpr wchar_t kShortcutReferenceClassName[] = L"HyperBrowseShortcutReference";
    constexpr int kShortcutReferenceWidth = 820;
    constexpr int kShortcutReferenceHeight = 620;
    constexpr int kShortcutReferenceMinimumWidth = 560;
    constexpr int kShortcutReferenceMinimumHeight = 360;
    constexpr int kShortcutReferenceMargin = 18;
    constexpr int kShortcutReferenceSubtitleHeight = 24;
    constexpr int kShortcutReferenceControlGap = 8;
    constexpr int kShortcutReferenceButtonWidth = 88;
    constexpr int kShortcutReferenceButtonHeight = 28;
    constexpr int kShortcutReferenceListControlId = 401;
    constexpr wchar_t kAboutDialogGitHubLabel[] = L"GitHub Project";
    constexpr wchar_t kAboutDialogSupportLabel[] = L"Buy Me A Coffee";

    struct MemoryPressureSampleResult
    {
        bool pressureDetected{};
        bool recoveryCandidate{};
    };

    constexpr wchar_t kAboutDialogGitHubUrl[] = L"https://github.com/thetheosopher/HyperBrowse";
    constexpr wchar_t kAboutDialogSupportUrl[] = L"https://buymeacoffee.com/theosopher";
    constexpr int kAboutDialogWidth = 1180;
    constexpr int kAboutDialogHeight = 720;
    constexpr int kAboutDialogMargin = 36;
    constexpr int kAboutDialogHeaderHeight = 224;
    constexpr int kAboutDialogFooterHeight = 96;
    constexpr int kAboutDialogButtonWidth = 104;
    constexpr int kAboutDialogButtonHeight = 38;
    constexpr std::size_t kQuickAccessFolderLimit = 8;
    constexpr std::size_t kFavoriteDestinationLimit = hyperbrowse::ui::kQuickSendShortcutCount;
    constexpr int kAboutDialogLinkButtonWidth = 172;
    constexpr int kAboutDialogSupportButtonWidth = 196;
    constexpr int kAboutDialogButtonGap = 12;
    constexpr int kAboutDialogBrandArtSize = 152;
    constexpr wchar_t kPerformanceSettingsDialogClassName[] = L"HyperBrowsePerformanceSettingsDialog";
    constexpr int kPerformanceSettingsDialogWidth = 640;
    constexpr int kPerformanceSettingsDialogHeight = 432;
    constexpr int kPerformanceSettingsDialogLabelWidth = 210;
    constexpr int kPerformanceSettingsDialogEditWidth = 118;
    constexpr int kPerformanceSettingsDialogUnitWidth = 72;
    constexpr int kPerformanceSettingsDialogCheckboxWidth = 124;
    constexpr int kPerformanceSettingsDialogValueTopGap = 14;
    constexpr int kPerformanceSettingsDialogControlGap = 10;
    constexpr int kPerformanceSettingsDialogSectionInset = 14;
    constexpr int kPerformanceSettingsDialogTitleControlId = 300;
    constexpr int kPerformanceSettingsInstructionControlId = 301;
    constexpr int kPerformanceSettingsSummaryGroupControlId = 302;
    constexpr int kPerformanceSettingsSummaryControlId = 303;
    constexpr int kPerformanceSettingsCacheGroupControlId = 304;
    constexpr int kPerformanceSettingsThumbnailLabelControlId = 305;
    constexpr int kPerformanceSettingsThumbnailEditControlId = 306;
    constexpr int kPerformanceSettingsThumbnailUnitControlId = 307;
    constexpr int kPerformanceSettingsThumbnailAutoControlId = 308;
    constexpr int kPerformanceSettingsMetadataLabelControlId = 309;
    constexpr int kPerformanceSettingsMetadataEditControlId = 310;
    constexpr int kPerformanceSettingsMetadataUnitControlId = 311;
    constexpr int kPerformanceSettingsMetadataAutoControlId = 312;
    constexpr int kPerformanceSettingsPressureStatusControlId = 313;
    constexpr int kPerformanceSettingsFootnoteControlId = 314;
    constexpr int kPerformanceSettingsDividerControlId = 315;
    constexpr wchar_t kSlideshowSettingsDialogClassName[] = L"HyperBrowseSlideshowSettingsDialog";
    constexpr int kSlideshowSettingsDialogWidth = 560;
    constexpr int kSlideshowSettingsDialogHeight = 368;
    constexpr int kSlideshowSettingsInstructionControlId = 340;
    constexpr int kSlideshowSettingsTransitionLabelControlId = 341;
    constexpr int kSlideshowSettingsTransitionComboControlId = 342;
    constexpr int kSlideshowSettingsDurationLabelControlId = 343;
    constexpr int kSlideshowSettingsDurationEditControlId = 344;
    constexpr int kSlideshowSettingsDurationUnitControlId = 345;
    constexpr int kSlideshowSettingsDurationSpinControlId = 351;
    constexpr int kSlideshowSettingsTransitionDurationLabelControlId = 346;
    constexpr int kSlideshowSettingsTransitionDurationEditControlId = 347;
    constexpr int kSlideshowSettingsTransitionDurationUnitControlId = 348;
    constexpr int kSlideshowSettingsTransitionDurationSpinControlId = 352;
    constexpr int kSlideshowSettingsFootnoteControlId = 349;
    constexpr int kSlideshowSettingsDividerControlId = 350;
    constexpr UINT kSlideshowMinimumDurationMs = 250U;
    constexpr UINT kSlideshowMaximumDurationMs = 60000U;
    constexpr UINT kSlideshowMinimumTransitionDurationMs = 100U;
    constexpr UINT kSlideshowMaximumTransitionDurationMs = 5000U;
    constexpr wchar_t kConsolidatedSettingsDialogClassName[] = L"HyperBrowseConsolidatedSettingsDialog";
    constexpr int kConsolidatedSettingsDialogWidth = 900;
    constexpr int kConsolidatedSettingsDialogHeight = 600;
    constexpr int kConsolidatedSettingsTabControlId = 360;
    constexpr int kConsolidatedSettingsFirstControlId = 5000;
    constexpr int kConsolidatedSettingsMargin = 18;
    constexpr int kConsolidatedSettingsButtonWidth = 84;
    constexpr int kConsolidatedSettingsButtonHeight = 30;
    constexpr int kConsolidatedSettingsButtonGap = 10;

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
        Count,
    };

    constexpr int ConsolidatedSettingsControlId(ConsolidatedSettingsControl control)
    {
        return kConsolidatedSettingsFirstControlId + static_cast<int>(control);
    }

    constexpr wchar_t kFileAssociationsDialogClassName[] = L"HyperBrowseFileAssociationsDialog";
    constexpr int kFileAssociationsDialogWidth = 760;
    constexpr int kFileAssociationsDialogHeight = 500;
    constexpr int kFileAssociationsDialogMargin = 20;
    constexpr int kFileAssociationsDialogInstructionControlId = 361;
    constexpr int kFileAssociationsDialogFormatGroupControlId = 362;
    constexpr int kFileAssociationsDialogSelectAllControlId = 363;
    constexpr int kFileAssociationsDialogClearAllControlId = 364;
    constexpr int kFileAssociationsDialogFootnoteControlId = 365;
    constexpr int kFileAssociationsDialogDividerControlId = 366;
    constexpr int kFileAssociationsDialogDefaultAppsControlId = 367;
    constexpr int kFileAssociationsDialogFormatBaseControlId = 380;
    constexpr int kFileAssociationsDialogButtonWidth = 88;
    constexpr int kFileAssociationsDialogButtonHeight = 28;
    constexpr int kFileAssociationsDialogDefaultAppsButtonWidth = 124;
    constexpr int kFileAssociationsDialogDefaultAppsButtonHeight = 32;
    constexpr int kFileAssociationsDialogFormatRowHeight = 28;
    constexpr int kFileAssociationsDialogFormatCheckboxWidth = 72;

    hyperbrowse::cache::ThumbnailCacheKey MakeThumbnailCacheKey(const hyperbrowse::browser::BrowserItem& item,
                                                                int targetWidth,
                                                                int targetHeight)
    {
        hyperbrowse::cache::ThumbnailCacheKey key;
        key.filePath = item.filePath;
        key.modifiedTimestampUtc = item.modifiedTimestampUtc;
        key.targetWidth = targetWidth;
        key.targetHeight = targetHeight;
        return key;
    }

    std::wstring FormatQuickAccessImageCount(std::uintmax_t imageCount)
    {
        return std::to_wstring(imageCount) + (imageCount == 1 ? L" image" : L" images");
    }

    bool TryCountSupportedImagesInFolder(std::wstring_view folderPath, std::uintmax_t* imageCount)
    {
        if (!imageCount)
        {
            return false;
        }

        *imageCount = 0;

        std::error_code error;
        const fs::path directoryPath(folderPath);
        if (!fs::is_directory(directoryPath, error) || error)
        {
            return false;
        }

        for (fs::directory_iterator iterator(directoryPath, fs::directory_options::skip_permission_denied, error), end;
             iterator != end;
             iterator.increment(error))
        {
            if (error)
            {
                return false;
            }

            std::error_code statusError;
            if (!iterator->is_regular_file(statusError) || statusError)
            {
                continue;
            }

            if (hyperbrowse::browser::IsSupportedImageExtension(iterator->path().extension().wstring()))
            {
                ++(*imageCount);
            }
        }

        return true;
    }

    std::wstring BuildQuickAccessDestinationMetadata(std::wstring_view folderPath, bool favorite, bool currentFolder)
    {
        std::wstring metadata = favorite ? L"Favorite destination" : L"Recent destination";

        std::uintmax_t imageCount = 0;
        metadata.append(L" | ");
        if (TryCountSupportedImagesInFolder(folderPath, &imageCount))
        {
            metadata.append(FormatQuickAccessImageCount(imageCount));
        }
        else
        {
            metadata.append(L"folder unavailable");
        }

        metadata.append(currentFolder ? L" | Current folder" : L" | Drop here");
        return metadata;
    }

    std::vector<std::wstring> CollectShellDropPaths(HDROP dropHandle)
    {
        std::vector<std::wstring> paths;
        if (!dropHandle)
        {
            return paths;
        }

        const UINT pathCount = DragQueryFileW(dropHandle, 0xFFFFFFFFu, nullptr, 0);
        paths.reserve(pathCount);
        for (UINT index = 0; index < pathCount; ++index)
        {
            const UINT pathLength = DragQueryFileW(dropHandle, index, nullptr, 0);
            if (pathLength == 0)
            {
                continue;
            }

            std::wstring path(pathLength + 1, L'\0');
            const UINT copiedLength = DragQueryFileW(dropHandle, index, path.data(), pathLength + 1);
            path.resize(copiedLength);
            if (!path.empty())
            {
                paths.push_back(std::move(path));
            }
        }

        return paths;
    }

    // Reads a CF_HDROP out of an OLE IDataObject (as opposed to the raw HDROP the
    // legacy WM_DROPFILES path receives).
    std::vector<std::wstring> CollectShellDropPathsFromDataObject(IDataObject* dataObject)
    {
        std::vector<std::wstring> paths;
        if (!dataObject)
        {
            return paths;
        }

        FORMATETC format{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        STGMEDIUM storage{};
        if (FAILED(dataObject->GetData(&format, &storage)) || !storage.hGlobal)
        {
            return paths;
        }

        paths = CollectShellDropPaths(static_cast<HDROP>(storage.hGlobal));
        ReleaseStgMedium(&storage);
        return paths;
    }

    struct TextInputDialogState
    {
        HWND ownerWindow{};
        HWND editWindow{};
        HWND okButton{};
        HFONT bodyFont{};
        hyperbrowse::util::AppTextSize appTextSize{hyperbrowse::util::kDefaultAppTextSize};
        std::wstring title;
        std::wstring instruction;
        std::wstring confirmLabel;
        std::wstring initialText;
        std::wstring resultText;
        int selectionStart{};
        int selectionEnd{};
        bool accepted{};
        bool done{};
    };

    struct TextInputDialogLayoutMetrics
    {
        int clientWidth{};
        int contentWidth{};
        int okButtonWidth{};
        int cancelButtonWidth{};
        int instructionHeight{};
        int editTop{};
        int dividerTop{};
        int buttonTop{};
        int clientHeight{};
    };

    struct BatchRenamePreviewRow
    {
        std::wstring currentLeafName;
        std::wstring renamedLeafName;
        std::wstring status;
        bool valid{true};
    };

    struct BatchRenameDialogState
    {
        HWND ownerWindow{};
        HWND patternEditWindow{};
        HWND previewListWindow{};
        HWND okButton{};
        HFONT bodyFont{};
        hyperbrowse::util::AppTextSize appTextSize{hyperbrowse::util::kDefaultAppTextSize};
        std::wstring title;
        std::wstring instruction;
        std::wstring initialPattern;
        std::wstring pattern;
        std::vector<hyperbrowse::browser::BrowserItem> items;
        std::vector<std::wstring> resultLeafNames;
        std::vector<BatchRenamePreviewRow> previewRows;
        int numberWidth{};
        bool canAccept{};
        bool accepted{};
        bool done{};
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
        hyperbrowse::util::AppTextSize appTextSize{hyperbrowse::util::kDefaultAppTextSize};
        HWND firstFormatWindow{};
        HWND okButton{};
        std::vector<HWND> formatCheckWindows;
        std::vector<HWND> formatDescriptionWindows;
        std::vector<bool> initialDefaults;
        std::vector<bool> selectedDefaults;
        std::wstring title;
        std::wstring instruction;
        std::wstring footnote;
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
        std::wstring title;
        std::array<std::vector<HWND>, static_cast<std::size_t>(ConsolidatedSettingsPage::Count)> pageControls;
        std::array<HWND, static_cast<std::size_t>(ConsolidatedSettingsControl::Count)> controls{};
        hyperbrowse::util::AppTextSize appTextSize{hyperbrowse::util::kDefaultAppTextSize};
        bool darkTheme{};
        hyperbrowse::util::ResourceProfile resourceProfile{hyperbrowse::util::ResourceProfile::Balanced};
        hyperbrowse::browser::ThumbnailSizePreset thumbnailSizePreset{static_cast<hyperbrowse::browser::ThumbnailSizePreset>(192)};
        hyperbrowse::viewer::MouseWheelBehavior viewerMouseWheelBehavior{hyperbrowse::viewer::MouseWheelBehavior::Zoom};
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
        bool fullMetadataVisible{};
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

    bool LaunchShellTarget(HWND ownerWindow, const wchar_t* verb, std::wstring_view target);
    bool IsWindows11OrGreater();
    bool LaunchDefaultAppsSettings(HWND ownerWindow);

    HWND CreateConsolidatedSettingsControl(ConsolidatedSettingsDialogState& state,
                                           ConsolidatedSettingsPage page,
                                           const wchar_t* className,
                                           const wchar_t* text,
                                           DWORD style,
                                           int x,
                                           int y,
                                           int width,
                                           int height,
                                           int controlId = 0,
                                           ConsolidatedSettingsControl control = ConsolidatedSettingsControl::Count)
    {
        HWND window = CreateWindowExW(
            0,
            className,
            text,
            style | WS_CHILD | WS_VISIBLE,
            x,
            y,
            width,
            height,
            state.dialogWindow,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(controlId)),
            state.instance,
            nullptr);
        if (window)
        {
            const std::size_t pageIndex = static_cast<std::size_t>(page);
            if (pageIndex < state.pageControls.size())
            {
                state.pageControls[pageIndex].push_back(window);
            }
            if (control != ConsolidatedSettingsControl::Count)
            {
                state.controls[static_cast<std::size_t>(control)] = window;
            }
        }
        return window;
    }

    bool TryReadDwordValue(HKEY key, const wchar_t* valueName, DWORD* value)
    {
        DWORD size = sizeof(*value);
        DWORD type = REG_DWORD;
        return RegQueryValueExW(key, valueName, nullptr, &type, reinterpret_cast<LPBYTE>(value), &size) == ERROR_SUCCESS
            && type == REG_DWORD;
    }

    bool HasNvJpegCapability()
    {
        return hyperbrowse::decode::IsNvJpegBuildEnabled()
            && hyperbrowse::decode::IsNvJpegRuntimeAvailable();
    }

    void WriteDwordValue(HKEY key, const wchar_t* valueName, DWORD value)
    {
        RegSetValueExW(key, valueName, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
    }

    bool TryReadPersistedWindowBounds(HKEY key, RECT* bounds)
    {
        if (!bounds)
        {
            return false;
        }

        DWORD leftValue = 0;
        DWORD topValue = 0;
        DWORD widthValue = 0;
        DWORD heightValue = 0;
        if (!TryReadDwordValue(key, kRegistryValueWindowLeft, &leftValue)
            || !TryReadDwordValue(key, kRegistryValueWindowTop, &topValue)
            || !TryReadDwordValue(key, kRegistryValueWindowWidth, &widthValue)
            || !TryReadDwordValue(key, kRegistryValueWindowHeight, &heightValue))
        {
            return false;
        }

        if (widthValue > static_cast<DWORD>(std::numeric_limits<LONG>::max())
            || heightValue > static_cast<DWORD>(std::numeric_limits<LONG>::max()))
        {
            return false;
        }

        const LONG left = static_cast<LONG>(leftValue);
        const LONG top = static_cast<LONG>(topValue);
        const LONG width = static_cast<LONG>(widthValue);
        const LONG height = static_cast<LONG>(heightValue);
        if (width <= 0 || height <= 0)
        {
            return false;
        }

        const long long right = static_cast<long long>(left) + static_cast<long long>(width);
        const long long bottom = static_cast<long long>(top) + static_cast<long long>(height);
        if (right < static_cast<long long>(std::numeric_limits<LONG>::min())
            || right > static_cast<long long>(std::numeric_limits<LONG>::max())
            || bottom < static_cast<long long>(std::numeric_limits<LONG>::min())
            || bottom > static_cast<long long>(std::numeric_limits<LONG>::max()))
        {
            return false;
        }

        bounds->left = left;
        bounds->top = top;
        bounds->right = static_cast<LONG>(right);
        bounds->bottom = static_cast<LONG>(bottom);
        return true;
    }

    bool IsPersistedWindowBoundsValid(const RECT& bounds, LONG minimumWidth, LONG minimumHeight)
    {
        const LONG width = bounds.right - bounds.left;
        const LONG height = bounds.bottom - bounds.top;
        if (width < minimumWidth || height < minimumHeight)
        {
            return false;
        }

        const HMONITOR monitor = MonitorFromRect(&bounds, MONITOR_DEFAULTTONULL);
        if (!monitor)
        {
            return false;
        }

        MONITORINFO monitorInfo{};
        monitorInfo.cbSize = sizeof(monitorInfo);
        if (!GetMonitorInfoW(monitor, &monitorInfo))
        {
            return false;
        }

        return bounds.left >= monitorInfo.rcWork.left
            && bounds.top >= monitorInfo.rcWork.top
            && bounds.right <= monitorInfo.rcWork.right
            && bounds.bottom <= monitorInfo.rcWork.bottom;
    }

    bool TryReadQwordValue(HKEY key, const wchar_t* valueName, std::uint64_t* value)
    {
        if (!value)
        {
            return false;
        }

        DWORD size = sizeof(*value);
        DWORD type = REG_QWORD;
        return RegQueryValueExW(key, valueName, nullptr, &type, reinterpret_cast<LPBYTE>(value), &size) == ERROR_SUCCESS
            && type == REG_QWORD;
    }

    void WriteQwordValue(HKEY key, const wchar_t* valueName, std::uint64_t value)
    {
        RegSetValueExW(key, valueName, 0, REG_QWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
    }

    std::wstring ReadWindowText(HWND hwnd)
    {
        if (!hwnd)
        {
            return {};
        }

        const int textLength = GetWindowTextLengthW(hwnd);
        std::wstring text(static_cast<std::size_t>(textLength) + 1, L'\0');
        GetWindowTextW(hwnd, text.data(), static_cast<int>(text.size()));
        text.resize(wcslen(text.c_str()));
        return text;
    }

    bool SetWindowTextIfDifferent(HWND hwnd, const std::wstring& text)
    {
        if (!hwnd)
        {
            return false;
        }

        if (ReadWindowText(hwnd) == text)
        {
            return false;
        }

        SetWindowTextW(hwnd, text.c_str());
        return true;
    }

    bool SetWindowEnabledIfDifferent(HWND hwnd, bool enabled)
    {
        if (!hwnd)
        {
            return false;
        }

        const bool currentlyEnabled = IsWindowEnabled(hwnd) != FALSE;
        if (currentlyEnabled == enabled)
        {
            return false;
        }

        EnableWindow(hwnd, enabled ? TRUE : FALSE);
        return true;
    }

    void RedrawWindowNoErase(HWND hwnd)
    {
        if (!hwnd)
        {
            return;
        }

        RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_NOERASE);
    }

    void RemoveAllMenuItems(HMENU menu)
    {
        if (!menu)
        {
            return;
        }

        while (GetMenuItemCount(menu) > 0)
        {
            DeleteMenu(menu, 0, MF_BYPOSITION);
        }
    }

    bool TryReadStringValue(HKEY key, const wchar_t* valueName, std::wstring* value)
    {
        value->clear();

        DWORD type = 0;
        DWORD size = 0;
        if (RegQueryValueExW(key, valueName, nullptr, &type, nullptr, &size) != ERROR_SUCCESS
            || (type != REG_SZ && type != REG_EXPAND_SZ)
            || size < sizeof(wchar_t))
        {
            return false;
        }

        std::vector<wchar_t> buffer((size / sizeof(wchar_t)) + 1, L'\0');
        if (RegQueryValueExW(
            key,
            valueName,
            nullptr,
            &type,
            reinterpret_cast<LPBYTE>(buffer.data()),
            &size) != ERROR_SUCCESS)
        {
            return false;
        }

        *value = buffer.data();
        return true;
    }

    void WriteStringValue(HKEY key, const wchar_t* valueName, std::wstring_view value)
    {
        // Copy into a guaranteed null-terminated buffer; std::wstring_view is not required to be
        // null-terminated, so reading value.size() + 1 wchar_t's directly from value.data() risks
        // a heap over-read (or writing trailing garbage to the registry) for non-string callers.
        std::wstring buffer(value);
        const DWORD size = static_cast<DWORD>((buffer.size() + 1) * sizeof(wchar_t));
        RegSetValueExW(key, valueName, 0, REG_SZ, reinterpret_cast<const BYTE*>(buffer.c_str()), size);
    }

    void ApplyWindowFrameTheme(HWND hwnd,
                               bool useDarkMode,
                               COLORREF captionColor = CLR_INVALID,
                               COLORREF textColor = CLR_INVALID,
                               COLORREF borderColor = CLR_INVALID)
    {
        const BOOL enabled = useDarkMode ? TRUE : FALSE;
        const HRESULT result = DwmSetWindowAttribute(
            hwnd,
            kDwmUseImmersiveDarkModeAttribute,
            &enabled,
            sizeof(enabled));

        if (FAILED(result))
        {
            DwmSetWindowAttribute(
                hwnd,
                kDwmUseImmersiveDarkModeLegacyAttribute,
                &enabled,
                sizeof(enabled));
        }

        if (captionColor != CLR_INVALID)
        {
            DwmSetWindowAttribute(
                hwnd,
                kDwmCaptionColorAttribute,
                &captionColor,
                sizeof(captionColor));
        }

        if (textColor != CLR_INVALID)
        {
            DwmSetWindowAttribute(
                hwnd,
                kDwmTextColorAttribute,
                &textColor,
                sizeof(textColor));
        }

        if (borderColor != CLR_INVALID)
        {
            DwmSetWindowAttribute(
                hwnd,
                kDwmBorderColorAttribute,
                &borderColor,
                sizeof(borderColor));
        }
    }

    void RefreshWindowNonClientArea(HWND hwnd)
    {
        if (!hwnd || !IsWindow(hwnd))
        {
            return;
        }

        SetWindowPos(hwnd,
                     nullptr,
                     0,
                     0,
                     0,
                     0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        RedrawWindow(hwnd, nullptr, nullptr, RDW_FRAME | RDW_INVALIDATE | RDW_UPDATENOW);
        DwmFlush();
    }

    COLORREF BlendColor(COLORREF baseColor, COLORREF mixColor, BYTE mixAmount)
    {
        const BYTE baseAmount = static_cast<BYTE>(255 - mixAmount);
        return RGB(
            (GetRValue(baseColor) * baseAmount + GetRValue(mixColor) * mixAmount) / 255,
            (GetGValue(baseColor) * baseAmount + GetGValue(mixColor) * mixAmount) / 255,
            (GetBValue(baseColor) * baseAmount + GetBValue(mixColor) * mixAmount) / 255);
    }

    void AlphaBlendBitmap(HDC targetDC, HDC scratchDC, HBITMAP bitmap, int x, int y, int width, int height)
    {
        if (!targetDC || !scratchDC || !bitmap || width <= 0 || height <= 0)
        {
            return;
        }

        const HGDIOBJ oldBitmap = SelectObject(scratchDC, bitmap);
        BLENDFUNCTION blend{};
        blend.BlendOp = AC_SRC_OVER;
        blend.SourceConstantAlpha = 255;
        blend.AlphaFormat = AC_SRC_ALPHA;
        AlphaBlend(targetDC, x, y, width, height, scratchDC, 0, 0, width, height, blend);
        SelectObject(scratchDC, oldBitmap);
    }

    std::wstring GetFolderDisplayName(std::wstring_view folderPath)
    {
        if (folderPath.empty())
        {
            return L"No Folder";
        }

        const fs::path path(folderPath);
        const std::wstring leaf = path.filename().wstring();
        return leaf.empty() ? std::wstring(folderPath) : leaf;
    }

    std::wstring FormatFolderShortcutMenuLabel(std::wstring_view folderPath)
    {
        const std::wstring displayName = GetFolderDisplayName(folderPath);
        if (displayName.empty() || displayName == folderPath)
        {
            return std::wstring(folderPath);
        }

        return displayName + L" (" + std::wstring(folderPath) + L")";
    }

    std::wstring EscapeMenuMnemonicText(std::wstring_view text)
    {
        std::wstring escaped;
        escaped.reserve(text.size());
        for (const wchar_t character : text)
        {
            if (character == L'&')
            {
                escaped.push_back(L'&');
            }
            escaped.push_back(character);
        }
        return escaped;
    }

    int CurrentCalendarYear()
    {
        SYSTEMTIME localTime{};
        GetLocalTime(&localTime);
        return static_cast<int>(localTime.wYear);
    }

    HFONT CreateDialogUiFont(int pointSize,
                             int weight,
                             hyperbrowse::util::AppTextSize size = hyperbrowse::util::kDefaultAppTextSize)
    {
        NONCLIENTMETRICSW metrics{};
        metrics.cbSize = sizeof(metrics);

        LOGFONTW logFont{};
        if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0) != FALSE)
        {
            logFont = metrics.lfMessageFont;
        }
        else
        {
            wcscpy_s(logFont.lfFaceName, L"Segoe UI");
        }

        HDC screenDc = GetDC(nullptr);
        const int dpiY = screenDc ? GetDeviceCaps(screenDc, LOGPIXELSY) : 96;
        if (screenDc)
        {
            ReleaseDC(nullptr, screenDc);
        }

        logFont.lfHeight = -MulDiv(hyperbrowse::util::ScaleAppTextDimension(pointSize, size), dpiY, 72);
        logFont.lfWeight = weight;
        logFont.lfCharSet = DEFAULT_CHARSET;
        logFont.lfQuality = CLEARTYPE_NATURAL_QUALITY;
        return CreateFontIndirectW(&logFont);
    }

    HFONT CreateSystemUiFont(hyperbrowse::util::AppTextSize size)
    {
        NONCLIENTMETRICSW metrics{};
        metrics.cbSize = sizeof(metrics);
        if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0) == FALSE)
        {
            return CreateDialogUiFont(9, FW_NORMAL, size);
        }

        metrics.lfMessageFont.lfCharSet = DEFAULT_CHARSET;
        metrics.lfMessageFont.lfQuality = CLEARTYPE_NATURAL_QUALITY;
        metrics.lfMessageFont.lfHeight = static_cast<LONG>(
            static_cast<double>(metrics.lfMessageFont.lfHeight)
            * hyperbrowse::util::AppTextSizeScale(size));
        return CreateFontIndirectW(&metrics.lfMessageFont);
    }

    void DeleteFontIfOwned(HFONT font)
    {
        if (font && font != GetStockObject(DEFAULT_GUI_FONT))
        {
            DeleteObject(font);
        }
    }

    std::wstring NormalizeMenuDisplayText(std::wstring_view text)
    {
        std::wstring normalized;
        normalized.reserve(text.size());
        for (std::size_t index = 0; index < text.size(); ++index)
        {
            const wchar_t ch = text[index];
            if (ch == L'&')
            {
                if (index + 1 < text.size() && text[index + 1] == L'&')
                {
                    normalized.push_back(L'&');
                    ++index;
                }
                continue;
            }

            normalized.push_back(ch);
        }

        return normalized;
    }

    void SplitMenuDisplayText(std::wstring_view text, std::wstring* label, std::wstring* shortcut)
    {
        if (!label || !shortcut)
        {
            return;
        }

        const std::size_t tabIndex = text.find(L'\t');
        const std::wstring_view labelView = tabIndex == std::wstring_view::npos ? text : text.substr(0, tabIndex);
        const std::wstring_view shortcutView = tabIndex == std::wstring_view::npos ? std::wstring_view{} : text.substr(tabIndex + 1);
        *label = NormalizeMenuDisplayText(labelView);
        *shortcut = NormalizeMenuDisplayText(shortcutView);
    }

    wchar_t FindMenuMnemonic(std::wstring_view text)
    {
        for (std::size_t index = 0; index < text.size(); ++index)
        {
            if (text[index] != L'&')
            {
                continue;
            }

            if (index + 1 >= text.size())
            {
                break;
            }

            if (text[index + 1] == L'&')
            {
                ++index;
                continue;
            }

            return static_cast<wchar_t>(towupper(text[index + 1]));
        }

        return L'\0';
    }

    int CommandBarMenuIndexFromVirtualKey(WPARAM virtualKey)
    {
        switch (towupper(static_cast<wchar_t>(virtualKey)))
        {
        case L'F':
            return 0;
        case L'V':
            return 1;
        case L'H':
            return 2;
        default:
            return -1;
        }
    }

    int MeasureTextBlockHeight(HFONT font,
                               std::wstring_view text,
                               int width,
                               UINT format,
                               int minimumHeight = 0)
    {
        if (width <= 0)
        {
            return minimumHeight;
        }

        std::wstring localText = text.empty() ? std::wstring(L" ") : std::wstring(text);
        HDC screenDc = GetDC(nullptr);
        if (!screenDc)
        {
            return minimumHeight;
        }

        const HGDIOBJ oldFont = font ? SelectObject(screenDc, font) : nullptr;
        RECT bounds{0, 0, width, 0};
        DrawTextW(screenDc, localText.c_str(), -1, &bounds, format | DT_CALCRECT);
        if (oldFont)
        {
            SelectObject(screenDc, oldFont);
        }
        ReleaseDC(nullptr, screenDc);
        const int measuredHeight = static_cast<int>(bounds.bottom - bounds.top);
        return std::max(minimumHeight, measuredHeight);
    }

    int MeasureDialogButtonWidth(HFONT font, std::wstring_view label, int minimumWidth)
    {
        if (label.empty())
        {
            return minimumWidth;
        }

        HDC screenDc = GetDC(nullptr);
        if (!screenDc)
        {
            return minimumWidth;
        }

        const HFONT effectiveFont = font
            ? font
            : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        const HGDIOBJ oldFont = effectiveFont ? SelectObject(screenDc, effectiveFont) : nullptr;
        const std::wstring localText(label);
        SIZE size{};
        GetTextExtentPoint32W(screenDc,
                              localText.c_str(),
                              static_cast<int>(localText.size()),
                              &size);
        if (oldFont)
        {
            SelectObject(screenDc, oldFont);
        }
        ReleaseDC(nullptr, screenDc);

        return std::max(minimumWidth, static_cast<int>(size.cx) + 24);
    }

    TextInputDialogLayoutMetrics BuildTextInputDialogLayoutMetrics(const TextInputDialogState& state)
    {
        TextInputDialogLayoutMetrics metrics;
        metrics.clientWidth = kTextInputDialogWidth;

        const auto measureButtonWidth = [&state](std::wstring_view label) -> int
        {
            if (label.empty())
            {
                return kTextInputButtonWidth;
            }

            std::wstring localText(label);
            HDC screenDc = GetDC(nullptr);
            if (!screenDc)
            {
                return kTextInputButtonWidth;
            }

            const HFONT font = state.bodyFont
                ? state.bodyFont
                : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            const HGDIOBJ oldFont = font ? SelectObject(screenDc, font) : nullptr;
            SIZE size{};
            GetTextExtentPoint32W(screenDc,
                                  localText.c_str(),
                                  static_cast<int>(localText.size()),
                                  &size);
            if (oldFont)
            {
                SelectObject(screenDc, oldFont);
            }
            ReleaseDC(nullptr, screenDc);

            return std::max(kTextInputButtonWidth, static_cast<int>(size.cx) + 24);
        };

        metrics.okButtonWidth = measureButtonWidth(state.confirmLabel);
        metrics.cancelButtonWidth = measureButtonWidth(L"Cancel");

        const int buttonRowWidth = metrics.okButtonWidth + 8 + metrics.cancelButtonWidth;
        const int minimumClientWidthForButtons = (kTextInputDialogMargin * 2) + buttonRowWidth;
        metrics.clientWidth = std::max(metrics.clientWidth, minimumClientWidthForButtons);

        metrics.contentWidth = metrics.clientWidth - (kTextInputDialogMargin * 2);
        metrics.instructionHeight = MeasureTextBlockHeight(state.bodyFont,
                                                           state.instruction,
                                                           metrics.contentWidth,
                                                           DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK,
                                                           kTextInputDialogInstructionMinHeight);
        metrics.editTop = kTextInputDialogMargin + metrics.instructionHeight + kTextInputDialogEditTopGap;
        metrics.dividerTop = metrics.editTop + kTextInputEditHeight + kTextInputDialogDividerTopGap;
        metrics.buttonTop = metrics.dividerTop + kTextInputDialogButtonTopGap;
        metrics.clientHeight = metrics.buttonTop + kTextInputButtonHeight + kTextInputDialogMargin;
        return metrics;
    }

    struct QuickAccessPanelMetrics
    {
        int headerHeight{kQuickAccessPanelHeaderHeight};
        int rowHeight{kQuickAccessPanelRowHeight};
        int labelTopInset{kQuickAccessPanelRowTextTopInset};
        int labelHeight{15};
        int metadataTopInset{21};
        int metadataBottomInset{kQuickAccessPanelRowBottomInset};
        int buttonHeight{kTextInputButtonHeight};
        int buttonTopInset{kQuickAccessPanelButtonVerticalInset};
    };

    int MeasureSingleLineTextHeight(HFONT font, int minimumHeight)
    {
        return MeasureTextBlockHeight(font,
                                      L"Ag",
                                      4096,
                                      DT_LEFT | DT_TOP | DT_NOPREFIX | DT_SINGLELINE,
                                      minimumHeight);
    }

    QuickAccessPanelMetrics BuildQuickAccessPanelMetrics(HFONT summaryFont, HFONT bodyFont)
    {
        const HFONT defaultGuiFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        const HFONT effectiveSummaryFont = summaryFont ? summaryFont : defaultGuiFont;
        const HFONT effectiveBodyFont = bodyFont ? bodyFont : defaultGuiFont;

        QuickAccessPanelMetrics metrics;
        metrics.headerHeight = std::max(kQuickAccessPanelHeaderHeight,
                                        MeasureSingleLineTextHeight(effectiveSummaryFont, kQuickAccessPanelHeaderHeight)
                                            + kQuickAccessPanelHeaderVerticalPadding);
        metrics.labelHeight = MeasureSingleLineTextHeight(effectiveSummaryFont, metrics.labelHeight);
        const int metadataHeight = MeasureSingleLineTextHeight(effectiveBodyFont, 14);
        metrics.metadataTopInset = metrics.labelTopInset + metrics.labelHeight + kQuickAccessPanelRowTextGap;
        metrics.rowHeight = std::max(kQuickAccessPanelRowHeight,
                                     metrics.metadataTopInset + metadataHeight + metrics.metadataBottomInset);
        metrics.buttonHeight = std::min(kTextInputButtonHeight,
                                        std::max(0, metrics.rowHeight - (kQuickAccessPanelButtonVerticalInset * 2)));
        metrics.buttonTopInset = std::max(0, (metrics.rowHeight - metrics.buttonHeight) / 2);
        return metrics;
    }

    int MeasureTextWidth(HFONT font, std::wstring_view text)
    {
        if (text.empty())
        {
            return 0;
        }

        std::wstring localText(text);
        HDC screenDc = GetDC(nullptr);
        if (!screenDc)
        {
            return 0;
        }

        const HGDIOBJ oldFont = font ? SelectObject(screenDc, font) : nullptr;
        SIZE size{};
        GetTextExtentPoint32W(screenDc, localText.c_str(), static_cast<int>(localText.size()), &size);
        if (oldFont)
        {
            SelectObject(screenDc, oldFont);
        }
        ReleaseDC(nullptr, screenDc);
        return static_cast<int>(size.cx);
    }

    int MeasureAboutDialogLinkButtonWidth(HFONT font, std::wstring_view firstLabel, std::wstring_view secondLabel)
    {
        const int textWidth = std::max(MeasureTextWidth(font, firstLabel), MeasureTextWidth(font, secondLabel));
        return std::max(180, textWidth + 40);
    }

    int MeasureAboutDialogClientHeight(const AboutDialogState& state)
    {
        const int contentRight = kAboutDialogWidth - kAboutDialogMargin;
        const int artLeft = contentRight - kAboutDialogBrandArtSize;
        const int iconLeft = kAboutDialogMargin;
        const int iconSize = 48;
        const int textLeft = iconLeft + iconSize + 20;
        const int textRight = artLeft - 28;
        const int textWidth = std::max(320, textRight - textLeft);

        const int titleTop = kAboutDialogMargin - 2;
        const int titleHeight = MeasureTextBlockHeight(state.titleFont, state.title, textWidth, DT_LEFT | DT_NOPREFIX | DT_SINGLELINE, 44);
        const int subtitleTop = titleTop + titleHeight + 10;
        const int subtitleHeight = MeasureTextBlockHeight(state.subtitleFont, state.subtitle, textWidth, DT_LEFT | DT_NOPREFIX | DT_SINGLELINE, 28);
        const int introTop = subtitleTop + subtitleHeight + 10;
        const int introHeight = MeasureTextBlockHeight(state.bodyFont, state.intro, textWidth, DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK, 40);
        const int artFrameBottom = (kAboutDialogMargin - 4) + kAboutDialogBrandArtSize + 10;
        const int headerHeight = std::max(kAboutDialogHeaderHeight,
                                          std::max(artFrameBottom + kAboutDialogMargin - 8,
                                                   introTop + introHeight + kAboutDialogMargin - 8));

        const int bodyWidth = kAboutDialogWidth - (kAboutDialogMargin * 2);
        const int headingHeight = MeasureTextBlockHeight(state.subtitleFont, state.bodyHeading, bodyWidth, DT_LEFT | DT_NOPREFIX | DT_SINGLELINE, 28);
        const int bodyTextHeight = MeasureTextBlockHeight(state.bodyFont, state.bodyContent, bodyWidth, DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK, 0);
        const int bodyHeight = 24 + headingHeight + 12 + bodyTextHeight + 26;

        const int footerActionWidth = state.githubButtonWidth + state.supportButtonWidth + kAboutDialogButtonWidth + (kAboutDialogButtonGap * 2);
        const int footerTextWidth = std::max(320, kAboutDialogWidth - (kAboutDialogMargin * 2) - footerActionWidth - 20);
        const int footerTextHeight = MeasureTextBlockHeight(state.footerFont, state.footer, footerTextWidth, DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK, 0);
        const int footerHeight = std::max(kAboutDialogFooterHeight, std::max(footerTextHeight + 34, kAboutDialogButtonHeight + 36));

        return headerHeight + bodyHeight + footerHeight;
    }

    int MeasureAboutDialogFooterActionWidth(const AboutDialogState& state)
    {
        return state.githubButtonWidth + state.supportButtonWidth + kAboutDialogButtonWidth + (kAboutDialogButtonGap * 2);
    }

    int MeasureAboutDialogFooterTextWidth(const AboutDialogState& state, int clientWidth)
    {
        return std::max(320, clientWidth - (kAboutDialogMargin * 2) - MeasureAboutDialogFooterActionWidth(state) - 20);
    }

    int AboutDialogFooterButtonsLeft(const RECT& clientRect, const AboutDialogState& state)
    {
        const int buttonsLeft = static_cast<int>(clientRect.right) - kAboutDialogMargin - MeasureAboutDialogFooterActionWidth(state);
        return std::max<int>(kAboutDialogMargin, buttonsLeft);
    }

    int AboutDialogFooterButtonsTop(const RECT& clientRect)
    {
        return clientRect.bottom - kAboutDialogMargin - kAboutDialogButtonHeight;
    }

    const wchar_t* GetAboutDialogLinkLabel(UINT controlId)
    {
        switch (controlId)
        {
        case ID_ABOUT_OPEN_GITHUB:
            return kAboutDialogGitHubLabel;
        case ID_ABOUT_OPEN_SUPPORT:
            return kAboutDialogSupportLabel;
        default:
            return L"";
        }
    }

    const wchar_t* GetAboutDialogLinkTarget(UINT controlId)
    {
        switch (controlId)
        {
        case ID_ABOUT_OPEN_GITHUB:
            return kAboutDialogGitHubUrl;
        case ID_ABOUT_OPEN_SUPPORT:
            return kAboutDialogSupportUrl;
        default:
            return nullptr;
        }
    }

    const wchar_t* GetAboutDialogLinkFailureMessage(UINT controlId)
    {
        switch (controlId)
        {
        case ID_ABOUT_OPEN_GITHUB:
            return L"Failed to open the HyperBrowse GitHub project.";
        case ID_ABOUT_OPEN_SUPPORT:
            return L"Failed to open the Buy Me A Coffee page.";
        default:
            return L"Failed to open the selected link.";
        }
    }

    COLORREF GetAboutDialogSupportAccent(bool darkMode)
    {
        return darkMode ? RGB(255, 214, 126) : RGB(145, 78, 16);
    }

    struct AboutDialogLinkPalette
    {
        COLORREF fill{};
        COLORREF border{};
        COLORREF text{};
    };

    AboutDialogLinkPalette BuildAboutDialogLinkPalette(UINT controlId, const AboutDialogState& state, UINT itemState)
    {
        const bool supportButton = controlId == ID_ABOUT_OPEN_SUPPORT;
        const bool pressed = (itemState & ODS_SELECTED) != 0;
        const bool hot = (itemState & ODS_HOTLIGHT) != 0;
        const bool disabled = (itemState & ODS_DISABLED) != 0;

        const COLORREF accent = supportButton ? GetAboutDialogSupportAccent(state.darkMode) : state.accent;
        int fillMixAmount = supportButton
            ? (state.darkMode ? 34 : 18)
            : (state.darkMode ? 28 : 10);
        if (hot)
        {
            fillMixAmount += supportButton ? 10 : 8;
        }
        if (pressed)
        {
            fillMixAmount += supportButton ? 18 : 14;
        }
        fillMixAmount = std::min(fillMixAmount, 96);

        AboutDialogLinkPalette palette;
        if (supportButton)
        {
            const COLORREF baseFill = state.darkMode ? RGB(88, 61, 19) : RGB(255, 245, 219);
            palette.fill = BlendColor(baseFill, accent, static_cast<BYTE>(fillMixAmount));
            palette.text = state.darkMode ? RGB(255, 236, 194) : RGB(112, 62, 15);
        }
        else
        {
            const COLORREF baseFill = state.darkMode ? state.footerBackground : RGB(255, 255, 255);
            palette.fill = BlendColor(baseFill, accent, static_cast<BYTE>(fillMixAmount));
            palette.text = accent;
        }

        palette.border = BlendColor(accent, state.border, supportButton ? 22 : 28);

        if (disabled)
        {
            palette.fill = BlendColor(palette.fill, state.footerBackground, 120);
            palette.border = BlendColor(palette.border, state.border, 120);
            palette.text = state.mutedText;
        }

        return palette;
    }

    void OpenAboutDialogLink(HWND hwnd, UINT controlId)
    {
        const wchar_t* target = GetAboutDialogLinkTarget(controlId);
        if (!target)
        {
            return;
        }

        if (!LaunchShellTarget(hwnd, L"open", target))
        {
            MessageBoxW(hwnd, GetAboutDialogLinkFailureMessage(controlId), L"About HyperBrowse", MB_OK | MB_ICONERROR);
        }
    }

    void DrawAboutDialogLinkButton(const DRAWITEMSTRUCT& drawItem, const AboutDialogState& state)
    {
        RECT buttonRect{};
        GetClientRect(drawItem.hwndItem, &buttonRect);

        const HBRUSH footerBrush = CreateSolidBrush(state.footerBackground);
        FillRect(drawItem.hDC, &buttonRect, footerBrush);
        DeleteObject(footerBrush);

        const AboutDialogLinkPalette palette = BuildAboutDialogLinkPalette(drawItem.CtlID, state, drawItem.itemState);
        RECT pillRect = buttonRect;
        InflateRect(&pillRect, -1, -1);

        const HBRUSH fillBrush = CreateSolidBrush(palette.fill);
        const HPEN borderPen = CreatePen(PS_SOLID, 1, palette.border);
        const HGDIOBJ oldBrush = SelectObject(drawItem.hDC, fillBrush);
        const HGDIOBJ oldPen = SelectObject(drawItem.hDC, borderPen);
        RoundRect(drawItem.hDC, pillRect.left, pillRect.top, pillRect.right, pillRect.bottom, 16, 16);
        SelectObject(drawItem.hDC, oldPen);
        SelectObject(drawItem.hDC, oldBrush);
        DeleteObject(borderPen);
        DeleteObject(fillBrush);

        RECT textRect = pillRect;
        InflateRect(&textRect, -12, -6);
        hyperbrowse::render::DrawGdiText(drawItem.hDC,
                    state.subtitleFont ? state.subtitleFont : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)),
                    GetAboutDialogLinkLabel(drawItem.CtlID),
                    -1,
                    textRect,
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX,
                    palette.text,
                    palette.fill);

        if ((drawItem.itemState & ODS_FOCUS) != 0)
        {
            RECT focusRect = pillRect;
            InflateRect(&focusRect, -4, -4);
            DrawFocusRect(drawItem.hDC, &focusRect);
        }
    }

    void LayoutAboutDialogControls(HWND hwnd, const AboutDialogState& state)
    {
        RECT client{};
        GetClientRect(hwnd, &client);
        const int buttonTop = AboutDialogFooterButtonsTop(client);
        int buttonLeft = AboutDialogFooterButtonsLeft(client, state);

        if (state.githubButton)
        {
            MoveWindow(state.githubButton,
                       buttonLeft,
                       buttonTop,
                       state.githubButtonWidth,
                       kAboutDialogButtonHeight,
                       TRUE);
            buttonLeft += state.githubButtonWidth + kAboutDialogButtonGap;
        }

        if (state.supportButton)
        {
            MoveWindow(state.supportButton,
                       buttonLeft,
                       buttonTop,
                       state.supportButtonWidth,
                       kAboutDialogButtonHeight,
                       TRUE);
            buttonLeft += state.supportButtonWidth + kAboutDialogButtonGap;
        }

        if (state.okButton)
        {
            MoveWindow(state.okButton,
                       buttonLeft,
                       buttonTop,
                       kAboutDialogButtonWidth,
                       kAboutDialogButtonHeight,
                       TRUE);
        }
    }

    bool StringsEqualInsensitive(std::wstring_view lhs, std::wstring_view rhs)
    {
        return hyperbrowse::util::EqualsIgnoreCaseOrdinal(lhs, rhs);
    }

    bool IsJpegFileType(std::wstring_view fileType)
    {
        return StringsEqualInsensitive(fileType, L"jpg")
            || StringsEqualInsensitive(fileType, L"jpeg")
            || StringsEqualInsensitive(fileType, L".jpg")
            || StringsEqualInsensitive(fileType, L".jpeg");
    }

    std::wstring BuildCameraSummaryLabel(const hyperbrowse::services::ImageMetadata& metadata)
    {
        if (!metadata.cameraMake.empty() && !metadata.cameraModel.empty())
        {
            return metadata.cameraMake + L" " + metadata.cameraModel;
        }

        return !metadata.cameraModel.empty() ? metadata.cameraModel : metadata.cameraMake;
    }

    bool IsCuratedDetailsProperty(std::wstring_view canonicalName)
    {
        return canonicalName == L"System.Image.Dimensions"
            || canonicalName == L"System.Image.HorizontalSize"
            || canonicalName == L"System.Image.VerticalSize"
            || canonicalName == L"System.Photo.CameraManufacturer"
            || canonicalName == L"System.Photo.CameraModel"
            || canonicalName == L"System.Photo.DateTaken"
            || canonicalName == L"System.Photo.ExposureTime"
            || canonicalName == L"System.Photo.FNumber"
            || canonicalName == L"System.Photo.ISOSpeed"
            || canonicalName == L"System.Photo.FocalLength"
            || canonicalName == L"System.Title"
            || canonicalName == L"System.Author"
            || canonicalName == L"System.Keywords"
            || canonicalName == L"System.Comment";
    }

    void AppendLabeledLine(std::wstring* text, std::wstring_view label, std::wstring_view value)
    {
        if (!text || value.empty())
        {
            return;
        }

        text->append(label);
        text->append(value);
        text->append(L"\r\n");
    }

    bool HasEquivalentDisplayedProperty(const std::vector<hyperbrowse::services::MetadataPropertyEntry>& properties,
                                       const hyperbrowse::services::MetadataPropertyEntry& candidate)
    {
        return std::any_of(properties.begin(),
                           properties.end(),
                           [&](const hyperbrowse::services::MetadataPropertyEntry& property)
                           {
                               return StringsEqualInsensitive(property.displayName, candidate.displayName)
                                   && StringsEqualInsensitive(property.value, candidate.value);
                           });
    }

    std::wstring BuildSingleSelectionSummary(const hyperbrowse::browser::BrowserItem& item)
    {
        if (item.isDirectory)
        {
            return L"Folder";
        }

        std::wstring summary = item.fileType;

        const std::wstring dimensions = hyperbrowse::browser::FormatDimensionsForItem(item);
        if (!dimensions.empty() && dimensions != L"...")
        {
            if (!summary.empty())
            {
                summary.append(L" | ");
            }
            summary.append(dimensions);
        }

        const std::wstring fileSize = hyperbrowse::browser::FormatByteSize(item.fileSizeBytes);
        if (!fileSize.empty())
        {
            if (!summary.empty())
            {
                summary.append(L" | ");
            }
            summary.append(fileSize);
        }

        return summary;
    }

    template <typename Getter>
    bool TryGetCommonItemString(const std::vector<hyperbrowse::browser::BrowserItem>& items,
                                Getter getter,
                                std::wstring* commonValue)
    {
        if (!commonValue || items.empty())
        {
            return false;
        }

        const std::wstring first = getter(items.front());
        if (first.empty() || first == L"...")
        {
            return false;
        }

        for (std::size_t index = 1; index < items.size(); ++index)
        {
            const std::wstring candidate = getter(items[index]);
            if (candidate.empty() || candidate == L"..." || !StringsEqualInsensitive(first, candidate))
            {
                return false;
            }
        }

        *commonValue = first;
        return true;
    }

    template <typename Getter>
    bool TryGetCommonMetadataString(const std::vector<std::shared_ptr<const hyperbrowse::services::ImageMetadata>>& metadataList,
                                    Getter getter,
                                    std::wstring* commonValue)
    {
        if (!commonValue || metadataList.empty() || !metadataList.front())
        {
            return false;
        }

        const std::wstring first = getter(*metadataList.front());
        if (first.empty())
        {
            return false;
        }

        for (std::size_t index = 1; index < metadataList.size(); ++index)
        {
            if (!metadataList[index])
            {
                return false;
            }

            const std::wstring candidate = getter(*metadataList[index]);
            if (candidate.empty() || !StringsEqualInsensitive(first, candidate))
            {
                return false;
            }
        }

        *commonValue = first;
        return true;
    }

    bool TryGetCommonDimensions(const std::vector<hyperbrowse::browser::BrowserItem>& items,
                                std::wstring* commonValue)
    {
        if (!commonValue || items.empty())
        {
            return false;
        }

        const int firstWidth = items.front().imageWidth;
        const int firstHeight = items.front().imageHeight;
        if (firstWidth <= 0 || firstHeight <= 0)
        {
            return false;
        }

        for (std::size_t index = 1; index < items.size(); ++index)
        {
            if (items[index].imageWidth != firstWidth || items[index].imageHeight != firstHeight)
            {
                return false;
            }
        }

        *commonValue = hyperbrowse::browser::FormatDimensions(firstWidth, firstHeight);
        return true;
    }

    std::vector<hyperbrowse::services::MetadataPropertyEntry> FindCommonMetadataProperties(
        const std::vector<std::shared_ptr<const hyperbrowse::services::ImageMetadata>>& metadataList)
    {
        if (metadataList.empty() || !metadataList.front())
        {
            return {};
        }

        std::vector<hyperbrowse::services::MetadataPropertyEntry> commonProperties;
        for (const hyperbrowse::services::MetadataPropertyEntry& property : metadataList.front()->properties)
        {
            if (property.value.empty())
            {
                continue;
            }

            bool isCommon = true;
            for (std::size_t index = 1; index < metadataList.size(); ++index)
            {
                if (!metadataList[index])
                {
                    isCommon = false;
                    break;
                }

                const auto propertyIt = std::find_if(metadataList[index]->properties.begin(),
                                                     metadataList[index]->properties.end(),
                                                     [&](const hyperbrowse::services::MetadataPropertyEntry& candidate)
                                                     {
                                                         return candidate.canonicalName == property.canonicalName
                                                             && StringsEqualInsensitive(candidate.value, property.value);
                                                     });
                if (propertyIt == metadataList[index]->properties.end())
                {
                    isCommon = false;
                    break;
                }
            }

            if (isCommon)
            {
                commonProperties.push_back(property);
            }
        }

        return commonProperties;
    }

    std::wstring CompactSortLabel(hyperbrowse::browser::BrowserSortMode sortMode)
    {
        switch (sortMode)
        {
        case hyperbrowse::browser::BrowserSortMode::FileName:
            return L"Name";
        case hyperbrowse::browser::BrowserSortMode::ModifiedDate:
            return L"Date";
        case hyperbrowse::browser::BrowserSortMode::FileSize:
            return L"Size";
        case hyperbrowse::browser::BrowserSortMode::Dimensions:
            return L"Pixels";
        case hyperbrowse::browser::BrowserSortMode::FileType:
            return L"Type";
        case hyperbrowse::browser::BrowserSortMode::DateTaken:
            return L"Taken";
        case hyperbrowse::browser::BrowserSortMode::Rating:
            return L"Rating";
        case hyperbrowse::browser::BrowserSortMode::Tags:
            return L"Tags";
        case hyperbrowse::browser::BrowserSortMode::Random:
        default:
            return L"Random";
        }
    }

    std::wstring NormalizeFolderPath(std::wstring path)
    {
        std::replace(path.begin(), path.end(), L'/', L'\\');
        while (path.size() > 3 && !path.empty() && path.back() == L'\\')
        {
            path.pop_back();
        }

        if (path.size() == 2 && path[1] == L':')
        {
            path.push_back(L'\\');
        }

        return path;
    }

    std::wstring RewritePathPrefix(std::wstring_view path, std::wstring_view oldPrefix, std::wstring_view newPrefix)
    {
        if (!hyperbrowse::browser::PathHasPrefix(path, oldPrefix))
        {
            return std::wstring(path);
        }

        std::wstring rewrittenPath(newPrefix);
        std::wstring suffix(std::wstring(path).substr(oldPrefix.size()));
        if (!rewrittenPath.empty() && !suffix.empty() && rewrittenPath.back() == L'\\' && suffix.front() == L'\\')
        {
            suffix.erase(suffix.begin());
        }

        rewrittenPath.append(suffix);
        return NormalizeFolderPath(std::move(rewrittenPath));
    }

    int DefaultRenameSelectionEnd(std::wstring_view leafName, bool isFile)
    {
        if (!isFile)
        {
            return static_cast<int>(leafName.size());
        }

        const fs::path leafPath(leafName);
        const std::wstring stem = leafPath.stem().wstring();
        const std::wstring extension = leafPath.extension().wstring();
        if (!stem.empty() && !extension.empty())
        {
            return static_cast<int>(stem.size());
        }

        return static_cast<int>(leafName.size());
    }

    void CenterWindowOnOwner(HWND window, HWND ownerWindow)
    {
        RECT ownerRect{};
        RECT dialogRect{};
        const HWND referenceWindow = ownerWindow ? ownerWindow : GetDesktopWindow();
        GetWindowRect(referenceWindow, &ownerRect);
        GetWindowRect(window, &dialogRect);

        const int width = dialogRect.right - dialogRect.left;
        const int height = dialogRect.bottom - dialogRect.top;
        const int x = ownerRect.left + ((ownerRect.right - ownerRect.left) - width) / 2;
        const int y = ownerRect.top + ((ownerRect.bottom - ownerRect.top) - height) / 2;
        SetWindowPos(window, nullptr, x, y, 0, 0, SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
    }

    std::wstring TrimWhitespaceCopy(std::wstring value);

    bool TryParsePositiveSizeValue(const std::wstring& text, std::size_t* value)
    {
        if (!value)
        {
            return false;
        }

        const std::wstring trimmed = TrimWhitespaceCopy(text);
        if (trimmed.empty())
        {
            return false;
        }

        wchar_t* end = nullptr;
        errno = 0;
        const unsigned long long parsed = wcstoull(trimmed.c_str(), &end, 10);
        if (errno != 0 || !end || *end != L'\0' || parsed == 0ULL)
        {
            return false;
        }

        *value = hyperbrowse::util::SaturatingCastToSizeT(parsed);
        return true;
    }

    std::wstring FormatMegabytesFromBytes(std::size_t bytes)
    {
        return std::to_wstring(bytes / (1024ULL * 1024ULL));
    }

    constexpr std::array<SlideshowTransitionOption, 21> kSlideshowTransitionOptions = {{
        {hyperbrowse::viewer::TransitionStyle::Random, L"Random (All animated styles)"},
        {hyperbrowse::viewer::TransitionStyle::BlurCrossfade, L"Blur Crossfade"},
        {hyperbrowse::viewer::TransitionStyle::CenterWipe, L"Center Wipe"},
        {hyperbrowse::viewer::TransitionStyle::CheckerboardWipe, L"Checkerboard Wipe"},
        {hyperbrowse::viewer::TransitionStyle::ColorWash, L"Color Wash"},
        {hyperbrowse::viewer::TransitionStyle::Crossfade, L"Crossfade"},
        {hyperbrowse::viewer::TransitionStyle::DiagonalSlide, L"Diagonal Slide"},
        {hyperbrowse::viewer::TransitionStyle::FadeToBlack, L"Fade to Black"},
        {hyperbrowse::viewer::TransitionStyle::Flashbulb, L"Flashbulb"},
        {hyperbrowse::viewer::TransitionStyle::HorizontalBlinds, L"Horizontal Blinds"},
        {hyperbrowse::viewer::TransitionStyle::KenBurns, L"Ken Burns"},
        {hyperbrowse::viewer::TransitionStyle::MonochromeReveal, L"Monochrome Reveal"},
        {hyperbrowse::viewer::TransitionStyle::MotionBlur, L"Motion Blur"},
        {hyperbrowse::viewer::TransitionStyle::Cut, L"None (Cut)"},
        {hyperbrowse::viewer::TransitionStyle::Prism, L"Prism"},
        {hyperbrowse::viewer::TransitionStyle::Push, L"Push"},
        {hyperbrowse::viewer::TransitionStyle::SepiaDrift, L"Sepia Drift"},
        {hyperbrowse::viewer::TransitionStyle::Slide, L"Slide"},
        {hyperbrowse::viewer::TransitionStyle::SplitWipe, L"Split Wipe"},
        {hyperbrowse::viewer::TransitionStyle::VenetianBlinds, L"Venetian Blinds"},
        {hyperbrowse::viewer::TransitionStyle::ZoomFade, L"Zoom Fade"},
    }};

    int SlideshowTransitionComboIndex(hyperbrowse::viewer::TransitionStyle style)
    {
        for (std::size_t index = 0; index < kSlideshowTransitionOptions.size(); ++index)
        {
            if (kSlideshowTransitionOptions[index].style == style)
            {
                return static_cast<int>(index);
            }
        }

        return SlideshowTransitionComboIndex(hyperbrowse::viewer::TransitionStyle::Crossfade);
    }

    bool TryReadDialogUInt(HWND window, UINT minimum, UINT maximum, UINT* value)
    {
        if (!window || !value)
        {
            return false;
        }

        std::size_t parsedValue = 0;
        if (!TryParsePositiveSizeValue(ReadWindowText(window), &parsedValue))
        {
            return false;
        }

        if (parsedValue < minimum || parsedValue > maximum)
        {
            return false;
        }

        *value = static_cast<UINT>(parsedValue);
        return true;
    }

    void SetDialogUIntEditAndSpin(HWND editWindow, HWND spinWindow, UINT value)
    {
        if (spinWindow)
        {
            SendMessageW(spinWindow, UDM_SETPOS32, 0, static_cast<LPARAM>(value));
        }

        if (editWindow)
        {
            SetWindowTextIfDifferent(editWindow, std::to_wstring(value));
        }
    }

    UINT ComputeNextSpinValue(HWND editWindow, int fallbackValue, int delta, UINT minimum, UINT maximum)
    {
        UINT currentValue = static_cast<UINT>(std::clamp(fallbackValue, static_cast<int>(minimum), static_cast<int>(maximum)));
        TryReadDialogUInt(editWindow, minimum, maximum, &currentValue);

        const int nextValue = std::clamp(static_cast<int>(currentValue) + delta,
                                         static_cast<int>(minimum),
                                         static_cast<int>(maximum));
        return static_cast<UINT>(nextValue);
    }

    std::wstring BuildPersistentThumbnailCacheSummary(const hyperbrowse::cache::DiskThumbnailCache::Statistics& statistics,
                                                      bool persistentCacheEnabled)
    {
        std::wstring summary = persistentCacheEnabled
            ? L"Persistent thumbnail caching is currently enabled.\r\n"
            : L"Persistent thumbnail caching is currently disabled. Saved thumbnails remain on disk until they are purged.\r\n";
        summary.append(L"Indexed thumbnails: ");
        summary.append(std::to_wstring(statistics.indexedEntryCount));
        summary.append(L"\r\nDisk usage: ");
        summary.append(hyperbrowse::browser::FormatByteSize(statistics.cacheFileBytes));
        summary.append(L" on disk, ");
        summary.append(hyperbrowse::browser::FormatByteSize(statistics.indexedBytes));
        summary.append(L" tracked in the cache index.\r\nConfigured budget: ");
        summary.append(hyperbrowse::browser::FormatByteSize(statistics.capacityBytes));
        return summary;
    }

    std::wstring BuildPersistentThumbnailCacheDetails(const hyperbrowse::cache::DiskThumbnailCache::Statistics& statistics)
    {
        std::wstring details;
        AppendLabeledLine(&details, L"Cache Folder: ", statistics.cacheDirectory.empty() ? std::wstring(L"(unavailable)") : statistics.cacheDirectory);
        AppendLabeledLine(&details, L"Configured Budget: ", hyperbrowse::browser::FormatByteSize(statistics.capacityBytes));
        AppendLabeledLine(&details, L"Indexed Entries: ", std::to_wstring(statistics.indexedEntryCount));
        AppendLabeledLine(&details, L"Indexed Bytes: ", hyperbrowse::browser::FormatByteSize(statistics.indexedBytes));
        AppendLabeledLine(&details, L"Thumbnail Files On Disk: ", std::to_wstring(statistics.cacheFileCount));
        AppendLabeledLine(&details, L"Thumbnail File Bytes: ", hyperbrowse::browser::FormatByteSize(statistics.cacheFileBytes));
        AppendLabeledLine(&details, L"Index File Size: ", hyperbrowse::browser::FormatByteSize(statistics.indexFileBytes));
        AppendLabeledLine(&details, L"Missing Indexed Files: ", std::to_wstring(statistics.missingFileCount));
        AppendLabeledLine(&details, L"Orphaned Files: ", std::to_wstring(statistics.orphanFileCount));
        AppendLabeledLine(&details, L"Orphaned File Bytes: ", hyperbrowse::browser::FormatByteSize(statistics.orphanFileBytes));
        return details;
    }

    void RefreshPerformanceSettingsDialogControls(const PerformanceSettingsDialogState& state)
    {
        const bool thumbnailAutomatic = state.thumbnailAutoCheckWindow
            && SendMessageW(state.thumbnailAutoCheckWindow, BM_GETCHECK, 0, 0) == BST_CHECKED;
        const bool metadataAutomatic = state.metadataAutoCheckWindow
            && SendMessageW(state.metadataAutoCheckWindow, BM_GETCHECK, 0, 0) == BST_CHECKED;

        SetWindowEnabledIfDifferent(state.thumbnailEditWindow, !thumbnailAutomatic);
        SetWindowEnabledIfDifferent(state.metadataEditWindow, !metadataAutomatic);
    }

    PerformanceSettingsDialogLayoutMetrics BuildPerformanceSettingsDialogLayoutMetrics(int clientWidth,
                                                                                      const PerformanceSettingsDialogState& state)
    {
        PerformanceSettingsDialogLayoutMetrics metrics;
        metrics.margin = hyperbrowse::util::ScaleAppTextDimension(kTextInputDialogMargin, state.appTextSize);
        metrics.contentLeft = metrics.margin + 2;
        metrics.contentWidth = std::max(0, clientWidth - (metrics.contentLeft * 2));
        metrics.sectionInset = hyperbrowse::util::ScaleAppTextDimension(kPerformanceSettingsDialogSectionInset, state.appTextSize);
        metrics.controlGap = hyperbrowse::util::ScaleAppTextDimension(kPerformanceSettingsDialogControlGap, state.appTextSize);

        const HFONT titleFont = state.titleFont ? state.titleFont : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        const HFONT bodyFont = state.bodyFont ? state.bodyFont : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        const int lineHeight = MeasureSingleLineTextHeight(bodyFont, 20);

        metrics.titleTop = metrics.margin + hyperbrowse::util::ScaleAppTextDimension(4, state.appTextSize);
        metrics.titleHeight = MeasureTextBlockHeight(titleFont,
                                                     state.title,
                                                     metrics.contentWidth,
                                                     DT_LEFT | DT_NOPREFIX | DT_SINGLELINE,
                                                     hyperbrowse::util::ScaleAppTextDimension(28, state.appTextSize));
        metrics.instructionTop = metrics.titleTop + metrics.titleHeight
            + hyperbrowse::util::ScaleAppTextDimension(6, state.appTextSize);
        metrics.instructionHeight = MeasureTextBlockHeight(bodyFont,
                                                           state.instruction,
                                                           metrics.contentWidth,
                                                           DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK,
                                                           hyperbrowse::util::ScaleAppTextDimension(42, state.appTextSize));
        metrics.summaryGroupTop = metrics.instructionTop + metrics.instructionHeight
            + hyperbrowse::util::ScaleAppTextDimension(14, state.appTextSize);
        metrics.summaryInnerWidth = std::max(0, metrics.contentWidth - (metrics.sectionInset * 2));
        metrics.summaryHeight = MeasureTextBlockHeight(bodyFont,
                                                       state.summary,
                                                       metrics.summaryInnerWidth,
                                                       DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK,
                                                       hyperbrowse::util::ScaleAppTextDimension(52, state.appTextSize));
        metrics.summaryGroupHeight = std::max(
            hyperbrowse::util::ScaleAppTextDimension(78, state.appTextSize),
            metrics.summaryHeight + hyperbrowse::util::ScaleAppTextDimension(30, state.appTextSize));
        metrics.cacheGroupTop = metrics.summaryGroupTop + metrics.summaryGroupHeight
            + hyperbrowse::util::ScaleAppTextDimension(12, state.appTextSize);
        metrics.labelWidth = std::max(
            hyperbrowse::util::ScaleAppTextDimension(kPerformanceSettingsDialogLabelWidth, state.appTextSize),
            std::max(MeasureDialogButtonWidth(bodyFont, L"Thumbnail memory cache:", 0),
                     MeasureDialogButtonWidth(bodyFont, L"Metadata cache:", 0)));
        metrics.editWidth = hyperbrowse::util::ScaleAppTextDimension(kPerformanceSettingsDialogEditWidth, state.appTextSize);
        metrics.unitWidth = std::max(
            hyperbrowse::util::ScaleAppTextDimension(kPerformanceSettingsDialogUnitWidth, state.appTextSize),
            MeasureDialogButtonWidth(bodyFont, L"entries", 0));
        metrics.checkboxWidth = MeasureDialogButtonWidth(
            bodyFont,
            L"Follow profile",
            hyperbrowse::util::ScaleAppTextDimension(kPerformanceSettingsDialogCheckboxWidth, state.appTextSize));
        metrics.rowHeight = std::max(
            hyperbrowse::util::ScaleAppTextDimension(kTextInputEditHeight, state.appTextSize),
            lineHeight + hyperbrowse::util::ScaleAppTextDimension(8, state.appTextSize));
        metrics.checkboxHeight = std::max(
            hyperbrowse::util::ScaleAppTextDimension(22, state.appTextSize),
            lineHeight + hyperbrowse::util::ScaleAppTextDimension(2, state.appTextSize));
        metrics.rowLabelLeft = metrics.contentLeft + metrics.sectionInset;
        metrics.rowValueLeft = metrics.rowLabelLeft + metrics.labelWidth + metrics.controlGap;
        metrics.rowUnitLeft = metrics.rowValueLeft + metrics.editWidth + hyperbrowse::util::ScaleAppTextDimension(8, state.appTextSize);
        metrics.rowCheckboxLeft = metrics.contentLeft + metrics.contentWidth - metrics.sectionInset - metrics.checkboxWidth;
        metrics.firstRowTop = metrics.cacheGroupTop + hyperbrowse::util::ScaleAppTextDimension(30, state.appTextSize);
        metrics.secondRowTop = metrics.firstRowTop + metrics.rowHeight
            + hyperbrowse::util::ScaleAppTextDimension(kPerformanceSettingsDialogValueTopGap + 12, state.appTextSize);
        metrics.pressureStatusTop = metrics.secondRowTop + metrics.rowHeight
            + hyperbrowse::util::ScaleAppTextDimension(18, state.appTextSize);
        const int pressureStatusBottom = metrics.pressureStatusTop + metrics.checkboxHeight;
        metrics.cacheGroupHeight = std::max(
            hyperbrowse::util::ScaleAppTextDimension(136, state.appTextSize),
            pressureStatusBottom - metrics.cacheGroupTop + hyperbrowse::util::ScaleAppTextDimension(12, state.appTextSize));
        metrics.footnoteTop = metrics.cacheGroupTop + metrics.cacheGroupHeight
            + hyperbrowse::util::ScaleAppTextDimension(12, state.appTextSize);
        metrics.minimumFootnoteHeight = MeasureTextBlockHeight(bodyFont,
                                                               state.footnote,
                                                               metrics.contentWidth,
                                                               DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK,
                                                               hyperbrowse::util::ScaleAppTextDimension(42, state.appTextSize));
        metrics.buttonHeight = std::max(
            hyperbrowse::util::ScaleAppTextDimension(kTextInputButtonHeight, state.appTextSize),
            lineHeight + hyperbrowse::util::ScaleAppTextDimension(8, state.appTextSize));
        metrics.applyButtonWidth = MeasureDialogButtonWidth(
            bodyFont,
            L"Apply",
            hyperbrowse::util::ScaleAppTextDimension(kTextInputButtonWidth, state.appTextSize));
        metrics.cancelButtonWidth = MeasureDialogButtonWidth(
            bodyFont,
            L"Cancel",
            hyperbrowse::util::ScaleAppTextDimension(kTextInputButtonWidth, state.appTextSize));
        const int leftRowWidth = metrics.labelWidth
            + metrics.controlGap
            + metrics.editWidth
            + hyperbrowse::util::ScaleAppTextDimension(8, state.appTextSize)
            + metrics.unitWidth;
        metrics.minimumClientWidth = std::max(
            kPerformanceSettingsDialogWidth,
            metrics.contentLeft * 2
                + metrics.sectionInset * 2
                + leftRowWidth
                + hyperbrowse::util::ScaleAppTextDimension(24, state.appTextSize)
                + metrics.checkboxWidth);
        metrics.minimumClientHeight = metrics.footnoteTop
            + metrics.minimumFootnoteHeight
            + hyperbrowse::util::ScaleAppTextDimension(8, state.appTextSize)
            + hyperbrowse::util::ScaleAppTextDimension(16, state.appTextSize)
            + metrics.buttonHeight
            + metrics.margin;
        return metrics;
    }

    void LayoutPerformanceSettingsDialogControls(HWND hwnd, const PerformanceSettingsDialogState& state)
    {
        RECT clientRect{};
        GetClientRect(hwnd, &clientRect);

        const int clientWidth = clientRect.right - clientRect.left;
        const int clientHeight = clientRect.bottom - clientRect.top;
        const PerformanceSettingsDialogLayoutMetrics metrics = BuildPerformanceSettingsDialogLayoutMetrics(clientWidth, state);
        const HFONT bodyFont = state.bodyFont
            ? state.bodyFont
            : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        const int lineHeight = MeasureSingleLineTextHeight(bodyFont, 20);
        const int buttonTop = clientHeight - metrics.margin - metrics.buttonHeight;
        const int dividerTop = buttonTop - hyperbrowse::util::ScaleAppTextDimension(16, state.appTextSize);
        const int cancelLeft = clientWidth - metrics.margin - metrics.cancelButtonWidth;
        const int okLeft = cancelLeft - metrics.controlGap - metrics.applyButtonWidth;

        const HWND titleWindow = GetDlgItem(hwnd, kPerformanceSettingsDialogTitleControlId);
        if (titleWindow)
        {
            MoveWindow(titleWindow, metrics.contentLeft, metrics.titleTop, metrics.contentWidth, metrics.titleHeight, TRUE);
        }

        const HWND instructionWindow = GetDlgItem(hwnd, kPerformanceSettingsInstructionControlId);
        if (instructionWindow)
        {
            MoveWindow(instructionWindow, metrics.contentLeft, metrics.instructionTop, metrics.contentWidth, metrics.instructionHeight, TRUE);
        }

        const HWND summaryGroupWindow = GetDlgItem(hwnd, kPerformanceSettingsSummaryGroupControlId);
        if (summaryGroupWindow)
        {
            MoveWindow(summaryGroupWindow, metrics.contentLeft, metrics.summaryGroupTop, metrics.contentWidth, metrics.summaryGroupHeight, TRUE);
        }

        const HWND summaryWindow = GetDlgItem(hwnd, kPerformanceSettingsSummaryControlId);
        if (summaryWindow)
        {
            MoveWindow(summaryWindow,
                       metrics.contentLeft + kPerformanceSettingsDialogSectionInset,
                       metrics.summaryGroupTop + 22,
                       metrics.summaryInnerWidth,
                       metrics.summaryHeight,
                       TRUE);
        }

        const HWND cacheGroupWindow = GetDlgItem(hwnd, kPerformanceSettingsCacheGroupControlId);
        if (cacheGroupWindow)
        {
            MoveWindow(cacheGroupWindow, metrics.contentLeft, metrics.cacheGroupTop, metrics.contentWidth, metrics.cacheGroupHeight, TRUE);
        }

        const HWND thumbnailLabel = GetDlgItem(hwnd, kPerformanceSettingsThumbnailLabelControlId);
        if (thumbnailLabel)
        {
            MoveWindow(thumbnailLabel,
                       metrics.rowLabelLeft,
                       metrics.firstRowTop + (metrics.rowHeight - lineHeight) / 2,
                       metrics.labelWidth,
                       lineHeight,
                       TRUE);
        }
        if (state.thumbnailEditWindow)
        {
            MoveWindow(state.thumbnailEditWindow,
                       metrics.rowValueLeft,
                       metrics.firstRowTop,
                       metrics.editWidth,
                       metrics.rowHeight,
                       TRUE);
        }
        const HWND thumbnailUnit = GetDlgItem(hwnd, kPerformanceSettingsThumbnailUnitControlId);
        if (thumbnailUnit)
        {
            MoveWindow(thumbnailUnit,
                       metrics.rowUnitLeft,
                       metrics.firstRowTop + (metrics.rowHeight - lineHeight) / 2,
                       metrics.unitWidth,
                       lineHeight,
                       TRUE);
        }
        if (state.thumbnailAutoCheckWindow)
        {
            MoveWindow(state.thumbnailAutoCheckWindow,
                       metrics.rowCheckboxLeft,
                       metrics.firstRowTop + (metrics.rowHeight - metrics.checkboxHeight) / 2,
                       metrics.checkboxWidth,
                       metrics.checkboxHeight,
                       TRUE);
        }

        const HWND metadataLabel = GetDlgItem(hwnd, kPerformanceSettingsMetadataLabelControlId);
        if (metadataLabel)
        {
            MoveWindow(metadataLabel,
                       metrics.rowLabelLeft,
                       metrics.secondRowTop + (metrics.rowHeight - lineHeight) / 2,
                       metrics.labelWidth,
                       lineHeight,
                       TRUE);
        }
        if (state.metadataEditWindow)
        {
            MoveWindow(state.metadataEditWindow,
                       metrics.rowValueLeft,
                       metrics.secondRowTop,
                       metrics.editWidth,
                       metrics.rowHeight,
                       TRUE);
        }
        const HWND metadataUnit = GetDlgItem(hwnd, kPerformanceSettingsMetadataUnitControlId);
        if (metadataUnit)
        {
            MoveWindow(metadataUnit,
                       metrics.rowUnitLeft,
                       metrics.secondRowTop + (metrics.rowHeight - lineHeight) / 2,
                       metrics.unitWidth,
                       lineHeight,
                       TRUE);
        }
        if (state.metadataAutoCheckWindow)
        {
            MoveWindow(state.metadataAutoCheckWindow,
                       metrics.rowCheckboxLeft,
                       metrics.secondRowTop + (metrics.rowHeight - metrics.checkboxHeight) / 2,
                       metrics.checkboxWidth,
                       metrics.checkboxHeight,
                       TRUE);
        }

        if (state.pressureStatusCheckWindow)
        {
            MoveWindow(state.pressureStatusCheckWindow,
                       metrics.rowLabelLeft,
                       metrics.pressureStatusTop,
                       metrics.contentWidth - (kPerformanceSettingsDialogSectionInset * 2),
                       metrics.checkboxHeight,
                       TRUE);
        }

        const HWND footnoteWindow = GetDlgItem(hwnd, kPerformanceSettingsFootnoteControlId);
        if (footnoteWindow)
        {
            MoveWindow(footnoteWindow,
                       metrics.contentLeft,
                       metrics.footnoteTop,
                       metrics.contentWidth,
                       std::max(metrics.minimumFootnoteHeight, dividerTop - metrics.footnoteTop - 8),
                       TRUE);
        }

        const HWND dividerWindow = GetDlgItem(hwnd, kPerformanceSettingsDividerControlId);
        if (dividerWindow)
        {
            MoveWindow(dividerWindow, metrics.contentLeft, dividerTop, metrics.contentWidth, 2, TRUE);
        }

        if (state.okButton)
        {
            MoveWindow(state.okButton, okLeft, buttonTop, metrics.applyButtonWidth, metrics.buttonHeight, TRUE);
        }

        const HWND cancelButton = GetDlgItem(hwnd, IDCANCEL);
        if (cancelButton)
        {
            MoveWindow(cancelButton, cancelLeft, buttonTop, metrics.cancelButtonWidth, metrics.buttonHeight, TRUE);
        }
    }

    bool CollectPerformanceSettingsDialogResult(HWND hwnd, PerformanceSettingsDialogState* state)
    {
        if (!hwnd || !state)
        {
            return false;
        }

        const bool thumbnailAutomatic = state->thumbnailAutoCheckWindow
            && SendMessageW(state->thumbnailAutoCheckWindow, BM_GETCHECK, 0, 0) == BST_CHECKED;
        const bool metadataAutomatic = state->metadataAutoCheckWindow
            && SendMessageW(state->metadataAutoCheckWindow, BM_GETCHECK, 0, 0) == BST_CHECKED;

        std::size_t thumbnailMegabytes = 0;
        if (!thumbnailAutomatic && !TryParsePositiveSizeValue(ReadWindowText(state->thumbnailEditWindow), &thumbnailMegabytes))
        {
            MessageBoxW(hwnd,
                        L"Enter a positive thumbnail cache size in megabytes, or keep Follow profile enabled.",
                        state->title.c_str(),
                        MB_OK | MB_ICONWARNING);
            if (state->thumbnailEditWindow)
            {
                SetFocus(state->thumbnailEditWindow);
            }
            return false;
        }

        std::size_t metadataEntries = 0;
        if (!metadataAutomatic && !TryParsePositiveSizeValue(ReadWindowText(state->metadataEditWindow), &metadataEntries))
        {
            MessageBoxW(hwnd,
                        L"Enter a positive metadata cache capacity in entries, or keep Follow profile enabled.",
                        state->title.c_str(),
                        MB_OK | MB_ICONWARNING);
            if (state->metadataEditWindow)
            {
                SetFocus(state->metadataEditWindow);
            }
            return false;
        }

        state->thumbnailCacheAutomatic = thumbnailAutomatic;
        state->metadataCacheAutomatic = metadataAutomatic;
        state->thumbnailCacheCapacityOverrideBytes = thumbnailAutomatic
            ? 0
            : hyperbrowse::util::SaturatingCastToSizeT(static_cast<std::uint64_t>(thumbnailMegabytes) * 1024ULL * 1024ULL);
        state->metadataCacheCapacityOverrideEntries = metadataAutomatic ? 0 : metadataEntries;
        state->showPressureStateInStatusBar = state->pressureStatusCheckWindow
            && SendMessageW(state->pressureStatusCheckWindow, BM_GETCHECK, 0, 0) == BST_CHECKED;
        return true;
    }

    LRESULT CALLBACK PerformanceSettingsDialogProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        auto* state = reinterpret_cast<PerformanceSettingsDialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

        switch (message)
        {
        case WM_NCCREATE:
        {
            const auto* createStruct = reinterpret_cast<const CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(createStruct->lpCreateParams));
            return TRUE;
        }
        case WM_CREATE:
        {
            state = reinterpret_cast<PerformanceSettingsDialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            if (!state)
            {
                return -1;
            }

            const HFONT font = state->bodyFont
                ? state->bodyFont
                : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            const HINSTANCE hInstance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd, GWLP_HINSTANCE));

            const HWND titleWindow = CreateWindowExW(
                0,
                L"STATIC",
                state->title.c_str(),
                WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX | SS_CENTERIMAGE,
                0,
                0,
                100,
                24,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPerformanceSettingsDialogTitleControlId)),
                hInstance,
                nullptr);
            state->instructionWindow = CreateWindowExW(
                0,
                L"STATIC",
                state->instruction.c_str(),
                WS_CHILD | WS_VISIBLE,
                0,
                0,
                100,
                42,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPerformanceSettingsInstructionControlId)),
                hInstance,
                nullptr);
            const HWND summaryGroupWindow = CreateWindowExW(
                0,
                L"BUTTON",
                L"Current profile",
                WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                0,
                0,
                100,
                70,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPerformanceSettingsSummaryGroupControlId)),
                hInstance,
                nullptr);
            state->summaryWindow = CreateWindowExW(
                0,
                L"STATIC",
                state->summary.c_str(),
                WS_CHILD | WS_VISIBLE,
                0,
                0,
                100,
                46,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPerformanceSettingsSummaryControlId)),
                hInstance,
                nullptr);
            const HWND cacheGroupWindow = CreateWindowExW(
                0,
                L"BUTTON",
                L"Cache limits",
                WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                0,
                0,
                100,
                120,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPerformanceSettingsCacheGroupControlId)),
                hInstance,
                nullptr);
            const HWND thumbnailLabel = CreateWindowExW(
                0,
                L"STATIC",
                L"Thumbnail memory cache:",
                WS_CHILD | WS_VISIBLE,
                0,
                0,
                100,
                20,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPerformanceSettingsThumbnailLabelControlId)),
                hInstance,
                nullptr);
            state->thumbnailEditWindow = CreateWindowExW(
                WS_EX_CLIENTEDGE,
                L"EDIT",
                state->thumbnailCacheText.c_str(),
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_NUMBER,
                0,
                0,
                100,
                kTextInputEditHeight,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPerformanceSettingsThumbnailEditControlId)),
                hInstance,
                nullptr);
            const HWND thumbnailUnitLabel = CreateWindowExW(
                0,
                L"STATIC",
                L"MB",
                WS_CHILD | WS_VISIBLE,
                0,
                0,
                48,
                20,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPerformanceSettingsThumbnailUnitControlId)),
                hInstance,
                nullptr);
            state->thumbnailAutoCheckWindow = CreateWindowExW(
                0,
                L"BUTTON",
                L"Follow profile",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                0,
                0,
                100,
                20,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPerformanceSettingsThumbnailAutoControlId)),
                hInstance,
                nullptr);
            const HWND metadataLabel = CreateWindowExW(
                0,
                L"STATIC",
                L"Metadata cache:",
                WS_CHILD | WS_VISIBLE,
                0,
                0,
                100,
                20,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPerformanceSettingsMetadataLabelControlId)),
                hInstance,
                nullptr);
            state->metadataEditWindow = CreateWindowExW(
                WS_EX_CLIENTEDGE,
                L"EDIT",
                state->metadataCacheText.c_str(),
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_NUMBER,
                0,
                0,
                100,
                kTextInputEditHeight,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPerformanceSettingsMetadataEditControlId)),
                hInstance,
                nullptr);
            const HWND metadataUnitLabel = CreateWindowExW(
                0,
                L"STATIC",
                L"entries",
                WS_CHILD | WS_VISIBLE,
                0,
                0,
                60,
                20,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPerformanceSettingsMetadataUnitControlId)),
                hInstance,
                nullptr);
            state->metadataAutoCheckWindow = CreateWindowExW(
                0,
                L"BUTTON",
                L"Follow profile",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                0,
                0,
                100,
                20,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPerformanceSettingsMetadataAutoControlId)),
                hInstance,
                nullptr);
            state->pressureStatusCheckWindow = CreateWindowExW(
                0,
                L"BUTTON",
                L"Show memory pressure state in the status bar",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                0,
                0,
                100,
                20,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPerformanceSettingsPressureStatusControlId)),
                hInstance,
                nullptr);
            const HWND footnoteWindow = CreateWindowExW(
                0,
                L"STATIC",
                state->footnote.c_str(),
                WS_CHILD | WS_VISIBLE,
                0,
                0,
                100,
                42,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPerformanceSettingsFootnoteControlId)),
                hInstance,
                nullptr);
            const HWND dividerWindow = CreateWindowExW(
                0,
                L"STATIC",
                nullptr,
                WS_CHILD | WS_VISIBLE | SS_ETCHEDHORZ,
                0,
                0,
                100,
                2,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPerformanceSettingsDividerControlId)),
                hInstance,
                nullptr);
            state->okButton = CreateWindowExW(
                0,
                L"BUTTON",
                L"Apply",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                0,
                0,
                kTextInputButtonWidth,
                kTextInputButtonHeight,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDOK)),
                hInstance,
                nullptr);
            const HWND cancelButton = CreateWindowExW(
                0,
                L"BUTTON",
                L"Cancel",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                0,
                0,
                kTextInputButtonWidth,
                kTextInputButtonHeight,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDCANCEL)),
                hInstance,
                nullptr);

            const HWND windows[] = {
                titleWindow,
                state->instructionWindow,
                summaryGroupWindow,
                state->summaryWindow,
                cacheGroupWindow,
                thumbnailLabel,
                state->thumbnailEditWindow,
                thumbnailUnitLabel,
                state->thumbnailAutoCheckWindow,
                metadataLabel,
                state->metadataEditWindow,
                metadataUnitLabel,
                state->metadataAutoCheckWindow,
                state->pressureStatusCheckWindow,
                footnoteWindow,
                dividerWindow,
                state->okButton,
                cancelButton,
            };
            for (HWND window : windows)
            {
                if (window)
                {
                    SendMessageW(window, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
                }
            }
            if (titleWindow && state->titleFont)
            {
                SendMessageW(titleWindow, WM_SETFONT, reinterpret_cast<WPARAM>(state->titleFont), TRUE);
            }
            if (state->instructionWindow && state->bodyFont)
            {
                SendMessageW(state->instructionWindow, WM_SETFONT, reinterpret_cast<WPARAM>(state->bodyFont), TRUE);
            }
            if (state->summaryWindow && state->bodyFont)
            {
                SendMessageW(state->summaryWindow, WM_SETFONT, reinterpret_cast<WPARAM>(state->bodyFont), TRUE);
            }
            if (footnoteWindow && state->bodyFont)
            {
                SendMessageW(footnoteWindow, WM_SETFONT, reinterpret_cast<WPARAM>(state->bodyFont), TRUE);
            }

            if (state->thumbnailAutoCheckWindow)
            {
                SendMessageW(state->thumbnailAutoCheckWindow, BM_SETCHECK, state->thumbnailCacheAutomatic ? BST_CHECKED : BST_UNCHECKED, 0);
            }
            if (state->metadataAutoCheckWindow)
            {
                SendMessageW(state->metadataAutoCheckWindow, BM_SETCHECK, state->metadataCacheAutomatic ? BST_CHECKED : BST_UNCHECKED, 0);
            }
            if (state->pressureStatusCheckWindow)
            {
                SendMessageW(state->pressureStatusCheckWindow, BM_SETCHECK, state->showPressureStateInStatusBar ? BST_CHECKED : BST_UNCHECKED, 0);
            }

            LayoutPerformanceSettingsDialogControls(hwnd, *state);
            RefreshPerformanceSettingsDialogControls(*state);
            CenterWindowOnOwner(hwnd, state->ownerWindow);
            return 0;
        }
        case WM_SIZE:
            if (state)
            {
                LayoutPerformanceSettingsDialogControls(hwnd, *state);
            }
            return 0;
        case WM_SHOWWINDOW:
            if (wParam != FALSE && state)
            {
                SetFocus(state->thumbnailAutoCheckWindow ? state->thumbnailAutoCheckWindow : state->okButton);
                return FALSE;
            }
            break;
        case WM_CTLCOLORDLG:
            return reinterpret_cast<INT_PTR>(GetSysColorBrush(COLOR_WINDOW));
        case WM_CTLCOLORSTATIC:
            SetBkMode(reinterpret_cast<HDC>(wParam), TRANSPARENT);
            SetTextColor(reinterpret_cast<HDC>(wParam), GetSysColor(COLOR_WINDOWTEXT));
            SetBkColor(reinterpret_cast<HDC>(wParam), GetSysColor(COLOR_WINDOW));
            return reinterpret_cast<INT_PTR>(GetSysColorBrush(COLOR_WINDOW));
        case WM_CTLCOLORBTN:
            SetBkMode(reinterpret_cast<HDC>(wParam), TRANSPARENT);
            SetTextColor(reinterpret_cast<HDC>(wParam), GetSysColor(COLOR_WINDOWTEXT));
            SetBkColor(reinterpret_cast<HDC>(wParam), GetSysColor(COLOR_WINDOW));
            return reinterpret_cast<INT_PTR>(GetSysColorBrush(COLOR_WINDOW));
        case WM_COMMAND:
            if (!state)
            {
                break;
            }

            if (LOWORD(wParam) == kPerformanceSettingsThumbnailAutoControlId
                || LOWORD(wParam) == kPerformanceSettingsMetadataAutoControlId)
            {
                RefreshPerformanceSettingsDialogControls(*state);
                return 0;
            }

            if (LOWORD(wParam) == IDOK)
            {
                if (CollectPerformanceSettingsDialogResult(hwnd, state))
                {
                    state->accepted = true;
                    DestroyWindow(hwnd);
                }
                return 0;
            }

            if (LOWORD(wParam) == IDCANCEL)
            {
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            if (state)
            {
                state->done = true;
            }
            return 0;
        default:
            break;
        }

        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    LRESULT CALLBACK TextInputDialogProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        auto* state = reinterpret_cast<TextInputDialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

        switch (message)
        {
        case WM_NCCREATE:
        {
            const auto* createStruct = reinterpret_cast<const CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(createStruct->lpCreateParams));
            return TRUE;
        }
        case WM_CREATE:
        {
            state = reinterpret_cast<TextInputDialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            if (!state)
            {
                return -1;
            }

            const HFONT font = state->bodyFont
                ? state->bodyFont
                : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            const TextInputDialogLayoutMetrics metrics = BuildTextInputDialogLayoutMetrics(*state);
            const int clientWidth = metrics.clientWidth;
            const int contentWidth = metrics.contentWidth;
            const int buttonTop = metrics.buttonTop;
            const int cancelLeft = clientWidth - kTextInputDialogMargin - metrics.cancelButtonWidth;
            const int okLeft = cancelLeft - 8 - metrics.okButtonWidth;

            HWND instructionWindow = CreateWindowExW(
                0,
                L"STATIC",
                state->instruction.c_str(),
                WS_CHILD | WS_VISIBLE,
                kTextInputDialogMargin,
                kTextInputDialogMargin,
                contentWidth,
                metrics.instructionHeight,
                hwnd,
                nullptr,
                reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd, GWLP_HINSTANCE)),
                nullptr);
            state->editWindow = CreateWindowExW(
                WS_EX_CLIENTEDGE,
                L"EDIT",
                state->initialText.c_str(),
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                kTextInputDialogMargin,
                metrics.editTop,
                contentWidth,
                kTextInputEditHeight,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kTextInputEditControlId)),
                reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd, GWLP_HINSTANCE)),
                nullptr);
            HWND dividerWindow = CreateWindowExW(
                0,
                L"STATIC",
                nullptr,
                WS_CHILD | WS_VISIBLE | SS_ETCHEDHORZ,
                kTextInputDialogMargin,
                metrics.dividerTop,
                contentWidth,
                2,
                hwnd,
                nullptr,
                reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd, GWLP_HINSTANCE)),
                nullptr);
            state->okButton = CreateWindowExW(
                0,
                L"BUTTON",
                state->confirmLabel.c_str(),
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                okLeft,
                buttonTop,
                metrics.okButtonWidth,
                kTextInputButtonHeight,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDOK)),
                reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd, GWLP_HINSTANCE)),
                nullptr);
            HWND cancelButton = CreateWindowExW(
                0,
                L"BUTTON",
                L"Cancel",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                cancelLeft,
                buttonTop,
                metrics.cancelButtonWidth,
                kTextInputButtonHeight,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDCANCEL)),
                reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd, GWLP_HINSTANCE)),
                nullptr);

            if (instructionWindow) SendMessageW(instructionWindow, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            if (state->editWindow) SendMessageW(state->editWindow, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            if (dividerWindow) SendMessageW(dividerWindow, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            if (state->okButton) SendMessageW(state->okButton, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            if (cancelButton) SendMessageW(cancelButton, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

            if (state->editWindow)
            {
                SendMessageW(state->editWindow, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(6, 6));
                SendMessageW(state->editWindow,
                             EM_SETSEL,
                             static_cast<WPARAM>(state->selectionStart),
                             static_cast<LPARAM>(state->selectionEnd));
            }

            CenterWindowOnOwner(hwnd, state->ownerWindow);
            return 0;
        }
        case WM_SHOWWINDOW:
            if (wParam != FALSE && state && state->editWindow)
            {
                SetFocus(state->editWindow);
                return FALSE;
            }
            break;
        case WM_CTLCOLORDLG:
            return reinterpret_cast<INT_PTR>(GetSysColorBrush(COLOR_WINDOW));
        case WM_CTLCOLORSTATIC:
            SetBkMode(reinterpret_cast<HDC>(wParam), TRANSPARENT);
            SetTextColor(reinterpret_cast<HDC>(wParam), GetSysColor(COLOR_WINDOWTEXT));
            SetBkColor(reinterpret_cast<HDC>(wParam), GetSysColor(COLOR_WINDOW));
            return reinterpret_cast<INT_PTR>(GetSysColorBrush(COLOR_WINDOW));
        case WM_COMMAND:
            if (!state)
            {
                break;
            }

            if (LOWORD(wParam) == IDOK)
            {
                const int length = state->editWindow ? GetWindowTextLengthW(state->editWindow) : 0;
                std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
                if (state->editWindow)
                {
                    GetWindowTextW(state->editWindow, text.data(), static_cast<int>(text.size()));
                }
                text.resize(wcslen(text.c_str()));
                state->resultText = std::move(text);
                state->accepted = true;
                DestroyWindow(hwnd);
                return 0;
            }

            if (LOWORD(wParam) == IDCANCEL)
            {
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            if (state)
            {
                state->done = true;
            }
            return 0;
        default:
            break;
        }

        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    void LayoutBatchRenameDialogControls(HWND hwnd, const BatchRenameDialogState& state)
    {
        RECT clientRect{};
        GetClientRect(hwnd, &clientRect);

        const int clientWidth = clientRect.right - clientRect.left;
        const int clientHeight = clientRect.bottom - clientRect.top;
        const int contentWidth = clientWidth - (kTextInputDialogMargin * 2);
        const int instructionHeight = 44;
        const int helpHeight = 38;
        const int buttonTop = clientHeight - kTextInputDialogMargin - kTextInputButtonHeight;
        const int listTop = kTextInputDialogMargin + instructionHeight + 6 + kTextInputEditHeight + 10 + helpHeight + 8;
        const int listHeight = std::max(120, buttonTop - 12 - listTop);
        const int cancelLeft = clientWidth - kTextInputDialogMargin - kTextInputButtonWidth;
        const int okLeft = cancelLeft - 8 - kTextInputButtonWidth;

        const HWND instructionWindow = GetDlgItem(hwnd, kBatchRenameInstructionControlId);
        if (instructionWindow)
        {
            MoveWindow(instructionWindow,
                       kTextInputDialogMargin,
                       kTextInputDialogMargin,
                       contentWidth,
                       instructionHeight,
                       TRUE);
        }

        if (state.patternEditWindow)
        {
            MoveWindow(state.patternEditWindow,
                       kTextInputDialogMargin,
                       kTextInputDialogMargin + instructionHeight + 6,
                       contentWidth,
                       kTextInputEditHeight,
                       TRUE);
        }

        const HWND helpWindow = GetDlgItem(hwnd, kBatchRenameHelpControlId);
        if (helpWindow)
        {
            MoveWindow(helpWindow,
                       kTextInputDialogMargin,
                       kTextInputDialogMargin + instructionHeight + 6 + kTextInputEditHeight + 10,
                       contentWidth,
                       helpHeight,
                       TRUE);
        }

        if (state.previewListWindow)
        {
            MoveWindow(state.previewListWindow,
                       kTextInputDialogMargin,
                       listTop,
                       contentWidth,
                       listHeight,
                       TRUE);

            ListView_SetColumnWidth(state.previewListWindow, 0, std::max(160, contentWidth / 3));
            ListView_SetColumnWidth(state.previewListWindow, 1, std::max(200, contentWidth / 2));
            ListView_SetColumnWidth(state.previewListWindow, 2, std::max(110, contentWidth - (std::max(160, contentWidth / 3) + std::max(200, contentWidth / 2)) - 8));
        }

        if (state.okButton)
        {
            MoveWindow(state.okButton, okLeft, buttonTop, kTextInputButtonWidth, kTextInputButtonHeight, TRUE);
        }

        const HWND cancelButton = GetDlgItem(hwnd, IDCANCEL);
        if (cancelButton)
        {
            MoveWindow(cancelButton, cancelLeft, buttonTop, kTextInputButtonWidth, kTextInputButtonHeight, TRUE);
        }
    }

    void RefreshBatchRenameDialogPreview(BatchRenameDialogState* state)
    {
        if (!state || !state->patternEditWindow || !state->previewListWindow)
        {
            return;
        }

        const int textLength = GetWindowTextLengthW(state->patternEditWindow);
        std::wstring pattern(static_cast<std::size_t>(textLength) + 1, L'\0');
        GetWindowTextW(state->patternEditWindow, pattern.data(), static_cast<int>(pattern.size()));
        pattern.resize(wcslen(pattern.c_str()));

        state->pattern = pattern;
        state->previewRows.clear();
        state->resultLeafNames.clear();
        state->canAccept = false;

        std::vector<std::wstring> generatedLeafNames;
        generatedLeafNames.reserve(state->items.size());
        state->previewRows.reserve(state->items.size());

        bool hasErrors = false;
        bool hasChanges = false;
        for (std::size_t index = 0; index < state->items.size(); ++index)
        {
            const auto& item = state->items[index];
            BatchRenamePreviewRow row;
            row.currentLeafName = item.fileName;

            std::wstring validationMessage;
            if (TryBuildBatchRenamePatternLeafName(pattern,
                                                   item,
                                                   index + 1,
                                                   state->items.size(),
                                                   state->numberWidth,
                                                   &row.renamedLeafName,
                                                   &validationMessage))
            {
                row.status = StringsEqualInsensitive(row.renamedLeafName, item.fileName)
                    ? L"No change"
                    : L"Ready";
                hasChanges = hasChanges || !StringsEqualInsensitive(row.renamedLeafName, item.fileName);
                generatedLeafNames.push_back(row.renamedLeafName);
            }
            else
            {
                row.valid = false;
                row.status = validationMessage.empty() ? L"Invalid pattern" : validationMessage;
                hasErrors = true;
                generatedLeafNames.push_back(std::wstring{});
            }

            state->previewRows.push_back(std::move(row));
        }

        if (!hasErrors)
        {
            std::unordered_map<std::wstring, std::size_t> firstIndexByName;
            for (std::size_t index = 0; index < generatedLeafNames.size(); ++index)
            {
                const std::wstring normalizedLeafName = ToLowercaseCopy(generatedLeafNames[index]);
                const auto [iterator, inserted] = firstIndexByName.emplace(normalizedLeafName, index);
                if (inserted)
                {
                    continue;
                }

                state->previewRows[index].valid = false;
                state->previewRows[index].status = L"Duplicate name";
                state->previewRows[iterator->second].valid = false;
                state->previewRows[iterator->second].status = L"Duplicate name";
                hasErrors = true;
            }
        }

        if (!hasErrors && hasChanges)
        {
            state->resultLeafNames = std::move(generatedLeafNames);
            state->canAccept = true;
        }

        ListView_DeleteAllItems(state->previewListWindow);
        for (std::size_t index = 0; index < state->previewRows.size(); ++index)
        {
            const BatchRenamePreviewRow& row = state->previewRows[index];
            LVITEMW item{};
            item.mask = LVIF_TEXT;
            item.iItem = static_cast<int>(index);
            item.pszText = const_cast<LPWSTR>(row.currentLeafName.c_str());
            const int insertedIndex = ListView_InsertItem(state->previewListWindow, &item);
            if (insertedIndex < 0)
            {
                continue;
            }

            ListView_SetItemText(state->previewListWindow,
                                 insertedIndex,
                                 1,
                                 const_cast<LPWSTR>(row.renamedLeafName.c_str()));
            ListView_SetItemText(state->previewListWindow,
                                 insertedIndex,
                                 2,
                                 const_cast<LPWSTR>(row.status.c_str()));
        }

        if (state->okButton)
        {
            EnableWindow(state->okButton, state->canAccept ? TRUE : FALSE);
        }
    }

    LRESULT CALLBACK BatchRenameDialogProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        auto* state = reinterpret_cast<BatchRenameDialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

        switch (message)
        {
        case WM_NCCREATE:
        {
            const auto* createStruct = reinterpret_cast<const CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(createStruct->lpCreateParams));
            return TRUE;
        }
        case WM_CREATE:
        {
            state = reinterpret_cast<BatchRenameDialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            if (!state)
            {
                return -1;
            }

            const HFONT font = state->bodyFont
                ? state->bodyFont
                : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            const HWND instructionWindow = CreateWindowExW(
                0,
                L"STATIC",
                state->instruction.c_str(),
                WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
                0,
                0,
                100,
                40,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kBatchRenameInstructionControlId)),
                reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd, GWLP_HINSTANCE)),
                nullptr);

            state->patternEditWindow = CreateWindowExW(
                WS_EX_CLIENTEDGE,
                L"EDIT",
                state->initialPattern.c_str(),
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                0,
                0,
                100,
                kTextInputEditHeight,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kBatchRenamePatternEditControlId)),
                reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd, GWLP_HINSTANCE)),
                nullptr);

            const HWND helpWindow = CreateWindowExW(
                0,
                L"STATIC",
                L"Tokens: {name} original stem, {num} zero-padded sequence, {num:N} explicit width, {ext} original extension, {folder} parent folder.",
                WS_CHILD | WS_VISIBLE,
                0,
                0,
                100,
                38,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kBatchRenameHelpControlId)),
                reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd, GWLP_HINSTANCE)),
                nullptr);

            state->previewListWindow = CreateWindowExW(
                WS_EX_CLIENTEDGE,
                WC_LISTVIEWW,
                nullptr,
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SHOWSELALWAYS,
                0,
                0,
                100,
                100,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kBatchRenamePreviewListControlId)),
                reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd, GWLP_HINSTANCE)),
                nullptr);

            state->okButton = CreateWindowExW(
                0,
                L"BUTTON",
                L"Rename",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                0,
                0,
                kTextInputButtonWidth,
                kTextInputButtonHeight,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDOK)),
                reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd, GWLP_HINSTANCE)),
                nullptr);

            const HWND cancelButton = CreateWindowExW(
                0,
                L"BUTTON",
                L"Cancel",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                0,
                0,
                kTextInputButtonWidth,
                kTextInputButtonHeight,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDCANCEL)),
                reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd, GWLP_HINSTANCE)),
                nullptr);

            if (instructionWindow) SendMessageW(instructionWindow, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            if (state->patternEditWindow) SendMessageW(state->patternEditWindow, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            if (helpWindow) SendMessageW(helpWindow, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            if (state->okButton) SendMessageW(state->okButton, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            if (cancelButton) SendMessageW(cancelButton, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

            if (state->previewListWindow)
            {
                ListView_SetExtendedListViewStyle(state->previewListWindow,
                                                  LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);

                LVCOLUMNW column{};
                column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
                column.pszText = const_cast<LPWSTR>(L"Current Name");
                column.cx = 220;
                ListView_InsertColumn(state->previewListWindow, 0, &column);

                column.pszText = const_cast<LPWSTR>(L"New Name");
                column.cx = 300;
                column.iSubItem = 1;
                ListView_InsertColumn(state->previewListWindow, 1, &column);

                column.pszText = const_cast<LPWSTR>(L"Status");
                column.cx = 140;
                column.iSubItem = 2;
                ListView_InsertColumn(state->previewListWindow, 2, &column);
            }

            LayoutBatchRenameDialogControls(hwnd, *state);
            if (state->patternEditWindow)
            {
                SendMessageW(state->patternEditWindow,
                             EM_SETSEL,
                             0,
                             static_cast<LPARAM>(state->initialPattern.size()));
            }
            RefreshBatchRenameDialogPreview(state);
            CenterWindowOnOwner(hwnd, state->ownerWindow);
            return 0;
        }
        case WM_SIZE:
            if (state)
            {
                LayoutBatchRenameDialogControls(hwnd, *state);
            }
            return 0;
        case WM_SHOWWINDOW:
            if (wParam != FALSE && state && state->patternEditWindow)
            {
                SetFocus(state->patternEditWindow);
                return FALSE;
            }
            break;
        case WM_CTLCOLORDLG:
            return reinterpret_cast<INT_PTR>(GetSysColorBrush(COLOR_WINDOW));
        case WM_CTLCOLORSTATIC:
            SetBkMode(reinterpret_cast<HDC>(wParam), TRANSPARENT);
            SetTextColor(reinterpret_cast<HDC>(wParam), GetSysColor(COLOR_WINDOWTEXT));
            SetBkColor(reinterpret_cast<HDC>(wParam), GetSysColor(COLOR_WINDOW));
            return reinterpret_cast<INT_PTR>(GetSysColorBrush(COLOR_WINDOW));
        case WM_CTLCOLORBTN:
            SetBkMode(reinterpret_cast<HDC>(wParam), TRANSPARENT);
            SetTextColor(reinterpret_cast<HDC>(wParam), GetSysColor(COLOR_WINDOWTEXT));
            SetBkColor(reinterpret_cast<HDC>(wParam), GetSysColor(COLOR_WINDOW));
            return reinterpret_cast<INT_PTR>(GetSysColorBrush(COLOR_WINDOW));
        case WM_COMMAND:
            if (!state)
            {
                break;
            }

            if (LOWORD(wParam) == kBatchRenamePatternEditControlId && HIWORD(wParam) == EN_CHANGE)
            {
                RefreshBatchRenameDialogPreview(state);
                return 0;
            }

            if (LOWORD(wParam) == IDOK)
            {
                if (state->canAccept)
                {
                    state->accepted = true;
                    DestroyWindow(hwnd);
                }
                return 0;
            }

            if (LOWORD(wParam) == IDCANCEL)
            {
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            if (state)
            {
                state->done = true;
            }
            return 0;
        default:
            break;
        }

        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    bool PromptForBatchRenamePattern(HWND ownerWindow,
                                     HINSTANCE instance,
                                     hyperbrowse::util::AppTextSize appTextSize,
                                     std::wstring initialPattern,
                                     std::vector<hyperbrowse::browser::BrowserItem> items,
                                     std::vector<std::wstring>* resultLeafNames)
    {
        if (!resultLeafNames || items.size() < 2)
        {
            return false;
        }

        WNDCLASSEXW windowClass{};
        if (GetClassInfoExW(instance, kBatchRenameDialogClassName, &windowClass) == FALSE)
        {
            windowClass.cbSize = sizeof(windowClass);
            windowClass.lpfnWndProc = &BatchRenameDialogProc;
            windowClass.hInstance = instance;
            windowClass.lpszClassName = kBatchRenameDialogClassName;
            windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
            if (RegisterClassExW(&windowClass) == 0)
            {
                return false;
            }
        }

        BatchRenameDialogState state;
        state.ownerWindow = ownerWindow;
        state.appTextSize = hyperbrowse::util::NormalizeAppTextSize(static_cast<std::uint32_t>(appTextSize));
        state.bodyFont = CreateDialogUiFont(9, FW_NORMAL, state.appTextSize);
        if (!state.bodyFont)
        {
            state.bodyFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        }
        state.title = L"Batch Rename";
        state.instruction = L"Enter a rename pattern. HyperBrowse previews every generated file name and preserves extensions unless you place {ext} yourself.";
        state.initialPattern = std::move(initialPattern);
        state.items = std::move(items);
        state.numberWidth = std::max(3, CountDecimalDigits(state.items.size()));

        RECT windowRect{0, 0, kBatchRenameDialogWidth, kBatchRenameDialogHeight};
        AdjustWindowRectEx(&windowRect,
                           WS_CAPTION | WS_SYSMENU | WS_POPUP | WS_CLIPCHILDREN,
                           FALSE,
                           WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT);

        if (ownerWindow)
        {
            EnableWindow(ownerWindow, FALSE);
        }

        HWND dialogWindow = CreateWindowExW(
            WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT,
            kBatchRenameDialogClassName,
            state.title.c_str(),
            WS_CAPTION | WS_SYSMENU | WS_POPUP | WS_CLIPCHILDREN,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            windowRect.right - windowRect.left,
            windowRect.bottom - windowRect.top,
            ownerWindow,
            nullptr,
            instance,
            &state);

        if (!dialogWindow)
        {
            if (ownerWindow)
            {
                EnableWindow(ownerWindow, TRUE);
            }
            DeleteFontIfOwned(state.bodyFont);
            return false;
        }

        SetWindowTextW(dialogWindow, state.title.c_str());

        ShowWindow(dialogWindow, SW_SHOWNORMAL);
        UpdateWindow(dialogWindow);

        MSG message{};
        while (!state.done && GetMessageW(&message, nullptr, 0, 0) > 0)
        {
            if (!IsDialogMessageW(dialogWindow, &message))
            {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }

        if (ownerWindow)
        {
            EnableWindow(ownerWindow, TRUE);
            SetForegroundWindow(ownerWindow);
            SetActiveWindow(ownerWindow);
        }

        DeleteFontIfOwned(state.bodyFont);

        if (!state.accepted)
        {
            return false;
        }

        *resultLeafNames = std::move(state.resultLeafNames);
        return true;
    }

    void PaintAboutDialog(HDC hdc, const RECT& clientRect, const AboutDialogState& state)
    {
        const int contentRight = clientRect.right - kAboutDialogMargin;
        const int artLeft = contentRight - kAboutDialogBrandArtSize;
        const int artTop = kAboutDialogMargin - 4;
        const int iconLeft = kAboutDialogMargin;
        const int iconTop = kAboutDialogMargin + 2;
        const int iconSize = 48;
        const int textLeft = iconLeft + iconSize + 20;
        const int textRight = artLeft - 28;
        const int textWidth = std::max(320, textRight - textLeft);

        const int titleTop = kAboutDialogMargin - 2;
        const int titleHeight = MeasureTextBlockHeight(state.titleFont, state.title, textWidth, DT_LEFT | DT_NOPREFIX | DT_SINGLELINE, 44);
        const int subtitleTop = titleTop + titleHeight + 10;
        const int subtitleHeight = MeasureTextBlockHeight(state.subtitleFont, state.subtitle, textWidth, DT_LEFT | DT_NOPREFIX | DT_SINGLELINE, 28);
        const int introTop = subtitleTop + subtitleHeight + 10;
        const int introHeight = MeasureTextBlockHeight(state.bodyFont, state.intro, textWidth, DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK, 40);
        const int artFrameBottom = artTop + kAboutDialogBrandArtSize + 10;
        const int headerHeight = std::max(kAboutDialogHeaderHeight,
                                          std::max(artFrameBottom + kAboutDialogMargin - 8,
                                                   introTop + introHeight + kAboutDialogMargin - 8));

        const int bodyWidth = clientRect.right - (kAboutDialogMargin * 2);
        const int headingHeight = MeasureTextBlockHeight(state.subtitleFont, state.bodyHeading, bodyWidth, DT_LEFT | DT_NOPREFIX | DT_SINGLELINE, 28);
        const int footerTextWidth = MeasureAboutDialogFooterTextWidth(state, clientRect.right);
        const int footerTextHeight = MeasureTextBlockHeight(state.footerFont, state.footer, footerTextWidth, DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK, 0);
        const int footerHeight = std::max(kAboutDialogFooterHeight, std::max(footerTextHeight + 34, kAboutDialogButtonHeight + 36));

        const RECT headerRect{clientRect.left, clientRect.top, clientRect.right, clientRect.top + headerHeight};
        const RECT footerRect{clientRect.left, clientRect.bottom - footerHeight, clientRect.right, clientRect.bottom};
        const RECT bodyRect{clientRect.left, headerRect.bottom, clientRect.right, footerRect.top};

        HBRUSH backgroundBrush = CreateSolidBrush(state.background);
        FillRect(hdc, &clientRect, backgroundBrush);
        DeleteObject(backgroundBrush);

        HBRUSH headerBrush = CreateSolidBrush(state.headerBackground);
        FillRect(hdc, &headerRect, headerBrush);
        DeleteObject(headerBrush);

        HBRUSH footerBrush = CreateSolidBrush(state.footerBackground);
        FillRect(hdc, &footerRect, footerBrush);
        DeleteObject(footerBrush);

        HBRUSH bodyBrush = CreateSolidBrush(state.panelBackground);
        FillRect(hdc, &bodyRect, bodyBrush);
        DeleteObject(bodyBrush);

        HPEN borderPen = CreatePen(PS_SOLID, 1, state.border);
        HGDIOBJ oldPen = SelectObject(hdc, borderPen);
        MoveToEx(hdc, clientRect.left, headerRect.bottom - 1, nullptr);
        LineTo(hdc, clientRect.right, headerRect.bottom - 1);
        MoveToEx(hdc, clientRect.left, footerRect.top, nullptr);
        LineTo(hdc, clientRect.right, footerRect.top);
        SelectObject(hdc, oldPen);
        DeleteObject(borderPen);

        if (state.heroIcon)
        {
            DrawIconEx(hdc, iconLeft, iconTop, state.heroIcon, iconSize, iconSize, 0, nullptr, DI_NORMAL);
        }

        if (state.brandArt)
        {
            RECT artFrame{artLeft - 10, artTop - 10, artLeft + kAboutDialogBrandArtSize + 10, artTop + kAboutDialogBrandArtSize + 10};
            HBRUSH artPanelBrush = CreateSolidBrush(BlendColor(state.headerBackground, state.background, state.darkMode ? 42 : 18));
            HPEN artBorderPen = CreatePen(PS_SOLID, 1, state.border);
            HGDIOBJ oldBrush = SelectObject(hdc, artPanelBrush);
            HGDIOBJ oldArtPen = SelectObject(hdc, artBorderPen);
            RoundRect(hdc, artFrame.left, artFrame.top, artFrame.right, artFrame.bottom, 18, 18);
            SelectObject(hdc, oldArtPen);
            SelectObject(hdc, oldBrush);
            DeleteObject(artBorderPen);
            DeleteObject(artPanelBrush);

            hyperbrowse::util::DrawBitmapWithAlpha(hdc, *state.brandArt, artLeft, artTop, kAboutDialogBrandArtSize, kAboutDialogBrandArtSize);
        }

        RECT titleRect{textLeft, titleTop, textRight, titleTop + titleHeight};
        RECT subtitleRect{textLeft, subtitleTop, textRight, subtitleTop + subtitleHeight};
        RECT introRect{textLeft, introTop, textRight, introTop + introHeight};

        hyperbrowse::render::DrawGdiText(hdc,
                    state.titleFont ? state.titleFont : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)),
                    state.title.c_str(),
                    -1,
                    titleRect,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX,
                    state.text,
                    state.headerBackground);

        hyperbrowse::render::DrawGdiText(hdc,
                    state.subtitleFont ? state.subtitleFont : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)),
                    state.subtitle.c_str(),
                    -1,
                    subtitleRect,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX,
                    state.accent,
                    state.headerBackground);

        hyperbrowse::render::DrawGdiText(hdc,
                    state.bodyFont ? state.bodyFont : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)),
                    state.intro.c_str(),
                    -1,
                    introRect,
                    DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK,
                    state.mutedText,
                    state.headerBackground);

        RECT headingRect{kAboutDialogMargin, bodyRect.top + 24, clientRect.right - kAboutDialogMargin, bodyRect.top + 24 + headingHeight};
        hyperbrowse::render::DrawGdiText(hdc,
                    state.subtitleFont ? state.subtitleFont : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)),
                    state.bodyHeading.c_str(),
                    -1,
                    headingRect,
                    DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX,
                    state.accent,
                    state.panelBackground);

        RECT bodyTextRect{kAboutDialogMargin, headingRect.bottom + 12, clientRect.right - kAboutDialogMargin, footerRect.top - 18};
        hyperbrowse::render::DrawGdiText(hdc,
                    state.bodyFont ? state.bodyFont : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)),
                    state.bodyContent.c_str(),
                    -1,
                    bodyTextRect,
                    DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK,
                    state.text,
                    state.panelBackground);

        RECT footerTextRect{kAboutDialogMargin,
                    footerRect.top + 18,
                    std::max(kAboutDialogMargin + 200, AboutDialogFooterButtonsLeft(clientRect, state) - 20),
                    footerRect.bottom - 16};
        hyperbrowse::render::DrawGdiText(hdc,
                    state.footerFont ? state.footerFont : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)),
                    state.footer.c_str(),
                    -1,
                    footerTextRect,
                    DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK,
                    state.mutedText,
                    state.footerBackground);
    }

    LRESULT CALLBACK AboutDialogProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        auto* state = reinterpret_cast<AboutDialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

        switch (message)
        {
        case WM_NCCREATE:
        {
            const auto* createStruct = reinterpret_cast<const CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(createStruct->lpCreateParams));
            return TRUE;
        }
        case WM_CREATE:
        {
            state = reinterpret_cast<AboutDialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            if (!state)
            {
                return -1;
            }

            state->githubButton = CreateWindowExW(
                0,
                L"BUTTON",
                kAboutDialogGitHubLabel,
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                0,
                0,
                state->githubButtonWidth,
                kAboutDialogButtonHeight,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_ABOUT_OPEN_GITHUB)),
                reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd, GWLP_HINSTANCE)),
                nullptr);
            state->supportButton = CreateWindowExW(
                0,
                L"BUTTON",
                kAboutDialogSupportLabel,
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                0,
                0,
                state->supportButtonWidth,
                kAboutDialogButtonHeight,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_ABOUT_OPEN_SUPPORT)),
                reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd, GWLP_HINSTANCE)),
                nullptr);
            state->okButton = CreateWindowExW(
                0,
                L"BUTTON",
                L"OK",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                0,
                0,
                kAboutDialogButtonWidth,
                kAboutDialogButtonHeight,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDOK)),
                reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd, GWLP_HINSTANCE)),
                nullptr);
            if (state->githubButton)
            {
                SendMessageW(state->githubButton,
                             WM_SETFONT,
                             reinterpret_cast<WPARAM>(state->subtitleFont ? state->subtitleFont : GetStockObject(DEFAULT_GUI_FONT)),
                             TRUE);
            }
            if (state->supportButton)
            {
                SendMessageW(state->supportButton,
                             WM_SETFONT,
                             reinterpret_cast<WPARAM>(state->subtitleFont ? state->subtitleFont : GetStockObject(DEFAULT_GUI_FONT)),
                             TRUE);
            }
            if (state->okButton)
            {
                SendMessageW(state->okButton,
                             WM_SETFONT,
                             reinterpret_cast<WPARAM>(state->bodyFont ? state->bodyFont : GetStockObject(DEFAULT_GUI_FONT)),
                             TRUE);
            }

            if (state->windowIcon)
            {
                SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(state->windowIcon));
                SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(state->windowIcon));
            }

            ApplyWindowFrameTheme(hwnd, state->darkMode);
            CenterWindowOnOwner(hwnd, state->ownerWindow);
            LayoutAboutDialogControls(hwnd, *state);
            return 0;
        }
        case WM_SIZE:
            if (state)
            {
                LayoutAboutDialogControls(hwnd, *state);
            }
            return 0;
        case WM_SHOWWINDOW:
            if (wParam != FALSE && state && state->okButton)
            {
                SetFocus(state->okButton);
                return FALSE;
            }
            break;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
        {
            if (!state)
            {
                break;
            }

            PAINTSTRUCT ps{};
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT clientRect{};
            GetClientRect(hwnd, &clientRect);
            PaintAboutDialog(hdc, clientRect, *state);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_DRAWITEM:
            if (state && (wParam == ID_ABOUT_OPEN_GITHUB || wParam == ID_ABOUT_OPEN_SUPPORT))
            {
                DrawAboutDialogLinkButton(*reinterpret_cast<const DRAWITEMSTRUCT*>(lParam), *state);
                return TRUE;
            }
            break;
        case WM_COMMAND:
            if (LOWORD(wParam) == ID_ABOUT_OPEN_GITHUB || LOWORD(wParam) == ID_ABOUT_OPEN_SUPPORT)
            {
                OpenAboutDialogLink(hwnd, LOWORD(wParam));
                return 0;
            }
            if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
            {
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            if (state)
            {
                state->done = true;
            }
            return 0;
        default:
            break;
        }

        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    void LayoutShortcutReferenceControls(HWND hwnd, const ShortcutReferenceState& state)
    {
        RECT client{};
        GetClientRect(hwnd, &client);
        const int clientWidth = static_cast<int>(client.right);
        const int clientHeight = static_cast<int>(client.bottom);
        const int margin = MulDiv(kShortcutReferenceMargin, static_cast<int>(state.dpi), 96);
        const int gap = MulDiv(kShortcutReferenceControlGap, static_cast<int>(state.dpi), 96);
        const int subtitleHeight = MulDiv(kShortcutReferenceSubtitleHeight, static_cast<int>(state.dpi), 96);
        const int buttonWidth = MulDiv(kShortcutReferenceButtonWidth, static_cast<int>(state.dpi), 96);
        const int buttonHeight = MulDiv(kShortcutReferenceButtonHeight, static_cast<int>(state.dpi), 96);
        const int listTop = margin + subtitleHeight + gap;
        const int buttonTop = std::max(listTop + 1, clientHeight - margin - buttonHeight);
        const int listBottom = std::max(listTop + 1, buttonTop - gap);

        if (state.subtitleWindow)
        {
            SetWindowPos(state.subtitleWindow, nullptr, margin, margin,
                         std::max(1, clientWidth - margin * 2), subtitleHeight,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
        if (state.listWindow)
        {
            SetWindowPos(state.listWindow, nullptr, margin, listTop,
                         std::max(1, clientWidth - margin * 2), std::max(1, listBottom - listTop),
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
        if (state.closeButton)
        {
            SetWindowPos(state.closeButton, nullptr, clientWidth - margin - buttonWidth, buttonTop,
                         buttonWidth, buttonHeight,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }

    }

    void AutoSizeShortcutReferenceColumns(HWND listWindow)
    {
        if (!listWindow)
        {
            return;
        }

        const int columnCount = Header_GetItemCount(ListView_GetHeader(listWindow));
        for (int column = 0; column < columnCount; ++column)
        {
            ListView_SetColumnWidth(listWindow, column, LVSCW_AUTOSIZE_USEHEADER);
        }
    }

    void AddShortcutReferenceRows(HWND listWindow)
    {
        if (!listWindow)
        {
            return;
        }

        LVCOLUMNW column{};
        column.mask = LVCF_TEXT | LVCF_WIDTH;
        column.cx = 170;
        column.pszText = const_cast<LPWSTR>(L"Category");
        ListView_InsertColumn(listWindow, 0, &column);
        column.cx = 130;
        column.pszText = const_cast<LPWSTR>(L"Shortcut");
        ListView_InsertColumn(listWindow, 1, &column);
        column.cx = 430;
        column.pszText = const_cast<LPWSTR>(L"Action");
        ListView_InsertColumn(listWindow, 2, &column);
        column.cx = 110;
        column.pszText = const_cast<LPWSTR>(L"Context");
        ListView_InsertColumn(listWindow, 3, &column);

        std::unordered_set<std::wstring> seenRows;
        const auto appendCatalog = [&](std::span<const hyperbrowse::ui::ShortcutDefinition> catalog)
        {
            for (const hyperbrowse::ui::ShortcutDefinition& shortcut : catalog)
            {
                std::wstring rowKey(shortcut.group);
                rowKey.push_back(L'\x1f');
                rowKey.append(shortcut.displayChord);
                rowKey.push_back(L'\x1f');
                rowKey.append(shortcut.action);
                rowKey.push_back(L'\x1f');
                rowKey.append(shortcut.context == hyperbrowse::ui::ShortcutContext::MainWindow
                    ? L"main"
                    : L"viewer");
                if (!seenRows.insert(std::move(rowKey)).second)
                {
                    continue;
                }

                const std::wstring category(shortcut.group);
                const std::wstring chord(shortcut.displayChord);
                const std::wstring action(shortcut.action);
                const std::wstring context = shortcut.context == hyperbrowse::ui::ShortcutContext::MainWindow
                    ? L"Main window"
                    : L"Viewer";
                LVITEMW item{};
                item.mask = LVIF_TEXT;
                item.iItem = ListView_GetItemCount(listWindow);
                item.pszText = const_cast<LPWSTR>(category.c_str());
                const int row = ListView_InsertItem(listWindow, &item);
                if (row < 0)
                {
                    continue;
                }
                ListView_SetItemText(listWindow, row, 1, const_cast<LPWSTR>(chord.c_str()));
                ListView_SetItemText(listWindow, row, 2, const_cast<LPWSTR>(action.c_str()));
                ListView_SetItemText(listWindow, row, 3, const_cast<LPWSTR>(context.c_str()));
            }
        };

        appendCatalog(hyperbrowse::ui::MainWindowShortcuts());
        appendCatalog(hyperbrowse::ui::ViewerShortcuts());
        AutoSizeShortcutReferenceColumns(listWindow);
    }

    LRESULT CALLBACK ShortcutReferenceProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        auto* state = reinterpret_cast<ShortcutReferenceState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

        switch (message)
        {
        case WM_NCCREATE:
        {
            const auto* createStruct = reinterpret_cast<const CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(createStruct->lpCreateParams));
            return TRUE;
        }
        case WM_CREATE:
        {
            state = reinterpret_cast<ShortcutReferenceState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            if (!state)
            {
                return -1;
            }

            state->subtitleWindow = CreateWindowExW(
                0, L"STATIC", L"Main-window and viewer commands, grouped by task.",
                WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
                0, 0, 0, 0, hwnd, nullptr,
                reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd, GWLP_HINSTANCE)), nullptr);
            state->listWindow = CreateWindowExW(
                WS_EX_CLIENTEDGE, WC_LISTVIEWW, nullptr,
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL,
                0, 0, 0, 0, hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kShortcutReferenceListControlId)),
                reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd, GWLP_HINSTANCE)), nullptr);
            state->closeButton = CreateWindowExW(
                0, L"BUTTON", L"Close",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                0, 0, 0, 0, hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDCANCEL)),
                reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd, GWLP_HINSTANCE)), nullptr);

            const HFONT bodyFont = state->bodyFont ? state->bodyFont : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            if (state->subtitleWindow)
            {
                SendMessageW(state->subtitleWindow, WM_SETFONT, reinterpret_cast<WPARAM>(bodyFont), TRUE);
            }
            if (state->listWindow)
            {
                SendMessageW(state->listWindow, WM_SETFONT, reinterpret_cast<WPARAM>(bodyFont), TRUE);
                ListView_SetExtendedListViewStyle(state->listWindow, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
                ListView_SetBkColor(state->listWindow, state->listBackground);
                ListView_SetTextBkColor(state->listWindow, state->listBackground);
                ListView_SetTextColor(state->listWindow, state->text);
                AddShortcutReferenceRows(state->listWindow);
            }
            if (state->closeButton)
            {
                SendMessageW(state->closeButton, WM_SETFONT, reinterpret_cast<WPARAM>(bodyFont), TRUE);
            }

            state->backgroundBrush = CreateSolidBrush(state->background);
            state->listBackgroundBrush = CreateSolidBrush(state->listBackground);
            ApplyWindowFrameTheme(hwnd, state->darkMode, state->background, state->text, state->border);
            LayoutShortcutReferenceControls(hwnd, *state);
            return 0;
        }
        case WM_SIZE:
            if (state)
            {
                LayoutShortcutReferenceControls(hwnd, *state);
            }
            return 0;
        case WM_GETMINMAXINFO:
            if (state)
            {
                auto* sizeInfo = reinterpret_cast<MINMAXINFO*>(lParam);
                if (sizeInfo)
                {
                    const int dpi = static_cast<int>(state->dpi == 0 ? 96 : state->dpi);
                    sizeInfo->ptMinTrackSize.x = MulDiv(kShortcutReferenceMinimumWidth, dpi, 96);
                    sizeInfo->ptMinTrackSize.y = MulDiv(kShortcutReferenceMinimumHeight, dpi, 96);
                    return 0;
                }
            }
            break;
        case WM_DPICHANGED:
            if (state)
            {
                state->dpi = HIWORD(wParam) == 0 ? 96 : HIWORD(wParam);
                const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
                if (suggested)
                {
                    SetWindowPos(hwnd, nullptr,
                                 suggested->left, suggested->top,
                                 suggested->right - suggested->left,
                                 suggested->bottom - suggested->top,
                                 SWP_NOZORDER | SWP_NOACTIVATE);
                }
                LayoutShortcutReferenceControls(hwnd, *state);
            }
            return 0;
        case WM_CTLCOLORSTATIC:
            if (state)
            {
                const HDC dc = reinterpret_cast<HDC>(wParam);
                SetTextColor(dc, state->text);
                SetBkColor(dc, state->background);
                SetBkMode(dc, TRANSPARENT);
                return reinterpret_cast<LRESULT>(state->backgroundBrush);
            }
            break;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            if (state)
            {
                PAINTSTRUCT paint{};
                const HDC dc = BeginPaint(hwnd, &paint);
                RECT client{};
                GetClientRect(hwnd, &client);
                FillRect(dc, &client, state->backgroundBrush);
                EndPaint(hwnd, &paint);
                return 0;
            }
            break;
        case WM_COMMAND:
            if (LOWORD(wParam) == IDCANCEL || LOWORD(wParam) == IDOK)
            {
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            if (state)
            {
                if (state->windowSlot && *state->windowSlot == hwnd)
                {
                    *state->windowSlot = nullptr;
                }
                if (state->backgroundBrush)
                {
                    DeleteObject(state->backgroundBrush);
                    state->backgroundBrush = nullptr;
                }
                if (state->listBackgroundBrush)
                {
                    DeleteObject(state->listBackgroundBrush);
                    state->listBackgroundBrush = nullptr;
                }
                DeleteFontIfOwned(state->bodyFont);
                state->bodyFont = nullptr;
                delete state;
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            }
            return 0;
        default:
            break;
        }

        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    HWND ConsolidatedSettingsControlHandle(const ConsolidatedSettingsDialogState& state,
                                            ConsolidatedSettingsControl control)
    {
        return state.controls[static_cast<std::size_t>(control)];
    }

    void SetConsolidatedSettingsCheck(const ConsolidatedSettingsDialogState& state,
                                      ConsolidatedSettingsControl control,
                                      bool checked)
    {
        if (const HWND window = ConsolidatedSettingsControlHandle(state, control))
        {
            SendMessageW(window, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
        }
    }

    void UpdateConsolidatedSettingsDependencies(const ConsolidatedSettingsDialogState& state)
    {
        const bool rawPairingEnabled = ConsolidatedSettingsControlHandle(state, ConsolidatedSettingsControl::RawPairingEnabled)
            && SendMessageW(ConsolidatedSettingsControlHandle(state, ConsolidatedSettingsControl::RawPairingEnabled), BM_GETCHECK, 0, 0) == BST_CHECKED;
        const bool thumbnailAutomatic = ConsolidatedSettingsControlHandle(state, ConsolidatedSettingsControl::ThumbnailCacheAutomatic)
            && SendMessageW(ConsolidatedSettingsControlHandle(state, ConsolidatedSettingsControl::ThumbnailCacheAutomatic), BM_GETCHECK, 0, 0) == BST_CHECKED;
        const bool metadataAutomatic = ConsolidatedSettingsControlHandle(state, ConsolidatedSettingsControl::MetadataCacheAutomatic)
            && SendMessageW(ConsolidatedSettingsControlHandle(state, ConsolidatedSettingsControl::MetadataCacheAutomatic), BM_GETCHECK, 0, 0) == BST_CHECKED;
        EnableWindow(ConsolidatedSettingsControlHandle(state, ConsolidatedSettingsControl::RawPreferJpeg), rawPairingEnabled);
        EnableWindow(ConsolidatedSettingsControlHandle(state, ConsolidatedSettingsControl::RawPreferRaw), rawPairingEnabled);
        EnableWindow(ConsolidatedSettingsControlHandle(state, ConsolidatedSettingsControl::ThumbnailCache), !thumbnailAutomatic);
        EnableWindow(ConsolidatedSettingsControlHandle(state, ConsolidatedSettingsControl::MetadataCache), !metadataAutomatic);
    }

    void UpdateConsolidatedSettingsCacheValues(
        const ConsolidatedSettingsDialogState& state,
        ConsolidatedSettingsControl changedAutomaticControl = ConsolidatedSettingsControl::Count)
    {
        const HWND profileCombo = ConsolidatedSettingsControlHandle(state, ConsolidatedSettingsControl::ResourceProfile);
        const HWND thumbnailAutomatic = ConsolidatedSettingsControlHandle(state, ConsolidatedSettingsControl::ThumbnailCacheAutomatic);
        const HWND metadataAutomatic = ConsolidatedSettingsControlHandle(state, ConsolidatedSettingsControl::MetadataCacheAutomatic);
        if (!profileCombo || !thumbnailAutomatic || !metadataAutomatic)
        {
            return;
        }

        const int profileIndex = static_cast<int>(SendMessageW(profileCombo, CB_GETCURSEL, 0, 0));
        if (profileIndex < 0 || profileIndex > 3)
        {
            return;
        }

        const auto profile = static_cast<hyperbrowse::util::ResourceProfile>(profileIndex);
        const auto updateCacheValue = [&](ConsolidatedSettingsControl automaticControl,
                                          ConsolidatedSettingsControl cacheControl,
                                          std::size_t capacity)
        {
            const HWND automatic = ConsolidatedSettingsControlHandle(state, automaticControl);
            if (SendMessageW(automatic, BM_GETCHECK, 0, 0) == BST_CHECKED
                || changedAutomaticControl == automaticControl)
            {
                SetWindowTextW(ConsolidatedSettingsControlHandle(state, cacheControl), std::to_wstring(capacity).c_str());
            }
        };

        updateCacheValue(
            ConsolidatedSettingsControl::ThumbnailCacheAutomatic,
            ConsolidatedSettingsControl::ThumbnailCache,
            hyperbrowse::services::ThumbnailScheduler::ResolveCacheCapacityBytes(0, profile) / (1024ULL * 1024ULL));
        updateCacheValue(
            ConsolidatedSettingsControl::MetadataCacheAutomatic,
            ConsolidatedSettingsControl::MetadataCache,
            hyperbrowse::services::ImageMetadataService::ResolveCacheCapacityEntries(0, profile));
    }

    void ShowConsolidatedSettingsPage(ConsolidatedSettingsDialogState& state, ConsolidatedSettingsPage page)
    {
        for (std::size_t index = 0; index < state.pageControls.size(); ++index)
        {
            const int command = index == static_cast<std::size_t>(page) ? SW_SHOW : SW_HIDE;
            for (HWND control : state.pageControls[index])
            {
                ShowWindow(control, command);
            }
        }
    }

    bool CollectConsolidatedSettings(HWND hwnd, ConsolidatedSettingsDialogState* state)
    {
        if (!hwnd || !state)
        {
            return false;
        }

        const auto comboIndex = [&](ConsolidatedSettingsControl control) -> int
        {
            const HWND combo = ConsolidatedSettingsControlHandle(*state, control);
            return combo ? static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0)) : -1;
        };
        const auto isChecked = [&](ConsolidatedSettingsControl control) -> bool
        {
            const HWND button = ConsolidatedSettingsControlHandle(*state, control);
            return button && SendMessageW(button, BM_GETCHECK, 0, 0) == BST_CHECKED;
        };

        const int transitionIndex = comboIndex(ConsolidatedSettingsControl::TransitionStyle);
        if (transitionIndex < 0 || transitionIndex >= static_cast<int>(kSlideshowTransitionOptions.size()))
        {
            MessageBoxW(hwnd, L"Select a transition type.", state->title.c_str(), MB_OK | MB_ICONWARNING);
            SetFocus(ConsolidatedSettingsControlHandle(*state, ConsolidatedSettingsControl::TransitionStyle));
            return false;
        }

        UINT slideshowDurationMs = 0;
        if (!TryReadDialogUInt(ConsolidatedSettingsControlHandle(*state, ConsolidatedSettingsControl::SlideshowDuration),
                               kSlideshowMinimumDurationMs,
                               kSlideshowMaximumDurationMs,
                               &slideshowDurationMs))
        {
            MessageBoxW(hwnd, L"Slide duration must be between 250 and 60000 milliseconds.", state->title.c_str(), MB_OK | MB_ICONWARNING);
            SetFocus(ConsolidatedSettingsControlHandle(*state, ConsolidatedSettingsControl::SlideshowDuration));
            return false;
        }

        UINT transitionDurationMs = 0;
        if (!TryReadDialogUInt(ConsolidatedSettingsControlHandle(*state, ConsolidatedSettingsControl::TransitionDuration),
                               kSlideshowMinimumTransitionDurationMs,
                               kSlideshowMaximumTransitionDurationMs,
                               &transitionDurationMs))
        {
            MessageBoxW(hwnd, L"Transition duration must be between 100 and 5000 milliseconds.", state->title.c_str(), MB_OK | MB_ICONWARNING);
            SetFocus(ConsolidatedSettingsControlHandle(*state, ConsolidatedSettingsControl::TransitionDuration));
            return false;
        }

        const bool thumbnailAutomatic = isChecked(ConsolidatedSettingsControl::ThumbnailCacheAutomatic);
        const bool metadataAutomatic = isChecked(ConsolidatedSettingsControl::MetadataCacheAutomatic);
        std::size_t thumbnailMegabytes = 0;
        if (!thumbnailAutomatic
            && (!TryParsePositiveSizeValue(ReadWindowText(ConsolidatedSettingsControlHandle(*state, ConsolidatedSettingsControl::ThumbnailCache)), &thumbnailMegabytes)
                || thumbnailMegabytes > std::numeric_limits<std::uint64_t>::max() / (1024ULL * 1024ULL)))
        {
            MessageBoxW(hwnd, L"Enter a positive thumbnail cache size in megabytes, or keep Follow profile enabled.", state->title.c_str(), MB_OK | MB_ICONWARNING);
            SetFocus(ConsolidatedSettingsControlHandle(*state, ConsolidatedSettingsControl::ThumbnailCache));
            return false;
        }

        std::size_t metadataEntries = 0;
        if (!metadataAutomatic
            && !TryParsePositiveSizeValue(ReadWindowText(ConsolidatedSettingsControlHandle(*state, ConsolidatedSettingsControl::MetadataCache)), &metadataEntries))
        {
            MessageBoxW(hwnd, L"Enter a positive metadata cache capacity in entries, or keep Follow profile enabled.", state->title.c_str(), MB_OK | MB_ICONWARNING);
            SetFocus(ConsolidatedSettingsControlHandle(*state, ConsolidatedSettingsControl::MetadataCache));
            return false;
        }

        const int wheelIndex = isChecked(ConsolidatedSettingsControl::ViewerWheelNavigate) ? 1 : 0;
        const int overlaySizeIndex = comboIndex(ConsolidatedSettingsControl::OverlayTextSize);
        const int appTextSizeIndex = comboIndex(ConsolidatedSettingsControl::AppTextSize);
        const int thumbnailSizeIndex = comboIndex(ConsolidatedSettingsControl::ThumbnailSize);
        const int resourceProfileIndex = comboIndex(ConsolidatedSettingsControl::ResourceProfile);
        if (overlaySizeIndex < 0 || overlaySizeIndex > 2 || appTextSizeIndex < 0 || appTextSizeIndex > 2
            || thumbnailSizeIndex < 0 || thumbnailSizeIndex >= static_cast<int>(kThumbnailSizePresets.size())
            || resourceProfileIndex < 0 || resourceProfileIndex > 3)
        {
            MessageBoxW(hwnd, L"Select a value for every settings list.", state->title.c_str(), MB_OK | MB_ICONWARNING);
            return false;
        }

        state->slideshowIntervalMs = slideshowDurationMs;
        state->slideshowTransitionDurationMs = transitionDurationMs;
        state->slideshowTransitionStyle = kSlideshowTransitionOptions[static_cast<std::size_t>(transitionIndex)].style;
        state->useSlideshowTransition = isChecked(ConsolidatedSettingsControl::TransitionEnabled);
        state->viewerMouseWheelBehavior = static_cast<hyperbrowse::viewer::MouseWheelBehavior>(wheelIndex);
            state->invertKeyboardPanning = isChecked(ConsolidatedSettingsControl::InvertKeyboardPanning);
        state->rawJpegPairedOperationsEnabled = isChecked(ConsolidatedSettingsControl::RawPairingEnabled);
        state->pairedRawJpegViewerPreference = isChecked(ConsolidatedSettingsControl::RawPreferJpeg)
            ? hyperbrowse::browser::RawJpegDisplayPreference::Jpeg
            : hyperbrowse::browser::RawJpegDisplayPreference::Raw;
        state->defaultViewerToSecondaryMonitor = isChecked(ConsolidatedSettingsControl::SecondaryMonitor);
        state->infoOverlaysVisible = isChecked(ConsolidatedSettingsControl::InfoOverlays);
        state->fullMetadataVisible = isChecked(ConsolidatedSettingsControl::FullMetadata);
        state->overlayTextSize = static_cast<hyperbrowse::viewer::InfoOverlayTextSize>(overlaySizeIndex);
        state->darkTheme = isChecked(ConsolidatedSettingsControl::ThemeDark);
        state->appTextSize = static_cast<hyperbrowse::util::AppTextSize>(appTextSizeIndex);
        state->thumbnailSizePreset = kThumbnailSizePresets[static_cast<std::size_t>(thumbnailSizeIndex)];
        state->thumbnailDetailsVisible = isChecked(ConsolidatedSettingsControl::ThumbnailDetails);
        state->compactThumbnailLayout = isChecked(ConsolidatedSettingsControl::CompactLayout);
        state->detailsStripVisible = isChecked(ConsolidatedSettingsControl::DetailsPanel);
        state->resourceProfile = static_cast<hyperbrowse::util::ResourceProfile>(resourceProfileIndex);
        state->persistentThumbnailCacheEnabled = isChecked(ConsolidatedSettingsControl::PersistentCache);
        state->thumbnailCacheCapacityOverrideBytes = thumbnailAutomatic
            ? 0
            : hyperbrowse::util::SaturatingCastToSizeT(static_cast<std::uint64_t>(thumbnailMegabytes) * 1024ULL * 1024ULL);
        state->metadataCacheCapacityOverrideEntries = metadataAutomatic ? 0 : metadataEntries;
        state->showPressureStateInStatusBar = isChecked(ConsolidatedSettingsControl::PressureStatus);
        state->nvJpegEnabled = isChecked(ConsolidatedSettingsControl::NvJpeg);
        state->libRawOutOfProcessEnabled = isChecked(ConsolidatedSettingsControl::LibRawOutOfProcess);
        state->recursiveBrowsingEnabled = isChecked(ConsolidatedSettingsControl::RecursiveBrowsing);
        state->showSubfoldersInBrowser = isChecked(ConsolidatedSettingsControl::ShowSubfolders);
        state->closeMainWindowOnEscape = isChecked(ConsolidatedSettingsControl::CloseOnEscape);
        state->singleInstanceEnabled = isChecked(ConsolidatedSettingsControl::SingleInstance);
        return true;
    }

    LRESULT CALLBACK ConsolidatedSettingsDialogProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        auto* state = reinterpret_cast<ConsolidatedSettingsDialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        switch (message)
        {
        case WM_NCCREATE:
        {
            const auto* createStruct = reinterpret_cast<const CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(createStruct->lpCreateParams));
            return TRUE;
        }
        case WM_CREATE:
        {
            state = reinterpret_cast<ConsolidatedSettingsDialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            if (!state)
            {
                return -1;
            }
            state->dialogWindow = hwnd;
            const HFONT font = state->bodyFont ? state->bodyFont : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            const int tabLeft = kConsolidatedSettingsMargin;
            const int tabTop = kConsolidatedSettingsMargin;
            const int tabWidth = kConsolidatedSettingsDialogWidth - (kConsolidatedSettingsMargin * 2);
            const int buttonTop = kConsolidatedSettingsDialogHeight
                - kConsolidatedSettingsMargin
                - kConsolidatedSettingsButtonHeight;
            const int tabHeight = buttonTop - kConsolidatedSettingsMargin - tabTop;
            state->tabWindow = CreateWindowExW(
                0, WC_TABCONTROLW, nullptr,
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | TCS_FOCUSNEVER,
                tabLeft, tabTop, tabWidth, tabHeight, hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kConsolidatedSettingsTabControlId)),
                state->instance, nullptr);
            const wchar_t* tabNames[] = {L"Slideshow", L"Viewer", L"Appearance", L"Performance", L"Behavior"};
            for (std::size_t tabIndex = 0; tabIndex < std::size(tabNames); ++tabIndex)
            {
                TCITEMW item{};
                item.mask = TCIF_TEXT;
                item.pszText = const_cast<wchar_t*>(tabNames[tabIndex]);
                TabCtrl_InsertItem(state->tabWindow, static_cast<int>(tabIndex), &item);
            }

            RECT displayRect{};
            GetClientRect(state->tabWindow, &displayRect);
            TabCtrl_AdjustRect(state->tabWindow, FALSE, &displayRect);
            const int pageLeft = tabLeft + displayRect.left;
            const int pageTop = tabTop + displayRect.top;
            const int pageRight = tabLeft + displayRect.right;
            HDC measureDc = GetDC(hwnd);
            HFONT oldMeasureFont = nullptr;
            if (measureDc)
            {
                oldMeasureFont = static_cast<HFONT>(SelectObject(measureDc, font));
            }
            TEXTMETRICW textMetrics{};
            if (measureDc)
            {
                GetTextMetricsW(measureDc, &textMetrics);
            }
            const auto measureTextWidth = [&](const wchar_t* text)
            {
                if (!measureDc || !text)
                {
                    return 0;
                }
                SIZE size{};
                GetTextExtentPoint32W(measureDc, text, static_cast<int>(wcslen(text)), &size);
                return static_cast<int>(size.cx);
            };
            const std::array labelTexts{
                L"Transition style",
                L"Slide duration (milliseconds)",
                L"Transition duration (milliseconds)",
                L"Timing bounds",
                L"Mouse wheel",
                L"Paired viewer preference",
                L"Overlay text size",
                L"Application text size",
                L"Thumbnail size",
                L"Theme",
                L"Resource profile",
                L"Thumbnail cache cap (MB)",
                L"Metadata cache cap (entries)"};
            const std::array checkboxTexts{
                L"Use slideshow transitions",
                L"Treat paired RAW+JPEG files as one operation",
                L"Open viewers on a secondary monitor when available",
                L"Show viewer detail overlays",
                L"Show full metadata",
                L"Show thumbnail details",
                L"Use compact thumbnail layout",
                L"Show the details panel",
                L"Keep the persistent thumbnail cache enabled",
                L"Follow profile",
                L"Show memory pressure state in the status bar",
                L"Use NVIDIA JPEG acceleration when available",
                L"Use out-of-process LibRaw fallback",
                L"Browse folders recursively",
                L"Show subfolders in the browser",
                L"Close the main window when Esc is pressed",
                L"Use a single application instance"};
            const std::array comboTexts{
                L"Crossfade",
                L"Horizontal Blinds",
                L"Venetian Blinds",
                L"Monochrome Reveal",
                L"250-60000 ms slides; 100-5000 ms transitions",
                L"Small",
                L"Medium",
                L"Large",
                L"640 px",
                L"Conservative",
                L"Balanced",
                L"Performance",
                L"Aggressive"};
            int measuredLabelWidth = 0;
            for (const wchar_t* text : labelTexts)
            {
                measuredLabelWidth = std::max(measuredLabelWidth, measureTextWidth(text));
            }
            int measuredValueWidth = 0;
            for (const wchar_t* text : comboTexts)
            {
                measuredValueWidth = std::max(measuredValueWidth, measureTextWidth(text));
            }
            int measuredCheckboxWidth = 0;
            for (const wchar_t* text : checkboxTexts)
            {
                measuredCheckboxWidth = std::max(measuredCheckboxWidth, measureTextWidth(text));
            }
            if (measureDc)
            {
                SelectObject(measureDc, oldMeasureFont);
                ReleaseDC(hwnd, measureDc);
            }
            const int labelWidth = std::max(300, measuredLabelWidth + 18);
            const int valueLeft = pageLeft + labelWidth;
            const int valueWidth = std::max(360, measuredValueWidth + 48);
            const int rowHeight = std::max(32, static_cast<int>(textMetrics.tmHeight) + 12);
            const int rowGap = std::max(10, rowHeight / 3);
            const int checkboxWidth = std::max(measuredCheckboxWidth + 38, pageRight - pageLeft);
            const int labelTopInset = std::max(2, (rowHeight - static_cast<int>(textMetrics.tmHeight)) / 2);
            auto label = [&](ConsolidatedSettingsPage page, const wchar_t* text, int y)
            {
                return CreateConsolidatedSettingsControl(*state, page, L"STATIC", text, SS_LEFT | SS_NOPREFIX,
                                                          pageLeft, y + labelTopInset, labelWidth - 12, rowHeight - labelTopInset, 0);
            };
            auto check = [&](ConsolidatedSettingsPage page, ConsolidatedSettingsControl control, const wchar_t* text, int y, DWORD extraStyle = 0)
            {
                return CreateConsolidatedSettingsControl(*state, page, L"BUTTON", text, BS_AUTOCHECKBOX | WS_TABSTOP | extraStyle,
                                                          pageLeft, y, checkboxWidth, rowHeight, ConsolidatedSettingsControlId(control), control);
            };
            auto combo = [&](ConsolidatedSettingsPage page, ConsolidatedSettingsControl control, int y)
            {
                return CreateConsolidatedSettingsControl(*state, page, L"COMBOBOX", nullptr, CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL,
                                                          valueLeft, y, valueWidth, 180, ConsolidatedSettingsControlId(control), control);
            };
            auto edit = [&](ConsolidatedSettingsPage page, ConsolidatedSettingsControl control, int y)
            {
                return CreateConsolidatedSettingsControl(*state, page, L"EDIT", nullptr, WS_TABSTOP | ES_AUTOHSCROLL | ES_NUMBER,
                                                          valueLeft, y, 140, rowHeight, ConsolidatedSettingsControlId(control), control);
            };
            auto spin = [&](ConsolidatedSettingsPage page, ConsolidatedSettingsControl control, int y)
            {
                return CreateConsolidatedSettingsControl(*state, page, UPDOWN_CLASSW, nullptr, UDS_ALIGNRIGHT | UDS_ARROWKEYS,
                                                          valueLeft + 140, y, 22, rowHeight, ConsolidatedSettingsControlId(control), control);
            };
            auto radio = [&](ConsolidatedSettingsPage page, ConsolidatedSettingsControl control, const wchar_t* text, int x, int y, DWORD extraStyle = 0)
            {
                return CreateConsolidatedSettingsControl(*state, page, L"BUTTON", text, BS_AUTORADIOBUTTON | WS_TABSTOP | extraStyle,
                                                          x, y, valueWidth / 2, rowHeight, ConsolidatedSettingsControlId(control), control);
            };

            int y = pageTop + 16;
            label(ConsolidatedSettingsPage::Slideshow, L"Transition style", y);
            combo(ConsolidatedSettingsPage::Slideshow, ConsolidatedSettingsControl::TransitionStyle, y);
            y += rowHeight + rowGap;
            label(ConsolidatedSettingsPage::Slideshow, L"Slide duration (milliseconds)", y);
            edit(ConsolidatedSettingsPage::Slideshow, ConsolidatedSettingsControl::SlideshowDuration, y);
            spin(ConsolidatedSettingsPage::Slideshow, ConsolidatedSettingsControl::SlideshowDurationSpin, y);
            y += rowHeight + rowGap;
            label(ConsolidatedSettingsPage::Slideshow, L"Transition duration (milliseconds)", y);
            edit(ConsolidatedSettingsPage::Slideshow, ConsolidatedSettingsControl::TransitionDuration, y);
            spin(ConsolidatedSettingsPage::Slideshow, ConsolidatedSettingsControl::TransitionDurationSpin, y);
            y += rowHeight + rowGap;
            label(ConsolidatedSettingsPage::Slideshow, L"Timing bounds", y);
            CreateConsolidatedSettingsControl(*state, ConsolidatedSettingsPage::Slideshow, L"STATIC", L"250-60000 ms slides; 100-5000 ms transitions",
                                              SS_LEFT | SS_NOPREFIX, valueLeft, y + 4, valueWidth, rowHeight, 0);

            y = pageTop + 16;
            check(ConsolidatedSettingsPage::Viewer, ConsolidatedSettingsControl::TransitionEnabled, L"Use slideshow transitions", y);
            y += rowHeight + rowGap;
            label(ConsolidatedSettingsPage::Viewer, L"Mouse wheel", y);
            radio(ConsolidatedSettingsPage::Viewer, ConsolidatedSettingsControl::ViewerWheelZoom, L"Zoom", valueLeft, y, WS_GROUP);
            radio(ConsolidatedSettingsPage::Viewer, ConsolidatedSettingsControl::ViewerWheelNavigate, L"Navigate", valueLeft + 145, y);
            y += rowHeight + rowGap;
                check(ConsolidatedSettingsPage::Viewer, ConsolidatedSettingsControl::InvertKeyboardPanning, L"Invert Keyboard Panning", y);
                y += rowHeight + rowGap;
            check(ConsolidatedSettingsPage::Viewer, ConsolidatedSettingsControl::RawPairingEnabled, L"Treat paired RAW+JPEG files as one operation", y);
            y += rowHeight + rowGap;
            label(ConsolidatedSettingsPage::Viewer, L"Paired viewer preference", y);
            radio(ConsolidatedSettingsPage::Viewer, ConsolidatedSettingsControl::RawPreferRaw, L"Prefer RAW", valueLeft, y, WS_GROUP);
            radio(ConsolidatedSettingsPage::Viewer, ConsolidatedSettingsControl::RawPreferJpeg, L"Prefer JPEG", valueLeft + 145, y);
            y += rowHeight + rowGap;
            check(ConsolidatedSettingsPage::Viewer, ConsolidatedSettingsControl::SecondaryMonitor, L"Open viewers on a secondary monitor when available", y);
            y += rowHeight + rowGap;
            check(ConsolidatedSettingsPage::Viewer, ConsolidatedSettingsControl::InfoOverlays, L"Show viewer detail overlays", y);
            y += rowHeight + rowGap;
            check(ConsolidatedSettingsPage::Viewer, ConsolidatedSettingsControl::FullMetadata, L"Show full metadata", y);
            y += rowHeight + rowGap;
            label(ConsolidatedSettingsPage::Viewer, L"Overlay text size", y);
            combo(ConsolidatedSettingsPage::Viewer, ConsolidatedSettingsControl::OverlayTextSize, y);

            y = pageTop + 16;
            label(ConsolidatedSettingsPage::Appearance, L"Theme", y);
            radio(ConsolidatedSettingsPage::Appearance, ConsolidatedSettingsControl::ThemeLight, L"Light", valueLeft, y, WS_GROUP);
            radio(ConsolidatedSettingsPage::Appearance, ConsolidatedSettingsControl::ThemeDark, L"Dark", valueLeft + 145, y);
            y += rowHeight + rowGap;
            label(ConsolidatedSettingsPage::Appearance, L"Application text size", y);
            combo(ConsolidatedSettingsPage::Appearance, ConsolidatedSettingsControl::AppTextSize, y);
            y += rowHeight + rowGap;
            label(ConsolidatedSettingsPage::Appearance, L"Thumbnail size", y);
            combo(ConsolidatedSettingsPage::Appearance, ConsolidatedSettingsControl::ThumbnailSize, y);
            y += rowHeight + rowGap;
            check(ConsolidatedSettingsPage::Appearance, ConsolidatedSettingsControl::ThumbnailDetails, L"Show thumbnail details", y);
            y += rowHeight + rowGap;
            check(ConsolidatedSettingsPage::Appearance, ConsolidatedSettingsControl::CompactLayout, L"Use compact thumbnail layout", y);
            y += rowHeight + rowGap;
            check(ConsolidatedSettingsPage::Appearance, ConsolidatedSettingsControl::DetailsPanel, L"Show the details panel", y);

            y = pageTop + 16;
            label(ConsolidatedSettingsPage::Performance, L"Resource profile", y);
            combo(ConsolidatedSettingsPage::Performance, ConsolidatedSettingsControl::ResourceProfile, y);
            y += rowHeight + rowGap;
            check(ConsolidatedSettingsPage::Performance, ConsolidatedSettingsControl::PersistentCache, L"Keep the persistent thumbnail cache enabled", y);
            y += rowHeight + rowGap;
            label(ConsolidatedSettingsPage::Performance, L"Thumbnail cache cap (MB)", y);
            edit(ConsolidatedSettingsPage::Performance, ConsolidatedSettingsControl::ThumbnailCache, y);
            check(ConsolidatedSettingsPage::Performance, ConsolidatedSettingsControl::ThumbnailCacheAutomatic, L"Follow profile", y + 30);
            y += rowHeight + rowGap + 34;
            label(ConsolidatedSettingsPage::Performance, L"Metadata cache cap (entries)", y);
            edit(ConsolidatedSettingsPage::Performance, ConsolidatedSettingsControl::MetadataCache, y);
            check(ConsolidatedSettingsPage::Performance, ConsolidatedSettingsControl::MetadataCacheAutomatic, L"Follow profile", y + 30);
            y += rowHeight + rowGap + 34;
            check(ConsolidatedSettingsPage::Performance, ConsolidatedSettingsControl::PressureStatus, L"Show memory pressure state in the status bar", y);
            y += rowHeight + rowGap;
            check(ConsolidatedSettingsPage::Performance, ConsolidatedSettingsControl::NvJpeg, L"Use NVIDIA JPEG acceleration when available", y);
            y += rowHeight + rowGap;
            check(ConsolidatedSettingsPage::Performance, ConsolidatedSettingsControl::LibRawOutOfProcess, L"Use out-of-process LibRaw fallback", y);

            y = pageTop + 16;
            check(ConsolidatedSettingsPage::Behavior, ConsolidatedSettingsControl::RecursiveBrowsing, L"Browse folders recursively", y);
            y += rowHeight + rowGap;
            check(ConsolidatedSettingsPage::Behavior, ConsolidatedSettingsControl::ShowSubfolders, L"Show subfolders in the browser", y);
            y += rowHeight + rowGap;
            check(ConsolidatedSettingsPage::Behavior, ConsolidatedSettingsControl::CloseOnEscape, L"Close the main window when Esc is pressed", y);
            y += rowHeight + rowGap;
            check(ConsolidatedSettingsPage::Behavior, ConsolidatedSettingsControl::SingleInstance, L"Use a single application instance", y);

            for (HWND control : state->controls)
            {
                if (control)
                {
                    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
                }
            }

            const auto addComboText = [&](ConsolidatedSettingsControl control, const wchar_t* text)
            {
                SendMessageW(ConsolidatedSettingsControlHandle(*state, control), CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
            };
            for (const SlideshowTransitionOption& option : kSlideshowTransitionOptions)
            {
                addComboText(ConsolidatedSettingsControl::TransitionStyle, option.label);
            }
            addComboText(ConsolidatedSettingsControl::OverlayTextSize, L"Small");
            addComboText(ConsolidatedSettingsControl::OverlayTextSize, L"Medium");
            addComboText(ConsolidatedSettingsControl::OverlayTextSize, L"Large");
            addComboText(ConsolidatedSettingsControl::AppTextSize, L"Small");
            addComboText(ConsolidatedSettingsControl::AppTextSize, L"Medium");
            addComboText(ConsolidatedSettingsControl::AppTextSize, L"Large");
            for (const auto preset : kThumbnailSizePresets)
            {
                const std::wstring text = std::to_wstring(static_cast<int>(preset)) + L" px";
                addComboText(ConsolidatedSettingsControl::ThumbnailSize, text.c_str());
            }
            addComboText(ConsolidatedSettingsControl::ResourceProfile, L"Conservative");
            addComboText(ConsolidatedSettingsControl::ResourceProfile, L"Balanced");
            addComboText(ConsolidatedSettingsControl::ResourceProfile, L"Performance");
            addComboText(ConsolidatedSettingsControl::ResourceProfile, L"Aggressive");

            SendMessageW(ConsolidatedSettingsControlHandle(*state, ConsolidatedSettingsControl::TransitionStyle), CB_SETCURSEL,
                         SlideshowTransitionComboIndex(state->slideshowTransitionStyle), 0);
            SendMessageW(ConsolidatedSettingsControlHandle(*state, ConsolidatedSettingsControl::OverlayTextSize), CB_SETCURSEL,
                         static_cast<int>(state->overlayTextSize), 0);
            SendMessageW(ConsolidatedSettingsControlHandle(*state, ConsolidatedSettingsControl::AppTextSize), CB_SETCURSEL,
                         static_cast<int>(state->appTextSize), 0);
            const auto thumbnailIterator = std::find(kThumbnailSizePresets.begin(), kThumbnailSizePresets.end(), state->thumbnailSizePreset);
            SendMessageW(ConsolidatedSettingsControlHandle(*state, ConsolidatedSettingsControl::ThumbnailSize), CB_SETCURSEL,
                         thumbnailIterator == kThumbnailSizePresets.end() ? 0 : static_cast<int>(thumbnailIterator - kThumbnailSizePresets.begin()), 0);
            SendMessageW(ConsolidatedSettingsControlHandle(*state, ConsolidatedSettingsControl::ResourceProfile), CB_SETCURSEL,
                         static_cast<int>(state->resourceProfile), 0);
            SetWindowTextW(ConsolidatedSettingsControlHandle(*state, ConsolidatedSettingsControl::SlideshowDuration), std::to_wstring(state->slideshowIntervalMs).c_str());
            SetWindowTextW(ConsolidatedSettingsControlHandle(*state, ConsolidatedSettingsControl::TransitionDuration), std::to_wstring(state->slideshowTransitionDurationMs).c_str());
            SetWindowTextW(ConsolidatedSettingsControlHandle(*state, ConsolidatedSettingsControl::ThumbnailCache),
                           std::to_wstring(state->thumbnailCacheCapacityOverrideBytes / (1024ULL * 1024ULL)).c_str());
            SetWindowTextW(ConsolidatedSettingsControlHandle(*state, ConsolidatedSettingsControl::MetadataCache),
                           std::to_wstring(state->metadataCacheCapacityOverrideEntries).c_str());
            SendMessageW(ConsolidatedSettingsControlHandle(*state, ConsolidatedSettingsControl::SlideshowDurationSpin), UDM_SETBUDDY,
                         reinterpret_cast<WPARAM>(ConsolidatedSettingsControlHandle(*state, ConsolidatedSettingsControl::SlideshowDuration)), 0);
            SendMessageW(ConsolidatedSettingsControlHandle(*state, ConsolidatedSettingsControl::SlideshowDurationSpin), UDM_SETRANGE32,
                         kSlideshowMinimumDurationMs, kSlideshowMaximumDurationMs);
            SendMessageW(ConsolidatedSettingsControlHandle(*state, ConsolidatedSettingsControl::TransitionDurationSpin), UDM_SETBUDDY,
                         reinterpret_cast<WPARAM>(ConsolidatedSettingsControlHandle(*state, ConsolidatedSettingsControl::TransitionDuration)), 0);
            SendMessageW(ConsolidatedSettingsControlHandle(*state, ConsolidatedSettingsControl::TransitionDurationSpin), UDM_SETRANGE32,
                         kSlideshowMinimumTransitionDurationMs, kSlideshowMaximumTransitionDurationMs);
            SetConsolidatedSettingsCheck(*state, ConsolidatedSettingsControl::TransitionEnabled, state->useSlideshowTransition);
            SetConsolidatedSettingsCheck(*state, ConsolidatedSettingsControl::ViewerWheelZoom, state->viewerMouseWheelBehavior == hyperbrowse::viewer::MouseWheelBehavior::Zoom);
            SetConsolidatedSettingsCheck(*state, ConsolidatedSettingsControl::ViewerWheelNavigate, state->viewerMouseWheelBehavior == hyperbrowse::viewer::MouseWheelBehavior::Navigate);
                SetConsolidatedSettingsCheck(*state, ConsolidatedSettingsControl::InvertKeyboardPanning, state->invertKeyboardPanning);
            SetConsolidatedSettingsCheck(*state, ConsolidatedSettingsControl::RawPairingEnabled, state->rawJpegPairedOperationsEnabled);
            SetConsolidatedSettingsCheck(*state, ConsolidatedSettingsControl::RawPreferJpeg, state->pairedRawJpegViewerPreference == hyperbrowse::browser::RawJpegDisplayPreference::Jpeg);
            SetConsolidatedSettingsCheck(*state, ConsolidatedSettingsControl::RawPreferRaw, state->pairedRawJpegViewerPreference == hyperbrowse::browser::RawJpegDisplayPreference::Raw);
            SetConsolidatedSettingsCheck(*state, ConsolidatedSettingsControl::SecondaryMonitor, state->defaultViewerToSecondaryMonitor);
            SetConsolidatedSettingsCheck(*state, ConsolidatedSettingsControl::InfoOverlays, state->infoOverlaysVisible);
            SetConsolidatedSettingsCheck(*state, ConsolidatedSettingsControl::FullMetadata, state->fullMetadataVisible);
            SetConsolidatedSettingsCheck(*state, ConsolidatedSettingsControl::ThemeDark, state->darkTheme);
            SetConsolidatedSettingsCheck(*state, ConsolidatedSettingsControl::ThemeLight, !state->darkTheme);
            SetConsolidatedSettingsCheck(*state, ConsolidatedSettingsControl::ThumbnailDetails, state->thumbnailDetailsVisible);
            SetConsolidatedSettingsCheck(*state, ConsolidatedSettingsControl::CompactLayout, state->compactThumbnailLayout);
            SetConsolidatedSettingsCheck(*state, ConsolidatedSettingsControl::DetailsPanel, state->detailsStripVisible);
            SetConsolidatedSettingsCheck(*state, ConsolidatedSettingsControl::PersistentCache, state->persistentThumbnailCacheEnabled);
            SetConsolidatedSettingsCheck(*state, ConsolidatedSettingsControl::ThumbnailCacheAutomatic, state->thumbnailCacheCapacityOverrideBytes == 0);
            SetConsolidatedSettingsCheck(*state, ConsolidatedSettingsControl::MetadataCacheAutomatic, state->metadataCacheCapacityOverrideEntries == 0);
            SetConsolidatedSettingsCheck(*state, ConsolidatedSettingsControl::PressureStatus, state->showPressureStateInStatusBar);
            SetConsolidatedSettingsCheck(*state, ConsolidatedSettingsControl::NvJpeg, state->nvJpegEnabled);
            SetConsolidatedSettingsCheck(*state, ConsolidatedSettingsControl::LibRawOutOfProcess, state->libRawOutOfProcessEnabled);
            SetConsolidatedSettingsCheck(*state, ConsolidatedSettingsControl::RecursiveBrowsing, state->recursiveBrowsingEnabled);
            SetConsolidatedSettingsCheck(*state, ConsolidatedSettingsControl::ShowSubfolders, state->showSubfoldersInBrowser);
            SetConsolidatedSettingsCheck(*state, ConsolidatedSettingsControl::CloseOnEscape, state->closeMainWindowOnEscape);
            SetConsolidatedSettingsCheck(*state, ConsolidatedSettingsControl::SingleInstance, state->singleInstanceEnabled);
            UpdateConsolidatedSettingsDependencies(*state);
            UpdateConsolidatedSettingsCacheValues(*state);
            EnableWindow(ConsolidatedSettingsControlHandle(*state, ConsolidatedSettingsControl::SecondaryMonitor), state->secondaryMonitorAvailable);
            EnableWindow(ConsolidatedSettingsControlHandle(*state, ConsolidatedSettingsControl::NvJpeg), state->nvJpegAvailable);
            EnableWindow(ConsolidatedSettingsControlHandle(*state, ConsolidatedSettingsControl::LibRawOutOfProcess), state->libRawAvailable);
            ShowConsolidatedSettingsPage(*state, ConsolidatedSettingsPage::Slideshow);

            const int cancelButtonLeft = kConsolidatedSettingsDialogWidth
                - kConsolidatedSettingsMargin
                - kConsolidatedSettingsButtonWidth;
            const int okButtonLeft = cancelButtonLeft
                - kConsolidatedSettingsButtonGap
                - kConsolidatedSettingsButtonWidth;
            const int applyButtonLeft = okButtonLeft
                - kConsolidatedSettingsButtonGap
                - kConsolidatedSettingsButtonWidth;
            CreateConsolidatedSettingsControl(*state, ConsolidatedSettingsPage::Count, L"BUTTON", L"Apply",
                                              BS_DEFPUSHBUTTON | WS_TABSTOP, applyButtonLeft, buttonTop,
                                              kConsolidatedSettingsButtonWidth, kConsolidatedSettingsButtonHeight, 5500);
            CreateConsolidatedSettingsControl(*state, ConsolidatedSettingsPage::Count, L"BUTTON", L"OK",
                                              BS_DEFPUSHBUTTON | WS_TABSTOP, okButtonLeft, buttonTop,
                                              kConsolidatedSettingsButtonWidth, kConsolidatedSettingsButtonHeight, IDOK);
            CreateConsolidatedSettingsControl(*state, ConsolidatedSettingsPage::Count, L"BUTTON", L"Cancel",
                                              WS_TABSTOP, cancelButtonLeft, buttonTop,
                                              kConsolidatedSettingsButtonWidth, kConsolidatedSettingsButtonHeight, IDCANCEL);
            const HWND applyButton = GetDlgItem(hwnd, 5500);
            const HWND okButton = GetDlgItem(hwnd, IDOK);
            const HWND cancelButton = GetDlgItem(hwnd, IDCANCEL);
            for (HWND button : {applyButton, okButton, cancelButton})
            {
                if (button)
                {
                    SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
                }
            }
            CenterWindowOnOwner(hwnd, state->ownerWindow);
            return 0;
        }
        case WM_CTLCOLORDLG:
            return reinterpret_cast<INT_PTR>(GetSysColorBrush(COLOR_WINDOW));
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN:
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX:
        {
            const HDC dc = reinterpret_cast<HDC>(wParam);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
            SetBkColor(dc, GetSysColor(COLOR_WINDOW));
            return reinterpret_cast<INT_PTR>(GetSysColorBrush(COLOR_WINDOW));
        }
        case WM_ERASEBKGND:
        {
            RECT client{};
            GetClientRect(hwnd, &client);
            FillRect(reinterpret_cast<HDC>(wParam), &client, GetSysColorBrush(COLOR_WINDOW));
            return 1;
        }
        case WM_NOTIFY:
            if (state)
            {
                const auto* notify = reinterpret_cast<const NMHDR*>(lParam);
                if (notify && notify->hwndFrom == state->tabWindow && notify->code == NM_CUSTOMDRAW)
                {
                    auto* customDraw = reinterpret_cast<NMCUSTOMDRAW*>(lParam);
                    if (customDraw->dwDrawStage == CDDS_PREPAINT)
                    {
                        return CDRF_NOTIFYITEMDRAW | CDRF_NOTIFYPOSTPAINT;
                    }
                    if (customDraw->dwDrawStage == CDDS_ITEMPREPAINT)
                    {
                        return CDRF_NEWFONT;
                    }
                    if (customDraw->dwDrawStage == CDDS_POSTPAINT)
                    {
                        RECT pageRect{};
                        GetClientRect(state->tabWindow, &pageRect);
                        TabCtrl_AdjustRect(state->tabWindow, FALSE, &pageRect);
                        FillRect(customDraw->hdc, &pageRect, GetSysColorBrush(COLOR_WINDOW));
                    }
                    return CDRF_DODEFAULT;
                }
                if (notify && notify->code == UDN_DELTAPOS)
                {
                    const auto* upDown = reinterpret_cast<const NMUPDOWN*>(lParam);
                    if (notify->idFrom == ConsolidatedSettingsControlId(ConsolidatedSettingsControl::SlideshowDurationSpin))
                    {
                        const UINT nextValue = ComputeNextSpinValue(
                            ConsolidatedSettingsControlHandle(*state, ConsolidatedSettingsControl::SlideshowDuration),
                            upDown->iPos,
                            upDown->iDelta,
                            kSlideshowMinimumDurationMs,
                            kSlideshowMaximumDurationMs);
                        SetDialogUIntEditAndSpin(
                            ConsolidatedSettingsControlHandle(*state, ConsolidatedSettingsControl::SlideshowDuration),
                            ConsolidatedSettingsControlHandle(*state, ConsolidatedSettingsControl::SlideshowDurationSpin),
                            nextValue);
                        return TRUE;
                    }
                    if (notify->idFrom == ConsolidatedSettingsControlId(ConsolidatedSettingsControl::TransitionDurationSpin))
                    {
                        const UINT nextValue = ComputeNextSpinValue(
                            ConsolidatedSettingsControlHandle(*state, ConsolidatedSettingsControl::TransitionDuration),
                            upDown->iPos,
                            upDown->iDelta,
                            kSlideshowMinimumTransitionDurationMs,
                            kSlideshowMaximumTransitionDurationMs);
                        SetDialogUIntEditAndSpin(
                            ConsolidatedSettingsControlHandle(*state, ConsolidatedSettingsControl::TransitionDuration),
                            ConsolidatedSettingsControlHandle(*state, ConsolidatedSettingsControl::TransitionDurationSpin),
                            nextValue);
                        return TRUE;
                    }
                }
                if (notify && notify->idFrom == kConsolidatedSettingsTabControlId && notify->code == TCN_SELCHANGE)
                {
                    const int selectedIndex = TabCtrl_GetCurSel(state->tabWindow);
                    if (selectedIndex >= 0 && selectedIndex < static_cast<int>(ConsolidatedSettingsPage::Count))
                    {
                        ShowConsolidatedSettingsPage(*state, static_cast<ConsolidatedSettingsPage>(selectedIndex));
                    }
                    return 0;
                }
            }
            break;
        case WM_COMMAND:
            if (!state)
            {
                break;
            }
            if (LOWORD(wParam) == 5500 || LOWORD(wParam) == IDOK)
            {
                if (CollectConsolidatedSettings(hwnd, state))
                {
                    if (state->apply)
                    {
                        state->apply(*state);
                    }
                    if (LOWORD(wParam) == IDOK)
                    {
                        state->accepted = true;
                        DestroyWindow(hwnd);
                    }
                }
                return 0;
            }
            if (LOWORD(wParam) == IDCANCEL)
            {
                DestroyWindow(hwnd);
                return 0;
            }
            if (HIWORD(wParam) == BN_CLICKED
                && (LOWORD(wParam) == ConsolidatedSettingsControlId(ConsolidatedSettingsControl::RawPairingEnabled)
                    || LOWORD(wParam) == ConsolidatedSettingsControlId(ConsolidatedSettingsControl::ThumbnailCacheAutomatic)
                    || LOWORD(wParam) == ConsolidatedSettingsControlId(ConsolidatedSettingsControl::MetadataCacheAutomatic)))
            {
                UpdateConsolidatedSettingsDependencies(*state);
                const auto changedControl = LOWORD(wParam) == ConsolidatedSettingsControlId(ConsolidatedSettingsControl::ThumbnailCacheAutomatic)
                    ? ConsolidatedSettingsControl::ThumbnailCacheAutomatic
                    : LOWORD(wParam) == ConsolidatedSettingsControlId(ConsolidatedSettingsControl::MetadataCacheAutomatic)
                        ? ConsolidatedSettingsControl::MetadataCacheAutomatic
                        : ConsolidatedSettingsControl::Count;
                UpdateConsolidatedSettingsCacheValues(*state, changedControl);
            }
            if (HIWORD(wParam) == CBN_SELCHANGE
                && LOWORD(wParam) == ConsolidatedSettingsControlId(ConsolidatedSettingsControl::ResourceProfile))
            {
                UpdateConsolidatedSettingsCacheValues(*state);
            }
            break;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            if (state)
            {
                state->done = true;
            }
            return 0;
        default:
            break;
        }
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    bool PromptForConsolidatedSettings(HWND ownerWindow,
                                       HINSTANCE instance,
                                       ConsolidatedSettingsDialogState* state)
    {
        if (!state)
        {
            return false;
        }
        WNDCLASSEXW windowClass{};
        if (GetClassInfoExW(instance, kConsolidatedSettingsDialogClassName, &windowClass) == FALSE)
        {
            windowClass.cbSize = sizeof(windowClass);
            windowClass.lpfnWndProc = &ConsolidatedSettingsDialogProc;
            windowClass.hInstance = instance;
            windowClass.lpszClassName = kConsolidatedSettingsDialogClassName;
            windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
            if (RegisterClassExW(&windowClass) == 0)
            {
                return false;
            }
        }

        RECT windowRect{0, 0, kConsolidatedSettingsDialogWidth, kConsolidatedSettingsDialogHeight};
        AdjustWindowRectEx(&windowRect, WS_CAPTION | WS_SYSMENU | WS_POPUP, FALSE, WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT);
        if (ownerWindow)
        {
            EnableWindow(ownerWindow, FALSE);
        }
        HWND dialogWindow = CreateWindowExW(
            WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT,
            kConsolidatedSettingsDialogClassName,
            state->title.c_str(),
            WS_CAPTION | WS_SYSMENU | WS_POPUP | WS_CLIPCHILDREN,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            windowRect.right - windowRect.left,
            windowRect.bottom - windowRect.top,
            ownerWindow,
            nullptr,
            instance,
            state);
        if (!dialogWindow)
        {
            if (ownerWindow)
            {
                EnableWindow(ownerWindow, TRUE);
            }
            DeleteFontIfOwned(state->bodyFont);
            state->bodyFont = nullptr;
            return false;
        }

        SetWindowTextW(dialogWindow, state->title.c_str());

        ShowWindow(dialogWindow, SW_SHOWNORMAL);
        UpdateWindow(dialogWindow);
        MSG message{};
        while (!state->done && GetMessageW(&message, nullptr, 0, 0) > 0)
        {
            if (!IsDialogMessageW(dialogWindow, &message))
            {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
        if (ownerWindow)
        {
            EnableWindow(ownerWindow, TRUE);
            SetForegroundWindow(ownerWindow);
            SetActiveWindow(ownerWindow);
        }
        DeleteFontIfOwned(state->bodyFont);
        state->bodyFont = nullptr;
        return state->accepted;
    }

    bool PromptForPerformanceSettings(HWND ownerWindow,
                                      HINSTANCE instance,
                                      hyperbrowse::util::AppTextSize appTextSize,
                                      hyperbrowse::util::ResourceProfile resourceProfile,
                                      std::size_t currentThumbnailCacheCapacityBytes,
                                      std::size_t currentMetadataCacheCapacityEntries,
                                      std::size_t initialThumbnailCacheCapacityOverrideBytes,
                                      std::size_t initialMetadataCacheCapacityOverrideEntries,
                                      bool initialShowPressureStateInStatusBar,
                                      std::size_t* thumbnailCacheCapacityOverrideBytes,
                                      std::size_t* metadataCacheCapacityOverrideEntries,
                                      bool* showPressureStateInStatusBar)
    {
        if (!thumbnailCacheCapacityOverrideBytes || !metadataCacheCapacityOverrideEntries || !showPressureStateInStatusBar)
        {
            return false;
        }

        WNDCLASSEXW windowClass{};
        if (GetClassInfoExW(instance, kPerformanceSettingsDialogClassName, &windowClass) == FALSE)
        {
            windowClass.cbSize = sizeof(windowClass);
            windowClass.lpfnWndProc = &PerformanceSettingsDialogProc;
            windowClass.hInstance = instance;
            windowClass.lpszClassName = kPerformanceSettingsDialogClassName;
            windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
            if (RegisterClassExW(&windowClass) == 0)
            {
                return false;
            }
        }

        PerformanceSettingsDialogState state;
        state.ownerWindow = ownerWindow;
        state.appTextSize = hyperbrowse::util::NormalizeAppTextSize(static_cast<std::uint32_t>(appTextSize));
        state.titleFont = CreateDialogUiFont(16, FW_BOLD, state.appTextSize);
        state.bodyFont = CreateDialogUiFont(9, FW_NORMAL, state.appTextSize);
        state.title = L"Performance Settings";
        state.instruction = L"Choose whether HyperBrowse should follow adaptive cache sizing or use explicit cache caps for the active profile.";
        state.summary = L"Profile: ";
        state.summary.append(hyperbrowse::util::ResourceProfileToDisplayName(resourceProfile));
        state.summary.append(L"\r\nEffective thumbnail cache: ");
        state.summary.append(FormatMegabytesFromBytes(currentThumbnailCacheCapacityBytes));
        state.summary.append(L" MB\r\nEffective metadata cache: ");
        state.summary.append(std::to_wstring(currentMetadataCacheCapacityEntries));
        state.summary.append(L" entries");
        state.footnote = L"Changes apply immediately to browser and file-details services. Thumbnail values are entered in megabytes; metadata values are entry counts.";
        state.thumbnailCacheAutomatic = initialThumbnailCacheCapacityOverrideBytes == 0;
        state.metadataCacheAutomatic = initialMetadataCacheCapacityOverrideEntries == 0;
        state.showPressureStateInStatusBar = initialShowPressureStateInStatusBar;
        state.thumbnailCacheText = state.thumbnailCacheAutomatic
            ? FormatMegabytesFromBytes(currentThumbnailCacheCapacityBytes)
            : FormatMegabytesFromBytes(initialThumbnailCacheCapacityOverrideBytes);
        state.metadataCacheText = state.metadataCacheAutomatic
            ? std::to_wstring(currentMetadataCacheCapacityEntries)
            : std::to_wstring(initialMetadataCacheCapacityOverrideEntries);

        const PerformanceSettingsDialogLayoutMetrics initialLayoutMetrics =
            BuildPerformanceSettingsDialogLayoutMetrics(kPerformanceSettingsDialogWidth, state);
        const int dialogWidth = std::max(kPerformanceSettingsDialogWidth, initialLayoutMetrics.minimumClientWidth);
        const PerformanceSettingsDialogLayoutMetrics layoutMetrics =
            BuildPerformanceSettingsDialogLayoutMetrics(dialogWidth, state);
        RECT windowRect{0, 0, dialogWidth, std::max(kPerformanceSettingsDialogHeight, layoutMetrics.minimumClientHeight)};
        AdjustWindowRectEx(&windowRect,
                           WS_CAPTION | WS_SYSMENU | WS_POPUP,
                           FALSE,
                           WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT);

        if (ownerWindow)
        {
            EnableWindow(ownerWindow, FALSE);
        }

        HWND dialogWindow = CreateWindowExW(
            WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT,
            kPerformanceSettingsDialogClassName,
            state.title.c_str(),
            WS_CAPTION | WS_SYSMENU | WS_POPUP | WS_CLIPCHILDREN,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            windowRect.right - windowRect.left,
            windowRect.bottom - windowRect.top,
            ownerWindow,
            nullptr,
            instance,
            &state);

        if (!dialogWindow)
        {
            if (ownerWindow)
            {
                EnableWindow(ownerWindow, TRUE);
            }
            DeleteFontIfOwned(state.titleFont);
            DeleteFontIfOwned(state.bodyFont);
            return false;
        }

        SetWindowTextW(dialogWindow, state.title.c_str());

        ShowWindow(dialogWindow, SW_SHOWNORMAL);
        UpdateWindow(dialogWindow);

        MSG message{};
        while (!state.done && GetMessageW(&message, nullptr, 0, 0) > 0)
        {
            if (!IsDialogMessageW(dialogWindow, &message))
            {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }

        if (ownerWindow)
        {
            EnableWindow(ownerWindow, TRUE);
            SetForegroundWindow(ownerWindow);
            SetActiveWindow(ownerWindow);
        }

        DeleteFontIfOwned(state.titleFont);
        DeleteFontIfOwned(state.bodyFont);

        if (!state.accepted)
        {
            return false;
        }

        *thumbnailCacheCapacityOverrideBytes = state.thumbnailCacheCapacityOverrideBytes;
        *metadataCacheCapacityOverrideEntries = state.metadataCacheCapacityOverrideEntries;
        *showPressureStateInStatusBar = state.showPressureStateInStatusBar;
        return true;
    }

    FileAssociationsDialogLayoutMetrics BuildFileAssociationsDialogLayoutMetrics(
        int clientWidth,
        const FileAssociationsDialogState& state,
        std::size_t formatCount)
    {
        FileAssociationsDialogLayoutMetrics metrics;
        metrics.margin = hyperbrowse::util::ScaleAppTextDimension(kFileAssociationsDialogMargin, state.appTextSize);
        metrics.contentWidth = std::max(0, clientWidth - (metrics.margin * 2));
        const int lineHeight = MeasureSingleLineTextHeight(state.bodyFont, 20);
        const int scaledGap = hyperbrowse::util::ScaleAppTextDimension(8, state.appTextSize);
        const int formatRowCount = std::max(1, static_cast<int>((formatCount + 1) / 2));

        metrics.instructionTop = hyperbrowse::util::ScaleAppTextDimension(16, state.appTextSize);
        metrics.instructionHeight = MeasureTextBlockHeight(state.bodyFont,
                                                           state.instruction,
                                                           metrics.contentWidth,
                                                           DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK,
                                                           lineHeight + scaledGap / 2);
        metrics.actionGap = scaledGap;
        metrics.actionTop = metrics.instructionTop + metrics.instructionHeight + metrics.actionGap;
        metrics.selectAllWidth = MeasureDialogButtonWidth(
            state.bodyFont,
            L"Select all",
            hyperbrowse::util::ScaleAppTextDimension(92, state.appTextSize));
        metrics.clearAllWidth = MeasureDialogButtonWidth(
            state.bodyFont,
            L"Clear all",
            hyperbrowse::util::ScaleAppTextDimension(92, state.appTextSize));
        metrics.defaultAppsButtonWidth = MeasureDialogButtonWidth(
            state.bodyFont,
            L"Default Apps...",
            hyperbrowse::util::ScaleAppTextDimension(kFileAssociationsDialogDefaultAppsButtonWidth, state.appTextSize));
        metrics.buttonHeight = std::max(
            hyperbrowse::util::ScaleAppTextDimension(kFileAssociationsDialogButtonHeight, state.appTextSize),
            lineHeight + hyperbrowse::util::ScaleAppTextDimension(8, state.appTextSize));
        metrics.defaultAppsButtonHeight = std::max(
            hyperbrowse::util::ScaleAppTextDimension(kFileAssociationsDialogDefaultAppsButtonHeight, state.appTextSize),
            lineHeight + hyperbrowse::util::ScaleAppTextDimension(8, state.appTextSize));
        metrics.formatGroupTop = metrics.actionTop + metrics.buttonHeight + metrics.actionGap;
        metrics.formatRowHeight = std::max(
            hyperbrowse::util::ScaleAppTextDimension(kFileAssociationsDialogFormatRowHeight, state.appTextSize),
            lineHeight + hyperbrowse::util::ScaleAppTextDimension(6, state.appTextSize));
        metrics.formatGroupContentTop = hyperbrowse::util::ScaleAppTextDimension(26, state.appTextSize);
        const int formatGroupBottomInset = hyperbrowse::util::ScaleAppTextDimension(10, state.appTextSize);
        metrics.formatGroupHeight = formatGroupBottomInset
            + metrics.formatGroupContentTop
            + formatRowCount * metrics.formatRowHeight;
        metrics.footnoteTop = metrics.formatGroupTop + metrics.formatGroupHeight + metrics.actionGap;
        metrics.footnoteHeight = MeasureTextBlockHeight(state.bodyFont,
                                                        state.footnote,
                                                        metrics.contentWidth,
                                                        DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK,
                                                        lineHeight);
        metrics.dividerTop = metrics.footnoteTop + metrics.footnoteHeight + metrics.actionGap;
        metrics.buttonRowHeight = std::max(metrics.buttonHeight, metrics.defaultAppsButtonHeight);
        metrics.buttonTop = metrics.dividerTop + metrics.actionGap;
        metrics.minimumClientHeight = metrics.buttonTop
            + metrics.buttonRowHeight
            + metrics.margin;
        return metrics;
    }

    void LayoutFileAssociationsDialogControls(HWND hwnd, const FileAssociationsDialogState& state)
    {
        RECT clientRect{};
        GetClientRect(hwnd, &clientRect);
        const int clientWidth = clientRect.right - clientRect.left;
        const int clientHeight = clientRect.bottom - clientRect.top;
        const FileAssociationsDialogLayoutMetrics metrics = BuildFileAssociationsDialogLayoutMetrics(
            clientWidth,
            state,
            state.formatCheckWindows.size());
        const int contentWidth = metrics.contentWidth;
        const int buttonRowTop = std::max(metrics.buttonTop, clientHeight - metrics.margin - metrics.buttonRowHeight);
        const int dividerTop = std::max(metrics.dividerTop, buttonRowTop - metrics.actionGap);
        const int buttonTop = buttonRowTop + (metrics.buttonRowHeight - metrics.buttonHeight) / 2;
        const int cancelLeft = clientWidth - metrics.margin - kFileAssociationsDialogButtonWidth;
        const int okLeft = cancelLeft - metrics.actionGap - kFileAssociationsDialogButtonWidth;
        const int defaultAppsTop = buttonRowTop + (metrics.buttonRowHeight - metrics.defaultAppsButtonHeight) / 2;

        const HWND instructionWindow = GetDlgItem(hwnd, kFileAssociationsDialogInstructionControlId);
        if (instructionWindow)
        {
            MoveWindow(instructionWindow, metrics.margin, metrics.instructionTop, contentWidth, metrics.instructionHeight, TRUE);
        }

        const HWND selectAllButton = GetDlgItem(hwnd, kFileAssociationsDialogSelectAllControlId);
        if (selectAllButton)
        {
            MoveWindow(selectAllButton,
                       metrics.margin,
                       metrics.actionTop,
                       metrics.selectAllWidth,
                       metrics.buttonHeight,
                       TRUE);
        }

        const HWND clearAllButton = GetDlgItem(hwnd, kFileAssociationsDialogClearAllControlId);
        if (clearAllButton)
        {
            MoveWindow(clearAllButton,
                       metrics.margin + metrics.selectAllWidth + metrics.actionGap,
                       metrics.actionTop,
                       metrics.clearAllWidth,
                       metrics.buttonHeight,
                       TRUE);
        }

        const HWND formatGroupWindow = GetDlgItem(hwnd, kFileAssociationsDialogFormatGroupControlId);
        if (formatGroupWindow)
        {
            MoveWindow(formatGroupWindow,
                       metrics.margin,
                       metrics.formatGroupTop,
                       contentWidth,
                       metrics.formatGroupHeight,
                       TRUE);
        }

        const int columnGap = hyperbrowse::util::ScaleAppTextDimension(28, state.appTextSize);
        const int columnWidth = std::max(0, (contentWidth - columnGap) / 2);
        const int formatLeft = metrics.margin + hyperbrowse::util::ScaleAppTextDimension(16, state.appTextSize);
        const int descriptionRightInset = hyperbrowse::util::ScaleAppTextDimension(16, state.appTextSize);
        const int formatColumnCount = std::max(1, static_cast<int>((state.formatCheckWindows.size() + 1) / 2));
        for (std::size_t index = 0; index < state.formatCheckWindows.size(); ++index)
        {
            const int column = index < static_cast<std::size_t>(formatColumnCount) ? 0 : 1;
            const int row = static_cast<int>(index % static_cast<std::size_t>(formatColumnCount));
            const int left = formatLeft + column * (columnWidth + columnGap);
            const HWND formatWindow = state.formatCheckWindows[index];
            if (formatWindow)
            {
                MoveWindow(formatWindow,
                           left,
                           metrics.formatGroupTop + metrics.formatGroupContentTop + row * metrics.formatRowHeight,
                           kFileAssociationsDialogFormatCheckboxWidth,
                           metrics.formatRowHeight,
                           TRUE);
            }
            if (index < state.formatDescriptionWindows.size())
            {
                const HWND descriptionWindow = state.formatDescriptionWindows[index];
                if (descriptionWindow)
                {
                    MoveWindow(descriptionWindow,
                               left + kFileAssociationsDialogFormatCheckboxWidth,
                               metrics.formatGroupTop + metrics.formatGroupContentTop + row * metrics.formatRowHeight,
                               std::max(0, columnWidth - kFileAssociationsDialogFormatCheckboxWidth - descriptionRightInset),
                               metrics.formatRowHeight,
                               TRUE);
                }
            }
        }

        const HWND footnoteWindow = GetDlgItem(hwnd, kFileAssociationsDialogFootnoteControlId);
        if (footnoteWindow)
        {
            MoveWindow(footnoteWindow,
                       metrics.margin,
                       metrics.footnoteTop,
                       contentWidth,
                       std::max(metrics.footnoteHeight, dividerTop - metrics.footnoteTop - metrics.actionGap),
                       TRUE);
        }

        const HWND dividerWindow = GetDlgItem(hwnd, kFileAssociationsDialogDividerControlId);
        if (dividerWindow)
        {
            MoveWindow(dividerWindow, metrics.margin, dividerTop, contentWidth, 2, TRUE);
        }

        if (state.okButton)
        {
            MoveWindow(state.okButton,
                       okLeft,
                       buttonTop,
                       kFileAssociationsDialogButtonWidth,
                       metrics.buttonHeight,
                       TRUE);
        }

        const HWND defaultAppsButton = GetDlgItem(hwnd, kFileAssociationsDialogDefaultAppsControlId);
        if (defaultAppsButton)
        {
            MoveWindow(defaultAppsButton,
                       metrics.margin,
                       defaultAppsTop,
                       metrics.defaultAppsButtonWidth,
                       metrics.defaultAppsButtonHeight,
                       TRUE);
        }

        const HWND cancelButton = GetDlgItem(hwnd, IDCANCEL);
        if (cancelButton)
        {
            MoveWindow(cancelButton,
                       cancelLeft,
                       buttonTop,
                       kFileAssociationsDialogButtonWidth,
                       metrics.buttonHeight,
                       TRUE);
        }
    }

    void SetFileAssociationChecks(const FileAssociationsDialogState& state, bool checked)
    {
        for (const HWND formatWindow : state.formatCheckWindows)
        {
            if (formatWindow)
            {
                SendMessageW(formatWindow, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
            }
        }
    }

    bool CollectFileAssociationDialogResult(HWND hwnd, FileAssociationsDialogState* state)
    {
        if (!hwnd || !state)
        {
            return false;
        }

        state->selectedDefaults.clear();
        state->selectedDefaults.reserve(state->formatCheckWindows.size());
        for (const HWND formatWindow : state->formatCheckWindows)
        {
            state->selectedDefaults.push_back(formatWindow
                && SendMessageW(formatWindow, BM_GETCHECK, 0, 0) == BST_CHECKED);
        }

        std::wstring errorMessage;
        bool defaultsRejected = false;
        if (!hyperbrowse::services::ApplyFileAssociationDefaults(state->selectedDefaults,
                                                                 &errorMessage,
                                                                 &defaultsRejected))
        {
            const std::wstring message = (errorMessage.empty()
                ? L"Windows could not apply the selected file associations."
                : errorMessage)
                + (defaultsRejected ? L"\n\nOpen Windows Default Apps now?" : L"");
            const UINT messageBoxType = defaultsRejected
                ? MB_YESNO | MB_ICONWARNING
                : MB_OK | MB_ICONERROR;
            const int result = MessageBoxW(hwnd, message.c_str(), state->title.c_str(), messageBoxType);
            if (defaultsRejected && result == IDYES && !LaunchDefaultAppsSettings(hwnd))
            {
                MessageBoxW(hwnd,
                            L"Windows Default Apps could not be opened.",
                            state->title.c_str(),
                            MB_OK | MB_ICONERROR);
            }
            if (state->firstFormatWindow)
            {
                SetFocus(state->firstFormatWindow);
            }
            return false;
        }

        return true;
    }

    LRESULT CALLBACK FileAssociationsDialogProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        auto* state = reinterpret_cast<FileAssociationsDialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

        switch (message)
        {
        case WM_NCCREATE:
        {
            const auto* createStruct = reinterpret_cast<const CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(createStruct->lpCreateParams));
            return TRUE;
        }
        case WM_CREATE:
        {
            state = reinterpret_cast<FileAssociationsDialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            if (!state)
            {
                return -1;
            }

            const HFONT font = state->bodyFont
                ? state->bodyFont
                : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            const HINSTANCE hInstance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd, GWLP_HINSTANCE));
            const std::span<const hyperbrowse::decode::SupportedFileType> supportedFileTypes =
                hyperbrowse::decode::SupportedFileTypes();

            const HWND instructionWindow = CreateWindowExW(
                0,
                L"STATIC",
                state->instruction.c_str(),
                WS_CHILD | WS_VISIBLE,
                0,
                0,
                100,
                44,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kFileAssociationsDialogInstructionControlId)),
                hInstance,
                nullptr);
            const HWND formatGroupWindow = CreateWindowExW(
                0,
                L"BUTTON",
                L"Supported formats",
                WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                0,
                0,
                100,
                100,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kFileAssociationsDialogFormatGroupControlId)),
                hInstance,
                nullptr);
            const HWND selectAllButton = CreateWindowExW(
                0,
                L"BUTTON",
                L"Select all",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                0,
                0,
                kFileAssociationsDialogButtonWidth,
                kFileAssociationsDialogButtonHeight,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kFileAssociationsDialogSelectAllControlId)),
                hInstance,
                nullptr);
            const HWND clearAllButton = CreateWindowExW(
                0,
                L"BUTTON",
                L"Clear all",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                0,
                0,
                kFileAssociationsDialogButtonWidth,
                kFileAssociationsDialogButtonHeight,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kFileAssociationsDialogClearAllControlId)),
                hInstance,
                nullptr);
            HWND defaultAppsButton = nullptr;
            if (IsWindows11OrGreater())
            {
                defaultAppsButton = CreateWindowExW(
                    0,
                    L"BUTTON",
                    L"Default &Apps...",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                    0,
                    0,
                    kFileAssociationsDialogDefaultAppsButtonWidth,
                    kFileAssociationsDialogDefaultAppsButtonHeight,
                    hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(kFileAssociationsDialogDefaultAppsControlId)),
                    hInstance,
                    nullptr);
            }

            state->formatCheckWindows.reserve(supportedFileTypes.size());
            for (std::size_t index = 0; index < supportedFileTypes.size(); ++index)
            {
                const auto& fileType = supportedFileTypes[index];
                std::wstring label = L".";
                label.append(fileType.extension);
                const HWND formatWindow = CreateWindowExW(
                    0,
                    L"BUTTON",
                    label.c_str(),
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                    0,
                    0,
                    100,
                    22,
                    hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(kFileAssociationsDialogFormatBaseControlId
                                                                  + static_cast<int>(index))),
                    hInstance,
                    nullptr);
                const HWND descriptionWindow = CreateWindowExW(
                    0,
                    L"STATIC",
                    fileType.description,
                    WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX | SS_ENDELLIPSIS,
                    0,
                    0,
                    100,
                    20,
                    hwnd,
                    nullptr,
                    hInstance,
                    nullptr);
                state->formatCheckWindows.push_back(formatWindow);
                state->formatDescriptionWindows.push_back(descriptionWindow);
                if (!state->firstFormatWindow)
                {
                    state->firstFormatWindow = formatWindow;
                }
                if (formatWindow && index < state->initialDefaults.size())
                {
                    SendMessageW(formatWindow,
                                 BM_SETCHECK,
                                 state->initialDefaults[index] ? BST_CHECKED : BST_UNCHECKED,
                                 0);
                }
            }

            const HWND footnoteWindow = CreateWindowExW(
                0,
                L"STATIC",
                state->footnote.c_str(),
                WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
                0,
                0,
                100,
                48,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kFileAssociationsDialogFootnoteControlId)),
                hInstance,
                nullptr);
            const HWND dividerWindow = CreateWindowExW(
                0,
                L"STATIC",
                nullptr,
                WS_CHILD | WS_VISIBLE | SS_ETCHEDHORZ,
                0,
                0,
                100,
                2,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kFileAssociationsDialogDividerControlId)),
                hInstance,
                nullptr);
            state->okButton = CreateWindowExW(
                0,
                L"BUTTON",
                L"Apply",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                0,
                0,
                kFileAssociationsDialogButtonWidth,
                kFileAssociationsDialogButtonHeight,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDOK)),
                hInstance,
                nullptr);
            const HWND cancelButton = CreateWindowExW(
                0,
                L"BUTTON",
                L"Cancel",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                0,
                0,
                kFileAssociationsDialogButtonWidth,
                kFileAssociationsDialogButtonHeight,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDCANCEL)),
                hInstance,
                nullptr);

            const HWND windows[] = {
                instructionWindow,
                formatGroupWindow,
                selectAllButton,
                clearAllButton,
                defaultAppsButton,
                footnoteWindow,
                dividerWindow,
                state->okButton,
                cancelButton,
            };
            for (HWND window : windows)
            {
                if (window)
                {
                    SendMessageW(window, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
                }
            }
            for (const HWND formatWindow : state->formatCheckWindows)
            {
                if (formatWindow)
                {
                    SendMessageW(formatWindow, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
                }
            }
            for (const HWND descriptionWindow : state->formatDescriptionWindows)
            {
                if (descriptionWindow)
                {
                    SendMessageW(descriptionWindow, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
                }
            }
            if (instructionWindow && state->bodyFont)
            {
                SendMessageW(instructionWindow, WM_SETFONT, reinterpret_cast<WPARAM>(state->bodyFont), TRUE);
            }
            if (footnoteWindow && state->bodyFont)
            {
                SendMessageW(footnoteWindow, WM_SETFONT, reinterpret_cast<WPARAM>(state->bodyFont), TRUE);
            }

            LayoutFileAssociationsDialogControls(hwnd, *state);
            CenterWindowOnOwner(hwnd, state->ownerWindow);
            return 0;
        }
        case WM_SIZE:
            if (state)
            {
                LayoutFileAssociationsDialogControls(hwnd, *state);
            }
            return 0;
        case WM_SHOWWINDOW:
            if (wParam != FALSE && state)
            {
                SetFocus(state->firstFormatWindow ? state->firstFormatWindow : state->okButton);
                return FALSE;
            }
            break;
        case WM_CTLCOLORDLG:
            return reinterpret_cast<INT_PTR>(GetSysColorBrush(COLOR_WINDOW));
        case WM_CTLCOLORSTATIC:
            SetBkMode(reinterpret_cast<HDC>(wParam), TRANSPARENT);
            SetTextColor(reinterpret_cast<HDC>(wParam), GetSysColor(COLOR_WINDOWTEXT));
            SetBkColor(reinterpret_cast<HDC>(wParam), GetSysColor(COLOR_WINDOW));
            return reinterpret_cast<INT_PTR>(GetSysColorBrush(COLOR_WINDOW));
        case WM_CTLCOLORBTN:
            SetBkMode(reinterpret_cast<HDC>(wParam), TRANSPARENT);
            SetTextColor(reinterpret_cast<HDC>(wParam), GetSysColor(COLOR_WINDOWTEXT));
            SetBkColor(reinterpret_cast<HDC>(wParam), GetSysColor(COLOR_WINDOW));
            return reinterpret_cast<INT_PTR>(GetSysColorBrush(COLOR_WINDOW));
        case WM_COMMAND:
            if (!state)
            {
                break;
            }

            if (LOWORD(wParam) == kFileAssociationsDialogSelectAllControlId)
            {
                SetFileAssociationChecks(*state, true);
                return 0;
            }

            if (LOWORD(wParam) == kFileAssociationsDialogClearAllControlId)
            {
                SetFileAssociationChecks(*state, false);
                return 0;
            }

            if (LOWORD(wParam) == kFileAssociationsDialogDefaultAppsControlId)
            {
                if (!LaunchDefaultAppsSettings(hwnd))
                {
                    MessageBoxW(hwnd,
                                L"Windows Default Apps could not be opened.",
                                state->title.c_str(),
                                MB_OK | MB_ICONERROR);
                }
                return 0;
            }

            if (LOWORD(wParam) == IDOK)
            {
                if (CollectFileAssociationDialogResult(hwnd, state))
                {
                    state->accepted = true;
                    DestroyWindow(hwnd);
                }
                return 0;
            }

            if (LOWORD(wParam) == IDCANCEL)
            {
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            if (state)
            {
                state->done = true;
            }
            return 0;
        default:
            break;
        }

        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    bool PromptForFileAssociations(HWND ownerWindow,
                                   HINSTANCE instance,
                                   hyperbrowse::util::AppTextSize appTextSize,
                                   const std::vector<bool>& initialDefaults,
                                   std::vector<bool>* selectedDefaults)
    {
        if (!selectedDefaults
            || initialDefaults.size() != hyperbrowse::decode::SupportedFileTypes().size())
        {
            return false;
        }

        WNDCLASSEXW windowClass{};
        if (GetClassInfoExW(instance, kFileAssociationsDialogClassName, &windowClass) == FALSE)
        {
            windowClass.cbSize = sizeof(windowClass);
            windowClass.lpfnWndProc = &FileAssociationsDialogProc;
            windowClass.hInstance = instance;
            windowClass.lpszClassName = kFileAssociationsDialogClassName;
            windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
            if (RegisterClassExW(&windowClass) == 0)
            {
                return false;
            }
        }

        FileAssociationsDialogState state;
        state.ownerWindow = ownerWindow;
        state.appTextSize = hyperbrowse::util::NormalizeAppTextSize(static_cast<std::uint32_t>(appTextSize));
        state.bodyFont = CreateDialogUiFont(10, FW_NORMAL, state.appTextSize);
        state.title = L"File Associations";
        state.instruction = L"Select the formats HyperBrowse should open by default.";
        state.footnote = L"Checked formats become defaults; unchecked formats are left unchanged. Windows may protect an existing choice; use Default apps if a format is rejected.";
        state.initialDefaults = initialDefaults;

        const FileAssociationsDialogLayoutMetrics layoutMetrics = BuildFileAssociationsDialogLayoutMetrics(
            kFileAssociationsDialogWidth,
            state,
            hyperbrowse::decode::SupportedFileTypes().size());
        RECT windowRect{0, 0,
                        kFileAssociationsDialogWidth,
                        std::max(kFileAssociationsDialogHeight, layoutMetrics.minimumClientHeight)};
        AdjustWindowRectEx(&windowRect,
                           WS_CAPTION | WS_SYSMENU | WS_POPUP,
                           FALSE,
                           WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT);

        if (ownerWindow)
        {
            EnableWindow(ownerWindow, FALSE);
        }

        HWND dialogWindow = CreateWindowExW(
            WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT,
            kFileAssociationsDialogClassName,
            state.title.c_str(),
            WS_CAPTION | WS_SYSMENU | WS_POPUP | WS_CLIPCHILDREN,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            windowRect.right - windowRect.left,
            windowRect.bottom - windowRect.top,
            ownerWindow,
            nullptr,
            instance,
            &state);

        if (!dialogWindow)
        {
            if (ownerWindow)
            {
                EnableWindow(ownerWindow, TRUE);
            }
            DeleteFontIfOwned(state.bodyFont);
            return false;
        }

        SetWindowTextW(dialogWindow, state.title.c_str());
        ShowWindow(dialogWindow, SW_SHOWNORMAL);
        UpdateWindow(dialogWindow);

        MSG message{};
        while (!state.done && GetMessageW(&message, nullptr, 0, 0) > 0)
        {
            if (!IsDialogMessageW(dialogWindow, &message))
            {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }

        if (ownerWindow)
        {
            EnableWindow(ownerWindow, TRUE);
            SetForegroundWindow(ownerWindow);
            SetActiveWindow(ownerWindow);
        }

        DeleteFontIfOwned(state.bodyFont);
        if (!state.accepted)
        {
            return false;
        }

        *selectedDefaults = std::move(state.selectedDefaults);
        return true;
    }

    bool CollectSlideshowSettingsDialogResult(HWND hwnd, SlideshowSettingsDialogState* state)
    {
        if (!hwnd || !state)
        {
            return false;
        }

        const int selectedIndex = state->transitionComboWindow
            ? static_cast<int>(SendMessageW(state->transitionComboWindow, CB_GETCURSEL, 0, 0))
            : -1;
        if (selectedIndex < 0 || selectedIndex >= static_cast<int>(kSlideshowTransitionOptions.size()))
        {
            MessageBoxW(hwnd,
                        L"Select a transition type.",
                        state->title.c_str(),
                        MB_OK | MB_ICONWARNING);
            if (state->transitionComboWindow)
            {
                SetFocus(state->transitionComboWindow);
            }
            return false;
        }

        UINT slideshowDurationMs = 0;
        if (!TryReadDialogUInt(state->durationEditWindow,
                               kSlideshowMinimumDurationMs,
                               kSlideshowMaximumDurationMs,
                               &slideshowDurationMs))
        {
            MessageBoxW(hwnd,
                        L"Slide duration must be between 250 and 60000 milliseconds.",
                        state->title.c_str(),
                        MB_OK | MB_ICONWARNING);
            if (state->durationEditWindow)
            {
                SetFocus(state->durationEditWindow);
            }
            return false;
        }

        UINT transitionDurationMs = 0;
        if (!TryReadDialogUInt(state->transitionDurationEditWindow,
                               kSlideshowMinimumTransitionDurationMs,
                               kSlideshowMaximumTransitionDurationMs,
                               &transitionDurationMs))
        {
            MessageBoxW(hwnd,
                        L"Transition duration must be between 100 and 5000 milliseconds.",
                        state->title.c_str(),
                        MB_OK | MB_ICONWARNING);
            if (state->transitionDurationEditWindow)
            {
                SetFocus(state->transitionDurationEditWindow);
            }
            return false;
        }

        state->transitionStyle = kSlideshowTransitionOptions[static_cast<std::size_t>(selectedIndex)].style;
        state->slideshowDurationMs = slideshowDurationMs;
        state->transitionDurationMs = transitionDurationMs;
        return true;
    }

    SlideshowSettingsDialogLayoutMetrics BuildSlideshowSettingsDialogLayoutMetrics(
        const SlideshowSettingsDialogState& state)
    {
        SlideshowSettingsDialogLayoutMetrics metrics;
        metrics.margin = hyperbrowse::util::ScaleAppTextDimension(kTextInputDialogMargin, state.appTextSize);
        metrics.contentWidth = kSlideshowSettingsDialogWidth - metrics.margin * 2;
        metrics.lineHeight = MeasureSingleLineTextHeight(state.bodyFont, 20);
        metrics.instructionHeight = MeasureTextBlockHeight(state.bodyFont,
                                                           state.instruction,
                                                           metrics.contentWidth,
                                                           DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK,
                                                           metrics.lineHeight + hyperbrowse::util::ScaleAppTextDimension(4, state.appTextSize));
        metrics.instructionHeight = std::max(
            metrics.instructionHeight,
            hyperbrowse::util::ScaleAppTextDimension(44, state.appTextSize));
        metrics.labelWidth = std::max(
            170,
            std::max(MeasureDialogButtonWidth(state.bodyFont, L"Transition type:", 0),
                     MeasureDialogButtonWidth(state.bodyFont, L"Transition duration:", 0)));
        metrics.valueWidth = 300;
        metrics.numericEditWidth = 120;
        metrics.spinWidth = 22;
        metrics.controlHeight = std::max(
            hyperbrowse::util::ScaleAppTextDimension(kTextInputEditHeight, state.appTextSize),
            metrics.lineHeight + hyperbrowse::util::ScaleAppTextDimension(8, state.appTextSize));
        metrics.rowGap = hyperbrowse::util::ScaleAppTextDimension(10, state.appTextSize);
        metrics.transitionTop = metrics.margin + std::max(
            hyperbrowse::util::ScaleAppTextDimension(56, state.appTextSize),
            metrics.instructionHeight + hyperbrowse::util::ScaleAppTextDimension(12, state.appTextSize));
        metrics.durationTop = metrics.transitionTop + metrics.controlHeight + metrics.rowGap;
        metrics.transitionDurationTop = metrics.durationTop + metrics.controlHeight + metrics.rowGap;
        metrics.footnoteTop = metrics.transitionDurationTop
            + metrics.controlHeight
            + hyperbrowse::util::ScaleAppTextDimension(20, state.appTextSize);
        metrics.footnoteHeight = MeasureTextBlockHeight(state.bodyFont,
                                                        state.footnote,
                                                        metrics.contentWidth,
                                                        DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK,
                                                        metrics.lineHeight);
        metrics.footnoteHeight = std::max(
            metrics.footnoteHeight,
            hyperbrowse::util::ScaleAppTextDimension(54, state.appTextSize));
        metrics.dividerTop = metrics.footnoteTop
            + metrics.footnoteHeight
            + hyperbrowse::util::ScaleAppTextDimension(56, state.appTextSize);
        metrics.buttonHeight = std::max(
            hyperbrowse::util::ScaleAppTextDimension(kTextInputButtonHeight, state.appTextSize),
            metrics.lineHeight + hyperbrowse::util::ScaleAppTextDimension(8, state.appTextSize));
        metrics.applyButtonWidth = MeasureDialogButtonWidth(
            state.bodyFont,
            L"Apply",
            hyperbrowse::util::ScaleAppTextDimension(kTextInputButtonWidth, state.appTextSize));
        metrics.cancelButtonWidth = MeasureDialogButtonWidth(
            state.bodyFont,
            L"Cancel",
            hyperbrowse::util::ScaleAppTextDimension(kTextInputButtonWidth, state.appTextSize));
        metrics.buttonTop = metrics.dividerTop + hyperbrowse::util::ScaleAppTextDimension(20, state.appTextSize);
        metrics.minimumClientHeight = metrics.buttonTop
            + metrics.buttonHeight
            + hyperbrowse::util::ScaleAppTextDimension(26, state.appTextSize);
        return metrics;
    }

    LRESULT CALLBACK SlideshowSettingsDialogProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        auto* state = reinterpret_cast<SlideshowSettingsDialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

        switch (message)
        {
        case WM_NCCREATE:
        {
            const auto* createStruct = reinterpret_cast<const CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(createStruct->lpCreateParams));
            return TRUE;
        }
        case WM_CREATE:
        {
            state = reinterpret_cast<SlideshowSettingsDialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            if (!state)
            {
                return -1;
            }

            const HFONT font = state->bodyFont
                ? state->bodyFont
                : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            const HINSTANCE hInstance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd, GWLP_HINSTANCE));
            const SlideshowSettingsDialogLayoutMetrics metrics = BuildSlideshowSettingsDialogLayoutMetrics(*state);
            const int contentLeft = metrics.margin;
            const int contentWidth = metrics.contentWidth;
            const int labelWidth = metrics.labelWidth;
            const int valueWidth = metrics.valueWidth;
            const int transitionTop = metrics.transitionTop;
            const int durationTop = metrics.durationTop;
            const int transitionDurationTop = metrics.transitionDurationTop;
            const int footnoteTop = metrics.footnoteTop;
            const int dividerTop = metrics.dividerTop;
            const int buttonTop = metrics.buttonTop;
            const int cancelLeft = kSlideshowSettingsDialogWidth - metrics.margin - metrics.cancelButtonWidth;
            const int okLeft = cancelLeft - metrics.rowGap - metrics.applyButtonWidth;
            const int numericEditWidth = metrics.numericEditWidth;
            const int spinWidth = metrics.spinWidth;
            const int labelTopOffset = (metrics.controlHeight - metrics.lineHeight) / 2;
            const int footnoteHeight = metrics.footnoteHeight;

            const HWND instructionWindow = CreateWindowExW(
                0,
                L"STATIC",
                state->instruction.c_str(),
                WS_CHILD | WS_VISIBLE,
                contentLeft,
                metrics.margin,
                contentWidth,
                metrics.instructionHeight,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSlideshowSettingsInstructionControlId)),
                hInstance,
                nullptr);
            const HWND transitionLabel = CreateWindowExW(
                0,
                L"STATIC",
                L"Transition type:",
                WS_CHILD | WS_VISIBLE,
                contentLeft,
                transitionTop + labelTopOffset,
                labelWidth,
                metrics.lineHeight,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSlideshowSettingsTransitionLabelControlId)),
                hInstance,
                nullptr);
            state->transitionComboWindow = CreateWindowExW(
                0,
                L"COMBOBOX",
                nullptr,
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
                contentLeft + labelWidth,
                transitionTop,
                valueWidth,
                140,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSlideshowSettingsTransitionComboControlId)),
                hInstance,
                nullptr);
            const HWND durationLabel = CreateWindowExW(
                0,
                L"STATIC",
                L"Slide duration:",
                WS_CHILD | WS_VISIBLE,
                contentLeft,
                durationTop + labelTopOffset,
                labelWidth,
                metrics.lineHeight,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSlideshowSettingsDurationLabelControlId)),
                hInstance,
                nullptr);
            state->durationEditWindow = CreateWindowExW(
                WS_EX_CLIENTEDGE,
                L"EDIT",
                std::to_wstring(state->slideshowDurationMs).c_str(),
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_NUMBER,
                contentLeft + labelWidth,
                durationTop,
                numericEditWidth,
                metrics.controlHeight,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSlideshowSettingsDurationEditControlId)),
                hInstance,
                nullptr);
            state->durationSpinWindow = CreateWindowExW(
                0,
                UPDOWN_CLASSW,
                nullptr,
                WS_CHILD | WS_VISIBLE | UDS_ALIGNRIGHT | UDS_ARROWKEYS,
                contentLeft + labelWidth + numericEditWidth,
                durationTop,
                spinWidth,
                metrics.controlHeight,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSlideshowSettingsDurationSpinControlId)),
                hInstance,
                nullptr);
            const HWND durationUnit = CreateWindowExW(
                0,
                L"STATIC",
                L"ms",
                WS_CHILD | WS_VISIBLE,
                contentLeft + labelWidth + numericEditWidth + spinWidth + 8,
                durationTop + labelTopOffset,
                36,
                metrics.lineHeight,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSlideshowSettingsDurationUnitControlId)),
                hInstance,
                nullptr);
            const HWND transitionDurationLabel = CreateWindowExW(
                0,
                L"STATIC",
                L"Transition duration:",
                WS_CHILD | WS_VISIBLE,
                contentLeft,
                transitionDurationTop + labelTopOffset,
                labelWidth,
                metrics.lineHeight,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSlideshowSettingsTransitionDurationLabelControlId)),
                hInstance,
                nullptr);
            state->transitionDurationEditWindow = CreateWindowExW(
                WS_EX_CLIENTEDGE,
                L"EDIT",
                std::to_wstring(state->transitionDurationMs).c_str(),
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_NUMBER,
                contentLeft + labelWidth,
                transitionDurationTop,
                numericEditWidth,
                metrics.controlHeight,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSlideshowSettingsTransitionDurationEditControlId)),
                hInstance,
                nullptr);
            state->transitionDurationSpinWindow = CreateWindowExW(
                0,
                UPDOWN_CLASSW,
                nullptr,
                WS_CHILD | WS_VISIBLE | UDS_ALIGNRIGHT | UDS_ARROWKEYS,
                contentLeft + labelWidth + numericEditWidth,
                transitionDurationTop,
                spinWidth,
                metrics.controlHeight,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSlideshowSettingsTransitionDurationSpinControlId)),
                hInstance,
                nullptr);
            const HWND transitionDurationUnit = CreateWindowExW(
                0,
                L"STATIC",
                L"ms",
                WS_CHILD | WS_VISIBLE,
                contentLeft + labelWidth + numericEditWidth + spinWidth + 8,
                transitionDurationTop + labelTopOffset,
                36,
                metrics.lineHeight,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSlideshowSettingsTransitionDurationUnitControlId)),
                hInstance,
                nullptr);
            const HWND footnoteWindow = CreateWindowExW(
                0,
                L"STATIC",
                state->footnote.c_str(),
                WS_CHILD | WS_VISIBLE,
                contentLeft,
                footnoteTop,
                contentWidth,
                footnoteHeight,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSlideshowSettingsFootnoteControlId)),
                hInstance,
                nullptr);
            const HWND dividerWindow = CreateWindowExW(
                0,
                L"STATIC",
                nullptr,
                WS_CHILD | WS_VISIBLE | SS_ETCHEDHORZ,
                contentLeft,
                dividerTop,
                contentWidth,
                2,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSlideshowSettingsDividerControlId)),
                hInstance,
                nullptr);
            state->okButton = CreateWindowExW(
                0,
                L"BUTTON",
                L"Apply",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                okLeft,
                buttonTop,
                metrics.applyButtonWidth,
                metrics.buttonHeight,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDOK)),
                hInstance,
                nullptr);
            const HWND cancelButton = CreateWindowExW(
                0,
                L"BUTTON",
                L"Cancel",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                cancelLeft,
                buttonTop,
                metrics.cancelButtonWidth,
                metrics.buttonHeight,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDCANCEL)),
                hInstance,
                nullptr);

            const HWND windows[] = {
                instructionWindow,
                transitionLabel,
                state->transitionComboWindow,
                durationLabel,
                state->durationEditWindow,
                state->durationSpinWindow,
                durationUnit,
                transitionDurationLabel,
                state->transitionDurationEditWindow,
                state->transitionDurationSpinWindow,
                transitionDurationUnit,
                footnoteWindow,
                dividerWindow,
                state->okButton,
                cancelButton,
            };
            for (HWND window : windows)
            {
                if (window)
                {
                    SendMessageW(window, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
                }
            }

            if (state->durationEditWindow)
            {
                SendMessageW(state->durationEditWindow, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(6, 6));
            }
            if (state->durationSpinWindow)
            {
                SendMessageW(state->durationSpinWindow,
                             UDM_SETRANGE32,
                             kSlideshowMinimumDurationMs,
                             kSlideshowMaximumDurationMs);
                SendMessageW(state->durationSpinWindow,
                             UDM_SETBUDDY,
                             reinterpret_cast<WPARAM>(state->durationEditWindow),
                             0);
                SetDialogUIntEditAndSpin(state->durationEditWindow,
                                         state->durationSpinWindow,
                                         state->slideshowDurationMs);
                const UDACCEL accelerations[] = {
                    {0, 500},
                    {3, 1000},
                    {8, 5000},
                };
                SendMessageW(state->durationSpinWindow,
                             UDM_SETACCEL,
                             static_cast<WPARAM>(std::size(accelerations)),
                             reinterpret_cast<LPARAM>(accelerations));
            }
            if (state->transitionDurationEditWindow)
            {
                SendMessageW(state->transitionDurationEditWindow, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(6, 6));
            }
            if (state->transitionDurationSpinWindow)
            {
                SendMessageW(state->transitionDurationSpinWindow,
                             UDM_SETRANGE32,
                             kSlideshowMinimumTransitionDurationMs,
                             kSlideshowMaximumTransitionDurationMs);
                SendMessageW(state->transitionDurationSpinWindow,
                             UDM_SETBUDDY,
                             reinterpret_cast<WPARAM>(state->transitionDurationEditWindow),
                             0);
                SetDialogUIntEditAndSpin(state->transitionDurationEditWindow,
                                         state->transitionDurationSpinWindow,
                                         state->transitionDurationMs);
                const UDACCEL accelerations[] = {
                    {0, 50},
                    {3, 100},
                    {8, 250},
                };
                SendMessageW(state->transitionDurationSpinWindow,
                             UDM_SETACCEL,
                             static_cast<WPARAM>(std::size(accelerations)),
                             reinterpret_cast<LPARAM>(accelerations));
            }

            if (state->transitionComboWindow)
            {
                for (const SlideshowTransitionOption& option : kSlideshowTransitionOptions)
                {
                    SendMessageW(state->transitionComboWindow, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(option.label));
                }
                SendMessageW(state->transitionComboWindow,
                             CB_SETCURSEL,
                             SlideshowTransitionComboIndex(state->transitionStyle),
                             0);
            }

            CenterWindowOnOwner(hwnd, state->ownerWindow);
            return 0;
        }
        case WM_SHOWWINDOW:
            if (wParam != FALSE && state)
            {
                SetFocus(state->transitionComboWindow ? state->transitionComboWindow : state->okButton);
                return FALSE;
            }
            break;
        case WM_CTLCOLORDLG:
            return reinterpret_cast<INT_PTR>(GetSysColorBrush(COLOR_WINDOW));
        case WM_CTLCOLORSTATIC:
            SetBkMode(reinterpret_cast<HDC>(wParam), TRANSPARENT);
            SetTextColor(reinterpret_cast<HDC>(wParam), GetSysColor(COLOR_WINDOWTEXT));
            SetBkColor(reinterpret_cast<HDC>(wParam), GetSysColor(COLOR_WINDOW));
            return reinterpret_cast<INT_PTR>(GetSysColorBrush(COLOR_WINDOW));
        case WM_NOTIFY:
            if (!state)
            {
                break;
            }

            if (const auto* notify = reinterpret_cast<const NMHDR*>(lParam);
                notify && notify->code == UDN_DELTAPOS)
            {
                const auto* upDown = reinterpret_cast<const NMUPDOWN*>(lParam);
                if (notify->idFrom == kSlideshowSettingsDurationSpinControlId)
                {
                    const UINT nextValue = ComputeNextSpinValue(state->durationEditWindow,
                                                                upDown->iPos,
                                                                upDown->iDelta,
                                                                kSlideshowMinimumDurationMs,
                                                                kSlideshowMaximumDurationMs);
                    SetDialogUIntEditAndSpin(state->durationEditWindow, state->durationSpinWindow, nextValue);
                    return TRUE;
                }

                if (notify->idFrom == kSlideshowSettingsTransitionDurationSpinControlId)
                {
                    const UINT nextValue = ComputeNextSpinValue(state->transitionDurationEditWindow,
                                                                upDown->iPos,
                                                                upDown->iDelta,
                                                                kSlideshowMinimumTransitionDurationMs,
                                                                kSlideshowMaximumTransitionDurationMs);
                    SetDialogUIntEditAndSpin(state->transitionDurationEditWindow,
                                             state->transitionDurationSpinWindow,
                                             nextValue);
                    return TRUE;
                }
            }
            break;
        case WM_COMMAND:
            if (!state)
            {
                break;
            }

            if (LOWORD(wParam) == IDOK)
            {
                if (CollectSlideshowSettingsDialogResult(hwnd, state))
                {
                    state->accepted = true;
                    DestroyWindow(hwnd);
                }
                return 0;
            }

            if (LOWORD(wParam) == IDCANCEL)
            {
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            if (state)
            {
                state->done = true;
            }
            return 0;
        default:
            break;
        }

        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    bool PromptForSlideshowSettings(HWND ownerWindow,
                                    HINSTANCE instance,
                                    hyperbrowse::util::AppTextSize appTextSize,
                                    UINT initialSlideshowDurationMs,
                                    hyperbrowse::viewer::TransitionStyle initialTransitionStyle,
                                    UINT initialTransitionDurationMs,
                                    UINT* slideshowDurationMs,
                                    hyperbrowse::viewer::TransitionStyle* transitionStyle,
                                    UINT* transitionDurationMs)
    {
        if (!slideshowDurationMs || !transitionStyle || !transitionDurationMs)
        {
            return false;
        }

        WNDCLASSEXW windowClass{};
        if (GetClassInfoExW(instance, kSlideshowSettingsDialogClassName, &windowClass) == FALSE)
        {
            windowClass.cbSize = sizeof(windowClass);
            windowClass.lpfnWndProc = &SlideshowSettingsDialogProc;
            windowClass.hInstance = instance;
            windowClass.lpszClassName = kSlideshowSettingsDialogClassName;
            windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
            if (RegisterClassExW(&windowClass) == 0)
            {
                return false;
            }
        }

        SlideshowSettingsDialogState state;
        state.ownerWindow = ownerWindow;
        state.appTextSize = hyperbrowse::util::NormalizeAppTextSize(static_cast<std::uint32_t>(appTextSize));
        state.bodyFont = CreateDialogUiFont(9, FW_NORMAL, state.appTextSize);
        if (!state.bodyFont)
        {
            state.bodyFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        }
        state.title = L"Slideshow Settings";
        state.instruction = L"Choose transition style and precise slideshow timing.";
        state.footnote = L"Random selects from the animated transition styles for each transition. None uses hard cuts.";
        state.slideshowDurationMs = std::clamp<UINT>(initialSlideshowDurationMs,
                                 kSlideshowMinimumDurationMs,
                                 kSlideshowMaximumDurationMs);
        state.transitionDurationMs = std::clamp<UINT>(initialTransitionDurationMs,
                                  kSlideshowMinimumTransitionDurationMs,
                                  kSlideshowMaximumTransitionDurationMs);
        state.transitionStyle = initialTransitionStyle;

        const SlideshowSettingsDialogLayoutMetrics layoutMetrics = BuildSlideshowSettingsDialogLayoutMetrics(state);
        state.dialogHeight = std::max(kSlideshowSettingsDialogHeight, layoutMetrics.minimumClientHeight);
        RECT windowRect{0, 0, kSlideshowSettingsDialogWidth, state.dialogHeight};
        AdjustWindowRectEx(&windowRect,
                           WS_CAPTION | WS_SYSMENU | WS_POPUP,
                           FALSE,
                           WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT);

        if (ownerWindow)
        {
            EnableWindow(ownerWindow, FALSE);
        }

        HWND dialogWindow = CreateWindowExW(
            WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT,
            kSlideshowSettingsDialogClassName,
            state.title.c_str(),
            WS_CAPTION | WS_SYSMENU | WS_POPUP | WS_CLIPCHILDREN,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            windowRect.right - windowRect.left,
            windowRect.bottom - windowRect.top,
            ownerWindow,
            nullptr,
            instance,
            &state);

        if (!dialogWindow)
        {
            if (ownerWindow)
            {
                EnableWindow(ownerWindow, TRUE);
            }
            DeleteFontIfOwned(state.bodyFont);
            return false;
        }

        SetWindowTextW(dialogWindow, state.title.c_str());

        ShowWindow(dialogWindow, SW_SHOWNORMAL);
        UpdateWindow(dialogWindow);

        MSG message{};
        while (!state.done && GetMessageW(&message, nullptr, 0, 0) > 0)
        {
            if (!IsDialogMessageW(dialogWindow, &message))
            {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }

        if (ownerWindow)
        {
            EnableWindow(ownerWindow, TRUE);
            SetForegroundWindow(ownerWindow);
            SetActiveWindow(ownerWindow);
        }

        DeleteFontIfOwned(state.bodyFont);

        if (!state.accepted)
        {
            return false;
        }

        *slideshowDurationMs = state.slideshowDurationMs;
        *transitionStyle = state.transitionStyle;
        *transitionDurationMs = state.transitionDurationMs;
        return true;
    }

    bool PromptForSingleLineText(HWND ownerWindow,
                                 HINSTANCE instance,
                                 hyperbrowse::util::AppTextSize appTextSize,
                                 const std::wstring& title,
                                 const std::wstring& instruction,
                                 const std::wstring& confirmLabel,
                                 const std::wstring& initialText,
                                 int selectionStart,
                                 int selectionEnd,
                                 std::wstring* resultText)
    {
        if (!resultText)
        {
            return false;
        }

        WNDCLASSEXW windowClass{};
        if (GetClassInfoExW(instance, kTextInputDialogClassName, &windowClass) == FALSE)
        {
            windowClass.cbSize = sizeof(windowClass);
            windowClass.lpfnWndProc = &TextInputDialogProc;
            windowClass.hInstance = instance;
            windowClass.lpszClassName = kTextInputDialogClassName;
            windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
            if (RegisterClassExW(&windowClass) == 0)
            {
                return false;
            }
        }

        TextInputDialogState state;
        state.ownerWindow = ownerWindow;
        state.appTextSize = hyperbrowse::util::NormalizeAppTextSize(static_cast<std::uint32_t>(appTextSize));
        state.bodyFont = CreateDialogUiFont(9, FW_NORMAL, state.appTextSize);
        if (!state.bodyFont)
        {
            state.bodyFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        }
        state.title = title;
        state.instruction = instruction;
        state.confirmLabel = confirmLabel;
        state.initialText = initialText;
        state.selectionStart = selectionStart;
        state.selectionEnd = selectionEnd;

        const TextInputDialogLayoutMetrics layoutMetrics = BuildTextInputDialogLayoutMetrics(state);
        RECT windowRect{0, 0, layoutMetrics.clientWidth, std::max(kTextInputDialogHeight, layoutMetrics.clientHeight)};
        AdjustWindowRectEx(&windowRect, WS_CAPTION | WS_SYSMENU | WS_POPUP, FALSE, WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT);

        if (ownerWindow)
        {
            EnableWindow(ownerWindow, FALSE);
        }

        HWND dialogWindow = CreateWindowExW(
            WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT,
            kTextInputDialogClassName,
            state.title.c_str(),
            WS_CAPTION | WS_SYSMENU | WS_POPUP | WS_CLIPCHILDREN,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            windowRect.right - windowRect.left,
            windowRect.bottom - windowRect.top,
            ownerWindow,
            nullptr,
            instance,
            &state);

        if (!dialogWindow)
        {
            if (ownerWindow)
            {
                EnableWindow(ownerWindow, TRUE);
            }
            DeleteFontIfOwned(state.bodyFont);
            return false;
        }

        SetWindowTextW(dialogWindow, state.title.c_str());

        ShowWindow(dialogWindow, SW_SHOWNORMAL);
        UpdateWindow(dialogWindow);

        MSG message{};
        while (!state.done && GetMessageW(&message, nullptr, 0, 0) > 0)
        {
            if (!IsDialogMessageW(dialogWindow, &message))
            {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }

        if (ownerWindow)
        {
            EnableWindow(ownerWindow, TRUE);
            SetForegroundWindow(ownerWindow);
            SetActiveWindow(ownerWindow);
        }

        DeleteFontIfOwned(state.bodyFont);

        if (!state.accepted)
        {
            return false;
        }

        *resultText = state.resultText;
        return true;
    }

    bool IsValidRenameLeafName(std::wstring_view leafName, std::wstring* errorMessage)
    {
        if (leafName.empty())
        {
            if (errorMessage) *errorMessage = L"The name cannot be empty.";
            return false;
        }

        if (leafName == L"." || leafName == L"..")
        {
            if (errorMessage) *errorMessage = L"The name is not valid.";
            return false;
        }

        if (std::any_of(leafName.begin(), leafName.end(), [](wchar_t character)
        {
            return character < 32 || wcschr(L"<>:\"/\\|?*", character) != nullptr;
        }))
        {
            if (errorMessage) *errorMessage = L"The name contains characters that Windows does not allow.";
            return false;
        }

        if (!leafName.empty() && (leafName.back() == L' ' || leafName.back() == L'.'))
        {
            if (errorMessage) *errorMessage = L"Names cannot end with a space or a period.";
            return false;
        }

        return true;
    }

    bool PromptForRenameLeafName(HWND ownerWindow,
                                 HINSTANCE instance,
                                 hyperbrowse::util::AppTextSize appTextSize,
                                 std::wstring title,
                                 std::wstring instruction,
                                 std::wstring currentLeafName,
                                 bool isFile,
                                 std::wstring* renamedLeafName)
    {
        if (!renamedLeafName)
        {
            return false;
        }

        std::wstring candidate = currentLeafName;
        const int selectionEnd = DefaultRenameSelectionEnd(currentLeafName, isFile);
        while (PromptForSingleLineText(ownerWindow,
                                       instance,
                                       appTextSize,
                                       title,
                                       instruction,
                                       L"Rename",
                                       candidate,
                                       0,
                                       selectionEnd,
                                       &candidate))
        {
            std::wstring errorMessage;
            if (!IsValidRenameLeafName(candidate, &errorMessage))
            {
                MessageBoxW(ownerWindow, errorMessage.c_str(), title.c_str(), MB_OK | MB_ICONWARNING);
                continue;
            }

            if (candidate == currentLeafName)
            {
                return false;
            }

            *renamedLeafName = candidate;
            return true;
        }

        return false;
    }

    bool FolderPathsEqual(std::wstring_view lhs, std::wstring_view rhs)
    {
        const std::wstring normalizedLeft = NormalizeFolderPath(std::wstring(lhs));
        const std::wstring normalizedRight = NormalizeFolderPath(std::wstring(rhs));
        return _wcsicmp(normalizedLeft.c_str(), normalizedRight.c_str()) == 0;
    }

    bool IsExistingDirectory(std::wstring_view folderPath)
    {
        if (folderPath.empty())
        {
            return false;
        }

        std::error_code error;
        return fs::is_directory(fs::path(folderPath), error) && !error;
    }

    bool IsTextInputControlWindow(HWND window)
    {
        if (!window)
        {
            return false;
        }

        wchar_t className[64] = {};
        if (GetClassNameW(window, className, static_cast<int>(std::size(className))) == 0)
        {
            return false;
        }

        return _wcsicmp(className, L"Edit") == 0
            || _wcsicmp(className, L"RichEdit20W") == 0
            || _wcsicmp(className, L"RichEdit50W") == 0
            || _wcsicmp(className, MSFTEDIT_CLASS) == 0;
    }

    bool AreFoldersOnSameDrive(std::wstring_view lhs, std::wstring_view rhs)
    {
        if (lhs.empty() || rhs.empty())
        {
            return false;
        }

        const std::wstring leftRoot = NormalizeFolderPath(fs::path(lhs).root_path().wstring());
        const std::wstring rightRoot = NormalizeFolderPath(fs::path(rhs).root_path().wstring());
        if (leftRoot.empty() || rightRoot.empty())
        {
            return false;
        }

        return FolderPathsEqual(leftRoot, rightRoot);
    }

    bool IsValidFolderTreeDropDestination(std::wstring_view sourcePath, std::wstring_view destinationPath)
    {
        if (sourcePath.empty() || destinationPath.empty())
        {
            return false;
        }

        if (!IsExistingDirectory(destinationPath)
            || !AreFoldersOnSameDrive(sourcePath, destinationPath))
        {
            return false;
        }

        if (FolderPathsEqual(sourcePath, destinationPath)
            || hyperbrowse::browser::PathHasPrefix(destinationPath, sourcePath))
        {
            return false;
        }

        const std::wstring sourceParentPath = NormalizeFolderPath(fs::path(sourcePath).parent_path().wstring());
        if (!sourceParentPath.empty() && FolderPathsEqual(sourceParentPath, destinationPath))
        {
            return false;
        }

        return true;
    }

    hyperbrowse::services::FileOperationType ResolveQuickAccessDropOperationType(
        const std::vector<std::wstring>& sourcePaths,
        std::wstring_view destinationFolder)
    {
        if ((GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0)
        {
            return hyperbrowse::services::FileOperationType::Copy;
        }

        if ((GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0)
        {
            return hyperbrowse::services::FileOperationType::Move;
        }

        if (destinationFolder.empty() || sourcePaths.empty())
        {
            return hyperbrowse::services::FileOperationType::Copy;
        }

        const bool allSourcesOnSameDrive = std::all_of(sourcePaths.begin(), sourcePaths.end(), [&](const std::wstring& sourcePath)
        {
            return !sourcePath.empty() && AreFoldersOnSameDrive(sourcePath, destinationFolder);
        });

        return allSourcesOnSameDrive
            ? hyperbrowse::services::FileOperationType::Move
            : hyperbrowse::services::FileOperationType::Copy;
    }

    bool AreAllSourcePathsOnSameDrive(const std::vector<std::wstring>& sourcePaths,
                                      std::wstring_view destinationFolder)
    {
        if (destinationFolder.empty() || sourcePaths.empty())
        {
            return false;
        }

        return std::all_of(sourcePaths.begin(), sourcePaths.end(), [&](const std::wstring& sourcePath)
        {
            return !sourcePath.empty() && AreFoldersOnSameDrive(sourcePath, destinationFolder);
        });
    }

    bool IsValidFolderName(const std::wstring& name, std::wstring* errorMessage)
    {
        if (name.empty())
        {
            if (errorMessage) *errorMessage = L"Folder name cannot be empty.";
            return false;
        }

        if (name.size() > 255)
        {
            if (errorMessage) *errorMessage = L"Folder name is too long.";
            return false;
        }

        const wchar_t invalidChars[] = L"<>:\"/\\|?*";
        for (const wchar_t invalidChar : invalidChars)
        {
            if (name.find(invalidChar) != std::wstring::npos)
            {
                if (errorMessage) *errorMessage = L"Folder name contains invalid characters.";
                return false;
            }
        }

        if (name.back() == L' ' || name.back() == L'.')
        {
            if (errorMessage) *errorMessage = L"Folder names cannot end with a space or a period.";
            return false;
        }

        return true;
    }

    std::wstring LongestCommonPrefix(const std::vector<std::wstring>& values)
    {
        if (values.empty())
        {
            return {};
        }

        std::wstring prefix = values.front();
        for (std::size_t index = 1; index < values.size() && !prefix.empty(); ++index)
        {
            const std::wstring& value = values[index];
            const std::size_t maxCommonLength = std::min(prefix.size(), value.size());
            std::size_t commonLength = 0;
            while (commonLength < maxCommonLength && prefix[commonLength] == value[commonLength])
            {
                ++commonLength;
            }

            prefix.resize(commonLength);
        }

        return prefix;
    }

    std::wstring TrimTrailingFolderNameSeparators(std::wstring value)
    {
        while (!value.empty())
        {
            const wchar_t ch = value.back();
            if (ch == L' ' || ch == L'_' || ch == L'-' || ch == L'.')
            {
                value.pop_back();
                continue;
            }

            break;
        }

        return value;
    }

    std::wstring ResolveStartupPath(std::wstring_view path)
    {
        if (path.empty())
        {
            return {};
        }

        std::error_code error;
        fs::path resolvedPath = fs::absolute(fs::path(path), error);
        if (error)
        {
            resolvedPath = fs::path(path);
        }

        return NormalizeFolderPath(resolvedPath.lexically_normal().wstring());
    }

    bool InsertFolderPath(std::vector<std::wstring>* paths,
                          std::wstring folderPath,
                          std::size_t maxCount,
                          bool moveToFront)
    {
        if (!paths)
        {
            return false;
        }

        folderPath = NormalizeFolderPath(std::move(folderPath));
        if (folderPath.empty())
        {
            return false;
        }

        const auto existing = std::find_if(paths->begin(), paths->end(), [&](const std::wstring& candidate)
        {
            return FolderPathsEqual(candidate, folderPath);
        });

        if (existing != paths->end())
        {
            if (!moveToFront)
            {
                return false;
            }

            if (existing == paths->begin())
            {
                return false;
            }

            paths->erase(existing);
        }

        if (moveToFront)
        {
            paths->insert(paths->begin(), std::move(folderPath));
        }
        else if (paths->size() < maxCount)
        {
            paths->push_back(std::move(folderPath));
        }
        else
        {
            return false;
        }

        if (paths->size() > maxCount)
        {
            paths->resize(maxCount);
        }

        return true;
    }

    std::vector<std::wstring> DeserializeFolderPathList(std::wstring_view serialized, std::size_t maxCount)
    {
        std::vector<std::wstring> paths;
        std::wstring current;
        for (const wchar_t character : serialized)
        {
            if (character == L'\r')
            {
                continue;
            }

            if (character == L'\n')
            {
                InsertFolderPath(&paths, std::move(current), maxCount, false);
                current.clear();
                continue;
            }

            current.push_back(character);
        }

        InsertFolderPath(&paths, std::move(current), maxCount, false);
        return paths;
    }

    std::wstring SerializeFolderPathList(const std::vector<std::wstring>& paths)
    {
        std::wstring serialized;
        for (std::size_t index = 0; index < paths.size(); ++index)
        {
            if (index > 0)
            {
                serialized.push_back(L'\n');
            }

            serialized.append(paths[index]);
        }

        return serialized;
    }

    bool CopyTextToClipboard(HWND ownerWindow, std::wstring_view text)
    {
        if (!OpenClipboard(ownerWindow))
        {
            return false;
        }

        if (!EmptyClipboard())
        {
            CloseClipboard();
            return false;
        }

        const std::size_t bytes = (text.size() + 1) * sizeof(wchar_t);
        HGLOBAL buffer = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (!buffer)
        {
            CloseClipboard();
            return false;
        }

        void* locked = GlobalLock(buffer);
        if (!locked)
        {
            GlobalFree(buffer);
            CloseClipboard();
            return false;
        }

        memcpy(locked, text.data(), text.size() * sizeof(wchar_t));
        static_cast<wchar_t*>(locked)[text.size()] = L'\0';
        GlobalUnlock(buffer);

        if (!SetClipboardData(CF_UNICODETEXT, buffer))
        {
            GlobalFree(buffer);
            CloseClipboard();
            return false;
        }

        CloseClipboard();
        return true;
    }

    std::wstring BuildDeleteConfirmationMessage(std::size_t itemCount, bool permanent)
    {
        if (itemCount <= 1)
        {
            return permanent
                ? L"Permanently delete the selected image?\n\nThis cannot be undone."
                : L"Move the selected image to the Recycle Bin?";
        }

        return permanent
            ? L"Permanently delete " + std::to_wstring(itemCount) + L" selected images?\n\nThis cannot be undone."
            : L"Move " + std::to_wstring(itemCount) + L" selected images to the Recycle Bin?";
    }

    bool ConfirmFileDeletion(HWND ownerWindow, std::size_t itemCount, bool permanent)
    {
        const std::wstring prompt = BuildDeleteConfirmationMessage(itemCount, permanent);
        const int result = MessageBoxW(ownerWindow,
                                       prompt.c_str(),
                                       permanent ? L"Permanent Delete" : L"Delete",
                                       MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2);
        return result == IDOK;
    }

    std::wstring BuildFolderDeleteConfirmationMessage(std::wstring_view folderPath, bool permanent)
    {
        const std::wstring folderLabel = GetFolderDisplayName(folderPath);
        return permanent
            ? L"Permanently delete the folder \"" + folderLabel + L"\"?\n\nThis cannot be undone."
            : L"Move the folder \"" + folderLabel + L"\" to the Recycle Bin?";
    }

    bool ConfirmFolderDeletion(HWND ownerWindow, std::wstring_view folderPath, bool permanent)
    {
        const std::wstring prompt = BuildFolderDeleteConfirmationMessage(folderPath, permanent);
        const int result = MessageBoxW(ownerWindow,
                                       prompt.c_str(),
                                       permanent ? L"Permanent Delete Folder" : L"Delete Folder",
                                       MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2);
        return result == IDOK;
    }

    bool ConfirmFavoriteDestinationClear(HWND ownerWindow, std::size_t favoriteCount)
    {
        const std::wstring prompt = favoriteCount == 1
            ? L"Remove the only favorite destination?"
            : L"Remove all " + std::to_wstring(favoriteCount) + L" favorite destinations?";
        const int result = MessageBoxW(ownerWindow,
                                       prompt.c_str(),
                                       L"Clear Favorite Destinations",
                                       MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
        return result == IDYES;
    }

    bool ShouldConfirmDeletion(bool permanent)
    {
        return permanent && (GetAsyncKeyState(VK_SHIFT) & 0x8000) == 0;
    }

    std::wstring FindExistingFolderAncestor(fs::path candidate)
    {
        std::error_code error;
        while (!candidate.empty())
        {
            if (fs::is_directory(candidate, error) && !error)
            {
                return NormalizeFolderPath(candidate.wstring());
            }

            error.clear();
            const fs::path parent = candidate.parent_path();
            if (parent == candidate)
            {
                break;
            }

            candidate = parent;
        }

        return {};
    }

    std::wstring JoinLines(const std::vector<std::wstring>& lines)
    {
        std::wstring combined;
        for (std::size_t index = 0; index < lines.size(); ++index)
        {
            if (index > 0)
            {
                combined.append(L"\r\n");
            }
            combined.append(lines[index]);
        }
        return combined;
    }

    std::wstring BuildFileConflictContent(std::size_t conflictCount)
    {
        if (conflictCount == 1)
        {
            return L"1 selected file would use a name that already exists in the destination or is already queued for this operation.";
        }

        return std::to_wstring(conflictCount)
            + L" selected files would use names that already exist in the destination or are already queued for this operation.";
    }

    bool PromptForFileConflictPolicy(HWND ownerWindow,
                                     hyperbrowse::services::FileOperationType type,
                                     std::size_t conflictCount,
                                     hyperbrowse::services::FileConflictPolicy* conflictPolicy)
    {
        if (!conflictPolicy)
        {
            return false;
        }

        if (conflictCount == 0)
        {
            *conflictPolicy = hyperbrowse::services::FileConflictPolicy::PromptShell;
            return true;
        }

        TASKDIALOG_BUTTON buttons[] = {
            {1001, L"Overwrite target files\nReplace the existing destination files when names collide."},
            {1002, L"Auto-rename incoming files\nKeep both versions using numeric suffixes like photo.1.jpg and photo.2.jpg."},
        };

        const std::wstring content = BuildFileConflictContent(conflictCount);

        TASKDIALOGCONFIG config{};
        config.cbSize = sizeof(config);
        config.hwndParent = ownerWindow;
        config.dwFlags = TDF_USE_COMMAND_LINKS | TDF_ALLOW_DIALOG_CANCELLATION;
        config.dwCommonButtons = TDCBF_CANCEL_BUTTON;
        config.pszWindowTitle = type == hyperbrowse::services::FileOperationType::Move
            ? L"Move Conflicts"
            : L"Copy Conflicts";
        config.pszMainIcon = TD_WARNING_ICON;
        config.pszMainInstruction = L"Conflicting file names were found in the destination.";
        config.pszContent = content.c_str();
        config.cButtons = static_cast<UINT>(std::size(buttons));
        config.pButtons = buttons;
        config.nDefaultButton = 1002;

        int clickedButton = 0;
        const HRESULT dialogResult = TaskDialogIndirect(&config, &clickedButton, nullptr, nullptr);
        if (FAILED(dialogResult) || clickedButton == IDCANCEL)
        {
            return false;
        }

        *conflictPolicy = clickedButton == 1001
            ? hyperbrowse::services::FileConflictPolicy::OverwriteExisting
            : hyperbrowse::services::FileConflictPolicy::AutoRenameNumericSuffix;
        return true;
    }

    bool LaunchShellTarget(HWND ownerWindow, const wchar_t* verb, std::wstring_view target)
    {
        if (target.empty())
        {
            return false;
        }

        const std::wstring path(target);
        const HINSTANCE result = ShellExecuteW(ownerWindow, verb, path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return reinterpret_cast<INT_PTR>(result) > 32;
    }

    bool IsWindows11OrGreater()
    {
        HKEY versionKey{};
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                          L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
                          0,
                          KEY_QUERY_VALUE | KEY_WOW64_64KEY,
                          &versionKey) != ERROR_SUCCESS)
        {
            return false;
        }

        wchar_t buildBuffer[32]{};
        DWORD valueType = 0;
        DWORD valueBytes = sizeof(buildBuffer);
        const LSTATUS result = RegQueryValueExW(versionKey,
                                                L"CurrentBuildNumber",
                                                nullptr,
                                                &valueType,
                                                reinterpret_cast<LPBYTE>(buildBuffer),
                                                &valueBytes);
        RegCloseKey(versionKey);
        if (result != ERROR_SUCCESS || valueType != REG_SZ)
        {
            return false;
        }

        wchar_t* parseEnd = nullptr;
        const unsigned long buildNumber = wcstoul(buildBuffer, &parseEnd, 10);
        return parseEnd != buildBuffer && *parseEnd == L'\0' && buildNumber >= 22000;
    }

    bool LaunchDefaultAppsSettings(HWND ownerWindow)
    {
        if (IsWindows11OrGreater()
            && LaunchShellTarget(ownerWindow, L"open", L"ms-settings:defaultapps?registeredAppUser=HyperBrowse"))
        {
            return true;
        }

        return LaunchShellTarget(ownerWindow, L"open", L"ms-settings:defaultapps");
    }

    std::wstring FindUserGuidePath()
    {
        std::vector<wchar_t> modulePath(512);
        DWORD length = 0;
        while (true)
        {
            length = GetModuleFileNameW(nullptr, modulePath.data(), static_cast<DWORD>(modulePath.size()));
            if (length == 0)
            {
                return {};
            }
            if (length < modulePath.size() - 1)
            {
                break;
            }
            if (modulePath.size() >= 32768)
            {
                return {};
            }
            modulePath.resize(modulePath.size() * 2);
        }

        const fs::path moduleDirectory = fs::path(std::wstring(modulePath.data(), length)).parent_path();
        const fs::path candidates[] = {
            moduleDirectory / L"docs" / L"user-guide.html",
            moduleDirectory.parent_path() / L"docs" / L"user-guide.html",
        };

        for (const fs::path& candidate : candidates)
        {
            std::error_code error;
            if (fs::is_regular_file(candidate, error))
            {
                return candidate.lexically_normal().wstring();
            }
        }

        return {};
    }

    bool PromptForCrossDriveDropOperation(HWND ownerWindow,
                                          std::wstring_view destinationFolder,
                                          hyperbrowse::services::FileOperationType* operationType)
    {
        if (!operationType)
        {
            return false;
        }

        TASKDIALOG_BUTTON buttons[] = {
            {1001, L"Copy files\nLeave the original files where they are and copy them into the target folder."},
            {1002, L"Move files\nTransfer the files into the target folder and remove them from the original drive."},
        };

        const std::wstring destinationLabel = GetFolderDisplayName(destinationFolder);
        const std::wstring content = L"The destination folder \""
            + destinationLabel
            + L"\" is on a different drive. Choose whether to copy the dropped files or move them.";

        TASKDIALOGCONFIG config{};
        config.cbSize = sizeof(config);
        config.hwndParent = ownerWindow;
        config.dwFlags = TDF_USE_COMMAND_LINKS | TDF_ALLOW_DIALOG_CANCELLATION;
        config.dwCommonButtons = TDCBF_CANCEL_BUTTON;
        config.pszWindowTitle = L"Drop Images";
        config.pszMainIcon = TD_INFORMATION_ICON;
        config.pszMainInstruction = L"Choose how to handle the dropped files.";
        config.pszContent = content.c_str();
        config.cButtons = static_cast<UINT>(std::size(buttons));
        config.pButtons = buttons;
        config.nDefaultButton = 1001;

        int clickedButton = 0;
        const HRESULT dialogResult = TaskDialogIndirect(&config, &clickedButton, nullptr, nullptr);
        if (FAILED(dialogResult) || clickedButton == IDCANCEL)
        {
            return false;
        }

        *operationType = clickedButton == 1002
            ? hyperbrowse::services::FileOperationType::Move
            : hyperbrowse::services::FileOperationType::Copy;
        return true;
    }

    bool RevealPathsInExplorer(const std::vector<std::wstring>& selectedPaths)
    {
        if (selectedPaths.empty())
        {
            return false;
        }

        std::vector<std::wstring> revealPaths;
        revealPaths.push_back(selectedPaths.front());

        const std::wstring primaryParent = NormalizeFolderPath(fs::path(selectedPaths.front()).parent_path().wstring());
        bool sameParent = !primaryParent.empty();
        for (const std::wstring& path : selectedPaths)
        {
            if (!FolderPathsEqual(primaryParent, fs::path(path).parent_path().wstring()))
            {
                sameParent = false;
                break;
            }
        }

        if (sameParent)
        {
            revealPaths = selectedPaths;
        }

        PIDLIST_ABSOLUTE folderPidl = ILCreateFromPathW(primaryParent.c_str());
        if (!folderPidl)
        {
            return false;
        }

        std::vector<PIDLIST_ABSOLUTE> itemPidls;
        std::vector<PCUITEMID_CHILD> childPidls;
        itemPidls.reserve(revealPaths.size());
        childPidls.reserve(revealPaths.size());
        for (const std::wstring& path : revealPaths)
        {
            PIDLIST_ABSOLUTE itemPidl = ILCreateFromPathW(path.c_str());
            if (!itemPidl)
            {
                continue;
            }

            itemPidls.push_back(itemPidl);
            childPidls.push_back(ILFindLastID(itemPidl));
        }

        const HRESULT result = SHOpenFolderAndSelectItems(folderPidl,
                                                          static_cast<UINT>(childPidls.size()),
                                                          childPidls.empty() ? nullptr : childPidls.data(),
                                                          0);

        for (PIDLIST_ABSOLUTE itemPidl : itemPidls)
        {
            ILFree(itemPidl);
        }
        ILFree(folderPidl);

        return SUCCEEDED(result);
    }

    bool ShowMultiFilePropertiesDialog(const std::vector<std::wstring>& selectedPaths)
    {
        if (selectedPaths.size() < 2)
        {
            return false;
        }

        std::vector<PIDLIST_ABSOLUTE> itemPidls;
        std::vector<PCIDLIST_ABSOLUTE> absolutePidls;
        itemPidls.reserve(selectedPaths.size());
        absolutePidls.reserve(selectedPaths.size());
        for (const std::wstring& path : selectedPaths)
        {
            PIDLIST_ABSOLUTE itemPidl = ILCreateFromPathW(path.c_str());
            if (!itemPidl)
            {
                continue;
            }

            itemPidls.push_back(itemPidl);
            absolutePidls.push_back(itemPidl);
        }

        HRESULT result = E_FAIL;
        if (absolutePidls.size() >= 2)
        {
            Microsoft::WRL::ComPtr<IShellItemArray> shellItemArray;
            result = SHCreateShellItemArrayFromIDLists(static_cast<UINT>(absolutePidls.size()),
                                                       absolutePidls.data(),
                                                       shellItemArray.GetAddressOf());
            if (SUCCEEDED(result) && shellItemArray)
            {
                Microsoft::WRL::ComPtr<IDataObject> dataObject;
                result = shellItemArray->BindToHandler(nullptr,
                                                       BHID_DataObject,
                                                       IID_PPV_ARGS(dataObject.GetAddressOf()));
                if (SUCCEEDED(result) && dataObject)
                {
                    result = SHMultiFileProperties(dataObject.Get(), 0);
                }
            }
        }

        for (PIDLIST_ABSOLUTE itemPidl : itemPidls)
        {
            ILFree(itemPidl);
        }

        return SUCCEEDED(result);
    }

    class ShellFileDragSource final : public IDropSource
    {
    public:
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID interfaceId, void** object) override
        {
            if (!object)
            {
                return E_POINTER;
            }

            *object = nullptr;
            if (interfaceId == IID_IUnknown || interfaceId == IID_IDropSource)
            {
                *object = static_cast<IDropSource*>(this);
                AddRef();
                return S_OK;
            }

            return E_NOINTERFACE;
        }

        ULONG STDMETHODCALLTYPE AddRef() override
        {
            return ++referenceCount_;
        }

        ULONG STDMETHODCALLTYPE Release() override
        {
            const ULONG remainingReferences = --referenceCount_;
            if (remainingReferences == 0)
            {
                delete this;
            }
            return remainingReferences;
        }

        HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL escapePressed, DWORD keyState) override
        {
            if (escapePressed)
            {
                return DRAGDROP_S_CANCEL;
            }

            return (keyState & MK_LBUTTON) == 0 ? DRAGDROP_S_DROP : S_OK;
        }

        HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD effect) override
        {
            (void)effect;
            return DRAGDROP_S_USEDEFAULTCURSORS;
        }

    private:
        ULONG referenceCount_{1};
    };

    bool CreateShellFileDataObject(const std::vector<std::wstring>& paths,
                                   Microsoft::WRL::ComPtr<IDataObject>* dataObject)
    {
        if (!dataObject || paths.empty())
        {
            return false;
        }

        std::vector<PIDLIST_ABSOLUTE> itemPidls;
        std::vector<PCIDLIST_ABSOLUTE> absolutePidls;
        itemPidls.reserve(paths.size());
        absolutePidls.reserve(paths.size());
        for (const std::wstring& path : paths)
        {
            if (PIDLIST_ABSOLUTE itemPidl = ILCreateFromPathW(path.c_str()))
            {
                itemPidls.push_back(itemPidl);
                absolutePidls.push_back(itemPidl);
            }
        }

        if (itemPidls.empty())
        {
            return false;
        }

        Microsoft::WRL::ComPtr<IShellItemArray> shellItemArray;
    HRESULT result = SHCreateShellItemArrayFromIDLists(static_cast<UINT>(absolutePidls.size()),
                               absolutePidls.data(),
                               shellItemArray.GetAddressOf());
        if (SUCCEEDED(result) && shellItemArray)
        {
            result = shellItemArray->BindToHandler(nullptr,
                                                    BHID_DataObject,
                                                    IID_PPV_ARGS(dataObject->GetAddressOf()));
        }

        for (PIDLIST_ABSOLUTE itemPidl : itemPidls)
        {
            ILFree(itemPidl);
        }

        return SUCCEEDED(result) && *dataObject;
    }

    struct ShellTreeItemInfo
    {
        std::wstring displayName;
        int iconIndex{};
        int openIconIndex{};
    };

    std::wstring FormatDriveDisplayName(const std::wstring& rootPath)
    {
        wchar_t volumeName[MAX_PATH]{};
        if (GetVolumeInformationW(
            rootPath.c_str(),
            volumeName,
            static_cast<DWORD>(std::size(volumeName)),
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            0) != FALSE
            && volumeName[0] != L'\0')
        {
            std::wstring label = volumeName;
            label.append(L" (");
            label.append(rootPath.substr(0, 2));
            label.append(L")");
            return label;
        }

        return rootPath;
    }

    std::wstring TryGetKnownFolderPath(REFKNOWNFOLDERID folderId)
    {
        PWSTR rawPath = nullptr;
        const HRESULT result = SHGetKnownFolderPath(folderId, KF_FLAG_DEFAULT, nullptr, &rawPath);
        if (FAILED(result) || !rawPath)
        {
            return {};
        }

        std::wstring path = rawPath;
        CoTaskMemFree(rawPath);
        return NormalizeFolderPath(std::move(path));
    }

    ShellTreeItemInfo QueryShellTreeItemInfo(const std::wstring& folderPath)
    {
        ShellTreeItemInfo info;

        SHFILEINFOW shellInfo{};
        if (SHGetFileInfoW(
            folderPath.c_str(),
            FILE_ATTRIBUTE_DIRECTORY,
            &shellInfo,
            sizeof(shellInfo),
            SHGFI_DISPLAYNAME | SHGFI_SYSICONINDEX | SHGFI_SMALLICON) != 0)
        {
            info.displayName = shellInfo.szDisplayName;
            info.iconIndex = shellInfo.iIcon;
        }

        if (SHGetFileInfoW(
            folderPath.c_str(),
            FILE_ATTRIBUTE_DIRECTORY,
            &shellInfo,
            sizeof(shellInfo),
            SHGFI_OPENICON | SHGFI_SYSICONINDEX | SHGFI_SMALLICON) != 0)
        {
            info.openIconIndex = shellInfo.iIcon;
        }
        else
        {
            info.openIconIndex = info.iconIndex;
        }

        if (info.displayName.empty())
        {
            const fs::path path(folderPath);
            const std::wstring normalizedRoot = NormalizeFolderPath(path.root_path().wstring());
            info.displayName = FolderPathsEqual(normalizedRoot, folderPath)
                ? FormatDriveDisplayName(folderPath)
                : GetFolderDisplayName(folderPath);
        }

        return info;
    }

    hyperbrowse::browser::BrowserSortMode SortModeFromCommandId(UINT commandId)
    {
        switch (commandId)
        {
        case ID_VIEW_SORT_FILENAME:
            return hyperbrowse::browser::BrowserSortMode::FileName;
        case ID_VIEW_SORT_MODIFIED:
            return hyperbrowse::browser::BrowserSortMode::ModifiedDate;
        case ID_VIEW_SORT_SIZE:
            return hyperbrowse::browser::BrowserSortMode::FileSize;
        case ID_VIEW_SORT_DIMENSIONS:
            return hyperbrowse::browser::BrowserSortMode::Dimensions;
        case ID_VIEW_SORT_TYPE:
            return hyperbrowse::browser::BrowserSortMode::FileType;
        case ID_VIEW_SORT_DATETAKEN:
            return hyperbrowse::browser::BrowserSortMode::DateTaken;
        case ID_VIEW_SORT_RATING:
            return hyperbrowse::browser::BrowserSortMode::Rating;
        case ID_VIEW_SORT_TAGS:
            return hyperbrowse::browser::BrowserSortMode::Tags;
        case ID_VIEW_SORT_RANDOM:
        default:
            return hyperbrowse::browser::BrowserSortMode::Random;
        }
    }

    UINT CommandIdFromSortMode(hyperbrowse::browser::BrowserSortMode sortMode)
    {
        switch (sortMode)
        {
        case hyperbrowse::browser::BrowserSortMode::FileName:
            return ID_VIEW_SORT_FILENAME;
        case hyperbrowse::browser::BrowserSortMode::ModifiedDate:
            return ID_VIEW_SORT_MODIFIED;
        case hyperbrowse::browser::BrowserSortMode::FileSize:
            return ID_VIEW_SORT_SIZE;
        case hyperbrowse::browser::BrowserSortMode::Dimensions:
            return ID_VIEW_SORT_DIMENSIONS;
        case hyperbrowse::browser::BrowserSortMode::FileType:
            return ID_VIEW_SORT_TYPE;
        case hyperbrowse::browser::BrowserSortMode::DateTaken:
            return ID_VIEW_SORT_DATETAKEN;
        case hyperbrowse::browser::BrowserSortMode::Rating:
            return ID_VIEW_SORT_RATING;
        case hyperbrowse::browser::BrowserSortMode::Tags:
            return ID_VIEW_SORT_TAGS;
        case hyperbrowse::browser::BrowserSortMode::Random:
        default:
            return ID_VIEW_SORT_RANDOM;
        }
    }

    bool TryParseThumbnailSizePreset(DWORD value, hyperbrowse::browser::ThumbnailSizePreset* preset)
    {
        if (!preset)
        {
            return false;
        }

        switch (value)
        {
        case static_cast<DWORD>(hyperbrowse::browser::ThumbnailSizePreset::Pixels96):
            *preset = hyperbrowse::browser::ThumbnailSizePreset::Pixels96;
            return true;
        case static_cast<DWORD>(hyperbrowse::browser::ThumbnailSizePreset::Pixels128):
            *preset = hyperbrowse::browser::ThumbnailSizePreset::Pixels128;
            return true;
        case static_cast<DWORD>(hyperbrowse::browser::ThumbnailSizePreset::Pixels160):
            *preset = hyperbrowse::browser::ThumbnailSizePreset::Pixels160;
            return true;
        case static_cast<DWORD>(hyperbrowse::browser::ThumbnailSizePreset::Pixels192):
            *preset = hyperbrowse::browser::ThumbnailSizePreset::Pixels192;
            return true;
        case static_cast<DWORD>(hyperbrowse::browser::ThumbnailSizePreset::Pixels256):
            *preset = hyperbrowse::browser::ThumbnailSizePreset::Pixels256;
            return true;
        case static_cast<DWORD>(hyperbrowse::browser::ThumbnailSizePreset::Pixels320):
            *preset = hyperbrowse::browser::ThumbnailSizePreset::Pixels320;
            return true;
        case static_cast<DWORD>(hyperbrowse::browser::ThumbnailSizePreset::Pixels360):
            *preset = hyperbrowse::browser::ThumbnailSizePreset::Pixels360;
            return true;
        case static_cast<DWORD>(hyperbrowse::browser::ThumbnailSizePreset::Pixels420):
            *preset = hyperbrowse::browser::ThumbnailSizePreset::Pixels420;
            return true;
        case static_cast<DWORD>(hyperbrowse::browser::ThumbnailSizePreset::Pixels480):
            *preset = hyperbrowse::browser::ThumbnailSizePreset::Pixels480;
            return true;
        case static_cast<DWORD>(hyperbrowse::browser::ThumbnailSizePreset::Pixels560):
            *preset = hyperbrowse::browser::ThumbnailSizePreset::Pixels560;
            return true;
        case static_cast<DWORD>(hyperbrowse::browser::ThumbnailSizePreset::Pixels640):
            *preset = hyperbrowse::browser::ThumbnailSizePreset::Pixels640;
            return true;
        default:
            return false;
        }
    }

    hyperbrowse::browser::ThumbnailSizePreset ThumbnailSizePresetFromCommandId(UINT commandId)
    {
        switch (commandId)
        {
        case ID_VIEW_THUMBNAIL_SIZE_96:
            return hyperbrowse::browser::ThumbnailSizePreset::Pixels96;
        case ID_VIEW_THUMBNAIL_SIZE_128:
            return hyperbrowse::browser::ThumbnailSizePreset::Pixels128;
        case ID_VIEW_THUMBNAIL_SIZE_160:
            return hyperbrowse::browser::ThumbnailSizePreset::Pixels160;
        case ID_VIEW_THUMBNAIL_SIZE_256:
            return hyperbrowse::browser::ThumbnailSizePreset::Pixels256;
        case ID_VIEW_THUMBNAIL_SIZE_320:
            return hyperbrowse::browser::ThumbnailSizePreset::Pixels320;
        case ID_VIEW_THUMBNAIL_SIZE_360:
            return hyperbrowse::browser::ThumbnailSizePreset::Pixels360;
        case ID_VIEW_THUMBNAIL_SIZE_420:
            return hyperbrowse::browser::ThumbnailSizePreset::Pixels420;
        case ID_VIEW_THUMBNAIL_SIZE_480:
            return hyperbrowse::browser::ThumbnailSizePreset::Pixels480;
        case ID_VIEW_THUMBNAIL_SIZE_560:
            return hyperbrowse::browser::ThumbnailSizePreset::Pixels560;
        case ID_VIEW_THUMBNAIL_SIZE_640:
            return hyperbrowse::browser::ThumbnailSizePreset::Pixels640;
        case ID_VIEW_THUMBNAIL_SIZE_192:
        default:
            return hyperbrowse::browser::ThumbnailSizePreset::Pixels192;
        }
    }

    UINT CommandIdFromThumbnailSizePreset(hyperbrowse::browser::ThumbnailSizePreset preset)
    {
        switch (preset)
        {
        case hyperbrowse::browser::ThumbnailSizePreset::Pixels96:
            return ID_VIEW_THUMBNAIL_SIZE_96;
        case hyperbrowse::browser::ThumbnailSizePreset::Pixels128:
            return ID_VIEW_THUMBNAIL_SIZE_128;
        case hyperbrowse::browser::ThumbnailSizePreset::Pixels160:
            return ID_VIEW_THUMBNAIL_SIZE_160;
        case hyperbrowse::browser::ThumbnailSizePreset::Pixels256:
            return ID_VIEW_THUMBNAIL_SIZE_256;
        case hyperbrowse::browser::ThumbnailSizePreset::Pixels320:
            return ID_VIEW_THUMBNAIL_SIZE_320;
        case hyperbrowse::browser::ThumbnailSizePreset::Pixels360:
            return ID_VIEW_THUMBNAIL_SIZE_360;
        case hyperbrowse::browser::ThumbnailSizePreset::Pixels420:
            return ID_VIEW_THUMBNAIL_SIZE_420;
        case hyperbrowse::browser::ThumbnailSizePreset::Pixels480:
            return ID_VIEW_THUMBNAIL_SIZE_480;
        case hyperbrowse::browser::ThumbnailSizePreset::Pixels560:
            return ID_VIEW_THUMBNAIL_SIZE_560;
        case hyperbrowse::browser::ThumbnailSizePreset::Pixels640:
            return ID_VIEW_THUMBNAIL_SIZE_640;
        case hyperbrowse::browser::ThumbnailSizePreset::Pixels192:
        default:
            return ID_VIEW_THUMBNAIL_SIZE_192;
        }
    }

    hyperbrowse::viewer::MouseWheelBehavior ViewerMouseWheelBehaviorFromCommandId(UINT commandId)
    {
        switch (commandId)
        {
        case ID_VIEW_VIEWER_MOUSE_WHEEL_NAVIGATE:
            return hyperbrowse::viewer::MouseWheelBehavior::Navigate;
        case ID_VIEW_VIEWER_MOUSE_WHEEL_ZOOM:
        default:
            return hyperbrowse::viewer::MouseWheelBehavior::Zoom;
        }
    }

    UINT CommandIdFromViewerMouseWheelBehavior(hyperbrowse::viewer::MouseWheelBehavior behavior)
    {
        switch (behavior)
        {
        case hyperbrowse::viewer::MouseWheelBehavior::Navigate:
            return ID_VIEW_VIEWER_MOUSE_WHEEL_NAVIGATE;
        case hyperbrowse::viewer::MouseWheelBehavior::Zoom:
        default:
            return ID_VIEW_VIEWER_MOUSE_WHEEL_ZOOM;
        }
    }

    bool IsViewerMouseWheelBehaviorCommand(UINT commandId)
    {
        return commandId >= ID_VIEW_VIEWER_MOUSE_WHEEL_ZOOM
            && commandId <= ID_VIEW_VIEWER_MOUSE_WHEEL_NAVIGATE;
    }

    hyperbrowse::viewer::InfoOverlayTextSize ViewerOverlayTextSizeFromCommandId(UINT commandId)
    {
        switch (commandId)
        {
        case ID_VIEW_VIEWER_OVERLAY_TEXT_MEDIUM:
            return hyperbrowse::viewer::InfoOverlayTextSize::Medium;
        case ID_VIEW_VIEWER_OVERLAY_TEXT_LARGE:
            return hyperbrowse::viewer::InfoOverlayTextSize::Large;
        case ID_VIEW_VIEWER_OVERLAY_TEXT_SMALL:
        default:
            return hyperbrowse::viewer::InfoOverlayTextSize::Small;
        }
    }

    UINT CommandIdFromViewerOverlayTextSize(hyperbrowse::viewer::InfoOverlayTextSize size)
    {
        switch (size)
        {
        case hyperbrowse::viewer::InfoOverlayTextSize::Medium:
            return ID_VIEW_VIEWER_OVERLAY_TEXT_MEDIUM;
        case hyperbrowse::viewer::InfoOverlayTextSize::Large:
            return ID_VIEW_VIEWER_OVERLAY_TEXT_LARGE;
        case hyperbrowse::viewer::InfoOverlayTextSize::Small:
        default:
            return ID_VIEW_VIEWER_OVERLAY_TEXT_SMALL;
        }
    }

    bool IsViewerOverlayTextSizeCommand(UINT commandId)
    {
        return commandId >= ID_VIEW_VIEWER_OVERLAY_TEXT_SMALL
            && commandId <= ID_VIEW_VIEWER_OVERLAY_TEXT_LARGE;
    }

    hyperbrowse::util::AppTextSize AppTextSizeFromCommandId(UINT commandId)
    {
        switch (commandId)
        {
        case ID_VIEW_APP_TEXT_SIZE_SMALL:
            return hyperbrowse::util::AppTextSize::Small;
        case ID_VIEW_APP_TEXT_SIZE_LARGE:
            return hyperbrowse::util::AppTextSize::Large;
        case ID_VIEW_APP_TEXT_SIZE_MEDIUM:
        default:
            return hyperbrowse::util::AppTextSize::Medium;
        }
    }

    UINT CommandIdFromAppTextSize(hyperbrowse::util::AppTextSize size)
    {
        switch (hyperbrowse::util::NormalizeAppTextSize(static_cast<std::uint32_t>(size)))
        {
        case hyperbrowse::util::AppTextSize::Small:
            return ID_VIEW_APP_TEXT_SIZE_SMALL;
        case hyperbrowse::util::AppTextSize::Large:
            return ID_VIEW_APP_TEXT_SIZE_LARGE;
        case hyperbrowse::util::AppTextSize::Medium:
        default:
            return ID_VIEW_APP_TEXT_SIZE_MEDIUM;
        }
    }

    bool IsAppTextSizeCommand(UINT commandId)
    {
        return commandId >= ID_VIEW_APP_TEXT_SIZE_SMALL
            && commandId <= ID_VIEW_APP_TEXT_SIZE_LARGE;
    }

    bool TryParseResourceProfile(DWORD value, hyperbrowse::util::ResourceProfile* resourceProfile)
    {
        if (!resourceProfile)
        {
            return false;
        }

        switch (value)
        {
        case static_cast<DWORD>(hyperbrowse::util::ResourceProfile::Conservative):
            *resourceProfile = hyperbrowse::util::ResourceProfile::Conservative;
            return true;
        case static_cast<DWORD>(hyperbrowse::util::ResourceProfile::Balanced):
            *resourceProfile = hyperbrowse::util::ResourceProfile::Balanced;
            return true;
        case static_cast<DWORD>(hyperbrowse::util::ResourceProfile::Performance):
            *resourceProfile = hyperbrowse::util::ResourceProfile::Performance;
            return true;
        case static_cast<DWORD>(hyperbrowse::util::ResourceProfile::Aggressive):
            *resourceProfile = hyperbrowse::util::ResourceProfile::Aggressive;
            return true;
        default:
            return false;
        }
    }

    hyperbrowse::util::ResourceProfile ResourceProfileFromCommandId(UINT commandId)
    {
        switch (commandId)
        {
        case ID_HELP_PERFORMANCE_PROFILE_CONSERVATIVE:
            return hyperbrowse::util::ResourceProfile::Conservative;
        case ID_HELP_PERFORMANCE_PROFILE_PERFORMANCE:
            return hyperbrowse::util::ResourceProfile::Performance;
        case ID_HELP_PERFORMANCE_PROFILE_AGGRESSIVE:
            return hyperbrowse::util::ResourceProfile::Aggressive;
        case ID_HELP_PERFORMANCE_PROFILE_BALANCED:
        default:
            return hyperbrowse::util::ResourceProfile::Balanced;
        }
    }

    UINT CommandIdFromResourceProfile(hyperbrowse::util::ResourceProfile resourceProfile)
    {
        switch (resourceProfile)
        {
        case hyperbrowse::util::ResourceProfile::Conservative:
            return ID_HELP_PERFORMANCE_PROFILE_CONSERVATIVE;
        case hyperbrowse::util::ResourceProfile::Performance:
            return ID_HELP_PERFORMANCE_PROFILE_PERFORMANCE;
        case hyperbrowse::util::ResourceProfile::Aggressive:
            return ID_HELP_PERFORMANCE_PROFILE_AGGRESSIVE;
        case hyperbrowse::util::ResourceProfile::Balanced:
        default:
            return ID_HELP_PERFORMANCE_PROFILE_BALANCED;
        }
    }

    bool IsCommandInRange(UINT commandId, UINT firstCommandId, UINT lastCommandId)
    {
        return commandId >= firstCommandId && commandId <= lastCommandId;
    }

    std::wstring ToLowercaseCopy(std::wstring value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](wchar_t character)
        {
            return static_cast<wchar_t>(towlower(character));
        });
        return value;
    }

    int RatingFromCommandId(UINT commandId)
    {
        if (commandId < ID_FILE_SET_RATING_0 || commandId > ID_FILE_SET_RATING_5)
        {
            return 0;
        }

        return static_cast<int>(commandId - ID_FILE_SET_RATING_0);
    }

    bool IsRatingCommand(UINT commandId)
    {
        return commandId >= ID_FILE_SET_RATING_0 && commandId <= ID_FILE_SET_RATING_5;
    }

    UINT CommandIdFromRating(int rating)
    {
        rating = std::clamp(rating, 0, 5);
        return ID_FILE_SET_RATING_0 + static_cast<UINT>(rating);
    }

    std::wstring FormatRatingForDisplay(int rating)
    {
        if (rating <= 0)
        {
            return L"Unrated";
        }

        return std::to_wstring(std::clamp(rating, 0, 5)) + L"/5";
    }

    std::wstring TrimWhitespaceCopy(std::wstring value)
    {
        const auto isSpace = [](wchar_t character)
        {
            return iswspace(character) != 0;
        };

        const auto first = std::find_if_not(value.begin(), value.end(), isSpace);
        const auto last = std::find_if_not(value.rbegin(), value.rend(), isSpace).base();
        if (first >= last)
        {
            return {};
        }

        return std::wstring(first, last);
    }

    int CountDecimalDigits(std::size_t value)
    {
        int digits = 1;
        while (value >= 10)
        {
            value /= 10;
            ++digits;
        }

        return digits;
    }

    std::wstring FormatZeroPaddedSequence(std::size_t ordinal, int width)
    {
        wchar_t numberBuffer[32]{};
        swprintf_s(numberBuffer, L"%0*u", std::max(1, width), static_cast<unsigned int>(ordinal));
        return numberBuffer;
    }

    std::wstring BuildBatchRenameLeafName(std::wstring_view baseName,
                                          std::size_t ordinal,
                                          int numberWidth,
                                          std::wstring_view extension)
    {
        wchar_t numberBuffer[32]{};
        swprintf_s(numberBuffer, L"%0*u", numberWidth, static_cast<unsigned int>(ordinal));

        std::wstring leafName(baseName);
        if (!leafName.empty())
        {
            leafName.push_back(L' ');
        }
        leafName.append(numberBuffer);
        leafName.append(extension);
        return leafName;
    }

    bool TryBuildBatchRenamePatternLeafName(std::wstring_view pattern,
                                            const hyperbrowse::browser::BrowserItem& item,
                                            std::size_t ordinal,
                                            std::size_t selectionCount,
                                            int defaultNumberWidth,
                                            std::wstring* leafName,
                                            std::wstring* errorMessage)
    {
        if (!leafName)
        {
            return false;
        }

        const std::wstring trimmedPattern = TrimWhitespaceCopy(std::wstring(pattern));
        if (trimmedPattern.empty())
        {
            if (errorMessage)
            {
                *errorMessage = L"Enter a rename pattern.";
            }
            return false;
        }

        const std::wstring originalLeafName = item.fileName;
        const std::wstring originalStem = fs::path(originalLeafName).stem().wstring();
        const std::wstring originalExtension = fs::path(originalLeafName).extension().wstring();
        const std::wstring folderName = fs::path(item.filePath).parent_path().filename().wstring();

        std::wstring generatedLeafName;
        generatedLeafName.reserve(trimmedPattern.size() + originalLeafName.size() + 16);
        bool usedNumberToken = false;
        bool usedExtensionToken = false;

        for (std::size_t index = 0; index < trimmedPattern.size();)
        {
            if (trimmedPattern[index] != L'{')
            {
                generatedLeafName.push_back(trimmedPattern[index]);
                ++index;
                continue;
            }

            const std::size_t closeBrace = trimmedPattern.find(L'}', index + 1);
            if (closeBrace == std::wstring::npos)
            {
                if (errorMessage)
                {
                    *errorMessage = L"Rename patterns must close every token with '}'.";
                }
                return false;
            }

            const std::wstring token = TrimWhitespaceCopy(trimmedPattern.substr(index + 1, closeBrace - index - 1));
            const std::wstring normalizedToken = ToLowercaseCopy(token);
            if (normalizedToken == L"name")
            {
                generatedLeafName.append(originalStem);
            }
            else if (normalizedToken == L"ext")
            {
                generatedLeafName.append(originalExtension);
                usedExtensionToken = true;
            }
            else if (normalizedToken == L"folder")
            {
                generatedLeafName.append(folderName);
            }
            else if (normalizedToken == L"num" || normalizedToken.rfind(L"num:", 0) == 0)
            {
                int tokenWidth = defaultNumberWidth;
                if (normalizedToken.size() > 4)
                {
                    const wchar_t* widthText = normalizedToken.c_str() + 4;
                    wchar_t* widthEnd = nullptr;
                    const long parsedWidth = wcstol(widthText, &widthEnd, 10);
                    if (widthEnd == widthText || *widthEnd != L'\0' || parsedWidth <= 0 || parsedWidth > 9)
                    {
                        if (errorMessage)
                        {
                            *errorMessage = L"Use {num} or {num:N} with N between 1 and 9.";
                        }
                        return false;
                    }
                    tokenWidth = static_cast<int>(parsedWidth);
                }

                generatedLeafName.append(FormatZeroPaddedSequence(ordinal, tokenWidth));
                usedNumberToken = true;
            }
            else
            {
                if (errorMessage)
                {
                    *errorMessage = L"Unknown token {" + token + L"}. Supported tokens: {name}, {num}, {num:N}, {ext}, {folder}.";
                }
                return false;
            }

            index = closeBrace + 1;
        }

        generatedLeafName = TrimWhitespaceCopy(std::move(generatedLeafName));
        if (!usedNumberToken && selectionCount > 1)
        {
            generatedLeafName = BuildBatchRenameLeafName(generatedLeafName, ordinal, defaultNumberWidth, L"");
        }
        if (!usedExtensionToken)
        {
            generatedLeafName.append(originalExtension);
        }

        std::wstring validationError;
        if (!IsValidRenameLeafName(generatedLeafName, &validationError))
        {
            if (errorMessage)
            {
                *errorMessage = std::move(validationError);
            }
            return false;
        }

        *leafName = std::move(generatedLeafName);
        return true;
    }

    bool IsJpegBrowserItem(const hyperbrowse::browser::BrowserItem& item)
    {
        return hyperbrowse::decode::IsWicFileType(item.fileType)
            && (_wcsicmp(item.fileType.c_str(), L"JPG") == 0 || _wcsicmp(item.fileType.c_str(), L"JPEG") == 0);
    }

    struct AlternateMonitorSearch
    {
        HMONITOR referenceMonitor{};
        HMONITOR alternateMonitor{};
    };

    BOOL CALLBACK CaptureAlternateMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM lParam)
    {
        auto* search = reinterpret_cast<AlternateMonitorSearch*>(lParam);
        if (!search)
        {
            return FALSE;
        }

        if (monitor != search->referenceMonitor)
        {
            search->alternateMonitor = monitor;
            return FALSE;
        }

        return TRUE;
    }

    HMONITOR FindAlternateMonitorForWindow(HWND hwnd)
    {
        if (GetSystemMetrics(SM_CMONITORS) < 2)
        {
            return nullptr;
        }

        const HMONITOR referenceMonitor = hwnd && IsWindow(hwnd)
            ? MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST)
            : MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
        AlternateMonitorSearch search{referenceMonitor, nullptr};
        EnumDisplayMonitors(nullptr, nullptr, &CaptureAlternateMonitor, reinterpret_cast<LPARAM>(&search));
        return search.alternateMonitor;
    }

    HMONITOR ResolveViewerMonitor(HWND hwnd, bool preferSecondaryMonitor)
    {
        if (preferSecondaryMonitor)
        {
            return FindAlternateMonitorForWindow(hwnd);
        }

        if (hwnd && IsWindow(hwnd))
        {
            return MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        }

        return MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    }
}

namespace hyperbrowse::ui
{
    struct PersistentThumbnailCacheMaintenanceState
    {
        std::mutex mutex;
        cache::DiskThumbnailCache::Statistics statistics;
    };

    // OLE drop target that gives real drag-over feedback (copy/move cursor and folder
    // highlight) for external file drags onto the main window. It reuses the window's
    // quick-access row / tree hit-testing so a drop lands where the cursor is.
    class HyperBrowseExternalDropTarget final : public IDropTarget
    {
    public:
        explicit HyperBrowseExternalDropTarget(MainWindow* window)
            : window_(window)
        {
        }

        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override
        {
            if (!object)
            {
                return E_POINTER;
            }
            *object = nullptr;
            if (riid == IID_IUnknown || riid == IID_IDropTarget)
            {
                *object = static_cast<IDropTarget*>(this);
                AddRef();
                return S_OK;
            }
            return E_NOINTERFACE;
        }

        ULONG STDMETHODCALLTYPE AddRef() override { return ++refCount_; }
        ULONG STDMETHODCALLTYPE Release() override
        {
            const ULONG remaining = --refCount_;
            if (remaining == 0)
            {
                delete this;
            }
            return remaining;
        }

        HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* dataObject, DWORD keyState, POINTL point, DWORD* effect) override
        {
            lastDataObject_ = dataObject;
            return DragOver(keyState, point, effect);
        }

        HRESULT STDMETHODCALLTYPE DragOver(DWORD keyState, POINTL point, DWORD* effect) override
        {
            if (!effect)
            {
                return E_POINTER;
            }

            MainWindow* window = window_;
            if (!window || !window->hwnd_)
            {
                *effect = DROPEFFECT_NONE;
                return S_OK;
            }

            POINT clientPoint{point.x, point.y};
            ScreenToClient(window->hwnd_, &clientPoint);

            HTREEITEM treeItem = nullptr;
            const std::vector<std::wstring> sourcePaths = window->ShellPathsFromDataObject(lastDataObject_);
            const std::wstring destination = window->ResolveExternalDropTarget(clientPoint, &treeItem);
            const DWORD allowed = window->DropEffectForKeyState(keyState, destination, sourcePaths);
            *effect = allowed;

            if (window->externalDropTreeHoverItem_ != treeItem && window->treePane_)
            {
                TreeView_SelectDropTarget(window->treePane_, treeItem);
                window->externalDropTreeHoverItem_ = treeItem;
            }
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE DragLeave() override
        {
            if (window_)
            {
                window_->ClearExternalDropVisuals();
            }
            lastDataObject_ = nullptr;
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE Drop(IDataObject* dataObject, DWORD keyState, POINTL point, DWORD* effect) override
        {
            if (!effect)
            {
                return E_POINTER;
            }

            MainWindow* window = window_;
            if (window && window->hwnd_)
            {
                POINT clientPoint{point.x, point.y};
                ScreenToClient(window->hwnd_, &clientPoint);
                HTREEITEM treeItem = nullptr;
                const std::vector<std::wstring> sourcePaths = window->ShellPathsFromDataObject(dataObject);
                const std::wstring destination = window->ResolveExternalDropTarget(clientPoint, &treeItem);
                const DWORD chosen = window->DropEffectForKeyState(keyState, destination, sourcePaths);
                window->HandleExternalDrop(dataObject, chosen, clientPoint);
                *effect = chosen;
            }
            else
            {
                *effect = DROPEFFECT_NONE;
            }
            lastDataObject_ = nullptr;
            return S_OK;
        }

    private:
        MainWindow* window_{};
        IDataObject* lastDataObject_{};
        ULONG refCount_{1};
    };

    MainWindow::MainWindow(HINSTANCE instance)
        : instance_(instance)
        , browserModel_(std::make_unique<browser::BrowserModel>())
        , browserPaneController_(std::make_unique<browser::BrowserPane>(instance))
        , batchConvertService_(std::make_unique<services::BatchConvertService>())
        , fileOperationService_(std::make_unique<services::FileOperationService>())
        , folderEnumerationService_(std::make_unique<services::FolderEnumerationService>())
        , folderTreeEnumerationService_(std::make_unique<services::FolderTreeEnumerationService>())
        , folderWatchService_(std::make_unique<services::FolderWatchService>())
        , userMetadataStore_(std::make_unique<services::UserMetadataStore>())
        , diagnosticsWindow_(std::make_unique<DiagnosticsWindow>(instance))
        , viewerWindow_(std::make_unique<viewer::ViewerWindow>(instance))
        , slideshowTransitionStyle_(viewer::TransitionStyle::Crossfade)
    {
    }

    MainWindow::~MainWindow()
    {
        cacheMaintenanceExecutor_.reset();

        if (shortcutReferenceWindow_ && IsWindow(shortcutReferenceWindow_))
        {
            DestroyWindow(shortcutReferenceWindow_);
            shortcutReferenceWindow_ = nullptr;
        }

        if (externalDropTarget_)
        {
            if (hwnd_)
            {
                RevokeDragDrop(hwnd_);
            }
            externalDropTarget_->Release();
            externalDropTarget_ = nullptr;
        }

        if (folderEnumerationService_)
        {
            folderEnumerationService_->Cancel();
        }

        if (folderTreeEnumerationService_)
        {
            folderTreeEnumerationService_->CancelAll();
        }

        if (folderWatchService_)
        {
            folderWatchService_->Stop();
        }

        if (batchConvertService_)
        {
            batchConvertService_->Cancel();
        }

        if (fileOperationService_)
        {
            fileOperationService_->Cancel();
        }

        if (backgroundBrush_)
        {
            DeleteObject(backgroundBrush_);
        }

        if (actionFieldBrush_)
        {
            DeleteObject(actionFieldBrush_);
        }

        if (detailsPanelBrush_)
        {
            DeleteObject(detailsPanelBrush_);
        }

        if (menuBackgroundBrush_)
        {
            DeleteObject(menuBackgroundBrush_);
        }

        DeleteFontIfOwned(appTextUiFont_);
        DeleteFontIfOwned(detailsPanelTitleFont_);
        DeleteFontIfOwned(detailsPanelSummaryFont_);
        DeleteFontIfOwned(detailsPanelBodyFont_);

        if (detailsPanelRichEditModule_)
        {
            FreeLibrary(detailsPanelRichEditModule_);
            detailsPanelRichEditModule_ = nullptr;
        }

        if (hwnd_)
        {
            RevokeDragDrop(hwnd_);
        }
        RemoveTrayIcon();
        if (externalDropTarget_)
        {
            externalDropTarget_->Release();
            externalDropTarget_ = nullptr;
        }
        if (taskbarList_)
        {
            taskbarList_->Release();
            taskbarList_ = nullptr;
        }

        if (accelerators_)
        {
            DestroyAcceleratorTable(accelerators_);
        }
    }

    bool MainWindow::Create()
    {
        util::ScopedTimer timer{L"MainWindow::Create"};

        if (!RegisterWindowClass())
        {
            return false;
        }

        LoadWindowState();
        ApplyStartupLaunchPathOverride();
        ApplyResourceProfileSetting();
        ApplyCacheCapacityOverrideSettings();
        ApplyViewerMouseWheelSetting();
        ApplyViewerTransitionSettings();

        int initialWindowX = CW_USEDEFAULT;
        int initialWindowY = CW_USEDEFAULT;
        int initialWindowWidth = CW_USEDEFAULT;
        int initialWindowHeight = CW_USEDEFAULT;
        if (hasPersistedWindowBounds_)
        {
            initialWindowX = persistedWindowBounds_.left;
            initialWindowY = persistedWindowBounds_.top;
            initialWindowWidth = persistedWindowBounds_.right - persistedWindowBounds_.left;
            initialWindowHeight = persistedWindowBounds_.bottom - persistedWindowBounds_.top;
        }

        hwnd_ = CreateWindowExW(
            0,
            kWindowClassName,
            L"HyperBrowse",
            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
            initialWindowX,
            initialWindowY,
            initialWindowWidth,
            initialWindowHeight,
            nullptr,
            nullptr,
            instance_,
            this);

        if (!hwnd_)
        {
            util::LogLastError(L"CreateWindowExW(MainWindow)");
            return false;
        }

        sessionNotificationRegistered_ = WTSRegisterSessionNotification(hwnd_, NOTIFY_FOR_THIS_SESSION) != FALSE;
        consoleDisplayNotify_ = RegisterPowerSettingNotification(
            hwnd_,
            &kConsoleDisplayStateGuid,
            DEVICE_NOTIFY_WINDOW_HANDLE);
        monitorPowerNotify_ = RegisterPowerSettingNotification(
            hwnd_,
            &kMonitorPowerOnGuid,
            DEVICE_NOTIFY_WINDOW_HANDLE);

        if (!CreateAccelerators() || !CreateMenuBar() || !CreateChildWindows())
        {
            return false;
        }

        ApplyAppTextSize();

        externalDropTarget_ = new (std::nothrow) HyperBrowseExternalDropTarget(this);
        if (externalDropTarget_ && SUCCEEDED(RegisterDragDrop(hwnd_, externalDropTarget_)))
        {
            // OLE drop target now owns feedback for file drags; the legacy
            // WM_DROPFILES path stays as a fallback for non-OLE sources.
        }
        else
        {
            if (externalDropTarget_)
            {
                externalDropTarget_->Release();
                externalDropTarget_ = nullptr;
            }
            DragAcceptFiles(hwnd_, TRUE);
        }

        memoryPressureExecutor_ = std::make_unique<util::BackgroundExecutor>(1);
        if (memoryPressureExecutor_)
        {
            memoryPressureTimerId_ = SetTimer(hwnd_, kMemoryPressureTimerId, kMemoryPressureIntervalMs, nullptr);
            QueueMemoryPressureSample();
        }
        cacheMaintenanceState_ = std::make_shared<PersistentThumbnailCacheMaintenanceState>();
        cacheMaintenanceExecutor_ = std::make_unique<util::BackgroundExecutor>(1, 1);

        ApplyPersistentThumbnailCacheSetting();
        ApplyTheme();
        UpdateMenuState();
        RefreshBrowserPane();
        UpdateStatusText();
        UpdateWindowTitle();
        return true;
    }

    void MainWindow::Show(int nCmdShow) const
    {
        ShowWindow(hwnd_, nCmdShow);
        UpdateWindow(hwnd_);
    }

    void MainWindow::SetStartupLaunchPath(std::wstring path)
    {
        startupLaunchPathOverride_ = std::move(path);
    }

    void MainWindow::OpenViewerAtPath(const std::wstring& filePath)
    {
        if (filePath.empty())
        {
            return;
        }

        std::error_code error;
        const fs::path resolvedPath(filePath);
        if (!fs::is_regular_file(resolvedPath, error) || error)
        {
            return;
        }

        if (!browser::IsSupportedImageExtension(resolvedPath.extension().wstring()))
        {
            return;
        }

        const fs::path containingFolder = resolvedPath.parent_path();
        if (containingFolder.empty())
        {
            return;
        }

        const std::wstring folderPath = NormalizeFolderPath(containingFolder.wstring());
        const std::wstring targetPath = NormalizeFolderPath(resolvedPath.wstring());

        // Navigate to the containing folder and mark the dropped file as the pending
        // viewer target; TryOpenPendingStartupViewerPath opens it once enumerated.
        pendingStartupViewerPath_ = targetPath;
        if (browserModel_ && FolderPathsEqual(browserModel_->FolderPath(), folderPath))
        {
            // Already browsing this folder: open immediately if the item exists.
            const int modelIndex = browserModel_->FindItemIndexByPath(targetPath);
            if (modelIndex >= 0)
            {
                pendingStartupViewerPath_.clear();
                browserPaneController_->RestoreSelectionByFilePaths({targetPath}, targetPath);
                OpenItemInViewer(modelIndex, ShouldDefaultViewerToSecondaryMonitor());
            }
            return;
        }

        LoadFolderAsync(folderPath);
    }

    void MainWindow::HandleExternalLaunchPath(const std::wstring& path)
    {
        if (path.empty() || !hwnd_)
        {
            return;
        }

        // Bring the existing window forward.
        if (IsIconic(hwnd_))
        {
            ShowWindow(hwnd_, SW_RESTORE);
        }
        SetForegroundWindow(hwnd_);

        std::error_code error;
        const fs::path resolvedPath(path);
        if (fs::is_directory(resolvedPath, error) && !error)
        {
            LoadFolderAsync(NormalizeFolderPath(resolvedPath.wstring()));
            return;
        }

        // A file path opens in the viewer.
        OpenViewerAtPath(path);
    }

    bool MainWindow::TranslateAcceleratorMessage(MSG* message)
    {
        if (!hwnd_ || !message)
        {
            return false;
        }

        // Do not translate accelerators when the message is destined for the viewer
        // window or one of its descendants. The viewer has its own keyboard handling
        // (notably for VK_DELETE) and must not be pre-empted by the main window's
        // browser-pane accelerators.
        if (viewerWindow_ && viewerWindow_->IsOpen())
        {
            const HWND viewerHwnd = viewerWindow_->Hwnd();
            if (viewerHwnd && message->hwnd
                && (message->hwnd == viewerHwnd || IsChild(viewerHwnd, message->hwnd)))
            {
                return false;
            }
        }

        if (shortcutReferenceWindow_ && IsWindow(shortcutReferenceWindow_)
            && message->hwnd
            && (message->hwnd == shortcutReferenceWindow_ || IsChild(shortcutReferenceWindow_, message->hwnd))
            && IsDialogMessageW(shortcutReferenceWindow_, message))
        {
            return true;
        }

        if ((message->message == WM_KEYDOWN || message->message == WM_SYSKEYDOWN)
            && message->hwnd
            && (message->hwnd == hwnd_ || IsChild(hwnd_, message->hwnd))
            && browserPaneController_
            && browserPaneController_->HandleNavigationKey(message->message,
                                                            message->wParam,
                                                            message->lParam))
        {
            return true;
        }

        if (!accelerators_)
        {
            return false;
        }

        // Do not translate the Escape accelerator when a folder drag is active.
        // The drag operation should have exclusive control over the Escape key to cancel itself.
        if (treeFolderDragActive_ && message->message == WM_KEYDOWN && message->wParam == VK_ESCAPE)
        {
            return false;
        }

        if ((message->message == WM_KEYDOWN || message->message == WM_SYSKEYDOWN)
            && message->wParam == VK_ESCAPE
            && message->hwnd
            && (message->hwnd == hwnd_ || IsChild(hwnd_, message->hwnd))
            && viewerWindow_
            && viewerWindow_->IsOpen())
        {
            const HWND viewerHwnd = viewerWindow_->Hwnd();
            if (viewerHwnd && PostMessageW(viewerHwnd, WM_CLOSE, 0, 0))
            {
                return true;
            }
        }

        // Preserve standard text-edit behavior in edit/rich-edit controls.
        if (message->message == WM_KEYDOWN
            && IsTextInputControlWindow(message->hwnd)
            && (message->wParam == VK_BACK || message->wParam == VK_DELETE
                || ((message->wParam == static_cast<WPARAM>('A')
                     || message->wParam == static_cast<WPARAM>('C')
                     || message->wParam == static_cast<WPARAM>('V')
                     || message->wParam == static_cast<WPARAM>('X')
                     || message->wParam == static_cast<WPARAM>('Y')
                     || message->wParam == static_cast<WPARAM>('Z'))
                    && (GetKeyState(VK_CONTROL) & 0x8000) != 0)))
        {
            return false;
        }

        return TranslateAcceleratorW(hwnd_, accelerators_, message) != 0;
    }

    bool MainWindow::RegisterWindowClass() const
    {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = &MainWindow::WindowProc;
        wc.hInstance = instance_;
        wc.lpszClassName = kWindowClassName;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
        wc.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(IDI_HYPERBROWSE));
        wc.hIconSm = static_cast<HICON>(
            LoadImageW(instance_, MAKEINTRESOURCEW(IDI_HYPERBROWSE),
                       IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                       GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));

        return RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    }

    bool MainWindow::CreateAccelerators()
    {
        if (accelerators_)
        {
            DestroyAcceleratorTable(accelerators_);
            accelerators_ = nullptr;
        }

        if (!kShortcutCatalogValid)
        {
            return false;
        }

        std::vector<ACCEL> accelerators;
        accelerators.reserve(MainWindowShortcuts().size());
        for (const ShortcutDefinition& shortcut : MainWindowShortcuts())
        {
            accelerators.push_back(ACCEL{
                static_cast<BYTE>(FVIRTKEY | shortcut.modifiers),
                shortcut.virtualKey,
                static_cast<WORD>(shortcut.commandId)});
        }

        accelerators_ = CreateAcceleratorTableW(accelerators.data(), static_cast<int>(accelerators.size()));
        return accelerators_ != nullptr;
    }

    bool MainWindow::CreateMenuBar()
    {
        menu_ = CreateMenu();
        fileMenu_ = CreatePopupMenu();
        editMenu_ = CreatePopupMenu();
        viewMenu_ = CreatePopupMenu();
        helpMenu_ = CreatePopupMenu();
        openRecentFolderMenu_ = CreatePopupMenu();
        copySelectionToMenu_ = CreatePopupMenu();
        moveSelectionToMenu_ = CreatePopupMenu();
        HMENU fileMetadataMenu = CreatePopupMenu();
        HMENU fileOrganizeMenu = CreatePopupMenu();
        HMENU fileConvertMenu = CreatePopupMenu();
        HMENU batchConvertSelectionMenu = CreatePopupMenu();
        HMENU batchConvertFolderMenu = CreatePopupMenu();
        HMENU ratingMenu = CreatePopupMenu();
        HMENU viewMenu = viewMenu_;
        HMENU editMenu = editMenu_;
        HMENU behaviorSettingsMenu = CreatePopupMenu();
        HMENU sortMenu = CreatePopupMenu();
        HMENU thumbnailSizeMenu = CreatePopupMenu();
        HMENU slideshowMenu = CreatePopupMenu();
        HMENU appTextSizeMenu = CreatePopupMenu();
        HMENU viewerMenu = CreatePopupMenu();
        HMENU viewerMouseWheelMenu = CreatePopupMenu();
        HMENU viewerOverlayTextSizeMenu = CreatePopupMenu();
        HMENU pairedRawJpegViewerMenu = CreatePopupMenu();
        HMENU themeMenu = CreatePopupMenu();
        HMENU advancedViewMenu = CreatePopupMenu();
        HMENU performanceMenu = CreatePopupMenu();
        HMENU performanceProfileMenu = CreatePopupMenu();
        HMENU diagnosticsMenu = CreatePopupMenu();
        HMENU helpMenu = helpMenu_;

        if (!menu_ || !fileMenu_ || !editMenu_ || !viewMenu_ || !helpMenu_ || !openRecentFolderMenu_ || !copySelectionToMenu_ || !moveSelectionToMenu_ || !fileMetadataMenu || !fileOrganizeMenu || !fileConvertMenu || !batchConvertSelectionMenu || !batchConvertFolderMenu || !ratingMenu || !behaviorSettingsMenu || !sortMenu || !thumbnailSizeMenu || !slideshowMenu || !appTextSizeMenu || !viewerMenu || !viewerMouseWheelMenu || !viewerOverlayTextSizeMenu || !pairedRawJpegViewerMenu || !themeMenu || !advancedViewMenu || !performanceMenu || !performanceProfileMenu || !diagnosticsMenu)
        {
            return false;
        }

        AppendMenuW(fileMenu_, MF_STRING, ID_FILE_OPEN_FOLDER, L"&Open Folder...\tCtrl+O");
        AppendMenuW(fileMenu_, MF_POPUP, reinterpret_cast<UINT_PTR>(openRecentFolderMenu_), L"Open &Recent Folder");
        AppendMenuW(fileMenu_, MF_STRING, ID_FILE_REFRESH_TREE, L"Refresh Folder &Tree\tF5");
        AppendMenuW(fileMenu_, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(fileMenu_, MF_STRING, ID_FILE_TOGGLE_CURRENT_FOLDER_FAVORITE_DESTINATION, L"Add Current Folder to Favorite &Destinations");
        AppendMenuW(fileMenu_, MF_STRING, ID_FILE_CLEAR_FAVORITE_DESTINATIONS, L"Clear All Favorite &Destinations");
        AppendMenuW(fileMenu_, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(fileMenu_, MF_STRING, ID_FILE_OPEN_SELECTED, L"&Open");
        AppendMenuW(fileMenu_, MF_STRING, ID_FILE_COMPARE_SELECTED, L"&Compare Selected");
        AppendMenuW(fileMenu_, MF_STRING, ID_FILE_VIEW_ON_SECONDARY_MONITOR, L"View on Secondary &Monitor");
        AppendMenuW(fileMenu_, MF_STRING, ID_FILE_IMAGE_INFORMATION, L"Image &Information\tCtrl+I");
        AppendMenuW(fileMenu_, MF_STRING, ID_FILE_PROPERTIES, L"P&roperties\tAlt+Enter");
        AppendMenuW(fileMenu_, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(fileMenu_, MF_STRING, ID_FILE_REVEAL_IN_EXPLORER, L"Reveal in &Explorer\tCtrl+E");
        AppendMenuW(fileMenu_, MF_STRING, ID_FILE_OPEN_CONTAINING_FOLDER, L"Open Containing &Folder");
        AppendMenuW(editMenu, MF_STRING, ID_EDIT_UNDO, L"&Undo\tCtrl+Z");
        AppendMenuW(editMenu, MF_STRING, ID_EDIT_REDO, L"&Redo\tCtrl+Y");
        AppendMenuW(editMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(editMenu, MF_STRING, ID_EDIT_CUT, L"Cu&t\tCtrl+X");
        AppendMenuW(editMenu, MF_STRING, ID_FILE_COPY_FILES_TO_CLIPBOARD, L"&Copy\tCtrl+C");
        AppendMenuW(editMenu, MF_STRING, ID_FILE_COPY_IMAGE_PIXELS, L"Copy &Image\tCtrl+Shift+I");
        AppendMenuW(editMenu, MF_STRING, ID_FILE_PASTE_FILES, L"&Paste\tCtrl+V");
        AppendMenuW(editMenu, MF_STRING, ID_FILE_COPY_PATH, L"Copy Pat&h\tCtrl+Shift+C");
        AppendMenuW(editMenu, MF_STRING, ID_FILE_SELECT_ALL, L"Select &All\tCtrl+A");
        AppendMenuW(editMenu, MF_STRING, ID_FILE_QUICK_SEND_MOVE, L"Quick Send &Move\tF7");
        AppendMenuW(editMenu, MF_STRING, ID_FILE_QUICK_SEND_COPY, L"Quick Send &Copy\tF8");
        AppendMenuW(editMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(ratingMenu, MF_STRING, ID_FILE_SET_RATING_0, L"&Clear Rating");
        AppendMenuW(ratingMenu, MF_STRING, ID_FILE_SET_RATING_1, L"&1 Star");
        AppendMenuW(ratingMenu, MF_STRING, ID_FILE_SET_RATING_2, L"&2 Stars");
        AppendMenuW(ratingMenu, MF_STRING, ID_FILE_SET_RATING_3, L"&3 Stars");
        AppendMenuW(ratingMenu, MF_STRING, ID_FILE_SET_RATING_4, L"&4 Stars");
        AppendMenuW(ratingMenu, MF_STRING, ID_FILE_SET_RATING_5, L"&5 Stars");
        AppendMenuW(fileMetadataMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(ratingMenu), L"Set &Rating");
        AppendMenuW(fileMetadataMenu, MF_STRING, ID_FILE_EDIT_TAGS, L"Edit &Tags...");
        AppendMenuW(editMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(fileMetadataMenu), L"&Metadata");

        AppendMenuW(fileOrganizeMenu, MF_STRING, ID_FILE_RENAME_SELECTED, L"Re&name...\tF2");
        AppendMenuW(fileOrganizeMenu, MF_STRING, ID_FILE_BATCH_RENAME_SELECTION, L"Batch R&ename...");
        AppendMenuW(fileOrganizeMenu, MF_STRING, ID_FILE_DUPLICATE_SELECTION, L"Dup&licate\tCtrl+D");
        AppendMenuW(fileOrganizeMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(fileOrganizeMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(copySelectionToMenu_), L"Cop&y Selection To");
        AppendMenuW(fileOrganizeMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(moveSelectionToMenu_), L"Mo&ve Selection To");
        AppendMenuW(fileOrganizeMenu, MF_STRING, ID_FILE_TOGGLE_PAIRED_RAW_JPEG_OPERATIONS, L"Include Paired &RAW+JPEG");
        AppendMenuW(fileOrganizeMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(fileOrganizeMenu, MF_STRING, ID_FILE_DELETE_SELECTION, L"&Delete\tDel");
        AppendMenuW(fileOrganizeMenu, MF_STRING, ID_FILE_DELETE_SELECTION_PERMANENT, L"Delete &Permanently\tShift+Del");
        AppendMenuW(editMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(fileOrganizeMenu), L"&Organize");

        AppendMenuW(batchConvertSelectionMenu, MF_STRING, ID_FILE_BATCH_CONVERT_SELECTION_JPEG, L"Selection to &JPEG");
        AppendMenuW(batchConvertSelectionMenu, MF_STRING, ID_FILE_BATCH_CONVERT_SELECTION_PNG, L"Selection to &PNG");
        AppendMenuW(batchConvertSelectionMenu, MF_STRING, ID_FILE_BATCH_CONVERT_SELECTION_TIFF, L"Selection to &TIFF");
        AppendMenuW(batchConvertFolderMenu, MF_STRING, ID_FILE_BATCH_CONVERT_FOLDER_JPEG, L"Folder to JPE&G");
        AppendMenuW(batchConvertFolderMenu, MF_STRING, ID_FILE_BATCH_CONVERT_FOLDER_PNG, L"Folder to P&NG");
        AppendMenuW(batchConvertFolderMenu, MF_STRING, ID_FILE_BATCH_CONVERT_FOLDER_TIFF, L"Folder to TIF&F");
        AppendMenuW(fileConvertMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(batchConvertSelectionMenu), L"Batch Convert &Selection");
        AppendMenuW(fileConvertMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(batchConvertFolderMenu), L"Batch Convert &Folder");
        AppendMenuW(fileConvertMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(fileConvertMenu, MF_STRING, ID_FILE_ROTATE_JPEG_LEFT, L"Adjust JPEG Orientation &Left");
        AppendMenuW(fileConvertMenu, MF_STRING, ID_FILE_ROTATE_JPEG_RIGHT, L"Adjust JPEG Orientation &Right");
        AppendMenuW(fileConvertMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(fileConvertMenu, MF_STRING, ID_FILE_BATCH_CONVERT_CANCEL, L"&Cancel Batch Convert");
        AppendMenuW(fileMenu_, MF_POPUP, reinterpret_cast<UINT_PTR>(fileConvertMenu), L"&Convert");

        AppendMenuW(fileMenu_, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(fileMenu_, MF_STRING, ID_FILE_ESCAPE, closeMainWindowOnEscape_ ? L"&Close\tEsc" : L"&Minimize\tEsc");
        AppendMenuW(fileMenu_, MF_STRING, ID_FILE_EXIT, L"E&xit");

        AppendMenuW(viewMenu, MF_STRING, ID_VIEW_THUMBNAILS, L"&Thumbnail Mode\tCtrl+1");
        AppendMenuW(viewMenu, MF_STRING, ID_VIEW_DETAILS, L"&Details Mode\tCtrl+2");
        AppendMenuW(viewMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(viewMenu, MF_STRING, ID_VIEW_NAVIGATE_BACK_FOLDER, L"Navigate &Back\tBackspace / Alt+Left");
        AppendMenuW(viewMenu, MF_STRING, ID_VIEW_NAVIGATE_FORWARD_FOLDER, L"Navigate &Forward\tAlt+Right");
        AppendMenuW(viewMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(sortMenu, MF_STRING, ID_VIEW_SORT_FILENAME, L"By &Filename");
        AppendMenuW(sortMenu, MF_STRING, ID_VIEW_SORT_MODIFIED, L"By &Modified Date");
        AppendMenuW(sortMenu, MF_STRING, ID_VIEW_SORT_SIZE, L"By File &Size");
        AppendMenuW(sortMenu, MF_STRING, ID_VIEW_SORT_DIMENSIONS, L"By &Dimensions");
        AppendMenuW(sortMenu, MF_STRING, ID_VIEW_SORT_TYPE, L"By &Type");
        AppendMenuW(sortMenu, MF_STRING, ID_VIEW_SORT_DATETAKEN, L"By Date &Taken");
        AppendMenuW(sortMenu, MF_STRING, ID_VIEW_SORT_RATING, L"By &Rating");
        AppendMenuW(sortMenu, MF_STRING, ID_VIEW_SORT_TAGS, L"By Ta&gs");
        AppendMenuW(sortMenu, MF_STRING, ID_VIEW_SORT_RANDOM, L"By &Random");
        AppendMenuW(sortMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(sortMenu, MF_STRING, ID_VIEW_SORT_DIRECTION, L"&Descending");
        AppendMenuW(viewMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(sortMenu), L"&Sort By");
        AppendMenuW(thumbnailSizeMenu, MF_STRING, ID_VIEW_THUMBNAIL_SIZE_INCREASE, L"Increase Size\t+ / =");
        AppendMenuW(thumbnailSizeMenu, MF_STRING, ID_VIEW_THUMBNAIL_SIZE_DECREASE, L"Decrease Size\t- / _");
        AppendMenuW(thumbnailSizeMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(thumbnailSizeMenu, MF_STRING, ID_VIEW_THUMBNAIL_SIZE_96, L"&96 px");
        AppendMenuW(thumbnailSizeMenu, MF_STRING, ID_VIEW_THUMBNAIL_SIZE_128, L"1&28 px");
        AppendMenuW(thumbnailSizeMenu, MF_STRING, ID_VIEW_THUMBNAIL_SIZE_160, L"1&60 px");
        AppendMenuW(thumbnailSizeMenu, MF_STRING, ID_VIEW_THUMBNAIL_SIZE_192, L"1&92 px");
        AppendMenuW(thumbnailSizeMenu, MF_STRING, ID_VIEW_THUMBNAIL_SIZE_256, L"2&56 px");
        AppendMenuW(thumbnailSizeMenu, MF_STRING, ID_VIEW_THUMBNAIL_SIZE_320, L"3&20 px");
        AppendMenuW(thumbnailSizeMenu, MF_STRING, ID_VIEW_THUMBNAIL_SIZE_360, L"3&60 px");
        AppendMenuW(thumbnailSizeMenu, MF_STRING, ID_VIEW_THUMBNAIL_SIZE_420, L"4&20 px");
        AppendMenuW(thumbnailSizeMenu, MF_STRING, ID_VIEW_THUMBNAIL_SIZE_480, L"4&80 px");
        AppendMenuW(thumbnailSizeMenu, MF_STRING, ID_VIEW_THUMBNAIL_SIZE_560, L"5&60 px");
        AppendMenuW(thumbnailSizeMenu, MF_STRING, ID_VIEW_THUMBNAIL_SIZE_640, L"6&40 px");
        AppendMenuW(slideshowMenu, MF_STRING, ID_VIEW_SLIDESHOW_SELECTION, L"From &Selection\tCtrl+Shift+S");
        AppendMenuW(slideshowMenu, MF_STRING, ID_VIEW_SLIDESHOW_FOLDER, L"From &Folder\tCtrl+Shift+F");
        AppendMenuW(slideshowMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(viewMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(viewMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(slideshowMenu), L"S&lideshow");

        AppendMenuW(performanceMenu, MF_STRING, ID_VIEW_PERSISTENT_THUMBNAIL_CACHE_MANAGER, L"Persistent Cache S&tats and Cleanup...");

        AppendMenuW(advancedViewMenu, MF_STRING, ID_FILE_ASSOCIATIONS, L"File &Associations...");
        AppendMenuW(viewMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(advancedViewMenu), L"&Integration");

        AppendMenuW(helpMenu, MF_STRING, ID_HELP_USER_GUIDE, L"&User Guide\tF1");
        AppendMenuW(helpMenu, MF_STRING, ID_HELP_KEYBOARD_SHORTCUTS, L"&Keyboard Shortcuts...");
        AppendMenuW(helpMenu, MF_STRING, ID_HELP_ABOUT, L"&About");
        AppendMenuW(viewMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(performanceMenu), L"&Performance");
        AppendMenuW(diagnosticsMenu, MF_STRING, ID_HELP_DIAGNOSTICS_SNAPSHOT, L"&Snapshot\tCtrl+Shift+D");
        AppendMenuW(diagnosticsMenu, MF_STRING, ID_HELP_DIAGNOSTICS_RESET, L"&Reset\tCtrl+Shift+X");
        AppendMenuW(viewMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(diagnosticsMenu), L"&Diagnostics");
        AppendMenuW(viewMenu, MF_STRING, ID_VIEW_SETTINGS, L"&Settings...\tCtrl+Shift+T");

        AppendMenuW(menu_, MF_POPUP, reinterpret_cast<UINT_PTR>(fileMenu_), L"&File");
        AppendMenuW(menu_, MF_POPUP, reinterpret_cast<UINT_PTR>(editMenu), L"&Edit");
        AppendMenuW(menu_, MF_POPUP, reinterpret_cast<UINT_PTR>(viewMenu), L"&View");
        AppendMenuW(menu_, MF_POPUP, reinterpret_cast<UINT_PTR>(helpMenu), L"&Help");

        RefreshPersistentMenuOwnerDraw();
        commandBarMenuButtons_[0].label = L"File";
        commandBarMenuButtons_[0].menu = fileMenu_;
        commandBarMenuButtons_[1].label = L"Edit";
        commandBarMenuButtons_[1].menu = editMenu_;
        commandBarMenuButtons_[2].label = L"View";
        commandBarMenuButtons_[2].menu = viewMenu_;
        commandBarMenuButtons_[3].label = L"Help";
        commandBarMenuButtons_[3].menu = helpMenu_;

        SetMenu(hwnd_, nullptr);
        DrawMenuBar(hwnd_);
        return true;
    }

    bool MainWindow::CreateChildWindows()
    {
        RebuildAppTextFonts();
        const HFONT defaultGuiFont = appTextUiFont_
            ? appTextUiFont_
            : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

        toolbarIconLibrary_ = std::make_unique<ToolbarIconLibrary>();
        if (toolbarIconLibrary_ && !toolbarIconLibrary_->Initialize())
        {
            util::LogError(L"Toolbar SVG icon library failed to initialize.");
        }

        InitToolbarItems();

        filterEdit_ = CreateWindowExW(
            0,
            L"EDIT",
            L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            0, 0, 120, kToolbarFilterEditHeight,
            hwnd_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_ACTION_FILTER_EDIT)),
            instance_,
            nullptr);

        if (filterEdit_)
        {
            SendMessageW(filterEdit_, WM_SETFONT, reinterpret_cast<WPARAM>(defaultGuiFont), TRUE);
            SendMessageW(filterEdit_, EM_LIMITTEXT, 260, 0);
            SendMessageW(filterEdit_, EM_SETCUEBANNER, FALSE, reinterpret_cast<LPARAM>(L"Filter names, rating:>=3, tag:pick"));
        }

        tooltipControl_ = CreateWindowExW(
            WS_EX_TOPMOST,
            TOOLTIPS_CLASSW,
            nullptr,
            WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
            CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
            hwnd_,
            nullptr,
            instance_,
            nullptr);
        if (tooltipControl_)
        {
            SendMessageW(tooltipControl_, WM_SETFONT, reinterpret_cast<WPARAM>(defaultGuiFont), TRUE);
            SendMessageW(tooltipControl_, TTM_SETDELAYTIME, TTDT_INITIAL, MAKELPARAM(400, 0));
            SendMessageW(tooltipControl_, TTM_SETDELAYTIME, TTDT_RESHOW, MAKELPARAM(100, 0));
            SendMessageW(tooltipControl_,
                         TTM_SETMAXTIPWIDTH,
                         0,
                         hyperbrowse::util::ScaleAppTextDimension(kToolbarTooltipMaxWidth, appTextSize_));

            for (int index = 0; index < static_cast<int>(toolbarItems_.size()); ++index)
            {
                TTTOOLINFOW toolInfo{};
                toolInfo.cbSize = sizeof(toolInfo);
                toolInfo.uFlags = TTF_SUBCLASS;
                toolInfo.hwnd = hwnd_;
                toolInfo.uId = static_cast<UINT_PTR>(index);
                toolInfo.rect = toolbarItems_[static_cast<std::size_t>(index)].rect;
                toolInfo.lpszText = LPSTR_TEXTCALLBACKW;
                SendMessageW(tooltipControl_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&toolInfo));
            }

            TTTOOLINFOW histogramToolInfo{};
            histogramToolInfo.cbSize = sizeof(histogramToolInfo);
            histogramToolInfo.uFlags = TTF_SUBCLASS;
            histogramToolInfo.hwnd = hwnd_;
            histogramToolInfo.uId = kDetailsPanelHistogramTooltipId;
            histogramToolInfo.rect = detailsPanelHistogramRect_;
            histogramToolInfo.lpszText = LPSTR_TEXTCALLBACKW;
            detailsPanelHistogramTooltipAdded_ = SendMessageW(tooltipControl_,
                                                               TTM_ADDTOOLW,
                                                               0,
                                                               reinterpret_cast<LPARAM>(&histogramToolInfo)) != FALSE;
        }

        treePane_ = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            WC_TREEVIEWW,
            L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT | TVS_SHOWSELALWAYS | TVS_EDITLABELS,
            0, 0, 100, 100,
            hwnd_,
            nullptr,
            instance_,
            nullptr);

        if (tooltipControl_ && treePane_)
        {
            TTTOOLINFOW toolInfo{};
            toolInfo.cbSize = sizeof(toolInfo);
            toolInfo.uFlags = TTF_IDISHWND;
            toolInfo.hwnd = hwnd_;
            toolInfo.uId = reinterpret_cast<UINT_PTR>(treePane_);
            toolInfo.lpszText = LPSTR_TEXTCALLBACKW;
            SendMessageW(tooltipControl_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&toolInfo));
            SetWindowSubclass(treePane_, &MainWindow::FolderTreeTooltipSubclassProc, 1, reinterpret_cast<DWORD_PTR>(this));
        }

        statusBar_ = CreateWindowExW(
            0,
            L"STATIC",
            nullptr,
            WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
            0, 0, 0, 0,
            hwnd_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kStatusStripControlId)),
            instance_,
            nullptr);

        if (!detailsPanelRichEditModule_)
        {
            detailsPanelRichEditModule_ = LoadLibraryW(L"Msftedit.dll");
        }

        const DWORD detailsPanelTextStyle = WS_CHILD | (detailsStripVisible_ ? WS_VISIBLE : 0) | WS_VSCROLL
            | ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | ES_NOHIDESEL;
        detailsPanelText_ = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            detailsPanelRichEditModule_ ? MSFTEDIT_CLASS : L"EDIT",
            L"",
            detailsPanelTextStyle,
            0, 0, 100, 100,
            hwnd_,
            nullptr,
            instance_,
            nullptr);

        if (!detailsPanelText_ && detailsPanelRichEditModule_)
        {
            FreeLibrary(detailsPanelRichEditModule_);
            detailsPanelRichEditModule_ = nullptr;
            detailsPanelText_ = CreateWindowExW(
                WS_EX_CLIENTEDGE,
                L"EDIT",
                L"",
                detailsPanelTextStyle,
                0, 0, 100, 100,
                hwnd_,
                nullptr,
                instance_,
                nullptr);
        }

            quickAccessScrollBar_ = CreateWindowExW(
                0,
                L"SCROLLBAR",
                nullptr,
                WS_CHILD | WS_TABSTOP | SBS_VERT,
                0, 0, 0, 0,
                hwnd_,
                nullptr,
                instance_,
                nullptr);

            if (!filterEdit_ || !treePane_ || !statusBar_ || !detailsPanelText_ || !quickAccessScrollBar_)
        {
            util::LogLastError(L"CreateChildWindows");
            return false;
        }

        SendMessageW(statusBar_, WM_SETFONT, reinterpret_cast<WPARAM>(defaultGuiFont), TRUE);

        SendMessageW(detailsPanelText_, WM_SETFONT, reinterpret_cast<WPARAM>(detailsPanelBodyFont_), TRUE);
        RefreshDetailsPanelBodyPresentation();

        detailsPanelThumbnailScheduler_ = std::make_unique<services::ThumbnailScheduler>(
            thumbnailCacheCapacityOverrideBytes_,
            0,
            resourceProfile_);
        if (detailsPanelThumbnailScheduler_)
        {
            detailsPanelThumbnailScheduler_->BindTargetWindow(hwnd_);
        }

        if (!browserPaneController_ || !browserPaneController_->Create(hwnd_))
        {
            util::LogError(L"Failed to create the browser pane control");
            return false;
        }

        browserPaneController_->SetUserMetadataStore(userMetadataStore_.get());

        browserPane_ = browserPaneController_->Hwnd();

        SendMessageW(treePane_, WM_SETFONT, reinterpret_cast<WPARAM>(defaultGuiFont), TRUE);

        SHFILEINFOW shellInfo{};
        treeImageList_ = reinterpret_cast<HIMAGELIST>(SHGetFileInfoW(
            L"C:\\",
            FILE_ATTRIBUTE_DIRECTORY,
            &shellInfo,
            sizeof(shellInfo),
            SHGFI_SYSICONINDEX | SHGFI_SMALLICON));
        if (treeImageList_)
        {
            ImageList_SetBkColor(treeImageList_, CLR_NONE);
            TreeView_SetImageList(treePane_, treeImageList_, TVSIL_NORMAL);
        }

        browserPaneController_->SetModel(browserModel_.get());
        browserPaneController_->SetViewMode(browserMode_ == BrowserMode::Thumbnails
            ? browser::BrowserViewMode::Thumbnails
            : browser::BrowserViewMode::Details);
        browserPaneController_->SetDarkTheme(themeMode_ == ThemeMode::Dark);
        ApplyRawJpegPairingSettings();
        ApplyThumbnailDisplaySettings();
        decode::SetNvJpegAccelerationEnabled(nvJpegEnabled_);
        decode::SetLibRawOutOfProcessEnabled(libRawOutOfProcessEnabled_);

        InitializeFolderTree();
        bool startupFolderLoadQueued = false;
        if (!startupFolderPath_.empty())
        {
            std::error_code error;
            if (fs::is_directory(fs::path(startupFolderPath_), error) && !error)
            {
                pendingStartupSelectionPath_.clear();
                if (pendingStartupViewerPath_.empty() && !startupSelectedImagePath_.empty())
                {
                    const std::wstring startupImageParentPath = NormalizeFolderPath(fs::path(startupSelectedImagePath_).parent_path().wstring());
                    if (!startupImageParentPath.empty() && FolderPathsEqual(startupImageParentPath, startupFolderPath_))
                    {
                        pendingStartupSelectionPath_ = startupSelectedImagePath_;
                    }
                }

                LoadFolderAsync(startupFolderPath_);
                startupFolderLoadQueued = true;
            }
        }
        if (!startupFolderLoadQueued)
        {
            RefreshBrowserPane();
        }
        UpdateStatusText();
        UpdateToolbarItemStates();
        UpdateDetailsPanel();
        LayoutChildren();
        return true;
    }

    void MainWindow::ApplyThumbnailDisplaySettings()
    {
        if (!browserPaneController_)
        {
            return;
        }

        browserPaneController_->SetThumbnailSizePreset(thumbnailSizePreset_);
        browserPaneController_->SetCompactThumbnailLayout(compactThumbnailLayout_);
        browserPaneController_->SetThumbnailDetailsVisible(thumbnailDetailsVisible_);
        browserPaneController_->SetAppTextSize(appTextSize_);
    }

    void MainWindow::InitializeFolderTree()
    {
        if (!treePane_)
        {
            return;
        }

        if (folderTreeEnumerationService_)
        {
            folderTreeEnumerationService_->CancelAll();
        }

        pendingFolderTreeEnumerationItems_.clear();
        pendingFolderTreeChildPresenceItems_.clear();
        pendingTreeSelectionPath_.clear();

        suppressTreeSelectionChange_ = true;
        TreeView_DeleteAllItems(treePane_);
        folderTreeNodes_.clear();
        PopulateSpecialFolderRoots();
        PopulateDriveRoots();
        suppressTreeSelectionChange_ = false;
        ShowSelectedFolderInTree();
    }

    void MainWindow::PopulateSpecialFolderRoots()
    {
        const KNOWNFOLDERID specialFolderIds[] = {
            FOLDERID_Desktop,
            FOLDERID_Documents,
            FOLDERID_Pictures,
        };

        for (const KNOWNFOLDERID& specialFolderId : specialFolderIds)
        {
            const std::wstring folderPath = TryGetKnownFolderPath(specialFolderId);
            if (folderPath.empty())
            {
                continue;
            }

            std::error_code error;
            if (!fs::is_directory(fs::path(folderPath), error) || error)
            {
                continue;
            }

            if (!FindChildFolderTreeItem(nullptr, folderPath))
            {
                InsertFolderTreeItem(TVI_ROOT, folderPath);
            }
        }
    }

    void MainWindow::PopulateDriveRoots()
    {
        const DWORD driveMask = GetLogicalDrives();
        for (wchar_t driveLetter = L'A'; driveLetter <= L'Z'; ++driveLetter)
        {
            const DWORD driveBit = 1UL << (driveLetter - L'A');
            if ((driveMask & driveBit) == 0)
            {
                continue;
            }

            std::wstring drivePath;
            drivePath.push_back(driveLetter);
            drivePath.append(L":\\");

            const UINT driveType = GetDriveTypeW(drivePath.c_str());
            if (driveType == DRIVE_NO_ROOT_DIR || driveType == DRIVE_UNKNOWN)
            {
                continue;
            }

            if (!FindChildFolderTreeItem(nullptr, drivePath))
            {
                InsertFolderTreeItem(TVI_ROOT, drivePath);
            }
        }
    }

    void MainWindow::RefreshFolderTree()
    {
        InitializeFolderTree();
    }

    HTREEITEM MainWindow::InsertFolderTreeItem(HTREEITEM parentItem,
                                               const std::wstring& folderPath,
                                               bool childrenKnown,
                                                   bool hasChildren,
                                                   bool requestPresence)
    {
        const std::wstring normalizedPath = NormalizeFolderPath(folderPath);
        if (!ShouldShowFolderInTree(normalizedPath))
        {
            return nullptr;
        }

        const ShellTreeItemInfo shellInfo = QueryShellTreeItemInfo(normalizedPath);

        auto nodeData = std::make_unique<FolderTreeNodeData>();
        nodeData->path = normalizedPath;
        nodeData->childrenKnown = childrenKnown;
        nodeData->hasChildren = hasChildren;
        nodeData->childrenLoaded = false;
        nodeData->childrenLoading = false;
        nodeData->childEnumerationRequestId = 0;
        nodeData->childPresenceLoading = false;
        nodeData->childPresenceRequestId = 0;
        FolderTreeNodeData* rawNodeData = nodeData.get();
        folderTreeNodes_.push_back(std::move(nodeData));

        TVINSERTSTRUCTW item{};
        item.hParent = parentItem;
        item.hInsertAfter = TVI_LAST;
        item.item.mask = TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_PARAM | TVIF_CHILDREN;
        item.item.pszText = const_cast<LPWSTR>(shellInfo.displayName.c_str());
        item.item.iImage = shellInfo.iconIndex;
        item.item.iSelectedImage = shellInfo.openIconIndex;
        item.item.lParam = reinterpret_cast<LPARAM>(rawNodeData);
        item.item.cChildren = childrenKnown && hasChildren ? 1 : 0;

        const HTREEITEM insertedItem = TreeView_InsertItem(treePane_, &item);
        if (insertedItem && childrenKnown && hasChildren)
        {
            AddFolderTreePlaceholder(insertedItem);
        }
        else if (insertedItem && !childrenKnown && requestPresence)
        {
            RequestFolderTreeChildPresence(std::vector<HTREEITEM>{insertedItem});
        }

        return insertedItem;
    }

    void MainWindow::AddFolderTreePlaceholder(HTREEITEM parentItem)
    {
        if (!treePane_ || !parentItem)
        {
            return;
        }

        HTREEITEM childItem = TreeView_GetChild(treePane_, parentItem);
        while (childItem)
        {
            if (!GetFolderTreeNodeData(childItem))
            {
                return;
            }

            childItem = TreeView_GetNextSibling(treePane_, childItem);
        }

        TVINSERTSTRUCTW placeholder{};
        placeholder.hParent = parentItem;
        placeholder.hInsertAfter = TVI_LAST;
        placeholder.item.mask = TVIF_TEXT;
        placeholder.item.pszText = const_cast<LPWSTR>(L"");
        TreeView_InsertItem(treePane_, &placeholder);
    }

    void MainWindow::RequestFolderTreeChildPresence(const std::vector<HTREEITEM>& items)
    {
        if (items.empty() || !folderTreeEnumerationService_)
        {
            return;
        }

        std::vector<HTREEITEM> pendingItems;
        std::vector<std::wstring> folderPaths;
        pendingItems.reserve(items.size());
        folderPaths.reserve(items.size());
        for (HTREEITEM item : items)
        {
            FolderTreeNodeData* nodeData = GetFolderTreeNodeData(item);
            if (!nodeData || nodeData->childrenKnown || nodeData->childPresenceLoading
                || nodeData->childrenLoading)
            {
                continue;
            }

            nodeData->childPresenceLoading = true;
            pendingItems.push_back(item);
            folderPaths.push_back(nodeData->path);
        }

        if (pendingItems.empty())
        {
            return;
        }

        const std::uint64_t requestId = folderTreeEnumerationService_->QueryChildDirectoryPresenceAsync(
            hwnd_,
            std::move(folderPaths));
        for (HTREEITEM item : pendingItems)
        {
            if (FolderTreeNodeData* nodeData = GetFolderTreeNodeData(item))
            {
                nodeData->childPresenceRequestId = requestId;
            }
        }
        pendingFolderTreeChildPresenceItems_[requestId] = std::move(pendingItems);
        UpdateStatusText();
    }

    void MainWindow::UpdateFolderTreeChildrenIndicator(HTREEITEM item)
    {
        FolderTreeNodeData* nodeData = GetFolderTreeNodeData(item);
        if (!nodeData)
        {
            return;
        }

        TVITEMW treeItem{};
        treeItem.mask = TVIF_CHILDREN;
        treeItem.hItem = item;
        treeItem.cChildren = nodeData->childrenKnown && nodeData->hasChildren ? 1 : 0;
        TreeView_SetItem(treePane_, &treeItem);
    }

    void MainWindow::RequestFolderTreeChildren(HTREEITEM item)
    {
        FolderTreeNodeData* nodeData = GetFolderTreeNodeData(item);
        if (!nodeData || nodeData->childrenLoaded || nodeData->childrenLoading || !folderTreeEnumerationService_)
        {
            return;
        }

        nodeData->childPresenceLoading = false;
        nodeData->childPresenceRequestId = 0;
        nodeData->childrenLoading = true;
        const std::uint64_t requestId = folderTreeEnumerationService_->EnumerateChildDirectoriesAsync(hwnd_, nodeData->path);
        nodeData->childEnumerationRequestId = requestId;
        pendingFolderTreeEnumerationItems_[requestId] = item;
        UpdateStatusText();
    }

    void MainWindow::ApplyFolderTreeChildren(HTREEITEM item,
                                             std::vector<services::FolderTreeChild> childFolders)
    {
        FolderTreeNodeData* nodeData = GetFolderTreeNodeData(item);
        if (!nodeData)
        {
            return;
        }

        nodeData->childrenKnown = true;
        nodeData->hasChildren = !childFolders.empty();
        nodeData->childPresenceLoading = false;
        nodeData->childPresenceRequestId = 0;
        nodeData->childrenLoaded = true;
        nodeData->childrenLoading = false;
        nodeData->childEnumerationRequestId = 0;
        UpdateFolderTreeChildrenIndicator(item);

        HTREEITEM childItem = TreeView_GetChild(treePane_, item);
        while (childItem)
        {
            HTREEITEM nextSibling = TreeView_GetNextSibling(treePane_, childItem);
            TreeView_DeleteItem(treePane_, childItem);
            childItem = nextSibling;
        }

        std::vector<HTREEITEM> childItems;
        childItems.reserve(childFolders.size());
        for (const services::FolderTreeChild& childFolder : childFolders)
        {
            const HTREEITEM insertedChildItem = InsertFolderTreeItem(item, childFolder.path, false, false, false);
            if (insertedChildItem)
            {
                childItems.push_back(insertedChildItem);
            }
        }

        RequestFolderTreeChildPresence(childItems);
    }

    void MainWindow::ShowSelectedFolderInTree()
    {
        if (!treePane_ || !browserModel_ || browserModel_->FolderPath().empty())
        {
            return;
        }

        SelectFolderInTree(browserModel_->FolderPath());
    }

    void MainWindow::SelectFolderInTree(const std::wstring& folderPath)
    {
        if (!treePane_ || folderPath.empty())
        {
            return;
        }

        pendingTreeSelectionPath_ = NormalizeFolderPath(folderPath);
        ContinueSelectingFolderInTree();
    }

    void MainWindow::ContinueSelectingFolderInTree()
    {
        if (!treePane_ || pendingTreeSelectionPath_.empty())
        {
            return;
        }

        const std::wstring normalizedPath = pendingTreeSelectionPath_;
        HTREEITEM currentItem = FindChildFolderTreeItem(nullptr, normalizedPath);
        if (currentItem)
        {
            suppressTreeSelectionChange_ = true;
            TreeView_SelectItem(treePane_, currentItem);
            TreeView_EnsureVisible(treePane_, currentItem);
            suppressTreeSelectionChange_ = false;
            pendingTreeSelectionPath_.clear();
            return;
        }

        const fs::path targetPath(normalizedPath);
        const std::wstring rootPath = NormalizeFolderPath(targetPath.root_path().wstring());
        if (rootPath.empty())
        {
            return;
        }

        currentItem = FindChildFolderTreeItem(nullptr, rootPath);
        if (!currentItem)
        {
            return;
        }

        if (!FolderPathsEqual(normalizedPath, rootPath))
        {
            fs::path currentPath(rootPath);
            for (const auto& segment : targetPath.relative_path())
            {
                if (segment.empty())
                {
                    continue;
                }

                currentPath /= segment;
                TreeView_Expand(treePane_, currentItem, TVE_EXPAND);

                FolderTreeNodeData* nodeData = GetFolderTreeNodeData(currentItem);
                if (!nodeData)
                {
                    return;
                }

                if (!nodeData->childrenLoaded)
                {
                    RequestFolderTreeChildren(currentItem);
                    return;
                }

                currentItem = FindChildFolderTreeItem(currentItem, currentPath.wstring());
                if (!currentItem)
                {
                    pendingTreeSelectionPath_.clear();
                    return;
                }
            }
        }

        suppressTreeSelectionChange_ = true;
        TreeView_SelectItem(treePane_, currentItem);
        TreeView_EnsureVisible(treePane_, currentItem);
        suppressTreeSelectionChange_ = false;
        pendingTreeSelectionPath_.clear();
    }

    HTREEITEM MainWindow::FindChildFolderTreeItem(HTREEITEM parentItem, const std::wstring& folderPath) const
    {
        const std::wstring normalizedPath = NormalizeFolderPath(folderPath);
        HTREEITEM currentItem = parentItem
            ? TreeView_GetChild(treePane_, parentItem)
            : TreeView_GetRoot(treePane_);
        while (currentItem)
        {
            FolderTreeNodeData* nodeData = GetFolderTreeNodeData(currentItem);
            if (nodeData && FolderPathsEqual(nodeData->path, normalizedPath))
            {
                return currentItem;
            }

            currentItem = TreeView_GetNextSibling(treePane_, currentItem);
        }

        return nullptr;
    }

    HTREEITEM MainWindow::FindFolderTreeItemByPath(const std::wstring& folderPath) const
    {
        if (!treePane_ || folderPath.empty())
        {
            return nullptr;
        }

        const std::wstring normalizedPath = NormalizeFolderPath(folderPath);
        std::function<HTREEITEM(HTREEITEM)> findItem = [&](HTREEITEM parentItem) -> HTREEITEM
        {
            HTREEITEM currentItem = parentItem
                ? TreeView_GetChild(treePane_, parentItem)
                : TreeView_GetRoot(treePane_);
            while (currentItem)
            {
                FolderTreeNodeData* nodeData = GetFolderTreeNodeData(currentItem);
                if (nodeData && FolderPathsEqual(nodeData->path, normalizedPath))
                {
                    return currentItem;
                }

                if (HTREEITEM descendant = findItem(currentItem))
                {
                    return descendant;
                }

                currentItem = TreeView_GetNextSibling(treePane_, currentItem);
            }

            return nullptr;
        };

        return findItem(nullptr);
    }

    void MainWindow::InsertFolderTreeFolderIfParentLoaded(const std::wstring& folderPath)
    {
        if (!treePane_ || folderPath.empty())
        {
            return;
        }

        const std::wstring normalizedPath = NormalizeFolderPath(folderPath);
        if (FindFolderTreeItemByPath(normalizedPath))
        {
            return;
        }

        const std::wstring parentPath = NormalizeFolderPath(fs::path(normalizedPath).parent_path().wstring());
        if (parentPath.empty())
        {
            return;
        }

        const HTREEITEM parentItem = FindFolderTreeItemByPath(parentPath);
        if (!parentItem)
        {
            return;
        }

        FolderTreeNodeData* parentNodeData = GetFolderTreeNodeData(parentItem);
        if (!parentNodeData || !parentNodeData->childrenLoaded)
        {
            return;
        }

        InsertFolderTreeItem(parentItem, normalizedPath);
    }

    MainWindow::FolderTreeNodeData* MainWindow::GetFolderTreeNodeData(HTREEITEM item) const
    {
        if (!treePane_ || !item)
        {
            return nullptr;
        }

        TVITEMW treeItem{};
        treeItem.mask = TVIF_PARAM;
        treeItem.hItem = item;
        if (TreeView_GetItem(treePane_, &treeItem) == FALSE)
        {
            return nullptr;
        }

        return reinterpret_cast<FolderTreeNodeData*>(treeItem.lParam);
    }

    std::wstring MainWindow::GetSelectedFolderTreePath() const
    {
        if (!treePane_)
        {
            return {};
        }

        const HTREEITEM selectedItem = TreeView_GetSelection(treePane_);
        const FolderTreeNodeData* nodeData = GetFolderTreeNodeData(selectedItem);
        return nodeData ? nodeData->path : std::wstring{};
    }

    LRESULT MainWindow::OnFolderTreeNotify(LPARAM lParam)
    {
        const auto* header = reinterpret_cast<const NMHDR*>(lParam);
        if (!header || header->hwndFrom != treePane_)
        {
            return 0;
        }

        switch (header->code)
        {
        case NM_RCLICK:
            return OnFolderTreeRightClick();
        case NM_CUSTOMDRAW:
        {
            auto* customDraw = reinterpret_cast<NMTVCUSTOMDRAW*>(lParam);
            if (customDraw->nmcd.dwDrawStage == CDDS_PREPAINT)
            {
                return CDRF_NOTIFYITEMDRAW;
            }

            if (customDraw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT
                && (customDraw->nmcd.uItemState & CDIS_SELECTED) == 0)
            {
                const HTREEITEM item = reinterpret_cast<HTREEITEM>(customDraw->nmcd.dwItemSpec);
                const FolderTreeNodeData* nodeData = GetFolderTreeNodeData(item);
                if (nodeData && IsFavoriteDestination(nodeData->path))
                {
                    customDraw->clrText = GetThemePalette().accent;
                }
            }

            return CDRF_DODEFAULT;
        }
        case TVN_BEGINDRAGW:
            return OnFolderTreeBeginDrag(*reinterpret_cast<const NMTREEVIEWW*>(lParam));
        case TVN_ITEMEXPANDINGW:
            return OnFolderTreeItemExpanding(*reinterpret_cast<const NMTREEVIEWW*>(lParam));
        case TVN_SELCHANGEDW:
            return OnFolderTreeSelectionChanged(*reinterpret_cast<const NMTREEVIEWW*>(lParam));
        case TVN_BEGINLABELEDITW:
            return OnFolderTreeBeginLabelEdit(*reinterpret_cast<const NMTVDISPINFOW*>(lParam));
        case TVN_ENDLABELEDITW:
            return OnFolderTreeEndLabelEdit(*reinterpret_cast<const NMTVDISPINFOW*>(lParam));
        default:
            return 0;
        }
    }

    void MainWindow::RelayFolderTreeTooltipEvent(UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (!tooltipControl_ || !treePane_)
        {
            return;
        }

        if (message == WM_MOUSEMOVE)
        {
            TRACKMOUSEEVENT tracking{};
            tracking.cbSize = sizeof(tracking);
            tracking.dwFlags = TME_LEAVE;
            tracking.hwndTrack = treePane_;
            TrackMouseEvent(&tracking);

            std::wstring hoveredPath;
            TVHITTESTINFO hitTest{};
            hitTest.pt = POINT{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            const HTREEITEM item = TreeView_HitTest(treePane_, &hitTest);
            if (item && (hitTest.flags & TVHT_ONITEM) != 0)
            {
                if (const FolderTreeNodeData* nodeData = GetFolderTreeNodeData(item);
                    nodeData && IsFavoriteDestination(nodeData->path)
                    && quickSendModel_.ShortcutForDestination(nodeData->path).has_value())
                {
                    hoveredPath = nodeData->path;
                }
            }

            if (!FolderPathsEqual(treeTooltipPath_, hoveredPath))
            {
                treeTooltipPath_ = std::move(hoveredPath);
                treeFolderTooltipText_.clear();
                SendMessageW(tooltipControl_, TTM_POP, 0, 0);
            }
        }
        else if (message == WM_MOUSELEAVE)
        {
            treeTooltipPath_.clear();
            treeFolderTooltipText_.clear();
            SendMessageW(tooltipControl_, TTM_POP, 0, 0);
        }

        MSG relayMessage{};
        relayMessage.hwnd = treePane_;
        relayMessage.message = message;
        relayMessage.wParam = wParam;
        relayMessage.lParam = lParam;
        SendMessageW(tooltipControl_, TTM_RELAYEVENT, 0, reinterpret_cast<LPARAM>(&relayMessage));
    }

    LRESULT CALLBACK MainWindow::FolderTreeTooltipSubclassProc(HWND hwnd,
                                                                 UINT message,
                                                                 WPARAM wParam,
                                                                 LPARAM lParam,
                                                                 UINT_PTR,
                                                                 DWORD_PTR refData)
    {
        auto* window = reinterpret_cast<MainWindow*>(refData);
        if (window && (message == WM_MOUSEMOVE || message == WM_MOUSELEAVE))
        {
            window->RelayFolderTreeTooltipEvent(message, wParam, lParam);
        }

        return DefSubclassProc(hwnd, message, wParam, lParam);
    }

    LRESULT CALLBACK MainWindow::QuickAccessShortcutEditSubclassProc(HWND hwnd,
                                                                       UINT message,
                                                                       WPARAM wParam,
                                                                       LPARAM lParam,
                                                                       UINT_PTR,
                                                                       DWORD_PTR)
    {
        const LRESULT result = DefSubclassProc(hwnd, message, wParam, lParam);
        if (message == WM_LBUTTONUP)
        {
            PostMessageW(hwnd, EM_SETSEL, 0, static_cast<LPARAM>(-1));
        }

        return result;
    }

    LRESULT MainWindow::OnFolderTreeSelectionChanged(const NMTREEVIEWW& treeView)
    {
        if (suppressTreeSelectionChange_)
        {
            return 0;
        }

        const FolderTreeNodeData* nodeData = GetFolderTreeNodeData(treeView.itemNew.hItem);
        if (!nodeData)
        {
            return 0;
        }

        if (!browserModel_ || !FolderPathsEqual(browserModel_->FolderPath(), nodeData->path))
        {
            LoadFolderAsync(nodeData->path);
        }

        return 0;
    }

    LRESULT MainWindow::OnFolderTreeItemExpanding(const NMTREEVIEWW& treeView)
    {
        if ((treeView.action & TVE_EXPAND) != 0)
        {
            RequestFolderTreeChildren(treeView.itemNew.hItem);
        }

        return 0;
    }

    LRESULT MainWindow::OnFolderTreeBeginLabelEdit(const NMTVDISPINFOW& dispInfo)
    {
        const FolderTreeNodeData* nodeData = GetFolderTreeNodeData(dispInfo.item.hItem);
        if (!nodeData || nodeData->path.empty())
        {
            return TRUE; // cancel edit
        }

        if (fileOperationActive_)
        {
            return TRUE; // cancel edit
        }

        // Roots (drives) cannot be renamed.
        if (!TreeView_GetParent(treePane_, dispInfo.item.hItem))
        {
            return TRUE;
        }

        return FALSE; // allow edit
    }

    LRESULT MainWindow::OnFolderTreeEndLabelEdit(const NMTVDISPINFOW& dispInfo)
    {
        // pszText is null when the user cancelled (Esc).
        if (!dispInfo.item.pszText)
        {
            return 0;
        }

        const FolderTreeNodeData* nodeData = GetFolderTreeNodeData(dispInfo.item.hItem);
        if (!nodeData || nodeData->path.empty())
        {
            return 0;
        }

        const std::wstring newLeafName = dispInfo.item.pszText;
        const std::wstring currentLeafName = fs::path(nodeData->path).filename().wstring();
        if (newLeafName.empty() || newLeafName == currentLeafName)
        {
            return 0; // no change; keep the old label
        }

        std::wstring validationError;
        if (!IsValidFolderName(newLeafName, &validationError))
        {
            MessageBoxW(hwnd_, validationError.c_str(), L"Rename Folder", MB_OK | MB_ICONWARNING);
            return 0;
        }

        const std::wstring folderPath = nodeData->path;
        activeTreeFolderRenamePath_ = folderPath;
        StartFileOperation(services::FileOperationType::Rename,
                           {folderPath},
                           {},
                           services::FileConflictPolicy::PromptShell,
                           {newLeafName});
        // Return 0 so the tree does not apply the label itself; the folder-watch /
        // operation-completion path will refresh the node with the real new name.
        return 0;
    }

    LRESULT MainWindow::OnFolderTreeRightClick()
    {
        if (!treePane_)
        {
            return 0;
        }

        POINT screenPoint{};
        if (!GetCursorPos(&screenPoint))
        {
            return 0;
        }

        POINT clientPoint = screenPoint;
        ScreenToClient(treePane_, &clientPoint);

        TVHITTESTINFO hitTest{};
        hitTest.pt = clientPoint;
        const HTREEITEM item = TreeView_HitTest(treePane_, &hitTest);
        if (!item || (hitTest.flags & TVHT_ONITEM) == 0)
        {
            return 0;
        }

        if (!GetFolderTreeNodeData(item) || !TreeView_GetParent(treePane_, item))
        {
            return 0;
        }

        ShowFolderTreeContextMenu(screenPoint, item);
        return 0;
    }

    LRESULT MainWindow::OnFolderTreeBeginDrag(const NMTREEVIEWW& treeView)
    {
        if (!treePane_ || fileOperationActive_)
        {
            return 0;
        }

        FolderTreeNodeData* nodeData = GetFolderTreeNodeData(treeView.itemNew.hItem);
        if (!nodeData || nodeData->path.empty() || !TreeView_GetParent(treePane_, treeView.itemNew.hItem))
        {
            return 0;
        }

        if (treeFolderDragActive_)
        {
            FinishFolderTreeDrag(false);
        }

        treeFolderDragActive_ = true;
        treeFolderDropAllowed_ = false;
        treeDragSourceItem_ = treeView.itemNew.hItem;
        treeDragHoverItem_ = nullptr;
        treeDragSourcePath_ = NormalizeFolderPath(nodeData->path);
        treeDragDestinationPath_.clear();

        TreeView_SelectDropTarget(treePane_, nullptr);

        int preferredHotspotX = 8;
        int preferredHotspotY = 8;
        treeDragImageList_ = nullptr;

        if (HIMAGELIST treeImageList = TreeView_GetImageList(treePane_, TVSIL_NORMAL))
        {
            int iconWidth = 0;
            int iconHeight = 0;
            if (ImageList_GetIconSize(treeImageList, &iconWidth, &iconHeight) != FALSE)
            {
                if (iconWidth > 0)
                {
                    preferredHotspotX = iconWidth / 2;
                }
                if (iconHeight > 0)
                {
                    preferredHotspotY = iconHeight / 2;
                }
            }

            TVITEMW dragItem{};
            dragItem.mask = TVIF_IMAGE | TVIF_SELECTEDIMAGE;
            dragItem.hItem = treeDragSourceItem_;
            if (TreeView_GetItem(treePane_, &dragItem) != FALSE)
            {
                const int dragImageIndex = dragItem.iSelectedImage >= 0 ? dragItem.iSelectedImage : dragItem.iImage;
                if (dragImageIndex >= 0)
                {
                    if (HICON dragIcon = ImageList_GetIcon(treeImageList, dragImageIndex, ILD_NORMAL))
                    {
                        const int dragImageWidth = std::max(1, preferredHotspotX * 2);
                        const int dragImageHeight = std::max(1, preferredHotspotY * 2);
                        treeDragImageList_ = ImageList_Create(dragImageWidth, dragImageHeight, ILC_COLOR32 | ILC_MASK, 1, 1);
                        if (treeDragImageList_)
                        {
                            if (ImageList_AddIcon(treeDragImageList_, dragIcon) == -1)
                            {
                                ImageList_Destroy(treeDragImageList_);
                                treeDragImageList_ = nullptr;
                            }
                        }
                        DestroyIcon(dragIcon);
                    }
                }
            }
        }

        if (!treeDragImageList_)
        {
            treeDragImageList_ = TreeView_CreateDragImage(treePane_, treeDragSourceItem_);
        }

        POINT dragScreenPoint = treeView.ptDrag;
        ClientToScreen(treePane_, &dragScreenPoint);

        if (treeDragImageList_)
        {
            int hotspotX = preferredHotspotX;
            int hotspotY = preferredHotspotY;

            int imageWidth = 0;
            int imageHeight = 0;
            if (ImageList_GetIconSize(treeDragImageList_, &imageWidth, &imageHeight) != FALSE)
            {
                if (imageWidth > 0)
                {
                    hotspotX = std::clamp(hotspotX, 0, imageWidth - 1);
                }
                if (imageHeight > 0)
                {
                    hotspotY = std::clamp(hotspotY, 0, imageHeight - 1);
                }
            }

            ImageList_BeginDrag(treeDragImageList_, 0, hotspotX, hotspotY);
            // ImageList_DragEnter expects coordinates relative to the window origin
            // (upper-left of the full window, not the client area).
            RECT windowRect{};
            GetWindowRect(hwnd_, &windowRect);
            POINT dragWindowPoint{
                dragScreenPoint.x - windowRect.left,
                dragScreenPoint.y - windowRect.top,
            };
            ImageList_DragEnter(hwnd_, dragWindowPoint.x, dragWindowPoint.y);
        }

        SetCapture(hwnd_);
        POINT dragWindowPoint = dragScreenPoint;
        ScreenToClient(hwnd_, &dragWindowPoint);
        UpdateFolderTreeDrag(dragWindowPoint);
        return 0;
    }

    void MainWindow::UpdateFolderTreeDrag(POINT windowPoint)
    {
        if (!treeFolderDragActive_)
        {
            return;
        }

        bool dragImageTemporarilyHidden = false;

        POINT screenPoint = windowPoint;
        ClientToScreen(hwnd_, &screenPoint);
        if (treeDragImageList_)
        {
            // DragMove expects coordinates relative to the window origin
            // (upper-left of the full window, not the client area).
            RECT windowRect{};
            GetWindowRect(hwnd_, &windowRect);
            ImageList_DragMove(screenPoint.x - windowRect.left, screenPoint.y - windowRect.top);

            // Hide the drag image while updating tree highlight state to avoid
            // paint artifacts from control redraws beneath the ghost image.
            ImageList_DragShowNolock(FALSE);
            dragImageTemporarilyHidden = true;
        }

        HTREEITEM hoverItem = nullptr;
        std::wstring destinationPath;
        bool dropAllowed = false;
        if (treePane_)
        {
            RECT treeRect{};
            GetClientRect(treePane_, &treeRect);

            POINT treePoint = screenPoint;
            ScreenToClient(treePane_, &treePoint);
            if (PtInRect(&treeRect, treePoint) != FALSE)
            {
                TVHITTESTINFO hitTest{};
                hitTest.pt = treePoint;
                const HTREEITEM item = TreeView_HitTest(treePane_, &hitTest);
                if (item && (hitTest.flags & TVHT_ONITEM) != 0)
                {
                    const FolderTreeNodeData* nodeData = GetFolderTreeNodeData(item);
                    if (nodeData && !nodeData->path.empty() && item != treeDragSourceItem_)
                    {
                        const std::wstring normalizedDestinationPath = NormalizeFolderPath(nodeData->path);
                        if (IsValidFolderTreeDropDestination(treeDragSourcePath_, normalizedDestinationPath))
                        {
                            hoverItem = item;
                            destinationPath = normalizedDestinationPath;
                            dropAllowed = true;
                        }
                    }
                }
            }
        }

        if (hoverItem != treeDragHoverItem_)
        {
            treeDragHoverItem_ = hoverItem;
            TreeView_SelectDropTarget(treePane_, treeDragHoverItem_);
        }

        if (dragImageTemporarilyHidden)
        {
            ImageList_DragShowNolock(TRUE);
        }

        treeFolderDropAllowed_ = dropAllowed;
        treeDragDestinationPath_ = dropAllowed ? std::move(destinationPath) : std::wstring{};
        SetCursor(LoadCursorW(nullptr, dropAllowed ? IDC_HAND : IDC_NO));
    }

    void MainWindow::FinishFolderTreeDrag(bool commitDrop)
    {
        if (!treeFolderDragActive_)
        {
            return;
        }

        const std::wstring sourcePath = treeDragSourcePath_;
        const std::wstring destinationPath = (commitDrop && treeFolderDropAllowed_)
            ? treeDragDestinationPath_
            : std::wstring{};

        treeFolderDragActive_ = false;
        treeFolderDropAllowed_ = false;
        treeDragSourceItem_ = nullptr;
        treeDragHoverItem_ = nullptr;
        treeDragSourcePath_.clear();
        treeDragDestinationPath_.clear();

        if (treePane_)
        {
            if (treeDragImageList_)
            {
                ImageList_DragShowNolock(FALSE);
            }
            TreeView_SelectDropTarget(treePane_, nullptr);
        }

        if (treeDragImageList_)
        {
            ImageList_DragLeave(hwnd_);
            ImageList_EndDrag();
            ImageList_Destroy(treeDragImageList_);
            treeDragImageList_ = nullptr;
        }

        if (GetCapture() == hwnd_)
        {
            ReleaseCapture();
        }

        if (commitDrop && !sourcePath.empty() && !destinationPath.empty())
        {
            StartFolderTreeMoveToDestination(sourcePath, destinationPath);
        }
    }

    void MainWindow::UpdateInternalSelectionDrag(POINT windowPoint)
    {
        if (dragMode_ != DragMode::QuickAccessInternal)
        {
            return;
        }

        const int previousQuickAccessRow = quickAccessHotRowIndex_;
        const int previousQuickAccessButton = quickAccessHotButtonIndex_;
        HTREEITEM nextTreeDropItem = nullptr;
        std::wstring nextTreeDropPath;

        quickAccessHotRowIndex_ = HitTestQuickAccessDestinationRow(windowPoint.x, windowPoint.y);
        quickAccessHotButtonIndex_ = -1;

        if (treePane_)
        {
            RECT treeRect{};
            GetWindowRect(treePane_, &treeRect);

            POINT screenPoint = windowPoint;
            ClientToScreen(hwnd_, &screenPoint);
            if (PtInRect(&treeRect, screenPoint) != FALSE)
            {
                POINT treePoint = screenPoint;
                ScreenToClient(treePane_, &treePoint);

                TVHITTESTINFO hitTest{};
                hitTest.pt = treePoint;
                const HTREEITEM item = TreeView_HitTest(treePane_, &hitTest);
                if (item && (hitTest.flags & TVHT_ONITEM) != 0)
                {
                    const FolderTreeNodeData* nodeData = GetFolderTreeNodeData(item);
                    if (nodeData && !nodeData->path.empty())
                    {
                        const std::wstring normalizedDestinationPath = NormalizeFolderPath(nodeData->path);
                        if (IsExistingDirectory(normalizedDestinationPath)
                            && !IsQuickAccessDestinationCurrentFolder(normalizedDestinationPath))
                        {
                            nextTreeDropItem = item;
                            nextTreeDropPath = normalizedDestinationPath;
                        }
                    }
                }
            }
        }

        if (internalSelectionTreeDropItem_ != nextTreeDropItem)
        {
            internalSelectionTreeDropItem_ = nextTreeDropItem;
            if (treePane_)
            {
                TreeView_SelectDropTarget(treePane_, internalSelectionTreeDropItem_);
            }
        }
        internalSelectionTreeDropPath_ = std::move(nextTreeDropPath);

        if ((previousQuickAccessRow != quickAccessHotRowIndex_
             || previousQuickAccessButton != quickAccessHotButtonIndex_)
            && !IsRectEmpty(&quickAccessDestinationPanelRect_))
        {
            InvalidateRect(hwnd_, &quickAccessDestinationPanelRect_, FALSE);
        }

        SetCursor(LoadCursorW(nullptr,
                              (quickAccessHotRowIndex_ >= 0 || internalSelectionTreeDropItem_ != nullptr)
                                  ? IDC_HAND
                                  : IDC_NO));
    }

    void MainWindow::FinishInternalSelectionDrag(bool commitDrop)
    {
        if (dragMode_ != DragMode::QuickAccessInternal)
        {
            return;
        }

        const int quickAccessRow = (commitDrop && quickAccessHotRowIndex_ >= 0)
            ? quickAccessHotRowIndex_
            : -1;
        const std::wstring treeDropPath = (commitDrop && !internalSelectionTreeDropPath_.empty())
            ? internalSelectionTreeDropPath_
            : std::wstring{};

        dragMode_ = DragMode::None;
        if (GetCapture() == hwnd_)
        {
            ReleaseCapture();
        }

        quickAccessHotRowIndex_ = -1;
        quickAccessHotButtonIndex_ = -1;
        internalSelectionTreeDropPath_.clear();
        internalSelectionTreeDropItem_ = nullptr;
        if (treePane_)
        {
            TreeView_SelectDropTarget(treePane_, nullptr);
        }
        if (!IsRectEmpty(&quickAccessDestinationPanelRect_))
        {
            InvalidateRect(hwnd_, &quickAccessDestinationPanelRect_, FALSE);
        }

        if (quickAccessRow >= 0 && quickAccessRow < static_cast<int>(quickAccessDestinationRows_.size()))
        {
            const services::FileOperationType type = ResolveQuickAccessDropOperationType(
                browserPaneController_->SelectedFilePathsSnapshot(),
                quickAccessDestinationRows_[static_cast<std::size_t>(quickAccessRow)].destinationPath);
            StartSelectionFileOperationToDestination(type,
                                                     quickAccessDestinationRows_[static_cast<std::size_t>(quickAccessRow)].destinationPath);
            return;
        }

        if (!treeDropPath.empty())
        {
            const std::vector<std::wstring> sourcePaths = SelectedFileOperationPathsSnapshot();
            if (sourcePaths.empty())
            {
                return;
            }

            services::FileOperationType type = services::FileOperationType::Move;
            if (!AreAllSourcePathsOnSameDrive(sourcePaths, treeDropPath))
            {
                if (!PromptForCrossDriveDropOperation(hwnd_, treeDropPath, &type))
                {
                    return;
                }
            }

            StartSelectionFileOperationToDestination(type, treeDropPath);
        }
    }

    void MainWindow::StartExternalSelectionDrag()
    {
        if (dragMode_ != DragMode::QuickAccessInternal || !browserPaneController_)
        {
            return;
        }

        const std::vector<std::wstring> sourcePaths = SelectedFileOperationPathsSnapshot();
        FinishInternalSelectionDrag(false);
        if (sourcePaths.empty())
        {
            return;
        }

        Microsoft::WRL::ComPtr<IDataObject> dataObject;
        if (!CreateShellFileDataObject(sourcePaths, &dataObject))
        {
            util::LogError(L"Failed to create the shell data object for the selected file drag");
            return;
        }

        Microsoft::WRL::ComPtr<IDropSource> dropSource;
        dropSource.Attach(new ShellFileDragSource());

        DWORD performedEffect = DROPEFFECT_NONE;
        const HRESULT dragResult = DoDragDrop(dataObject.Get(),
                                              dropSource.Get(),
                                              DROPEFFECT_COPY | DROPEFFECT_MOVE,
                                              &performedEffect);
        if (FAILED(dragResult))
        {
            util::LogError(std::wstring(L"External file drag failed with HRESULT value ")
                           + std::to_wstring(static_cast<unsigned long>(dragResult)));
        }
    }

    void MainWindow::LayoutChildren()
    {
        if (!hwnd_ || !treePane_ || !browserPane_ || !statusBar_)
        {
            return;
        }

        const int previousLeftPaneWidth = leftPaneWidth_;
        const RECT previousDetailsPanelRect = detailsPanelRect_;
        RECT client{};
        GetClientRect(hwnd_, &client);

        const int statusHeight = std::min(kStatusStripHeight, std::max(0, static_cast<int>(client.bottom - client.top) - kActionStripHeight));

        const int clientWidth = client.right - client.left;
        const int detailsSplitterWidth = detailsStripVisible_ ? kSplitterWidth : 0;
        const int maxDetailsPanelWidth = detailsStripVisible_
            ? std::max(0, clientWidth - kMinLeftPaneWidth - kMinRightPaneWidth - kSplitterWidth - detailsSplitterWidth)
            : 0;
        const int minDetailsPanelWidth = detailsStripVisible_ ? std::min(kDetailsPanelMinWidth, maxDetailsPanelWidth) : 0;
        const int desiredDetailsPanelWidth = detailsStripVisible_
            ? std::clamp(detailsPanelWidth_, minDetailsPanelWidth, maxDetailsPanelWidth)
            : 0;
        const int clientHeight = std::max(0, static_cast<int>(client.bottom - client.top) - statusHeight - kActionStripHeight);
        const int contentTop = kActionStripHeight;

        const int maxLeft = std::max(kMinLeftPaneWidth,
                                     clientWidth - desiredDetailsPanelWidth - kMinRightPaneWidth - kSplitterWidth - detailsSplitterWidth);
        leftPaneWidth_ = std::clamp(leftPaneWidth_, kMinLeftPaneWidth, maxLeft);

        const int browserWidth = std::max(kMinRightPaneWidth,
                                          clientWidth - leftPaneWidth_ - kSplitterWidth - detailsSplitterWidth - desiredDetailsPanelWidth);
        const int detailsPanelWidth = detailsStripVisible_
            ? std::max(0, clientWidth - leftPaneWidth_ - kSplitterWidth - browserWidth - detailsSplitterWidth)
            : 0;

        MoveWindow(statusBar_,
               0,
               std::max(0, static_cast<int>(client.bottom) - statusHeight),
               clientWidth,
               statusHeight,
               TRUE);

        MoveWindow(treePane_, 0, contentTop, leftPaneWidth_, clientHeight, TRUE);
        MoveWindow(browserPane_, leftPaneWidth_ + kSplitterWidth, contentTop,
                   browserWidth, clientHeight, TRUE);

        detailsPanelRect_ = RECT{leftPaneWidth_ + kSplitterWidth + browserWidth + detailsSplitterWidth,
                                 contentTop,
                                 clientWidth,
                                 contentTop + clientHeight};
        detailsPanelTabStripRect_ = RECT{};
        detailsPanelTabRects_ = {};
        detailsPanelContentRect_ = RECT{};
        detailsPanelHistogramRect_ = RECT{};
        detailsPanelCloseButtonRect_ = RECT{};
        quickAccessSortButtonRect_ = RECT{};
        quickAccessSortButtonHot_ = false;
        quickAccessSortButtonPressed_ = false;
        UpdateQuickAccessSortTooltip();
        quickAccessDestinationPanelRect_ = RECT{};
        quickAccessDestinationViewportRect_ = RECT{};
        quickAccessDestinationRows_.clear();
        if (quickAccessScrollBar_)
        {
            ShowWindow(quickAccessScrollBar_, SW_HIDE);
        }
        HideQuickAccessShortcutEditControls();
        quickAccessHotRowIndex_ = -1;
        quickAccessHotButtonIndex_ = -1;
        quickAccessPressedRowIndex_ = -1;
        quickAccessPressedButtonIndex_ = -1;
        const bool quickAccessPanelActive = detailsStripVisible_ && activeRightPaneTab_ == RightPaneTab::QuickSend;
        if (!quickAccessPanelActive)
        {
            if (quickAccessScrollBar_)
            {
                ShowWindow(quickAccessScrollBar_, SW_HIDE);
            }
            HideQuickAccessShortcutEditControls();
        }

        if (detailsPanelText_)
        {
            if (detailsStripVisible_ && detailsPanelWidth > 0)
            {
                const int innerLeft = detailsPanelRect_.left + kDetailsPanelMargin;
                const int innerRight = detailsPanelRect_.right - kDetailsPanelMargin;
                const int innerWidth = std::max(0, innerRight - innerLeft);
                const int tabTop = detailsPanelRect_.top + kDetailsPanelMargin;
                const int tabHeight = std::min(
                    kDetailsPanelTabHeight,
                    std::max(0, static_cast<int>(detailsPanelRect_.bottom - tabTop - kDetailsPanelMargin)));

                if (innerWidth > 0 && tabHeight > 0)
                {
                    const HFONT tabFont = detailsPanelSummaryFont_ ? detailsPanelSummaryFont_ : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
                    const int maxLabelWidth = std::max(MeasureTextWidth(tabFont, L"File Details"),
                                                       MeasureTextWidth(tabFont, L"Quick Send"));
                    const int desiredButtonWidth = std::max(kDetailsPanelTabMinButtonWidth,
                                                            maxLabelWidth + (kDetailsPanelTabButtonHorizontalPadding * 2));
                    const int maxButtonWidth = std::max(1, (std::max(0, innerWidth - kDetailsPanelTabButtonGap) / 2));
                    const int buttonWidth = std::min(desiredButtonWidth, maxButtonWidth);
                    const int secondButtonLeft = innerLeft + buttonWidth + kDetailsPanelTabButtonGap;

                    detailsPanelTabRects_[0] = RECT{innerLeft, tabTop, innerLeft + buttonWidth, tabTop + tabHeight};
                    detailsPanelTabRects_[1] = RECT{secondButtonLeft, tabTop, secondButtonLeft + buttonWidth, tabTop + tabHeight};
                    detailsPanelTabStripRect_ = RECT{detailsPanelTabRects_[0].left,
                                                     detailsPanelTabRects_[0].top,
                                                     detailsPanelTabRects_[1].right,
                                                     detailsPanelTabRects_[0].bottom};
                }

                detailsPanelContentRect_ = RECT{
                    innerLeft,
                    tabTop + tabHeight + kDetailsPanelTabGap,
                    innerRight,
                    detailsPanelRect_.bottom - kDetailsPanelMargin,
                };

                const int closeButtonRight = detailsPanelRect_.right - kDetailsPanelCloseButtonMargin;
                const int closeButtonLeft = closeButtonRight - kDetailsPanelCloseButtonSize;
                const int closeButtonTop = detailsPanelRect_.top + kDetailsPanelCloseButtonMargin;
                const int closeButtonBottom = closeButtonTop + kDetailsPanelCloseButtonSize;
                if (closeButtonLeft > detailsPanelTabStripRect_.right + kDetailsPanelCloseButtonGap)
                {
                    detailsPanelCloseButtonRect_ = RECT{closeButtonLeft, closeButtonTop, closeButtonRight, closeButtonBottom};
                }

                if (detailsPanelContentRect_.right > detailsPanelContentRect_.left
                    && detailsPanelContentRect_.bottom > detailsPanelContentRect_.top)
                {
                    if (activeRightPaneTab_ == RightPaneTab::FileDetails)
                    {
                        const std::wstring title = detailsPanelTitleText_.empty() ? std::wstring(L"File Details") : detailsPanelTitleText_;
                        const int titleHeight = MeasureTextBlockHeight(detailsPanelTitleFont_,
                                                                       title,
                                                                       innerWidth,
                                                                       DT_LEFT | DT_NOPREFIX | DT_WORDBREAK,
                                                                       22);
                        const int summaryHeight = detailsPanelSummaryText_.empty()
                            ? 0
                            : MeasureTextBlockHeight(detailsPanelSummaryFont_,
                                                     detailsPanelSummaryText_,
                                                     innerWidth,
                                                     DT_LEFT | DT_NOPREFIX | DT_WORDBREAK,
                                                     18);

                        int top = detailsPanelContentRect_.top + titleHeight + 6;
                        if (summaryHeight > 0)
                        {
                            top += summaryHeight + 8;
                        }

                        if (detailsPanelHistogramVisible_ || detailsPanelHistogramLoading_)
                        {
                            detailsPanelHistogramRect_ = RECT{
                                detailsPanelContentRect_.left,
                                top,
                                detailsPanelContentRect_.right,
                                top + kDetailsPanelHistogramHeight,
                            };
                            top = detailsPanelHistogramRect_.bottom + kDetailsPanelTextTopGap;
                        }

                        const int availableTextHeight = std::max(0, static_cast<int>(detailsPanelContentRect_.bottom) - top);
                        MoveWindow(detailsPanelText_, detailsPanelContentRect_.left, top, innerWidth, availableTextHeight, TRUE);
                        ShowWindow(detailsPanelText_, SW_SHOW);
                    }
                    else
                    {
                        RebuildQuickAccessDestinationRows(detailsPanelContentRect_.left,
                                                         detailsPanelContentRect_.right,
                                                         detailsPanelContentRect_.top);
                        ShowWindow(detailsPanelText_, SW_HIDE);
                    }
                }
                else
                {
                    detailsPanelContentRect_ = RECT{};
                    ShowWindow(detailsPanelText_, SW_HIDE);
                    if (quickAccessScrollBar_)
                    {
                        ShowWindow(quickAccessScrollBar_, SW_HIDE);
                    }
                    HideQuickAccessShortcutEditControls();
                }
            }
            else
            {
                ShowWindow(detailsPanelText_, SW_HIDE);
                detailsPanelHotTabIndex_ = -1;
                detailsPanelPressedTabIndex_ = -1;
                detailsPanelRect_ = RECT{};
                detailsPanelTabStripRect_ = RECT{};
                detailsPanelTabRects_ = {};
                detailsPanelContentRect_ = RECT{};
                quickAccessDestinationPanelRect_ = RECT{};
                quickAccessDestinationRows_.clear();
            }
        }

        if (tooltipControl_ && detailsPanelHistogramTooltipAdded_)
        {
            TTTOOLINFOW toolInfo{};
            toolInfo.cbSize = sizeof(toolInfo);
            toolInfo.hwnd = hwnd_;
            toolInfo.uId = kDetailsPanelHistogramTooltipId;
            toolInfo.rect = detailsPanelHistogramRect_;
            SendMessageW(tooltipControl_, TTM_NEWTOOLRECTW, 0, reinterpret_cast<LPARAM>(&toolInfo));
        }

        LayoutToolbar();

        RECT splitterRect{leftPaneWidth_, kActionStripHeight, leftPaneWidth_ + kSplitterWidth, client.bottom};
        InvalidateRect(hwnd_, &splitterRect, FALSE);
        if (previousLeftPaneWidth != leftPaneWidth_)
        {
            RECT previousSplitterRect{previousLeftPaneWidth,
                                      kActionStripHeight,
                                      previousLeftPaneWidth + kSplitterWidth,
                                      client.bottom};
            InvalidateRect(hwnd_, &previousSplitterRect, FALSE);
        }
        if (detailsStripVisible_)
        {
            if (!IsRectEmpty(&detailsPanelRect_))
            {
                RECT detailsSplitterRect{detailsPanelRect_.left - kSplitterWidth,
                                         kActionStripHeight,
                                         detailsPanelRect_.left,
                                         client.bottom};
                InvalidateRect(hwnd_, &detailsSplitterRect, FALSE);
                InvalidateRect(hwnd_, &detailsPanelRect_, FALSE);
            }

            if (!IsRectEmpty(&previousDetailsPanelRect))
            {
                RECT previousDetailsSplitterRect{previousDetailsPanelRect.left - kSplitterWidth,
                                                 kActionStripHeight,
                                                 previousDetailsPanelRect.left,
                                                 client.bottom};
                InvalidateRect(hwnd_, &previousDetailsSplitterRect, FALSE);
                InvalidateRect(hwnd_, &previousDetailsPanelRect, FALSE);
            }
        }

        UpdateStatusText();
    }

    void MainWindow::UpdateStatusText()
    {
        if (!statusBar_)
        {
            return;
        }

        const std::uint64_t folderCount = browserModel_ && !browserModel_->FolderPath().empty()
            ? browserModel_->TotalCount()
            : 0;
        const std::uint64_t folderBytes = browserModel_ && !browserModel_->FolderPath().empty()
            ? browserModel_->TotalBytes()
            : 0;
        const bool hasActiveFilter = browserPaneController_ && browserPaneController_->HasActiveFilter();
        const std::uint64_t displayedCount = hasActiveFilter
            ? browserPaneController_->DisplayedItemCount()
            : folderCount;
        statusPrimaryText_ = L"Folder: " + std::to_wstring(displayedCount)
            + (hasActiveFilter ? L" of " + std::to_wstring(folderCount) : L"")
            + (showSubfoldersInBrowser_ ? L" items | " : L" files | ")
            + browser::FormatByteSize(folderBytes);

        if (fileOperationActive_ && !activeFileOperationLabel_.empty())
        {
            statusPrimaryText_ = activeFileOperationLabel_ + L"  |  " + statusPrimaryText_;
        }
        else if (batchConvertActive_)
        {
            statusPrimaryText_ = L"Converting: "
                + std::to_wstring(batchConvertCompleted_)
                + L" / "
                + std::to_wstring(batchConvertTotal_)
                + L"  |  "
                + statusPrimaryText_;
        }

        const std::uint64_t selectedCount = browserPaneController_ ? browserPaneController_->SelectedCount() : 0;
        const std::uint64_t selectedBytes = browserPaneController_ ? browserPaneController_->SelectedBytes() : 0;
        statusSecondaryText_ = L"Selected: " + std::to_wstring(selectedCount)
            + L" items | " + browser::FormatByteSize(selectedBytes);
        if (rawJpegPairedOperationsEnabled_)
        {
            std::size_t pairedCompanionCount = 0;
            SelectedFileOperationPathsSnapshot(&pairedCompanionCount);
            statusSecondaryText_.append(L"  |  Paired RAW+JPEG On");
            if (pairedCompanionCount > 0)
            {
                statusSecondaryText_.append(L" (+");
                statusSecondaryText_.append(std::to_wstring(pairedCompanionCount));
                statusSecondaryText_.push_back(L')');
            }
        }
        statusSecondaryText_.append(L"  |  Profile: ");
        statusSecondaryText_.append(util::ResourceProfileToDisplayName(resourceProfile_));
        if (thumbnailCacheCapacityOverrideBytes_ != 0 || metadataCacheCapacityOverrideEntries_ != 0)
        {
            statusSecondaryText_.append(L" (Custom cache caps)");
        }
        if (showPressureStateInStatusBar_)
        {
            statusSecondaryText_.append(L"  |  Pressure: ");
            statusSecondaryText_.append(thumbnailMemoryPressureActive_ ? L"Adaptive throttling active" : L"Normal");
        }
        if (viewerWindowActive_ && viewerZoomPercent_ > 0)
        {
            statusSecondaryText_.append(L"  |  Viewer zoom: ");
            statusSecondaryText_.append(std::to_wstring(viewerZoomPercent_));
            statusSecondaryText_.push_back(L'%');
        }

        const bool folderTreeEnumerationActive = !pendingFolderTreeEnumerationItems_.empty()
            || !pendingFolderTreeChildPresenceItems_.empty();
        if (folderEnumerationActive_ || folderTreeEnumerationActive)
        {
            const wchar_t* activityText = folderEnumerationActive_ && folderTreeEnumerationActive
                ? L"Scanning folders...  |  "
                : (folderEnumerationActive_ ? L"Scanning folder...  |  " : L"Probing folders...  |  ");
            statusPrimaryText_ = activityText + statusPrimaryText_;
        }

        InvalidateRect(statusBar_, nullptr, TRUE);
    }

    void MainWindow::DrawStatusStrip(const DRAWITEMSTRUCT& drawItem) const
    {
        const ThemePalette palette = GetThemePalette();
        RECT clientRect = drawItem.rcItem;
        const int width = clientRect.right - clientRect.left;
        const int firstPartWidth = width > 0 ? width / 2 : 420;

        const COLORREF backgroundColor = BlendColor(palette.paneBackground,
                                                    palette.windowBackground,
                                                    themeMode_ == ThemeMode::Dark ? 34 : 18);
        const HBRUSH backgroundBrush = CreateSolidBrush(backgroundColor);
        FillRect(drawItem.hDC, &clientRect, backgroundBrush);
        DeleteObject(backgroundBrush);

        const HPEN borderPen = CreatePen(PS_SOLID, 1, palette.actionStripBorder);
        const HGDIOBJ oldPen = SelectObject(drawItem.hDC, borderPen);
        MoveToEx(drawItem.hDC, clientRect.left, clientRect.top, nullptr);
        LineTo(drawItem.hDC, clientRect.right, clientRect.top);
        MoveToEx(drawItem.hDC, clientRect.left + firstPartWidth, clientRect.top + 5, nullptr);
        LineTo(drawItem.hDC, clientRect.left + firstPartWidth, clientRect.bottom - 5);
        SelectObject(drawItem.hDC, oldPen);
        DeleteObject(borderPen);

        RECT primaryRect{clientRect.left + kStatusStripHorizontalPadding,
                         clientRect.top,
                         clientRect.left + firstPartWidth - kStatusStripHorizontalPadding,
                         clientRect.bottom};
        render::DrawGdiText(drawItem.hDC,
                    detailsPanelSummaryFont_ ? detailsPanelSummaryFont_ : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)),
                    statusPrimaryText_.c_str(),
                    -1,
                    primaryRect,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS,
                    palette.text,
                    backgroundColor);

        RECT secondaryRect{clientRect.left + firstPartWidth + kStatusStripHorizontalPadding,
                           clientRect.top,
                           clientRect.right - kStatusStripHorizontalPadding,
                           clientRect.bottom};
        render::DrawGdiText(drawItem.hDC,
                    detailsPanelSummaryFont_ ? detailsPanelSummaryFont_ : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)),
                    statusSecondaryText_.c_str(),
                    -1,
                    secondaryRect,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS,
                    palette.mutedText,
                    backgroundColor);
    }

    void MainWindow::MeasureOwnerDrawMenuItem(MEASUREITEMSTRUCT* measureItem) const
    {
        if (!measureItem)
        {
            return;
        }

        const auto* drawData = reinterpret_cast<const MenuDrawItemData*>(measureItem->itemData);
        if (!drawData)
        {
            measureItem->itemWidth = 0;
            measureItem->itemHeight = kMenuPopupItemHeight;
            return;
        }

        if (drawData->separator)
        {
            measureItem->itemWidth = 0;
            measureItem->itemHeight = kMenuPopupSeparatorHeight;
            return;
        }

        std::wstring label;
        std::wstring shortcut;
        SplitMenuDisplayText(drawData->text, &label, &shortcut);

        const auto scaleMenuDimension = [this](int dimension)
        {
            return hyperbrowse::util::ScaleAppTextDimension(dimension, appTextSize_);
        };
        const int itemHeight = scaleMenuDimension(kMenuPopupItemHeight);
        const int checkColumnWidth = scaleMenuDimension(kMenuPopupCheckColumnWidth);
        const int textPadding = scaleMenuDimension(kMenuPopupTextPadding);
        const int shortcutGap = scaleMenuDimension(kMenuPopupShortcutGap);
        const int measurementAllowance = scaleMenuDimension(kMenuPopupMeasurementAllowance);
        const int arrowWidth = scaleMenuDimension(kMenuPopupArrowWidth);
        const HFONT menuFont = appTextUiFont_ ? appTextUiFont_ : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        const int labelWidth = MeasureTextWidth(menuFont, label);
        const int shortcutWidth = shortcut.empty() ? 0 : MeasureTextWidth(menuFont, shortcut);
        int itemWidth = checkColumnWidth + (textPadding * 2) + labelWidth;
        if (shortcutWidth > 0)
        {
            itemWidth += shortcutGap + shortcutWidth;
        }
        itemWidth += measurementAllowance;
        if (drawData->hasSubmenu)
        {
            itemWidth += arrowWidth;
        }

        measureItem->itemWidth = static_cast<UINT>(itemWidth);
        measureItem->itemHeight = static_cast<UINT>(std::max(
            itemHeight,
            MeasureSingleLineTextHeight(menuFont, itemHeight - measurementAllowance)
                + measurementAllowance));
    }

    void MainWindow::DrawOwnerDrawMenuItem(const DRAWITEMSTRUCT& drawItem) const
    {
        const auto* drawData = reinterpret_cast<const MenuDrawItemData*>(drawItem.itemData);
        if (!drawData)
        {
            return;
        }

        const ThemePalette palette = GetThemePalette();
        RECT itemRect = drawItem.rcItem;
        const bool selected = (drawItem.itemState & ODS_SELECTED) != 0;
        const bool disabled = (drawItem.itemState & ODS_DISABLED) != 0;
        const bool checked = (drawItem.itemState & ODS_CHECKED) != 0;
        const COLORREF backgroundColor = selected
            ? BlendColor(palette.accentFill, palette.actionStripBackground, themeMode_ == ThemeMode::Dark ? 28 : 12)
            : BlendColor(palette.paneBackground, palette.windowBackground, themeMode_ == ThemeMode::Dark ? 26 : 12);

        const HBRUSH backgroundBrush = CreateSolidBrush(backgroundColor);
        FillRect(drawItem.hDC, &itemRect, backgroundBrush);
        DeleteObject(backgroundBrush);

        if (drawData->separator)
        {
            const HPEN separatorPen = CreatePen(PS_SOLID, 1, palette.actionStripBorder);
            const HGDIOBJ oldPen = SelectObject(drawItem.hDC, separatorPen);
            const int y = itemRect.top + ((itemRect.bottom - itemRect.top) / 2);
            MoveToEx(drawItem.hDC, itemRect.left + kMenuPopupCheckColumnWidth, y, nullptr);
            LineTo(drawItem.hDC, itemRect.right - kMenuPopupTextPadding, y);
            SelectObject(drawItem.hDC, oldPen);
            DeleteObject(separatorPen);
            return;
        }

        std::wstring label;
        std::wstring shortcut;
        SplitMenuDisplayText(drawData->text, &label, &shortcut);

        const auto scaleMenuDimension = [this](int dimension)
        {
            return hyperbrowse::util::ScaleAppTextDimension(dimension, appTextSize_);
        };
        const int checkColumnWidth = scaleMenuDimension(kMenuPopupCheckColumnWidth);
        const int textPadding = scaleMenuDimension(kMenuPopupTextPadding);
        const int shortcutGap = scaleMenuDimension(kMenuPopupShortcutGap);

        if (checked)
        {
            const int checkInset = scaleMenuDimension(4);
            RECT checkRect{itemRect.left + checkInset,
                           itemRect.top + checkInset,
                           itemRect.left + checkColumnWidth - checkInset,
                           itemRect.bottom - checkInset};
            const COLORREF checkFill = selected ? palette.accent : BlendColor(palette.accentFill, backgroundColor, 24);
            const HBRUSH checkBrush = CreateSolidBrush(checkFill);
            const HPEN checkPen = CreatePen(PS_SOLID, 1, selected ? palette.accent : palette.accentFill);
            const HGDIOBJ oldBrush = SelectObject(drawItem.hDC, checkBrush);
            const HGDIOBJ oldCheckPen = SelectObject(drawItem.hDC, checkPen);
            const int checkCorner = scaleMenuDimension(8);
            RoundRect(drawItem.hDC, checkRect.left, checkRect.top, checkRect.right, checkRect.bottom, checkCorner, checkCorner);
            SelectObject(drawItem.hDC, oldCheckPen);
            SelectObject(drawItem.hDC, oldBrush);
            DeleteObject(checkPen);
            DeleteObject(checkBrush);

            const HPEN markPen = CreatePen(PS_SOLID, 2, palette.accentText);
            const HGDIOBJ oldMarkPen = SelectObject(drawItem.hDC, markPen);
            const int checkMarkInset = scaleMenuDimension(5);
            MoveToEx(drawItem.hDC,
                     checkRect.left + checkMarkInset,
                     checkRect.top + ((checkRect.bottom - checkRect.top) / 2),
                     nullptr);
            LineTo(drawItem.hDC, checkRect.left + scaleMenuDimension(9), checkRect.bottom - scaleMenuDimension(6));
            LineTo(drawItem.hDC, checkRect.right - checkMarkInset, checkRect.top + scaleMenuDimension(6));
            SelectObject(drawItem.hDC, oldMarkPen);
            DeleteObject(markPen);
        }

        const COLORREF labelColor = disabled
            ? BlendColor(palette.mutedText, backgroundColor, 128)
            : (selected ? palette.text : palette.text);
        const COLORREF shortcutColor = disabled
            ? BlendColor(palette.mutedText, backgroundColor, 128)
            : (selected ? palette.text : palette.mutedText);
        const HFONT menuFont = appTextUiFont_ ? appTextUiFont_ : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

        RECT labelRect{itemRect.left + checkColumnWidth + textPadding,
                       itemRect.top,
                       itemRect.right - textPadding,
                       itemRect.bottom};
        if (!shortcut.empty())
        {
            labelRect.right -= MeasureTextWidth(menuFont, shortcut) + shortcutGap;
        }
        render::DrawGdiText(drawItem.hDC,
                            menuFont,
                            label.c_str(),
                            -1,
                            labelRect,
                            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS,
                            labelColor,
                            backgroundColor);

        if (!shortcut.empty())
        {
            RECT shortcutRect{labelRect.right + shortcutGap,
                              itemRect.top,
                              itemRect.right - textPadding,
                              itemRect.bottom};
                        render::DrawGdiText(drawItem.hDC,
                                                                menuFont,
                                                                shortcut.c_str(),
                                                                -1,
                                                                shortcutRect,
                                                                DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS,
                                                                shortcutColor,
                                                                backgroundColor);
        }
    }

    int MainWindow::CommandBarMenuHitTest(int x, int y) const
    {
        const POINT point{x, y};
        for (int index = 0; index < static_cast<int>(commandBarMenuButtons_.size()); ++index)
        {
            if (PtInRect(&commandBarMenuButtons_[static_cast<std::size_t>(index)].rect, point) != FALSE)
            {
                return index;
            }
        }

        return -1;
    }

    void MainWindow::ActivateCommandBarKeyboardMode(int index)
    {
        if (commandBarMenuButtons_.empty())
        {
            return;
        }

        const int targetIndex = std::clamp(index, 0, static_cast<int>(commandBarMenuButtons_.size()) - 1);
        if (!commandBarKeyboardActive_)
        {
            HWND focusWindow = GetFocus();
            if (focusWindow && (focusWindow == hwnd_ || IsChild(hwnd_, focusWindow)))
            {
                commandBarPreviousFocus_ = focusWindow;
            }
            else
            {
                commandBarPreviousFocus_ = nullptr;
            }
        }

        commandBarKeyboardActive_ = true;
        commandBarPressedIndex_ = -1;
        commandBarHotIndex_ = targetIndex;
        InvalidateToolbarStrip();

        if (hwnd_ && GetFocus() != hwnd_)
        {
            SetFocus(hwnd_);
        }
    }

    void MainWindow::DeactivateCommandBarKeyboardMode(bool restoreFocus)
    {
        const HWND restoreWindow = commandBarPreviousFocus_;
        const bool hadVisualState = commandBarKeyboardActive_ || commandBarHotIndex_ >= 0 || commandBarPressedIndex_ >= 0;

        commandBarKeyboardActive_ = false;
        commandBarPressedIndex_ = -1;
        commandBarHotIndex_ = -1;
        commandBarPreviousFocus_ = nullptr;

        if (hadVisualState)
        {
            InvalidateToolbarStrip();
        }

        if (restoreFocus && restoreWindow && restoreWindow != hwnd_ && IsWindow(restoreWindow)
            && (restoreWindow == hwnd_ || IsChild(hwnd_, restoreWindow)))
        {
            SetFocus(restoreWindow);
        }
    }

    bool MainWindow::HandleCommandBarKeyboardInput(UINT message, WPARAM wParam, LPARAM lParam)
    {
        const int mnemonicIndex = CommandBarMenuIndexFromVirtualKey(wParam);
        const bool shiftPressed = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        const bool isRepeat = (lParam & 0x40000000) != 0;

        if (message == WM_SYSCHAR)
        {
            return mnemonicIndex >= 0 || (commandBarKeyboardActive_ && wParam != L' ');
        }

        if (message == WM_SYSKEYDOWN)
        {
            if (wParam == VK_F10 && !shiftPressed)
            {
                if (!isRepeat)
                {
                    if (commandBarKeyboardActive_)
                    {
                        DeactivateCommandBarKeyboardMode(true);
                    }
                    else
                    {
                        ActivateCommandBarKeyboardMode(commandBarHotIndex_ >= 0 ? commandBarHotIndex_ : 0);
                    }
                }
                return true;
            }

            if (wParam == VK_MENU)
            {
                if (!isRepeat)
                {
                    if (commandBarKeyboardActive_)
                    {
                        DeactivateCommandBarKeyboardMode(true);
                    }
                    else
                    {
                        ActivateCommandBarKeyboardMode(commandBarHotIndex_ >= 0 ? commandBarHotIndex_ : 0);
                    }
                }
                return true;
            }

            if (mnemonicIndex >= 0)
            {
                ActivateCommandBarKeyboardMode(mnemonicIndex);
                OpenCommandBarMenu(mnemonicIndex);
                return true;
            }
        }

        if (message != WM_KEYDOWN && message != WM_SYSKEYDOWN)
        {
            return false;
        }

        if (!commandBarKeyboardActive_)
        {
            return false;
        }

        switch (wParam)
        {
        case VK_LEFT:
            ActivateCommandBarKeyboardMode((commandBarHotIndex_ + static_cast<int>(commandBarMenuButtons_.size()) - 1)
                                           % static_cast<int>(commandBarMenuButtons_.size()));
            return true;
        case VK_RIGHT:
            ActivateCommandBarKeyboardMode((commandBarHotIndex_ + 1)
                                           % static_cast<int>(commandBarMenuButtons_.size()));
            return true;
        case VK_HOME:
            ActivateCommandBarKeyboardMode(0);
            return true;
        case VK_END:
            ActivateCommandBarKeyboardMode(static_cast<int>(commandBarMenuButtons_.size()) - 1);
            return true;
        case VK_DOWN:
        case VK_RETURN:
            OpenCommandBarMenu(commandBarHotIndex_ >= 0 ? commandBarHotIndex_ : 0);
            return true;
        case VK_SPACE:
            if (message == WM_KEYDOWN)
            {
                OpenCommandBarMenu(commandBarHotIndex_ >= 0 ? commandBarHotIndex_ : 0);
                return true;
            }
            return false;
        case VK_ESCAPE:
            DeactivateCommandBarKeyboardMode(true);
            return true;
        default:
            break;
        }

        if (mnemonicIndex >= 0)
        {
            ActivateCommandBarKeyboardMode(mnemonicIndex);
            OpenCommandBarMenu(mnemonicIndex);
            return true;
        }

        return false;
    }

    void MainWindow::OpenCommandBarMenu(int index)
    {
        if (index < 0 || index >= static_cast<int>(commandBarMenuButtons_.size()))
        {
            return;
        }

        const CommandBarMenuButton& button = commandBarMenuButtons_[static_cast<std::size_t>(index)];
        if (!button.menu || IsRectEmpty(&button.rect))
        {
            return;
        }

        const bool keyboardOpen = commandBarKeyboardActive_;
        if (keyboardOpen)
        {
            commandBarHotIndex_ = index;
            InvalidateToolbarStrip();
            if (GetFocus() != hwnd_)
            {
                SetFocus(hwnd_);
            }
        }

        RECT screenRect = button.rect;
        MapWindowPoints(hwnd_, HWND_DESKTOP, reinterpret_cast<LPPOINT>(&screenRect), 2);
        SetForegroundWindow(hwnd_);
        const UINT commandId = TrackPopupMenuEx(button.menu,
                                                TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD | TPM_NONOTIFY,
                                                screenRect.left,
                                                screenRect.bottom,
                                                hwnd_,
                                                nullptr);
        PostMessageW(hwnd_, WM_NULL, 0, 0);
        if (commandId != 0)
        {
            if (keyboardOpen)
            {
                DeactivateCommandBarKeyboardMode(true);
            }
            HandleCommand(commandId);
        }
        else if (keyboardOpen && hwnd_ && GetFocus() != hwnd_)
        {
            SetFocus(hwnd_);
        }
    }

    void MainWindow::RecordRecentFolder(std::wstring folderPath)
    {
        // Surface the folder in the taskbar jump list's "Recent" category.
        SHAddToRecentDocs(SHARD_PATHW, folderPath.c_str());

        if (InsertFolderPath(&recentFolders_, std::move(folderPath), kQuickAccessFolderLimit, true) && menu_)
        {
            UpdateMenuState();
        }
    }

    void MainWindow::RecordOpenedFolderHistory(std::wstring folderPath)
    {
        folderPath = NormalizeFolderPath(std::move(folderPath));
        if (folderPath.empty())
        {
            return;
        }

        if (pendingFolderHistoryNavigation_ != FolderHistoryNavigationDirection::None)
        {
            const std::size_t targetIndex = pendingFolderHistoryTargetIndex_;
            pendingFolderHistoryNavigation_ = FolderHistoryNavigationDirection::None;
            pendingFolderHistoryTargetIndex_ = kInvalidHistoryIndex;

            if (targetIndex < openedFolderHistory_.size())
            {
                openedFolderHistory_[targetIndex] = folderPath;
                openedFolderHistoryIndex_ = targetIndex;
                return;
            }
        }

        if (openedFolderHistoryIndex_ != kInvalidHistoryIndex
            && openedFolderHistoryIndex_ + 1 < openedFolderHistory_.size())
        {
            openedFolderHistory_.erase(openedFolderHistory_.begin() + static_cast<std::ptrdiff_t>(openedFolderHistoryIndex_ + 1),
                                       openedFolderHistory_.end());
        }

        if (openedFolderHistory_.empty() || !FolderPathsEqual(openedFolderHistory_.back(), folderPath))
        {
            openedFolderHistory_.push_back(std::move(folderPath));
        }

        while (openedFolderHistory_.size() > kOpenedFolderHistoryLimit)
        {
            openedFolderHistory_.erase(openedFolderHistory_.begin());
        }

        openedFolderHistoryIndex_ = openedFolderHistory_.empty()
            ? kInvalidHistoryIndex
            : openedFolderHistory_.size() - 1;
    }

    bool MainWindow::NavigateBackToLastOpenedFolder()
    {
        if (!browserModel_ || openedFolderHistory_.empty() || openedFolderHistoryIndex_ == kInvalidHistoryIndex)
        {
            return false;
        }

        if (openedFolderHistoryIndex_ == 0)
        {
            return false;
        }

        const std::wstring currentFolderPath = NormalizeFolderPath(browserModel_->FolderPath());
        std::size_t candidateIndex = openedFolderHistoryIndex_;
        while (candidateIndex > 0)
        {
            --candidateIndex;
            const std::wstring& candidate = openedFolderHistory_[candidateIndex];

            const std::wstring resolvedFolderPath = FindExistingFolderAncestor(fs::path(candidate));
            if (resolvedFolderPath.empty())
            {
                continue;
            }

            if (!currentFolderPath.empty() && FolderPathsEqual(currentFolderPath, resolvedFolderPath))
            {
                continue;
            }

            pendingFolderHistoryNavigation_ = FolderHistoryNavigationDirection::Back;
            pendingFolderHistoryTargetIndex_ = candidateIndex;
            LoadFolderAsync(resolvedFolderPath, true);
            return true;
        }

        return false;
    }

    bool MainWindow::NavigateForwardToLastOpenedFolder()
    {
        if (!browserModel_ || openedFolderHistory_.empty() || openedFolderHistoryIndex_ == kInvalidHistoryIndex)
        {
            return false;
        }

        if (openedFolderHistoryIndex_ + 1 >= openedFolderHistory_.size())
        {
            return false;
        }

        const std::wstring currentFolderPath = NormalizeFolderPath(browserModel_->FolderPath());
        std::size_t candidateIndex = openedFolderHistoryIndex_;
        while (candidateIndex + 1 < openedFolderHistory_.size())
        {
            ++candidateIndex;
            const std::wstring& candidate = openedFolderHistory_[candidateIndex];

            const std::wstring resolvedFolderPath = FindExistingFolderAncestor(fs::path(candidate));
            if (resolvedFolderPath.empty())
            {
                continue;
            }

            if (!currentFolderPath.empty() && FolderPathsEqual(currentFolderPath, resolvedFolderPath))
            {
                continue;
            }

            pendingFolderHistoryNavigation_ = FolderHistoryNavigationDirection::Forward;
            pendingFolderHistoryTargetIndex_ = candidateIndex;
            LoadFolderAsync(resolvedFolderPath, true);
            return true;
        }

        return false;
    }

    void MainWindow::RecordRecentDestination(std::wstring folderPath)
    {
        if (InsertFolderPath(&recentDestinationFolders_, std::move(folderPath), kQuickAccessFolderLimit, true) && menu_)
        {
            UpdateMenuState();
            if (hwnd_ && detailsStripVisible_)
            {
                LayoutChildren();
            }
        }
    }

    void MainWindow::SyncQuickSendModel()
    {
        quickSendModel_.SetFavoriteDestinations(favoriteDestinationFolders_);
    }

    void MainWindow::SortFavoriteDestinationsByShortcut()
    {
        quickSendModel_.SortFavoriteDestinationsByShortcut();
        favoriteDestinationFolders_ = quickSendModel_.FavoriteDestinations();
    }

    void MainWindow::RefreshQuickAccessMenus()
    {
        if (!fileMenu_ || !openRecentFolderMenu_ || !copySelectionToMenu_ || !moveSelectionToMenu_)
        {
            return;
        }

        const bool hasFolder = browserModel_ && !browserModel_->FolderPath().empty();
        const bool allowMutatingFileCommands = browserPaneController_ && browserPaneController_->SelectedCount() > 0 && !fileOperationActive_;
        const std::wstring toggleLabel = hasFolder && IsFavoriteDestination(browserModel_->FolderPath())
            ? L"Remove Current Folder from Favorite &Destinations"
            : L"Add Current Folder to Favorite &Destinations";
        ModifyMenuW(fileMenu_,
                    ID_FILE_TOGGLE_CURRENT_FOLDER_FAVORITE_DESTINATION,
                    MF_BYCOMMAND | MF_STRING,
                    ID_FILE_TOGGLE_CURRENT_FOLDER_FAVORITE_DESTINATION,
                    toggleLabel.c_str());

        RemoveAllMenuItems(openRecentFolderMenu_);
        if (recentFolders_.empty())
        {
            AppendMenuW(openRecentFolderMenu_, MF_STRING | MF_GRAYED, 0, L"(No recent folders)");
        }
        else
        {
            const std::size_t recentCount = std::min<std::size_t>(recentFolders_.size(), ID_FILE_OPEN_RECENT_FOLDER_LAST - ID_FILE_OPEN_RECENT_FOLDER_BASE + 1);
            for (std::size_t index = 0; index < recentCount; ++index)
            {
                const std::wstring label = FormatFolderShortcutMenuLabel(recentFolders_[index]);
                AppendMenuW(openRecentFolderMenu_, MF_STRING, ID_FILE_OPEN_RECENT_FOLDER_BASE + static_cast<UINT>(index), label.c_str());
            }

            AppendMenuW(openRecentFolderMenu_, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(openRecentFolderMenu_, MF_STRING, ID_FILE_CLEAR_RECENT_FOLDERS, L"C&lear All Recent Folders");
        }

        const std::vector<std::wstring> recentDestinationPaths = RecentDestinationShortcutPaths();
        const auto populateDestinationMenu = [&](HMENU menu,
                                                 UINT browseCommandId,
                                                 UINT favoriteBaseCommandId,
                                                 UINT favoriteLastCommandId,
                                                 UINT recentBaseCommandId,
                                                 UINT recentLastCommandId)
        {
            RemoveAllMenuItems(menu);
            AppendMenuW(menu,
                        MF_STRING | (allowMutatingFileCommands ? 0 : MF_GRAYED),
                        browseCommandId,
                        L"Choose &Folder...");

            const std::size_t favoriteCapacity = favoriteLastCommandId - favoriteBaseCommandId + 1;
            const std::size_t recentCapacity = recentLastCommandId - recentBaseCommandId + 1;
            const std::size_t favoriteCount = std::min<std::size_t>(favoriteDestinationFolders_.size(), favoriteCapacity);
            const std::size_t recentCount = std::min<std::size_t>(recentDestinationPaths.size(), recentCapacity);
            if (favoriteCount == 0 && recentCount == 0)
            {
                AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
                AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, L"(No favorite or recent destinations)");
                return;
            }

            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            if (favoriteCount > 0)
            {
                AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, L"Favorite Destinations");
                for (std::size_t index = 0; index < favoriteCount; ++index)
                {
                    const std::wstring label = FormatFolderShortcutMenuLabel(favoriteDestinationFolders_[index]);
                    AppendMenuW(menu,
                                MF_STRING | (allowMutatingFileCommands ? 0 : MF_GRAYED),
                                favoriteBaseCommandId + static_cast<UINT>(index),
                                label.c_str());
                }
            }

            if (recentCount > 0)
            {
                if (favoriteCount > 0)
                {
                    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
                }

                AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, L"Recent Destinations");
                for (std::size_t index = 0; index < recentCount; ++index)
                {
                    const std::wstring label = FormatFolderShortcutMenuLabel(recentDestinationPaths[index]);
                    AppendMenuW(menu,
                                MF_STRING | (allowMutatingFileCommands ? 0 : MF_GRAYED),
                                recentBaseCommandId + static_cast<UINT>(index),
                                label.c_str());
                }

                AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
                AppendMenuW(menu, MF_STRING, ID_FILE_CLEAR_RECENT_DESTINATIONS, L"Clear All Recent &Destinations");
            }
        };

        populateDestinationMenu(copySelectionToMenu_,
                                ID_FILE_COPY_SELECTION_BROWSE,
                                ID_FILE_COPY_SELECTION_FAVORITE_BASE,
                                ID_FILE_COPY_SELECTION_FAVORITE_LAST,
                                ID_FILE_COPY_SELECTION_RECENT_BASE,
                                ID_FILE_COPY_SELECTION_RECENT_LAST);
        populateDestinationMenu(moveSelectionToMenu_,
                                ID_FILE_MOVE_SELECTION_BROWSE,
                                ID_FILE_MOVE_SELECTION_FAVORITE_BASE,
                                ID_FILE_MOVE_SELECTION_FAVORITE_LAST,
                                ID_FILE_MOVE_SELECTION_RECENT_BASE,
                                ID_FILE_MOVE_SELECTION_RECENT_LAST);
        RefreshPersistentMenuOwnerDraw();
    }

    void MainWindow::RefreshPersistentMenuOwnerDraw()
    {
        if (!menu_)
        {
            return;
        }

        std::vector<std::unique_ptr<MenuDrawItemData>> refreshedItems;
        PrepareMenuForOwnerDraw(menu_, refreshedItems, false);
        menuDrawItems_ = std::move(refreshedItems);
    }

    void MainWindow::PrepareMenuForOwnerDraw(HMENU menu,
                                             std::vector<std::unique_ptr<MenuDrawItemData>>& storage,
                                             bool ownerDrawCurrentLevel) const
    {
        if (!menu)
        {
            return;
        }

        const int itemCount = GetMenuItemCount(menu);
        for (int itemIndex = 0; itemIndex < itemCount; ++itemIndex)
        {
            MENUITEMINFOW menuInfo{};
            menuInfo.cbSize = sizeof(menuInfo);
            menuInfo.fMask = MIIM_FTYPE | MIIM_SUBMENU | MIIM_DATA;
            if (!GetMenuItemInfoW(menu, static_cast<UINT>(itemIndex), TRUE, &menuInfo))
            {
                continue;
            }

            const bool separator = (menuInfo.fType & MFT_SEPARATOR) != 0;
            const bool hasSubmenu = menuInfo.hSubMenu != nullptr;
            std::wstring text;
            if (!separator)
            {
                const auto* existingData = reinterpret_cast<const MenuDrawItemData*>(menuInfo.dwItemData);
                if ((menuInfo.fType & MFT_OWNERDRAW) != 0 && existingData)
                {
                    text = existingData->text;
                }
                else
                {
                    const int textLength = GetMenuStringW(menu, static_cast<UINT>(itemIndex), nullptr, 0, MF_BYPOSITION);
                    if (textLength > 0)
                    {
                        std::wstring buffer(static_cast<std::size_t>(textLength) + 1, L'\0');
                        GetMenuStringW(menu,
                                       static_cast<UINT>(itemIndex),
                                       buffer.data(),
                                       textLength + 1,
                                       MF_BYPOSITION);
                        buffer.resize(static_cast<std::size_t>(textLength));
                        text = std::move(buffer);
                    }
                }
            }

            if (ownerDrawCurrentLevel)
            {
                auto drawData = std::make_unique<MenuDrawItemData>();
                drawData->text = std::move(text);
                drawData->separator = separator;
                drawData->hasSubmenu = hasSubmenu;

                MENUITEMINFOW updateInfo{};
                updateInfo.cbSize = sizeof(updateInfo);
                updateInfo.fMask = MIIM_FTYPE | MIIM_DATA;
                updateInfo.fType = separator ? (MFT_SEPARATOR | MFT_OWNERDRAW) : MFT_OWNERDRAW;
                updateInfo.dwItemData = reinterpret_cast<ULONG_PTR>(drawData.get());
                SetMenuItemInfoW(menu, static_cast<UINT>(itemIndex), TRUE, &updateInfo);
                storage.push_back(std::move(drawData));
            }

            if (hasSubmenu)
            {
                PrepareMenuForOwnerDraw(menuInfo.hSubMenu, storage, true);
            }
        }
    }

    void MainWindow::ApplyDetailsPanelText(std::wstring title, std::wstring summary, std::wstring body)
    {
        detailsPanelTitleText_ = std::move(title);
        detailsPanelSummaryText_ = std::move(summary);
        detailsPanelBodyText_ = std::move(body);

        if (detailsPanelText_)
        {
            const bool changed = SetWindowTextIfDifferent(detailsPanelText_, detailsPanelBodyText_);
            RefreshDetailsPanelBodyPresentation();
            if (changed)
            {
                SendMessageW(detailsPanelText_, EM_SETSEL, 0, 0);
                SendMessageW(detailsPanelText_, WM_VSCROLL, MAKEWPARAM(SB_TOP, 0), 0);
            }
        }

        if (hwnd_ && !IsRectEmpty(&detailsPanelRect_))
        {
            InvalidateRect(hwnd_, &detailsPanelRect_, FALSE);
        }
    }

    void MainWindow::RefreshDetailsPanelBodyPresentation()
    {
        if (!detailsPanelText_ || !detailsPanelRichEditModule_)
        {
            return;
        }

        const ThemePalette palette = GetThemePalette();
        SendMessageW(detailsPanelText_, EM_SETBKGNDCOLOR, FALSE, palette.paneBackground);
        SendMessageW(detailsPanelText_, WM_SETREDRAW, FALSE, 0);

        CHARRANGE allText{};
        allText.cpMin = 0;
        allText.cpMax = -1;
        SendMessageW(detailsPanelText_, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&allText));

        CHARFORMAT2W baseFormat{};
        baseFormat.cbSize = sizeof(baseFormat);
        baseFormat.dwMask = CFM_BOLD | CFM_COLOR;
        baseFormat.dwEffects = 0;
        baseFormat.crTextColor = palette.mutedText;
        SendMessageW(detailsPanelText_, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&baseFormat));

        const LRESULT lineCount = SendMessageW(detailsPanelText_, EM_GETLINECOUNT, 0, 0);
        for (LONG lineIndex = 0; lineIndex < lineCount; ++lineIndex)
        {
            const LONG lineStart = static_cast<LONG>(SendMessageW(detailsPanelText_, EM_LINEINDEX, lineIndex, 0));
            if (lineStart < 0)
            {
                continue;
            }

            const LONG lineLength = static_cast<LONG>(SendMessageW(detailsPanelText_, EM_LINELENGTH, lineStart, 0));
            if (lineLength <= 0)
            {
                continue;
            }

            std::vector<wchar_t> lineBuffer(static_cast<std::size_t>(lineLength) + 2, L'\0');
            *reinterpret_cast<WORD*>(lineBuffer.data()) = static_cast<WORD>(lineBuffer.size() - 1);
            const LRESULT copiedChars = SendMessageW(detailsPanelText_,
                                                     EM_GETLINE,
                                                     static_cast<WPARAM>(lineIndex),
                                                     reinterpret_cast<LPARAM>(lineBuffer.data()));
            if (copiedChars <= 0)
            {
                continue;
            }

            std::wstring_view line(lineBuffer.data(), static_cast<std::size_t>(copiedChars));
            LONG emphasisStart = lineStart;
            LONG emphasisEnd = lineStart + static_cast<LONG>(line.size());

            const std::size_t colon = line.find(L':');
            if (colon != std::wstring::npos)
            {
                std::size_t valueOffset = colon + 1;
                while (valueOffset < line.size() && iswspace(line[valueOffset]) != 0)
                {
                    ++valueOffset;
                }

                if (valueOffset >= line.size())
                {
                    continue;
                }

                emphasisStart = lineStart + static_cast<LONG>(valueOffset);
            }

            CHARRANGE emphasisRange{};
            emphasisRange.cpMin = emphasisStart;
            emphasisRange.cpMax = emphasisEnd;
            SendMessageW(detailsPanelText_, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&emphasisRange));

            CHARFORMAT2W emphasisFormat{};
            emphasisFormat.cbSize = sizeof(emphasisFormat);
            emphasisFormat.dwMask = CFM_BOLD | CFM_COLOR;
            emphasisFormat.dwEffects = CFE_BOLD;
            emphasisFormat.crTextColor = palette.text;
            SendMessageW(detailsPanelText_, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&emphasisFormat));
        }

        CHARRANGE resetSelection{};
        resetSelection.cpMin = 0;
        resetSelection.cpMax = 0;
        SendMessageW(detailsPanelText_, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&resetSelection));
        SendMessageW(detailsPanelText_, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(detailsPanelText_, nullptr, TRUE);
    }

    void MainWindow::ResetDetailsPanelHistogram()
    {
        detailsPanelHistogramPath_.clear();
        detailsPanelHistogramModifiedTimestampUtc_ = 0;
        detailsPanelHistogramModelIndex_ = -1;
        detailsPanelHistogramVisible_ = false;
        detailsPanelHistogramLoading_ = false;
        detailsPanelHistogramPeak_ = 0;
        detailsPanelHistogramRed_.fill(0);
        detailsPanelHistogramGreen_.fill(0);
        detailsPanelHistogramBlue_.fill(0);

        if (detailsPanelThumbnailScheduler_)
        {
            detailsPanelThumbnailScheduler_->CancelOutstanding();
        }
    }

    void MainWindow::RequestDetailsPanelHistogram(const browser::BrowserItem& item, int modelIndex)
    {
        if (!detailsPanelThumbnailScheduler_ || !decode::CanDecodeThumbnail(item) || modelIndex < 0)
        {
            ResetDetailsPanelHistogram();
            return;
        }

        const int targetWidth = 192;
        const int targetHeight = 128;
        const auto cacheKey = MakeThumbnailCacheKey(item, targetWidth, targetHeight);

        if (detailsPanelHistogramModelIndex_ == modelIndex
            && detailsPanelHistogramModifiedTimestampUtc_ == item.modifiedTimestampUtc
            && StringsEqualInsensitive(detailsPanelHistogramPath_, item.filePath)
            && (detailsPanelHistogramVisible_ || detailsPanelHistogramLoading_))
        {
            return;
        }

        detailsPanelHistogramPath_ = item.filePath;
        detailsPanelHistogramModifiedTimestampUtc_ = item.modifiedTimestampUtc;
        detailsPanelHistogramModelIndex_ = modelIndex;

        if (const auto cachedThumbnail = detailsPanelThumbnailScheduler_->FindCachedThumbnail(cacheKey))
        {
            ApplyDetailsPanelHistogram(*cachedThumbnail);
            LayoutChildren();
            return;
        }

        detailsPanelHistogramVisible_ = false;
        detailsPanelHistogramLoading_ = true;
        detailsPanelHistogramPeak_ = 0;
        detailsPanelHistogramRed_.fill(0);
        detailsPanelHistogramGreen_.fill(0);
        detailsPanelHistogramBlue_.fill(0);

        ++detailsPanelThumbnailRequestEpoch_;
        detailsPanelThumbnailScheduler_->Schedule(detailsPanelThumbnailSessionId_,
                                                 detailsPanelThumbnailRequestEpoch_,
                                                 {services::ThumbnailWorkItem{modelIndex, cacheKey, 0, true}});
        LayoutChildren();
    }

    void MainWindow::ApplyDetailsPanelHistogram(const cache::CachedThumbnail& thumbnail)
    {
        detailsPanelHistogramVisible_ = false;
        detailsPanelHistogramLoading_ = false;
        detailsPanelHistogramPeak_ = 0;
        detailsPanelHistogramRed_.fill(0);
        detailsPanelHistogramGreen_.fill(0);
        detailsPanelHistogramBlue_.fill(0);

        BITMAP bitmap{};
        if (!thumbnail.Bitmap() || GetObjectW(thumbnail.Bitmap(), sizeof(bitmap), &bitmap) == 0)
        {
            return;
        }

        const int bitmapWidth = bitmap.bmWidth;
        const int bitmapHeight = std::abs(bitmap.bmHeight);
        if (bitmapWidth <= 0 || bitmapHeight <= 0)
        {
            return;
        }

        BITMAPINFO bitmapInfo{};
        bitmapInfo.bmiHeader.biSize = sizeof(bitmapInfo.bmiHeader);
        bitmapInfo.bmiHeader.biWidth = bitmapWidth;
        bitmapInfo.bmiHeader.biHeight = -bitmapHeight;
        bitmapInfo.bmiHeader.biPlanes = 1;
        bitmapInfo.bmiHeader.biBitCount = 32;
        bitmapInfo.bmiHeader.biCompression = BI_RGB;

        std::vector<RGBQUAD> pixels(static_cast<std::size_t>(bitmapWidth) * static_cast<std::size_t>(bitmapHeight));
        HDC screenDc = GetDC(nullptr);
        if (!screenDc)
        {
            return;
        }

        const int copiedScanLines = GetDIBits(screenDc,
                                              thumbnail.Bitmap(),
                                              0,
                                              static_cast<UINT>(bitmapHeight),
                                              pixels.data(),
                                              &bitmapInfo,
                                              DIB_RGB_COLORS);
        ReleaseDC(nullptr, screenDc);
        if (copiedScanLines == 0)
        {
            return;
        }

        for (const RGBQUAD& pixel : pixels)
        {
            const std::size_t redIndex = std::min<std::size_t>(kDetailsPanelHistogramBins - 1, (pixel.rgbRed * kDetailsPanelHistogramBins) / 256);
            const std::size_t greenIndex = std::min<std::size_t>(kDetailsPanelHistogramBins - 1, (pixel.rgbGreen * kDetailsPanelHistogramBins) / 256);
            const std::size_t blueIndex = std::min<std::size_t>(kDetailsPanelHistogramBins - 1, (pixel.rgbBlue * kDetailsPanelHistogramBins) / 256);

            detailsPanelHistogramRed_[redIndex] += 1;
            detailsPanelHistogramGreen_[greenIndex] += 1;
            detailsPanelHistogramBlue_[blueIndex] += 1;
        }

        for (std::size_t index = 0; index < kDetailsPanelHistogramBins; ++index)
        {
            detailsPanelHistogramPeak_ = std::max(detailsPanelHistogramPeak_, detailsPanelHistogramRed_[index]);
            detailsPanelHistogramPeak_ = std::max(detailsPanelHistogramPeak_, detailsPanelHistogramGreen_[index]);
            detailsPanelHistogramPeak_ = std::max(detailsPanelHistogramPeak_, detailsPanelHistogramBlue_[index]);
        }

        detailsPanelHistogramVisible_ = detailsPanelHistogramPeak_ > 0;
    }

    void MainWindow::UpdateDetailsPanel()
    {
        if (!detailsStripVisible_)
        {
            ResetDetailsPanelHistogram();
            return;
        }

        if (!browserPaneController_ || !browserModel_)
        {
            ApplyDetailsPanelText(L"File Details", L"Select one or more images to inspect metadata.", L"");
            ResetDetailsPanelHistogram();
            LayoutChildren();
            return;
        }

        const std::vector<int> selectedModelIndices = browserPaneController_->OrderedSelectedModelIndicesSnapshot();
        if (selectedModelIndices.empty())
        {
            ApplyDetailsPanelText(L"File Details",
                                  L"Select one or more images to inspect camera, EXIF, and other image metadata.",
                                  L"");
            ResetDetailsPanelHistogram();
            LayoutChildren();
            return;
        }

        const auto& items = browserModel_->Items();
        std::vector<browser::BrowserItem> selectedItems;
        selectedItems.reserve(selectedModelIndices.size());
        constexpr std::size_t kMaxSharedMetadataItems = 100;
        std::vector<std::shared_ptr<const services::ImageMetadata>> metadataList;
        metadataList.reserve(std::min(selectedModelIndices.size(), kMaxSharedMetadataItems));
        bool allMetadataLoaded = true;
        std::uint64_t selectedBytes = 0;

        for (const int modelIndex : selectedModelIndices)
        {
            if (modelIndex < 0 || modelIndex >= static_cast<int>(items.size()))
            {
                continue;
            }

            selectedItems.push_back(items[static_cast<std::size_t>(modelIndex)]);
            selectedBytes += items[static_cast<std::size_t>(modelIndex)].fileSizeBytes;
            if (metadataList.size() < kMaxSharedMetadataItems)
            {
                if (items[static_cast<std::size_t>(modelIndex)].isDirectory)
                {
                    metadataList.push_back(nullptr);
                    continue;
                }

                const auto metadata = browserPaneController_->FindCachedMetadataForModelIndex(modelIndex);
                metadataList.push_back(metadata);
                allMetadataLoaded = allMetadataLoaded && static_cast<bool>(metadata);
            }
        }

        browserPaneController_->RequestMetadataForModelIndices(selectedModelIndices);

        if (selectedItems.empty())
        {
            ApplyDetailsPanelText(L"File Details", L"Select one or more images to inspect metadata.", L"");
            ResetDetailsPanelHistogram();
            LayoutChildren();
            return;
        }

        if (selectedItems.size() == 1)
        {
            const browser::BrowserItem& item = selectedItems.front();
            const auto metadata = metadataList.front();

            const std::wstring summary = BuildSingleSelectionSummary(item);

            std::wstring body = item.isDirectory
                ? L"Double-click this folder to open it."
                : (metadata ? services::FormatImageMetadataReport(item, *metadata)
                            : L"Loading detailed metadata...");

            if (userMetadataStore_)
            {
                const services::UserMetadataEntry metadataEntry = userMetadataStore_->EntryForPath(item.filePath);
                if (metadataEntry.rating > 0 || !metadataEntry.tags.empty())
                {
                    if (!body.empty())
                    {
                        body.append(L"\r\n\r\n");
                    }
                    body.append(L"User Metadata\r\n");
                    if (metadataEntry.rating > 0)
                    {
                        AppendLabeledLine(&body, L"Rating: ", FormatRatingForDisplay(metadataEntry.rating));
                    }
                    if (!metadataEntry.tags.empty())
                    {
                        AppendLabeledLine(&body, L"Tags: ", metadataEntry.tags);
                    }
                }
            }

            ApplyDetailsPanelText(item.fileName, std::move(summary), std::move(body));
            RequestDetailsPanelHistogram(item, selectedModelIndices.front());
            LayoutChildren();
            return;
        }

        const bool analysisTruncated = selectedItems.size() > metadataList.size();
        const std::vector<browser::BrowserItem> analysisItems(
            selectedItems.begin(),
            selectedItems.begin() + static_cast<std::ptrdiff_t>(metadataList.size()));

        std::wstring body;
        body.reserve(2048);
        body.append(L"Common Attributes\r\n");
        bool hasCommonAttributes = false;

        std::wstring commonValue;
        if (TryGetCommonItemString(analysisItems, [](const browser::BrowserItem& item) { return item.fileType; }, &commonValue))
        {
            AppendLabeledLine(&body, L"Type: ", commonValue);
            hasCommonAttributes = true;
        }
        if (TryGetCommonDimensions(analysisItems, &commonValue))
        {
            AppendLabeledLine(&body, L"Dimensions: ", commonValue);
            hasCommonAttributes = true;
        }
        if (TryGetCommonMetadataString(metadataList, [](const services::ImageMetadata& metadata) { return BuildCameraSummaryLabel(metadata); }, &commonValue))
        {
            AppendLabeledLine(&body, L"Camera: ", commonValue);
            hasCommonAttributes = true;
        }
        if (TryGetCommonMetadataString(metadataList, [](const services::ImageMetadata& metadata) { return metadata.dateTaken; }, &commonValue))
        {
            AppendLabeledLine(&body, L"Date Taken: ", commonValue);
            hasCommonAttributes = true;
        }
        if (TryGetCommonMetadataString(metadataList, [](const services::ImageMetadata& metadata) { return metadata.exposureTime; }, &commonValue))
        {
            AppendLabeledLine(&body, L"Exposure: ", commonValue);
            hasCommonAttributes = true;
        }
        if (TryGetCommonMetadataString(metadataList, [](const services::ImageMetadata& metadata) { return metadata.fNumber; }, &commonValue))
        {
            AppendLabeledLine(&body, L"Aperture: ", commonValue);
            hasCommonAttributes = true;
        }
        if (TryGetCommonMetadataString(metadataList, [](const services::ImageMetadata& metadata) { return metadata.isoSpeed; }, &commonValue))
        {
            AppendLabeledLine(&body, L"ISO: ", commonValue);
            hasCommonAttributes = true;
        }
        if (TryGetCommonMetadataString(metadataList, [](const services::ImageMetadata& metadata) { return metadata.focalLength; }, &commonValue))
        {
            AppendLabeledLine(&body, L"Focal Length: ", commonValue);
            hasCommonAttributes = true;
        }
        if (TryGetCommonMetadataString(metadataList, [](const services::ImageMetadata& metadata) { return metadata.title; }, &commonValue))
        {
            AppendLabeledLine(&body, L"Title: ", commonValue);
            hasCommonAttributes = true;
        }
        if (TryGetCommonMetadataString(metadataList, [](const services::ImageMetadata& metadata) { return metadata.author; }, &commonValue))
        {
            AppendLabeledLine(&body, L"Author: ", commonValue);
            hasCommonAttributes = true;
        }
        if (TryGetCommonMetadataString(metadataList, [](const services::ImageMetadata& metadata) { return metadata.keywords; }, &commonValue))
        {
            AppendLabeledLine(&body, L"Keywords: ", commonValue);
            hasCommonAttributes = true;
        }
        if (TryGetCommonMetadataString(metadataList, [](const services::ImageMetadata& metadata) { return metadata.comment; }, &commonValue))
        {
            AppendLabeledLine(&body, L"Comment: ", commonValue);
            hasCommonAttributes = true;
        }

        bool wroteSelectionMetadataHeader = false;
        if (userMetadataStore_)
        {
            bool firstRating = true;
            int selectionRating = 0;
            bool mixedRating = false;
            bool firstTags = true;
            std::wstring selectionTags;
            bool mixedTags = false;

            for (const browser::BrowserItem& item : selectedItems)
            {
                const services::UserMetadataEntry metadataEntry = userMetadataStore_->EntryForPath(item.filePath);
                const int candidateRating = std::clamp(metadataEntry.rating, 0, 5);
                if (firstRating)
                {
                    selectionRating = candidateRating;
                    firstRating = false;
                }
                else if (candidateRating != selectionRating)
                {
                    mixedRating = true;
                }

                if (firstTags)
                {
                    selectionTags = metadataEntry.tags;
                    firstTags = false;
                }
                else if (!util::EqualsIgnoreCaseOrdinal(selectionTags, metadataEntry.tags))
                {
                    mixedTags = true;
                }
            }

            const bool showRating = mixedRating || selectionRating > 0;
            const bool showTags = mixedTags || !selectionTags.empty();
            if (showRating || showTags)
            {
                body.append(L"\r\nSelection Metadata\r\n");
                wroteSelectionMetadataHeader = true;
            }
            if (showRating)
            {
                AppendLabeledLine(&body, L"Rating: ", mixedRating ? std::wstring(L"Mixed") : FormatRatingForDisplay(selectionRating));
            }
            if (showTags)
            {
                AppendLabeledLine(&body, L"Tags: ", mixedTags ? std::wstring(L"Mixed") : selectionTags);
            }
        }

        if (allMetadataLoaded)
        {
            bool wroteAdditionalHeader = false;
            std::vector<services::MetadataPropertyEntry> renderedCommonProperties;
            for (const services::MetadataPropertyEntry& property : FindCommonMetadataProperties(metadataList))
            {
                if (property.value.empty() || IsCuratedDetailsProperty(property.canonicalName))
                {
                    continue;
                }

                if (HasEquivalentDisplayedProperty(renderedCommonProperties, property))
                {
                    continue;
                }

                if (!wroteAdditionalHeader)
                {
                    body.append(L"\r\nCommon Additional Metadata\r\n");
                    wroteAdditionalHeader = true;
                }
                AppendLabeledLine(&body, property.displayName + L": ", property.value);
                renderedCommonProperties.push_back(property);
                hasCommonAttributes = true;
            }
        }
        else
        {
            body.append(L"\r\nLoading detailed metadata for the current selection...\r\n");
        }

        if (!hasCommonAttributes && !wroteSelectionMetadataHeader)
        {
            body.append(L"No common file or metadata attributes are shared by the current selection.");
        }

        if (analysisTruncated)
        {
            body.append(L"\r\nNote: Shared attributes are based on the first ");
            body.append(std::to_wstring(kMaxSharedMetadataItems));
            body.append(L" of ");
            body.append(std::to_wstring(selectedItems.size()));
            body.append(L" selected files.");
        }

        ApplyDetailsPanelText(std::to_wstring(selectedItems.size()) + L" Files Selected",
                              std::to_wstring(selectedItems.size()) + L" items | " + browser::FormatByteSize(selectedBytes),
                              std::move(body));
        ResetDetailsPanelHistogram();
        LayoutChildren();
    }

    void MainWindow::PaintDetailsPanel(HDC hdc, const RECT& clientRect) const
    {
        (void)clientRect;
        if (!detailsStripVisible_ || IsRectEmpty(&detailsPanelRect_))
        {
            return;
        }

        const ThemePalette palette = GetThemePalette();
        FillRect(hdc,
                 &detailsPanelRect_,
                 detailsPanelBrush_ ? detailsPanelBrush_ : (backgroundBrush_ ? backgroundBrush_ : reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1)));

        HPEN borderPen = CreatePen(PS_SOLID, 1, palette.actionStripBorder);
        HGDIOBJ oldPen = SelectObject(hdc, borderPen);
        MoveToEx(hdc, detailsPanelRect_.left, detailsPanelRect_.top, nullptr);
        LineTo(hdc, detailsPanelRect_.left, detailsPanelRect_.bottom);
        SelectObject(hdc, oldPen);
        DeleteObject(borderPen);

        if (!IsRectEmpty(&detailsPanelTabStripRect_))
        {
            const COLORREF inactiveFill = BlendColor(palette.actionFieldBackground,
                                                     palette.paneBackground,
                                                     themeMode_ == ThemeMode::Dark ? 24 : 12);
            const COLORREF inactiveBorder = BlendColor(palette.actionStripBorder,
                                                       palette.paneBackground,
                                                       themeMode_ == ThemeMode::Dark ? 32 : 16);
            const COLORREF hoverFill = BlendColor(inactiveFill,
                                                  palette.accentFill,
                                                  themeMode_ == ThemeMode::Dark ? 24 : 14);
            const COLORREF pressedFill = BlendColor(inactiveFill,
                                                    palette.accent,
                                                    themeMode_ == ThemeMode::Dark ? 40 : 18);
            const COLORREF activePressedFill = BlendColor(palette.accentFill,
                                                          palette.accent,
                                                          themeMode_ == ThemeMode::Dark ? 22 : 12);
            const wchar_t* labels[] = {L"File Details", L"Quick Send"};

            for (std::size_t index = 0; index < detailsPanelTabRects_.size(); ++index)
            {
                const RECT& tabRect = detailsPanelTabRects_[index];
                if (IsRectEmpty(&tabRect))
                {
                    continue;
                }

                const bool active = static_cast<int>(index) == static_cast<int>(activeRightPaneTab_);
                const bool hot = static_cast<int>(index) == detailsPanelHotTabIndex_;
                const bool pressed = static_cast<int>(index) == detailsPanelPressedTabIndex_;
                const COLORREF fillColor = active
                    ? (pressed ? activePressedFill : palette.accentFill)
                    : (pressed ? pressedFill : (hot ? hoverFill : inactiveFill));
                const COLORREF borderColor = active
                    ? palette.accent
                    : ((hot || pressed)
                        ? BlendColor(inactiveBorder, palette.accent, themeMode_ == ThemeMode::Dark ? 44 : 24)
                        : inactiveBorder);
                const COLORREF textColor = active
                    ? palette.accentText
                    : ((hot || pressed) ? palette.text : palette.mutedText);

                HBRUSH tabBrush = CreateSolidBrush(fillColor);
                HPEN tabPen = CreatePen(PS_SOLID, 1, borderColor);
                const HGDIOBJ oldBrush = SelectObject(hdc, tabBrush);
                const HGDIOBJ oldTabPen = SelectObject(hdc, tabPen);
                RoundRect(hdc, tabRect.left, tabRect.top, tabRect.right, tabRect.bottom, 14, 14);
                SelectObject(hdc, oldTabPen);
                SelectObject(hdc, oldBrush);
                DeleteObject(tabPen);
                DeleteObject(tabBrush);

                RECT textRect = tabRect;
                hyperbrowse::render::DrawGdiText(hdc,
                                    detailsPanelSummaryFont_ ? detailsPanelSummaryFont_ : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)),
                                    labels[index],
                                    -1,
                                    textRect,
                                    DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS,
                                    textColor,
                                    fillColor);
            }
        }

        if (!IsRectEmpty(&detailsPanelCloseButtonRect_))
        {
            const bool hot = detailsPanelCloseButtonHot_;
            const bool pressed = detailsPanelCloseButtonPressed_;
            const COLORREF fillColor = pressed
                ? BlendColor(palette.actionFieldBackground, palette.accentFill, themeMode_ == ThemeMode::Dark ? 24 : 16)
                : (hot
                    ? BlendColor(palette.actionFieldBackground, palette.accentFill, themeMode_ == ThemeMode::Dark ? 14 : 10)
                    : palette.actionFieldBackground);
            const COLORREF borderColor = hot || pressed ? palette.accent : palette.actionStripBorder;
            const COLORREF textColor = hot || pressed ? palette.accentText : palette.mutedText;

            HBRUSH buttonBrush = CreateSolidBrush(fillColor);
            HPEN buttonPen = CreatePen(PS_SOLID, 1, borderColor);
            const HGDIOBJ oldBrush = SelectObject(hdc, buttonBrush);
            const HGDIOBJ oldButtonPen = SelectObject(hdc, buttonPen);
            RoundRect(hdc,
                      detailsPanelCloseButtonRect_.left,
                      detailsPanelCloseButtonRect_.top,
                      detailsPanelCloseButtonRect_.right,
                      detailsPanelCloseButtonRect_.bottom,
                      6,
                      6);
            SelectObject(hdc, oldButtonPen);
            SelectObject(hdc, oldBrush);
            DeleteObject(buttonPen);
            DeleteObject(buttonBrush);

            const int inset = 5;
            const int left = detailsPanelCloseButtonRect_.left + inset;
            const int top = detailsPanelCloseButtonRect_.top + inset;
            const int right = detailsPanelCloseButtonRect_.right - inset;
            const int bottom = detailsPanelCloseButtonRect_.bottom - inset;
            HPEN xPen = CreatePen(PS_SOLID, 1, textColor);
            const HGDIOBJ oldXPen = SelectObject(hdc, xPen);
            MoveToEx(hdc, left, top, nullptr);
            LineTo(hdc, right, bottom);
            MoveToEx(hdc, left, bottom, nullptr);
            LineTo(hdc, right, top);
            SelectObject(hdc, oldXPen);
            DeleteObject(xPen);
        }

        SelectObject(hdc, detailsPanelTitleFont_ ? detailsPanelTitleFont_ : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)));
        if (activeRightPaneTab_ == RightPaneTab::FileDetails && !IsRectEmpty(&detailsPanelContentRect_))
        {
            const int innerLeft = detailsPanelContentRect_.left;
            const int innerRight = detailsPanelContentRect_.right;
            const int innerWidth = std::max(0, innerRight - innerLeft);

            const std::wstring title = detailsPanelTitleText_.empty() ? std::wstring(L"File Details") : detailsPanelTitleText_;
            const int titleHeight = MeasureTextBlockHeight(detailsPanelTitleFont_,
                                                           title,
                                                           innerWidth,
                                                           DT_LEFT | DT_NOPREFIX | DT_WORDBREAK,
                                                           22);
            RECT titleRect{innerLeft, detailsPanelContentRect_.top, innerRight, detailsPanelContentRect_.top + titleHeight};

            hyperbrowse::render::DrawGdiText(hdc,
                                detailsPanelTitleFont_ ? detailsPanelTitleFont_ : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)),
                                title.c_str(),
                                -1,
                                titleRect,
                                DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK,
                                palette.text,
                                palette.paneBackground);

            const int summaryTop = titleRect.bottom + 6;
            if (!detailsPanelSummaryText_.empty())
            {
                const int summaryHeight = MeasureTextBlockHeight(detailsPanelSummaryFont_,
                                                                 detailsPanelSummaryText_,
                                                                 innerWidth,
                                                                 DT_LEFT | DT_NOPREFIX | DT_WORDBREAK,
                                                                 18);
                RECT summaryRect{innerLeft, summaryTop, innerRight, summaryTop + summaryHeight};
                hyperbrowse::render::DrawGdiText(hdc,
                                    detailsPanelSummaryFont_ ? detailsPanelSummaryFont_ : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)),
                                    detailsPanelSummaryText_.c_str(),
                                    -1,
                                    summaryRect,
                                    DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK,
                                    palette.mutedText,
                                    palette.paneBackground);
            }

            if ((detailsPanelHistogramVisible_ || detailsPanelHistogramLoading_) && !IsRectEmpty(&detailsPanelHistogramRect_))
            {
                const COLORREF histogramBackground = BlendColor(palette.actionFieldBackground, palette.paneBackground, themeMode_ == ThemeMode::Dark ? 24 : 12);
                HBRUSH histogramBrush = CreateSolidBrush(histogramBackground);
                FillRect(hdc, &detailsPanelHistogramRect_, histogramBrush);
                DeleteObject(histogramBrush);

                HPEN histogramBorderPen = CreatePen(PS_SOLID, 1, palette.actionStripBorder);
                oldPen = SelectObject(hdc, histogramBorderPen);
                MoveToEx(hdc, detailsPanelHistogramRect_.left, detailsPanelHistogramRect_.top, nullptr);
                LineTo(hdc, detailsPanelHistogramRect_.right, detailsPanelHistogramRect_.top);
                LineTo(hdc, detailsPanelHistogramRect_.right, detailsPanelHistogramRect_.bottom);
                LineTo(hdc, detailsPanelHistogramRect_.left, detailsPanelHistogramRect_.bottom);
                LineTo(hdc, detailsPanelHistogramRect_.left, detailsPanelHistogramRect_.top);
                SelectObject(hdc, oldPen);
                DeleteObject(histogramBorderPen);

                RECT histogramTextRect = detailsPanelHistogramRect_;
                InflateRect(&histogramTextRect, -8, -8);
                if (detailsPanelHistogramLoading_)
                {
                    hyperbrowse::render::DrawGdiText(hdc,
                                        detailsPanelSummaryFont_ ? detailsPanelSummaryFont_ : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)),
                                        L"Loading histogram...",
                                        -1,
                                        histogramTextRect,
                                        DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX,
                                        palette.mutedText,
                                        histogramBackground);
                }
                else if (!detailsPanelHistogramVisible_ || detailsPanelHistogramPeak_ == 0)
                {
                    render::DrawGdiText(hdc,
                                        detailsPanelSummaryFont_ ? detailsPanelSummaryFont_ : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)),
                                        L"Histogram unavailable",
                                        -1,
                                        histogramTextRect,
                                        DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX,
                                        palette.mutedText,
                                        histogramBackground);
                }
                else
                {
                    const int chartLeft = detailsPanelHistogramRect_.left + 6;
                    const int chartTop = detailsPanelHistogramRect_.top + 6;
                    const int chartRight = detailsPanelHistogramRect_.right - 6;
                    const int chartBottom = detailsPanelHistogramRect_.bottom - 6;
                    const int chartWidth = std::max(1, chartRight - chartLeft);
                    const int chartHeight = std::max(1, chartBottom - chartTop);

                    auto drawChannel = [&](const std::array<std::uint32_t, 64>& values, COLORREF color)
                    {
                        HPEN channelPen = CreatePen(PS_SOLID, 1, color);
                        HGDIOBJ oldChannelPen = SelectObject(hdc, channelPen);
                        for (int index = 0; index < kDetailsPanelHistogramBins; ++index)
                        {
                            const int x = chartLeft + MulDiv(index, chartWidth - 1, kDetailsPanelHistogramBins - 1);
                            const int valueHeight = detailsPanelHistogramPeak_ > 0
                                ? MulDiv(static_cast<int>(values[static_cast<std::size_t>(index)]), chartHeight - 1, static_cast<int>(detailsPanelHistogramPeak_))
                                : 0;
                            const int y = chartBottom - valueHeight;
                            if (index == 0)
                            {
                                MoveToEx(hdc, x, y, nullptr);
                            }
                            else
                            {
                                LineTo(hdc, x, y);
                            }
                        }
                        SelectObject(hdc, oldChannelPen);
                        DeleteObject(channelPen);
                    };

                    drawChannel(detailsPanelHistogramRed_, RGB(224, 98, 92));
                    drawChannel(detailsPanelHistogramGreen_, RGB(112, 188, 102));
                    drawChannel(detailsPanelHistogramBlue_, RGB(92, 150, 232));
                }
            }
        }

        if (activeRightPaneTab_ == RightPaneTab::QuickSend && !quickAccessDestinationRows_.empty() && !IsRectEmpty(&quickAccessDestinationPanelRect_))
        {
            const QuickAccessPanelMetrics metrics = BuildQuickAccessPanelMetrics(detailsPanelSummaryFont_, detailsPanelBodyFont_);
            RECT headerRect{quickAccessDestinationPanelRect_.left,
                            quickAccessDestinationPanelRect_.top,
                            quickAccessDestinationPanelRect_.right,
                            quickAccessDestinationPanelRect_.top + metrics.headerHeight};
            if (!IsRectEmpty(&quickAccessSortButtonRect_))
            {
                headerRect.right = quickAccessSortButtonRect_.left - kQuickAccessPanelSortButtonGap;
            }
            render::DrawGdiText(hdc,
                                detailsPanelSummaryFont_ ? detailsPanelSummaryFont_ : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)),
                                L"Quick Send",
                                -1,
                                headerRect,
                                DT_LEFT | DT_TOP | DT_NOPREFIX | DT_SINGLELINE,
                                palette.mutedText,
                                palette.paneBackground);

            if (!IsRectEmpty(&quickAccessSortButtonRect_))
            {
                const bool hot = quickAccessSortButtonHot_;
                const bool pressed = quickAccessSortButtonPressed_;
                if (hot || pressed)
                {
                    const COLORREF fillColor = pressed
                        ? BlendColor(palette.accentFill, palette.accent, themeMode_ == ThemeMode::Dark ? 40 : 18)
                        : BlendColor(palette.actionFieldBackground, palette.accentFill, themeMode_ == ThemeMode::Dark ? 28 : 16);
                    RECT buttonRect = quickAccessSortButtonRect_;
                    InflateRect(&buttonRect, -1, -1);
                    HBRUSH buttonBrush = CreateSolidBrush(fillColor);
                    HPEN buttonPen = CreatePen(PS_SOLID, 1, palette.accent);
                    const HGDIOBJ oldSortBrush = SelectObject(hdc, buttonBrush);
                    const HGDIOBJ oldSortPen = SelectObject(hdc, buttonPen);
                    RoundRect(hdc, buttonRect.left, buttonRect.top, buttonRect.right, buttonRect.bottom, 8, 8);
                    SelectObject(hdc, oldSortPen);
                    SelectObject(hdc, oldSortBrush);
                    DeleteObject(buttonPen);
                    DeleteObject(buttonBrush);
                }

                HDC iconDC = toolbarIconLibrary_ ? CreateCompatibleDC(hdc) : nullptr;
                if (toolbarIconLibrary_ && iconDC)
                {
                    const COLORREF iconColor = hot || pressed ? palette.accentText : palette.mutedText;
                    const int iconSize = 14;
                    const int iconX = quickAccessSortButtonRect_.left
                        + ((quickAccessSortButtonRect_.right - quickAccessSortButtonRect_.left) - iconSize) / 2;
                    const int iconY = quickAccessSortButtonRect_.top
                        + ((quickAccessSortButtonRect_.bottom - quickAccessSortButtonRect_.top) - iconSize) / 2;
                    const HBITMAP sortBitmap = toolbarIconLibrary_->GetBitmap("sort", iconSize, iconColor);
                    AlphaBlendBitmap(hdc, iconDC, sortBitmap, iconX, iconY, iconSize, iconSize);
                }
                if (iconDC)
                {
                    DeleteDC(iconDC);
                }
            }

            const COLORREF rowBackground = BlendColor(palette.actionFieldBackground, palette.paneBackground, themeMode_ == ThemeMode::Dark ? 24 : 12);
            const COLORREF rowBorder = palette.actionStripBorder;
            const COLORREF enabledButtonFill = palette.accentFill;
            const COLORREF enabledButtonText = palette.accentText;
            const COLORREF disabledButtonFill = BlendColor(palette.actionFieldBackground, palette.paneBackground, themeMode_ == ThemeMode::Dark ? 10 : 20);

            auto drawActionButton = [&](const RECT& rect,
                                        const wchar_t* label,
                                        int buttonIndex,
                                        bool enabled,
                                        COLORREF baseFill,
                                        COLORREF baseText,
                                        COLORREF enabledBorder)
            {
                const bool hot = buttonIndex == quickAccessHotButtonIndex_;
                const bool pressed = buttonIndex == quickAccessPressedButtonIndex_;
                const COLORREF fillColor = enabled
                    ? (pressed ? palette.accent : (hot ? BlendColor(baseFill, palette.accent, 48) : baseFill))
                    : disabledButtonFill;
                const COLORREF textColor = enabled ? baseText : palette.mutedText;

                HBRUSH buttonBrush = CreateSolidBrush(fillColor);
                HPEN buttonPen = CreatePen(PS_SOLID, 1, enabled ? enabledBorder : rowBorder);
                const HGDIOBJ oldBrush = SelectObject(hdc, buttonBrush);
                const HGDIOBJ oldButtonPen = SelectObject(hdc, buttonPen);
                RoundRect(hdc, rect.left, rect.top, rect.right, rect.bottom, 10, 10);
                SelectObject(hdc, oldButtonPen);
                SelectObject(hdc, oldBrush);
                DeleteObject(buttonPen);
                DeleteObject(buttonBrush);

                RECT textRect = rect;
                render::DrawGdiText(hdc,
                                    detailsPanelBodyFont_ ? detailsPanelBodyFont_ : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)),
                                    label,
                                    -1,
                                    textRect,
                                    DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX,
                                    textColor,
                                    fillColor);
            };

            const int savedDC = SaveDC(hdc);
            IntersectClipRect(hdc,
                              quickAccessDestinationViewportRect_.left,
                              quickAccessDestinationViewportRect_.top,
                              quickAccessDestinationViewportRect_.right,
                              quickAccessDestinationViewportRect_.bottom);
            for (std::size_t rowIndex = 0; rowIndex < quickAccessDestinationRows_.size(); ++rowIndex)
            {
                const QuickAccessDestinationRow& row = quickAccessDestinationRows_[rowIndex];
                const bool rowEnabled = CanNavigateToQuickAccessDestination(row.destinationPath);
                const bool actionsEnabled = CanUseQuickAccessDestinationActions(row.destinationPath);
                const bool rowHot = rowEnabled && static_cast<int>(rowIndex) == quickAccessHotRowIndex_;
                const bool rowPressed = rowEnabled && static_cast<int>(rowIndex) == quickAccessPressedRowIndex_;
                const COLORREF currentRowBackground = rowPressed
                    ? BlendColor(rowBackground, palette.accentFill, themeMode_ == ThemeMode::Dark ? 28 : 18)
                    : (rowHot
                        ? BlendColor(rowBackground, palette.accentFill, themeMode_ == ThemeMode::Dark ? 20 : 12)
                        : rowBackground);
                HBRUSH rowBrush = CreateSolidBrush(currentRowBackground);
                HPEN rowPen = CreatePen(PS_SOLID, 1, rowHot ? palette.accent : rowBorder);
                const HGDIOBJ oldBrush = SelectObject(hdc, rowBrush);
                const HGDIOBJ oldRowPen = SelectObject(hdc, rowPen);
                RoundRect(hdc, row.rowRect.left, row.rowRect.top, row.rowRect.right, row.rowRect.bottom, 12, 12);
                SelectObject(hdc, oldRowPen);
                SelectObject(hdc, oldBrush);
                DeleteObject(rowPen);
                DeleteObject(rowBrush);

                RECT labelRect = row.rowRect;
                labelRect.left += 10;
                labelRect.top += metrics.labelTopInset;
                labelRect.right = row.shortcutRect.left - 10;
                labelRect.bottom = labelRect.top + metrics.labelHeight;
                render::DrawGdiText(hdc,
                                    detailsPanelSummaryFont_ ? detailsPanelSummaryFont_ : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)),
                                    row.displayLabel.c_str(),
                                    -1,
                                    labelRect,
                                    DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX,
                                    rowEnabled ? palette.text : palette.mutedText,
                                    currentRowBackground);

                RECT metadataRect = row.rowRect;
                metadataRect.left += 10;
                metadataRect.top += metrics.metadataTopInset;
                metadataRect.right = row.copyRect.left - 10;
                metadataRect.bottom -= metrics.metadataBottomInset;
                render::DrawGdiText(hdc,
                                    detailsPanelBodyFont_ ? detailsPanelBodyFont_ : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)),
                                    row.metadataLabel.c_str(),
                                    -1,
                                    metadataRect,
                                    DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX,
                                    palette.mutedText,
                                    currentRowBackground);

                drawActionButton(row.copyRect,
                                 L"Copy",
                                 static_cast<int>(rowIndex * 3),
                                 actionsEnabled,
                                 enabledButtonFill,
                                 enabledButtonText,
                                 palette.accent);
                drawActionButton(row.moveRect,
                                 L"Move",
                                 static_cast<int>(rowIndex * 3 + 1),
                                 actionsEnabled,
                                 enabledButtonFill,
                                 enabledButtonText,
                                 palette.accent);
                drawActionButton(row.removeRect,
                                 L"x",
                                 static_cast<int>(rowIndex * 3 + 2),
                                 true,
                                 BlendColor(rowBackground, palette.actionFieldBackground, themeMode_ == ThemeMode::Dark ? 12 : 20),
                                 palette.mutedText,
                                 rowBorder);
            }
            RestoreDC(hdc, savedDC);
        }
        else if (activeRightPaneTab_ == RightPaneTab::QuickSend && !IsRectEmpty(&detailsPanelContentRect_))
        {
            RECT emptyStateRect = detailsPanelContentRect_;
            render::DrawGdiText(hdc,
                                detailsPanelSummaryFont_ ? detailsPanelSummaryFont_ : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)),
                                L"Favorite destinations will appear here.",
                                -1,
                                emptyStateRect,
                                DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK,
                                palette.mutedText,
                                palette.paneBackground);
        }
    }

    void MainWindow::RefreshBrowserPane()
    {
        if (!browserPaneController_)
        {
            return;
        }

        browserPaneController_->SetModel(browserModel_.get());
        browserPaneController_->SetViewMode(browserMode_ == BrowserMode::Thumbnails
            ? browser::BrowserViewMode::Thumbnails
            : browser::BrowserViewMode::Details);
        browserPaneController_->SetSortMode(sortMode_);
        browserPaneController_->SetSortAscending(sortAscending_);
        ApplyRawJpegPairingSettings();
        browserPaneController_->RefreshFromModel();
        UpdateDetailsPanel();
    }

    bool MainWindow::ShouldDefaultViewerToSecondaryMonitor() const
    {
        if (!defaultViewerToSecondaryMonitor_)
        {
            return false;
        }
        return FindAlternateMonitorForWindow(hwnd_) != nullptr;
    }

    browser::BrowserItem MainWindow::ResolvePairedRawJpegViewerItem(
        const browser::BrowserItem& item,
        browser::RawJpegDisplayPreference preference) const
    {
        if (!rawJpegPairedOperationsEnabled_ || !browserModel_)
        {
            return item;
        }

        const bool itemIsRaw = decode::IsRawFileType(item.fileType);
        const bool itemIsJpeg = IsJpegFileType(item.fileType);
        if (!itemIsRaw && !itemIsJpeg)
        {
            return item;
        }

        const bool preferRaw = preference == browser::RawJpegDisplayPreference::Raw;
        if ((preferRaw && itemIsRaw) || (!preferRaw && itemIsJpeg))
        {
            return item;
        }

        const fs::path itemPath(item.filePath);
        const std::wstring itemParent = itemPath.parent_path().wstring();
        const std::wstring itemStem = itemPath.stem().wstring();
        for (const browser::BrowserItem& candidate : browserModel_->Items())
        {
            if (browser::FilePathsEqual(candidate.filePath, item.filePath))
            {
                continue;
            }

            if (!FolderPathsEqual(fs::path(candidate.filePath).parent_path().wstring(), itemParent)
                || !StringsEqualInsensitive(fs::path(candidate.filePath).stem().wstring(), itemStem))
            {
                continue;
            }

            if (preferRaw && decode::IsRawFileType(candidate.fileType))
            {
                return candidate;
            }
            if (!preferRaw && IsJpegFileType(candidate.fileType))
            {
                return candidate;
            }
        }

        return item;
    }

    std::vector<browser::BrowserItem> MainWindow::ResolvePairedRawJpegViewerItems(
        std::vector<browser::BrowserItem> items,
        bool startSlideshow) const
    {
        if (!rawJpegPairedOperationsEnabled_ || items.empty())
        {
            return items;
        }

        const browser::RawJpegDisplayPreference preference = startSlideshow
            ? browser::RawJpegDisplayPreference::Jpeg
            : pairedRawJpegViewerPreference_;
        for (browser::BrowserItem& item : items)
        {
            item = ResolvePairedRawJpegViewerItem(item, preference);
        }

        return items;
    }

    void MainWindow::OpenItemInViewer(int modelIndex, bool preferSecondaryMonitor)
    {
        if (!browserModel_ || !browserPaneController_)
        {
            return;
        }

        const auto& modelItems = browserModel_->Items();
        if (modelIndex < 0 || modelIndex >= static_cast<int>(modelItems.size()))
        {
            return;
        }

        const browser::BrowserItem& selectedItem = modelItems[static_cast<std::size_t>(modelIndex)];
        if (selectedItem.isDirectory)
        {
            LoadFolderAsync(selectedItem.filePath);
            return;
        }

        if (!viewerWindow_)
        {
            return;
        }

        std::vector<int> orderedModelIndices = browserPaneController_->OrderedModelIndicesSnapshot();
        if (orderedModelIndices.empty())
        {
            orderedModelIndices.reserve(modelItems.size());
            for (std::size_t index = 0; index < modelItems.size(); ++index)
            {
                orderedModelIndices.push_back(static_cast<int>(index));
            }
        }

        std::vector<browser::BrowserItem> viewerItems;
        viewerItems.reserve(orderedModelIndices.size());
        int selectedViewerIndex = -1;
        for (int orderedModelIndex : orderedModelIndices)
        {
            if (orderedModelIndex < 0 || orderedModelIndex >= static_cast<int>(modelItems.size()))
            {
                continue;
            }

            viewerItems.push_back(modelItems[static_cast<std::size_t>(orderedModelIndex)]);
            if (orderedModelIndex == modelIndex)
            {
                selectedViewerIndex = static_cast<int>(viewerItems.size()) - 1;
            }
        }

        if (selectedViewerIndex < 0)
        {
            return;
        }

        OpenItemsInViewer(std::move(viewerItems), selectedViewerIndex, false, preferSecondaryMonitor);
    }

    bool MainWindow::OpenItemsInViewer(std::vector<browser::BrowserItem> items,
                                       int selectedIndex,
                                       bool startSlideshow,
                                       bool preferSecondaryMonitor,
                                       bool resolvePairedRawJpegItems)
    {
        if (std::any_of(items.begin(), items.end(), [](const browser::BrowserItem& item)
        {
            return item.isDirectory;
        }))
        {
            std::vector<browser::BrowserItem> imageItems;
            imageItems.reserve(items.size());
            int imageSelectedIndex = -1;
            for (int index = 0; index < static_cast<int>(items.size()); ++index)
            {
                if (items[static_cast<std::size_t>(index)].isDirectory)
                {
                    continue;
                }

                if (index == selectedIndex)
                {
                    imageSelectedIndex = static_cast<int>(imageItems.size());
                }
                imageItems.push_back(std::move(items[static_cast<std::size_t>(index)]));
            }
            items = std::move(imageItems);
            selectedIndex = imageSelectedIndex;
        }

        if (!viewerWindow_ || items.empty() || selectedIndex < 0 || selectedIndex >= static_cast<int>(items.size()))
        {
            return false;
        }

        if (resolvePairedRawJpegItems)
        {
            items = ResolvePairedRawJpegViewerItems(std::move(items), startSlideshow);
        }

        const HMONITOR targetMonitor = ResolveViewerMonitor(hwnd_, preferSecondaryMonitor);
        if (preferSecondaryMonitor && !targetMonitor)
        {
            MessageBoxW(hwnd_,
                        L"A secondary monitor is not currently available.",
                        L"View on Secondary Monitor",
                        MB_OK | MB_ICONINFORMATION);
            return false;
        }

        ApplyViewerMouseWheelSetting();
        ApplyViewerTransitionSettings();
        viewerWindow_->SetAppTextSize(appTextSize_);
        viewerWindow_->SetResourceProfile(resourceProfile_);
        viewerWindow_->SetMemoryPressureActive(thumbnailMemoryPressureActive_);
        if (viewerWindow_->Open(hwnd_, std::move(items), selectedIndex, themeMode_ == ThemeMode::Dark, targetMonitor))
        {
            if (startSlideshow)
            {
                viewerWindow_->StartSlideshow(slideshowIntervalMs_);
            }
            viewerWindowActive_ = true;
            UpdateStatusText();
            return true;
        }

        return false;
    }

    bool MainWindow::SyncViewerToBrowserModel(std::wstring_view preferredPath)
    {
        if (!viewerWindow_ || !viewerWindow_->IsOpen() || !browserModel_)
        {
            return false;
        }

        const auto& modelItems = browserModel_->Items();
        if (modelItems.empty())
        {
            const HWND viewerHwnd = viewerWindow_->Hwnd();
            if (viewerHwnd && IsWindow(viewerHwnd) != FALSE)
            {
                PostMessageW(viewerHwnd, WM_CLOSE, 0, 0);
            }
            return false;
        }

        std::vector<int> orderedModelIndices = browserPaneController_
            ? browserPaneController_->OrderedModelIndicesSnapshot()
            : std::vector<int>{};
        if (orderedModelIndices.empty())
        {
            orderedModelIndices.reserve(modelItems.size());
            for (std::size_t index = 0; index < modelItems.size(); ++index)
            {
                orderedModelIndices.push_back(static_cast<int>(index));
            }
        }

        const std::wstring currentViewerPath = viewerWindow_->CurrentFilePath();
        const int currentViewerIndex = viewerWindow_->CurrentIndex();
        std::vector<browser::BrowserItem> viewerItems;
        viewerItems.reserve(orderedModelIndices.size());
        int selectedViewerIndex = -1;
        int currentPathIndex = -1;

        for (int orderedModelIndex : orderedModelIndices)
        {
            if (orderedModelIndex < 0 || orderedModelIndex >= static_cast<int>(modelItems.size()))
            {
                continue;
            }

            viewerItems.push_back(modelItems[static_cast<std::size_t>(orderedModelIndex)]);
            const int viewerIndex = static_cast<int>(viewerItems.size()) - 1;
            const std::wstring& path = viewerItems.back().filePath;
            if (selectedViewerIndex < 0
                && !preferredPath.empty()
                && browser::FilePathsEqual(path, preferredPath))
            {
                selectedViewerIndex = viewerIndex;
            }
            if (currentPathIndex < 0
                && !currentViewerPath.empty()
                && browser::FilePathsEqual(path, currentViewerPath))
            {
                currentPathIndex = viewerIndex;
            }
        }

        if (viewerItems.empty())
        {
            const HWND viewerHwnd = viewerWindow_->Hwnd();
            if (viewerHwnd && IsWindow(viewerHwnd) != FALSE)
            {
                PostMessageW(viewerHwnd, WM_CLOSE, 0, 0);
            }
            return false;
        }

        if (selectedViewerIndex < 0)
        {
            selectedViewerIndex = currentPathIndex;
        }
        if (selectedViewerIndex < 0 && currentViewerIndex >= 0 && currentViewerIndex < static_cast<int>(viewerItems.size()))
        {
            selectedViewerIndex = currentViewerIndex;
        }
        if (selectedViewerIndex < 0)
        {
            selectedViewerIndex = 0;
        }

        viewerItems = ResolvePairedRawJpegViewerItems(std::move(viewerItems), viewerWindow_->IsSlideshowActive());
        return viewerWindow_->ReplaceItems(std::move(viewerItems), selectedViewerIndex);
    }

    void MainWindow::RebuildQuickAccessDestinationRows(int innerLeft, int innerRight, int top)
    {
        quickAccessDestinationRows_.clear();
        quickAccessDestinationPanelRect_ = RECT{};
        quickAccessDestinationViewportRect_ = RECT{};
        quickAccessSortButtonRect_ = RECT{};
        quickAccessSortButtonHot_ = false;
        quickAccessSortButtonPressed_ = false;
        quickAccessHotRowIndex_ = -1;
        quickAccessHotButtonIndex_ = -1;
        quickAccessPressedRowIndex_ = -1;
        quickAccessPressedButtonIndex_ = -1;

        if (!detailsStripVisible_ || innerRight <= innerLeft)
        {
            if (quickAccessScrollBar_)
            {
                ShowWindow(quickAccessScrollBar_, SW_HIDE);
            }
            HideQuickAccessShortcutEditControls();
            return;
        }

        std::vector<std::pair<std::wstring, bool>> destinations;
        destinations.reserve(quickSendModel_.FavoriteDestinations().size());
        for (const std::wstring& favoritePath : quickSendModel_.FavoriteDestinations())
        {
            destinations.emplace_back(favoritePath, true);
        }

        if (destinations.empty())
        {
            if (quickAccessScrollBar_)
            {
                ShowWindow(quickAccessScrollBar_, SW_HIDE);
            }
            HideQuickAccessShortcutEditControls();
            return;
        }

        const QuickAccessPanelMetrics metrics = BuildQuickAccessPanelMetrics(detailsPanelSummaryFont_, detailsPanelBodyFont_);
        const int panelBottom = detailsPanelContentRect_.bottom;
        const int viewportTop = top + metrics.headerHeight;
        if (panelBottom <= viewportTop)
        {
            if (quickAccessScrollBar_)
            {
                ShowWindow(quickAccessScrollBar_, SW_HIDE);
            }
            HideQuickAccessShortcutEditControls();
            return;
        }

        const int totalRowsHeight = static_cast<int>(destinations.size()) * metrics.rowHeight
            + static_cast<int>((destinations.size() - 1) * kQuickAccessPanelRowGap);
        const int viewportHeight = panelBottom - viewportTop;
        const int maximumScrollOffset = std::max(0, totalRowsHeight - viewportHeight);
        int contentRight = innerRight;
        if (maximumScrollOffset > 0 && quickAccessScrollBar_)
        {
            const int scrollBarWidth = std::max(1, GetSystemMetrics(SM_CXVSCROLL));
            const int scrollBarLeft = innerRight - scrollBarWidth;
            contentRight = scrollBarLeft - kQuickAccessPanelScrollBarGap;
            if (contentRight > innerLeft)
            {
                SCROLLINFO scrollInfo{};
                scrollInfo.cbSize = sizeof(scrollInfo);
                scrollInfo.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
                scrollInfo.nMin = 0;
                scrollInfo.nMax = std::max(0, totalRowsHeight - 1);
                scrollInfo.nPage = static_cast<UINT>(viewportHeight);
                scrollInfo.nPos = quickAccessScrollOffset_;
                SetScrollInfo(quickAccessScrollBar_, SB_CTL, &scrollInfo, TRUE);
                MoveWindow(quickAccessScrollBar_,
                           scrollBarLeft,
                           viewportTop,
                           scrollBarWidth,
                           viewportHeight,
                           TRUE);
                ShowWindow(quickAccessScrollBar_, SW_SHOW);
            }
            else
            {
                contentRight = innerRight;
            }
        }
        else if (quickAccessScrollBar_)
        {
            ShowWindow(quickAccessScrollBar_, SW_HIDE);
        }

        quickAccessDestinationPanelRect_ = RECT{innerLeft, top, innerRight, panelBottom};
        quickAccessDestinationViewportRect_ = RECT{innerLeft, viewportTop, contentRight, panelBottom};
        quickAccessScrollOffset_ = std::clamp(quickAccessScrollOffset_, 0, maximumScrollOffset);

        const HFONT headerFont = detailsPanelSummaryFont_
            ? detailsPanelSummaryFont_
            : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        const int sortButtonSize = std::min(kQuickAccessPanelSortButtonSize, metrics.headerHeight);
        const int sortButtonLeft = innerLeft
            + MeasureTextWidth(headerFont, L"Quick Send")
            + kQuickAccessPanelSortButtonGap;
        const int sortButtonTop = top + std::max(0, (metrics.headerHeight - sortButtonSize) / 2);
        if (sortButtonSize > 0 && sortButtonLeft + sortButtonSize <= innerRight)
        {
            quickAccessSortButtonRect_ = RECT{sortButtonLeft,
                                              sortButtonTop,
                                              sortButtonLeft + sortButtonSize,
                                              sortButtonTop + sortButtonSize};
        }
        UpdateQuickAccessSortTooltip();

        int rowTop = viewportTop - quickAccessScrollOffset_;
        for (const auto& [path, favorite] : destinations)
        {
            QuickAccessDestinationRow row;
            row.destinationPath = path;
            row.displayLabel = FormatFolderShortcutMenuLabel(path);
            row.metadataLabel = BuildQuickAccessDestinationMetadata(path, favorite, IsQuickAccessDestinationCurrentFolder(path));
            if (const std::optional<int> assignedShortcut = quickSendModel_.ShortcutForDestination(path))
            {
                row.assignedShortcut = *assignedShortcut;
            }
            row.favorite = favorite;
            row.rowRect = RECT{innerLeft, rowTop, contentRight, rowTop + metrics.rowHeight};
            const int buttonTop = rowTop + metrics.buttonTopInset;
            int buttonRight = contentRight - kQuickAccessPanelButtonRightInset;
            row.removeRect = RECT{buttonRight - kQuickAccessPanelRemoveButtonWidth,
                                  buttonTop,
                                  buttonRight,
                                  buttonTop + metrics.buttonHeight};
            buttonRight = row.removeRect.left - kQuickAccessPanelButtonGap;
            row.moveRect = RECT{buttonRight - kQuickAccessPanelButtonWidth,
                                buttonTop,
                                buttonRight,
                                buttonTop + metrics.buttonHeight};
            row.copyRect = RECT{row.moveRect.left - kQuickAccessPanelButtonGap - kQuickAccessPanelButtonWidth,
                                row.moveRect.top,
                                row.moveRect.left - kQuickAccessPanelButtonGap,
                                row.moveRect.bottom};
            const int shortcutRight = row.copyRect.left - kQuickAccessPanelShortcutGap;
            row.shortcutRect = RECT{shortcutRight - kQuickAccessPanelShortcutWidth,
                                    buttonTop,
                                    shortcutRight,
                                    buttonTop + metrics.buttonHeight};
            quickAccessDestinationRows_.push_back(std::move(row));
            rowTop += metrics.rowHeight + kQuickAccessPanelRowGap;
        }

        UpdateQuickAccessShortcutEditControls();
    }

    void MainWindow::UpdateQuickAccessSortTooltip()
    {
        if (!tooltipControl_ || !hwnd_)
        {
            return;
        }

        TTTOOLINFOW toolInfo{};
        toolInfo.cbSize = sizeof(toolInfo);
        toolInfo.uFlags = TTF_SUBCLASS;
        toolInfo.hwnd = hwnd_;
        toolInfo.uId = kQuickAccessSortTooltipId;
        toolInfo.rect = quickAccessSortButtonRect_;
        toolInfo.lpszText = LPSTR_TEXTCALLBACKW;
        if (!quickAccessSortTooltipAdded_)
        {
            quickAccessSortTooltipAdded_ = SendMessageW(tooltipControl_,
                                                        TTM_ADDTOOLW,
                                                        0,
                                                        reinterpret_cast<LPARAM>(&toolInfo)) != FALSE;
        }
        else
        {
            SendMessageW(tooltipControl_, TTM_NEWTOOLRECTW, 0, reinterpret_cast<LPARAM>(&toolInfo));
        }
    }

    void MainWindow::UpdateQuickAccessShortcutEditControls()
    {
        if (!hwnd_)
        {
            return;
        }

        const HFONT font = detailsPanelBodyFont_
            ? detailsPanelBodyFont_
            : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        while (quickAccessShortcutEdits_.size() < quickAccessDestinationRows_.size())
        {
            const UINT controlId = kQuickAccessShortcutEditBaseId
                + static_cast<UINT>(quickAccessShortcutEdits_.size());
            HWND edit = CreateWindowExW(
                WS_EX_CLIENTEDGE,
                L"EDIT",
                L"",
                WS_CHILD | WS_TABSTOP | ES_CENTER,
                0,
                0,
                kQuickAccessPanelShortcutWidth,
                kTextInputButtonHeight,
                hwnd_,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(controlId)),
                instance_,
                nullptr);
            if (!edit)
            {
                break;
            }

            SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            SendMessageW(edit, EM_LIMITTEXT, 1, 0);
            SetWindowSubclass(edit,
                              &MainWindow::QuickAccessShortcutEditSubclassProc,
                              1,
                              reinterpret_cast<DWORD_PTR>(this));
            quickAccessShortcutEdits_.push_back(edit);

            if (tooltipControl_)
            {
                TTTOOLINFOW toolInfo{};
                toolInfo.cbSize = sizeof(toolInfo);
                toolInfo.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
                toolInfo.hwnd = hwnd_;
                toolInfo.uId = reinterpret_cast<UINT_PTR>(edit);
                toolInfo.lpszText = const_cast<LPWSTR>(L"Quick Send hotkey: enter one digit or letter from 0 through 9 or A through Z.");
                SendMessageW(tooltipControl_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&toolInfo));
            }
        }

        updatingQuickAccessShortcutEdits_ = true;
        for (std::size_t index = 0; index < quickAccessShortcutEdits_.size(); ++index)
        {
            HWND edit = quickAccessShortcutEdits_[index];
            if (!edit)
            {
                continue;
            }

            if (index >= quickAccessDestinationRows_.size())
            {
                ShowWindow(edit, SW_HIDE);
                continue;
            }

            const QuickAccessDestinationRow& row = quickAccessDestinationRows_[index];
            RECT visibleIntersection{};
            const bool visible = !IsRectEmpty(&row.shortcutRect)
                && IntersectRect(&visibleIntersection,
                                 &row.shortcutRect,
                                 &quickAccessDestinationViewportRect_) != FALSE
                && EqualRect(&visibleIntersection, &row.shortcutRect) != FALSE;
            if (!visible)
            {
                ShowWindow(edit, SW_HIDE);
                continue;
            }

            MoveWindow(edit,
                       row.shortcutRect.left,
                       row.shortcutRect.top,
                       row.shortcutRect.right - row.shortcutRect.left,
                       row.shortcutRect.bottom - row.shortcutRect.top,
                       TRUE);
            std::wstring shortcutText;
            const wchar_t shortcutCharacter = QuickSendModel::ShortcutCharacter(row.assignedShortcut);
            if (shortcutCharacter != L'\0')
            {
                shortcutText.push_back(shortcutCharacter);
            }
            SetWindowTextW(edit, shortcutText.c_str());
            ShowWindow(edit, SW_SHOW);
        }
        updatingQuickAccessShortcutEdits_ = false;
    }

    void MainWindow::HideQuickAccessShortcutEditControls()
    {
        for (HWND edit : quickAccessShortcutEdits_)
        {
            if (edit)
            {
                ShowWindow(edit, SW_HIDE);
            }
        }
    }

    bool MainWindow::IsQuickAccessShortcutEdit(HWND control) const
    {
        return control
            && std::find(quickAccessShortcutEdits_.begin(), quickAccessShortcutEdits_.end(), control)
                != quickAccessShortcutEdits_.end();
    }

    bool MainWindow::IsQuickAccessDestinationCurrentFolder(std::wstring_view folderPath) const
    {
        return browserModel_
            && !browserModel_->FolderPath().empty()
            && !folderPath.empty()
            && FolderPathsEqual(browserModel_->FolderPath(), folderPath);
    }

    bool MainWindow::CanNavigateToQuickAccessDestination(std::wstring_view folderPath) const
    {
        return !folderPath.empty()
            && !IsQuickAccessDestinationCurrentFolder(folderPath)
            && IsExistingDirectory(folderPath);
    }

    int MainWindow::HitTestQuickAccessDestinationRow(int x, int y) const
    {
        const POINT point{x, y};
        if (IsRectEmpty(&quickAccessDestinationViewportRect_)
            || PtInRect(&quickAccessDestinationViewportRect_, point) == FALSE)
        {
            return -1;
        }

        for (std::size_t rowIndex = 0; rowIndex < quickAccessDestinationRows_.size(); ++rowIndex)
        {
            const QuickAccessDestinationRow& row = quickAccessDestinationRows_[rowIndex];
            if (CanNavigateToQuickAccessDestination(row.destinationPath)
                && PtInRect(&row.rowRect, point) != FALSE)
            {
                return static_cast<int>(rowIndex);
            }
        }

        return -1;
    }

    bool MainWindow::CanUseQuickAccessDestinationActions(std::wstring_view folderPath) const
    {
        return browserPaneController_
            && browserPaneController_->SelectedCount() > 0
            && !fileOperationActive_
            && CanNavigateToQuickAccessDestination(folderPath);
    }

    int MainWindow::HitTestQuickAccessDestinationButton(int x, int y, services::FileOperationType* type) const
    {
        const POINT point{x, y};
        if (IsRectEmpty(&quickAccessDestinationViewportRect_)
            || PtInRect(&quickAccessDestinationViewportRect_, point) == FALSE)
        {
            return -1;
        }

        for (std::size_t rowIndex = 0; rowIndex < quickAccessDestinationRows_.size(); ++rowIndex)
        {
            const QuickAccessDestinationRow& row = quickAccessDestinationRows_[rowIndex];
            const bool actionsEnabled = CanUseQuickAccessDestinationActions(row.destinationPath);
            if (actionsEnabled && PtInRect(&row.copyRect, point) != FALSE)
            {
                if (type)
                {
                    *type = services::FileOperationType::Copy;
                }
                return static_cast<int>(rowIndex * 3);
            }

            if (actionsEnabled && PtInRect(&row.moveRect, point) != FALSE)
            {
                if (type)
                {
                    *type = services::FileOperationType::Move;
                }
                return static_cast<int>(rowIndex * 3 + 1);
            }

            if (PtInRect(&row.removeRect, point) != FALSE)
            {
                return static_cast<int>(rowIndex * 3 + 2);
            }
        }

        return -1;
    }

    void MainWindow::SelectRightPaneTab(RightPaneTab tab)
    {
        if (activeRightPaneTab_ == tab)
        {
            return;
        }

        activeRightPaneTab_ = tab;
        LayoutChildren();
        if (!IsRectEmpty(&detailsPanelRect_))
        {
            InvalidateRect(hwnd_, &detailsPanelRect_, FALSE);
        }
    }

    void MainWindow::ToggleDetailsPanelVisibility()
    {
        detailsStripVisible_ = !detailsStripVisible_;
        if (detailsStripVisible_)
        {
            detailsPanelWidth_ = std::max(detailsPanelWidth_, kDetailsPanelMinWidth);
        }

        if (detailsPanelText_)
        {
            ShowWindow(detailsPanelText_, detailsStripVisible_ && activeRightPaneTab_ == RightPaneTab::FileDetails ? SW_SHOW : SW_HIDE);
        }

        detailsPanelCloseButtonHot_ = false;
        detailsPanelCloseButtonPressed_ = false;
        LayoutChildren();
        UpdateDetailsPanel();
        UpdateMenuState();
    }

    int MainWindow::HitTestDetailsPanelTab(int x, int y) const
    {
        if (!detailsStripVisible_ || IsRectEmpty(&detailsPanelTabStripRect_))
        {
            return -1;
        }

        const POINT point{x, y};
        for (std::size_t index = 0; index < detailsPanelTabRects_.size(); ++index)
        {
            if (!IsRectEmpty(&detailsPanelTabRects_[index]) && PtInRect(&detailsPanelTabRects_[index], point) != FALSE)
            {
                return static_cast<int>(index);
            }
        }

        return -1;
    }

    int MainWindow::HitTestDetailsPanelCloseButton(int x, int y) const
    {
        if (!detailsStripVisible_ || IsRectEmpty(&detailsPanelCloseButtonRect_))
        {
            return -1;
        }

        const POINT point{x, y};
        return PtInRect(&detailsPanelCloseButtonRect_, point) != FALSE ? 0 : -1;
    }

    int MainWindow::HitTestQuickAccessSortButton(int x, int y) const
    {
        if (IsRectEmpty(&quickAccessSortButtonRect_))
        {
            return -1;
        }

        const POINT point{x, y};
        return PtInRect(&quickAccessSortButtonRect_, point) != FALSE ? 0 : -1;
    }

    std::vector<browser::BrowserItem> MainWindow::CollectItemsForScope(bool selectionScope) const
    {
        std::vector<browser::BrowserItem> items;
        if (!browserModel_)
        {
            return items;
        }

        const auto& modelItems = browserModel_->Items();
        if (selectionScope && browserPaneController_)
        {
            for (const int modelIndex : browserPaneController_->OrderedSelectedModelIndicesSnapshot())
            {
                if (modelIndex >= 0 && modelIndex < static_cast<int>(modelItems.size()))
                {
                    items.push_back(modelItems[static_cast<std::size_t>(modelIndex)]);
                }
            }
            return items;
        }

        std::vector<int> orderedModelIndices = browserPaneController_
            ? browserPaneController_->OrderedModelIndicesSnapshot()
            : std::vector<int>{};
        if (orderedModelIndices.empty())
        {
            orderedModelIndices.reserve(modelItems.size());
            for (std::size_t index = 0; index < modelItems.size(); ++index)
            {
                orderedModelIndices.push_back(static_cast<int>(index));
            }
        }

        for (const int modelIndex : orderedModelIndices)
        {
            if (modelIndex >= 0 && modelIndex < static_cast<int>(modelItems.size()))
            {
                items.push_back(modelItems[static_cast<std::size_t>(modelIndex)]);
            }
        }

        return items;
    }

    std::vector<std::wstring> MainWindow::ExpandRawJpegPairedPaths(const std::vector<std::wstring>& paths,
                                                                   std::size_t* pairedCompanionCount) const
    {
        if (pairedCompanionCount)
        {
            *pairedCompanionCount = 0;
        }

        if (!rawJpegPairedOperationsEnabled_ || !browserModel_ || paths.empty())
        {
            return paths;
        }

        const auto& modelItems = browserModel_->Items();
        std::vector<std::wstring> expandedPaths = paths;
        for (const std::wstring& selectedPath : paths)
        {
            const int modelIndex = browserModel_->FindItemIndexByPath(selectedPath);
            if (modelIndex < 0 || modelIndex >= static_cast<int>(modelItems.size()))
            {
                continue;
            }

            const browser::BrowserItem& selectedItem = modelItems[static_cast<std::size_t>(modelIndex)];
            const bool selectedIsRaw = decode::IsRawFileType(selectedItem.fileType);
            const bool selectedIsJpeg = IsJpegFileType(selectedItem.fileType);
            if (!selectedIsRaw && !selectedIsJpeg)
            {
                continue;
            }

            const fs::path selectedFsPath(selectedItem.filePath);
            const std::wstring selectedParent = selectedFsPath.parent_path().wstring();
            const std::wstring selectedStem = selectedFsPath.stem().wstring();
            for (const browser::BrowserItem& candidate : modelItems)
            {
                if (browser::FilePathsEqual(candidate.filePath, selectedItem.filePath))
                {
                    continue;
                }

                if (!FolderPathsEqual(fs::path(candidate.filePath).parent_path().wstring(), selectedParent)
                    || !StringsEqualInsensitive(fs::path(candidate.filePath).stem().wstring(), selectedStem))
                {
                    continue;
                }

                const bool candidateIsRaw = decode::IsRawFileType(candidate.fileType);
                const bool candidateIsJpeg = IsJpegFileType(candidate.fileType);
                if (!((selectedIsRaw && candidateIsJpeg) || (selectedIsJpeg && candidateIsRaw)))
                {
                    continue;
                }

                const auto existing = std::find_if(expandedPaths.begin(), expandedPaths.end(), [&](const std::wstring& existingPath)
                {
                    return browser::FilePathsEqual(existingPath, candidate.filePath);
                });
                if (existing == expandedPaths.end())
                {
                    expandedPaths.push_back(candidate.filePath);
                }
            }
        }

        if (pairedCompanionCount && expandedPaths.size() > paths.size())
        {
            *pairedCompanionCount = expandedPaths.size() - paths.size();
        }

        return expandedPaths;
    }

    std::vector<std::wstring> MainWindow::SelectedFileOperationPathsSnapshot(std::size_t* pairedCompanionCount) const
    {
        if (!browserPaneController_)
        {
            return {};
        }

        return ExpandRawJpegPairedPaths(browserPaneController_->SelectedFilePathsSnapshot(), pairedCompanionCount);
    }

    void MainWindow::OpenFolder()
    {
        std::wstring folderPath;
        if (!ChooseFolder(&folderPath) || folderPath.empty())
        {
            return;
        }

        LoadFolderAsync(std::move(folderPath));
    }

    bool MainWindow::ChooseFolder(std::wstring* folderPath, HWND ownerWindow) const
    {
        if (!folderPath)
        {
            return false;
        }

        Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
        HRESULT result = CoCreateInstance(CLSID_FileOpenDialog,
                                          nullptr,
                                          CLSCTX_INPROC_SERVER,
                                          IID_PPV_ARGS(dialog.GetAddressOf()));
        if (FAILED(result) || !dialog)
        {
            return false;
        }

        DWORD options = 0;
        dialog->GetOptions(&options);
        dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
        result = dialog->Show(ownerWindow ? ownerWindow : hwnd_);
        if (FAILED(result))
        {
            return false;
        }

        Microsoft::WRL::ComPtr<IShellItem> shellItem;
        result = dialog->GetResult(&shellItem);
        if (FAILED(result) || !shellItem)
        {
            return false;
        }

        PWSTR rawFolderPath = nullptr;
        result = shellItem->GetDisplayName(SIGDN_FILESYSPATH, &rawFolderPath);
        if (FAILED(result) || !rawFolderPath)
        {
            return false;
        }

        *folderPath = rawFolderPath;
        CoTaskMemFree(rawFolderPath);
        return true;
    }

    bool MainWindow::HasSelectedJpegItems() const
    {
        for (const browser::BrowserItem& item : CollectItemsForScope(true))
        {
            if (IsJpegBrowserItem(item))
            {
                return true;
            }
        }

        return false;
    }

    void MainWindow::StartCompareSelected()
    {
        if (!browserModel_ || !browserPaneController_ || !viewerWindow_)
        {
            return;
        }

        if (browserPaneController_->SelectedCount() != 2)
        {
            MessageBoxW(hwnd_, L"Select exactly two images to compare.", L"Compare Selected", MB_OK | MB_ICONINFORMATION);
            return;
        }

        std::vector<browser::BrowserItem> items = CollectItemsForScope(true);
        if (items.size() != 2)
        {
            return;
        }

        int selectedIndex = 0;
        const int primaryModelIndex = browserPaneController_->PrimarySelectedModelIndex();
        const auto& modelItems = browserModel_->Items();
        if (primaryModelIndex >= 0 && primaryModelIndex < static_cast<int>(modelItems.size()))
        {
            const std::wstring& primaryPath = modelItems[static_cast<std::size_t>(primaryModelIndex)].filePath;
            if (browser::FilePathsEqual(items[1].filePath, primaryPath))
            {
                selectedIndex = 1;
            }
        }

        const viewer::CompareDirection compareDirection = selectedIndex == 0
            ? viewer::CompareDirection::Next
            : viewer::CompareDirection::Previous;
        if (OpenItemsInViewer(std::move(items), selectedIndex, false, false, false))
        {
            viewerWindow_->SetCompareMode(true, compareDirection);
        }
    }

    bool MainWindow::IsFavoriteDestination(std::wstring_view folderPath) const
    {
        return std::any_of(favoriteDestinationFolders_.begin(), favoriteDestinationFolders_.end(), [&](const std::wstring& candidate)
        {
            return FolderPathsEqual(candidate, folderPath);
        });
    }

    std::vector<std::wstring> MainWindow::RecentDestinationShortcutPaths() const
    {
        std::vector<std::wstring> paths;
        for (const std::wstring& recentPath : recentDestinationFolders_)
        {
            if (std::any_of(favoriteDestinationFolders_.begin(), favoriteDestinationFolders_.end(), [&](const std::wstring& favoritePath)
            {
                return FolderPathsEqual(favoritePath, recentPath);
            }))
            {
                continue;
            }

            InsertFolderPath(&paths, recentPath, kQuickAccessFolderLimit, false);
        }

        return paths;
    }

    void MainWindow::RemoveFavoriteDestination(std::wstring_view folderPath)
    {
        const auto newEnd = std::remove_if(favoriteDestinationFolders_.begin(), favoriteDestinationFolders_.end(),
                                           [&](const std::wstring& candidate)
                                           {
                                               return FolderPathsEqual(candidate, folderPath);
                                           });
        if (newEnd == favoriteDestinationFolders_.end())
        {
            return;
        }

        favoriteDestinationFolders_.erase(newEnd, favoriteDestinationFolders_.end());
        SyncQuickSendModel();
        if (treePane_)
        {
            InvalidateRect(treePane_, nullptr, FALSE);
        }
        UpdateMenuState();
        if (hwnd_ && detailsStripVisible_)
        {
            LayoutChildren();
        }
    }

    void MainWindow::RemoveRecentDestination(std::wstring_view folderPath)
    {
        const auto newEnd = std::remove_if(recentDestinationFolders_.begin(), recentDestinationFolders_.end(),
                                           [&](const std::wstring& candidate)
                                           {
                                               return FolderPathsEqual(candidate, folderPath);
                                           });
        if (newEnd == recentDestinationFolders_.end())
        {
            return;
        }

        recentDestinationFolders_.erase(newEnd, recentDestinationFolders_.end());
        UpdateMenuState();
        if (hwnd_ && detailsStripVisible_)
        {
            LayoutChildren();
        }
    }

    void MainWindow::ClearFavoriteDestinations()
    {
        if (favoriteDestinationFolders_.empty())
        {
            return;
        }

        favoriteDestinationFolders_.clear();
        SyncQuickSendModel();
        if (treePane_)
        {
            InvalidateRect(treePane_, nullptr, FALSE);
        }
        UpdateMenuState();
        if (hwnd_ && detailsStripVisible_)
        {
            LayoutChildren();
        }
    }

    void MainWindow::ClearRecentFolders()
    {
        if (recentFolders_.empty())
        {
            return;
        }

        recentFolders_.clear();
        UpdateMenuState();
    }

    void MainWindow::ClearRecentDestinations()
    {
        if (recentDestinationFolders_.empty())
        {
            return;
        }

        recentDestinationFolders_.clear();
        UpdateMenuState();
        if (hwnd_ && detailsStripVisible_)
        {
            LayoutChildren();
        }
    }

    void MainWindow::ToggleCurrentFolderFavoriteDestination()
    {
        if (!browserModel_ || browserModel_->FolderPath().empty())
        {
            return;
        }

        const std::wstring folderPath = NormalizeFolderPath(browserModel_->FolderPath());
        const auto existing = std::find_if(favoriteDestinationFolders_.begin(), favoriteDestinationFolders_.end(), [&](const std::wstring& candidate)
        {
            return FolderPathsEqual(candidate, folderPath);
        });
        bool addedFavorite = false;

        if (existing != favoriteDestinationFolders_.end())
        {
            favoriteDestinationFolders_.erase(existing);
        }
        else
        {
            addedFavorite = InsertFolderPath(&favoriteDestinationFolders_, folderPath, kFavoriteDestinationLimit, false);
        }
        SyncQuickSendModel();
        if (addedFavorite)
        {
            quickSendModel_.AssignNextAvailableShortcut(folderPath);
        }
        SortFavoriteDestinationsByShortcut();

        if (treePane_)
        {
            InvalidateRect(treePane_, nullptr, FALSE);
        }
        UpdateMenuState();
        if (hwnd_ && detailsStripVisible_)
        {
            LayoutChildren();
        }
    }

    void MainWindow::StartSelectionFileOperationToDestination(services::FileOperationType type, std::wstring destinationFolder)
    {
        if (!browserPaneController_ || fileOperationActive_)
        {
            return;
        }

        const std::vector<std::wstring> sourcePaths = SelectedFileOperationPathsSnapshot();
        if (sourcePaths.empty())
        {
            MessageBoxW(hwnd_,
                        L"Select one or more images first.",
                        type == services::FileOperationType::Move ? L"Move Selection" : L"Copy Selection",
                        MB_OK | MB_ICONINFORMATION);
            return;
        }

        destinationFolder = NormalizeFolderPath(std::move(destinationFolder));
        if (!IsExistingDirectory(destinationFolder))
        {
            MessageBoxW(hwnd_,
                        L"The selected destination folder is no longer available.",
                        type == services::FileOperationType::Move ? L"Move Selection" : L"Copy Selection",
                        MB_OK | MB_ICONINFORMATION);
            return;
        }

        if (IsQuickAccessDestinationCurrentFolder(destinationFolder))
        {
            MessageBoxW(hwnd_,
                        L"The selected destination is already the current folder.",
                        type == services::FileOperationType::Move ? L"Move Selection" : L"Copy Selection",
                        MB_OK | MB_ICONINFORMATION);
            return;
        }

        const services::FileConflictPlan conflictPlan = services::PlanDestinationConflicts(
            sourcePaths,
            destinationFolder,
            services::FileConflictPolicy::OverwriteExisting);

        services::FileConflictPolicy conflictPolicy = services::FileConflictPolicy::PromptShell;
        std::vector<std::wstring> targetLeafNames;
        if (!PromptForFileConflictPolicy(hwnd_, type, conflictPlan.conflictCount, &conflictPolicy))
        {
            return;
        }

        if (conflictPolicy == services::FileConflictPolicy::AutoRenameNumericSuffix)
        {
            targetLeafNames = services::PlanDestinationConflicts(
                sourcePaths,
                destinationFolder,
                conflictPolicy).targetLeafNames;
        }

        const std::wstring quickSendDestination = destinationFolder;
        if (StartFileOperation(type,
                               std::vector<std::wstring>(sourcePaths),
                               std::move(destinationFolder),
                               conflictPolicy,
                               std::move(targetLeafNames)))
        {
            lastQuickSendDestination_ = quickSendDestination;
        }
    }

    bool MainWindow::ShowShellContextMenuForSelection(POINT screenPoint)
    {
        if (!hwnd_ || !browserPaneController_)
        {
            return false;
        }

        const std::vector<std::wstring> selectedPaths = browserPaneController_->SelectedFilePathsSnapshot();
        if (selectedPaths.empty())
        {
            return false;
        }

        // All items must share a parent folder for a single shell context menu.
        const std::wstring parentPath = fs::path(selectedPaths.front()).parent_path().wstring();
        for (const std::wstring& path : selectedPaths)
        {
            if (!FolderPathsEqual(parentPath, fs::path(path).parent_path().wstring()))
            {
                return false;
            }
        }

        PIDLIST_ABSOLUTE folderPidl = ILCreateFromPathW(parentPath.c_str());
        if (!folderPidl)
        {
            return false;
        }

        Microsoft::WRL::ComPtr<IShellFolder> parentFolder;
        PCIDLIST_ABSOLUTE childIdl = nullptr;
        HRESULT result = SHBindToParent(folderPidl, IID_PPV_ARGS(parentFolder.GetAddressOf()), &childIdl);
        if (FAILED(result) || !parentFolder)
        {
            ILFree(folderPidl);
            return false;
        }

        // Resolve each item's child PIDL within the parent folder.
        std::vector<PIDLIST_ABSOLUTE> itemPidls;
        std::vector<PCUITEMID_CHILD> childPidls;
        for (const std::wstring& path : selectedPaths)
        {
            PIDLIST_ABSOLUTE itemPidl = ILCreateFromPathW(path.c_str());
            if (!itemPidl)
            {
                continue;
            }
            itemPidls.push_back(itemPidl);
            childPidls.push_back(ILFindLastID(itemPidl));
        }

        Microsoft::WRL::ComPtr<IContextMenu> contextMenu;
        result = parentFolder->GetUIObjectOf(hwnd_,
                                             static_cast<UINT>(childPidls.size()),
                                             childPidls.data(),
                                             IID_IContextMenu,
                                             nullptr,
                                             reinterpret_cast<void**>(contextMenu.GetAddressOf()));
        for (PIDLIST_ABSOLUTE itemPidl : itemPidls)
        {
            ILFree(itemPidl);
        }
        ILFree(folderPidl);

        if (FAILED(result) || !contextMenu)
        {
            return false;
        }

        HMENU menu = CreatePopupMenu();
        if (!menu)
        {
            return false;
        }

        // CMF_NORMAL lets the shell add its own verbs; CMF_EXTENDEDVERCS surfaces the
        // extended (shift) verbs since this path is triggered by Shift+right-click.
        result = contextMenu->QueryContextMenu(menu, 0, 1, 0x7FFF, CMF_NORMAL | CMF_EXTENDEDVERBS);
        if (FAILED(result))
        {
            DestroyMenu(menu);
            return false;
        }

        SetForegroundWindow(hwnd_);
        const UINT commandId = TrackPopupMenuEx(
            menu,
            TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD,
            screenPoint.x,
            screenPoint.y,
            hwnd_,
            nullptr);
        PostMessageW(hwnd_, WM_NULL, 0, 0);

        if (commandId > 0)
        {
            CMINVOKECOMMANDINFO invoke{};
            invoke.cbSize = sizeof(invoke);
            invoke.hwnd = hwnd_;
            invoke.lpVerb = MAKEINTRESOURCEA(commandId - 1);
            invoke.nShow = SW_SHOWNORMAL;
            contextMenu->InvokeCommand(&invoke);
        }

        DestroyMenu(menu);
        return true;
    }

    void MainWindow::ShowBrowserContextMenu(POINT screenPoint)
    {
        if (!hwnd_)
        {
            return;
        }

        const bool hasFolder = browserModel_ && !browserModel_->FolderPath().empty();
        const bool hasSelection = browserPaneController_ && browserPaneController_->SelectedCount() > 0;
        const bool hasSingleSelection = browserPaneController_ && browserPaneController_->SelectedCount() == 1;
        const bool hasBatchRenameSelection = browserPaneController_ && browserPaneController_->SelectedCount() > 1;
        const bool hasSelectedJpeg = HasSelectedJpegItems();
        const bool allowMutatingFileCommands = hasSelection && !fileOperationActive_;
        const int commonSelectionRating = hasSelection ? CommonSelectionRating() : -1;
        const bool allowRenameSelected = hasSingleSelection && !fileOperationActive_;
        const bool allowBatchRenameSelected = hasBatchRenameSelection && !fileOperationActive_;
        const bool hasSecondaryMonitor = FindAlternateMonitorForWindow(hwnd_) != nullptr;
        const bool sizeCommandsEnabled = hasFolder && browserMode_ == BrowserMode::Thumbnails;

        HMENU menu = CreatePopupMenu();
        HMENU metadataMenu = CreatePopupMenu();
        HMENU batchConvertSelectionMenu = CreatePopupMenu();
        HMENU ratingMenu = CreatePopupMenu();
        HMENU sortMenu = CreatePopupMenu();
        HMENU thumbnailSizeMenu = CreatePopupMenu();
        if (!menu || !metadataMenu || !batchConvertSelectionMenu || !ratingMenu || !sortMenu || !thumbnailSizeMenu)
        {
            if (thumbnailSizeMenu)
            {
                DestroyMenu(thumbnailSizeMenu);
            }
            if (sortMenu)
            {
                DestroyMenu(sortMenu);
            }
            if (ratingMenu)
            {
                DestroyMenu(ratingMenu);
            }
            if (batchConvertSelectionMenu)
            {
                DestroyMenu(batchConvertSelectionMenu);
            }
            if (metadataMenu)
            {
                DestroyMenu(metadataMenu);
            }
            if (menu)
            {
                DestroyMenu(menu);
            }
            return;
        }

        AppendMenuW(ratingMenu, MF_STRING, ID_FILE_SET_RATING_0, L"&Clear Rating");
        AppendMenuW(ratingMenu, MF_STRING, ID_FILE_SET_RATING_1, L"&1 Star");
        AppendMenuW(ratingMenu, MF_STRING, ID_FILE_SET_RATING_2, L"&2 Stars");
        AppendMenuW(ratingMenu, MF_STRING, ID_FILE_SET_RATING_3, L"&3 Stars");
        AppendMenuW(ratingMenu, MF_STRING, ID_FILE_SET_RATING_4, L"&4 Stars");
        AppendMenuW(ratingMenu, MF_STRING, ID_FILE_SET_RATING_5, L"&5 Stars");
        AppendMenuW(batchConvertSelectionMenu, MF_STRING, ID_FILE_BATCH_CONVERT_SELECTION_JPEG, L"Selection to &JPEG");
        AppendMenuW(batchConvertSelectionMenu, MF_STRING, ID_FILE_BATCH_CONVERT_SELECTION_PNG, L"Selection to &PNG");
        AppendMenuW(batchConvertSelectionMenu, MF_STRING, ID_FILE_BATCH_CONVERT_SELECTION_TIFF, L"Selection to &TIFF");
        AppendMenuW(sortMenu, MF_STRING, ID_VIEW_SORT_FILENAME, L"By &Filename");
        AppendMenuW(sortMenu, MF_STRING, ID_VIEW_SORT_MODIFIED, L"By &Modified Date");
        AppendMenuW(sortMenu, MF_STRING, ID_VIEW_SORT_SIZE, L"By File &Size");
        AppendMenuW(sortMenu, MF_STRING, ID_VIEW_SORT_DIMENSIONS, L"By &Dimensions");
        AppendMenuW(sortMenu, MF_STRING, ID_VIEW_SORT_TYPE, L"By &Type");
        AppendMenuW(sortMenu, MF_STRING, ID_VIEW_SORT_DATETAKEN, L"By Date &Taken");
        AppendMenuW(sortMenu, MF_STRING, ID_VIEW_SORT_RATING, L"By &Rating");
        AppendMenuW(sortMenu, MF_STRING, ID_VIEW_SORT_TAGS, L"By Ta&gs");
        AppendMenuW(sortMenu, MF_STRING, ID_VIEW_SORT_RANDOM, L"By &Random");
        AppendMenuW(sortMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(sortMenu, MF_STRING, ID_VIEW_SORT_DIRECTION, L"&Descending");
        AppendMenuW(thumbnailSizeMenu, MF_STRING, ID_VIEW_THUMBNAIL_SIZE_96, L"&96 px");
        AppendMenuW(thumbnailSizeMenu, MF_STRING, ID_VIEW_THUMBNAIL_SIZE_128, L"1&28 px");
        AppendMenuW(thumbnailSizeMenu, MF_STRING, ID_VIEW_THUMBNAIL_SIZE_160, L"1&60 px");
        AppendMenuW(thumbnailSizeMenu, MF_STRING, ID_VIEW_THUMBNAIL_SIZE_192, L"1&92 px");
        AppendMenuW(thumbnailSizeMenu, MF_STRING, ID_VIEW_THUMBNAIL_SIZE_256, L"2&56 px");
        AppendMenuW(thumbnailSizeMenu, MF_STRING, ID_VIEW_THUMBNAIL_SIZE_320, L"3&20 px");
        AppendMenuW(thumbnailSizeMenu, MF_STRING, ID_VIEW_THUMBNAIL_SIZE_360, L"3&60 px");
        AppendMenuW(thumbnailSizeMenu, MF_STRING, ID_VIEW_THUMBNAIL_SIZE_420, L"4&20 px");
        AppendMenuW(thumbnailSizeMenu, MF_STRING, ID_VIEW_THUMBNAIL_SIZE_480, L"4&80 px");
        AppendMenuW(thumbnailSizeMenu, MF_STRING, ID_VIEW_THUMBNAIL_SIZE_560, L"5&60 px");
        AppendMenuW(thumbnailSizeMenu, MF_STRING, ID_VIEW_THUMBNAIL_SIZE_640, L"6&40 px");

        if (hasSelection)
        {
            AppendMenuW(menu, MF_STRING, ID_FILE_OPEN_SELECTED, L"&Open");
            AppendMenuW(menu, MF_STRING, ID_FILE_COMPARE_SELECTED, L"&Compare Selected");
            AppendMenuW(menu, MF_STRING, ID_FILE_VIEW_ON_SECONDARY_MONITOR, L"View on Secondary &Monitor");
            AppendMenuW(menu, MF_STRING, ID_VIEW_SLIDESHOW_SELECTION, L"Slideshow from &Selection");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, ID_FILE_REVEAL_IN_EXPLORER, L"Reveal in &Explorer");
            AppendMenuW(menu, MF_STRING, ID_FILE_OPEN_CONTAINING_FOLDER, L"Open Containing &Folder");
            AppendMenuW(menu, MF_STRING, ID_FILE_COPY_FILES_TO_CLIPBOARD, L"&Copy\tCtrl+C");
            AppendMenuW(menu, MF_STRING, ID_FILE_COPY_IMAGE_PIXELS, L"Copy &Image\tCtrl+Shift+I");
            AppendMenuW(menu, MF_STRING, ID_FILE_COPY_PATH, L"Copy Pat&h\tCtrl+Shift+C");
            AppendMenuW(menu, MF_STRING, ID_FILE_IMAGE_INFORMATION, L"Image &Information");
            AppendMenuW(menu, MF_STRING, ID_FILE_PROPERTIES, L"P&roperties");
            AppendMenuW(metadataMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(ratingMenu), L"Set &Rating");
            AppendMenuW(metadataMenu, MF_STRING, ID_FILE_EDIT_TAGS, L"Edit &Tags...");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(metadataMenu), L"&Metadata");
            AppendMenuW(menu, MF_STRING, ID_FILE_RENAME_SELECTED, L"Re&name...");
            AppendMenuW(menu, MF_STRING, ID_FILE_BATCH_RENAME_SELECTION, L"Batch R&ename...");
            AppendMenuW(menu, MF_STRING, ID_FILE_DUPLICATE_SELECTION, L"Dup&licate\tCtrl+D");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, ID_FILE_COPY_SELECTION, L"Cop&y Selection...");
            AppendMenuW(menu, MF_STRING, ID_FILE_MOVE_SELECTION, L"Mo&ve Selection...");
            if (hasBatchRenameSelection)
            {
                AppendMenuW(menu, MF_STRING, ID_FILE_MOVE_SELECTION_TO_NEW_CHILD_FOLDER, L"Move to New Child &Folder...");
            }
            AppendMenuW(menu, MF_STRING, ID_FILE_DELETE_SELECTION, L"&Delete");
            AppendMenuW(menu, MF_STRING, ID_FILE_DELETE_SELECTION_PERMANENT, L"Delete &Permanently");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(batchConvertSelectionMenu), L"Batch Convert &Selection");
            AppendMenuW(menu, MF_STRING, ID_FILE_ROTATE_JPEG_LEFT, L"Adjust JPEG Orientation &Left");
            AppendMenuW(menu, MF_STRING, ID_FILE_ROTATE_JPEG_RIGHT, L"Adjust JPEG Orientation &Right");

            EnableMenuItem(menu, ID_FILE_OPEN_SELECTED, MF_BYCOMMAND | MF_ENABLED);
            EnableMenuItem(menu, ID_FILE_COMPARE_SELECTED,
                           MF_BYCOMMAND | ((browserPaneController_ && browserPaneController_->SelectedCount() == 2) ? MF_ENABLED : MF_GRAYED));
            EnableMenuItem(menu, ID_FILE_VIEW_ON_SECONDARY_MONITOR,
                           MF_BYCOMMAND | ((hasSelection && hasSecondaryMonitor) ? MF_ENABLED : MF_GRAYED));
            EnableMenuItem(menu, ID_VIEW_SLIDESHOW_SELECTION, MF_BYCOMMAND | MF_ENABLED);
            EnableMenuItem(menu, ID_FILE_REVEAL_IN_EXPLORER, MF_BYCOMMAND | MF_ENABLED);
            EnableMenuItem(menu, ID_FILE_OPEN_CONTAINING_FOLDER, MF_BYCOMMAND | MF_ENABLED);
            EnableMenuItem(menu, ID_FILE_COPY_FILES_TO_CLIPBOARD, MF_BYCOMMAND | MF_ENABLED);
            EnableMenuItem(menu, ID_FILE_COPY_IMAGE_PIXELS, MF_BYCOMMAND | (hasSingleSelection ? MF_ENABLED : MF_GRAYED));
            EnableMenuItem(menu, ID_FILE_COPY_PATH, MF_BYCOMMAND | MF_ENABLED);
            EnableMenuItem(menu, ID_FILE_IMAGE_INFORMATION, MF_BYCOMMAND | MF_ENABLED);
            EnableMenuItem(menu, ID_FILE_PROPERTIES, MF_BYCOMMAND | MF_ENABLED);
            EnableMenuItem(menu, ID_FILE_EDIT_TAGS, MF_BYCOMMAND | (allowMutatingFileCommands ? MF_ENABLED : MF_GRAYED));
            EnableMenuItem(menu, ID_FILE_RENAME_SELECTED, MF_BYCOMMAND | (allowRenameSelected ? MF_ENABLED : MF_GRAYED));
            EnableMenuItem(menu, ID_FILE_BATCH_RENAME_SELECTION, MF_BYCOMMAND | (allowBatchRenameSelected ? MF_ENABLED : MF_GRAYED));
            EnableMenuItem(menu, ID_FILE_DUPLICATE_SELECTION, MF_BYCOMMAND | (allowMutatingFileCommands ? MF_ENABLED : MF_GRAYED));
            EnableMenuItem(menu, ID_FILE_COPY_SELECTION, MF_BYCOMMAND | (allowMutatingFileCommands ? MF_ENABLED : MF_GRAYED));
            EnableMenuItem(menu, ID_FILE_MOVE_SELECTION, MF_BYCOMMAND | (allowMutatingFileCommands ? MF_ENABLED : MF_GRAYED));
            if (hasBatchRenameSelection)
            {
                EnableMenuItem(menu,
                               ID_FILE_MOVE_SELECTION_TO_NEW_CHILD_FOLDER,
                               MF_BYCOMMAND | (allowBatchRenameSelected ? MF_ENABLED : MF_GRAYED));
            }
            EnableMenuItem(menu, ID_FILE_DELETE_SELECTION, MF_BYCOMMAND | (allowMutatingFileCommands ? MF_ENABLED : MF_GRAYED));
            EnableMenuItem(menu, ID_FILE_DELETE_SELECTION_PERMANENT, MF_BYCOMMAND | (allowMutatingFileCommands ? MF_ENABLED : MF_GRAYED));
            EnableMenuItem(menu, ID_FILE_ROTATE_JPEG_LEFT, MF_BYCOMMAND | (hasSelectedJpeg ? MF_ENABLED : MF_GRAYED));
            EnableMenuItem(menu, ID_FILE_ROTATE_JPEG_RIGHT, MF_BYCOMMAND | (hasSelectedJpeg ? MF_ENABLED : MF_GRAYED));
        }
        else
        {
            AppendMenuW(menu, MF_STRING, ID_FILE_OPEN_FOLDER, L"Open &Folder...");
            AppendMenuW(menu, MF_STRING, ID_FILE_REFRESH_TREE, L"Refresh Folder &Tree");
            AppendMenuW(menu, MF_STRING, ID_FILE_PASTE_FILES, L"&Paste\tCtrl+V");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, ID_VIEW_THUMBNAILS, L"&Thumbnail Mode");
            AppendMenuW(menu, MF_STRING, ID_VIEW_DETAILS, L"&Details Mode");
            AppendMenuW(menu, MF_STRING, ID_VIEW_RECURSIVE, L"&Recursive Browsing");
            AppendMenuW(menu, MF_STRING, ID_VIEW_THUMBNAIL_DETAILS, L"Show Thumbnail &Details");
            AppendMenuW(menu, MF_STRING, ID_VIEW_DETAILS_STRIP, L"Show &Details Panel");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(sortMenu), L"&Sort By");
            AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(thumbnailSizeMenu), L"Thumbnail Si&ze");
            AppendMenuW(menu, MF_STRING, ID_VIEW_SLIDESHOW_FOLDER, L"Slideshow from &Folder");

            EnableMenuItem(menu, ID_FILE_REFRESH_TREE, MF_BYCOMMAND | (hasFolder ? MF_ENABLED : MF_GRAYED));
            EnableMenuItem(menu, ID_FILE_PASTE_FILES, MF_BYCOMMAND | (hasFolder && !fileOperationActive_ ? MF_ENABLED : MF_GRAYED));
            EnableMenuItem(menu, ID_VIEW_THUMBNAILS, MF_BYCOMMAND | (hasFolder ? MF_ENABLED : MF_GRAYED));
            EnableMenuItem(menu, ID_VIEW_DETAILS, MF_BYCOMMAND | (hasFolder ? MF_ENABLED : MF_GRAYED));
            EnableMenuItem(menu, ID_VIEW_RECURSIVE, MF_BYCOMMAND | (hasFolder ? MF_ENABLED : MF_GRAYED));
            EnableMenuItem(menu, ID_VIEW_THUMBNAIL_DETAILS, MF_BYCOMMAND | (hasFolder ? MF_ENABLED : MF_GRAYED));
            EnableMenuItem(menu, ID_VIEW_DETAILS_STRIP, MF_BYCOMMAND | (hasFolder ? MF_ENABLED : MF_GRAYED));
            EnableMenuItem(menu, ID_VIEW_SLIDESHOW_FOLDER, MF_BYCOMMAND | (hasFolder ? MF_ENABLED : MF_GRAYED));

            CheckMenuRadioItem(
                menu,
                ID_VIEW_THUMBNAILS,
                ID_VIEW_DETAILS,
                browserMode_ == BrowserMode::Thumbnails ? ID_VIEW_THUMBNAILS : ID_VIEW_DETAILS,
                MF_BYCOMMAND);
            CheckMenuItem(
                menu,
                ID_VIEW_RECURSIVE,
                MF_BYCOMMAND | (recursiveBrowsingEnabled_ ? MF_CHECKED : MF_UNCHECKED));
            CheckMenuItem(
                menu,
                ID_VIEW_THUMBNAIL_DETAILS,
                MF_BYCOMMAND | (thumbnailDetailsVisible_ ? MF_CHECKED : MF_UNCHECKED));
            CheckMenuItem(
                menu,
                ID_VIEW_DETAILS_STRIP,
                MF_BYCOMMAND | (detailsStripVisible_ ? MF_CHECKED : MF_UNCHECKED));

            EnableMenuItem(sortMenu, ID_VIEW_SORT_FILENAME, MF_BYCOMMAND | (hasFolder ? MF_ENABLED : MF_GRAYED));
            EnableMenuItem(sortMenu, ID_VIEW_SORT_MODIFIED, MF_BYCOMMAND | (hasFolder ? MF_ENABLED : MF_GRAYED));
            EnableMenuItem(sortMenu, ID_VIEW_SORT_SIZE, MF_BYCOMMAND | (hasFolder ? MF_ENABLED : MF_GRAYED));
            EnableMenuItem(sortMenu, ID_VIEW_SORT_DIMENSIONS, MF_BYCOMMAND | (hasFolder ? MF_ENABLED : MF_GRAYED));
            EnableMenuItem(sortMenu, ID_VIEW_SORT_TYPE, MF_BYCOMMAND | (hasFolder ? MF_ENABLED : MF_GRAYED));
            EnableMenuItem(sortMenu, ID_VIEW_SORT_RANDOM, MF_BYCOMMAND | (hasFolder ? MF_ENABLED : MF_GRAYED));
            EnableMenuItem(sortMenu, ID_VIEW_SORT_DATETAKEN, MF_BYCOMMAND | (hasFolder ? MF_ENABLED : MF_GRAYED));
            EnableMenuItem(sortMenu, ID_VIEW_SORT_RATING, MF_BYCOMMAND | (hasFolder ? MF_ENABLED : MF_GRAYED));
            EnableMenuItem(sortMenu, ID_VIEW_SORT_TAGS, MF_BYCOMMAND | (hasFolder ? MF_ENABLED : MF_GRAYED));
            for (UINT thumbnailSizeCommandId = ID_VIEW_THUMBNAIL_SIZE_96; thumbnailSizeCommandId <= ID_VIEW_THUMBNAIL_SIZE_640; ++thumbnailSizeCommandId)
            {
                EnableMenuItem(thumbnailSizeMenu, thumbnailSizeCommandId, MF_BYCOMMAND | (sizeCommandsEnabled ? MF_ENABLED : MF_GRAYED));
            }
        }

        for (UINT ratingCommandId = ID_FILE_SET_RATING_0; ratingCommandId <= ID_FILE_SET_RATING_5; ++ratingCommandId)
        {
            EnableMenuItem(ratingMenu, ratingCommandId, MF_BYCOMMAND | (allowMutatingFileCommands ? MF_ENABLED : MF_GRAYED));
            CheckMenuItem(ratingMenu,
                          ratingCommandId,
                          MF_BYCOMMAND | (commonSelectionRating >= 0 && ratingCommandId == CommandIdFromRating(commonSelectionRating)
                              ? MF_CHECKED
                              : MF_UNCHECKED));
        }
        EnableMenuItem(batchConvertSelectionMenu, ID_FILE_BATCH_CONVERT_SELECTION_JPEG,
                       MF_BYCOMMAND | (hasSelection && !batchConvertActive_ ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(batchConvertSelectionMenu, ID_FILE_BATCH_CONVERT_SELECTION_PNG,
                       MF_BYCOMMAND | (hasSelection && !batchConvertActive_ ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(batchConvertSelectionMenu, ID_FILE_BATCH_CONVERT_SELECTION_TIFF,
                       MF_BYCOMMAND | (hasSelection && !batchConvertActive_ ? MF_ENABLED : MF_GRAYED));

        CheckMenuRadioItem(
            thumbnailSizeMenu,
            ID_VIEW_THUMBNAIL_SIZE_96,
            ID_VIEW_THUMBNAIL_SIZE_640,
            CommandIdFromThumbnailSizePreset(thumbnailSizePreset_),
            MF_BYCOMMAND);

        const browser::BrowserSortMode sortMode = browserPaneController_
            ? browserPaneController_->GetSortMode()
            : browser::BrowserSortMode::FileName;
        const bool sortAscending = browserPaneController_
            ? browserPaneController_->IsSortAscending()
            : true;
        CheckMenuRadioItem(
            sortMenu,
            ID_VIEW_SORT_FILENAME,
            ID_VIEW_SORT_TAGS,
            CommandIdFromSortMode(sortMode),
            MF_BYCOMMAND);
        CheckMenuItem(
            sortMenu,
            ID_VIEW_SORT_DIRECTION,
            MF_BYCOMMAND | (sortAscending ? MF_UNCHECKED : MF_CHECKED));

        std::vector<std::unique_ptr<MenuDrawItemData>> menuDrawItems;
        PrepareMenuForOwnerDraw(menu, menuDrawItems, true);

        SetForegroundWindow(hwnd_);
        TrackPopupMenu(
            menu,
            TPM_LEFTALIGN | TPM_RIGHTBUTTON,
            screenPoint.x,
            screenPoint.y,
            0,
            hwnd_,
            nullptr);
        PostMessageW(hwnd_, WM_NULL, 0, 0);
        DestroyMenu(menu);
    }

    void MainWindow::ShowFolderTreeContextMenu(POINT screenPoint, HTREEITEM item)
    {
        if (!hwnd_ || !treePane_ || !item)
        {
            return;
        }

        const FolderTreeNodeData* nodeData = GetFolderTreeNodeData(item);
        if (!nodeData || nodeData->path.empty() || !TreeView_GetParent(treePane_, item))
        {
            return;
        }

        const std::wstring folderPath = nodeData->path;
        HMENU menu = CreatePopupMenu();
        if (!menu)
        {
            return;
        }

        constexpr UINT kRenameFolderCommandId = 1;
        constexpr UINT kDeleteFolderCommandId = 2;
        constexpr UINT kDeleteFolderPermanentCommandId = 3;
        constexpr UINT kOpenInExplorerCommandId = 4;
        constexpr UINT kCopyPathCommandId = 5;
        constexpr UINT kMoveFolderBrowseCommandId = 6;
        constexpr UINT kMoveFolderFavoriteBaseCommandId = 40;
        constexpr UINT kMoveFolderFavoriteLastCommandId = kMoveFolderFavoriteBaseCommandId
            + static_cast<UINT>(kFavoriteDestinationLimit) - 1;
        constexpr UINT kMoveFolderRecentBaseCommandId = 80;
        constexpr UINT kMoveFolderRecentLastCommandId = 87;
        constexpr UINT kNewFolderCommandId = 30;
        constexpr UINT kToggleFavoriteCommandId = 31;
        constexpr UINT kFolderPropertiesCommandId = 32;

        std::vector<std::wstring> favoriteMoveDestinations;
        std::vector<std::wstring> recentMoveDestinations;
        const std::wstring sourceParentPath = NormalizeFolderPath(fs::path(folderPath).parent_path().wstring());
        const auto canOfferMoveDestination = [&](const std::wstring& destinationPath)
        {
            const std::wstring normalizedDestination = NormalizeFolderPath(destinationPath);
            if (!IsExistingDirectory(normalizedDestination))
            {
                return false;
            }

            if (!AreFoldersOnSameDrive(folderPath, normalizedDestination))
            {
                return false;
            }

            if (FolderPathsEqual(normalizedDestination, folderPath)
                || hyperbrowse::browser::PathHasPrefix(normalizedDestination, folderPath)
                || (!sourceParentPath.empty() && FolderPathsEqual(normalizedDestination, sourceParentPath)))
            {
                return false;
            }

            return true;
        };

        for (const std::wstring& favoritePath : favoriteDestinationFolders_)
        {
            if (canOfferMoveDestination(favoritePath))
            {
                favoriteMoveDestinations.push_back(NormalizeFolderPath(favoritePath));
            }
        }

        for (const std::wstring& recentPath : RecentDestinationShortcutPaths())
        {
            if (!canOfferMoveDestination(recentPath))
            {
                continue;
            }

            const std::wstring normalizedRecentPath = NormalizeFolderPath(recentPath);
            const bool duplicate = std::any_of(favoriteMoveDestinations.begin(), favoriteMoveDestinations.end(), [&](const std::wstring& favoritePath)
            {
                return FolderPathsEqual(favoritePath, normalizedRecentPath);
            });
            if (!duplicate)
            {
                recentMoveDestinations.push_back(normalizedRecentPath);
            }
        }

        HMENU moveFolderMenu = CreatePopupMenu();
        if (!moveFolderMenu)
        {
            DestroyMenu(menu);
            return;
        }

        AppendMenuW(moveFolderMenu, MF_STRING, kMoveFolderBrowseCommandId, L"Choose &Folder...");
        if (!favoriteMoveDestinations.empty() || !recentMoveDestinations.empty())
        {
            AppendMenuW(moveFolderMenu, MF_SEPARATOR, 0, nullptr);
            const std::size_t favoriteCapacity = kMoveFolderFavoriteLastCommandId - kMoveFolderFavoriteBaseCommandId + 1;
            const std::size_t favoriteCount = std::min<std::size_t>(favoriteMoveDestinations.size(), favoriteCapacity);
            for (std::size_t index = 0; index < favoriteCount; ++index)
            {
                AppendMenuW(moveFolderMenu,
                            MF_STRING,
                            kMoveFolderFavoriteBaseCommandId + static_cast<UINT>(index),
                            FormatFolderShortcutMenuLabel(favoriteMoveDestinations[index]).c_str());
            }

            const std::size_t recentCapacity = kMoveFolderRecentLastCommandId - kMoveFolderRecentBaseCommandId + 1;
            const std::size_t recentCount = std::min<std::size_t>(recentMoveDestinations.size(), recentCapacity);
            for (std::size_t index = 0; index < recentCount; ++index)
            {
                AppendMenuW(moveFolderMenu,
                            MF_STRING,
                            kMoveFolderRecentBaseCommandId + static_cast<UINT>(index),
                            FormatFolderShortcutMenuLabel(recentMoveDestinations[index]).c_str());
            }
        }
        else
        {
            AppendMenuW(moveFolderMenu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(moveFolderMenu, MF_STRING | MF_GRAYED, 0, L"(No compatible quick destinations)");
        }

        const bool isFavoriteDestination = IsFavoriteDestination(folderPath);
        const wchar_t* toggleFavoriteLabel = isFavoriteDestination ? L"Remove from &Favorites" : L"&Add to Favorites";

        AppendMenuW(menu, MF_STRING, kNewFolderCommandId, L"&New Folder...");
        AppendMenuW(menu, MF_STRING, kRenameFolderCommandId, L"Re&name Folder...");
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(moveFolderMenu), L"Mo&ve Folder To");
        AppendMenuW(menu, MF_STRING, kOpenInExplorerCommandId, L"Open in &Explorer");
        AppendMenuW(menu, MF_STRING, kCopyPathCommandId, L"Copy Pat&h");
        AppendMenuW(menu, MF_STRING, kToggleFavoriteCommandId, toggleFavoriteLabel);
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kDeleteFolderCommandId, L"&Delete Folder");
        AppendMenuW(menu, MF_STRING, kDeleteFolderPermanentCommandId, L"Delete Folder &Permanently");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kFolderPropertiesCommandId, L"P&roperties");

        const UINT enableState = fileOperationActive_ ? MF_GRAYED : MF_ENABLED;
        EnableMenuItem(menu, kNewFolderCommandId, MF_BYCOMMAND | enableState);
        EnableMenuItem(menu, kRenameFolderCommandId, MF_BYCOMMAND | enableState);
        EnableMenuItem(menu, kOpenInExplorerCommandId, MF_BYCOMMAND | MF_ENABLED);
        EnableMenuItem(menu, kCopyPathCommandId, MF_BYCOMMAND | MF_ENABLED);
        EnableMenuItem(menu, kToggleFavoriteCommandId, MF_BYCOMMAND | MF_ENABLED);
        EnableMenuItem(menu, kDeleteFolderCommandId, MF_BYCOMMAND | enableState);
        EnableMenuItem(menu, kDeleteFolderPermanentCommandId, MF_BYCOMMAND | enableState);
        EnableMenuItem(moveFolderMenu, kMoveFolderBrowseCommandId, MF_BYCOMMAND | enableState);

        std::vector<std::unique_ptr<MenuDrawItemData>> menuDrawItems;
        PrepareMenuForOwnerDraw(menu, menuDrawItems, true);

        SetForegroundWindow(hwnd_);
        const UINT commandId = TrackPopupMenuEx(
            menu,
            TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD,
            screenPoint.x,
            screenPoint.y,
            hwnd_,
            nullptr);
        PostMessageW(hwnd_, WM_NULL, 0, 0);
        DestroyMenu(menu);

        switch (commandId)
        {
        case kNewFolderCommandId:
            StartFolderTreeCreateNewFolder(folderPath);
            break;
        case kRenameFolderCommandId:
            if (!BeginFolderTreeInlineRename(folderPath))
            {
                StartFolderTreeRename(folderPath);
            }
            break;
        case kMoveFolderBrowseCommandId:
        {
            std::wstring destinationFolder;
            if (ChooseFolder(&destinationFolder) && !destinationFolder.empty())
            {
                StartFolderTreeMoveToDestination(folderPath, std::move(destinationFolder));
            }
            break;
        }
        case kOpenInExplorerCommandId:
            if (!LaunchShellTarget(hwnd_, L"open", folderPath))
            {
                MessageBoxW(hwnd_, L"Failed to open the folder in Explorer.", L"Open in Explorer", MB_OK | MB_ICONERROR);
            }
            break;
        case kCopyPathCommandId:
            if (!CopyTextToClipboard(hwnd_, folderPath))
            {
                MessageBoxW(hwnd_, L"Failed to copy the folder path to the clipboard.", L"Copy Path", MB_OK | MB_ICONERROR);
            }
            break;
        case kToggleFavoriteCommandId:
        {
            const auto existing = std::find_if(favoriteDestinationFolders_.begin(), favoriteDestinationFolders_.end(), [&](const std::wstring& candidate)
            {
                return FolderPathsEqual(candidate, folderPath);
            });
            bool addedFavorite = false;
            if (existing != favoriteDestinationFolders_.end())
            {
                favoriteDestinationFolders_.erase(existing);
            }
            else
            {
                addedFavorite = InsertFolderPath(&favoriteDestinationFolders_, folderPath, kFavoriteDestinationLimit, false);
            }
            SyncQuickSendModel();
            if (addedFavorite)
            {
                quickSendModel_.AssignNextAvailableShortcut(folderPath);
            }
            SortFavoriteDestinationsByShortcut();
            if (treePane_)
            {
                InvalidateRect(treePane_, nullptr, FALSE);
            }
            UpdateMenuState();
            if (hwnd_ && detailsStripVisible_)
            {
                LayoutChildren();
            }
            break;
        }
        case kDeleteFolderCommandId:
            StartFolderTreeDelete(folderPath, false);
            break;
        case kDeleteFolderPermanentCommandId:
            StartFolderTreeDelete(folderPath, true);
            break;
        case kFolderPropertiesCommandId:
        {
            SHELLEXECUTEINFOW executeInfo{};
            executeInfo.cbSize = sizeof(executeInfo);
            executeInfo.fMask = SEE_MASK_INVOKEIDLIST;
            executeInfo.hwnd = hwnd_;
            executeInfo.lpVerb = L"properties";
            executeInfo.lpFile = folderPath.c_str();
            executeInfo.nShow = SW_SHOWNORMAL;
            if (ShellExecuteExW(&executeInfo) == FALSE)
            {
                MessageBoxW(hwnd_, L"Failed to open the folder properties dialog.", L"Properties", MB_OK | MB_ICONERROR);
            }
            break;
        }
        default:
            if (commandId >= kMoveFolderFavoriteBaseCommandId && commandId <= kMoveFolderFavoriteLastCommandId)
            {
                const std::size_t index = static_cast<std::size_t>(commandId - kMoveFolderFavoriteBaseCommandId);
                if (index < favoriteMoveDestinations.size())
                {
                    StartFolderTreeMoveToDestination(folderPath, favoriteMoveDestinations[index]);
                }
                break;
            }

            if (commandId >= kMoveFolderRecentBaseCommandId && commandId <= kMoveFolderRecentLastCommandId)
            {
                const std::size_t index = static_cast<std::size_t>(commandId - kMoveFolderRecentBaseCommandId);
                if (index < recentMoveDestinations.size())
                {
                    StartFolderTreeMoveToDestination(folderPath, recentMoveDestinations[index]);
                }
                break;
            }
            break;
        }
    }

    void MainWindow::ShowDiagnosticsSnapshot()
    {
        if (!diagnosticsWindow_)
        {
            return;
        }

        diagnosticsWindow_->Show(
            hwnd_,
            decode::DescribeJpegAccelerationState(),
            decode::DescribeRawDecodingState(),
            browserModel_ && !browserModel_->FolderPath().empty() ? browserModel_->FolderPath() : std::wstring(L"(none)"),
            util::CaptureDiagnosticsSnapshot(),
            themeMode_ == ThemeMode::Dark);
        util::LogInfo(L"Opened diagnostics snapshot window.");
    }

    void MainWindow::ShowUserGuide() const
    {
        const std::wstring guidePath = FindUserGuidePath();
        if (guidePath.empty())
        {
            MessageBoxW(hwnd_,
                        L"The local user guide could not be found. Make sure the application's docs folder is present.",
                        L"User Guide",
                        MB_OK | MB_ICONERROR);
            return;
        }

        if (!LaunchShellTarget(hwnd_, L"open", guidePath))
        {
            MessageBoxW(hwnd_,
                        L"Windows could not open the local user guide in the default browser.",
                        L"User Guide",
                        MB_OK | MB_ICONERROR);
        }
    }

    void MainWindow::ShowShortcutReference() const
    {
        if (shortcutReferenceWindow_ && IsWindow(shortcutReferenceWindow_))
        {
            ShowWindow(shortcutReferenceWindow_, SW_RESTORE);
            SetForegroundWindow(shortcutReferenceWindow_);
            return;
        }
        shortcutReferenceWindow_ = nullptr;

        WNDCLASSEXW windowClass{};
        if (GetClassInfoExW(instance_, kShortcutReferenceClassName, &windowClass) == FALSE)
        {
            windowClass.cbSize = sizeof(windowClass);
            windowClass.lpfnWndProc = &ShortcutReferenceProc;
            windowClass.hInstance = instance_;
            windowClass.lpszClassName = kShortcutReferenceClassName;
            windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
            if (RegisterClassExW(&windowClass) == 0)
            {
                return;
            }
        }

        const ThemePalette palette = GetThemePalette();
        auto state = std::make_unique<ShortcutReferenceState>();
        state->ownerWindow = hwnd_;
        state->windowSlot = &shortcutReferenceWindow_;
        state->appTextSize = appTextSize_;
        state->darkMode = themeMode_ == ThemeMode::Dark;
        state->dpi = hwnd_ ? GetDpiForWindow(hwnd_) : 96;
        state->background = palette.windowBackground;
        state->listBackground = palette.paneBackground;
        state->text = palette.text;
        state->mutedText = palette.mutedText;
        state->border = palette.actionStripBorder;
        state->bodyFont = CreateDialogUiFont(9, FW_NORMAL, state->appTextSize);
        if (!state->bodyFont)
        {
            state->bodyFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        }

        const int dpi = static_cast<int>(state->dpi == 0 ? 96 : state->dpi);
        constexpr DWORD shortcutReferenceWindowStyle =
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MAXIMIZEBOX | WS_CLIPCHILDREN;
        constexpr DWORD shortcutReferenceWindowExStyle = WS_EX_TOOLWINDOW | WS_EX_CONTROLPARENT;
        RECT windowRect{0, 0,
                MulDiv(kShortcutReferenceWidth, dpi, 96),
                MulDiv(kShortcutReferenceHeight, dpi, 96)};
        AdjustWindowRectEx(&windowRect,
                           shortcutReferenceWindowStyle,
                           FALSE,
                           shortcutReferenceWindowExStyle);

        HWND dialogWindow = CreateWindowExW(
            shortcutReferenceWindowExStyle,
            kShortcutReferenceClassName,
            L"Keyboard Shortcuts - HyperBrowse",
            shortcutReferenceWindowStyle,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            windowRect.right - windowRect.left,
            windowRect.bottom - windowRect.top,
            hwnd_,
            nullptr,
            instance_,
            state.get());

        if (!dialogWindow)
        {
            DeleteFontIfOwned(state->bodyFont);
            return;
        }

        shortcutReferenceWindow_ = dialogWindow;
        state.release();
        SetWindowTextW(dialogWindow, L"Keyboard Shortcuts - HyperBrowse");
        CenterWindowOnOwner(dialogWindow, hwnd_);
        ShowWindow(dialogWindow, SW_SHOWNORMAL);
        UpdateWindow(dialogWindow);
    }

    void MainWindow::ShowAboutDialog() const
    {
        WNDCLASSEXW windowClass{};
        if (GetClassInfoExW(instance_, kAboutDialogClassName, &windowClass) == FALSE)
        {
            windowClass.cbSize = sizeof(windowClass);
            windowClass.lpfnWndProc = &AboutDialogProc;
            windowClass.hInstance = instance_;
            windowClass.lpszClassName = kAboutDialogClassName;
            windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
            if (RegisterClassExW(&windowClass) == 0)
            {
                return;
            }
        }

        const ThemePalette palette = GetThemePalette();

        AboutDialogState state;
        state.ownerWindow = hwnd_;
        state.instance = instance_;
        state.appTextSize = appTextSize_;
        state.darkMode = themeMode_ == ThemeMode::Dark;
        state.background = palette.windowBackground;
        state.headerBackground = BlendColor(palette.actionStripBackground, palette.accent, state.darkMode ? 28 : 18);
        state.footerBackground = BlendColor(palette.paneBackground, palette.windowBackground, state.darkMode ? 38 : 58);
        state.panelBackground = palette.paneBackground;
        state.border = palette.actionStripBorder;
        state.text = palette.text;
        state.mutedText = palette.mutedText;
        state.accent = palette.accent;
        state.title = hyperbrowse::build::kDisplayName;
        state.subtitle = L"High-performance native image browser for Windows";
        state.intro = L"High-performance browsing and viewing for large Windows image folders.";
        state.bodyHeading = L"What sets it apart";
        state.bodyContent =
            L"- Native Win32 shell tuned for fast startup, compact chrome, and direct file-system browsing.\r\n"
            L"- Async folder enumeration, incremental folder watching, and responsive refresh in large image collections.\r\n"
            L"- Virtualized thumbnail and details views with filtering, metadata-aware sorting, and efficient multi-selection workflows.\r\n"
            L"- Broad format coverage across JPEG, PNG, GIF, TIFF, and major RAW camera formats with graceful fallback behavior.\r\n"
            L"- Dedicated viewer window with slideshow playback, transition effects, overlays, full-screen viewing, and background prefetch.\r\n"
            L"- Practical photographer workflows including batch convert, lossless JPEG orientation adjustment, and Explorer-friendly file operations.\r\n"
            L"- Optional NVIDIA-accelerated JPEG decoding when available, while preserving correctness on systems without GPU acceleration.";
        state.footer =
            L"Copyright (c) "
            + std::to_wstring(CurrentCalendarYear())
            + L" Michael A. McCloskey\r\nLicensed under the MIT License.";
        state.brandArt = util::LoadPngResourceBitmap(instance_, IDB_HYPERBROWSE_BRAND_PNG, kAboutDialogBrandArtSize, kAboutDialogBrandArtSize);
        state.heroIcon = static_cast<HICON>(LoadImageW(instance_, MAKEINTRESOURCEW(IDI_HYPERBROWSE), IMAGE_ICON, 48, 48, LR_DEFAULTCOLOR));
        state.windowIcon = static_cast<HICON>(LoadImageW(instance_, MAKEINTRESOURCEW(IDI_HYPERBROWSE), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR));
        state.titleFont = CreateDialogUiFont(21, FW_BOLD, state.appTextSize);
        state.subtitleFont = CreateDialogUiFont(11, FW_SEMIBOLD, state.appTextSize);
        state.bodyFont = CreateDialogUiFont(10, FW_NORMAL, state.appTextSize);
        state.footerFont = CreateDialogUiFont(9, FW_NORMAL, state.appTextSize);

        if (!state.titleFont) state.titleFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        if (!state.subtitleFont) state.subtitleFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        if (!state.bodyFont) state.bodyFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        if (!state.footerFont) state.footerFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

        state.githubButtonWidth = MeasureAboutDialogLinkButtonWidth(state.subtitleFont,
                                         kAboutDialogGitHubLabel,
                                         kAboutDialogSupportLabel);
        state.supportButtonWidth = state.githubButtonWidth;

        const int aboutClientHeight = std::max(kAboutDialogHeight, MeasureAboutDialogClientHeight(state));
        RECT windowRect{0, 0, kAboutDialogWidth, aboutClientHeight};
        AdjustWindowRectEx(&windowRect,
                           WS_CAPTION | WS_SYSMENU | WS_POPUP | WS_CLIPCHILDREN,
                           FALSE,
                           WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT);

        if (hwnd_)
        {
            EnableWindow(hwnd_, FALSE);
        }

        HWND dialogWindow = CreateWindowExW(
            WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT,
            kAboutDialogClassName,
            L"About HyperBrowse",
            WS_CAPTION | WS_SYSMENU | WS_POPUP | WS_CLIPCHILDREN,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            windowRect.right - windowRect.left,
            windowRect.bottom - windowRect.top,
            hwnd_,
            nullptr,
            instance_,
            &state);

        if (!dialogWindow)
        {
            if (hwnd_)
            {
                EnableWindow(hwnd_, TRUE);
            }
            DeleteFontIfOwned(state.titleFont);
            DeleteFontIfOwned(state.subtitleFont);
            DeleteFontIfOwned(state.bodyFont);
            DeleteFontIfOwned(state.footerFont);
            if (state.heroIcon) DestroyIcon(state.heroIcon);
            if (state.windowIcon) DestroyIcon(state.windowIcon);
            return;
        }

        ShowWindow(dialogWindow, SW_SHOWNORMAL);
        UpdateWindow(dialogWindow);

        MSG message{};
        while (!state.done && GetMessageW(&message, nullptr, 0, 0) > 0)
        {
            if (!IsDialogMessageW(dialogWindow, &message))
            {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }

        if (hwnd_)
        {
            EnableWindow(hwnd_, TRUE);
            SetForegroundWindow(hwnd_);
            SetActiveWindow(hwnd_);
        }

        DeleteFontIfOwned(state.titleFont);
        DeleteFontIfOwned(state.subtitleFont);
        DeleteFontIfOwned(state.bodyFont);
        DeleteFontIfOwned(state.footerFont);
        if (state.heroIcon) DestroyIcon(state.heroIcon);
        if (state.windowIcon) DestroyIcon(state.windowIcon);
    }

    void MainWindow::ResetDiagnosticsState()
    {
        util::ResetDiagnostics();
        util::LogInfo(L"Diagnostics timings and counters were reset.");

        if (diagnosticsWindow_ && diagnosticsWindow_->IsOpen())
        {
            diagnosticsWindow_->Show(
                hwnd_,
                decode::DescribeJpegAccelerationState(),
                decode::DescribeRawDecodingState(),
                browserModel_ && !browserModel_->FolderPath().empty() ? browserModel_->FolderPath() : std::wstring(L"(none)"),
                util::CaptureDiagnosticsSnapshot(),
                themeMode_ == ThemeMode::Dark);
        }

        MessageBoxW(hwnd_, L"Diagnostics timings and counters were reset.", L"Diagnostics", MB_OK | MB_ICONINFORMATION);
    }

    void MainWindow::ShowImageInformation()
    {
        if (!browserPaneController_)
        {
            return;
        }

        const int modelIndex = browserPaneController_->PrimarySelectedModelIndex();
        if (modelIndex < 0)
        {
            MessageBoxW(hwnd_, L"Select an image first.", L"Image Information", MB_OK | MB_ICONINFORMATION);
            return;
        }

        const auto& items = browserModel_->Items();
        if (modelIndex >= static_cast<int>(items.size()))
        {
            return;
        }

        const browser::BrowserItem& item = items[static_cast<std::size_t>(modelIndex)];
        std::wstring errorMessage;
        const auto metadata = browserPaneController_->FindCachedMetadataForModelIndex(modelIndex)
            ? browserPaneController_->FindCachedMetadataForModelIndex(modelIndex)
            : services::ExtractImageMetadata(item, &errorMessage);

        if (!metadata)
        {
            MessageBoxW(hwnd_,
                        errorMessage.empty() ? L"No metadata is available for the selected image." : errorMessage.c_str(),
                        L"Image Information",
                        MB_OK | MB_ICONINFORMATION);
            return;
        }

        const std::wstring content = services::FormatImageInfoContent(item);
        const std::wstring expanded = services::FormatImageInfoExpanded(*metadata);

        TASKDIALOGCONFIG config{};
        config.cbSize = sizeof(config);
        config.hwndParent = hwnd_;
        config.hInstance = instance_;
        config.dwFlags = TDF_EXPAND_FOOTER_AREA;
        config.dwCommonButtons = TDCBF_OK_BUTTON;
        config.pszWindowTitle = L"Image Information";
        config.pszMainIcon = MAKEINTRESOURCEW(IDI_HYPERBROWSE);
        config.pszMainInstruction = item.fileName.c_str();
        config.pszContent = content.c_str();
        config.pszExpandedInformation = expanded.c_str();
        config.pszCollapsedControlText = L"Show Metadata Details";
        config.pszExpandedControlText = L"Hide Metadata Details";

        TaskDialogIndirect(&config, nullptr, nullptr, nullptr);
    }

    void MainWindow::StartCopySelection()
    {
        if (!browserPaneController_ || fileOperationActive_)
        {
            return;
        }

        if (browserPaneController_->SelectedFilePathsSnapshot().empty())
        {
            MessageBoxW(hwnd_, L"Select one or more images first.", L"Copy Selection", MB_OK | MB_ICONINFORMATION);
            return;
        }

        std::wstring destinationFolder;
        if (!ChooseFolder(&destinationFolder) || destinationFolder.empty())
        {
            return;
        }

        StartSelectionFileOperationToDestination(services::FileOperationType::Copy, std::move(destinationFolder));
    }

    void MainWindow::StartRenameSelectedImage()
    {
        if (!browserModel_ || !browserPaneController_ || fileOperationActive_)
        {
            return;
        }

        if (browserPaneController_->SelectedCount() != 1)
        {
            MessageBoxW(hwnd_, L"Select a single image to rename.", L"Rename Image", MB_OK | MB_ICONINFORMATION);
            return;
        }

        const int modelIndex = browserPaneController_->PrimarySelectedModelIndex();
        const auto& items = browserModel_->Items();
        if (modelIndex < 0 || modelIndex >= static_cast<int>(items.size()))
        {
            return;
        }

        const browser::BrowserItem& item = items[static_cast<std::size_t>(modelIndex)];
        std::wstring renamedLeafName;
        if (!PromptForRenameLeafName(hwnd_,
                                     instance_,
                                     appTextSize_,
                                     L"Rename File",
                                     L"Enter a new file name.",
                                     item.fileName,
                                     true,
                                     &renamedLeafName))
        {
            return;
        }

        StartFileOperation(services::FileOperationType::Rename,
                           {item.filePath},
                           {},
                           services::FileConflictPolicy::PromptShell,
                           {renamedLeafName});
    }

    void MainWindow::StartBatchRenameSelection()
    {
        if (!browserModel_ || !browserPaneController_ || fileOperationActive_)
        {
            return;
        }

        const std::vector<int> selectedModelIndices = browserPaneController_->OrderedSelectedModelIndicesSnapshot();
        if (selectedModelIndices.size() < 2)
        {
            MessageBoxW(hwnd_, L"Select two or more images to batch rename.", L"Batch Rename", MB_OK | MB_ICONINFORMATION);
            return;
        }

        const auto& items = browserModel_->Items();
        std::vector<browser::BrowserItem> selectedItems;
        std::vector<std::wstring> sourcePaths;
        selectedItems.reserve(selectedModelIndices.size());
        sourcePaths.reserve(selectedModelIndices.size());
        for (const int modelIndex : selectedModelIndices)
        {
            if (modelIndex < 0 || modelIndex >= static_cast<int>(items.size()))
            {
                continue;
            }

            const browser::BrowserItem& item = items[static_cast<std::size_t>(modelIndex)];
            selectedItems.push_back(item);
            sourcePaths.push_back(item.filePath);
        }

        if (sourcePaths.size() < 2)
        {
            MessageBoxW(hwnd_, L"Select two or more images to batch rename.", L"Batch Rename", MB_OK | MB_ICONINFORMATION);
            return;
        }

        std::wstring initialPattern = TrimWhitespaceCopy(fs::path(selectedItems.front().fileName).stem().wstring());
        if (initialPattern.empty())
        {
            initialPattern = L"Image";
        }

        std::vector<std::wstring> targetLeafNames;
        if (!PromptForBatchRenamePattern(hwnd_,
                                         instance_,
                                         appTextSize_,
                                         std::move(initialPattern),
                                         std::move(selectedItems),
                                         &targetLeafNames))
        {
            return;
        }

        StartFileOperation(services::FileOperationType::Rename,
                           std::move(sourcePaths),
                           {},
                           services::FileConflictPolicy::PromptShell,
                           std::move(targetLeafNames));
    }

    void MainWindow::StartMoveSelection()
    {
        if (!browserPaneController_ || fileOperationActive_)
        {
            return;
        }

        if (browserPaneController_->SelectedFilePathsSnapshot().empty())
        {
            MessageBoxW(hwnd_, L"Select one or more images first.", L"Move Selection", MB_OK | MB_ICONINFORMATION);
            return;
        }

        std::wstring destinationFolder;
        if (!ChooseFolder(&destinationFolder) || destinationFolder.empty())
        {
            return;
        }

        StartSelectionFileOperationToDestination(services::FileOperationType::Move, std::move(destinationFolder));
    }

    void MainWindow::StartMoveSelectionToNewChildFolder()
    {
        if (!browserPaneController_ || !browserModel_ || fileOperationActive_)
        {
            return;
        }

        const std::vector<std::wstring> selectedPaths = browserPaneController_->SelectedFilePathsSnapshot();
        if (selectedPaths.size() < 2)
        {
            MessageBoxW(hwnd_, L"Select two or more images first.", L"Move to New Child Folder", MB_OK | MB_ICONINFORMATION);
            return;
        }

        const std::wstring parentPath = NormalizeFolderPath(browserModel_->FolderPath());
        if (parentPath.empty())
        {
            MessageBoxW(hwnd_, L"Open a folder first.", L"Move to New Child Folder", MB_OK | MB_ICONINFORMATION);
            return;
        }

        std::error_code parentError;
        if (!fs::is_directory(fs::path(parentPath), parentError) || parentError)
        {
            MessageBoxW(hwnd_, L"The active folder is no longer available.", L"Move to New Child Folder", MB_OK | MB_ICONINFORMATION);
            return;
        }

        std::vector<std::wstring> selectedLeafNames;
        selectedLeafNames.reserve(selectedPaths.size());
        for (const std::wstring& selectedPath : selectedPaths)
        {
            const std::wstring leafName = TrimWhitespaceCopy(fs::path(selectedPath).stem().wstring());
            if (!leafName.empty())
            {
                selectedLeafNames.push_back(leafName);
            }
        }

        std::wstring initialFolderName = TrimTrailingFolderNameSeparators(
            TrimWhitespaceCopy(LongestCommonPrefix(selectedLeafNames)));
        std::wstring validationError;
        if (initialFolderName.empty() || !IsValidFolderName(initialFolderName, &validationError))
        {
            initialFolderName = L"New Folder";
        }

        std::wstring folderName;
        while (PromptForSingleLineText(hwnd_,
                                       instance_,
                                       appTextSize_,
                                       L"Move to New Child Folder",
                                       L"Enter a name for the new child folder.",
                                       L"Create and Move",
                                       initialFolderName,
                                       0,
                                       static_cast<int>(initialFolderName.size()),
                                       &folderName))
        {
            std::wstring errorMessage;
            if (!IsValidFolderName(folderName, &errorMessage))
            {
                MessageBoxW(hwnd_, errorMessage.c_str(), L"Move to New Child Folder", MB_OK | MB_ICONWARNING);
                continue;
            }

            const std::wstring destinationFolder = NormalizeFolderPath((fs::path(parentPath) / fs::path(folderName)).wstring());
            std::error_code createError;
            if (!fs::create_directory(fs::path(destinationFolder), createError) || createError)
            {
                MessageBoxW(hwnd_,
                            L"Failed to create the folder. Check that the name is unique and that you have permission.",
                            L"Move to New Child Folder",
                            MB_OK | MB_ICONERROR);
                continue;
            }

            StartSelectionFileOperationToDestination(services::FileOperationType::Move, destinationFolder);
            break;
        }
    }

    void MainWindow::StartDeleteSelection(bool permanent)
    {
        if (!browserPaneController_ || fileOperationActive_)
        {
            return;
        }

        const std::vector<std::wstring> sourcePaths = SelectedFileOperationPathsSnapshot();
        if (sourcePaths.empty())
        {
            MessageBoxW(hwnd_, L"Select one or more images first.", permanent ? L"Permanent Delete" : L"Delete", MB_OK | MB_ICONINFORMATION);
            return;
        }

        if (ShouldConfirmDeletion(permanent) && !ConfirmFileDeletion(hwnd_, sourcePaths.size(), permanent))
        {
            return;
        }

        StartFileOperation(permanent ? services::FileOperationType::DeletePermanent : services::FileOperationType::DeleteRecycleBin,
                           std::vector<std::wstring>(sourcePaths),
                           {},
                           services::FileConflictPolicy::PromptShell,
                           {});
    }

    void MainWindow::StartFolderTreeRename(std::wstring folderPath)
    {
        if (folderPath.empty() || fileOperationActive_)
        {
            return;
        }

        folderPath = NormalizeFolderPath(std::move(folderPath));
        std::error_code error;
        if (!fs::is_directory(fs::path(folderPath), error) || error)
        {
            MessageBoxW(hwnd_, L"The selected folder is no longer available.", L"Rename Folder", MB_OK | MB_ICONINFORMATION);
            RefreshFolderTree();
            return;
        }

        const std::wstring currentLeafName = fs::path(folderPath).filename().wstring();
        std::wstring renamedLeafName;
        if (!PromptForRenameLeafName(hwnd_,
                                     instance_,
                                     appTextSize_,
                                     L"Rename Folder",
                                     L"Enter a new folder name.",
                                     currentLeafName,
                                     false,
                                     &renamedLeafName))
        {
            return;
        }

        activeTreeFolderRenamePath_ = folderPath;
        StartFileOperation(services::FileOperationType::Rename,
                           {folderPath},
                           {},
                           services::FileConflictPolicy::PromptShell,
                           {renamedLeafName});
    }

    bool MainWindow::BeginFolderTreeInlineRename(const std::wstring& folderPath)
    {
        if (!treePane_ || folderPath.empty() || fileOperationActive_)
        {
            return false;
        }

        HTREEITEM item = FindFolderTreeItemByPath(folderPath);
        if (!item || !TreeView_GetParent(treePane_, item))
        {
            return false;
        }

        TreeView_SelectItem(treePane_, item);
        TreeView_EnsureVisible(treePane_, item);
        return TreeView_EditLabel(treePane_, item) != nullptr;
    }

    void MainWindow::StartFolderTreeDelete(std::wstring folderPath, bool permanent)
    {
        if (folderPath.empty() || fileOperationActive_)
        {
            return;
        }

        folderPath = NormalizeFolderPath(std::move(folderPath));

        std::error_code error;
        if (!fs::is_directory(fs::path(folderPath), error) || error)
        {
            MessageBoxW(hwnd_,
                        L"The selected folder is no longer available.",
                        permanent ? L"Permanent Delete Folder" : L"Delete Folder",
                        MB_OK | MB_ICONINFORMATION);
            RefreshFolderTree();
            return;
        }

        if (ShouldConfirmDeletion(permanent) && !ConfirmFolderDeletion(hwnd_, folderPath, permanent))
        {
            return;
        }

        activeTreeFolderOperationPath_ = folderPath;
        StartFileOperation(permanent ? services::FileOperationType::DeletePermanent : services::FileOperationType::DeleteRecycleBin,
                           {folderPath},
                           {},
                           services::FileConflictPolicy::PromptShell,
                           {});
    }

    void MainWindow::StartFolderTreeCreateNewFolder(std::wstring parentPath)
    {
        if (parentPath.empty() || fileOperationActive_)
        {
            return;
        }

        parentPath = NormalizeFolderPath(std::move(parentPath));

        std::error_code parentError;
        if (!fs::is_directory(fs::path(parentPath), parentError) || parentError)
        {
            MessageBoxW(hwnd_,
                        L"The selected folder is no longer available.",
                        L"New Folder",
                        MB_OK | MB_ICONINFORMATION);
            RefreshFolderTree();
            return;
        }

        std::wstring folderName;
        const std::wstring initialName = L"New Folder";
        while (PromptForSingleLineText(hwnd_,
                                       instance_,
                                       appTextSize_,
                                       L"New Folder",
                                       L"Enter a name for the new folder.",
                                       L"Create",
                                       initialName,
                                       0,
                                       static_cast<int>(initialName.size()),
                                       &folderName))
        {
            std::wstring errorMessage;
            if (!IsValidFolderName(folderName, &errorMessage))
            {
                MessageBoxW(hwnd_, errorMessage.c_str(), L"New Folder", MB_OK | MB_ICONWARNING);
                continue;
            }

            const std::wstring newFolderPath = NormalizeFolderPath(
                (fs::path(parentPath) / fs::path(folderName)).wstring());

            std::error_code createError;
            if (!fs::create_directory(fs::path(newFolderPath), createError) || createError)
            {
                MessageBoxW(hwnd_,
                            L"Failed to create the folder. Check that the name is valid and you have permission.",
                            L"New Folder",
                            MB_OK | MB_ICONERROR);
                continue;
            }

            InsertFolderTreeFolderIfParentLoaded(newFolderPath);
            if (browserModel_ && !browserModel_->FolderPath().empty()
                && FolderPathsEqual(browserModel_->FolderPath(), parentPath))
            {
                LoadFolderAsync(parentPath);
            }
            break;
        }
    }

    void MainWindow::StartFolderTreeMoveToDestination(std::wstring folderPath, std::wstring destinationFolder)
    {
        if (folderPath.empty() || destinationFolder.empty() || fileOperationActive_)
        {
            return;
        }

        folderPath = NormalizeFolderPath(std::move(folderPath));
        destinationFolder = NormalizeFolderPath(std::move(destinationFolder));

        std::error_code sourceError;
        if (!fs::is_directory(fs::path(folderPath), sourceError) || sourceError)
        {
            MessageBoxW(hwnd_,
                        L"The selected folder is no longer available.",
                        L"Move Folder",
                        MB_OK | MB_ICONINFORMATION);
            RefreshFolderTree();
            return;
        }

        if (!IsExistingDirectory(destinationFolder))
        {
            MessageBoxW(hwnd_,
                        L"The selected destination folder is no longer available.",
                        L"Move Folder",
                        MB_OK | MB_ICONINFORMATION);
            return;
        }

        if (!IsValidFolderTreeDropDestination(folderPath, destinationFolder))
        {
            MessageBoxW(hwnd_,
                        L"Choose a valid destination on the same drive. The folder cannot move into itself, one of its children, or its current parent.",
                        L"Move Folder",
                        MB_OK | MB_ICONINFORMATION);
            return;
        }

        activeTreeFolderMoveSourcePath_ = folderPath;
        activeTreeFolderMoveDestinationFolder_ = destinationFolder;
        StartFileOperation(services::FileOperationType::Move,
                           {folderPath},
                           std::move(destinationFolder),
                           services::FileConflictPolicy::PromptShell,
                           {});
    }

    bool MainWindow::StartFileOperation(services::FileOperationType type,
                                        std::vector<std::wstring> sourcePaths,
                                        std::wstring destinationFolder,
                                        services::FileConflictPolicy conflictPolicy,
                                        std::vector<std::wstring> targetLeafNames,
                                        HWND ownerWindow)
    {
        if (!fileOperationService_ || sourcePaths.empty() || fileOperationActive_)
        {
            return false;
        }

        activeFileOperationLabel_ = services::FileOperationTypeToActivityLabel(type);
        activeFileOperationLabel_.append(L" ");
        activeFileOperationLabel_.append(std::to_wstring(sourcePaths.size()));
        activeFileOperationLabel_.append(L" item(s)");
        fileOperationActive_ = true;
        // Remember who was active first, so shell-operation completion can hand
        // activation back when the operation was initiated from the viewer.
        foregroundWindowAtFileOperationStart_ = GetForegroundWindow();
        const HWND currentFocusWindow = GetFocus();
        const bool viewerOperationOrigin = viewerWindow_
            && viewerWindow_->IsOpen()
            && ((currentFocusWindow
                 && (currentFocusWindow == viewerWindow_->Hwnd()
                     || IsChild(viewerWindow_->Hwnd(), currentFocusWindow)))
                || !pendingViewerDeleteSourcePath_.empty()
                || pendingViewerQuickSend_.active);
        if (viewerOperationOrigin)
        {
            focusWindowAtFileOperationStart_ = currentFocusWindow
                && (currentFocusWindow == viewerWindow_->Hwnd()
                    || IsChild(viewerWindow_->Hwnd(), currentFocusWindow))
                ? currentFocusWindow
                : viewerWindow_->Hwnd();
        }
        else if (!activeTreeFolderOperationPath_.empty() && treePane_)
        {
            focusWindowAtFileOperationStart_ = treePane_;
        }
        else if (browserPaneController_)
        {
            focusWindowAtFileOperationStart_ = browserPaneController_->Hwnd();
        }
        else if (currentFocusWindow
            && (currentFocusWindow == hwnd_ || IsChild(hwnd_, currentFocusWindow)))
        {
            focusWindowAtFileOperationStart_ = currentFocusWindow;
        }
        const HWND operationOwnerWindow = ownerWindow && IsWindow(ownerWindow)
            ? ownerWindow
            : hwnd_;
        activeFileOperationRequestId_ = fileOperationService_->Start(
            hwnd_,
            operationOwnerWindow,
            type,
            std::move(sourcePaths),
            std::move(destinationFolder),
            conflictPolicy,
            std::move(targetLeafNames));
        UpdateTaskbarProgress(0, 1); // show the bar immediately (0%) until ticks arrive
        UpdateStatusText();
        UpdateMenuState();
        return true;
    }

    void MainWindow::UpdateTaskbarProgress(ULONGLONG completed, ULONGLONG total)
    {
        if (!hwnd_)
        {
            return;
        }

        if (!taskbarList_)
        {
            // Lazy-init; ITaskbarList3 is available on Windows 7+.
            if (FAILED(CoCreateInstance(CLSID_TaskbarList,
                                        nullptr,
                                        CLSCTX_INPROC_SERVER,
                                        IID_PPV_ARGS(&taskbarList_)))
                || !taskbarList_)
            {
                return;
            }
            taskbarList_->HrInit();
        }

        taskbarProgressActive_ = true;
        taskbarList_->SetProgressState(hwnd_, TBPF_NORMAL);
        taskbarList_->SetProgressValue(hwnd_, completed, total);
    }

    void MainWindow::ClearTaskbarProgress()
    {
        if (!taskbarList_ || !hwnd_ || !taskbarProgressActive_)
        {
            return;
        }

        taskbarList_->SetProgressState(hwnd_, TBPF_NOPROGRESS);
        taskbarProgressActive_ = false;
    }

    void MainWindow::EnsureTrayIcon()
    {
        if (trayIconAdded_ || !hwnd_)
        {
            return;
        }

        if (trayIconMessageId_ == 0)
        {
            trayIconMessageId_ = RegisterWindowMessageW(L"TheTheosopher.HyperBrowse.TrayIcon");
        }

        NOTIFYICONDATAW iconData{};
        iconData.cbSize = sizeof(iconData);
        iconData.hWnd = hwnd_;
        iconData.uID = 1;
        iconData.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
        iconData.uCallbackMessage = trayIconMessageId_;
        iconData.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(IDI_HYPERBROWSE));
        if (!iconData.hIcon)
        {
            iconData.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        }
        wcsncpy_s(iconData.szTip, L"HyperBrowse", _TRUNCATE);
        trayIconAdded_ = Shell_NotifyIconW(NIM_ADD, &iconData) != FALSE;
    }

    void MainWindow::RemoveTrayIcon()
    {
        if (!trayIconAdded_ || !hwnd_)
        {
            return;
        }

        NOTIFYICONDATAW iconData{};
        iconData.cbSize = sizeof(iconData);
        iconData.hWnd = hwnd_;
        iconData.uID = 1;
        Shell_NotifyIconW(NIM_DELETE, &iconData);
        trayIconAdded_ = false;
    }

    void MainWindow::NotifyLongOperationComplete(const std::wstring& title, const std::wstring& message)
    {
        // Only notify when the user has moved focus elsewhere; an in-focus completion
        // is already visible in the status bar / dialogs.
        if (GetForegroundWindow() == hwnd_)
        {
            return;
        }

        EnsureTrayIcon();
        if (!trayIconAdded_)
        {
            return;
        }

        NOTIFYICONDATAW iconData{};
        iconData.cbSize = sizeof(iconData);
        iconData.hWnd = hwnd_;
        iconData.uID = 1;
        iconData.uFlags = NIF_INFO;
        iconData.dwInfoFlags = NIIF_INFO;
        wcsncpy_s(iconData.szInfoTitle, title.c_str(), _TRUNCATE);
        wcsncpy_s(iconData.szInfo, message.c_str(), _TRUNCATE);
        Shell_NotifyIconW(NIM_MODIFY, &iconData);
    }

    LRESULT MainWindow::OnFileOperationProgressMessage(LPARAM lParam)
    {
        std::unique_ptr<services::FileOperationProgress> progress(
            reinterpret_cast<services::FileOperationProgress*>(lParam));
        if (!progress || progress->requestId != activeFileOperationRequestId_)
        {
            return 0;
        }

        if (progress->total > 0)
        {
            UpdateTaskbarProgress(progress->completed, progress->total);
        }
        return 0;
    }

    void MainWindow::RevealSelectedInExplorer() const
    {
        if (!browserPaneController_)
        {
            return;
        }

        const std::vector<std::wstring> selectedPaths = SelectedFileOperationPathsSnapshot();
        if (selectedPaths.empty())
        {
            MessageBoxW(hwnd_, L"Select an image first.", L"Reveal in Explorer", MB_OK | MB_ICONINFORMATION);
            return;
        }

        if (!RevealPathsInExplorer(selectedPaths))
        {
            MessageBoxW(hwnd_, L"Failed to reveal the selected items in Explorer.", L"Reveal in Explorer", MB_OK | MB_ICONERROR);
        }
    }

    void MainWindow::OpenSelectedContainingFolder() const
    {
        if (!browserPaneController_)
        {
            return;
        }

        std::wstring targetPath = browserPaneController_->FocusedFilePathSnapshot();
        if (targetPath.empty())
        {
            const std::vector<std::wstring> selectedPaths = browserPaneController_->SelectedFilePathsSnapshot();
            if (!selectedPaths.empty())
            {
                targetPath = selectedPaths.front();
            }
        }

        if (targetPath.empty())
        {
            MessageBoxW(hwnd_, L"Select an image first.", L"Open Containing Folder", MB_OK | MB_ICONINFORMATION);
            return;
        }

        const std::wstring containingFolder = fs::path(targetPath).parent_path().wstring();
        if (!LaunchShellTarget(hwnd_, L"open", containingFolder))
        {
            MessageBoxW(hwnd_, L"Failed to open the containing folder.", L"Open Containing Folder", MB_OK | MB_ICONERROR);
        }
    }

    void MainWindow::CopySelectedPathsToClipboard() const
    {
        if (!browserPaneController_)
        {
            return;
        }

        const std::vector<std::wstring> selectedPaths = SelectedFileOperationPathsSnapshot();
        if (selectedPaths.empty())
        {
            MessageBoxW(hwnd_, L"Select one or more images first.", L"Copy Path", MB_OK | MB_ICONINFORMATION);
            return;
        }

        if (!CopyTextToClipboard(hwnd_, JoinLines(selectedPaths)))
        {
            MessageBoxW(hwnd_, L"Failed to copy the selected file paths to the clipboard.", L"Copy Path", MB_OK | MB_ICONERROR);
        }
    }

    void MainWindow::ShowSelectedFileProperties() const
    {
        if (!browserPaneController_)
        {
            return;
        }

        const std::vector<std::wstring> selectedPaths = browserPaneController_->SelectedFilePathsSnapshot();
        if (selectedPaths.size() > 1)
        {
            if (!ShowMultiFilePropertiesDialog(selectedPaths))
            {
                MessageBoxW(hwnd_, L"Failed to open the file properties dialog.", L"Properties", MB_OK | MB_ICONERROR);
            }
            return;
        }

        std::wstring targetPath = browserPaneController_->FocusedFilePathSnapshot();
        if (targetPath.empty())
        {
            if (!selectedPaths.empty())
            {
                targetPath = selectedPaths.front();
            }
        }

        if (targetPath.empty())
        {
            MessageBoxW(hwnd_, L"Select an image first.", L"Properties", MB_OK | MB_ICONINFORMATION);
            return;
        }

        SHELLEXECUTEINFOW executeInfo{};
        executeInfo.cbSize = sizeof(executeInfo);
        executeInfo.fMask = SEE_MASK_INVOKEIDLIST;
        executeInfo.hwnd = hwnd_;
        executeInfo.lpVerb = L"properties";
        executeInfo.lpFile = targetPath.c_str();
        executeInfo.nShow = SW_SHOWNORMAL;

        if (ShellExecuteExW(&executeInfo) == FALSE)
        {
            MessageBoxW(hwnd_, L"Failed to open the file properties dialog.", L"Properties", MB_OK | MB_ICONERROR);
        }
    }

    void MainWindow::CopySelectionFilesToClipboard(bool movePreferred) const
    {
        if (!browserPaneController_)
        {
            return;
        }

        const std::vector<std::wstring> selectedPaths = SelectedFileOperationPathsSnapshot();
        if (selectedPaths.empty())
        {
            MessageBoxW(hwnd_,
                        L"Select one or more images first.",
                        movePreferred ? L"Cut" : L"Copy",
                        MB_OK | MB_ICONINFORMATION);
            return;
        }

        // Build a CF_HDROP (DROPFILES + double-NUL-terminated path list) so the
        // selection can be pasted into Explorer or any other shell target.
        std::size_t pathChars = 0;
        for (const std::wstring& path : selectedPaths)
        {
            pathChars += path.size() + 1;
        }

        const std::size_t totalBytes = sizeof(DROPFILES) + (pathChars + 1) * sizeof(wchar_t);
        HGLOBAL buffer = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, totalBytes);
        if (!buffer)
        {
            MessageBoxW(hwnd_,
                        L"Failed to allocate the clipboard data.",
                        movePreferred ? L"Cut" : L"Copy",
                        MB_OK | MB_ICONERROR);
            return;
        }

        auto* dropFiles = static_cast<DROPFILES*>(GlobalLock(buffer));
        if (!dropFiles)
        {
            GlobalFree(buffer);
            MessageBoxW(hwnd_,
                        L"Failed to lock the clipboard data.",
                        movePreferred ? L"Cut" : L"Copy",
                        MB_OK | MB_ICONERROR);
            return;
        }

        dropFiles->pFiles = sizeof(DROPFILES);
        dropFiles->fWide = TRUE;
        wchar_t* cursor = reinterpret_cast<wchar_t*>(reinterpret_cast<BYTE*>(dropFiles) + sizeof(DROPFILES));
        for (const std::wstring& path : selectedPaths)
        {
            memcpy(cursor, path.data(), path.size() * sizeof(wchar_t));
            cursor += path.size() + 1;
        }
        GlobalUnlock(buffer);

        // Signal copy or cut semantics so Paste can reuse the shell's move contract.
        HGLOBAL effectBuffer = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(DWORD));
        if (effectBuffer)
        {
            if (DWORD* effect = static_cast<DWORD*>(GlobalLock(effectBuffer)))
            {
                *effect = movePreferred ? DROPEFFECT_MOVE : DROPEFFECT_COPY;
                GlobalUnlock(effectBuffer);
            }
        }

        if (!OpenClipboard(hwnd_))
        {
            GlobalFree(buffer);
            if (effectBuffer)
            {
                GlobalFree(effectBuffer);
            }
            MessageBoxW(hwnd_,
                        L"Failed to open the clipboard.",
                        movePreferred ? L"Cut" : L"Copy",
                        MB_OK | MB_ICONERROR);
            return;
        }

        bool success = EmptyClipboard() != FALSE;
        if (success && SetClipboardData(CF_HDROP, buffer) == nullptr)
        {
            success = false;
        }
        if (success && effectBuffer)
        {
            const UINT effectFormat = RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT);
            if (SetClipboardData(effectFormat, effectBuffer) == nullptr)
            {
                GlobalFree(effectBuffer);
            }
        }
        if (!success)
        {
            GlobalFree(buffer);
        }
        CloseClipboard();

        if (!success)
        {
            MessageBoxW(hwnd_,
                        movePreferred ? L"Failed to cut the selected files to the clipboard."
                                      : L"Failed to copy the selected files to the clipboard.",
                        movePreferred ? L"Cut" : L"Copy",
                        MB_OK | MB_ICONERROR);
        }
    }

    void MainWindow::PasteClipboardFilesIntoCurrentFolder()
    {
        if (!browserModel_ || browserModel_->FolderPath().empty())
        {
            return;
        }

        if (fileOperationActive_)
        {
            MessageBoxW(hwnd_, L"A file operation is already in progress.", L"Paste", MB_OK | MB_ICONINFORMATION);
            return;
        }

        if (!OpenClipboard(hwnd_))
        {
            return;
        }

        std::vector<std::wstring> sourcePaths;
        DWORD pasteEffect = DROPEFFECT_COPY;
        if (IsClipboardFormatAvailable(CF_HDROP))
        {
            if (HGLOBAL data = GetClipboardData(CF_HDROP))
            {
                if (auto* dropFiles = static_cast<const DROPFILES*>(GlobalLock(data)))
                {
                    if (dropFiles->fWide)
                    {
                        const wchar_t* cursor = reinterpret_cast<const wchar_t*>(
                            reinterpret_cast<const BYTE*>(dropFiles) + dropFiles->pFiles);
                        while (*cursor != L'\0')
                        {
                            const std::size_t length = wcslen(cursor);
                            if (length == 0)
                            {
                                break;
                            }
                            sourcePaths.emplace_back(cursor, length);
                            cursor += length + 1;
                        }
                    }
                    GlobalUnlock(data);
                }
            }

            const UINT effectFormat = RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT);
            if (HGLOBAL effectData = GetClipboardData(effectFormat))
            {
                if (const DWORD* effect = static_cast<const DWORD*>(GlobalLock(effectData)))
                {
                    pasteEffect = *effect;
                    GlobalUnlock(effectData);
                }
            }
        }
        CloseClipboard();

        if (sourcePaths.empty())
        {
            return;
        }

        const std::wstring destinationFolder = browserModel_->FolderPath();
        // A "cut" paste is a move; an Explorer copy is a copy. Same-drive copies stay
        // copies; cross-drive moves from a cut degrade gracefully via shell conflicts.
        const services::FileOperationType type = (pasteEffect & DROPEFFECT_MOVE) != 0
            ? services::FileOperationType::Move
            : services::FileOperationType::Copy;
        StartFileOperation(type,
                           std::move(sourcePaths),
                           destinationFolder,
                           services::FileConflictPolicy::PromptShell,
                           {});
    }

    void MainWindow::SetDesktopWallpaperFromImageFile(const std::wstring& imagePath)
    {
        if (imagePath.empty())
        {
            return;
        }

        if (SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0, const_cast<PWCHAR>(imagePath.c_str()), SPIF_UPDATEINIFILE | SPIF_SENDCHANGE) == FALSE)
        {
            MessageBoxW(hwnd_, L"Failed to set the desktop wallpaper.", L"Set Wallpaper", MB_OK | MB_ICONERROR);
        }
    }

    void MainWindow::CopySelectedImagePixelsToClipboard(std::wstring_view preferredPath)
    {
        if (!browserPaneController_ || !browserModel_)
        {
            return;
        }

        const int modelIndex = preferredPath.empty()
            ? browserPaneController_->PrimarySelectedModelIndex()
            : browserModel_->FindItemIndexByPath(preferredPath);
        if (modelIndex < 0 || modelIndex >= static_cast<int>(browserModel_->Items().size()))
        {
            MessageBoxW(hwnd_, L"Select a single image first.", L"Copy Image", MB_OK | MB_ICONINFORMATION);
            return;
        }

        const browser::BrowserItem& item = browserModel_->Items()[static_cast<std::size_t>(modelIndex)];
        std::wstring errorMessage;
        const auto image = decode::DecodeFullImage(item, &errorMessage);
        if (!image || !image->Bitmap())
        {
            MessageBoxW(hwnd_,
                        errorMessage.empty() ? L"Unable to decode the selected image." : errorMessage.c_str(),
                        L"Copy Image",
                        MB_OK | MB_ICONERROR);
            return;
        }

        // Convert the decoded bitmap into a packed CF_DIB (BITMAPINFOHEADER + pixels),
        // which is the clipboard format most image editors read.
        HDC screenDc = GetDC(nullptr);
        HDC memoryDc = CreateCompatibleDC(screenDc);
        HGDIOBJ oldBitmap = memoryDc ? SelectObject(memoryDc, image->Bitmap()) : nullptr;

        BITMAP bitmapInfo{};
        GetObjectW(image->Bitmap(), sizeof(bitmapInfo), &bitmapInfo);
        const int width = bitmapInfo.bmWidth;
        const int height = bitmapInfo.bmHeight;

        BITMAPINFOHEADER header{};
        header.biSize = sizeof(header);
        header.biWidth = width;
        header.biHeight = height; // bottom-up
        header.biPlanes = 1;
        header.biBitCount = 32;
        header.biCompression = BI_RGB;

        const std::size_t pixelBytes = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;
        const std::size_t totalBytes = sizeof(BITMAPINFOHEADER) + pixelBytes;
        HGLOBAL dibBuffer = GlobalAlloc(GMEM_MOVEABLE, totalBytes);
        bool success = false;
        if (dibBuffer)
        {
            if (BYTE* dib = static_cast<BYTE*>(GlobalLock(dibBuffer)))
            {
                memcpy(dib, &header, sizeof(header));
                const int scanLines = memoryDc
                    ? GetDIBits(memoryDc,
                                image->Bitmap(),
                                0,
                                static_cast<UINT>(height),
                                dib + sizeof(BITMAPINFOHEADER),
                                reinterpret_cast<BITMAPINFO*>(dib),
                                DIB_RGB_COLORS)
                    : 0;
                GlobalUnlock(dibBuffer);
                success = scanLines == height;
            }
            if (!success)
            {
                GlobalFree(dibBuffer);
                dibBuffer = nullptr;
            }
        }

        if (memoryDc)
        {
            if (oldBitmap)
            {
                SelectObject(memoryDc, oldBitmap);
            }
            DeleteDC(memoryDc);
        }
        if (screenDc)
        {
            ReleaseDC(nullptr, screenDc);
        }

        if (success && OpenClipboard(hwnd_))
        {
            success = EmptyClipboard() != FALSE && SetClipboardData(CF_DIB, dibBuffer) != nullptr;
            CloseClipboard();
        }

        if (!success)
        {
            if (dibBuffer)
            {
                GlobalFree(dibBuffer);
            }
            MessageBoxW(hwnd_, L"Failed to copy the image to the clipboard.", L"Copy Image", MB_OK | MB_ICONERROR);
        }
    }

    void MainWindow::StartDuplicateSelection()
    {
        if (!browserPaneController_ || !browserModel_ || fileOperationActive_)
        {
            return;
        }

        const std::vector<std::wstring> selectedPaths = SelectedFileOperationPathsSnapshot();
        if (selectedPaths.empty())
        {
            MessageBoxW(hwnd_, L"Select one or more images first.", L"Duplicate", MB_OK | MB_ICONINFORMATION);
            return;
        }

        // Duplicate = copy into the same folder with an auto numeric suffix ("name (2)").
        const std::wstring destinationFolder = browserModel_->FolderPath();
        StartFileOperation(services::FileOperationType::Copy,
                           std::move(selectedPaths),
                           destinationFolder,
                           services::FileConflictPolicy::AutoRenameNumericSuffix,
                           {});
    }

    void MainWindow::ShowImageInformationForPath(const std::wstring& filePath)
    {
        if (!browserPaneController_ || !browserModel_ || filePath.empty())
        {
            return;
        }

        const int modelIndex = browserModel_->FindItemIndexByPath(filePath);
        if (modelIndex < 0)
        {
            MessageBoxW(hwnd_, L"The image is no longer available in the current folder.", L"Image Information", MB_OK | MB_ICONINFORMATION);
            return;
        }

        if (browserPaneController_->PrimarySelectedModelIndex() != modelIndex)
        {
            browserPaneController_->RestoreSelectionByFilePaths({filePath}, filePath);
        }

        ShowImageInformation();
    }

    void MainWindow::SetSelectionRating(int rating)
    {
        if (!browserPaneController_ || !userMetadataStore_)
        {
            return;
        }

        const std::vector<std::wstring> selectedPaths = browserPaneController_->SelectedFilePathsSnapshot();
        if (selectedPaths.empty())
        {
            MessageBoxW(hwnd_, L"Select one or more images first.", L"Set Rating", MB_OK | MB_ICONINFORMATION);
            return;
        }

        userMetadataStore_->SetRating(selectedPaths, rating);
        RefreshBrowserPane();
        UpdateMenuState();
    }

    void MainWindow::EditSelectionTags()
    {
        if (!browserPaneController_ || !userMetadataStore_)
        {
            return;
        }

        const std::vector<std::wstring> selectedPaths = browserPaneController_->SelectedFilePathsSnapshot();
        if (selectedPaths.empty())
        {
            MessageBoxW(hwnd_, L"Select one or more images first.", L"Edit Tags", MB_OK | MB_ICONINFORMATION);
            return;
        }

        std::wstring initialTags;
        bool firstEntry = true;
        bool mixedTags = false;
        for (const std::wstring& path : selectedPaths)
        {
            const std::wstring& tags = userMetadataStore_->EntryForPath(path).tags;
            if (firstEntry)
            {
                initialTags = tags;
                firstEntry = false;
                continue;
            }

            if (!util::EqualsIgnoreCaseOrdinal(initialTags, tags))
            {
                mixedTags = true;
                break;
            }
        }

        std::wstring editedTags = mixedTags ? std::wstring{} : initialTags;
        if (!PromptForSingleLineText(hwnd_,
                                     instance_,
                                     appTextSize_,
                                     L"Edit Tags",
                                     L"Enter comma-separated tags for the selected images. Leave blank to clear tags.",
                                     L"Apply",
                                     editedTags,
                                     0,
                                     -1,
                                     &editedTags))
        {
            return;
        }

        userMetadataStore_->SetTags(selectedPaths, editedTags);
        RefreshBrowserPane();
        UpdateMenuState();
    }

    int MainWindow::CommonSelectionRating() const
    {
        if (!browserPaneController_ || !userMetadataStore_)
        {
            return -1;
        }

        const std::vector<std::wstring> selectedPaths = browserPaneController_->SelectedFilePathsSnapshot();
        if (selectedPaths.empty())
        {
            return -1;
        }

        int commonRating = std::clamp(userMetadataStore_->EntryForPath(selectedPaths.front()).rating, 0, 5);
        for (std::size_t index = 1; index < selectedPaths.size(); ++index)
        {
            const int candidateRating = std::clamp(userMetadataStore_->EntryForPath(selectedPaths[index]).rating, 0, 5);
            if (candidateRating != commonRating)
            {
                return -1;
            }
        }

        return commonRating;
    }

    void MainWindow::StartSlideshow(bool selectionScope)
    {
        if (!browserModel_ || !browserPaneController_)
        {
            return;
        }

        if (!selectionScope)
        {
            StartFolderSlideshow();
            return;
        }

        std::vector<browser::BrowserItem> items = CollectItemsForScope(selectionScope);
        if (items.empty())
        {
            MessageBoxW(hwnd_,
                        selectionScope ? L"Select one or more images first." : L"Open a folder with images first.",
                        L"Slideshow",
                        MB_OK | MB_ICONINFORMATION);
            return;
        }

        OpenItemsInViewer(std::move(items), 0, true, ShouldDefaultViewerToSecondaryMonitor());
    }

    void MainWindow::StartFolderSlideshow(std::wstring_view preferredPath)
    {
        if (!browserModel_ || !browserPaneController_)
        {
            return;
        }

        std::vector<browser::BrowserItem> items = CollectItemsForScope(false);
        if (items.empty())
        {
            MessageBoxW(hwnd_,
                        L"Open a folder with images first.",
                        L"Slideshow",
                        MB_OK | MB_ICONINFORMATION);
            return;
        }

        int selectedIndex = -1;
        if (!preferredPath.empty())
        {
            for (int index = 0; index < static_cast<int>(items.size()); ++index)
            {
                if (browser::FilePathsEqual(items[static_cast<std::size_t>(index)].filePath, preferredPath))
                {
                    selectedIndex = index;
                    break;
                }
            }
        }

        if (selectedIndex < 0)
        {
            const int primaryModelIndex = browserPaneController_->PrimarySelectedModelIndex();
            const auto orderedModelIndices = browserPaneController_->OrderedModelIndicesSnapshot();
            for (int index = 0; index < static_cast<int>(orderedModelIndices.size()); ++index)
            {
                if (orderedModelIndices[static_cast<std::size_t>(index)] == primaryModelIndex)
                {
                    selectedIndex = index;
                    break;
                }
            }
        }

        if (selectedIndex < 0)
        {
            selectedIndex = 0;
        }

        OpenItemsInViewer(std::move(items), selectedIndex, true, ShouldDefaultViewerToSecondaryMonitor());
    }

    void MainWindow::StartBatchConvert(bool selectionScope, services::BatchConvertFormat format)
    {
        if (!batchConvertService_)
        {
            return;
        }

        std::vector<browser::BrowserItem> items = CollectItemsForScope(selectionScope);
        if (items.empty())
        {
            MessageBoxW(hwnd_,
                        selectionScope ? L"Select one or more images first." : L"Open a folder with images first.",
                        L"Batch Convert",
                        MB_OK | MB_ICONINFORMATION);
            return;
        }

        std::wstring outputFolder;
        if (!ChooseFolder(&outputFolder) || outputFolder.empty())
        {
            return;
        }

        batchConvertOutputFolder_ = outputFolder;
        batchConvertCompleted_ = 0;
        batchConvertTotal_ = items.size();
        batchConvertFailed_ = 0;
        batchConvertCurrentFile_.clear();
        batchConvertActive_ = true;
        activeBatchConvertRequestId_ = batchConvertService_->Start(hwnd_, std::move(items), outputFolder, format);
        UpdateStatusText();
        UpdateMenuState();
    }

    void MainWindow::AdjustSelectedJpegOrientation(int quarterTurnsDelta)
    {
        if (!browserModel_ || !browserPaneController_)
        {
            return;
        }

        const std::vector<browser::BrowserItem> items = CollectItemsForScope(true);
        if (items.empty())
        {
            MessageBoxW(hwnd_, L"Select one or more JPEG images first.", L"Adjust JPEG Orientation", MB_OK | MB_ICONINFORMATION);
            return;
        }

        const std::vector<std::wstring> selectedPaths = browserPaneController_->SelectedFilePathsSnapshot();
        const std::wstring focusedPath = browserPaneController_->FocusedFilePathSnapshot();

        std::vector<std::wstring> updatedPaths;
        std::size_t successCount = 0;
        std::size_t failureCount = 0;
        std::wstring firstFailureMessage;
        for (const browser::BrowserItem& item : items)
        {
            if (!decode::IsWicFileType(item.fileType) || (_wcsicmp(item.fileType.c_str(), L"JPG") != 0 && _wcsicmp(item.fileType.c_str(), L"JPEG") != 0))
            {
                continue;
            }

            std::wstring errorMessage;
            if (services::AdjustJpegOrientation(item.filePath, quarterTurnsDelta, &errorMessage))
            {
                ++successCount;
                updatedPaths.push_back(item.filePath);
                browserModel_->UpsertItem(browser::BuildBrowserItemFromPath(fs::path(item.filePath)));
            }
            else
            {
                ++failureCount;
                if (firstFailureMessage.empty() && !errorMessage.empty())
                {
                    firstFailureMessage = std::move(errorMessage);
                }
            }
        }

        if (!updatedPaths.empty())
        {
            browserPaneController_->InvalidateMediaCacheForPaths(updatedPaths);
            RefreshBrowserPane();
            browserPaneController_->RestoreSelectionByFilePaths(selectedPaths, focusedPath);
        }

        std::wstring summary = L"Updated orientation metadata for " + std::to_wstring(successCount) + L" JPEG file(s).";
        if (failureCount > 0)
        {
            summary.append(L"\nFailed: ");
            summary.append(std::to_wstring(failureCount));
            summary.append(L".");
            if (!firstFailureMessage.empty())
            {
                summary.append(L"\nReason: ");
                summary.append(firstFailureMessage);
            }
        }
        MessageBoxW(hwnd_, summary.c_str(), L"Adjust JPEG Orientation", MB_OK | MB_ICONINFORMATION);
    }

    void MainWindow::RecordUndoableOperation(const services::FileOperationUpdate& update)
    {
        // Recycle-bin deletes are already undoable via the shell (FOF_ALLOWUNDO), and
        // permanent deletes cannot be undone; only journal Copy / Move / Rename.
        if (applyingUndoRedo_)
        {
            return;
        }

        const auto type = update.type;
        const bool isCopy = type == services::FileOperationType::Copy;
        const bool isMove = type == services::FileOperationType::Move;
        const bool isRename = type == services::FileOperationType::Rename;
        if (!isCopy && !isMove && !isRename)
        {
            return;
        }

        if (update.succeededSourcePaths.empty())
        {
            return;
        }

        UndoableOperation operation;
        operation.type = static_cast<int>(type);
        operation.sourcePaths = update.succeededSourcePaths;
        operation.createdPaths = update.createdPaths;
        operation.destinationFolder = update.destinationFolder;
        operation.description = services::FileOperationTypeToActivityLabel(type);

        // Every reversible operation needs a complete source/destination mapping.
        // Without it, an inverse could target an unrelated source path.
        if (operation.createdPaths.size() != operation.sourcePaths.size())
        {
            return;
        }

        if (isMove)
        {
            const std::wstring sourceFolder = NormalizeFolderPath(
                fs::path(operation.sourcePaths.front()).parent_path().wstring());
            if (sourceFolder.empty()
                || std::any_of(operation.sourcePaths.begin() + 1,
                               operation.sourcePaths.end(),
                               [&](const std::wstring& sourcePath)
                               {
                                   return !FolderPathsEqual(
                                       NormalizeFolderPath(fs::path(sourcePath).parent_path().wstring()),
                                       sourceFolder);
                               }))
            {
                return;
            }
        }

        undoStack_.push_back(std::move(operation));
        constexpr std::size_t kMaxUndoDepth = 32;
        while (undoStack_.size() > kMaxUndoDepth)
        {
            undoStack_.pop_front();
        }
        // A new operation invalidates the redo history.
        redoStack_.clear();
        UpdateUndoRedoMenuState();
    }

    void MainWindow::PerformUndo()
    {
        if (undoStack_.empty() || fileOperationActive_)
        {
            return;
        }

        const UndoableOperation& operation = undoStack_.back();

        const auto type = static_cast<services::FileOperationType>(operation.type);
        std::vector<std::wstring> undoSources;
        std::wstring undoDestination;
        std::vector<std::wstring> undoLeafNames;
        bool started = false;

        if (type == services::FileOperationType::Copy)
        {
            // Undo a copy = delete the created copies (recycle bin for safety).
            if (operation.createdPaths.empty())
            {
                return;
            }
            undoSources = operation.createdPaths;
            applyingUndoRedo_ = true;
            pendingUndoRedoOperation_ = UndoRedoOperation::Undo;
            started = StartFileOperation(services::FileOperationType::DeleteRecycleBin,
                                         undoSources,
                                         {},
                                         services::FileConflictPolicy::PromptShell,
                                         {});
        }
        else if (type == services::FileOperationType::Move)
        {
            // Undo a move = move the created files back to the original folder.
            // createdPaths[i] is the post-move location; move each back beside the
            // original source's parent folder.
            if (operation.createdPaths.empty())
            {
                return;
            }
            const std::wstring originalFolder = fs::path(operation.sourcePaths.front()).parent_path().wstring();
            applyingUndoRedo_ = true;
            pendingUndoRedoOperation_ = UndoRedoOperation::Undo;
            started = StartFileOperation(services::FileOperationType::Move,
                                         operation.createdPaths,
                                         originalFolder,
                                         services::FileConflictPolicy::PromptShell,
                                         {});
        }
        else // Rename
        {
            // Undo a rename = rename back to the original leaf name.
            std::vector<std::wstring> originalLeafNames;
            originalLeafNames.reserve(operation.sourcePaths.size());
            for (const std::wstring& sourcePath : operation.sourcePaths)
            {
                originalLeafNames.push_back(fs::path(sourcePath).filename().wstring());
            }
            applyingUndoRedo_ = true;
            pendingUndoRedoOperation_ = UndoRedoOperation::Undo;
            started = StartFileOperation(services::FileOperationType::Rename,
                                         operation.createdPaths,
                                         {},
                                         services::FileConflictPolicy::PromptShell,
                                         originalLeafNames);
        }

        if (!started)
        {
            applyingUndoRedo_ = false;
            pendingUndoRedoOperation_ = UndoRedoOperation::None;
        }
        UpdateUndoRedoMenuState();
    }

    void MainWindow::PerformRedo()
    {
        if (redoStack_.empty() || fileOperationActive_)
        {
            return;
        }

        const UndoableOperation& operation = redoStack_.back();

        const auto type = static_cast<services::FileOperationType>(operation.type);
        applyingUndoRedo_ = true;
        pendingUndoRedoOperation_ = UndoRedoOperation::Redo;
        bool started = false;
        if (type == services::FileOperationType::Copy)
        {
            started = StartFileOperation(services::FileOperationType::Copy,
                                         operation.sourcePaths,
                                         operation.destinationFolder,
                                         services::FileConflictPolicy::PromptShell,
                                         {});
        }
        else if (type == services::FileOperationType::Move)
        {
            started = StartFileOperation(services::FileOperationType::Move,
                                         operation.sourcePaths,
                                         operation.destinationFolder,
                                         services::FileConflictPolicy::PromptShell,
                                         {});
        }
        else // Rename: redo renames back to the new leaf names.
        {
            std::vector<std::wstring> newLeafNames;
            newLeafNames.reserve(operation.createdPaths.size());
            for (const std::wstring& createdPath : operation.createdPaths)
            {
                newLeafNames.push_back(fs::path(createdPath).filename().wstring());
            }
            started = StartFileOperation(services::FileOperationType::Rename,
                                         operation.sourcePaths,
                                         {},
                                         services::FileConflictPolicy::PromptShell,
                                         newLeafNames);
        }

        if (!started)
        {
            applyingUndoRedo_ = false;
            pendingUndoRedoOperation_ = UndoRedoOperation::None;
        }
        UpdateUndoRedoMenuState();
    }

    void MainWindow::UpdateUndoRedoMenuState()
    {
        if (!menu_)
        {
            return;
        }

        const bool canUndo = !undoStack_.empty() && !fileOperationActive_;
        const bool canRedo = !redoStack_.empty() && !fileOperationActive_;
        EnableMenuItem(menu_, ID_EDIT_UNDO, MF_BYCOMMAND | (canUndo ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_EDIT_REDO, MF_BYCOMMAND | (canRedo ? MF_ENABLED : MF_GRAYED));
    }

    void MainWindow::ApplyCompletedFileOperation(const services::FileOperationUpdate& update)
    {
        util::ScopedTimer applyTimer(L"MainWindow::ApplyCompletedFileOperation");
        const UndoRedoOperation completedUndoRedoOperation = pendingUndoRedoOperation_;
        fileOperationActive_ = false;
        activeFileOperationLabel_.clear();
        applyingUndoRedo_ = false;

        RecordUndoableOperation(update);

        const HWND activationRestoreWindow = foregroundWindowAtFileOperationStart_;
        foregroundWindowAtFileOperationStart_ = nullptr;
        const HWND focusRestoreWindow = focusWindowAtFileOperationStart_;
        focusWindowAtFileOperationStart_ = nullptr;
        bool viewerCloseRequested = false;

        const std::wstring viewerDeleteSourcePath = pendingViewerDeleteSourcePath_;
        const std::vector<std::wstring> viewerDeleteSourcePaths = pendingViewerDeleteSourcePaths_;
        const std::wstring viewerDeletePreferredFocusPath = pendingViewerDeletePreferredFocusPath_;
        pendingViewerDeleteSourcePath_.clear();
        pendingViewerDeleteSourcePaths_.clear();
        pendingViewerDeletePreferredFocusPath_.clear();

        const PendingViewerQuickSend viewerQuickSend = pendingViewerQuickSend_;
        pendingViewerQuickSend_ = {};
        const bool viewerQuickSendOperation = viewerQuickSend.active
            && viewerQuickSend.type == update.type;

        // Only deletes that originated in the viewer get the "do not re-enumerate"
        // treatment below; browser-initiated deletes must still update the model.
        const bool viewerDeleteOperation = !viewerDeleteSourcePath.empty()
            && (update.type == services::FileOperationType::DeleteRecycleBin
                || update.type == services::FileOperationType::DeletePermanent);
        const bool browserItemDeleteOperation = !viewerDeleteOperation
            && (update.type == services::FileOperationType::DeleteRecycleBin
                || update.type == services::FileOperationType::DeletePermanent);

        const std::wstring deferredFolderWatchReloadPath = pendingFolderWatchReloadPath_;
        const bool deferredFolderWatchTreeRefresh = pendingFolderWatchTreeRefresh_;
        pendingFolderWatchReloadPath_.clear();
        pendingFolderWatchTreeRefresh_ = false;

        const std::wstring treeFolderOperationPath = activeTreeFolderOperationPath_;
        activeTreeFolderOperationPath_.clear();
        const std::wstring treeFolderRenamePath = activeTreeFolderRenamePath_;
        activeTreeFolderRenamePath_.clear();
        const std::wstring treeFolderMoveSourcePath = activeTreeFolderMoveSourcePath_;
        activeTreeFolderMoveSourcePath_.clear();
        const std::wstring treeFolderMoveDestinationFolder = activeTreeFolderMoveDestinationFolder_;
        activeTreeFolderMoveDestinationFolder_.clear();
        const bool treeFolderDeleteOperation = !treeFolderOperationPath.empty()
            && (update.type == services::FileOperationType::DeleteRecycleBin
                || update.type == services::FileOperationType::DeletePermanent);
        const bool treeFolderMoveOperation = !treeFolderMoveSourcePath.empty()
            && update.type == services::FileOperationType::Move;
        const bool treeFolderDeleteSucceeded = treeFolderDeleteOperation
            && std::any_of(update.succeededSourcePaths.begin(), update.succeededSourcePaths.end(), [&](const std::wstring& sourcePath)
            {
                return FolderPathsEqual(sourcePath, treeFolderOperationPath);
            });

        std::wstring treeFolderMoveCreatedPath;
        if (treeFolderMoveOperation)
        {
            const std::size_t movePairCount = std::min(update.succeededSourcePaths.size(), update.createdPaths.size());
            for (std::size_t index = 0; index < movePairCount; ++index)
            {
                if (!FolderPathsEqual(update.succeededSourcePaths[index], treeFolderMoveSourcePath))
                {
                    continue;
                }

                treeFolderMoveCreatedPath = NormalizeFolderPath(update.createdPaths[index]);
                break;
            }

            if (treeFolderMoveCreatedPath.empty())
            {
                const bool sourcePathReported = std::any_of(update.succeededSourcePaths.begin(), update.succeededSourcePaths.end(), [&](const std::wstring& sourcePath)
                {
                    return FolderPathsEqual(sourcePath, treeFolderMoveSourcePath);
                });
                if (sourcePathReported && !treeFolderMoveDestinationFolder.empty())
                {
                    treeFolderMoveCreatedPath = NormalizeFolderPath(
                        (fs::path(treeFolderMoveDestinationFolder) / fs::path(treeFolderMoveSourcePath).filename()).wstring());
                }
            }
        }
        const bool treeFolderMoveSucceeded = treeFolderMoveOperation && !treeFolderMoveCreatedPath.empty();

        bool refreshFolderTree = treeFolderDeleteSucceeded || treeFolderMoveSucceeded;
        std::wstring fallbackFolderPath;
        if (treeFolderDeleteSucceeded
            && browserModel_
            && !browserModel_->FolderPath().empty()
            && browser::PathHasPrefix(browserModel_->FolderPath(), treeFolderOperationPath))
        {
            fallbackFolderPath = FindExistingFolderAncestor(fs::path(treeFolderOperationPath).parent_path());
        }

        std::wstring treeFolderReloadPath;
        if (!treeFolderRenamePath.empty() && update.type == services::FileOperationType::Rename)
        {
            const std::size_t renamePairCount = std::min(update.succeededSourcePaths.size(), update.createdPaths.size());
            for (std::size_t index = 0; index < renamePairCount; ++index)
            {
                if (!FolderPathsEqual(update.succeededSourcePaths[index], treeFolderRenamePath))
                {
                    continue;
                }

                refreshFolderTree = true;
                if (browserModel_ && !browserModel_->FolderPath().empty())
                {
                    if (browser::PathHasPrefix(browserModel_->FolderPath(), treeFolderRenamePath))
                    {
                        treeFolderReloadPath = RewritePathPrefix(
                            browserModel_->FolderPath(),
                            treeFolderRenamePath,
                            update.createdPaths[index]);
                    }
                    else if (browserModel_->IsRecursive()
                        && browser::PathHasPrefix(treeFolderRenamePath, browserModel_->FolderPath()))
                    {
                        treeFolderReloadPath = browserModel_->FolderPath();
                    }
                }
                break;
            }
        }

        if (treeFolderMoveSucceeded && browserModel_ && !browserModel_->FolderPath().empty())
        {
            if (browser::PathHasPrefix(browserModel_->FolderPath(), treeFolderMoveSourcePath))
            {
                treeFolderReloadPath = RewritePathPrefix(
                    browserModel_->FolderPath(),
                    treeFolderMoveSourcePath,
                    treeFolderMoveCreatedPath);
            }
        }

        if (userMetadataStore_)
        {
            userMetadataStore_->ApplyFileOperationUpdate(update.type, update.succeededSourcePaths, update.createdPaths);
        }

        bool reloadCurrentFolder = browserModel_
            && !browserModel_->FolderPath().empty()
            && !deferredFolderWatchReloadPath.empty()
            && FolderPathsEqual(browserModel_->FolderPath(), deferredFolderWatchReloadPath);

        if (!treeFolderReloadPath.empty())
        {
            reloadCurrentFolder = true;
        }
        else if (treeFolderMoveSucceeded && browserModel_ && !browserModel_->FolderPath().empty())
        {
            reloadCurrentFolder = true;
        }

        if (!reloadCurrentFolder && !browserItemDeleteOperation && browserModel_ && !browserModel_->FolderPath().empty())
        {
            const std::size_t affectedCount = update.succeededSourcePaths.size() + update.createdPaths.size();
            if (affectedCount >= kIncrementalFileOperationPathLimit)
            {
                const auto pathAffectsCurrentScope = [&](const std::wstring& path)
                {
                    return IsPathInCurrentScope(path)
                        || browser::PathHasPrefix(browserModel_->FolderPath(), path);
                };

                reloadCurrentFolder = std::any_of(
                    update.createdPaths.begin(),
                    update.createdPaths.end(),
                    pathAffectsCurrentScope)
                    || std::any_of(
                        update.succeededSourcePaths.begin(),
                        update.succeededSourcePaths.end(),
                        pathAffectsCurrentScope);
            }
        }

        if (viewerDeleteOperation)
        {
            // Viewer deletes are applied incrementally below and the viewer has
            // already advanced locally; a full folder re-enumeration would blank
            // the viewer and re-decode the visible image for no benefit.
            reloadCurrentFolder = false;
        }
        else if (browserItemDeleteOperation && !treeFolderDeleteOperation)
        {
            reloadCurrentFolder = false;
        }

        bool modelChanged = false;
        if (!reloadCurrentFolder && browserModel_ && browserPaneController_)
        {
            std::vector<std::wstring> selectedPaths = browserPaneController_->SelectedFilePathsSnapshot();
            std::wstring focusedPath = browserPaneController_->FocusedFilePathSnapshot();

            // Deleting the whole selection leaves nothing selected, which drops
            // keyboard focus entirely. Remember the nearest surviving neighbour so
            // focus can land there once the removed items are gone.
            std::wstring deleteFallbackFocusPath;
            if (!viewerDeleteOperation
                && (update.type == services::FileOperationType::DeleteRecycleBin
                    || update.type == services::FileOperationType::DeletePermanent))
            {
                const auto& itemsBeforeDelete = browserModel_->Items();
                const std::vector<int> orderedModelIndices = browserPaneController_->OrderedModelIndicesSnapshot();
                std::unordered_set<std::wstring> deletedPaths;
                deletedPaths.reserve(update.succeededSourcePaths.size());
                for (const std::wstring& deletedPath : update.succeededSourcePaths)
                {
                    deletedPaths.insert(util::NormalizePathForComparison(deletedPath));
                }
                const auto pathAtOrdinal = [&](int ordinal) -> const std::wstring*
                {
                    if (ordinal < 0 || ordinal >= static_cast<int>(orderedModelIndices.size()))
                    {
                        return nullptr;
                    }

                    const int modelIndex = orderedModelIndices[static_cast<std::size_t>(ordinal)];
                    if (modelIndex < 0 || modelIndex >= static_cast<int>(itemsBeforeDelete.size()))
                    {
                        return nullptr;
                    }

                    return &itemsBeforeDelete[static_cast<std::size_t>(modelIndex)].filePath;
                };
                const auto isDeletedPath = [&](const std::wstring& path)
                {
                    return deletedPaths.contains(util::NormalizePathForComparison(path));
                };

                const int ordinalCount = static_cast<int>(orderedModelIndices.size());
                int lastDeletedOrdinal = -1;
                for (int ordinal = 0; ordinal < ordinalCount; ++ordinal)
                {
                    const std::wstring* path = pathAtOrdinal(ordinal);
                    if (path && isDeletedPath(*path))
                    {
                        lastDeletedOrdinal = ordinal;
                    }
                }

                for (int ordinal = lastDeletedOrdinal + 1; lastDeletedOrdinal >= 0 && ordinal < ordinalCount; ++ordinal)
                {
                    const std::wstring* path = pathAtOrdinal(ordinal);
                    if (path && !isDeletedPath(*path))
                    {
                        deleteFallbackFocusPath = *path;
                        break;
                    }
                }

                for (int ordinal = lastDeletedOrdinal - 1; deleteFallbackFocusPath.empty() && ordinal >= 0; --ordinal)
                {
                    const std::wstring* path = pathAtOrdinal(ordinal);
                    if (path && !isDeletedPath(*path))
                    {
                        deleteFallbackFocusPath = *path;
                        break;
                    }
                }
            }

            if (update.type == services::FileOperationType::Rename)
            {
                const std::size_t renamePairCount = std::min(update.succeededSourcePaths.size(), update.createdPaths.size());
                for (std::size_t index = 0; index < renamePairCount; ++index)
                {
                    const std::wstring& sourcePath = update.succeededSourcePaths[index];
                    const std::wstring& createdPath = update.createdPaths[index];
                    for (std::wstring& selectedPath : selectedPaths)
                    {
                        if (browser::FilePathsEqual(selectedPath, sourcePath))
                        {
                            selectedPath = createdPath;
                        }
                    }

                    if (browser::FilePathsEqual(focusedPath, sourcePath))
                    {
                        focusedPath = createdPath;
                    }
                }
            }

            auto upsertVisiblePath = [&](const std::wstring& path)
            {
                if (!IsPathInCurrentScope(path))
                {
                    return;
                }

                const fs::path filePath(path);
                std::error_code error;
                if (fs::is_directory(filePath, error) && !error)
                {
                    if (!showSubfoldersInBrowser_
                        || !FolderPathsEqual(filePath.parent_path().wstring(), browserModel_->FolderPath()))
                    {
                        return;
                    }

                    modelChanged = browserModel_->UpsertItem(browser::BuildBrowserItemFromPath(filePath)) || modelChanged;
                    return;
                }

                if (!browser::IsSupportedImageExtension(filePath.extension().wstring()))
                {
                    return;
                }

                if (!fs::is_regular_file(filePath, error) || error)
                {
                    return;
                }

                modelChanged = browserModel_->UpsertItem(browser::BuildBrowserItemFromPath(filePath)) || modelChanged;
            };

            switch (update.type)
            {
            case services::FileOperationType::Copy:
                for (const std::wstring& createdPath : update.createdPaths)
                {
                    upsertVisiblePath(createdPath);
                }
                break;
            case services::FileOperationType::Move:
                for (const std::wstring& sourcePath : update.succeededSourcePaths)
                {
                    const bool removeByPrefix = treeFolderMoveOperation && FolderPathsEqual(sourcePath, treeFolderMoveSourcePath);
                    modelChanged = (removeByPrefix
                        ? browserModel_->RemoveItemsByPathPrefix(sourcePath)
                        : browserModel_->RemoveItemByPath(sourcePath)) || modelChanged;
                }
                for (const std::wstring& createdPath : update.createdPaths)
                {
                    upsertVisiblePath(createdPath);
                }
                break;
            case services::FileOperationType::DeleteRecycleBin:
            case services::FileOperationType::DeletePermanent:
                if (treeFolderDeleteOperation)
                {
                    for (const std::wstring& sourcePath : update.succeededSourcePaths)
                    {
                        modelChanged = (FolderPathsEqual(sourcePath, treeFolderOperationPath)
                            ? browserModel_->RemoveItemsByPathPrefix(sourcePath)
                            : browserModel_->RemoveItemByPath(sourcePath)) || modelChanged;
                    }
                }
                else
                {
                    modelChanged = browserModel_->RemoveItemsByPath(update.succeededSourcePaths) || modelChanged;
                }
                break;
            case services::FileOperationType::Rename:
            {
                const std::size_t renamePairCount = std::min(update.succeededSourcePaths.size(), update.createdPaths.size());
                for (std::size_t index = 0; index < renamePairCount; ++index)
                {
                    const std::wstring& sourcePath = update.succeededSourcePaths[index];
                    const std::wstring& createdPath = update.createdPaths[index];
                    if (!treeFolderRenamePath.empty() && FolderPathsEqual(sourcePath, treeFolderRenamePath))
                    {
                        continue;
                    }

                    modelChanged = browserModel_->RemoveItemByPath(sourcePath) || modelChanged;
                    upsertVisiblePath(createdPath);
                }
                break;
            }
            default:
                break;
            }

            std::vector<std::wstring> affectedPaths = update.succeededSourcePaths;
            affectedPaths.insert(affectedPaths.end(), update.createdPaths.begin(), update.createdPaths.end());
            if (!affectedPaths.empty())
            {
                util::ScopedTimer invalidateTimer(L"ApplyCompletedFileOperation InvalidateMediaCacheForPaths");
                browserPaneController_->InvalidateMediaCacheForPaths(affectedPaths);
            }

            if (modelChanged && fallbackFolderPath.empty())
            {
                util::ScopedTimer refreshTimer(L"ApplyCompletedFileOperation RefreshBrowserPane");
                RefreshBrowserPane();
                browserPaneController_->RestoreSelectionByFilePaths(selectedPaths, focusedPath);
                if (!deleteFallbackFocusPath.empty() && browserPaneController_->SelectedCount() == 0)
                {
                    browserPaneController_->RestoreSelectionByFilePaths({deleteFallbackFocusPath}, deleteFallbackFocusPath);
                    browserPaneController_->EnsureFocusedItemVisible();
                }
                UpdateWindowTitle();

                // Keep an open viewer consistent with the browser model after a
                // browser-initiated operation. Viewer deletes are excluded: the
                // viewer already advanced locally and re-syncing it here would
                // blank and re-decode the image that is already on screen.
                if (!viewerDeleteOperation
                    && !viewerQuickSendOperation
                    && viewerWindow_
                    && viewerWindow_->IsOpen())
                {
                    SyncViewerToBrowserModel(viewerWindow_->CurrentFilePath());
                }
            }
        }

        if ((update.type == services::FileOperationType::Copy || update.type == services::FileOperationType::Move)
            && !update.destinationFolder.empty())
        {
            std::error_code destinationError;
            if (fs::is_directory(fs::path(update.destinationFolder), destinationError) && !destinationError)
            {
                if (!update.succeededSourcePaths.empty() || !update.createdPaths.empty())
                {
                    RecordRecentDestination(update.destinationFolder);
                }
                InsertFolderTreeFolderIfParentLoaded(update.destinationFolder);
            }
        }

        if (viewerDeleteOperation)
        {
            const auto deletePathSucceeded = [&](const std::wstring& sourcePath)
            {
                const bool deleteSucceeded = std::any_of(
                    update.succeededSourcePaths.begin(),
                    update.succeededSourcePaths.end(),
                    [&](const std::wstring& succeededPath)
                    {
                        return browser::FilePathsEqual(succeededPath, sourcePath);
                    });
                if (deleteSucceeded)
                {
                    return true;
                }

                std::error_code error;
                const bool exists = fs::exists(fs::path(sourcePath), error);
                return !update.aborted && !exists && !error;
            };
            const bool viewerDeleteSucceeded = deletePathSucceeded(viewerDeleteSourcePath);

            bool browserModelChanged = false;
            if (browserModel_)
            {
                const std::vector<std::wstring>& deletedPaths = viewerDeleteSourcePaths.empty()
                    ? std::vector<std::wstring>{viewerDeleteSourcePath}
                    : viewerDeleteSourcePaths;
                for (const std::wstring& deletedPath : deletedPaths)
                {
                    if (deletedPath.empty() || !deletePathSucceeded(deletedPath))
                    {
                        continue;
                    }

                    browserModelChanged = browserModel_->RemoveItemByPath(deletedPath) || browserModelChanged;
                }
            }

            if (browserModelChanged)
            {
                // The incremental update above already refreshed the browser pane
                // for anything it removed; this only covers items it could not see
                // (e.g. no browser pane controller), so just refresh the title.
                UpdateWindowTitle();
            }

            if (viewerWindow_ && viewerWindow_->IsOpen())
            {
                if (viewerDeleteSucceeded && viewerDeletePreferredFocusPath.empty())
                {
                    const HWND viewerHwnd = viewerWindow_->Hwnd();
                    if (viewerHwnd && IsWindow(viewerHwnd) != FALSE)
                    {
                        viewerCloseRequested = true;
                        PostMessageW(viewerHwnd, WM_CLOSE, 0, 0);
                    }
                }
                else if (!viewerDeleteSucceeded)
                {
                    // The delete failed (or the file no longer exists for another
                    // reason); resync the viewer to the actual model state since
                    // AdvanceAfterDeleteCurrent optimistically advanced past an item
                    // that, in fact, was never removed.
                    SyncViewerToBrowserModel(viewerDeleteSourcePath);
                }
                // else: the delete succeeded and the viewer already advanced past the
                // deleted item locally (see AdvanceAfterDeleteCurrent, called at
                // keypress time). Calling SyncViewerToBrowserModel here would rebuild
                // the viewer's item list and force ReplaceItems() to blank the display
                // and fully re-decode the already-visible image — visible as a
                // delayed flash every time a delete completes. Skip it.
            }
            else if (!viewerDeleteSucceeded && browserModel_)
            {
                const int modelIndex = browserModel_->FindItemIndexByPath(viewerDeleteSourcePath);
                if (modelIndex >= 0)
                {
                    OpenItemInViewer(modelIndex);
                }
            }
        }

        if (viewerQuickSendOperation && viewerQuickSend.type == services::FileOperationType::Move)
        {
            const bool primaryPathSucceeded = std::any_of(
                update.succeededSourcePaths.begin(),
                update.succeededSourcePaths.end(),
                [&](const std::wstring& succeededPath)
                {
                    return browser::FilePathsEqual(succeededPath, viewerQuickSend.sourcePath);
                });
            const bool primaryPathMissingAfterOperation = [&]()
            {
                if (update.aborted || primaryPathSucceeded)
                {
                    return false;
                }

                std::error_code error;
                return !fs::exists(fs::path(viewerQuickSend.sourcePath), error) && !error;
            }();
            const bool viewerQuickSendSucceeded = primaryPathSucceeded || primaryPathMissingAfterOperation;

            if (!viewerQuickSendSucceeded)
            {
                if (viewerWindow_ && viewerWindow_->IsOpen())
                {
                    SyncViewerToBrowserModel(viewerQuickSend.sourcePath);
                }
                else if (browserModel_)
                {
                    const int sourceModelIndex = browserModel_->FindItemIndexByPath(viewerQuickSend.sourcePath);
                    if (sourceModelIndex >= 0)
                    {
                        OpenItemInViewer(sourceModelIndex);
                    }
                }
            }
        }

        if (refreshFolderTree || deferredFolderWatchTreeRefresh)
        {
            RefreshFolderTree();
        }

        if (!fallbackFolderPath.empty())
        {
            LoadFolderAsync(fallbackFolderPath);
        }
        else if (!treeFolderReloadPath.empty())
        {
            LoadFolderAsync(treeFolderReloadPath);
        }
        else if (reloadCurrentFolder && browserModel_ && !browserModel_->FolderPath().empty())
        {
            LoadFolderAsync(browserModel_->FolderPath());
        }

        if (completedUndoRedoOperation != UndoRedoOperation::None)
        {
            const bool undoRedoSucceeded = update.finished
                && !update.aborted
                && update.failedCount == 0
                && update.succeededSourcePaths.size() == update.requestedCount;
            if (undoRedoSucceeded)
            {
                if (completedUndoRedoOperation == UndoRedoOperation::Undo && !undoStack_.empty())
                {
                    redoStack_.push_back(std::move(undoStack_.back()));
                    undoStack_.pop_back();
                }
                else if (completedUndoRedoOperation == UndoRedoOperation::Redo && !redoStack_.empty())
                {
                    undoStack_.push_back(std::move(redoStack_.back()));
                    redoStack_.pop_back();
                }
            }

            pendingUndoRedoOperation_ = UndoRedoOperation::None;
            applyingUndoRedo_ = false;
        }

        UpdateStatusText();
        UpdateMenuState();

        if (!update.message.empty() && update.failedCount > 0)
        {
            MessageBoxW(hwnd_, update.message.c_str(), L"File Operation", MB_OK | MB_ICONWARNING);
        }

        // Hand activation back to whichever of our windows the user was working in when
        // the operation began. Without this a viewer-initiated delete leaves the main
        // window active and the viewer stops responding to the keyboard until clicked.
        if (activationRestoreWindow
            && !viewerCloseRequested
            && activationRestoreWindow != hwnd_
            && IsWindow(activationRestoreWindow) != FALSE
            && IsWindowVisible(activationRestoreWindow) != FALSE
            && viewerWindow_
            && viewerWindow_->IsOpen()
            && viewerWindow_->Hwnd() == activationRestoreWindow
            && GetForegroundWindow() != activationRestoreWindow)
        {
            util::LogInfo(L"ApplyCompletedFileOperation restoring viewer activation");
            SetForegroundWindow(activationRestoreWindow);
            SetFocus(activationRestoreWindow);
        }

        if (!viewerCloseRequested
            && focusRestoreWindow
            && focusRestoreWindow != activationRestoreWindow
            && IsWindow(focusRestoreWindow) != FALSE
            && IsWindowVisible(focusRestoreWindow) != FALSE
            && GetForegroundWindow() == hwnd_
            && (GetFocus() == hwnd_ || !GetFocus()))
        {
            SetFocus(focusRestoreWindow);
        }

        // Dispatch the next queued viewer delete, if any, now that the file
        // operation slot is free.  Use the viewer's current path as the sync
        // target so we never navigate the viewer back to an image it has already
        // advanced past.
        if (closePending_)
        {
            pendingViewerDeletes_.clear();
            PostMessageW(hwnd_, WM_CLOSE, 0, 0);
        }
        else if (!pendingViewerDeletes_.empty())
        {
            PendingViewerDelete next = std::move(pendingViewerDeletes_.front());
            pendingViewerDeletes_.pop_front();
            pendingViewerDeleteSourcePath_ = next.sourcePath;
            pendingViewerDeleteSourcePaths_ = next.sourcePaths;
            pendingViewerDeletePreferredFocusPath_ = viewerWindow_ && viewerWindow_->IsOpen()
                ? viewerWindow_->CurrentFilePath()
                : next.preferredFocusPath;
            StartFileOperation(
                next.permanent ? services::FileOperationType::DeletePermanent
                               : services::FileOperationType::DeleteRecycleBin,
                std::move(next.sourcePaths),
                {},
                services::FileConflictPolicy::PromptShell,
                {});
        }
    }

    bool MainWindow::IsPathInCurrentScope(std::wstring_view path) const
    {
        if (!browserModel_ || browserModel_->FolderPath().empty() || path.empty())
        {
            return false;
        }

        if (browserModel_->IsRecursive())
        {
            return browser::PathHasPrefix(path, browserModel_->FolderPath());
        }

        return FolderPathsEqual(fs::path(path).parent_path().wstring(), browserModel_->FolderPath());
    }

    void MainWindow::ApplyFolderWatchChanges(const services::FolderWatchUpdate& update)
    {
        if (!browserModel_ || !browserPaneController_)
        {
            return;
        }

        const auto reloadFolderPreservingSelection = [&](std::wstring folderPath)
        {
            const std::vector<std::wstring> selectedPaths = browserPaneController_->SelectedFilePathsSnapshot();
            const std::wstring focusedPath = browserPaneController_->FocusedFilePathSnapshot();
            LoadFolderAsync(std::move(folderPath));
            pendingFolderReloadSelectionPaths_ = selectedPaths;
            pendingFolderReloadFocusedPath_ = focusedPath;
        };

        if (fileOperationActive_)
        {
            // Watch notifications raised while our own file operation is running are
            // normally just echoes of that operation, and ApplyCompletedFileOperation
            // applies those changes incrementally from the operation result. Deferring
            // a full folder reload for them would discard the selection and re-enumerate
            // the folder for nothing, so only escalate when the burst is too large (or
            // too ambiguous) to reconstruct from the operation result.
            if (update.requiresFullReload || update.events.size() >= kIncrementalFolderWatchEventLimit)
            {
                pendingFolderWatchReloadPath_ = update.folderPath.empty() ? browserModel_->FolderPath() : update.folderPath;
            }

            const auto treeRefreshNeededForPath = [&](const std::wstring& path)
            {
                return !path.empty() && FindFolderTreeItemByPath(path);
            };
            const auto isExistingDirectory = [](const std::wstring& path)
            {
                if (path.empty())
                {
                    return false;
                }

                std::error_code error;
                return fs::is_directory(fs::path(path), error) && !error;
            };

            pendingFolderWatchTreeRefresh_ = pendingFolderWatchTreeRefresh_
                || update.requiresFullReload
                || std::any_of(update.events.begin(), update.events.end(), [&](const services::FolderWatchEvent& event)
                {
                    switch (event.kind)
                    {
                    case services::FolderWatchEventKind::Added:
                        return isExistingDirectory(event.path);
                    case services::FolderWatchEventKind::Removed:
                        return treeRefreshNeededForPath(event.path);
                    case services::FolderWatchEventKind::Renamed:
                        return treeRefreshNeededForPath(event.oldPath)
                            || isExistingDirectory(event.path);
                    case services::FolderWatchEventKind::Modified:
                    default:
                        return false;
                    }
                });
            return;
        }

        if (update.requiresFullReload)
        {
            RefreshFolderTree();
            reloadFolderPreservingSelection(update.folderPath.empty() ? browserModel_->FolderPath() : update.folderPath);
            return;
        }

        const std::vector<std::wstring> selectedPaths = browserPaneController_->SelectedFilePathsSnapshot();
        const std::wstring focusedPath = browserPaneController_->FocusedFilePathSnapshot();
        std::vector<std::wstring> invalidatedPaths;
        std::vector<std::wstring> foldersToInsertIntoTree;
        bool changed = false;
        bool refreshFolderTree = false;
        bool preferAsyncReload = update.events.size() >= kIncrementalFolderWatchEventLimit;

        auto isExistingDirectory = [](const std::wstring& path)
        {
            if (path.empty())
            {
                return false;
            }

            std::error_code error;
            return fs::is_directory(fs::path(path), error) && !error;
        };

        auto upsertFromPath = [&](const std::wstring& path)
        {
            std::error_code error;
            const fs::path watchedPath(path);
            if (fs::is_regular_file(watchedPath, error) && !error)
            {
                if (browser::IsSupportedImageExtension(watchedPath.extension().wstring()))
                {
                    changed = browserModel_->UpsertItem(browser::BuildBrowserItemFromPath(watchedPath)) || changed;
                    invalidatedPaths.push_back(path);
                }
                return;
            }

            if (!fs::is_directory(watchedPath, error) || error)
            {
                return;
            }

            if (showSubfoldersInBrowser_
                && FolderPathsEqual(watchedPath.parent_path().wstring(), browserModel_->FolderPath()))
            {
                changed = browserModel_->UpsertItem(browser::BuildBrowserItemFromPath(watchedPath)) || changed;
            }

            if (recursiveBrowsingEnabled_)
            {
                preferAsyncReload = true;
            }
        };

        for (const services::FolderWatchEvent& event : update.events)
        {
            if (event.kind == services::FolderWatchEventKind::Added && isExistingDirectory(event.path))
            {
                foldersToInsertIntoTree.push_back(event.path);
                if (recursiveBrowsingEnabled_)
                {
                    preferAsyncReload = true;
                }
            }

            if (event.kind == services::FolderWatchEventKind::Removed
                && FindFolderTreeItemByPath(event.path))
            {
                refreshFolderTree = true;
            }

            if (event.kind == services::FolderWatchEventKind::Renamed)
            {
                if (!event.oldPath.empty() && FindFolderTreeItemByPath(event.oldPath))
                {
                    refreshFolderTree = true;
                }

                if (isExistingDirectory(event.path))
                {
                    foldersToInsertIntoTree.push_back(event.path);
                    if (recursiveBrowsingEnabled_)
                    {
                        preferAsyncReload = true;
                    }
                }
            }

            switch (event.kind)
            {
            case services::FolderWatchEventKind::Added:
            case services::FolderWatchEventKind::Modified:
                upsertFromPath(event.path);
                break;
            case services::FolderWatchEventKind::Removed:
                changed = browserModel_->RemoveItemByPath(event.path) || changed;
                changed = browserModel_->RemoveItemsByPathPrefix(event.path) || changed;
                invalidatedPaths.push_back(event.path);
                break;
            case services::FolderWatchEventKind::Renamed:
            {
                const bool renamed = browserModel_->ReplacePathPrefix(event.oldPath, event.path);
                changed = renamed || changed;
                invalidatedPaths.push_back(event.oldPath);
                invalidatedPaths.push_back(event.path);
                if (!renamed)
                {
                    changed = browserModel_->RemoveItemByPath(event.oldPath) || changed;
                    changed = browserModel_->RemoveItemsByPathPrefix(event.oldPath) || changed;
                    upsertFromPath(event.path);
                }
                break;
            }
            default:
                break;
            }
        }

        if (preferAsyncReload)
        {
            if (refreshFolderTree)
            {
                RefreshFolderTree();
            }
            else
            {
                for (const std::wstring& folderPath : foldersToInsertIntoTree)
                {
                    InsertFolderTreeFolderIfParentLoaded(folderPath);
                }
            }

            reloadFolderPreservingSelection(browserModel_->FolderPath());
            return;
        }

        if (!changed && invalidatedPaths.empty())
        {
            if (refreshFolderTree)
            {
                RefreshFolderTree();
            }
            else
            {
                for (const std::wstring& folderPath : foldersToInsertIntoTree)
                {
                    InsertFolderTreeFolderIfParentLoaded(folderPath);
                }
            }
            return;
        }

        browserPaneController_->InvalidateMediaCacheForPaths(invalidatedPaths);
        RefreshBrowserPane();
        browserPaneController_->RestoreSelectionByFilePaths(selectedPaths, focusedPath);
        if (refreshFolderTree)
        {
            RefreshFolderTree();
        }
        else
        {
            for (const std::wstring& folderPath : foldersToInsertIntoTree)
            {
                InsertFolderTreeFolderIfParentLoaded(folderPath);
            }
        }
        UpdateStatusText();
        UpdateWindowTitle();
    }

    void MainWindow::UpdateMenuState()
    {
        if (!menu_)
        {
            return;
        }

        if (menuLoopActive_)
        {
            menuStateRefreshPending_ = true;
            return;
        }

        RefreshQuickAccessMenus();
        UpdateUndoRedoMenuState();

        const bool hasFolder = browserModel_ && !browserModel_->FolderPath().empty();
        const bool hasSelection = browserPaneController_ && browserPaneController_->SelectedCount() > 0;
        const bool hasSingleSelection = browserPaneController_ && browserPaneController_->SelectedCount() == 1;
        const bool hasBatchRenameSelection = browserPaneController_ && browserPaneController_->SelectedCount() > 1;
        const bool hasCompareSelection = browserPaneController_ && browserPaneController_->SelectedCount() == 2;
        const bool hasSelectedJpeg = HasSelectedJpegItems();
        const bool hasSecondaryMonitor = FindAlternateMonitorForWindow(hwnd_) != nullptr;
        const bool allowMetadataEdit = hasSelection && !fileOperationActive_;
        const int commonSelectionRating = hasSelection ? CommonSelectionRating() : -1;
        const browser::ThumbnailSizePreset thumbnailSizePreset = browserPaneController_
            ? browserPaneController_->GetThumbnailSizePreset()
            : thumbnailSizePreset_;
        const bool thumbnailDetailsVisible = browserPaneController_
            ? browserPaneController_->AreThumbnailDetailsVisible()
            : thumbnailDetailsVisible_;
        const bool compactThumbnailLayout = browserPaneController_
            ? browserPaneController_->IsCompactThumbnailLayoutEnabled()
            : compactThumbnailLayout_;
        const bool historyNavigationSettled = !folderEnumerationActive_
            && pendingFolderHistoryNavigation_ == FolderHistoryNavigationDirection::None;
        const bool canNavigateBack = hasFolder && historyNavigationSettled
            && openedFolderHistoryIndex_ != kInvalidHistoryIndex
            && openedFolderHistoryIndex_ > 0;
        const bool canNavigateForward = hasFolder && historyNavigationSettled
            && openedFolderHistoryIndex_ != kInvalidHistoryIndex
            && openedFolderHistoryIndex_ + 1 < openedFolderHistory_.size();
        const bool thumbnailSteppingEnabled = hasFolder && browserMode_ == BrowserMode::Thumbnails
            && !folderEnumerationActive_;

        EnableMenuItem(menu_, ID_FILE_OPEN_SELECTED, MF_BYCOMMAND | (hasSelection ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_COMPARE_SELECTED, MF_BYCOMMAND | (hasCompareSelection ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_VIEW_ON_SECONDARY_MONITOR,
                   MF_BYCOMMAND | ((hasSelection && hasSecondaryMonitor) ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_IMAGE_INFORMATION, MF_BYCOMMAND | (hasSelection ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_REVEAL_IN_EXPLORER, MF_BYCOMMAND | (hasSelection ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_OPEN_CONTAINING_FOLDER, MF_BYCOMMAND | (hasSelection ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_COPY_PATH, MF_BYCOMMAND | (hasSelection ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_COPY_FILES_TO_CLIPBOARD, MF_BYCOMMAND | (hasSelection ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_EDIT_CUT, MF_BYCOMMAND | (hasSelection && !fileOperationActive_ ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_COPY_IMAGE_PIXELS, MF_BYCOMMAND | (hasSingleSelection ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_PASTE_FILES, MF_BYCOMMAND | (hasFolder && !fileOperationActive_ ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_DUPLICATE_SELECTION, MF_BYCOMMAND | (hasSelection && !fileOperationActive_ ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_SELECT_ALL, MF_BYCOMMAND | (hasFolder ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_RENAME_SELECTED, MF_BYCOMMAND | (hasSingleSelection && !fileOperationActive_ ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_BATCH_RENAME_SELECTION, MF_BYCOMMAND | (hasBatchRenameSelection && !fileOperationActive_ ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_PROPERTIES, MF_BYCOMMAND | (hasSelection ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_EDIT_TAGS, MF_BYCOMMAND | (allowMetadataEdit ? MF_ENABLED : MF_GRAYED));
        for (UINT ratingCommandId = ID_FILE_SET_RATING_0; ratingCommandId <= ID_FILE_SET_RATING_5; ++ratingCommandId)
        {
            EnableMenuItem(menu_, ratingCommandId, MF_BYCOMMAND | (allowMetadataEdit ? MF_ENABLED : MF_GRAYED));
            CheckMenuItem(menu_,
                          ratingCommandId,
                          MF_BYCOMMAND | (commonSelectionRating >= 0 && ratingCommandId == CommandIdFromRating(commonSelectionRating)
                              ? MF_CHECKED
                              : MF_UNCHECKED));
        }
        EnableMenuItem(menu_, ID_FILE_TOGGLE_CURRENT_FOLDER_FAVORITE_DESTINATION, MF_BYCOMMAND | (hasFolder ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_CLEAR_FAVORITE_DESTINATIONS,
                   MF_BYCOMMAND | (!favoriteDestinationFolders_.empty() ? MF_ENABLED : MF_GRAYED));
        CheckMenuItem(menu_, ID_FILE_TOGGLE_PAIRED_RAW_JPEG_OPERATIONS,
                  MF_BYCOMMAND | (rawJpegPairedOperationsEnabled_ ? MF_CHECKED : MF_UNCHECKED));
        EnableMenuItem(menu_, ID_FILE_COPY_SELECTION,
                       MF_BYCOMMAND | (hasSelection && !fileOperationActive_ ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_MOVE_SELECTION,
                       MF_BYCOMMAND | (hasSelection && !fileOperationActive_ ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_DELETE_SELECTION,
                       MF_BYCOMMAND | (hasSelection && !fileOperationActive_ ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_DELETE_SELECTION_PERMANENT,
                       MF_BYCOMMAND | (hasSelection && !fileOperationActive_ ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_ROTATE_JPEG_LEFT, MF_BYCOMMAND | (hasSelectedJpeg ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_ROTATE_JPEG_RIGHT, MF_BYCOMMAND | (hasSelectedJpeg ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_BATCH_CONVERT_SELECTION_JPEG,
                       MF_BYCOMMAND | (hasSelection && !batchConvertActive_ ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_BATCH_CONVERT_SELECTION_PNG,
                       MF_BYCOMMAND | (hasSelection && !batchConvertActive_ ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_BATCH_CONVERT_SELECTION_TIFF,
                       MF_BYCOMMAND | (hasSelection && !batchConvertActive_ ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_BATCH_CONVERT_FOLDER_JPEG,
                       MF_BYCOMMAND | (hasFolder && !batchConvertActive_ ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_BATCH_CONVERT_FOLDER_PNG,
                       MF_BYCOMMAND | (hasFolder && !batchConvertActive_ ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_FILE_BATCH_CONVERT_FOLDER_TIFF,
                       MF_BYCOMMAND | (hasFolder && !batchConvertActive_ ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_VIEW_SLIDESHOW_SELECTION, MF_BYCOMMAND | (hasSelection ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_VIEW_SLIDESHOW_FOLDER, MF_BYCOMMAND | (hasFolder ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_VIEW_SETTINGS, MF_BYCOMMAND | MF_ENABLED);
        EnableMenuItem(menu_, ID_VIEW_NAVIGATE_BACK_FOLDER, MF_BYCOMMAND | (canNavigateBack ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_VIEW_NAVIGATE_FORWARD_FOLDER, MF_BYCOMMAND | (canNavigateForward ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_VIEW_THUMBNAIL_SIZE_INCREASE, MF_BYCOMMAND | (thumbnailSteppingEnabled ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu_, ID_VIEW_THUMBNAIL_SIZE_DECREASE, MF_BYCOMMAND | (thumbnailSteppingEnabled ? MF_ENABLED : MF_GRAYED));

        CheckMenuRadioItem(
            menu_,
            ID_VIEW_THUMBNAILS,
            ID_VIEW_DETAILS,
            browserMode_ == BrowserMode::Thumbnails ? ID_VIEW_THUMBNAILS : ID_VIEW_DETAILS,
            MF_BYCOMMAND);

        CheckMenuItem(
            menu_,
            ID_VIEW_RECURSIVE,
            MF_BYCOMMAND | (recursiveBrowsingEnabled_ ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(
            menu_,
            ID_VIEW_SHOW_SUBFOLDERS,
            MF_BYCOMMAND | (showSubfoldersInBrowser_ ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(
            menu_,
            ID_VIEW_THUMBNAIL_LAYOUT_COMPACT,
            MF_BYCOMMAND | (compactThumbnailLayout ? MF_CHECKED : MF_UNCHECKED));

        CheckMenuRadioItem(
            menu_,
            ID_VIEW_THEME_LIGHT,
            ID_VIEW_THEME_DARK,
            themeMode_ == ThemeMode::Light ? ID_VIEW_THEME_LIGHT : ID_VIEW_THEME_DARK,
            MF_BYCOMMAND);

        CheckMenuItem(
            menu_,
            ID_VIEW_NVJPEG_ACCELERATION,
            MF_BYCOMMAND | ((nvJpegEnabled_ && HasNvJpegCapability()) ? MF_CHECKED : MF_UNCHECKED));
        EnableMenuItem(
            menu_,
            ID_VIEW_NVJPEG_ACCELERATION,
            MF_BYCOMMAND | (HasNvJpegCapability() ? MF_ENABLED : MF_GRAYED));
        CheckMenuItem(
            menu_,
            ID_VIEW_LIBRAW_OUT_OF_PROCESS,
            MF_BYCOMMAND | ((libRawOutOfProcessEnabled_ && decode::IsLibRawBuildEnabled()) ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(
            menu_,
            ID_VIEW_PERSISTENT_THUMBNAIL_CACHE,
            MF_BYCOMMAND | (persistentThumbnailCacheEnabled_ ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(
            menu_,
            ID_VIEW_SINGLE_INSTANCE,
            MF_BYCOMMAND | (app::Application::IsSingleInstanceEnabled() ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(
            menu_,
            ID_VIEW_DEFAULT_VIEWER_SECONDARY_MONITOR,
            MF_BYCOMMAND | (defaultViewerToSecondaryMonitor_ ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(
            menu_,
            ID_VIEW_USE_SLIDESHOW_TRANSITION,
            MF_BYCOMMAND | (useSlideshowTransition_ ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuRadioItem(
            menu_,
            ID_VIEW_PAIRED_RAW_JPEG_PREFER_JPEG,
            ID_VIEW_PAIRED_RAW_JPEG_PREFER_RAW,
            pairedRawJpegViewerPreference_ == browser::RawJpegDisplayPreference::Jpeg
                ? ID_VIEW_PAIRED_RAW_JPEG_PREFER_JPEG
                : ID_VIEW_PAIRED_RAW_JPEG_PREFER_RAW,
            MF_BYCOMMAND);
        EnableMenuItem(
            menu_,
            ID_VIEW_PAIRED_RAW_JPEG_PREFER_JPEG,
            MF_BYCOMMAND | (rawJpegPairedOperationsEnabled_ ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(
            menu_,
            ID_VIEW_PAIRED_RAW_JPEG_PREFER_RAW,
            MF_BYCOMMAND | (rawJpegPairedOperationsEnabled_ ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(
            menu_,
            ID_VIEW_DEFAULT_VIEWER_SECONDARY_MONITOR,
            MF_BYCOMMAND | (hasSecondaryMonitor ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(
            menu_,
            ID_VIEW_LIBRAW_OUT_OF_PROCESS,
            MF_BYCOMMAND | (decode::IsLibRawBuildEnabled() ? MF_ENABLED : MF_GRAYED));
        CheckMenuRadioItem(
            menu_,
            ID_VIEW_THUMBNAIL_SIZE_96,
            ID_VIEW_THUMBNAIL_SIZE_640,
            CommandIdFromThumbnailSizePreset(thumbnailSizePreset),
            MF_BYCOMMAND);
        CheckMenuItem(
            menu_,
            ID_VIEW_THUMBNAIL_DETAILS,
            MF_BYCOMMAND | (thumbnailDetailsVisible ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(
            menu_,
            ID_VIEW_DETAILS_STRIP,
            MF_BYCOMMAND | (detailsStripVisible_ ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuRadioItem(
            menu_,
            ID_VIEW_APP_TEXT_SIZE_SMALL,
            ID_VIEW_APP_TEXT_SIZE_LARGE,
            CommandIdFromAppTextSize(appTextSize_),
            MF_BYCOMMAND);
        CheckMenuRadioItem(
            menu_,
            ID_VIEW_VIEWER_MOUSE_WHEEL_ZOOM,
            ID_VIEW_VIEWER_MOUSE_WHEEL_NAVIGATE,
            CommandIdFromViewerMouseWheelBehavior(viewerMouseWheelBehavior_),
            MF_BYCOMMAND);
        const viewer::InfoOverlayTextSize overlayTextSize = viewerWindow_
            ? viewerWindow_->OverlayTextSize()
            : viewer::InfoOverlayTextSize::Small;
        CheckMenuRadioItem(
            menu_,
            ID_VIEW_VIEWER_OVERLAY_TEXT_SMALL,
            ID_VIEW_VIEWER_OVERLAY_TEXT_LARGE,
            CommandIdFromViewerOverlayTextSize(overlayTextSize),
            MF_BYCOMMAND);
        CheckMenuItem(
            menu_,
            ID_VIEW_VIEWER_DETAIL_OVERLAYS,
            MF_BYCOMMAND | ((viewerWindow_ && viewerWindow_->AreInfoOverlaysVisible()) ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(
            menu_,
            ID_VIEW_VIEWER_FULL_METADATA,
            MF_BYCOMMAND | ((viewerWindow_ && viewerWindow_->IsFullMetadataVisible()) ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(
            menu_,
            ID_VIEW_PRESSURE_STATE_STATUS,
            MF_BYCOMMAND | (showPressureStateInStatusBar_ ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(
            menu_,
            ID_EDIT_CLOSE_MAIN_WINDOW_ON_ESCAPE,
            MF_BYCOMMAND | (closeMainWindowOnEscape_ ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuRadioItem(
            menu_,
            ID_HELP_PERFORMANCE_PROFILE_CONSERVATIVE,
            ID_HELP_PERFORMANCE_PROFILE_AGGRESSIVE,
            CommandIdFromResourceProfile(resourceProfile_),
            MF_BYCOMMAND);
        EnableMenuItem(
            menu_,
            ID_FILE_BATCH_CONVERT_CANCEL,
            MF_BYCOMMAND | (batchConvertActive_ ? MF_ENABLED : MF_GRAYED));

        const browser::BrowserSortMode sortMode = browserPaneController_
            ? browserPaneController_->GetSortMode()
            : browser::BrowserSortMode::FileName;
        const bool sortAscending = browserPaneController_
            ? browserPaneController_->IsSortAscending()
            : true;
        CheckMenuRadioItem(
            menu_,
            ID_VIEW_SORT_FILENAME,
            ID_VIEW_SORT_TAGS,
            CommandIdFromSortMode(sortMode),
            MF_BYCOMMAND);
        CheckMenuItem(
            menu_,
            ID_VIEW_SORT_DIRECTION,
            MF_BYCOMMAND | (sortAscending ? MF_UNCHECKED : MF_CHECKED));

        UpdateToolbarItemStates();
    }

    void MainWindow::UpdateToolbarItemStates()
    {
        const bool hasSelection = browserPaneController_ && browserPaneController_->SelectedCount() > 0;
        const bool hasCompareSelection = browserPaneController_ && browserPaneController_->SelectedCount() == 2;
        const bool selectionActionsEnabled = hasSelection && !fileOperationActive_;
        const bool sizeEnabled = browserMode_ == BrowserMode::Thumbnails;

        for (auto& item : toolbarItems_)
        {
            switch (item.commandId)
            {
            case ID_VIEW_NAVIGATE_BACK_FOLDER:
                item.enabled = browserModel_
                    && openedFolderHistoryIndex_ != kInvalidHistoryIndex
                    && openedFolderHistoryIndex_ > 0
                    && pendingFolderHistoryNavigation_ == FolderHistoryNavigationDirection::None
                    && !folderEnumerationActive_;
                break;
            case ID_VIEW_NAVIGATE_FORWARD_FOLDER:
                item.enabled = browserModel_
                    && openedFolderHistoryIndex_ != kInvalidHistoryIndex
                    && openedFolderHistoryIndex_ + 1 < openedFolderHistory_.size()
                    && pendingFolderHistoryNavigation_ == FolderHistoryNavigationDirection::None
                    && !folderEnumerationActive_;
                break;
            case ID_VIEW_RECURSIVE:
                item.checked = recursiveBrowsingEnabled_;
                break;
            case ID_VIEW_THUMBNAILS:
                item.checked = browserMode_ == BrowserMode::Thumbnails;
                break;
            case ID_VIEW_DETAILS:
                item.checked = browserMode_ == BrowserMode::Details;
                break;
            case ID_ACTION_THUMBNAIL_SIZE_MENU:
                item.enabled = sizeEnabled;
                break;
            case ID_FILE_COMPARE_SELECTED:
                item.enabled = hasCompareSelection;
                break;
            case ID_FILE_COPY_SELECTION:
            case ID_FILE_MOVE_SELECTION:
            case ID_FILE_DELETE_SELECTION:
                item.enabled = selectionActionsEnabled;
                break;
            default:
                break;
            }
        }

        InvalidateToolbarStrip();
    }

    void MainWindow::InvalidateToolbarStrip()
    {
        if (!hwnd_)
        {
            return;
        }

        RECT client{};
        GetClientRect(hwnd_, &client);
        RECT stripRect{0, 0, client.right, kActionStripHeight};
        InvalidateRect(hwnd_, &stripRect, FALSE);
    }

    void MainWindow::HandleDisplaySurfaceChange()
    {
        if (displaySurfaceRecoveryTimerId_ == 0)
        {
            RecoverDisplaySurfaces(true);
        }

        ScheduleDisplaySurfaceRecoveryRetries();
    }

    void MainWindow::RecoverDisplaySurfaces(bool relayout)
    {
        if (!hwnd_ || IsWindow(hwnd_) == FALSE)
        {
            return;
        }

        if (relayout && !IsIconic(hwnd_))
        {
            LayoutChildren();
        }

        if (browserPaneController_)
        {
            browserPaneController_->RecoverDisplaySurface();
        }

        if (viewerWindow_ && viewerWindow_->IsOpen())
        {
            viewerWindow_->RecoverDisplaySurface();
        }

        if (diagnosticsWindow_ && diagnosticsWindow_->IsOpen())
        {
            diagnosticsWindow_->RecoverDisplaySurface();
        }

        RedrawWindow(hwnd_, nullptr, nullptr,
                     RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME | RDW_UPDATENOW);
    }

    void MainWindow::ScheduleDisplaySurfaceRecoveryRetries()
    {
        if (!hwnd_ || IsWindow(hwnd_) == FALSE)
        {
            return;
        }

        displaySurfaceRecoveryAttempt_ = 0;
        if (displaySurfaceRecoveryTimerId_ != 0)
        {
            return;
        }

        displaySurfaceRecoveryTimerId_ = SetTimer(
            hwnd_,
            kDisplaySurfaceRecoveryTimerId,
            kDisplaySurfaceRecoveryIntervalMs,
            nullptr);
    }

    void MainWindow::StopDisplaySurfaceRecoveryRetries()
    {
        if (displaySurfaceRecoveryTimerId_ != 0 && hwnd_ && IsWindow(hwnd_) != FALSE)
        {
            KillTimer(hwnd_, kDisplaySurfaceRecoveryTimerId);
        }

        displaySurfaceRecoveryTimerId_ = 0;
        displaySurfaceRecoveryAttempt_ = 0;
    }

    void MainWindow::UpdateWindowTitle() const
    {
        if (!hwnd_)
        {
            return;
        }

        std::wstring title = L"HyperBrowse";
        if (browserModel_ && !browserModel_->FolderPath().empty())
        {
            title.append(L" - ");
            title.append(browserModel_->FolderPath());
        }
        SetWindowTextW(hwnd_, title.c_str());
    }

    void MainWindow::RebuildAppTextFonts()
    {
        const HFONT defaultGuiFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

        DeleteFontIfOwned(appTextUiFont_);
        appTextUiFont_ = CreateSystemUiFont(appTextSize_);
        if (!appTextUiFont_)
        {
            appTextUiFont_ = defaultGuiFont;
        }

        DeleteFontIfOwned(detailsPanelTitleFont_);
        DeleteFontIfOwned(detailsPanelSummaryFont_);
        DeleteFontIfOwned(detailsPanelBodyFont_);
        detailsPanelTitleFont_ = CreateDialogUiFont(11, FW_SEMIBOLD, appTextSize_);
        detailsPanelSummaryFont_ = CreateDialogUiFont(9, FW_NORMAL, appTextSize_);
        detailsPanelBodyFont_ = CreateDialogUiFont(9, FW_NORMAL, appTextSize_);
        if (!detailsPanelTitleFont_) detailsPanelTitleFont_ = defaultGuiFont;
        if (!detailsPanelSummaryFont_) detailsPanelSummaryFont_ = defaultGuiFont;
        if (!detailsPanelBodyFont_) detailsPanelBodyFont_ = defaultGuiFont;

        const HWND controls[] = {filterEdit_, treePane_, statusBar_};
        for (HWND control : controls)
        {
            if (control)
            {
                SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(appTextUiFont_), TRUE);
            }
        }

        if (tooltipControl_)
        {
            SendMessageW(tooltipControl_, WM_SETFONT, reinterpret_cast<WPARAM>(appTextUiFont_), TRUE);
            SendMessageW(tooltipControl_,
                         TTM_SETMAXTIPWIDTH,
                         0,
                         hyperbrowse::util::ScaleAppTextDimension(kToolbarTooltipMaxWidth, appTextSize_));
        }

        if (detailsPanelText_)
        {
            SendMessageW(detailsPanelText_, WM_SETFONT, reinterpret_cast<WPARAM>(detailsPanelBodyFont_), TRUE);
        }

        for (HWND edit : quickAccessShortcutEdits_)
        {
            if (edit)
            {
                SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(detailsPanelBodyFont_), TRUE);
            }
        }

        RefreshDetailsPanelBodyPresentation();
    }

    void MainWindow::ApplyAppTextSize()
    {
        appTextSize_ = util::NormalizeAppTextSize(static_cast<std::uint32_t>(appTextSize_));
        RebuildAppTextFonts();
        if (browserPaneController_)
        {
            browserPaneController_->SetAppTextSize(appTextSize_);
        }
        if (diagnosticsWindow_)
        {
            diagnosticsWindow_->SetAppTextSize(appTextSize_);
        }
        if (viewerWindow_)
        {
            viewerWindow_->SetAppTextSize(appTextSize_);
        }
        if (menu_)
        {
            RefreshPersistentMenuOwnerDraw();
        }
        if (hwnd_)
        {
            LayoutChildren();
            InvalidateToolbarStrip();
            InvalidateRect(statusBar_, nullptr, TRUE);
            InvalidateRect(detailsPanelText_, nullptr, TRUE);
            RedrawWindow(hwnd_, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        }
    }

    void MainWindow::ApplyTheme()
    {
        const ThemePalette palette = GetThemePalette();

        if (backgroundBrush_)
        {
            DeleteObject(backgroundBrush_);
            backgroundBrush_ = nullptr;
        }

        if (actionFieldBrush_)
        {
            DeleteObject(actionFieldBrush_);
            actionFieldBrush_ = nullptr;
        }

        if (detailsPanelBrush_)
        {
            DeleteObject(detailsPanelBrush_);
            detailsPanelBrush_ = nullptr;
        }

        backgroundBrush_ = CreateSolidBrush(palette.windowBackground);
        actionFieldBrush_ = CreateSolidBrush(palette.actionFieldBackground);
        detailsPanelBrush_ = CreateSolidBrush(palette.paneBackground);

        if (menuBackgroundBrush_)
        {
            DeleteObject(menuBackgroundBrush_);
            menuBackgroundBrush_ = nullptr;
        }
        menuBackgroundBrush_ = CreateSolidBrush(palette.paneBackground);
        if (menu_ && menuBackgroundBrush_)
        {
            MENUINFO menuInfo{};
            menuInfo.cbSize = sizeof(menuInfo);
            menuInfo.fMask = MIM_BACKGROUND | MIM_APPLYTOSUBMENUS;
            menuInfo.hbrBack = menuBackgroundBrush_;
            SetMenuInfo(menu_, &menuInfo);
        }

        if (hwnd_)
        {
            ApplyWindowFrameTheme(hwnd_,
                                  themeMode_ == ThemeMode::Dark,
                                  palette.actionStripBackground,
                                  palette.text,
                                  palette.actionStripBorder);
        }

        if (treePane_)
        {
            if (treeImageList_)
            {
                ImageList_SetBkColor(treeImageList_, CLR_NONE);
            }
            TreeView_SetBkColor(treePane_, palette.paneBackground);
            TreeView_SetTextColor(treePane_, palette.text);
            TreeView_SetLineColor(treePane_, palette.treeLine);
            InvalidateRect(treePane_, nullptr, TRUE);
        }

        if (browserPaneController_)
        {
            browserPaneController_->SetDarkTheme(themeMode_ == ThemeMode::Dark);
        }

        if (viewerWindow_)
        {
            viewerWindow_->SetDarkTheme(themeMode_ == ThemeMode::Dark);
        }

        if (diagnosticsWindow_)
        {
            diagnosticsWindow_->SetDarkTheme(themeMode_ == ThemeMode::Dark);
        }

        RefreshDetailsPanelBodyPresentation();

        if (hwnd_)
        {
            InvalidateToolbarStrip();
            InvalidateRect(statusBar_, nullptr, TRUE);
            InvalidateRect(detailsPanelText_, nullptr, TRUE);
            if (filterEdit_)
            {
                RedrawWindow(filterEdit_, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_UPDATENOW);
            }
            RefreshWindowNonClientArea(hwnd_);
            RedrawWindow(hwnd_, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        }
    }

    void MainWindow::ApplyViewerMouseWheelSetting()
    {
        if (viewerWindow_)
        {
            viewerWindow_->SetMouseWheelBehavior(viewerMouseWheelBehavior_);
                viewerWindow_->SetKeyboardPanningInverted(invertKeyboardPanning_);
        }
    }

    void MainWindow::ApplyViewerTransitionSettings()
    {
        if (viewerWindow_)
        {
            viewerWindow_->SetTransitionSettings(
                slideshowTransitionStyle_,
                slideshowTransitionDurationMs_);
            viewerWindow_->SetManualTransitionEnabled(useSlideshowTransition_);
        }
    }

    void MainWindow::ApplyThumbnailMemoryPressureState()
    {
        if (browserPaneController_)
        {
            browserPaneController_->SetThumbnailMemoryPressureActive(thumbnailMemoryPressureActive_);
        }

        if (viewerWindow_)
        {
            viewerWindow_->SetMemoryPressureActive(thumbnailMemoryPressureActive_);
        }

        if (detailsPanelThumbnailScheduler_)
        {
            detailsPanelThumbnailScheduler_->SetPressureModeEnabled(thumbnailMemoryPressureActive_);
            if (thumbnailMemoryPressureActive_)
            {
                detailsPanelThumbnailScheduler_->TrimCacheToBytes(std::max<std::size_t>(1, detailsPanelThumbnailScheduler_->CacheCapacityBytes() / 2));
            }
        }

        UpdateStatusText();
    }

    void MainWindow::ApplyResourceProfileSetting()
    {
        if (browserPaneController_)
        {
            browserPaneController_->SetResourceProfile(resourceProfile_);
        }

        if (viewerWindow_)
        {
            viewerWindow_->SetResourceProfile(resourceProfile_);
        }

        RecreateDetailsPanelThumbnailScheduler();
    }

    void MainWindow::QueueMemoryPressureSample()
    {
        if (!hwnd_ || !memoryPressureExecutor_ || memoryPressureSampleQueued_)
        {
            return;
        }

        memoryPressureSampleQueued_ = true;
        const HWND targetWindow = hwnd_;
        if (!memoryPressureExecutor_->Post([targetWindow]()
            {
                auto update = std::make_unique<MemoryPressureSampleResult>();
                const util::MemorySnapshot memorySnapshot = util::QueryMemorySnapshot();
                if (memorySnapshot.IsValid() && memorySnapshot.totalPhysicalBytes != 0)
                {
                    const std::uint64_t usedPercent = 100ULL
                        - ((memorySnapshot.availablePhysicalBytes * 100ULL) / memorySnapshot.totalPhysicalBytes);
                    update->pressureDetected = memorySnapshot.availablePhysicalBytes < kMemoryPressureAvailableBytesThreshold
                        || usedPercent >= kMemoryPressureActivateUsedPercent;
                    update->recoveryCandidate = memorySnapshot.availablePhysicalBytes >= kMemoryPressureAvailableBytesThreshold
                        && usedPercent < kMemoryPressureRecoverUsedPercent;
                }

                if (!PostMessageW(targetWindow, kMemoryPressureSampledMessage, 0, reinterpret_cast<LPARAM>(update.get())))
                {
                    return;
                }

                update.release();
            }))
        {
            memoryPressureSampleQueued_ = false;
        }
    }

    void MainWindow::RecreateDetailsPanelThumbnailScheduler()
    {
        if (!hwnd_)
        {
            return;
        }

        if (detailsPanelThumbnailScheduler_)
        {
            ++detailsPanelThumbnailSessionId_;
            ++detailsPanelThumbnailRequestEpoch_;

            auto scheduler = std::make_unique<services::ThumbnailScheduler>(
                thumbnailCacheCapacityOverrideBytes_,
                0,
                resourceProfile_);
            if (scheduler)
            {
                if (hwnd_)
                {
                    scheduler->BindTargetWindow(hwnd_);
                }
                scheduler->SetDiskCacheEnabled(persistentThumbnailCacheEnabled_);
                scheduler->SetPressureModeEnabled(thumbnailMemoryPressureActive_);
                if (thumbnailMemoryPressureActive_)
                {
                    scheduler->TrimCacheToBytes(std::max<std::size_t>(1, scheduler->CacheCapacityBytes() / 2));
                }
            }
            detailsPanelThumbnailScheduler_ = std::move(scheduler);
        }
    }

    void MainWindow::ApplyCacheCapacityOverrideSettings()
    {
        if (browserPaneController_)
        {
            browserPaneController_->SetCacheCapacityOverrides(
                thumbnailCacheCapacityOverrideBytes_,
                metadataCacheCapacityOverrideEntries_);
        }

        RecreateDetailsPanelThumbnailScheduler();
    }

    void MainWindow::ApplyPersistentThumbnailCacheSetting()
    {
        if (browserPaneController_)
        {
            browserPaneController_->SetPersistentThumbnailCacheEnabled(persistentThumbnailCacheEnabled_);
        }

        if (detailsPanelThumbnailScheduler_)
        {
            detailsPanelThumbnailScheduler_->SetDiskCacheEnabled(persistentThumbnailCacheEnabled_);
        }
    }

    void MainWindow::ShowPersistentThumbnailCacheDialog()
    {
        if (cacheMaintenanceActive_ || !cacheMaintenanceState_)
        {
            return;
        }

        StartPersistentThumbnailCacheStatistics();
    }

    void MainWindow::ShowPersistentThumbnailCacheDialogContents(std::wstring content,
                                                                 std::wstring expandedInformation)
    {

        constexpr int kCompactPersistentCacheButtonId = 1001;
        constexpr int kPurgePersistentCacheButtonId = 1002;

        TASKDIALOG_BUTTON buttons[] = {
            {kCompactPersistentCacheButtonId, L"Compact cache\nRepair the saved index, remove orphaned thumbnails, and trim the cache to its storage budget."},
            {kPurgePersistentCacheButtonId, L"Purge cache\nDelete every persistent thumbnail and clear the saved cache index."},
        };

        TASKDIALOGCONFIG config{};
        config.cbSize = sizeof(config);
        config.hwndParent = hwnd_;
        config.hInstance = instance_;
        config.dwFlags = TDF_USE_COMMAND_LINKS | TDF_ALLOW_DIALOG_CANCELLATION;
        config.dwCommonButtons = TDCBF_CLOSE_BUTTON;
        config.pszWindowTitle = L"Persistent Thumbnail Cache";
        config.pszMainIcon = MAKEINTRESOURCEW(IDI_HYPERBROWSE);
        config.pszMainInstruction = L"Inspect or clean the persistent thumbnail cache.";
        config.pszContent = content.c_str();
        config.pszExpandedInformation = expandedInformation.c_str();
        config.pszCollapsedControlText = L"Show Cache Details";
        config.pszExpandedControlText = L"Hide Cache Details";
        config.cButtons = static_cast<UINT>(std::size(buttons));
        config.pButtons = buttons;
        config.nDefaultButton = kCompactPersistentCacheButtonId;

        int clickedButton = 0;
        const HRESULT dialogResult = TaskDialogIndirect(&config, &clickedButton, nullptr, nullptr);
        if (FAILED(dialogResult))
        {
            MessageBoxW(hwnd_,
                        L"Failed to open the persistent thumbnail cache dialog.",
                        L"Persistent Thumbnail Cache",
                        MB_OK | MB_ICONERROR);
            return;
        }

        if (clickedButton == kCompactPersistentCacheButtonId)
        {
            StartPersistentThumbnailCacheMaintenance(false);
            return;
        }

        if (clickedButton == kPurgePersistentCacheButtonId)
        {
            const int confirmResult = MessageBoxW(hwnd_,
                                                  L"Delete all thumbnails saved in the persistent cache? This does not delete your images.",
                                                  L"Purge Persistent Thumbnail Cache",
                                                  MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
            if (confirmResult == IDYES)
            {
                StartPersistentThumbnailCacheMaintenance(true);
            }
        }
    }

    void MainWindow::StartPersistentThumbnailCacheStatistics()
    {
        if (cacheMaintenanceActive_ || !cacheMaintenanceExecutor_ || !cacheMaintenanceState_ || !hwnd_)
        {
            return;
        }

        cacheMaintenanceActive_ = true;
        const HWND targetWindow = hwnd_;
        const auto state = cacheMaintenanceState_;
        const bool queued = cacheMaintenanceExecutor_->Post([targetWindow, state]()
        {
            bool succeeded = true;
            try
            {
                cache::DiskThumbnailCache persistentCache;
                auto statistics = persistentCache.QueryStatistics();
                {
                    std::scoped_lock lock(state->mutex);
                    state->statistics = std::move(statistics);
                }
            }
            catch (...)
            {
                succeeded = false;
            }

            if (!PostMessageW(targetWindow,
                              kPersistentThumbnailCacheMaintenanceMessage,
                              static_cast<WPARAM>(succeeded ? 4u : 0u),
                              0))
            {
                return;
            }
        });

        if (!queued)
        {
            cacheMaintenanceActive_ = false;
            MessageBoxW(hwnd_,
                        L"The persistent thumbnail cache operation could not be started.",
                        L"Persistent Thumbnail Cache",
                        MB_OK | MB_ICONERROR);
        }
    }

    void MainWindow::StartPersistentThumbnailCacheMaintenance(bool purge)
    {
        if (cacheMaintenanceActive_ || !cacheMaintenanceExecutor_ || !hwnd_)
        {
            return;
        }

        cacheMaintenanceActive_ = true;
        const HWND targetWindow = hwnd_;
        const bool queued = cacheMaintenanceExecutor_->Post([targetWindow, purge]()
        {
            bool succeeded = true;
            try
            {
                cache::DiskThumbnailCache persistentCache;
                if (purge)
                {
                    persistentCache.Clear();
                }
                else
                {
                    succeeded = persistentCache.Compact();
                }
            }
            catch (...)
            {
                succeeded = false;
            }

            if (!PostMessageW(targetWindow,
                              kPersistentThumbnailCacheMaintenanceMessage,
                              static_cast<WPARAM>(static_cast<unsigned int>(purge
                                                                                 ? PersistentThumbnailCacheMaintenanceOperation::Purge
                                                                                 : PersistentThumbnailCacheMaintenanceOperation::Compact)
                                                 | (succeeded ? kPersistentThumbnailCacheMaintenanceSuccessFlag : 0u)),
                              0))
            {
                return;
            }
        });

        if (!queued)
        {
            cacheMaintenanceActive_ = false;
            MessageBoxW(hwnd_,
                        L"The persistent thumbnail cache operation could not be started.",
                        L"Persistent Thumbnail Cache",
                        MB_OK | MB_ICONERROR);
        }
    }

    LRESULT MainWindow::OnPersistentThumbnailCacheMaintenanceMessage(WPARAM wParam)
    {
        cacheMaintenanceActive_ = false;
        const auto operation = static_cast<PersistentThumbnailCacheMaintenanceOperation>(wParam & 3u);
        const bool succeeded = (wParam & kPersistentThumbnailCacheMaintenanceSuccessFlag) != 0;

        if (operation == PersistentThumbnailCacheMaintenanceOperation::Statistics)
        {
            if (!succeeded || !cacheMaintenanceState_)
            {
                MessageBoxW(hwnd_,
                            L"Failed to inspect the persistent thumbnail cache.",
                            L"Persistent Thumbnail Cache",
                            MB_OK | MB_ICONERROR);
                return 0;
            }

            cache::DiskThumbnailCache::Statistics statistics;
            {
                std::scoped_lock lock(cacheMaintenanceState_->mutex);
                statistics = cacheMaintenanceState_->statistics;
            }
            if (statistics.cacheDirectory.empty())
            {
                MessageBoxW(hwnd_,
                            L"The persistent thumbnail cache folder could not be resolved.",
                            L"Persistent Thumbnail Cache",
                            MB_OK | MB_ICONERROR);
                return 0;
            }

            ShowPersistentThumbnailCacheDialogContents(
                BuildPersistentThumbnailCacheSummary(statistics, persistentThumbnailCacheEnabled_),
                BuildPersistentThumbnailCacheDetails(statistics));
            return 0;
        }

        const bool purge = operation == PersistentThumbnailCacheMaintenanceOperation::Purge;

        if (!succeeded)
        {
            MessageBoxW(hwnd_,
                        purge
                            ? L"Failed to purge the persistent thumbnail cache."
                            : L"Failed to compact the persistent thumbnail cache.",
                        L"Persistent Thumbnail Cache",
                        MB_OK | MB_ICONERROR);
            return 0;
        }

        NotifyLongOperationComplete(
            purge ? L"Persistent Thumbnail Cache Purged" : L"Persistent Thumbnail Cache Compacted",
            purge
                ? L"All saved thumbnails were removed from the persistent cache."
                : L"The persistent thumbnail cache index and storage were repaired.");
        ShowPersistentThumbnailCacheDialog();
        return 0;
    }

    void MainWindow::ApplyRawJpegPairingSettings()
    {
        if (!browserPaneController_)
        {
            return;
        }

        browserPaneController_->SetRawJpegDisplayPreference(pairedRawJpegViewerPreference_);
        browserPaneController_->SetRawJpegStackingEnabled(rawJpegPairedOperationsEnabled_);
    }

    void MainWindow::ShowFileAssociationsDialog()
    {
        std::vector<bool> defaults;
        std::wstring errorMessage;
        if (!services::QueryFileAssociationDefaults(&defaults, &errorMessage))
        {
            MessageBoxW(hwnd_,
                        errorMessage.empty() ? L"Windows could not read the current file associations." : errorMessage.c_str(),
                        L"File Associations",
                        MB_OK | MB_ICONERROR);
            return;
        }

        std::vector<bool> selectedDefaults;
        PromptForFileAssociations(hwnd_, instance_, appTextSize_, defaults, &selectedDefaults);
    }

    void MainWindow::ShowConsolidatedSettingsDialog()
    {
        ConsolidatedSettingsDialogState state;
        state.ownerWindow = hwnd_;
        state.instance = instance_;
        state.title = L"Settings";
        state.appTextSize = appTextSize_;
        state.darkTheme = themeMode_ == ThemeMode::Dark;
        state.resourceProfile = resourceProfile_;
        state.thumbnailSizePreset = thumbnailSizePreset_;
        state.viewerMouseWheelBehavior = viewerMouseWheelBehavior_;
        state.invertKeyboardPanning = invertKeyboardPanning_;
        state.slideshowTransitionStyle = slideshowTransitionStyle_;
        state.slideshowIntervalMs = slideshowIntervalMs_;
        state.slideshowTransitionDurationMs = slideshowTransitionDurationMs_;
        state.useSlideshowTransition = useSlideshowTransition_;
        state.compactThumbnailLayout = compactThumbnailLayout_;
        state.thumbnailDetailsVisible = thumbnailDetailsVisible_;
        state.detailsStripVisible = detailsStripVisible_;
        state.recursiveBrowsingEnabled = recursiveBrowsingEnabled_;
        state.showSubfoldersInBrowser = showSubfoldersInBrowser_;
        state.rawJpegPairedOperationsEnabled = rawJpegPairedOperationsEnabled_;
        state.pairedRawJpegViewerPreference = pairedRawJpegViewerPreference_;
        state.defaultViewerToSecondaryMonitor = defaultViewerToSecondaryMonitor_;
        state.persistentThumbnailCacheEnabled = persistentThumbnailCacheEnabled_;
        state.thumbnailCacheCapacityOverrideBytes = thumbnailCacheCapacityOverrideBytes_;
        state.metadataCacheCapacityOverrideEntries = metadataCacheCapacityOverrideEntries_;
        state.showPressureStateInStatusBar = showPressureStateInStatusBar_;
        state.nvJpegEnabled = nvJpegEnabled_;
        state.libRawOutOfProcessEnabled = libRawOutOfProcessEnabled_;
        state.closeMainWindowOnEscape = closeMainWindowOnEscape_;
        state.singleInstanceEnabled = app::Application::IsSingleInstanceEnabled();
        state.secondaryMonitorAvailable = FindAlternateMonitorForWindow(hwnd_) != nullptr;
        state.nvJpegAvailable = HasNvJpegCapability();
        state.libRawAvailable = decode::IsLibRawBuildEnabled();
        state.infoOverlaysVisible = viewerWindow_ && viewerWindow_->IsOpen()
            ? viewerWindow_->AreInfoOverlaysVisible()
            : viewer::ViewerWindow::DefaultInfoOverlaysVisible();
        state.overlayTextSize = viewerWindow_ && viewerWindow_->IsOpen()
            ? viewerWindow_->OverlayTextSize()
            : viewer::ViewerWindow::DefaultOverlayTextSize();
        state.fullMetadataVisible = viewerWindow_ && viewerWindow_->IsOpen()
            ? viewerWindow_->IsFullMetadataVisible()
            : viewer::ViewerWindow::DefaultFullMetadataVisible();

        state.bodyFont = CreateDialogUiFont(9, FW_NORMAL, state.appTextSize);
        if (!state.bodyFont)
        {
            state.bodyFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        }

        state.apply = [this](const ConsolidatedSettingsDialogState& draft)
        {
            const bool slideshowDurationChanged = slideshowIntervalMs_ != draft.slideshowIntervalMs;
            const bool slideshowTransitionChanged = slideshowTransitionStyle_ != draft.slideshowTransitionStyle
                || slideshowTransitionDurationMs_ != draft.slideshowTransitionDurationMs
                || useSlideshowTransition_ != draft.useSlideshowTransition;
            const bool thumbnailDisplayChanged = thumbnailSizePreset_ != draft.thumbnailSizePreset
                || compactThumbnailLayout_ != draft.compactThumbnailLayout
                || thumbnailDetailsVisible_ != draft.thumbnailDetailsVisible
                || appTextSize_ != draft.appTextSize;
            const bool folderScopeChanged = recursiveBrowsingEnabled_ != draft.recursiveBrowsingEnabled
                || showSubfoldersInBrowser_ != draft.showSubfoldersInBrowser;
            const bool viewerOverlayChanged = !viewerWindow_ || !viewerWindow_->IsOpen()
                || viewerWindow_->AreInfoOverlaysVisible() != draft.infoOverlaysVisible
                || viewerWindow_->OverlayTextSize() != draft.overlayTextSize
                || viewerWindow_->IsFullMetadataVisible() != draft.fullMetadataVisible;

            slideshowIntervalMs_ = draft.slideshowIntervalMs;
            slideshowTransitionStyle_ = draft.slideshowTransitionStyle;
            slideshowTransitionDurationMs_ = draft.slideshowTransitionDurationMs;
            useSlideshowTransition_ = draft.useSlideshowTransition;
            viewerMouseWheelBehavior_ = draft.viewerMouseWheelBehavior;
            invertKeyboardPanning_ = draft.invertKeyboardPanning;
            rawJpegPairedOperationsEnabled_ = draft.rawJpegPairedOperationsEnabled;
            pairedRawJpegViewerPreference_ = draft.pairedRawJpegViewerPreference;
            defaultViewerToSecondaryMonitor_ = draft.defaultViewerToSecondaryMonitor;
            themeMode_ = draft.darkTheme ? ThemeMode::Dark : ThemeMode::Light;
            appTextSize_ = draft.appTextSize;
            thumbnailSizePreset_ = draft.thumbnailSizePreset;
            thumbnailDetailsVisible_ = draft.thumbnailDetailsVisible;
            compactThumbnailLayout_ = draft.compactThumbnailLayout;
            detailsStripVisible_ = draft.detailsStripVisible;
            resourceProfile_ = draft.resourceProfile;
            persistentThumbnailCacheEnabled_ = draft.persistentThumbnailCacheEnabled;
            thumbnailCacheCapacityOverrideBytes_ = draft.thumbnailCacheCapacityOverrideBytes;
            metadataCacheCapacityOverrideEntries_ = draft.metadataCacheCapacityOverrideEntries;
            showPressureStateInStatusBar_ = draft.showPressureStateInStatusBar;
            nvJpegEnabled_ = draft.nvJpegEnabled;
            libRawOutOfProcessEnabled_ = draft.libRawOutOfProcessEnabled;
            recursiveBrowsingEnabled_ = draft.recursiveBrowsingEnabled;
            showSubfoldersInBrowser_ = draft.showSubfoldersInBrowser;
            closeMainWindowOnEscape_ = draft.closeMainWindowOnEscape;

            app::Application::SetSingleInstanceEnabled(draft.singleInstanceEnabled);
            decode::SetNvJpegAccelerationEnabled(nvJpegEnabled_ && HasNvJpegCapability());
            ApplyTheme();
            if (thumbnailDisplayChanged)
            {
                ApplyAppTextSize();
                ApplyThumbnailDisplaySettings();
            }
            if (viewerOverlayChanged)
            {
                if (viewerWindow_ && viewerWindow_->IsOpen())
                {
                    viewerWindow_->SetInfoOverlaysVisible(draft.infoOverlaysVisible);
                    viewerWindow_->SetOverlayTextSize(draft.overlayTextSize);
                    viewerWindow_->SetFullMetadataVisible(draft.fullMetadataVisible);
                }
                else
                {
                    viewer::ViewerWindow::SetDefaultInfoOverlaysVisible(draft.infoOverlaysVisible);
                    viewer::ViewerWindow::SetDefaultOverlayTextSize(draft.overlayTextSize);
                    viewer::ViewerWindow::SetDefaultFullMetadataVisible(draft.fullMetadataVisible);
                }
            }
            ApplyViewerMouseWheelSetting();
            if (slideshowTransitionChanged)
            {
                ApplyViewerTransitionSettings();
            }
            ApplyRawJpegPairingSettings();
            ApplyResourceProfileSetting();
            ApplyCacheCapacityOverrideSettings();
            ApplyPersistentThumbnailCacheSetting();

            if (detailsStripVisible_)
            {
                UpdateDetailsPanel();
            }
            else if (detailsPanelText_)
            {
                ShowWindow(detailsPanelText_, SW_HIDE);
            }
            LayoutChildren();

            if (folderScopeChanged && browserModel_ && !browserModel_->FolderPath().empty())
            {
                LoadFolderAsync(browserModel_->FolderPath());
            }
            else if (folderScopeChanged)
            {
                RefreshBrowserPane();
            }

            if (slideshowDurationChanged && viewerWindow_ && viewerWindow_->IsSlideshowActive())
            {
                viewerWindow_->StartSlideshow(slideshowIntervalMs_);
            }
            UpdateStatusText();
            UpdateMenuState();
            UpdateWindowTitle();
            SaveWindowState();
        };

        PromptForConsolidatedSettings(hwnd_, instance_, &state);
    }

    void MainWindow::ShowSlideshowSettingsDialog()
    {
        UINT slideshowDurationMs = slideshowIntervalMs_;
        viewer::TransitionStyle transitionStyle = slideshowTransitionStyle_;
        UINT transitionDurationMs = slideshowTransitionDurationMs_;
        if (!PromptForSlideshowSettings(hwnd_,
                                        instance_,
                                        appTextSize_,
                                        slideshowIntervalMs_,
                                        slideshowTransitionStyle_,
                                        slideshowTransitionDurationMs_,
                                        &slideshowDurationMs,
                                        &transitionStyle,
                                        &transitionDurationMs))
        {
            return;
        }

        const bool slideshowDurationChanged = slideshowIntervalMs_ != slideshowDurationMs;
        const bool transitionStyleChanged = slideshowTransitionStyle_ != transitionStyle;
        const bool transitionDurationChanged = slideshowTransitionDurationMs_ != transitionDurationMs;
        if (!slideshowDurationChanged && !transitionStyleChanged && !transitionDurationChanged)
        {
            return;
        }

        slideshowIntervalMs_ = slideshowDurationMs;
        slideshowTransitionStyle_ = transitionStyle;
        slideshowTransitionDurationMs_ = transitionDurationMs;
        ApplyViewerTransitionSettings();
        if (slideshowDurationChanged && viewerWindow_ && viewerWindow_->IsSlideshowActive())
        {
            viewerWindow_->StartSlideshow(slideshowIntervalMs_);
        }
        UpdateMenuState();
    }

    void MainWindow::ShowPerformanceSettingsDialog()
    {
        const std::size_t currentThumbnailCacheCapacityBytes = browserPaneController_
            ? browserPaneController_->ThumbnailCacheCapacityBytes()
            : thumbnailCacheCapacityOverrideBytes_;
        const std::size_t currentMetadataCacheCapacityEntries = browserPaneController_
            ? browserPaneController_->MetadataCacheCapacityEntries()
            : metadataCacheCapacityOverrideEntries_;

        std::size_t thumbnailCacheCapacityOverrideBytes = thumbnailCacheCapacityOverrideBytes_;
        std::size_t metadataCacheCapacityOverrideEntries = metadataCacheCapacityOverrideEntries_;
        bool showPressureStateInStatusBar = showPressureStateInStatusBar_;
        if (!PromptForPerformanceSettings(hwnd_,
                                          instance_,
                                          appTextSize_,
                                          resourceProfile_,
                                          currentThumbnailCacheCapacityBytes,
                                          currentMetadataCacheCapacityEntries,
                                          thumbnailCacheCapacityOverrideBytes_,
                                          metadataCacheCapacityOverrideEntries_,
                                          showPressureStateInStatusBar_,
                                          &thumbnailCacheCapacityOverrideBytes,
                                          &metadataCacheCapacityOverrideEntries,
                                          &showPressureStateInStatusBar))
        {
            return;
        }

        if (thumbnailCacheCapacityOverrideBytes_ == thumbnailCacheCapacityOverrideBytes
            && metadataCacheCapacityOverrideEntries_ == metadataCacheCapacityOverrideEntries
            && showPressureStateInStatusBar_ == showPressureStateInStatusBar)
        {
            return;
        }

        thumbnailCacheCapacityOverrideBytes_ = thumbnailCacheCapacityOverrideBytes;
        metadataCacheCapacityOverrideEntries_ = metadataCacheCapacityOverrideEntries;
        showPressureStateInStatusBar_ = showPressureStateInStatusBar;
        ApplyCacheCapacityOverrideSettings();
        UpdateStatusText();
    }

    void MainWindow::LoadWindowState()
    {
        // Always start non-recursive so restoring the last folder cannot trigger an expensive drive-wide scan.
        recursiveBrowsingEnabled_ = false;
        showSubfoldersInBrowser_ = false;
        hasPersistedWindowBounds_ = false;
        persistedWindowBounds_ = {};

        HKEY key{};
        if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegistryPath, 0, KEY_READ, &key) == ERROR_SUCCESS)
        {
            DWORD value = 0;

            if (TryReadDwordValue(key, kRegistryValueLeftPaneWidth, &value))
            {
                leftPaneWidth_ = static_cast<int>(value);
            }

            if (TryReadDwordValue(key, kRegistryValueBrowserMode, &value) && value <= static_cast<DWORD>(BrowserMode::Details))
            {
                browserMode_ = static_cast<BrowserMode>(value);
            }

            if (TryReadDwordValue(key, kRegistryValueThemeMode, &value) && value <= static_cast<DWORD>(ThemeMode::Dark))
            {
                themeMode_ = static_cast<ThemeMode>(value);
            }

            if (TryReadDwordValue(key, kRegistryValueAppTextSize, &value))
            {
                appTextSize_ = util::NormalizeAppTextSize(value);
            }

            if (TryReadDwordValue(key, kRegistryValueNvJpegEnabled, &value))
            {
                nvJpegEnabled_ = value != 0;
            }

            if (TryReadDwordValue(key, kRegistryValueLibRawOutOfProcessEnabled, &value))
            {
                libRawOutOfProcessEnabled_ = value != 0;
            }

            if (TryReadDwordValue(key, kRegistryValueThumbnailSizePreset, &value))
            {
                TryParseThumbnailSizePreset(value, &thumbnailSizePreset_);
            }

            if (TryReadDwordValue(key, kRegistryValueCompactThumbnailLayout, &value))
            {
                compactThumbnailLayout_ = value != 0;
            }

            if (TryReadDwordValue(key, kRegistryValueThumbnailDetailsVisible, &value))
            {
                thumbnailDetailsVisible_ = value != 0;
            }

            if (TryReadDwordValue(key, kRegistryValueShowSubfoldersInBrowser, &value))
            {
                showSubfoldersInBrowser_ = value != 0;
            }

            TryReadStringValue(key, kRegistryValueSelectedFolderPath, &startupFolderPath_);
            TryReadStringValue(key, kRegistryValueSelectedImagePath, &startupSelectedImagePath_);

            RECT persistedWindowBounds{};
            if (TryReadPersistedWindowBounds(key, &persistedWindowBounds)
                && IsPersistedWindowBoundsValid(persistedWindowBounds, kMinWindowWidth, kMinWindowHeight))
            {
                persistedWindowBounds_ = persistedWindowBounds;
                hasPersistedWindowBounds_ = true;
            }

            std::wstring serializedPaths;
            if (TryReadStringValue(key, kRegistryValueRecentFolders, &serializedPaths))
            {
                recentFolders_ = DeserializeFolderPathList(serializedPaths, kQuickAccessFolderLimit);
            }

            if (TryReadStringValue(key, kRegistryValueRecentDestinationFolders, &serializedPaths))
            {
                recentDestinationFolders_ = DeserializeFolderPathList(serializedPaths, kQuickAccessFolderLimit);
            }

            if (TryReadStringValue(key, kRegistryValueFavoriteDestinationFolders, &serializedPaths))
            {
                favoriteDestinationFolders_ = DeserializeFolderPathList(serializedPaths, kFavoriteDestinationLimit);
            }

            TryReadStringValue(key, kRegistryValueLastQuickSendDestination, &lastQuickSendDestination_);

            SyncQuickSendModel();
            QuickSendModel::ShortcutAssignments shortcutAssignments{};
            for (std::size_t index = 0; index < shortcutAssignments.size(); ++index)
            {
                const std::wstring valueName = std::wstring(kRegistryValueQuickSendShortcutPrefix)
                    + std::to_wstring(index);
                TryReadStringValue(key, valueName.c_str(), &shortcutAssignments[index]);
            }
            quickSendModel_.SetShortcutAssignments(shortcutAssignments);
            SortFavoriteDestinationsByShortcut();

            if (TryReadDwordValue(key, kRegistryValueSortMode, &value) && value <= static_cast<DWORD>(browser::BrowserSortMode::Tags))
            {
                sortMode_ = static_cast<browser::BrowserSortMode>(value);
            }

            if (TryReadDwordValue(key, kRegistryValueSortAscending, &value))
            {
                sortAscending_ = value != 0;
            }

            if (TryReadDwordValue(key, kRegistryValueSlideshowInterval, &value)
                && value >= kSlideshowMinimumDurationMs
                && value <= kSlideshowMaximumDurationMs)
            {
                slideshowIntervalMs_ = static_cast<UINT>(value);
            }

            if (TryReadDwordValue(key, kRegistryValueSlideshowTransitionStyle, &value)
                && value <= static_cast<DWORD>(viewer::TransitionStyle::MonochromeReveal))
            {
                slideshowTransitionStyle_ = static_cast<viewer::TransitionStyle>(value);
            }

            if (TryReadDwordValue(key, kRegistryValueSlideshowTransitionDuration, &value)
                && value >= kSlideshowMinimumTransitionDurationMs
                && value <= kSlideshowMaximumTransitionDurationMs)
            {
                slideshowTransitionDurationMs_ = static_cast<UINT>(value);
            }

            if (TryReadDwordValue(key, kRegistryValueUseSlideshowTransition, &value))
            {
                useSlideshowTransition_ = value != 0;
            }

            if (TryReadDwordValue(key, kRegistryValueDetailsStripVisible, &value))
            {
                detailsStripVisible_ = value != 0;
            }

            if (TryReadDwordValue(key, kRegistryValueDetailsPanelWidth, &value))
            {
                detailsPanelWidth_ = static_cast<int>(value);
            }

            if (TryReadDwordValue(key, kRegistryValueViewerMouseWheelBehavior, &value)
                && value <= static_cast<DWORD>(viewer::MouseWheelBehavior::Navigate))
            {
                viewerMouseWheelBehavior_ = static_cast<viewer::MouseWheelBehavior>(value);
            }

            if (TryReadDwordValue(key, kRegistryValueInvertKeyboardPanning, &value))
            {
                invertKeyboardPanning_ = value != 0;
            }

            if (TryReadDwordValue(key, kRegistryValueRawJpegPairedOperationsEnabled, &value))
            {
                rawJpegPairedOperationsEnabled_ = value != 0;
            }

            if (TryReadDwordValue(key, kRegistryValuePairedRawJpegViewerPreference, &value)
                && value <= static_cast<DWORD>(browser::RawJpegDisplayPreference::Raw))
            {
                pairedRawJpegViewerPreference_ = static_cast<browser::RawJpegDisplayPreference>(value);
            }

            if (TryReadDwordValue(key, kRegistryValueDefaultViewerToSecondaryMonitor, &value))
            {
                defaultViewerToSecondaryMonitor_ = value != 0;
            }

            if (TryReadDwordValue(key, kRegistryValuePersistentThumbnailCacheEnabled, &value))
            {
                persistentThumbnailCacheEnabled_ = value != 0;
            }

            std::uint64_t qwordValue = 0;
            if (TryReadQwordValue(key, kRegistryValueThumbnailCacheCapacityOverrideBytes, &qwordValue))
            {
                thumbnailCacheCapacityOverrideBytes_ = util::SaturatingCastToSizeT(qwordValue);
            }

            if (TryReadQwordValue(key, kRegistryValueMetadataCacheCapacityOverrideEntries, &qwordValue))
            {
                metadataCacheCapacityOverrideEntries_ = util::SaturatingCastToSizeT(qwordValue);
            }

            if (TryReadDwordValue(key, kRegistryValueResourceProfile, &value))
            {
                TryParseResourceProfile(value, &resourceProfile_);
            }

            if (TryReadDwordValue(key, kRegistryValueShowPressureStateInStatusBar, &value))
            {
                showPressureStateInStatusBar_ = value != 0;
            }

            if (TryReadDwordValue(key, kRegistryValueCloseMainWindowOnEscape, &value))
            {
                closeMainWindowOnEscape_ = value != 0;
            }

            RegCloseKey(key);
        }

        leftPaneWidth_ = std::max(leftPaneWidth_, kMinLeftPaneWidth);
        detailsPanelWidth_ = std::max(detailsPanelWidth_, kDetailsPanelMinWidth);
        startupFolderPath_ = NormalizeFolderPath(std::move(startupFolderPath_));
        startupSelectedImagePath_ = NormalizeFolderPath(std::move(startupSelectedImagePath_));
        lastQuickSendDestination_ = NormalizeFolderPath(std::move(lastQuickSendDestination_));
    }

    void MainWindow::ApplyStartupLaunchPathOverride()
    {
        pendingStartupViewerPath_.clear();
        pendingStartupSelectionPath_.clear();
        if (startupLaunchPathOverride_.empty())
        {
            return;
        }

        const std::wstring launchPath = ResolveStartupPath(startupLaunchPathOverride_);
        startupLaunchPathOverride_.clear();
        if (launchPath.empty())
        {
            return;
        }

        std::error_code error;
        const fs::path resolvedPath(launchPath);
        if (fs::is_directory(resolvedPath, error) && !error)
        {
            startupFolderPath_ = NormalizeFolderPath(resolvedPath.wstring());
            return;
        }

        error.clear();
        if (!fs::is_regular_file(resolvedPath, error) || error)
        {
            return;
        }

        if (!browser::IsSupportedImageExtension(resolvedPath.extension().wstring()))
        {
            return;
        }

        const fs::path containingFolder = resolvedPath.parent_path();
        if (containingFolder.empty())
        {
            return;
        }

        startupFolderPath_ = NormalizeFolderPath(containingFolder.wstring());
        pendingStartupViewerPath_ = NormalizeFolderPath(resolvedPath.wstring());
    }

    void MainWindow::SaveWindowState() const
    {
        HKEY key{};
        DWORD disposition = 0;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegistryPath, 0, nullptr, 0, KEY_WRITE, nullptr, &key, &disposition) == ERROR_SUCCESS)
        {
            const std::wstring selectedFolderPath = browserModel_ && !browserModel_->FolderPath().empty()
                ? NormalizeFolderPath(browserModel_->FolderPath())
                : GetSelectedFolderTreePath();
            std::wstring selectedImagePath;
            if (viewerWindow_ && viewerWindow_->IsOpen())
            {
                selectedImagePath = NormalizeFolderPath(viewerWindow_->CurrentFilePath());
            }
            else if (browserPaneController_)
            {
                selectedImagePath = NormalizeFolderPath(browserPaneController_->FocusedFilePathSnapshot());
            }

            WINDOWPLACEMENT placement{};
            placement.length = sizeof(placement);
            if (hwnd_ && GetWindowPlacement(hwnd_, &placement))
            {
                const RECT& normalBounds = placement.rcNormalPosition;
                const LONG width = normalBounds.right - normalBounds.left;
                const LONG height = normalBounds.bottom - normalBounds.top;
                if (width >= kMinWindowWidth && height >= kMinWindowHeight)
                {
                    WriteDwordValue(key, kRegistryValueWindowLeft, static_cast<DWORD>(normalBounds.left));
                    WriteDwordValue(key, kRegistryValueWindowTop, static_cast<DWORD>(normalBounds.top));
                    WriteDwordValue(key, kRegistryValueWindowWidth, static_cast<DWORD>(width));
                    WriteDwordValue(key, kRegistryValueWindowHeight, static_cast<DWORD>(height));
                }
            }

            WriteDwordValue(key, kRegistryValueLeftPaneWidth, static_cast<DWORD>(std::max(leftPaneWidth_, kMinLeftPaneWidth)));
            WriteDwordValue(key, kRegistryValueBrowserMode, static_cast<DWORD>(browserMode_));
            WriteDwordValue(key, kRegistryValueThemeMode, static_cast<DWORD>(themeMode_));
            WriteDwordValue(key, kRegistryValueAppTextSize, static_cast<DWORD>(appTextSize_));
            RegDeleteValueW(key, L"RecursiveBrowsing");
            WriteDwordValue(key, kRegistryValueNvJpegEnabled, nvJpegEnabled_ ? 1UL : 0UL);
            WriteDwordValue(key, kRegistryValueLibRawOutOfProcessEnabled, libRawOutOfProcessEnabled_ ? 1UL : 0UL);
            WriteDwordValue(key, kRegistryValueThumbnailSizePreset, static_cast<DWORD>(thumbnailSizePreset_));
            WriteDwordValue(key, kRegistryValueCompactThumbnailLayout, compactThumbnailLayout_ ? 1UL : 0UL);
            WriteDwordValue(key, kRegistryValueThumbnailDetailsVisible, thumbnailDetailsVisible_ ? 1UL : 0UL);
            WriteDwordValue(key, kRegistryValueShowSubfoldersInBrowser, showSubfoldersInBrowser_ ? 1UL : 0UL);
            if (!selectedFolderPath.empty())
            {
                WriteStringValue(key, kRegistryValueSelectedFolderPath, selectedFolderPath);
            }
            if (!selectedImagePath.empty())
            {
                WriteStringValue(key, kRegistryValueSelectedImagePath, selectedImagePath);
            }
            else
            {
                RegDeleteValueW(key, kRegistryValueSelectedImagePath);
            }
            WriteStringValue(key, kRegistryValueRecentFolders, SerializeFolderPathList(recentFolders_));
            WriteStringValue(key, kRegistryValueRecentDestinationFolders, SerializeFolderPathList(recentDestinationFolders_));
            WriteStringValue(key, kRegistryValueFavoriteDestinationFolders, SerializeFolderPathList(favoriteDestinationFolders_));
            if (!lastQuickSendDestination_.empty())
            {
                WriteStringValue(key, kRegistryValueLastQuickSendDestination, lastQuickSendDestination_);
            }
            else
            {
                RegDeleteValueW(key, kRegistryValueLastQuickSendDestination);
            }
            const QuickSendModel::ShortcutAssignments& shortcutAssignments = quickSendModel_.ShortcutAssignmentsByKey();
            for (std::size_t index = 0; index < shortcutAssignments.size(); ++index)
            {
                const std::wstring valueName = std::wstring(kRegistryValueQuickSendShortcutPrefix)
                    + std::to_wstring(index);
                WriteStringValue(key, valueName.c_str(), shortcutAssignments[index]);
            }
            if (browserPaneController_)
            {
                WriteDwordValue(key, kRegistryValueSortMode, static_cast<DWORD>(browserPaneController_->GetSortMode()));
                WriteDwordValue(key, kRegistryValueSortAscending, browserPaneController_->IsSortAscending() ? 1UL : 0UL);
            }
            WriteDwordValue(key, kRegistryValueSlideshowInterval, static_cast<DWORD>(slideshowIntervalMs_));
            WriteDwordValue(key, kRegistryValueSlideshowTransitionStyle, static_cast<DWORD>(slideshowTransitionStyle_));
            WriteDwordValue(key, kRegistryValueSlideshowTransitionDuration, static_cast<DWORD>(slideshowTransitionDurationMs_));
            WriteDwordValue(key, kRegistryValueUseSlideshowTransition, useSlideshowTransition_ ? 1UL : 0UL);
            WriteDwordValue(key, kRegistryValueDetailsStripVisible, detailsStripVisible_ ? 1UL : 0UL);
            WriteDwordValue(key, kRegistryValueDetailsPanelWidth, static_cast<DWORD>(std::max(detailsPanelWidth_, kDetailsPanelMinWidth)));
            WriteDwordValue(key, kRegistryValueViewerMouseWheelBehavior, static_cast<DWORD>(viewerMouseWheelBehavior_));
            WriteDwordValue(key, kRegistryValueInvertKeyboardPanning, invertKeyboardPanning_ ? 1UL : 0UL);
            WriteDwordValue(key, kRegistryValueRawJpegPairedOperationsEnabled, rawJpegPairedOperationsEnabled_ ? 1UL : 0UL);
            WriteDwordValue(key, kRegistryValuePairedRawJpegViewerPreference, static_cast<DWORD>(pairedRawJpegViewerPreference_));
            WriteDwordValue(key, kRegistryValueDefaultViewerToSecondaryMonitor, defaultViewerToSecondaryMonitor_ ? 1UL : 0UL);
            WriteDwordValue(key, kRegistryValuePersistentThumbnailCacheEnabled, persistentThumbnailCacheEnabled_ ? 1UL : 0UL);
            WriteDwordValue(key, kRegistryValueResourceProfile, static_cast<DWORD>(resourceProfile_));
            WriteDwordValue(key, kRegistryValueShowPressureStateInStatusBar, showPressureStateInStatusBar_ ? 1UL : 0UL);
            WriteDwordValue(key, kRegistryValueCloseMainWindowOnEscape, closeMainWindowOnEscape_ ? 1UL : 0UL);
            WriteQwordValue(key, kRegistryValueThumbnailCacheCapacityOverrideBytes, static_cast<std::uint64_t>(thumbnailCacheCapacityOverrideBytes_));
            WriteQwordValue(key, kRegistryValueMetadataCacheCapacityOverrideEntries, static_cast<std::uint64_t>(metadataCacheCapacityOverrideEntries_));
            RegCloseKey(key);
        }
    }

    void MainWindow::LoadFolderAsync(std::wstring folderPath, bool historyNavigation)
    {
        if (folderPath.empty() || !browserModel_ || !folderEnumerationService_)
        {
            return;
        }

        pendingFolderReloadSelectionPaths_.clear();
        pendingFolderReloadFocusedPath_.clear();

        if (!historyNavigation)
        {
            pendingFolderHistoryNavigation_ = FolderHistoryNavigationDirection::None;
            pendingFolderHistoryTargetIndex_ = kInvalidHistoryIndex;
        }

        folderPath = NormalizeFolderPath(std::move(folderPath));

        util::LogInfo(L"Queueing folder enumeration for " + folderPath);
        if (folderEnumerationPresentationTimerId_ != 0)
        {
            KillTimer(hwnd_, folderEnumerationPresentationTimerId_);
            folderEnumerationPresentationTimerId_ = 0;
        }
        folderEnumerationPresentationPending_ = false;
        folderEnumerationFirstBatchPresented_ = false;
        if (folderWatchService_)
        {
            folderWatchService_->Stop();
            activeFolderWatchRequestId_ = 0;
        }
        browserModel_->Reset(folderPath, recursiveBrowsingEnabled_);
        if (browserPaneController_)
        {
            browserPaneController_->BeginFolderLoad();
            browserPaneController_->ClearSelection();
            browserPaneController_->SetFilterQuery({});
        }
        if (filterEdit_)
        {
            SetWindowTextW(filterEdit_, L"");
        }
        RefreshBrowserPane();
        UpdateStatusText();
        UpdateWindowTitle();
        UpdateWindow(browserPane_);
        activeEnumerationRequestId_ = folderEnumerationService_->EnumerateFolderAsync(
            hwnd_,
            std::move(folderPath),
            recursiveBrowsingEnabled_,
            showSubfoldersInBrowser_);
        folderEnumerationActive_ = true;
        UpdateStatusText();
        ShowSelectedFolderInTree();
    }

    void MainWindow::ScheduleFolderEnumerationPresentation()
    {
        folderEnumerationPresentationPending_ = true;
        if (folderEnumerationPresentationTimerId_ == 0)
        {
            folderEnumerationPresentationTimerId_ = SetTimer(
                hwnd_,
                kFolderEnumerationPresentationTimerId,
                kFolderEnumerationPresentationIntervalMs,
                nullptr);
        }
    }

    void MainWindow::FlushFolderEnumerationPresentation(bool clearStartupPathsIfNotFound)
    {
        if (folderEnumerationPresentationTimerId_ != 0)
        {
            KillTimer(hwnd_, folderEnumerationPresentationTimerId_);
            folderEnumerationPresentationTimerId_ = 0;
        }

        if (!folderEnumerationPresentationPending_ && !clearStartupPathsIfNotFound)
        {
            return;
        }

        folderEnumerationPresentationPending_ = false;
        util::ScopedTimer timer(L"MainWindow::FlushFolderEnumerationPresentation");
        RefreshBrowserPane();
        TryRestorePendingStartupSelectionPath(clearStartupPathsIfNotFound);
        TryRestorePendingFolderReloadSelection(clearStartupPathsIfNotFound);
        TryOpenPendingStartupViewerPath(clearStartupPathsIfNotFound);
        UpdateStatusText();
        UpdateWindowTitle();
    }

    LRESULT MainWindow::OnFolderEnumerationMessage(LPARAM lParam)
    {
        std::unique_ptr<services::FolderEnumerationUpdate> update(
            reinterpret_cast<services::FolderEnumerationUpdate*>(lParam));
        if (!update || !browserModel_ || update->requestId != activeEnumerationRequestId_)
        {
            return 0;
        }

        switch (update->kind)
        {
        case services::FolderEnumerationUpdateKind::Batch:
            browserModel_->AppendItems(std::move(update->items), update->totalCount, update->totalBytes);
            if (!folderEnumerationFirstBatchPresented_)
            {
                folderEnumerationFirstBatchPresented_ = true;
                folderEnumerationPresentationPending_ = true;
                FlushFolderEnumerationPresentation(false);
                return 0;
            }
            ScheduleFolderEnumerationPresentation();
            return 0;
        case services::FolderEnumerationUpdateKind::Completed:
            browserModel_->Complete();
            folderEnumerationActive_ = false;
            util::LogInfo(L"Completed folder enumeration for " + update->folderPath);
            RecordOpenedFolderHistory(update->folderPath);
            if (update->totalCount > 0)
            {
                RecordRecentFolder(update->folderPath);
            }
            if (folderWatchService_)
            {
                activeFolderWatchRequestId_ = folderWatchService_->StartWatching(hwnd_, update->folderPath, browserModel_->IsRecursive());
            }
            break;
        case services::FolderEnumerationUpdateKind::Failed:
            browserModel_->Fail(update->message);
            folderEnumerationActive_ = false;
            pendingFolderHistoryNavigation_ = FolderHistoryNavigationDirection::None;
            pendingFolderHistoryTargetIndex_ = kInvalidHistoryIndex;
            util::LogError(update->message);
            break;
        default:
            break;
        }

        FlushFolderEnumerationPresentation(true);
        return 0;
    }

    void MainWindow::TryRestorePendingStartupSelectionPath(bool clearIfNotFound)
    {
        if (pendingStartupSelectionPath_.empty() || !browserModel_ || !browserPaneController_ || !pendingStartupViewerPath_.empty())
        {
            return;
        }

        const int modelIndex = browserModel_->FindItemIndexByPath(pendingStartupSelectionPath_);
        if (modelIndex < 0)
        {
            if (clearIfNotFound)
            {
                util::LogInfo(L"Startup selected image was not found in the enumerated folder: " + pendingStartupSelectionPath_);
                pendingStartupSelectionPath_.clear();
            }
            return;
        }

        const std::wstring startupSelectionPath = pendingStartupSelectionPath_;
        pendingStartupSelectionPath_.clear();
        browserPaneController_->RestoreSelectionByFilePaths({startupSelectionPath}, startupSelectionPath);
        browserPaneController_->EnsureFocusedItemVisible();
    }

    void MainWindow::TryRestorePendingFolderReloadSelection(bool clearIfNotFound)
    {
        if (pendingFolderReloadSelectionPaths_.empty()
            || !browserModel_
            || !browserPaneController_
            || !pendingStartupViewerPath_.empty())
        {
            return;
        }

        const bool anyPathFound = std::any_of(
            pendingFolderReloadSelectionPaths_.begin(),
            pendingFolderReloadSelectionPaths_.end(),
            [&](const std::wstring& path)
            {
                return browserModel_->FindItemIndexByPath(path) >= 0;
            });
        if (!anyPathFound)
        {
            if (clearIfNotFound)
            {
                pendingFolderReloadSelectionPaths_.clear();
                pendingFolderReloadFocusedPath_.clear();
            }
            return;
        }

        std::vector<std::wstring> selectionPaths = std::move(pendingFolderReloadSelectionPaths_);
        const std::wstring focusedPath = std::move(pendingFolderReloadFocusedPath_);
        pendingFolderReloadSelectionPaths_.clear();
        pendingFolderReloadFocusedPath_.clear();
        browserPaneController_->RestoreSelectionByFilePaths(selectionPaths, focusedPath);
        browserPaneController_->EnsureFocusedItemVisible();
    }

    void MainWindow::TryOpenPendingStartupViewerPath(bool clearIfNotFound)
    {
        if (pendingStartupViewerPath_.empty() || !browserModel_ || !browserPaneController_)
        {
            return;
        }

        const int modelIndex = browserModel_->FindItemIndexByPath(pendingStartupViewerPath_);
        if (modelIndex < 0)
        {
            if (clearIfNotFound)
            {
                util::LogInfo(L"Startup launch image was not found in the enumerated folder: " + pendingStartupViewerPath_);
                pendingStartupViewerPath_.clear();
            }
            return;
        }

        const std::wstring startupViewerPath = pendingStartupViewerPath_;
        pendingStartupViewerPath_.clear();
        browserPaneController_->RestoreSelectionByFilePaths({startupViewerPath}, startupViewerPath);
        OpenItemInViewer(modelIndex, ShouldDefaultViewerToSecondaryMonitor());
    }

    LRESULT MainWindow::OnFolderTreeEnumerationMessage(LPARAM lParam)
    {
        std::unique_ptr<services::FolderTreeEnumerationUpdate> update(
            reinterpret_cast<services::FolderTreeEnumerationUpdate*>(lParam));
        if (!update)
        {
            return 0;
        }

        const auto pendingEnumerationItem = pendingFolderTreeEnumerationItems_.find(update->requestId);
        if (pendingEnumerationItem != pendingFolderTreeEnumerationItems_.end())
        {
            const HTREEITEM item = pendingEnumerationItem->second;
            pendingFolderTreeEnumerationItems_.erase(pendingEnumerationItem);
            UpdateStatusText();

            FolderTreeNodeData* nodeData = GetFolderTreeNodeData(item);
            if (!nodeData)
            {
                return 0;
            }

            switch (update->kind)
            {
            case services::FolderTreeEnumerationUpdateKind::Completed:
                if (nodeData->childEnumerationRequestId != update->requestId)
                {
                    return 0;
                }

                ApplyFolderTreeChildren(item, std::move(update->childFolders));
                ContinueSelectingFolderInTree();
                return 0;
            case services::FolderTreeEnumerationUpdateKind::Failed:
                if (nodeData->childEnumerationRequestId != update->requestId)
                {
                    return 0;
                }

                nodeData->childrenLoading = false;
                nodeData->childEnumerationRequestId = 0;
                nodeData->childrenLoaded = false;
                if (!nodeData->childrenKnown)
                {
                    nodeData->hasChildren = false;
                    UpdateFolderTreeChildrenIndicator(item);
                }
                util::LogError(update->message);
                return 0;
            default:
                return 0;
            }
        }

        const auto pendingPresenceItems = pendingFolderTreeChildPresenceItems_.find(update->requestId);
        if (pendingPresenceItems == pendingFolderTreeChildPresenceItems_.end())
        {
            return 0;
        }

        const std::vector<HTREEITEM> items = std::move(pendingPresenceItems->second);
        pendingFolderTreeChildPresenceItems_.erase(pendingPresenceItems);
        UpdateStatusText();

        switch (update->kind)
        {
        case services::FolderTreeEnumerationUpdateKind::ChildPresenceCompleted:
            for (const services::FolderTreeChild& childPresence : update->childPresenceResults)
            {
                const HTREEITEM item = FindFolderTreeItemByPath(childPresence.path);
                FolderTreeNodeData* nodeData = GetFolderTreeNodeData(item);
                if (!nodeData || nodeData->childPresenceRequestId != update->requestId)
                {
                    continue;
                }

                nodeData->childPresenceLoading = false;
                nodeData->childPresenceRequestId = 0;
                nodeData->childrenKnown = true;
                nodeData->hasChildren = childPresence.hasChildren;
                if (nodeData->hasChildren)
                {
                    AddFolderTreePlaceholder(item);
                }
                UpdateFolderTreeChildrenIndicator(item);
            }
            return 0;
        case services::FolderTreeEnumerationUpdateKind::Failed:
            for (HTREEITEM item : items)
            {
                FolderTreeNodeData* nodeData = GetFolderTreeNodeData(item);
                if (nodeData && nodeData->childPresenceRequestId == update->requestId)
                {
                    nodeData->childPresenceLoading = false;
                    nodeData->childPresenceRequestId = 0;
                }
            }
            util::LogError(update->message);
            return 0;
        default:
            return 0;
        }
    }

    LRESULT MainWindow::OnFolderWatchMessage(LPARAM lParam)
    {
        (void)lParam;
        std::unique_ptr<services::FolderWatchUpdate> update = folderWatchService_
            ? folderWatchService_->TakePendingUpdate()
            : nullptr;
        if (!update || update->requestId != activeFolderWatchRequestId_)
        {
            return 0;
        }

        ApplyFolderWatchChanges(*update);
        return 0;
    }

    LRESULT MainWindow::OnBrowserPaneStateMessage(WPARAM wParam, LPARAM lParam)
    {
        (void)lParam;
        if (reinterpret_cast<HWND>(wParam) != browserPane_)
        {
            return 0;
        }

        if (browserPaneController_)
        {
            browserPaneController_->AcknowledgeStateChangedMessage();
            sortMode_ = browserPaneController_->GetSortMode();
            sortAscending_ = browserPaneController_->IsSortAscending();
        }

        UpdateStatusText();
        UpdateMenuState();
        UpdateDetailsPanel();
        return 0;
    }

    LRESULT MainWindow::OnBrowserPaneOpenItemMessage(WPARAM wParam, LPARAM lParam)
    {
        if (reinterpret_cast<HWND>(wParam) != browserPane_)
        {
            return 0;
        }

        OpenItemInViewer(static_cast<int>(lParam), ShouldDefaultViewerToSecondaryMonitor());
        return 0;
    }

    LRESULT MainWindow::OnBrowserPaneContextMenuMessage(WPARAM wParam, LPARAM lParam)
    {
        if (reinterpret_cast<HWND>(wParam) != browserPane_)
        {
            return 0;
        }

        const POINT screenPoint{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        // Shift+right-click opens the real Windows shell context menu (with the
        // user's shell extensions); a plain right-click shows HyperBrowse's menu.
        if ((GetKeyState(VK_SHIFT) & 0x8000) != 0 && ShowShellContextMenuForSelection(screenPoint))
        {
            return 0;
        }
        ShowBrowserContextMenu(screenPoint);
        return 0;
    }

    LRESULT MainWindow::OnBrowserPaneQuickSendDragMessage(WPARAM wParam, LPARAM lParam)
    {
        (void)lParam;

        if (reinterpret_cast<HWND>(wParam) != browserPane_
            || dragMode_ != DragMode::None
            || !browserPaneController_
            || browserPaneController_->SelectedCount() == 0
            || fileOperationActive_)
        {
            return 0;
        }

        dragMode_ = DragMode::QuickAccessInternal;
        quickAccessPressedRowIndex_ = -1;
        quickAccessPressedButtonIndex_ = -1;
        internalSelectionTreeDropItem_ = nullptr;
        internalSelectionTreeDropPath_.clear();

        POINT point{};
        GetCursorPos(&point);
        ScreenToClient(hwnd_, &point);
        SetCapture(hwnd_);

        UpdateInternalSelectionDrag(point);
        return TRUE;
    }

    LRESULT MainWindow::OnBatchConvertMessage(LPARAM lParam)
    {
        std::unique_ptr<services::BatchConvertUpdate> update(
            reinterpret_cast<services::BatchConvertUpdate*>(lParam));
        if (!update || update->requestId != activeBatchConvertRequestId_)
        {
            return 0;
        }

        batchConvertCompleted_ = update->completedCount;
        batchConvertTotal_ = update->totalCount;
        batchConvertFailed_ = update->failedCount;
        batchConvertCurrentFile_ = update->currentFileName;
        batchConvertOutputFolder_ = update->outputFolder;

        if (update->finished)
        {
            batchConvertActive_ = false;
            UpdateMenuState();

            std::wstring summary;
            if (!update->message.empty())
            {
                summary = update->message;
                summary.append(L"\n\n");
            }

            summary.append(L"Converted ");
            summary.append(std::to_wstring(update->completedCount - update->failedCount));
            summary.append(L" of ");
            summary.append(std::to_wstring(update->totalCount));
            summary.append(L" image(s).");
            if (update->failedCount > 0)
            {
                summary.append(L" Failures: ");
                summary.append(std::to_wstring(update->failedCount));
                summary.append(L".");
            }

            if (update->cancelled)
            {
                NotifyLongOperationComplete(L"Batch Convert", L"Batch conversion was cancelled.");
            }
            else
            {
                NotifyLongOperationComplete(L"Batch Convert Complete", summary);
                if (update->failedCount > 0)
                {
                    MessageBoxW(hwnd_, summary.c_str(), L"Batch Convert", MB_OK | MB_ICONWARNING);
                }
            }
        }

        if (update->finished && closePending_)
        {
            PostMessageW(hwnd_, WM_CLOSE, 0, 0);
        }

        UpdateStatusText();
        return 0;
    }

    LRESULT MainWindow::OnDetailsPanelThumbnailMessage(LPARAM lParam)
    {
        std::unique_ptr<services::ThumbnailReadyUpdate> update(
            reinterpret_cast<services::ThumbnailReadyUpdate*>(lParam));
        if (!update
            || !detailsPanelThumbnailScheduler_
            || update->sessionId != detailsPanelThumbnailSessionId_
            || update->requestEpoch != detailsPanelThumbnailRequestEpoch_
            || update->modelIndex != detailsPanelHistogramModelIndex_
            || !StringsEqualInsensitive(update->cacheKey.filePath, detailsPanelHistogramPath_)
            || update->cacheKey.modifiedTimestampUtc != detailsPanelHistogramModifiedTimestampUtc_)
        {
            return 0;
        }

        detailsPanelHistogramLoading_ = false;
        if (update->success)
        {
            if (const auto thumbnail = detailsPanelThumbnailScheduler_->FindCachedThumbnail(update->cacheKey))
            {
                ApplyDetailsPanelHistogram(*thumbnail);
            }
        }
        else
        {
            detailsPanelHistogramVisible_ = false;
            detailsPanelHistogramPeak_ = 0;
        }

        LayoutChildren();
        if (hwnd_ && !IsRectEmpty(&detailsPanelRect_))
        {
            InvalidateRect(hwnd_, &detailsPanelRect_, FALSE);
        }
        return 0;
    }

    LRESULT MainWindow::OnFileOperationMessage(LPARAM lParam)
    {
        std::unique_ptr<services::FileOperationUpdate> update(
            reinterpret_cast<services::FileOperationUpdate*>(lParam));
        if (!update || update->requestId != activeFileOperationRequestId_)
        {
            return 0;
        }

        ApplyCompletedFileOperation(*update);
        ClearTaskbarProgress();
        return 0;
    }

    LRESULT MainWindow::OnViewerZoomMessage(LPARAM lParam)
    {
        viewerZoomPercent_ = static_cast<int>(lParam);
        UpdateStatusText();
        return 0;
    }

    LRESULT MainWindow::OnViewerActivityMessage(LPARAM lParam)
    {
        viewerWindowActive_ = lParam != 0;
        UpdateStatusText();
        return 0;
    }

    LRESULT MainWindow::OnViewerCurrentItemChangedMessage(WPARAM wParam)
    {
        if (!viewerWindow_
            || !viewerWindow_->IsOpen()
            || reinterpret_cast<HWND>(wParam) != viewerWindow_->Hwnd()
            || !browserPaneController_)
        {
            return 0;
        }

        const std::wstring currentPath = viewerWindow_->CurrentFilePath();
        if (currentPath.empty())
        {
            return 0;
        }

        browserPaneController_->RestoreSelectionByFilePaths({currentPath}, currentPath);
        browserPaneController_->EnsureFocusedItemVisible();
        return 0;
    }

    bool MainWindow::ChooseQuickSendDestination(services::FileOperationType operationType,
                                                 POINT popupPoint,
                                                 HWND dialogOwner,
                                                 std::wstring* destinationFolder)
    {
        if (!destinationFolder || !hwnd_)
        {
            return false;
        }

        const HWND browseOwner = dialogOwner && IsWindow(dialogOwner) ? dialogOwner : hwnd_;
        const std::vector<std::wstring> destinations = quickSendModel_.FavoriteDestinations();
        HMENU popupMenu = CreatePopupMenu();
        if (!popupMenu)
        {
            return false;
        }

        quickSendPopupInitialDownCount_ = destinations.empty() ? 0 : 1;
        if (!lastQuickSendDestination_.empty())
        {
            for (std::size_t index = 0; index < destinations.size(); ++index)
            {
                if (FolderPathsEqual(destinations[index], lastQuickSendDestination_)
                    && IsExistingDirectory(destinations[index]))
                {
                    quickSendPopupInitialDownCount_ = index + 2;
                    break;
                }
            }
        }

        AppendMenuW(popupMenu,
                    MF_STRING,
                    kQuickSendPopupBrowseCommand,
                    operationType == services::FileOperationType::Move
                        ? L"Move to..."
                        : L"Copy to...");
        AppendMenuW(popupMenu, MF_SEPARATOR, 0, nullptr);

        for (std::size_t index = 0; index < destinations.size(); ++index)
        {
            std::wstring label;
            if (const std::optional<int> assignedShortcut = quickSendModel_.ShortcutForDestination(destinations[index]))
            {
                label.push_back(L'&');
                label.push_back(QuickSendModel::ShortcutCharacter(*assignedShortcut));
                label.append(L"  ");
            }
            label.append(EscapeMenuMnemonicText(FormatFolderShortcutMenuLabel(destinations[index])));

            const UINT flags = IsExistingDirectory(destinations[index]) ? MF_ENABLED : MF_GRAYED;
            AppendMenuW(popupMenu,
                        MF_STRING | flags,
                        kQuickSendPopupDestinationBase + static_cast<UINT>(index),
                        label.c_str());
        }

        std::vector<std::unique_ptr<MenuDrawItemData>> menuDrawItems;
        PrepareMenuForOwnerDraw(popupMenu, menuDrawItems, true);
        if (menuBackgroundBrush_)
        {
            MENUINFO menuInfo{};
            menuInfo.cbSize = sizeof(menuInfo);
            menuInfo.fMask = MIM_BACKGROUND;
            menuInfo.hbrBack = menuBackgroundBrush_;
            SetMenuInfo(popupMenu, &menuInfo);
        }

        quickSendPopupActive_ = true;
        const int selectedCommand = TrackPopupMenuEx(
            popupMenu,
            TPM_RETURNCMD | TPM_RIGHTBUTTON,
            popupPoint.x,
            popupPoint.y,
            hwnd_,
            nullptr);
        quickSendPopupActive_ = false;
        quickSendPopupInitialDownCount_ = 0;
        DestroyMenu(popupMenu);
        SetForegroundWindow(browseOwner);

        if (selectedCommand == static_cast<int>(kQuickSendPopupBrowseCommand))
        {
            return ChooseFolder(destinationFolder, browseOwner) && !destinationFolder->empty();
        }

        if (selectedCommand < static_cast<int>(kQuickSendPopupDestinationBase)
            || selectedCommand >= static_cast<int>(kQuickSendPopupDestinationBase + destinations.size()))
        {
            return false;
        }

        const std::wstring& selectedDestination = destinations[
            static_cast<std::size_t>(selectedCommand - static_cast<int>(kQuickSendPopupDestinationBase))];
        if (!IsExistingDirectory(selectedDestination))
        {
            return false;
        }

        *destinationFolder = selectedDestination;
        return true;
    }

    void MainWindow::StartQuickSendForSelection(services::FileOperationType type)
    {
        if (fileOperationActive_ || !browserPaneController_ || SelectedFileOperationPathsSnapshot().empty())
        {
            return;
        }

        RECT mainRect{};
        GetClientRect(hwnd_, &mainRect);
        POINT popupPoint{
            (mainRect.left + mainRect.right) / 2,
            (mainRect.top + mainRect.bottom) / 2,
        };
        ClientToScreen(hwnd_, &popupPoint);

        std::wstring destinationFolder;
        if (ChooseQuickSendDestination(type, popupPoint, hwnd_, &destinationFolder))
        {
            StartSelectionFileOperationToDestination(type, std::move(destinationFolder));
        }
    }

    LRESULT MainWindow::OnViewerQuickSendRequest(WPARAM wParam, LPARAM lParam)
    {
        if (!viewerWindow_
            || !viewerWindow_->IsOpen()
            || reinterpret_cast<HWND>(lParam) != viewerWindow_->Hwnd()
            || fileOperationActive_
            || quickSendPopupActive_)
        {
            return 0;
        }

        services::FileOperationType operationType{};
        if (wParam == static_cast<WPARAM>(viewer::QuickSendOperation::Move))
        {
            operationType = services::FileOperationType::Move;
        }
        else if (wParam == static_cast<WPARAM>(viewer::QuickSendOperation::Copy))
        {
            operationType = services::FileOperationType::Copy;
        }
        else
        {
            return 0;
        }

        const std::wstring sourcePath = viewerWindow_->CurrentFilePath();
        if (sourcePath.empty())
        {
            return 0;
        }

        RECT viewerRect{};
        GetClientRect(viewerWindow_->Hwnd(), &viewerRect);
        POINT popupPoint{
            (viewerRect.left + viewerRect.right) / 2,
            (viewerRect.top + viewerRect.bottom) / 2,
        };
        ClientToScreen(viewerWindow_->Hwnd(), &popupPoint);

        std::wstring destinationFolder;
        if (ChooseQuickSendDestination(operationType,
                                       popupPoint,
                                       viewerWindow_->Hwnd(),
                                       &destinationFolder))
        {
            StartViewerQuickSendOperation(operationType, std::move(destinationFolder));
        }
        return 0;
    }

    bool MainWindow::StartViewerQuickSendOperation(services::FileOperationType type,
                                                    std::wstring destinationFolder)
    {
        if (!viewerWindow_ || !viewerWindow_->IsOpen() || fileOperationActive_)
        {
            return false;
        }

        const HWND viewerHwnd = viewerWindow_->Hwnd();
        const std::wstring sourcePath = NormalizeFolderPath(viewerWindow_->CurrentFilePath());
        if (sourcePath.empty())
        {
            return false;
        }

        std::vector<std::wstring> sourcePaths = ExpandRawJpegPairedPaths({sourcePath});
        if (sourcePaths.empty())
        {
            return false;
        }

        destinationFolder = NormalizeFolderPath(std::move(destinationFolder));
        if (!IsExistingDirectory(destinationFolder))
        {
            MessageBoxW(viewerHwnd,
                        L"The selected destination folder is no longer available.",
                        L"Quick Send",
                        MB_OK | MB_ICONINFORMATION);
            return false;
        }

        const std::wstring sourceParent = NormalizeFolderPath(fs::path(sourcePath).parent_path().wstring());
        if (FolderPathsEqual(sourceParent, destinationFolder))
        {
            MessageBoxW(viewerHwnd,
                        L"The selected destination is already the current folder.",
                        L"Quick Send",
                        MB_OK | MB_ICONINFORMATION);
            return false;
        }

        const services::FileConflictPlan conflictPlan = services::PlanDestinationConflicts(
            sourcePaths,
            destinationFolder,
            services::FileConflictPolicy::OverwriteExisting);
        services::FileConflictPolicy conflictPolicy = services::FileConflictPolicy::PromptShell;
        if (!PromptForFileConflictPolicy(viewerHwnd, type, conflictPlan.conflictCount, &conflictPolicy))
        {
            return false;
        }

        std::vector<std::wstring> targetLeafNames;
        if (conflictPolicy == services::FileConflictPolicy::AutoRenameNumericSuffix)
        {
            targetLeafNames = services::PlanDestinationConflicts(
                sourcePaths,
                destinationFolder,
                conflictPolicy).targetLeafNames;
        }

        PendingViewerQuickSend pending;
        pending.type = type;
        pending.sourcePath = sourcePath;
        pending.sourcePaths = sourcePaths;
        pending.destinationFolder = destinationFolder;
        pending.active = true;
        pendingViewerQuickSend_ = std::move(pending);

        const std::wstring quickSendDestination = destinationFolder;
        if (!StartFileOperation(type,
                                sourcePaths,
                                destinationFolder,
                                conflictPolicy,
                                std::move(targetLeafNames),
                                viewerHwnd))
        {
            pendingViewerQuickSend_ = {};
            return false;
        }
        lastQuickSendDestination_ = quickSendDestination;

        if (type == services::FileOperationType::Move)
        {
            pendingViewerQuickSend_.viewerAdvanced = viewerWindow_->AdvanceAfterDeleteCurrent();
        }
        return true;
    }

    LRESULT MainWindow::OnViewerDeleteRequested(WPARAM wParam)
    {
        util::LogInfo(L"MainWindow::OnViewerDeleteRequested entered");
        util::ScopedTimer functionTimer(L"MainWindow::OnViewerDeleteRequested");
        if (!viewerWindow_ || !viewerWindow_->IsOpen() || !fileOperationService_)
        {
            return 0;
        }

        std::wstring sourcePath;
        std::wstring preferredFocusPath;
        if (!viewerWindow_->GetDeleteCurrentPaths(&sourcePath, &preferredFocusPath) || sourcePath.empty())
        {
            return 0;
        }

        std::vector<std::wstring> sourcePaths;
        {
            util::ScopedTimer expandTimer(L"MainWindow::OnViewerDeleteRequested ExpandRawJpegPairedPaths");
            sourcePaths = ExpandRawJpegPairedPaths({sourcePath});
        }
        if (sourcePaths.empty())
        {
            return 0;
        }

        const bool permanentDelete = (wParam & viewer::ViewerWindow::kDeleteRequestPermanent) != 0;
        {
            util::ScopedTimer confirmTimer(L"MainWindow::OnViewerDeleteRequested ConfirmFileDeletion");
            if (ShouldConfirmDeletion(permanentDelete)
                && !ConfirmFileDeletion(hwnd_, sourcePaths.size(), permanentDelete))
            {
                return 0;
            }
        }

        // Advance the viewer immediately so the next image is visible at once,
        // regardless of how long the file operation takes.
        viewerWindow_->AdvanceAfterDeleteCurrent();

        if (fileOperationActive_)
        {
            util::LogInfo(L"MainWindow::OnViewerDeleteRequested queuing delete (file operation already active)");
            // A file operation is already running. Queue this delete so it is
            // dispatched as soon as the current operation completes.
            PendingViewerDelete queued;
            queued.sourcePath = sourcePath;
            queued.sourcePaths = sourcePaths;
            queued.preferredFocusPath = preferredFocusPath;
            queued.permanent = permanentDelete;
            pendingViewerDeletes_.push_back(std::move(queued));
            return 0;
        }

        pendingViewerDeleteSourcePath_ = std::move(sourcePath);
        pendingViewerDeleteSourcePaths_ = sourcePaths;
        pendingViewerDeletePreferredFocusPath_ = std::move(preferredFocusPath);
        {
            util::ScopedTimer fileOpTimer(L"MainWindow::OnViewerDeleteRequested StartFileOperation");
            StartFileOperation(
                permanentDelete ? services::FileOperationType::DeletePermanent
                                : services::FileOperationType::DeleteRecycleBin,
                sourcePaths,
                {},
                services::FileConflictPolicy::PromptShell,
                {});
        }
        return 0;
    }

    LRESULT MainWindow::OnViewerContextMenuCommand(WPARAM wParam)
    {
        if (!viewerWindow_ || !viewerWindow_->IsOpen())
        {
            return 0;
        }

        const std::wstring currentPath = viewerWindow_->CurrentFilePath();
        if (currentPath.empty())
        {
            return 0;
        }

        switch (static_cast<UINT>(wParam))
        {
        case viewer::ViewerWindow::kContextMenuCopyImage:
            CopySelectedImagePixelsToClipboard(currentPath);
            break;
        case viewer::ViewerWindow::kContextMenuImageInformation:
            ShowImageInformationForPath(currentPath);
            break;
        case viewer::ViewerWindow::kContextMenuSetWallpaper:
            SetDesktopWallpaperFromImageFile(currentPath);
            break;
        case viewer::ViewerWindow::kContextMenuProperties:
        {
            SHELLEXECUTEINFOW executeInfo{};
            executeInfo.cbSize = sizeof(executeInfo);
            executeInfo.fMask = SEE_MASK_INVOKEIDLIST;
            executeInfo.hwnd = hwnd_;
            executeInfo.lpVerb = L"properties";
            executeInfo.lpFile = currentPath.c_str();
            executeInfo.nShow = SW_SHOWNORMAL;
            if (ShellExecuteExW(&executeInfo) == FALSE)
            {
                MessageBoxW(hwnd_, L"Failed to open the file properties dialog.", L"Properties", MB_OK | MB_ICONERROR);
            }
            break;
        }
        default:
            break;
        }
        return 0;
    }

    LRESULT MainWindow::OnViewerDroppedFileMessage(LPARAM lParam)
    {
        std::unique_ptr<std::wstring> path(reinterpret_cast<std::wstring*>(lParam));
        if (!path || path->empty())
        {
            return 0;
        }

        OpenViewerAtPath(*path);
        return 0;
    }

    LRESULT MainWindow::OnViewerStartFolderSlideshowMessage(WPARAM wParam)
    {
        if (!viewerWindow_ || !viewerWindow_->IsOpen())
        {
            return 0;
        }

        if (reinterpret_cast<HWND>(wParam) != viewerWindow_->Hwnd())
        {
            return 0;
        }

        StartFolderSlideshow(viewerWindow_->CurrentFilePath());
        return 0;
    }

    LRESULT MainWindow::OnViewerClosedMessage()
    {
        const std::wstring viewerPath = viewerWindow_ ? NormalizeFolderPath(viewerWindow_->CurrentFilePath()) : std::wstring{};
        if (!viewerPath.empty() && browserPaneController_)
        {
            browserPaneController_->RestoreSelectionByFilePaths({viewerPath}, viewerPath);
            browserPaneController_->EnsureFocusedItemVisible();
        }
        if (browserPaneController_ && browserPaneController_->Hwnd())
        {
            SetFocus(browserPaneController_->Hwnd());
        }

        viewerWindowActive_ = false;
        viewerZoomPercent_ = 0;
        UpdateStatusText();
        UpdateMenuState();
        return 0;
    }

    LRESULT MainWindow::OnMemoryPressureSampleMessage(LPARAM lParam)
    {
        std::unique_ptr<MemoryPressureSampleResult> update(reinterpret_cast<MemoryPressureSampleResult*>(lParam));
        memoryPressureSampleQueued_ = false;
        if (!update)
        {
            return 0;
        }

        bool nextPressureActive = thumbnailMemoryPressureActive_;
        if (update->pressureDetected)
        {
            nextPressureActive = true;
            memoryPressureRecoveryClearSampleCount_ = 0;
        }
        else if (thumbnailMemoryPressureActive_ && update->recoveryCandidate)
        {
            ++memoryPressureRecoveryClearSampleCount_;
            if (memoryPressureRecoveryClearSampleCount_ >= kMemoryPressureRecoverySamplesRequired)
            {
                nextPressureActive = false;
                memoryPressureRecoveryClearSampleCount_ = 0;
            }
        }
        else
        {
            memoryPressureRecoveryClearSampleCount_ = 0;
        }

        if (nextPressureActive != thumbnailMemoryPressureActive_)
        {
            thumbnailMemoryPressureActive_ = nextPressureActive;
            ApplyThumbnailMemoryPressureState();
        }
        return 0;
    }

    bool MainWindow::HandleCommand(UINT commandId)
    {
        if (IsCommandInRange(commandId, ID_FILE_OPEN_RECENT_FOLDER_BASE, ID_FILE_OPEN_RECENT_FOLDER_LAST))
        {
            const std::size_t index = static_cast<std::size_t>(commandId - ID_FILE_OPEN_RECENT_FOLDER_BASE);
            if (index < recentFolders_.size())
            {
                const std::wstring folderPath = recentFolders_[index];
                if (!IsExistingDirectory(folderPath))
                {
                    MessageBoxW(hwnd_, L"The selected recent folder is no longer available.", L"Open Recent Folder", MB_OK | MB_ICONINFORMATION);
                }
                else
                {
                    LoadFolderAsync(folderPath);
                }
            }
            return true;
        }

        if (IsCommandInRange(commandId, ID_FILE_COPY_SELECTION_FAVORITE_BASE, ID_FILE_COPY_SELECTION_FAVORITE_LAST))
        {
            const std::size_t index = static_cast<std::size_t>(commandId - ID_FILE_COPY_SELECTION_FAVORITE_BASE);
            if (index < favoriteDestinationFolders_.size())
            {
                StartSelectionFileOperationToDestination(services::FileOperationType::Copy, favoriteDestinationFolders_[index]);
            }
            return true;
        }

        if (IsCommandInRange(commandId, ID_FILE_COPY_SELECTION_RECENT_BASE, ID_FILE_COPY_SELECTION_RECENT_LAST))
        {
            const std::vector<std::wstring> recentDestinationPaths = RecentDestinationShortcutPaths();
            const std::size_t index = static_cast<std::size_t>(commandId - ID_FILE_COPY_SELECTION_RECENT_BASE);
            if (index < recentDestinationPaths.size())
            {
                StartSelectionFileOperationToDestination(services::FileOperationType::Copy, recentDestinationPaths[index]);
            }
            return true;
        }

        if (IsCommandInRange(commandId, ID_FILE_MOVE_SELECTION_FAVORITE_BASE, ID_FILE_MOVE_SELECTION_FAVORITE_LAST))
        {
            const std::size_t index = static_cast<std::size_t>(commandId - ID_FILE_MOVE_SELECTION_FAVORITE_BASE);
            if (index < favoriteDestinationFolders_.size())
            {
                StartSelectionFileOperationToDestination(services::FileOperationType::Move, favoriteDestinationFolders_[index]);
            }
            return true;
        }

        if (IsCommandInRange(commandId, ID_FILE_MOVE_SELECTION_RECENT_BASE, ID_FILE_MOVE_SELECTION_RECENT_LAST))
        {
            const std::vector<std::wstring> recentDestinationPaths = RecentDestinationShortcutPaths();
            const std::size_t index = static_cast<std::size_t>(commandId - ID_FILE_MOVE_SELECTION_RECENT_BASE);
            if (index < recentDestinationPaths.size())
            {
                StartSelectionFileOperationToDestination(services::FileOperationType::Move, recentDestinationPaths[index]);
            }
            return true;
        }

        if (IsViewerMouseWheelBehaviorCommand(commandId))
        {
            viewerMouseWheelBehavior_ = ViewerMouseWheelBehaviorFromCommandId(commandId);
            ApplyViewerMouseWheelSetting();
            UpdateMenuState();
            return true;
        }

        if (IsAppTextSizeCommand(commandId))
        {
            appTextSize_ = AppTextSizeFromCommandId(commandId);
            ApplyAppTextSize();
            UpdateMenuState();
            return true;
        }

        if (IsViewerOverlayTextSizeCommand(commandId))
        {
            if (viewerWindow_)
            {
                viewerWindow_->SetOverlayTextSize(ViewerOverlayTextSizeFromCommandId(commandId));
                UpdateMenuState();
            }
            return true;
        }

        if (IsRatingCommand(commandId))
        {
            SetSelectionRating(RatingFromCommandId(commandId));
            return true;
        }

        switch (commandId)
        {
        case ID_FILE_OPEN_FOLDER:
            OpenFolder();
            return true;
        case ID_VIEW_NAVIGATE_BACK_FOLDER:
            NavigateBackToLastOpenedFolder();
            return true;
        case ID_VIEW_NAVIGATE_FORWARD_FOLDER:
            NavigateForwardToLastOpenedFolder();
            return true;
        case ID_FILE_TOGGLE_CURRENT_FOLDER_FAVORITE_DESTINATION:
            ToggleCurrentFolderFavoriteDestination();
            return true;
        case ID_FILE_CLEAR_FAVORITE_DESTINATIONS:
            if (ConfirmFavoriteDestinationClear(hwnd_, favoriteDestinationFolders_.size()))
            {
                ClearFavoriteDestinations();
            }
            return true;
        case ID_FILE_CLEAR_RECENT_FOLDERS:
            ClearRecentFolders();
            return true;
        case ID_FILE_CLEAR_RECENT_DESTINATIONS:
            ClearRecentDestinations();
            return true;
        case ID_FILE_EXIT:
            PostMessageW(hwnd_, WM_CLOSE, 0, 0);
            return true;
        case ID_FILE_ESCAPE:
            if (closeMainWindowOnEscape_)
            {
                PostMessageW(hwnd_, WM_CLOSE, 0, 0);
            }
            else
            {
                ShowWindow(hwnd_, SW_MINIMIZE);
            }
            return true;
        case ID_FILE_MINIMIZE:
            ShowWindow(hwnd_, SW_MINIMIZE);
            return true;
        case ID_EDIT_CLOSE_MAIN_WINDOW_ON_ESCAPE:
            closeMainWindowOnEscape_ = !closeMainWindowOnEscape_;
            ModifyMenuW(fileMenu_,
                        ID_FILE_ESCAPE,
                        MF_BYCOMMAND | MF_STRING,
                        ID_FILE_ESCAPE,
                        closeMainWindowOnEscape_ ? L"&Close\tEsc" : L"&Minimize\tEsc");
            RefreshPersistentMenuOwnerDraw();
            UpdateMenuState();
            return true;
        case ID_FILE_REFRESH_TREE:
            RefreshFolderTree();
            return true;
        case ID_FILE_OPEN_SELECTED:
            OpenItemInViewer(browserPaneController_ ? browserPaneController_->PrimarySelectedModelIndex() : -1,
                             ShouldDefaultViewerToSecondaryMonitor());
            return true;
        case ID_FILE_COMPARE_SELECTED:
            StartCompareSelected();
            return true;
        case ID_FILE_VIEW_ON_SECONDARY_MONITOR:
            OpenItemInViewer(browserPaneController_ ? browserPaneController_->PrimarySelectedModelIndex() : -1, true);
            return true;
        case ID_FILE_IMAGE_INFORMATION:
            ShowImageInformation();
            return true;
        case ID_FILE_QUICK_SEND_MOVE:
            StartQuickSendForSelection(services::FileOperationType::Move);
            return true;
        case ID_FILE_QUICK_SEND_COPY:
            StartQuickSendForSelection(services::FileOperationType::Copy);
            return true;
        case ID_FILE_COPY_SELECTION:
            StartCopySelection();
            return true;
        case ID_FILE_COPY_SELECTION_BROWSE:
            StartCopySelection();
            return true;
        case ID_FILE_RENAME_SELECTED:
            StartRenameSelectedImage();
            return true;
        case ID_FILE_BATCH_RENAME_SELECTION:
            StartBatchRenameSelection();
            return true;
        case ID_FILE_MOVE_SELECTION:
            StartMoveSelection();
            return true;
        case ID_FILE_MOVE_SELECTION_BROWSE:
            StartMoveSelection();
            return true;
        case ID_FILE_MOVE_SELECTION_TO_NEW_CHILD_FOLDER:
            StartMoveSelectionToNewChildFolder();
            return true;
        case ID_FILE_TOGGLE_PAIRED_RAW_JPEG_OPERATIONS:
            rawJpegPairedOperationsEnabled_ = !rawJpegPairedOperationsEnabled_;
            ApplyRawJpegPairingSettings();
            if (viewerWindow_ && viewerWindow_->IsOpen())
            {
                SyncViewerToBrowserModel(viewerWindow_->CurrentFilePath());
            }
            UpdateStatusText();
            UpdateMenuState();
            return true;
        case ID_FILE_DELETE_SELECTION:
            StartDeleteSelection(false);
            return true;
        case ID_FILE_DELETE_SELECTION_PERMANENT:
            StartDeleteSelection(true);
            return true;
        case ID_FILE_REVEAL_IN_EXPLORER:
            RevealSelectedInExplorer();
            return true;
        case ID_FILE_OPEN_CONTAINING_FOLDER:
            OpenSelectedContainingFolder();
            return true;
        case ID_FILE_COPY_PATH:
            CopySelectedPathsToClipboard();
            return true;
        case ID_FILE_COPY_FILES_TO_CLIPBOARD:
            CopySelectionFilesToClipboard();
            return true;
        case ID_EDIT_CUT:
            CopySelectionFilesToClipboard(true);
            return true;
        case ID_FILE_COPY_IMAGE_PIXELS:
            CopySelectedImagePixelsToClipboard();
            return true;
        case ID_FILE_PASTE_FILES:
            PasteClipboardFilesIntoCurrentFolder();
            return true;
        case ID_EDIT_UNDO:
            PerformUndo();
            return true;
        case ID_EDIT_REDO:
            PerformRedo();
            return true;
        case ID_FILE_DUPLICATE_SELECTION:
            StartDuplicateSelection();
            return true;
        case ID_FILE_SELECT_ALL:
            if (browserPaneController_)
            {
                browserPaneController_->SelectAll();
            }
            return true;
        case ID_FILE_PROPERTIES:
            ShowSelectedFileProperties();
            return true;
        case ID_FILE_EDIT_TAGS:
            EditSelectionTags();
            return true;
        case ID_FILE_ROTATE_JPEG_LEFT:
            AdjustSelectedJpegOrientation(-1);
            return true;
        case ID_FILE_ROTATE_JPEG_RIGHT:
            AdjustSelectedJpegOrientation(+1);
            return true;
        case ID_FILE_BATCH_CONVERT_SELECTION_JPEG:
            StartBatchConvert(true, services::BatchConvertFormat::Jpeg);
            return true;
        case ID_FILE_BATCH_CONVERT_SELECTION_PNG:
            StartBatchConvert(true, services::BatchConvertFormat::Png);
            return true;
        case ID_FILE_BATCH_CONVERT_SELECTION_TIFF:
            StartBatchConvert(true, services::BatchConvertFormat::Tiff);
            return true;
        case ID_FILE_BATCH_CONVERT_FOLDER_JPEG:
            StartBatchConvert(false, services::BatchConvertFormat::Jpeg);
            return true;
        case ID_FILE_BATCH_CONVERT_FOLDER_PNG:
            StartBatchConvert(false, services::BatchConvertFormat::Png);
            return true;
        case ID_FILE_BATCH_CONVERT_FOLDER_TIFF:
            StartBatchConvert(false, services::BatchConvertFormat::Tiff);
            return true;
        case ID_FILE_BATCH_CONVERT_CANCEL:
            if (batchConvertService_)
            {
                batchConvertService_->Cancel();
                batchConvertActive_ = false;
                UpdateStatusText();
                UpdateMenuState();
            }
            return true;
        case ID_VIEW_THUMBNAILS:
            SetBrowserMode(BrowserMode::Thumbnails);
            return true;
        case ID_VIEW_DETAILS:
            SetBrowserMode(BrowserMode::Details);
            return true;
        case ID_ACTION_SORT_MENU:
        {
            for (int i = 0; i < static_cast<int>(toolbarItems_.size()); ++i)
            {
                if (toolbarItems_[static_cast<std::size_t>(i)].commandId == ID_ACTION_SORT_MENU)
                {
                    ShowDropdownForItem(i);
                    break;
                }
            }
            return true;
        }
        case ID_ACTION_THUMBNAIL_SIZE_MENU:
        {
            for (int i = 0; i < static_cast<int>(toolbarItems_.size()); ++i)
            {
                if (toolbarItems_[static_cast<std::size_t>(i)].commandId == ID_ACTION_THUMBNAIL_SIZE_MENU)
                {
                    ShowDropdownForItem(i);
                    break;
                }
            }
            return true;
        }
        case ID_ACTION_THEME_MENU:
            // Theme is now toggled via menu; no dropdown button
            return true;
        case ID_VIEW_RECURSIVE:
            ToggleRecursiveBrowsing();
            return true;
        case ID_VIEW_SHOW_SUBFOLDERS:
            ToggleShowSubfoldersInBrowser();
            return true;
        case ID_VIEW_SETTINGS:
            ShowConsolidatedSettingsDialog();
            return true;
        case ID_FILE_ASSOCIATIONS:
            ShowFileAssociationsDialog();
            return true;
        case ID_VIEW_NVJPEG_ACCELERATION:
            if (HasNvJpegCapability())
            {
                nvJpegEnabled_ = !nvJpegEnabled_;
                decode::SetNvJpegAccelerationEnabled(nvJpegEnabled_);
                UpdateStatusText();
                UpdateMenuState();
            }
            return true;
        case ID_VIEW_LIBRAW_OUT_OF_PROCESS:
            if (decode::IsLibRawBuildEnabled())
            {
                libRawOutOfProcessEnabled_ = !libRawOutOfProcessEnabled_;
                decode::SetLibRawOutOfProcessEnabled(libRawOutOfProcessEnabled_);
                UpdateStatusText();
                UpdateMenuState();
            }
            return true;
        case ID_VIEW_PERSISTENT_THUMBNAIL_CACHE:
            persistentThumbnailCacheEnabled_ = !persistentThumbnailCacheEnabled_;
            ApplyPersistentThumbnailCacheSetting();
            UpdateMenuState();
            return true;
        case ID_VIEW_PERSISTENT_THUMBNAIL_CACHE_MANAGER:
            ShowPersistentThumbnailCacheDialog();
            return true;
        case ID_VIEW_SINGLE_INSTANCE:
            app::Application::SetSingleInstanceEnabled(!app::Application::IsSingleInstanceEnabled());
            UpdateMenuState();
            return true;
        case ID_VIEW_PAIRED_RAW_JPEG_PREFER_JPEG:
            pairedRawJpegViewerPreference_ = browser::RawJpegDisplayPreference::Jpeg;
            if (viewerWindow_ && viewerWindow_->IsOpen())
            {
                SyncViewerToBrowserModel(viewerWindow_->CurrentFilePath());
            }
            UpdateMenuState();
            return true;
        case ID_VIEW_PAIRED_RAW_JPEG_PREFER_RAW:
            pairedRawJpegViewerPreference_ = browser::RawJpegDisplayPreference::Raw;
            if (viewerWindow_ && viewerWindow_->IsOpen())
            {
                SyncViewerToBrowserModel(viewerWindow_->CurrentFilePath());
            }
            UpdateMenuState();
            return true;
        case ID_VIEW_DEFAULT_VIEWER_SECONDARY_MONITOR:
            defaultViewerToSecondaryMonitor_ = !defaultViewerToSecondaryMonitor_;
            UpdateMenuState();
            return true;
        case ID_VIEW_USE_SLIDESHOW_TRANSITION:
            useSlideshowTransition_ = !useSlideshowTransition_;
            ApplyViewerTransitionSettings();
            UpdateMenuState();
            return true;
        case ID_VIEW_THUMBNAIL_SIZE_96:
        case ID_VIEW_THUMBNAIL_SIZE_128:
        case ID_VIEW_THUMBNAIL_SIZE_160:
        case ID_VIEW_THUMBNAIL_SIZE_192:
        case ID_VIEW_THUMBNAIL_SIZE_256:
        case ID_VIEW_THUMBNAIL_SIZE_320:
        case ID_VIEW_THUMBNAIL_SIZE_360:
        case ID_VIEW_THUMBNAIL_SIZE_420:
        case ID_VIEW_THUMBNAIL_SIZE_480:
        case ID_VIEW_THUMBNAIL_SIZE_560:
        case ID_VIEW_THUMBNAIL_SIZE_640:
            thumbnailSizePreset_ = ThumbnailSizePresetFromCommandId(commandId);
            ApplyThumbnailDisplaySettings();
            UpdateStatusText();
            UpdateMenuState();
            return true;
        case ID_VIEW_THUMBNAIL_SIZE_INCREASE:
            StepThumbnailSize(1);
            return true;
        case ID_VIEW_THUMBNAIL_SIZE_DECREASE:
            StepThumbnailSize(-1);
            return true;
        case ID_VIEW_THUMBNAIL_DETAILS:
            thumbnailDetailsVisible_ = !thumbnailDetailsVisible_;
            ApplyThumbnailDisplaySettings();
            UpdateStatusText();
            UpdateMenuState();
            return true;
        case ID_VIEW_THUMBNAIL_LAYOUT_COMPACT:
            compactThumbnailLayout_ = !compactThumbnailLayout_;
            ApplyThumbnailDisplaySettings();
            UpdateStatusText();
            UpdateMenuState();
            return true;
        case ID_VIEW_DETAILS_STRIP:
            ToggleDetailsPanelVisibility();
            return true;
        case ID_VIEW_SORT_FILENAME:
        case ID_VIEW_SORT_MODIFIED:
        case ID_VIEW_SORT_SIZE:
        case ID_VIEW_SORT_DIMENSIONS:
        case ID_VIEW_SORT_TYPE:
        case ID_VIEW_SORT_DATETAKEN:
        case ID_VIEW_SORT_RATING:
        case ID_VIEW_SORT_TAGS:
        case ID_VIEW_SORT_RANDOM:
            if (browserPaneController_)
            {
                sortMode_ = SortModeFromCommandId(commandId);
                browserPaneController_->SetSortMode(sortMode_);
                UpdateStatusText();
                UpdateMenuState();
            }
            return true;
        case ID_VIEW_SORT_DIRECTION:
            if (browserPaneController_)
            {
                sortAscending_ = !browserPaneController_->IsSortAscending();
                browserPaneController_->SetSortAscending(sortAscending_);
                UpdateStatusText();
                UpdateMenuState();
            }
            return true;
        case ID_VIEW_THEME_LIGHT:
            SetThemeMode(ThemeMode::Light);
            return true;
        case ID_VIEW_THEME_DARK:
            SetThemeMode(ThemeMode::Dark);
            return true;
        case ID_VIEW_VIEWER_DETAIL_OVERLAYS:
            if (viewerWindow_)
            {
                viewerWindow_->SetInfoOverlaysVisible(!viewerWindow_->AreInfoOverlaysVisible());
                UpdateMenuState();
            }
            return true;
        case ID_VIEW_VIEWER_FULL_METADATA:
            if (viewerWindow_)
            {
                viewerWindow_->SetFullMetadataVisible(!viewerWindow_->IsFullMetadataVisible());
                UpdateMenuState();
            }
            return true;
        case ID_VIEW_PRESSURE_STATE_STATUS:
            showPressureStateInStatusBar_ = !showPressureStateInStatusBar_;
            UpdateStatusText();
            UpdateMenuState();
            return true;
        case ID_VIEW_SLIDESHOW_SELECTION:
            StartSlideshow(true);
            return true;
        case ID_VIEW_SLIDESHOW_FOLDER:
            StartSlideshow(false);
            return true;
        case ID_HELP_USER_GUIDE:
            ShowUserGuide();
            return true;
        case ID_HELP_KEYBOARD_SHORTCUTS:
            ShowShortcutReference();
            return true;
        case ID_HELP_ABOUT:
            ShowAboutDialog();
            return true;
        case ID_HELP_PERFORMANCE_SETTINGS:
            ShowPerformanceSettingsDialog();
            return true;
        case ID_HELP_PERFORMANCE_PROFILE_CONSERVATIVE:
        case ID_HELP_PERFORMANCE_PROFILE_BALANCED:
        case ID_HELP_PERFORMANCE_PROFILE_PERFORMANCE:
        case ID_HELP_PERFORMANCE_PROFILE_AGGRESSIVE:
        {
            const util::ResourceProfile requestedProfile = ResourceProfileFromCommandId(commandId);
            if (resourceProfile_ != requestedProfile)
            {
                resourceProfile_ = requestedProfile;
                ApplyResourceProfileSetting();
                UpdateDetailsPanel();
                UpdateStatusText();
                UpdateMenuState();
            }
            return true;
        }
        case ID_HELP_DIAGNOSTICS_SNAPSHOT:
            ShowDiagnosticsSnapshot();
            return true;
        case ID_HELP_DIAGNOSTICS_RESET:
            ResetDiagnosticsState();
            return true;
        default:
            break;
        }

        return false;
    }

    void MainWindow::SetBrowserMode(BrowserMode mode)
    {
        if (browserMode_ == mode)
        {
            return;
        }

        browserMode_ = mode;
        util::LogInfo(browserMode_ == BrowserMode::Thumbnails
            ? L"Switched shell to thumbnail mode"
            : L"Switched shell to details mode");

        if (browserPaneController_)
        {
            browserPaneController_->SetViewMode(browserMode_ == BrowserMode::Thumbnails
                ? browser::BrowserViewMode::Thumbnails
                : browser::BrowserViewMode::Details);
        }

        UpdateStatusText();
        UpdateMenuState();
        UpdateWindowTitle();
    }

    void MainWindow::StepThumbnailSize(int direction)
    {
        if (direction == 0 || browserMode_ != BrowserMode::Thumbnails || !browserPaneController_)
        {
            return;
        }

        const browser::ThumbnailSizePreset currentPreset = browserPaneController_->GetThumbnailSizePreset();
        const auto currentIterator = std::find(kThumbnailSizePresets.begin(), kThumbnailSizePresets.end(), currentPreset);
        if (currentIterator == kThumbnailSizePresets.end())
        {
            return;
        }

        std::size_t currentIndex = static_cast<std::size_t>(currentIterator - kThumbnailSizePresets.begin());
        if (direction > 0)
        {
            currentIndex = std::min(currentIndex + 1, kThumbnailSizePresets.size() - 1);
        }
        else
        {
            currentIndex = currentIndex == 0 ? 0 : currentIndex - 1;
        }

        const browser::ThumbnailSizePreset nextPreset = kThumbnailSizePresets[currentIndex];
        if (nextPreset == currentPreset)
        {
            return;
        }

        thumbnailSizePreset_ = nextPreset;
        ApplyThumbnailDisplaySettings();
        UpdateStatusText();
        UpdateMenuState();
    }

    void MainWindow::ToggleRecursiveBrowsing()
    {
        recursiveBrowsingEnabled_ = !recursiveBrowsingEnabled_;
        util::LogInfo(recursiveBrowsingEnabled_
            ? L"Enabled recursive browsing for folder enumeration"
            : L"Disabled recursive browsing for folder enumeration");
        UpdateMenuState();

        if (browserModel_ && !browserModel_->FolderPath().empty())
        {
            LoadFolderAsync(browserModel_->FolderPath());
            return;
        }

        RefreshBrowserPane();
        UpdateStatusText();
        UpdateWindowTitle();
    }

    void MainWindow::ToggleShowSubfoldersInBrowser()
    {
        showSubfoldersInBrowser_ = !showSubfoldersInBrowser_;
        util::LogInfo(showSubfoldersInBrowser_
            ? L"Enabled subfolder entries in the browser"
            : L"Disabled subfolder entries in the browser");
        UpdateMenuState();

        if (browserModel_ && !browserModel_->FolderPath().empty())
        {
            LoadFolderAsync(browserModel_->FolderPath());
            return;
        }

        RefreshBrowserPane();
        UpdateStatusText();
        UpdateWindowTitle();
    }

    void MainWindow::SetThemeMode(ThemeMode themeMode)
    {
        if (themeMode_ == themeMode)
        {
            return;
        }

        themeMode_ = themeMode;
        util::LogInfo(themeMode_ == ThemeMode::Dark
            ? L"Switched shell to dark theme"
            : L"Switched shell to light theme");
        ApplyTheme();
        UpdateStatusText();
        UpdateMenuState();
        UpdateWindowTitle();
    }

    MainWindow::ThemePalette MainWindow::GetThemePalette() const
    {
        switch (themeMode_)
        {
        case ThemeMode::Dark:
            return ThemePalette{
                RGB(24, 28, 32),
                RGB(34, 38, 43),
                RGB(234, 238, 242),
                RGB(140, 148, 158),
                RGB(96, 102, 110),
                RGB(78, 84, 92),
                RGB(29, 33, 38),
                RGB(74, 82, 92),
                RGB(21, 25, 30),
                RGB(112, 169, 227),
                RGB(47, 68, 92),
                RGB(244, 248, 252),
            };
        case ThemeMode::Light:
        default:
            return ThemePalette{
                RGB(243, 245, 248),
                RGB(255, 255, 255),
                RGB(32, 36, 40),
                RGB(128, 136, 148),
                RGB(198, 204, 212),
                RGB(210, 215, 223),
                RGB(249, 250, 252),
                RGB(210, 215, 223),
                RGB(255, 255, 255),
                RGB(54, 114, 186),
                RGB(220, 233, 247),
                RGB(25, 35, 50),
            };
        }
    }

    void MainWindow::InitToolbarItems()
    {
        toolbarItems_.clear();

        auto addIcon = [this](UINT cmdId, std::string iconName, std::wstring tip,
                              ToolbarItemKind kind = ToolbarItemKind::IconButton,
                              ToolbarAlignment align = ToolbarAlignment::Left)
        {
            ToolbarItem item;
            item.commandId = cmdId;
            item.iconName = std::move(iconName);
            item.tooltip = std::move(tip);
            item.kind = kind;
            item.alignment = align;
            toolbarItems_.push_back(std::move(item));
        };

        auto addSeparator = [this](ToolbarAlignment align = ToolbarAlignment::Left)
        {
            ToolbarItem sep;
            sep.kind = ToolbarItemKind::Separator;
            sep.alignment = align;
            toolbarItems_.push_back(std::move(sep));
        };

        // Left group 1: Navigation
        addIcon(ID_VIEW_NAVIGATE_BACK_FOLDER, "back", L"Back to Previous Folder (Backspace)");
        addIcon(ID_VIEW_NAVIGATE_FORWARD_FOLDER, "forward", L"Forward to Next Folder (Alt+Right)");
        addIcon(ID_FILE_OPEN_FOLDER, "open-folder", L"Open Folder (Ctrl+O)");
        addIcon(ID_VIEW_RECURSIVE, "recursive", L"Recursive Browsing (Ctrl+R)", ToolbarItemKind::IconToggle);

        addSeparator();

        // Left group 2: View mode
        addIcon(ID_VIEW_THUMBNAILS, "view-grid", L"Thumbnail Mode (Ctrl+1)", ToolbarItemKind::IconToggle);
        addIcon(ID_VIEW_DETAILS, "view-list", L"Details Mode (Ctrl+2)", ToolbarItemKind::IconToggle);

        addSeparator();

        // Left group 3: Display controls (dropdowns)
        addIcon(ID_ACTION_SORT_MENU, "sort", L"Sort By", ToolbarItemKind::IconDropdown);
        addIcon(ID_ACTION_THUMBNAIL_SIZE_MENU, "thumbnail-size", L"Thumbnail Size", ToolbarItemKind::IconDropdown);

        addSeparator();

        // Filter placeholder (will be positioned in LayoutToolbar)
        {
            ToolbarItem filterItem;
            filterItem.kind = ToolbarItemKind::FilterEdit;
            filterItem.alignment = ToolbarAlignment::Left;
            toolbarItems_.push_back(std::move(filterItem));
        }

        addSeparator(ToolbarAlignment::Right);

        // Right group: Selection actions
        addIcon(ID_FILE_COMPARE_SELECTED, "compare", L"Compare Selected", ToolbarItemKind::IconButton, ToolbarAlignment::Right);
        addIcon(ID_FILE_COPY_SELECTION, "copy", L"Copy Selection", ToolbarItemKind::IconButton, ToolbarAlignment::Right);
        addIcon(ID_FILE_MOVE_SELECTION, "move", L"Move Selection", ToolbarItemKind::IconButton, ToolbarAlignment::Right);
        addIcon(ID_FILE_DELETE_SELECTION, "delete", L"Delete (Del)", ToolbarItemKind::IconButton, ToolbarAlignment::Right);
    }

    void MainWindow::LayoutToolbar()
    {
        RECT client{};
        GetClientRect(hwnd_, &client);
        const int clientWidth = client.right - client.left;
        const int itemTop = kActionStripPaddingY;

        // Lay out left-aligned items first, then right-aligned items, then fill filter gap.
        int leftCursor = kActionStripPaddingX;
        int rightCursor = clientWidth - kActionStripPaddingX;
        int filterItemIndex = -1;

        const HFONT menuFont = detailsPanelSummaryFont_ ? detailsPanelSummaryFont_ : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        for (auto& button : commandBarMenuButtons_)
        {
            if (button.label.empty() || !button.menu)
            {
                button.rect = RECT{};
                continue;
            }

            const int textWidth = MeasureTextWidth(menuFont, button.label);
            const int buttonWidth = std::max(kCommandBarMenuButtonMinWidth,
                                             textWidth + (kCommandBarMenuButtonPadding * 2) + kCommandBarMenuChevronWidth + 8);
            button.rect = RECT{leftCursor, itemTop, leftCursor + buttonWidth, itemTop + kToolbarItemSize};
            leftCursor += buttonWidth + kCommandBarMenuButtonGap;
        }

        leftCursor += 8;

        // Left pass
        for (int i = 0; i < static_cast<int>(toolbarItems_.size()); ++i)
        {
            auto& item = toolbarItems_[static_cast<std::size_t>(i)];
            if (item.alignment != ToolbarAlignment::Left)
            {
                continue;
            }

            if (item.kind == ToolbarItemKind::Separator)
            {
                item.rect = RECT{leftCursor + kToolbarSeparatorGap, itemTop,
                                 leftCursor + kToolbarSeparatorGap + 1, itemTop + kToolbarItemSize};
                leftCursor += kToolbarSeparatorWidth;
                continue;
            }

            if (item.kind == ToolbarItemKind::FilterEdit)
            {
                filterItemIndex = i;
                continue;
            }

            item.rect = RECT{leftCursor, itemTop, leftCursor + kToolbarItemSize, itemTop + kToolbarItemSize};
            leftCursor += kToolbarItemSize + 2;
        }

        // Right pass (iterate in reverse so rightmost items stay rightmost)
        for (int i = static_cast<int>(toolbarItems_.size()) - 1; i >= 0; --i)
        {
            auto& item = toolbarItems_[static_cast<std::size_t>(i)];
            if (item.alignment != ToolbarAlignment::Right)
            {
                continue;
            }

            if (item.kind == ToolbarItemKind::Separator)
            {
                rightCursor -= kToolbarSeparatorWidth;
                item.rect = RECT{rightCursor + kToolbarSeparatorGap, itemTop,
                                 rightCursor + kToolbarSeparatorGap + 1, itemTop + kToolbarItemSize};
                continue;
            }

            rightCursor -= kToolbarItemSize;
            item.rect = RECT{rightCursor, itemTop, rightCursor + kToolbarItemSize, itemTop + kToolbarItemSize};
            rightCursor -= 2;
        }

        // Place filter edit control in the gap
        if (filterItemIndex >= 0 && filterEdit_)
        {
            const int filterLeft = leftCursor + 6;
            const int filterRight = rightCursor - 6;
            const int filterWidth = std::max(0, filterRight - filterLeft);
            const int filterTop = itemTop + std::max(0, (kToolbarItemSize - kToolbarFilterEditHeight) / 2);
            toolbarItems_[static_cast<std::size_t>(filterItemIndex)].rect =
                RECT{filterLeft, itemTop, filterLeft + filterWidth, itemTop + kToolbarItemSize};
            MoveWindow(filterEdit_, filterLeft + 10, filterTop, std::max(0, filterWidth - 20), kToolbarFilterEditHeight, TRUE);
        }

        // Update tooltip rects
        if (tooltipControl_)
        {
            for (int i = 0; i < static_cast<int>(toolbarItems_.size()); ++i)
            {
                TTTOOLINFOW toolInfo{};
                toolInfo.cbSize = sizeof(toolInfo);
                toolInfo.hwnd = hwnd_;
                toolInfo.uId = static_cast<UINT_PTR>(i);
                toolInfo.rect = toolbarItems_[static_cast<std::size_t>(i)].rect;
                SendMessageW(tooltipControl_, TTM_NEWTOOLRECTW, 0, reinterpret_cast<LPARAM>(&toolInfo));
            }
        }

        // Invalidate the strip area
        RECT stripRect{0, 0, clientWidth, kActionStripHeight};
        InvalidateRect(hwnd_, &stripRect, FALSE);
    }

    void MainWindow::PaintToolbar(HDC hdc, const RECT& stripRect)
    {
        const ThemePalette palette = GetThemePalette();
        HDC iconDC = toolbarIconLibrary_ ? CreateCompatibleDC(hdc) : nullptr;

        // Fill strip background
        const HBRUSH stripBrush = CreateSolidBrush(palette.actionStripBackground);
        FillRect(hdc, &stripRect, stripBrush);
        DeleteObject(stripBrush);

        // Bottom border
        const HPEN borderPen = CreatePen(PS_SOLID, 1, palette.actionStripBorder);
        const HGDIOBJ oldPen = SelectObject(hdc, borderPen);
        MoveToEx(hdc, stripRect.left, stripRect.bottom - 1, nullptr);
        LineTo(hdc, stripRect.right, stripRect.bottom - 1);
        SelectObject(hdc, oldPen);
        DeleteObject(borderPen);

        for (int index = 0; index < static_cast<int>(commandBarMenuButtons_.size()); ++index)
        {
            const CommandBarMenuButton& button = commandBarMenuButtons_[static_cast<std::size_t>(index)];
            if (IsRectEmpty(&button.rect))
            {
                continue;
            }

            const bool hot = index == commandBarHotIndex_;
            const bool pressed = index == commandBarPressedIndex_;
            RECT buttonRect = button.rect;
            InflateRect(&buttonRect, -1, -1);

            const COLORREF fillColor = pressed
                ? BlendColor(palette.actionStripBackground, palette.accent, 48)
                : (hot
                    ? BlendColor(palette.actionStripBackground, palette.text, 20)
                    : palette.actionStripBackground);
            const COLORREF borderColor = (hot || pressed)
                ? BlendColor(palette.actionStripBorder, palette.accent, 28)
                : fillColor;

            const HBRUSH buttonBrush = CreateSolidBrush(fillColor);
            const HPEN buttonPen = CreatePen(PS_SOLID, 1, borderColor);
            const HGDIOBJ oldBrush = SelectObject(hdc, buttonBrush);
            const HGDIOBJ oldButtonPen = SelectObject(hdc, buttonPen);
            RoundRect(hdc, buttonRect.left, buttonRect.top, buttonRect.right, buttonRect.bottom, 10, 10);
            SelectObject(hdc, oldButtonPen);
            SelectObject(hdc, oldBrush);
            DeleteObject(buttonPen);
            DeleteObject(buttonBrush);

            RECT textRect = buttonRect;
            textRect.left += kCommandBarMenuButtonPadding;
            textRect.right -= kCommandBarMenuButtonPadding + kCommandBarMenuChevronWidth + 4;
            render::DrawGdiText(hdc,
                                detailsPanelSummaryFont_ ? detailsPanelSummaryFont_ : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)),
                                button.label.c_str(),
                                -1,
                                textRect,
                                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS,
                                palette.text,
                                fillColor);

            const int chevronX = buttonRect.right - kCommandBarMenuButtonPadding - kCommandBarMenuChevronWidth;
            const int chevronY = buttonRect.top + ((buttonRect.bottom - buttonRect.top) - kCommandBarMenuChevronWidth) / 2;
            const HPEN chevronPen = CreatePen(PS_SOLID, 2, palette.mutedText);
            const HGDIOBJ oldChevronPen = SelectObject(hdc, chevronPen);
            MoveToEx(hdc, chevronX, chevronY + 2, nullptr);
            LineTo(hdc, chevronX + (kCommandBarMenuChevronWidth / 2), chevronY + 6);
            LineTo(hdc, chevronX + kCommandBarMenuChevronWidth, chevronY + 2);
            SelectObject(hdc, oldChevronPen);
            DeleteObject(chevronPen);
        }

        for (int i = 0; i < static_cast<int>(toolbarItems_.size()); ++i)
        {
            const auto& item = toolbarItems_[static_cast<std::size_t>(i)];

            if (item.kind == ToolbarItemKind::Separator)
            {
                const HPEN sepPen = CreatePen(PS_SOLID, 1, palette.actionStripBorder);
                const HGDIOBJ savedPen = SelectObject(hdc, sepPen);
                MoveToEx(hdc, item.rect.left, item.rect.top + 4, nullptr);
                LineTo(hdc, item.rect.left, item.rect.bottom - 4);
                SelectObject(hdc, savedPen);
                DeleteObject(sepPen);
                continue;
            }

            if (item.kind == ToolbarItemKind::FilterEdit)
            {
                // Draw filter field background
                if (filterEdit_)
                {
                    RECT filterBg = item.rect;
                    InflateRect(&filterBg, 0, -2);
                    const bool filterFocused = GetFocus() == filterEdit_;
                    const HBRUSH fieldBrush = CreateSolidBrush(palette.actionFieldBackground);
                    const HPEN fieldPen = CreatePen(PS_SOLID, 1, filterFocused ? palette.accent : palette.actionStripBorder);
                    const HGDIOBJ oldb = SelectObject(hdc, fieldBrush);
                    const HGDIOBJ oldp = SelectObject(hdc, fieldPen);
                    RoundRect(hdc, filterBg.left, filterBg.top, filterBg.right, filterBg.bottom, 14, 14);
                    SelectObject(hdc, oldp);
                    SelectObject(hdc, oldb);
                    DeleteObject(fieldPen);
                    DeleteObject(fieldBrush);

                    if (toolbarIconLibrary_ && iconDC)
                    {
                        const HBITMAP searchBitmap = toolbarIconLibrary_->GetBitmap("search", 14, palette.mutedText);
                        AlphaBlendBitmap(hdc, iconDC, searchBitmap, filterBg.left + 7, filterBg.top + 7, 14, 14);
                    }
                }
                continue;
            }

            // Determine visual state
            const bool isHot = (i == toolbarHotIndex_);
            const bool isPressed = (i == toolbarPressedIndex_);
            const bool isChecked = item.checked;
            const bool isEnabled = item.enabled;

            // Choose icon color
            COLORREF iconColor = palette.mutedText;
            if (isChecked)
            {
                iconColor = palette.accentText;
            }
            else if (!isEnabled)
            {
                iconColor = BlendColor(palette.mutedText, palette.actionStripBackground, 140);
            }

            // Draw background for hot/pressed/checked states
            if (isEnabled && (isHot || isPressed || isChecked))
            {
                RECT bgRect = item.rect;
                InflateRect(&bgRect, -1, -1);

                COLORREF bgColor;
                if (isChecked)
                {
                    bgColor = palette.accentFill;
                    if (isPressed)
                    {
                        bgColor = BlendColor(bgColor, palette.accent, 48);
                    }
                    else if (isHot)
                    {
                        bgColor = BlendColor(bgColor, palette.accent, 24);
                    }
                }
                else if (isPressed)
                {
                    bgColor = BlendColor(palette.actionStripBackground, palette.accent, 48);
                }
                else
                {
                    bgColor = BlendColor(palette.actionStripBackground, palette.text, 20);
                }

                const HBRUSH bgBrush = CreateSolidBrush(bgColor);
                const HPEN bgPen = CreatePen(PS_SOLID, 1, bgColor);
                const HGDIOBJ oldb = SelectObject(hdc, bgBrush);
                const HGDIOBJ oldp = SelectObject(hdc, bgPen);
                RoundRect(hdc, bgRect.left, bgRect.top, bgRect.right, bgRect.bottom, 10, 10);
                SelectObject(hdc, oldp);
                SelectObject(hdc, oldb);
                DeleteObject(bgPen);
                DeleteObject(bgBrush);
            }

            if (!item.iconName.empty() && toolbarIconLibrary_ && iconDC)
            {
                RECT iconRect = item.rect;
                if (item.kind == ToolbarItemKind::IconDropdown)
                {
                    iconRect.right -= kToolbarDropdownChevronSize + 2;
                }

                const int iconWidth = kToolbarIconSize;
                const int iconHeight = kToolbarIconSize;
                const int iconX = iconRect.left + ((iconRect.right - iconRect.left) - iconWidth) / 2;
                const int iconY = iconRect.top + ((iconRect.bottom - iconRect.top) - iconHeight) / 2;
                const HBITMAP iconBitmap = toolbarIconLibrary_->GetBitmap(item.iconName, kToolbarIconSize, iconColor);
                AlphaBlendBitmap(hdc, iconDC, iconBitmap, iconX, iconY, iconWidth, iconHeight);
            }

            // Draw dropdown indicator
            if (item.kind == ToolbarItemKind::IconDropdown && isEnabled && toolbarIconLibrary_ && iconDC)
            {
                RECT chevronRect = item.rect;
                const int chevronX = chevronRect.right - kToolbarDropdownChevronSize - 6;
                const int chevronY = chevronRect.top + ((chevronRect.bottom - chevronRect.top) - kToolbarDropdownChevronSize) / 2;
                const HBITMAP chevronBitmap = toolbarIconLibrary_->GetBitmap("chevron-down", kToolbarDropdownChevronSize, palette.mutedText);
                AlphaBlendBitmap(hdc, iconDC, chevronBitmap, chevronX, chevronY, kToolbarDropdownChevronSize, kToolbarDropdownChevronSize);
            }
        }

        if (iconDC)
        {
            DeleteDC(iconDC);
        }
    }

    int MainWindow::ToolbarHitTest(int x, int y) const
    {
        POINT pt{x, y};
        for (int i = 0; i < static_cast<int>(toolbarItems_.size()); ++i)
        {
            const auto& item = toolbarItems_[static_cast<std::size_t>(i)];
            if (item.kind == ToolbarItemKind::Separator || item.kind == ToolbarItemKind::FilterEdit)
            {
                continue;
            }

            if (PtInRect(&item.rect, pt))
            {
                return i;
            }
        }
        return -1;
    }

    void MainWindow::ToolbarHandleClick(int itemIndex)
    {
        if (itemIndex < 0 || itemIndex >= static_cast<int>(toolbarItems_.size()))
        {
            return;
        }

        const auto& item = toolbarItems_[static_cast<std::size_t>(itemIndex)];
        if (!item.enabled)
        {
            return;
        }

        if (item.kind == ToolbarItemKind::IconDropdown)
        {
            ShowDropdownForItem(itemIndex);
            return;
        }

        if (item.commandId != 0)
        {
            HandleCommand(item.commandId);
        }
    }

    void MainWindow::ShowDropdownForItem(int itemIndex)
    {
        if (itemIndex < 0 || itemIndex >= static_cast<int>(toolbarItems_.size()))
        {
            return;
        }

        const auto& item = toolbarItems_[static_cast<std::size_t>(itemIndex)];
        RECT itemScreenRect = item.rect;
        MapWindowPoints(hwnd_, HWND_DESKTOP, reinterpret_cast<LPPOINT>(&itemScreenRect), 2);

        switch (item.commandId)
        {
        case ID_ACTION_SORT_MENU:
        {
            HMENU menu = CreatePopupMenu();
            if (!menu) return;
            AppendMenuW(menu, MF_STRING, ID_VIEW_SORT_FILENAME, L"By Filename");
            AppendMenuW(menu, MF_STRING, ID_VIEW_SORT_MODIFIED, L"By Modified Date");
            AppendMenuW(menu, MF_STRING, ID_VIEW_SORT_SIZE, L"By File Size");
            AppendMenuW(menu, MF_STRING, ID_VIEW_SORT_DIMENSIONS, L"By Dimensions");
            AppendMenuW(menu, MF_STRING, ID_VIEW_SORT_TYPE, L"By Type");
            AppendMenuW(menu, MF_STRING, ID_VIEW_SORT_DATETAKEN, L"By Date Taken");
            AppendMenuW(menu, MF_STRING, ID_VIEW_SORT_RANDOM, L"By Random");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, ID_VIEW_SORT_DIRECTION, L"Descending");

            const bool sortAscending = browserPaneController_
                ? browserPaneController_->IsSortAscending() : true;
            const UINT checkedCommand = browserPaneController_
                ? CommandIdFromSortMode(browserPaneController_->GetSortMode())
                : ID_VIEW_SORT_FILENAME;
            CheckMenuRadioItem(menu, ID_VIEW_SORT_FILENAME, ID_VIEW_SORT_RANDOM, checkedCommand, MF_BYCOMMAND);
            CheckMenuItem(menu, ID_VIEW_SORT_DIRECTION, MF_BYCOMMAND | (sortAscending ? MF_UNCHECKED : MF_CHECKED));

            std::vector<std::unique_ptr<MenuDrawItemData>> menuDrawItems;
            PrepareMenuForOwnerDraw(menu, menuDrawItems, true);

            SetForegroundWindow(hwnd_);
            const UINT cmdId = TrackPopupMenuEx(menu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD | TPM_NONOTIFY,
                itemScreenRect.left, itemScreenRect.bottom, hwnd_, nullptr);
            DestroyMenu(menu);
            PostMessageW(hwnd_, WM_NULL, 0, 0);
            if (cmdId != 0) HandleCommand(cmdId);
            break;
        }
        case ID_ACTION_THUMBNAIL_SIZE_MENU:
        {
            if (browserMode_ != BrowserMode::Thumbnails) return;
            HMENU menu = CreatePopupMenu();
            if (!menu) return;
            AppendMenuW(menu, MF_STRING, ID_VIEW_THUMBNAIL_SIZE_96, L"96 px");
            AppendMenuW(menu, MF_STRING, ID_VIEW_THUMBNAIL_SIZE_128, L"128 px");
            AppendMenuW(menu, MF_STRING, ID_VIEW_THUMBNAIL_SIZE_160, L"160 px");
            AppendMenuW(menu, MF_STRING, ID_VIEW_THUMBNAIL_SIZE_192, L"192 px");
            AppendMenuW(menu, MF_STRING, ID_VIEW_THUMBNAIL_SIZE_256, L"256 px");
            AppendMenuW(menu, MF_STRING, ID_VIEW_THUMBNAIL_SIZE_320, L"320 px");
            AppendMenuW(menu, MF_STRING, ID_VIEW_THUMBNAIL_SIZE_360, L"360 px");
            AppendMenuW(menu, MF_STRING, ID_VIEW_THUMBNAIL_SIZE_420, L"420 px");
            AppendMenuW(menu, MF_STRING, ID_VIEW_THUMBNAIL_SIZE_480, L"480 px");
            AppendMenuW(menu, MF_STRING, ID_VIEW_THUMBNAIL_SIZE_560, L"560 px");
            AppendMenuW(menu, MF_STRING, ID_VIEW_THUMBNAIL_SIZE_640, L"640 px");

            const browser::ThumbnailSizePreset preset = browserPaneController_
                ? browserPaneController_->GetThumbnailSizePreset() : thumbnailSizePreset_;
            CheckMenuRadioItem(menu, ID_VIEW_THUMBNAIL_SIZE_96, ID_VIEW_THUMBNAIL_SIZE_640,
                CommandIdFromThumbnailSizePreset(preset), MF_BYCOMMAND);

            std::vector<std::unique_ptr<MenuDrawItemData>> menuDrawItems;
            PrepareMenuForOwnerDraw(menu, menuDrawItems, true);

            SetForegroundWindow(hwnd_);
            const UINT cmdId = TrackPopupMenuEx(menu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD | TPM_NONOTIFY,
                itemScreenRect.left, itemScreenRect.bottom, hwnd_, nullptr);
            DestroyMenu(menu);
            PostMessageW(hwnd_, WM_NULL, 0, 0);
            if (cmdId != 0) HandleCommand(cmdId);
            break;
        }
        default:
            break;
        }
    }

    void MainWindow::OnSize()
    {
        LayoutChildren();
    }

    void MainWindow::OnGetMinMaxInfo(MINMAXINFO* minMaxInfo) const
    {
        minMaxInfo->ptMinTrackSize.x = kMinWindowWidth;
        minMaxInfo->ptMinTrackSize.y = kMinWindowHeight;
    }

    bool MainWindow::IsOverSplitter(int x, int y) const
    {
        if (y < kActionStripHeight)
        {
            return false;
        }

        if (x >= leftPaneWidth_ && x < leftPaneWidth_ + kSplitterWidth)
        {
            return true;
        }

        return IsOverDetailsPanelSplitter(x, y);
    }

    bool MainWindow::IsOverDetailsPanelSplitter(int x, int y) const
    {
        if (!detailsStripVisible_ || IsRectEmpty(&detailsPanelRect_))
        {
            return false;
        }

        return y >= detailsPanelRect_.top
            && y < detailsPanelRect_.bottom
            && x >= detailsPanelRect_.left - kSplitterWidth
            && x < detailsPanelRect_.left;
    }

    void MainWindow::OnLButtonDown(int x, int y)
    {
        auto invalidateDetailsPanelTabs = [&]()
        {
            if (!IsRectEmpty(&detailsPanelTabStripRect_))
            {
                InvalidateRect(hwnd_, &detailsPanelTabStripRect_, FALSE);
            }
        };

        const int commandBarMenuHit = y < kActionStripHeight ? CommandBarMenuHitTest(x, y) : -1;
        if (commandBarKeyboardActive_ && commandBarMenuHit < 0)
        {
            DeactivateCommandBarKeyboardMode(false);
        }

        // Toolbar hit test
        if (y < kActionStripHeight)
        {
            const int menuHit = commandBarMenuHit;
            if (menuHit >= 0)
            {
                commandBarPressedIndex_ = menuHit;
                InvalidateToolbarStrip();
                SetCapture(hwnd_);
                return;
            }

            const int hit = ToolbarHitTest(x, y);
            if (hit >= 0)
            {
                toolbarPressedIndex_ = hit;
                InvalidateToolbarStrip();
                SetCapture(hwnd_);
                return;
            }
        }

        const int detailsPanelTab = HitTestDetailsPanelTab(x, y);
        if (detailsPanelTab >= 0)
        {
            detailsPanelPressedTabIndex_ = detailsPanelTab;
            SetCapture(hwnd_);
            invalidateDetailsPanelTabs();
            return;
        }

        if (HitTestDetailsPanelCloseButton(x, y) >= 0)
        {
            detailsPanelCloseButtonPressed_ = true;
            SetCapture(hwnd_);
            if (!IsRectEmpty(&detailsPanelRect_))
            {
                InvalidateRect(hwnd_, &detailsPanelRect_, FALSE);
            }
            return;
        }

        if (HitTestQuickAccessSortButton(x, y) >= 0)
        {
            quickAccessSortButtonPressed_ = true;
            SetCapture(hwnd_);
            if (!IsRectEmpty(&quickAccessDestinationPanelRect_))
            {
                InvalidateRect(hwnd_, &quickAccessDestinationPanelRect_, FALSE);
            }
            return;
        }

        const int quickAccessButton = HitTestQuickAccessDestinationButton(x, y);
        if (quickAccessButton >= 0)
        {
            quickAccessPressedButtonIndex_ = quickAccessButton;
            SetCapture(hwnd_);
            if (!IsRectEmpty(&quickAccessDestinationPanelRect_))
            {
                InvalidateRect(hwnd_, &quickAccessDestinationPanelRect_, FALSE);
            }
            return;
        }

        const int quickAccessRow = HitTestQuickAccessDestinationRow(x, y);
        if (quickAccessRow >= 0)
        {
            quickAccessPressedRowIndex_ = quickAccessRow;
            SetCapture(hwnd_);
            if (!IsRectEmpty(&quickAccessDestinationPanelRect_))
            {
                InvalidateRect(hwnd_, &quickAccessDestinationPanelRect_, FALSE);
            }
            return;
        }

        if (IsOverDetailsPanelSplitter(x, y))
        {
            dragMode_ = DragMode::DetailsSplitter;
            SetCapture(hwnd_);
            return;
        }

        if (IsOverSplitter(x, y))
        {
            dragMode_ = DragMode::LeftSplitter;
            SetCapture(hwnd_);
        }
    }

    void MainWindow::OnLButtonDoubleClick(int x, int y)
    {
        if (IsOverDetailsPanelSplitter(x, y))
        {
            detailsPanelWidth_ = kDetailsPanelPreferredWidth;
            util::LogInfo(L"Reset details and Quick Send panel to default width");
            LayoutChildren();
            return;
        }

        if (IsOverSplitter(x, y))
        {
            leftPaneWidth_ = kDefaultLeftPaneWidth;
            util::LogInfo(L"Reset splitter to default width");
            LayoutChildren();
        }
    }

    void MainWindow::OnLButtonUp()
    {
        if (treeFolderDragActive_)
        {
            FinishFolderTreeDrag(true);
            return;
        }

        if (commandBarPressedIndex_ >= 0)
        {
            const int pressedIdx = commandBarPressedIndex_;
            commandBarPressedIndex_ = -1;
            ReleaseCapture();

            POINT point{};
            GetCursorPos(&point);
            ScreenToClient(hwnd_, &point);
            const int hit = CommandBarMenuHitTest(point.x, point.y);
            commandBarHotIndex_ = commandBarKeyboardActive_
                ? (hit >= 0 ? hit : pressedIdx)
                : hit;
            InvalidateToolbarStrip();
            if (hit == pressedIdx)
            {
                OpenCommandBarMenu(pressedIdx);
            }
            return;
        }

        auto invalidateDetailsPanelTabs = [&]()
        {
            if (!IsRectEmpty(&detailsPanelTabStripRect_))
            {
                InvalidateRect(hwnd_, &detailsPanelTabStripRect_, FALSE);
            }
        };

        if (toolbarPressedIndex_ >= 0)
        {
            const int pressedIdx = toolbarPressedIndex_;
            toolbarPressedIndex_ = -1;
            ReleaseCapture();
            InvalidateToolbarStrip();

            // Confirm click: mouse must still be over the same item
            POINT pt{};
            GetCursorPos(&pt);
            ScreenToClient(hwnd_, &pt);
            if (ToolbarHitTest(pt.x, pt.y) == pressedIdx)
            {
                ToolbarHandleClick(pressedIdx);
            }
            return;
        }

        if (detailsPanelPressedTabIndex_ >= 0)
        {
            const int pressedTab = detailsPanelPressedTabIndex_;
            detailsPanelPressedTabIndex_ = -1;
            ReleaseCapture();

            POINT point{};
            GetCursorPos(&point);
            ScreenToClient(hwnd_, &point);
            const int hitTab = HitTestDetailsPanelTab(point.x, point.y);
            detailsPanelHotTabIndex_ = hitTab;
            invalidateDetailsPanelTabs();

            if (hitTab == pressedTab)
            {
                SelectRightPaneTab(hitTab == static_cast<int>(RightPaneTab::QuickSend)
                    ? RightPaneTab::QuickSend
                    : RightPaneTab::FileDetails);
            }
            return;
        }

        if (detailsPanelCloseButtonPressed_)
        {
            detailsPanelCloseButtonPressed_ = false;
            ReleaseCapture();

            POINT point{};
            GetCursorPos(&point);
            ScreenToClient(hwnd_, &point);
            detailsPanelCloseButtonHot_ = HitTestDetailsPanelCloseButton(point.x, point.y) >= 0;
            if (!IsRectEmpty(&detailsPanelRect_))
            {
                InvalidateRect(hwnd_, &detailsPanelRect_, FALSE);
            }

            if (detailsPanelCloseButtonHot_)
            {
                ToggleDetailsPanelVisibility();
            }
            return;
        }

        if (quickAccessSortButtonPressed_)
        {
            quickAccessSortButtonPressed_ = false;
            ReleaseCapture();
            if (!IsRectEmpty(&quickAccessDestinationPanelRect_))
            {
                InvalidateRect(hwnd_, &quickAccessDestinationPanelRect_, FALSE);
            }

            POINT point{};
            GetCursorPos(&point);
            ScreenToClient(hwnd_, &point);
            if (HitTestQuickAccessSortButton(point.x, point.y) >= 0)
            {
                SortFavoriteDestinationsByShortcut();
                if (hwnd_ && detailsStripVisible_)
                {
                    LayoutChildren();
                }
                UpdateMenuState();
            }
            return;
        }

        if (quickAccessPressedButtonIndex_ >= 0)
        {
            const int pressedButton = quickAccessPressedButtonIndex_;
            quickAccessPressedButtonIndex_ = -1;
            ReleaseCapture();
            if (!IsRectEmpty(&quickAccessDestinationPanelRect_))
            {
                InvalidateRect(hwnd_, &quickAccessDestinationPanelRect_, FALSE);
            }

            POINT point{};
            GetCursorPos(&point);
            ScreenToClient(hwnd_, &point);
            services::FileOperationType type = services::FileOperationType::Copy;
            const int hitButton = HitTestQuickAccessDestinationButton(point.x, point.y, &type);
            if (hitButton == pressedButton)
            {
                const std::size_t rowIndex = static_cast<std::size_t>(pressedButton / 3);
                if (rowIndex < quickAccessDestinationRows_.size())
                {
                    const QuickAccessDestinationRow& row = quickAccessDestinationRows_[rowIndex];
                    const int actionIndex = pressedButton % 3;
                    if (actionIndex == 2)
                    {
                        if (row.favorite)
                        {
                            RemoveFavoriteDestination(row.destinationPath);
                        }
                        else
                        {
                            RemoveRecentDestination(row.destinationPath);
                        }
                    }
                    else
                    {
                        StartSelectionFileOperationToDestination(type, row.destinationPath);
                    }
                }
            }
            return;
        }

        if (quickAccessPressedRowIndex_ >= 0)
        {
            const int pressedRow = quickAccessPressedRowIndex_;
            quickAccessPressedRowIndex_ = -1;
            ReleaseCapture();
            if (!IsRectEmpty(&quickAccessDestinationPanelRect_))
            {
                InvalidateRect(hwnd_, &quickAccessDestinationPanelRect_, FALSE);
            }

            POINT point{};
            GetCursorPos(&point);
            ScreenToClient(hwnd_, &point);
            const int hitRow = HitTestQuickAccessDestinationRow(point.x, point.y);
            if (hitRow == pressedRow)
            {
                const std::size_t rowIndex = static_cast<std::size_t>(pressedRow);
                if (rowIndex < quickAccessDestinationRows_.size())
                {
                    LoadFolderAsync(quickAccessDestinationRows_[rowIndex].destinationPath);
                }
            }
            return;
        }

        if (dragMode_ == DragMode::QuickAccessInternal)
        {
            POINT point{};
            GetCursorPos(&point);
            ScreenToClient(hwnd_, &point);
            UpdateInternalSelectionDrag(point);
            FinishInternalSelectionDrag(true);
            return;
        }

        if (dragMode_ != DragMode::None)
        {
            dragMode_ = DragMode::None;
            ReleaseCapture();
        }
    }

    void MainWindow::OnMouseMove(int x, int y)
    {
        if (treeFolderDragActive_)
        {
            UpdateFolderTreeDrag(POINT{x, y});
            return;
        }

        if (dragMode_ == DragMode::QuickAccessInternal)
        {
            POINT screenPoint{x, y};
            ClientToScreen(hwnd_, &screenPoint);
            RECT windowRect{};
            GetWindowRect(hwnd_, &windowRect);
            if (PtInRect(&windowRect, screenPoint) == FALSE)
            {
                StartExternalSelectionDrag();
                return;
            }

            UpdateInternalSelectionDrag(POINT{x, y});
            return;
        }

        auto invalidateDetailsPanelTabs = [&]()
        {
            if (!IsRectEmpty(&detailsPanelTabStripRect_))
            {
                InvalidateRect(hwnd_, &detailsPanelTabStripRect_, FALSE);
            }
        };

        auto invalidateQuickAccessPanel = [&]()
        {
            if (!IsRectEmpty(&quickAccessDestinationPanelRect_))
            {
                InvalidateRect(hwnd_, &quickAccessDestinationPanelRect_, FALSE);
            }
        };

        // Track mouse leave for toolbar hover reset
        if (!toolbarMouseTracking_)
        {
            TRACKMOUSEEVENT tme{};
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd_;
            TrackMouseEvent(&tme);
            toolbarMouseTracking_ = true;
        }

        // Toolbar hover
        if (y < kActionStripHeight && dragMode_ == DragMode::None)
        {
            if (detailsPanelHotTabIndex_ >= 0)
            {
                detailsPanelHotTabIndex_ = -1;
                invalidateDetailsPanelTabs();
            }
            if (detailsPanelCloseButtonHot_)
            {
                detailsPanelCloseButtonHot_ = false;
                if (!IsRectEmpty(&detailsPanelRect_))
                {
                    InvalidateRect(hwnd_, &detailsPanelRect_, FALSE);
                }
            }

            const int menuHit = CommandBarMenuHitTest(x, y);
            if (!commandBarKeyboardActive_ && menuHit != commandBarHotIndex_)
            {
                commandBarHotIndex_ = menuHit;
                InvalidateToolbarStrip();
            }

            bool quickAccessChanged = false;
            if (quickAccessHotRowIndex_ >= 0)
            {
                quickAccessHotRowIndex_ = -1;
                quickAccessChanged = true;
            }
            if (quickAccessHotButtonIndex_ >= 0)
            {
                quickAccessHotButtonIndex_ = -1;
                quickAccessChanged = true;
            }
            if (quickAccessChanged)
            {
                invalidateQuickAccessPanel();
            }

            const int hit = ToolbarHitTest(x, y);
            if (hit != toolbarHotIndex_)
            {
                toolbarHotIndex_ = hit;
                InvalidateToolbarStrip();
            }

            // Relay to tooltip
            if (tooltipControl_)
            {
                MSG msg{};
                msg.hwnd = hwnd_;
                msg.message = WM_MOUSEMOVE;
                msg.lParam = MAKELPARAM(x, y);
                SendMessageW(tooltipControl_, TTM_RELAYEVENT, 0, reinterpret_cast<LPARAM>(&msg));
            }
            return;
        }

        // Clear toolbar hover when mouse leaves strip
        if (toolbarHotIndex_ >= 0)
        {
            toolbarHotIndex_ = -1;
            InvalidateToolbarStrip();
        }
        if (!commandBarKeyboardActive_ && commandBarHotIndex_ >= 0)
        {
            commandBarHotIndex_ = -1;
            InvalidateToolbarStrip();
        }

        const int detailsPanelTabHit = HitTestDetailsPanelTab(x, y);
        if (detailsPanelTabHit != detailsPanelHotTabIndex_)
        {
            detailsPanelHotTabIndex_ = detailsPanelTabHit;
            invalidateDetailsPanelTabs();
        }

        const int detailsPanelCloseButtonHit = HitTestDetailsPanelCloseButton(x, y);
        if ((detailsPanelCloseButtonHit >= 0) != detailsPanelCloseButtonHot_)
        {
            detailsPanelCloseButtonHot_ = detailsPanelCloseButtonHit >= 0;
            if (!IsRectEmpty(&detailsPanelRect_))
            {
                InvalidateRect(hwnd_, &detailsPanelRect_, FALSE);
            }
        }

        const int quickAccessRowHit = HitTestQuickAccessDestinationRow(x, y);
        const int quickAccessHit = HitTestQuickAccessDestinationButton(x, y);
        const bool quickAccessSortHit = HitTestQuickAccessSortButton(x, y) >= 0;
        if (quickAccessRowHit != quickAccessHotRowIndex_
            || quickAccessHit != quickAccessHotButtonIndex_
            || quickAccessSortHit != quickAccessSortButtonHot_)
        {
            quickAccessHotRowIndex_ = quickAccessRowHit;
            quickAccessHotButtonIndex_ = quickAccessHit;
            quickAccessSortButtonHot_ = quickAccessSortHit;
            invalidateQuickAccessPanel();
        }

        if (dragMode_ == DragMode::LeftSplitter)
        {
            RECT client{};
            GetClientRect(hwnd_, &client);
            const int detailsSplitterWidth = detailsStripVisible_ ? kSplitterWidth : 0;
            const int visibleDetailsPanelWidth = detailsStripVisible_ ? detailsPanelWidth_ : 0;
            const int maxLeft = std::max(kMinLeftPaneWidth,
                                         static_cast<int>(client.right) - visibleDetailsPanelWidth - kMinRightPaneWidth - kSplitterWidth - detailsSplitterWidth);
            leftPaneWidth_ = std::clamp(x, kMinLeftPaneWidth, maxLeft);
            LayoutChildren();
            RedrawWindow(hwnd_, nullptr, nullptr, RDW_NOERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        }
        else if (dragMode_ == DragMode::DetailsSplitter)
        {
            RECT client{};
            GetClientRect(hwnd_, &client);
            const int maxDetailsPanelWidth = std::max(0,
                                                      static_cast<int>(client.right) - leftPaneWidth_ - kMinRightPaneWidth - (kSplitterWidth * 2));
            if (maxDetailsPanelWidth >= kDetailsPanelMinWidth)
            {
                detailsPanelWidth_ = std::clamp(static_cast<int>(client.right) - x - kSplitterWidth,
                                                kDetailsPanelMinWidth,
                                                maxDetailsPanelWidth);
            }
            LayoutChildren();
            RedrawWindow(hwnd_, nullptr, nullptr, RDW_NOERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        }
        else
        {
            SetCursor(LoadCursorW(nullptr,
                                  (IsOverSplitter(x, y)
                                      ? IDC_SIZEWE
                                      : ((detailsPanelCloseButtonHit >= 0
                                          || quickAccessSortHit
                                          || quickAccessRowHit >= 0
                                          || quickAccessHit >= 0)
                                          ? IDC_HAND
                                          : IDC_ARROW))));
        }
    }

    bool MainWindow::OnQuickAccessMouseWheel(WPARAM wParam, LPARAM lParam)
    {
        if (quickAccessDestinationRows_.empty() || IsRectEmpty(&quickAccessDestinationViewportRect_))
        {
            return false;
        }

        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ScreenToClient(hwnd_, &point);
        if (PtInRect(&quickAccessDestinationPanelRect_, point) == FALSE)
        {
            return false;
        }

        const int wheelSteps = GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
        if (wheelSteps == 0)
        {
            return true;
        }

        const QuickAccessPanelMetrics metrics = BuildQuickAccessPanelMetrics(detailsPanelSummaryFont_, detailsPanelBodyFont_);
        const int totalRowsHeight = static_cast<int>(quickAccessDestinationRows_.size()) * metrics.rowHeight
            + static_cast<int>((quickAccessDestinationRows_.size() - 1) * kQuickAccessPanelRowGap);
        const int maximumScrollOffset = std::max(
            0,
            totalRowsHeight - static_cast<int>(quickAccessDestinationViewportRect_.bottom - quickAccessDestinationViewportRect_.top));
        const int scrollStep = metrics.rowHeight + kQuickAccessPanelRowGap;
        quickAccessScrollOffset_ = std::clamp(quickAccessScrollOffset_ - (wheelSteps * scrollStep * 2),
                                              0,
                                              maximumScrollOffset);
        LayoutChildren();
        return true;
    }

    void MainWindow::OnQuickAccessScroll(WPARAM wParam)
    {
        if (!quickAccessScrollBar_)
        {
            return;
        }

        SCROLLINFO scrollInfo{};
        scrollInfo.cbSize = sizeof(scrollInfo);
        scrollInfo.fMask = SIF_ALL;
        if (GetScrollInfo(quickAccessScrollBar_, SB_CTL, &scrollInfo) == FALSE)
        {
            return;
        }

        const int maximumScrollOffset = std::max(0,
                                                  scrollInfo.nMax
                                                      - std::max(0, static_cast<int>(scrollInfo.nPage) - 1));
        const int scrollStep = BuildQuickAccessPanelMetrics(detailsPanelSummaryFont_, detailsPanelBodyFont_).rowHeight
            + kQuickAccessPanelRowGap;
        int nextOffset = quickAccessScrollOffset_;
        switch (LOWORD(wParam))
        {
        case SB_LINEUP:
            nextOffset -= scrollStep;
            break;
        case SB_LINEDOWN:
            nextOffset += scrollStep;
            break;
        case SB_PAGEUP:
            nextOffset -= static_cast<int>(scrollInfo.nPage);
            break;
        case SB_PAGEDOWN:
            nextOffset += static_cast<int>(scrollInfo.nPage);
            break;
        case SB_THUMBPOSITION:
        case SB_THUMBTRACK:
            nextOffset = scrollInfo.nTrackPos;
            break;
        case SB_TOP:
            nextOffset = 0;
            break;
        case SB_BOTTOM:
            nextOffset = maximumScrollOffset;
            break;
        default:
            return;
        }

        nextOffset = std::clamp(nextOffset, 0, maximumScrollOffset);
        if (nextOffset != quickAccessScrollOffset_)
        {
            quickAccessScrollOffset_ = nextOffset;
            LayoutChildren();
        }
    }

    LRESULT MainWindow::OnDropFiles(HDROP dropHandle)
    {
        if (!dropHandle)
        {
            return 0;
        }

        POINT dropPoint{};
        DragQueryPoint(dropHandle, &dropPoint);
        const int rowIndex = HitTestQuickAccessDestinationRow(dropPoint.x, dropPoint.y);
        std::vector<std::wstring> sourcePaths = CollectShellDropPaths(dropHandle);
        DragFinish(dropHandle);

        if (rowIndex < 0
            || rowIndex >= static_cast<int>(quickAccessDestinationRows_.size())
            || sourcePaths.empty()
            || fileOperationActive_)
        {
            return 0;
        }

        std::wstring destinationFolder = NormalizeFolderPath(quickAccessDestinationRows_[static_cast<std::size_t>(rowIndex)].destinationPath);
        if (!IsExistingDirectory(destinationFolder))
        {
            MessageBoxW(hwnd_,
                        L"The selected destination folder is no longer available.",
                        L"Quick Send",
                        MB_OK | MB_ICONINFORMATION);
            return 0;
        }

        const services::FileOperationType type = ResolveQuickAccessDropOperationType(sourcePaths, destinationFolder);
        StartFileOperation(type,
                           std::move(sourcePaths),
                           std::move(destinationFolder),
                           services::FileConflictPolicy::PromptShell,
                           {});
        return 0;
    }

    std::vector<std::wstring> MainWindow::ShellPathsFromDataObject(IDataObject* dataObject) const
    {
        return CollectShellDropPathsFromDataObject(dataObject);
    }

    DWORD MainWindow::DropEffectForKeyState(DWORD keyState,
                                            const std::wstring& destinationFolder,
                                            const std::vector<std::wstring>& sourcePaths) const
    {
        if (destinationFolder.empty() || sourcePaths.empty())
        {
            return DROPEFFECT_NONE;
        }

        if ((keyState & MK_CONTROL) != 0)
        {
            return DROPEFFECT_COPY;
        }
        if ((keyState & MK_SHIFT) != 0)
        {
            return DROPEFFECT_MOVE;
        }
        return AreAllSourcePathsOnSameDrive(sourcePaths, destinationFolder) ? DROPEFFECT_MOVE : DROPEFFECT_COPY;
    }

    std::wstring MainWindow::ResolveExternalDropTarget(POINT clientPoint, HTREEITEM* treeItemOut) const
    {
        if (treeItemOut)
        {
            *treeItemOut = nullptr;
        }

        // Quick-access destination row first (screen-space panel on the right).
        const int rowIndex = HitTestQuickAccessDestinationRow(clientPoint.x, clientPoint.y);
        if (rowIndex >= 0 && rowIndex < static_cast<int>(quickAccessDestinationRows_.size()))
        {
            return NormalizeFolderPath(quickAccessDestinationRows_[static_cast<std::size_t>(rowIndex)].destinationPath);
        }

        // Then the folder tree.
        if (treePane_)
        {
            RECT treeRect{};
            GetWindowRect(treePane_, &treeRect);
            POINT screenPoint = clientPoint;
            ClientToScreen(hwnd_, &screenPoint);
            if (PtInRect(&treeRect, screenPoint) != FALSE)
            {
                POINT treePoint = screenPoint;
                ScreenToClient(treePane_, &treePoint);
                TVHITTESTINFO hitTest{};
                hitTest.pt = treePoint;
                if (HTREEITEM item = TreeView_HitTest(treePane_, &hitTest))
                {
                    if ((hitTest.flags & TVHT_ONITEM) != 0)
                    {
                        if (const FolderTreeNodeData* nodeData = GetFolderTreeNodeData(item))
                        {
                            if (!nodeData->path.empty())
                            {
                                if (treeItemOut)
                                {
                                    *treeItemOut = item;
                                }
                                return NormalizeFolderPath(nodeData->path);
                            }
                        }
                    }
                }
            }
            // Dropping anywhere else in the tree pane targets the current folder.
            if (browserModel_ && !browserModel_->FolderPath().empty())
            {
                return browserModel_->FolderPath();
            }
            return {};
        }

        // Browser/details area: drop into the currently open folder.
        if (browserModel_ && !browserModel_->FolderPath().empty())
        {
            return browserModel_->FolderPath();
        }
        return {};
    }

    void MainWindow::ClearExternalDropVisuals()
    {
        if (externalDropTreeHoverItem_ && treePane_)
        {
            TreeView_SelectDropTarget(treePane_, nullptr);
        }
        externalDropTreeHoverItem_ = nullptr;
        if (!IsRectEmpty(&quickAccessDestinationPanelRect_))
        {
            InvalidateRect(hwnd_, &quickAccessDestinationPanelRect_, FALSE);
        }
    }

    void MainWindow::HandleExternalDrop(IDataObject* dataObject, DWORD effect, POINT clientPoint)
    {
        std::vector<std::wstring> sourcePaths = ShellPathsFromDataObject(dataObject);
        HTREEITEM treeItem = nullptr;
        const std::wstring destinationFolder = ResolveExternalDropTarget(clientPoint, &treeItem);
        ClearExternalDropVisuals();

        if (sourcePaths.empty() || destinationFolder.empty() || fileOperationActive_)
        {
            return;
        }

        if (!IsExistingDirectory(destinationFolder))
        {
            MessageBoxW(hwnd_,
                        L"The drop destination folder is no longer available.",
                        L"Drop Files",
                        MB_OK | MB_ICONINFORMATION);
            return;
        }

        const services::FileOperationType type = (effect & DROPEFFECT_MOVE) != 0
            ? services::FileOperationType::Move
            : services::FileOperationType::Copy;
        StartFileOperation(type,
                           std::move(sourcePaths),
                           std::move(destinationFolder),
                           services::FileConflictPolicy::PromptShell,
                           {});
    }

    LRESULT MainWindow::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam)
    {
        switch (message)
        {
        case WM_CLOSE:
            if (fileOperationActive_ || batchConvertActive_)
            {
                closePending_ = true;
                if (fileOperationActive_ && fileOperationService_)
                {
                    fileOperationService_->Cancel();
                }
                if (batchConvertActive_ && batchConvertService_)
                {
                    batchConvertService_->Cancel();
                }
                activeFileOperationLabel_ = fileOperationActive_
                    ? L"Cancelling file operation"
                    : L"Cancelling batch conversion";
                UpdateStatusText();
                UpdateMenuState();
                return 0;
            }
            return DefWindowProcW(hwnd_, message, wParam, lParam);
        case WM_GETMINMAXINFO:
            OnGetMinMaxInfo(reinterpret_cast<MINMAXINFO*>(lParam));
            return 0;
        case WM_SIZE:
            if (wParam != SIZE_MINIMIZED)
            {
                OnSize();
            }
            return 0;
        case WM_APP + 1:
            LayoutChildren();
            return 0;
        case WM_DPICHANGED:
        {
            const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
            SetWindowPos(hwnd_, nullptr,
                         suggested->left, suggested->top,
                         suggested->right - suggested->left,
                         suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            ApplyTheme();
            return 0;
        }
        case WM_DISPLAYCHANGE:
            HandleDisplaySurfaceChange();
            return 0;
        case WM_SETTINGCHANGE:
            if (lParam == 0
                || _wcsicmp(reinterpret_cast<const wchar_t*>(lParam), L"ShellState") == 0)
            {
                RefreshFolderTree();
            }
            return 0;
        case WM_WTSSESSION_CHANGE:
            switch (wParam)
            {
            case WTS_CONSOLE_CONNECT:
            case WTS_SESSION_UNLOCK:
            case WTS_SESSION_REMOTE_CONTROL:
                HandleDisplaySurfaceChange();
                break;
            default:
                break;
            }
            return 0;
        case WM_POWERBROADCAST:
            if (wParam == PBT_APMRESUMEAUTOMATIC || wParam == PBT_APMRESUMESUSPEND)
            {
                HandleDisplaySurfaceChange();
                return TRUE;
            }
            if (wParam == PBT_POWERSETTINGCHANGE)
            {
                const auto* setting = reinterpret_cast<POWERBROADCAST_SETTING*>(lParam);
                if (setting
                    && (IsEqualGUID(setting->PowerSetting, kConsoleDisplayStateGuid)
                        || IsEqualGUID(setting->PowerSetting, kMonitorPowerOnGuid))
                    && setting->DataLength >= sizeof(DWORD)
                    && *reinterpret_cast<const DWORD*>(setting->Data) != 0)
                {
                    HandleDisplaySurfaceChange();
                }
                return TRUE;
            }
            break;
        case WM_ACTIVATE:
            if (LOWORD(wParam) == WA_INACTIVE && commandBarKeyboardActive_)
            {
                DeactivateCommandBarKeyboardMode(false);
            }
            if (LOWORD(wParam) != WA_INACTIVE && !IsIconic(hwnd_))
            {
                LayoutChildren();
            }
            RedrawWindow(hwnd_, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
            break;
        case WM_SYSCOMMAND:
            if ((wParam & 0xFFF0) == SC_MONITORPOWER && lParam == static_cast<LPARAM>(-1))
            {
                HandleDisplaySurfaceChange();
            }
            break;
        case WM_ENTERMENULOOP:
            menuLoopActive_ = true;
            break;
        case WM_EXITMENULOOP:
            menuLoopActive_ = false;
            if (menuStateRefreshPending_ && !menuStateRefreshPosted_)
            {
                menuStateRefreshPosted_ = PostMessageW(hwnd_, kDeferredMenuStateMessage, 0, 0) != FALSE;
            }
            break;
        case WM_INITMENUPOPUP:
            if (quickSendPopupActive_ && HIWORD(lParam) == FALSE)
            {
                const HWND popupWindow = FindPopupMenuWindow(reinterpret_cast<HMENU>(lParam));
                for (std::size_t index = 0; popupWindow && index < quickSendPopupInitialDownCount_; ++index)
                {
                    PostMessageW(popupWindow, WM_KEYDOWN, VK_DOWN, 0);
                    PostMessageW(popupWindow, WM_KEYUP, VK_DOWN, 0);
                }
            }
            break;
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
        case WM_SYSCHAR:
            if (treeFolderDragActive_ && (message == WM_KEYDOWN || message == WM_SYSKEYDOWN) && wParam == VK_ESCAPE)
            {
                FinishFolderTreeDrag(false);
                return 0;
            }
            if (dragMode_ == DragMode::QuickAccessInternal
                && (message == WM_KEYDOWN || message == WM_SYSKEYDOWN)
                && wParam == VK_ESCAPE)
            {
                FinishInternalSelectionDrag(false);
                return 0;
            }
            if (HandleCommandBarKeyboardInput(message, wParam, lParam))
            {
                return 0;
            }
            break;
        case WM_LBUTTONDOWN:
            OnLButtonDown(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        case WM_PARENTNOTIFY:
            if (commandBarKeyboardActive_)
            {
                switch (LOWORD(wParam))
                {
                case WM_LBUTTONDOWN:
                case WM_RBUTTONDOWN:
                case WM_MBUTTONDOWN:
                case WM_XBUTTONDOWN:
                    DeactivateCommandBarKeyboardMode(false);
                    break;
                default:
                    break;
                }
            }
            break;
        case WM_LBUTTONDBLCLK:
            OnLButtonDoubleClick(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        case WM_LBUTTONUP:
            OnLButtonUp();
            return 0;
        case WM_MOUSEMOVE:
            OnMouseMove(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        case WM_MOUSEWHEEL:
            if (OnQuickAccessMouseWheel(wParam, lParam))
            {
                return 0;
            }
            break;
        case WM_VSCROLL:
            if (reinterpret_cast<HWND>(lParam) == quickAccessScrollBar_)
            {
                OnQuickAccessScroll(wParam);
                return 0;
            }
            break;
        case WM_CAPTURECHANGED:
            if (treeFolderDragActive_)
            {
                FinishFolderTreeDrag(false);
                return 0;
            }
            if (dragMode_ == DragMode::QuickAccessInternal)
            {
                FinishInternalSelectionDrag(false);
                return 0;
            }
            break;
        case WM_CANCELMODE:
            if (treeFolderDragActive_)
            {
                FinishFolderTreeDrag(false);
                return 0;
            }
            if (dragMode_ == DragMode::QuickAccessInternal)
            {
                FinishInternalSelectionDrag(false);
                return 0;
            }
            break;
        case WM_DROPFILES:
            return OnDropFiles(reinterpret_cast<HDROP>(wParam));
        case kExternalLaunchMessage:
        {
            std::unique_ptr<std::wstring> path(reinterpret_cast<std::wstring*>(lParam));
            if (path)
            {
                HandleExternalLaunchPath(*path);
            }
            return 0;
        }
        case WM_SETCURSOR:
        {
            POINT point{};
            GetCursorPos(&point);
            ScreenToClient(hwnd_, &point);
            if (treeFolderDragActive_)
            {
                SetCursor(LoadCursorW(nullptr, treeFolderDropAllowed_ ? IDC_HAND : IDC_NO));
                return TRUE;
            }
            if (dragMode_ == DragMode::QuickAccessInternal)
            {
                SetCursor(LoadCursorW(nullptr,
                                      (quickAccessHotRowIndex_ >= 0 || internalSelectionTreeDropItem_ != nullptr)
                                          ? IDC_HAND
                                          : IDC_NO));
                return TRUE;
            }
            if (dragMode_ == DragMode::LeftSplitter || dragMode_ == DragMode::DetailsSplitter || IsOverSplitter(point.x, point.y))
            {
                SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
                return TRUE;
            }
            if (HitTestQuickAccessDestinationRow(point.x, point.y) >= 0)
            {
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
                return TRUE;
            }
            if (HitTestQuickAccessSortButton(point.x, point.y) >= 0)
            {
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
                return TRUE;
            }
            if (HitTestQuickAccessDestinationButton(point.x, point.y) >= 0)
            {
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
                return TRUE;
            }
            if (folderEnumerationActive_
                || !pendingFolderTreeEnumerationItems_.empty()
                || !pendingFolderTreeChildPresenceItems_.empty())
            {
                SetCursor(LoadCursorW(nullptr, IDC_APPSTARTING));
                return TRUE;
            }
            break;
        }
        case services::FolderEnumerationService::kMessageId:
            return OnFolderEnumerationMessage(lParam);
        case services::FolderTreeEnumerationService::kMessageId:
            return OnFolderTreeEnumerationMessage(lParam);
        case services::FolderWatchService::kMessageId:
            return OnFolderWatchMessage(lParam);
        case browser::BrowserPane::kStateChangedMessage:
            return OnBrowserPaneStateMessage(wParam, lParam);
        case browser::BrowserPane::kOpenItemMessage:
            return OnBrowserPaneOpenItemMessage(wParam, lParam);
        case browser::BrowserPane::kContextMenuMessage:
            return OnBrowserPaneContextMenuMessage(wParam, lParam);
        case browser::BrowserPane::kQuickSendDragMessage:
            return OnBrowserPaneQuickSendDragMessage(wParam, lParam);
        case services::BatchConvertService::kMessageId:
            return OnBatchConvertMessage(lParam);
        case services::FileOperationService::kMessageId:
            return OnFileOperationMessage(lParam);
        case services::FileOperationService::kProgressMessageId:
            return OnFileOperationProgressMessage(lParam);
        case services::ThumbnailScheduler::kMessageId:
            return OnDetailsPanelThumbnailMessage(lParam);
        case viewer::ViewerWindow::kZoomChangedMessage:
            return OnViewerZoomMessage(lParam);
        case viewer::ViewerWindow::kActivityChangedMessage:
            return OnViewerActivityMessage(lParam);
        case viewer::ViewerWindow::kCurrentItemChangedMessage:
            return OnViewerCurrentItemChangedMessage(wParam);
        case viewer::ViewerWindow::kDeleteRequestedMessage:
            return OnViewerDeleteRequested(wParam);
        case viewer::ViewerWindow::kQuickSendRequestedMessage:
            return OnViewerQuickSendRequest(wParam, lParam);
        case viewer::ViewerWindow::kStartFolderSlideshowMessage:
            return OnViewerStartFolderSlideshowMessage(wParam);
        case viewer::ViewerWindow::kContextMenuCommandMessage:
            return OnViewerContextMenuCommand(wParam);
        case viewer::ViewerWindow::kDroppedFileMessage:
            return OnViewerDroppedFileMessage(lParam);
        case viewer::ViewerWindow::kClosedMessage:
            return OnViewerClosedMessage();
        case kMemoryPressureSampledMessage:
            return OnMemoryPressureSampleMessage(lParam);
        case kPersistentThumbnailCacheMaintenanceMessage:
            return OnPersistentThumbnailCacheMaintenanceMessage(wParam);
        case kDeferredMenuStateMessage:
            menuStateRefreshPosted_ = false;
            if (!menuLoopActive_ && menuStateRefreshPending_)
            {
                menuStateRefreshPending_ = false;
                UpdateMenuState();
            }
            return 0;
        case WM_MEASUREITEM:
        {
            auto* measureItem = reinterpret_cast<MEASUREITEMSTRUCT*>(lParam);
            if (measureItem && measureItem->CtlType == ODT_MENU)
            {
                MeasureOwnerDrawMenuItem(measureItem);
                return TRUE;
            }
            break;
        }
        case WM_DRAWITEM:
        {
            const auto* drawItem = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
            if (drawItem && drawItem->CtlType == ODT_MENU)
            {
                DrawOwnerDrawMenuItem(*drawItem);
                return TRUE;
            }
            if (drawItem && drawItem->CtlType == ODT_STATIC && drawItem->CtlID == kStatusStripControlId)
            {
                DrawStatusStrip(*drawItem);
                return TRUE;
            }
            break;
        }
        case WM_MENUCHAR:
        {
            const HMENU menu = reinterpret_cast<HMENU>(lParam);
            if (!menu)
            {
                return MAKELRESULT(0, MNC_IGNORE);
            }

            const wchar_t pressed = static_cast<wchar_t>(towupper(static_cast<wchar_t>(LOWORD(wParam))));
            int matchedIndex = -1;
            bool duplicateMatch = false;
            const int itemCount = GetMenuItemCount(menu);
            for (int itemIndex = 0; itemIndex < itemCount; ++itemIndex)
            {
                MENUITEMINFOW itemInfo{};
                itemInfo.cbSize = sizeof(itemInfo);
                itemInfo.fMask = MIIM_FTYPE | MIIM_DATA;
                if (!GetMenuItemInfoW(menu, itemIndex, TRUE, &itemInfo) || (itemInfo.fType & MFT_SEPARATOR) != 0)
                {
                    continue;
                }

                const auto* drawData = reinterpret_cast<const MenuDrawItemData*>(itemInfo.dwItemData);
                if (!drawData)
                {
                    continue;
                }

                if (FindMenuMnemonic(drawData->text) != pressed)
                {
                    continue;
                }

                if (matchedIndex >= 0)
                {
                    duplicateMatch = true;
                    break;
                }

                matchedIndex = itemIndex;
            }

            if (matchedIndex >= 0)
            {
                return MAKELRESULT(matchedIndex, duplicateMatch ? MNC_SELECT : MNC_EXECUTE);
            }

            return MAKELRESULT(0, MNC_IGNORE);
        }
        case WM_TIMER:
            if (wParam == kFolderEnumerationPresentationTimerId
                && folderEnumerationPresentationTimerId_ != 0)
            {
                FlushFolderEnumerationPresentation(false);
                return 0;
            }
            if (wParam == kMemoryPressureTimerId && memoryPressureTimerId_ != 0)
            {
                QueueMemoryPressureSample();
                return 0;
            }
            if (wParam == kDisplaySurfaceRecoveryTimerId && displaySurfaceRecoveryTimerId_ != 0)
            {
                ++displaySurfaceRecoveryAttempt_;
                RecoverDisplaySurfaces(displaySurfaceRecoveryAttempt_ == 1);
                if (displaySurfaceRecoveryAttempt_ >= kDisplaySurfaceRecoveryRetryLimit)
                {
                    StopDisplaySurfaceRecoveryRetries();
                }
                return 0;
            }
            break;
        case WM_MOUSELEAVE:
            toolbarMouseTracking_ = false;
            if (toolbarHotIndex_ >= 0)
            {
                toolbarHotIndex_ = -1;
                InvalidateToolbarStrip();
            }
            if (!commandBarKeyboardActive_ && commandBarHotIndex_ >= 0)
            {
                commandBarHotIndex_ = -1;
                InvalidateToolbarStrip();
            }
            if (detailsPanelHotTabIndex_ >= 0)
            {
                detailsPanelHotTabIndex_ = -1;
                if (!IsRectEmpty(&detailsPanelTabStripRect_))
                {
                    InvalidateRect(hwnd_, &detailsPanelTabStripRect_, FALSE);
                }
            }
            if (detailsPanelCloseButtonHot_ || detailsPanelCloseButtonPressed_)
            {
                detailsPanelCloseButtonHot_ = false;
                detailsPanelCloseButtonPressed_ = false;
                if (!IsRectEmpty(&detailsPanelRect_))
                {
                    InvalidateRect(hwnd_, &detailsPanelRect_, FALSE);
                }
            }
            if (quickAccessHotRowIndex_ >= 0
                || quickAccessHotButtonIndex_ >= 0
                || quickAccessSortButtonHot_)
            {
                quickAccessHotRowIndex_ = -1;
                quickAccessHotButtonIndex_ = -1;
                quickAccessSortButtonHot_ = false;
                if (!IsRectEmpty(&quickAccessDestinationPanelRect_))
                {
                    InvalidateRect(hwnd_, &quickAccessDestinationPanelRect_, FALSE);
                }
            }
            break;
        case WM_NOTIFY:
        {
            const auto* nmh = reinterpret_cast<NMHDR*>(lParam);
            if (nmh->hwndFrom == tooltipControl_ && nmh->code == TTN_GETDISPINFOW)
            {
                auto* di = reinterpret_cast<NMTTDISPINFOW*>(lParam);
                if (treePane_ && di->hdr.idFrom == reinterpret_cast<UINT_PTR>(treePane_))
                {
                    treeFolderTooltipText_.clear();
                    if (!treeTooltipPath_.empty())
                    {
                        if (const std::optional<int> assignedShortcut = quickSendModel_.ShortcutForDestination(treeTooltipPath_))
                        {
                            const wchar_t shortcutCharacter = QuickSendModel::ShortcutCharacter(*assignedShortcut);
                            if (shortcutCharacter != L'\0')
                            {
                                treeFolderTooltipText_ = L"Move: F7,";
                                treeFolderTooltipText_.push_back(shortcutCharacter);
                                treeFolderTooltipText_.append(L"  Copy: F8,");
                                treeFolderTooltipText_.push_back(shortcutCharacter);
                            }
                        }
                    }

                    di->lpszText = const_cast<wchar_t*>(treeFolderTooltipText_.c_str());
                    return 0;
                }

                if (di->hdr.idFrom == kQuickAccessSortTooltipId)
                {
                    di->lpszText = const_cast<LPWSTR>(L"Sort Quick Send destinations by hotkey");
                    return 0;
                }

                if (di->hdr.idFrom == kDetailsPanelHistogramTooltipId)
                {
                    di->lpszText = const_cast<LPWSTR>(L"RGB histogram: shows the distribution of pixel brightness across the red, green, and blue channels.");
                    return 0;
                }

                const auto idx = static_cast<std::size_t>(di->hdr.idFrom);
                if (idx < toolbarItems_.size() && !toolbarItems_[idx].tooltip.empty())
                {
                    di->lpszText = const_cast<wchar_t*>(toolbarItems_[idx].tooltip.c_str());
                }
                return 0;
            }
            return OnFolderTreeNotify(lParam);
        }
        case WM_CTLCOLOREDIT:
            if (IsQuickAccessShortcutEdit(reinterpret_cast<HWND>(lParam)))
            {
                const ThemePalette palette = GetThemePalette();
                SetTextColor(reinterpret_cast<HDC>(wParam), palette.text);
                SetBkColor(reinterpret_cast<HDC>(wParam), palette.actionFieldBackground);
                return reinterpret_cast<INT_PTR>(actionFieldBrush_ ? actionFieldBrush_ : backgroundBrush_);
            }
            if (reinterpret_cast<HWND>(lParam) == filterEdit_)
            {
                const ThemePalette palette = GetThemePalette();
                SetTextColor(reinterpret_cast<HDC>(wParam), palette.text);
                SetBkColor(reinterpret_cast<HDC>(wParam), palette.actionFieldBackground);
                return reinterpret_cast<INT_PTR>(actionFieldBrush_ ? actionFieldBrush_ : backgroundBrush_);
            }
            if (reinterpret_cast<HWND>(lParam) == detailsPanelText_)
            {
                const ThemePalette palette = GetThemePalette();
                SetTextColor(reinterpret_cast<HDC>(wParam), palette.text);
                SetBkColor(reinterpret_cast<HDC>(wParam), palette.paneBackground);
                return reinterpret_cast<INT_PTR>(detailsPanelBrush_ ? detailsPanelBrush_ : backgroundBrush_);
            }
            break;
        case WM_CTLCOLORSTATIC:
            if (reinterpret_cast<HWND>(lParam) == detailsPanelText_)
            {
                const ThemePalette palette = GetThemePalette();
                SetTextColor(reinterpret_cast<HDC>(wParam), palette.text);
                SetBkColor(reinterpret_cast<HDC>(wParam), palette.paneBackground);
                return reinterpret_cast<INT_PTR>(detailsPanelBrush_ ? detailsPanelBrush_ : backgroundBrush_);
            }
            break;
        case WM_COMMAND:
            if (LOWORD(wParam) >= kQuickAccessShortcutEditBaseId
                && LOWORD(wParam) < kQuickAccessShortcutEditBaseId + quickAccessShortcutEdits_.size())
            {
                if (HIWORD(wParam) == EN_SETFOCUS)
                {
                    PostMessageW(reinterpret_cast<HWND>(lParam), EM_SETSEL, 0, static_cast<LPARAM>(-1));
                    return 0;
                }

                if (HIWORD(wParam) == EN_CHANGE && !updatingQuickAccessShortcutEdits_)
                {
                    const std::size_t rowIndex = static_cast<std::size_t>(LOWORD(wParam) - kQuickAccessShortcutEditBaseId);
                    if (rowIndex < quickAccessDestinationRows_.size())
                    {
                        HWND edit = quickAccessShortcutEdits_[rowIndex];
                        const int textLength = GetWindowTextLengthW(edit);
                        std::wstring shortcutText(static_cast<std::size_t>(textLength) + 1, L'\0');
                        GetWindowTextW(edit, shortcutText.data(), static_cast<int>(shortcutText.size()));
                        shortcutText.resize(wcslen(shortcutText.c_str()));

                        QuickAccessDestinationRow& row = quickAccessDestinationRows_[rowIndex];
                        const QuickSendAssignmentResult result = quickSendModel_.SetShortcutForDestination(
                            row.destinationPath,
                            shortcutText);
                        if (result != QuickSendAssignmentResult::Accepted)
                        {
                            std::wstring restoredShortcut;
                            if (const std::optional<int> assignedShortcut = quickSendModel_.ShortcutForDestination(row.destinationPath))
                            {
                                const wchar_t shortcutCharacter = QuickSendModel::ShortcutCharacter(*assignedShortcut);
                                if (shortcutCharacter != L'\0')
                                {
                                    restoredShortcut.push_back(shortcutCharacter);
                                }
                                row.assignedShortcut = *assignedShortcut;
                            }
                            else
                            {
                                row.assignedShortcut = -1;
                            }

                            updatingQuickAccessShortcutEdits_ = true;
                            SetWindowTextW(edit, restoredShortcut.c_str());
                            updatingQuickAccessShortcutEdits_ = false;
                        }
                        else if (const std::optional<int> assignedShortcut = quickSendModel_.ShortcutForDestination(row.destinationPath))
                        {
                            row.assignedShortcut = *assignedShortcut;
                            const wchar_t shortcutCharacter = QuickSendModel::ShortcutCharacter(*assignedShortcut);
                            if (shortcutCharacter != L'\0' && shortcutText != std::wstring(1, shortcutCharacter))
                            {
                                updatingQuickAccessShortcutEdits_ = true;
                                SetWindowTextW(edit, std::wstring(1, shortcutCharacter).c_str());
                                updatingQuickAccessShortcutEdits_ = false;
                            }
                        }
                        else
                        {
                            row.assignedShortcut = -1;
                        }

                        InvalidateRect(hwnd_, &quickAccessDestinationPanelRect_, FALSE);
                    }
                }
                return 0;
            }
            if (LOWORD(wParam) == ID_ACTION_FILTER_EDIT && HIWORD(wParam) == EN_CHANGE && browserPaneController_)
            {
                const int textLength = GetWindowTextLengthW(filterEdit_);
                std::wstring filterText(static_cast<std::size_t>(textLength) + 1, L'\0');
                GetWindowTextW(filterEdit_, filterText.data(), static_cast<int>(filterText.size()));
                filterText.resize(wcslen(filterText.c_str()));
                browserPaneController_->SetFilterQuery(std::move(filterText));
                return 0;
            }
            if (HandleCommand(LOWORD(wParam)))
            {
                return 0;
            }
            break;
        case WM_ERASEBKGND:
        {
            RECT client{};
            GetClientRect(hwnd_, &client);
            HDC eraseDC = reinterpret_cast<HDC>(wParam);
            // Exclude the action strip — PaintToolbar handles it
            RECT below{0, kActionStripHeight, client.right, client.bottom};
            FillRect(
                eraseDC,
                &below,
                backgroundBrush_ ? backgroundBrush_ : reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));
            return 1;
        }
        case WM_PAINT:
        {
            PAINTSTRUCT ps{};
            HDC hdc = BeginPaint(hwnd_, &ps);
            RECT client{};
            GetClientRect(hwnd_, &client);

            const int clientWidth = std::max(1, static_cast<int>(client.right - client.left));
            const int clientHeight = std::max(1, static_cast<int>(client.bottom - client.top));
            HDC memDC = CreateCompatibleDC(hdc);
            HBITMAP memBmp = CreateCompatibleBitmap(hdc, clientWidth, clientHeight);
            HGDIOBJ oldBmp = SelectObject(memDC, memBmp);

            FillRect(memDC,
                     &client,
                     backgroundBrush_ ? backgroundBrush_ : reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));

            RECT stripRect{0, 0, client.right, kActionStripHeight};
            PaintToolbar(memDC, stripRect);

            const ThemePalette palette = GetThemePalette();
            auto paintSplitter = [&](const RECT& splitterRect)
            {
                const HBRUSH splitterBrush = CreateSolidBrush(palette.splitter);
                FillRect(memDC, &splitterRect, splitterBrush);
                DeleteObject(splitterBrush);

                const HPEN gripPen = CreatePen(PS_SOLID, 1, palette.actionStripBorder);
                const HGDIOBJ oldPen = SelectObject(memDC, gripPen);
                const int gripX = (splitterRect.left + splitterRect.right) / 2;
                const int gripTop = splitterRect.top + 20;
                const int gripBottom = std::max(gripTop + 12, static_cast<int>(splitterRect.bottom) - 20);
                MoveToEx(memDC, gripX, gripTop, nullptr);
                LineTo(memDC, gripX, gripBottom);
                SelectObject(memDC, oldPen);
                DeleteObject(gripPen);
            };

            RECT splitterRect{leftPaneWidth_, kActionStripHeight, leftPaneWidth_ + kSplitterWidth, client.bottom};
            paintSplitter(splitterRect);
            if (detailsStripVisible_ && !IsRectEmpty(&detailsPanelRect_))
            {
                RECT detailsSplitterRect{detailsPanelRect_.left - kSplitterWidth,
                                         kActionStripHeight,
                                         detailsPanelRect_.left,
                                         client.bottom};
                paintSplitter(detailsSplitterRect);
            }

            PaintDetailsPanel(memDC, client);

            BitBlt(hdc, 0, 0, clientWidth, clientHeight, memDC, 0, 0, SRCCOPY);
            SelectObject(memDC, oldBmp);
            DeleteObject(memBmp);
            DeleteDC(memDC);

            EndPaint(hwnd_, &ps);
            return 0;
        }
        case WM_QUERYENDSESSION:
            // Persist state before the session ends. WM_DESTROY is not guaranteed during
            // a forced shutdown / Windows Update / sign-out.
            SaveWindowState();
            return TRUE;
        case WM_ENDSESSION:
            // Save again on a confirmed end-session in case any state changed between the
            // query and the actual termination. Cheap and idempotent.
            if (wParam)
            {
                SaveWindowState();
            }
            return 0;
        case WM_DESTROY:
            if (treeFolderDragActive_)
            {
                FinishFolderTreeDrag(false);
            }
            StopDisplaySurfaceRecoveryRetries();
            if (sessionNotificationRegistered_)
            {
                WTSUnRegisterSessionNotification(hwnd_);
                sessionNotificationRegistered_ = false;
            }
            if (consoleDisplayNotify_)
            {
                UnregisterPowerSettingNotification(consoleDisplayNotify_);
                consoleDisplayNotify_ = nullptr;
            }
            if (monitorPowerNotify_)
            {
                UnregisterPowerSettingNotification(monitorPowerNotify_);
                monitorPowerNotify_ = nullptr;
            }
            if (folderEnumerationPresentationTimerId_ != 0)
            {
                KillTimer(hwnd_, folderEnumerationPresentationTimerId_);
                folderEnumerationPresentationTimerId_ = 0;
            }
            if (memoryPressureTimerId_ != 0)
            {
                KillTimer(hwnd_, kMemoryPressureTimerId);
                memoryPressureTimerId_ = 0;
            }
            memoryPressureExecutor_.reset();
            if (folderEnumerationService_)
            {
                folderEnumerationService_->Cancel();
            }
            if (folderTreeEnumerationService_)
            {
                folderTreeEnumerationService_->CancelAll();
            }
            if (folderWatchService_)
            {
                folderWatchService_->Stop();
            }
            if (batchConvertService_)
            {
                batchConvertService_->Cancel();
            }
            SaveWindowState();
            PostQuitMessage(0);
            return 0;
        default:
            break;
        }

        return DefWindowProcW(hwnd_, message, wParam, lParam);
    }

    LRESULT CALLBACK MainWindow::WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        MainWindow* self = nullptr;

        if (message == WM_NCCREATE)
        {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            self = static_cast<MainWindow*>(cs->lpCreateParams);
            self->hwnd_ = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        else
        {
            self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }

        if (self)
        {
            return self->HandleMessage(message, wParam, lParam);
        }

        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}

