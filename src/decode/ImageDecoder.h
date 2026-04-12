#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "browser/BrowserModel.h"
#include "cache/ThumbnailCache.h"

namespace hyperbrowse::decode
{
#if defined(HYPERBROWSE_ENABLE_LIBRAW)
    enum class LibRawHelperMode
    {
        Thumbnail = 0,
        FullImage = 1,
    };

    struct LibRawHelperInvocation
    {
        LibRawHelperMode mode{LibRawHelperMode::Thumbnail};
        std::wstring filePath;
        std::wstring outputFilePath;
        int targetWidth{};
        int targetHeight{};
    };

    bool RunLibRawHelperInvocation(const LibRawHelperInvocation& invocation,
                                   std::wstring* errorMessage = nullptr);
#endif

    bool IsLibRawBuildEnabled();
    void SetLibRawOutOfProcessEnabled(bool enabled);
    bool IsLibRawOutOfProcessEnabled();
    std::wstring DescribeRawDecodingState();

    void SetNvJpegAccelerationEnabled(bool enabled);
    bool IsNvJpegAccelerationEnabled();
    bool IsNvJpegBuildEnabled();
    bool IsNvJpegRuntimeAvailable();
    std::wstring DescribeJpegAccelerationState();

    bool IsWicFileType(std::wstring_view fileType);
    bool IsRawFileType(std::wstring_view fileType);
    bool CanDecodeThumbnail(const browser::BrowserItem& item);
    bool CanDecodeFullImage(const browser::BrowserItem& item);

    std::shared_ptr<const cache::CachedThumbnail> DecodeThumbnailCpuOnly(const cache::ThumbnailCacheKey& key,
                                                                         std::wstring* errorMessage = nullptr);
    std::shared_ptr<const cache::CachedThumbnail> DecodeThumbnail(const cache::ThumbnailCacheKey& key,
                                                                  std::wstring* errorMessage = nullptr);
    std::vector<std::shared_ptr<const cache::CachedThumbnail>> DecodeThumbnailBatch(
        const std::vector<cache::ThumbnailCacheKey>& keys,
        std::vector<std::wstring>* errorMessages = nullptr);
    std::shared_ptr<const cache::CachedThumbnail> DecodeFullImage(const browser::BrowserItem& item,
                                                                  std::wstring* errorMessage);
}