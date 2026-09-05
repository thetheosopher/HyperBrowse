#pragma once

#include <windows.h>

#include <array>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace hyperbrowse::ui
{
    class CommandBarController final
    {
    public:
        enum class ToolbarItemKind
        {
            IconButton,
            IconToggle,
            IconDropdown,
            Separator,
            FilterEdit,
        };

        enum class ToolbarAlignment
        {
            Left,
            Right,
        };

        struct ToolbarItem
        {
            UINT commandId{};
            std::string iconName;
            std::wstring tooltip;
            ToolbarItemKind kind{ToolbarItemKind::IconButton};
            ToolbarAlignment alignment{ToolbarAlignment::Left};
            bool enabled{true};
            bool checked{};
            RECT rect{};
        };

        struct CommandBarMenuButton
        {
            std::wstring label;
            wchar_t mnemonic{};
            HMENU menu{};
            RECT rect{};
        };

        struct ToolbarState
        {
            bool canNavigateBack{};
            bool canNavigateForward{};
            bool folderEnumerationActive{};
            bool pendingNavigation{};
            bool recursiveChecked{};
            bool thumbnailsChecked{};
            bool detailsChecked{};
            bool thumbnailSizeEnabled{};
            bool compareEnabled{};
            bool selectionActionsEnabled{};
        };

        using TextWidthHandler = std::function<int(HFONT, std::wstring_view)>;

        CommandBarController() = default;
        CommandBarController(const CommandBarController&) = delete;
        CommandBarController& operator=(const CommandBarController&) = delete;

        void InitializeItems();
        void SetMenuButton(std::size_t index, std::wstring label, wchar_t mnemonic, HMENU menu);
        void Layout(int clientWidth, int itemTop, HFONT menuFont, const TextWidthHandler& measureTextWidth);
        void UpdateItemStates(const ToolbarState& state);

        int MenuHitTest(int x, int y) const;
        int ToolbarHitTest(int x, int y) const;

        std::vector<ToolbarItem>& Items();
        const std::vector<ToolbarItem>& Items() const;
        std::array<CommandBarMenuButton, 4>& MenuButtons();
        const std::array<CommandBarMenuButton, 4>& MenuButtons() const;

    private:
        std::array<CommandBarMenuButton, 4> menuButtons_{};
        std::vector<ToolbarItem> items_;
    };
}
