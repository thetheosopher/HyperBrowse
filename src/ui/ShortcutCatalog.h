#pragma once

#include <array>
#include <cstddef>
#include <span>
#include <string_view>

#include "ui/CommandIds.h"

namespace hyperbrowse::ui
{
    enum class ShortcutContext
    {
        MainWindow,
        Viewer,
    };

    struct ShortcutDefinition
    {
        ShortcutContext context;
        UINT commandId;
        WORD virtualKey;
        BYTE modifiers;
        std::wstring_view displayChord;
        std::wstring_view action;
        std::wstring_view group;
    };

    struct MenuMnemonicDefinition
    {
        wchar_t mnemonic;
        std::wstring_view label;
    };

    inline constexpr std::array kMainMenuMnemonicCatalog{
        MenuMnemonicDefinition{L'F', L"File"},
        MenuMnemonicDefinition{L'E', L"Edit"},
        MenuMnemonicDefinition{L'V', L"View"},
        MenuMnemonicDefinition{L'H', L"Help"},
    };

    template <std::size_t Count>
    constexpr bool HasDuplicateMenuMnemonics(const std::array<MenuMnemonicDefinition, Count>& catalog) noexcept
    {
        for (std::size_t index = 0; index < Count; ++index)
        {
            for (std::size_t otherIndex = index + 1; otherIndex < Count; ++otherIndex)
            {
                if (catalog[index].mnemonic == catalog[otherIndex].mnemonic)
                {
                    return true;
                }
            }
        }
        return false;
    }

    constexpr int MainMenuMnemonicIndexFromVirtualKey(WORD virtualKey) noexcept
    {
        const wchar_t key = virtualKey >= static_cast<WORD>('a') && virtualKey <= static_cast<WORD>('z')
            ? static_cast<wchar_t>(virtualKey - (static_cast<WORD>('a') - static_cast<WORD>('A')))
            : static_cast<wchar_t>(virtualKey);
        for (std::size_t index = 0; index < kMainMenuMnemonicCatalog.size(); ++index)
        {
            if (kMainMenuMnemonicCatalog[index].mnemonic == key)
            {
                return static_cast<int>(index);
            }
        }
        return -1;
    }

    inline constexpr bool kMainMenuMnemonicCatalogValid = !HasDuplicateMenuMnemonics(kMainMenuMnemonicCatalog);

