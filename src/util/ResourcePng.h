#pragma once

#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <memory>
#include <string>

#include "cache/ThumbnailCache.h"
#include "decode/WicDecodeHelpers.h"

namespace hyperbrowse::util
{
    inline std::shared_ptr<const cache::CachedThumbnail> LoadPngResourceBitmap(HINSTANCE instance,
                                                                                int resourceId,
                                                                                int targetWidth,
                                                                                int targetHeight,
                                                                                std::wstring* errorMessage = nullptr)
    {
        namespace wic = hyperbrowse::decode::wic_support;
        using Microsoft::WRL::ComPtr;

        wic::ComInitializationScope comInitialization(
            COINIT_APARTMENTTHREADED,
            errorMessage,
            L"Failed to initialize COM for placeholder art decoding.");
        if (!comInitialization.Succeeded())
        {
            return {};
        }

        ComPtr<IWICImagingFactory> factory;
        if (!wic::InitializeWicFactory(&factory, errorMessage))
        {
            return {};
        }

        HRSRC resourceHandle = FindResourceW(instance, MAKEINTRESOURCEW(resourceId), RT_RCDATA);
        if (!resourceHandle)
        {
            if (errorMessage)
            {
                *errorMessage = L"Failed to locate the placeholder PNG resource.";
            }
            return {};
        }

        HGLOBAL loadedResource = LoadResource(instance, resourceHandle);
        if (!loadedResource)
        {
            if (errorMessage)
            {
                *errorMessage = L"Failed to load the placeholder PNG resource.";
            }
            return {};
        }

        const DWORD resourceSize = SizeofResource(instance, resourceHandle);
        void* resourceBytes = LockResource(loadedResource);
        if (!resourceBytes || resourceSize == 0)
        {
            if (errorMessage)
            {
                *errorMessage = L"The placeholder PNG resource is empty.";
            }
            return {};
        }

        ComPtr<IWICStream> stream;
        HRESULT result = factory->CreateStream(&stream);
        if (FAILED(result)
            || FAILED(stream->InitializeFromMemory(static_cast<BYTE*>(resourceBytes), resourceSize)))
        {
            if (errorMessage)
            {
                *errorMessage = L"Failed to initialize the WIC stream for placeholder art.";
            }
            return {};
        }

        ComPtr<IWICBitmapDecoder> decoder;
        result = factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &decoder);
        if (FAILED(result))
        {
            if (errorMessage)
            {
                *errorMessage = L"Failed to create the WIC decoder for placeholder art.";
            }
            return {};
        }

        ComPtr<IWICBitmapFrameDecode> frame;
        result = decoder->GetFrame(0, &frame);
        if (FAILED(result))
        {
            if (errorMessage)
            {
                *errorMessage = L"Failed to read the placeholder PNG frame.";
            }
            return {};
        }

        UINT sourceWidth = 0;
        UINT sourceHeight = 0;
        result = frame->GetSize(&sourceWidth, &sourceHeight);
        if (FAILED(result) || sourceWidth == 0 || sourceHeight == 0)
        {
            if (errorMessage)
            {
                *errorMessage = L"The placeholder PNG frame has invalid dimensions.";
            }
            return {};
        }

        UINT scaledWidth = sourceWidth;
        UINT scaledHeight = sourceHeight;
        ComPtr<IWICBitmapSource> source = frame;
        if (targetWidth > 0 && targetHeight > 0)
        {
            wic::ComputeScaledSize(sourceWidth, sourceHeight, targetWidth, targetHeight, &scaledWidth, &scaledHeight);
            if (scaledWidth != sourceWidth || scaledHeight != sourceHeight)
            {
                ComPtr<IWICBitmapScaler> scaler;
                result = factory->CreateBitmapScaler(&scaler);
                if (FAILED(result)
                    || FAILED(scaler->Initialize(source.Get(), scaledWidth, scaledHeight, WICBitmapInterpolationModeFant)))
                {
                    if (errorMessage)
                    {
                        *errorMessage = L"Failed to scale the placeholder PNG.";
                    }
                    return {};
                }
                source = scaler;
            }
        }

        ComPtr<IWICFormatConverter> converter;
        result = factory->CreateFormatConverter(&converter);
        if (FAILED(result)
            || FAILED(converter->Initialize(source.Get(),
                                            GUID_WICPixelFormat32bppPBGRA,
                                            WICBitmapDitherTypeNone,
                                            nullptr,
                                            0.0,
                                            WICBitmapPaletteTypeCustom)))
        {
            if (errorMessage)
            {
                *errorMessage = L"Failed to convert the placeholder PNG to BGRA.";
            }
            return {};
        }

        void* bits = nullptr;
        HBITMAP bitmap = wic::CreateBitmapBuffer(scaledWidth, scaledHeight, &bits);
        if (!bitmap || !bits)
        {
            if (errorMessage)
            {
                *errorMessage = L"Failed to allocate the placeholder bitmap buffer.";
            }
            if (bitmap)
            {
                DeleteObject(bitmap);
            }
            return {};
        }

        const UINT stride = scaledWidth * 4;
        const UINT bufferSize = stride * scaledHeight;
        result = converter->CopyPixels(nullptr, stride, bufferSize, static_cast<BYTE*>(bits));
        if (FAILED(result))
        {
            DeleteObject(bitmap);
            if (errorMessage)
            {
                *errorMessage = L"Failed to copy the placeholder PNG pixels.";
            }
            return {};
        }

        return std::make_shared<const cache::CachedThumbnail>(bitmap,
                                                               static_cast<int>(scaledWidth),
                                                               static_cast<int>(scaledHeight),
                                                               static_cast<std::size_t>(bufferSize),
                                                               static_cast<int>(sourceWidth),
                                                               static_cast<int>(sourceHeight));
    }

    inline void DrawBitmapWithAlpha(HDC destinationDc,
                                    const cache::CachedThumbnail& bitmap,
                                    int x,
                                    int y,
                                    int width = 0,
                                    int height = 0)
    {
        HDC bitmapDc = CreateCompatibleDC(destinationDc);
        if (!bitmapDc)
        {
            return;
        }

        HGDIOBJ oldBitmap = SelectObject(bitmapDc, bitmap.Bitmap());
        const int drawWidth = width > 0 ? width : bitmap.Width();
        const int drawHeight = height > 0 ? height : bitmap.Height();

        BLENDFUNCTION blend{};
        blend.BlendOp = AC_SRC_OVER;
        blend.SourceConstantAlpha = 255;
        blend.AlphaFormat = AC_SRC_ALPHA;
        AlphaBlend(destinationDc,
                   x,
                   y,
                   drawWidth,
                   drawHeight,
                   bitmapDc,
                   0,
                   0,
                   bitmap.Width(),
                   bitmap.Height(),
                   blend);

        SelectObject(bitmapDc, oldBitmap);
        DeleteDC(bitmapDc);
    }
}