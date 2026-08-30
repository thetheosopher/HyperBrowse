#include "decode/WicThumbnailDecoder.h"

#include <wincodec.h>
#include <wrl/client.h>

#include "decode/WicDecodeHelpers.h"

namespace hyperbrowse::decode
{
    std::shared_ptr<const cache::CachedThumbnail> WicThumbnailDecoder::Decode(const cache::ThumbnailCacheKey& key,
                                                                               std::wstring* errorMessage) const
    {
        using Microsoft::WRL::ComPtr;
        namespace wic = hyperbrowse::decode::wic_support;

        std::wstring localErrorMessage;
        std::wstring* decodeError = errorMessage ? errorMessage : &localErrorMessage;
        wic::ComInitializationScope comInitialization(
            COINIT_MULTITHREADED,
            decodeError,
            L"Failed to initialize COM for image decode.");
        if (!comInitialization.Succeeded())
        {
            return {};
        }

        ComPtr<IWICImagingFactory> factory;
        if (!wic::InitializeWicFactory(&factory, decodeError))
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
            wic::SetError(decodeError, L"Failed to open the image with the WIC decoder.", result);
            return {};
        }

        ComPtr<IWICBitmapFrameDecode> frame;
        result = decoder->GetFrame(0, &frame);
        if (FAILED(result))
        {
            wic::SetError(decodeError, L"Failed to read the first image frame.", result);
            return {};
        }

        UINT sourceWidth = 0;
        UINT sourceHeight = 0;
        result = frame->GetSize(&sourceWidth, &sourceHeight);
        if (FAILED(result) || sourceWidth == 0 || sourceHeight == 0)
        {
            wic::SetError(decodeError,
                          sourceWidth == 0 || sourceHeight == 0
                              ? L"The selected image does not report valid dimensions."
                              : L"Failed to read the image dimensions.",
                          result);
            return {};
        }

        const WICBitmapTransformOptions transform = wic::OrientationToTransform(wic::ReadOrientation(frame.Get()));
        UINT orientedWidth = sourceWidth;
        UINT orientedHeight = sourceHeight;
        if (wic::TransformSwapsDimensions(transform))
        {
            std::swap(orientedWidth, orientedHeight);
        }

        UINT scaledWidth = 0;
        UINT scaledHeight = 0;
        wic::ComputeScaledSize(orientedWidth, orientedHeight, key.targetWidth, key.targetHeight, &scaledWidth, &scaledHeight);

        ComPtr<IWICBitmapSource> source = frame;
        const bool swapsDimensions = wic::TransformSwapsDimensions(transform);
        if (transform != WICBitmapTransformRotate0)
        {
            if (swapsDimensions && (scaledWidth != orientedWidth || scaledHeight != orientedHeight))
            {
                ComPtr<IWICBitmapScaler> preRotationScaler;
                result = factory->CreateBitmapScaler(&preRotationScaler);
                if (FAILED(result) || FAILED(preRotationScaler->Initialize(
                    frame.Get(),
                    scaledHeight,
                    scaledWidth,
                    WICBitmapInterpolationModeFant)))
                {
                    wic::SetError(decodeError, L"Failed to scale the decoded image.", result);
                    return {};
                }

                source = preRotationScaler;
            }
            else if (swapsDimensions)
            {
                ComPtr<IWICBitmap> cachedFrame;
                result = factory->CreateBitmapFromSource(frame.Get(), WICBitmapCacheOnLoad, &cachedFrame);
                if (FAILED(result))
                {
                    wic::SetError(decodeError, L"Failed to apply image orientation.", result);
                    return {};
                }

                source = cachedFrame;
            }

            ComPtr<IWICBitmapFlipRotator> rotator;
            result = factory->CreateBitmapFlipRotator(&rotator);
            if (FAILED(result) || FAILED(rotator->Initialize(source.Get(), transform)))
            {
                wic::SetError(decodeError, L"Failed to apply image orientation.", result);
                return {};
            }

            source = rotator;
        }

        if (!swapsDimensions && (scaledWidth != orientedWidth || scaledHeight != orientedHeight))
        {
            ComPtr<IWICBitmapScaler> scaler;
            result = factory->CreateBitmapScaler(&scaler);
            if (FAILED(result) || FAILED(scaler->Initialize(source.Get(), scaledWidth, scaledHeight, WICBitmapInterpolationModeFant)))
            {
                wic::SetError(decodeError, L"Failed to scale the decoded image.", result);
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
            wic::SetError(decodeError, L"Failed to convert the decoded image into the viewer pixel format.", result);
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
            wic::SetError(decodeError, L"Failed to allocate the destination bitmap.", E_OUTOFMEMORY);
            return {};
        }

        const UINT stride = scaledWidth * 4;
        const UINT bufferSize = stride * scaledHeight;
        result = converter->CopyPixels(nullptr, stride, bufferSize, static_cast<BYTE*>(bits));
        if (FAILED(result))
        {
            DeleteObject(bitmap);
            wic::SetError(decodeError, L"Failed to copy decoded pixels into the destination bitmap.", result);
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