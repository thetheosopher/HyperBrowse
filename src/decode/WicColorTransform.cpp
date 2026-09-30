#include "decode/WicColorTransform.h"

#include <icm.h>
#include <wrl/client.h>

#include <limits>
#include <vector>

#include "decode/WicDecodeHelpers.h"

namespace hyperbrowse::decode::color
{
    namespace
    {
        using Microsoft::WRL::ComPtr;

        class ColorProfileHandle
        {
        public:
            explicit ColorProfileHandle(HPROFILE handle) : handle_(handle) {}
            ~ColorProfileHandle() { if (handle_) CloseColorProfile(handle_); }
            HPROFILE Get() const noexcept { return handle_; }
            ColorProfileHandle(const ColorProfileHandle&) = delete;
            ColorProfileHandle& operator=(const ColorProfileHandle&) = delete;

        private:
            HPROFILE handle_{};
        };

        bool Fail(std::wstring* errorMessage, const wchar_t* message)
        {
            if (errorMessage)
            {
                *errorMessage = message;
            }
            return false;
        }

        bool HasEmbeddedProfileMetadata(IWICBitmapFrameDecode* frame)
        {
            ComPtr<IWICMetadataQueryReader> reader;
            if (!frame || FAILED(frame->GetMetadataQueryReader(&reader))) return false;
            for (const wchar_t* query : {L"/iCCP", L"/ifd/{ushort=34675}", L"/app1/ifd/{ushort=34675}"})
            {
                PROPVARIANT value{};
                const bool present = SUCCEEDED(reader->GetMetadataByName(query, &value));
                PropVariantClear(&value);
                if (present) return true;
            }
            return false;
        }
    }

    bool IsUsableRgbProfile(std::span<const unsigned char> profile, std::wstring* errorMessage)
    {
        if (profile.size() < 128 || profile.size() > kMaximumIccProfileBytes)
        {
            return Fail(errorMessage, L"ICC profile size is invalid or exceeds the supported limit.");
        }

        PROFILE descriptor{PROFILE_MEMBUFFER,
                           const_cast<unsigned char*>(profile.data()),
                           static_cast<DWORD>(profile.size())};
        ColorProfileHandle handle(OpenColorProfileW(&descriptor, PROFILE_READ, FILE_SHARE_READ, OPEN_EXISTING));
        BOOL valid = FALSE;
        PROFILEHEADER header{};
        if (!handle.Get() || !IsColorProfileValid(handle.Get(), &valid) || !valid
            || !GetColorProfileHeader(handle.Get(), &header))
        {
            return Fail(errorMessage, L"ICC profile is malformed or unavailable to Windows Color System.");
        }
        if (header.phDataColorSpace != SPACE_RGB)
        {
            return Fail(errorMessage, L"ICC profile is not compatible with the canonical RGB pixel surface.");
        }
        return true;
    }

    cache::SourceColorInfo ReadSourceColorInfo(IWICBitmapFrameDecode* frame)
    {
        UINT count = 0;
        const HRESULT query = frame ? frame->GetColorContexts(0, nullptr, &count) : E_POINTER;
        if (query == WINCODEC_ERR_UNSUPPORTEDOPERATION || (SUCCEEDED(query) && count == 0))
        {
            return {HasEmbeddedProfileMetadata(frame) ? cache::SourceColorKind::Unsupported : cache::SourceColorKind::Srgb, {}};
        }
        if (FAILED(query) || count > 16)
        {
            return {cache::SourceColorKind::Unsupported, {}};
        }

        ComPtr<IWICImagingFactory> factory;
        if (!wic_support::InitializeWicFactory(&factory, nullptr))
        {
            return {cache::SourceColorKind::Unsupported, {}};
        }
        std::vector<ComPtr<IWICColorContext>> contexts(count);
        std::vector<IWICColorContext*> pointers(count);
        for (UINT index = 0; index < count; ++index)
        {
            if (FAILED(factory->CreateColorContext(&contexts[index])))
            {
                return {cache::SourceColorKind::Unsupported, {}};
            }
            pointers[index] = contexts[index].Get();
        }
        if (FAILED(frame->GetColorContexts(count, pointers.data(), &count)))
        {
            return {cache::SourceColorKind::Unsupported, {}};
        }
        for (const auto& context : contexts)
        {
            WICColorContextType type = WICColorContextUninitialized;
            if (FAILED(context->GetType(&type)))
            {
                return {cache::SourceColorKind::Unsupported, {}};
            }
            if (type != WICColorContextProfile)
            {
                continue;
            }
            UINT byteCount = 0;
            if (FAILED(context->GetProfileBytes(0, nullptr, &byteCount))
                || byteCount == 0 || byteCount > kMaximumIccProfileBytes)
            {
                return {cache::SourceColorKind::Unsupported, {}};
            }
            cache::SourceColorInfo info{cache::SourceColorKind::Icc, std::vector<unsigned char>(byteCount)};
            if (FAILED(context->GetProfileBytes(byteCount, info.iccProfile.data(), &byteCount))
                || !IsUsableRgbProfile(info.iccProfile))
            {
                return {cache::SourceColorKind::Unsupported, {}};
            }
            return info;
        }
        return {cache::SourceColorKind::Srgb, {}};
    }

