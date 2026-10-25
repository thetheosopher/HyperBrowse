#pragma once

#include <windows.h>
#include <commctrl.h>

#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "services/FolderTreeEnumerationService.h"

namespace hyperbrowse::services
{
    struct FolderTreeChild;
}

namespace hyperbrowse::ui
{
    class FolderTreeController
    {
    public:
        struct NodeData
        {
            std::wstring path;
            bool childrenKnown{};
            bool hasChildren{};
            bool childrenLoaded{};
            bool childrenLoading{};
            std::uint64_t childEnumerationRequestId{};
            bool childPresenceLoading{};
            std::uint64_t childPresenceRequestId{};
            bool childrenFilling{};
            bool shellInfoPending{};
        };

        using StatusChangedHandler = std::function<void()>;
        using SelectionSuppressionHandler = std::function<void(bool)>;

        struct Handlers
        {
            StatusChangedHandler onStatusChanged;
            SelectionSuppressionHandler onSelectionSuppressionChanged;
        };

        static constexpr UINT kEnumerationMessageId = services::FolderTreeEnumerationService::kMessageId;

        FolderTreeController() = default;
        ~FolderTreeController();
        FolderTreeController(const FolderTreeController&) = delete;
        FolderTreeController& operator=(const FolderTreeController&) = delete;

        void Configure(HWND ownerWindow, HWND treeWindow, Handlers handlers);
        void Cancel();
        bool IsBusy() const noexcept;

        void Initialize(std::wstring selectedFolderPath);
        void Refresh(std::wstring selectedFolderPath);
        void SelectFolder(std::wstring folderPath);
        void RequestChildren(HTREEITEM item);
        void InvalidateChildPresence(std::wstring_view folderPath);
        void ClearChildPresenceCache();
        void InsertFolderIfParentLoaded(std::wstring folderPath);

        HTREEITEM FindItemByPath(const std::wstring& folderPath) const;
        NodeData* GetNodeData(HTREEITEM item) const;
        std::wstring GetSelectedFolderPath() const;
        LRESULT HandleEnumerationMessage(LPARAM lParam);

    private:
        struct ChildPresenceCacheEntry
        {
            bool hasChildren{};
            std::uint64_t checkedTickCount{};
        };

        using Clock = std::chrono::steady_clock;

        // Children are inserted in slices so a very large folder never blocks the UI thread.
        struct FillState
        {
            HTREEITEM parent{};
            NodeData* parentData{};
            std::vector<services::FolderTreeChild> children;
            std::size_t next{};
            std::vector<HTREEITEM> inserted;
            HTREEITEM loadingItem{};
            bool stale{};
            Clock::duration uiTime{};
        };

        static constexpr std::uint64_t kChildPresenceCacheTtlMs = 3000;
        static constexpr std::chrono::milliseconds kSliceBudget{8};
        static constexpr UINT kSliceIntervalMs = 10;
        static constexpr std::size_t kLargeFillLogThreshold = 256;

        static void CALLBACK SliceTimerProc(HWND window, UINT message, UINT_PTR timerId, DWORD tickCount);

        void PopulateSpecialFolderRoots();
        void PopulateDriveRoots();
        HTREEITEM InsertItem(HTREEITEM parentItem,
                             const std::wstring& folderPath,
                             bool childrenKnown = false,
                             bool hasChildren = false,
                             bool requestPresence = true,
                             bool deferShellInfo = false);
        bool HasPendingSliceWork() const noexcept;
        void ScheduleSlices();
        void StopSliceTimer();
        void RunSlice(Clock::time_point deadline);
        bool RunFillSlice(FillState& fill, Clock::time_point deadline);
        void EnsureLoadingItem(FillState& fill);
        void FinishFill(FillState& fill);
        void RunShellInfoSlice(Clock::time_point deadline);
        void RefineShellInfo(HTREEITEM item);
        void AddPlaceholder(HTREEITEM parentItem);
        void RequestChildPresence(const std::vector<HTREEITEM>& items);
        bool TryGetCachedChildPresence(std::wstring_view folderPath, bool* hasChildren) const;
        void CacheChildPresence(std::wstring_view folderPath, bool hasChildren);
        void UpdateChildrenIndicator(HTREEITEM item);
        void ApplyChildren(HTREEITEM item, std::vector<services::FolderTreeChild> childFolders);
        void ContinueSelectingFolder();
        void RestoreItemVerticalPosition(HTREEITEM item, const std::wstring& selectedPath);
        HTREEITEM FindChildItem(HTREEITEM parentItem, const std::wstring& folderPath) const;
        void NotifyStatusChanged() const;
        void SetSelectionSuppressed(bool suppressed) const;

        HWND ownerWindow_{};
        HWND treeWindow_{};
        Handlers handlers_;
        services::FolderTreeEnumerationService service_;
        std::vector<std::unique_ptr<NodeData>> nodes_;
        std::unordered_map<std::wstring, ChildPresenceCacheEntry> childPresenceCache_;
        std::unordered_map<std::uint64_t, HTREEITEM> pendingEnumerationItems_;
        std::unordered_map<std::uint64_t, std::vector<HTREEITEM>> pendingChildPresenceItems_;
        std::deque<FillState> fills_;
        std::deque<HTREEITEM> shellInfoQueue_;
        bool sliceTimerActive_{};
        std::wstring pendingSelectionPath_;
        std::wstring selectionRestorePath_;
        int selectionRestoreY_{};
    };
}
