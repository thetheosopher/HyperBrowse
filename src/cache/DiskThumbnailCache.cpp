#include "cache/DiskThumbnailCache.h"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <vector>

#include "util/HashUtils.h"
#include "util/PathUtils.h"

namespace
{
    namespace fs = std::filesystem;

    constexpr std::size_t kDefaultDiskThumbnailCacheCapacityBytes = 512ULL * 1024ULL * 1024ULL;
    constexpr std::wstring_view kCacheRootFolder = L"HyperBrowse\\thumbnail-cache";
    constexpr std::wstring_view kIndexFileName = L"index.tsv";

#pragma pack(push, 1)
    struct DiskThumbnailHeader
    {
        char magic[8];
        std::uint32_t width{};
        std::uint32_t height{};
        std::uint32_t sourceWidth{};
        std::uint32_t sourceHeight{};
        std::uint64_t pixelBytes{};
    };
#pragma pack(pop)

    constexpr std::array<char, 8> kDiskThumbnailMagic{{'H', 'B', 'T', 'H', 'M', 'B', '0', '1'}};

    std::wstring EscapeField(std::wstring_view value)
    {
        std::wstring escaped;
        escaped.reserve(value.size());
        for (const wchar_t character : value)
        {
            switch (character)
            {
            case L'\\':
                escaped.append(L"\\\\");
                break;
            case L'\t':
                escaped.append(L"\\t");
                break;
            case L'\n':
                escaped.append(L"\\n");
                break;
            case L'\r':
                escaped.append(L"\\r");
                break;
            default:
                escaped.push_back(character);
                break;
            }
        }
        return escaped;
    }

    std::wstring UnescapeField(std::wstring_view value)
    {
        std::wstring unescaped;
        unescaped.reserve(value.size());
        bool escaping = false;
        for (const wchar_t character : value)
        {
            if (!escaping)
            {
                if (character == L'\\')
                {
                    escaping = true;
                }
                else
                {
                    unescaped.push_back(character);
                }
                continue;
            }

            switch (character)
            {
            case L't':
                unescaped.push_back(L'\t');
                break;
            case L'n':
                unescaped.push_back(L'\n');
                break;
            case L'r':
                unescaped.push_back(L'\r');
                break;
            case L'\\':
            default:
                unescaped.push_back(character);
                break;
            }
            escaping = false;
        }

        if (escaping)
        {
            unescaped.push_back(L'\\');
        }
        return unescaped;
    }

    std::vector<std::wstring> SplitTabFields(const std::wstring& line)
    {
        std::vector<std::wstring> fields;
        std::wstring current;
        bool escaping = false;
        for (const wchar_t character : line)
        {
            if (escaping)
            {
                current.push_back(L'\\');
                current.push_back(character);
                escaping = false;
                continue;
            }

            if (character == L'\\')
            {
                escaping = true;
                continue;
            }

            if (character == L'\t')
            {
                fields.push_back(UnescapeField(current));
                current.clear();
                continue;
            }

            current.push_back(character);
        }

        if (escaping)
        {
            current.push_back(L'\\');
        }
        fields.push_back(UnescapeField(current));
        return fields;
    }

    std::wstring TryGetLocalAppDataPath()
    {
        PWSTR rawPath = nullptr;
        const HRESULT result = SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &rawPath);
        if (FAILED(result) || !rawPath)
        {
            return {};
        }

