#pragma once

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "browser/BrowserModel.h"

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

        std::uint64_t EnumerateFolderAsync(HWND targetWindow, std::wstring folderPath, bool recursive);
        void Cancel();

    private:
        std::shared_ptr<FolderEnumerationSharedState> sharedState_;
        std::atomic_uint64_t nextRequestId_{0};
    };
}