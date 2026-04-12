#include "decode/WicThumbnailDecoder.h"

#include <wincodec.h>
#include <wrl/client.h>

#include "decode/WicDecodeHelpers.h"

namespace hyperbrowse::decode
{
    std::shared_ptr<const cache::CachedThumbnail> WicThumbnailDecoder::Decode(const cache::ThumbnailCacheKey& key) const
    {
        using Microsoft::WRL::ComPtr;
        namespace wic = hyperbrowse::decode::wic_support;

        std::wstring errorMessage;
        wic::ComInitializationScope comInitialization(
            COINIT_MULTITHREADED,
            &errorMessage,
            L"Failed to initialize COM for image decode.");
        if (!comInitialization.Succeeded())
        {
            return {};
        }

        ComPtr<IWICImagingFactory> factory;
        if (!wic::InitializeWicFactory(&factory, &errorMessage))
        {
            return {};
        }

        ComPtr<IWICBitmapDecoder> decoder;
        HRESULT result = factory->CreateDecoderFromFilename(
            key.filePath.c_str(),
            nullptr,
            GENERIC_READ,
            WICDecodeMetadataCacheOnLoad,
            &decoder);
        if (FAILED(result))
        {
            return {};
        }

        ComPtr<IWICBitmapFrameDecode> frame;
        result = decoder->GetFrame(0, &frame);
        if (FAILED(result))
        {
            return {};
        }

        UINT sourceWidth = 0;
        UINT sourceHeight = 0;
        result = frame->GetSize(&sourceWidth, &sourceHeight);
        if (FAILED(result) || sourceWidth == 0 || sourceHeight == 0)
        {
            return {};
        }

        const WICBitmapTransformOptions transform = wic::OrientationToTransform(wic::ReadOrientation(frame.Get()));
        UINT orientedWidth = sourceWidth;
        UINT orientedHeight = sourceHeight;
        if (wic::TransformSwapsDimensions(transform))
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
                return {};
            }

            source = rotator;
        }

        UINT scaledWidth = 0;
        UINT scaledHeight = 0;
        wic::ComputeScaledSize(orientedWidth, orientedHeight, key.targetWidth, key.targetHeight, &scaledWidth, &scaledHeight);

        if (scaledWidth != orientedWidth || scaledHeight != orientedHeight)
        {
            ComPtr<IWICBitmapScaler> scaler;
            result = factory->CreateBitmapScaler(&scaler);
            if (FAILED(result) || FAILED(scaler->Initialize(source.Get(), scaledWidth, scaledHeight, WICBitmapInterpolationModeFant)))
            {
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
            return {};
        }

        void* bits = nullptr;
        HBITMAP bitmap = wic::CreateBitmapBuffer(scaledWidth, scaledHeight, &bits);
        if (!bitmap || !bits)
        {
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
            return {};
        }

        return std::make_shared<cache::CachedThumbnail>(bitmap,
                                                        static_cast<int>(scaledWidth),
                                                        static_cast<int>(scaledHeight),
                                                        bufferSize,
                                                        static_cast<int>(orientedWidth),
                                                        static_cast<int>(orientedHeight));
    }
}