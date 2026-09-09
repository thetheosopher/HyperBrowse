#include "services/UserMetadataStore.h"

#include <shlobj.h>

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <limits>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "util/PathUtils.h"

namespace
{
    namespace fs = std::filesystem;

    using hyperbrowse::services::UserMetadataEntry;

    constexpr std::wstring_view kMetadataFolder = L"HyperBrowse";
    constexpr std::wstring_view kMetadataFileName = L"user-metadata.tsv";
    constexpr std::wstring_view kMetadataLockFileName = L"user-metadata.lock";
    constexpr std::wstring_view kMetadataHeader = L"# HyperBrowse user metadata v2; encoding=utf-8";
    constexpr std::uint64_t kMaximumMetadataFileBytes = 64ULL * 1024ULL * 1024ULL;
    constexpr DWORD kLockRetryCount = 200;
    constexpr DWORD kLockRetryDelayMs = 25;
    constexpr DWORD kPublishRetryCount = 80;
    constexpr DWORD kPublishRetryDelayMs = 25;

    class UniqueHandle final
    {
    public:
        UniqueHandle() = default;
        explicit UniqueHandle(HANDLE handle) noexcept : handle_(handle) {}
        ~UniqueHandle()
        {
            Reset();
        }

        UniqueHandle(const UniqueHandle&) = delete;
        UniqueHandle& operator=(const UniqueHandle&) = delete;

        UniqueHandle(UniqueHandle&& other) noexcept : handle_(std::exchange(other.handle_, INVALID_HANDLE_VALUE)) {}
        UniqueHandle& operator=(UniqueHandle&& other) noexcept
        {
            if (this != &other)
            {
                Reset();
                handle_ = std::exchange(other.handle_, INVALID_HANDLE_VALUE);
            }
            return *this;
        }

        HANDLE Get() const noexcept
        {
            return handle_;
        }

        explicit operator bool() const noexcept
        {
            return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
        }

        void Reset(HANDLE handle = INVALID_HANDLE_VALUE) noexcept
        {
            if (*this)
            {
                CloseHandle(handle_);
            }
            handle_ = handle;
        }

    private:
        HANDLE handle_{INVALID_HANDLE_VALUE};
    };

