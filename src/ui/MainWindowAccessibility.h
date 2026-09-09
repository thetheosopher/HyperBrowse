#pragma once

#include <windows.h>
#include <oleacc.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace hyperbrowse::ui
{
    class MainWindowAccessibility final
    {
    public:
        struct Item
        {
            std::wstring name;
            std::wstring value;
            std::wstring description;
            std::wstring defaultAction;
            std::wstring keyboardShortcut;
            RECT bounds{};
            long role{ROLE_SYSTEM_CLIENT};
            long state{STATE_SYSTEM_FOCUSABLE};
        };

        using SnapshotProvider = std::function<std::vector<Item>()>;
        using FocusedChildProvider = std::function<long()>;
        using FocusProvider = std::function<bool(long)>;
        using DefaultActionProvider = std::function<bool(long)>;

        MainWindowAccessibility(HWND window,
                                SnapshotProvider snapshotProvider,
                                FocusedChildProvider focusedChildProvider,
                                FocusProvider focusProvider,
                                DefaultActionProvider defaultActionProvider,
                                std::wstring rootName = L"HyperBrowse",
                                std::wstring rootDescription = L"Keyboard-accessible image browser window",
                                long rootRole = ROLE_SYSTEM_WINDOW);
        ~MainWindowAccessibility();

        LRESULT HandleGetObject(WPARAM wParam, LPARAM lParam) const;
        void NotifyFocusChanged() const;
        void NotifyStateChanged(long childId) const;

    private:
        class AccessibleObject;
        struct Data;

        std::shared_ptr<Data> data_;
    };
}
