#pragma once

#include <windows.h>

#include <commctrl.h>

#include <objbase.h>
#include <oleacc.h>
#include <propvarutil.h>
#include <shlobj.h>
#include <shellapi.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
#include "browser/BrowserModel.h"
#include "browser/BrowserPane.h"
#include "app/Application.h"
#include "cache/DiskThumbnailCache.h"
#include "decode/ImageDecoder.h"
#include "decode/NvJpegDecoder.h"
#include "decode/WicDecodeHelpers.h"
#include "decode/WicThumbnailDecoder.h"
#include "services/BatchConvertService.h"
#include "services/FileOperationService.h"
#include "services/FolderEnumerationService.h"
#include "services/FolderTreeEnumerationService.h"
#include "services/FolderWatchService.h"
#include "services/ImageMetadataService.h"
#include "services/ImageCommandService.h"
#include "services/JpegTransformService.h"
#include "services/SavedSearchStore.h"
#include "services/ThumbnailScheduler.h"
#include "services/UserMetadataStore.h"
#include "ui/CommandIds.h"
#include "ui/DialogDpi.h"
#include "ui/DialogShell.h"
#include "ui/ExternalDropTarget.h"
#include "ui/MainWindowDialogs.h"
#include "ui/MainWindow.h"
#include "ui/MainWindowDialogState.h"
#include "ui/SavedSearchController.h"
#include "ui/SettingsLayout.h"
#include "ui/ToolbarIconLibrary.h"
#include "util/BackgroundExecutor.h"
#include "util/Diagnostics.h"
#include "util/ResourceSizing.h"
#include "util/SettingsRegistry.h"
#include "util/UiTextSize.h"
#include "viewer/ViewerWindow.h"

namespace fs = std::filesystem;

// Shared infrastructure for the split smoke-test translation units. Everything is inline so each
// scenario file sees the same helpers it used when the suite was a single file.
namespace hyperbrowse::tests::smoke
{
    using Microsoft::WRL::ComPtr;

    inline constexpr wchar_t kTestWindowClassName[] = L"HyperBrowseFolderEnumerationTestWindow";
    inline std::wstring gSmokeRegistryPath;
    inline fs::path gSmokeCacheDirectory;
    inline const wchar_t* kRegistryPath = nullptr;
    inline constexpr wchar_t kRegistryValueViewerInfoOverlaysVisible[] = L"ViewerInfoOverlaysVisible";
    inline constexpr wchar_t kRegistryValueViewerInfoOverlayTextSize[] = L"ViewerInfoOverlayTextSize";
    inline constexpr wchar_t kRegistryValueViewerWindowedFullMetadataVisible[] = L"ViewerWindowedFullMetadataVisible";
    inline constexpr wchar_t kRegistryValueViewerFullScreenFullMetadataVisible[] = L"ViewerFullScreenFullMetadataVisible";
    inline constexpr wchar_t kRegistryValueViewerFullMetadataVisible[] = L"ViewerFullMetadataVisible";
    inline constexpr wchar_t kRegistryValueViewerEscapeKeyBehavior[] = L"ViewerEscapeKeyBehavior";
    inline constexpr wchar_t kRegistryValueInvertKeyboardPanning[] = L"InvertKeyboardPanning";
    inline constexpr wchar_t kRegistryValueAppTextSize[] = L"AppTextSize";
    inline constexpr wchar_t kRegistryValueWindowLeft[] = L"WindowLeft";
    inline constexpr wchar_t kRegistryValueWindowTop[] = L"WindowTop";
    inline constexpr wchar_t kRegistryValueWindowWidth[] = L"WindowWidth";
    inline constexpr wchar_t kRegistryValueWindowHeight[] = L"WindowHeight";
    inline constexpr wchar_t kRegistryValueSlideshowInterval[] = L"SlideshowIntervalMs";
    inline constexpr wchar_t kRegistryValueUseSlideshowTransition[] = L"UseSlideshowTransition";
    inline constexpr wchar_t kRegistryValueSingleInstanceEnabled[] = L"SingleInstanceEnabled";
    inline constexpr wchar_t kRegistryValueKeepInNotificationAreaEnabled[] = L"KeepInNotificationAreaEnabled";
    inline constexpr wchar_t kRegistryValueThumbnailCacheCapacityOverrideBytes[] = L"ThumbnailCacheCapacityOverrideBytes";
    inline constexpr wchar_t kRegistryValueMetadataCacheCapacityOverrideEntries[] = L"MetadataCacheCapacityOverrideEntries";
    inline constexpr wchar_t kRegistryValuePrefetchDepthOverride[] = L"PrefetchDepthOverride";

