#pragma once

#include <windows.h>

#include <array>
#include <vector>

#include "ui/CommandBarController.h"

struct ID2D1RenderTarget;
struct IDWriteTextFormat;

namespace hyperbrowse::ui
{
    class ToolbarIconLibrary;

    struct CommandBarPalette
    {
        COLORREF actionStripBackground{};
        COLORREF actionStripBorder{};
        COLORREF actionFieldBackground{};
        COLORREF text{};
        COLORREF mutedText{};
        COLORREF accent{};
        COLORREF accentFill{};
        COLORREF accentText{};
    };

    struct CommandBarPaintState
    {
        int hotMenuIndex{-1};
        int pressedMenuIndex{-1};
        int hotToolbarIndex{-1};
        int pressedToolbarIndex{-1};
        bool keyboardActive{};
        bool filterEditPresent{};
        bool filterFocused{};
    };

    class CommandBarPainter final
    {
    public:
        void PaintGdi(HDC hdc,
                      const RECT& stripRect,
                      const std::array<CommandBarController::CommandBarMenuButton, 4>& menuButtons,
                      const std::vector<CommandBarController::ToolbarItem>& toolbarItems,
                      const CommandBarPalette& palette,
                      HFONT menuFont,
                      ToolbarIconLibrary* iconLibrary,
                      const CommandBarPaintState& state) const;

        void PaintD2D(ID2D1RenderTarget* renderTarget,
                      const RECT& stripRect,
                      const std::array<CommandBarController::CommandBarMenuButton, 4>& menuButtons,
                      const std::vector<CommandBarController::ToolbarItem>& toolbarItems,
                      const CommandBarPalette& palette,
                      IDWriteTextFormat* textFormat,
                      ToolbarIconLibrary* iconLibrary,
                      const CommandBarPaintState& state) const;
    };
}
