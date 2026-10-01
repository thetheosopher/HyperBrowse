#pragma once

#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <string>
#include <string_view>
#include <sstream>

#include "services/WicCodecReadinessService.h"

namespace hyperbrowse::decode::wic_support
{
    inline const wchar_t* HResultName(HRESULT result) noexcept
    {
        switch (result)
        {
        case WINCODEC_ERR_UNKNOWNIMAGEFORMAT:
            return L"WINCODEC_ERR_UNKNOWNIMAGEFORMAT";
        case WINCODEC_ERR_BADIMAGE:
            return L"WINCODEC_ERR_BADIMAGE";
        case WINCODEC_ERR_BADHEADER:
            return L"WINCODEC_ERR_BADHEADER";
        case WINCODEC_ERR_BADSTREAMDATA:
            return L"WINCODEC_ERR_BADSTREAMDATA";
        case WINCODEC_ERR_FRAMEMISSING:
            return L"WINCODEC_ERR_FRAMEMISSING";
        case WINCODEC_ERR_PROPERTYUNEXPECTEDTYPE:
            return L"WINCODEC_ERR_PROPERTYUNEXPECTEDTYPE";
        default:
            return nullptr;
        }
    }

    inline void SetError(std::wstring* errorMessage,
                         std::wstring_view operation,
                         HRESULT result)
    {
        if (!errorMessage)
        {
            return;
        }

        std::wostringstream stream;
        stream << operation
               << L" (HRESULT 0x"
               << std::uppercase << std::hex << std::setw(8) << std::setfill(L'0')
               << static_cast<unsigned long>(result)
               << L")";
        if (const wchar_t* name = HResultName(result))
        {
            stream << L" [" << name << L"]";
        }
        *errorMessage = stream.str();
    }

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
                SetError(errorMessage, initializationErrorMessage, result);
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
            SetError(errorMessage, L"Failed to create the WIC imaging factory.", result);
            return false;
        }

        return true;
    }

    inline bool DecoderListsExtension(std::wstring_view extensions,
                                      std::wstring_view extension) noexcept
    {
        if (extensions.size() > 4096 || extension.size() > 64)
        {
            return false;
        }

        const auto normalize = [](std::wstring_view value)
        {
            const auto first = value.find_first_not_of(L" \t\r\n");
            if (first == std::wstring_view::npos)
            {
                return std::wstring_view{};
            }
            value = value.substr(first, value.find_last_not_of(L" \t\r\n") - first + 1);
            if (value.front() == L'.')
            {
                value.remove_prefix(1);
            }
            return value;
        };

        extension = normalize(extension);
        if (extension.empty())
        {
            return false;
        }

        while (!extensions.empty())
        {
            const auto separator = extensions.find_first_of(L",;");
            const auto candidate = normalize(extensions.substr(0, separator));
            if (candidate.size() == extension.size()
                && CompareStringOrdinal(candidate.data(), static_cast<int>(candidate.size()),
                                        extension.data(), static_cast<int>(extension.size()), TRUE) == CSTR_EQUAL)
            {
                return true;
            }
            if (separator == std::wstring_view::npos)
            {
                break;
            }
            extensions.remove_prefix(separator + 1);
        }
        return false;
    }

    inline bool DecoderMatchesExtension(IWICBitmapDecoder* decoder, std::wstring_view extension)
    {
        Microsoft::WRL::ComPtr<IWICBitmapDecoderInfo> information;
        GUID container{};
        GUID registeredContainer{};
        if (!decoder || FAILED(decoder->GetDecoderInfo(&information)) || !information
            || FAILED(decoder->GetContainerFormat(&container))
            || FAILED(information->GetContainerFormat(&registeredContainer)) || container != registeredContainer)
        {
            return false;
        }
        std::array<wchar_t, 4096> extensions{};
        UINT actual = 0;
        if (FAILED(information->GetFileExtensions(static_cast<UINT>(extensions.size()), extensions.data(), &actual))
            || actual == 0 || actual > extensions.size() || extensions[actual - 1] != L'\0')
        {
            return false;
        }
        const std::wstring_view listed(extensions.data(), actual - 1);
        return DecoderListsExtension(listed, extension)
            || (DecoderListsExtension(L".heic", extension) && DecoderListsExtension(listed, L"heif"));
    }

    class OptionalCodecDecodeObserver
    {
    public:
        OptionalCodecDecodeObserver(std::wstring_view fileType, services::WicCodecDecodeKind kind)
            : fileType_(fileType)
            , kind_(kind)
            , optional_(DecoderListsExtension(L".heic,.jxl", fileType))
        {
        }

        ~OptionalCodecDecodeObserver()
        {
            if (optional_ && !completed_)
            {
                Record(false);
            }
        }

        OptionalCodecDecodeObserver(const OptionalCodecDecodeObserver&) = delete;
        OptionalCodecDecodeObserver& operator=(const OptionalCodecDecodeObserver&) = delete;

        void Complete(IWICBitmapDecoder* decoder, bool succeeded) noexcept
        {
            if (!optional_ || completed_)
            {
                return;
            }
            completed_ = true;
            try
            {
                if (!succeeded || DecoderMatchesExtension(decoder, fileType_))
                {
                    Record(succeeded);
                }
            }
            catch (...)
            {
            }
        }

    private:
        void Record(bool succeeded) noexcept
        {
            try
            {
                services::GetWicCodecReadinessService().RecordDecode(fileType_, kind_, succeeded);
            }
            catch (...)
            {
            }
        }

        std::wstring_view fileType_;
        services::WicCodecDecodeKind kind_;
        bool optional_{};
        bool completed_{};
    };

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

    inline HBITMAP CreateBitmapBuffer(UINT width, UINT height, void** bits, HRESULT* result = nullptr)
    {
        if (!bits || width == 0 || height == 0)
        {
            if (result)
            {
                *result = E_INVALIDARG;
            }
            return nullptr;
        }

        *bits = nullptr;
        BITMAPINFO bitmapInfo{};
        bitmapInfo.bmiHeader.biSize = sizeof(bitmapInfo.bmiHeader);
        bitmapInfo.bmiHeader.biWidth = static_cast<LONG>(width);
        bitmapInfo.bmiHeader.biHeight = -static_cast<LONG>(height);
        bitmapInfo.bmiHeader.biPlanes = 1;
        bitmapInfo.bmiHeader.biBitCount = 32;
        bitmapInfo.bmiHeader.biCompression = BI_RGB;
        HBITMAP bitmap = CreateDIBSection(nullptr, &bitmapInfo, DIB_RGB_COLORS, bits, nullptr, 0);
        if (!bitmap)
        {
            if (result)
            {
                const DWORD lastError = GetLastError();
                *result = lastError != ERROR_SUCCESS ? HRESULT_FROM_WIN32(lastError) : E_FAIL;
            }
            return nullptr;
        }

        if (result)
        {
            *result = bits && *bits ? S_OK : E_POINTER;
        }
        return bitmap;
    }
}