    inline bool ConfigureSmokeEnvironment()
    {
        gSmokeRegistryPath = L"Software\\HyperBrowse\\SmokeTests\\" + std::to_wstring(GetCurrentProcessId());
        kRegistryPath = gSmokeRegistryPath.c_str();
        if (!SetEnvironmentVariableW(
            hyperbrowse::util::kSettingsRegistryEnvironmentVariable,
            gSmokeRegistryPath.c_str()))
        {
            return false;
        }

        gSmokeCacheDirectory = fs::temp_directory_path()
            / (L"HyperBrowseSmokeCache-" + std::to_wstring(GetCurrentProcessId())
               + L"-" + std::to_wstring(GetTickCount64()));
        std::error_code directoryError;
        fs::create_directories(gSmokeCacheDirectory, directoryError);
        return !directoryError && SetEnvironmentVariableW(
            hyperbrowse::cache::kCacheDirectoryEnvironmentVariable,
            gSmokeCacheDirectory.c_str()) != FALSE
            && SetEnvironmentVariableW(hyperbrowse::services::kSavedSearchDirectoryEnvironmentVariable,
                (gSmokeCacheDirectory / L"saved-search-profile").c_str()) != FALSE;
    }

    inline void CleanupSmokeEnvironment()
    {
        if (!gSmokeRegistryPath.empty())
        {
            const LONG result = RegDeleteTreeW(HKEY_CURRENT_USER, gSmokeRegistryPath.c_str());
            if (result != ERROR_SUCCESS && result != ERROR_FILE_NOT_FOUND && result != ERROR_PATH_NOT_FOUND)
            {
                std::cerr << "Failed to remove the smoke-test settings registry key (error "
                          << result << ")\n";
            }
        }

        if (!gSmokeCacheDirectory.empty())
        {
            std::error_code removalError;
            fs::remove_all(gSmokeCacheDirectory, removalError);
            if (removalError)
            {
                std::cerr << "Failed to remove the smoke-test cache directory: "
                          << removalError.message() << '\n';
            }
        }
    }

    class ScopedSmokeEnvironmentCleanup
    {
    public:
        ~ScopedSmokeEnvironmentCleanup()
        {
            CleanupSmokeEnvironment();
        }
    };

    inline void Expect(bool condition, const std::string& message);

    struct EnumerationResult
    {
        std::uint64_t totalCount{};
        std::uint64_t totalBytes{};
        std::size_t firstBatchSize{};
        std::size_t secondBatchSize{};
        std::size_t batchCount{};
        std::vector<hyperbrowse::browser::BrowserItem> items;
        std::wstring errorMessage;
        bool completed{};
        bool failed{};
    };

    struct ThumbnailResult
    {
        std::uint64_t expectedSessionId{};
        int readyCount{};
        int failedCount{};
        std::vector<std::wstring> readyPaths;
        std::vector<std::wstring> failedPaths;
    };

    struct FolderTreeEnumerationResult
    {
        std::uint64_t expectedRequestId{};
        std::vector<hyperbrowse::services::FolderTreeChild> childFolders;
        std::vector<hyperbrowse::services::FolderTreeChild> childPresenceResults;
        std::wstring errorMessage;
        bool completed{};
        bool failed{};
    };

    struct FileOperationResult
    {
        std::uint64_t expectedRequestId{};
        hyperbrowse::services::FileOperationUpdate update;
        bool completed{};
    };

    struct BatchConvertResult
    {
        std::uint64_t expectedRequestId{};
        hyperbrowse::services::BatchConvertUpdate update;
        bool completed{};
    };

    struct ImageCommandResult
    {
        std::uint64_t expectedRequestId{};
        hyperbrowse::services::ImageCommandUpdate update;
        bool completed{};
    };

    struct TestWindowState
    {
        std::uint64_t expectedRequestId{};
        EnumerationResult enumerationResult;
        ThumbnailResult thumbnailResult;
        FolderTreeEnumerationResult folderTreeEnumerationResult;
        FileOperationResult fileOperationResult;
        BatchConvertResult batchConvertResult;
        ImageCommandResult imageCommandResult;
        int viewerStartFolderSlideshowRequests{};
        HWND lastViewerStartFolderSlideshowSource{};
        int viewerQuickSendRequests{};
        hyperbrowse::viewer::QuickSendOperation lastViewerQuickSendOperation{
            hyperbrowse::viewer::QuickSendOperation::Move};
        HWND lastViewerQuickSendSource{};
        int viewerCompareRatingRequests{};
        int lastViewerCompareRating{-1};
        HWND lastViewerCompareRatingSource{};
    };

