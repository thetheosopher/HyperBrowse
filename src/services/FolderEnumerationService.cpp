#include "services/FolderEnumerationService.h"

#include <cwctype>
#include <filesystem>
#include <string_view>
#include <system_error>
#include <thread>

#include "util/Log.h"

namespace fs = std::filesystem;

namespace hyperbrowse::services
{
    struct FolderEnumerationSharedState
    {
        std::atomic_uint64_t activeRequestId{0};
        std::atomic_bool shutdown{false};
    };
}

namespace
{
    constexpr std::size_t kBatchSize = 64;
    constexpr int kPlaceholderWidth = 256;
    constexpr int kPlaceholderHeight = 256;

    struct EnumerationSharedStateView
    {
        std::shared_ptr<hyperbrowse::services::FolderEnumerationSharedState> state;
        HWND targetWindow{};
        std::uint64_t requestId{};
    };

    std::wstring ToLowercase(std::wstring_view value)
    {
        std::wstring lowercased;
        lowercased.reserve(value.size());
        for (wchar_t character : value)
        {
            lowercased.push_back(static_cast<wchar_t>(towlower(character)));
        }

        return lowercased;
    }

    bool IsSupportedImageExtension(std::wstring_view extension)
    {
        const std::wstring normalized = ToLowercase(extension);
        return normalized == L".jpg"
            || normalized == L".jpeg"
            || normalized == L".png"
            || normalized == L".gif"
            || normalized == L".tif"
            || normalized == L".tiff"
            || normalized == L".nef"
            || normalized == L".nrw";
    }

    bool ShouldStop(const EnumerationSharedStateView& stateView)
    {
        return stateView.state->shutdown.load(std::memory_order_acquire)
            || stateView.state->activeRequestId.load(std::memory_order_acquire) != stateView.requestId;
    }

    std::wstring FormatFileTime(const FILETIME& fileTime)
    {
        FILETIME localFileTime{};
        SYSTEMTIME systemTime{};
        if (!FileTimeToLocalFileTime(&fileTime, &localFileTime)
            || !FileTimeToSystemTime(&localFileTime, &systemTime))
        {
            return L"Unavailable";
        }

        wchar_t buffer[64]{};
        swprintf_s(buffer,
                   L"%04u-%02u-%02u %02u:%02u",
                   systemTime.wYear,
                   systemTime.wMonth,
                   systemTime.wDay,
                   systemTime.wHour,
                   systemTime.wMinute);
        return buffer;
    }

    hyperbrowse::browser::BrowserItem BuildBrowserItem(const fs::path& path)
    {
        hyperbrowse::browser::BrowserItem item;
        item.fileName = path.filename().wstring();
        item.filePath = path.wstring();
        item.fileType = ToLowercase(path.extension().wstring());
        if (!item.fileType.empty() && item.fileType.front() == L'.')
        {
            item.fileType.erase(item.fileType.begin());
        }
        if (item.fileType.empty())
        {
            item.fileType = L"unknown";
        }

        std::transform(item.fileType.begin(), item.fileType.end(), item.fileType.begin(), [](wchar_t character)
        {
            return static_cast<wchar_t>(towupper(character));
        });

        item.placeholderWidth = kPlaceholderWidth;
        item.placeholderHeight = kPlaceholderHeight;

        WIN32_FILE_ATTRIBUTE_DATA attributeData{};
        if (GetFileAttributesExW(item.filePath.c_str(), GetFileExInfoStandard, &attributeData) != FALSE)
        {
            ULARGE_INTEGER fileSize{};
            fileSize.HighPart = attributeData.nFileSizeHigh;
            fileSize.LowPart = attributeData.nFileSizeLow;
            item.fileSizeBytes = fileSize.QuadPart;
            item.modifiedDate = FormatFileTime(attributeData.ftLastWriteTime);

            ULARGE_INTEGER modifiedTimestamp{};
            modifiedTimestamp.HighPart = attributeData.ftLastWriteTime.dwHighDateTime;
            modifiedTimestamp.LowPart = attributeData.ftLastWriteTime.dwLowDateTime;
            item.modifiedTimestampUtc = modifiedTimestamp.QuadPart;
        }
        else
        {
            item.modifiedDate = L"Unavailable";
        }

        return item;
    }

    void PostUpdate(HWND targetWindow, std::unique_ptr<hyperbrowse::services::FolderEnumerationUpdate> update)
    {
        if (!targetWindow)
        {
            return;
        }

        if (!PostMessageW(targetWindow,
                          hyperbrowse::services::FolderEnumerationService::kMessageId,
                          0,
                          reinterpret_cast<LPARAM>(update.get())))
        {
            return;
        }

        update.release();
    }

    void FlushBatch(const EnumerationSharedStateView& stateView,
                    const std::wstring& folderPath,
                    std::vector<hyperbrowse::browser::BrowserItem>* batch,
                    std::uint64_t totalCount,
                    std::uint64_t totalBytes)
    {
        if (batch->empty() || ShouldStop(stateView))
        {
            return;
        }

        auto update = std::make_unique<hyperbrowse::services::FolderEnumerationUpdate>();
        update->requestId = stateView.requestId;
        update->kind = hyperbrowse::services::FolderEnumerationUpdateKind::Batch;
        update->folderPath = folderPath;
        update->items = std::move(*batch);
        update->totalCount = totalCount;
        update->totalBytes = totalBytes;
        PostUpdate(stateView.targetWindow, std::move(update));
        batch->clear();
    }

