#include "render/D2DRenderer.h"

#include <algorithm>
#include <d2d1.h>
#include <dwrite.h>
#include <vector>

#include "cache/ThumbnailCache.h"

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

namespace hyperbrowse::render
{
    D2DRenderer& D2DRenderer::Instance()
    {
        static D2DRenderer instance;
        return instance;
    }

    D2DRenderer::D2DRenderer()
    {
        std::call_once(initFlag_, [this]() { available_ = Initialize(); });
    }

    bool D2DRenderer::Initialize()
    {
        HRESULT hr = D2D1CreateFactory(
            D2D1_FACTORY_TYPE_SINGLE_THREADED,
            d2dFactory_.GetAddressOf());
        if (FAILED(hr))
        {
            return false;
        }

        hr = DWriteCreateFactory(
            DWRITE_FACTORY_TYPE_SHARED,
            __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(dwriteFactory_.GetAddressOf()));
        if (FAILED(hr))
        {
            return false;
        }

        return true;
    }

    bool D2DRenderer::IsAvailable() const noexcept
    {
        return available_;
    }

    ID2D1Factory* D2DRenderer::D2DFactory() const noexcept
    {
        return d2dFactory_.Get();
    }

    IDWriteFactory* D2DRenderer::DWriteFactory() const noexcept
    {
        return dwriteFactory_.Get();
    }

    ComPtr<ID2D1HwndRenderTarget> D2DRenderer::CreateHwndRenderTarget(HWND hwnd)
    {
        if (!d2dFactory_ || !hwnd)
        {
            return {};
        }

        RECT clientRect{};
        GetClientRect(hwnd, &clientRect);

        const D2D1_SIZE_U size = D2D1::SizeU(
            static_cast<UINT>(std::max(1L, clientRect.right - clientRect.left)),
            static_cast<UINT>(std::max(1L, clientRect.bottom - clientRect.top)));

        D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties();
        rtProps.type = D2D1_RENDER_TARGET_TYPE_DEFAULT;
        rtProps.dpiX = 96.0f;
        rtProps.dpiY = 96.0f;

        D2D1_HWND_RENDER_TARGET_PROPERTIES hwndRtProps = D2D1::HwndRenderTargetProperties(hwnd, size);
        hwndRtProps.presentOptions = D2D1_PRESENT_OPTIONS_NONE;

        ComPtr<ID2D1HwndRenderTarget> renderTarget;
        const HRESULT hr = d2dFactory_->CreateHwndRenderTarget(
            rtProps,
            hwndRtProps,
            renderTarget.GetAddressOf());
        if (FAILED(hr))
        {
            return {};
        }

        renderTarget->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        renderTarget->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);

        return renderTarget;
    }

    void D2DRenderer::ResizeRenderTarget(ID2D1HwndRenderTarget* renderTarget, HWND hwnd)
    {
        if (!renderTarget || !hwnd)
        {
            return;
        }

        RECT clientRect{};
        GetClientRect(hwnd, &clientRect);

        const D2D1_SIZE_U size = D2D1::SizeU(
            static_cast<UINT>(std::max(1L, clientRect.right - clientRect.left)),
            static_cast<UINT>(std::max(1L, clientRect.bottom - clientRect.top)));

        const D2D1_SIZE_U currentSize = renderTarget->GetPixelSize();
        if (currentSize.width != size.width || currentSize.height != size.height)
        {
            renderTarget->Resize(size);
        }
    }

    ComPtr<ID2D1Bitmap> D2DRenderer::CreateBitmapFromHBITMAP(
        ID2D1RenderTarget* renderTarget,
        HBITMAP hbitmap,
        int width,
        int height)
    {
        if (!renderTarget || !hbitmap || width <= 0 || height <= 0)
        {
            return {};
        }

        D2D1_BITMAP_PROPERTIES bitmapProps = D2D1::BitmapProperties(
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));

        DIBSECTION dibSection{};
        if (GetObjectW(hbitmap, sizeof(dibSection), &dibSection) == sizeof(dibSection)
            && dibSection.dsBm.bmBits != nullptr
            && dibSection.dsBm.bmBitsPixel == 32)
        {
            ComPtr<ID2D1Bitmap> bitmap;
            const HRESULT hr = renderTarget->CreateBitmap(
                D2D1::SizeU(static_cast<UINT>(width), static_cast<UINT>(height)),
                dibSection.dsBm.bmBits,
                static_cast<UINT>(dibSection.dsBm.bmWidthBytes),
                bitmapProps,
                bitmap.GetAddressOf());
            if (SUCCEEDED(hr))
            {
                return bitmap;
            }
        }

        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = width;
        bmi.bmiHeader.biHeight = -height;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        const std::size_t bufferSize = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;
        std::vector<BYTE> pixels(bufferSize);

        HDC screenDc = GetDC(nullptr);
        if (!screenDc)
        {
            return {};
        }

        const int result = GetDIBits(screenDc,
                                     hbitmap,
                                     0,
                                     static_cast<UINT>(height),
                                     pixels.data(),
                                     &bmi,
                                     DIB_RGB_COLORS);
        ReleaseDC(nullptr, screenDc);

        if (result == 0)
        {
            return {};
        }

        ComPtr<ID2D1Bitmap> bitmap;
        const HRESULT hr = renderTarget->CreateBitmap(
            D2D1::SizeU(static_cast<UINT>(width), static_cast<UINT>(height)),
            pixels.data(),
            static_cast<UINT>(width * 4),
            bitmapProps,
            bitmap.GetAddressOf());
        if (FAILED(hr))
        {
            return {};
        }

        return bitmap;
    }

    ComPtr<ID2D1Bitmap> D2DRenderer::CreateBitmapFromCachedThumbnail(
        ID2D1RenderTarget* renderTarget,
        const cache::CachedThumbnail& thumbnail)
    {
        return CreateBitmapFromHBITMAP(
            renderTarget,
            thumbnail.Bitmap(),
            thumbnail.Width(),
            thumbnail.Height());
    }

    ComPtr<IDWriteTextFormat> D2DRenderer::CreateTextFormat(
        const wchar_t* fontFamily,
        float fontSize,
        DWRITE_FONT_WEIGHT weight,
        DWRITE_FONT_STYLE style)
    {
        if (!dwriteFactory_)
        {
            return {};
        }

        ComPtr<IDWriteTextFormat> textFormat;
        const HRESULT hr = dwriteFactory_->CreateTextFormat(
            fontFamily,
            nullptr,
            weight,
            style,
            DWRITE_FONT_STRETCH_NORMAL,
            fontSize,
            L"en-us",
            textFormat.GetAddressOf());
        if (FAILED(hr))
        {
            return {};
        }

        textFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        return textFormat;
    }
}
