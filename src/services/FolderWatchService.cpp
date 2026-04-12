#include "services/FolderWatchService.h"

#include <algorithm>
#include <array>
#include <filesystem>

#include "util/Log.h"

namespace fs = std::filesystem;

namespace
{
    void PostUpdate(HWND targetWindow, std::unique_ptr<hyperbrowse::services::FolderWatchUpdate> update)
    {
        if (!targetWindow)
        {
            return;
        }

        if (!PostMessageW(targetWindow,
                          hyperbrowse::services::FolderWatchService::kMessageId,
                          0,
                          reinterpret_cast<LPARAM>(update.get())))
        {
            return;
        }

        update.release();
    }

    bool ShouldStop(const std::shared_ptr<hyperbrowse::services::FolderWatchService::SharedState>& sharedState,
                    std::uint64_t requestId)
    {
        return sharedState->shutdown.load(std::memory_order_acquire)
            || sharedState->activeRequestId.load(std::memory_order_acquire) != requestId;
    }

    std::wstring NormalizePath(std::wstring path)
    {
        std::replace(path.begin(), path.end(), L'/', L'\\');
        while (path.size() > 3 && !path.empty() && path.back() == L'\\')
        {
            path.pop_back();
        }
        return path;
    }

    std::wstring CombinePath(const std::wstring& folderPath, const std::wstring& relativePath)
    {
        fs::path combined = fs::path(folderPath) / fs::path(relativePath);
        return NormalizePath(combined.wstring());
    }

    void CloseHandles(const std::shared_ptr<hyperbrowse::services::FolderWatchService::SharedState>& sharedState)
    {
        std::scoped_lock lock(sharedState->handleMutex);
        if (sharedState->directoryHandle != INVALID_HANDLE_VALUE)
        {
            CloseHandle(sharedState->directoryHandle);
            sharedState->directoryHandle = INVALID_HANDLE_VALUE;
        }
        if (sharedState->stopEvent)
        {
            CloseHandle(sharedState->stopEvent);
            sharedState->stopEvent = nullptr;
        }
    }

    void WatchLoop(const std::shared_ptr<hyperbrowse::services::FolderWatchService::SharedState>& sharedState,
                   HWND targetWindow,
                   const std::wstring& folderPath,
                   bool recursive,
                   std::uint64_t requestId)
    {
        const HANDLE directoryHandle = CreateFileW(folderPath.c_str(),
                                                   FILE_LIST_DIRECTORY,
                                                   FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                                   nullptr,
                                                   OPEN_EXISTING,
                                                   FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
                                                   nullptr);
        if (directoryHandle == INVALID_HANDLE_VALUE)
        {
            auto update = std::make_unique<hyperbrowse::services::FolderWatchUpdate>();
            update->requestId = requestId;
            update->folderPath = folderPath;
            update->requiresFullReload = true;
            update->message = L"Failed to open the folder watcher handle.";
            PostUpdate(targetWindow, std::move(update));
            return;
        }

        const HANDLE stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!stopEvent)
        {
            CloseHandle(directoryHandle);
            return;
        }

        {
            std::scoped_lock lock(sharedState->handleMutex);
            sharedState->directoryHandle = directoryHandle;
            sharedState->stopEvent = stopEvent;
        }

        constexpr DWORD kNotifyFilter = FILE_NOTIFY_CHANGE_FILE_NAME
            | FILE_NOTIFY_CHANGE_DIR_NAME
            | FILE_NOTIFY_CHANGE_LAST_WRITE
            | FILE_NOTIFY_CHANGE_SIZE
            | FILE_NOTIFY_CHANGE_CREATION;
        std::array<BYTE, 64 * 1024> buffer{};

        while (!ShouldStop(sharedState, requestId))
        {
            OVERLAPPED overlapped{};
            overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (!overlapped.hEvent)
            {
                break;
            }

            DWORD bytesReturned = 0;
            const BOOL started = ReadDirectoryChangesW(directoryHandle,
                                                       buffer.data(),
                                                       static_cast<DWORD>(buffer.size()),
                                                       recursive ? TRUE : FALSE,
                                                       kNotifyFilter,
                                                       &bytesReturned,
                                                       &overlapped,
                                                       nullptr);
            if (!started)
            {
                CloseHandle(overlapped.hEvent);
                auto update = std::make_unique<hyperbrowse::services::FolderWatchUpdate>();
                update->requestId = requestId;
                update->folderPath = folderPath;
                update->requiresFullReload = true;
                update->message = L"ReadDirectoryChangesW failed for the current folder watcher.";
                PostUpdate(targetWindow, std::move(update));
                break;
            }

            HANDLE waitHandles[] = {stopEvent, overlapped.hEvent};
            const DWORD waitResult = WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE);
            if (waitResult == WAIT_OBJECT_0)
            {
                CancelIoEx(directoryHandle, &overlapped);
                CloseHandle(overlapped.hEvent);
                break;
            }