    inline std::vector<HWND> FindOpenViewerWindowHandles()
    {
        struct SearchState
        {
            std::vector<HWND>* handles;
            DWORD processId;
        } state{nullptr, GetCurrentProcessId()};
        std::vector<HWND> handles;
        state.handles = &handles;
        EnumWindows([](HWND window, LPARAM parameter) -> BOOL
        {
            auto* state = reinterpret_cast<SearchState*>(parameter);
            DWORD processId = 0;
            wchar_t className[64]{};
            if (GetWindowThreadProcessId(window, &processId) != 0
                && processId == state->processId
                && GetClassNameW(window, className, static_cast<int>(std::size(className))) > 0
                && std::wstring_view(className) == L"HyperBrowseViewerWindow"
                && IsWindow(window) != FALSE)
            {
                state->handles->push_back(window);
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&state));
        return handles;
    }

    class ComScope
    {
    public:
        ComScope()
        {
            const HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
            if (FAILED(result) && result != RPC_E_CHANGED_MODE)
            {
                throw std::runtime_error("Failed to initialize COM for tests");
            }

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

    class TempFolder
    {
    public:
        explicit TempFolder(std::wstring name)
            : TempFolder(fs::temp_directory_path(), std::move(name))
        {
        }

        TempFolder(fs::path parent, std::wstring name)
            : root_(std::move(parent) / std::move(name))
        {
            std::error_code error;
            fs::remove_all(root_, error);
            fs::create_directories(root_);
        }

        ~TempFolder()
        {
            std::error_code error;
            fs::remove_all(root_, error);
        }

        const fs::path& Root() const noexcept
        {
            return root_;
        }

        void WriteFile(const fs::path& relativePath, std::size_t byteCount)
        {
            const fs::path absolutePath = root_ / relativePath;
            fs::create_directories(absolutePath.parent_path());
            std::ofstream stream(absolutePath, std::ios::binary);
            stream << std::string(byteCount, 'x');
        }

    private:
        fs::path root_;
    };

    class ScopedRegistryDwordBackup
    {
    public:
        ScopedRegistryDwordBackup(const wchar_t* path, const wchar_t* valueName)
            : path_(path)
            , valueName_(valueName)
        {
            HKEY key{};
            if (RegOpenKeyExW(HKEY_CURRENT_USER, path_, 0, KEY_READ, &key) != ERROR_SUCCESS)
            {
                return;
            }

            DWORD size = sizeof(value_);
            DWORD type = REG_DWORD;
            hadValue_ = RegQueryValueExW(key,
                                         valueName_,
                                         nullptr,
                                         &type,
                                         reinterpret_cast<LPBYTE>(&value_),
                                         &size) == ERROR_SUCCESS
                && type == REG_DWORD;
            RegCloseKey(key);
        }

        ~ScopedRegistryDwordBackup()
        {
            HKEY key{};
            DWORD disposition = 0;
            if (RegCreateKeyExW(HKEY_CURRENT_USER,
                                path_,
                                0,
                                nullptr,
                                0,
                                KEY_WRITE,
                                nullptr,
                                &key,
                                &disposition) != ERROR_SUCCESS)
            {
                return;
            }

            if (hadValue_)
            {
                RegSetValueExW(key,
                               valueName_,
                               0,
                               REG_DWORD,
                               reinterpret_cast<const BYTE*>(&value_),
                               sizeof(value_));
            }
            else
            {
                RegDeleteValueW(key, valueName_);
            }

            RegCloseKey(key);
        }

    private:
        const wchar_t* path_{};
        const wchar_t* valueName_{};
        DWORD value_{};
        bool hadValue_{};
    };

    class ScopedMemorySnapshotOverride
    {
    public:
        explicit ScopedMemorySnapshotOverride(hyperbrowse::util::MemorySnapshot snapshot)
        {
            hyperbrowse::util::SetMemorySnapshotOverrideForTests(snapshot);
        }

        ~ScopedMemorySnapshotOverride()
        {
            hyperbrowse::util::ClearMemorySnapshotOverrideForTests();
        }

        ScopedMemorySnapshotOverride(const ScopedMemorySnapshotOverride&) = delete;
        ScopedMemorySnapshotOverride& operator=(const ScopedMemorySnapshotOverride&) = delete;
    };

    enum class TestImageFormat
    {
        Jpeg,
        Png,
        Gif,
        Tiff,
    };

    inline void Expect(bool condition, const std::string& message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    inline void SetRegistryDwordValue(const wchar_t* path, const wchar_t* valueName, DWORD value)
    {
        HKEY key{};
        DWORD disposition = 0;
        Expect(RegCreateKeyExW(HKEY_CURRENT_USER,
                               path,
                               0,
                               nullptr,
                               0,
                               KEY_WRITE,
                               nullptr,
                               &key,
                               &disposition) == ERROR_SUCCESS,
               "Failed to open the HyperBrowse registry key for a smoke-test value");
        const LONG result = RegSetValueExW(key,
                                           valueName,
                                           0,
                                           REG_DWORD,
                                           reinterpret_cast<const BYTE*>(&value),
                                           sizeof(value));
        RegCloseKey(key);
        Expect(result == ERROR_SUCCESS, "Failed to write a HyperBrowse registry smoke-test value");
    }

    inline void DeleteRegistryValue(const wchar_t* path, const wchar_t* valueName)
    {
        HKEY key{};
        if (RegOpenKeyExW(HKEY_CURRENT_USER, path, 0, KEY_WRITE, &key) == ERROR_SUCCESS)
        {
            RegDeleteValueW(key, valueName);
            RegCloseKey(key);
        }
    }

    inline bool TryReadRegistryDwordValue(const wchar_t* path, const wchar_t* valueName, DWORD* value)
    {
        if (!value)
        {
            return false;
        }

        HKEY key{};
        if (RegOpenKeyExW(HKEY_CURRENT_USER, path, 0, KEY_READ, &key) != ERROR_SUCCESS)
        {
            return false;
        }

        DWORD type = 0;
        DWORD size = sizeof(*value);
        const LONG result = RegQueryValueExW(key,
                                             valueName,
                                             nullptr,
                                             &type,
                                             reinterpret_cast<LPBYTE>(value),
                                             &size);
        RegCloseKey(key);
        return result == ERROR_SUCCESS && type == REG_DWORD && size == sizeof(*value);
    }

    inline void CheckHResult(HRESULT result, const char* message)
    {
        if (FAILED(result))
        {
            throw std::runtime_error(message);
        }
    }

    inline std::string Utf8FromWide(std::wstring_view text)
    {
        if (text.empty())
        {
            return {};
        }

        const int required = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
        if (required <= 0)
        {
            std::string fallback;
            fallback.reserve(text.size());
            for (wchar_t character : text)
            {
                fallback.push_back(character <= 0x7f ? static_cast<char>(character) : '?');
            }
            return fallback;
        }

        std::string utf8(static_cast<std::size_t>(required), '\0');
        WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), utf8.data(), required, nullptr, nullptr);
        return utf8;
    }

    inline void ResetEnumerationResult(TestWindowState* state)
    {
        state->enumerationResult = EnumerationResult{};
    }

    inline void ResetThumbnailResult(TestWindowState* state, std::uint64_t expectedSessionId)
    {
        state->thumbnailResult = ThumbnailResult{};
        state->thumbnailResult.expectedSessionId = expectedSessionId;
    }

    inline void ResetFolderTreeEnumerationResult(TestWindowState* state)
    {
        state->folderTreeEnumerationResult = FolderTreeEnumerationResult{};
    }

    inline bool PumpMessagesUntil(const std::function<bool()>& predicate, DWORD timeoutMs)
    {
        const ULONGLONG deadline = GetTickCount64() + timeoutMs;
        MSG msg{};
        while (GetTickCount64() < deadline)
        {
            for (std::size_t processed = 0;
                 processed < 64 && GetTickCount64() < deadline && PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE);
                 ++processed)
            {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }

            if (predicate())
            {
                return true;
            }

            MsgWaitForMultipleObjectsEx(0, nullptr, 25, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        }

        return predicate();
    }

    inline void PumpMessagesFor(DWORD durationMs)
    {
        const ULONGLONG deadline = GetTickCount64() + durationMs;
        MSG msg{};
        while (GetTickCount64() < deadline)
        {
            for (std::size_t processed = 0;
                 processed < 64 && GetTickCount64() < deadline && PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE);
                 ++processed)
            {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }

            MsgWaitForMultipleObjectsEx(0, nullptr, 25, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        }
    }

    inline bool ReadClientPixel(HWND window, POINT point, COLORREF* color)
    {
        if (!window || !color)
        {
            return false;
        }

        HDC dc = GetDC(window);
        if (!dc)
        {
            return false;
        }

        const COLORREF sampledColor = GetPixel(dc, point.x, point.y);
        ReleaseDC(window, dc);
        if (sampledColor == CLR_INVALID)
        {
            return false;
        }

        *color = sampledColor;
        return true;
    }

    inline std::wstring ReadTreeItemText(HWND treeView, HTREEITEM item)
    {
        wchar_t buffer[260]{};
        TVITEMW treeItem{};
        treeItem.mask = TVIF_TEXT;
        treeItem.hItem = item;
        treeItem.pszText = buffer;
        treeItem.cchTextMax = static_cast<int>(std::size(buffer));
        Expect(TreeView_GetItem(treeView, &treeItem) != FALSE, "Failed to read a tree item text");
        return buffer;
    }

    inline std::wstring QueryShellDisplayName(const std::wstring& folderPath)
    {
        SHFILEINFOW shellInfo{};
        if (SHGetFileInfoW(
            folderPath.c_str(),
            FILE_ATTRIBUTE_DIRECTORY,
            &shellInfo,
            sizeof(shellInfo),
            SHGFI_DISPLAYNAME | SHGFI_SYSICONINDEX | SHGFI_SMALLICON) != 0
            && shellInfo.szDisplayName[0] != L'\0')
        {
            return shellInfo.szDisplayName;
        }

        const fs::path path(folderPath);
        const std::wstring leaf = path.filename().wstring();
        return leaf.empty() ? folderPath : leaf;
    }

    inline std::wstring TryGetKnownFolderPathForTest(REFKNOWNFOLDERID folderId)
    {
        PWSTR rawPath = nullptr;
        const HRESULT result = SHGetKnownFolderPath(folderId, KF_FLAG_DEFAULT, nullptr, &rawPath);
        if (FAILED(result) || !rawPath)
        {
            return {};
        }

        std::wstring path = rawPath;
        CoTaskMemFree(rawPath);
        return path;
    }

    inline std::vector<std::wstring> ExpectedSpecialFolderRootTexts()
    {
        std::vector<std::wstring> rootTexts;
        const KNOWNFOLDERID folderIds[] = {
            FOLDERID_Desktop,
            FOLDERID_Documents,
            FOLDERID_Pictures,
        };

        for (const KNOWNFOLDERID& folderId : folderIds)
        {
            const std::wstring folderPath = TryGetKnownFolderPathForTest(folderId);
            if (folderPath.empty())
            {
                continue;
            }

            std::error_code error;
            if (!fs::is_directory(fs::path(folderPath), error) || error)
            {
                continue;
            }

            rootTexts.push_back(QueryShellDisplayName(folderPath));
        }

        return rootTexts;
    }

    inline LRESULT CALLBACK TestWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (message == WM_NCCREATE)
        {
            auto* createStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(createStruct->lpCreateParams));
            return TRUE;
        }

        auto* state = reinterpret_cast<TestWindowState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == hyperbrowse::viewer::ViewerWindow::kContextMenuCommandMessage
            && wParam >= hyperbrowse::viewer::ViewerWindow::kContextMenuSetCompareRatingBase
            && wParam < hyperbrowse::viewer::ViewerWindow::kContextMenuSetCompareRatingBase + 6)
        {
            if (!state)
            {
                return 0;
            }

            ++state->viewerCompareRatingRequests;
            state->lastViewerCompareRating = static_cast<int>(
                wParam - hyperbrowse::viewer::ViewerWindow::kContextMenuSetCompareRatingBase);
            state->lastViewerCompareRatingSource = reinterpret_cast<HWND>(lParam);
            return 0;
        }

