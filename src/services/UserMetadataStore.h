#pragma once

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#include "services/FileOperationService.h"

namespace hyperbrowse::services
{
    struct UserMetadataEntry
    {
        int rating{};
        std::wstring tags;
    };

    class UserMetadataStore
    {
    public:
        UserMetadataStore();
        ~UserMetadataStore();

        UserMetadataStore(const UserMetadataStore&) = delete;
        UserMetadataStore& operator=(const UserMetadataStore&) = delete;

        UserMetadataEntry EntryForPath(std::wstring_view filePath) const;
        void SetRating(const std::vector<std::wstring>& filePaths, int rating);
        void SetTags(const std::vector<std::wstring>& filePaths, std::wstring_view tags);
        void ApplyFileOperationUpdate(FileOperationType type,
                                      const std::vector<std::wstring>& sourcePaths,
                                      const std::vector<std::wstring>& createdPaths);

    private:
        void EnsureLoadedLocked() const;
        bool LoadLocked() const;
        void QueueSaveLocked();
        void SaveWorkerLoop();
        static std::wstring NormalizeTags(std::wstring_view tags);
        static bool IsEmptyEntry(const UserMetadataEntry& entry) noexcept;

        mutable std::mutex mutex_;
        mutable bool loaded_{};
        mutable std::unordered_map<std::wstring, UserMetadataEntry> entries_;
        std::mutex saveMutex_;
        std::condition_variable saveAvailable_;
        std::thread saveWorker_;
        std::uint64_t requestedSaveGeneration_{};
        std::uint64_t completedSaveGeneration_{};
        bool saveShuttingDown_{};
    };
}