    inline constexpr std::array kMainWindowShortcutCatalog{
        ShortcutDefinition{ShortcutContext::MainWindow, command_ids::ID_FILE_OPEN_FOLDER, static_cast<WORD>('O'), FCONTROL, L"Ctrl+O", L"Open folder", L"File"},
        ShortcutDefinition{ShortcutContext::MainWindow, command_ids::ID_HELP_USER_GUIDE, VK_F1, 0, L"F1", L"Open user guide", L"Help"},
        ShortcutDefinition{ShortcutContext::MainWindow, command_ids::ID_VIEW_NAVIGATE_BACK_FOLDER, VK_BACK, 0, L"Backspace", L"Navigate to the previous folder", L"View and navigation"},
        ShortcutDefinition{ShortcutContext::MainWindow, command_ids::ID_VIEW_NAVIGATE_BACK_FOLDER, VK_LEFT, FALT, L"Alt+Left", L"Navigate to the previous folder", L"View and navigation"},
        ShortcutDefinition{ShortcutContext::MainWindow, command_ids::ID_VIEW_NAVIGATE_FORWARD_FOLDER, VK_RIGHT, FALT, L"Alt+Right", L"Navigate to the next folder", L"View and navigation"},
        ShortcutDefinition{ShortcutContext::MainWindow, command_ids::ID_FILE_ESCAPE, VK_ESCAPE, 0, L"Esc", L"Close the main window when enabled", L"File"},
        ShortcutDefinition{ShortcutContext::MainWindow, command_ids::ID_FILE_MINIMIZE, static_cast<WORD>('W'), FCONTROL, L"Ctrl+W", L"Minimize the main window", L"File"},
        ShortcutDefinition{ShortcutContext::MainWindow, command_ids::ID_FILE_REFRESH_TREE, VK_F5, 0, L"F5", L"Refresh the folder tree", L"File"},
        ShortcutDefinition{ShortcutContext::MainWindow, command_ids::ID_FILE_RENAME_SELECTED, VK_F2, 0, L"F2", L"Rename the selected item", L"File and selection actions"},
        ShortcutDefinition{ShortcutContext::MainWindow, command_ids::ID_FILE_QUICK_SEND_MOVE, VK_F7, 0, L"F7", L"Move selection to a quick action destination", L"Quick Actions"},
        ShortcutDefinition{ShortcutContext::MainWindow, command_ids::ID_FILE_QUICK_SEND_COPY, VK_F8, 0, L"F8", L"Copy selection to a quick action destination", L"Quick Actions"},
        ShortcutDefinition{ShortcutContext::MainWindow, command_ids::ID_FILE_IMAGE_INFORMATION, static_cast<WORD>('I'), FCONTROL, L"Ctrl+I", L"Show image information", L"File and selection actions"},
        ShortcutDefinition{ShortcutContext::MainWindow, command_ids::ID_FILE_COPY_PATH, static_cast<WORD>('C'), FCONTROL | FSHIFT, L"Ctrl+Shift+C", L"Copy selected paths", L"File and selection actions"},
        ShortcutDefinition{ShortcutContext::MainWindow, command_ids::ID_FILE_COPY_FILES_TO_CLIPBOARD, static_cast<WORD>('C'), FCONTROL, L"Ctrl+C", L"Copy selected files", L"File and selection actions"},
        ShortcutDefinition{ShortcutContext::MainWindow, command_ids::ID_FILE_COPY_IMAGE_PIXELS, static_cast<WORD>('I'), FCONTROL | FSHIFT, L"Ctrl+Shift+I", L"Copy displayed image pixels", L"File and selection actions"},
        ShortcutDefinition{ShortcutContext::MainWindow, command_ids::ID_FILE_PASTE_FILES, static_cast<WORD>('V'), FCONTROL, L"Ctrl+V", L"Paste files into the current folder", L"File and selection actions"},
        ShortcutDefinition{ShortcutContext::MainWindow, command_ids::ID_EDIT_CUT, static_cast<WORD>('X'), FCONTROL, L"Ctrl+X", L"Cut selected files", L"File and selection actions"},
        ShortcutDefinition{ShortcutContext::MainWindow, command_ids::ID_FILE_SELECT_ALL, static_cast<WORD>('A'), FCONTROL, L"Ctrl+A", L"Select all items", L"File and selection actions"},
        ShortcutDefinition{ShortcutContext::MainWindow, command_ids::ID_FILE_DUPLICATE_SELECTION, static_cast<WORD>('D'), FCONTROL, L"Ctrl+D", L"Duplicate selected files", L"File and selection actions"},
        ShortcutDefinition{ShortcutContext::MainWindow, command_ids::ID_EDIT_UNDO, static_cast<WORD>('Z'), FCONTROL, L"Ctrl+Z", L"Undo the last file operation", L"File and selection actions"},
        ShortcutDefinition{ShortcutContext::MainWindow, command_ids::ID_EDIT_REDO, static_cast<WORD>('Y'), FCONTROL, L"Ctrl+Y", L"Redo the last file operation", L"File and selection actions"},
        ShortcutDefinition{ShortcutContext::MainWindow, command_ids::ID_FILE_REVEAL_IN_EXPLORER, static_cast<WORD>('E'), FCONTROL, L"Ctrl+E", L"Reveal the selection in Explorer", L"File and selection actions"},
        ShortcutDefinition{ShortcutContext::MainWindow, command_ids::ID_FILE_PROPERTIES, VK_RETURN, FALT, L"Alt+Enter", L"Show file properties", L"File and selection actions"},
        ShortcutDefinition{ShortcutContext::MainWindow, command_ids::ID_FILE_DELETE_SELECTION, VK_DELETE, 0, L"Del", L"Move selected files to the Recycle Bin", L"File and selection actions"},
        ShortcutDefinition{ShortcutContext::MainWindow, command_ids::ID_FILE_DELETE_SELECTION_PERMANENT, VK_DELETE, FSHIFT, L"Shift+Del", L"Delete selected files permanently", L"File and selection actions"},
        ShortcutDefinition{ShortcutContext::MainWindow, command_ids::ID_VIEW_THUMBNAILS, static_cast<WORD>('1'), FCONTROL, L"Ctrl+1", L"Use thumbnail mode", L"View and navigation"},
        ShortcutDefinition{ShortcutContext::MainWindow, command_ids::ID_VIEW_DETAILS, static_cast<WORD>('2'), FCONTROL, L"Ctrl+2", L"Use details mode", L"View and navigation"},
        ShortcutDefinition{ShortcutContext::MainWindow, command_ids::ID_VIEW_DETAILS_STRIP, static_cast<WORD>('3'), FCONTROL, L"Ctrl+3", L"Toggle the details panel", L"View and navigation"},
        ShortcutDefinition{ShortcutContext::MainWindow, command_ids::ID_VIEW_RECURSIVE, static_cast<WORD>('R'), FCONTROL, L"Ctrl+R", L"Toggle recursive browsing", L"View and navigation"},
        ShortcutDefinition{ShortcutContext::MainWindow, command_ids::ID_VIEW_THUMBNAIL_SIZE_INCREASE, VK_OEM_PLUS, 0, L"+ / =", L"Increase thumbnail size", L"View and navigation"},
        ShortcutDefinition{ShortcutContext::MainWindow, command_ids::ID_VIEW_THUMBNAIL_SIZE_INCREASE, VK_OEM_PLUS, FSHIFT, L"+ / =", L"Increase thumbnail size", L"View and navigation"},
        ShortcutDefinition{ShortcutContext::MainWindow, command_ids::ID_VIEW_THUMBNAIL_SIZE_INCREASE, VK_ADD, 0, L"Numpad +", L"Increase thumbnail size", L"View and navigation"},
        ShortcutDefinition{ShortcutContext::MainWindow, command_ids::ID_VIEW_THUMBNAIL_SIZE_DECREASE, VK_OEM_MINUS, 0, L"- / _", L"Decrease thumbnail size", L"View and navigation"},
        ShortcutDefinition{ShortcutContext::MainWindow, command_ids::ID_VIEW_THUMBNAIL_SIZE_DECREASE, VK_OEM_MINUS, FSHIFT, L"- / _", L"Decrease thumbnail size", L"View and navigation"},
        ShortcutDefinition{ShortcutContext::MainWindow, command_ids::ID_VIEW_THUMBNAIL_SIZE_DECREASE, VK_SUBTRACT, 0, L"Numpad -", L"Decrease thumbnail size", L"View and navigation"},
        ShortcutDefinition{ShortcutContext::MainWindow, command_ids::ID_VIEW_SLIDESHOW_SELECTION, static_cast<WORD>('S'), FCONTROL | FSHIFT, L"Ctrl+Shift+S", L"Start a slideshow from the selection", L"View and navigation"},
        ShortcutDefinition{ShortcutContext::MainWindow, command_ids::ID_VIEW_SLIDESHOW_FOLDER, static_cast<WORD>('F'), FCONTROL | FSHIFT, L"Ctrl+Shift+F", L"Start a slideshow from the folder", L"View and navigation"},
        ShortcutDefinition{ShortcutContext::MainWindow, command_ids::ID_VIEW_SETTINGS, static_cast<WORD>('T'), FCONTROL | FSHIFT, L"Ctrl+Shift+T", L"Open Settings", L"Settings"},
        ShortcutDefinition{ShortcutContext::MainWindow, command_ids::ID_HELP_DIAGNOSTICS_SNAPSHOT, static_cast<WORD>('D'), FCONTROL | FSHIFT, L"Ctrl+Shift+D", L"Capture a diagnostics snapshot", L"Help"},
        ShortcutDefinition{ShortcutContext::MainWindow, command_ids::ID_HELP_DIAGNOSTICS_RESET, static_cast<WORD>('X'), FCONTROL | FSHIFT, L"Ctrl+Shift+X", L"Reset diagnostics state", L"Help"},
    };

