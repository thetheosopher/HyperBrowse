#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace hyperbrowse::services
{
    inline constexpr wchar_t kSavedSearchDirectoryEnvironmentVariable[] = L"HYPERBROWSE_SAVED_SEARCH_DIRECTORY";

    struct SavedSearch
    {
        std::wstring name;
        std::wstring expression;

        bool operator==(const SavedSearch&) const = default;
    };

    class SavedSearchStore
    {
    public:
        static constexpr std::size_t kMaximumSearches = 64;
        static constexpr std::size_t kMaximumNameCharacters = 128;
        static constexpr std::size_t kMaximumExpressionCharacters = 260;
        static constexpr std::size_t kMaximumFileBytes = 256 * 1024;

        explicit SavedSearchStore(std::wstring storageDirectory = {});

        bool LoadOnWorker(std::vector<SavedSearch>* searches, std::wstring* error = nullptr) const;
        bool AddOnWorker(SavedSearch search, std::wstring* error = nullptr,
                 std::vector<SavedSearch>* updatedSearches = nullptr) const;
        bool UpdateOnWorker(std::wstring_view name, std::wstring expression, std::wstring* error = nullptr,
                    std::vector<SavedSearch>* updatedSearches = nullptr) const;
        bool RenameOnWorker(std::wstring_view name, std::wstring newName, std::wstring* error = nullptr,
                    std::vector<SavedSearch>* updatedSearches = nullptr) const;
        bool RemoveOnWorker(std::wstring_view name, std::wstring* error = nullptr,
                    std::vector<SavedSearch>* updatedSearches = nullptr) const;

    private:
        enum class Mutation { Add, Update, Rename, Remove };
        bool MutateOnWorker(Mutation mutation, std::wstring_view name,
                            SavedSearch replacement, std::wstring* error,
                            std::vector<SavedSearch>* updatedSearches) const;

        std::wstring storageDirectory_;
    };
}
