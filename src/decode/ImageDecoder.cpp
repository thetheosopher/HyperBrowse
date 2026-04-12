#include "decode/ImageDecoder.h"

#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <vector>

#if defined(HYPERBROWSE_ENABLE_LIBRAW)
#include <libraw/libraw.h>
#endif

#include "decode/WicDecodeHelpers.h"
#include "decode/WicThumbnailDecoder.h"
#include "util/Diagnostics.h"

namespace fs = std::filesystem;

namespace
{
    using Microsoft::WRL::ComPtr;

    namespace wic = hyperbrowse::decode::wic_support;

    std::atomic_bool g_nvJpegEnabled{false};

    std::wstring NormalizeFileType(std::wstring_view fileType)
    {
        std::wstring normalized(fileType);
        if (!normalized.empty() && normalized.front() == L'.')
        {
            normalized.erase(normalized.begin());
        }

        std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](wchar_t character)
        {
            return static_cast<wchar_t>(towlower(character));
        });
        return normalized;
    }

    std::wstring FileTypeFromPath(const std::wstring& filePath)
    {
        return NormalizeFileType(fs::path(filePath).extension().wstring());
    }

    std::wstring WideErrorString(const char* message)
    {
        if (!message)
        {
            return L"Unknown LibRaw error.";
        }

        return std::wstring(message, message + std::strlen(message));
    }

    bool ProbeNvJpegRuntime()
    {
#if defined(HYPERBROWSE_ENABLE_NVJPEG)
        static constexpr const wchar_t* kCandidateDlls[] = {
            L"nvjpeg64_12.dll",
            L"nvjpeg64_11.dll",
            L"nvjpeg64_10.dll",
        };

        for (const wchar_t* dllName : kCandidateDlls)
        {
            HMODULE module = LoadLibraryW(dllName);
            if (module)
            {
                FreeLibrary(module);
                return true;
            }
        }
#endif

        return false;
    }

    std::shared_ptr<const hyperbrowse::cache::CachedThumbnail> DecodeWicSource(IWICImagingFactory* factory,
                                                                                IWICBitmapDecoder* decoder,
                                                                                int targetWidth,
                                                                                int targetHeight,
                                                                                int sourceWidthOverride,
                                                                                int sourceHeightOverride,
                                                                                std::wstring* errorMessage)
    {
        ComPtr<IWICBitmapFrameDecode> frame;
        HRESULT result = decoder->GetFrame(0, &frame);
        if (FAILED(result))
        {
            if (errorMessage)
            {
                *errorMessage = L"Failed to read the first image frame.";
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
                *errorMessage = L"The selected image does not report valid dimensions.";
            }
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
                if (errorMessage)
                {
                    *errorMessage = L"Failed to apply image orientation.";
                }
                return {};
            }

            source = rotator;
        }

        UINT scaledWidth = orientedWidth;
        UINT scaledHeight = orientedHeight;
        if (targetWidth > 0 && targetHeight > 0)
        {
            wic::ComputeScaledSize(orientedWidth, orientedHeight, targetWidth, targetHeight, &scaledWidth, &scaledHeight);
            if (scaledWidth != orientedWidth || scaledHeight != orientedHeight)
            {
                ComPtr<IWICBitmapScaler> scaler;
                result = factory->CreateBitmapScaler(&scaler);
                if (FAILED(result) || FAILED(scaler->Initialize(source.Get(), scaledWidth, scaledHeight, WICBitmapInterpolationModeFant)))
                {
                    if (errorMessage)
                    {
                        *errorMessage = L"Failed to scale the decoded image.";
                    }
                    return {};
                }

                source = scaler;
            }
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
            if (errorMessage)
            {
                *errorMessage = L"Failed to convert the decoded image into the viewer pixel format.";
            }
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

            if (errorMessage)
            {
                *errorMessage = L"Failed to allocate the destination bitmap.";
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
                *errorMessage = L"Failed to copy decoded pixels into the destination bitmap.";
            }
            return {};
        }

        return std::make_shared<hyperbrowse::cache::CachedThumbnail>(bitmap,
                                                                      static_cast<int>(scaledWidth),
                                                                      static_cast<int>(scaledHeight),
                                                                      bufferSize,
                                                                      sourceWidthOverride > 0 ? sourceWidthOverride : static_cast<int>(orientedWidth),
                                                                      sourceHeightOverride > 0 ? sourceHeightOverride : static_cast<int>(orientedHeight));
    }

    std::shared_ptr<const hyperbrowse::cache::CachedThumbnail> DecodeWicMemoryJpeg(const unsigned char* data,
                                                                                     std::size_t dataSize,
                                                                                     int targetWidth,
                                                                                     int targetHeight,
                                                                                     int sourceWidthOverride,
                                                                                     int sourceHeightOverride,
                                                                                     std::wstring* errorMessage)
    {
        if (!data || dataSize == 0)
        {
            if (errorMessage)
            {
                *errorMessage = L"The RAW preview payload is empty.";
            }
            return {};
        }

        wic::ComInitializationScope comInitialization(
            COINIT_MULTITHREADED,
            errorMessage,
            L"Failed to initialize COM for image decode.");
        if (!comInitialization.Succeeded())
        {
            return {};
        }

        ComPtr<IWICImagingFactory> factory;
        if (!wic::InitializeWicFactory(&factory, errorMessage))
        {
            return {};
        }

        std::vector<BYTE> mutableBytes(data, data + dataSize);
        ComPtr<IWICStream> stream;
        HRESULT result = factory->CreateStream(&stream);
        if (FAILED(result) || FAILED(stream->InitializeFromMemory(mutableBytes.data(), static_cast<DWORD>(mutableBytes.size()))))
        {
            if (errorMessage)
            {
                *errorMessage = L"Failed to initialize the WIC memory stream for the RAW preview.";
            }
            return {};
        }

        ComPtr<IWICBitmapDecoder> decoder;
        result = factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &decoder);
        if (FAILED(result))
        {
            if (errorMessage)
            {
                *errorMessage = L"Failed to create a WIC decoder for the embedded RAW preview.";
            }
            return {};
        }

        return DecodeWicSource(factory.Get(),
                               decoder.Get(),
                               targetWidth,
                               targetHeight,
                               sourceWidthOverride,
                               sourceHeightOverride,
                               errorMessage);
    }

    std::shared_ptr<const hyperbrowse::cache::CachedThumbnail> DecodeWicFile(const hyperbrowse::browser::BrowserItem& item,
                                                                              std::wstring* errorMessage)
    {
        wic::ComInitializationScope comInitialization(
            COINIT_MULTITHREADED,
            errorMessage,
            L"Failed to initialize COM for image decode.");
        if (!comInitialization.Succeeded())
        {
            return {};
        }

        ComPtr<IWICImagingFactory> factory;
        if (!wic::InitializeWicFactory(&factory, errorMessage))
        {
            return {};
        }

        ComPtr<IWICBitmapDecoder> decoder;
        const HRESULT result = factory->CreateDecoderFromFilename(item.filePath.c_str(),
                                                                  nullptr,
                                                                  GENERIC_READ,
                                                                  WICDecodeMetadataCacheOnLoad,
                                                                  &decoder);
        if (FAILED(result))
        {
            if (errorMessage)
            {
                *errorMessage = L"Failed to open the selected image for decode.";
            }
            return {};
        }

        return DecodeWicSource(factory.Get(), decoder.Get(), 0, 0, 0, 0, errorMessage);
    }

    std::vector<unsigned char> ScaleBgra(const std::vector<unsigned char>& sourcePixels,
                                         int sourceWidth,
                                         int sourceHeight,
                                         int destinationWidth,
                                         int destinationHeight)
    {
        if (sourceWidth == destinationWidth && sourceHeight == destinationHeight)
        {
            return sourcePixels;
        }

        std::vector<unsigned char> scaled(static_cast<std::size_t>(destinationWidth) * static_cast<std::size_t>(destinationHeight) * 4U);
        for (int y = 0; y < destinationHeight; ++y)
        {
            const int sourceY = std::clamp((y * sourceHeight) / std::max(1, destinationHeight), 0, sourceHeight - 1);
            for (int x = 0; x < destinationWidth; ++x)
            {
                const int sourceX = std::clamp((x * sourceWidth) / std::max(1, destinationWidth), 0, sourceWidth - 1);
                const std::size_t sourceOffset = (static_cast<std::size_t>(sourceY) * static_cast<std::size_t>(sourceWidth) + static_cast<std::size_t>(sourceX)) * 4U;
                const std::size_t destinationOffset = (static_cast<std::size_t>(y) * static_cast<std::size_t>(destinationWidth) + static_cast<std::size_t>(x)) * 4U;
                scaled[destinationOffset + 0] = sourcePixels[sourceOffset + 0];
                scaled[destinationOffset + 1] = sourcePixels[sourceOffset + 1];
                scaled[destinationOffset + 2] = sourcePixels[sourceOffset + 2];
                scaled[destinationOffset + 3] = sourcePixels[sourceOffset + 3];
            }
        }

        return scaled;
    }

    std::shared_ptr<const hyperbrowse::cache::CachedThumbnail> BuildCachedThumbnailFromBgra(std::vector<unsigned char> bgraPixels,
                                                                                              int sourceBitmapWidth,
                                                                                              int sourceBitmapHeight,
                                                                                              int targetWidth,
                                                                                              int targetHeight,
                                                                                              int sourceWidthMetadata,
                                                                                              int sourceHeightMetadata,
                                                                                              std::wstring* errorMessage)
    {
        int destinationWidth = sourceBitmapWidth;
        int destinationHeight = sourceBitmapHeight;
        if (targetWidth > 0 && targetHeight > 0)
        {
            UINT scaledWidth = 0;
            UINT scaledHeight = 0;
            wic::ComputeScaledSize(static_cast<UINT>(sourceBitmapWidth),
                                   static_cast<UINT>(sourceBitmapHeight),
                                   targetWidth,
                                   targetHeight,
                                   &scaledWidth,
                                   &scaledHeight);
            destinationWidth = static_cast<int>(scaledWidth);
            destinationHeight = static_cast<int>(scaledHeight);
            bgraPixels = ScaleBgra(bgraPixels, sourceBitmapWidth, sourceBitmapHeight, destinationWidth, destinationHeight);
        }

        void* bits = nullptr;
        HBITMAP bitmap = wic::CreateBitmapBuffer(static_cast<UINT>(destinationWidth), static_cast<UINT>(destinationHeight), &bits);
        if (!bitmap || !bits)
        {
            if (bitmap)
            {
                DeleteObject(bitmap);
            }
            if (errorMessage)
            {
                *errorMessage = L"Failed to allocate the destination bitmap for the RAW decode result.";
            }
            return {};
        }

        const std::size_t byteCount = static_cast<std::size_t>(destinationWidth) * static_cast<std::size_t>(destinationHeight) * 4U;
        std::memcpy(bits, bgraPixels.data(), byteCount);
        return std::make_shared<hyperbrowse::cache::CachedThumbnail>(bitmap,
                                                                      destinationWidth,
                                                                      destinationHeight,
                                                                      byteCount,
                                                                      sourceWidthMetadata > 0 ? sourceWidthMetadata : sourceBitmapWidth,
                                                                      sourceHeightMetadata > 0 ? sourceHeightMetadata : sourceBitmapHeight);
    }