            DWORD bytesTransferred = 0;
            const BOOL overlappedResult = GetOverlappedResult(directoryHandle, &overlapped, &bytesTransferred, FALSE);
            CloseHandle(overlapped.hEvent);
            if (!overlappedResult)
            {
                const DWORD error = GetLastError();
                if (error == ERROR_OPERATION_ABORTED && ShouldStop(sharedState, requestId))
                {
                    break;
                }

                auto update = std::make_unique<hyperbrowse::services::FolderWatchUpdate>();
                update->requestId = requestId;
                update->folderPath = folderPath;
                update->requiresFullReload = true;
                update->message = error == ERROR_NOTIFY_ENUM_DIR
                    ? L"Folder watcher overflowed and requested a full reload."
                    : L"Folder watcher could not process a filesystem notification.";
                PostUpdate(targetWindow, std::move(update));
                continue;
            }

            if (bytesTransferred == 0 || ShouldStop(sharedState, requestId))
            {
                continue;
            }

            auto update = std::make_unique<hyperbrowse::services::FolderWatchUpdate>();
            update->requestId = requestId;
            update->folderPath = folderPath;

            std::wstring pendingRenameOldPath;
            BYTE* current = buffer.data();
            while (current)
            {
                const auto* info = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(current);
                const std::wstring relativePath(info->FileName, info->FileNameLength / sizeof(WCHAR));
                const std::wstring fullPath = CombinePath(folderPath, relativePath);

                switch (info->Action)
                {
                case FILE_ACTION_ADDED:
                    update->events.push_back({hyperbrowse::services::FolderWatchEventKind::Added, fullPath, {}});
                    break;
                case FILE_ACTION_REMOVED:
                    update->events.push_back({hyperbrowse::services::FolderWatchEventKind::Removed, fullPath, {}});
                    break;
                case FILE_ACTION_MODIFIED:
                    update->events.push_back({hyperbrowse::services::FolderWatchEventKind::Modified, fullPath, {}});
                    break;
                case FILE_ACTION_RENAMED_OLD_NAME:
                    pendingRenameOldPath = fullPath;
                    break;
                case FILE_ACTION_RENAMED_NEW_NAME:
                    update->events.push_back({hyperbrowse::services::FolderWatchEventKind::Renamed, fullPath, pendingRenameOldPath});
                    pendingRenameOldPath.clear();
                    break;
                default:
                    break;
                }

                if (info->NextEntryOffset == 0)
                {
                    break;
                }

                current += info->NextEntryOffset;
            }

            if (!update->events.empty())
            {
                PostUpdate(targetWindow, std::move(update));
            }
        }

        CloseHandles(sharedState);
    }
}

namespace hyperbrowse::services
{
    FolderWatchService::FolderWatchService()
        : sharedState_(std::make_shared<SharedState>())
    {
    }

    FolderWatchService::~FolderWatchService()
    {
        sharedState_->shutdown.store(true, std::memory_order_release);
        Stop();
    }

    std::uint64_t FolderWatchService::StartWatching(HWND targetWindow, std::wstring folderPath, bool recursive)
    {
        Stop();

        const std::uint64_t requestId = nextRequestId_.fetch_add(1, std::memory_order_acq_rel) + 1;
        sharedState_->activeRequestId.store(requestId, std::memory_order_release);
        worker_ = std::thread([sharedState = sharedState_, targetWindow, folderPath = NormalizePath(std::move(folderPath)), recursive, requestId]() mutable
        {
            util::LogInfo(L"Starting folder watch for " + folderPath);
            WatchLoop(sharedState, targetWindow, folderPath, recursive, requestId);
        });
        return requestId;
    }

    void FolderWatchService::Stop()
    {
        sharedState_->activeRequestId.fetch_add(1, std::memory_order_acq_rel);

        HANDLE directoryHandle = INVALID_HANDLE_VALUE;
        HANDLE stopEvent = nullptr;
        {
            std::scoped_lock lock(sharedState_->handleMutex);
            directoryHandle = sharedState_->directoryHandle;
            stopEvent = sharedState_->stopEvent;
        }

        if (stopEvent)
        {
            SetEvent(stopEvent);
        }
        if (directoryHandle != INVALID_HANDLE_VALUE)
        {
            CancelIoEx(directoryHandle, nullptr);
        }

        if (worker_.joinable())
        {
            worker_.join();
        }
    }
}