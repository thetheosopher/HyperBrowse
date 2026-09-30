#pragma once

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "util/PathUtils.h"

namespace hyperbrowse::cache
{
    struct ThumbnailCacheKey
    {
        std::wstring filePath;
        std::uint64_t modifiedTimestampUtc{};
        int targetWidth{};
        int targetHeight{};

        bool operator==(const ThumbnailCacheKey& other) const noexcept
        {
            return modifiedTimestampUtc == other.modifiedTimestampUtc
                && targetWidth == other.targetWidth
                && targetHeight == other.targetHeight
                && util::NormalizedPathEquals(filePath, other.filePath);
        }
    };

    struct ThumbnailCacheKeyHasher
    {
        std::size_t operator()(const ThumbnailCacheKey& key) const noexcept;
    };

    enum class SourceColorKind : std::uint32_t
    {
        Unspecified,
        Srgb,
        Icc,
        Unsupported,
        DisplayConverted,
    };

    struct SourceColorInfo
    {
        SourceColorKind kind{SourceColorKind::Unspecified};
        std::vector<unsigned char> iccProfile;
    };

    inline constexpr std::size_t kMaximumSourceProfileBytes = 4 * 1024 * 1024;
    std::size_t SerializedSourceColorBytes(const SourceColorInfo& info) noexcept;
    bool WriteSourceColorInfo(std::ostream& stream, const SourceColorInfo& info);
    bool ReadSourceColorInfo(std::istream& stream, std::size_t availableBytes, SourceColorInfo* info);

    class CachedThumbnail
    {
    public:
        CachedThumbnail(HBITMAP bitmap,
                        int width,
                        int height,
                        std::size_t byteCount,
                        int sourceWidth,
                        int sourceHeight,
                        SourceColorInfo sourceColor = {});
        ~CachedThumbnail();

        CachedThumbnail(const CachedThumbnail&) = delete;
        CachedThumbnail& operator=(const CachedThumbnail&) = delete;

        HBITMAP Bitmap() const noexcept;
        int Width() const noexcept;
        int Height() const noexcept;
        std::size_t ByteCount() const noexcept;
        int SourceWidth() const noexcept;
        int SourceHeight() const noexcept;
        const SourceColorInfo& SourceColor() const noexcept;

    private:
        HBITMAP bitmap_{};
        int width_{};
        int height_{};
        std::size_t byteCount_{};
        int sourceWidth_{};
        int sourceHeight_{};
        SourceColorInfo sourceColor_;
    };

    class ThumbnailCache
    {
    public:
        struct Statistics
        {
            std::uint64_t hitCount{};
            std::uint64_t missCount{};
            std::uint64_t evictionCount{};
        };

        explicit ThumbnailCache(std::size_t capacityBytes);

        std::shared_ptr<const CachedThumbnail> Find(const ThumbnailCacheKey& key) const;
        void Insert(ThumbnailCacheKey key, std::shared_ptr<const CachedThumbnail> thumbnail);
        void InvalidateFilePaths(const std::vector<std::wstring>& filePaths);
        void TrimToBytes(std::size_t targetBytes);
        void SetCapacityBytes(std::size_t capacityBytes);
        void Clear();
        std::size_t CurrentBytes() const;
        std::size_t CapacityBytes() const;
        Statistics GetStatistics() const;

    private:
        struct Entry
        {
            std::shared_ptr<const CachedThumbnail> thumbnail;
            std::list<ThumbnailCacheKey>::iterator lruIterator;
            std::size_t byteCount{};
        };

        void EvictIfNeeded();
        void EvictToEntryCount();
        void EvictToBytes(std::size_t targetBytes);

        std::size_t capacityBytes_{};
        mutable std::mutex mutex_;
        std::size_t currentBytes_{};
        mutable std::uint64_t hitCount_{};
        mutable std::uint64_t missCount_{};
        std::uint64_t evictionCount_{};
        mutable std::list<ThumbnailCacheKey> lruOrder_;
        mutable std::unordered_map<ThumbnailCacheKey, Entry, ThumbnailCacheKeyHasher> entries_;
    };
}
