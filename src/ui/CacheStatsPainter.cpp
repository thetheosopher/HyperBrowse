#include "ui/CacheStatsPainter.h"

#include <d2d1.h>

#include <algorithm>

#include "render/D2DRenderer.h"
#include "render/GdiText.h"

namespace hyperbrowse::ui
{
    namespace
    {
        template <typename Callback>
        void VisitLines(std::wstring_view text, Callback&& callback)
        {
            std::size_t start = 0;
            while (start <= text.size())
            {
                const std::size_t lineEnd = text.find(L'\n', start);
                const std::size_t end = lineEnd == std::wstring_view::npos ? text.size() : lineEnd;
                const std::size_t length = end > start && text[end - 1] == L'\r' ? end - start - 1 : end - start;
                callback(text.substr(start, length));
                if (lineEnd == std::wstring_view::npos)
                {
                    break;
                }
                start = lineEnd + 1;
            }
        }

        bool IsHeading(std::wstring_view line)
        {
            return line == L"Thumbnail Cache"
                || line == L"Metadata Cache"
                || line == L"Persistent Thumbnail Cache"
                || line.starts_with(L"Memory pressure:");
        }
    }

    void CacheStatsPainter::PaintD2D(ID2D1RenderTarget* renderTarget,
                                     const State& state,
                                     const Palette& palette,
                                     IDWriteTextFormat* bodyFormat,
                                     IDWriteTextFormat* headingFormat)
    {
        if (!renderTarget || !bodyFormat || !headingFormat || IsRectEmpty(&state.bodyRect))
        {
            return;
        }

        const auto createBrush = [renderTarget](COLORREF color)
        {
            render::ComPtr<ID2D1SolidColorBrush> brush;
            renderTarget->CreateSolidColorBrush(render::ToD2DColor(color), brush.GetAddressOf());
            return brush;
        };

        int top = state.bodyRect.top;
        VisitLines(state.text, [&](std::wstring_view line)
        {
            if (top >= state.bodyRect.bottom)
            {
                return;
            }

            const bool heading = IsHeading(line);
            const int lineHeight = heading ? state.headingLineHeight : state.lineHeight;
            const auto brush = createBrush(heading ? palette.headingText : palette.mutedText);
            if (brush && !line.empty())
            {
                const RECT lineRect{state.bodyRect.left,
                                    top,
                                    state.bodyRect.right,
                                    (std::min)(static_cast<int>(state.bodyRect.bottom), top + lineHeight)};
                renderTarget->DrawText(line.data(),
                                       static_cast<UINT32>(line.size()),
                                       heading ? headingFormat : bodyFormat,
                                       render::ToD2DRect(lineRect),
                                       brush.Get());
            }
            top += lineHeight;
        });
    }

    void CacheStatsPainter::PaintGdi(HDC hdc,
                                     const State& state,
                                     const Palette& palette,
                                     HFONT bodyFont,
                                     HFONT headingFont)
    {
        if (!hdc || IsRectEmpty(&state.bodyRect))
        {
            return;
        }

        int top = state.bodyRect.top;
        VisitLines(state.text, [&](std::wstring_view line)
        {
            if (top >= state.bodyRect.bottom)
            {
                return;
            }

            const bool heading = IsHeading(line);
            const int lineHeight = heading ? state.headingLineHeight : state.lineHeight;
            const RECT lineRect{state.bodyRect.left,
                                top,
                                state.bodyRect.right,
                                (std::min)(static_cast<int>(state.bodyRect.bottom), top + lineHeight)};
            if (!line.empty())
            {
                render::DrawGdiText(hdc,
                                    heading ? headingFont : bodyFont,
                                    line.data(),
                                    static_cast<int>(line.size()),
                                    lineRect,
                                    DT_LEFT | DT_TOP | DT_NOPREFIX | DT_SINGLELINE,
                                    heading ? palette.headingText : palette.mutedText,
                                    palette.paneBackground);
            }
            top += lineHeight;
        });
    }
}
