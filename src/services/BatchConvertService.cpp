#include "services/BatchConvertService.h"

#include <objbase.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <filesystem>
#include <functional>
#include <vector>

#include "cache/ThumbnailCache.h"
#include "decode/ImageDecoder.h"
#include "util/Diagnostics.h"

namespace fs = std::filesystem;

namespace hyperbrowse::services
{
    std::wstring BatchConvertFormatExtension(BatchConvertFormat format);
}

namespace
{
    using Microsoft::WRL::ComPtr;

    class ComScope
    {
    public:
        ComScope()
        {
            const HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            shouldUninitialize_ = SUCCEEDED(result) || result == S_FALSE;
        }

        ~ComScope()
        {
            if (shouldUninitialize_)
            {
                CoUninitialize();
            }
        }

    private:
        bool shouldUninitialize_{};
    };

    void PostUpdate(HWND targetWindow,
                    std::unique_ptr<hyperbrowse::services::BatchConvertUpdate> update,
                    const std::atomic_bool* shutdown = nullptr)
    {
        if (!targetWindow || (shutdown && shutdown->load(std::memory_order_acquire)))
        {
            return;
        }

        if (!PostMessageW(targetWindow,
                          hyperbrowse::services::BatchConvertService::kMessageId,
                          0,
                          reinterpret_cast<LPARAM>(update.get())))
        {
            return;
        }

        update.release();
    }

    GUID ContainerFormatGuid(hyperbrowse::services::BatchConvertFormat format)
    {
        switch (format)
        {
        case hyperbrowse::services::BatchConvertFormat::Png:
            return GUID_ContainerFormatPng;
        case hyperbrowse::services::BatchConvertFormat::Tiff:
            return GUID_ContainerFormatTiff;
        case hyperbrowse::services::BatchConvertFormat::Jpeg:
        default:
            return GUID_ContainerFormatJpeg;
        }
    }

    WICPixelFormatGUID TargetPixelFormat(hyperbrowse::services::BatchConvertFormat format)
    {
        switch (format)
        {
        case hyperbrowse::services::BatchConvertFormat::Jpeg:
            return GUID_WICPixelFormat24bppBGR;
        case hyperbrowse::services::BatchConvertFormat::Png:
        case hyperbrowse::services::BatchConvertFormat::Tiff:
        default:
            return GUID_WICPixelFormat32bppBGRA;
        }
    }

    bool ReadBitmapPixels(const hyperbrowse::cache::CachedThumbnail& image,
                          std::vector<BYTE>* pixels,
                          int* width,
                          int* height,
                          std::wstring* errorMessage)
    {
        BITMAP bitmap{};
        if (GetObjectW(image.Bitmap(), sizeof(bitmap), &bitmap) == 0)
        {
            if (errorMessage)
            {
                *errorMessage = L"Failed to inspect the decoded bitmap for batch conversion.";
            }
            return false;
        }

        *width = bitmap.bmWidth;
        *height = bitmap.bmHeight;
        BITMAPINFO bitmapInfo{};
        bitmapInfo.bmiHeader.biSize = sizeof(bitmapInfo.bmiHeader);
        bitmapInfo.bmiHeader.biWidth = bitmap.bmWidth;
        bitmapInfo.bmiHeader.biHeight = -bitmap.bmHeight;
        bitmapInfo.bmiHeader.biPlanes = 1;
        bitmapInfo.bmiHeader.biBitCount = 32;
        bitmapInfo.bmiHeader.biCompression = BI_RGB;

        const UINT stride = static_cast<UINT>(bitmap.bmWidth * 4);
        pixels->assign(static_cast<std::size_t>(stride) * static_cast<std::size_t>(bitmap.bmHeight), 0);

        HDC screenDc = GetDC(nullptr);
        const int copiedLines = GetDIBits(screenDc,
                                          image.Bitmap(),
                                          0,
                                          static_cast<UINT>(bitmap.bmHeight),
                                          pixels->data(),
                                          &bitmapInfo,
                                          DIB_RGB_COLORS);
        ReleaseDC(nullptr, screenDc);

        if (copiedLines == 0)
        {
            if (errorMessage)
            {
                *errorMessage = L"Failed to read pixels from the decoded bitmap.";
            }
            return false;
        }

        return true;
    }

