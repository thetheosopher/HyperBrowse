#include "services/UserMetadataStore.h"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "util/PathUtils.h"

namespace
{
    namespace fs = std::filesystem;

    constexpr std::wstring_view kMetadataFolder = L"HyperBrowse";
    constexpr std::wstring_view kMetadataFileName = L"user-metadata.tsv";

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

    fs::path MetadataFilePath()
    {
        const std::wstring localAppDataPath = TryGetLocalAppDataPath();
        if (localAppDataPath.empty())
        {
            return {};
        }

        std::error_code error;
        const fs::path metadataDirectory = fs::path(localAppDataPath) / kMetadataFolder;
        fs::create_directories(metadataDirectory, error);
        if (error)
        {
            return {};
        }

        return metadataDirectory / kMetadataFileName;
    }
}

namespace hyperbrowse::services
{
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
        std::scoped_lock lock(mutex_);
        EnsureLoadedLocked();
        for (const std::wstring& filePath : filePaths)
        {
            const std::wstring normalizedPath = util::NormalizePathForComparison(filePath);
            UserMetadataEntry& entry = entries_[normalizedPath];
            entry.rating = rating;
            if (IsEmptyEntry(entry))
            {
                entries_.erase(normalizedPath);
            }
        }
        SaveLocked();
    }

    void UserMetadataStore::SetTags(const std::vector<std::wstring>& filePaths, std::wstring_view tags)
    {
        if (filePaths.empty())
        {
            return;
        }

        const std::wstring normalizedTags = NormalizeTags(tags);
        std::scoped_lock lock(mutex_);
        EnsureLoadedLocked();
        for (const std::wstring& filePath : filePaths)
        {
            const std::wstring normalizedPath = util::NormalizePathForComparison(filePath);
            UserMetadataEntry& entry = entries_[normalizedPath];
            entry.tags = normalizedTags;
            if (IsEmptyEntry(entry))
            {
                entries_.erase(normalizedPath);
            }
        }
        SaveLocked();
    }

    void UserMetadataStore::ApplyFileOperationUpdate(FileOperationType type,
                                                     const std::vector<std::wstring>& sourcePaths,
                                                     const std::vector<std::wstring>& createdPaths)
    {
        std::scoped_lock lock(mutex_);
        EnsureLoadedLocked();
        bool changed = false;

        if (type == FileOperationType::DeleteRecycleBin || type == FileOperationType::DeletePermanent)
        {
            for (const std::wstring& sourcePath : sourcePaths)
            {
                changed = entries_.erase(util::NormalizePathForComparison(sourcePath)) > 0 || changed;
            }
        }
        else if (type == FileOperationType::Rename || type == FileOperationType::Move || type == FileOperationType::Copy)
        {
            const std::size_t pairCount = std::min(sourcePaths.size(), createdPaths.size());
            for (std::size_t index = 0; index < pairCount; ++index)
            {
                const std::wstring sourceKey = util::NormalizePathForComparison(sourcePaths[index]);
                const std::wstring createdKey = util::NormalizePathForComparison(createdPaths[index]);
                const auto source = entries_.find(sourceKey);
                if (source == entries_.end())
                {
                    continue;
                }

                entries_[createdKey] = source->second;
                changed = true;
                if (type == FileOperationType::Rename || type == FileOperationType::Move)
                {
                    entries_.erase(source);
                }
            }
        }

        if (changed)
        {
            SaveLocked();
        }
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
        const fs::path metadataPath = MetadataFilePath();
        if (metadataPath.empty())
        {
            return false;
        }

        std::wifstream stream(metadataPath);
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
            if (fields.size() != 3)
            {
                continue;
            }

            UserMetadataEntry entry;
            entry.rating = std::clamp(_wtoi(fields[1].c_str()), 0, 5);
            entry.tags = fields[2];
            if (!IsEmptyEntry(entry))
            {
                entries_[fields[0]] = std::move(entry);
            }
        }

        return true;
    }

    void UserMetadataStore::SaveLocked() const
    {
        const fs::path metadataPath = MetadataFilePath();
        if (metadataPath.empty())
        {
            return;
        }

        std::wofstream stream(metadataPath, std::ios::trunc);
        if (!stream)
        {
            return;
        }

        for (const auto& [path, entry] : entries_)
        {
            if (IsEmptyEntry(entry))
            {
                continue;
            }

            stream << EscapeField(path)
                   << L'\t' << entry.rating
                   << L'\t' << EscapeField(entry.tags)
                   << L'\n';
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
