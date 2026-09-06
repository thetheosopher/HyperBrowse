#include "ui/BrowserPresentationPersistence.h"

#include <algorithm>

namespace hyperbrowse::ui
{
    namespace
    {
        constexpr std::wstring_view kLeftPaneWidthValue = L"LeftPaneWidth";
        constexpr std::wstring_view kBrowserModeValue = L"BrowserMode";
        constexpr std::wstring_view kThemeModeValue = L"ThemeMode";
        constexpr std::wstring_view kAppTextSizeValue = L"AppTextSize";
        constexpr std::wstring_view kThumbnailSizePresetValue = L"ThumbnailSizePreset";
        constexpr std::wstring_view kCompactThumbnailLayoutValue = L"CompactThumbnailLayout";
        constexpr std::wstring_view kThumbnailDetailsVisibleValue = L"ThumbnailDetailsVisible";
        constexpr std::wstring_view kShowSubfoldersInBrowserValue = L"ShowSubfoldersInBrowser";
        constexpr std::wstring_view kSortModeValue = L"SortMode";
        constexpr std::wstring_view kSortAscendingValue = L"SortAscending";
        constexpr std::wstring_view kDetailsStripVisibleValue = L"DetailsStripVisible";
        constexpr std::wstring_view kDetailsPanelWidthValue = L"DetailsPanelWidth";
        constexpr int kMinimumLeftPaneWidth = 250;
        constexpr int kMinimumDetailsPanelWidth = 250;

        bool TryParseThumbnailSizePreset(DWORD value, browser::ThumbnailSizePreset* preset)
        {
            if (!preset)
            {
                return false;
            }

            switch (value)
            {
            case static_cast<DWORD>(browser::ThumbnailSizePreset::Pixels96):
                *preset = browser::ThumbnailSizePreset::Pixels96;
                return true;
            case static_cast<DWORD>(browser::ThumbnailSizePreset::Pixels128):
                *preset = browser::ThumbnailSizePreset::Pixels128;
                return true;
            case static_cast<DWORD>(browser::ThumbnailSizePreset::Pixels160):
                *preset = browser::ThumbnailSizePreset::Pixels160;
                return true;
            case static_cast<DWORD>(browser::ThumbnailSizePreset::Pixels192):
                *preset = browser::ThumbnailSizePreset::Pixels192;
                return true;
            case static_cast<DWORD>(browser::ThumbnailSizePreset::Pixels256):
                *preset = browser::ThumbnailSizePreset::Pixels256;
                return true;
            case static_cast<DWORD>(browser::ThumbnailSizePreset::Pixels320):
                *preset = browser::ThumbnailSizePreset::Pixels320;
                return true;
            case static_cast<DWORD>(browser::ThumbnailSizePreset::Pixels360):
                *preset = browser::ThumbnailSizePreset::Pixels360;
                return true;
            case static_cast<DWORD>(browser::ThumbnailSizePreset::Pixels420):
                *preset = browser::ThumbnailSizePreset::Pixels420;
                return true;
            case static_cast<DWORD>(browser::ThumbnailSizePreset::Pixels480):
                *preset = browser::ThumbnailSizePreset::Pixels480;
                return true;
            case static_cast<DWORD>(browser::ThumbnailSizePreset::Pixels560):
                *preset = browser::ThumbnailSizePreset::Pixels560;
                return true;
            case static_cast<DWORD>(browser::ThumbnailSizePreset::Pixels640):
                *preset = browser::ThumbnailSizePreset::Pixels640;
                return true;
            default:
                return false;
            }
        }
    }

    BrowserPresentationState BrowserPresentationPersistence::Load(const ReadDword& readDword,
                                                                  BrowserPresentationState state)
    {
        DWORD value = 0;
        if (readDword(kLeftPaneWidthValue, &value))
        {
            state.leftPaneWidth = static_cast<int>(value);
        }

        if (readDword(kBrowserModeValue, &value) && value <= 1)
        {
            state.browserMode = value;
        }

        if (readDword(kThemeModeValue, &value) && value <= 1)
        {
            state.themeMode = value;
        }

        if (readDword(kAppTextSizeValue, &value))
        {
            state.appTextSize = util::NormalizeAppTextSize(value);
        }

        if (readDword(kThumbnailSizePresetValue, &value))
        {
            TryParseThumbnailSizePreset(value, &state.thumbnailSizePreset);
        }

        if (readDword(kCompactThumbnailLayoutValue, &value))
        {
            state.compactThumbnailLayout = value != 0;
        }

        if (readDword(kThumbnailDetailsVisibleValue, &value))
        {
            state.thumbnailDetailsVisible = value != 0;
        }

        if (readDword(kShowSubfoldersInBrowserValue, &value))
        {
            state.showSubfoldersInBrowser = value != 0;
        }

        if (readDword(kSortModeValue, &value) && value <= static_cast<DWORD>(browser::BrowserSortMode::Tags))
        {
            state.sortMode = static_cast<browser::BrowserSortMode>(value);
        }

        if (readDword(kSortAscendingValue, &value))
        {
            state.sortAscending = value != 0;
        }

        if (readDword(kDetailsStripVisibleValue, &value))
        {
            state.detailsStripVisible = value != 0;
        }

        if (readDword(kDetailsPanelWidthValue, &value))
        {
            state.detailsPanelWidth = static_cast<int>(value);
        }

        return state;
    }

    void BrowserPresentationPersistence::Save(const BrowserPresentationState& state,
                                              const WriteDword& writeDword)
    {
        writeDword(kLeftPaneWidthValue, static_cast<DWORD>((std::max)(state.leftPaneWidth, kMinimumLeftPaneWidth)));
        writeDword(kBrowserModeValue, state.browserMode);
        writeDword(kThemeModeValue, state.themeMode);
        writeDword(kAppTextSizeValue, static_cast<DWORD>(state.appTextSize));
        writeDword(kThumbnailSizePresetValue, static_cast<DWORD>(state.thumbnailSizePreset));
        writeDword(kCompactThumbnailLayoutValue, state.compactThumbnailLayout ? 1UL : 0UL);
        writeDword(kThumbnailDetailsVisibleValue, state.thumbnailDetailsVisible ? 1UL : 0UL);
        writeDword(kShowSubfoldersInBrowserValue, state.showSubfoldersInBrowser ? 1UL : 0UL);
        writeDword(kSortModeValue, static_cast<DWORD>(state.sortMode));
        writeDword(kSortAscendingValue, state.sortAscending ? 1UL : 0UL);
        writeDword(kDetailsStripVisibleValue, state.detailsStripVisible ? 1UL : 0UL);
        writeDword(kDetailsPanelWidthValue, static_cast<DWORD>((std::max)(state.detailsPanelWidth, kMinimumDetailsPanelWidth)));
    }
}
