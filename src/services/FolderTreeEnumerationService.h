#pragma once

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "util/BackgroundExecutor.h"

namespace hyperbrowse::services
{
    struct FolderTreeEnumerationSharedState;

    enum class FolderTreeEnumerationUpdateKind
    {
        Completed,
        ChildPresenceCompleted,
        Failed,
    };

    struct FolderTreeChild
    {
        std::wstring path;
        bool hasChildren{};
    };

    struct FolderTreeEnumerationUpdate
    {
        std::uint64_t requestId{};
        FolderTreeEnumerationUpdateKind kind{FolderTreeEnumerationUpdateKind::Completed};
        std::wstring folderPath;
        std::vector<FolderTreeChild> childFolders;
        std::vector<FolderTreeChild> childPresenceResults;
        std::wstring message;
    };

    class FolderTreeEnumerationService
    {
    public:
        static constexpr UINT kMessageId = WM_APP + 49;

        FolderTreeEnumerationService();
        ~FolderTreeEnumerationService();

        std::uint64_t EnumerateChildDirectoriesAsync(HWND targetWindow, std::wstring folderPath);
        std::uint64_t QueryChildDirectoryPresenceAsync(HWND targetWindow, std::wstring folderPath);
        std::uint64_t QueryChildDirectoryPresenceAsync(HWND targetWindow,
                                   std::vector<std::wstring> folderPaths);
        void CancelAll();
        std::size_t ActiveTaskCount() const noexcept
        {
            return executor_.ActiveTaskCount();
        }
        std::size_t PendingTaskCount() const noexcept
        {
            return executor_.PendingTaskCount();
        }
        std::size_t RejectedTaskCount() const noexcept
        {
            return executor_.RejectedTaskCount();
        }
        std::size_t PeakPendingTaskCount() const noexcept
        {
            return executor_.PeakPendingTaskCount();
        }
        std::uint64_t CancellationCount() const noexcept
        {
            return cancellationCount_.load(std::memory_order_acquire);
        }

    private:
        std::shared_ptr<FolderTreeEnumerationSharedState> sharedState_;
        util::BackgroundExecutor executor_;
        std::atomic_uint64_t nextRequestId_{0};
        std::atomic_uint64_t cancellationCount_{0};
    };
}
