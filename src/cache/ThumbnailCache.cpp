#include "cache/ThumbnailCache.h"

#include <algorithm>

#include "util/HashUtils.h"

namespace hyperbrowse::cache
{
    std::size_t ThumbnailCacheKeyHasher::operator()(const ThumbnailCacheKey& key) const noexcept
    {
        std::size_t seed = 0;
        util::HashCombine(&seed, util::NormalizePathForComparison(key.filePath));
        util::HashCombine(&seed, key.modifiedTimestampUtc);
        util::HashCombine(&seed, key.targetWidth);
        util::HashCombine(&seed, key.targetHeight);
        return seed;
    }

    CachedThumbnail::CachedThumbnail(HBITMAP bitmap,
                                     int width,
                                     int height,
                                     std::size_t byteCount,
                                     int sourceWidth,
                                     int sourceHeight)
        : bitmap_(bitmap)
        , width_(width)
        , height_(height)
        , byteCount_(byteCount)
        , sourceWidth_(sourceWidth)
        , sourceHeight_(sourceHeight)
    {
    }

    CachedThumbnail::~CachedThumbnail()
    {
        if (bitmap_)
        {
            DeleteObject(bitmap_);
        }
    }

    HBITMAP CachedThumbnail::Bitmap() const noexcept
    {
        return bitmap_;
    }

    int CachedThumbnail::Width() const noexcept
    {
        return width_;
    }

    int CachedThumbnail::Height() const noexcept
    {
        return height_;
    }

    std::size_t CachedThumbnail::ByteCount() const noexcept
    {
        return byteCount_;
    }

    int CachedThumbnail::SourceWidth() const noexcept
    {
        return sourceWidth_;
    }

    int CachedThumbnail::SourceHeight() const noexcept
    {
        return sourceHeight_;
    }

    ThumbnailCache::ThumbnailCache(std::size_t capacityBytes)
        : capacityBytes_(capacityBytes)
    {
    }

    std::shared_ptr<const CachedThumbnail> ThumbnailCache::Find(const ThumbnailCacheKey& key) const
    {
        ThumbnailCacheKey normalizedKey = key;
        normalizedKey.filePath = util::NormalizePathForComparison(normalizedKey.filePath);

        std::scoped_lock lock(mutex_);
        const auto iterator = entries_.find(normalizedKey);
        if (iterator == entries_.end())
        {
            return {};
        }

        lruOrder_.splice(lruOrder_.begin(), lruOrder_, iterator->second.lruIterator);
        iterator->second.lruIterator = lruOrder_.begin();
        return iterator->second.thumbnail;
    }

    void ThumbnailCache::Insert(ThumbnailCacheKey key, std::shared_ptr<const CachedThumbnail> thumbnail)
    {
        if (!thumbnail)
        {
            return;
        }

        key.filePath = util::NormalizePathForComparison(key.filePath);

        std::scoped_lock lock(mutex_);

        const auto existing = entries_.find(key);
        if (existing != entries_.end())
        {
            currentBytes_ -= existing->second.byteCount;
            lruOrder_.erase(existing->second.lruIterator);
            entries_.erase(existing);
        }

        lruOrder_.push_front(key);
        const std::size_t byteCount = thumbnail->ByteCount();
        entries_.emplace(lruOrder_.front(), Entry{thumbnail, lruOrder_.begin(), byteCount});
        currentBytes_ += byteCount;
        EvictIfNeeded();
    }

    void ThumbnailCache::InvalidateFilePaths(const std::vector<std::wstring>& filePaths)
    {
        if (filePaths.empty())
        {
            return;
        }

        std::vector<std::wstring> normalizedPaths;
        normalizedPaths.reserve(filePaths.size());
        for (const std::wstring& filePath : filePaths)
        {
            normalizedPaths.push_back(util::NormalizePathForComparison(filePath));
        }

        std::scoped_lock lock(mutex_);
        for (auto iterator = entries_.begin(); iterator != entries_.end();)
        {
            const bool shouldErase = std::find(normalizedPaths.begin(), normalizedPaths.end(), iterator->first.filePath) != normalizedPaths.end();
            if (!shouldErase)
            {
                ++iterator;
                continue;
            }

            currentBytes_ -= iterator->second.byteCount;
            lruOrder_.erase(iterator->second.lruIterator);
            iterator = entries_.erase(iterator);
        }
    }

    void ThumbnailCache::Clear()
    {
        std::scoped_lock lock(mutex_);
        currentBytes_ = 0;
        lruOrder_.clear();
        entries_.clear();
    }

    std::size_t ThumbnailCache::CurrentBytes() const
    {
        std::scoped_lock lock(mutex_);
        return currentBytes_;
    }

    std::size_t ThumbnailCache::CapacityBytes() const noexcept
    {
        return capacityBytes_;
    }

    void ThumbnailCache::EvictIfNeeded()
    {
        while (currentBytes_ > capacityBytes_ && !lruOrder_.empty())
        {
            const ThumbnailCacheKey key = lruOrder_.back();
            const auto iterator = entries_.find(key);
            if (iterator != entries_.end())
            {
                currentBytes_ -= iterator->second.byteCount;
                entries_.erase(iterator);
            }

            lruOrder_.pop_back();
        }
    }
}