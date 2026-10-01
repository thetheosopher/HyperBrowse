#include "services/SavedSearchStore.h"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <utility>

namespace
{
    namespace fs = std::filesystem;
    using hyperbrowse::services::SavedSearch;
    using hyperbrowse::services::SavedSearchStore;

    constexpr std::wstring_view kHeader = L"# HyperBrowse saved searches v1; encoding=utf-8";

    class FileHandle
    {
    public:
        explicit FileHandle(HANDLE handle = INVALID_HANDLE_VALUE) noexcept : handle_(handle) {}
        ~FileHandle() { Reset(); }
        FileHandle(const FileHandle&) = delete;
        FileHandle& operator=(const FileHandle&) = delete;
        HANDLE Get() const noexcept { return handle_; }
        explicit operator bool() const noexcept { return handle_ && handle_ != INVALID_HANDLE_VALUE; }
        void Reset() noexcept
        {
            if (*this) CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
        }
    private:
        HANDLE handle_;
    };

    bool Fail(std::wstring* error, std::wstring message)
    {
        if (error) *error = std::move(message);
        return false;
    }

    bool Win32Failure(std::wstring* error, std::wstring_view operation, DWORD code = GetLastError())
    {
        return Fail(error, std::wstring(operation) + L" failed (error " + std::to_wstring(code) + L").");
    }

    std::wstring Trim(std::wstring_view value)
    {
        const auto isSpace = [](wchar_t character) { return iswspace(character) != 0; };
        const auto first = std::find_if_not(value.begin(), value.end(), isSpace);
        const auto last = std::find_if_not(value.rbegin(), value.rend(), isSpace).base();
        return first < last ? std::wstring(first, last) : std::wstring{};
    }

    bool NamesEqual(std::wstring_view first, std::wstring_view second)
    {
        return CompareStringOrdinal(first.data(), static_cast<int>(first.size()),
                                    second.data(), static_cast<int>(second.size()), TRUE) == CSTR_EQUAL;
    }

    bool ValidName(std::wstring_view name)
    {
        return !name.empty() && name.size() <= SavedSearchStore::kMaximumNameCharacters
            && std::none_of(name.begin(), name.end(), [](wchar_t character) { return character < L' '; });
    }

    bool ValidSearch(const SavedSearch& search)
    {
        return ValidName(search.name) && !search.expression.empty()
            && search.expression.size() <= SavedSearchStore::kMaximumExpressionCharacters
            && search.expression.find(L'\0') == std::wstring::npos;
    }