        if (message == hyperbrowse::services::FolderEnumerationService::kMessageId)
        {
            std::unique_ptr<hyperbrowse::services::FolderEnumerationUpdate> update(
                reinterpret_cast<hyperbrowse::services::FolderEnumerationUpdate*>(lParam));

            if (!state || !update || update->requestId != state->expectedRequestId)
            {
                return 0;
            }

            switch (update->kind)
            {
            case hyperbrowse::services::FolderEnumerationUpdateKind::Batch:
                if (state->enumerationResult.batchCount == 0)
                {
                    state->enumerationResult.firstBatchSize = update->items.size();
                }
                else if (state->enumerationResult.batchCount == 1)
                {
                    state->enumerationResult.secondBatchSize = update->items.size();
                }
                ++state->enumerationResult.batchCount;
                state->enumerationResult.totalCount = update->totalCount;
                state->enumerationResult.totalBytes = update->totalBytes;
                state->enumerationResult.items.insert(state->enumerationResult.items.end(),
                                                      std::make_move_iterator(update->items.begin()),
                                                      std::make_move_iterator(update->items.end()));
                return 0;
            case hyperbrowse::services::FolderEnumerationUpdateKind::Completed:
                state->enumerationResult.totalCount = update->totalCount;
                state->enumerationResult.totalBytes = update->totalBytes;
                state->enumerationResult.completed = true;
                return 0;
            case hyperbrowse::services::FolderEnumerationUpdateKind::Failed:
                state->enumerationResult.errorMessage = update->message;
                state->enumerationResult.failed = true;
                return 0;
            default:
                return 0;
            }
        }

