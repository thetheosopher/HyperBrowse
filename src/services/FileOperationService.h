#pragma once

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <future>
#include <string>
#include <string_view>
#include <vector>

namespace hyperbrowse::services
{
    enum class FileOperationType : int
    {
        Copy = 0,
        Move = 1,
        DeleteRecycleBin = 2,
        DeletePermanent = 3,
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

    FileConflictPlan PlanDestinationConflicts(const std::vector<std::wstring>& sourcePaths,
                                              std::wstring_view destinationFolder,
                                              FileConflictPolicy conflictPolicy);

    std::wstring FileOperationTypeToLabel(FileOperationType type);
    std::wstring FileOperationTypeToActivityLabel(FileOperationType type);

    class FileOperationService
    {
    public:
        static constexpr UINT kMessageId = WM_APP + 48;

        FileOperationService() = default;
        ~FileOperationService();

        std::uint64_t Start(HWND targetWindow,
                            HWND ownerWindow,
                            FileOperationType type,
                            std::vector<std::wstring> sourcePaths,
                            std::wstring destinationFolder = {},
                            FileConflictPolicy conflictPolicy = FileConflictPolicy::PromptShell,
                            std::vector<std::wstring> targetLeafNames = {});

    private:
        void ReapCompletedWorkers();
        void WaitForWorkers();

        std::vector<std::future<void>> workers_;
        std::atomic_uint64_t nextRequestId_{0};
    };
}