    cache::SourceColorInfo ReadSourceColorInfo(const std::wstring& filePath)
    {
        wic_support::ComInitializationScope com(COINIT_MULTITHREADED, nullptr, L"Color metadata COM initialization failed.");
        ComPtr<IWICImagingFactory> factory;
        ComPtr<IWICBitmapDecoder> decoder;
        ComPtr<IWICBitmapFrameDecode> frame;
        if (!com.Succeeded() || !wic_support::InitializeWicFactory(&factory, nullptr)
            || FAILED(factory->CreateDecoderFromFilename(filePath.c_str(), nullptr, GENERIC_READ,
                                                         WICDecodeMetadataCacheOnDemand, &decoder))
            || FAILED(decoder->GetFrame(0, &frame)))
        {
            return {cache::SourceColorKind::Unsupported, {}};
        }
        return ReadSourceColorInfo(frame.Get());
    }

    cache::SourceColorInfo ReadSourceColorInfo(std::span<const unsigned char> encodedBytes)
    {
        wic_support::ComInitializationScope com(COINIT_MULTITHREADED, nullptr, L"Color metadata COM initialization failed.");
        ComPtr<IWICImagingFactory> factory;
        ComPtr<IWICStream> stream;
        ComPtr<IWICBitmapDecoder> decoder;
        ComPtr<IWICBitmapFrameDecode> frame;
        if (encodedBytes.empty() || encodedBytes.size() > (std::numeric_limits<DWORD>::max)()
            || !com.Succeeded() || !wic_support::InitializeWicFactory(&factory, nullptr)
            || FAILED(factory->CreateStream(&stream))
            || FAILED(stream->InitializeFromMemory(const_cast<BYTE*>(encodedBytes.data()), static_cast<DWORD>(encodedBytes.size())))
            || FAILED(factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder))
            || FAILED(decoder->GetFrame(0, &frame)))
        {
            return {cache::SourceColorKind::Unsupported, {}};
        }
        return ReadSourceColorInfo(frame.Get());
    }

    std::shared_ptr<const cache::CachedThumbnail> TransformForDisplay(
        const cache::CachedThumbnail& source,
        const cache::SourceColorInfo& sourceColor,
        std::span<const unsigned char> destinationProfile,
        std::wstring* errorMessage)
    {
        if (source.SourceColor().kind == cache::SourceColorKind::DisplayConverted
            || (sourceColor.kind != cache::SourceColorKind::Srgb && sourceColor.kind != cache::SourceColorKind::Icc))
        {
            Fail(errorMessage, L"Source color context is unsupported, unresolved, or already display-converted.");
            return {};
        }
        if (!IsUsableRgbProfile(destinationProfile, errorMessage)
            || (sourceColor.kind == cache::SourceColorKind::Icc
                && !IsUsableRgbProfile(sourceColor.iccProfile, errorMessage)))
        {
            return {};
        }

        const std::uint64_t pixelBytes = static_cast<std::uint64_t>(source.Width())
            * static_cast<std::uint64_t>(source.Height()) * 4;
        DIBSECTION dib{};
        if (source.Width() <= 0 || source.Height() <= 0 || pixelBytes > (std::numeric_limits<UINT>::max)()
            || GetObjectW(source.Bitmap(), sizeof(dib), &dib) != sizeof(dib)
            || !dib.dsBm.bmBits || dib.dsBm.bmBitsPixel != 32
            || dib.dsBm.bmWidthBytes != source.Width() * 4)
        {
            Fail(errorMessage, L"Canonical color-conversion pixel surface is invalid or too large.");
            return {};
        }

        wic_support::ComInitializationScope com(COINIT_MULTITHREADED, errorMessage, L"Color transform COM initialization failed.");
        ComPtr<IWICImagingFactory> factory;
        if (!com.Succeeded() || !wic_support::InitializeWicFactory(&factory, errorMessage))
        {
            return {};
        }
        ComPtr<IWICBitmap> bitmap;
        ComPtr<IWICFormatConverter> straight;
        ComPtr<IWICColorContext> sourceContext;
        ComPtr<IWICColorContext> destinationContext;
        ComPtr<IWICColorTransform> transform;
        HRESULT result = factory->CreateBitmapFromMemory(static_cast<UINT>(source.Width()),
                                                         static_cast<UINT>(source.Height()),
                                                         GUID_WICPixelFormat32bppPBGRA,
                                                         static_cast<UINT>(source.Width() * 4),
                                                         static_cast<UINT>(pixelBytes),
                                                         static_cast<BYTE*>(dib.dsBm.bmBits), &bitmap);
        if (SUCCEEDED(result)) result = factory->CreateFormatConverter(&straight);
        if (SUCCEEDED(result)) result = straight->Initialize(bitmap.Get(), GUID_WICPixelFormat32bppBGRA,
                                                             WICBitmapDitherTypeNone, nullptr, 0.0,
                                                             WICBitmapPaletteTypeCustom);
        if (SUCCEEDED(result)) result = factory->CreateColorContext(&sourceContext);
        if (SUCCEEDED(result))
        {
            result = sourceColor.kind == cache::SourceColorKind::Icc
                ? sourceContext->InitializeFromMemory(sourceColor.iccProfile.data(), static_cast<UINT>(sourceColor.iccProfile.size()))
                : sourceContext->InitializeFromExifColorSpace(1);
        }
        if (SUCCEEDED(result)) result = factory->CreateColorContext(&destinationContext);
        if (SUCCEEDED(result)) result = destinationContext->InitializeFromMemory(destinationProfile.data(), static_cast<UINT>(destinationProfile.size()));
        if (SUCCEEDED(result)) result = factory->CreateColorTransformer(&transform);
        if (SUCCEEDED(result)) result = transform->Initialize(straight.Get(), sourceContext.Get(), destinationContext.Get(), GUID_WICPixelFormat32bppBGRA);
        if (FAILED(result))
        {
            wic_support::SetError(errorMessage, L"WIC display color transform initialization failed.", result);
            return {};
        }

        void* outputBits = nullptr;
        HBITMAP output = wic_support::CreateBitmapBuffer(static_cast<UINT>(source.Width()), static_cast<UINT>(source.Height()), &outputBits);
        const auto deleteBitmap = [](HBITMAP handle) { if (handle) DeleteObject(handle); };
        std::unique_ptr<std::remove_pointer_t<HBITMAP>, decltype(deleteBitmap)> ownedBitmap(output, deleteBitmap);
        if (!output || !outputBits)
        {
            Fail(errorMessage, L"WIC display color transform output allocation failed.");
            return {};
        }
        result = transform->CopyPixels(nullptr, static_cast<UINT>(source.Width() * 4),
                                       static_cast<UINT>(pixelBytes), static_cast<BYTE*>(outputBits));
        if (FAILED(result))
        {
            wic_support::SetError(errorMessage, L"WIC display color transform pixel conversion failed.", result);
            return {};
        }
        const auto* inputPixels = static_cast<const BYTE*>(dib.dsBm.bmBits);
        auto* outputPixels = static_cast<BYTE*>(outputBits);
        for (std::size_t offset = 0; offset < pixelBytes; offset += 4)
        {
            const unsigned int alpha = inputPixels[offset + 3];
            for (std::size_t channel = 0; channel < 3; ++channel)
            {
                outputPixels[offset + channel] = static_cast<BYTE>((outputPixels[offset + channel] * alpha + 127) / 255);
            }
            outputPixels[offset + 3] = static_cast<BYTE>(alpha);
        }
        auto converted = std::make_shared<cache::CachedThumbnail>(output, source.Width(), source.Height(),
                                                                 static_cast<std::size_t>(pixelBytes),
                                                                 source.SourceWidth(), source.SourceHeight(),
                                                                 cache::SourceColorInfo{cache::SourceColorKind::DisplayConverted, {}});
        ownedBitmap.release();
        return converted;
    }
}
