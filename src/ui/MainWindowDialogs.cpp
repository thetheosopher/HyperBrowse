#include "ui/MainWindowDialogs.h"

#include <commctrl.h>
#include <windowsx.h>

#include <algorithm>
#include <cwchar>
#include <cwctype>
#include <filesystem>
#include <unordered_map>

#include "ui/DialogTheme.h"
#include "ui/DialogDpi.h"

namespace fs = std::filesystem;

namespace hyperbrowse::ui
{
    bool IsValidRenameLeafName(std::wstring_view leafName, std::wstring* errorMessage);

    namespace
    {
        constexpr wchar_t kTextInputDialogClassName[] = L"HyperBrowseTextInputDialog";
        constexpr int kTextInputDialogWidth = 440;
        constexpr int kTextInputDialogHeight = 160;
        constexpr int kTextInputDialogMargin = 14;
        constexpr int kTextInputDialogInstructionMinHeight = 42;
        constexpr int kTextInputDialogEditTopGap = 8;
        constexpr int kTextInputDialogDividerTopGap = 14;
        constexpr int kTextInputDialogButtonTopGap = 10;
        constexpr int kTextInputEditHeight = 30;
        constexpr int kTextInputButtonWidth = 88;
        constexpr int kTextInputButtonHeight = 28;
        constexpr int kTextInputEditControlId = 100;
        constexpr wchar_t kBatchRenameDialogClassName[] = L"HyperBrowseBatchRenameDialog";
        constexpr int kBatchRenameDialogWidth = 760;
        constexpr int kBatchRenameDialogHeight = 460;
        constexpr int kBatchRenamePatternEditControlId = 200;
        constexpr int kBatchRenamePreviewListControlId = 201;
        constexpr int kBatchRenameInstructionControlId = 202;
        constexpr int kBatchRenameHelpControlId = 203;

        struct TextInputDialogState
        {
            HWND ownerWindow{};
            UINT dpi{96};
            HWND instructionWindow{};
            HWND editWindow{};
            HWND dividerWindow{};
            HWND okButton{};
            HFONT bodyFont{};
            DialogTheme theme{};
            HBRUSH backgroundBrush{};
            HBRUSH fieldBrush{};
            hyperbrowse::util::AppTextSize appTextSize{hyperbrowse::util::kDefaultAppTextSize};
            std::wstring title;
            std::wstring instruction;
            std::wstring confirmLabel;
            std::wstring initialText;
            std::wstring resultText;
            int selectionStart{};
            int selectionEnd{};
            bool accepted{};
            bool done{};
        };

        struct TextInputDialogLayoutMetrics
        {
            int clientWidth{};
            int contentWidth{};
            int okButtonWidth{};
            int cancelButtonWidth{};
            int instructionHeight{};
            int editTop{};
            int dividerTop{};
            int buttonTop{};
            int clientHeight{};
        };

        struct BatchRenamePreviewRow
        {
            std::wstring currentLeafName;
            std::wstring renamedLeafName;
            std::wstring status;
            bool valid{true};
        };

        struct BatchRenameDialogState
        {
            HWND ownerWindow{};
            UINT dpi{96};
            HWND patternEditWindow{};
            HWND previewListWindow{};
            HWND okButton{};
            HFONT bodyFont{};
            DialogTheme theme{};
            HBRUSH backgroundBrush{};
            HBRUSH fieldBrush{};
            HBRUSH surfaceBrush{};
            hyperbrowse::util::AppTextSize appTextSize{hyperbrowse::util::kDefaultAppTextSize};
            std::wstring title;
            std::wstring instruction;
            std::wstring initialPattern;
            std::wstring pattern;
            std::vector<hyperbrowse::browser::BrowserItem> items;
            std::vector<std::wstring> resultLeafNames;
            std::vector<BatchRenamePreviewRow> previewRows;
            int numberWidth{};
            bool canAccept{};
            bool accepted{};
            bool done{};
        };

        HFONT CreateDialogUiFont(int pointSize,
                                 int weight,
                                 hyperbrowse::util::AppTextSize size,
                                 UINT dpi = 0)
        {
            NONCLIENTMETRICSW metrics{};
            metrics.cbSize = sizeof(metrics);

            LOGFONTW logFont{};
            if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0) != FALSE)
            {
                logFont = metrics.lfMessageFont;
            }
            else
            {
                wcscpy_s(logFont.lfFaceName, L"Segoe UI");
            }

            HDC screenDc = GetDC(nullptr);
            const int dpiY = dpi != 0
                ? static_cast<int>(dpi)
                : (screenDc ? GetDeviceCaps(screenDc, LOGPIXELSY) : 96);
            if (screenDc)
            {
                ReleaseDC(nullptr, screenDc);
            }

