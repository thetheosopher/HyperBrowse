#include "services/ImageMetadataService.h"

#include <propsys.h>
#include <propvarutil.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include <algorithm>
#include <condition_variable>
#include <cwctype>
#include <cstring>
#include <thread>

#if defined(HYPERBROWSE_ENABLE_LIBRAW)
#include <libraw/libraw.h>
#endif

#include "decode/ImageDecoder.h"

namespace
{
    using Microsoft::WRL::ComPtr;

    template <typename TValue>
    void HashCombine(std::size_t* seed, const TValue& value)
    {
        const std::size_t hashedValue = std::hash<TValue>{}(value);
        *seed ^= hashedValue + 0x9e3779b9 + (*seed << 6) + (*seed >> 2);
    }

    std::wstring NormalizePath(std::wstring_view value)
    {
        std::wstring normalized(value);
        std::replace(normalized.begin(), normalized.end(), L'/', L'\\');
        std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](wchar_t character)
        {
            return static_cast<wchar_t>(towlower(character));
        });
        return normalized;
    }

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

    std::wstring PropertyToString(IPropertyStore* propertyStore, REFPROPERTYKEY key)
    {
        if (!propertyStore)
        {
            return {};
        }

        PROPVARIANT value;
        PropVariantInit(&value);
        const HRESULT getResult = propertyStore->GetValue(key, &value);
        if (FAILED(getResult) || value.vt == VT_EMPTY)
        {
            PropVariantClear(&value);
            return {};
        }

        PWSTR displayValue = nullptr;
        const HRESULT formatResult = PSFormatForDisplayAlloc(key, value, PDFF_DEFAULT, &displayValue);
        std::wstring formattedValue;
        if (SUCCEEDED(formatResult) && displayValue)
        {
            formattedValue = displayValue;
            CoTaskMemFree(displayValue);
        }

        PropVariantClear(&value);
        if (formattedValue == L"No data")
        {
            return {};
        }

        return formattedValue;
    }

    std::wstring PropertyToString(IPropertyStore* propertyStore, PCWSTR canonicalName)
    {
        PROPERTYKEY key{};
        if (FAILED(PSGetPropertyKeyFromName(canonicalName, &key)))
        {
            return {};
        }

        return PropertyToString(propertyStore, key);
    }

#if defined(HYPERBROWSE_ENABLE_LIBRAW)
    std::wstring WideText(const char* value)
    {
        if (!value || *value == '\0')
        {
            return {};
        }

        return std::wstring(value, value + std::strlen(value));
    }

    int OpenRawFile(LibRaw& processor, const std::wstring& filePath)
    {
#if defined(_WIN32) && defined(LIBRAW_WIN32_UNICODEPATHS)
        return processor.open_file(filePath.c_str());
#else
        return processor.open_file(std::string(filePath.begin(), filePath.end()).c_str());
#endif
    }

    std::wstring FormatTimestamp(time_t timestamp)
    {
        if (timestamp <= 0)
        {
            return {};
        }

        const __time64_t rawTimestamp = static_cast<__time64_t>(timestamp);
        std::tm localTime{};
        if (_localtime64_s(&localTime, &rawTimestamp) != 0)
        {
            return {};
        }

        wchar_t buffer[64]{};
        wcsftime(buffer, std::size(buffer), L"%Y-%m-%d %H:%M", &localTime);
        return buffer;
    }

    void PopulateRawFallback(const hyperbrowse::browser::BrowserItem& item, hyperbrowse::services::ImageMetadata* metadata)
    {
        if (!metadata || !hyperbrowse::decode::IsRawFileType(item.fileType))
        {
            return;
        }

        LibRaw processor;
        if (OpenRawFile(processor, item.filePath) != LIBRAW_SUCCESS)
        {
            return;
        }

        if (processor.adjust_sizes_info_only() != LIBRAW_SUCCESS)
        {
            return;
        }

        if (metadata->cameraMake.empty())
        {
            metadata->cameraMake = WideText(processor.imgdata.idata.make);
        }
        if (metadata->cameraModel.empty())
        {
            metadata->cameraModel = WideText(processor.imgdata.idata.model);
        }
        if (metadata->dateTaken.empty())
        {
            metadata->dateTaken = FormatTimestamp(processor.imgdata.other.timestamp);
        }
        if (metadata->exposureTime.empty() && processor.imgdata.other.shutter > 0.0f)
        {
            metadata->exposureTime = std::to_wstring(processor.imgdata.other.shutter) + L" s";
        }
        if (metadata->fNumber.empty() && processor.imgdata.other.aperture > 0.0f)
        {
            metadata->fNumber = L"f/" + std::to_wstring(processor.imgdata.other.aperture);
        }
        if (metadata->isoSpeed.empty() && processor.imgdata.other.iso_speed > 0.0f)
        {
            metadata->isoSpeed = std::to_wstring(static_cast<int>(processor.imgdata.other.iso_speed));
        }
        if (metadata->focalLength.empty() && processor.imgdata.other.focal_len > 0.0f)
        {
            metadata->focalLength = std::to_wstring(processor.imgdata.other.focal_len) + L" mm";
        }

        metadata->hasExif = metadata->hasExif
            || !metadata->cameraMake.empty()
            || !metadata->cameraModel.empty()
            || !metadata->dateTaken.empty()
            || !metadata->exposureTime.empty()
            || !metadata->fNumber.empty()
            || !metadata->isoSpeed.empty()
            || !metadata->focalLength.empty();
    }