        std::wstring path = rawPath;
        CoTaskMemFree(rawPath);
        return path;
    }

    std::wstring BuildCacheFileName(const hyperbrowse::cache::ThumbnailCacheKey& key)
    {
        std::size_t seed = 0;
        hyperbrowse::util::HashCombine(&seed, hyperbrowse::util::NormalizePathForComparison(key.filePath));
        hyperbrowse::util::HashCombine(&seed, key.modifiedTimestampUtc);
        hyperbrowse::util::HashCombine(&seed, key.targetWidth);
        hyperbrowse::util::HashCombine(&seed, key.targetHeight);

        wchar_t buffer[17]{};
        swprintf_s(buffer, L"%016llx", static_cast<unsigned long long>(seed));
        return std::wstring(buffer) + L".thumb";
    }

    bool ExtractBitmapPixels(HBITMAP bitmap,
                             int* width,
                             int* height,
                             std::vector<unsigned char>* pixels)
    {
        if (!bitmap || !width || !height || !pixels)
        {
            return false;
        }

        BITMAP bitmapInfo{};
        if (GetObjectW(bitmap, sizeof(bitmapInfo), &bitmapInfo) == 0)
        {
            return false;
        }

        const int bitmapWidth = bitmapInfo.bmWidth;
        const int bitmapHeight = std::abs(bitmapInfo.bmHeight);
        if (bitmapWidth <= 0 || bitmapHeight <= 0)
        {
            return false;
        }

        BITMAPINFO dibInfo{};
        dibInfo.bmiHeader.biSize = sizeof(dibInfo.bmiHeader);
        dibInfo.bmiHeader.biWidth = bitmapWidth;
        dibInfo.bmiHeader.biHeight = -bitmapHeight;
        dibInfo.bmiHeader.biPlanes = 1;
        dibInfo.bmiHeader.biBitCount = 32;
        dibInfo.bmiHeader.biCompression = BI_RGB;

        pixels->assign(static_cast<std::size_t>(bitmapWidth) * static_cast<std::size_t>(bitmapHeight) * 4U, 0);
        HDC screenDc = GetDC(nullptr);
        if (!screenDc)
        {
            return false;
        }

        const int copiedScanLines = GetDIBits(screenDc,
                                              bitmap,
                                              0,
                                              static_cast<UINT>(bitmapHeight),
                                              pixels->data(),
                                              &dibInfo,
                                              DIB_RGB_COLORS);
        ReleaseDC(nullptr, screenDc);
        if (copiedScanLines == 0)
        {
            pixels->clear();
            return false;
        }

        *width = bitmapWidth;
        *height = bitmapHeight;
        return true;
    }

    HBITMAP CreateBitmapFromPixels(int width, int height, const std::vector<unsigned char>& pixels)
    {
        if (width <= 0 || height <= 0 || pixels.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U)
        {
            return nullptr;
        }

        BITMAPINFO bitmapInfo{};
        bitmapInfo.bmiHeader.biSize = sizeof(bitmapInfo.bmiHeader);
        bitmapInfo.bmiHeader.biWidth = width;
        bitmapInfo.bmiHeader.biHeight = -height;
        bitmapInfo.bmiHeader.biPlanes = 1;
        bitmapInfo.bmiHeader.biBitCount = 32;
        bitmapInfo.bmiHeader.biCompression = BI_RGB;

        void* dibBits = nullptr;
        HBITMAP dib = CreateDIBSection(nullptr, &bitmapInfo, DIB_RGB_COLORS, &dibBits, nullptr, 0);
        if (!dib || !dibBits)
        {
            if (dib)
            {
                DeleteObject(dib);
            }
            return nullptr;
        }

        std::memcpy(dibBits, pixels.data(), pixels.size());
        return dib;
    }

    std::wstring BuildIndexLine(const hyperbrowse::cache::ThumbnailCacheKey& key,
                                std::wstring_view cacheFileName,
                                std::size_t fileBytes,
                                std::uint64_t lastAccessOrdinal)
    {
        return EscapeField(hyperbrowse::util::NormalizePathForComparison(key.filePath))
            + L"\t" + std::to_wstring(key.modifiedTimestampUtc)
            + L"\t" + std::to_wstring(key.targetWidth)
            + L"\t" + std::to_wstring(key.targetHeight)
            + L"\t" + EscapeField(cacheFileName)
            + L"\t" + std::to_wstring(fileBytes)
            + L"\t" + std::to_wstring(lastAccessOrdinal);
    }
}

namespace hyperbrowse::cache
{
    DiskThumbnailCache::DiskThumbnailCache(std::size_t capacityBytes)
        : capacityBytes_(capacityBytes == 0 ? kDefaultDiskThumbnailCacheCapacityBytes : capacityBytes)
    {
    }

