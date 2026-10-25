#include "viewer/ViewerWindowInternal.h"

namespace
{
    constexpr int kContextMenuItemHeight = 28;
    constexpr int kContextMenuSeparatorHeight = 10;
    constexpr int kContextMenuCheckColumnWidth = 24;
    constexpr int kContextMenuTextPadding = 12;
    constexpr int kContextMenuShortcutGap = 24;
    constexpr int kContextMenuMeasurementAllowance = 8;

    int MeasureMenuTextWidth(HFONT font, const std::wstring& text)
    {
        if (!font || text.empty())
        {
            return 0;
        }

        HDC dc = GetDC(nullptr);
        if (!dc)
        {
            return 0;
        }

        const HGDIOBJ oldFont = SelectObject(dc, font);
        SIZE size{};
        GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &size);
        SelectObject(dc, oldFont);
        ReleaseDC(nullptr, dc);
        return size.cx;
    }

    int MeasureMenuTextHeight(HFONT font)
    {
        if (!font)
        {
            return 0;
        }

        HDC dc = GetDC(nullptr);
        if (!dc)
        {
            return 0;
        }

        const HGDIOBJ oldFont = SelectObject(dc, font);
        TEXTMETRICW metrics{};
        GetTextMetricsW(dc, &metrics);
        SelectObject(dc, oldFont);
        ReleaseDC(nullptr, dc);
        return metrics.tmHeight;
    }

    void SplitMenuDisplayText(const std::wstring& text, std::wstring* label, std::wstring* shortcut)
    {
        if (!label || !shortcut)
        {
            return;
        }

        const std::size_t separator = text.find(L'\t');
        if (separator == std::wstring::npos)
        {
            *label = text;
            shortcut->clear();
            return;
        }

        *label = text.substr(0, separator);
        *shortcut = text.substr(separator + 1);
    }

    COLORREF BlendMenuColor(COLORREF baseColor, COLORREF mixColor, BYTE mixAmount)
    {
        const BYTE baseAmount = static_cast<BYTE>(255 - mixAmount);
        return RGB(
            (GetRValue(baseColor) * baseAmount + GetRValue(mixColor) * mixAmount) / 255,
            (GetGValue(baseColor) * baseAmount + GetGValue(mixColor) * mixAmount) / 255,
            (GetBValue(baseColor) * baseAmount + GetBValue(mixColor) * mixAmount) / 255);
    }

    bool CopyTextToClipboardLocal(HWND ownerWindow, const std::wstring& text)
    {
        if (!OpenClipboard(ownerWindow))
        {
            return false;
        }

        if (!EmptyClipboard())
        {
            CloseClipboard();
            return false;
        }

        const std::size_t bytes = (text.size() + 1) * sizeof(wchar_t);
        HGLOBAL buffer = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (!buffer)
        {
            CloseClipboard();
            return false;
        }

        void* locked = GlobalLock(buffer);
        if (!locked)
        {
            GlobalFree(buffer);
            CloseClipboard();
            return false;
        }

        memcpy(locked, text.data(), text.size() * sizeof(wchar_t));
        static_cast<wchar_t*>(locked)[text.size()] = L'\0';
        GlobalUnlock(buffer);

        if (!SetClipboardData(CF_UNICODETEXT, buffer))
        {
            GlobalFree(buffer);
            CloseClipboard();
            return false;
        }

        CloseClipboard();
        return true;
    }
}

namespace hyperbrowse::viewer
{

