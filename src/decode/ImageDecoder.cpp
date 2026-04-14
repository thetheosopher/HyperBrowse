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

#include "decode/NvJpegDecoder.h"
#include "decode/RawHelperProtocol.h"
#include "decode/WicDecodeHelpers.h"
#include "decode/WicThumbnailDecoder.h"
#include "util/Diagnostics.h"

namespace fs = std::filesystem;

namespace
{
    using Microsoft::WRL::ComPtr;

    namespace wic = hyperbrowse::decode::wic_support;

    std::atomic_bool g_nvJpegEnabled{false};
    std::atomic_bool g_libRawOutOfProcessEnabled{true};
    constexpr DWORD kRawHelperThumbnailTimeoutMs = 8000;
    constexpr DWORD kRawHelperFullImageTimeoutMs = 30000;

    hyperbrowse::decode::ThumbnailDecodeFailureKind ClassifyThumbnailFailureKindFromMessage(std::wstring_view errorMessage)
    {
        if (errorMessage.empty())
        {
            return hyperbrowse::decode::ThumbnailDecodeFailureKind::DecodeFailed;
        }

        std::wstring normalized(errorMessage);
        std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](wchar_t value)
        {
            return static_cast<wchar_t>(towlower(value));
        });

        return normalized.find(L"timed out") != std::wstring::npos
            ? hyperbrowse::decode::ThumbnailDecodeFailureKind::TimedOut
            : hyperbrowse::decode::ThumbnailDecodeFailureKind::DecodeFailed;
    }
    constexpr const wchar_t* kRawHelperExecutableName = L"HyperBrowseRawHelper.exe";

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

    std::wstring CurrentModuleDirectory()
    {
        std::wstring path(MAX_PATH, L'\0');
        while (true)
        {
            const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
            if (length == 0)
            {
                return {};
            }

            if (length < path.size() - 1)
            {
                path.resize(length);
                return fs::path(path).parent_path().wstring();
            }

            path.resize(path.size() * 2);
        }
    }

    std::wstring RawHelperExecutablePath()
    {
        const std::wstring moduleDirectory = CurrentModuleDirectory();
        if (moduleDirectory.empty())
        {
            return {};
        }

        return (fs::path(moduleDirectory) / kRawHelperExecutableName).wstring();
    }

    bool IsLibRawHelperExecutableAvailable()
    {
#if defined(HYPERBROWSE_ENABLE_LIBRAW)
        const std::wstring helperPath = RawHelperExecutablePath();
        return !helperPath.empty() && fs::exists(fs::path(helperPath));
#else
        return false;
#endif
    }

    std::wstring QuoteCommandLineArgument(std::wstring_view value)
    {
        std::wstring quoted;
        quoted.reserve(value.size() + 2);
        quoted.push_back(L'"');
        for (wchar_t character : value)
        {
            if (character == L'"')
            {
                quoted.push_back(L'\\');
            }
            quoted.push_back(character);
        }
        quoted.push_back(L'"');
        return quoted;
    }

    std::wstring CreateTemporaryOutputPath(std::wstring* errorMessage)
    {
        wchar_t tempDirectory[MAX_PATH]{};
        const DWORD tempDirectoryLength = GetTempPathW(static_cast<DWORD>(std::size(tempDirectory)), tempDirectory);
        if (tempDirectoryLength == 0 || tempDirectoryLength >= std::size(tempDirectory))
        {
            if (errorMessage)
            {
                *errorMessage = L"Failed to resolve the temporary directory for the RAW helper.";
            }
            return {};
        }

        wchar_t tempFile[MAX_PATH]{};
        if (GetTempFileNameW(tempDirectory, L"HBR", 0, tempFile) == 0)
        {
            if (errorMessage)
            {
                *errorMessage = L"Failed to create a temporary output file for the RAW helper.";
            }
            return {};
        }

        return tempFile;
    }

    bool ExtractBgraPixelsFromBitmap(HBITMAP bitmap,
                                     int width,
                                     int height,
                                     std::vector<unsigned char>* bgraPixels,
                                     std::wstring* errorMessage)
    {
        if (!bitmap || !bgraPixels || width <= 0 || height <= 0)
        {
            if (errorMessage)
            {
                *errorMessage = L"The RAW helper received an invalid bitmap surface.";
            }
            return false;
        }

        DIBSECTION dibSection{};
        if (GetObjectW(bitmap, sizeof(dibSection), &dibSection) != sizeof(dibSection) || !dibSection.dsBm.bmBits)
        {
            if (errorMessage)
            {
                *errorMessage = L"The RAW helper could not access the decoded bitmap pixels.";
            }
            return false;
        }

        const std::size_t pixelBytes = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U;
        bgraPixels->resize(pixelBytes);
        std::memcpy(bgraPixels->data(), dibSection.dsBm.bmBits, pixelBytes);
        return true;
    }

    bool ProbeNvJpegRuntime()
    {
#if defined(HYPERBROWSE_ENABLE_NVJPEG)
        return hyperbrowse::decode::NvJpegDecoder::IsRuntimeAvailable();
#else
        return false;
#endif
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

    std::shared_ptr<const hyperbrowse::cache::CachedThumbnail> TryDecodeRawThumbnailWithWic(const hyperbrowse::cache::ThumbnailCacheKey& key)
    {
        return hyperbrowse::decode::WicThumbnailDecoder{}.Decode(key);
    }

    std::shared_ptr<const hyperbrowse::cache::CachedThumbnail> TryDecodeRawFullImageWithWic(const hyperbrowse::browser::BrowserItem& item)
    {
        std::wstring ignoredError;
        return DecodeWicFile(item, &ignoredError);
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
    bool InvokeLibRawHelper(const hyperbrowse::decode::LibRawHelperInvocation& invocation,
                            DWORD timeoutMs,
                            hyperbrowse::decode::RawHelperDecodedPixels* payload,
                            std::wstring* errorMessage)
    {
        if (!payload)
        {
            return false;
        }

        const std::wstring helperPath = RawHelperExecutablePath();
        if (helperPath.empty() || !fs::exists(fs::path(helperPath)))
        {
            if (errorMessage)
            {
                *errorMessage = L"The RAW helper executable is not available beside the current application binary.";
            }
            return false;
        }

        std::wstring commandLine = QuoteCommandLineArgument(helperPath);
        commandLine.append(L" --mode ");
        commandLine.append(invocation.mode == hyperbrowse::decode::LibRawHelperMode::Thumbnail ? L"thumbnail" : L"full");
        commandLine.append(L" --input ");
        commandLine.append(QuoteCommandLineArgument(invocation.filePath));
        commandLine.append(L" --output ");
        commandLine.append(QuoteCommandLineArgument(invocation.outputFilePath));
        if (invocation.mode == hyperbrowse::decode::LibRawHelperMode::Thumbnail)
        {
            commandLine.append(L" --width ");
            commandLine.append(std::to_wstring(invocation.targetWidth));
            commandLine.append(L" --height ");
            commandLine.append(std::to_wstring(invocation.targetHeight));
        }

        std::vector<wchar_t> mutableCommandLine(commandLine.begin(), commandLine.end());
        mutableCommandLine.push_back(L'\0');

        STARTUPINFOW startupInfo{};
        startupInfo.cb = sizeof(startupInfo);
        PROCESS_INFORMATION processInfo{};
        const std::wstring workingDirectory = fs::path(helperPath).parent_path().wstring();
        const BOOL created = CreateProcessW(helperPath.c_str(),
                                            mutableCommandLine.data(),
                                            nullptr,
                                            nullptr,
                                            FALSE,
                                            CREATE_NO_WINDOW,
                                            nullptr,
                                            workingDirectory.c_str(),
                                            &startupInfo,
                                            &processInfo);
        if (!created)
        {
            if (errorMessage)
            {
                *errorMessage = L"Failed to launch the RAW helper process.";
            }
            return false;
        }

        const DWORD waitResult = WaitForSingleObject(processInfo.hProcess, timeoutMs);
        if (waitResult == WAIT_TIMEOUT)
        {
            TerminateProcess(processInfo.hProcess, WAIT_TIMEOUT);
            WaitForSingleObject(processInfo.hProcess, 1000);
            CloseHandle(processInfo.hThread);
            CloseHandle(processInfo.hProcess);

            if (errorMessage)
            {
                *errorMessage = L"The RAW helper timed out and was terminated.";
            }
            return false;
        }

        DWORD exitCode = 0;
        GetExitCodeProcess(processInfo.hProcess, &exitCode);
        CloseHandle(processInfo.hThread);
        CloseHandle(processInfo.hProcess);

        if (waitResult != WAIT_OBJECT_0)
        {
            if (errorMessage)
            {
                *errorMessage = L"The RAW helper did not finish normally.";
            }
            return false;
        }

        if (exitCode != 0)
        {
            if (errorMessage)
            {
                *errorMessage = L"The RAW helper failed with exit code " + std::to_wstring(exitCode) + L".";
            }
            return false;
        }

        return hyperbrowse::decode::ReadRawHelperPayload(invocation.outputFilePath, payload, errorMessage);
    }

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
                                                                                   std::wstring* errorMessage,
                                                                                   hyperbrowse::decode::ThumbnailDecodeFailureKind* failureKind)
    {
        hyperbrowse::util::Stopwatch decodeStopwatch;
        if (failureKind)
        {
            *failureKind = hyperbrowse::decode::ThumbnailDecodeFailureKind::None;
        }

        LibRaw processor;
        int result = OpenRawFile(processor, key.filePath);
        if (result != LIBRAW_SUCCESS)
        {
            if (errorMessage)
            {
                *errorMessage = L"Failed to open the RAW image: " + WideErrorString(libraw_strerror(result));
            }
            if (failureKind)
            {
                *failureKind = hyperbrowse::decode::ThumbnailDecodeFailureKind::DecodeFailed;
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
            if (failureKind)
            {
                *failureKind = hyperbrowse::decode::ThumbnailDecodeFailureKind::DecodeFailed;
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
            if (failureKind)
            {
                *failureKind = hyperbrowse::decode::ThumbnailDecodeFailureKind::DecodeFailed;
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
            if (failureKind)
            {
                *failureKind = hyperbrowse::decode::ThumbnailDecodeFailureKind::DecodeFailed;
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
            if (failureKind)
            {
                *failureKind = hyperbrowse::decode::ThumbnailDecodeFailureKind::DecodeFailed;
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
        if (!decodedImage && failureKind)
        {
            *failureKind = hyperbrowse::decode::ThumbnailDecodeFailureKind::DecodeFailed;
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

    std::shared_ptr<const hyperbrowse::cache::CachedThumbnail> DecodeRawThumbnailWithHelper(const hyperbrowse::cache::ThumbnailCacheKey& key,
                                                                                             std::wstring* errorMessage,
                                                                                             hyperbrowse::decode::ThumbnailDecodeFailureKind* failureKind)
    {
        if (failureKind)
        {
            *failureKind = hyperbrowse::decode::ThumbnailDecodeFailureKind::None;
        }

        std::wstring outputPath = CreateTemporaryOutputPath(errorMessage);
        if (outputPath.empty())
        {
            if (failureKind)
            {
                *failureKind = hyperbrowse::decode::ClassifyThumbnailDecodeFailure(errorMessage ? *errorMessage : std::wstring_view{});
            }
            return {};
        }

        hyperbrowse::decode::RawHelperDecodedPixels payload;
        const hyperbrowse::decode::LibRawHelperInvocation invocation{
            hyperbrowse::decode::LibRawHelperMode::Thumbnail,
            key.filePath,
            outputPath,
            key.targetWidth,
            key.targetHeight,
        };

        const bool success = InvokeLibRawHelper(invocation, kRawHelperThumbnailTimeoutMs, &payload, errorMessage);
        DeleteFileW(outputPath.c_str());
        if (!success)
        {
            if (failureKind)
            {
                *failureKind = hyperbrowse::decode::ClassifyThumbnailDecodeFailure(errorMessage ? *errorMessage : std::wstring_view{});
            }
            hyperbrowse::util::IncrementCounter(L"thumbnail.decode.raw.helper.failures");
            return {};
        }

        hyperbrowse::util::IncrementCounter(L"thumbnail.decode.raw.helper.successes");
        return BuildCachedThumbnailFromBgra(std::move(payload.bgraPixels),
                                            payload.bitmapWidth,
                                            payload.bitmapHeight,
                                            0,
                                            0,
                                            payload.sourceWidth,
                                            payload.sourceHeight,
                                            errorMessage);
    }

    std::shared_ptr<const hyperbrowse::cache::CachedThumbnail> DecodeRawFullImageWithHelper(const hyperbrowse::browser::BrowserItem& item,
                                                                                             std::wstring* errorMessage)
    {
        std::wstring outputPath = CreateTemporaryOutputPath(errorMessage);
        if (outputPath.empty())
        {
            return {};
        }

        hyperbrowse::decode::RawHelperDecodedPixels payload;
        const hyperbrowse::decode::LibRawHelperInvocation invocation{
            hyperbrowse::decode::LibRawHelperMode::FullImage,
            item.filePath,
            outputPath,
            0,
            0,
        };

        const bool success = InvokeLibRawHelper(invocation, kRawHelperFullImageTimeoutMs, &payload, errorMessage);
        DeleteFileW(outputPath.c_str());
        if (!success)
        {
            hyperbrowse::util::IncrementCounter(L"viewer.decode.raw.helper.failures");
            return {};
        }

        hyperbrowse::util::IncrementCounter(L"viewer.decode.raw.helper.successes");
        return BuildCachedThumbnailFromBgra(std::move(payload.bgraPixels),
                                            payload.bitmapWidth,
                                            payload.bitmapHeight,
                                            0,
                                            0,
                                            payload.sourceWidth,
                                            payload.sourceHeight,
                                            errorMessage);
    }
#endif
}

namespace hyperbrowse::decode
{
    void SetNvJpegAccelerationEnabled(bool enabled)
    {
        g_nvJpegEnabled.store(enabled && IsNvJpegBuildEnabled(), std::memory_order_release);
    }

    bool IsLibRawBuildEnabled()
    {
#if defined(HYPERBROWSE_ENABLE_LIBRAW)
        return true;
#else
        return false;
#endif
    }

    void SetLibRawOutOfProcessEnabled(bool enabled)
    {
        g_libRawOutOfProcessEnabled.store(enabled && IsLibRawBuildEnabled(), std::memory_order_release);
    }

    bool IsLibRawOutOfProcessEnabled()
    {
        return IsLibRawBuildEnabled() && g_libRawOutOfProcessEnabled.load(std::memory_order_acquire);
    }

    std::wstring DescribeRawDecodingState()
    {
        if (!IsLibRawBuildEnabled())
        {
            return L"WIC only (LibRaw build disabled)";
        }

        if (!IsLibRawOutOfProcessEnabled())
        {
            return L"WIC first, in-process LibRaw fallback";
        }

        if (!IsLibRawHelperExecutableAvailable())
        {
            return L"WIC first, in-process LibRaw fallback (helper unavailable)";
        }

        return L"WIC first, out-of-process LibRaw fallback";
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

#if defined(HYPERBROWSE_ENABLE_LIBRAW)
    bool RunLibRawHelperInvocation(const LibRawHelperInvocation& invocation,
                                   std::wstring* errorMessage)
    {
        if (invocation.filePath.empty() || invocation.outputFilePath.empty())
        {
            if (errorMessage)
            {
                *errorMessage = L"The RAW helper invocation is missing the input or output path.";
            }
            return false;
        }

        std::shared_ptr<const cache::CachedThumbnail> decodedImage;
        if (invocation.mode == LibRawHelperMode::Thumbnail)
        {
            cache::ThumbnailCacheKey cacheKey;
            cacheKey.filePath = invocation.filePath;
            cacheKey.targetWidth = invocation.targetWidth;
            cacheKey.targetHeight = invocation.targetHeight;
            decodedImage = DecodeRawThumbnail(cacheKey, errorMessage, nullptr);
        }
        else
        {
            browser::BrowserItem item;
            item.filePath = invocation.filePath;
            item.fileType = fs::path(invocation.filePath).extension().wstring();
            decodedImage = DecodeRawFullImage(item, errorMessage);
        }

        if (!decodedImage)
        {
            return false;
        }

        RawHelperDecodedPixels payload;
        payload.bitmapWidth = decodedImage->Width();
        payload.bitmapHeight = decodedImage->Height();
        payload.sourceWidth = decodedImage->SourceWidth();
        payload.sourceHeight = decodedImage->SourceHeight();
        if (!ExtractBgraPixelsFromBitmap(decodedImage->Bitmap(),
                                         decodedImage->Width(),
                                         decodedImage->Height(),
                                         &payload.bgraPixels,
                                         errorMessage))
        {
            return false;
        }

        return WriteRawHelperPayload(invocation.outputFilePath, payload, errorMessage);
    }
#endif

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

        return L"nvJPEG active for JPEG thumbnails";
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
        return normalized == L"arw"
            || normalized == L"cr2"
            || normalized == L"cr3"
            || normalized == L"dng"
            || normalized == L"nef"
            || normalized == L"nrw"
            || normalized == L"raf"
            || normalized == L"rw2";
    }

    bool CanDecodeThumbnail(const browser::BrowserItem& item)
    {
        return IsWicFileType(item.fileType) || IsRawFileType(item.fileType);
    }

    bool CanDecodeFullImage(const browser::BrowserItem& item)
    {
        return IsWicFileType(item.fileType) || IsRawFileType(item.fileType);
    }

    ThumbnailDecodeFailureKind ClassifyThumbnailDecodeFailure(std::wstring_view errorMessage)
    {
        return ClassifyThumbnailFailureKindFromMessage(errorMessage);
    }

    std::shared_ptr<const cache::CachedThumbnail> DecodeThumbnailCpuOnly(const cache::ThumbnailCacheKey& key,
                                                                         std::wstring* errorMessage,
                                                                         ThumbnailDecodeFailureKind* failureKind)
    {
        hyperbrowse::util::Stopwatch stopwatch;
        if (failureKind)
        {
            *failureKind = ThumbnailDecodeFailureKind::None;
        }

        const std::wstring fileType = FileTypeFromPath(key.filePath);
        if (IsWicFileType(fileType))
        {
            auto thumbnail = WicThumbnailDecoder{}.Decode(key);
            const double elapsedMs = stopwatch.ElapsedMilliseconds();
            hyperbrowse::util::RecordTiming(L"thumbnail.decode.wic", elapsedMs);
            if (fileType == L"jpg" || fileType == L"jpeg")
            {
                hyperbrowse::util::RecordTiming(L"thumbnail.decode.jpeg.cpu", elapsedMs);
            }
            if (!thumbnail && failureKind)
            {
                *failureKind = ThumbnailDecodeFailureKind::DecodeFailed;
            }
            return thumbnail;
        }

        if (IsRawFileType(fileType))
        {
            if (auto thumbnail = TryDecodeRawThumbnailWithWic(key))
            {
                hyperbrowse::util::RecordTiming(L"thumbnail.decode.raw.wic", stopwatch.ElapsedMilliseconds());
                return thumbnail;
            }

#if defined(HYPERBROWSE_ENABLE_LIBRAW)
            ThumbnailDecodeFailureKind rawFailureKind = ThumbnailDecodeFailureKind::None;
            auto thumbnail = IsLibRawOutOfProcessEnabled() && IsLibRawHelperExecutableAvailable()
                ? DecodeRawThumbnailWithHelper(key, errorMessage, &rawFailureKind)
                : DecodeRawThumbnail(key, errorMessage, &rawFailureKind);
            hyperbrowse::util::RecordTiming(L"thumbnail.decode.raw", stopwatch.ElapsedMilliseconds());
            if (!thumbnail && failureKind)
            {
                *failureKind = rawFailureKind == ThumbnailDecodeFailureKind::None
                    ? ThumbnailDecodeFailureKind::DecodeFailed
                    : rawFailureKind;
            }
            return thumbnail;
#else
            if (errorMessage)
            {
                *errorMessage = L"This build does not include LibRaw support for the configured RAW thumbnail formats.";
            }
            if (failureKind)
            {
                *failureKind = ThumbnailDecodeFailureKind::DecodeFailed;
            }
            return {};
#endif
        }

        if (errorMessage)
        {
            *errorMessage = L"The selected file type is not supported for thumbnail decode.";
        }
        if (failureKind)
        {
            *failureKind = ThumbnailDecodeFailureKind::DecodeFailed;
        }
        return {};
    }

    std::shared_ptr<const cache::CachedThumbnail> DecodeThumbnail(const cache::ThumbnailCacheKey& key,
                                                                  std::wstring* errorMessage,
                                                                  ThumbnailDecodeFailureKind* failureKind)
    {
        hyperbrowse::util::Stopwatch stopwatch;
        if (failureKind)
        {
            *failureKind = ThumbnailDecodeFailureKind::None;
        }

        const std::wstring fileType = FileTypeFromPath(key.filePath);
        const bool isJpeg = fileType == L"jpg" || fileType == L"jpeg";
        if (isJpeg && IsNvJpegAccelerationEnabled() && IsNvJpegRuntimeAvailable())
        {
            std::wstring nvJpegError;
            auto thumbnail = NvJpegDecoder{}.Decode(key, &nvJpegError);
            if (thumbnail)
            {
                hyperbrowse::util::RecordTiming(L"thumbnail.decode.nvjpeg", stopwatch.ElapsedMilliseconds());
                return thumbnail;
            }

            if (errorMessage)
            {
                *errorMessage = std::move(nvJpegError);
            }
        }

        return DecodeThumbnailCpuOnly(key, errorMessage, failureKind);
    }

    std::vector<std::shared_ptr<const cache::CachedThumbnail>> DecodeThumbnailBatch(
        const std::vector<cache::ThumbnailCacheKey>& keys,
        std::vector<std::wstring>* errorMessages,
        std::vector<ThumbnailDecodeFailureKind>* failureKinds)
    {
        std::vector<std::shared_ptr<const cache::CachedThumbnail>> thumbnails(keys.size());
        if (errorMessages)
        {
            errorMessages->assign(keys.size(), std::wstring{});
        }
        if (failureKinds)
        {
            failureKinds->assign(keys.size(), ThumbnailDecodeFailureKind::None);
        }

        if (keys.empty())
        {
            return thumbnails;
        }

        bool canUseNvJpegBatch = keys.size() > 1
            && IsNvJpegAccelerationEnabled()
            && IsNvJpegRuntimeAvailable();
        if (canUseNvJpegBatch)
        {
            for (const cache::ThumbnailCacheKey& key : keys)
            {
                const std::wstring fileType = FileTypeFromPath(key.filePath);
                if (fileType != L"jpg" && fileType != L"jpeg")
                {
                    canUseNvJpegBatch = false;
                    break;
                }
            }
        }

        if (canUseNvJpegBatch)
        {
            hyperbrowse::util::Stopwatch stopwatch;
            std::vector<std::wstring> nvJpegErrors;
            thumbnails = NvJpegDecoder{}.DecodeBatch(keys, &nvJpegErrors);

            std::uint64_t successCount = 0;
            for (const auto& thumbnail : thumbnails)
            {
                if (thumbnail)
                {
                    ++successCount;
                }
            }

            const std::uint64_t batchImageCount = static_cast<std::uint64_t>(keys.size());
            const std::uint64_t fallbackCount = batchImageCount - successCount;
            hyperbrowse::util::RecordTiming(L"thumbnail.decode.nvjpeg.batch", stopwatch.ElapsedMilliseconds());
            hyperbrowse::util::IncrementCounter(L"thumbnail.decode.nvjpeg.batch.submissions");
            hyperbrowse::util::IncrementCounter(L"thumbnail.decode.nvjpeg.batch.images", batchImageCount);
            std::wstring batchSizeCounter = L"thumbnail.decode.nvjpeg.batch.size.";
            batchSizeCounter.append(std::to_wstring(static_cast<unsigned long long>(keys.size())));
            hyperbrowse::util::IncrementCounter(batchSizeCounter);
            hyperbrowse::util::IncrementCounter(L"thumbnail.decode.nvjpeg.batch.success_images", successCount);
            hyperbrowse::util::IncrementCounter(L"thumbnail.decode.nvjpeg.batch.fallback_images", fallbackCount);
            if (fallbackCount == 0)
            {
                hyperbrowse::util::IncrementCounter(L"thumbnail.decode.nvjpeg.batch.full_success_submissions");
            }
            else
            {
                hyperbrowse::util::IncrementCounter(L"thumbnail.decode.nvjpeg.batch.fallback_submissions");
            }

            for (std::size_t index = 0; index < keys.size(); ++index)
            {
                if (thumbnails[index])
                {
                    continue;
                }

                std::wstring fallbackError;
                ThumbnailDecodeFailureKind fallbackFailureKind = ThumbnailDecodeFailureKind::None;
                thumbnails[index] = DecodeThumbnail(keys[index], &fallbackError, &fallbackFailureKind);
                if (errorMessages)
                {
                    (*errorMessages)[index] = fallbackError.empty() ? nvJpegErrors[index] : std::move(fallbackError);
                }
                if (failureKinds)
                {
                    (*failureKinds)[index] = fallbackFailureKind;
                }
            }

            return thumbnails;
        }

        for (std::size_t index = 0; index < keys.size(); ++index)
        {
            std::wstring error;
            ThumbnailDecodeFailureKind batchFailureKind = ThumbnailDecodeFailureKind::None;
            thumbnails[index] = DecodeThumbnail(keys[index], &error, &batchFailureKind);
            if (errorMessages)
            {
                (*errorMessages)[index] = std::move(error);
            }
            if (failureKinds)
            {
                (*failureKinds)[index] = batchFailureKind;
            }
        }

        return thumbnails;
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
            if (auto image = TryDecodeRawFullImageWithWic(item))
            {
                hyperbrowse::util::RecordTiming(L"viewer.decode.raw.wic", stopwatch.ElapsedMilliseconds());
                return image;
            }

#if defined(HYPERBROWSE_ENABLE_LIBRAW)
            auto image = IsLibRawOutOfProcessEnabled() && IsLibRawHelperExecutableAvailable()
                ? DecodeRawFullImageWithHelper(item, errorMessage)
                : DecodeRawFullImage(item, errorMessage);
            hyperbrowse::util::RecordTiming(L"viewer.decode.raw", stopwatch.ElapsedMilliseconds());
            return image;
#else
            if (errorMessage)
            {
                *errorMessage = L"This build does not include LibRaw support for the configured RAW viewer formats.";
            }
            return {};
#endif
        }

        if (errorMessage)
        {
            *errorMessage = L"The current viewer build supports JPEG, PNG, GIF, TIFF, and RAW formats ARW, CR2, CR3, DNG, NEF, NRW, RAF, and RW2.";
        }
        return {};
    }
}