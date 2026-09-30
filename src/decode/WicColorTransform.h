#pragma once

#include <wincodec.h>

#include <memory>
#include <span>
#include <string>

#include "cache/ThumbnailCache.h"

namespace hyperbrowse::decode::color
{
    inline constexpr std::size_t kMaximumIccProfileBytes = cache::kMaximumSourceProfileBytes;

    bool IsUsableRgbProfile(std::span<const unsigned char> profile,
                            std::wstring* errorMessage = nullptr);
    cache::SourceColorInfo ReadSourceColorInfo(IWICBitmapFrameDecode* frame);
    cache::SourceColorInfo ReadSourceColorInfo(const std::wstring& filePath);
    cache::SourceColorInfo ReadSourceColorInfo(std::span<const unsigned char> encodedBytes);
    std::shared_ptr<const cache::CachedThumbnail> TransformForDisplay(
        const cache::CachedThumbnail& source,
        const cache::SourceColorInfo& sourceColor,
        std::span<const unsigned char> destinationProfile,
        std::wstring* errorMessage);
}
