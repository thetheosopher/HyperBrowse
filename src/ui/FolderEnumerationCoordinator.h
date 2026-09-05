#pragma once

#include <windows.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "browser/BrowserModel.h"
#include "services/FolderEnumerationService.h"

namespace hyperbrowse::ui
{
    class FolderEnumerationCoordinator
    {
    public:
        static constexpr UINT_PTR kPresentationTimerId = 9102;

        using BatchHandler = std::function<void(std::vector<browser::BrowserItem>,
                                                 std::uint64_t,
                                                 std::uint64_t)>;
        using CompletionHandler = std::function<void(std::wstring, std::uint64_t, std::uint64_t)>;
        using FailureHandler = std::function<void(std::wstring, std::wstring)>;
        using PresentationHandler = std::function<bool(bool)>;
        using SettledHandler = std::function<void(bool)>;

        struct Handlers
        {
            BatchHandler onBatch;
            CompletionHandler onCompleted;
            FailureHandler onFailed;
            PresentationHandler onPresentation;
            SettledHandler onSettled;
        };

        FolderEnumerationCoordinator() = default;
        FolderEnumerationCoordinator(const FolderEnumerationCoordinator&) = delete;
        FolderEnumerationCoordinator& operator=(const FolderEnumerationCoordinator&) = delete;

        std::uint64_t Start(HWND targetWindow,
                            std::wstring folderPath,
                            bool recursive,
                            bool includeSubfolders);
        void Cancel();
        bool IsActive() const noexcept;
        void HandleMessage(LPARAM lParam, const Handlers& handlers);
        void HandlePresentationTimer(const PresentationHandler& handler);

    private:
        static constexpr UINT kPresentationIntervalMs = 50;

        void SchedulePresentation();
        void FlushPresentation(bool clearStartupPathsIfNotFound,
                               const PresentationHandler& handler);
        void StopPresentationTimer();

        services::FolderEnumerationService service_;
        HWND targetWindow_{};
        std::uint64_t activeRequestId_{};
        bool active_{};
        bool firstBatchPresented_{};
        bool presentationPending_{};
        UINT_PTR presentationTimerId_{};
    };
}
