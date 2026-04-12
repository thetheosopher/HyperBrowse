#pragma once

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "browser/BrowserModel.h"

namespace hyperbrowse::services
{
    enum class BatchConvertFormat
    {
        Jpeg,
        Png,
        Tiff,
    };

    struct BatchConvertUpdate
    {
        std::uint64_t requestId{};
        std::size_t completedCount{};
        std::size_t totalCount{};
        std::size_t failedCount{};
        BatchConvertFormat format{BatchConvertFormat::Jpeg};
        std::wstring outputFolder;
        std::wstring currentFileName;
        bool finished{};
        bool cancelled{};
        std::wstring message;
    };

    std::wstring BatchConvertFormatToLabel(BatchConvertFormat format);
    std::wstring BatchConvertFormatExtension(BatchConvertFormat format);

    class BatchConvertService
    {
    public:
        static constexpr UINT kMessageId = WM_APP + 47;

        BatchConvertService();
        ~BatchConvertService();

        std::uint64_t Start(HWND targetWindow,
                            std::vector<browser::BrowserItem> items,
                            std::wstring outputFolder,
                            BatchConvertFormat format);
        void Cancel();

    private:
        struct SharedState
        {
            std::atomic_uint64_t activeRequestId{0};
            std::atomic_bool shutdown{false};
        };

        std::shared_ptr<SharedState> sharedState_;
        std::thread worker_;
        std::atomic_uint64_t nextRequestId_{0};
    };
}