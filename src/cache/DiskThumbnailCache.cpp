#include "cache/DiskThumbnailCache.h"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "util/PathUtils.h"
#include "util/Diagnostics.h"

namespace
{
    namespace fs = std::filesystem;

    constexpr std::size_t kDefaultDiskThumbnailCacheCapacityBytes = 512ULL * 1024ULL * 1024ULL;
    constexpr std::uint32_t kMaximumThumbnailDimension = 4096;
    constexpr std::uint64_t kMaximumThumbnailPixelBytes = static_cast<std::uint64_t>(kMaximumThumbnailDimension)
        * kMaximumThumbnailDimension * 4U;
    constexpr std::wstring_view kCacheRootFolder = L"HyperBrowse\\thumbnail-cache";
    constexpr std::wstring_view kIndexFileName = L"index.tsv";
    constexpr std::wstring_view kFormatVersionFileName = L"format.version";
    constexpr std::wstring_view kCurrentFormatVersion = L"2";

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
    constexpr std::array<char, 8> kDiskThumbnailColorMagic{{'H', 'B', 'T', 'H', 'M', 'B', '0', '2'}};
    constexpr std::uint64_t kMaximumThumbnailFileBytes = sizeof(DiskThumbnailHeader)
        + kMaximumThumbnailPixelBytes + 8 + hyperbrowse::cache::kMaximumSourceProfileBytes;
    constexpr std::size_t kAccessPersistenceInterval = 64;
    constexpr std::size_t kMaximumCompactionRemovals = 256;
    constexpr std::size_t kLegacyMigrationBatchSize = 32;
    constexpr std::size_t kJournalCompactionThresholdBytes = 8ULL * 1024ULL * 1024ULL;
    constexpr std::wstring_view kJournalFileName = L"index.journal.tsv";

