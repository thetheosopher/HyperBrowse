#include "ui/QuickSendConfirmation.h"

#include <filesystem>

namespace hyperbrowse::ui
{
    namespace
    {
        namespace fs = std::filesystem;

        std::wstring DestinationLabel(std::wstring_view destinationFolder)
        {
            if (destinationFolder.empty())
            {
                return {};
            }

            const fs::path path(destinationFolder);
            const std::wstring leaf = path.filename().wstring();
            if (leaf.empty() || leaf == destinationFolder)
            {
                return std::wstring(destinationFolder);
            }

            return leaf + L" (" + std::wstring(destinationFolder) + L")";
        }
    }

    std::optional<std::wstring> BuildQuickSendConfirmation(
        const QuickSendConfirmationRequest& request)
    {
        if (request.successfulCount == 0 || request.destinationFolder.empty())
        {
            return std::nullopt;
        }

        const wchar_t* operation = request.type == services::FileOperationType::Move
            ? L"Moved"
            : L"Copied";
        std::wstring message = operation;
        message.push_back(L' ');

        if (!request.displaySourcePath.empty())
        {
            const std::wstring sourceName = fs::path(request.displaySourcePath).filename().wstring();
            message.append(sourceName.empty() ? request.displaySourcePath : sourceName);
        }
        else if (request.successfulCount == request.requestedCount)
        {
            message.append(std::to_wstring(request.successfulCount));
            message.append(request.successfulCount == 1 ? L" item" : L" items");
        }
        else
        {
            message.append(std::to_wstring(request.successfulCount));
            message.append(L" of ");
            message.append(std::to_wstring(request.requestedCount));
            message.append(L" items");
        }

        message.append(L" to ");
        message.append(DestinationLabel(request.destinationFolder));
        if (request.destinationShortcut)
        {
            message.append(L" [");
            message.push_back(*request.destinationShortcut);
            message.push_back(L']');
        }

        return message;
    }
}