        if (message == hyperbrowse::services::ThumbnailScheduler::kMessageId)
        {
            std::unique_ptr<hyperbrowse::services::ThumbnailReadyUpdate> update(
                reinterpret_cast<hyperbrowse::services::ThumbnailReadyUpdate*>(lParam));
            if (!state || !update || update->sessionId != state->thumbnailResult.expectedSessionId)
            {
                return 0;
            }

            if (update->success)
            {
                ++state->thumbnailResult.readyCount;
                state->thumbnailResult.readyPaths.push_back(update->cacheKey.filePath);
            }
            else
            {
                ++state->thumbnailResult.failedCount;
                state->thumbnailResult.failedPaths.push_back(update->cacheKey.filePath);
            }
            return 0;
        }

        if (message == hyperbrowse::services::FolderTreeEnumerationService::kMessageId)
        {
            std::unique_ptr<hyperbrowse::services::FolderTreeEnumerationUpdate> update(
                reinterpret_cast<hyperbrowse::services::FolderTreeEnumerationUpdate*>(lParam));
            if (!state || !update || update->requestId != state->folderTreeEnumerationResult.expectedRequestId)
            {
                return 0;
            }

            switch (update->kind)
            {
            case hyperbrowse::services::FolderTreeEnumerationUpdateKind::Completed:
                state->folderTreeEnumerationResult.childFolders = std::move(update->childFolders);
                state->folderTreeEnumerationResult.completed = true;
                return 0;
            case hyperbrowse::services::FolderTreeEnumerationUpdateKind::ChildPresenceCompleted:
                state->folderTreeEnumerationResult.childPresenceResults = std::move(update->childPresenceResults);
                state->folderTreeEnumerationResult.completed = true;
                return 0;
            case hyperbrowse::services::FolderTreeEnumerationUpdateKind::Failed:
                state->folderTreeEnumerationResult.errorMessage = update->message;
                state->folderTreeEnumerationResult.failed = true;
                return 0;
            default:
                return 0;
            }
        }

