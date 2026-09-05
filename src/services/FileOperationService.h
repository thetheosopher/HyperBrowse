#pragma once

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "util/BackgroundExecutor.h"

namespace hyperbrowse::services
{
    struct FileOperationSharedState;

    enum class FileOperationType : int
    {
        Copy = 0,
        Move = 1,
        DeleteRecycleBin = 2,
        DeletePermanent = 3,
        Rename = 4,
    };

    enum class FileConflictPolicy : int
    {
        PromptShell = 0,
        OverwriteExisting = 1,
        AutoRenameNumericSuffix = 2,
    };

    struct FileConflictPlan
    {
        std::size_t conflictCount{};
        std::size_t renamedCount{};
        std::vector<std::wstring> targetLeafNames;
    };

    struct FileOperationUpdate
    {
        std::uint64_t requestId{};
        FileOperationType type{FileOperationType::Copy};
        std::size_t requestedCount{};
        std::size_t failedCount{};
        std::vector<std::wstring> succeededSourcePaths;
        std::vector<std::wstring> createdPaths;
        std::wstring destinationFolder;
        bool finished{};
        bool aborted{};
        std::wstring message;
    };

    // Lightweight progress tick posted while an operation runs (percent 0-100).
    struct FileOperationProgress
    {
        std::uint64_t requestId{};
        UINT completed{};
        UINT total{};
    };

    FileConflictPlan PlanDestinationConflicts(const std::vector<std::wstring>& sourcePaths,
                                              std::wstring_view destinationFolder,
                                              FileConflictPolicy conflictPolicy);

    std::wstring FileOperationTypeToLabel(FileOperationType type);
    std::wstring FileOperationTypeToActivityLabel(FileOperationType type);

    class FileOperationService
    {
    public:
        static constexpr UINT kMessageId = WM_APP + 48;
        static constexpr UINT kProgressMessageId = WM_APP + 51;
        using PerformOperationsCallback = std::function<HRESULT()>;

        explicit FileOperationService(PerformOperationsCallback performOperationsCallback = {});
        ~FileOperationService();

        void Cancel() noexcept;
        void Shutdown() noexcept;

        std::uint64_t Start(HWND targetWindow,
                            HWND ownerWindow,
                            FileOperationType type,
                            std::vector<std::wstring> sourcePaths,
                            std::wstring destinationFolder = {},
                            FileConflictPolicy conflictPolicy = FileConflictPolicy::PromptShell,
                            std::vector<std::wstring> targetLeafNames = {});
        std::size_t ActiveTaskCount() const noexcept
        {
            return executor_.ActiveTaskCount();
        }

    private:
        std::shared_ptr<FileOperationSharedState> sharedState_;
        util::BackgroundExecutor executor_;
        PerformOperationsCallback performOperationsCallback_;
        std::atomic_uint64_t nextRequestId_{0};
    };
}