    inline constexpr std::array kViewerShortcutCatalog{
        ShortcutDefinition{ShortcutContext::Viewer, 0, VK_ESCAPE, 0, L"Esc", L"Leave full screen or close the viewer", L"Viewer delete and exit"},
        ShortcutDefinition{ShortcutContext::Viewer, 0, static_cast<WORD>('W'), FCONTROL, L"Ctrl+W", L"Close the viewer", L"Viewer delete and exit"},
        ShortcutDefinition{ShortcutContext::Viewer, 0, VK_LEFT, 0, L"Left", L"Navigate or pan left", L"Viewer navigation"},
        ShortcutDefinition{ShortcutContext::Viewer, 0, VK_RIGHT, 0, L"Right", L"Navigate or pan right", L"Viewer navigation"},
        ShortcutDefinition{ShortcutContext::Viewer, 0, VK_UP, 0, L"Up", L"Navigate or pan up", L"Viewer navigation"},
        ShortcutDefinition{ShortcutContext::Viewer, 0, VK_DOWN, 0, L"Down", L"Navigate or pan down", L"Viewer navigation"},
        ShortcutDefinition{ShortcutContext::Viewer, 0, VK_LEFT, FSHIFT, L"Shift+Left", L"Navigate the comparison pair left", L"Viewer compare and display"},
        ShortcutDefinition{ShortcutContext::Viewer, 0, VK_RIGHT, FSHIFT, L"Shift+Right", L"Navigate the comparison pair right", L"Viewer compare and display"},
        ShortcutDefinition{ShortcutContext::Viewer, 0, VK_UP, FSHIFT, L"Shift+Up", L"Navigate the comparison pair up", L"Viewer compare and display"},
        ShortcutDefinition{ShortcutContext::Viewer, 0, VK_DOWN, FSHIFT, L"Shift+Down", L"Navigate the comparison pair down", L"Viewer compare and display"},
        ShortcutDefinition{ShortcutContext::Viewer, 0, VK_PRIOR, 0, L"Page Up", L"Navigate to the previous image", L"Viewer navigation"},
        ShortcutDefinition{ShortcutContext::Viewer, 0, VK_NEXT, 0, L"Page Down", L"Navigate to the next image", L"Viewer navigation"},
        ShortcutDefinition{ShortcutContext::Viewer, 0, VK_HOME, FCONTROL, L"Ctrl+Home", L"Go to the first image", L"Viewer navigation"},
        ShortcutDefinition{ShortcutContext::Viewer, 0, VK_END, FCONTROL, L"Ctrl+End", L"Go to the last image", L"Viewer navigation"},
        ShortcutDefinition{ShortcutContext::Viewer, command_ids::ID_VIEW_SLIDESHOW_FOLDER, static_cast<WORD>('F'), FCONTROL | FSHIFT, L"Ctrl+Shift+F", L"Start a slideshow from the current folder", L"Viewer slideshow and Quick Actions"},
        ShortcutDefinition{ShortcutContext::Viewer, command_ids::ID_FILE_QUICK_SEND_MOVE, VK_F7, 0, L"F7", L"Move the displayed image to a Quick Actions destination", L"Viewer slideshow and Quick Actions"},
        ShortcutDefinition{ShortcutContext::Viewer, command_ids::ID_FILE_QUICK_SEND_COPY, VK_F8, 0, L"F8", L"Copy the displayed image to a Quick Actions destination", L"Viewer slideshow and Quick Actions"},
        ShortcutDefinition{ShortcutContext::Viewer, command_ids::ID_FILE_COPY_IMAGE_PIXELS, static_cast<WORD>('I'), FCONTROL | FSHIFT, L"Ctrl+Shift+I", L"Copy the displayed image", L"Viewer compare and display"},
        ShortcutDefinition{ShortcutContext::Viewer, 0, VK_TAB, 0, L"Tab", L"Toggle image information overlays", L"Viewer compare and display"},
        ShortcutDefinition{ShortcutContext::Viewer, 0, VK_ADD, 0, L"Numpad +", L"Zoom in", L"Viewer zoom and display"},
        ShortcutDefinition{ShortcutContext::Viewer, 0, VK_OEM_PLUS, 0, L"+ / =", L"Zoom in", L"Viewer zoom and display"},
        ShortcutDefinition{ShortcutContext::Viewer, 0, VK_SUBTRACT, 0, L"Numpad -", L"Zoom out", L"Viewer zoom and display"},
        ShortcutDefinition{ShortcutContext::Viewer, 0, VK_OEM_MINUS, 0, L"- / _", L"Zoom out", L"Viewer zoom and display"},
        ShortcutDefinition{ShortcutContext::Viewer, 0, VK_RETURN, 0, L"Enter", L"Toggle between fit and actual-size viewing", L"Viewer zoom and display"},
        ShortcutDefinition{ShortcutContext::Viewer, 0, static_cast<WORD>('0'), 0, L"0", L"Fit the image to the window", L"Viewer compare and display"},
        ShortcutDefinition{ShortcutContext::Viewer, 0, static_cast<WORD>('1'), 0, L"1", L"Show the image at actual size", L"Viewer compare and display"},
        ShortcutDefinition{ShortcutContext::Viewer, 0, static_cast<WORD>('H'), 0, L"H", L"Fit the image to the window height", L"Viewer zoom and display"},
        ShortcutDefinition{ShortcutContext::Viewer, 0, static_cast<WORD>('W'), 0, L"W", L"Fit the image to the window width", L"Viewer zoom and display"},
        ShortcutDefinition{ShortcutContext::Viewer, 0, static_cast<WORD>('H'), FCONTROL | FSHIFT, L"Ctrl+Shift+H", L"Size the window to the monitor work-area height", L"Viewer window"},
        ShortcutDefinition{ShortcutContext::Viewer, 0, static_cast<WORD>('W'), FCONTROL | FSHIFT, L"Ctrl+Shift+W", L"Size the window to the monitor work-area width", L"Viewer window"},
        ShortcutDefinition{ShortcutContext::Viewer, 0, static_cast<WORD>('L'), 0, L"L", L"Rotate the image left", L"Viewer transform and compare"},
        ShortcutDefinition{ShortcutContext::Viewer, 0, static_cast<WORD>('R'), 0, L"R", L"Rotate the image right", L"Viewer transform and compare"},
        ShortcutDefinition{ShortcutContext::Viewer, 0, static_cast<WORD>('C'), 0, L"C", L"Toggle comparison mode", L"Viewer transform and compare"},
        ShortcutDefinition{ShortcutContext::Viewer, 0, static_cast<WORD>('X'), 0, L"X", L"Activate the compared image", L"Viewer transform and compare"},
        ShortcutDefinition{ShortcutContext::Viewer, 0, VK_SPACE, 0, L"Space", L"Start or stop the slideshow", L"Viewer slideshow and Quick Actions"},
        ShortcutDefinition{ShortcutContext::Viewer, 0, VK_F11, 0, L"F11", L"Toggle full-screen mode", L"Viewer window"},
        ShortcutDefinition{ShortcutContext::Viewer, 0, VK_RETURN, FCONTROL, L"Ctrl+Enter", L"Toggle full-screen mode", L"Viewer window"},
        ShortcutDefinition{ShortcutContext::Viewer, 0, VK_DELETE, 0, L"Del", L"Move the displayed file to the Recycle Bin", L"Viewer delete and exit"},
        ShortcutDefinition{ShortcutContext::Viewer, 0, VK_DELETE, FSHIFT, L"Shift+Del", L"Delete the displayed file permanently", L"Viewer delete and exit"},
    };

    constexpr bool SameShortcut(const ShortcutDefinition& left, const ShortcutDefinition& right) noexcept
    {
        return left.context == right.context
            && left.virtualKey == right.virtualKey
            && left.modifiers == right.modifiers;
    }

    template <std::size_t Count>
    constexpr bool HasDuplicateShortcuts(const std::array<ShortcutDefinition, Count>& catalog) noexcept
    {
        for (std::size_t index = 0; index < Count; ++index)
        {
            for (std::size_t otherIndex = index + 1; otherIndex < Count; ++otherIndex)
            {
                if (SameShortcut(catalog[index], catalog[otherIndex]))
                {
                    return true;
                }
            }
        }
        return false;
    }

    inline constexpr bool kShortcutCatalogValid = !HasDuplicateShortcuts(kMainWindowShortcutCatalog);

    inline constexpr std::span<const ShortcutDefinition> MainWindowShortcuts() noexcept
    {
        return kMainWindowShortcutCatalog;
    }

    inline constexpr std::span<const ShortcutDefinition> ViewerShortcuts() noexcept
    {
        return kViewerShortcutCatalog;
    }
}