#endif

    std::wstring JoinLine(std::wstring label, const std::wstring& value)
    {
        if (value.empty())
        {
            return {};
        }

        label.append(value);
        label.append(L"\r\n");
        return label;
    }
}

namespace hyperbrowse::services
{
    std::shared_ptr<const ImageMetadata> ExtractImageMetadata(const browser::BrowserItem& item,
                                                              std::wstring* errorMessage)
    {
        ComScope comScope;

        auto metadata = std::make_shared<ImageMetadata>();
        ComPtr<IPropertyStore> propertyStore;
        const HRESULT storeResult = SHGetPropertyStoreFromParsingName(item.filePath.c_str(),
                                                                      nullptr,
                                                                      GPS_BESTEFFORT,
                                                                      IID_PPV_ARGS(propertyStore.GetAddressOf()));
        if (SUCCEEDED(storeResult) && propertyStore)
        {
            metadata->cameraMake = PropertyToString(propertyStore.Get(), L"System.Photo.CameraManufacturer");
            metadata->cameraModel = PropertyToString(propertyStore.Get(), L"System.Photo.CameraModel");
            metadata->dateTaken = PropertyToString(propertyStore.Get(), L"System.Photo.DateTaken");
            metadata->exposureTime = PropertyToString(propertyStore.Get(), L"System.Photo.ExposureTime");
            metadata->fNumber = PropertyToString(propertyStore.Get(), L"System.Photo.FNumber");
            metadata->isoSpeed = PropertyToString(propertyStore.Get(), L"System.Photo.ISOSpeed");
            metadata->focalLength = PropertyToString(propertyStore.Get(), L"System.Photo.FocalLength");
            metadata->title = PropertyToString(propertyStore.Get(), L"System.Title");
            metadata->author = PropertyToString(propertyStore.Get(), L"System.Author");
            metadata->keywords = PropertyToString(propertyStore.Get(), L"System.Keywords");
            metadata->comment = PropertyToString(propertyStore.Get(), L"System.Comment");
        }

#if defined(HYPERBROWSE_ENABLE_LIBRAW)
        PopulateRawFallback(item, metadata.get());
#endif

        metadata->hasExif = metadata->hasExif
            || !metadata->cameraMake.empty()
            || !metadata->cameraModel.empty()
            || !metadata->dateTaken.empty()
            || !metadata->exposureTime.empty()
            || !metadata->fNumber.empty()
            || !metadata->isoSpeed.empty()
            || !metadata->focalLength.empty();
        metadata->hasIptc = !metadata->author.empty() || !metadata->keywords.empty() || !metadata->comment.empty();
        metadata->hasXmp = !metadata->title.empty() || !metadata->author.empty() || !metadata->keywords.empty() || !metadata->comment.empty();

        if (!metadata->hasExif && !metadata->hasIptc && !metadata->hasXmp)
        {
            if (errorMessage)
            {
                *errorMessage = L"No EXIF, IPTC, or XMP metadata was available for this image.";
            }
        }

        return metadata;
    }

