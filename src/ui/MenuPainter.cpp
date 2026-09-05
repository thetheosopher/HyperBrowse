#include "ui/MenuPainter.h"

#include <d2d1.h>
#include <dwrite.h>

#include <algorithm>
#include <string>
#include <string_view>

#include "render/D2DRenderer.h"
#include "render/GdiText.h"

namespace hyperbrowse::ui
{
    namespace
    {
        constexpr int kMenuPopupItemHeight = 28;
        constexpr int kMenuPopupSeparatorHeight = 10;
        constexpr int kMenuPopupCheckColumnWidth = 24;
        constexpr int kMenuPopupTextPadding = 12;
        constexpr int kMenuPopupShortcutGap = 24;
        constexpr int kMenuPopupMeasurementAllowance = 8;
        constexpr int kMenuPopupArrowWidth = 12;

        COLORREF BlendColor(COLORREF baseColor, COLORREF mixColor, BYTE mixAmount)
        {
            const BYTE baseAmount = static_cast<BYTE>(255 - mixAmount);
            return RGB(
                (GetRValue(baseColor) * baseAmount + GetRValue(mixColor) * mixAmount) / 255,
                (GetGValue(baseColor) * baseAmount + GetGValue(mixColor) * mixAmount) / 255,
                (GetBValue(baseColor) * baseAmount + GetBValue(mixColor) * mixAmount) / 255);
        }

        std::wstring NormalizeMenuDisplayText(std::wstring_view text)
        {
            std::wstring normalized;
            normalized.reserve(text.size());
            for (std::size_t index = 0; index < text.size(); ++index)
            {
                const wchar_t character = text[index];
                if (character == L'&')
                {
                    if (index + 1 < text.size() && text[index + 1] == L'&')
                    {
                        normalized.push_back(L'&');
                        ++index;
                    }
                    continue;
                }

                normalized.push_back(character);
            }

            return normalized;
        }

        void SplitMenuDisplayText(std::wstring_view text, std::wstring* label, std::wstring* shortcut)
        {
            if (!label || !shortcut)
            {
                return;
            }

            const std::size_t tabIndex = text.find(L'\t');
            const std::wstring_view labelView = tabIndex == std::wstring_view::npos ? text : text.substr(0, tabIndex);
            const std::wstring_view shortcutView = tabIndex == std::wstring_view::npos ? std::wstring_view{} : text.substr(tabIndex + 1);
            *label = NormalizeMenuDisplayText(labelView);
            *shortcut = NormalizeMenuDisplayText(shortcutView);
        }

        int FindMenuMnemonicDisplayIndex(std::wstring_view text)
        {
            int displayIndex = 0;
            for (std::size_t index = 0; index < text.size() && text[index] != L'\t'; ++index)
            {
                if (text[index] != L'&')
                {
                    ++displayIndex;
                    continue;
                }

                if (index + 1 >= text.size())
                {
                    break;
                }

                if (text[index + 1] == L'&')
                {
                    ++index;
                    ++displayIndex;
                    continue;
                }

                return displayIndex;
            }

            return -1;
        }

        int MeasureTextBlockHeight(HFONT font, int minimumHeight)
        {
            HDC screenDc = GetDC(nullptr);
            if (!screenDc)
            {
                return minimumHeight;
            }

            const HGDIOBJ oldFont = font ? SelectObject(screenDc, font) : nullptr;
            RECT bounds{0, 0, 4096, 0};
            DrawTextW(screenDc, L"Ag", -1, &bounds, DT_LEFT | DT_TOP | DT_NOPREFIX | DT_SINGLELINE | DT_CALCRECT);
            if (oldFont)
            {
                SelectObject(screenDc, oldFont);
            }
            ReleaseDC(nullptr, screenDc);
            return (std::max)(minimumHeight, static_cast<int>(bounds.bottom - bounds.top));
        }

        int MeasureTextWidth(HFONT font, std::wstring_view text)
        {
            if (text.empty())
            {
                return 0;
            }

            const std::wstring localText(text);
            HDC screenDc = GetDC(nullptr);
            if (!screenDc)
            {
                return 0;
            }

            const HGDIOBJ oldFont = font ? SelectObject(screenDc, font) : nullptr;
            SIZE size{};
            GetTextExtentPoint32W(screenDc, localText.c_str(), static_cast<int>(localText.size()), &size);
            if (oldFont)
            {
                SelectObject(screenDc, oldFont);
            }
            ReleaseDC(nullptr, screenDc);
            return static_cast<int>(size.cx);
        }

