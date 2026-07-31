#include "cache/DiskThumbnailCache.h"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "util/HashUtils.h"
#include "util/PathUtils.h"

namespace
{
    namespace fs = std::filesystem;

    constexpr std::size_t kDefaultDiskThumbnailCacheCapacityBytes = 512ULL * 1024ULL * 1024ULL;
    constexpr std::uint32_t kMaximumThumbnailDimension = 4096;
    constexpr std::uint64_t kMaximumThumbnailPixelBytes = static_cast<std::uint64_t>(kMaximumThumbnailDimension)
        * kMaximumThumbnailDimension * 4U;
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

    struct ParsedIndexEntry
    {
        hyperbrowse::cache::ThumbnailCacheKey key;
        std::wstring cacheFileName;
        std::size_t fileBytes{};
        std::uint64_t lastAccessOrdinal{};
    };

    std::mutex& PersistentCacheFilesystemMutex()
    {
        static std::mutex mutex;
        return mutex;
    }

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

    bool TryParseIndexEntry(const std::wstring& line, ParsedIndexEntry* entry)
    {
        if (!entry || line.empty())
        {
            return false;
        }

        const std::vector<std::wstring> fields = SplitTabFields(line);
        if (fields.size() != 7)
        {
            return false;
        }

        ParsedIndexEntry parsed;
        parsed.key.filePath = fields[0];
        parsed.key.modifiedTimestampUtc = _wcstoui64(fields[1].c_str(), nullptr, 10);
        parsed.key.targetWidth = _wtoi(fields[2].c_str());
        parsed.key.targetHeight = _wtoi(fields[3].c_str());
        parsed.cacheFileName = fields[4];
        parsed.fileBytes = static_cast<std::size_t>(_wcstoui64(fields[5].c_str(), nullptr, 10));
        parsed.lastAccessOrdinal = _wcstoui64(fields[6].c_str(), nullptr, 10);
        *entry = std::move(parsed);
        return true;
    }
}

namespace hyperbrowse::cache
{
    DiskThumbnailCache::DiskThumbnailCache(std::size_t capacityBytes, std::wstring cacheDirectory)
        : capacityBytes_(capacityBytes == 0 ? kDefaultDiskThumbnailCacheCapacityBytes : capacityBytes)
        , cacheDirectory_(std::move(cacheDirectory))
    {
    }

