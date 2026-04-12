#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "browser/BrowserModel.h"
#include "cache/ThumbnailCache.h"

namespace hyperbrowse::decode
{
    bool IsWicFileType(std::wstring_view fileType);
    bool IsRawFileType(std::wstring_view fileType);
    bool CanDecodeThumbnail(const browser::BrowserItem& item);
    bool CanDecodeFullImage(const browser::BrowserItem& item);

    std::shared_ptr<const cache::CachedThumbnail> DecodeThumbnail(const cache::ThumbnailCacheKey& key,
                                                                  std::wstring* errorMessage = nullptr);
    std::shared_ptr<const cache::CachedThumbnail> DecodeFullImage(const browser::BrowserItem& item,
                                                                  std::wstring* errorMessage);
}