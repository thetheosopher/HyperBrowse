#pragma once

#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

namespace hyperbrowse::decode::wic_support
{
    class ComInitializationScope
    {
    public:
        ComInitializationScope(DWORD coinitFlags,
                               std::wstring* errorMessage,
                               std::wstring_view initializationErrorMessage)
        {
            const HRESULT result = CoInitializeEx(nullptr, coinitFlags);
            shouldUninitialize_ = SUCCEEDED(result) || result == S_FALSE;
            succeeded_ = SUCCEEDED(result) || result == S_FALSE || result == RPC_E_CHANGED_MODE;
            if (!succeeded_ && errorMessage)
            {
                *errorMessage = std::wstring(initializationErrorMessage);
            }
        }

        ~ComInitializationScope()
        {
            if (shouldUninitialize_)
            {
                CoUninitialize();
            }
        }

        bool Succeeded() const noexcept
        {
            return succeeded_;
        }

    private:
        bool shouldUninitialize_{};
        bool succeeded_{};
    };

    inline bool InitializeWicFactory(Microsoft::WRL::ComPtr<IWICImagingFactory>* factory,
                                     std::wstring* errorMessage)
    {
        const HRESULT result = CoCreateInstance(
            CLSID_WICImagingFactory,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(factory->GetAddressOf()));
        if (FAILED(result))
        {
            if (errorMessage)
            {
                *errorMessage = L"Failed to create the WIC imaging factory.";
            }
            return false;
        }

        return true;
    }

    inline WICBitmapTransformOptions OrientationToTransform(std::uint16_t orientation)
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

    inline std::uint16_t ReadOrientation(IWICBitmapFrameDecode* frame)
    {
        Microsoft::WRL::ComPtr<IWICMetadataQueryReader> metadataQueryReader;
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

    inline bool TransformSwapsDimensions(WICBitmapTransformOptions transform)
    {
        return transform == WICBitmapTransformRotate90
            || transform == WICBitmapTransformRotate270
            || transform == static_cast<WICBitmapTransformOptions>(WICBitmapTransformRotate90 | WICBitmapTransformFlipHorizontal)
            || transform == static_cast<WICBitmapTransformOptions>(WICBitmapTransformRotate270 | WICBitmapTransformFlipHorizontal);
    }

    inline void ComputeScaledSize(UINT sourceWidth,
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

    inline HBITMAP CreateBitmapBuffer(UINT width, UINT height, void** bits)
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