#if defined(HYPERBROWSE_ENABLE_LIBRAW)
    void ConfigureRawPostprocess(LibRaw& processor, bool halfSize)
    {
        processor.imgdata.params.output_bps = 8;
        processor.imgdata.params.use_camera_wb = 1;
        processor.imgdata.params.half_size = halfSize ? 1 : 0;
    }

    int RawImageWidth(LibRaw& processor)
    {
        const auto& sizes = processor.imgdata.sizes;
        if (sizes.iwidth > 0)
        {
            return sizes.iwidth;
        }
        if (sizes.width > 0)
        {
            return sizes.width;
        }
        return sizes.raw_width;
    }

    int RawImageHeight(LibRaw& processor)
    {
        const auto& sizes = processor.imgdata.sizes;
        if (sizes.iheight > 0)
        {
            return sizes.iheight;
        }
        if (sizes.height > 0)
        {
            return sizes.height;
        }
        return sizes.raw_height;
    }

    int OpenRawFile(LibRaw& processor, const std::wstring& filePath)
    {
#if defined(_WIN32) && defined(LIBRAW_WIN32_UNICODEPATHS)
        return processor.open_file(filePath.c_str());
#else
        return processor.open_file(std::string(filePath.begin(), filePath.end()).c_str());
#endif
    }

    std::shared_ptr<const hyperbrowse::cache::CachedThumbnail> BuildCachedThumbnailFromProcessedImage(const libraw_processed_image_t* image,
                                                                                                        int targetWidth,
                                                                                                        int targetHeight,
                                                                                                        int sourceWidthMetadata,
                                                                                                        int sourceHeightMetadata,
                                                                                                        std::wstring* errorMessage)
    {
        if (!image)
        {
            if (errorMessage)
            {
                *errorMessage = L"LibRaw did not return an image buffer.";
            }
            return {};
        }

        if (image->type != LIBRAW_IMAGE_BITMAP)
        {
            if (errorMessage)
            {
                *errorMessage = L"LibRaw returned an unsupported bitmap layout.";
            }
            return {};
        }

        const int width = static_cast<int>(image->width);
        const int height = static_cast<int>(image->height);
        const int colors = static_cast<int>(image->colors);
        const int bits = static_cast<int>(image->bits);
        if (width <= 0 || height <= 0 || colors <= 0)
        {
            if (errorMessage)
            {
                *errorMessage = L"LibRaw returned invalid image dimensions.";
            }
            return {};
        }

        const std::size_t bytesPerSample = bits > 8 ? 2U : 1U;
        const std::size_t expectedSize = static_cast<std::size_t>(width) * static_cast<std::size_t>(height)
            * static_cast<std::size_t>(colors) * bytesPerSample;
        if (image->data_size < expectedSize)
        {
            if (errorMessage)
            {
                *errorMessage = L"LibRaw returned a truncated image buffer.";
            }
            return {};
        }

        std::vector<unsigned char> bgraPixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U, 255U);
        if (bits > 8)
        {
            const auto* samples = reinterpret_cast<const std::uint16_t*>(image->data);
            for (int pixelIndex = 0; pixelIndex < width * height; ++pixelIndex)
            {
                const std::size_t sampleOffset = static_cast<std::size_t>(pixelIndex) * static_cast<std::size_t>(colors);
                const std::size_t pixelOffset = static_cast<std::size_t>(pixelIndex) * 4U;
                const unsigned char red = static_cast<unsigned char>(samples[sampleOffset + 0] >> 8);
                const unsigned char green = static_cast<unsigned char>((colors > 1 ? samples[sampleOffset + 1] : samples[sampleOffset + 0]) >> 8);
                const unsigned char blue = static_cast<unsigned char>((colors > 2 ? samples[sampleOffset + 2] : samples[sampleOffset + 0]) >> 8);
                bgraPixels[pixelOffset + 0] = blue;
                bgraPixels[pixelOffset + 1] = green;
                bgraPixels[pixelOffset + 2] = red;
            }
        }
        else
        {
            const auto* samples = image->data;
            for (int pixelIndex = 0; pixelIndex < width * height; ++pixelIndex)
            {
                const std::size_t sampleOffset = static_cast<std::size_t>(pixelIndex) * static_cast<std::size_t>(colors);
                const std::size_t pixelOffset = static_cast<std::size_t>(pixelIndex) * 4U;
                const unsigned char red = samples[sampleOffset + 0];
                const unsigned char green = colors > 1 ? samples[sampleOffset + 1] : samples[sampleOffset + 0];
                const unsigned char blue = colors > 2 ? samples[sampleOffset + 2] : samples[sampleOffset + 0];
                bgraPixels[pixelOffset + 0] = blue;
                bgraPixels[pixelOffset + 1] = green;
                bgraPixels[pixelOffset + 2] = red;
            }
        }

        return BuildCachedThumbnailFromBgra(std::move(bgraPixels),
                                            width,
                                            height,
                                            targetWidth,
                                            targetHeight,
                                            sourceWidthMetadata,
                                            sourceHeightMetadata,
                                            errorMessage);
    }

    std::shared_ptr<const hyperbrowse::cache::CachedThumbnail> DecodeRawThumbnail(const hyperbrowse::cache::ThumbnailCacheKey& key,
                                                                                   std::wstring* errorMessage)
    {
        hyperbrowse::util::Stopwatch decodeStopwatch;
        LibRaw processor;
        int result = OpenRawFile(processor, key.filePath);
        if (result != LIBRAW_SUCCESS)
        {
            if (errorMessage)
            {
                *errorMessage = L"Failed to open the RAW image: " + WideErrorString(libraw_strerror(result));
            }
            return {};
        }

        result = processor.adjust_sizes_info_only();
        if (result != LIBRAW_SUCCESS)
        {
            if (errorMessage)
            {
                *errorMessage = L"Failed to inspect the RAW metadata: " + WideErrorString(libraw_strerror(result));
            }
            return {};
        }

        const int sourceWidth = RawImageWidth(processor);
        const int sourceHeight = RawImageHeight(processor);

        result = processor.unpack_thumb();
        if (result == LIBRAW_SUCCESS)
        {
            int thumbnailResult = LIBRAW_SUCCESS;
            libraw_processed_image_t* thumbnail = processor.dcraw_make_mem_thumb(&thumbnailResult);
            if (thumbnail)
            {
                std::shared_ptr<const hyperbrowse::cache::CachedThumbnail> decodedThumbnail;
                if (thumbnail->type == LIBRAW_IMAGE_JPEG)
                {
                    decodedThumbnail = DecodeWicMemoryJpeg(thumbnail->data,
                                                           thumbnail->data_size,
                                                           key.targetWidth,
                                                           key.targetHeight,
                                                           sourceWidth,
                                                           sourceHeight,
                                                           errorMessage);
                }
                else if (thumbnail->type == LIBRAW_IMAGE_BITMAP)
                {
                    decodedThumbnail = BuildCachedThumbnailFromProcessedImage(thumbnail,
                                                                             key.targetWidth,
                                                                             key.targetHeight,
                                                                             sourceWidth,
                                                                             sourceHeight,
                                                                             errorMessage);
                }
                LibRaw::dcraw_clear_mem(thumbnail);
                if (decodedThumbnail)
                {
                    hyperbrowse::util::RecordTiming(L"thumbnail.decode.raw.embedded_preview", decodeStopwatch.ElapsedMilliseconds());
                    return decodedThumbnail;
                }
            }
        }

        processor.recycle();
        result = OpenRawFile(processor, key.filePath);
        if (result != LIBRAW_SUCCESS)
        {
            if (errorMessage)
            {
                *errorMessage = L"Failed to reopen the RAW image for thumbnail fallback: " + WideErrorString(libraw_strerror(result));
            }
            return {};
        }

        ConfigureRawPostprocess(processor, true);
        result = processor.unpack();
        if (result != LIBRAW_SUCCESS)
        {
            if (errorMessage)
            {
                *errorMessage = L"Failed to unpack the RAW thumbnail fallback: " + WideErrorString(libraw_strerror(result));
            }
            return {};
        }

        result = processor.dcraw_process();
        if (result != LIBRAW_SUCCESS)
        {
            if (errorMessage)
            {
                *errorMessage = L"Failed to process the RAW thumbnail fallback: " + WideErrorString(libraw_strerror(result));
            }
            return {};
        }

        int imageResult = LIBRAW_SUCCESS;
        libraw_processed_image_t* image = processor.dcraw_make_mem_image(&imageResult);
        auto decodedImage = BuildCachedThumbnailFromProcessedImage(image,
                                                                   key.targetWidth,
                                                                   key.targetHeight,
                                                                   sourceWidth,
                                                                   sourceHeight,
                                                                   errorMessage);
        if (image)
        {
            LibRaw::dcraw_clear_mem(image);
        }
        hyperbrowse::util::RecordTiming(L"thumbnail.decode.raw.fallback_full", decodeStopwatch.ElapsedMilliseconds());
        return decodedImage;
    }

    std::shared_ptr<const hyperbrowse::cache::CachedThumbnail> DecodeRawFullImage(const hyperbrowse::browser::BrowserItem& item,
                                                                                   std::wstring* errorMessage)
    {
        LibRaw processor;
        int result = OpenRawFile(processor, item.filePath);
        if (result != LIBRAW_SUCCESS)
        {
            if (errorMessage)
            {
                *errorMessage = L"RAW decode failed while opening the file: " + WideErrorString(libraw_strerror(result));
            }
            return {};
        }

        result = processor.adjust_sizes_info_only();
        if (result != LIBRAW_SUCCESS)
        {
            if (errorMessage)
            {
                *errorMessage = L"RAW metadata extraction failed: " + WideErrorString(libraw_strerror(result));
            }
            return {};
        }

        const int sourceWidth = RawImageWidth(processor);
        const int sourceHeight = RawImageHeight(processor);
        processor.recycle();

        result = OpenRawFile(processor, item.filePath);
        if (result != LIBRAW_SUCCESS)
        {
            if (errorMessage)
            {
                *errorMessage = L"RAW decode failed while reopening the file: " + WideErrorString(libraw_strerror(result));
            }
            return {};
        }

        ConfigureRawPostprocess(processor, false);

        result = processor.unpack();
        if (result != LIBRAW_SUCCESS)
        {
            if (errorMessage)
            {
                *errorMessage = L"RAW decode failed while unpacking the file: " + WideErrorString(libraw_strerror(result));
            }
            return {};
        }

        result = processor.dcraw_process();
        if (result != LIBRAW_SUCCESS)
        {
            if (errorMessage)
            {
                *errorMessage = L"RAW decode failed during image processing: " + WideErrorString(libraw_strerror(result));
            }
            return {};
        }

        int imageResult = LIBRAW_SUCCESS;
        libraw_processed_image_t* image = processor.dcraw_make_mem_image(&imageResult);
        auto decodedImage = BuildCachedThumbnailFromProcessedImage(image,
                                                                   0,
                                                                   0,
                                                                   sourceWidth,
                                                                   sourceHeight,
                                                                   errorMessage);
        if (image)
        {
            LibRaw::dcraw_clear_mem(image);
        }
        return decodedImage;
    }