    fs::path ResolveDirectory(const std::wstring& directory, std::wstring* error)
    {
        if (!directory.empty()) return fs::path(directory);
        const DWORD required = GetEnvironmentVariableW(
            hyperbrowse::services::kSavedSearchDirectoryEnvironmentVariable, nullptr, 0);
        if (required != 0)
        {
            std::wstring overrideDirectory(required, L'\0');
            const DWORD copied = GetEnvironmentVariableW(
                hyperbrowse::services::kSavedSearchDirectoryEnvironmentVariable,
                overrideDirectory.data(), required);
            if (copied == 0 || copied >= required)
            {
                Fail(error, L"The saved-search directory override could not be read.");
                return {};
            }
            overrideDirectory.resize(copied);
            return fs::path(overrideDirectory);
        }
        PWSTR localAppData = nullptr;
        const HRESULT result = SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &localAppData);
        if (FAILED(result) || !localAppData)
        {
            if (localAppData) CoTaskMemFree(localAppData);
            Fail(error, L"The saved-search storage directory is unavailable.");
            return {};
        }
        const fs::path resolved = fs::path(localAppData) / L"HyperBrowse";
        CoTaskMemFree(localAppData);
        return resolved;
    }

    class LockedStore
    {
    public:
        LockedStore(const std::wstring& directory, std::wstring* error)
            : directory_(ResolveDirectory(directory, error))
            , lock_(Open(error))
        {
        }
        explicit operator bool() const noexcept { return static_cast<bool>(lock_); }
        fs::path DataPath() const { return directory_ / L"saved-searches.tsv"; }
    private:
        HANDLE Open(std::wstring* error)
        {
            if (directory_.empty()) return INVALID_HANDLE_VALUE;
            std::error_code directoryError;
            fs::create_directories(directory_, directoryError);
            if (directoryError)
            {
                Fail(error, L"Creating the saved-search storage directory failed.");
                return INVALID_HANDLE_VALUE;
            }
            HANDLE handle = CreateFileW((directory_ / L"saved-searches.lock").c_str(),
                                        GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
                                        FILE_ATTRIBUTE_HIDDEN, nullptr);
            if (handle == INVALID_HANDLE_VALUE) Win32Failure(error, L"Locking saved searches");
            return handle;
        }
        fs::path directory_;
        FileHandle lock_;
    };

    std::wstring Escape(std::wstring_view value)
    {
        std::wstring result;
        result.reserve(value.size());
        for (wchar_t character : value)
        {
            switch (character)
            {
            case L'\\': result += L"\\\\"; break;
            case L'\t': result += L"\\t"; break;
            case L'\r': result += L"\\r"; break;
            case L'\n': result += L"\\n"; break;
            default: result += character; break;
            }
        }
        return result;
    }

    bool Unescape(std::wstring_view value, std::wstring* result)
    {
        result->clear();
        for (std::size_t index = 0; index < value.size(); ++index)
        {
            wchar_t character = value[index];
            if (character == L'\\')
            {
                if (++index == value.size()) return false;
                switch (value[index])
                {
                case L'\\': character = L'\\'; break;
                case L't': character = L'\t'; break;
                case L'r': character = L'\r'; break;
                case L'n': character = L'\n'; break;
                default: return false;
                }
            }
            result->push_back(character);
        }
        return true;
    }

    bool Decode(const std::string& bytes, std::wstring* text, std::wstring* error)
    {
        const int length = static_cast<int>(bytes.size());
        const int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), length, nullptr, 0);
        if (required <= 0) return Win32Failure(error, L"Decoding saved-search UTF-8");
        text->resize(static_cast<std::size_t>(required));
        return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), length, text->data(), required) == required
            || Win32Failure(error, L"Decoding saved-search UTF-8");
    }

    bool ReadSearches(const fs::path& path, std::vector<SavedSearch>* searches, std::wstring* error)
    {
        FileHandle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (!file)
        {
            const DWORD code = GetLastError();
            if (code == ERROR_FILE_NOT_FOUND)
            {
                searches->clear();
                return true;
            }
            return Win32Failure(error, L"Reading saved searches", code);
        }
        LARGE_INTEGER size{};
        if (!GetFileSizeEx(file.Get(), &size)) return Win32Failure(error, L"Sizing saved searches");
        if (size.QuadPart <= 0 || size.QuadPart > SavedSearchStore::kMaximumFileBytes)
            return Fail(error, L"The saved-search file has an unsupported size.");
        std::string bytes(static_cast<std::size_t>(size.QuadPart), '\0');
        DWORD read = 0;
        if (!ReadFile(file.Get(), bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr)
            || read != bytes.size()) return Win32Failure(error, L"Reading saved searches");
        std::wstring text;
        if (!Decode(bytes, &text, error)) return false;
        if (!text.empty() && text.front() == L'\ufeff') text.erase(0, 1);

        std::vector<SavedSearch> parsed;
        std::size_t offset = 0;
        bool headerRead = false;
        while (offset < text.size())
        {
            const std::size_t newline = text.find(L'\n', offset);
            std::wstring_view line(text.data() + offset,
                (newline == std::wstring::npos ? text.size() : newline) - offset);
            offset = newline == std::wstring::npos ? text.size() : newline + 1;
            if (!line.empty() && line.back() == L'\r') line.remove_suffix(1);
            if (!headerRead)
            {
                if (line != kHeader) return Fail(error, L"The saved-search file version is unsupported.");
                headerRead = true;
                continue;
            }
            if (line.empty()) continue;
            const std::size_t separator = line.find(L'\t');
            SavedSearch search;
            if (separator == std::wstring::npos || line.find(L'\t', separator + 1) != std::wstring::npos
                || !Unescape(line.substr(0, separator), &search.name)
                || !Unescape(line.substr(separator + 1), &search.expression)
                || !ValidSearch(search) || search.name != Trim(search.name)
                || search.expression != Trim(search.expression))
                return Fail(error, L"The saved-search file contains an invalid record.");
            if (parsed.size() >= SavedSearchStore::kMaximumSearches
                || std::any_of(parsed.begin(), parsed.end(), [&](const SavedSearch& existing)
                   { return NamesEqual(existing.name, search.name); }))
                return Fail(error, L"The saved-search file exceeds its limit or contains duplicate names.");
            parsed.push_back(std::move(search));
        }
        if (!headerRead) return Fail(error, L"The saved-search header is missing.");
        *searches = std::move(parsed);
        return true;
    }

    bool Publish(const fs::path& path, const std::vector<SavedSearch>& searches, std::wstring* error)
    {
        std::wstring text(kHeader);
        text += L'\n';
        for (const SavedSearch& search : searches)
            text += Escape(search.name) + L"\t" + Escape(search.expression) + L"\n";
        const int required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
            static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
        if (required <= 0) return Win32Failure(error, L"Encoding saved-search UTF-8");
        if (static_cast<std::size_t>(required) > SavedSearchStore::kMaximumFileBytes)
            return Fail(error, L"The saved-search file would exceed its size limit.");
        std::string bytes(static_cast<std::size_t>(required), '\0');
        if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
                               bytes.data(), required, nullptr, nullptr) != required)
            return Win32Failure(error, L"Encoding saved-search UTF-8");

        const fs::path temporary(path.wstring() + L".tmp." + std::to_wstring(GetCurrentProcessId())
            + L"." + std::to_wstring(GetCurrentThreadId()) + L"." + std::to_wstring(GetTickCount64()));
        FileHandle file(CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                    FILE_ATTRIBUTE_TEMPORARY, nullptr));
        if (!file) return Win32Failure(error, L"Creating a temporary saved-search file");
        DWORD written = 0;
        const bool writeSucceeded = WriteFile(file.Get(), bytes.data(), static_cast<DWORD>(bytes.size()),
                                               &written, nullptr) && written == bytes.size()
            && FlushFileBuffers(file.Get());
        const DWORD writeError = GetLastError();
        file.Reset();
        if (!writeSucceeded)
        {
            DeleteFileW(temporary.c_str());
            return Win32Failure(error, L"Writing saved searches", writeError);
        }
        if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            const DWORD publishError = GetLastError();
            DeleteFileW(temporary.c_str());
            return Win32Failure(error, L"Publishing saved searches", publishError);
        }
        return true;
    }
}

