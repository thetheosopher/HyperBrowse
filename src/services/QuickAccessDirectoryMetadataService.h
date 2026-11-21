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
    struct QuickAccessDirectoryImageCount
    {
        std::wstring folderPath;
        bool available{};
        std::uintmax_t imageCount{};
        DWORD errorCode{};
    };

    struct QuickAccessDirectoryMetadataUpdate
    {
        std::uint64_t requestId{};
        std::vector<QuickAccessDirectoryImageCount> imageCounts;
    };

    struct QuickAccessDirectoryMetadataSharedState;

    class QuickAccessDirectoryMetadataService final
    {
    public:
        static constexpr UINT kMessageId = WM_APP + 55;

        QuickAccessDirectoryMetadataService();
        ~QuickAccessDirectoryMetadataService();

        QuickAccessDirectoryMetadataService(const QuickAccessDirectoryMetadataService&) = delete;
        QuickAccessDirectoryMetadataService& operator=(const QuickAccessDirectoryMetadataService&) = delete;

        std::uint64_t CountSupportedImagesAsync(HWND targetWindow,
                                                std::vector<std::wstring> folderPaths);
        void CancelAll();

    private:
        std::shared_ptr<QuickAccessDirectoryMetadataSharedState> sharedState_;
        util::BackgroundExecutor executor_;
        std::atomic_uint64_t nextRequestId_{};
    };
}