    std::wstring FormatImageMetadataReport(const browser::BrowserItem& item, const ImageMetadata& metadata)
    {
        std::wstring report;
        report.reserve(1024);
        report.append(L"File: ");
        report.append(item.fileName);
        report.append(L"\r\nPath: ");
        report.append(item.filePath);
        report.append(L"\r\nType: ");
        report.append(item.fileType);
        report.append(L"\r\nSize: ");
        report.append(browser::FormatByteSize(item.fileSizeBytes));
        report.append(L"\r\nDimensions: ");
        report.append(browser::FormatDimensionsForItem(item));
        report.append(L"\r\n\r\nEXIF\r\n");
        report.append(JoinLine(L"Camera Make: ", metadata.cameraMake));
        report.append(JoinLine(L"Camera Model: ", metadata.cameraModel));
        report.append(JoinLine(L"Date Taken: ", metadata.dateTaken));
        report.append(JoinLine(L"Exposure: ", metadata.exposureTime));
        report.append(JoinLine(L"Aperture: ", metadata.fNumber));
        report.append(JoinLine(L"ISO: ", metadata.isoSpeed));
        report.append(JoinLine(L"Focal Length: ", metadata.focalLength));
        report.append(L"\r\nIPTC / XMP\r\n");
        report.append(JoinLine(L"Title: ", metadata.title));
        report.append(JoinLine(L"Author: ", metadata.author));
        report.append(JoinLine(L"Keywords: ", metadata.keywords));
        report.append(JoinLine(L"Comment: ", metadata.comment));
        return report;
    }

    std::size_t ImageMetadataService::MetadataCacheKeyHasher::operator()(const MetadataCacheKey& key) const noexcept
    {
        std::size_t seed = 0;
        HashCombine(&seed, NormalizePath(key.filePath));
        HashCombine(&seed, key.modifiedTimestampUtc);
        return seed;
    }

    ImageMetadataService::ImageMetadataService(std::size_t workerCount)
    {
        const std::size_t resolvedWorkerCount = std::max<std::size_t>(1, workerCount);
        workers_.reserve(resolvedWorkerCount);
        for (std::size_t index = 0; index < resolvedWorkerCount; ++index)
        {
            workers_.emplace_back([this]()
            {
                WorkerLoop();
            });
        }
    }

    ImageMetadataService::~ImageMetadataService()
    {
        {
            std::scoped_lock lock(mutex_);
            shuttingDown_ = true;
            pendingJobs_.clear();
            queuedKeys_.clear();
        }

        workAvailable_.notify_all();
        for (std::thread& worker : workers_)
        {
            if (worker.joinable())
            {
                worker.join();
            }
        }
    }

    void ImageMetadataService::BindTargetWindow(HWND targetWindow)
    {
        std::scoped_lock lock(mutex_);
        targetWindow_ = targetWindow;
    }

    void ImageMetadataService::Schedule(std::uint64_t sessionId, MetadataWorkItem workItem)
    {
        const MetadataCacheKey key{workItem.item.filePath, workItem.item.modifiedTimestampUtc};

        {
            std::scoped_lock lock(mutex_);
            activeSessionId_ = sessionId;
            if (cache_.contains(key) || queuedKeys_.contains(key) || inflightKeys_.contains(key))
            {
                return;
            }

            pendingJobs_.push_back(PendingJob{sessionId, nextSequence_++, std::move(workItem)});
            queuedKeys_.insert(key);
        }

        workAvailable_.notify_one();
    }