        bool DrawOwnerDrawMenuItemD2D(
            const DRAWITEMSTRUCT& drawItem,
            const MenuPainterPalette& palette,
            hyperbrowse::util::AppTextSize appTextSize,
            HFONT menuFont,
            bool darkTheme)
        {
            const auto* drawData = reinterpret_cast<const MenuDrawItemData*>(drawItem.itemData);
            auto& renderer = hyperbrowse::render::D2DRenderer::Instance();
            if (!drawData || !renderer.IsAvailable() || !drawItem.hDC)
            {
                return false;
            }

            const RECT& itemRect = drawItem.rcItem;
            const int width = itemRect.right - itemRect.left;
            const int height = itemRect.bottom - itemRect.top;
            if (width <= 0 || height <= 0)
            {
                return false;
            }

            const auto renderTarget = renderer.CreateDCRenderTarget();
            if (!renderTarget || FAILED(renderTarget->BindDC(drawItem.hDC, &itemRect)))
            {
                return false;
            }

            const bool selected = (drawItem.itemState & ODS_SELECTED) != 0;
            const bool disabled = (drawItem.itemState & ODS_DISABLED) != 0;
            const bool checked = (drawItem.itemState & ODS_CHECKED) != 0;
            const COLORREF backgroundColor = selected
                ? BlendColor(palette.accentFill, palette.actionStripBackground, darkTheme ? 28 : 12)
                : BlendColor(palette.paneBackground, palette.windowBackground, darkTheme ? 26 : 12);
            const auto createBrush = [renderTarget](COLORREF color)
            {
                hyperbrowse::render::ComPtr<ID2D1SolidColorBrush> brush;
                renderTarget->CreateSolidColorBrush(
                    hyperbrowse::render::ToD2DColor(color),
                    brush.GetAddressOf());
                return brush;
            };
            const auto backgroundBrush = createBrush(backgroundColor);
            const auto borderBrush = createBrush(palette.actionStripBorder);
            if (!backgroundBrush || !borderBrush)
            {
                return false;
            }

            renderTarget->BeginDraw();
            renderTarget->Clear(hyperbrowse::render::ToD2DColor(backgroundColor));
            renderTarget->FillRectangle(
                D2D1::RectF(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)),
                backgroundBrush.Get());

            const auto scaleMenuDimension = [appTextSize](int dimension)
            {
                return hyperbrowse::util::ScaleAppTextDimension(dimension, appTextSize);
            };
            const int checkColumnWidth = scaleMenuDimension(kMenuPopupCheckColumnWidth);
            const int textPadding = scaleMenuDimension(kMenuPopupTextPadding);
            const int shortcutGap = scaleMenuDimension(kMenuPopupShortcutGap);

            if (drawData->separator)
            {
                renderTarget->DrawLine(
                    hyperbrowse::render::ToD2DPoint(static_cast<float>(checkColumnWidth), static_cast<float>(height / 2)),
                    hyperbrowse::render::ToD2DPoint(static_cast<float>((std::max)(checkColumnWidth, width - textPadding)), static_cast<float>(height / 2)),
                    borderBrush.Get());
                const HRESULT drawResult = renderTarget->EndDraw();
                return SUCCEEDED(drawResult);
            }

            std::wstring label;
            std::wstring shortcut;
            SplitMenuDisplayText(drawData->text, &label, &shortcut);
            const int mnemonicIndex = drawData->mnemonicDisplayIndex;

