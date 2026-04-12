#pragma once

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace hyperbrowse::services
{
    enum class FolderWatchEventKind
    {
        Added,
        Removed,
        Modified,
        Renamed,
    };

    struct FolderWatchEvent
    {
        FolderWatchEventKind kind{FolderWatchEventKind::Modified};
        std::wstring path;
        std::wstring oldPath;
    };

    struct FolderWatchUpdate
    {
        std::uint64_t requestId{};
        std::wstring folderPath;
        std::vector<FolderWatchEvent> events;
        bool requiresFullReload{};
        std::wstring message;
    };

    class FolderWatchService
    {
    public:
        static constexpr UINT kMessageId = WM_APP + 46;

        struct SharedState
        {
            std::atomic_uint64_t activeRequestId{0};
            std::atomic_bool shutdown{false};
            std::mutex handleMutex;
            HANDLE directoryHandle{INVALID_HANDLE_VALUE};
            HANDLE stopEvent{};
        };

        FolderWatchService();
        ~FolderWatchService();

        std::uint64_t StartWatching(HWND targetWindow, std::wstring folderPath, bool recursive);
        void Stop();

    private:
        std::shared_ptr<SharedState> sharedState_;
        std::thread worker_;
        std::atomic_uint64_t nextRequestId_{0};
    };
}