#pragma once

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "browser/BrowserModel.h"
#include "util/BackgroundExecutor.h"

namespace hyperbrowse::services
{
    struct FolderEnumerationSharedState;

    enum class FolderEnumerationUpdateKind
    {
        Batch,
        Completed,
        Failed,
    };

    struct FolderEnumerationUpdate
    {
        std::uint64_t requestId{};
        FolderEnumerationUpdateKind kind{FolderEnumerationUpdateKind::Batch};
        std::wstring folderPath;
        std::vector<hyperbrowse::browser::BrowserItem> items;
        std::uint64_t totalCount{};
        std::uint64_t totalBytes{};
        std::wstring message;
    };

    class FolderEnumerationService
    {
    public:
        static constexpr UINT kMessageId = WM_APP + 41;

        FolderEnumerationService();
        ~FolderEnumerationService();

        std::uint64_t EnumerateFolderAsync(HWND targetWindow,
                           std::wstring folderPath,
                           bool recursive,
                           bool includeSubfolders = false);
        void Cancel();
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
        std::shared_ptr<FolderEnumerationSharedState> sharedState_;
        util::BackgroundExecutor executor_;
        std::atomic_uint64_t nextRequestId_{0};
        std::atomic_uint64_t cancellationCount_{0};
    };
}
