#pragma once

#include <memory>
#include <string>

#include "cache/ThumbnailCache.h"

namespace hyperbrowse::decode
{
    class NvJpegDecoder
    {
    public:
        static bool IsRuntimeAvailable();

        std::shared_ptr<const cache::CachedThumbnail> Decode(const cache::ThumbnailCacheKey& key,
                                                             std::wstring* errorMessage = nullptr) const;
    };
}