    void ImageMetadataService::CancelOutstanding()
    {
        std::scoped_lock lock(mutex_);
        ++activeSessionId_;
        pendingJobs_.clear();
        queuedKeys_.clear();
    }

    std::shared_ptr<const ImageMetadata> ImageMetadataService::FindCachedMetadata(const browser::BrowserItem& item) const
    {
        const MetadataCacheKey key{item.filePath, item.modifiedTimestampUtc};
        std::scoped_lock lock(mutex_);
        const auto iterator = cache_.find(key);
        return iterator == cache_.end() ? nullptr : iterator->second;
    }

    void ImageMetadataService::InvalidateFilePaths(const std::vector<std::wstring>& filePaths)
    {
        if (filePaths.empty())
        {
            return;
        }

        const std::vector<std::wstring> normalizedPaths = [&]()
        {
            std::vector<std::wstring> paths;
            paths.reserve(filePaths.size());
            for (const std::wstring& filePath : filePaths)
            {
                paths.push_back(NormalizePath(filePath));
            }
            return paths;
        }();

        std::scoped_lock lock(mutex_);
        for (auto iterator = cache_.begin(); iterator != cache_.end();)
        {
            const bool shouldErase = std::find(normalizedPaths.begin(), normalizedPaths.end(), NormalizePath(iterator->first.filePath))
                != normalizedPaths.end();
            if (shouldErase)
            {
                iterator = cache_.erase(iterator);
            }
            else
            {
                ++iterator;
            }
        }
    }

    void ImageMetadataService::WorkerLoop()
    {
        while (true)
        {
            PendingJob job;
            {
                std::unique_lock lock(mutex_);
                workAvailable_.wait(lock, [this]()
                {
                    return shuttingDown_ || !pendingJobs_.empty();
                });

                if (shuttingDown_)
                {
                    return;
                }

                const auto jobIterator = std::min_element(pendingJobs_.begin(), pendingJobs_.end(), [](const PendingJob& lhs, const PendingJob& rhs)
                {
                    if (lhs.workItem.priority != rhs.workItem.priority)
                    {
                        return lhs.workItem.priority < rhs.workItem.priority;
                    }
                    return lhs.sequence < rhs.sequence;
                });

                job = *jobIterator;
                queuedKeys_.erase(MetadataCacheKey{job.workItem.item.filePath, job.workItem.item.modifiedTimestampUtc});
                inflightKeys_.insert(MetadataCacheKey{job.workItem.item.filePath, job.workItem.item.modifiedTimestampUtc});
                pendingJobs_.erase(jobIterator);
            }

            std::wstring errorMessage;
            std::shared_ptr<const ImageMetadata> metadata = ExtractImageMetadata(job.workItem.item, &errorMessage);

            bool shouldNotify = false;
            {
                std::scoped_lock lock(mutex_);
                const MetadataCacheKey key{job.workItem.item.filePath, job.workItem.item.modifiedTimestampUtc};
                inflightKeys_.erase(key);
                if (metadata)
                {
                    cache_[key] = metadata;
                }

                shouldNotify = metadata != nullptr && targetWindow_ != nullptr && job.sessionId == activeSessionId_;
            }

            if (shouldNotify)
            {
                PostReady(job.sessionId, job.workItem.modelIndex, job.workItem.item, metadata != nullptr);
            }
        }
    }

    void ImageMetadataService::PostReady(std::uint64_t sessionId, int modelIndex, const browser::BrowserItem& item, bool success) const
    {
        HWND targetWindow = nullptr;
        {
            std::scoped_lock lock(mutex_);
            targetWindow = targetWindow_;
        }

        if (!targetWindow)
        {
            return;
        }

        auto update = std::make_unique<MetadataReadyUpdate>();
        update->sessionId = sessionId;
        update->modelIndex = modelIndex;
        update->item = item;
        update->success = success;
        if (!PostMessageW(targetWindow, kMessageId, 0, reinterpret_cast<LPARAM>(update.get())))
        {
            return;
        }

        update.release();
    }
}