    void PostFailure(const EnumerationSharedStateView& stateView,
                     const std::wstring& folderPath,
                     std::wstring message)
    {
        if (ShouldStop(stateView))
        {
            return;
        }

        auto update = std::make_unique<hyperbrowse::services::FolderEnumerationUpdate>();
        update->requestId = stateView.requestId;
        update->kind = hyperbrowse::services::FolderEnumerationUpdateKind::Failed;
        update->folderPath = folderPath;
        update->message = std::move(message);
        PostUpdate(stateView.targetWindow, std::move(update));
    }

    void PostCompletion(const EnumerationSharedStateView& stateView,
                        const std::wstring& folderPath,
                        std::uint64_t totalCount,
                        std::uint64_t totalBytes)
    {
        if (ShouldStop(stateView))
        {
            return;
        }

        auto update = std::make_unique<hyperbrowse::services::FolderEnumerationUpdate>();
        update->requestId = stateView.requestId;
        update->kind = hyperbrowse::services::FolderEnumerationUpdateKind::Completed;
        update->folderPath = folderPath;
        update->totalCount = totalCount;
        update->totalBytes = totalBytes;
        PostUpdate(stateView.targetWindow, std::move(update));
    }

    void HandleEntry(const EnumerationSharedStateView& stateView,
                     const std::wstring& folderPath,
                     const fs::directory_entry& entry,
                     std::vector<hyperbrowse::browser::BrowserItem>* batch,
                     std::uint64_t* totalCount,
                     std::uint64_t* totalBytes)
    {
        std::error_code statusError;
        if (!entry.is_regular_file(statusError) || statusError)
        {
            return;
        }

        if (!IsSupportedImageExtension(entry.path().extension().wstring()))
        {
            return;
        }

        hyperbrowse::browser::BrowserItem item = BuildBrowserItem(entry.path());
        *totalBytes += item.fileSizeBytes;
        ++(*totalCount);
        batch->push_back(std::move(item));

        if (batch->size() >= kBatchSize)
        {
            FlushBatch(stateView, folderPath, batch, *totalCount, *totalBytes);
        }
    }

    void EnumerateFolder(const EnumerationSharedStateView& stateView,
                         const std::wstring& folderPath,
                         bool recursive)
    {
        try
        {
            const fs::path basePath(folderPath);
            std::error_code existsError;
            if (!fs::exists(basePath, existsError) || existsError)
            {
                PostFailure(stateView, folderPath, L"The selected folder no longer exists.");
                return;
            }

            std::vector<hyperbrowse::browser::BrowserItem> batch;
            batch.reserve(kBatchSize);
            std::uint64_t totalCount = 0;
            std::uint64_t totalBytes = 0;
            const fs::directory_options options = fs::directory_options::skip_permission_denied;

            if (recursive)
            {
                std::error_code iteratorError;
                for (fs::recursive_directory_iterator iterator(basePath, options, iteratorError), end;
                     iterator != end;
                     iterator.increment(iteratorError))
                {
                    if (ShouldStop(stateView))
                    {
                        return;
                    }

                    if (iteratorError)
                    {
                        iteratorError.clear();
                        continue;
                    }

                    HandleEntry(stateView, folderPath, *iterator, &batch, &totalCount, &totalBytes);
                }
            }
            else
            {
                std::error_code iteratorError;
                for (fs::directory_iterator iterator(basePath, options, iteratorError), end;
                     iterator != end;
                     iterator.increment(iteratorError))
                {
                    if (ShouldStop(stateView))
                    {
                        return;
                    }

                    if (iteratorError)
                    {
                        iteratorError.clear();
                        continue;
                    }

                    HandleEntry(stateView, folderPath, *iterator, &batch, &totalCount, &totalBytes);
                }
            }

            FlushBatch(stateView, folderPath, &batch, totalCount, totalBytes);
            PostCompletion(stateView, folderPath, totalCount, totalBytes);
        }
        catch (const std::exception& exception)
        {
            const std::string message = exception.what();
            PostFailure(stateView,
                        folderPath,
                        L"Folder enumeration failed: " + std::wstring(message.begin(), message.end()));
        }
    }
}

namespace hyperbrowse::services
{
    FolderEnumerationService::FolderEnumerationService()
        : sharedState_(std::make_shared<FolderEnumerationSharedState>())
    {
    }

    FolderEnumerationService::~FolderEnumerationService()
    {
        sharedState_->shutdown.store(true, std::memory_order_release);
        Cancel();
    }

    std::uint64_t FolderEnumerationService::EnumerateFolderAsync(HWND targetWindow, std::wstring folderPath, bool recursive)
    {
        const std::uint64_t requestId = nextRequestId_.fetch_add(1, std::memory_order_acq_rel) + 1;
        sharedState_->activeRequestId.store(requestId, std::memory_order_release);

        EnumerationSharedStateView stateView{sharedState_, targetWindow, requestId};
        util::LogInfo(L"Starting async folder enumeration for " + folderPath);

        std::thread([stateView, folderPath = std::move(folderPath), recursive]() mutable
        {
            EnumerateFolder(stateView, folderPath, recursive);
        }).detach();

        return requestId;
    }

    void FolderEnumerationService::Cancel()
    {
        sharedState_->activeRequestId.fetch_add(1, std::memory_order_acq_rel);
    }
}