    std::shared_ptr<const CachedThumbnail> DiskThumbnailCache::TryLoad(const ThumbnailCacheKey& key)
    {
        std::scoped_lock filesystemLock(PersistentCacheFilesystemMutex());

        ThumbnailCacheKey normalizedKey = key;
        normalizedKey.filePath = util::NormalizePathForComparison(normalizedKey.filePath);

        std::wstring cachePath;
        {
            std::scoped_lock lock(mutex_);
            ReloadIndexLocked();
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
        const auto removeInvalidEntry = [&]()
        {
            stream.close();
            {
                std::scoped_lock lock(mutex_);
                ReloadIndexLocked();
                const auto iterator = entries_.find(normalizedKey);
                if (iterator != entries_.end())
                {
                    currentBytes_ = iterator->second.fileBytes > currentBytes_
                        ? 0
                        : currentBytes_ - iterator->second.fileBytes;
                    entries_.erase(iterator);
                    SaveIndexLocked();
                }
            }

            std::error_code error;
            fs::remove(fs::path(cachePath), error);
        };

        if (!stream)
        {
            removeInvalidEntry();
            return {};
        }

        DiskThumbnailHeader header{};
        stream.read(reinterpret_cast<char*>(&header), sizeof(header));
        if (!stream || !std::equal(std::begin(header.magic), std::end(header.magic), kDiskThumbnailMagic.begin(), kDiskThumbnailMagic.end()))
        {
            removeInvalidEntry();
            return {};
        }

        if (header.width == 0
            || header.height == 0
            || header.width > kMaximumThumbnailDimension
            || header.height > kMaximumThumbnailDimension
            || header.sourceWidth > static_cast<std::uint32_t>(std::numeric_limits<int>::max())
            || header.sourceHeight > static_cast<std::uint32_t>(std::numeric_limits<int>::max()))
        {
            removeInvalidEntry();
            return {};
        }

        const std::uint64_t expectedPixelBytes = static_cast<std::uint64_t>(header.width)
            * static_cast<std::uint64_t>(header.height) * 4U;
        if (expectedPixelBytes == 0
            || expectedPixelBytes > kMaximumThumbnailPixelBytes
            || header.pixelBytes != expectedPixelBytes
            || header.pixelBytes > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())
            || header.pixelBytes > static_cast<std::uint64_t>(std::numeric_limits<std::streamsize>::max()))
        {
            removeInvalidEntry();
            return {};
        }

        std::error_code fileSizeError;
        const std::uintmax_t fileSize = fs::file_size(fs::path(cachePath), fileSizeError);
        if (fileSizeError
            || fileSize != sizeof(DiskThumbnailHeader) + header.pixelBytes)
        {
            removeInvalidEntry();
            return {};
        }

        std::vector<unsigned char> pixels;
        try
        {
            pixels.resize(static_cast<std::size_t>(header.pixelBytes));
        }
        catch (const std::exception&)
        {
            removeInvalidEntry();
            return {};
        }

        stream.read(reinterpret_cast<char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
        if (!stream)
        {
            removeInvalidEntry();
            return {};
        }

        HBITMAP bitmap = CreateBitmapFromPixels(static_cast<int>(header.width), static_cast<int>(header.height), pixels);
        if (!bitmap)
        {
            removeInvalidEntry();
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

        std::scoped_lock filesystemLock(PersistentCacheFilesystemMutex());

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
            ReloadIndexLocked();
            cacheDirectory = EnsureCacheDirectoryLocked();
            entry.cacheFileName = BuildCacheFileName(normalizedKey);
            entry.fileBytes = sizeof(DiskThumbnailHeader) + pixels.size();
            entry.lastAccessOrdinal = nextAccessOrdinal_++;
        }

        if (cacheDirectory.empty())
        {
            return;
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
        if (!stream)
        {
            return;
        }

        std::scoped_lock lock(mutex_);
        ReloadIndexLocked();
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

    void DiskThumbnailCache::InvalidateFilePaths(const std::vector<std::wstring>& filePaths)
    {
        if (filePaths.empty())
        {
            return;
        }

        std::scoped_lock filesystemLock(PersistentCacheFilesystemMutex());

        std::vector<std::wstring> normalizedPaths;
        normalizedPaths.reserve(filePaths.size());
        for (const std::wstring& filePath : filePaths)
        {
            normalizedPaths.push_back(util::NormalizePathForComparison(filePath));
        }

        std::vector<std::wstring> cacheFilesToDelete;
        {
            std::scoped_lock lock(mutex_);
            ReloadIndexLocked();
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
        std::scoped_lock filesystemLock(PersistentCacheFilesystemMutex());

        std::vector<std::wstring> cacheFilesToDelete;
        std::wstring cacheDirectory;
        {
            std::scoped_lock lock(mutex_);
            ReloadIndexLocked();
            cacheDirectory = EnsureCacheDirectoryLocked();
            for (const auto& [_, entry] : entries_)
            {
                cacheFilesToDelete.push_back(entry.cacheFileName);
            }
            entries_.clear();
            currentBytes_ = 0;
            SaveIndexLocked();

            std::error_code directoryError;
            for (const fs::directory_entry& directoryEntry : fs::directory_iterator(fs::path(cacheDirectory), directoryError))
            {
                if (directoryError)
                {
                    break;
                }

                std::error_code fileError;
                if (!directoryEntry.is_regular_file(fileError) || fileError)
                {
                    continue;
                }

                cacheFilesToDelete.push_back(directoryEntry.path().filename().wstring());
            }
        }

        const fs::path cacheDirectoryPath(cacheDirectory);
        for (const std::wstring& cacheFileName : cacheFilesToDelete)
        {
            std::error_code error;
            fs::remove(cacheDirectoryPath / cacheFileName, error);
        }
    }

    bool DiskThumbnailCache::Compact()
    {
        std::scoped_lock filesystemLock(PersistentCacheFilesystemMutex());
        std::scoped_lock lock(mutex_);

        const std::wstring cacheDirectory = EnsureCacheDirectoryLocked();
        if (cacheDirectory.empty())
        {
            return false;
        }

        ReloadIndexLocked();

        std::unordered_map<std::wstring, std::size_t> existingCacheFiles;
        std::error_code directoryError;
        for (const fs::directory_entry& directoryEntry : fs::directory_iterator(fs::path(cacheDirectory), directoryError))
        {
            if (directoryError)
            {
                break;
            }

            std::error_code fileError;
            if (!directoryEntry.is_regular_file(fileError) || fileError)
            {
                continue;
            }

            const std::wstring fileName = directoryEntry.path().filename().wstring();
            if (fileName == kIndexFileName)
            {
                continue;
            }

            const std::uintmax_t fileSize = directoryEntry.file_size(fileError);
            if (fileError)
            {
                continue;
            }

            existingCacheFiles[fileName] = static_cast<std::size_t>(fileSize);
        }

        currentBytes_ = 0;
        nextAccessOrdinal_ = 1;
        std::unordered_set<std::wstring> referencedCacheFiles;
        for (auto iterator = entries_.begin(); iterator != entries_.end();)
        {
            const auto fileIterator = existingCacheFiles.find(iterator->second.cacheFileName);
            if (fileIterator == existingCacheFiles.end())
            {
                iterator = entries_.erase(iterator);
                continue;
            }

            iterator->second.fileBytes = fileIterator->second;
            currentBytes_ += iterator->second.fileBytes;
            nextAccessOrdinal_ = std::max(nextAccessOrdinal_, iterator->second.lastAccessOrdinal + 1);
            referencedCacheFiles.insert(iterator->second.cacheFileName);
            ++iterator;
        }

        const fs::path cacheDirectoryPath(cacheDirectory);
        for (const auto& [fileName, _] : existingCacheFiles)
        {
            if (referencedCacheFiles.contains(fileName))
            {
                continue;
            }

            std::error_code removeError;
            fs::remove(cacheDirectoryPath / fileName, removeError);
        }

        EvictIfNeededLocked();
        SaveIndexLocked();
        return true;
    }

    DiskThumbnailCache::Statistics DiskThumbnailCache::QueryStatistics() const
    {
        Statistics statistics{};
        statistics.capacityBytes = capacityBytes_;

        std::scoped_lock filesystemLock(PersistentCacheFilesystemMutex());
        std::scoped_lock lock(mutex_);

        auto* self = const_cast<DiskThumbnailCache*>(this);
        const std::wstring cacheDirectory = self->EnsureCacheDirectoryLocked();
        statistics.cacheDirectory = cacheDirectory;
        if (cacheDirectory.empty())
        {
            return statistics;
        }

        self->ReloadIndexLocked();
        statistics.indexedEntryCount = entries_.size();
        statistics.indexedBytes = currentBytes_;

        std::unordered_set<std::wstring> referencedCacheFiles;
        referencedCacheFiles.reserve(entries_.size());
        for (const auto& [_, entry] : entries_)
        {
            referencedCacheFiles.insert(entry.cacheFileName);
        }

        const fs::path cacheDirectoryPath(cacheDirectory);
        std::error_code directoryError;
        for (const fs::directory_entry& directoryEntry : fs::directory_iterator(cacheDirectoryPath, directoryError))
        {
            if (directoryError)
            {
                break;
            }

            std::error_code fileError;
            if (!directoryEntry.is_regular_file(fileError) || fileError)
            {
                continue;
            }

            const std::uintmax_t fileSize = directoryEntry.file_size(fileError);
            if (fileError)
            {
                continue;
            }

            const std::wstring fileName = directoryEntry.path().filename().wstring();
            if (fileName == kIndexFileName)
            {
                statistics.indexFileBytes = static_cast<std::size_t>(fileSize);
                continue;
            }

            statistics.cacheFileCount += 1;
            statistics.cacheFileBytes += static_cast<std::size_t>(fileSize);
            if (!referencedCacheFiles.contains(fileName))
            {
                statistics.orphanFileCount += 1;
                statistics.orphanFileBytes += static_cast<std::size_t>(fileSize);
            }
        }

        for (const auto& [_, entry] : entries_)
        {
            std::error_code existsError;
            if (!fs::exists(cacheDirectoryPath / entry.cacheFileName, existsError) || existsError)
            {
                statistics.missingFileCount += 1;
            }
        }

        return statistics;
    }

    std::size_t DiskThumbnailCache::CurrentBytes() const
    {
        std::scoped_lock filesystemLock(PersistentCacheFilesystemMutex());
        std::scoped_lock lock(mutex_);
        const_cast<DiskThumbnailCache*>(this)->ReloadIndexLocked();
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

        ReloadIndexLocked();
    }

    void DiskThumbnailCache::ReloadIndexLocked()
    {
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
            ParsedIndexEntry parsedEntry;
            if (!TryParseIndexEntry(line, &parsedEntry))
            {
                continue;
            }

            Entry entry;
            entry.cacheFileName = std::move(parsedEntry.cacheFileName);
            entry.fileBytes = parsedEntry.fileBytes;
            entry.lastAccessOrdinal = parsedEntry.lastAccessOrdinal;
            nextAccessOrdinal_ = std::max(nextAccessOrdinal_, entry.lastAccessOrdinal + 1);
            currentBytes_ += entry.fileBytes;
            entries_.emplace(std::move(parsedEntry.key), std::move(entry));
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
