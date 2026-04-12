#include "decode/WicThumbnailDecoder.h"

#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>

namespace
{
    using Microsoft::WRL::ComPtr;

    WICBitmapTransformOptions OrientationToTransform(std::uint16_t orientation)
    {
        switch (orientation)
        {
        case 2:
            return WICBitmapTransformFlipHorizontal;
        case 3:
            return WICBitmapTransformRotate180;
        case 4:
            return WICBitmapTransformFlipVertical;
        case 5:
            return static_cast<WICBitmapTransformOptions>(WICBitmapTransformRotate90 | WICBitmapTransformFlipHorizontal);
        case 6:
            return WICBitmapTransformRotate90;
        case 7:
            return static_cast<WICBitmapTransformOptions>(WICBitmapTransformRotate270 | WICBitmapTransformFlipHorizontal);
        case 8:
            return WICBitmapTransformRotate270;
        case 1:
        default:
            return WICBitmapTransformRotate0;
        }
    }

    std::uint16_t ReadOrientation(IWICBitmapFrameDecode* frame)
    {
        ComPtr<IWICMetadataQueryReader> metadataQueryReader;
        if (FAILED(frame->GetMetadataQueryReader(&metadataQueryReader)) || !metadataQueryReader)
        {
            return 1;
        }

        static constexpr const wchar_t* kOrientationQueries[] = {
            L"/app1/ifd/{ushort=274}",
            L"/ifd/{ushort=274}",
        };

        for (const wchar_t* query : kOrientationQueries)
        {
            PROPVARIANT value;
            PropVariantInit(&value);
            const HRESULT queryResult = metadataQueryReader->GetMetadataByName(query, &value);
            if (SUCCEEDED(queryResult))
            {
                std::uint16_t orientation = 1;
                switch (value.vt)
                {
                case VT_UI1:
                    orientation = value.bVal;
                    break;
                case VT_UI2:
                    orientation = value.uiVal;
                    break;
                case VT_UI4:
                    orientation = static_cast<std::uint16_t>(value.ulVal);
                    break;
                default:
                    break;
                }

                PropVariantClear(&value);
                return orientation;
            }

            PropVariantClear(&value);
        }

        return 1;
    }

    bool TransformSwapsDimensions(WICBitmapTransformOptions transform)
    {
        return transform == WICBitmapTransformRotate90
            || transform == WICBitmapTransformRotate270
            || transform == static_cast<WICBitmapTransformOptions>(WICBitmapTransformRotate90 | WICBitmapTransformFlipHorizontal)
            || transform == static_cast<WICBitmapTransformOptions>(WICBitmapTransformRotate270 | WICBitmapTransformFlipHorizontal);
    }

    void ComputeScaledSize(UINT sourceWidth,
                           UINT sourceHeight,
                           int targetWidth,
                           int targetHeight,
                           UINT* scaledWidth,
                           UINT* scaledHeight)
    {
        const double widthRatio = static_cast<double>(std::max(1, targetWidth)) / static_cast<double>(std::max<UINT>(1, sourceWidth));
        const double heightRatio = static_cast<double>(std::max(1, targetHeight)) / static_cast<double>(std::max<UINT>(1, sourceHeight));
        const double scale = std::min(widthRatio, heightRatio);

        *scaledWidth = std::max<UINT>(1, static_cast<UINT>(std::lround(static_cast<double>(sourceWidth) * scale)));
        *scaledHeight = std::max<UINT>(1, static_cast<UINT>(std::lround(static_cast<double>(sourceHeight) * scale)));
    }

    HBITMAP CreateBitmapBuffer(UINT width, UINT height, void** bits)
    {
        BITMAPINFO bitmapInfo{};
        bitmapInfo.bmiHeader.biSize = sizeof(bitmapInfo.bmiHeader);
        bitmapInfo.bmiHeader.biWidth = static_cast<LONG>(width);
        bitmapInfo.bmiHeader.biHeight = -static_cast<LONG>(height);
        bitmapInfo.bmiHeader.biPlanes = 1;
        bitmapInfo.bmiHeader.biBitCount = 32;
        bitmapInfo.bmiHeader.biCompression = BI_RGB;
        return CreateDIBSection(nullptr, &bitmapInfo, DIB_RGB_COLORS, bits, nullptr, 0);
    }
}

