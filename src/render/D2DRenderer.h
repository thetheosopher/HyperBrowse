#pragma once

#include <windows.h>
#include <d2d1.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <memory>
#include <mutex>
#include <string>

namespace hyperbrowse::cache
{
    class CachedThumbnail;
}

namespace hyperbrowse::render
{
    using Microsoft::WRL::ComPtr;

    class D2DRenderer
    {
    public:
        static D2DRenderer& Instance();

        bool IsAvailable() const noexcept;
        ID2D1Factory* D2DFactory() const noexcept;
        IDWriteFactory* DWriteFactory() const noexcept;

        ComPtr<ID2D1HwndRenderTarget> CreateHwndRenderTarget(HWND hwnd);
        void ResizeRenderTarget(ID2D1HwndRenderTarget* renderTarget, HWND hwnd);

        ComPtr<ID2D1Bitmap> CreateBitmapFromHBITMAP(
            ID2D1RenderTarget* renderTarget,
            HBITMAP hbitmap,
            int width,
            int height);

        ComPtr<ID2D1Bitmap> CreateBitmapFromCachedThumbnail(
            ID2D1RenderTarget* renderTarget,
            const cache::CachedThumbnail& thumbnail);

        ComPtr<IDWriteTextFormat> CreateTextFormat(
            const wchar_t* fontFamily,
            float fontSize,
            DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE style = DWRITE_FONT_STYLE_NORMAL);

    private:
        D2DRenderer();

        bool Initialize();

        ComPtr<ID2D1Factory> d2dFactory_;
        ComPtr<IDWriteFactory> dwriteFactory_;
        bool available_{};
        std::once_flag initFlag_;
    };

    inline D2D1_COLOR_F ToD2DColor(COLORREF color, float alpha = 1.0f)
    {
        return D2D1::ColorF(
            GetRValue(color) / 255.0f,
            GetGValue(color) / 255.0f,
            GetBValue(color) / 255.0f,
            alpha);
    }

    inline D2D1_RECT_F ToD2DRect(const RECT& rect)
    {
        return D2D1::RectF(
            static_cast<float>(rect.left),
            static_cast<float>(rect.top),
            static_cast<float>(rect.right),
            static_cast<float>(rect.bottom));
    }

    inline D2D1_ROUNDED_RECT ToD2DRoundedRect(const RECT& rect, float radiusX, float radiusY)
    {
        return D2D1::RoundedRect(ToD2DRect(rect), radiusX, radiusY);
    }

    inline D2D1_POINT_2F ToD2DPoint(float x, float y)
    {
        return D2D1::Point2F(x, y);
    }

    // Draw a bitmap with the highest-quality interpolation supported by the runtime.
    // On Windows 8+ this uses ID2D1DeviceContext::DrawBitmap with
    // D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC, which preserves significantly more
    // detail than the older linear mode when scaling images. Falls back to linear if
    // ID2D1DeviceContext is unavailable on the target.
    //
    // sourceRectangle may be nullptr to draw the entire bitmap.
    inline void DrawBitmapHighQuality(ID2D1RenderTarget* renderTarget,
                                      ID2D1Bitmap* bitmap,
                                      const D2D1_RECT_F& destinationRectangle,
                                      float opacity = 1.0f,
                                      const D2D1_RECT_F* sourceRectangle = nullptr)
    {
        if (!renderTarget || !bitmap)
        {
            return;
        }

        ComPtr<ID2D1DeviceContext> deviceContext;
        if (SUCCEEDED(renderTarget->QueryInterface(__uuidof(ID2D1DeviceContext),
                                                   reinterpret_cast<void**>(deviceContext.GetAddressOf()))))
        {
            deviceContext->DrawBitmap(
                bitmap,
                &destinationRectangle,
                opacity,
                D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC,
                sourceRectangle,
                nullptr);
            return;
        }

        renderTarget->DrawBitmap(
            bitmap,
            &destinationRectangle,
            opacity,
            D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
            sourceRectangle);
    }
}
