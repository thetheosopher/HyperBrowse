#include "services/FileOperationService.h"

#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include <algorithm>
#include <chrono>
#include <cwctype>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace hyperbrowse::services
{
    std::wstring FileOperationTypeToLabel(FileOperationType type);
}

namespace
{
    using Microsoft::WRL::ComPtr;
    namespace fs = std::filesystem;

    class ComScope
    {
    public:
        explicit ComScope(DWORD apartmentType)
        {
            const HRESULT result = CoInitializeEx(nullptr, apartmentType);
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

    void PostUpdate(HWND targetWindow, std::unique_ptr<hyperbrowse::services::FileOperationUpdate> update)
    {
        if (!targetWindow)
        {
            return;
        }

        if (!PostMessageW(targetWindow,
                          hyperbrowse::services::FileOperationService::kMessageId,
                          0,
                          reinterpret_cast<LPARAM>(update.get())))
        {
            return;
        }

        update.release();
    }

    std::wstring PathFromShellItem(IShellItem* item)
    {
        if (!item)
        {
            return {};
        }

        PWSTR rawPath = nullptr;
        const HRESULT result = item->GetDisplayName(SIGDN_FILESYSPATH, &rawPath);
        if (FAILED(result) || !rawPath)
        {
            return {};
        }

        std::wstring path = rawPath;
        CoTaskMemFree(rawPath);
        return path;
    }

    std::wstring BuildCaseInsensitiveKey(std::wstring_view value)
    {
        std::wstring key(value);
        std::transform(key.begin(), key.end(), key.begin(), [](wchar_t character)
        {
            return static_cast<wchar_t>(std::towlower(static_cast<wint_t>(character)));
        });
        return key;
    }

    bool PathExists(const fs::path& path)
    {
        std::error_code error;
        const bool exists = fs::exists(path, error);
        return exists && !error;
    }

    std::wstring BuildNumericSuffixFileName(std::wstring_view fileName, std::size_t suffix)
    {
        const fs::path namePath(fileName);
        std::wstring stem = namePath.stem().wstring();
        const std::wstring extension = namePath.extension().wstring();
        if (stem.empty())
        {
            stem = std::wstring(fileName);
        }

        stem.append(L".");
        stem.append(std::to_wstring(suffix));
        stem.append(extension);
        return stem;
    }

    DWORD BuildOperationFlags(hyperbrowse::services::FileOperationType type,
                              hyperbrowse::services::FileConflictPolicy conflictPolicy)
    {
        DWORD flags = FOFX_SHOWELEVATIONPROMPT | FOF_NOCONFIRMMKDIR | FOFX_NOCOPYHOOKS;
        if (conflictPolicy == hyperbrowse::services::FileConflictPolicy::OverwriteExisting
            && (type == hyperbrowse::services::FileOperationType::Copy
                || type == hyperbrowse::services::FileOperationType::Move))
        {
            flags |= FOF_NOCONFIRMATION;
        }

        if (type == hyperbrowse::services::FileOperationType::DeleteRecycleBin)
        {
            flags |= FOF_ALLOWUNDO | FOFX_RECYCLEONDELETE | FOF_NOCONFIRMATION;
        }
        else if (type == hyperbrowse::services::FileOperationType::DeletePermanent)
        {
            flags |= FOF_NOCONFIRMATION;
        }

        return flags;
    }

    class FileOperationProgressSink final : public IFileOperationProgressSink
    {
    public:
        const std::vector<std::wstring>& SucceededSourcePaths() const noexcept
        {
            return succeededSourcePaths_;
        }

        const std::vector<std::wstring>& CreatedPaths() const noexcept
        {
            return createdPaths_;
        }

        std::size_t FailedCount() const noexcept
        {
            return failedCount_;
        }

        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override
        {
            if (!object)
            {
                return E_POINTER;
            }

            if (riid == IID_IUnknown || riid == IID_IFileOperationProgressSink)
            {
                *object = static_cast<IFileOperationProgressSink*>(this);
                AddRef();
                return S_OK;
            }

            *object = nullptr;
            return E_NOINTERFACE;
        }

        ULONG STDMETHODCALLTYPE AddRef() override
        {
            return static_cast<ULONG>(InterlockedIncrement(&refCount_));
        }

        ULONG STDMETHODCALLTYPE Release() override
        {
            const ULONG count = static_cast<ULONG>(InterlockedDecrement(&refCount_));
            if (count == 0)
            {
                delete this;
            }
            return count;
        }

        HRESULT STDMETHODCALLTYPE StartOperations() override
        {
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE FinishOperations(HRESULT) override
        {
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE PreRenameItem(DWORD, IShellItem*, LPCWSTR) override
        {
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE PostRenameItem(DWORD,
                                                 IShellItem* item,
                                                 LPCWSTR,
                                                 HRESULT result,
                                                 IShellItem* newlyCreated) override
        {
            RecordResult(item, result, newlyCreated);
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE PreMoveItem(DWORD, IShellItem*, IShellItem*, LPCWSTR) override
        {
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE PostMoveItem(DWORD,
                                               IShellItem* item,
                                               IShellItem*,
                                               LPCWSTR,
                                               HRESULT result,
                                               IShellItem* newlyCreated) override
        {
            RecordResult(item, result, newlyCreated);
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE PreCopyItem(DWORD, IShellItem*, IShellItem*, LPCWSTR) override
        {
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE PostCopyItem(DWORD,
                                               IShellItem* item,
                                               IShellItem*,
                                               LPCWSTR,
                                               HRESULT result,
                                               IShellItem* newlyCreated) override
        {
            RecordResult(item, result, newlyCreated);
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE PreDeleteItem(DWORD, IShellItem* item) override
        {
            pendingDeleteSourcePaths_.push_back(PathFromShellItem(item));
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE PostDeleteItem(DWORD,
                                                 IShellItem* item,
                                                 HRESULT result,
                                                 IShellItem* newlyCreated) override
        {
            const std::wstring fallbackSourcePath = nextPendingDeleteSourcePathIndex_ < pendingDeleteSourcePaths_.size()
                ? pendingDeleteSourcePaths_[nextPendingDeleteSourcePathIndex_++]
                : std::wstring{};
            RecordResult(item, result, newlyCreated, fallbackSourcePath);
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE PreNewItem(DWORD, IShellItem*, LPCWSTR) override
        {
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE PostNewItem(DWORD,
                                              IShellItem*,
                                              LPCWSTR,
                                              LPCWSTR,
                                              DWORD,
                                              HRESULT,
                                              IShellItem*) override
        {
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE UpdateProgress(UINT, UINT) override
        {
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE ResetTimer() override
        {
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE PauseTimer() override
        {
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE ResumeTimer() override
        {
            return S_OK;
        }

    private:
        void RecordResult(IShellItem* sourceItem,
                          HRESULT result,
                          IShellItem* newlyCreated,
                          std::wstring_view fallbackSourcePath = {})
        {
            if (SUCCEEDED(result))
            {
                std::wstring sourcePath = PathFromShellItem(sourceItem);
                if (sourcePath.empty() && !fallbackSourcePath.empty())
                {
                    sourcePath.assign(fallbackSourcePath);
                }
                if (!sourcePath.empty())
                {
                    succeededSourcePaths_.push_back(sourcePath);
                }

                const std::wstring createdPath = PathFromShellItem(newlyCreated);
                if (!createdPath.empty())
                {
                    createdPaths_.push_back(createdPath);
                }
                return;
            }

            ++failedCount_;
        }

        volatile long refCount_{1};
        std::vector<std::wstring> succeededSourcePaths_;
        std::vector<std::wstring> createdPaths_;
        std::vector<std::wstring> pendingDeleteSourcePaths_;
        std::size_t nextPendingDeleteSourcePathIndex_{};
        std::size_t failedCount_{};
    };

    std::wstring BuildCompletionMessage(hyperbrowse::services::FileOperationType type,
                                        std::size_t requestedCount,
                                        std::size_t succeededCount,
                                        std::size_t failedCount,
                                        bool aborted)
    {
        std::wstring message = hyperbrowse::services::FileOperationTypeToLabel(type);
        message.append(L" finished.");

        if (requestedCount > 0)
        {
            message.append(L" Success: ");
            message.append(std::to_wstring(succeededCount));
            message.append(L" of ");
            message.append(std::to_wstring(requestedCount));
            message.append(L" item(s).");
        }

        if (failedCount > 0)
        {
            message.append(L" Failures: ");
            message.append(std::to_wstring(failedCount));
            message.append(L".");
        }

        if (aborted)
        {
            message.append(L" Some operations were cancelled or skipped.");
        }

        return message;
    }
}

namespace hyperbrowse::services
{
    FileConflictPlan PlanDestinationConflicts(const std::vector<std::wstring>& sourcePaths,
                                              std::wstring_view destinationFolder,
                                              FileConflictPolicy conflictPolicy)
    {
        FileConflictPlan plan;
        if (destinationFolder.empty()
            || (conflictPolicy != FileConflictPolicy::OverwriteExisting
                && conflictPolicy != FileConflictPolicy::AutoRenameNumericSuffix))
        {
            return plan;
        }

        const fs::path destinationRoot(destinationFolder);
        std::unordered_set<std::wstring> queuedNames;
        if (conflictPolicy == FileConflictPolicy::AutoRenameNumericSuffix)
        {
            plan.targetLeafNames.resize(sourcePaths.size());
        }

        for (std::size_t index = 0; index < sourcePaths.size(); ++index)
        {
            const std::wstring fileName = fs::path(sourcePaths[index]).filename().wstring();
            if (fileName.empty())
            {
                continue;
            }

            const std::wstring fileKey = BuildCaseInsensitiveKey(fileName);
            const bool conflictsWithDestination = PathExists(destinationRoot / fileName);
            const bool conflictsWithQueuedName = queuedNames.contains(fileKey);
            const bool hasConflict = conflictsWithDestination || conflictsWithQueuedName;

            if (!hasConflict)
            {
                queuedNames.insert(fileKey);
                continue;
            }

            ++plan.conflictCount;
            if (conflictPolicy == FileConflictPolicy::OverwriteExisting)
            {
                queuedNames.insert(fileKey);
                continue;
            }

            for (std::size_t suffix = 1;; ++suffix)
            {
                const std::wstring renamedFileName = BuildNumericSuffixFileName(fileName, suffix);
                const std::wstring renamedKey = BuildCaseInsensitiveKey(renamedFileName);
                if (queuedNames.contains(renamedKey) || PathExists(destinationRoot / renamedFileName))
                {
                    continue;
                }

                plan.targetLeafNames[index] = renamedFileName;
                queuedNames.insert(renamedKey);
                ++plan.renamedCount;
                break;
            }
        }

        return plan;
    }

    std::wstring FileOperationTypeToLabel(FileOperationType type)
    {
        switch (type)
        {
        case FileOperationType::Rename:
            return L"Rename";
        case FileOperationType::Move:
            return L"Move";
        case FileOperationType::DeleteRecycleBin:
            return L"Delete";
        case FileOperationType::DeletePermanent:
            return L"Permanent delete";
        case FileOperationType::Copy:
        default:
            return L"Copy";
        }
    }

    std::wstring FileOperationTypeToActivityLabel(FileOperationType type)
    {
        switch (type)
        {
        case FileOperationType::Rename:
            return L"Renaming";
        case FileOperationType::Move:
            return L"Moving";
        case FileOperationType::DeleteRecycleBin:
            return L"Deleting";
        case FileOperationType::DeletePermanent:
            return L"Deleting permanently";
        case FileOperationType::Copy:
        default:
            return L"Copying";
        }
    }

    FileOperationService::~FileOperationService()
    {
        WaitForWorkers();
    }

    std::uint64_t FileOperationService::Start(HWND targetWindow,
                                              HWND ownerWindow,
                                              FileOperationType type,
                                              std::vector<std::wstring> sourcePaths,
                                              std::wstring destinationFolder,
                                              FileConflictPolicy conflictPolicy,
                                              std::vector<std::wstring> targetLeafNames)
    {
        ReapCompletedWorkers();

        const std::uint64_t requestId = nextRequestId_.fetch_add(1, std::memory_order_acq_rel) + 1;
        if (targetLeafNames.size() != sourcePaths.size())
        {
            targetLeafNames.clear();
        }

        workers_.push_back(std::async(std::launch::async,
            [targetWindow,
             ownerWindow,
             type,
             sourcePaths = std::move(sourcePaths),
             destinationFolder = std::move(destinationFolder),
             conflictPolicy,
             targetLeafNames = std::move(targetLeafNames),
             requestId]() mutable
        {
            auto update = std::make_unique<FileOperationUpdate>();
            update->requestId = requestId;
            update->type = type;
            update->requestedCount = sourcePaths.size();
            update->destinationFolder = destinationFolder;
            update->finished = true;

            if (sourcePaths.empty())
            {
                update->message = L"No files were selected for the requested file operation.";
                PostUpdate(targetWindow, std::move(update));
                return;
            }

            ComScope comScope(COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

            ComPtr<IFileOperation> operation;
            HRESULT result = CoCreateInstance(CLSID_FileOperation,
                                              nullptr,
                                              CLSCTX_INPROC_SERVER,
                                              IID_PPV_ARGS(operation.GetAddressOf()));
            if (FAILED(result) || !operation)
            {
                update->failedCount = sourcePaths.size();
                update->message = L"Failed to initialize the Windows file operation service.";
                PostUpdate(targetWindow, std::move(update));
                return;
            }

            operation->SetOwnerWindow(ownerWindow);
            operation->SetOperationFlags(BuildOperationFlags(type, conflictPolicy));

            ComPtr<IShellItem> destinationItem;
            if (type == FileOperationType::Copy || type == FileOperationType::Move)
            {
                result = SHCreateItemFromParsingName(destinationFolder.c_str(), nullptr, IID_PPV_ARGS(destinationItem.GetAddressOf()));
                if (FAILED(result) || !destinationItem)
                {
                    update->failedCount = sourcePaths.size();
                    update->message = L"Failed to open the destination folder for the requested file operation.";
                    PostUpdate(targetWindow, std::move(update));
                    return;
                }
            }

            auto* sink = new FileOperationProgressSink();
            DWORD sinkCookie = 0;
            result = operation->Advise(sink, &sinkCookie);
            if (FAILED(result))
            {
                sink->Release();
                update->failedCount = sourcePaths.size();
                update->message = L"Failed to register the Windows file operation progress sink.";
                PostUpdate(targetWindow, std::move(update));
                return;
            }

            std::size_t queueFailures = 0;
            std::size_t queuedCount = 0;
            for (std::size_t index = 0; index < sourcePaths.size(); ++index)
            {
                const std::wstring& sourcePath = sourcePaths[index];
                ComPtr<IShellItem> sourceItem;
                result = SHCreateItemFromParsingName(sourcePath.c_str(), nullptr, IID_PPV_ARGS(sourceItem.GetAddressOf()));
                if (FAILED(result) || !sourceItem)
                {
                    ++queueFailures;
                    continue;
                }

                const wchar_t* targetLeafName = nullptr;
                if ((type == FileOperationType::Copy || type == FileOperationType::Move || type == FileOperationType::Rename)
                    && index < targetLeafNames.size()
                    && !targetLeafNames[index].empty())
                {
                    targetLeafName = targetLeafNames[index].c_str();
                }

                switch (type)
                {
                case FileOperationType::Copy:
                    result = operation->CopyItem(sourceItem.Get(), destinationItem.Get(), targetLeafName, nullptr);
                    break;
                case FileOperationType::Move:
                    result = operation->MoveItem(sourceItem.Get(), destinationItem.Get(), targetLeafName, nullptr);
                    break;
                case FileOperationType::Rename:
                    result = targetLeafName
                        ? operation->RenameItem(sourceItem.Get(), targetLeafName, nullptr)
                        : E_INVALIDARG;
                    break;
                case FileOperationType::DeleteRecycleBin:
                case FileOperationType::DeletePermanent:
                    result = operation->DeleteItem(sourceItem.Get(), nullptr);
                    break;
                default:
                    result = E_NOTIMPL;
                    break;
                }

                if (FAILED(result))
                {
                    ++queueFailures;
                    continue;
                }

                ++queuedCount;
            }

            if (queuedCount == 0)
            {
                operation->Unadvise(sinkCookie);
                sink->Release();
                update->failedCount = queueFailures;
                update->message = L"No files could be queued for the requested file operation.";
                PostUpdate(targetWindow, std::move(update));
                return;
            }

            result = operation->PerformOperations();
            BOOL aborted = FALSE;
            operation->GetAnyOperationsAborted(&aborted);
            operation->Unadvise(sinkCookie);

            update->aborted = aborted != FALSE;
            update->failedCount = queueFailures + sink->FailedCount();
            update->succeededSourcePaths = sink->SucceededSourcePaths();
            update->createdPaths = sink->CreatedPaths();
            sink->Release();

            if (FAILED(result) && update->succeededSourcePaths.empty())
            {
                update->failedCount = (std::max)(update->failedCount, sourcePaths.size());
                update->message = L"The Windows file operation did not complete successfully.";
                PostUpdate(targetWindow, std::move(update));
                return;
            }

            update->message = BuildCompletionMessage(type,
                                                     update->requestedCount,
                                                     update->succeededSourcePaths.size(),
                                                     update->failedCount,
                                                     update->aborted);
            PostUpdate(targetWindow, std::move(update));
        }));

        return requestId;
    }

    void FileOperationService::ReapCompletedWorkers()
    {
        workers_.erase(std::remove_if(workers_.begin(), workers_.end(), [](std::future<void>& worker)
        {
            return !worker.valid() || worker.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready;
        }), workers_.end());
    }

    void FileOperationService::WaitForWorkers()
    {
        for (std::future<void>& worker : workers_)
        {
            if (worker.valid())
            {
                worker.wait();
            }
        }
        workers_.clear();
    }
}