namespace hyperbrowse::decode
{
    std::shared_ptr<const cache::CachedThumbnail> WicThumbnailDecoder::Decode(const cache::ThumbnailCacheKey& key) const
    {
        const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        const bool shouldUninitializeCom = SUCCEEDED(comResult) || comResult == S_FALSE;
        if (FAILED(comResult) && comResult != RPC_E_CHANGED_MODE)
        {
            return {};
        }

        ComPtr<IWICImagingFactory> factory;
        HRESULT result = CoCreateInstance(
            CLSID_WICImagingFactory,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&factory));
        if (FAILED(result))
        {
            if (shouldUninitializeCom)
            {
                CoUninitialize();
            }
            return {};
        }

        ComPtr<IWICBitmapDecoder> decoder;
        result = factory->CreateDecoderFromFilename(
            key.filePath.c_str(),
            nullptr,
            GENERIC_READ,
            WICDecodeMetadataCacheOnLoad,
            &decoder);
        if (FAILED(result))
        {
            if (shouldUninitializeCom)
            {
                CoUninitialize();
            }
            return {};
        }

        ComPtr<IWICBitmapFrameDecode> frame;
        result = decoder->GetFrame(0, &frame);
        if (FAILED(result))
        {
            if (shouldUninitializeCom)
            {
                CoUninitialize();
            }
            return {};
        }

        UINT sourceWidth = 0;
        UINT sourceHeight = 0;
        result = frame->GetSize(&sourceWidth, &sourceHeight);
        if (FAILED(result) || sourceWidth == 0 || sourceHeight == 0)
        {
            if (shouldUninitializeCom)
            {
                CoUninitialize();
            }
            return {};
        }

        const WICBitmapTransformOptions transform = OrientationToTransform(ReadOrientation(frame.Get()));
        UINT orientedWidth = sourceWidth;
        UINT orientedHeight = sourceHeight;
        if (TransformSwapsDimensions(transform))
        {
            std::swap(orientedWidth, orientedHeight);
        }

        ComPtr<IWICBitmapSource> source = frame;
        if (transform != WICBitmapTransformRotate0)
        {
            ComPtr<IWICBitmapFlipRotator> rotator;
            result = factory->CreateBitmapFlipRotator(&rotator);
            if (FAILED(result) || FAILED(rotator->Initialize(frame.Get(), transform)))
            {
                if (shouldUninitializeCom)
                {
                    CoUninitialize();
                }
                return {};
            }

            source = rotator;
        }

        UINT scaledWidth = 0;
        UINT scaledHeight = 0;
        ComputeScaledSize(orientedWidth, orientedHeight, key.targetWidth, key.targetHeight, &scaledWidth, &scaledHeight);

        if (scaledWidth != orientedWidth || scaledHeight != orientedHeight)
        {
            ComPtr<IWICBitmapScaler> scaler;
            result = factory->CreateBitmapScaler(&scaler);
            if (FAILED(result) || FAILED(scaler->Initialize(source.Get(), scaledWidth, scaledHeight, WICBitmapInterpolationModeFant)))
            {
                if (shouldUninitializeCom)
                {
                    CoUninitialize();
                }
                return {};
            }

            source = scaler;
        }

        ComPtr<IWICFormatConverter> converter;
        result = factory->CreateFormatConverter(&converter);
        if (FAILED(result) || FAILED(converter->Initialize(
            source.Get(),
            GUID_WICPixelFormat32bppPBGRA,
            WICBitmapDitherTypeNone,
            nullptr,
            0.0,
            WICBitmapPaletteTypeCustom)))
        {
            if (shouldUninitializeCom)
            {
                CoUninitialize();
            }
            return {};
        }

        void* bits = nullptr;
        HBITMAP bitmap = CreateBitmapBuffer(scaledWidth, scaledHeight, &bits);
        if (!bitmap || !bits)
        {
            if (bitmap)
            {
                DeleteObject(bitmap);
            }

            if (shouldUninitializeCom)
            {
                CoUninitialize();
            }
            return {};
        }

        const UINT stride = scaledWidth * 4;
        const UINT bufferSize = stride * scaledHeight;
        result = converter->CopyPixels(nullptr, stride, bufferSize, static_cast<BYTE*>(bits));
        if (FAILED(result))
        {
            DeleteObject(bitmap);
            if (shouldUninitializeCom)
            {
                CoUninitialize();
            }
            return {};
        }

        if (shouldUninitializeCom)
        {
            CoUninitialize();
        }

        return std::make_shared<cache::CachedThumbnail>(bitmap,
                                                        static_cast<int>(scaledWidth),
                                                        static_cast<int>(scaledHeight),
                                                        bufferSize,
                                                        static_cast<int>(orientedWidth),
                                                        static_cast<int>(orientedHeight));
    }
}