namespace hyperbrowse::services
{
    SavedSearchStore::SavedSearchStore(std::wstring storageDirectory)
        : storageDirectory_(std::move(storageDirectory))
    {
    }

    bool SavedSearchStore::LoadOnWorker(std::vector<SavedSearch>* searches, std::wstring* error) const
    {
        if (error) error->clear();
        if (!searches) return Fail(error, L"No saved-search output was supplied.");
        LockedStore store(storageDirectory_, error);
        return store && ReadSearches(store.DataPath(), searches, error);
    }

    bool SavedSearchStore::AddOnWorker(SavedSearch search, std::wstring* error,
                                      std::vector<SavedSearch>* updatedSearches) const
    {
        return MutateOnWorker(Mutation::Add, {}, std::move(search), error, updatedSearches);
    }

    bool SavedSearchStore::UpdateOnWorker(std::wstring_view name, std::wstring expression, std::wstring* error,
                                         std::vector<SavedSearch>* updatedSearches) const
    {
        return MutateOnWorker(Mutation::Update, name, {{}, std::move(expression)}, error, updatedSearches);
    }

    bool SavedSearchStore::RenameOnWorker(std::wstring_view name, std::wstring newName, std::wstring* error,
                                         std::vector<SavedSearch>* updatedSearches) const
    {
        return MutateOnWorker(Mutation::Rename, name, {std::move(newName), {}}, error, updatedSearches);
    }

    bool SavedSearchStore::RemoveOnWorker(std::wstring_view name, std::wstring* error,
                                         std::vector<SavedSearch>* updatedSearches) const
    {
        return MutateOnWorker(Mutation::Remove, name, {}, error, updatedSearches);
    }

    bool SavedSearchStore::MutateOnWorker(Mutation mutation, std::wstring_view name,
                                         SavedSearch replacement, std::wstring* error,
                                         std::vector<SavedSearch>* updatedSearches) const
    {
        if (error) error->clear();
        const std::wstring existingName = Trim(name);
        replacement.name = Trim(replacement.name);
        replacement.expression = Trim(replacement.expression);
        if ((mutation == Mutation::Add && !ValidSearch(replacement))
            || (mutation != Mutation::Add && !ValidName(existingName))
            || (mutation == Mutation::Rename && !ValidName(replacement.name))
            || (mutation == Mutation::Update
                && !ValidSearch({existingName, replacement.expression})))
            return Fail(error, L"Saved-search name or expression is empty, invalid, or too long.");

        LockedStore store(storageDirectory_, error);
        if (!store) return false;
        std::vector<SavedSearch> searches;
        if (!ReadSearches(store.DataPath(), &searches, error)) return false;
        auto existing = std::find_if(searches.begin(), searches.end(), [&](const SavedSearch& search)
        {
            return NamesEqual(search.name, existingName);
        });
        if (mutation != Mutation::Add && existing == searches.end())
            return Fail(error, L"The saved search no longer exists.");
        if (mutation == Mutation::Add || mutation == Mutation::Rename)
        {
            const bool duplicate = std::any_of(searches.begin(), searches.end(), [&](const SavedSearch& search)
            {
                return &search != (existing == searches.end() ? nullptr : &*existing)
                    && NamesEqual(search.name, replacement.name);
            });
            if (duplicate) return Fail(error, L"A saved search with that name already exists.");
        }
        switch (mutation)
        {
        case Mutation::Add:
            if (searches.size() >= kMaximumSearches) return Fail(error, L"The saved-search limit has been reached.");
            searches.push_back(std::move(replacement));
            break;
        case Mutation::Update: existing->expression = std::move(replacement.expression); break;
        case Mutation::Rename: existing->name = std::move(replacement.name); break;
        case Mutation::Remove: searches.erase(existing); break;
        }
        if (!Publish(store.DataPath(), searches, error)) return false;
        if (updatedSearches) *updatedSearches = std::move(searches);
        return true;
    }
}