    bool EncodeImage(const hyperbrowse::cache::CachedThumbnail& image,
                     const fs::path& outputPath,
                     hyperbrowse::services::BatchConvertFormat format,
                     std::wstring* errorMessage)
    {
        ComScope comScope;

        ComPtr<IWICImagingFactory> factory;
        HRESULT result = CoCreateInstance(CLSID_WICImagingFactory,
                                          nullptr,
                                          CLSCTX_INPROC_SERVER,
                                          IID_PPV_ARGS(factory.GetAddressOf()));
        if (FAILED(result) || !factory)
        {
            if (errorMessage)
            {
                *errorMessage = L"Failed to create the WIC imaging factory for batch conversion.";
            }
            return false;
        }

        int width = 0;
        int height = 0;
        std::vector<BYTE> pixels;
        if (!ReadBitmapPixels(image, &pixels, &width, &height, errorMessage))
        {
            return false;
        }

        ComPtr<IWICBitmap> sourceBitmap;
        result = factory->CreateBitmapFromMemory(static_cast<UINT>(width),
                                                 static_cast<UINT>(height),
                                                 GUID_WICPixelFormat32bppBGRA,
                                                 static_cast<UINT>(width * 4),
                                                 static_cast<UINT>(pixels.size()),
                                                 pixels.data(),
                                                 &sourceBitmap);
        if (FAILED(result) || !sourceBitmap)
        {
            if (errorMessage)
            {
                *errorMessage = L"Failed to create a WIC bitmap from the decoded pixels.";
            }
            return false;
        }

        ComPtr<IWICBitmapSource> writeSource = sourceBitmap;
        const WICPixelFormatGUID targetFormat = TargetPixelFormat(format);
        if (!InlineIsEqualGUID(targetFormat, GUID_WICPixelFormat32bppBGRA))
        {
            ComPtr<IWICFormatConverter> converter;
            result = factory->CreateFormatConverter(&converter);
            if (FAILED(result) || FAILED(converter->Initialize(sourceBitmap.Get(),
                                                               targetFormat,
                                                               WICBitmapDitherTypeNone,
                                                               nullptr,
                                                               0.0,
                                                               WICBitmapPaletteTypeCustom)))
            {
                if (errorMessage)
                {
                    *errorMessage = L"Failed to convert pixels into the requested export format.";
                }
                return false;
            }

            writeSource = converter;
        }

        ComPtr<IWICStream> stream;
        result = factory->CreateStream(&stream);
        if (FAILED(result) || FAILED(stream->InitializeFromFilename(outputPath.c_str(), GENERIC_WRITE)))
        {
            if (errorMessage)
            {
                *errorMessage = L"Failed to open the output file for batch conversion.";
            }
            return false;
        }

        ComPtr<IWICBitmapEncoder> encoder;
        result = factory->CreateEncoder(ContainerFormatGuid(format), nullptr, &encoder);
        if (FAILED(result) || FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)))
        {
            if (errorMessage)
            {
                *errorMessage = L"Failed to initialize the WIC encoder for batch conversion.";
            }
            return false;
        }

        ComPtr<IWICBitmapFrameEncode> frame;
        ComPtr<IPropertyBag2> propertyBag;
        result = encoder->CreateNewFrame(&frame, &propertyBag);
        if (FAILED(result) || !frame)
        {
            if (errorMessage)
            {
                *errorMessage = L"Failed to create the WIC output frame.";
            }
            return false;
        }

        if (propertyBag && format == hyperbrowse::services::BatchConvertFormat::Jpeg)
        {
            PROPBAG2 option{};
            option.pstrName = const_cast<LPOLESTR>(L"ImageQuality");
            VARIANT value;
            VariantInit(&value);
            value.vt = VT_R4;
            value.fltVal = 0.92f;
            propertyBag->Write(1, &option, &value);
            VariantClear(&value);
        }

        WICPixelFormatGUID pixelFormat = targetFormat;
        result = frame->Initialize(propertyBag.Get());
        result = SUCCEEDED(result) ? frame->SetSize(static_cast<UINT>(width), static_cast<UINT>(height)) : result;
        result = SUCCEEDED(result) ? frame->SetPixelFormat(&pixelFormat) : result;
        result = SUCCEEDED(result) ? frame->WriteSource(writeSource.Get(), nullptr) : result;
        result = SUCCEEDED(result) ? frame->Commit() : result;
        result = SUCCEEDED(result) ? encoder->Commit() : result;
        if (FAILED(result))
        {
            if (errorMessage)
            {
                *errorMessage = L"Failed to write the converted image file.";
            }
            return false;
        }

        return true;
    }

    fs::path OutputPathForSuffix(const fs::path& outputFolder,
                                 const hyperbrowse::browser::BrowserItem& item,
                                 hyperbrowse::services::BatchConvertFormat format,
                                 int suffix)
    {
        std::wstring fileName = fs::path(item.fileName).stem().wstring();
        if (suffix > 0)
        {
            fileName.push_back(L'_');
            fileName.append(std::to_wstring(suffix));
        }
        fileName.append(hyperbrowse::services::BatchConvertFormatExtension(format));
        return outputFolder / fileName;
    }

    fs::path CreateTemporaryOutputPath(const fs::path& outputFolder, std::wstring* errorMessage)
    {
        static std::atomic_uint64_t nextTemporaryId{0};
        for (unsigned int attempt = 0; attempt < 32; ++attempt)
        {
            const fs::path temporaryPath = outputFolder
                / (L".hyperbrowse-convert-"
                   + std::to_wstring(GetCurrentProcessId())
                   + L"-"
                   + std::to_wstring(GetCurrentThreadId())
                   + L"-"
                   + std::to_wstring(nextTemporaryId.fetch_add(1, std::memory_order_relaxed) + 1)
                   + L".tmp");
            const HANDLE file = CreateFileW(
                temporaryPath.c_str(),
                GENERIC_WRITE,
                0,
                nullptr,
                CREATE_NEW,
                FILE_ATTRIBUTE_TEMPORARY,
                nullptr);
            if (file != INVALID_HANDLE_VALUE)
            {
                CloseHandle(file);
                return temporaryPath;
            }
            const DWORD error = GetLastError();
            if (error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS)
            {
                if (errorMessage)
                {
                    *errorMessage = L"Failed to reserve a temporary output file (error "
                        + std::to_wstring(error)
                        + L").";
                }
                return {};
            }
        }

        if (errorMessage)
        {
            *errorMessage = L"No temporary output filename was available.";
        }
        return {};
    }

    bool EncodeAndPublishImage(
        const hyperbrowse::cache::CachedThumbnail& image,
        const fs::path& outputFolder,
        const hyperbrowse::browser::BrowserItem& item,
        hyperbrowse::services::BatchConvertFormat format,
        const std::function<bool()>& isCancelled,
        bool* cancelled,
        std::wstring* errorMessage)
    {
        *cancelled = false;
        const fs::path temporaryPath = CreateTemporaryOutputPath(outputFolder, errorMessage);
        if (temporaryPath.empty())
        {
            return false;
        }

        bool succeeded = EncodeImage(image, temporaryPath, format, errorMessage);
        if (succeeded && isCancelled())
        {
            *cancelled = true;
            succeeded = false;
        }

        if (succeeded)
        {
            for (int suffix = 0; suffix < 1000; ++suffix)
            {
                const fs::path candidate = OutputPathForSuffix(outputFolder, item, format, suffix);
                if (MoveFileExW(temporaryPath.c_str(), candidate.c_str(), MOVEFILE_WRITE_THROUGH))
                {
                    return true;
                }

                const DWORD error = GetLastError();
                if (error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS)
                {
                    if (errorMessage)
                    {
                        *errorMessage = L"Failed to publish the converted image (error "
                            + std::to_wstring(error)
                            + L").";
                    }
                    succeeded = false;
                    break;
                }
                if (isCancelled())
                {
                    *cancelled = true;
                    succeeded = false;
                    break;
                }
            }
            if (succeeded)
            {
                if (errorMessage)
                {
                    *errorMessage = L"No available output filename could be selected.";
                }
                succeeded = false;
            }
        }

        DeleteFileW(temporaryPath.c_str());
        return false;
    }
}

