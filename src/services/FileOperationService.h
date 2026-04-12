#pragma once

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <future>
#include <string>
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
                            std::wstring destinationFolder = {});

    private:
        void ReapCompletedWorkers();
        void WaitForWorkers();

        std::vector<std::future<void>> workers_;
        std::atomic_uint64_t nextRequestId_{0};
    };
}