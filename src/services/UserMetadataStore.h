#pragma once

#include <windows.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
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

        bool operator==(const UserMetadataEntry&) const = default;
    };

    class UserMetadataStore
    {
    public:
        static constexpr UINT kMessageId = WM_APP + 76;

        explicit UserMetadataStore(std::wstring storageDirectory = {});
        ~UserMetadataStore();

        UserMetadataStore(const UserMetadataStore&) = delete;
        UserMetadataStore& operator=(const UserMetadataStore&) = delete;

        UserMetadataEntry EntryForPath(std::wstring_view filePath) const;
        void SetRating(const std::vector<std::wstring>& filePaths, int rating);
        void SetTags(const std::vector<std::wstring>& filePaths, std::wstring_view tags);
        void ApplyFileOperationUpdate(FileOperationType type,
                                      const std::vector<std::wstring>& sourcePaths,
                                      const std::vector<std::wstring>& createdPaths);

        void SetNotificationWindow(HWND window) noexcept;
        std::wstring LastSaveError() const;
        bool Flush(std::chrono::milliseconds timeout, std::wstring* errorMessage = nullptr);
        void Shutdown();

    private:
        struct PendingMutation
        {
            std::optional<UserMetadataEntry> entry;
            std::uint64_t generation{};
        };

        void EnsureLoadedLocked() const;
        bool LoadLocked() const;
        void QueueSaveLocked(const std::vector<std::wstring>& changedKeys);
        void SaveWorkerLoop() noexcept;
        static std::wstring NormalizeTags(std::wstring_view tags);
        static bool IsEmptyEntry(const UserMetadataEntry& entry) noexcept;

        std::wstring storageDirectory_;
        std::wstring metadataFilePath_;
        std::wstring lockFilePath_;

        mutable std::mutex mutex_;
        mutable bool loaded_{};
        mutable std::unordered_map<std::wstring, UserMetadataEntry> entries_;
        std::unordered_map<std::wstring, PendingMutation> pendingMutations_;
        std::uint64_t mutationGeneration_{};

        mutable std::mutex saveMutex_;
        std::condition_variable saveAvailable_;
        std::condition_variable saveFinished_;
        std::uint64_t requestedSaveGeneration_{};
        std::uint64_t attemptedSaveGeneration_{};
        std::uint64_t successfulSaveGeneration_{};
        bool saveShuttingDown_{};
        bool shutdownComplete_{};
        std::wstring lastSaveError_;
        std::atomic<HWND> notificationWindow_{nullptr};
        std::thread saveWorker_;
    };
}
