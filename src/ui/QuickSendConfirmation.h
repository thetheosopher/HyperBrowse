#pragma once

#include <optional>
#include <string>

#include "services/FileOperationService.h"

namespace hyperbrowse::ui
{
    struct QuickSendConfirmationRequest
    {
        services::FileOperationType type{services::FileOperationType::Copy};
        std::size_t requestedCount{};
        std::size_t successfulCount{};
        std::wstring displaySourcePath;
        std::wstring destinationFolder;
        std::optional<wchar_t> destinationShortcut;
    };

    std::optional<std::wstring> BuildQuickSendConfirmation(
        const QuickSendConfirmationRequest& request);
}