            logFont.lfHeight = -MulDiv(hyperbrowse::util::ScaleAppTextDimension(pointSize, size), dpiY, 72);
            logFont.lfWeight = weight;
            logFont.lfCharSet = DEFAULT_CHARSET;
            logFont.lfQuality = CLEARTYPE_NATURAL_QUALITY;
            return CreateFontIndirectW(&logFont);
        }

        void DeleteFontIfOwned(HFONT font)
        {
            if (font && font != GetStockObject(DEFAULT_GUI_FONT))
            {
                DeleteObject(font);
            }
        }

        void CenterWindowOnOwner(HWND window, HWND ownerWindow)
        {
            RECT ownerRect{};
            RECT dialogRect{};
            const HWND referenceWindow = ownerWindow ? ownerWindow : GetDesktopWindow();
            GetWindowRect(referenceWindow, &ownerRect);
            GetWindowRect(window, &dialogRect);

            const int width = dialogRect.right - dialogRect.left;
            const int height = dialogRect.bottom - dialogRect.top;
            const int x = ownerRect.left + ((ownerRect.right - ownerRect.left) - width) / 2;
            const int y = ownerRect.top + ((ownerRect.bottom - ownerRect.top) - height) / 2;
            SetWindowPos(window, nullptr, x, y, 0, 0, SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
        }

        int MeasureTextBlockHeight(HFONT font,
                                   std::wstring_view text,
                                   int width,
                                   UINT format,
                                   int minimumHeight = 0)
        {
            if (width <= 0)
            {
                return minimumHeight;
            }

            std::wstring localText = text.empty() ? std::wstring(L" ") : std::wstring(text);
            HDC screenDc = GetDC(nullptr);
            if (!screenDc)
            {
                return minimumHeight;
            }

            const HGDIOBJ oldFont = font ? SelectObject(screenDc, font) : nullptr;
            RECT bounds{0, 0, width, 0};
            DrawTextW(screenDc, localText.c_str(), -1, &bounds, format | DT_CALCRECT);
            if (oldFont)
            {
                SelectObject(screenDc, oldFont);
            }
            ReleaseDC(nullptr, screenDc);
            const int measuredHeight = static_cast<int>(bounds.bottom - bounds.top);
            return std::max(minimumHeight, measuredHeight);
        }

        std::wstring TrimWhitespaceCopy(std::wstring value)
        {
            const auto isSpace = [](wchar_t character)
            {
                return iswspace(character) != 0;
            };

            const auto first = std::find_if_not(value.begin(), value.end(), isSpace);
            const auto last = std::find_if_not(value.rbegin(), value.rend(), isSpace).base();
            if (first >= last)
            {
                return {};
            }

            return std::wstring(first, last);
        }

        std::wstring ToLowercaseCopy(std::wstring value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](wchar_t character)
            {
                return static_cast<wchar_t>(towlower(character));
            });
            return value;
        }

        bool StringsEqualInsensitive(std::wstring_view lhs, std::wstring_view rhs)
        {
            return lhs.size() == rhs.size()
                && std::equal(lhs.begin(), lhs.end(), rhs.begin(), [](wchar_t left, wchar_t right)
                {
                    return towlower(left) == towlower(right);
                });
        }

        int CountDecimalDigits(std::size_t value)
        {
            int digits = 1;
            while (value >= 10)
            {
                value /= 10;
                ++digits;
            }

            return digits;
        }

        std::wstring FormatZeroPaddedSequence(std::size_t ordinal, int width)
        {
            wchar_t numberBuffer[32]{};
            swprintf_s(numberBuffer, L"%0*u", std::max(1, width), static_cast<unsigned int>(ordinal));
            return numberBuffer;
        }

        std::wstring BuildBatchRenameLeafName(std::wstring_view baseName,
                                              std::size_t ordinal,
                                              int numberWidth,
                                              std::wstring_view extension)
        {
            wchar_t numberBuffer[32]{};
            swprintf_s(numberBuffer, L"%0*u", numberWidth, static_cast<unsigned int>(ordinal));

            std::wstring leafName(baseName);
            if (!leafName.empty())
            {
                leafName.push_back(L' ');
            }
            leafName.append(numberBuffer);
            leafName.append(extension);
            return leafName;
        }

        bool TryBuildBatchRenamePatternLeafName(std::wstring_view pattern,
                                                const hyperbrowse::browser::BrowserItem& item,
                                                std::size_t ordinal,
                                                std::size_t selectionCount,
                                                int defaultNumberWidth,
                                                std::wstring* leafName,
                                                std::wstring* errorMessage)
        {
            if (!leafName)
            {
                return false;
            }

            const std::wstring trimmedPattern = TrimWhitespaceCopy(std::wstring(pattern));
            if (trimmedPattern.empty())
            {
                if (errorMessage)
                {
                    *errorMessage = L"Enter a rename pattern.";
                }
                return false;
            }

            const std::wstring originalLeafName = item.fileName;
            const std::wstring originalStem = fs::path(originalLeafName).stem().wstring();
            const std::wstring originalExtension = fs::path(originalLeafName).extension().wstring();
            const std::wstring folderName = fs::path(item.filePath).parent_path().filename().wstring();

            std::wstring generatedLeafName;
            generatedLeafName.reserve(trimmedPattern.size() + originalLeafName.size() + 16);
            bool usedNumberToken = false;
            bool usedExtensionToken = false;

            for (std::size_t index = 0; index < trimmedPattern.size();)
            {
                if (trimmedPattern[index] != L'{')
                {
                    generatedLeafName.push_back(trimmedPattern[index]);
                    ++index;
                    continue;
                }

                const std::size_t closeBrace = trimmedPattern.find(L'}', index + 1);
                if (closeBrace == std::wstring::npos)
                {
                    if (errorMessage)
                    {
                        *errorMessage = L"Rename patterns must close every token with '}'.";
                    }
                    return false;
                }

                const std::wstring token = TrimWhitespaceCopy(trimmedPattern.substr(index + 1, closeBrace - index - 1));
                const std::wstring normalizedToken = ToLowercaseCopy(token);
                if (normalizedToken == L"name")
                {
                    generatedLeafName.append(originalStem);
                }
                else if (normalizedToken == L"ext")
                {
                    generatedLeafName.append(originalExtension);
                    usedExtensionToken = true;
                }
                else if (normalizedToken == L"folder")
                {
                    generatedLeafName.append(folderName);
                }
                else if (normalizedToken == L"num" || normalizedToken.rfind(L"num:", 0) == 0)
                {
                    int tokenWidth = defaultNumberWidth;
                    if (normalizedToken.size() > 4)
                    {
                        const wchar_t* widthText = normalizedToken.c_str() + 4;
                        wchar_t* widthEnd = nullptr;
                        const long parsedWidth = wcstol(widthText, &widthEnd, 10);
                        if (widthEnd == widthText || *widthEnd != L'\0' || parsedWidth <= 0 || parsedWidth > 9)
                        {
                            if (errorMessage)
                            {
                                *errorMessage = L"Use {num} or {num:N} with N between 1 and 9.";
                            }
                            return false;
                        }
                        tokenWidth = static_cast<int>(parsedWidth);
                    }

                    generatedLeafName.append(FormatZeroPaddedSequence(ordinal, tokenWidth));
                    usedNumberToken = true;
                }
                else
                {
                    if (errorMessage)
                    {
                        *errorMessage = L"Unknown token {" + token + L"}. Supported tokens: {name}, {num}, {num:N}, {ext}, {folder}.";
                    }
                    return false;
                }

                index = closeBrace + 1;
            }

            generatedLeafName = TrimWhitespaceCopy(std::move(generatedLeafName));
            if (!usedNumberToken && selectionCount > 1)
            {
                generatedLeafName = BuildBatchRenameLeafName(generatedLeafName, ordinal, defaultNumberWidth, L"");
            }
            if (!usedExtensionToken)
            {
                generatedLeafName.append(originalExtension);
            }

            std::wstring validationError;
            if (!IsValidRenameLeafName(generatedLeafName, &validationError))
            {
                if (errorMessage)
                {
                    *errorMessage = std::move(validationError);
                }
                return false;
            }

            *leafName = std::move(generatedLeafName);
            return true;
        }

        TextInputDialogLayoutMetrics BuildTextInputDialogLayoutMetrics(const TextInputDialogState& state)
        {
            TextInputDialogLayoutMetrics metrics;
            const auto scale = [&state](int value)
            {
                return ScaleDialogDimension(value, state.dpi);
            };
            metrics.clientWidth = scale(kTextInputDialogWidth);

            const auto measureButtonWidth = [&state, &scale](std::wstring_view label) -> int
            {
                if (label.empty())
                {
                    return scale(kTextInputButtonWidth);
                }

                HDC screenDc = GetDC(nullptr);
                if (!screenDc)
                {
                    return scale(kTextInputButtonWidth);
                }

                const HFONT font = state.bodyFont
                    ? state.bodyFont
                    : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
                const HGDIOBJ oldFont = font ? SelectObject(screenDc, font) : nullptr;
                SIZE size{};
                const std::wstring localText(label);
                GetTextExtentPoint32W(screenDc,
                                      localText.c_str(),
                                      static_cast<int>(localText.size()),
                                      &size);
                if (oldFont)
                {
                    SelectObject(screenDc, oldFont);
                }
                ReleaseDC(nullptr, screenDc);

                return std::max(scale(kTextInputButtonWidth), static_cast<int>(size.cx) + scale(24));
            };

            metrics.okButtonWidth = measureButtonWidth(state.confirmLabel);
            metrics.cancelButtonWidth = measureButtonWidth(L"Cancel");

            const int buttonRowWidth = metrics.okButtonWidth + scale(8) + metrics.cancelButtonWidth;
            const int minimumClientWidthForButtons = scale(kTextInputDialogMargin * 2) + buttonRowWidth;
            metrics.clientWidth = std::max(metrics.clientWidth, minimumClientWidthForButtons);

            metrics.contentWidth = metrics.clientWidth - scale(kTextInputDialogMargin * 2);
            metrics.instructionHeight = MeasureTextBlockHeight(state.bodyFont,
                                                               state.instruction,
                                                               metrics.contentWidth,
                                                               DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK,
                                                               scale(kTextInputDialogInstructionMinHeight));
            metrics.editTop = scale(kTextInputDialogMargin) + metrics.instructionHeight + scale(kTextInputDialogEditTopGap);
            metrics.dividerTop = metrics.editTop + scale(kTextInputEditHeight) + scale(kTextInputDialogDividerTopGap);
            metrics.buttonTop = metrics.dividerTop + scale(kTextInputDialogButtonTopGap);
            metrics.clientHeight = metrics.buttonTop + scale(kTextInputButtonHeight + kTextInputDialogMargin);
            return metrics;
        }

        void LayoutTextInputDialogControls(HWND hwnd, const TextInputDialogState& state)
        {
            const auto scale = [&state](int value)
            {
                return ScaleDialogDimension(value, state.dpi);
            };
            RECT client{};
            GetClientRect(hwnd, &client);
            const int clientWidth = client.right - client.left;
            const TextInputDialogLayoutMetrics metrics = BuildTextInputDialogLayoutMetrics(state);
            const int margin = scale(kTextInputDialogMargin);
            const int contentWidth = std::max(1, clientWidth - margin * 2);
            const int cancelLeft = clientWidth - margin - metrics.cancelButtonWidth;
            const int okLeft = cancelLeft - scale(8) - metrics.okButtonWidth;

            if (state.instructionWindow)
            {
                MoveWindow(state.instructionWindow, margin, margin, contentWidth, metrics.instructionHeight, TRUE);
            }
            if (state.editWindow)
            {
                MoveWindow(state.editWindow, margin, metrics.editTop, contentWidth, scale(kTextInputEditHeight), TRUE);
            }
            if (state.dividerWindow)
            {
                MoveWindow(state.dividerWindow, margin, metrics.dividerTop, contentWidth, scale(2), TRUE);
            }
            if (state.okButton)
            {
                MoveWindow(state.okButton, okLeft, metrics.buttonTop, metrics.okButtonWidth, scale(kTextInputButtonHeight), TRUE);
            }
            const HWND cancelButton = GetDlgItem(hwnd, IDCANCEL);
            if (cancelButton)
            {
                MoveWindow(cancelButton, cancelLeft, metrics.buttonTop, metrics.cancelButtonWidth, scale(kTextInputButtonHeight), TRUE);
            }
        }

        int DefaultRenameSelectionEnd(std::wstring_view leafName, bool isFile)
        {
            if (!isFile)
            {
                return static_cast<int>(leafName.size());
            }

            const fs::path leafPath(leafName);
            const std::wstring stem = leafPath.stem().wstring();
            const std::wstring extension = leafPath.extension().wstring();
            if (!stem.empty() && !extension.empty())
            {
                return static_cast<int>(stem.size());
            }

            return static_cast<int>(leafName.size());
        }

        void LayoutBatchRenameDialogControls(HWND hwnd, const BatchRenameDialogState& state)
        {
            RECT clientRect{};
            GetClientRect(hwnd, &clientRect);

            const int clientWidth = clientRect.right - clientRect.left;
            const int clientHeight = clientRect.bottom - clientRect.top;
            const auto scale = [&state](int value)
            {
                return ScaleDialogDimension(value, state.dpi);
            };
            const int contentWidth = clientWidth - scale(kTextInputDialogMargin * 2);
            const int instructionHeight = scale(44);
            const int helpHeight = scale(38);
            const int buttonTop = clientHeight - scale(kTextInputDialogMargin) - scale(kTextInputButtonHeight);
            const int listTop = scale(kTextInputDialogMargin) + instructionHeight + scale(6) + scale(kTextInputEditHeight) + scale(10) + helpHeight + scale(8);
            const int listHeight = std::max(scale(120), buttonTop - scale(12) - listTop);
            const int cancelLeft = clientWidth - scale(kTextInputDialogMargin) - scale(kTextInputButtonWidth);
            const int okLeft = cancelLeft - scale(8) - scale(kTextInputButtonWidth);

            const HWND instructionWindow = GetDlgItem(hwnd, kBatchRenameInstructionControlId);
            if (instructionWindow)
            {
                MoveWindow(instructionWindow,
                           scale(kTextInputDialogMargin),
                           scale(kTextInputDialogMargin),
                           contentWidth,
                           instructionHeight,
                           TRUE);
            }

            if (state.patternEditWindow)
            {
                MoveWindow(state.patternEditWindow,
                           scale(kTextInputDialogMargin),
                           scale(kTextInputDialogMargin) + instructionHeight + scale(6),
                           contentWidth,
                           scale(kTextInputEditHeight),
                           TRUE);
            }

            const HWND helpWindow = GetDlgItem(hwnd, kBatchRenameHelpControlId);
            if (helpWindow)
            {
                MoveWindow(helpWindow,
                           scale(kTextInputDialogMargin),
                           scale(kTextInputDialogMargin) + instructionHeight + scale(6) + scale(kTextInputEditHeight) + scale(10),
                           contentWidth,
                           helpHeight,
                           TRUE);
            }

            if (state.previewListWindow)
            {
                MoveWindow(state.previewListWindow,
                           kTextInputDialogMargin,
                           listTop,
                           contentWidth,
                           listHeight,
                           TRUE);

                ListView_SetColumnWidth(state.previewListWindow, 0, std::max(160, contentWidth / 3));
                ListView_SetColumnWidth(state.previewListWindow, 1, std::max(200, contentWidth / 2));
                ListView_SetColumnWidth(state.previewListWindow, 2, std::max(110, contentWidth - (std::max(160, contentWidth / 3) + std::max(200, contentWidth / 2)) - 8));
            }

            if (state.okButton)
            {
                MoveWindow(state.okButton, okLeft, buttonTop, scale(kTextInputButtonWidth), scale(kTextInputButtonHeight), TRUE);
            }

            const HWND cancelButton = GetDlgItem(hwnd, IDCANCEL);
            if (cancelButton)
            {
                MoveWindow(cancelButton, cancelLeft, buttonTop, scale(kTextInputButtonWidth), scale(kTextInputButtonHeight), TRUE);
            }
        }

        void RefreshBatchRenameDialogPreview(BatchRenameDialogState* state)
        {
            if (!state || !state->patternEditWindow || !state->previewListWindow)
            {
                return;
            }

            const int textLength = GetWindowTextLengthW(state->patternEditWindow);
            std::wstring pattern(static_cast<std::size_t>(textLength) + 1, L'\0');
            GetWindowTextW(state->patternEditWindow, pattern.data(), static_cast<int>(pattern.size()));
            pattern.resize(wcslen(pattern.c_str()));

            state->pattern = pattern;
            state->previewRows.clear();
            state->resultLeafNames.clear();
            state->canAccept = false;

            std::vector<std::wstring> generatedLeafNames;
            generatedLeafNames.reserve(state->items.size());
            state->previewRows.reserve(state->items.size());

            bool hasErrors = false;
            bool hasChanges = false;
            for (std::size_t index = 0; index < state->items.size(); ++index)
            {
                const auto& item = state->items[index];
                BatchRenamePreviewRow row;
                row.currentLeafName = item.fileName;

                std::wstring validationMessage;
                if (TryBuildBatchRenamePatternLeafName(pattern,
                                                       item,
                                                       index + 1,
                                                       state->items.size(),
                                                       state->numberWidth,
                                                       &row.renamedLeafName,
                                                       &validationMessage))
                {
                    row.status = StringsEqualInsensitive(row.renamedLeafName, item.fileName)
                        ? L"No change"
                        : L"Ready";
                    hasChanges = hasChanges || !StringsEqualInsensitive(row.renamedLeafName, item.fileName);
                    generatedLeafNames.push_back(row.renamedLeafName);
                }
                else
                {
                    row.valid = false;
                    row.status = validationMessage.empty() ? L"Invalid pattern" : validationMessage;
                    hasErrors = true;
                    generatedLeafNames.push_back(std::wstring{});
                }

                state->previewRows.push_back(std::move(row));
            }

            if (!hasErrors)
            {
                std::unordered_map<std::wstring, std::size_t> firstIndexByName;
                for (std::size_t index = 0; index < generatedLeafNames.size(); ++index)
                {
                    const std::wstring normalizedLeafName = ToLowercaseCopy(generatedLeafNames[index]);
                    const auto [iterator, inserted] = firstIndexByName.emplace(normalizedLeafName, index);
                    if (inserted)
                    {
                        continue;
                    }

                    state->previewRows[index].valid = false;
                    state->previewRows[index].status = L"Duplicate name";
                    state->previewRows[iterator->second].valid = false;
                    state->previewRows[iterator->second].status = L"Duplicate name";
                    hasErrors = true;
                }
            }

            if (!hasErrors && hasChanges)
            {
                state->resultLeafNames = std::move(generatedLeafNames);
                state->canAccept = true;
            }

            ListView_DeleteAllItems(state->previewListWindow);
            for (std::size_t index = 0; index < state->previewRows.size(); ++index)
            {
                const BatchRenamePreviewRow& row = state->previewRows[index];
                LVITEMW item{};
                item.mask = LVIF_TEXT;
                item.iItem = static_cast<int>(index);
                item.pszText = const_cast<LPWSTR>(row.currentLeafName.c_str());
                const int insertedIndex = ListView_InsertItem(state->previewListWindow, &item);
                if (insertedIndex < 0)
                {
                    continue;
                }

                ListView_SetItemText(state->previewListWindow,
                                     insertedIndex,
                                     1,
                                     const_cast<LPWSTR>(row.renamedLeafName.c_str()));
                ListView_SetItemText(state->previewListWindow,
                                     insertedIndex,
                                     2,
                                     const_cast<LPWSTR>(row.status.c_str()));
            }

            if (state->okButton)
            {
                EnableWindow(state->okButton, state->canAccept ? TRUE : FALSE);
            }
        }

        LRESULT CALLBACK TextInputDialogProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
        {
            auto* state = reinterpret_cast<TextInputDialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

            switch (message)
            {
            case WM_NCCREATE:
            {
                const auto* createStruct = reinterpret_cast<const CREATESTRUCTW*>(lParam);
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(createStruct->lpCreateParams));
                return TRUE;
            }
            case WM_CREATE:
            {
                state = reinterpret_cast<TextInputDialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
                if (!state)
                {
                    return -1;
                }

                const HFONT font = state->bodyFont
                    ? state->bodyFont
                    : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
                const TextInputDialogLayoutMetrics metrics = BuildTextInputDialogLayoutMetrics(*state);
                const int clientWidth = metrics.clientWidth;
                const int contentWidth = metrics.contentWidth;
                const int buttonTop = metrics.buttonTop;
                const int margin = ScaleDialogDimension(kTextInputDialogMargin, state->dpi);
                const int cancelLeft = clientWidth - margin - metrics.cancelButtonWidth;
                const int okLeft = cancelLeft - ScaleDialogDimension(8, state->dpi) - metrics.okButtonWidth;

                state->instructionWindow = CreateWindowExW(
                    0,
                    L"STATIC",
                    state->instruction.c_str(),
                    WS_CHILD | WS_VISIBLE,
                    margin,
                    margin,
                    contentWidth,
                    metrics.instructionHeight,
                    hwnd,
                    nullptr,
                    reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd, GWLP_HINSTANCE)),
                    nullptr);
                state->editWindow = CreateWindowExW(
                    WS_EX_CLIENTEDGE,
                    L"EDIT",
                    state->initialText.c_str(),
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                    margin,
                    metrics.editTop,
                    contentWidth,
                    ScaleDialogDimension(kTextInputEditHeight, state->dpi),
                    hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(kTextInputEditControlId)),
                    reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd, GWLP_HINSTANCE)),
                    nullptr);
                state->dividerWindow = CreateWindowExW(
                    0,
                    L"STATIC",
                    nullptr,
                    WS_CHILD | WS_VISIBLE | SS_ETCHEDHORZ,
                    margin,
                    metrics.dividerTop,
                    contentWidth,
                    ScaleDialogDimension(2, state->dpi),
                    hwnd,
                    nullptr,
                    reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd, GWLP_HINSTANCE)),
                    nullptr);
                state->okButton = CreateWindowExW(
                    0,
                    L"BUTTON",
                    state->confirmLabel.c_str(),
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                    okLeft,
                    buttonTop,
                    metrics.okButtonWidth,
                    ScaleDialogDimension(kTextInputButtonHeight, state->dpi),
                    hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDOK)),
                    reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd, GWLP_HINSTANCE)),
                    nullptr);
                HWND cancelButton = CreateWindowExW(
                    0,
                    L"BUTTON",
                    L"Cancel",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                    cancelLeft,
                    buttonTop,
                    metrics.cancelButtonWidth,
                    ScaleDialogDimension(kTextInputButtonHeight, state->dpi),
                    hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDCANCEL)),
                    reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd, GWLP_HINSTANCE)),
                    nullptr);

                if (state->instructionWindow) SendMessageW(state->instructionWindow, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
                if (state->editWindow) SendMessageW(state->editWindow, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
                if (state->dividerWindow) SendMessageW(state->dividerWindow, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
                if (state->okButton) SendMessageW(state->okButton, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
                if (cancelButton) SendMessageW(cancelButton, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

                if (state->editWindow)
                {
                    SendMessageW(state->editWindow, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(6, 6));
                    SendMessageW(state->editWindow,
                                 EM_SETSEL,
                                 static_cast<WPARAM>(state->selectionStart),
                                 static_cast<LPARAM>(state->selectionEnd));
                }

                state->backgroundBrush = CreateSolidBrush(state->theme.windowBackground);
                state->fieldBrush = CreateSolidBrush(state->theme.fieldBackground);

                CenterWindowOnOwner(hwnd, state->ownerWindow);
                return 0;
            }
            case WM_SHOWWINDOW:
                if (wParam != FALSE && state && state->editWindow)
                {
                    SetFocus(state->editWindow);
                    return FALSE;
                }
                break;
            case WM_SIZE:
                if (state)
                {
                    LayoutTextInputDialogControls(hwnd, *state);
                }
                return 0;
            case WM_DPICHANGED:
                if (state)
                {
                    state->dpi = std::max<UINT>(96, HIWORD(wParam));
                    DeleteFontIfOwned(state->bodyFont);
                    state->bodyFont = CreateDialogUiFont(9, FW_NORMAL, state->appTextSize, state->dpi);
                    const HFONT font = state->bodyFont
                        ? state->bodyFont
                        : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
                    for (const HWND child : {state->instructionWindow,
                                              state->editWindow,
                                              state->dividerWindow,
                                              state->okButton,
                                              GetDlgItem(hwnd, IDCANCEL)})
                    {
                        if (child)
                        {
                            SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
                        }
                    }
                    const auto* suggestedRect = reinterpret_cast<const RECT*>(lParam);
                    if (suggestedRect)
                    {
                        SetWindowPos(hwnd, nullptr,
                                     suggestedRect->left,
                                     suggestedRect->top,
                                     suggestedRect->right - suggestedRect->left,
                                     suggestedRect->bottom - suggestedRect->top,
                                     SWP_NOZORDER | SWP_NOACTIVATE);
                    }
                    LayoutTextInputDialogControls(hwnd, *state);
                }
                return 0;
            case WM_CTLCOLORDLG:
                if (state && state->backgroundBrush)
                {
                    return reinterpret_cast<INT_PTR>(state->backgroundBrush);
                }
                break;
            case WM_CTLCOLORSTATIC:
                if (state)
                {
                    const HDC dc = reinterpret_cast<HDC>(wParam);
                    SetBkMode(dc, TRANSPARENT);
                    SetTextColor(dc, state->theme.text);
                    SetBkColor(dc, state->theme.windowBackground);
                    return reinterpret_cast<INT_PTR>(state->backgroundBrush);
                }
                break;
            case WM_CTLCOLOREDIT:
                if (state)
                {
                    const HDC dc = reinterpret_cast<HDC>(wParam);
                    SetBkMode(dc, OPAQUE);
                    SetTextColor(dc, state->theme.text);
                    SetBkColor(dc, state->theme.fieldBackground);
                    return reinterpret_cast<INT_PTR>(state->fieldBrush);
                }
                break;
            case WM_CTLCOLORBTN:
                if (state)
                {
                    const HDC dc = reinterpret_cast<HDC>(wParam);
                    SetBkMode(dc, TRANSPARENT);
                    SetTextColor(dc, state->theme.text);
                    SetBkColor(dc, state->theme.windowBackground);
                    return reinterpret_cast<INT_PTR>(state->backgroundBrush);
                }
                break;
            case WM_ERASEBKGND:
                if (state && state->backgroundBrush)
                {
                    RECT client{};
                    GetClientRect(hwnd, &client);
                    FillRect(reinterpret_cast<HDC>(wParam), &client, state->backgroundBrush);
                    return 1;
                }
                break;
            case WM_COMMAND:
                if (!state)
                {
                    break;
                }

                if (LOWORD(wParam) == IDOK)
                {
                    const int length = state->editWindow ? GetWindowTextLengthW(state->editWindow) : 0;
                    std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
                    if (state->editWindow)
                    {
                        GetWindowTextW(state->editWindow, text.data(), static_cast<int>(text.size()));
                    }
                    text.resize(wcslen(text.c_str()));
                    state->resultText = std::move(text);
                    state->accepted = true;
                    DestroyWindow(hwnd);
                    return 0;
                }

                if (LOWORD(wParam) == IDCANCEL)
                {
                    DestroyWindow(hwnd);
                    return 0;
                }
                break;
            case WM_CLOSE:
                DestroyWindow(hwnd);
                return 0;
            case WM_DESTROY:
                if (state)
                {
                    if (state->backgroundBrush)
                    {
                        DeleteObject(state->backgroundBrush);
                        state->backgroundBrush = nullptr;
                    }
                    if (state->fieldBrush)
                    {
                        DeleteObject(state->fieldBrush);
                        state->fieldBrush = nullptr;
                    }
                    state->done = true;
                }
                return 0;
            default:
                break;
            }

            return DefWindowProcW(hwnd, message, wParam, lParam);
        }

        LRESULT CALLBACK BatchRenameDialogProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
        {
            auto* state = reinterpret_cast<BatchRenameDialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

            switch (message)
            {
            case WM_NCCREATE:
            {
                const auto* createStruct = reinterpret_cast<const CREATESTRUCTW*>(lParam);
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(createStruct->lpCreateParams));
                return TRUE;
            }
            case WM_CREATE:
            {
                state = reinterpret_cast<BatchRenameDialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
                if (!state)
                {
                    return -1;
                }

                const HFONT font = state->bodyFont
                    ? state->bodyFont
                    : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
                state->backgroundBrush = CreateSolidBrush(state->theme.windowBackground);
                state->fieldBrush = CreateSolidBrush(state->theme.fieldBackground);
                state->surfaceBrush = CreateSolidBrush(state->theme.surfaceBackground);
                const HWND instructionWindow = CreateWindowExW(
                    0,
                    L"STATIC",
                    state->instruction.c_str(),
                    WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
                    0,
                    0,
                    100,
                    40,
                    hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(kBatchRenameInstructionControlId)),
                    reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd, GWLP_HINSTANCE)),
                    nullptr);

                state->patternEditWindow = CreateWindowExW(
                    WS_EX_CLIENTEDGE,
                    L"EDIT",
                    state->initialPattern.c_str(),
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                    0,
                    0,
                    100,
                    kTextInputEditHeight,
                    hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(kBatchRenamePatternEditControlId)),
                    reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd, GWLP_HINSTANCE)),
                    nullptr);

                const HWND helpWindow = CreateWindowExW(
                    0,
                    L"STATIC",
                    L"Tokens: {name} original stem, {num} zero-padded sequence, {num:N} explicit width, {ext} original extension, {folder} parent folder.",
                    WS_CHILD | WS_VISIBLE,
                    0,
                    0,
                    100,
                    38,
                    hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(kBatchRenameHelpControlId)),
                    reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd, GWLP_HINSTANCE)),
                    nullptr);

                state->previewListWindow = CreateWindowExW(
                    WS_EX_CLIENTEDGE,
                    WC_LISTVIEWW,
                    nullptr,
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SHOWSELALWAYS,
                    0,
                    0,
                    100,
                    100,
                    hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(kBatchRenamePreviewListControlId)),
                    reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd, GWLP_HINSTANCE)),
                    nullptr);

                state->okButton = CreateWindowExW(
                    0,
                    L"BUTTON",
                    L"Rename",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                    0,
                    0,
                    kTextInputButtonWidth,
                    kTextInputButtonHeight,
                    hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDOK)),
                    reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd, GWLP_HINSTANCE)),
                    nullptr);

                const HWND cancelButton = CreateWindowExW(
                    0,
                    L"BUTTON",
                    L"Cancel",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                    0,
                    0,
                    kTextInputButtonWidth,
                    kTextInputButtonHeight,
                    hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDCANCEL)),
                    reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd, GWLP_HINSTANCE)),
                    nullptr);

                if (instructionWindow) SendMessageW(instructionWindow, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
                if (state->patternEditWindow) SendMessageW(state->patternEditWindow, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
                if (helpWindow) SendMessageW(helpWindow, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
                if (state->okButton) SendMessageW(state->okButton, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
                if (cancelButton) SendMessageW(cancelButton, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

                if (state->previewListWindow)
                {
                    ListView_SetExtendedListViewStyle(state->previewListWindow,
                                                      LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
                    ListView_SetBkColor(state->previewListWindow, state->theme.surfaceBackground);
                    ListView_SetTextBkColor(state->previewListWindow, state->theme.surfaceBackground);
                    ListView_SetTextColor(state->previewListWindow, state->theme.text);

                    LVCOLUMNW column{};
                    column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
                    column.pszText = const_cast<LPWSTR>(L"Current Name");
                    column.cx = 220;
                    ListView_InsertColumn(state->previewListWindow, 0, &column);

                    column.pszText = const_cast<LPWSTR>(L"New Name");
                    column.cx = 300;
                    column.iSubItem = 1;
                    ListView_InsertColumn(state->previewListWindow, 1, &column);

                    column.pszText = const_cast<LPWSTR>(L"Status");
                    column.cx = 140;
                    column.iSubItem = 2;
                    ListView_InsertColumn(state->previewListWindow, 2, &column);
                }

                LayoutBatchRenameDialogControls(hwnd, *state);
                if (state->patternEditWindow)
                {
                    SendMessageW(state->patternEditWindow,
                                 EM_SETSEL,
                                 0,
                                 static_cast<LPARAM>(state->initialPattern.size()));
                }
                RefreshBatchRenameDialogPreview(state);
                CenterWindowOnOwner(hwnd, state->ownerWindow);
                return 0;
            }
            case WM_SIZE:
                if (state)
                {
                    LayoutBatchRenameDialogControls(hwnd, *state);
                }
                return 0;
            case WM_DPICHANGED:
                if (state)
                {
                    state->dpi = std::max<UINT>(96, HIWORD(wParam));
                    DeleteFontIfOwned(state->bodyFont);
                    state->bodyFont = CreateDialogUiFont(9, FW_NORMAL, state->appTextSize, state->dpi);
                    const HFONT font = state->bodyFont
                        ? state->bodyFont
                        : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
                    for (const HWND child : {GetDlgItem(hwnd, kBatchRenameInstructionControlId),
                                              state->patternEditWindow,
                                              GetDlgItem(hwnd, kBatchRenameHelpControlId),
                                              state->previewListWindow,
                                              state->okButton,
                                              GetDlgItem(hwnd, IDCANCEL)})
                    {
                        if (child)
                        {
                            SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
                        }
                    }
                    if (state->previewListWindow)
                    {
                        ListView_SetColumnWidth(state->previewListWindow, 0, ScaleDialogDimension(220, state->dpi));
                        ListView_SetColumnWidth(state->previewListWindow, 1, ScaleDialogDimension(300, state->dpi));
                        ListView_SetColumnWidth(state->previewListWindow, 2, ScaleDialogDimension(140, state->dpi));
                    }
                    const auto* suggestedRect = reinterpret_cast<const RECT*>(lParam);
                    if (suggestedRect)
                    {
                        SetWindowPos(hwnd, nullptr,
                                     suggestedRect->left,
                                     suggestedRect->top,
                                     suggestedRect->right - suggestedRect->left,
                                     suggestedRect->bottom - suggestedRect->top,
                                     SWP_NOZORDER | SWP_NOACTIVATE);
                    }
                    LayoutBatchRenameDialogControls(hwnd, *state);
                }
                return 0;
            case WM_SHOWWINDOW:
                if (wParam != FALSE && state && state->patternEditWindow)
                {
                    SetFocus(state->patternEditWindow);
                    return FALSE;
                }
                break;
            case WM_CTLCOLORDLG:
                return state && state->backgroundBrush
                    ? reinterpret_cast<INT_PTR>(state->backgroundBrush)
                    : 0;
            case WM_CTLCOLORSTATIC:
                if (state)
                {
                    const HDC dc = reinterpret_cast<HDC>(wParam);
                    SetBkMode(dc, TRANSPARENT);
                    SetTextColor(dc, state->theme.text);
                    SetBkColor(dc, state->theme.windowBackground);
                    return reinterpret_cast<INT_PTR>(state->backgroundBrush);
                }
                break;
            case WM_CTLCOLOREDIT:
                if (state)
                {
                    const HDC dc = reinterpret_cast<HDC>(wParam);
                    SetBkMode(dc, OPAQUE);
                    SetTextColor(dc, state->theme.text);
                    SetBkColor(dc, state->theme.fieldBackground);
                    return reinterpret_cast<INT_PTR>(state->fieldBrush);
                }
                break;
            case WM_CTLCOLORBTN:
                if (state)
                {
                    const HDC dc = reinterpret_cast<HDC>(wParam);
                    SetBkMode(dc, TRANSPARENT);
                    SetTextColor(dc, state->theme.text);
                    SetBkColor(dc, state->theme.windowBackground);
                    return reinterpret_cast<INT_PTR>(state->backgroundBrush);
                }
                break;
            case WM_ERASEBKGND:
                if (state && state->backgroundBrush)
                {
                    RECT client{};
                    GetClientRect(hwnd, &client);
                    FillRect(reinterpret_cast<HDC>(wParam), &client, state->backgroundBrush);
                    return 1;
                }
                break;
            case WM_COMMAND:
                if (!state)
                {
                    break;
                }

                if (LOWORD(wParam) == kBatchRenamePatternEditControlId && HIWORD(wParam) == EN_CHANGE)
                {
                    RefreshBatchRenameDialogPreview(state);
                    return 0;
                }

                if (LOWORD(wParam) == IDOK)
                {
                    if (state->canAccept)
                    {
                        state->accepted = true;
                        DestroyWindow(hwnd);
                    }
                    return 0;
                }

                if (LOWORD(wParam) == IDCANCEL)
                {
                    DestroyWindow(hwnd);
                    return 0;
                }
                break;
            case WM_CLOSE:
                DestroyWindow(hwnd);
                return 0;
            case WM_DESTROY:
                if (state)
                {
                    if (state->backgroundBrush)
                    {
                        DeleteObject(state->backgroundBrush);
                        state->backgroundBrush = nullptr;
                    }
                    if (state->fieldBrush)
                    {
                        DeleteObject(state->fieldBrush);
                        state->fieldBrush = nullptr;
                    }
                    if (state->surfaceBrush)
                    {
                        DeleteObject(state->surfaceBrush);
                        state->surfaceBrush = nullptr;
                    }
                    state->done = true;
                }
                return 0;
            default:
                break;
            }

            return DefWindowProcW(hwnd, message, wParam, lParam);
        }
    }

    bool IsValidRenameLeafName(std::wstring_view leafName, std::wstring* errorMessage)
    {
        if (leafName.empty())
        {
            if (errorMessage) *errorMessage = L"The name cannot be empty.";
            return false;
        }

        if (leafName == L"." || leafName == L"..")
        {
            if (errorMessage) *errorMessage = L"The name is not valid.";
            return false;
        }

        if (std::any_of(leafName.begin(), leafName.end(), [](wchar_t character)
        {
            return character < 32 || wcschr(L"<>:\"/\\|?*", character) != nullptr;
        }))
        {
            if (errorMessage) *errorMessage = L"The name contains characters that Windows does not allow.";
            return false;
        }

        if (leafName.back() == L' ' || leafName.back() == L'.')
        {
            if (errorMessage) *errorMessage = L"Names cannot end with a space or a period.";
            return false;
        }

        return true;
    }

    bool PromptForSingleLineText(HWND ownerWindow,
                                 HINSTANCE instance,
                                 hyperbrowse::util::AppTextSize appTextSize,
                                 bool darkTheme,
                                 const std::wstring& title,
                                 const std::wstring& instruction,
                                 const std::wstring& confirmLabel,
                                 const std::wstring& initialText,
                                 int selectionStart,
                                 int selectionEnd,
                                 std::wstring* resultText)
    {
        if (!resultText)
        {
            return false;
        }

        WNDCLASSEXW windowClass{};
        if (GetClassInfoExW(instance, kTextInputDialogClassName, &windowClass) == FALSE)
        {
            windowClass.cbSize = sizeof(windowClass);
            windowClass.lpfnWndProc = &TextInputDialogProc;
            windowClass.hInstance = instance;
            windowClass.lpszClassName = kTextInputDialogClassName;
            windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            windowClass.hbrBackground = nullptr;
            if (RegisterClassExW(&windowClass) == 0)
            {
                return false;
            }
        }

        TextInputDialogState state;
        state.ownerWindow = ownerWindow;
        state.dpi = DialogDpiForWindow(ownerWindow);
        state.appTextSize = hyperbrowse::util::NormalizeAppTextSize(static_cast<std::uint32_t>(appTextSize));
        state.theme = MakeDialogTheme(darkTheme);
        state.bodyFont = CreateDialogUiFont(9, FW_NORMAL, state.appTextSize, state.dpi);
        if (!state.bodyFont)
        {
            state.bodyFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        }
        state.title = title;
        state.instruction = instruction;
        state.confirmLabel = confirmLabel;
        state.initialText = initialText;
        state.selectionStart = selectionStart;
        state.selectionEnd = selectionEnd;

        const TextInputDialogLayoutMetrics layoutMetrics = BuildTextInputDialogLayoutMetrics(state);
        RECT windowRect{0, 0, layoutMetrics.clientWidth, std::max(ScaleDialogDimension(kTextInputDialogHeight, state.dpi), layoutMetrics.clientHeight)};
        AdjustDialogWindowRectForDpi(&windowRect,
                         WS_CAPTION | WS_SYSMENU | WS_POPUP,
                         FALSE,
                         WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT,
                         state.dpi);

        if (ownerWindow)
        {
            EnableWindow(ownerWindow, FALSE);
        }

        HWND dialogWindow = CreateWindowExW(
            WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT,
            kTextInputDialogClassName,
            state.title.c_str(),
            WS_CAPTION | WS_SYSMENU | WS_POPUP | WS_CLIPCHILDREN,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            windowRect.right - windowRect.left,
            windowRect.bottom - windowRect.top,
            ownerWindow,
            nullptr,
            instance,
            &state);

        if (!dialogWindow)
        {
            if (ownerWindow)
            {
                EnableWindow(ownerWindow, TRUE);
            }
            DeleteFontIfOwned(state.bodyFont);
            return false;
        }

        SetWindowTextW(dialogWindow, state.title.c_str());

        ShowWindow(dialogWindow, SW_SHOWNORMAL);
        UpdateWindow(dialogWindow);

        MSG message{};
        while (!state.done && GetMessageW(&message, nullptr, 0, 0) > 0)
        {
            if (!IsDialogMessageW(dialogWindow, &message))
            {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }

        if (ownerWindow)
        {
            EnableWindow(ownerWindow, TRUE);
            SetForegroundWindow(ownerWindow);
            SetActiveWindow(ownerWindow);
        }

        DeleteFontIfOwned(state.bodyFont);

        if (!state.accepted)
        {
            return false;
        }

        *resultText = state.resultText;
        return true;
    }

    bool PromptForRenameLeafName(HWND ownerWindow,
                                 HINSTANCE instance,
                                 hyperbrowse::util::AppTextSize appTextSize,
                                 bool darkTheme,
                                 const std::wstring& title,
                                 const std::wstring& instruction,
                                 const std::wstring& currentLeafName,
                                 bool isFile,
                                 std::wstring* renamedLeafName)
    {
        if (!renamedLeafName)
        {
            return false;
        }

        std::wstring candidate = currentLeafName;
        const int selectionEnd = DefaultRenameSelectionEnd(currentLeafName, isFile);
        while (PromptForSingleLineText(ownerWindow,
                                       instance,
                                       appTextSize,
                                       darkTheme,
                                       title,
                                       instruction,
                                       L"Rename",
                                       candidate,
                                       0,
                                       selectionEnd,
                                       &candidate))
        {
            std::wstring errorMessage;
            if (!IsValidRenameLeafName(candidate, &errorMessage))
            {
                MessageBoxW(ownerWindow, errorMessage.c_str(), title.c_str(), MB_OK | MB_ICONWARNING);
                continue;
            }

            if (candidate == currentLeafName)
            {
                return false;
            }

            *renamedLeafName = candidate;
            return true;
        }

        return false;
    }

    bool PromptForBatchRenamePattern(HWND ownerWindow,
                                     HINSTANCE instance,
                                     hyperbrowse::util::AppTextSize appTextSize,
                                     bool darkTheme,
                                     std::wstring initialPattern,
                                     std::vector<hyperbrowse::browser::BrowserItem> items,
                                     std::vector<std::wstring>* resultLeafNames)
    {
        if (!resultLeafNames || items.size() < 2)
        {
            return false;
        }

        WNDCLASSEXW windowClass{};
        if (GetClassInfoExW(instance, kBatchRenameDialogClassName, &windowClass) == FALSE)
        {
            windowClass.cbSize = sizeof(windowClass);
            windowClass.lpfnWndProc = &BatchRenameDialogProc;
            windowClass.hInstance = instance;
            windowClass.lpszClassName = kBatchRenameDialogClassName;
            windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            windowClass.hbrBackground = nullptr;
            if (RegisterClassExW(&windowClass) == 0)
            {
                return false;
            }
        }

        BatchRenameDialogState state;
        state.ownerWindow = ownerWindow;
        state.dpi = DialogDpiForWindow(ownerWindow);
        state.appTextSize = hyperbrowse::util::NormalizeAppTextSize(static_cast<std::uint32_t>(appTextSize));
        state.theme = MakeDialogTheme(darkTheme);
        state.bodyFont = CreateDialogUiFont(9, FW_NORMAL, state.appTextSize, state.dpi);
        if (!state.bodyFont)
        {
            state.bodyFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        }
        state.title = L"Batch Rename";
        state.instruction = L"Enter a rename pattern. HyperBrowse previews every generated file name and preserves extensions unless you place {ext} yourself.";
        state.initialPattern = std::move(initialPattern);
        state.items = std::move(items);
        state.numberWidth = std::max(3, CountDecimalDigits(state.items.size()));

        RECT windowRect{0, 0,
                ScaleDialogDimension(kBatchRenameDialogWidth, state.dpi),
                ScaleDialogDimension(kBatchRenameDialogHeight, state.dpi)};
        AdjustDialogWindowRectForDpi(&windowRect,
                         WS_CAPTION | WS_SYSMENU | WS_POPUP | WS_CLIPCHILDREN,
                         FALSE,
                         WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT,
                         state.dpi);

        if (ownerWindow)
        {
            EnableWindow(ownerWindow, FALSE);
        }

        HWND dialogWindow = CreateWindowExW(
            WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT,
            kBatchRenameDialogClassName,
            state.title.c_str(),
            WS_CAPTION | WS_SYSMENU | WS_POPUP | WS_CLIPCHILDREN,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            windowRect.right - windowRect.left,
            windowRect.bottom - windowRect.top,
            ownerWindow,
            nullptr,
            instance,
            &state);

        if (!dialogWindow)
        {
            if (ownerWindow)
            {
                EnableWindow(ownerWindow, TRUE);
            }
            DeleteFontIfOwned(state.bodyFont);
            return false;
        }

        SetWindowTextW(dialogWindow, state.title.c_str());

        ShowWindow(dialogWindow, SW_SHOWNORMAL);
        UpdateWindow(dialogWindow);

        MSG message{};
        while (!state.done && GetMessageW(&message, nullptr, 0, 0) > 0)
        {
            if (!IsDialogMessageW(dialogWindow, &message))
            {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }

        if (ownerWindow)
        {
            EnableWindow(ownerWindow, TRUE);
            SetForegroundWindow(ownerWindow);
            SetActiveWindow(ownerWindow);
        }

        DeleteFontIfOwned(state.bodyFont);

        if (!state.accepted)
        {
            return false;
        }

        *resultLeafNames = std::move(state.resultLeafNames);
        return true;
    }
}