    std::wstring FormatWin32Error(std::wstring_view operation, DWORD error)
    {
        wchar_t* rawMessage = nullptr;
        const DWORD length = FormatMessageW(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr,
            error,
            0,
            reinterpret_cast<wchar_t*>(&rawMessage),
            0,
            nullptr);
        std::wstring message(operation);
        message.append(L" failed");
        if (length != 0 && rawMessage)
        {
            std::wstring detail(rawMessage, length);
            while (!detail.empty() && iswspace(detail.back()) != 0)
            {
                detail.pop_back();
            }
            if (!detail.empty())
            {
                message.append(L": ");
                message.append(detail);
            }
            LocalFree(rawMessage);
        }
        message.append(L" (error ");
        message.append(std::to_wstring(error));
        message.push_back(L')');
        return message;
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

    std::vector<std::wstring> SplitTabFields(std::wstring_view line)
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

    std::wstring TrimWhitespace(std::wstring value)
    {
        const auto isSpace = [](wchar_t character)
        {
            return iswspace(character) != 0;
        };

        const auto first = std::find_if_not(value.begin(), value.end(), isSpace);
        const auto last = std::find_if_not(value.rbegin(), value.rend(), isSpace).base();
        if (first >= last)
        {
            return {};
        }

        return std::wstring(first, last);
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

    bool WideToUtf8(std::wstring_view value, std::string* result, std::wstring* errorMessage)
    {
        result->clear();
        if (value.empty())
        {
            return true;
        }
        if (value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        {
            if (errorMessage)
            {
                *errorMessage = L"Metadata text is too large to encode as UTF-8.";
            }
            return false;
        }

        const int inputLength = static_cast<int>(value.size());
        const int required = WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            value.data(),
            inputLength,
            nullptr,
            0,
            nullptr,
            nullptr);
        if (required <= 0)
        {
            if (errorMessage)
            {
                *errorMessage = FormatWin32Error(L"UTF-8 metadata encoding", GetLastError());
            }
            return false;
        }

        result->resize(static_cast<std::size_t>(required));
        if (WideCharToMultiByte(
                CP_UTF8,
                WC_ERR_INVALID_CHARS,
                value.data(),
                inputLength,
                result->data(),
                required,
                nullptr,
                nullptr) != required)
        {
            if (errorMessage)
            {
                *errorMessage = FormatWin32Error(L"UTF-8 metadata encoding", GetLastError());
            }
            result->clear();
            return false;
        }
        return true;
    }

    bool DecodeText(std::string_view bytes,
                    UINT codePage,
                    DWORD flags,
                    std::wstring* result,
                    std::wstring* errorMessage)
    {
        result->clear();
        if (bytes.empty())
        {
            return true;
        }
        if (bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        {
            if (errorMessage)
            {
                *errorMessage = L"Metadata file is too large to decode.";
            }
            return false;
        }

        const int inputLength = static_cast<int>(bytes.size());
        const int required = MultiByteToWideChar(codePage, flags, bytes.data(), inputLength, nullptr, 0);
        if (required <= 0)
        {
            if (errorMessage)
            {
                *errorMessage = FormatWin32Error(L"Metadata text decoding", GetLastError());
            }
            return false;
        }

        result->resize(static_cast<std::size_t>(required));
        if (MultiByteToWideChar(codePage, flags, bytes.data(), inputLength, result->data(), required) != required)
        {
            if (errorMessage)
            {
                *errorMessage = FormatWin32Error(L"Metadata text decoding", GetLastError());
            }
            result->clear();
            return false;
        }
        return true;
    }

    bool ReadAllBytes(const fs::path& path, std::string* bytes, bool* fileExists, std::wstring* errorMessage)
    {
        bytes->clear();
        *fileExists = false;
        UniqueHandle file(CreateFileW(
            path.c_str(),
            GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr));
        if (!file)
        {
            const DWORD error = GetLastError();
            if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
            {
                return true;
            }
            if (errorMessage)
            {
                *errorMessage = FormatWin32Error(L"Opening the metadata file", error);
            }
            return false;
        }
        *fileExists = true;

        LARGE_INTEGER fileSize{};
        if (!GetFileSizeEx(file.Get(), &fileSize))
        {
            if (errorMessage)
            {
                *errorMessage = FormatWin32Error(L"Reading the metadata file size", GetLastError());
            }
            return false;
        }
        if (fileSize.QuadPart < 0 || static_cast<std::uint64_t>(fileSize.QuadPart) > kMaximumMetadataFileBytes)
        {
            if (errorMessage)
            {
                *errorMessage = L"The metadata file exceeds the 64 MiB safety limit.";
            }
            return false;
        }

        bytes->resize(static_cast<std::size_t>(fileSize.QuadPart));
        std::size_t offset = 0;
        while (offset < bytes->size())
        {
            const DWORD request = static_cast<DWORD>(std::min<std::size_t>(
                bytes->size() - offset,
                static_cast<std::size_t>(std::numeric_limits<DWORD>::max())));
            DWORD read = 0;
            if (!ReadFile(file.Get(), bytes->data() + offset, request, &read, nullptr))
            {
                if (errorMessage)
                {
                    *errorMessage = FormatWin32Error(L"Reading the metadata file", GetLastError());
                }
                bytes->clear();
                return false;
            }
            if (read == 0)
            {
                if (errorMessage)
                {
                    *errorMessage = L"The metadata file ended before its reported size.";
                }
                bytes->clear();
                return false;
            }
            offset += read;
        }
        return true;
    }

    bool ParseMetadataText(std::wstring_view text,
                           std::unordered_map<std::wstring, UserMetadataEntry>* entries)
    {
        entries->clear();
        std::size_t lineStart = 0;
        while (lineStart <= text.size())
        {
            const std::size_t lineEnd = text.find(L'\n', lineStart);
            const std::size_t count = lineEnd == std::wstring_view::npos
                ? text.size() - lineStart
                : lineEnd - lineStart;
            std::wstring_view line = text.substr(lineStart, count);
            if (!line.empty() && line.back() == L'\r')
            {
                line.remove_suffix(1);
            }

            if (!line.empty() && line != kMetadataHeader && !line.starts_with(L"# HyperBrowse user metadata"))
            {
                const std::vector<std::wstring> fields = SplitTabFields(line);
                if (fields.size() == 3)
                {
                    wchar_t* ratingEnd = nullptr;
                    const long parsedRating = wcstol(fields[1].c_str(), &ratingEnd, 10);
                    if (ratingEnd != fields[1].c_str() && *ratingEnd == L'\0')
                    {
                        UserMetadataEntry entry;
                        entry.rating = std::clamp(static_cast<int>(parsedRating), 0, 5);
                        entry.tags = fields[2];
                        if (entry.rating > 0 || !entry.tags.empty())
                        {
                            (*entries)[hyperbrowse::util::NormalizePathForComparison(fields[0])] = std::move(entry);
                        }
                    }
                }
            }

            if (lineEnd == std::wstring_view::npos)
            {
                break;
            }
            lineStart = lineEnd + 1;
        }
        return true;
    }

    bool LoadEntries(const fs::path& path,
                     std::unordered_map<std::wstring, UserMetadataEntry>* entries,
                     std::wstring* errorMessage)
    {
        std::string bytes;
        bool fileExists = false;
        if (!ReadAllBytes(path, &bytes, &fileExists, errorMessage))
        {
            return false;
        }
        if (!fileExists || bytes.empty())
        {
            entries->clear();
            return true;
        }

        std::string_view encoded(bytes);
        if (encoded.size() >= 3
            && static_cast<unsigned char>(encoded[0]) == 0xEF
            && static_cast<unsigned char>(encoded[1]) == 0xBB
            && static_cast<unsigned char>(encoded[2]) == 0xBF)
        {
            encoded.remove_prefix(3);
        }

        constexpr std::string_view utf8Header = "# HyperBrowse user metadata v2; encoding=utf-8";
        const bool versionedUtf8 = encoded.starts_with(utf8Header);
        std::wstring decoded;
        std::wstring utf8Error;
        if (!DecodeText(encoded, CP_UTF8, MB_ERR_INVALID_CHARS, &decoded, &utf8Error))
        {
            if (versionedUtf8 || !DecodeText(encoded, CP_ACP, 0, &decoded, errorMessage))
            {
                if (errorMessage && versionedUtf8)
                {
                    *errorMessage = std::move(utf8Error);
                }
                return false;
            }
        }
        return ParseMetadataText(decoded, entries);
    }

    bool SerializeEntries(const std::unordered_map<std::wstring, UserMetadataEntry>& entries,
                          std::string* bytes,
                          std::wstring* errorMessage)
    {
        std::vector<std::pair<std::wstring, UserMetadataEntry>> sortedEntries;
        sortedEntries.reserve(entries.size());
        for (const auto& [path, entry] : entries)
        {
            if (entry.rating > 0 || !entry.tags.empty())
            {
                sortedEntries.emplace_back(path, entry);
            }
        }
        std::sort(sortedEntries.begin(), sortedEntries.end(), [](const auto& lhs, const auto& rhs)
        {
            return lhs.first < rhs.first;
        });

        std::wstring content(kMetadataHeader);
        content.push_back(L'\n');
        for (const auto& [path, entry] : sortedEntries)
        {
            content.append(EscapeField(path));
            content.push_back(L'\t');
            content.append(std::to_wstring(entry.rating));
            content.push_back(L'\t');
            content.append(EscapeField(entry.tags));
            content.push_back(L'\n');
        }
        return WideToUtf8(content, bytes, errorMessage);
    }

    bool EnsureStorageDirectory(const fs::path& directory, std::wstring* errorMessage)
    {
        if (directory.empty())
        {
            if (errorMessage)
            {
                *errorMessage = L"The metadata storage directory is unavailable.";
            }
            return false;
        }

        std::error_code error;
        fs::create_directories(directory, error);
        if (error)
        {
            if (errorMessage)
            {
                *errorMessage = L"Creating the metadata storage directory failed: ";
                const std::string detail = error.message();
                errorMessage->append(detail.begin(), detail.end());
            }
            return false;
        }
        return true;
    }

    UniqueHandle AcquireMetadataLock(const fs::path& lockPath, std::wstring* errorMessage)
    {
        DWORD lastError = ERROR_SUCCESS;
        for (DWORD attempt = 0; attempt < kLockRetryCount; ++attempt)
        {
            UniqueHandle handle(CreateFileW(
                lockPath.c_str(),
                GENERIC_READ | GENERIC_WRITE,
                0,
                nullptr,
                OPEN_ALWAYS,
                FILE_ATTRIBUTE_HIDDEN,
                nullptr));
            if (handle)
            {
                return handle;
            }

            lastError = GetLastError();
            if (lastError != ERROR_SHARING_VIOLATION && lastError != ERROR_LOCK_VIOLATION)
            {
                break;
            }
            Sleep(kLockRetryDelayMs);
        }

        if (errorMessage)
        {
            *errorMessage = FormatWin32Error(L"Locking the metadata file", lastError);
        }
        return {};
    }

    bool WriteEntriesAtomically(const fs::path& metadataPath,
                                const std::unordered_map<std::wstring, UserMetadataEntry>& entries,
                                std::uint64_t generation,
                                std::wstring* errorMessage)
    {
        std::string bytes;
        if (!SerializeEntries(entries, &bytes, errorMessage))
        {
            return false;
        }

        fs::path temporaryPath;
        UniqueHandle temporaryFile;
        DWORD createError = ERROR_FILE_EXISTS;
        for (unsigned int attempt = 0; attempt < 32; ++attempt)
        {
            temporaryPath = fs::path(
                metadataPath.wstring()
                + L".tmp."
                + std::to_wstring(GetCurrentProcessId())
                + L"."
                + std::to_wstring(GetCurrentThreadId())
                + L"."
                + std::to_wstring(generation)
                + L"."
                + std::to_wstring(attempt));
            temporaryFile.Reset(CreateFileW(
                temporaryPath.c_str(),
                GENERIC_WRITE,
                0,
                nullptr,
                CREATE_NEW,
                FILE_ATTRIBUTE_TEMPORARY,
                nullptr));
            if (temporaryFile)
            {
                break;
            }
            createError = GetLastError();
            if (createError != ERROR_FILE_EXISTS && createError != ERROR_ALREADY_EXISTS)
            {
                break;
            }
        }
        if (!temporaryFile)
        {
            if (errorMessage)
            {
                *errorMessage = FormatWin32Error(L"Creating a temporary metadata file", createError);
            }
            return false;
        }

        bool writeSucceeded = true;
        std::size_t offset = 0;
        while (offset < bytes.size())
        {
            const DWORD request = static_cast<DWORD>(std::min<std::size_t>(
                bytes.size() - offset,
                static_cast<std::size_t>(std::numeric_limits<DWORD>::max())));
            DWORD written = 0;
            if (!WriteFile(temporaryFile.Get(), bytes.data() + offset, request, &written, nullptr)
                || written == 0)
            {
                writeSucceeded = false;
                if (errorMessage)
                {
                    *errorMessage = FormatWin32Error(L"Writing the metadata file", GetLastError());
                }
                break;
            }
            offset += written;
        }
        if (writeSucceeded && !FlushFileBuffers(temporaryFile.Get()))
        {
            writeSucceeded = false;
            if (errorMessage)
            {
                *errorMessage = FormatWin32Error(L"Flushing the metadata file", GetLastError());
            }
        }
        temporaryFile.Reset();

        if (writeSucceeded)
        {
            bool published = false;
            DWORD publishError = ERROR_SUCCESS;
            for (DWORD attempt = 0; attempt < kPublishRetryCount; ++attempt)
            {
                if (MoveFileExW(
                        temporaryPath.c_str(),
                        metadataPath.c_str(),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                {
                    published = true;
                    break;
                }

                publishError = GetLastError();
                if (publishError != ERROR_ACCESS_DENIED
                    && publishError != ERROR_SHARING_VIOLATION
                    && publishError != ERROR_LOCK_VIOLATION)
                {
                    break;
                }
                Sleep(kPublishRetryDelayMs);
            }
            if (!published)
            {
                writeSucceeded = false;
                if (errorMessage)
                {
                    *errorMessage = FormatWin32Error(L"Publishing the metadata file", publishError);
                }
            }
        }

        if (!writeSucceeded)
        {
            DeleteFileW(temporaryPath.c_str());
        }
        return writeSucceeded;
    }

    bool IsSameOrDescendant(std::wstring_view candidate, std::wstring_view root) noexcept
    {
        if (candidate == root)
        {
            return true;
        }
        if (candidate.size() <= root.size() || !candidate.starts_with(root))
        {
            return false;
        }
        return root.ends_with(L'\\') || candidate[root.size()] == L'\\';
    }

    std::wstring RemapPath(std::wstring_view source,
                           std::wstring_view destination,
                           std::wstring_view candidate)
    {
        std::wstring remapped(destination);
        remapped.append(candidate.substr(source.size()));
        return remapped;
    }

    void AddUniqueKey(std::vector<std::wstring>* keys, const std::wstring& key)
    {
        if (std::find(keys->begin(), keys->end(), key) == keys->end())
        {
            keys->push_back(key);
        }
    }
}

namespace hyperbrowse::services
{
    UserMetadataStore::UserMetadataStore(std::wstring storageDirectory)
        : storageDirectory_(std::move(storageDirectory))
    {
        if (storageDirectory_.empty())
        {
            const std::wstring localAppDataPath = TryGetLocalAppDataPath();
            if (!localAppDataPath.empty())
            {
                storageDirectory_ = (fs::path(localAppDataPath) / kMetadataFolder).wstring();
            }
        }
        if (!storageDirectory_.empty())
        {
            metadataFilePath_ = (fs::path(storageDirectory_) / kMetadataFileName).wstring();
            lockFilePath_ = (fs::path(storageDirectory_) / kMetadataLockFileName).wstring();
        }

        saveWorker_ = std::thread(&UserMetadataStore::SaveWorkerLoop, this);
    }

    UserMetadataStore::~UserMetadataStore()
    {
        Shutdown();
    }

    UserMetadataEntry UserMetadataStore::EntryForPath(std::wstring_view filePath) const
    {
        const std::wstring normalizedPath = util::NormalizePathForComparison(filePath);
        std::scoped_lock lock(mutex_);
        EnsureLoadedLocked();
        const auto iterator = entries_.find(normalizedPath);
        return iterator == entries_.end() ? UserMetadataEntry{} : iterator->second;
    }

    void UserMetadataStore::SetRating(const std::vector<std::wstring>& filePaths, int rating)
    {
        if (filePaths.empty())
        {
            return;
        }

        rating = std::clamp(rating, 0, 5);
        std::vector<std::wstring> changedKeys;
        std::scoped_lock lock(mutex_);
        EnsureLoadedLocked();
        for (const std::wstring& filePath : filePaths)
        {
            const std::wstring normalizedPath = util::NormalizePathForComparison(filePath);
            if (normalizedPath.empty())
            {
                continue;
            }
            const auto existing = entries_.find(normalizedPath);
            const int previousRating = existing == entries_.end() ? 0 : existing->second.rating;
            if (previousRating == rating)
            {
                continue;
            }

            UserMetadataEntry& entry = entries_[normalizedPath];
            entry.rating = rating;
            if (IsEmptyEntry(entry))
            {
                entries_.erase(normalizedPath);
            }
            AddUniqueKey(&changedKeys, normalizedPath);
        }
        QueueSaveLocked(changedKeys);
    }

    void UserMetadataStore::SetTags(const std::vector<std::wstring>& filePaths, std::wstring_view tags)
    {
        if (filePaths.empty())
        {
            return;
        }

        const std::wstring normalizedTags = NormalizeTags(tags);
        std::vector<std::wstring> changedKeys;
        std::scoped_lock lock(mutex_);
        EnsureLoadedLocked();
        for (const std::wstring& filePath : filePaths)
        {
            const std::wstring normalizedPath = util::NormalizePathForComparison(filePath);
            if (normalizedPath.empty())
            {
                continue;
            }
            const auto existing = entries_.find(normalizedPath);
            const std::wstring previousTags = existing == entries_.end() ? std::wstring{} : existing->second.tags;
            if (previousTags == normalizedTags)
            {
                continue;
            }

            UserMetadataEntry& entry = entries_[normalizedPath];
            entry.tags = normalizedTags;
            if (IsEmptyEntry(entry))
            {
                entries_.erase(normalizedPath);
            }
            AddUniqueKey(&changedKeys, normalizedPath);
        }
        QueueSaveLocked(changedKeys);
    }

    void UserMetadataStore::ApplyFileOperationUpdate(FileOperationType type,
                                                     const std::vector<std::wstring>& sourcePaths,
                                                     const std::vector<std::wstring>& createdPaths)
    {
        std::scoped_lock lock(mutex_);
        EnsureLoadedLocked();
        std::vector<std::wstring> changedKeys;

        if (type == FileOperationType::DeleteRecycleBin || type == FileOperationType::DeletePermanent)
        {
            std::vector<std::wstring> normalizedSources;
            normalizedSources.reserve(sourcePaths.size());
            for (const std::wstring& path : sourcePaths)
            {
                normalizedSources.push_back(util::NormalizePathForComparison(path));
            }

            for (auto iterator = entries_.begin(); iterator != entries_.end();)
            {
                const bool remove = std::any_of(normalizedSources.begin(), normalizedSources.end(), [&](const std::wstring& source)
                {
                    return !source.empty() && IsSameOrDescendant(iterator->first, source);
                });
                if (remove)
                {
                    AddUniqueKey(&changedKeys, iterator->first);
                    iterator = entries_.erase(iterator);
                }
                else
                {
                    ++iterator;
                }
            }
        }
        else if (type == FileOperationType::Rename || type == FileOperationType::Move || type == FileOperationType::Copy)
        {
            const auto originalEntries = entries_;
            const std::size_t pairCount = std::min(sourcePaths.size(), createdPaths.size());
            for (std::size_t index = 0; index < pairCount; ++index)
            {
                const std::wstring sourceKey = util::NormalizePathForComparison(sourcePaths[index]);
                const std::wstring createdKey = util::NormalizePathForComparison(createdPaths[index]);
                if (sourceKey.empty() || createdKey.empty() || sourceKey == createdKey)
                {
                    continue;
                }

                std::vector<std::pair<std::wstring, UserMetadataEntry>> remappedEntries;
                for (const auto& [path, entry] : originalEntries)
                {
                    if (IsSameOrDescendant(path, sourceKey))
                    {
                        remappedEntries.emplace_back(RemapPath(sourceKey, createdKey, path), entry);
                    }
                }

                if (type == FileOperationType::Rename || type == FileOperationType::Move)
                {
                    std::vector<std::wstring> sourceKeysToErase;
                    for (const auto& [path, unusedEntry] : entries_)
                    {
                        (void)unusedEntry;
                        if (IsSameOrDescendant(path, sourceKey))
                        {
                            sourceKeysToErase.push_back(path);
                        }
                    }
                    for (const std::wstring& path : sourceKeysToErase)
                    {
                        entries_.erase(path);
                        AddUniqueKey(&changedKeys, path);
                    }
                }

                const DWORD destinationAttributes = GetFileAttributesW(createdPaths[index].c_str());
                const bool destinationIsDirectory = destinationAttributes != INVALID_FILE_ATTRIBUTES
                    && (destinationAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                if (!destinationIsDirectory && remappedEntries.empty())
                {
                    if (entries_.erase(createdKey) > 0)
                    {
                        AddUniqueKey(&changedKeys, createdKey);
                    }
                }

                for (auto& [path, entry] : remappedEntries)
                {
                    const auto existing = entries_.find(path);
                    if (existing == entries_.end() || existing->second != entry)
                    {
                        entries_[path] = std::move(entry);
                        AddUniqueKey(&changedKeys, path);
                    }
                }
            }
        }

        QueueSaveLocked(changedKeys);
    }

    void UserMetadataStore::SetNotificationWindow(HWND window) noexcept
    {
        notificationWindow_.store(window, std::memory_order_release);
    }

    std::wstring UserMetadataStore::LastSaveError() const
    {
        std::scoped_lock lock(saveMutex_);
        return lastSaveError_;
    }

    bool UserMetadataStore::Flush(std::chrono::milliseconds timeout, std::wstring* errorMessage)
    {
        std::unique_lock lock(saveMutex_);
        const std::uint64_t targetGeneration = requestedSaveGeneration_;
        if (targetGeneration == 0)
        {
            if (errorMessage)
            {
                errorMessage->clear();
            }
            return true;
        }

        const bool completed = saveFinished_.wait_for(lock, timeout, [&]()
        {
            return attemptedSaveGeneration_ >= targetGeneration || shutdownComplete_;
        });
        if (!completed || successfulSaveGeneration_ < targetGeneration)
        {
            if (errorMessage)
            {
                *errorMessage = completed && !lastSaveError_.empty()
                    ? lastSaveError_
                    : L"Timed out while saving ratings and tags.";
            }
            return false;
        }

        if (errorMessage)
        {
            errorMessage->clear();
        }
        return true;
    }

    void UserMetadataStore::Shutdown()
    {
        notificationWindow_.store(nullptr, std::memory_order_release);

        std::uint64_t retryGeneration = 0;
        {
            std::scoped_lock lock(mutex_);
            if (!pendingMutations_.empty())
            {
                retryGeneration = ++mutationGeneration_;
            }
        }
        {
            std::scoped_lock lock(saveMutex_);
            if (shutdownComplete_)
            {
                return;
            }
            if (retryGeneration != 0 && requestedSaveGeneration_ == attemptedSaveGeneration_)
            {
                requestedSaveGeneration_ = retryGeneration;
            }
            saveShuttingDown_ = true;
        }
        saveAvailable_.notify_one();
        if (saveWorker_.joinable())
        {
            saveWorker_.join();
        }
        {
            std::scoped_lock lock(saveMutex_);
            shutdownComplete_ = true;
        }
        saveFinished_.notify_all();
    }

    void UserMetadataStore::EnsureLoadedLocked() const
    {
        if (loaded_)
        {
            return;
        }

        LoadLocked();
        loaded_ = true;
    }

    bool UserMetadataStore::LoadLocked() const
    {
        entries_.clear();
        if (metadataFilePath_.empty())
        {
            return false;
        }
        std::wstring ignoredError;
        return LoadEntries(fs::path(metadataFilePath_), &entries_, &ignoredError);
    }

    void UserMetadataStore::QueueSaveLocked(const std::vector<std::wstring>& changedKeys)
    {
        if (changedKeys.empty())
        {
            return;
        }

        const std::uint64_t generation = ++mutationGeneration_;
        for (const std::wstring& key : changedKeys)
        {
            const auto entry = entries_.find(key);
            pendingMutations_[key] = PendingMutation{
                entry == entries_.end() ? std::optional<UserMetadataEntry>{} : std::optional<UserMetadataEntry>{entry->second},
                generation};
        }
        {
            std::scoped_lock lock(saveMutex_);
            if (!saveShuttingDown_)
            {
                requestedSaveGeneration_ = generation;
            }
        }
        saveAvailable_.notify_one();
    }

    void UserMetadataStore::SaveWorkerLoop() noexcept
    {
        for (;;)
        {
            std::uint64_t generation = 0;
            {
                std::unique_lock lock(saveMutex_);
                saveAvailable_.wait(lock, [this]()
                {
                    return saveShuttingDown_ || requestedSaveGeneration_ != attemptedSaveGeneration_;
                });

                if (saveShuttingDown_ && requestedSaveGeneration_ == attemptedSaveGeneration_)
                {
                    return;
                }
                generation = requestedSaveGeneration_;
            }

            std::unordered_map<std::wstring, std::optional<UserMetadataEntry>> mutations;
            {
                std::scoped_lock lock(mutex_);
                for (const auto& [path, mutation] : pendingMutations_)
                {
                    if (mutation.generation <= generation)
                    {
                        mutations[path] = mutation.entry;
                    }
                }
            }

            bool succeeded = false;
            std::wstring errorMessage;
            std::unordered_map<std::wstring, UserMetadataEntry> mergedEntries;
            try
            {
                const fs::path storageDirectory(storageDirectory_);
                if (EnsureStorageDirectory(storageDirectory, &errorMessage))
                {
                    UniqueHandle metadataLock = AcquireMetadataLock(fs::path(lockFilePath_), &errorMessage);
                    if (metadataLock
                        && LoadEntries(fs::path(metadataFilePath_), &mergedEntries, &errorMessage))
                    {
                        for (const auto& [path, entry] : mutations)
                        {
                            if (entry)
                            {
                                mergedEntries[path] = *entry;
                            }
                            else
                            {
                                mergedEntries.erase(path);
                            }
                        }
                        succeeded = WriteEntriesAtomically(
                            fs::path(metadataFilePath_),
                            mergedEntries,
                            generation,
                            &errorMessage);
                    }
                }
            }
            catch (const std::exception& exception)
            {
                std::string_view message(exception.what());
                std::wstring converted;
                std::wstring ignored;
                if (!DecodeText(message, CP_UTF8, MB_ERR_INVALID_CHARS, &converted, &ignored))
                {
                    DecodeText(message, CP_ACP, 0, &converted, &ignored);
                }
                errorMessage = L"Saving ratings and tags failed";
                if (!converted.empty())
                {
                    errorMessage.append(L": ");
                    errorMessage.append(converted);
                }
            }
            catch (...)
            {
                errorMessage = L"Saving ratings and tags failed because of an unexpected error.";
            }

            if (succeeded)
            {
                std::scoped_lock lock(mutex_);
                for (auto iterator = pendingMutations_.begin(); iterator != pendingMutations_.end();)
                {
                    if (iterator->second.generation <= generation)
                    {
                        iterator = pendingMutations_.erase(iterator);
                    }
                    else
                    {
                        ++iterator;
                    }
                }
                for (const auto& [path, mutation] : pendingMutations_)
                {
                    if (mutation.entry)
                    {
                        mergedEntries[path] = *mutation.entry;
                    }
                    else
                    {
                        mergedEntries.erase(path);
                    }
                }
                entries_ = std::move(mergedEntries);
            }

            bool notifyFailure = false;
            {
                std::scoped_lock lock(saveMutex_);
                attemptedSaveGeneration_ = generation;
                if (succeeded)
                {
                    successfulSaveGeneration_ = generation;
                    lastSaveError_.clear();
                }
                else
                {
                    if (errorMessage.empty())
                    {
                        errorMessage = L"Saving ratings and tags failed.";
                    }
                    notifyFailure = lastSaveError_ != errorMessage;
                    lastSaveError_ = std::move(errorMessage);
                }
            }
            saveFinished_.notify_all();

            if (notifyFailure)
            {
                const HWND notificationWindow = notificationWindow_.load(std::memory_order_acquire);
                if (notificationWindow)
                {
                    PostMessageW(notificationWindow, kMessageId, 0, 0);
                }
            }
        }
    }

    std::wstring UserMetadataStore::NormalizeTags(std::wstring_view tags)
    {
        std::wstring normalized;
        std::vector<std::wstring> parts;
        std::wstring current;
        for (const wchar_t character : tags)
        {
            if (character == L',' || character == L';' || character == L'\n' || character == L'\r')
            {
                std::wstring trimmed = TrimWhitespace(std::move(current));
                current.clear();
                if (!trimmed.empty())
                {
                    trimmed.erase(std::remove(trimmed.begin(), trimmed.end(), L'\t'), trimmed.end());
                    parts.push_back(std::move(trimmed));
                }
                continue;
            }

            current.push_back(character);
        }

        std::wstring trimmed = TrimWhitespace(std::move(current));
        if (!trimmed.empty())
        {
            trimmed.erase(std::remove(trimmed.begin(), trimmed.end(), L'\t'), trimmed.end());
            parts.push_back(std::move(trimmed));
        }

        std::sort(parts.begin(), parts.end(), [](const std::wstring& lhs, const std::wstring& rhs)
        {
            return _wcsicmp(lhs.c_str(), rhs.c_str()) < 0;
        });
        parts.erase(std::unique(parts.begin(), parts.end(), [](const std::wstring& lhs, const std::wstring& rhs)
        {
            return _wcsicmp(lhs.c_str(), rhs.c_str()) == 0;
        }), parts.end());

        for (std::size_t index = 0; index < parts.size(); ++index)
        {
            if (index > 0)
            {
                normalized.append(L", ");
            }
            normalized.append(parts[index]);
        }
        return normalized;
    }

    bool UserMetadataStore::IsEmptyEntry(const UserMetadataEntry& entry) noexcept
    {
        return entry.rating <= 0 && entry.tags.empty();
    }
}