    bool EncodeUtf8(std::wstring_view value, std::string* encoded)
    {
        if (!encoded || value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        {
            return false;
        }

        encoded->clear();
        if (value.empty())
        {
            return true;
        }

        const int inputLength = static_cast<int>(value.size());
        const int required = WideCharToMultiByte(CP_UTF8,
                                                 WC_ERR_INVALID_CHARS,
                                                 value.data(),
                                                 inputLength,
                                                 nullptr,
                                                 0,
                                                 nullptr,
                                                 nullptr);
        if (required <= 0)
        {
            return false;
        }

        encoded->resize(static_cast<std::size_t>(required));
        if (WideCharToMultiByte(CP_UTF8,
                                WC_ERR_INVALID_CHARS,
                                value.data(),
                                inputLength,
                                encoded->data(),
                                required,
                                nullptr,
                                nullptr) != required)
        {
            encoded->clear();
            return false;
        }
        return true;
    }

    bool DecodeUtf8(std::string_view encoded, std::wstring* value)
    {
        if (!value || encoded.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        {
            return false;
        }

        value->clear();
        if (encoded.empty())
        {
            return true;
        }

        const int inputLength = static_cast<int>(encoded.size());
        const int required = MultiByteToWideChar(CP_UTF8,
                                                 MB_ERR_INVALID_CHARS,
                                                 encoded.data(),
                                                 inputLength,
                                                 nullptr,
                                                 0);
        if (required <= 0)
        {
            return false;
        }

        value->resize(static_cast<std::size_t>(required));
        if (MultiByteToWideChar(CP_UTF8,
                                MB_ERR_INVALID_CHARS,
                                encoded.data(),
                                inputLength,
                                value->data(),
                                required) != required)
        {
            value->clear();
            return false;
        }
        return true;
    }

    bool DecodeCacheLine(std::string_view encoded, std::wstring* value)
    {
        if (DecodeUtf8(encoded, value))
        {
            return true;
        }

        if (!value)
        {
            return false;
        }

        value->clear();
        value->reserve(encoded.size());
        for (const unsigned char character : encoded)
        {
            value->push_back(static_cast<wchar_t>(character));
        }
        return true;
    }

    bool WriteUtf8Line(std::ofstream& stream, std::wstring_view value, std::size_t* bytesWritten = nullptr)
    {
        std::string encoded;
        if (!EncodeUtf8(value, &encoded))
        {
            return false;
        }

        if (!encoded.empty())
        {
            stream.write(encoded.data(), static_cast<std::streamsize>(encoded.size()));
        }
        stream.put('\n');
        if (!stream)
        {
            return false;
        }

        if (bytesWritten)
        {
            *bytesWritten = encoded.size() + 1;
        }
        return true;
    }

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

    std::wstring TryGetCacheDirectoryOverride()
    {
        const DWORD requiredLength = GetEnvironmentVariableW(
            hyperbrowse::cache::kCacheDirectoryEnvironmentVariable, nullptr, 0);
        if (requiredLength == 0)
        {
            return {};
        }

        std::wstring path(requiredLength, L'\0');
        const DWORD copiedLength = GetEnvironmentVariableW(
            hyperbrowse::cache::kCacheDirectoryEnvironmentVariable, path.data(), requiredLength);
        if (copiedLength == 0 || copiedLength >= requiredLength)
        {
            return {};
        }
        path.resize(copiedLength);
        return path;
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

    void AppendStableHashBytes(std::uint64_t* hash, const void* data, std::size_t byteCount) noexcept
    {
        constexpr std::uint64_t kFnvOffset = 14695981039346656037ULL;
        constexpr std::uint64_t kFnvPrime = 1099511628211ULL;
        if (!hash)
        {
            return;
        }

        if (*hash == 0)
        {
            *hash = kFnvOffset;
        }

        const auto* bytes = static_cast<const unsigned char*>(data);
        for (std::size_t index = 0; index < byteCount; ++index)
        {
            *hash ^= bytes[index];
            *hash *= kFnvPrime;
        }
    }

    void AppendStableHashValue(std::uint64_t* hash, std::uint64_t value) noexcept
    {
        AppendStableHashBytes(hash, &value, sizeof(value));
        constexpr std::uint8_t kFieldSeparator = 0xff;
        AppendStableHashBytes(hash, &kFieldSeparator, sizeof(kFieldSeparator));
    }

    std::wstring BuildCacheFileName(const hyperbrowse::cache::ThumbnailCacheKey& key)
    {
        std::uint64_t hash = 0;
        const std::wstring normalizedPath = hyperbrowse::util::NormalizePathForComparison(key.filePath);
        AppendStableHashBytes(&hash, normalizedPath.data(), normalizedPath.size() * sizeof(wchar_t));
        AppendStableHashValue(&hash, key.modifiedTimestampUtc);
        AppendStableHashValue(&hash, static_cast<std::uint64_t>(key.targetWidth));
        AppendStableHashValue(&hash, static_cast<std::uint64_t>(key.targetHeight));

        wchar_t buffer[17]{};
        swprintf_s(buffer, L"%016llx", static_cast<unsigned long long>(hash));
        const std::wstring hashText(buffer);
        return hashText.substr(0, 2) + L"/" + hashText.substr(2, 2) + L"/" + hashText + L".bin";
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

    std::wstring BuildJournalRecord(wchar_t operation, std::wstring_view payload)
    {
        std::wstring record;
        record.reserve(payload.size() + 2);
        record.push_back(operation);
        record.push_back(L'\t');
        record.append(payload);
        return record;
    }

    bool TryParseUnsignedField(std::wstring_view value, std::uint64_t* parsedValue) noexcept
    {
        if (!parsedValue || value.empty())
        {
            return false;
        }

        std::uint64_t result = 0;
        for (const wchar_t character : value)
        {
            if (character < L'0' || character > L'9')
            {
                return false;
            }

            const std::uint64_t digit = static_cast<std::uint64_t>(character - L'0');
            if (result > (std::numeric_limits<std::uint64_t>::max() - digit) / 10U)
            {
                return false;
            }

            result = result * 10U + digit;
        }

        *parsedValue = result;
        return true;
    }

    bool TryParsePositiveIntField(std::wstring_view value, int* parsedValue) noexcept
    {
        std::uint64_t parsed = 0;
        if (!parsedValue
            || !TryParseUnsignedField(value, &parsed)
            || parsed == 0
            || parsed > static_cast<std::uint64_t>(std::numeric_limits<int>::max()))
        {
            return false;
        }

        *parsedValue = static_cast<int>(parsed);
        return true;
    }

    bool IsSafeCacheFileName(std::wstring_view fileName) noexcept
    {
        constexpr std::size_t kHashCharacterCount = 16;
        constexpr std::wstring_view kLegacySuffix = L".thumb";
        constexpr std::wstring_view kShardedSuffix = L".bin";
        const bool isLegacy = fileName.size() == kHashCharacterCount + kLegacySuffix.size()
            && fileName.compare(kHashCharacterCount, kLegacySuffix.size(), kLegacySuffix) == 0;
        const bool isSharded = fileName.size() == 2 + 1 + 2 + 1 + kHashCharacterCount + kShardedSuffix.size()
            && fileName[2] == L'/'
            && fileName[5] == L'/'
            && fileName.compare(22, kShardedSuffix.size(), kShardedSuffix) == 0;
        if (!isLegacy && !isSharded)
        {
            return false;
        }

        const auto isHex = [](wchar_t character) noexcept
        {
            const bool isDecimal = character >= L'0' && character <= L'9';
            const bool isLowerHex = character >= L'a' && character <= L'f';
            const bool isUpperHex = character >= L'A' && character <= L'F';
            return isDecimal || isLowerHex || isUpperHex;
        };

        if (isLegacy)
        {
            for (std::size_t index = 0; index < kHashCharacterCount; ++index)
            {
                if (!isHex(fileName[index]))
                {
                    return false;
                }
            }
            return true;
        }

        for (std::size_t index = 0; index < 2; ++index)
        {
            if (!isHex(fileName[index]) || !isHex(fileName[index + 3]) || !isHex(fileName[index + 6]))
            {
                return false;
            }
        }
        for (std::size_t index = 0; index < kHashCharacterCount; ++index)
        {
            if (!isHex(fileName[index + 6]))
            {
                return false;
            }
        }
        return true;
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

        std::uint64_t modifiedTimestampUtc = 0;
        std::uint64_t fileBytes = 0;
        std::uint64_t lastAccessOrdinal = 0;
        int targetWidth = 0;
        int targetHeight = 0;
        if (!TryParseUnsignedField(fields[1], &modifiedTimestampUtc)
            || !TryParsePositiveIntField(fields[2], &targetWidth)
            || !TryParsePositiveIntField(fields[3], &targetHeight)
            || !IsSafeCacheFileName(fields[4])
            || !TryParseUnsignedField(fields[5], &fileBytes)
            || !TryParseUnsignedField(fields[6], &lastAccessOrdinal)
            || fileBytes < sizeof(DiskThumbnailHeader)
            || fileBytes > kMaximumThumbnailFileBytes
            || fileBytes > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())
            || lastAccessOrdinal == 0
            || lastAccessOrdinal == std::numeric_limits<std::uint64_t>::max())
        {
            return false;
        }

        ParsedIndexEntry parsed;
        parsed.key.filePath = fields[0];
        parsed.key.modifiedTimestampUtc = modifiedTimestampUtc;
        parsed.key.targetWidth = targetWidth;
        parsed.key.targetHeight = targetHeight;
        parsed.cacheFileName = fields[4];
        parsed.fileBytes = static_cast<std::size_t>(fileBytes);
        parsed.lastAccessOrdinal = lastAccessOrdinal;
        *entry = std::move(parsed);
        return true;
    }
}

namespace hyperbrowse::cache
{
    std::uint64_t DiskThumbnailCache::QueryDefaultCacheVolumeFreeBytes() noexcept
    {
        std::wstring cacheVolumePath = TryGetCacheDirectoryOverride();
        if (cacheVolumePath.empty())
        {
            cacheVolumePath = TryGetLocalAppDataPath();
        }
        if (cacheVolumePath.empty())
        {
            return 0;
        }

        ULARGE_INTEGER availableBytes{};
        if (!GetDiskFreeSpaceExW(cacheVolumePath.c_str(), &availableBytes, nullptr, nullptr))
        {
            return 0;
        }

        return availableBytes.QuadPart;
    }

    DiskThumbnailCache::DiskThumbnailCache(std::size_t capacityBytes, std::wstring cacheDirectory)
        : capacityBytes_(capacityBytes == 0 ? kDefaultDiskThumbnailCacheCapacityBytes : capacityBytes)
        , cacheDirectory_(std::move(cacheDirectory))
    {
        if (cacheDirectory_.empty())
        {
            cacheDirectory_ = TryGetCacheDirectoryOverride();
        }
    }

    DiskThumbnailCache::~DiskThumbnailCache()
    {
        FlushPendingAccessUpdates();
    }

    std::shared_ptr<const CachedThumbnail> DiskThumbnailCache::TryLoad(const ThumbnailCacheKey& key)
    {
        std::scoped_lock filesystemLock(PersistentCacheFilesystemMutex());

        ThumbnailCacheKey normalizedKey = key;
        normalizedKey.filePath = util::NormalizePathForComparison(normalizedKey.filePath);

        std::wstring cachePath;
        bool flushAccessUpdates = false;
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
            ++pendingAccessUpdates_;
            pendingAccessKeys_.push_back(normalizedKey);
            flushAccessUpdates = pendingAccessUpdates_ >= kAccessPersistenceInterval;
        }
        if (flushAccessUpdates)
        {
            std::scoped_lock lock(mutex_);
            if (!FlushPendingAccessUpdatesLocked())
            {
                util::IncrementCounter(L"persistent_cache.access_flush_failed");
            }
        }
        std::ifstream stream(fs::path(cachePath), std::ios::binary);
        const auto removeInvalidEntry = [&]()
        {
            util::IncrementCounter(L"persistent_cache.invalid_entries");
            stream.close();
            {
                std::scoped_lock lock(mutex_);
                EnsureLoadedLocked();
                const auto iterator = entries_.find(normalizedKey);
                if (iterator != entries_.end())
                {
                    const std::wstring journalRecord = BuildJournalRecord(
                        L'R',
                        BuildIndexLine(normalizedKey,
                                       iterator->second.cacheFileName,
                                       iterator->second.fileBytes,
                                       iterator->second.lastAccessOrdinal));
                    currentBytes_ = iterator->second.fileBytes > currentBytes_
                        ? 0
                        : currentBytes_ - iterator->second.fileBytes;
                    entries_.erase(iterator);
                    AppendJournalRecordLocked(journalRecord);
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
        const bool hasColorMetadata = std::equal(std::begin(header.magic), std::end(header.magic), kDiskThumbnailColorMagic.begin());
        if (!stream || (!hasColorMetadata
            && !std::equal(std::begin(header.magic), std::end(header.magic), kDiskThumbnailMagic.begin())))
        {
            removeInvalidEntry();
            return {};
        }

        if (header.width == 0
            || header.height == 0
            || header.sourceWidth == 0
            || header.sourceHeight == 0
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
            || fileSize < sizeof(DiskThumbnailHeader) + header.pixelBytes
            || fileSize > kMaximumThumbnailFileBytes
            || (!hasColorMetadata && fileSize != sizeof(DiskThumbnailHeader) + header.pixelBytes))
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
        SourceColorInfo sourceColor;
        if (!stream || (hasColorMetadata && !ReadSourceColorInfo(stream,
            static_cast<std::size_t>(fileSize - sizeof(DiskThumbnailHeader) - header.pixelBytes), &sourceColor)))
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
                                                 static_cast<int>(header.sourceHeight),
                                                 std::move(sourceColor));
    }

    void DiskThumbnailCache::Store(const ThumbnailCacheKey& key, std::shared_ptr<const CachedThumbnail> thumbnail)
    {
        if (!thumbnail || SerializedSourceColorBytes(thumbnail->SourceColor()) == 0)
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
            EnsureLoadedLocked();
            cacheDirectory = EnsureCacheDirectoryLocked();
            entry.cacheFileName = BuildCacheFileName(normalizedKey);
            for (const auto& [existingKey, existingEntry] : entries_)
            {
                if (existingKey != normalizedKey && existingEntry.cacheFileName == entry.cacheFileName)
                {
                    util::IncrementCounter(L"persistent_cache.hash_collisions");
                    return;
                }
            }
            entry.fileBytes = sizeof(DiskThumbnailHeader) + pixels.size() + SerializedSourceColorBytes(thumbnail->SourceColor());
            entry.lastAccessOrdinal = nextAccessOrdinal_++;
        }

        if (cacheDirectory.empty())
        {
            return;
        }

        const fs::path cachePath = fs::path(cacheDirectory) / entry.cacheFileName;
        std::error_code directoryError;
        fs::create_directories(cachePath.parent_path(), directoryError);
        if (directoryError)
        {
            return;
        }

        DiskThumbnailHeader header{};
        std::copy(kDiskThumbnailColorMagic.begin(), kDiskThumbnailColorMagic.end(), std::begin(header.magic));
        header.width = static_cast<std::uint32_t>(width);
        header.height = static_cast<std::uint32_t>(height);
        header.sourceWidth = static_cast<std::uint32_t>(thumbnail->SourceWidth());
        header.sourceHeight = static_cast<std::uint32_t>(thumbnail->SourceHeight());
        header.pixelBytes = static_cast<std::uint64_t>(pixels.size());

        const fs::path temporaryPath = fs::path(cachePath.wstring() + L".tmp." + std::to_wstring(GetCurrentProcessId()));
        std::ofstream stream(temporaryPath, std::ios::binary | std::ios::trunc);
        if (!stream)
        {
            return;
        }

        stream.write(reinterpret_cast<const char*>(&header), sizeof(header));
        stream.write(reinterpret_cast<const char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
        if (!WriteSourceColorInfo(stream, thumbnail->SourceColor()))
        {
            stream.close();
            std::error_code error;
            fs::remove(temporaryPath, error);
            return;
        }

        stream.close();
        if (!MoveFileExW(temporaryPath.c_str(),
                         cachePath.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            std::error_code error;
            fs::remove(temporaryPath, error);
            return;
        }

        {
            std::scoped_lock lock(mutex_);
            EnsureLoadedLocked();
            const auto existing = entries_.find(normalizedKey);
            if (existing != entries_.end())
            {
                currentBytes_ -= existing->second.fileBytes;
                entries_.erase(existing);
            }

            entries_[normalizedKey] = entry;
            currentBytes_ += entry.fileBytes;
            AppendJournalRecordLocked(BuildJournalRecord(
                L'P',
                BuildIndexLine(normalizedKey,
                               entry.cacheFileName,
                               entry.fileBytes,
                               entry.lastAccessOrdinal)));
            EvictIfNeededLocked();
        }
    }

    void DiskThumbnailCache::InvalidateFilePaths(const std::vector<std::wstring>& filePaths)
    {
        if (filePaths.empty())
        {
            return;
        }

        std::scoped_lock filesystemLock(PersistentCacheFilesystemMutex());

        std::unordered_set<std::wstring> normalizedPaths;
        normalizedPaths.reserve(filePaths.size());
        for (const std::wstring& filePath : filePaths)
        {
            normalizedPaths.insert(util::NormalizePathForComparison(filePath));
        }

        std::vector<fs::path> cacheFilesToDelete;
        {
            std::scoped_lock lock(mutex_);
            EnsureLoadedLocked();
            for (auto iterator = entries_.begin(); iterator != entries_.end();)
            {
                const bool shouldErase = normalizedPaths.contains(iterator->first.filePath);
                if (!shouldErase)
                {
                    ++iterator;
                    continue;
                }

                currentBytes_ -= iterator->second.fileBytes;
                cacheFilesToDelete.push_back(fs::path(cacheDirectory_) / iterator->second.cacheFileName);
                iterator = entries_.erase(iterator);
            }
            for (const std::wstring& normalizedPath : normalizedPaths)
            {
                AppendJournalRecordLocked(BuildJournalRecord(L'I', EscapeField(normalizedPath)));
            }
        }

        if (!cacheFilesToDelete.empty())
        {
            for (const fs::path& cacheFilePath : cacheFilesToDelete)
            {
                std::error_code error;
                fs::remove(cacheFilePath, error);
            }
        }
    }

    void DiskThumbnailCache::Clear()
    {
        std::scoped_lock filesystemLock(PersistentCacheFilesystemMutex());

        std::vector<fs::path> cacheFilesToDelete;
        std::wstring cacheDirectory;
        {
            std::scoped_lock lock(mutex_);
            EnsureLoadedLocked();
            cacheDirectory = EnsureCacheDirectoryLocked();
            for (const auto& [_, entry] : entries_)
            {
                cacheFilesToDelete.push_back(fs::path(cacheDirectory) / entry.cacheFileName);
            }
            entries_.clear();
            currentBytes_ = 0;
            AppendJournalRecordLocked(L"C");

            std::error_code directoryError;
            for (const fs::directory_entry& directoryEntry : fs::recursive_directory_iterator(fs::path(cacheDirectory), directoryError))
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

                cacheFilesToDelete.push_back(directoryEntry.path());
            }
        }

        for (const fs::path& cacheFilePath : cacheFilesToDelete)
        {
            std::error_code error;
            fs::remove(cacheFilePath, error);
        }

        std::scoped_lock lock(mutex_);
        CompactIndexLocked();
    }

    bool DiskThumbnailCache::Compact()
    {
        util::Stopwatch compactionTimer;
        std::scoped_lock filesystemLock(PersistentCacheFilesystemMutex());
        std::scoped_lock lock(mutex_);

        const std::wstring cacheDirectory = EnsureCacheDirectoryLocked();
        if (cacheDirectory.empty())
        {
            util::RecordTiming(L"persistent_cache.compaction", compactionTimer.ElapsedMilliseconds());
            return false;
        }

        EnsureLoadedLocked();

        if (!FlushPendingAccessUpdatesLocked())
        {
            util::IncrementCounter(L"persistent_cache.access_flush_failed");
            return false;
        }

        for (auto iterator = entries_.begin(); iterator != entries_.end();)
        {
            std::error_code sourceError;
            const bool sourceExists = fs::exists(fs::path(iterator->first.filePath), sourceError);
            const bool sourceMissing = !sourceExists
                && (!sourceError || sourceError == std::make_error_code(std::errc::no_such_file_or_directory));
            if (!sourceMissing)
            {
                if (sourceError)
                {
                    util::IncrementCounter(L"persistent_cache.source_access_unavailable");
                }
                ++iterator;
                continue;
            }

            const std::wstring journalRecord = BuildJournalRecord(
                L'R',
                BuildIndexLine(iterator->first,
                               iterator->second.cacheFileName,
                               iterator->second.fileBytes,
                               iterator->second.lastAccessOrdinal));
            if (!AppendJournalRecordLocked(journalRecord))
            {
                ++iterator;
                continue;
            }

            currentBytes_ = iterator->second.fileBytes > currentBytes_
                ? 0
                : currentBytes_ - iterator->second.fileBytes;
            iterator = entries_.erase(iterator);
            util::IncrementCounter(L"persistent_cache.source_missing_removed");
        }

        std::unordered_map<std::wstring, std::size_t> existingCacheFiles;
        std::error_code directoryError;
        for (const fs::directory_entry& directoryEntry : fs::recursive_directory_iterator(fs::path(cacheDirectory), directoryError))
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

            std::error_code relativePathError;
            const fs::path relativePath = fs::relative(directoryEntry.path(), fs::path(cacheDirectory), relativePathError);
            if (relativePathError)
            {
                continue;
            }

            const std::wstring fileName = relativePath.generic_wstring();
            if (fileName == kIndexFileName || fileName == kJournalFileName)
            {
                continue;
            }
            if (fileName == kFormatVersionFileName)
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
        std::size_t removedOrphanCount = 0;
        bool orphanCleanupDeferred = false;
        for (const auto& [fileName, _] : existingCacheFiles)
        {
            if (referencedCacheFiles.contains(fileName))
            {
                continue;
            }

            if (removedOrphanCount >= kMaximumCompactionRemovals)
            {
                orphanCleanupDeferred = true;
                continue;
            }

            std::error_code removeError;
            fs::remove(cacheDirectoryPath / fileName, removeError);
            if (!removeError)
            {
                ++removedOrphanCount;
            }
        }

        EvictIfNeededLocked();
        const bool compacted = CompactIndexLocked();
        if (compacted && orphanCleanupDeferred)
        {
            compactionRequested_ = true;
            util::IncrementCounter(L"persistent_cache.compaction.deferred");
        }
        util::RecordTiming(L"persistent_cache.compaction", compactionTimer.ElapsedMilliseconds());
        return compacted;
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

        self->EnsureLoadedLocked();
        statistics.indexedEntryCount = entries_.size();
        statistics.indexedBytes = currentBytes_;

        std::unordered_set<std::wstring> referencedCacheFiles;
        referencedCacheFiles.reserve(entries_.size());
        std::map<std::wstring, Statistics::ShardStatistics> shardStatistics;
        const auto shardNameFor = [](std::wstring_view fileName)
        {
            const std::size_t firstSeparator = fileName.find(L'/');
            if (firstSeparator == std::wstring_view::npos)
            {
                return std::wstring(L"(root)");
            }

            const std::size_t secondSeparator = fileName.find(L'/', firstSeparator + 1);
            if (secondSeparator == std::wstring_view::npos)
            {
                return std::wstring(fileName.substr(0, firstSeparator));
            }
            return std::wstring(fileName.substr(0, secondSeparator));
        };
        for (const auto& [_, entry] : entries_)
        {
            referencedCacheFiles.insert(entry.cacheFileName);
            auto& shard = shardStatistics[shardNameFor(entry.cacheFileName)];
            ++shard.indexedEntryCount;
            shard.indexedBytes += entry.fileBytes;
        }

        const fs::path cacheDirectoryPath(cacheDirectory);
        std::error_code directoryError;
        for (const fs::directory_entry& directoryEntry : fs::recursive_directory_iterator(cacheDirectoryPath, directoryError))
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

            std::error_code relativePathError;
            const fs::path relativePath = fs::relative(directoryEntry.path(), cacheDirectoryPath, relativePathError);
            if (relativePathError)
            {
                continue;
            }

            const std::wstring fileName = relativePath.generic_wstring();
            if (fileName == kIndexFileName)
            {
                statistics.indexFileBytes = static_cast<std::size_t>(fileSize);
                continue;
            }
            if (fileName == kJournalFileName)
            {
                continue;
            }
            if (fileName == kFormatVersionFileName)
            {
                continue;
            }

            statistics.cacheFileCount += 1;
            statistics.cacheFileBytes += static_cast<std::size_t>(fileSize);
            auto& shard = shardStatistics[shardNameFor(fileName)];
            ++shard.fileCount;
            shard.fileBytes += static_cast<std::size_t>(fileSize);
            if (!referencedCacheFiles.contains(fileName))
            {
                statistics.orphanFileCount += 1;
                statistics.orphanFileBytes += static_cast<std::size_t>(fileSize);
                ++shard.orphanFileCount;
                shard.orphanFileBytes += static_cast<std::size_t>(fileSize);
            }
        }

        for (const auto& [_, entry] : entries_)
        {
            std::error_code existsError;
            if (!fs::exists(cacheDirectoryPath / entry.cacheFileName, existsError) || existsError)
            {
                statistics.missingFileCount += 1;
                ++shardStatistics[shardNameFor(entry.cacheFileName)].missingFileCount;
            }
        }

        for (const auto& [key, _] : entries_)
        {
            std::error_code sourceError;
            const bool sourceExists = fs::exists(fs::path(key.filePath), sourceError);
            const bool sourceMissing = !sourceExists
                && (!sourceError || sourceError == std::make_error_code(std::errc::no_such_file_or_directory));
            if (sourceMissing)
            {
                ++statistics.missingSourceCount;
            }
            else if (sourceError)
            {
                ++statistics.inaccessibleSourceCount;
            }
        }

        statistics.shards.reserve(shardStatistics.size());
        for (auto& [name, shard] : shardStatistics)
        {
            shard.name = std::move(name);
            statistics.shards.push_back(std::move(shard));
        }

        return statistics;
    }

    std::size_t DiskThumbnailCache::CurrentBytes() const
    {
        std::scoped_lock filesystemLock(PersistentCacheFilesystemMutex());
        std::scoped_lock lock(mutex_);
        const_cast<DiskThumbnailCache*>(this)->EnsureLoadedLocked();
        return currentBytes_;
    }

    void DiskThumbnailCache::SetCapacityBytes(std::size_t capacityBytes)
    {
        std::scoped_lock filesystemLock(PersistentCacheFilesystemMutex());
        std::scoped_lock lock(mutex_);
        capacityBytes_.store(std::max<std::size_t>(1, capacityBytes), std::memory_order_relaxed);
        EnsureLoadedLocked();
        EvictIfNeededLocked();
    }

    std::size_t DiskThumbnailCache::CapacityBytes() const noexcept
    {
        return capacityBytes_.load(std::memory_order_relaxed);
    }

    void DiskThumbnailCache::EnsureLoadedLocked()
    {
        if (loaded_)
        {
            MigrateLegacyLayoutLocked();
            return;
        }

        ReloadIndexLocked();
    }

    void DiskThumbnailCache::ReloadIndexLocked()
    {
        LoadIndexLocked();
        loaded_ = true;
    }

    bool DiskThumbnailCache::AppendJournalRecordLocked(std::wstring_view record)
    {
        if (cacheDirectory_.empty() || record.empty())
        {
            return false;
        }

        const fs::path journalPath = fs::path(cacheDirectory_) / kJournalFileName;
        std::ofstream stream(journalPath, std::ios::binary | std::ios::app);
        if (!stream)
        {
            return false;
        }

        std::size_t bytesWritten = 0;
        if (!WriteUtf8Line(stream, record, &bytesWritten))
        {
            return false;
        }
        stream.flush();
        if (!stream)
        {
            return false;
        }

        if (bytesWritten <= std::numeric_limits<std::size_t>::max() - journalBytes_)
        {
            journalBytes_ += bytesWritten;
            if (journalBytes_ >= kJournalCompactionThresholdBytes)
            {
                compactionRequested_ = true;
            }
        }
        return true;
    }

    void DiskThumbnailCache::ReplayJournalRecordLocked(const std::wstring& record)
    {
        if (record == L"C")
        {
            entries_.clear();
            currentBytes_ = 0;
            nextAccessOrdinal_ = 1;
            return;
        }

        if (record.size() < 3 || record[1] != L'\t')
        {
            return;
        }

        const wchar_t operation = record[0];
        if (operation == L'I')
        {
            const std::wstring path = UnescapeField(std::wstring_view(record).substr(2));
            for (auto iterator = entries_.begin(); iterator != entries_.end();)
            {
                if (iterator->first.filePath != path)
                {
                    ++iterator;
                    continue;
                }

                currentBytes_ = iterator->second.fileBytes > currentBytes_
                    ? 0
                    : currentBytes_ - iterator->second.fileBytes;
                iterator = entries_.erase(iterator);
            }
            return;
        }

        if (operation != L'P' && operation != L'R' && operation != L'T')
        {
            return;
        }

        ParsedIndexEntry parsedEntry;
        if (!TryParseIndexEntry(std::wstring(record.substr(2)), &parsedEntry))
        {
            return;
        }

        const auto existing = entries_.find(parsedEntry.key);
        if (operation == L'R')
        {
            if (existing != entries_.end())
            {
                currentBytes_ = existing->second.fileBytes > currentBytes_
                    ? 0
                    : currentBytes_ - existing->second.fileBytes;
                entries_.erase(existing);
            }
            return;
        }

        if (operation == L'T')
        {
            if (existing != entries_.end())
            {
                existing->second.lastAccessOrdinal = parsedEntry.lastAccessOrdinal;
                nextAccessOrdinal_ = std::max(nextAccessOrdinal_, parsedEntry.lastAccessOrdinal + 1);
            }
            return;
        }

        Entry entry;
        entry.cacheFileName = std::move(parsedEntry.cacheFileName);
        entry.fileBytes = parsedEntry.fileBytes;
        entry.lastAccessOrdinal = parsedEntry.lastAccessOrdinal;
        if (existing != entries_.end())
        {
            currentBytes_ = existing->second.fileBytes > currentBytes_
                ? 0
                : currentBytes_ - existing->second.fileBytes;
            entries_.erase(existing);
        }

        const ThumbnailCacheKey key = parsedEntry.key;
        entries_.emplace(key, std::move(entry));
        currentBytes_ += entries_.find(key)->second.fileBytes;
        nextAccessOrdinal_ = std::max(nextAccessOrdinal_, parsedEntry.lastAccessOrdinal + 1);
    }

    bool DiskThumbnailCache::CompactIndexLocked()
    {
        if (!SaveIndexLocked())
        {
            return false;
        }

        const fs::path journalPath = fs::path(cacheDirectory_) / kJournalFileName;
        std::ofstream stream(journalPath, std::ios::binary | std::ios::trunc);
        if (!stream)
        {
            return false;
        }

        stream.flush();
        if (!stream)
        {
            return false;
        }

        pendingAccessUpdates_ = 0;
        pendingAccessKeys_.clear();
        journalBytes_ = 0;
        compactionRequested_ = false;
        return true;
    }

    bool DiskThumbnailCache::FlushPendingAccessUpdates()
    {
        std::scoped_lock filesystemLock(PersistentCacheFilesystemMutex());
        std::scoped_lock lock(mutex_);
        EnsureLoadedLocked();

        return FlushPendingAccessUpdatesLocked();
    }

    bool DiskThumbnailCache::FlushPendingAccessUpdatesLocked()
    {

        for (const ThumbnailCacheKey& key : pendingAccessKeys_)
        {
            const auto iterator = entries_.find(key);
            if (iterator == entries_.end())
            {
                continue;
            }

            if (!AppendJournalRecordLocked(BuildJournalRecord(
                    L'T',
                    BuildIndexLine(key,
                                   iterator->second.cacheFileName,
                                   iterator->second.fileBytes,
                           iterator->second.lastAccessOrdinal))))
            {
                return false;
            }
        }

        pendingAccessUpdates_ = 0;
        pendingAccessKeys_.clear();
        return true;
    }

    bool DiskThumbnailCache::NeedsCompaction() const
    {
        std::scoped_lock filesystemLock(PersistentCacheFilesystemMutex());
        std::scoped_lock lock(mutex_);
        const_cast<DiskThumbnailCache*>(this)->EnsureLoadedLocked();
        return compactionRequested_;
    }

    bool DiskThumbnailCache::LoadIndexLocked()
    {
        entries_.clear();
        currentBytes_ = 0;
        journalBytes_ = 0;
        nextAccessOrdinal_ = 1;

        const std::wstring cacheDirectory = EnsureCacheDirectoryLocked();
        if (cacheDirectory.empty())
        {
            return false;
        }

        const fs::path indexPath = fs::path(cacheDirectory) / kIndexFileName;
        std::ifstream stream(indexPath, std::ios::binary);
        if (stream)
        {
            std::string encodedLine;
            while (std::getline(stream, encodedLine))
            {
                if (!encodedLine.empty() && encodedLine.back() == '\r')
                {
                    encodedLine.pop_back();
                }

                std::wstring line;
                if (!DecodeCacheLine(encodedLine, &line))
                {
                    continue;
                }

                ParsedIndexEntry parsedEntry;
                if (!TryParseIndexEntry(line, &parsedEntry))
                {
                    continue;
                }

                Entry entry;
                entry.cacheFileName = std::move(parsedEntry.cacheFileName);
                entry.fileBytes = parsedEntry.fileBytes;
                entry.lastAccessOrdinal = parsedEntry.lastAccessOrdinal;
                if (entry.fileBytes > std::numeric_limits<std::size_t>::max() - currentBytes_)
                {
                    continue;
                }

                const auto [iterator, inserted] = entries_.emplace(std::move(parsedEntry.key), std::move(entry));
                if (!inserted)
                {
                    continue;
                }

                currentBytes_ += iterator->second.fileBytes;
                nextAccessOrdinal_ = std::max(nextAccessOrdinal_, iterator->second.lastAccessOrdinal + 1);
            }
        }
        stream.close();

        const fs::path journalPath = fs::path(cacheDirectory) / kJournalFileName;
        std::error_code journalSizeError;
        const std::uintmax_t journalFileBytes = fs::file_size(journalPath, journalSizeError);
        if (!journalSizeError)
        {
            journalBytes_ = journalFileBytes > std::numeric_limits<std::size_t>::max()
                ? std::numeric_limits<std::size_t>::max()
                : static_cast<std::size_t>(journalFileBytes);
        }
        std::ifstream journalStream(journalPath, std::ios::binary);
        if (journalStream)
        {
            std::string encodedLine;
            while (std::getline(journalStream, encodedLine))
            {
                if (journalStream.eof())
                {
                    break;
                }

                if (!encodedLine.empty() && encodedLine.back() == '\r')
                {
                    encodedLine.pop_back();
                }

                std::wstring line;
                if (!DecodeCacheLine(encodedLine, &line))
                {
                    continue;
                }
                ReplayJournalRecordLocked(line);
            }
        }
        journalStream.close();

        MigrateLegacyLayoutLocked();
        std::unordered_set<std::wstring> shardDirectories;
        for (const auto& [_, entry] : entries_)
        {
            if (!entry.cacheFileName.ends_with(L".bin"))
            {
                continue;
            }

            const std::size_t separator = entry.cacheFileName.find(L'/');
            if (separator != std::wstring::npos)
            {
                shardDirectories.insert(entry.cacheFileName.substr(0, separator));
            }
        }
        util::RecordMaximum(L"persistent_cache.shard_count", shardDirectories.size());
        return true;
    }

    bool DiskThumbnailCache::SaveIndexLocked() const
    {
        if (cacheDirectory_.empty())
        {
            return false;
        }

        const fs::path indexPath = fs::path(cacheDirectory_) / kIndexFileName;
        const fs::path temporaryPath = fs::path(indexPath.wstring() + L".tmp." + std::to_wstring(GetCurrentProcessId()));
        std::ofstream stream(temporaryPath, std::ios::binary | std::ios::trunc);
        if (!stream)
        {
            return false;
        }

        for (const auto& [key, entry] : entries_)
        {
            if (!WriteUtf8Line(stream,
                               BuildIndexLine(key, entry.cacheFileName, entry.fileBytes, entry.lastAccessOrdinal)))
            {
                stream.close();
                std::error_code error;
                fs::remove(temporaryPath, error);
                return false;
            }
        }

        stream.flush();
        if (!stream)
        {
            stream.close();
            std::error_code error;
            fs::remove(temporaryPath, error);
            return false;
        }

        stream.close();
        if (!MoveFileExW(temporaryPath.c_str(),
                         indexPath.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            std::error_code error;
            fs::remove(temporaryPath, error);
        }
        else
        {
            return true;
        }

        return false;
    }

    bool DiskThumbnailCache::WriteFormatVersionLocked() const
    {
        if (cacheDirectory_.empty())
        {
            return false;
        }

        const fs::path versionPath = fs::path(cacheDirectory_) / kFormatVersionFileName;
        const fs::path temporaryPath = fs::path(versionPath.wstring() + L".tmp." + std::to_wstring(GetCurrentProcessId()));
        std::ofstream stream(temporaryPath, std::ios::binary | std::ios::trunc);
        if (!stream || !WriteUtf8Line(stream, kCurrentFormatVersion) || (stream.flush(), !stream))
        {
            stream.close();
            std::error_code error;
            fs::remove(temporaryPath, error);
            return false;
        }

        stream.close();
        if (MoveFileExW(temporaryPath.c_str(),
                        versionPath.c_str(),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            return true;
        }

        std::error_code error;
        fs::remove(temporaryPath, error);
        return false;
    }

    bool DiskThumbnailCache::MigrateLegacyLayoutLocked()
    {
        struct Migration
        {
            ThumbnailCacheKey key;
            std::wstring oldFileName;
            std::wstring newFileName;
        };

        std::vector<Migration> migrations;
        std::unordered_set<std::wstring> migrationTargets;
        bool hasLegacyEntries = false;
        for (const auto& [key, entry] : entries_)
        {
            if (entry.cacheFileName.find(L'/') != std::wstring::npos
                || entry.cacheFileName.find(L'\\') != std::wstring::npos
                || !entry.cacheFileName.ends_with(L".thumb"))
            {
                continue;
            }

            hasLegacyEntries = true;
            if (migrations.size() >= kLegacyMigrationBatchSize)
            {
                continue;
            }
            const fs::path sourcePath = fs::path(cacheDirectory_) / entry.cacheFileName;
            std::error_code sourceError;
            if (!fs::is_regular_file(sourcePath, sourceError) || sourceError)
            {
                continue;
            }

            const std::wstring newFileName = BuildCacheFileName(key);
            if (!migrationTargets.insert(newFileName).second)
            {
                util::IncrementCounter(L"persistent_cache.hash_collisions");
                return false;
            }
            for (const auto& [otherKey, otherEntry] : entries_)
            {
                if (otherKey == key)
                {
                    continue;
                }
                if (otherEntry.cacheFileName == newFileName)
                {
                    util::IncrementCounter(L"persistent_cache.hash_collisions");
                    return false;
                }
            }

            migrations.push_back(Migration{key, entry.cacheFileName, newFileName});
        }

        if (migrations.empty())
        {
            if (!hasLegacyEntries)
            {
                WriteFormatVersionLocked();
            }
            return !hasLegacyEntries;
        }

        std::vector<fs::path> createdFiles;
        for (const Migration& migration : migrations)
        {
            const fs::path sourcePath = fs::path(cacheDirectory_) / migration.oldFileName;
            const fs::path targetPath = fs::path(cacheDirectory_) / migration.newFileName;
            std::error_code directoryError;
            fs::create_directories(targetPath.parent_path(), directoryError);
            if (directoryError)
            {
                for (const fs::path& createdFile : createdFiles)
                {
                    std::error_code error;
                    fs::remove(createdFile, error);
                }
                return false;
            }

            const fs::path temporaryPath = fs::path(targetPath.wstring() + L".migrate." + std::to_wstring(GetCurrentProcessId()));
            std::error_code copyError;
            fs::copy_file(sourcePath, temporaryPath, fs::copy_options::overwrite_existing, copyError);
            if (copyError)
            {
                util::IncrementCounter(L"persistent_cache.migration_copy_failed");
            }
            const bool moved = !copyError
                && MoveFileExW(temporaryPath.c_str(),
                               targetPath.c_str(),
                               MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
            if (!moved)
            {
                if (!copyError)
                {
                    util::IncrementCounter(L"persistent_cache.migration_rename_failed");
                }
                std::error_code error;
                fs::remove(temporaryPath, error);
                for (const fs::path& createdFile : createdFiles)
                {
                    fs::remove(createdFile, error);
                }
                return false;
            }
            createdFiles.push_back(targetPath);
        }

        for (const Migration& migration : migrations)
        {
            entries_.find(migration.key)->second.cacheFileName = migration.newFileName;
        }

        if (!SaveIndexLocked())
        {
            util::IncrementCounter(L"persistent_cache.migration_index_write_failed");
            for (const Migration& migration : migrations)
            {
                entries_.find(migration.key)->second.cacheFileName = migration.oldFileName;
            }
            for (const fs::path& createdFile : createdFiles)
            {
                std::error_code error;
                fs::remove(createdFile, error);
            }
            return false;
        }

        bool allLegacyEntriesMigrated = true;
        for (const auto& [_, entry] : entries_)
        {
            if (entry.cacheFileName.ends_with(L".thumb"))
            {
                allLegacyEntriesMigrated = false;
                break;
            }
        }
        const bool journalCompacted = CompactIndexLocked();
        if (!journalCompacted)
        {
            util::IncrementCounter(L"persistent_cache.migration_compaction_failed");
            return true;
        }

        for (const Migration& migration : migrations)
        {
            std::error_code error;
            fs::remove(fs::path(cacheDirectory_) / migration.oldFileName, error);
            if (error)
            {
                util::IncrementCounter(L"persistent_cache.migration_source_remove_failed");
            }
        }

        if (!allLegacyEntriesMigrated)
        {
            return true;
        }

        const bool formatVersionWritten = WriteFormatVersionLocked();
        if (!formatVersionWritten)
        {
            util::IncrementCounter(L"persistent_cache.migration_version_write_failed");
            return true;
        }

        util::IncrementCounter(L"persistent_cache.migration.completed");
        return true;
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
            AppendJournalRecordLocked(BuildJournalRecord(
                L'R',
                BuildIndexLine(key,
                               iterator->second.cacheFileName,
                               iterator->second.fileBytes,
                               iterator->second.lastAccessOrdinal)));
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
