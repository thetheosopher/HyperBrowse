#pragma once

#include "browser/BrowserPane.h"

#include <commctrl.h>
#include <commoncontrols.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <windowsx.h>
#include <d2d1.h>
#include <dwrite.h>

#include <algorithm>
#include <cwchar>
#include <cwctype>
#include <filesystem>
#include <functional>
#include <memory>
#include <string_view>

#include "app/resource.h"
#include "browser/BrowserModel.h"
#include "cache/ThumbnailCache.h"
#include "decode/ImageDecoder.h"
#include "render/D2DRenderer.h"
#include "render/GdiText.h"
#include "services/ImageMetadataService.h"
#include "services/ThumbnailScheduler.h"
#include "services/UserMetadataStore.h"
#include "util/Diagnostics.h"
#include "util/BackgroundExecutor.h"
#include "util/Log.h"
#include "util/ResourcePng.h"
#include "ui/SystemTheme.h"
#include "ui/ThemeTokens.h"

// Helpers used by more than one translation unit. They were file-local before the
// source file was split, so they are inline and live in a named namespace.
namespace browser_pane_shared
{
    namespace fs = std::filesystem;
    inline constexpr int kPlaceholderIconDisplaySize = 72;
    inline constexpr int kUnavailableThumbnailIconSize = 48;
    inline constexpr std::wstring_view kThumbnailRatingStars = L"\x2605\x2605\x2605\x2605\x2605";
    inline constexpr COLORREF kThumbnailRatingGold = RGB(214, 172, 46);

    inline const wchar_t* UnavailableThumbnailMessage(hyperbrowse::decode::ThumbnailDecodeFailureKind failureKind)
    {
        switch (failureKind)
        {
        case hyperbrowse::decode::ThumbnailDecodeFailureKind::TimedOut:
            return L"Thumbnail\r\nTimed Out";
        case hyperbrowse::decode::ThumbnailDecodeFailureKind::DecodeFailed:
        default:
            return L"Thumbnail\r\nDecode Failed";
        }
    }

    inline std::wstring ToLowercase(std::wstring value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](wchar_t character)
        {
            return static_cast<wchar_t>(towlower(character));
        });
        return value;
    }

    inline std::wstring TrimWhitespace(std::wstring value)
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

    inline std::wstring BuildThumbnailDisplayTitle(const hyperbrowse::browser::BrowserItem& item)
    {
        if (item.isDirectory)
        {
            return item.fileName;
        }

        const std::wstring stem = fs::path(item.fileName).stem().wstring();
        return stem.empty() ? item.fileName : stem;
    }

    inline hyperbrowse::cache::ThumbnailCacheKey MakeThumbnailCacheKey(const hyperbrowse::browser::BrowserItem& item,
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

    inline int BrowserTextDpi(HWND window) noexcept
    {
        return hyperbrowse::util::EffectiveTextDpi(
            window ? static_cast<int>(GetDpiForWindow(window)) : 96);
    }

    inline int ThumbnailRatingForItem(const hyperbrowse::services::UserMetadataStore* userMetadataStore,
                               const hyperbrowse::browser::BrowserItem& item)
    {
        if (!userMetadataStore)
        {
            return 0;
        }

        return std::clamp(userMetadataStore->EntryForPath(item.filePath).rating,
                          0,
                          static_cast<int>(kThumbnailRatingStars.size()));
    }
}

using namespace browser_pane_shared;