    void ViewerWindow::ShowContextMenu(POINT screenPoint)
    {
        if (!hwnd_)
        {
            return;
        }

        const std::wstring currentPath = CurrentFilePath();
        if (currentPath.empty())
        {
            return;
        }

        HMENU menu = CreatePopupMenu();
        if (!menu)
        {
            return;
        }

        AppendMenuW(menu, MF_STRING, kContextMenuCopyPath, L"Copy Pat&h\tCtrl+Shift+C");
        AppendMenuW(menu, MF_STRING, kContextMenuCopyImage, L"Copy &Image\tCtrl+Shift+I");
        AppendMenuW(menu, MF_STRING, kContextMenuRevealInExplorer, L"Reveal in &Explorer");
        AppendMenuW(menu, MF_STRING, kContextMenuImageInformation, L"Image &Information\tCtrl+I");
        AppendMenuW(menu, MF_STRING, kContextMenuProperties, L"P&roperties\tAlt+Enter");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu,
                MF_STRING,
                kContextMenuToggleInfoOverlays,
            infoOverlaysVisible_ ? L"Hide &Overlays\tTab" : L"Show &Overlays\tTab");
        AppendMenuW(menu,
            MF_STRING
                | (fullMetadataVisible_ ? MF_CHECKED : MF_UNCHECKED)
                | (infoOverlaysVisible_ ? MF_ENABLED : MF_DISABLED),
                kContextMenuToggleFullMetadata,
                L"Show &Full Metadata");
        if (HasCompareSession())
        {
            AppendMenuW(menu,
                        MF_STRING | (compareViewsSynchronized_ ? MF_CHECKED : MF_UNCHECKED),
                        kContextMenuToggleCompareViewSync,
                        L"Synchronize Compare &Zoom and Pan");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            HMENU ratingMenu = CreatePopupMenu();
            if (ratingMenu)
            {
                const int rating = compareSessionActive_ && focusedCompareTile_ < compareTiles_.size()
                    ? compareTiles_[focusedCompareTile_].metadata.rating
                    : 0;
                for (int value = 0; value <= 5; ++value)
                {
                    std::wstring label = value == 0
                        ? L"0 - Clear Rating"
                        : std::to_wstring(value) + L" Star" + (value == 1 ? L"" : L"s");
                    AppendMenuW(ratingMenu,
                                MF_STRING | (rating == value ? MF_CHECKED : MF_UNCHECKED),
                                kContextMenuSetCompareRatingBase + static_cast<UINT>(value),
                                label.c_str());
                }
                AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(ratingMenu), L"Set Image &Rating");
            }
            AppendMenuW(menu, MF_STRING, kContextMenuEditCompareTags, L"Edit Image &Tags...");
        }
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kContextMenuSetWallpaper, L"Set as Desktop &Wallpaper");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kContextMenuDelete, L"&Delete\tDel");
        AppendMenuW(menu, MF_STRING, kContextMenuDeletePermanently, L"Delete &Permanently\tShift+Del");

        std::vector<std::unique_ptr<MenuDrawItemData>> menuDrawItems;
        PrepareContextMenuForOwnerDraw(menu, menuDrawItems);

        HBRUSH menuBackgroundBrush = CreateSolidBrush(PanelFillColor(darkTheme_));
        if (menuBackgroundBrush)
        {
            MENUINFO menuInfo{};
            menuInfo.cbSize = sizeof(menuInfo);
            menuInfo.fMask = MIM_BACKGROUND;
            menuInfo.hbrBack = menuBackgroundBrush;
            SetMenuInfo(menu, &menuInfo);
        }

        SetForegroundWindow(hwnd_);
        const UINT commandId = TrackPopupMenuEx(
            menu,
            TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD,
            screenPoint.x,
            screenPoint.y,
            hwnd_,
            nullptr);
        PostMessageW(hwnd_, WM_NULL, 0, 0);
        DestroyMenu(menu);
        if (menuBackgroundBrush)
        {
            DeleteObject(menuBackgroundBrush);
        }

        if (commandId == 0)
        {
            return;
        }

        if (commandId == kContextMenuDelete || commandId == kContextMenuDeletePermanently)
        {
            // Route deletes through the owner's delete pipeline (confirmation,
            // recycle-bin handling, focus restoration) exactly like VK_DELETE.
            if (owner_ && IsWindow(owner_))
            {
                const WPARAM flags = commandId == kContextMenuDeletePermanently ? kDeleteRequestPermanent : 0;
                SendMessageW(owner_, kDeleteRequestedMessage, flags, reinterpret_cast<LPARAM>(hwnd_));
            }
            return;
        }

        DispatchContextMenuCommand(commandId);
    }

    void ViewerWindow::MeasureContextMenuItem(MEASUREITEMSTRUCT* measureItem) const
    {
        if (!measureItem)
        {
            return;
        }

        const auto* drawData = reinterpret_cast<const MenuDrawItemData*>(measureItem->itemData);
        const auto scaleMenuDimension = [this](int dimension)
        {
            return util::ScaleAppTextDimension(dimension, appTextSize_);
        };
        if (!drawData)
        {
            measureItem->itemWidth = 0;
            measureItem->itemHeight = static_cast<UINT>(scaleMenuDimension(kContextMenuItemHeight));
            return;
        }

        if (drawData->separator)
        {
            measureItem->itemWidth = 0;
            measureItem->itemHeight = static_cast<UINT>(scaleMenuDimension(kContextMenuSeparatorHeight));
            return;
        }

        std::wstring label;
        std::wstring shortcut;
        SplitMenuDisplayText(drawData->text, &label, &shortcut);
        const HFONT menuFont = MenuFontOrDefault(menuFont_);
        const int textPadding = scaleMenuDimension(kContextMenuTextPadding);
        const int checkColumnWidth = scaleMenuDimension(kContextMenuCheckColumnWidth);
        const int shortcutGap = scaleMenuDimension(kContextMenuShortcutGap);
        const int measurementAllowance = scaleMenuDimension(kContextMenuMeasurementAllowance);
        const int labelWidth = MeasureMenuTextWidth(menuFont, label);
        const int shortcutWidth = shortcut.empty() ? 0 : MeasureMenuTextWidth(menuFont, shortcut);
        int itemWidth = checkColumnWidth + (textPadding * 2) + labelWidth + measurementAllowance;
        if (shortcutWidth > 0)
        {
            itemWidth += shortcutGap + shortcutWidth;
        }

        const int itemHeight = scaleMenuDimension(kContextMenuItemHeight);
        measureItem->itemWidth = static_cast<UINT>(itemWidth);
        measureItem->itemHeight = static_cast<UINT>(std::max(
            itemHeight,
            MeasureMenuTextHeight(menuFont) + measurementAllowance));
    }

    void ViewerWindow::DrawContextMenuItem(const DRAWITEMSTRUCT& drawItem) const
    {
        const auto* drawData = reinterpret_cast<const MenuDrawItemData*>(drawItem.itemData);
        if (!drawData)
        {
            return;
        }

        const auto scaleMenuDimension = [this](int dimension)
        {
            return util::ScaleAppTextDimension(dimension, appTextSize_);
        };
        const COLORREF menuBackground = PanelFillColor(darkTheme_);
        const hyperbrowse::ui::ThemeVariant themeVariant = hyperbrowse::ui::ResolveThemeVariant(darkTheme_);
        const COLORREF selectedBackground = hyperbrowse::ui::MakeMenuSelectionFill(themeVariant);
        const bool selected = (drawItem.itemState & ODS_SELECTED) != 0;
        const bool disabled = (drawItem.itemState & ODS_DISABLED) != 0;
        const COLORREF backgroundColor = selected ? selectedBackground : menuBackground;
        // The system highlight needs its paired text color to stay readable in high contrast.
        const bool highlightText = selected && themeVariant == hyperbrowse::ui::ThemeVariant::HighContrast;
        const COLORREF menuTextColor = highlightText ? GetSysColor(COLOR_HIGHLIGHTTEXT) : TextColor(darkTheme_);
        const COLORREF menuMutedTextColor = highlightText ? GetSysColor(COLOR_HIGHLIGHTTEXT) : MutedTextColor(darkTheme_);
        const HBRUSH backgroundBrush = CreateSolidBrush(backgroundColor);
        FillRect(drawItem.hDC, &drawItem.rcItem, backgroundBrush);
        DeleteObject(backgroundBrush);

        if (drawData->separator)
        {
            const HPEN separatorPen = CreatePen(PS_SOLID, 1, PanelBorderColor(darkTheme_));
            const HGDIOBJ oldPen = SelectObject(drawItem.hDC, separatorPen);
            const int y = drawItem.rcItem.top + ((drawItem.rcItem.bottom - drawItem.rcItem.top) / 2);
            MoveToEx(drawItem.hDC,
                     drawItem.rcItem.left + scaleMenuDimension(kContextMenuCheckColumnWidth),
                     y,
                     nullptr);
            LineTo(drawItem.hDC, drawItem.rcItem.right - scaleMenuDimension(kContextMenuTextPadding), y);
            SelectObject(drawItem.hDC, oldPen);
            DeleteObject(separatorPen);
            return;
        }

        std::wstring label;
        std::wstring shortcut;
        SplitMenuDisplayText(drawData->text, &label, &shortcut);
        const HFONT menuFont = MenuFontOrDefault(menuFont_);
        const int textPadding = scaleMenuDimension(kContextMenuTextPadding);
        const int checkColumnWidth = scaleMenuDimension(kContextMenuCheckColumnWidth);
        const int shortcutGap = scaleMenuDimension(kContextMenuShortcutGap);
        RECT labelRect{drawItem.rcItem.left + checkColumnWidth + textPadding,
                       drawItem.rcItem.top,
                       drawItem.rcItem.right - textPadding,
                       drawItem.rcItem.bottom};
        if (!shortcut.empty())
        {
            labelRect.right -= MeasureMenuTextWidth(menuFont, shortcut) + shortcutGap;
        }

        render::DrawGdiText(drawItem.hDC,
                            menuFont,
                            label.c_str(),
                            -1,
                            labelRect,
                            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS,
                            disabled
                                ? BlendMenuColor(menuMutedTextColor, backgroundColor, 128)
                                : menuTextColor,
                            backgroundColor);

        if ((drawItem.itemState & ODS_CHECKED) != 0)
        {
            const int checkMarkWidth = scaleMenuDimension(10);
            const int checkMarkHeight = scaleMenuDimension(8);
            const int checkMarkLeft = drawItem.rcItem.left + (checkColumnWidth - checkMarkWidth) / 2;
            const int checkMarkTop = drawItem.rcItem.top
                + ((drawItem.rcItem.bottom - drawItem.rcItem.top) - checkMarkHeight) / 2;
            const COLORREF checkColor = disabled
                ? BlendMenuColor(menuMutedTextColor, backgroundColor, 128)
                : menuTextColor;
            const HPEN checkPen = CreatePen(PS_SOLID, std::max(1, scaleMenuDimension(2)), checkColor);
            const HGDIOBJ oldPen = SelectObject(drawItem.hDC, checkPen);
            MoveToEx(drawItem.hDC, checkMarkLeft, checkMarkTop + checkMarkHeight / 2, nullptr);
            LineTo(drawItem.hDC, checkMarkLeft + checkMarkWidth / 3, checkMarkTop + checkMarkHeight - 1);
            LineTo(drawItem.hDC, checkMarkLeft + checkMarkWidth, checkMarkTop);
            SelectObject(drawItem.hDC, oldPen);
            DeleteObject(checkPen);
        }

        if (!shortcut.empty())
        {
            RECT shortcutRect{labelRect.right + shortcutGap,
                              drawItem.rcItem.top,
                              drawItem.rcItem.right - textPadding,
                              drawItem.rcItem.bottom};
            render::DrawGdiText(drawItem.hDC,
                                menuFont,
                                shortcut.c_str(),
                                -1,
                                shortcutRect,
                                DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS,
                                disabled
                                    ? BlendMenuColor(menuMutedTextColor, backgroundColor, 128)
                                    : menuMutedTextColor,
                                backgroundColor);
        }
    }

    void ViewerWindow::PrepareContextMenuForOwnerDraw(
        HMENU menu,
        std::vector<std::unique_ptr<MenuDrawItemData>>& storage) const
    {
        if (!menu)
        {
            return;
        }

        const int itemCount = GetMenuItemCount(menu);
        for (int itemIndex = 0; itemIndex < itemCount; ++itemIndex)
        {
            MENUITEMINFOW menuInfo{};
            menuInfo.cbSize = sizeof(menuInfo);
            menuInfo.fMask = MIIM_FTYPE;
            if (!GetMenuItemInfoW(menu, static_cast<UINT>(itemIndex), TRUE, &menuInfo))
            {
                continue;
            }

            auto drawData = std::make_unique<MenuDrawItemData>();
            drawData->separator = (menuInfo.fType & MFT_SEPARATOR) != 0;
            if (!drawData->separator)
            {
                const int textLength = GetMenuStringW(menu, static_cast<UINT>(itemIndex), nullptr, 0, MF_BYPOSITION);
                if (textLength > 0)
                {
                    std::wstring buffer(static_cast<std::size_t>(textLength) + 1, L'\0');
                    GetMenuStringW(menu,
                                   static_cast<UINT>(itemIndex),
                                   buffer.data(),
                                   textLength + 1,
                                   MF_BYPOSITION);
                    buffer.resize(static_cast<std::size_t>(textLength));
                    drawData->text = std::move(buffer);
                }
            }

            MENUITEMINFOW updateInfo{};
            updateInfo.cbSize = sizeof(updateInfo);
            updateInfo.fMask = MIIM_FTYPE | MIIM_DATA;
            updateInfo.fType = drawData->separator ? (MFT_SEPARATOR | MFT_OWNERDRAW) : MFT_OWNERDRAW;
            updateInfo.dwItemData = reinterpret_cast<ULONG_PTR>(drawData.get());
            SetMenuItemInfoW(menu, static_cast<UINT>(itemIndex), TRUE, &updateInfo);
            storage.push_back(std::move(drawData));
        }
    }

    void ViewerWindow::DispatchContextMenuCommand(UINT commandId)
    {
        const std::wstring currentPath = CurrentFilePath();
        if (currentPath.empty())
        {
            return;
        }

        switch (commandId)
        {
        case kContextMenuToggleInfoOverlays:
            ToggleInfoOverlays();
            return;
        case kContextMenuToggleFullMetadata:
            SetFullMetadataVisible(!fullMetadataVisible_);
            return;
        case kContextMenuToggleCompareViewSync:
            SetCompareViewsSynchronized(!compareViewsSynchronized_);
            return;
        case kContextMenuCopyPath:
            if (!CopyTextToClipboardLocal(hwnd_, currentPath))
            {
                MessageBoxW(hwnd_, L"Failed to copy the file path to the clipboard.", L"Copy Path", MB_OK | MB_ICONERROR);
            }
            return;
        case kContextMenuRevealInExplorer:
        {
            PIDLIST_ABSOLUTE folderPidl = nullptr;
            const std::wstring parentPath = std::filesystem::path(currentPath).parent_path().wstring();
            if (SUCCEEDED(SHParseDisplayName(parentPath.c_str(), nullptr, &folderPidl, 0, nullptr)) && folderPidl)
            {
                PIDLIST_ABSOLUTE itemPidl = nullptr;
                LPCITEMIDLIST relativePidl = nullptr;
                if (SUCCEEDED(SHParseDisplayName(currentPath.c_str(), nullptr, &itemPidl, 0, nullptr)) && itemPidl)
                {
                    relativePidl = ILFindLastID(itemPidl);
                    if (relativePidl)
                    {
                        SHOpenFolderAndSelectItems(folderPidl, 1, &relativePidl, 0);
                    }
                    ILFree(itemPidl);
                }
                ILFree(folderPidl);
            }
            return;
        }
        case kContextMenuCopyImage:
        case kContextMenuImageInformation:
        case kContextMenuProperties:
        case kContextMenuSetWallpaper:
        case kContextMenuRefreshCompareTileMetadata:
        case kContextMenuEditCompareTags:
            if (owner_ && IsWindow(owner_))
            {
                // Defer to the main window so metadata dialogs and the wallpaper
                // operation reuse the same code paths as the browser.
                PostMessageW(owner_, kContextMenuCommandMessage,
                             static_cast<WPARAM>(commandId),
                             reinterpret_cast<LPARAM>(hwnd_));
            }
            return;
        default:
            if (commandId >= kContextMenuSetCompareRatingBase
                && commandId < kContextMenuSetCompareRatingBase + 6
                && owner_ && IsWindow(owner_))
            {
                PostMessageW(owner_, kContextMenuCommandMessage,
                             static_cast<WPARAM>(commandId),
                             reinterpret_cast<LPARAM>(hwnd_));
            }
            return;
        }
    }
}
