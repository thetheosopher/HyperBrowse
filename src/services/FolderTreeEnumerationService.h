#pragma once

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <future>
#include <memory>
#include <string>
#include <vector>

namespace hyperbrowse::services
{
    struct FolderTreeEnumerationSharedState;

    enum class FolderTreeEnumerationUpdateKind
    {
        Completed,
        Failed,
    };

    struct FolderTreeEnumerationUpdate
    {
        std::uint64_t requestId{};
        FolderTreeEnumerationUpdateKind kind{FolderTreeEnumerationUpdateKind::Completed};
        std::wstring folderPath;
        std::vector<std::wstring> childFolders;
        std::wstring message;
    };

    class FolderTreeEnumerationService
    {
    public:
        static constexpr UINT kMessageId = WM_APP + 49;

        FolderTreeEnumerationService();
        ~FolderTreeEnumerationService();

        std::uint64_t EnumerateChildDirectoriesAsync(HWND targetWindow, std::wstring folderPath);
        void CancelAll();

    private:
        void ReapCompletedWorkers();
        void WaitForWorkers();

        std::shared_ptr<FolderTreeEnumerationSharedState> sharedState_;
        std::vector<std::future<void>> workers_;
        std::atomic_uint64_t nextRequestId_{0};
    };
}