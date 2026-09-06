#pragma once

#include <windows.h>

#include <functional>
#include <string_view>

#include "browser/BrowserPane.h"
#include "util/UiTextSize.h"

namespace hyperbrowse::ui
{
    struct BrowserPresentationState
    {
        int leftPaneWidth{280};
        DWORD browserMode{};
        DWORD themeMode{};
        util::AppTextSize appTextSize{util::kDefaultAppTextSize};
        browser::ThumbnailSizePreset thumbnailSizePreset{browser::ThumbnailSizePreset::Pixels192};
        bool compactThumbnailLayout{true};
        bool thumbnailDetailsVisible{true};
        bool showSubfoldersInBrowser{};
        browser::BrowserSortMode sortMode{browser::BrowserSortMode::FileName};
        bool sortAscending{true};
        bool detailsStripVisible{true};
        int detailsPanelWidth{340};
    };

    class BrowserPresentationPersistence
    {
    public:
        using ReadDword = std::function<bool(std::wstring_view valueName, DWORD* value)>;
        using WriteDword = std::function<void(std::wstring_view valueName, DWORD value)>;

        static BrowserPresentationState Load(const ReadDword& readDword,
                                             BrowserPresentationState state = {});
        static void Save(const BrowserPresentationState& state,
                         const WriteDword& writeDword);
    };
}
