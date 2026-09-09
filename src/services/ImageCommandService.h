#pragma once

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "browser/BrowserModel.h"
#include "util/BackgroundExecutor.h"

namespace hyperbrowse::cache
{
    class CachedThumbnail;
}

namespace hyperbrowse::services
{
    struct ImageMetadata;

    enum class ImageCommandKind
    {
        Information,
        CopyPixels,
        AdjustJpegOrientation,
    };

    struct ImageCommandUpdate
    {
        std::uint64_t requestId{};
        ImageCommandKind kind{ImageCommandKind::Information};
        HWND ownerWindow{};
        browser::BrowserItem item;
        std::shared_ptr<const ImageMetadata> metadata;
        std::shared_ptr<const cache::CachedThumbnail> image;
        std::vector<std::wstring> updatedPaths;
        std::size_t completedCount{};
        std::size_t totalCount{};
        std::size_t succeededCount{};
        std::size_t failedCount{};
        bool finished{};
        bool cancelled{};
        std::wstring message;
    };

    class ImageCommandService final
    {
    public:
        static constexpr UINT kMessageId = WM_APP + 77;

        ImageCommandService();
        ~ImageCommandService();

        ImageCommandService(const ImageCommandService&) = delete;
        ImageCommandService& operator=(const ImageCommandService&) = delete;

        std::uint64_t StartInformation(HWND targetWindow, HWND ownerWindow, browser::BrowserItem item);
        std::uint64_t StartCopyPixels(HWND targetWindow, HWND ownerWindow, browser::BrowserItem item);
        std::uint64_t StartJpegOrientation(HWND targetWindow,
                                           HWND ownerWindow,
                                           std::vector<browser::BrowserItem> items,
                                           int quarterTurnsDelta);
        void Cancel() noexcept;
        void Shutdown() noexcept;

        std::size_t ActiveTaskCount() const noexcept
        {
            return executor_.ActiveTaskCount();
        }

    private:
        struct SharedState
        {
            std::atomic_uint64_t activeRequestId{0};
            std::atomic_bool shutdown{false};
        };

        std::uint64_t BeginRequest() noexcept;
        bool PostOrReportFailure(HWND targetWindow,
                                 std::uint64_t requestId,
                                 ImageCommandKind kind,
                                 HWND ownerWindow,
                                 std::function<void()> task);

        std::shared_ptr<SharedState> sharedState_;
        util::BackgroundExecutor executor_;
        std::atomic_uint64_t nextRequestId_{0};
    };
}