    std::shared_ptr<const CachedThumbnail> DiskThumbnailCache::TryLoad(const ThumbnailCacheKey& key)
    {
        ThumbnailCacheKey normalizedKey = key;
        normalizedKey.filePath = util::NormalizePathForComparison(normalizedKey.filePath);

        std::wstring cachePath;
        {
            std::scoped_lock lock(mutex_);
            EnsureLoadedLocked();
            const auto iterator = entries_.find(normalizedKey);
            if (iterator == entries_.end())
            {
                return {};
            }

            cachePath = (fs::path(EnsureCacheDirectoryLocked()) / iterator->second.cacheFileName).wstring();
            iterator->second.lastAccessOrdinal = nextAccessOrdinal_++;
            SaveIndexLocked();
        }

        std::ifstream stream(fs::path(cachePath), std::ios::binary);
        if (!stream)
        {
            std::scoped_lock lock(mutex_);
            entries_.erase(normalizedKey);
            SaveIndexLocked();
            return {};
        }

        DiskThumbnailHeader header{};
        stream.read(reinterpret_cast<char*>(&header), sizeof(header));
        if (!stream || !std::equal(std::begin(header.magic), std::end(header.magic), kDiskThumbnailMagic.begin(), kDiskThumbnailMagic.end()))
        {
            return {};
        }

        if (header.width == 0 || header.height == 0 || header.pixelBytes == 0)
        {
            return {};
        }

        std::vector<unsigned char> pixels(static_cast<std::size_t>(header.pixelBytes));
        stream.read(reinterpret_cast<char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
        if (!stream)
        {
            return {};
        }

        HBITMAP bitmap = CreateBitmapFromPixels(static_cast<int>(header.width), static_cast<int>(header.height), pixels);
        if (!bitmap)
        {
            return {};
        }

        return std::make_shared<CachedThumbnail>(bitmap,
                                                 static_cast<int>(header.width),
                                                 static_cast<int>(header.height),
                                                 pixels.size(),
                                                 static_cast<int>(header.sourceWidth),
                                                 static_cast<int>(header.sourceHeight));
    }

    void DiskThumbnailCache::Store(const ThumbnailCacheKey& key, std::shared_ptr<const CachedThumbnail> thumbnail)
    {
        if (!thumbnail)
        {
            return;
        }

        int width = 0;
        int height = 0;
        std::vector<unsigned char> pixels;
        if (!ExtractBitmapPixels(thumbnail->Bitmap(), &width, &height, &pixels))
        {
            return;
        }

        ThumbnailCacheKey normalizedKey = key;
        normalizedKey.filePath = util::NormalizePathForComparison(normalizedKey.filePath);

        std::wstring cacheDirectory;
        Entry entry;
        {
            std::scoped_lock lock(mutex_);
            EnsureLoadedLocked();
            cacheDirectory = EnsureCacheDirectoryLocked();
            entry.cacheFileName = BuildCacheFileName(normalizedKey);
            entry.fileBytes = sizeof(DiskThumbnailHeader) + pixels.size();
            entry.lastAccessOrdinal = nextAccessOrdinal_++;

            const auto existing = entries_.find(normalizedKey);
            if (existing != entries_.end())
            {
                currentBytes_ -= existing->second.fileBytes;
                entries_.erase(existing);
            }

            entries_[normalizedKey] = entry;
            currentBytes_ += entry.fileBytes;
            EvictIfNeededLocked();
            SaveIndexLocked();
        }

        DiskThumbnailHeader header{};
        std::copy(kDiskThumbnailMagic.begin(), kDiskThumbnailMagic.end(), std::begin(header.magic));
        header.width = static_cast<std::uint32_t>(width);
        header.height = static_cast<std::uint32_t>(height);
        header.sourceWidth = static_cast<std::uint32_t>(thumbnail->SourceWidth());
        header.sourceHeight = static_cast<std::uint32_t>(thumbnail->SourceHeight());
        header.pixelBytes = static_cast<std::uint64_t>(pixels.size());

        std::ofstream stream(fs::path(cacheDirectory) / entry.cacheFileName, std::ios::binary | std::ios::trunc);
        if (!stream)
        {
            return;
        }

        stream.write(reinterpret_cast<const char*>(&header), sizeof(header));
        stream.write(reinterpret_cast<const char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
    }

    void DiskThumbnailCache::InvalidateFilePaths(const std::vector<std::wstring>& filePaths)
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

        std::vector<std::wstring> cacheFilesToDelete;
        {
            std::scoped_lock lock(mutex_);
            EnsureLoadedLocked();
            for (auto iterator = entries_.begin(); iterator != entries_.end();)
            {
                const bool shouldErase = std::find(normalizedPaths.begin(), normalizedPaths.end(), iterator->first.filePath) != normalizedPaths.end();
                if (!shouldErase)
                {
                    ++iterator;
                    continue;
                }

                currentBytes_ -= iterator->second.fileBytes;
                cacheFilesToDelete.push_back(iterator->second.cacheFileName);
                iterator = entries_.erase(iterator);
            }
            SaveIndexLocked();
        }

        if (!cacheFilesToDelete.empty())
        {
            const fs::path cacheDirectory = cacheDirectory_;
            for (const std::wstring& cacheFileName : cacheFilesToDelete)
            {
                std::error_code error;
                fs::remove(cacheDirectory / cacheFileName, error);
            }
        }
    }

    void DiskThumbnailCache::Clear()
    {
        std::vector<std::wstring> cacheFilesToDelete;
        {
            std::scoped_lock lock(mutex_);
            EnsureLoadedLocked();
            for (const auto& [_, entry] : entries_)
            {
                cacheFilesToDelete.push_back(entry.cacheFileName);
            }
            entries_.clear();
            currentBytes_ = 0;
            SaveIndexLocked();
        }

        const fs::path cacheDirectory = cacheDirectory_;
        for (const std::wstring& cacheFileName : cacheFilesToDelete)
        {
            std::error_code error;
            fs::remove(cacheDirectory / cacheFileName, error);
        }
    }

    std::size_t DiskThumbnailCache::CurrentBytes() const
    {
        std::scoped_lock lock(mutex_);
        return currentBytes_;
    }

    std::size_t DiskThumbnailCache::CapacityBytes() const noexcept
    {
        return capacityBytes_;
    }

    void DiskThumbnailCache::EnsureLoadedLocked()
    {
        if (loaded_)
        {
            return;
        }

        LoadIndexLocked();
        loaded_ = true;
    }

    bool DiskThumbnailCache::LoadIndexLocked()
    {
        entries_.clear();
        currentBytes_ = 0;
        nextAccessOrdinal_ = 1;

        const fs::path indexPath = fs::path(EnsureCacheDirectoryLocked()) / kIndexFileName;
        std::wifstream stream(indexPath);
        if (!stream)
        {
            return false;
        }

        std::wstring line;
        while (std::getline(stream, line))
        {
            if (line.empty())
            {
                continue;
            }

            const std::vector<std::wstring> fields = SplitTabFields(line);
            if (fields.size() != 7)
            {
                continue;
            }

            ThumbnailCacheKey key;
            key.filePath = fields[0];
            key.modifiedTimestampUtc = _wcstoui64(fields[1].c_str(), nullptr, 10);
            key.targetWidth = _wtoi(fields[2].c_str());
            key.targetHeight = _wtoi(fields[3].c_str());

            Entry entry;
            entry.cacheFileName = fields[4];
            entry.fileBytes = static_cast<std::size_t>(_wcstoui64(fields[5].c_str(), nullptr, 10));
            entry.lastAccessOrdinal = _wcstoui64(fields[6].c_str(), nullptr, 10);
            nextAccessOrdinal_ = std::max(nextAccessOrdinal_, entry.lastAccessOrdinal + 1);
            currentBytes_ += entry.fileBytes;
            entries_.emplace(std::move(key), std::move(entry));
        }

        return true;
    }

    void DiskThumbnailCache::SaveIndexLocked() const
    {
        const fs::path indexPath = fs::path(cacheDirectory_) / kIndexFileName;
        std::wofstream stream(indexPath, std::ios::trunc);
        if (!stream)
        {
            return;
        }

        for (const auto& [key, entry] : entries_)
        {
            stream << BuildIndexLine(key, entry.cacheFileName, entry.fileBytes, entry.lastAccessOrdinal) << L'\n';
        }
    }

    void DiskThumbnailCache::EvictIfNeededLocked()
    {
        if (currentBytes_ <= capacityBytes_)
        {
            return;
        }

        std::vector<ThumbnailCacheKey> evictionOrder;
        evictionOrder.reserve(entries_.size());
        for (const auto& [key, _] : entries_)
        {
            evictionOrder.push_back(key);
        }
        std::sort(evictionOrder.begin(), evictionOrder.end(), [&](const ThumbnailCacheKey& lhs, const ThumbnailCacheKey& rhs)
        {
            return entries_[lhs].lastAccessOrdinal < entries_[rhs].lastAccessOrdinal;
        });

        const fs::path cacheDirectory = cacheDirectory_;
        for (const ThumbnailCacheKey& key : evictionOrder)
        {
            if (currentBytes_ <= capacityBytes_)
            {
                break;
            }

            const auto iterator = entries_.find(key);
            if (iterator == entries_.end())
            {
                continue;
            }

            std::error_code error;
            fs::remove(cacheDirectory / iterator->second.cacheFileName, error);
            currentBytes_ -= iterator->second.fileBytes;
            entries_.erase(iterator);
        }
    }

    std::wstring DiskThumbnailCache::EnsureCacheDirectoryLocked()
    {
        if (!cacheDirectory_.empty())
        {
            return cacheDirectory_;
        }

        const std::wstring localAppDataPath = TryGetLocalAppDataPath();
        if (localAppDataPath.empty())
        {
            return {};
        }

        const fs::path cacheDirectory = fs::path(localAppDataPath) / kCacheRootFolder;
        std::error_code error;
        fs::create_directories(cacheDirectory, error);
        if (error)
        {
            return {};
        }

        cacheDirectory_ = cacheDirectory.wstring();
        return cacheDirectory_;
    }
}