        if (message == hyperbrowse::services::FileOperationService::kMessageId)
        {
            std::unique_ptr<hyperbrowse::services::FileOperationUpdate> update(
                reinterpret_cast<hyperbrowse::services::FileOperationUpdate*>(lParam));
            if (!state || !update || update->requestId != state->fileOperationResult.expectedRequestId)
            {
                return 0;
            }

            state->fileOperationResult.update = std::move(*update);
            state->fileOperationResult.completed = true;
            return 0;
        }

        if (message == hyperbrowse::services::BatchConvertService::kMessageId)
        {
            std::unique_ptr<hyperbrowse::services::BatchConvertUpdate> update(
                reinterpret_cast<hyperbrowse::services::BatchConvertUpdate*>(lParam));
            if (!state || !update || update->requestId != state->batchConvertResult.expectedRequestId)
            {
                return 0;
            }

            state->batchConvertResult.update = std::move(*update);
            state->batchConvertResult.completed = state->batchConvertResult.update.finished;
            return 0;
        }

        if (message == hyperbrowse::services::ImageCommandService::kMessageId)
        {
            std::unique_ptr<hyperbrowse::services::ImageCommandUpdate> update(
                reinterpret_cast<hyperbrowse::services::ImageCommandUpdate*>(lParam));
            if (!state || !update || update->requestId != state->imageCommandResult.expectedRequestId)
            {
                return 0;
            }

            state->imageCommandResult.update = std::move(*update);
            state->imageCommandResult.completed = state->imageCommandResult.update.finished;
            return 0;
        }

        if (message == hyperbrowse::viewer::ViewerWindow::kStartFolderSlideshowMessage)
        {
            if (!state)
            {
                return 0;
            }

            ++state->viewerStartFolderSlideshowRequests;
            state->lastViewerStartFolderSlideshowSource = reinterpret_cast<HWND>(wParam);
            return 0;
        }

        if (message == hyperbrowse::viewer::ViewerWindow::kQuickSendRequestedMessage)
        {
            if (!state)
            {
                return 0;
            }

            ++state->viewerQuickSendRequests;
            state->lastViewerQuickSendOperation = static_cast<hyperbrowse::viewer::QuickSendOperation>(wParam);
            state->lastViewerQuickSendSource = reinterpret_cast<HWND>(lParam);
            return 0;
        }

        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    inline HWND CreateTestWindow(TestWindowState* state, HINSTANCE instance)
    {
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = &TestWindowProc;
        windowClass.hInstance = instance;
        windowClass.lpszClassName = kTestWindowClassName;
        RegisterClassExW(&windowClass);

        return CreateWindowExW(
            0,
            kTestWindowClassName,
            L"HyperBrowseTests",
            0,
            0,
            0,
            0,
            0,
            HWND_MESSAGE,
            nullptr,
            instance,
            state);
    }

    inline HWND CreateUiHostWindow(HINSTANCE instance)
    {
        return CreateWindowExW(
            0,
            kTestWindowClassName,
            L"HyperBrowseUiHost",
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            960,
            720,
            nullptr,
            nullptr,
            instance,
            nullptr);
    }

    inline GUID ContainerFormatGuid(TestImageFormat format)
    {
        switch (format)
        {
        case TestImageFormat::Jpeg:
            return GUID_ContainerFormatJpeg;
        case TestImageFormat::Png:
            return GUID_ContainerFormatPng;
        case TestImageFormat::Gif:
            return GUID_ContainerFormatGif;
        case TestImageFormat::Tiff:
        default:
            return GUID_ContainerFormatTiff;
        }
    }

    inline std::vector<BYTE> BuildPixelBuffer(UINT width, UINT height)
    {
        std::vector<BYTE> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U, 0);
        for (UINT y = 0; y < height; ++y)
        {
            for (UINT x = 0; x < width; ++x)
            {
                const std::size_t index = (static_cast<std::size_t>(y) * width + x) * 4U;
                pixels[index + 0] = static_cast<BYTE>((x * 37U) % 255U);
                pixels[index + 1] = static_cast<BYTE>((y * 53U) % 255U);
                pixels[index + 2] = static_cast<BYTE>(((x + y) * 29U) % 255U);
                pixels[index + 3] = 255;
            }
        }

