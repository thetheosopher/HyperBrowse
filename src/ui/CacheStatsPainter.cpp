#include "ui/CacheStatsPainter.h"

#include <d2d1.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

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

        float ClampFraction(float value)
        {
            return (std::max)(0.0f, (std::min)(1.0f, value));
        }

        constexpr float kPi = 3.14159265358979323846f;

        std::vector<POINT> BuildArcPoints(const RECT& rect, float fraction)
        {
            const float centerX = (static_cast<float>(rect.left) + static_cast<float>(rect.right)) * 0.5f;
            const float centerY = (static_cast<float>(rect.top) + static_cast<float>(rect.bottom)) * 0.5f;
            const float radius = (std::min)(static_cast<float>(rect.right - rect.left),
                                            static_cast<float>(rect.bottom - rect.top)) * 0.5f;
            const float clampedFraction = ClampFraction(fraction);
            const float angle = clampedFraction * 2.0f * kPi;
            const int segmentCount = (std::max)(1, static_cast<int>(std::ceil(48.0f * clampedFraction)));

            std::vector<POINT> points;
            points.reserve(static_cast<std::size_t>(segmentCount + 1));
            for (int index = 0; index <= segmentCount; ++index)
            {
                const float progress = static_cast<float>(index) / static_cast<float>(segmentCount);
                const float currentAngle = -kPi * 0.5f + angle * progress;
                points.push_back(POINT{
                    static_cast<LONG>(std::lround(centerX + std::cos(currentAngle) * radius)),
                    static_cast<LONG>(std::lround(centerY + std::sin(currentAngle) * radius))});
            }
            return points;
        }

        void DrawD2DText(ID2D1RenderTarget* renderTarget,
                         std::wstring_view text,
                         IDWriteTextFormat* format,
                         const RECT& rect,
                         ID2D1Brush* brush,
                         bool centered = false,
                         bool trailing = false)
        {
            if (!renderTarget || !format || !brush || text.empty() || IsRectEmpty(&rect))
            {
                return;
            }

            auto layout = render::D2DRenderer::Instance().CreateTextLayout(
                text,
                format,
                static_cast<float>(rect.right - rect.left),
                static_cast<float>(rect.bottom - rect.top));
            if (!layout)
            {
                return;
            }

            layout->SetTextAlignment(centered
                                         ? DWRITE_TEXT_ALIGNMENT_CENTER
                                         : trailing
                                             ? DWRITE_TEXT_ALIGNMENT_TRAILING
                                             : DWRITE_TEXT_ALIGNMENT_LEADING);
            layout->SetParagraphAlignment(centered
                                              ? DWRITE_PARAGRAPH_ALIGNMENT_CENTER
                                              : DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
            renderTarget->DrawTextLayout(render::ToD2DPoint(static_cast<float>(rect.left), static_cast<float>(rect.top)),
                                         layout.Get(),
                                         brush);
        }

        void DrawD2DArc(ID2D1RenderTarget* renderTarget,
                        const RECT& rect,
                        float fraction,
                        ID2D1Brush* brush,
                        float strokeWidth)
        {
            if (!renderTarget || !brush || ClampFraction(fraction) <= 0.0f)
            {
                return;
            }

            const std::vector<POINT> points = BuildArcPoints(rect, fraction);
            for (std::size_t index = 1; index < points.size(); ++index)
            {
                renderTarget->DrawLine(
                    render::ToD2DPoint(static_cast<float>(points[index - 1].x), static_cast<float>(points[index - 1].y)),
                    render::ToD2DPoint(static_cast<float>(points[index].x), static_cast<float>(points[index].y)),
                    brush,
                    strokeWidth);
            }
        }

        void PaintVisualD2D(ID2D1RenderTarget* renderTarget,
                            const CacheStatsPainter::VisualState& visual,
                            const CacheStatsPainter::Palette& palette,
                            IDWriteTextFormat* bodyFormat,
                            IDWriteTextFormat* headingFormat)
        {
            if (!renderTarget || !bodyFormat || !headingFormat || IsRectEmpty(&visual.rect))
            {
                return;
            }

            const auto createBrush = [renderTarget](COLORREF color)
            {
                render::ComPtr<ID2D1SolidColorBrush> brush;
                renderTarget->CreateSolidColorBrush(render::ToD2DColor(color), brush.GetAddressOf());
                return brush;
            };
            const auto backgroundBrush = createBrush(palette.visualBackground);
            const auto trackBrush = createBrush(palette.meterTrack);
            const auto accentBrush = createBrush(visual.pressureActive ? palette.pressure : palette.accent);
            const auto mutedBrush = createBrush(palette.mutedText);
            const auto headingBrush = createBrush(palette.headingText);
            if (!backgroundBrush || !trackBrush || !accentBrush || !mutedBrush || !headingBrush)
            {
                return;
            }

            renderTarget->FillRoundedRectangle(
                render::ToD2DRoundedRect(visual.rect, 8.0f, 8.0f),
                backgroundBrush.Get());

            const int width = visual.rect.right - visual.rect.left;
            const bool sideBySide = width >= 300;
            const bool compact = width < 140;
            const int availableRingSize = (std::min)(width - 24,
                                                      static_cast<int>(visual.rect.bottom - visual.rect.top) - 42);
            const int ringSize = (std::min)(88, (std::max)(54, availableRingSize));
            const int ringTop = visual.rect.top + 34;
            const int ringLeft = sideBySide
                ? visual.rect.left + 12
                : visual.rect.left + (width - ringSize) / 2;
            const RECT ringRect{ringLeft, ringTop, ringLeft + ringSize, ringTop + ringSize};
            const D2D1_POINT_2F ringCenter = D2D1::Point2F(
                (static_cast<float>(ringRect.left) + static_cast<float>(ringRect.right)) * 0.5f,
                (static_cast<float>(ringRect.top) + static_cast<float>(ringRect.bottom)) * 0.5f);
            const float ringRadius = static_cast<float>(ringSize) * 0.5f - 8.0f;
            renderTarget->DrawEllipse(D2D1::Ellipse(ringCenter, ringRadius, ringRadius), trackBrush.Get(), 8.0f);
            const RECT arcRect{static_cast<int>(ringCenter.x - ringRadius),
                               static_cast<int>(ringCenter.y - ringRadius),
                               static_cast<int>(ringCenter.x + ringRadius),
                               static_cast<int>(ringCenter.y + ringRadius)};
            DrawD2DArc(renderTarget, arcRect, visual.hitRate, accentBrush.Get(), 8.0f);

            if (!compact)
            {
                DrawD2DText(renderTarget,
                            L"HIT RATE",
                            bodyFormat,
                            RECT{visual.rect.left + 10, visual.rect.top + 6, visual.rect.right - 10, visual.rect.top + 30},
                            mutedBrush.Get());
            }
            const int hitRatePercent = static_cast<int>(std::lround(ClampFraction(visual.hitRate) * 100.0f));
            const std::wstring hitRateText = std::to_wstring(hitRatePercent) + L"%";
            DrawD2DText(renderTarget,
                        hitRateText,
                        headingFormat,
                        ringRect,
                        headingBrush.Get(),
                        true);
            if (!compact)
            {
                const std::wstring pressureText = visual.pressureActive ? L"PRESSURE" : L"NORMAL";
                DrawD2DText(renderTarget,
                            pressureText,
                            bodyFormat,
                            RECT{visual.rect.left + 8,
                                   ringRect.bottom + 6,
                                 visual.rect.right - 8,
                                   ringRect.bottom + 30},
                            accentBrush.Get(),
                            true);
            }

            const int gaugeLeft = sideBySide
                ? visual.rect.left + ringSize + 28
                : visual.rect.left + 12;
            const int gaugeRight = visual.rect.right - 12;
            const int gaugeTop = sideBySide ? visual.rect.top + 34 : ringRect.bottom + 34;
            const int gaugeHeight = compact ? 25 : 36;
            for (std::size_t gaugeIndex = 0; gaugeIndex < visual.gauges.size(); ++gaugeIndex)
            {
                const auto& gauge = visual.gauges[gaugeIndex];
                const int top = gaugeTop + static_cast<int>(gaugeIndex) * gaugeHeight;
                if (top + gaugeHeight > visual.rect.bottom - 8 || gaugeLeft >= gaugeRight)
                {
                    break;
                }

                if (!compact)
                {
                    constexpr int valueWidth = 82;
                    const RECT labelRect{gaugeLeft, top, gaugeRight - valueWidth, top + 22};
                    const RECT valueRect{gaugeRight - valueWidth, top, gaugeRight, top + 22};
                    DrawD2DText(renderTarget, gauge.label, bodyFormat, labelRect, mutedBrush.Get());
                    DrawD2DText(renderTarget, gauge.value, bodyFormat, valueRect, headingBrush.Get(), false, true);
                }

                const RECT trackRect{gaugeLeft,
                                     top + (compact ? 8 : 25),
                                     gaugeRight,
                                     top + (compact ? 14 : 31)};
                renderTarget->FillRoundedRectangle(render::ToD2DRoundedRect(trackRect, 3.0f, 3.0f), trackBrush.Get());
                const int fillWidth = static_cast<int>(std::lround(static_cast<float>(trackRect.right - trackRect.left)
                                                                    * ClampFraction(gauge.fraction)));
                if (fillWidth > 0)
                {
                    const RECT fillRect{trackRect.left, trackRect.top, trackRect.left + fillWidth, trackRect.bottom};
                    renderTarget->FillRoundedRectangle(render::ToD2DRoundedRect(fillRect, 3.0f, 3.0f), accentBrush.Get());
                }
            }
        }

        void DrawGdiText(HDC hdc,
                         HFONT font,
                         std::wstring_view text,
                         const RECT& rect,
                         COLORREF color,
                         COLORREF background,
                         UINT format)
        {
            if (!hdc || !font || text.empty() || IsRectEmpty(&rect))
            {
                return;
            }

            render::DrawGdiText(hdc,
                                font,
                                text.data(),
                                static_cast<int>(text.size()),
                                rect,
                                format | DT_NOPREFIX,
                                color,
                                background);
        }

        void PaintVisualGdi(HDC hdc,
                            const CacheStatsPainter::VisualState& visual,
                            const CacheStatsPainter::Palette& palette,
                            HFONT bodyFont,
                            HFONT headingFont)
        {
            if (!hdc || !bodyFont || !headingFont || IsRectEmpty(&visual.rect))
            {
                return;
            }

            const HBRUSH backgroundBrush = CreateSolidBrush(palette.visualBackground);
            const HPEN borderPen = CreatePen(PS_SOLID, 1, palette.visualBackground);
            const HGDIOBJ previousBrush = SelectObject(hdc, backgroundBrush);
            const HGDIOBJ previousPen = SelectObject(hdc, borderPen);
            RoundRect(hdc, visual.rect.left, visual.rect.top, visual.rect.right, visual.rect.bottom, 16, 16);
            SelectObject(hdc, previousPen);
            SelectObject(hdc, previousBrush);
            DeleteObject(borderPen);
            DeleteObject(backgroundBrush);

            const int width = visual.rect.right - visual.rect.left;
            const bool sideBySide = width >= 300;
            const bool compact = width < 140;
            const int availableRingSize = (std::min)(width - 24,
                                                      static_cast<int>(visual.rect.bottom - visual.rect.top) - 42);
            const int ringSize = (std::min)(88, (std::max)(54, availableRingSize));
            const int ringTop = visual.rect.top + 34;
            const int ringLeft = sideBySide
                ? visual.rect.left + 12
                : visual.rect.left + (width - ringSize) / 2;
            const RECT ringRect{ringLeft, ringTop, ringLeft + ringSize, ringTop + ringSize};
            const int ringInset = 8;
            const RECT ringBounds{ringRect.left + ringInset,
                                  ringRect.top + ringInset,
                                  ringRect.right - ringInset,
                                  ringRect.bottom - ringInset};
            const HPEN trackPen = CreatePen(PS_SOLID, 8, palette.meterTrack);
            const HGDIOBJ previousTrackPen = SelectObject(hdc, trackPen);
            const HGDIOBJ previousNullBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
            Ellipse(hdc, ringBounds.left, ringBounds.top, ringBounds.right, ringBounds.bottom);
            SelectObject(hdc, previousNullBrush);
            SelectObject(hdc, previousTrackPen);
            DeleteObject(trackPen);

            const COLORREF accent = visual.pressureActive ? palette.pressure : palette.accent;
            const std::vector<POINT> arcPoints = BuildArcPoints(ringBounds, visual.hitRate);
            if (arcPoints.size() > 1)
            {
                const HPEN accentPen = CreatePen(PS_SOLID, 8, accent);
                const HGDIOBJ previousAccentPen = SelectObject(hdc, accentPen);
                Polyline(hdc, arcPoints.data(), static_cast<int>(arcPoints.size()));
                SelectObject(hdc, previousAccentPen);
                DeleteObject(accentPen);
            }

            if (!compact)
            {
                DrawGdiText(hdc,
                            bodyFont,
                            L"HIT RATE",
                            RECT{visual.rect.left + 10, visual.rect.top + 6, visual.rect.right - 10, visual.rect.top + 30},
                            palette.mutedText,
                            palette.visualBackground,
                            DT_LEFT | DT_TOP | DT_SINGLELINE);
            }
            const int hitRatePercent = static_cast<int>(std::lround(ClampFraction(visual.hitRate) * 100.0f));
            const std::wstring hitRateText = std::to_wstring(hitRatePercent) + L"%";
            DrawGdiText(hdc,
                        headingFont,
                        hitRateText,
                        ringRect,
                        palette.headingText,
                        palette.visualBackground,
                        DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            if (!compact)
            {
                const std::wstring pressureText = visual.pressureActive ? L"PRESSURE" : L"NORMAL";
                DrawGdiText(hdc,
                            bodyFont,
                            pressureText,
                            RECT{visual.rect.left + 8,
                                   ringRect.bottom + 6,
                                 visual.rect.right - 8,
                                   ringRect.bottom + 30},
                            accent,
                            palette.visualBackground,
                            DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }

            const int gaugeLeft = sideBySide
                ? visual.rect.left + ringSize + 28
                : visual.rect.left + 12;
            const int gaugeRight = visual.rect.right - 12;
            const int gaugeTop = sideBySide ? visual.rect.top + 34 : ringRect.bottom + 34;
            const int gaugeHeight = compact ? 25 : 36;
            const HBRUSH trackBrush = CreateSolidBrush(palette.meterTrack);
            const HBRUSH accentBrush = CreateSolidBrush(accent);
            for (std::size_t gaugeIndex = 0; gaugeIndex < visual.gauges.size(); ++gaugeIndex)
            {
                const auto& gauge = visual.gauges[gaugeIndex];
                const int top = gaugeTop + static_cast<int>(gaugeIndex) * gaugeHeight;
                if (top + gaugeHeight > visual.rect.bottom - 8 || gaugeLeft >= gaugeRight)
                {
                    break;
                }

                if (!compact)
                {
                    constexpr int valueWidth = 82;
                    DrawGdiText(hdc,
                                bodyFont,
                                gauge.label,
                                RECT{gaugeLeft, top, gaugeRight - valueWidth, top + 22},
                                palette.mutedText,
                                palette.visualBackground,
                                DT_LEFT | DT_TOP | DT_SINGLELINE);
                    DrawGdiText(hdc,
                                bodyFont,
                                gauge.value,
                                RECT{gaugeRight - valueWidth, top, gaugeRight, top + 22},
                                palette.headingText,
                                palette.visualBackground,
                                DT_RIGHT | DT_TOP | DT_SINGLELINE);
                }

                const RECT trackRect{gaugeLeft,
                                     top + (compact ? 8 : 25),
                                     gaugeRight,
                                     top + (compact ? 14 : 31)};
                SelectObject(hdc, trackBrush);
                RoundRect(hdc, trackRect.left, trackRect.top, trackRect.right, trackRect.bottom, 6, 6);
                const int fillWidth = static_cast<int>(std::lround(static_cast<float>(trackRect.right - trackRect.left)
                                                                    * ClampFraction(gauge.fraction)));
                if (fillWidth > 0)
                {
                    SelectObject(hdc, accentBrush);
                    RoundRect(hdc, trackRect.left, trackRect.top, trackRect.left + fillWidth, trackRect.bottom, 6, 6);
                }
            }
            DeleteObject(accentBrush);
            DeleteObject(trackBrush);
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

        PaintVisualD2D(renderTarget, state.visual, palette, bodyFormat, headingFormat);
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

        PaintVisualGdi(hdc, state.visual, palette, bodyFont, headingFont);
    }
}
