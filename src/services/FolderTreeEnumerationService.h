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

    private:
        std::shared_ptr<FolderTreeEnumerationSharedState> sharedState_;
        util::BackgroundExecutor executor_;
        std::atomic_uint64_t nextRequestId_{0};
    };
}