#endif
}

namespace hyperbrowse::decode
{
    void SetNvJpegAccelerationEnabled(bool enabled)
    {
        g_nvJpegEnabled.store(enabled && IsNvJpegBuildEnabled(), std::memory_order_release);
    }

    bool IsNvJpegAccelerationEnabled()
    {
        return g_nvJpegEnabled.load(std::memory_order_acquire);
    }

    bool IsNvJpegBuildEnabled()
    {
#if defined(HYPERBROWSE_ENABLE_NVJPEG)
        return true;
#else
        return false;
#endif
    }

    bool IsNvJpegRuntimeAvailable()
    {
        return IsNvJpegBuildEnabled() && ProbeNvJpegRuntime();
    }

    std::wstring DescribeJpegAccelerationState()
    {
        if (!IsNvJpegBuildEnabled())
        {
            return L"WIC (nvJPEG build disabled)";
        }

        if (!IsNvJpegAccelerationEnabled())
        {
            return L"WIC (nvJPEG disabled)";
        }

        if (!IsNvJpegRuntimeAvailable())
        {
            return L"WIC (nvJPEG runtime unavailable)";
        }

        return L"WIC (nvJPEG plumbing active)";
    }

    bool IsWicFileType(std::wstring_view fileType)
    {
        const std::wstring normalized = NormalizeFileType(fileType);
        return normalized == L"jpg"
            || normalized == L"jpeg"
            || normalized == L"png"
            || normalized == L"gif"
            || normalized == L"tif"
            || normalized == L"tiff";
    }