        return pixels;
    }

    inline void InitPropVariantFromAnsiText(std::string_view text, PROPVARIANT* value)
    {
        if (!value)
        {
            return;
        }

        PropVariantInit(value);
        char* buffer = static_cast<char*>(CoTaskMemAlloc(text.size() + 1));
        Expect(buffer != nullptr, "Failed to allocate the PNG text metadata buffer");
        std::memcpy(buffer, text.data(), text.size());
        buffer[text.size()] = '\0';
        value->vt = VT_LPSTR;
        value->pszVal = buffer;
    }

    inline void WriteTestImage(const fs::path& path,
                        TestImageFormat format,
                        UINT width,
                        UINT height,
                        std::uint16_t orientation = 1,
                        std::wstring_view pngTextKey = {},
                        std::string_view pngTextValue = {},
                        std::string_view jpegDescription = {})
    {
        fs::create_directories(path.parent_path());

        ComPtr<IWICImagingFactory> factory;
        CheckHResult(
            CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)),
            "Failed to create the WIC imaging factory for test image generation");

        std::vector<BYTE> pixels = BuildPixelBuffer(width, height);
        const UINT stride = width * 4;

        ComPtr<IWICBitmap> bitmap;
        CheckHResult(
            factory->CreateBitmapFromMemory(width,
                                            height,
                                            GUID_WICPixelFormat32bppBGRA,
                                            stride,
                                            static_cast<UINT>(pixels.size()),
                                            pixels.data(),
                                            &bitmap),
            "Failed to create the WIC bitmap backing store for a test image");

        ComPtr<IWICStream> stream;
        CheckHResult(factory->CreateStream(&stream), "Failed to create a WIC stream for a test image");
        CheckHResult(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE), "Failed to open the test image output path");

        ComPtr<IWICBitmapEncoder> encoder;
        CheckHResult(factory->CreateEncoder(ContainerFormatGuid(format), nullptr, &encoder), "Failed to create a WIC encoder");
        CheckHResult(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache), "Failed to initialize the WIC encoder");

        ComPtr<IWICBitmapFrameEncode> frame;
        ComPtr<IPropertyBag2> propertyBag;
        CheckHResult(encoder->CreateNewFrame(&frame, &propertyBag), "Failed to create a WIC frame encoder");
        CheckHResult(frame->Initialize(propertyBag.Get()), "Failed to initialize the WIC frame encoder");
        CheckHResult(frame->SetSize(width, height), "Failed to set the test image size");

        WICPixelFormatGUID pixelFormat = GUID_WICPixelFormat32bppBGRA;
        CheckHResult(frame->SetPixelFormat(&pixelFormat), "Failed to set the test image pixel format");

        if (format == TestImageFormat::Jpeg && (orientation != 1 || !jpegDescription.empty()))
        {
            ComPtr<IWICMetadataQueryWriter> metadataWriter;
            CheckHResult(frame->GetMetadataQueryWriter(&metadataWriter), "Failed to acquire the JPEG metadata writer");

            if (orientation != 1)
            {
                PROPVARIANT value;
                PropVariantInit(&value);
                CheckHResult(InitPropVariantFromUInt16(orientation, &value), "Failed to build the JPEG orientation metadata value");
                CheckHResult(metadataWriter->SetMetadataByName(L"/app1/ifd/{ushort=274}", &value), "Failed to write the JPEG orientation metadata");
                PropVariantClear(&value);
            }
            if (!jpegDescription.empty())
            {
                PROPVARIANT value;
                InitPropVariantFromAnsiText(jpegDescription, &value);
                CheckHResult(
                    metadataWriter->SetMetadataByName(L"/app1/ifd/{ushort=270}", &value),
                    "Failed to write the JPEG image description metadata");
                PropVariantClear(&value);
            }
        }
        else if (format == TestImageFormat::Png && !pngTextKey.empty() && !pngTextValue.empty())
        {
            ComPtr<IWICMetadataQueryWriter> metadataWriter;
            CheckHResult(frame->GetMetadataQueryWriter(&metadataWriter), "Failed to acquire the PNG metadata writer");

            std::wstring query = L"/tEXt/{str=";
            query.append(pngTextKey);
            query.append(L"}");

            PROPVARIANT value;
            InitPropVariantFromAnsiText(pngTextValue, &value);
            CheckHResult(metadataWriter->SetMetadataByName(query.c_str(), &value), "Failed to write the PNG text metadata");
            PropVariantClear(&value);
        }

        CheckHResult(frame->WriteSource(bitmap.Get(), nullptr), "Failed to write the test image pixels");
        CheckHResult(frame->Commit(), "Failed to commit the test image frame");
        CheckHResult(encoder->Commit(), "Failed to commit the test image encoder");
    }

    inline std::uint16_t ReadJpegOrientation(const fs::path& path)
    {
        ComPtr<IWICImagingFactory> factory;
        CheckHResult(
            CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)),
            "Failed to create the WIC imaging factory for JPEG metadata inspection");

        ComPtr<IWICBitmapDecoder> decoder;
        CheckHResult(
            factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder),
            "Failed to open the JPEG test image for metadata inspection");

        ComPtr<IWICBitmapFrameDecode> frame;
        CheckHResult(decoder->GetFrame(0, &frame), "Failed to read the JPEG frame for metadata inspection");

        ComPtr<IWICMetadataQueryReader> metadataReader;
        CheckHResult(frame->GetMetadataQueryReader(&metadataReader), "Failed to acquire the JPEG metadata reader");

        PROPVARIANT value;
        PropVariantInit(&value);
        CheckHResult(metadataReader->GetMetadataByName(L"/app1/ifd/{ushort=274}", &value), "Failed to read the JPEG orientation metadata");

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

    inline std::string ReadJpegMetadataString(const fs::path& path, const wchar_t* query)
    {
        ComPtr<IWICImagingFactory> factory;
        CheckHResult(
            CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)),
            "Failed to create the WIC imaging factory for JPEG metadata inspection");

        ComPtr<IWICBitmapDecoder> decoder;
        CheckHResult(
            factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder),
            "Failed to open the JPEG for metadata inspection");

        ComPtr<IWICBitmapFrameDecode> frame;
        CheckHResult(decoder->GetFrame(0, &frame), "Failed to read the JPEG frame for metadata inspection");

        ComPtr<IWICMetadataQueryReader> metadataReader;
        CheckHResult(frame->GetMetadataQueryReader(&metadataReader), "Failed to acquire the JPEG metadata reader");

        PROPVARIANT value;
        PropVariantInit(&value);
        CheckHResult(metadataReader->GetMetadataByName(query, &value), "Failed to read JPEG string metadata");
        const bool isAnsiString = value.vt == VT_LPSTR && value.pszVal != nullptr;
        const std::string result = isAnsiString ? value.pszVal : std::string{};
        PropVariantClear(&value);
        Expect(isAnsiString, "JPEG string metadata did not use the expected ANSI representation");
        return result;
    }

    inline std::vector<BYTE> ReadJpegScanData(const fs::path& path)
    {
        std::ifstream input(path, std::ios::binary);
        Expect(static_cast<bool>(input), "Failed to open a JPEG for scan-data comparison");
        const std::istreambuf_iterator<char> begin(input);
        const std::istreambuf_iterator<char> end;
        const std::vector<BYTE> bytes(begin, end);
        Expect(bytes.size() >= 4 && bytes[0] == 0xff && bytes[1] == 0xd8,
               "JPEG scan-data comparison received an invalid JPEG");

        std::size_t offset = 2;
        while (offset < bytes.size())
        {
            const std::size_t markerOffset = offset;
            Expect(bytes[offset] == 0xff, "JPEG scan-data comparison found an invalid marker");
            while (offset < bytes.size() && bytes[offset] == 0xff)
            {
                ++offset;
            }
            Expect(offset < bytes.size(), "JPEG scan-data comparison found a truncated marker");
            const BYTE marker = bytes[offset++];
            if (marker == 0xda)
            {
                return { bytes.begin() + markerOffset, bytes.end() };
            }
            if (marker == 0xd9)
            {
                break;
            }
            if (marker == 0x01 || (marker >= 0xd0 && marker <= 0xd7))
            {
                continue;
            }

            Expect(offset <= bytes.size() && bytes.size() - offset >= 2,
                   "JPEG scan-data comparison found a truncated segment length");
            const std::size_t segmentLength = (static_cast<std::size_t>(bytes[offset]) << 8)
                | static_cast<std::size_t>(bytes[offset + 1]);
            Expect(segmentLength >= 2 && segmentLength <= bytes.size() - offset,
                   "JPEG scan-data comparison found an invalid segment length");
            offset += segmentLength;
        }

        Expect(false, "JPEG scan-data comparison did not find the image scan");
        return {};
    }

    inline hyperbrowse::cache::ThumbnailCacheKey MakeCacheKey(const fs::path& path,
                                                       std::uint64_t modifiedTimestampUtc,
                                                       int targetWidth = 160,
                                                       int targetHeight = 112)
    {
        hyperbrowse::cache::ThumbnailCacheKey key;
        key.filePath = path.wstring();
        key.modifiedTimestampUtc = modifiedTimestampUtc;
        key.targetWidth = targetWidth;
        key.targetHeight = targetHeight;
        return key;
    }

    inline hyperbrowse::browser::BrowserItem MakeMetadataItem(std::wstring fileName,
                                                       std::wstring filePath,
                                                       std::uint64_t modifiedTimestampUtc)
    {
        hyperbrowse::browser::BrowserItem item;
        item.fileName = std::move(fileName);
        item.filePath = std::move(filePath);
        item.fileType = L"JPG";
        item.modifiedTimestampUtc = modifiedTimestampUtc;
        return item;
    }

    inline std::shared_ptr<const hyperbrowse::services::ImageMetadata> MakeMetadata(std::wstring cameraModel)
    {
        auto metadata = std::make_shared<hyperbrowse::services::ImageMetadata>();
        metadata->cameraModel = std::move(cameraModel);
        metadata->hasExif = !metadata->cameraModel.empty();
        return metadata;
    }

    inline fs::path TestSourceDirectory()
    {
#ifdef HYPERBROWSE_TESTS_SOURCE_DIR
        return fs::path(HYPERBROWSE_TESTS_SOURCE_DIR);
#else
        return fs::current_path();
#endif
    }
}