namespace hyperbrowse::services
{
    std::wstring BatchConvertFormatToLabel(BatchConvertFormat format)
    {
        switch (format)
        {
        case BatchConvertFormat::Png:
            return L"PNG";
        case BatchConvertFormat::Tiff:
            return L"TIFF";
        case BatchConvertFormat::Jpeg:
        default:
            return L"JPEG";
        }
    }

    std::wstring BatchConvertFormatExtension(BatchConvertFormat format)
    {
        switch (format)
        {
        case BatchConvertFormat::Png:
            return L".png";
        case BatchConvertFormat::Tiff:
            return L".tif";
        case BatchConvertFormat::Jpeg:
        default:
            return L".jpg";
        }
    }

    BatchConvertService::BatchConvertService()
        : sharedState_(std::make_shared<SharedState>())
        , executor_(1, 1)
    {
    }

    BatchConvertService::~BatchConvertService()
    {
        Shutdown();
    }

    std::uint64_t BatchConvertService::Start(HWND targetWindow,
                                             std::vector<browser::BrowserItem> items,
                                             std::wstring outputFolder,
                                             BatchConvertFormat format)
    {
        Cancel();

        const std::uint64_t requestId = nextRequestId_.fetch_add(1, std::memory_order_acq_rel) + 1;
        sharedState_->activeRequestId.store(requestId, std::memory_order_release);
        const std::size_t requestedItemCount = items.size();
        const std::wstring requestedOutputFolder = outputFolder;
        const bool accepted = executor_.Post(
            [sharedState = sharedState_,
             targetWindow,
             items = std::move(items),
             outputFolder = std::move(outputFolder),
             format,
             requestId]() mutable
        {
            std::size_t completedCount = 0;
            std::size_t succeededCount = 0;
            std::size_t failedCount = 0;
            try
            {
            const auto isCancelled = [&]()
            {
                return sharedState->shutdown.load(std::memory_order_acquire)
                    || sharedState->activeRequestId.load(std::memory_order_acquire) != requestId;
            };

            const auto postCancelled = [&]()
            {
                auto update = std::make_unique<BatchConvertUpdate>();
                update->requestId = requestId;
                update->completedCount = completedCount;
                update->totalCount = items.size();
                update->succeededCount = succeededCount;
                update->failedCount = failedCount;
                update->format = format;
                update->outputFolder = outputFolder;
                update->cancelled = true;
                update->finished = true;
                PostUpdate(targetWindow, std::move(update), &sharedState->shutdown);
            };

            std::error_code directoryError;
            fs::create_directories(outputFolder, directoryError);
            if (directoryError)
            {
                auto update = std::make_unique<BatchConvertUpdate>();
                update->requestId = requestId;
                update->completedCount = items.size();
                update->totalCount = items.size();
                update->succeededCount = 0;
                update->failedCount = items.size();
                update->format = format;
                update->outputFolder = outputFolder;
                update->finished = true;
                update->message = L"Failed to create the output folder for batch conversion.";
                PostUpdate(targetWindow, std::move(update), &sharedState->shutdown);
                return;
            }

            for (const browser::BrowserItem& item : items)
            {
                if (isCancelled())
                {
                    postCancelled();
                    return;
                }

                std::wstring errorMessage;
                const auto decodedImage = decode::DecodeFullImage(item, &errorMessage);
                if (isCancelled())
                {
                    postCancelled();
                    return;
                }

                if (!decodedImage)
                {
                    ++failedCount;
                }
                else
                {
                    bool cancelled = false;
                    if (!EncodeAndPublishImage(
                            *decodedImage,
                            fs::path(outputFolder),
                            item,
                            format,
                            isCancelled,
                            &cancelled,
                            &errorMessage))
                    {
                        if (cancelled)
                        {
                            postCancelled();
                            return;
                        }
                        ++failedCount;
                    }
                    else
                    {
                        ++succeededCount;
                    }
                }

                if (isCancelled())
                {
                    postCancelled();
                    return;
                }

                ++completedCount;
                auto progress = std::make_unique<BatchConvertUpdate>();
                progress->requestId = requestId;
                progress->completedCount = completedCount;
                progress->totalCount = items.size();
                progress->succeededCount = succeededCount;
                progress->failedCount = failedCount;
                progress->format = format;
                progress->outputFolder = outputFolder;
                progress->currentFileName = item.fileName;
                progress->message = errorMessage;
                progress->finished = completedCount == items.size();
                PostUpdate(targetWindow, std::move(progress), &sharedState->shutdown);
            }
            }
            catch (const std::exception&)
            {
                auto update = std::make_unique<BatchConvertUpdate>();
                update->requestId = requestId;
                update->completedCount = items.size();
                update->totalCount = items.size();
                update->succeededCount = succeededCount;
                update->failedCount = items.size() - succeededCount;
                update->format = format;
                update->outputFolder = outputFolder;
                update->finished = true;
                update->message = L"Batch conversion failed unexpectedly.";
                PostUpdate(targetWindow, std::move(update), &sharedState->shutdown);
            }
            catch (...)
            {
                auto update = std::make_unique<BatchConvertUpdate>();
                update->requestId = requestId;
                update->completedCount = items.size();
                update->totalCount = items.size();
                update->succeededCount = succeededCount;
                update->failedCount = items.size() - succeededCount;
                update->format = format;
                update->outputFolder = outputFolder;
                update->finished = true;
                update->message = L"Batch conversion failed unexpectedly.";
                PostUpdate(targetWindow, std::move(update), &sharedState->shutdown);
            }
        });
        if (!accepted)
        {
            util::IncrementCounter(L"service.batch_convert.queue_rejected");
            auto update = std::make_unique<BatchConvertUpdate>();
            update->requestId = requestId;
            update->completedCount = requestedItemCount;
            update->totalCount = requestedItemCount;
            update->succeededCount = 0;
            update->failedCount = requestedItemCount;
            update->format = format;
            update->outputFolder = requestedOutputFolder;
            update->finished = true;
            update->message = L"Batch conversion could not be queued.";
            PostUpdate(targetWindow, std::move(update), &sharedState_->shutdown);
        }
        util::RecordMaximum(L"service.batch_convert.queue_depth_peak",
                            executor_.PeakPendingTaskCount());
        return requestId;
    }

    void BatchConvertService::Cancel() noexcept
    {
        sharedState_->activeRequestId.fetch_add(1, std::memory_order_acq_rel);
        cancellationCount_.fetch_add(1, std::memory_order_relaxed);
        util::IncrementCounter(L"service.batch_convert.cancelled");
    }

    void BatchConvertService::Shutdown() noexcept
    {
        if (!sharedState_->shutdown.exchange(true, std::memory_order_acq_rel))
        {
            Cancel();
            executor_.Shutdown();
        }
    }
}
