#pragma once

#include <memory>

#include "cache/ThumbnailCache.h"

namespace hyperbrowse::decode
{
    class WicThumbnailDecoder
    {
    public:
        std::shared_ptr<const cache::CachedThumbnail> Decode(const cache::ThumbnailCacheKey& key) const;
    };
}