    bool IsRawFileType(std::wstring_view fileType)
    {
        const std::wstring normalized = NormalizeFileType(fileType);
        return normalized == L"nef" || normalized == L"nrw";
    }

    bool CanDecodeThumbnail(const browser::BrowserItem& item)
    {
        return IsWicFileType(item.fileType) || IsRawFileType(item.fileType);
    }

    bool CanDecodeFullImage(const browser::BrowserItem& item)
    {
        return IsWicFileType(item.fileType) || IsRawFileType(item.fileType);
    }

    std::shared_ptr<const cache::CachedThumbnail> DecodeThumbnail(const cache::ThumbnailCacheKey& key,
                                                                  std::wstring* errorMessage)
    {
        hyperbrowse::util::Stopwatch stopwatch;
        const std::wstring fileType = FileTypeFromPath(key.filePath);
        if (IsWicFileType(fileType))
        {
            auto thumbnail = WicThumbnailDecoder{}.Decode(key);
            hyperbrowse::util::RecordTiming(L"thumbnail.decode.wic", stopwatch.ElapsedMilliseconds());
            return thumbnail;
        }

        if (IsRawFileType(fileType))
        {
#if defined(HYPERBROWSE_ENABLE_LIBRAW)
            auto thumbnail = DecodeRawThumbnail(key, errorMessage);
            hyperbrowse::util::RecordTiming(L"thumbnail.decode.raw", stopwatch.ElapsedMilliseconds());
            return thumbnail;
#else
            if (errorMessage)
            {
                *errorMessage = L"This build does not include LibRaw support for Nikon RAW thumbnails.";
            }
            return {};
#endif
        }

        if (errorMessage)
        {
            *errorMessage = L"The selected file type is not supported for thumbnail decode.";
        }
        return {};
    }

    std::shared_ptr<const cache::CachedThumbnail> DecodeFullImage(const browser::BrowserItem& item,
                                                                  std::wstring* errorMessage)
    {
        hyperbrowse::util::Stopwatch stopwatch;
        if (IsWicFileType(item.fileType))
        {
            auto image = DecodeWicFile(item, errorMessage);
            hyperbrowse::util::RecordTiming(L"viewer.decode.wic", stopwatch.ElapsedMilliseconds());
            return image;
        }

        if (IsRawFileType(item.fileType))
        {
#if defined(HYPERBROWSE_ENABLE_LIBRAW)
            auto image = DecodeRawFullImage(item, errorMessage);
            hyperbrowse::util::RecordTiming(L"viewer.decode.raw", stopwatch.ElapsedMilliseconds());
            return image;
#else
            if (errorMessage)
            {
                *errorMessage = L"This build does not include LibRaw support for Nikon RAW viewer decode.";
            }
            return {};
#endif
        }

        if (errorMessage)
        {
            *errorMessage = L"The current viewer build supports JPEG, PNG, GIF, TIFF, NEF, and NRW images.";
        }
        return {};
    }
}