            if (checked)
            {
                const int checkInset = scaleMenuDimension(4);
                const RECT checkRect{checkInset,
                                     checkInset,
                                     (std::max)(checkInset, checkColumnWidth - checkInset),
                                     (std::max)(checkInset, height - checkInset)};
                const COLORREF checkFill = selected ? palette.accent : BlendColor(palette.accentFill, backgroundColor, 24);
                const auto checkBrush = createBrush(checkFill);
                const auto markBrush = createBrush(palette.accentText);
                if (checkBrush && markBrush)
                {
                    const D2D1_ROUNDED_RECT roundedCheck = hyperbrowse::render::ToD2DRoundedRect(
                        checkRect,
                        static_cast<float>(scaleMenuDimension(8)),
                        static_cast<float>(scaleMenuDimension(8)));
                    renderTarget->FillRoundedRectangle(&roundedCheck, checkBrush.Get());
                    const int checkMarkInset = scaleMenuDimension(5);
                    renderTarget->DrawLine(
                        hyperbrowse::render::ToD2DPoint(static_cast<float>(checkRect.left + checkMarkInset), static_cast<float>(height / 2)),
                        hyperbrowse::render::ToD2DPoint(static_cast<float>(checkRect.left + scaleMenuDimension(9)), static_cast<float>(checkRect.bottom - scaleMenuDimension(6))),
                        markBrush.Get(),
                        2.0f);
                    renderTarget->DrawLine(
                        hyperbrowse::render::ToD2DPoint(static_cast<float>(checkRect.left + scaleMenuDimension(9)), static_cast<float>(checkRect.bottom - scaleMenuDimension(6))),
                        hyperbrowse::render::ToD2DPoint(static_cast<float>(checkRect.right - checkMarkInset), static_cast<float>(checkRect.top + scaleMenuDimension(6))),
                        markBrush.Get(),
                        2.0f);
                }
            }

            const COLORREF labelColor = disabled
                ? BlendColor(palette.mutedText, backgroundColor, 128)
                : palette.text;
            const COLORREF shortcutColor = disabled
                ? BlendColor(palette.mutedText, backgroundColor, 128)
                : palette.mutedText;
            const auto labelBrush = createBrush(labelColor);
            const auto shortcutBrush = createBrush(shortcutColor);
            auto menuFormat = renderer.CreateTextFormatFromFont(menuFont ? menuFont : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)));
            if (!labelBrush || !shortcutBrush || !menuFormat)
            {
                renderTarget->EndDraw();
                return false;
            }
            menuFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            menuFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

            RECT labelRect{checkColumnWidth + textPadding,
                           0,
                           width - textPadding,
                           height};
            if (!shortcut.empty())
            {
                labelRect.right -= static_cast<int>(renderer.MeasureTextWidth(shortcut, menuFormat.Get()) + 0.5f) + shortcutGap;
            }
            const bool hasMnemonic = mnemonicIndex >= 0 && mnemonicIndex < static_cast<int>(label.size());
            if (hasMnemonic)
            {
                const auto labelLayout = renderer.CreateTextLayout(
                    label,
                    menuFormat.Get(),
                    static_cast<float>(std::max<LONG>(1, labelRect.right - labelRect.left)),
                    static_cast<float>(std::max<LONG>(1, labelRect.bottom - labelRect.top)));
                if (!labelLayout)
                {
                    renderTarget->EndDraw();
                    return false;
                }

                labelLayout->SetUnderline(TRUE, DWRITE_TEXT_RANGE{
                    static_cast<UINT32>(mnemonicIndex),
                    1});
                renderTarget->DrawTextLayout(
                    hyperbrowse::render::ToD2DPoint(static_cast<float>(labelRect.left), static_cast<float>(labelRect.top)),
                    labelLayout.Get(),
                    labelBrush.Get());
            }
            else
            {
                renderTarget->DrawText(
                    label.c_str(),
                    static_cast<UINT32>(label.size()),
                    menuFormat.Get(),
                    hyperbrowse::render::ToD2DRect(labelRect),
                    labelBrush.Get());
            }

            if (!shortcut.empty())
            {
                menuFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
                const RECT shortcutRect{labelRect.right + shortcutGap,
                                        0,
                                        width - textPadding,
                                        height};
                renderTarget->DrawText(
                    shortcut.c_str(),
                    static_cast<UINT32>(shortcut.size()),
                    menuFormat.Get(),
                    hyperbrowse::render::ToD2DRect(shortcutRect),
                    shortcutBrush.Get());
            }

            const HRESULT drawResult = renderTarget->EndDraw();
            return SUCCEEDED(drawResult);
        }
    }

    void MenuPainter::PrepareMenuForOwnerDraw(
        HMENU menu,
        std::vector<std::unique_ptr<MenuDrawItemData>>& storage,
        bool ownerDrawCurrentLevel) const
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
            menuInfo.fMask = MIIM_FTYPE | MIIM_SUBMENU | MIIM_DATA;
            if (!GetMenuItemInfoW(menu, static_cast<UINT>(itemIndex), TRUE, &menuInfo))
            {
                continue;
            }

            const bool separator = (menuInfo.fType & MFT_SEPARATOR) != 0;
            const bool hasSubmenu = menuInfo.hSubMenu != nullptr;
            std::wstring text;
            if (!separator)
            {
                const auto* existingData = reinterpret_cast<const MenuDrawItemData*>(menuInfo.dwItemData);
                if ((menuInfo.fType & MFT_OWNERDRAW) != 0 && existingData)
                {
                    text = existingData->text;
                }
                else
                {
                    const int textLength = GetMenuStringW(menu, static_cast<UINT>(itemIndex), nullptr, 0, MF_BYPOSITION);
                    if (textLength > 0)
                    {
                        std::wstring buffer(static_cast<std::size_t>(textLength) + 1, L'\0');
                        GetMenuStringW(
                            menu,
                            static_cast<UINT>(itemIndex),
                            buffer.data(),
                            textLength + 1,
                            MF_BYPOSITION);
                        buffer.resize(static_cast<std::size_t>(textLength));
                        text = std::move(buffer);
                    }
                }
            }

            if (ownerDrawCurrentLevel)
            {
                auto drawData = std::make_unique<MenuDrawItemData>();
                drawData->text = std::move(text);
                drawData->mnemonic = FindMenuMnemonic(drawData->text);
                drawData->mnemonicDisplayIndex = FindMenuMnemonicDisplayIndex(drawData->text);
                drawData->separator = separator;
                drawData->hasSubmenu = hasSubmenu;

                MENUITEMINFOW updateInfo{};
                updateInfo.cbSize = sizeof(updateInfo);
                updateInfo.fMask = MIIM_FTYPE | MIIM_DATA;
                updateInfo.fType = separator ? (MFT_SEPARATOR | MFT_OWNERDRAW) : MFT_OWNERDRAW;
                updateInfo.dwItemData = reinterpret_cast<ULONG_PTR>(drawData.get());
                SetMenuItemInfoW(menu, static_cast<UINT>(itemIndex), TRUE, &updateInfo);
                storage.push_back(std::move(drawData));
            }

            if (hasSubmenu)
            {
                PrepareMenuForOwnerDraw(menuInfo.hSubMenu, storage, true);
            }
        }
    }

    void MenuPainter::MeasureOwnerDrawMenuItem(
        MEASUREITEMSTRUCT* measureItem,
        hyperbrowse::util::AppTextSize appTextSize,
        HFONT menuFont) const
    {
        if (!measureItem)
        {
            return;
        }

        const auto* drawData = reinterpret_cast<const MenuDrawItemData*>(measureItem->itemData);
        if (!drawData)
        {
            measureItem->itemWidth = 0;
            measureItem->itemHeight = kMenuPopupItemHeight;
            return;
        }

        if (drawData->separator)
        {
            measureItem->itemWidth = 0;
            measureItem->itemHeight = kMenuPopupSeparatorHeight;
            return;
        }

        std::wstring label;
        std::wstring shortcut;
        SplitMenuDisplayText(drawData->text, &label, &shortcut);

        const auto scaleMenuDimension = [appTextSize](int dimension)
        {
            return hyperbrowse::util::ScaleAppTextDimension(dimension, appTextSize);
        };
        const int itemHeight = scaleMenuDimension(kMenuPopupItemHeight);
        const int checkColumnWidth = scaleMenuDimension(kMenuPopupCheckColumnWidth);
        const int textPadding = scaleMenuDimension(kMenuPopupTextPadding);
        const int shortcutGap = scaleMenuDimension(kMenuPopupShortcutGap);
        const int measurementAllowance = scaleMenuDimension(kMenuPopupMeasurementAllowance);
        const int arrowWidth = scaleMenuDimension(kMenuPopupArrowWidth);
        const HFONT effectiveMenuFont = menuFont ? menuFont : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        const auto d2dMenuFormat = hyperbrowse::render::D2DRenderer::Instance().CreateTextFormatFromFont(effectiveMenuFont);
        const int labelWidth = d2dMenuFormat
            ? static_cast<int>(hyperbrowse::render::D2DRenderer::Instance().MeasureTextWidth(label, d2dMenuFormat.Get()) + 0.5f)
            : MeasureTextWidth(effectiveMenuFont, label);
        const int shortcutWidth = shortcut.empty()
            ? 0
            : (d2dMenuFormat
                ? static_cast<int>(hyperbrowse::render::D2DRenderer::Instance().MeasureTextWidth(shortcut, d2dMenuFormat.Get()) + 0.5f)
                : MeasureTextWidth(effectiveMenuFont, shortcut));
        int itemWidth = checkColumnWidth + (textPadding * 2) + labelWidth;
        if (shortcutWidth > 0)
        {
            itemWidth += shortcutGap + shortcutWidth;
        }
        itemWidth += measurementAllowance;
        if (drawData->hasSubmenu)
        {
            itemWidth += arrowWidth;
        }

        measureItem->itemWidth = static_cast<UINT>(itemWidth);
        measureItem->itemHeight = static_cast<UINT>((std::max)(
            itemHeight,
            MeasureTextBlockHeight(effectiveMenuFont, itemHeight - measurementAllowance)
            + measurementAllowance));
    }

    void MenuPainter::DrawOwnerDrawMenuItem(
        const DRAWITEMSTRUCT& drawItem,
        const MenuPainterPalette& palette,
        hyperbrowse::util::AppTextSize appTextSize,
        HFONT menuFont,
        bool darkTheme) const
    {
        const auto* drawData = reinterpret_cast<const MenuDrawItemData*>(drawItem.itemData);
        if (!drawData)
        {
            return;
        }

        if (DrawOwnerDrawMenuItemD2D(drawItem, palette, appTextSize, menuFont, darkTheme))
        {
            return;
        }

        RECT itemRect = drawItem.rcItem;
        const bool selected = (drawItem.itemState & ODS_SELECTED) != 0;
        const bool disabled = (drawItem.itemState & ODS_DISABLED) != 0;
        const bool checked = (drawItem.itemState & ODS_CHECKED) != 0;
        const COLORREF backgroundColor = selected
            ? BlendColor(palette.accentFill, palette.actionStripBackground, darkTheme ? 28 : 12)
            : BlendColor(palette.paneBackground, palette.windowBackground, darkTheme ? 26 : 12);

        const HBRUSH backgroundBrush = CreateSolidBrush(backgroundColor);
        FillRect(drawItem.hDC, &itemRect, backgroundBrush);
        DeleteObject(backgroundBrush);

        if (drawData->separator)
        {
            const HPEN separatorPen = CreatePen(PS_SOLID, 1, palette.actionStripBorder);
            const HGDIOBJ oldPen = SelectObject(drawItem.hDC, separatorPen);
            const int y = itemRect.top + ((itemRect.bottom - itemRect.top) / 2);
            MoveToEx(drawItem.hDC, itemRect.left + kMenuPopupCheckColumnWidth, y, nullptr);
            LineTo(drawItem.hDC, itemRect.right - kMenuPopupTextPadding, y);
            SelectObject(drawItem.hDC, oldPen);
            DeleteObject(separatorPen);
            return;
        }

        std::wstring label;
        std::wstring shortcut;
        SplitMenuDisplayText(drawData->text, &label, &shortcut);
        const int mnemonicIndex = drawData->mnemonicDisplayIndex;

        const auto scaleMenuDimension = [appTextSize](int dimension)
        {
            return hyperbrowse::util::ScaleAppTextDimension(dimension, appTextSize);
        };
        const int checkColumnWidth = scaleMenuDimension(kMenuPopupCheckColumnWidth);
        const int textPadding = scaleMenuDimension(kMenuPopupTextPadding);
        const int shortcutGap = scaleMenuDimension(kMenuPopupShortcutGap);

        if (checked)
        {
            const int checkInset = scaleMenuDimension(4);
            RECT checkRect{itemRect.left + checkInset,
                           itemRect.top + checkInset,
                           itemRect.left + checkColumnWidth - checkInset,
                           itemRect.bottom - checkInset};
            const COLORREF checkFill = selected ? palette.accent : BlendColor(palette.accentFill, backgroundColor, 24);
            const HBRUSH checkBrush = CreateSolidBrush(checkFill);
            const HPEN checkPen = CreatePen(PS_SOLID, 1, selected ? palette.accent : palette.accentFill);
            const HGDIOBJ oldBrush = SelectObject(drawItem.hDC, checkBrush);
            const HGDIOBJ oldCheckPen = SelectObject(drawItem.hDC, checkPen);
            const int checkCorner = scaleMenuDimension(8);
            RoundRect(drawItem.hDC, checkRect.left, checkRect.top, checkRect.right, checkRect.bottom, checkCorner, checkCorner);
            SelectObject(drawItem.hDC, oldCheckPen);
            SelectObject(drawItem.hDC, oldBrush);
            DeleteObject(checkPen);
            DeleteObject(checkBrush);

            const HPEN markPen = CreatePen(PS_SOLID, 2, palette.accentText);
            const HGDIOBJ oldMarkPen = SelectObject(drawItem.hDC, markPen);
            const int checkMarkInset = scaleMenuDimension(5);
            MoveToEx(drawItem.hDC,
                     checkRect.left + checkMarkInset,
                     checkRect.top + ((checkRect.bottom - checkRect.top) / 2),
                     nullptr);
            LineTo(drawItem.hDC, checkRect.left + scaleMenuDimension(9), checkRect.bottom - scaleMenuDimension(6));
            LineTo(drawItem.hDC, checkRect.right - checkMarkInset, checkRect.top + scaleMenuDimension(6));
            SelectObject(drawItem.hDC, oldMarkPen);
            DeleteObject(markPen);
        }

        const COLORREF labelColor = disabled
            ? BlendColor(palette.mutedText, backgroundColor, 128)
            : palette.text;
        const COLORREF shortcutColor = disabled
            ? BlendColor(palette.mutedText, backgroundColor, 128)
            : (selected ? palette.text : palette.mutedText);
        const HFONT effectiveMenuFont = menuFont ? menuFont : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

        RECT labelRect{itemRect.left + checkColumnWidth + textPadding,
                       itemRect.top,
                       itemRect.right - textPadding,
                       itemRect.bottom};
        if (!shortcut.empty())
        {
            labelRect.right -= MeasureTextWidth(effectiveMenuFont, shortcut) + shortcutGap;
        }
        std::wstring gdiLabel = label;
        UINT labelFormat = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS;
        if (drawData->mnemonic == L'&')
        {
            labelFormat |= DT_NOPREFIX;
        }
        else if (mnemonicIndex >= 0 && mnemonicIndex < static_cast<int>(gdiLabel.size()))
        {
            gdiLabel.insert(
                static_cast<std::wstring::size_type>(mnemonicIndex),
                1,
                L'&');
        }
        render::DrawGdiText(drawItem.hDC,
                            effectiveMenuFont,
                            gdiLabel.c_str(),
                            -1,
                            labelRect,
                            labelFormat,
                            labelColor,
                            backgroundColor);

        if (drawData->mnemonic == L'&' && !label.empty())
        {
            const int savedDc = SaveDC(drawItem.hDC);
            if (savedDc != 0)
            {
                SelectObject(drawItem.hDC, effectiveMenuFont);
                SIZE mnemonicSize{};
                if (GetTextExtentPoint32W(drawItem.hDC, label.c_str(), 1, &mnemonicSize))
                {
                    const HPEN mnemonicPen = CreatePen(PS_SOLID, 1, labelColor);
                    if (mnemonicPen)
                    {
                        const HGDIOBJ oldPen = SelectObject(drawItem.hDC, mnemonicPen);
                        const LONG underlineY = itemRect.bottom - std::max<LONG>(2, mnemonicSize.cy / 10);
                        MoveToEx(drawItem.hDC, labelRect.left, underlineY, nullptr);
                        LineTo(drawItem.hDC, labelRect.left + mnemonicSize.cx, underlineY);
                        SelectObject(drawItem.hDC, oldPen);
                        DeleteObject(mnemonicPen);
                    }
                }
                RestoreDC(drawItem.hDC, savedDc);
            }
        }

        if (!shortcut.empty())
        {
            const RECT shortcutRect{labelRect.right + shortcutGap,
                                    itemRect.top,
                                    itemRect.right - textPadding,
                                    itemRect.bottom};
            render::DrawGdiText(drawItem.hDC,
                                effectiveMenuFont,
                                shortcut.c_str(),
                                -1,
                                shortcutRect,
                                DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS,
                                shortcutColor,
                                backgroundColor);
        }
    }
}
