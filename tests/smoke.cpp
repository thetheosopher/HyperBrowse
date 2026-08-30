#include <windows.h>

#include <commctrl.h>

#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#include <objbase.h>
#include <propvarutil.h>
#include <shlobj.h>
#include <shellapi.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
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
#include "decode/RawHelperProtocol.h"
#include "decode/WicThumbnailDecoder.h"
#include "services/BatchConvertService.h"
#include "services/FileOperationService.h"
#include "services/FolderEnumerationService.h"
#include "services/FolderTreeEnumerationService.h"
#include "services/FolderWatchService.h"
#include "services/ImageMetadataService.h"
#include "services/JpegTransformService.h"
#include "services/ThumbnailScheduler.h"
#include "ui/CommandIds.h"
#include "ui/MainWindow.h"
#include "ui/QuickSend.h"
#include "ui/ShortcutCatalog.h"
#include "util/BackgroundExecutor.h"
#include "util/SettingsRegistry.h"
#include "util/UiTextSize.h"
#include "viewer/ViewerWindow.h"

namespace fs = std::filesystem;

namespace
{
    using Microsoft::WRL::ComPtr;

    constexpr wchar_t kTestWindowClassName[] = L"HyperBrowseFolderEnumerationTestWindow";
    std::wstring gSmokeRegistryPath;
    const wchar_t* kRegistryPath = nullptr;
    constexpr wchar_t kRegistryValueViewerInfoOverlaysVisible[] = L"ViewerInfoOverlaysVisible";
    constexpr wchar_t kRegistryValueViewerInfoOverlayTextSize[] = L"ViewerInfoOverlayTextSize";
    constexpr wchar_t kRegistryValueViewerWindowedFullMetadataVisible[] = L"ViewerWindowedFullMetadataVisible";
    constexpr wchar_t kRegistryValueViewerFullScreenFullMetadataVisible[] = L"ViewerFullScreenFullMetadataVisible";
    constexpr wchar_t kRegistryValueViewerFullMetadataVisible[] = L"ViewerFullMetadataVisible";
    constexpr wchar_t kRegistryValueViewerEscapeKeyBehavior[] = L"ViewerEscapeKeyBehavior";
    constexpr wchar_t kRegistryValueInvertKeyboardPanning[] = L"InvertKeyboardPanning";
    constexpr wchar_t kRegistryValueAppTextSize[] = L"AppTextSize";
    constexpr wchar_t kRegistryValueWindowLeft[] = L"WindowLeft";
    constexpr wchar_t kRegistryValueWindowTop[] = L"WindowTop";
    constexpr wchar_t kRegistryValueWindowWidth[] = L"WindowWidth";
    constexpr wchar_t kRegistryValueWindowHeight[] = L"WindowHeight";
    constexpr wchar_t kRegistryValueSlideshowInterval[] = L"SlideshowIntervalMs";
    constexpr wchar_t kRegistryValueThumbnailCacheCapacityOverrideBytes[] = L"ThumbnailCacheCapacityOverrideBytes";
    constexpr wchar_t kRegistryValueMetadataCacheCapacityOverrideEntries[] = L"MetadataCacheCapacityOverrideEntries";

    bool ConfigureSmokeSettingsRegistry()
    {
        gSmokeRegistryPath = L"Software\\HyperBrowse\\SmokeTests\\" + std::to_wstring(GetCurrentProcessId());
        kRegistryPath = gSmokeRegistryPath.c_str();
        return SetEnvironmentVariableW(
            hyperbrowse::util::kSettingsRegistryEnvironmentVariable,
            gSmokeRegistryPath.c_str()) != FALSE;
    }

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

    struct TestWindowState
    {
        std::uint64_t expectedRequestId{};
        EnumerationResult enumerationResult;
        ThumbnailResult thumbnailResult;
        FolderTreeEnumerationResult folderTreeEnumerationResult;
        FileOperationResult fileOperationResult;
        int viewerStartFolderSlideshowRequests{};
        HWND lastViewerStartFolderSlideshowSource{};
        int viewerQuickSendRequests{};
        hyperbrowse::viewer::QuickSendOperation lastViewerQuickSendOperation{
            hyperbrowse::viewer::QuickSendOperation::Move};
        HWND lastViewerQuickSendSource{};
    };

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
            : root_(fs::temp_directory_path() / std::move(name))
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

    enum class TestImageFormat
    {
        Jpeg,
        Png,
        Gif,
        Tiff,
    };

    void Expect(bool condition, const std::string& message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    void RunSingleInstanceIdleClientScenario()
    {
        constexpr wchar_t kSingleInstancePipeName[] = L"\\\\.\\pipe\\TheTheosopher.HyperBrowse.Launch";
        constexpr wchar_t kMainWindowClassName[] = L"HyperBrowseMainWindow";

        ScopedRegistryDwordBackup singleInstanceBackup(
            kRegistryPath,
            L"SingleInstanceEnabled");
        hyperbrowse::app::Application::SetSingleInstanceEnabled(true);

        wchar_t modulePath[MAX_PATH]{};
        const DWORD modulePathLength = GetModuleFileNameW(nullptr, modulePath, static_cast<DWORD>(std::size(modulePath)));
        Expect(modulePathLength > 0 && modulePathLength < std::size(modulePath),
               "Failed to locate the smoke-test executable");

        const fs::path testDirectory = fs::path(modulePath).parent_path();
        const fs::path applicationPath = testDirectory.parent_path().parent_path()
            / testDirectory.filename()
            / L"HyperBrowse.exe";
        Expect(fs::exists(applicationPath), "Failed to locate the HyperBrowse executable for IPC testing");

        std::wstring commandLine = L"\"" + applicationPath.wstring() + L"\"";
        std::vector<wchar_t> mutableCommandLine(commandLine.begin(), commandLine.end());
        mutableCommandLine.push_back(L'\0');

        STARTUPINFOW startupInfo{};
        startupInfo.cb = sizeof(startupInfo);
        PROCESS_INFORMATION processInfo{};
        Expect(CreateProcessW(applicationPath.c_str(),
                               mutableCommandLine.data(),
                               nullptr,
                               nullptr,
                               FALSE,
                               0,
                               nullptr,
                               applicationPath.parent_path().c_str(),
                               &startupInfo,
                               &processInfo) != FALSE,
               "Failed to launch HyperBrowse for IPC testing");

        HANDLE pipe = INVALID_HANDLE_VALUE;
        const auto cleanup = [&]()
        {
            if (pipe != INVALID_HANDLE_VALUE)
            {
                CloseHandle(pipe);
                pipe = INVALID_HANDLE_VALUE;
            }
            if (processInfo.hProcess)
            {
                if (WaitForSingleObject(processInfo.hProcess, 0) == WAIT_TIMEOUT)
                {
                    TerminateProcess(processInfo.hProcess, 1);
                    WaitForSingleObject(processInfo.hProcess, 5000);
                }
                CloseHandle(processInfo.hProcess);
                processInfo.hProcess = nullptr;
            }
            if (processInfo.hThread)
            {
                CloseHandle(processInfo.hThread);
                processInfo.hThread = nullptr;
            }
        };
        const auto require = [&](bool condition, const char* message)
        {
            if (!condition)
            {
                cleanup();
                throw std::runtime_error(message);
            }
        };

        HWND mainWindow = nullptr;
        const ULONGLONG windowDeadline = GetTickCount64() + 10000;
        while (GetTickCount64() < windowDeadline)
        {
            if (WaitForSingleObject(processInfo.hProcess, 0) != WAIT_TIMEOUT)
            {
                break;
            }

            HWND candidate = FindWindowW(kMainWindowClassName, nullptr);
            DWORD candidateProcessId = 0;
            if (candidate)
            {
                GetWindowThreadProcessId(candidate, &candidateProcessId);
            }
            if (candidate && candidateProcessId == processInfo.dwProcessId)
            {
                mainWindow = candidate;
                break;
            }
            Sleep(25);
        }
        require(mainWindow != nullptr, "HyperBrowse did not create its main window for IPC testing");

        DWORD pipeError = ERROR_SUCCESS;
        const ULONGLONG pipeDeadline = GetTickCount64() + 10000;
        while (GetTickCount64() < pipeDeadline)
        {
            pipe = CreateFileW(kSingleInstancePipeName,
                               FILE_WRITE_DATA,
                               0,
                               nullptr,
                               OPEN_EXISTING,
                               0,
                               nullptr);
            if (pipe != INVALID_HANDLE_VALUE)
            {
                break;
            }
            pipeError = GetLastError();
            if (pipeError != ERROR_PIPE_BUSY && pipeError != ERROR_FILE_NOT_FOUND)
            {
                break;
            }
            WaitNamedPipeW(kSingleInstancePipeName, 100);
        }
        if (pipe == INVALID_HANDLE_VALUE)
        {
            cleanup();
            throw std::runtime_error(
                "Failed to connect an idle single-instance client (Win32 error "
                + std::to_string(pipeError)
                + ")");
        }
        require(PostMessageW(mainWindow, WM_CLOSE, 0, 0) != FALSE,
                "Failed to request HyperBrowse shutdown during idle-client testing");
        require(WaitForSingleObject(processInfo.hProcess, 5000) == WAIT_OBJECT_0,
                "An idle single-instance client prevented HyperBrowse from shutting down");

        DWORD exitCode = 1;
        require(GetExitCodeProcess(processInfo.hProcess, &exitCode) != FALSE && exitCode == 0,
                "HyperBrowse exited unsuccessfully during idle-client testing");
        cleanup();
    }

        void RunShortcutCatalogScenario()
        {
         using hyperbrowse::ui::ShortcutContext;
         using hyperbrowse::ui::ShortcutDefinition;
         using namespace hyperbrowse::ui::command_ids;

         Expect(hyperbrowse::ui::kMainMenuMnemonicCatalogValid,
             "Main command-bar menu contains duplicate access keys");
         Expect(hyperbrowse::ui::MainMenuMnemonicIndexFromVirtualKey('F') == 0,
             "Alt+F does not select the File menu");
         Expect(hyperbrowse::ui::MainMenuMnemonicIndexFromVirtualKey('E') == 1,
             "Alt+E does not select the Edit menu");
         Expect(hyperbrowse::ui::MainMenuMnemonicIndexFromVirtualKey('V') == 2,
             "Alt+V does not select the View menu");
         Expect(hyperbrowse::ui::MainMenuMnemonicIndexFromVirtualKey('H') == 3,
             "Alt+H does not select the Help menu");
         Expect(hyperbrowse::ui::MainMenuMnemonicIndexFromVirtualKey('x') == -1,
             "An unrelated access key selected a command-bar menu");

         Expect(hyperbrowse::ui::kShortcutCatalogValid,
             "Main-window shortcut catalog contains duplicate accelerator ownership");
         Expect(!hyperbrowse::ui::HasDuplicateShortcuts(hyperbrowse::ui::kViewerShortcutCatalog),
             "Viewer shortcut catalog contains duplicate keyboard behavior");

         const auto hasShortcut = [](std::span<const ShortcutDefinition> catalog,
                         ShortcutContext context,
                         UINT commandId,
                         WORD virtualKey,
                         BYTE modifiers)
         {
             return std::any_of(catalog.begin(),
                       catalog.end(),
                       [&](const ShortcutDefinition& shortcut)
                       {
                        return shortcut.context == context
                            && shortcut.commandId == commandId
                            && shortcut.virtualKey == virtualKey
                            && shortcut.modifiers == modifiers;
                       });
         };

         const auto mainShortcuts = hyperbrowse::ui::MainWindowShortcuts();
         Expect(hasShortcut(mainShortcuts, ShortcutContext::MainWindow, ID_FILE_ESCAPE, VK_ESCAPE, 0),
             "Escape is not owned by the dedicated Escape command");
         Expect(hasShortcut(mainShortcuts, ShortcutContext::MainWindow, ID_FILE_MINIMIZE, 'W', FCONTROL),
             "Ctrl+W no longer owns the main-window minimize command");
         Expect(!hasShortcut(mainShortcuts, ShortcutContext::MainWindow, ID_FILE_MINIMIZE, VK_ESCAPE, 0),
             "Escape still shares the Ctrl+W minimize command");
         Expect(hasShortcut(mainShortcuts, ShortcutContext::MainWindow, ID_EDIT_CUT, 'X', FCONTROL),
             "Ctrl+X is missing from the main-window shortcut catalog");
         Expect(hasShortcut(mainShortcuts, ShortcutContext::MainWindow, ID_VIEW_NAVIGATE_FORWARD_FOLDER, VK_RIGHT, FALT),
             "Alt+Right is missing from the folder navigation catalog");
         Expect(hasShortcut(mainShortcuts, ShortcutContext::MainWindow, ID_VIEW_THUMBNAIL_SIZE_INCREASE, VK_ADD, 0),
             "Numpad plus is missing from the thumbnail stepping catalog");
         Expect(hasShortcut(hyperbrowse::ui::ViewerShortcuts(),
                      ShortcutContext::Viewer,
                      ID_FILE_COPY_IMAGE_PIXELS,
                      'I',
                      FCONTROL | FSHIFT),
             "Viewer Copy Image shortcut is missing from the shared catalog");
         Expect(hasShortcut(hyperbrowse::ui::ViewerShortcuts(),
                      ShortcutContext::Viewer,
                      0,
                      '0',
                      0),
             "Viewer 0 fit shortcut is missing from the shared catalog");
         Expect(hasShortcut(hyperbrowse::ui::ViewerShortcuts(),
                      ShortcutContext::Viewer,
                      0,
                      '1',
                      0),
             "Viewer 1 actual-size shortcut is missing from the shared catalog");
         Expect(hasShortcut(hyperbrowse::ui::ViewerShortcuts(),
                      ShortcutContext::Viewer,
                      ID_VIEW_SLIDESHOW_FOLDER,
                      'F',
                      FCONTROL | FSHIFT),
             "Viewer Ctrl+Shift+F slideshow shortcut is missing from the shared catalog");
         Expect(hasShortcut(hyperbrowse::ui::ViewerShortcuts(),
                      ShortcutContext::Viewer,
                      ID_FILE_QUICK_SEND_MOVE,
                      VK_F7,
                      0),
             "Viewer F7 Quick Actions shortcut is missing from the shared catalog");
         Expect(hasShortcut(hyperbrowse::ui::ViewerShortcuts(),
                      ShortcutContext::Viewer,
                      ID_FILE_QUICK_SEND_COPY,
                      VK_F8,
                      0),
             "Viewer F8 Quick Actions shortcut is missing from the shared catalog");
         Expect(hasShortcut(hyperbrowse::ui::ViewerShortcuts(), ShortcutContext::Viewer, 0, 'H', 0),
             "Viewer H fit-height shortcut is missing from the shared catalog");
         Expect(hasShortcut(hyperbrowse::ui::ViewerShortcuts(), ShortcutContext::Viewer, 0, 'W', 0),
             "Viewer W fit-width shortcut is missing from the shared catalog");
         Expect(hasShortcut(hyperbrowse::ui::ViewerShortcuts(), ShortcutContext::Viewer, 0, 'L', 0),
             "Viewer L rotate-left shortcut is missing from the shared catalog");
         Expect(hasShortcut(hyperbrowse::ui::ViewerShortcuts(), ShortcutContext::Viewer, 0, 'R', 0),
             "Viewer R rotate-right shortcut is missing from the shared catalog");
         Expect(hasShortcut(hyperbrowse::ui::ViewerShortcuts(), ShortcutContext::Viewer, 0, 'C', 0),
             "Viewer C compare shortcut is missing from the shared catalog");
         Expect(hasShortcut(hyperbrowse::ui::ViewerShortcuts(), ShortcutContext::Viewer, 0, 'X', 0),
             "Viewer X compared-image shortcut is missing from the shared catalog");
         Expect(hasShortcut(hyperbrowse::ui::ViewerShortcuts(), ShortcutContext::Viewer, 0, VK_TAB, 0),
             "Viewer Tab overlay shortcut is missing from the shared catalog");
         Expect(hasShortcut(hyperbrowse::ui::ViewerShortcuts(), ShortcutContext::Viewer, 0, VK_SPACE, 0),
             "Viewer Space slideshow shortcut is missing from the shared catalog");
         Expect(hasShortcut(hyperbrowse::ui::ViewerShortcuts(), ShortcutContext::Viewer, 0, VK_F11, 0),
             "Viewer F11 full-screen shortcut is missing from the shared catalog");
         Expect(hasShortcut(hyperbrowse::ui::ViewerShortcuts(), ShortcutContext::Viewer, 0, VK_RETURN, FCONTROL),
             "Viewer Ctrl+Enter full-screen shortcut is missing from the shared catalog");
         Expect(hasShortcut(hyperbrowse::ui::ViewerShortcuts(), ShortcutContext::Viewer, 0, VK_PRIOR, 0),
             "Viewer Page Up shortcut is missing from the shared catalog");
         Expect(hasShortcut(hyperbrowse::ui::ViewerShortcuts(), ShortcutContext::Viewer, 0, VK_NEXT, 0),
             "Viewer Page Down shortcut is missing from the shared catalog");
         Expect(hasShortcut(hyperbrowse::ui::ViewerShortcuts(), ShortcutContext::Viewer, 0, VK_RETURN, 0),
             "Viewer Enter fit-toggle shortcut is missing from the shared catalog");
         Expect(hasShortcut(hyperbrowse::ui::ViewerShortcuts(), ShortcutContext::Viewer, 0, VK_OEM_PLUS, 0),
             "Viewer plus zoom shortcut is missing from the shared catalog");
         Expect(hasShortcut(hyperbrowse::ui::ViewerShortcuts(), ShortcutContext::Viewer, 0, VK_OEM_MINUS, 0),
             "Viewer minus zoom shortcut is missing from the shared catalog");
        }

    void RunBackgroundExecutorExceptionScenario()
    {
        hyperbrowse::util::BackgroundExecutor executor(1, 2);
        std::mutex completionMutex;
        std::condition_variable completionCondition;
        bool completed = false;

        Expect(executor.Post([]()
        {
            throw std::runtime_error("intentional background executor test failure");
        }), "Background executor rejected the exception-isolation test task");
        Expect(executor.Post([&]()
        {
            {
                std::scoped_lock lock(completionMutex);
                completed = true;
            }
            completionCondition.notify_one();
        }), "Background executor rejected the post-exception recovery task");

        std::unique_lock lock(completionMutex);
        Expect(completionCondition.wait_for(lock, std::chrono::seconds(2), [&]() { return completed; }),
               "Background executor did not continue after a task exception");
    }

    void SetRegistryDwordValue(const wchar_t* path, const wchar_t* valueName, DWORD value)
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

    void DeleteRegistryValue(const wchar_t* path, const wchar_t* valueName)
    {
        HKEY key{};
        if (RegOpenKeyExW(HKEY_CURRENT_USER, path, 0, KEY_WRITE, &key) == ERROR_SUCCESS)
        {
            RegDeleteValueW(key, valueName);
            RegCloseKey(key);
        }
    }

    bool TryReadRegistryDwordValue(const wchar_t* path, const wchar_t* valueName, DWORD* value)
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

    void CheckHResult(HRESULT result, const char* message)
    {
        if (FAILED(result))
        {
            throw std::runtime_error(message);
        }
    }

    std::string Utf8FromWide(std::wstring_view text)
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

    void ResetEnumerationResult(TestWindowState* state)
    {
        state->enumerationResult = EnumerationResult{};
    }

    void ResetThumbnailResult(TestWindowState* state, std::uint64_t expectedSessionId)
    {
        state->thumbnailResult = ThumbnailResult{};
        state->thumbnailResult.expectedSessionId = expectedSessionId;
    }

    void ResetFolderTreeEnumerationResult(TestWindowState* state)
    {
        state->folderTreeEnumerationResult = FolderTreeEnumerationResult{};
    }

    bool PumpMessagesUntil(const std::function<bool()>& predicate, DWORD timeoutMs)
    {
        const ULONGLONG deadline = GetTickCount64() + timeoutMs;
        MSG msg{};
        while (GetTickCount64() < deadline)
        {
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
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

    void PumpMessagesFor(DWORD durationMs)
    {
        const ULONGLONG deadline = GetTickCount64() + durationMs;
        MSG msg{};
        while (GetTickCount64() < deadline)
        {
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }

            MsgWaitForMultipleObjectsEx(0, nullptr, 25, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        }
    }

    bool ReadClientPixel(HWND window, POINT point, COLORREF* color)
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

    std::wstring ReadTreeItemText(HWND treeView, HTREEITEM item)
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

    std::wstring QueryShellDisplayName(const std::wstring& folderPath)
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

    std::wstring TryGetKnownFolderPathForTest(REFKNOWNFOLDERID folderId)
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

    std::vector<std::wstring> ExpectedSpecialFolderRootTexts()
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

    LRESULT CALLBACK TestWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        (void)wParam;
        if (message == WM_NCCREATE)
        {
            auto* createStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(createStruct->lpCreateParams));
            return TRUE;
        }

        auto* state = reinterpret_cast<TestWindowState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
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

    HWND CreateTestWindow(TestWindowState* state, HINSTANCE instance)
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

    HWND CreateUiHostWindow(HINSTANCE instance)
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

    GUID ContainerFormatGuid(TestImageFormat format)
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

    std::vector<BYTE> BuildPixelBuffer(UINT width, UINT height)
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

    void InitPropVariantFromAnsiText(std::string_view text, PROPVARIANT* value)
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

    void WriteTestImage(const fs::path& path,
                        TestImageFormat format,
                        UINT width,
                        UINT height,
                        std::uint16_t orientation = 1,
                        std::wstring_view pngTextKey = {},
                        std::string_view pngTextValue = {})
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

        if (format == TestImageFormat::Jpeg && orientation != 1)
        {
            ComPtr<IWICMetadataQueryWriter> metadataWriter;
            CheckHResult(frame->GetMetadataQueryWriter(&metadataWriter), "Failed to acquire the JPEG metadata writer");

            PROPVARIANT value;
            PropVariantInit(&value);
            CheckHResult(InitPropVariantFromUInt16(orientation, &value), "Failed to build the JPEG orientation metadata value");
            CheckHResult(metadataWriter->SetMetadataByName(L"/app1/ifd/{ushort=274}", &value), "Failed to write the JPEG orientation metadata");
            PropVariantClear(&value);
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

    std::uint16_t ReadJpegOrientation(const fs::path& path)
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

    hyperbrowse::cache::ThumbnailCacheKey MakeCacheKey(const fs::path& path,
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

    hyperbrowse::browser::BrowserItem MakeMetadataItem(std::wstring fileName,
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

    std::shared_ptr<const hyperbrowse::services::ImageMetadata> MakeMetadata(std::wstring cameraModel)
    {
        auto metadata = std::make_shared<hyperbrowse::services::ImageMetadata>();
        metadata->cameraModel = std::move(cameraModel);
        metadata->hasExif = !metadata->cameraModel.empty();
        return metadata;
    }

    fs::path TestSourceDirectory()
    {
#ifdef HYPERBROWSE_TESTS_SOURCE_DIR
        return fs::path(HYPERBROWSE_TESTS_SOURCE_DIR);
#else
        return fs::current_path();
#endif
    }

    void RunEnumerationScenario(HWND hwnd, TestWindowState* state)
    {
        hyperbrowse::services::FolderEnumerationService service;

        TempFolder root(L"HyperBrowsePrompt3Root");
        root.WriteFile(L"one.jpg", 10);
        root.WriteFile(L"two.png", 20);
        root.WriteFile(L"six.cr2", 60);
        root.WriteFile(L"seven.cr3", 70);
        root.WriteFile(L"eight.arw", 80);
        root.WriteFile(L"nine.dng", 90);
        root.WriteFile(L"ten.raf", 100);
        root.WriteFile(L"eleven.rw2", 110);
        root.WriteFile(L"five.NRW", 50);
        root.WriteFile(L"ignore.txt", 5);
        root.WriteFile(L"nested\\three.gif", 30);
        root.WriteFile(L"nested\\four.nef", 40);

        ResetEnumerationResult(state);
        state->expectedRequestId = service.EnumerateFolderAsync(hwnd, root.Root().wstring(), false);
        Expect(PumpMessagesUntil([&]() { return state->enumerationResult.completed || state->enumerationResult.failed; }, 5000),
               "Non-recursive enumeration timed out or failed");
         Expect(state->enumerationResult.totalCount == 9, "Non-recursive enumeration returned the wrong supported-file count");
         Expect(state->enumerationResult.totalBytes == 590, "Non-recursive enumeration returned the wrong byte total");
         Expect(state->enumerationResult.items.size() == 9, "Non-recursive enumeration returned the wrong batch item count");
        Expect(state->enumerationResult.items.front().placeholderWidth == 256, "Placeholder width was not collected");
        Expect(state->enumerationResult.items.front().placeholderHeight == 256, "Placeholder height was not collected");

         ResetEnumerationResult(state);
         state->expectedRequestId = service.EnumerateFolderAsync(hwnd, root.Root().wstring(), false, true);
         Expect(PumpMessagesUntil([&]() { return state->enumerationResult.completed || state->enumerationResult.failed; }, 5000),
             "Subfolder enumeration timed out or failed");
         Expect(state->enumerationResult.totalCount == 10, "Subfolder enumeration returned the wrong item count");
         Expect(state->enumerationResult.totalBytes == 590, "Subfolder enumeration changed the file byte total");
         const auto nestedFolder = std::find_if(state->enumerationResult.items.begin(),
                                 state->enumerationResult.items.end(),
                                 [](const hyperbrowse::browser::BrowserItem& item)
                                 {
                                  return item.fileName == L"nested";
                                 });
         Expect(nestedFolder != state->enumerationResult.items.end() && nestedFolder->isDirectory,
             "Subfolder enumeration did not emit a directory item");

         TempFolder firstPage(L"HyperBrowseFirstPageEnumeration");
        for (int index = 0; index < 40; ++index)
         {
             firstPage.WriteFile(L"image_" + std::to_wstring(index) + L".jpg", 1);
         }
         ResetEnumerationResult(state);
         state->expectedRequestId = service.EnumerateFolderAsync(hwnd, firstPage.Root().wstring(), false);
         Expect(PumpMessagesUntil([&]() { return state->enumerationResult.completed || state->enumerationResult.failed; }, 5000),
             "First-page enumeration timed out or failed");
         Expect(state->enumerationResult.firstBatchSize == 16,
             "Folder enumeration did not publish the optimized first-page batch");
         Expect(state->enumerationResult.secondBatchSize == 16,
             "Folder enumeration did not publish the optimized second-page batch");
         Expect(state->enumerationResult.batchCount == 3,
             "Folder enumeration did not flush the remaining items after the priority batches");

        ResetEnumerationResult(state);
          state->expectedRequestId = service.EnumerateFolderAsync(hwnd, root.Root().wstring(), true, true);
        Expect(PumpMessagesUntil([&]() { return state->enumerationResult.completed || state->enumerationResult.failed; }, 5000),
               "Recursive enumeration timed out or failed");
           Expect(state->enumerationResult.totalCount == 12, "Recursive enumeration returned the wrong item count");
         Expect(state->enumerationResult.totalBytes == 660, "Recursive enumeration returned the wrong byte total");

        TempFolder slow(L"HyperBrowsePrompt3Slow");
        for (int index = 0; index < 400; ++index)
        {
            slow.WriteFile(L"bulk\\image_" + std::to_wstring(index) + L".jpg", 1);
        }

        TempFolder quick(L"HyperBrowsePrompt3Quick");
        quick.WriteFile(L"picked.png", 7);

        ResetEnumerationResult(state);
        service.EnumerateFolderAsync(hwnd, slow.Root().wstring(), false);
        state->expectedRequestId = service.EnumerateFolderAsync(hwnd, quick.Root().wstring(), false);
        Expect(PumpMessagesUntil([&]() { return state->enumerationResult.completed || state->enumerationResult.failed; }, 5000),
               "Cancellation scenario timed out or failed");
        Expect(state->enumerationResult.totalCount == 1, "Cancellation scenario did not surface the latest folder request");
        Expect(state->enumerationResult.items.size() == 1, "Cancellation scenario returned stale items from the superseded request");
        Expect(state->enumerationResult.items.front().fileName == L"picked.png", "Cancellation scenario returned the wrong final file");
        Expect(state->enumerationResult.items.front().fileType == L"PNG", "Enumeration did not capture the file type field");
        Expect(state->enumerationResult.items.front().modifiedTimestampUtc != 0, "Enumeration did not capture the modified timestamp field");
    }

        void RunFolderTreeEnumerationScenario(HWND hwnd, TestWindowState* state)
        {
         hyperbrowse::services::FolderTreeEnumerationService service;

         TempFolder root(L"HyperBrowseFolderTreeEnumeration");
         fs::create_directories(root.Root() / L"gamma");
         fs::create_directories(root.Root() / L"alpha");
         fs::create_directories(root.Root() / L"beta");
         fs::create_directories(root.Root() / L"beta" / L"nested");
         const fs::path hiddenFolder = root.Root() / L"hidden";
         fs::create_directories(hiddenFolder);
         Expect(SetFileAttributesW(hiddenFolder.c_str(), FILE_ATTRIBUTE_HIDDEN) != FALSE,
             "Folder-tree enumeration test could not mark the hidden directory");
         root.WriteFile(L"alpha\\image.jpg", 1);

         SHELLFLAGSTATE shellState{};
         SHGetSettings(&shellState, SSF_SHOWALLOBJECTS);
         const bool showHiddenFolders = shellState.fShowAllObjects != FALSE;

         ResetFolderTreeEnumerationResult(state);
         state->folderTreeEnumerationResult.expectedRequestId = service.EnumerateChildDirectoriesAsync(hwnd, root.Root().wstring());
         Expect(PumpMessagesUntil([&]()
         {
             return state->folderTreeEnumerationResult.completed || state->folderTreeEnumerationResult.failed;
         }, 5000), "Folder-tree enumeration timed out or failed");
         Expect(!state->folderTreeEnumerationResult.failed,
             std::string("Folder-tree enumeration failed: ") + Utf8FromWide(state->folderTreeEnumerationResult.errorMessage));
         Expect(state->folderTreeEnumerationResult.childFolders.size() == (showHiddenFolders ? 4 : 3),
             "Folder-tree enumeration returned the wrong number of child directories");
         Expect(fs::path(state->folderTreeEnumerationResult.childFolders[0].path).filename().wstring() == L"alpha",
             "Folder-tree enumeration did not sort child folders alphabetically");
         Expect(fs::path(state->folderTreeEnumerationResult.childFolders[1].path).filename().wstring() == L"beta",
             "Folder-tree enumeration did not preserve the expected alphabetical order");
         Expect(fs::path(state->folderTreeEnumerationResult.childFolders[2].path).filename().wstring() == L"gamma",
             "Folder-tree enumeration omitted the last child folder");
         ResetFolderTreeEnumerationResult(state);
         std::vector<std::wstring> childPresencePaths = {
             (root.Root() / L"alpha").wstring(),
             (root.Root() / L"beta").wstring(),
             (root.Root() / L"gamma").wstring(),
         };
         state->folderTreeEnumerationResult.expectedRequestId = service.QueryChildDirectoryPresenceAsync(
             hwnd,
             std::move(childPresencePaths));
         Expect(PumpMessagesUntil([&]()
         {
             return state->folderTreeEnumerationResult.completed || state->folderTreeEnumerationResult.failed;
         }, 5000), "Folder-tree child-presence query timed out or failed");
         Expect(!state->folderTreeEnumerationResult.failed,
             std::string("Folder-tree child-presence query failed: ")
                 + Utf8FromWide(state->folderTreeEnumerationResult.errorMessage));
         Expect(state->folderTreeEnumerationResult.childPresenceResults.size() == 3,
             "Folder-tree child-presence query returned the wrong number of results");
         Expect(!state->folderTreeEnumerationResult.childPresenceResults[0].hasChildren,
             "Folder-tree child-presence query incorrectly marked a leaf folder as expandable");
         Expect(state->folderTreeEnumerationResult.childPresenceResults[1].hasChildren,
             "Folder-tree child-presence query did not detect a nested child folder");
         Expect(!state->folderTreeEnumerationResult.childPresenceResults[2].hasChildren,
             "Folder-tree child-presence query incorrectly marked an empty folder as expandable");
         if (showHiddenFolders)
         {
             Expect(fs::path(state->folderTreeEnumerationResult.childFolders[3].path).filename().wstring() == L"hidden",
                 "Folder-tree enumeration omitted a visible hidden child folder");
         }
         Expect(SetFileAttributesW(hiddenFolder.c_str(), FILE_ATTRIBUTE_NORMAL) != FALSE,
             "Folder-tree enumeration test could not restore the hidden directory attributes");
        }

    void RunFolderWatchStartStopScenario(HWND hwnd)
    {
        TempFolder root(L"HyperBrowseFolderWatchStop");
        root.WriteFile(L"seed.jpg", 10);

        hyperbrowse::services::FolderWatchService service;
        for (int iteration = 0; iteration < 32; ++iteration)
        {
            service.StartWatching(hwnd, root.Root().wstring(), false);
            root.WriteFile(L"image_" + std::to_wstring(iteration) + L".jpg", static_cast<std::size_t>(iteration + 1));
            PumpMessagesFor(10);
            service.Stop();
        }
    }

        void AppendFolderWatchRecord(std::vector<BYTE>* buffer, DWORD action, const std::wstring& relativePath)
        {
         Expect(buffer != nullptr, "Folder watch test buffer was null");
         const std::size_t headerBytes = offsetof(FILE_NOTIFY_INFORMATION, FileName);
         const std::size_t recordBytes = headerBytes + relativePath.size() * sizeof(WCHAR);
         const std::size_t alignedRecordBytes = (recordBytes + sizeof(DWORD) - 1) & ~(sizeof(DWORD) - 1);
         const std::size_t recordOffset = buffer->size();
         buffer->resize(recordOffset + alignedRecordBytes);
         std::memset(buffer->data() + recordOffset, 0, alignedRecordBytes);

         if (recordOffset != 0)
         {
             std::size_t previousOffset = 0;
             while (true)
             {
              auto* previous = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(buffer->data() + previousOffset);
              if (previous->NextEntryOffset == 0)
              {
                  previous->NextEntryOffset = static_cast<DWORD>(recordOffset - previousOffset);
                  break;
              }
              previousOffset += previous->NextEntryOffset;
             }
         }

         auto* record = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(buffer->data() + recordOffset);
         record->Action = action;
         record->FileNameLength = static_cast<DWORD>(relativePath.size() * sizeof(WCHAR));
         std::memcpy(record->FileName, relativePath.data(), record->FileNameLength);
        }

        void RunFolderWatchNotificationParserScenario()
        {
         constexpr wchar_t folderPath[] = L"C:\\HyperBrowseWatchTest";
         hyperbrowse::services::FolderWatchNotificationParser parser;

         std::vector<BYTE> oldNameBuffer;
         AppendFolderWatchRecord(&oldNameBuffer, FILE_ACTION_RENAMED_OLD_NAME, L"old-name.jpg");
         hyperbrowse::services::FolderWatchUpdate oldNameUpdate;
         parser.Append(oldNameBuffer.data(), static_cast<DWORD>(oldNameBuffer.size()), folderPath, &oldNameUpdate);
         Expect(oldNameUpdate.events.empty() && !oldNameUpdate.requiresFullReload,
             "Folder watcher did not retain the first half of a rename");

         std::vector<BYTE> newNameBuffer;
         AppendFolderWatchRecord(&newNameBuffer, FILE_ACTION_RENAMED_NEW_NAME, L"new-name.jpg");
         hyperbrowse::services::FolderWatchUpdate splitRenameUpdate;
         parser.Append(newNameBuffer.data(), static_cast<DWORD>(newNameBuffer.size()), folderPath, &splitRenameUpdate);
         Expect(splitRenameUpdate.events.size() == 1,
             "Folder watcher did not pair rename records split across completions");
         Expect(splitRenameUpdate.events.front().kind == hyperbrowse::services::FolderWatchEventKind::Renamed
                 && fs::path(splitRenameUpdate.events.front().oldPath).filename() == L"old-name.jpg"
                 && fs::path(splitRenameUpdate.events.front().path).filename() == L"new-name.jpg",
             "Folder watcher paired split rename records incorrectly");

         parser.Reset();
         std::vector<BYTE> sameBuffer;
         AppendFolderWatchRecord(&sameBuffer, FILE_ACTION_RENAMED_OLD_NAME, L"first.jpg");
         AppendFolderWatchRecord(&sameBuffer, FILE_ACTION_RENAMED_NEW_NAME, L"second.jpg");
         hyperbrowse::services::FolderWatchUpdate sameBufferUpdate;
         parser.Append(sameBuffer.data(), static_cast<DWORD>(sameBuffer.size()), folderPath, &sameBufferUpdate);
         Expect(sameBufferUpdate.events.size() == 1 && !sameBufferUpdate.requiresFullReload,
             "Folder watcher did not pair rename records in one completion");

         parser.Reset();
         hyperbrowse::services::FolderWatchUpdate orphanNewUpdate;
         parser.Append(newNameBuffer.data(), static_cast<DWORD>(newNameBuffer.size()), folderPath, &orphanNewUpdate);
         Expect(orphanNewUpdate.requiresFullReload,
             "Folder watcher did not request a full reload for an orphan new-name record");

         parser.Reset();
         hyperbrowse::services::FolderWatchUpdate pendingOldUpdate;
         parser.Append(oldNameBuffer.data(), static_cast<DWORD>(oldNameBuffer.size()), folderPath, &pendingOldUpdate);
         std::vector<BYTE> addedBuffer;
         AppendFolderWatchRecord(&addedBuffer, FILE_ACTION_ADDED, L"added.jpg");
         hyperbrowse::services::FolderWatchUpdate orphanOldUpdate;
         parser.Append(addedBuffer.data(), static_cast<DWORD>(addedBuffer.size()), folderPath, &orphanOldUpdate);
         Expect(orphanOldUpdate.requiresFullReload,
             "Folder watcher did not request a full reload for an orphan old-name record");

         parser.Reset();
         hyperbrowse::services::FolderWatchUpdate resetOldUpdate;
         parser.Append(oldNameBuffer.data(), static_cast<DWORD>(oldNameBuffer.size()), folderPath, &resetOldUpdate);
         parser.Reset();
         hyperbrowse::services::FolderWatchUpdate resetNewUpdate;
         parser.Append(newNameBuffer.data(), static_cast<DWORD>(newNameBuffer.size()), folderPath, &resetNewUpdate);
         Expect(resetNewUpdate.requiresFullReload,
             "Folder watcher did not clear rename state after reset");

         std::vector<BYTE> malformedBuffer(offsetof(FILE_NOTIFY_INFORMATION, FileName));
         auto* malformedRecord = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(malformedBuffer.data());
         malformedRecord->NextEntryOffset = 1;
         hyperbrowse::services::FolderWatchUpdate malformedUpdate;
         parser.Append(malformedBuffer.data(), static_cast<DWORD>(malformedBuffer.size()), folderPath, &malformedUpdate);
         Expect(malformedUpdate.requiresFullReload,
             "Folder watcher did not request a full reload for a malformed notification");
        }

    void RunThumbnailCacheNormalizationScenario()
    {
        TempFolder root(L"HyperBrowseThumbnailCacheNormalization");
        const fs::path imagePath = root.Root() / L"MixedCase.png";
        WriteTestImage(imagePath, TestImageFormat::Png, 96, 48);

        hyperbrowse::decode::WicThumbnailDecoder decoder;
        const auto insertedKey = MakeCacheKey(imagePath, 17);
        const auto thumbnail = decoder.Decode(insertedKey);
        Expect(thumbnail != nullptr, "Failed to create the thumbnail used for cache normalization testing");

        hyperbrowse::cache::ThumbnailCache cache(4ULL * 1024ULL * 1024ULL);
        cache.Insert(insertedKey, thumbnail);

        auto lookupKey = insertedKey;
        lookupKey.filePath = imagePath.wstring();
        std::replace(lookupKey.filePath.begin(), lookupKey.filePath.end(), L'\\', L'/');
        std::transform(lookupKey.filePath.begin(), lookupKey.filePath.end(), lookupKey.filePath.begin(), [](wchar_t character)
        {
            return static_cast<wchar_t>(towupper(character));
        });

        Expect(cache.Find(lookupKey) != nullptr, "Thumbnail cache lookup did not normalize slash and case differences");

        cache.InvalidateFilePaths({lookupKey.filePath});
        Expect(cache.Find(insertedKey) == nullptr, "Thumbnail cache invalidation did not normalize the supplied file path");
    }

    void RunDiskThumbnailCacheCorruptionScenario()
    {
        TempFolder root(L"HyperBrowseDiskThumbnailCacheCorruption");
        const fs::path imagePath = root.Root() / L"sample.png";
        const fs::path cacheRoot = root.Root() / L"cache";
        fs::create_directories(cacheRoot);
        WriteTestImage(imagePath, TestImageFormat::Png, 32, 16);

        hyperbrowse::decode::WicThumbnailDecoder decoder;
        const auto key = MakeCacheKey(imagePath, 29);
        const auto thumbnail = decoder.Decode(key);
        Expect(thumbnail != nullptr, "Failed to create the thumbnail used for persistent-cache testing");

        hyperbrowse::cache::DiskThumbnailCache cache(4ULL * 1024ULL * 1024ULL, cacheRoot.wstring());
        cache.Store(key, thumbnail);
         std::wstring indexLineBeforeHit;
         {
             std::wifstream indexStream(cacheRoot / L"index.tsv");
             Expect(static_cast<bool>(std::getline(indexStream, indexLineBeforeHit)),
                 "Persistent thumbnail cache did not write an index row");
         }
        Expect(cache.TryLoad(key) != nullptr, "Persistent thumbnail cache did not round-trip a valid entry");
         std::wstring indexLineAfterHit;
         {
             std::wifstream indexStream(cacheRoot / L"index.tsv");
             Expect(static_cast<bool>(std::getline(indexStream, indexLineAfterHit)),
                 "Persistent thumbnail cache lost its index row after a cache hit");
         }
         Expect(indexLineBeforeHit == indexLineAfterHit,
             "Persistent thumbnail cache rewrote the index on a single cache hit");
         for (int hit = 1; hit < 64; ++hit)
         {
             Expect(cache.TryLoad(key) != nullptr,
                 "Persistent thumbnail cache failed during bounded access persistence testing");
         }
         std::wstring indexLineAfterBatch;
         bool accessMetadataPersisted = false;
         for (int attempt = 0; attempt < 100 && !accessMetadataPersisted; ++attempt)
         {
             indexLineAfterBatch.clear();
             std::wifstream indexStream(cacheRoot / L"index.tsv");
             if (std::getline(indexStream, indexLineAfterBatch))
             {
                 accessMetadataPersisted = indexLineAfterBatch != indexLineBeforeHit;
             }
             if (!accessMetadataPersisted)
             {
                 Sleep(10);
             }
         }
         Expect(accessMetadataPersisted,
             "Persistent thumbnail cache did not persist batched access metadata");

#pragma pack(push, 1)
        struct TestDiskThumbnailHeader
        {
            char magic[8];
            std::uint32_t width{};
            std::uint32_t height{};
            std::uint32_t sourceWidth{};
            std::uint32_t sourceHeight{};
            std::uint64_t pixelBytes{};
        };
#pragma pack(pop)

        const auto corruptEntry = [&](TestDiskThumbnailHeader header, std::size_t payloadBytes, const char* message)
        {
            cache.Store(key, thumbnail);
            fs::path cacheFile;
            for (const fs::directory_entry& entry : fs::directory_iterator(cacheRoot))
            {
                if (entry.path().extension() == L".thumb")
                {
                    cacheFile = entry.path();
                    break;
                }
            }
            Expect(!cacheFile.empty(), "Persistent thumbnail cache did not create an entry file");

            std::ofstream stream(cacheFile, std::ios::binary | std::ios::trunc);
            stream.write(reinterpret_cast<const char*>(&header), sizeof(header));
            std::vector<char> payload(payloadBytes, '\0');
            stream.write(payload.data(), static_cast<std::streamsize>(payload.size()));
            stream.close();

            Expect(cache.TryLoad(key) == nullptr, message);
            Expect(!fs::exists(cacheFile), "Persistent thumbnail cache did not remove the corrupt entry");
        };

        TestDiskThumbnailHeader validHeader{};
        std::memcpy(validHeader.magic, "HBTHMB01", sizeof(validHeader.magic));
        validHeader.width = 32;
        validHeader.height = 16;
        validHeader.sourceWidth = 32;
        validHeader.sourceHeight = 16;
        validHeader.pixelBytes = 32ULL * 16ULL * 4ULL;

        TestDiskThumbnailHeader wrongMagic = validHeader;
        wrongMagic.magic[0] = 'X';
        corruptEntry(wrongMagic, static_cast<std::size_t>(wrongMagic.pixelBytes), "Persistent thumbnail cache accepted an invalid magic value");

        TestDiskThumbnailHeader oversizedDimensions = validHeader;
        oversizedDimensions.width = UINT32_MAX;
        corruptEntry(oversizedDimensions, 0, "Persistent thumbnail cache accepted oversized dimensions");

        TestDiskThumbnailHeader wrongByteCount = validHeader;
        wrongByteCount.pixelBytes += 4;
        corruptEntry(wrongByteCount, static_cast<std::size_t>(wrongByteCount.pixelBytes), "Persistent thumbnail cache accepted a mismatched pixel byte count");

        corruptEntry(validHeader, static_cast<std::size_t>(validHeader.pixelBytes - 1), "Persistent thumbnail cache accepted a truncated payload");

        cache.Store(key, thumbnail);
        fs::path validCacheFile;
        for (const fs::directory_entry& entry : fs::directory_iterator(cacheRoot))
        {
            if (entry.path().extension() == L".thumb")
            {
                validCacheFile = entry.path();
                break;
            }
        }
        Expect(!validCacheFile.empty(), "Persistent thumbnail cache did not recreate a valid entry");
        std::wstring validIndexLine;
        {
            std::wifstream indexStream(cacheRoot / L"index.tsv");
            Expect(static_cast<bool>(std::getline(indexStream, validIndexLine)) && !validIndexLine.empty(),
                   "Persistent thumbnail cache did not serialize a valid index row");
        }
        {
            std::wofstream indexStream(cacheRoot / L"index.tsv", std::ios::trunc);
            indexStream << validIndexLine << L'\n'
                        << L"C:\\invalid\\overflow.jpg\t18446744073709551616\t29\t29\t0123456789abcdef.thumb\t100\t1\n"
                        << L"C:\\invalid\\text.jpg\tnot-a-number\t29\t29\t0123456789abcdef.thumb\t100\t1\n"
                        << L"C:\\invalid\\zero-width.jpg\t1\t0\t29\t0123456789abcdef.thumb\t100\t1\n"
                        << L"C:\\invalid\\unsafe-name.jpg\t1\t29\t29\t..\\outside.thumb\t100\t1\n";
        }

         {
             hyperbrowse::cache::DiskThumbnailCache reloadedCache(4ULL * 1024ULL * 1024ULL, cacheRoot.wstring());
             const auto statistics = reloadedCache.QueryStatistics();
             Expect(statistics.indexedEntryCount == 1,
                 "Persistent thumbnail cache did not skip malformed index rows");
             Expect(reloadedCache.TryLoad(key) != nullptr,
                 "Persistent thumbnail cache discarded a valid row beside malformed index rows");
         }

        const fs::path outsideCacheFile = root.Root() / L"outside.thumb";
        {
            std::ofstream outsideStream(outsideCacheFile, std::ios::binary | std::ios::trunc);
            outsideStream << "keep";
        }

        {
            std::wofstream indexStream(cacheRoot / L"index.tsv", std::ios::trunc);
            indexStream << hyperbrowse::util::NormalizePathForComparison(key.filePath)
                        << L'\t' << key.modifiedTimestampUtc
                        << L'\t' << key.targetWidth
                        << L'\t' << key.targetHeight
                        << L"\t..\\outside.thumb\t"
                        << (sizeof(TestDiskThumbnailHeader) + key.targetWidth * key.targetHeight * 4)
                        << L"\t1\n";
        }

         hyperbrowse::cache::DiskThumbnailCache reloadedCache(4ULL * 1024ULL * 1024ULL, cacheRoot.wstring());
         Expect(reloadedCache.TryLoad(key) == nullptr,
             "Persistent thumbnail cache accepted an unsafe index file name");
        Expect(fs::exists(outsideCacheFile),
               "Persistent thumbnail cache cleanup escaped its cache directory");
    }

    void RunWicDecoderScenario()
    {
        TempFolder root(L"HyperBrowsePrompt5Decoder");
        const fs::path jpegPath = root.Root() / L"rotated.jpg";
        const fs::path pngPath = root.Root() / L"sample.png";
        const fs::path gifPath = root.Root() / L"sample.gif";
        const fs::path tiffPath = root.Root() / L"sample.tif";

        WriteTestImage(jpegPath, TestImageFormat::Jpeg, 24, 48, 6);
        WriteTestImage(pngPath, TestImageFormat::Png, 96, 48);
        WriteTestImage(gifPath, TestImageFormat::Gif, 36, 18);
        WriteTestImage(tiffPath, TestImageFormat::Tiff, 18, 54);

        hyperbrowse::decode::WicThumbnailDecoder decoder;

        const auto jpegThumbnail = decoder.Decode(MakeCacheKey(jpegPath, 1));
        Expect(jpegThumbnail != nullptr, "WIC failed to decode the JPEG thumbnail");
         Expect(jpegThumbnail->Width() > jpegThumbnail->Height(), "WIC did not apply JPEG EXIF orientation");
         Expect(jpegThumbnail->SourceWidth() == 48 && jpegThumbnail->SourceHeight() == 24,
             "WIC did not surface the oriented JPEG source dimensions");

        const auto pngThumbnail = decoder.Decode(MakeCacheKey(pngPath, 2));
        Expect(pngThumbnail != nullptr, "WIC failed to decode the PNG thumbnail");
         Expect(pngThumbnail->Width() <= 160 && pngThumbnail->Height() <= 112, "PNG thumbnail scaling exceeded the target bounds");
         Expect(pngThumbnail->SourceWidth() == 96 && pngThumbnail->SourceHeight() == 48,
             "WIC did not surface the PNG source dimensions");

        const auto gifThumbnail = decoder.Decode(MakeCacheKey(gifPath, 3));
         Expect(gifThumbnail != nullptr, "WIC failed to decode the GIF first frame thumbnail");
         Expect(gifThumbnail->SourceWidth() == 36 && gifThumbnail->SourceHeight() == 18,
             "WIC did not surface the GIF source dimensions");

        const auto tiffThumbnail = decoder.Decode(MakeCacheKey(tiffPath, 4));
         Expect(tiffThumbnail != nullptr, "WIC failed to decode the TIFF first page thumbnail");
         Expect(tiffThumbnail->SourceWidth() == 18 && tiffThumbnail->SourceHeight() == 54,
             "WIC did not surface the TIFF source dimensions");
    }

    void RunJpegOrientationAdjustmentScenario()
    {
        TempFolder root(L"HyperBrowseJpegOrientation");

        const auto runCase = [&](const wchar_t* fileName, int quarterTurnsDelta, std::uint16_t expectedOrientation)
        {
            const fs::path jpegPath = root.Root() / fileName;
            WriteTestImage(jpegPath, TestImageFormat::Jpeg, 24, 48, 6);

            std::wstring errorMessage;
            Expect(hyperbrowse::services::AdjustJpegOrientation(jpegPath.wstring(), quarterTurnsDelta, &errorMessage),
                   std::string("JPEG orientation adjustment failed: ") + Utf8FromWide(errorMessage));
            Expect(ReadJpegOrientation(jpegPath) == expectedOrientation,
                   "JPEG orientation adjustment wrote the wrong EXIF orientation value");
        };

        runCase(L"minus-one.jpg", -1, 1);
        runCase(L"plus-one.jpg", +1, 3);
        runCase(L"minus-three.jpg", -3, 3);
        runCase(L"plus-three.jpg", +3, 1);
    }

    void RunBatchConvertCancellationScenario(HWND hwnd)
    {
        const fs::path fixtureRoot = TestSourceDirectory() / L"fixtures" / L"raw";
        const fs::path nefPath = fixtureRoot / L"RAW_NIKON_D1.NEF";
        const fs::path nrwPath = fixtureRoot / L"RAW_NIKON_P7000.NRW";

        Expect(fs::exists(nefPath), "The NEF fixture is missing from tests/fixtures/raw");
        Expect(fs::exists(nrwPath), "The NRW fixture is missing from tests/fixtures/raw");

        std::vector<hyperbrowse::browser::BrowserItem> items;
        items.push_back(hyperbrowse::browser::BuildBrowserItemFromPath(nefPath));
        items.push_back(hyperbrowse::browser::BuildBrowserItemFromPath(nrwPath));
        items.push_back(hyperbrowse::browser::BuildBrowserItemFromPath(nefPath));

        TempFolder output(L"HyperBrowseBatchCancel");
        hyperbrowse::services::BatchConvertService service;
        service.Start(hwnd, std::move(items), output.Root().wstring(), hyperbrowse::services::BatchConvertFormat::Png);

        PumpMessagesFor(100);
        const ULONGLONG start = GetTickCount64();
        service.Cancel();
        const ULONGLONG elapsed = GetTickCount64() - start;
        Expect(elapsed < 250, "Batch convert cancellation blocked instead of returning promptly");
    }

    void RunFileRenameOperationScenario(HWND hwnd, TestWindowState* state)
    {
        TempFolder root(L"HyperBrowseFileRenameOperation");
        const fs::path sourcePath = root.Root() / L"before.jpg";
        const fs::path renamedPath = root.Root() / L"after.jpg";
        root.WriteFile(L"before.jpg", 16);

        hyperbrowse::services::FileOperationService service;
        state->fileOperationResult = {};
        state->fileOperationResult.expectedRequestId = service.Start(
            hwnd,
            nullptr,
            hyperbrowse::services::FileOperationType::Rename,
            {sourcePath.wstring()},
            {},
            hyperbrowse::services::FileConflictPolicy::PromptShell,
            {L"after.jpg"});

        Expect(PumpMessagesUntil([&]() { return state->fileOperationResult.completed; }, 5000),
               "File rename operation timed out");
        Expect(state->fileOperationResult.update.failedCount == 0,
               "File rename operation reported a failure");
        Expect(!fs::exists(sourcePath), "The original file still exists after rename");
        Expect(fs::exists(renamedPath), "The renamed file was not created");
        Expect(state->fileOperationResult.update.createdPaths.size() == 1
                   && state->fileOperationResult.update.createdPaths.front() == renamedPath.wstring(),
               "File rename operation did not report the created path");
    }

        void RunFileConflictPlanningScenario()
        {
         TempFolder root(L"HyperBrowseFileConflictPlanning");
         root.WriteFile(L"source-a\\alpha.jpg", 16);
         root.WriteFile(L"source-b\\alpha.jpg", 16);
         root.WriteFile(L"source-c\\beta.png", 16);
         root.WriteFile(L"dest\\alpha.jpg", 16);
         root.WriteFile(L"dest\\beta.png", 16);
         root.WriteFile(L"dest\\beta.1.png", 16);

         const fs::path destinationFolder = root.Root() / L"dest";
         const std::vector<std::wstring> sourcePaths{
             (root.Root() / L"source-a" / L"alpha.jpg").wstring(),
             (root.Root() / L"source-b" / L"alpha.jpg").wstring(),
             (root.Root() / L"source-c" / L"beta.png").wstring(),
         };

         const hyperbrowse::services::FileConflictPlan overwritePlan = hyperbrowse::services::PlanDestinationConflicts(
             sourcePaths,
             destinationFolder.wstring(),
             hyperbrowse::services::FileConflictPolicy::OverwriteExisting);
         Expect(overwritePlan.conflictCount == 3,
             "Overwrite planning did not detect all copy/move destination conflicts");

         const hyperbrowse::services::FileConflictPlan renamePlan = hyperbrowse::services::PlanDestinationConflicts(
             sourcePaths,
             destinationFolder.wstring(),
             hyperbrowse::services::FileConflictPolicy::AutoRenameNumericSuffix);
         Expect(renamePlan.conflictCount == 3,
             "Auto-rename planning did not detect all destination conflicts");
         Expect(renamePlan.renamedCount == 3,
             "Auto-rename planning did not record the expected rename count");
         Expect(renamePlan.targetLeafNames.size() == sourcePaths.size(),
             "Auto-rename planning did not preserve target-name alignment");
         Expect(renamePlan.targetLeafNames[0] == L"alpha.1.jpg",
             "Auto-rename planning chose the wrong suffix for the first alpha conflict");
         Expect(renamePlan.targetLeafNames[1] == L"alpha.2.jpg",
             "Auto-rename planning chose the wrong suffix for the second alpha conflict");
         Expect(renamePlan.targetLeafNames[2] == L"beta.2.png",
             "Auto-rename planning did not skip the pre-existing beta.1 target");
        }

    void RunThumbnailSchedulerScenario(HWND hwnd, TestWindowState* state)
    {
        TempFolder root(L"HyperBrowsePrompt5Scheduler");
        const fs::path offscreenPngPath = root.Root() / L"offscreen.png";
        const fs::path visibleJpegPath = root.Root() / L"visible.jpg";
        const fs::path visibleGifPath = root.Root() / L"visible.gif";
        const fs::path offscreenTiffPath = root.Root() / L"offscreen.tif";

        WriteTestImage(offscreenPngPath, TestImageFormat::Png, 96, 48);
        WriteTestImage(visibleJpegPath, TestImageFormat::Jpeg, 24, 48, 6);
        WriteTestImage(visibleGifPath, TestImageFormat::Gif, 36, 18);
        WriteTestImage(offscreenTiffPath, TestImageFormat::Tiff, 18, 54);

        const auto offscreenPngKey = MakeCacheKey(offscreenPngPath, 10);
        const auto visibleJpegKey = MakeCacheKey(visibleJpegPath, 11);
        const auto visibleGifKey = MakeCacheKey(visibleGifPath, 12);
        const auto offscreenTiffKey = MakeCacheKey(offscreenTiffPath, 13);

        hyperbrowse::services::ThumbnailScheduler scheduler(8ULL * 1024ULL * 1024ULL, 1);
        scheduler.BindTargetWindow(hwnd);

        ResetThumbnailResult(state, 7);
        const std::vector<hyperbrowse::services::ThumbnailWorkItem> requests{
            {0, offscreenPngKey, 1},
            {1, visibleJpegKey, 0},
            {2, visibleGifKey, 0},
            {3, offscreenTiffKey, 1},
        };
        scheduler.Schedule(7, 1, requests);

        Expect(PumpMessagesUntil([&]() { return state->thumbnailResult.readyCount >= 4; }, 5000),
               "Thumbnail scheduler decode work timed out");
        Expect(!state->thumbnailResult.readyPaths.empty(), "Thumbnail scheduler did not post any ready messages");
        const std::wstring& firstReadyPath = state->thumbnailResult.readyPaths.front();
        Expect(firstReadyPath == visibleJpegPath.wstring() || firstReadyPath == visibleGifPath.wstring(),
               "Thumbnail scheduler did not prioritize visible work ahead of offscreen work");
        Expect(scheduler.FindCachedThumbnail(offscreenPngKey) != nullptr, "Offscreen PNG thumbnail was not cached");
        Expect(scheduler.FindCachedThumbnail(visibleJpegKey) != nullptr, "Visible JPEG thumbnail was not cached");
        Expect(scheduler.FindCachedThumbnail(visibleGifKey) != nullptr, "Visible GIF thumbnail was not cached");
        Expect(scheduler.FindCachedThumbnail(offscreenTiffKey) != nullptr, "Offscreen TIFF thumbnail was not cached");

        const auto visibleJpegThumbnail = scheduler.FindCachedThumbnail(visibleJpegKey);
        Expect(visibleJpegThumbnail->SourceWidth() == 48 && visibleJpegThumbnail->SourceHeight() == 24,
               "Thumbnail scheduler did not retain the JPEG source dimensions in cache");
        const auto offscreenPngThumbnail = scheduler.FindCachedThumbnail(offscreenPngKey);
        Expect(offscreenPngThumbnail->SourceWidth() == 96 && offscreenPngThumbnail->SourceHeight() == 48,
               "Thumbnail scheduler did not retain the PNG source dimensions in cache");

        ResetThumbnailResult(state, 7);
        scheduler.Schedule(7, 2, requests);
        PumpMessagesFor(300);
        Expect(state->thumbnailResult.readyCount == 0, "Cached thumbnails should not be re-decoded on the next schedule pass");
    }

        void RunThumbnailSchedulerFailureScenario(HWND hwnd, TestWindowState* state)
        {
         TempFolder root(L"HyperBrowsePrompt5SchedulerFailure");
         const fs::path missingRawPath = root.Root() / L"missing.nef";
         const auto missingRawKey = MakeCacheKey(missingRawPath, 1);

         hyperbrowse::services::ThumbnailScheduler scheduler(8ULL * 1024ULL * 1024ULL, 1);
         scheduler.BindTargetWindow(hwnd);

         ResetThumbnailResult(state, 8);
         scheduler.Schedule(8, 1, {{0, missingRawKey, 0, true}});
         Expect(PumpMessagesUntil([&]() { return state->thumbnailResult.failedCount >= 1; }, 5000),
             "Thumbnail scheduler did not surface a failed decode update");
         Expect(state->thumbnailResult.failedPaths.size() == 1
                 && state->thumbnailResult.failedPaths.front() == missingRawPath.wstring(),
             "Thumbnail scheduler reported the wrong failed path");
         Expect(scheduler.FindCachedThumbnail(missingRawKey) == nullptr,
             "Failed thumbnail decodes should not populate the thumbnail cache");
         Expect(scheduler.HasKnownFailure(missingRawKey),
             "Thumbnail scheduler did not retain the failed-thumbnail state");
         Expect(scheduler.KnownFailureKind(missingRawKey) == hyperbrowse::decode::ThumbnailDecodeFailureKind::DecodeFailed,
             "Thumbnail scheduler misclassified a generic decode failure");

         ResetThumbnailResult(state, 8);
         scheduler.Schedule(8, 2, {{0, missingRawKey, 0, true}});
         PumpMessagesFor(300);
         Expect(state->thumbnailResult.readyCount == 0 && state->thumbnailResult.failedCount == 0,
             "Known failed thumbnails should not be requeued until the file path is invalidated");

         scheduler.InvalidateFilePaths({missingRawPath.wstring()});
         Expect(!scheduler.HasKnownFailure(missingRawKey),
             "Invalidating a file path should clear the scheduler's known-failure state");
        }

    void RunThumbnailFailureClassificationScenario()
    {
        Expect(hyperbrowse::decode::ClassifyThumbnailDecodeFailure(L"The RAW helper timed out and was terminated.")
                   == hyperbrowse::decode::ThumbnailDecodeFailureKind::TimedOut,
               "Timeout classification did not detect a helper timeout");
        Expect(hyperbrowse::decode::ClassifyThumbnailDecodeFailure(L"Failed to process the RAW thumbnail fallback.")
                   == hyperbrowse::decode::ThumbnailDecodeFailureKind::DecodeFailed,
               "Decode-failure classification misidentified a generic decode failure");
    }

        void RunThumbnailSchedulerWorkerAllocationScenario()
        {
         hyperbrowse::services::ThumbnailScheduler minimumScheduler(8ULL * 1024ULL * 1024ULL, 1);
         Expect(minimumScheduler.WorkerCount() == 2,
             "Thumbnail scheduler should preserve two lanes when configured below the minimum worker count");
         Expect(minimumScheduler.GeneralWorkerCount() == 1,
             "Thumbnail scheduler should preserve a general worker when configured below the minimum worker count");
         Expect(minimumScheduler.RawWorkerCount() == 1,
             "Thumbnail scheduler should preserve a RAW worker when configured below the minimum worker count");

         hyperbrowse::services::ThumbnailScheduler scaledScheduler(8ULL * 1024ULL * 1024ULL, 8);
         Expect(scaledScheduler.WorkerCount() == 8,
             "Thumbnail scheduler did not preserve the requested worker count for a larger pool");
         Expect(scaledScheduler.GeneralWorkerCount() == 6,
             "Thumbnail scheduler did not allocate the expected number of general workers");
         Expect(scaledScheduler.RawWorkerCount() == 2,
             "Thumbnail scheduler did not scale the RAW worker allocation above one lane");
        }

    void RunImageMetadataServiceScenario()
    {
        const auto itemA = MakeMetadataItem(L"alpha.jpg", L"C:\\Metadata\\alpha.jpg", 11);
        const auto itemB = MakeMetadataItem(L"beta.jpg", L"C:\\Metadata\\beta.jpg", 12);
        const auto itemC = MakeMetadataItem(L"gamma.jpg", L"C:\\Metadata\\gamma.jpg", 13);

        {
            hyperbrowse::services::ImageMetadataService service(
                1,
                2,
                hyperbrowse::util::ResourceProfile::Balanced,
                [](const hyperbrowse::browser::BrowserItem& item, std::wstring*)
                {
                    return MakeMetadata(item.fileName);
                });

            Expect(service.CacheCapacityEntries() == 2, "Metadata cache did not preserve the configured entry capacity");

            service.Schedule(1, {0, itemA, 0});
            service.Schedule(1, {1, itemB, 0});
            Expect(PumpMessagesUntil([&]()
            {
                return service.FindCachedMetadata(itemA) != nullptr && service.FindCachedMetadata(itemB) != nullptr;
            }, 5000), "Metadata service did not populate the initial cache entries");
            Expect(service.CacheEntryCount() == 2, "Metadata cache did not report the expected initial entry count");

            const auto cachedA = service.FindCachedMetadata(itemA);
            Expect(cachedA != nullptr && cachedA->cameraModel == L"alpha.jpg",
                   "Metadata service returned the wrong cached payload for the first item");

            service.Schedule(1, {2, itemC, 0});
            Expect(PumpMessagesUntil([&]()
            {
                return service.FindCachedMetadata(itemC) != nullptr;
            }, 5000), "Metadata service did not populate the replacement cache entry");
            Expect(service.CacheEntryCount() == 2, "Metadata cache grew past the configured entry capacity");
            Expect(service.FindCachedMetadata(itemA) != nullptr,
                   "Metadata cache evicted the most recently used entry instead of keeping it resident");
            Expect(service.FindCachedMetadata(itemB) == nullptr,
                   "Metadata cache did not evict the least recently used entry after reaching capacity");
        }

        {
            struct BlockingState
            {
                std::mutex mutex;
                std::condition_variable started;
                std::condition_variable released;
                bool extractionStarted{};
                bool allowCompletion{};
            } cancellationState;

            const auto cancelledItem = MakeMetadataItem(L"cancelled.jpg", L"C:\\Metadata\\cancelled.jpg", 21);
            const auto currentItem = MakeMetadataItem(L"current.jpg", L"C:\\Metadata\\current.jpg", 22);

            hyperbrowse::services::ImageMetadataService service(
                1,
                4,
                hyperbrowse::util::ResourceProfile::Balanced,
                [&](const hyperbrowse::browser::BrowserItem& item, std::wstring*)
                {
                    if (item.modifiedTimestampUtc == cancelledItem.modifiedTimestampUtc)
                    {
                        std::unique_lock lock(cancellationState.mutex);
                        cancellationState.extractionStarted = true;
                        cancellationState.started.notify_all();
                        cancellationState.released.wait(lock, [&]()
                        {
                            return cancellationState.allowCompletion;
                        });
                    }

                    return MakeMetadata(item.fileName);
                });

            service.Schedule(41, {0, cancelledItem, 0});
            Expect(PumpMessagesUntil([&]()
            {
                std::scoped_lock lock(cancellationState.mutex);
                return cancellationState.extractionStarted;
            }, 1000), "Metadata cancellation scenario never started the blocked extraction");

            service.CancelOutstanding();
            service.Schedule(42, {1, currentItem, 0});

            {
                std::scoped_lock lock(cancellationState.mutex);
                cancellationState.allowCompletion = true;
            }
            cancellationState.released.notify_all();

            Expect(PumpMessagesUntil([&]()
            {
                return service.FindCachedMetadata(currentItem) != nullptr;
            }, 5000), "Metadata cancellation scenario did not cache the latest session item");
            PumpMessagesFor(100);
            Expect(service.FindCachedMetadata(cancelledItem) == nullptr,
                   "Metadata cancellation allowed an in-flight stale result to repopulate the cache");
        }

        {
            struct BlockingState
            {
                std::mutex mutex;
                std::condition_variable started;
                std::condition_variable released;
                bool extractionStarted{};
                bool allowCompletion{};
            } invalidationState;

            const auto staleItem = MakeMetadataItem(L"watched.jpg", L"C:\\Metadata\\watched.jpg", 31);
            const auto refreshedItem = MakeMetadataItem(L"watched.jpg", L"C:\\Metadata\\watched.jpg", 32);

            hyperbrowse::services::ImageMetadataService service(
                1,
                4,
                hyperbrowse::util::ResourceProfile::Balanced,
                [&](const hyperbrowse::browser::BrowserItem& item, std::wstring*)
                {
                    if (item.modifiedTimestampUtc == staleItem.modifiedTimestampUtc)
                    {
                        std::unique_lock lock(invalidationState.mutex);
                        invalidationState.extractionStarted = true;
                        invalidationState.started.notify_all();
                        invalidationState.released.wait(lock, [&]()
                        {
                            return invalidationState.allowCompletion;
                        });
                    }

                    return MakeMetadata(item.fileName + L"-" + std::to_wstring(item.modifiedTimestampUtc));
                });

            service.Schedule(55, {0, staleItem, 0});
            Expect(PumpMessagesUntil([&]()
            {
                std::scoped_lock lock(invalidationState.mutex);
                return invalidationState.extractionStarted;
            }, 1000), "Metadata invalidation scenario never started the blocked extraction");

            service.InvalidateFilePaths({staleItem.filePath});
            service.Schedule(55, {1, refreshedItem, 0});

            {
                std::scoped_lock lock(invalidationState.mutex);
                invalidationState.allowCompletion = true;
            }
            invalidationState.released.notify_all();

            Expect(PumpMessagesUntil([&]()
            {
                return service.FindCachedMetadata(refreshedItem) != nullptr;
            }, 5000), "Metadata invalidation scenario did not cache the refreshed file metadata");
            PumpMessagesFor(100);

            const auto refreshedMetadata = service.FindCachedMetadata(refreshedItem);
            Expect(refreshedMetadata != nullptr && refreshedMetadata->cameraModel == L"watched.jpg-32",
                   "Metadata invalidation scenario cached the wrong refreshed payload");
            Expect(service.FindCachedMetadata(staleItem) == nullptr,
                   "Metadata invalidation allowed an in-flight stale result to repopulate the cache");
        }
    }

    void RunSwarmUiMetadataExtractionScenario()
    {
        TempFolder root(L"HyperBrowsePrompt9SwarmMetadata");
        const fs::path pngPath = root.Root() / L"swarm-ui.png";
        const std::string parameters =
            "{ sui_image_params : { prompt : cinematic portrait, volumetric lighting, sharp focus, negativeprompt : low quality, deformed hands, text, model : swarm-model-xl, seed : 12345, steps : 30, cfgscale : 7, swarm_version : 0.9.1.1, date : 2026-05-16, generation_time : 1.23 seconds } }";
        WriteTestImage(pngPath,
                       TestImageFormat::Png,
                       96,
                       48,
                       1,
                       L"parameters",
                       parameters);

        hyperbrowse::browser::BrowserItem item;
        item.fileName = pngPath.filename().wstring();
        item.filePath = pngPath.wstring();
        item.fileType = L"PNG";
        item.modifiedTimestampUtc = 77;

        std::wstring errorMessage;
        const auto metadata = hyperbrowse::services::ExtractImageMetadata(item, &errorMessage);
        Expect(metadata != nullptr, "SwarmUI PNG metadata extraction returned a null result");

        const auto findProperty = [&](std::wstring_view canonicalName)
            -> const hyperbrowse::services::MetadataPropertyEntry*
        {
            const auto match = std::find_if(metadata->properties.begin(),
                                            metadata->properties.end(),
                                            [&](const hyperbrowse::services::MetadataPropertyEntry& property)
                                            {
                                                return property.canonicalName == canonicalName;
                                            });
            return match != metadata->properties.end() ? &(*match) : nullptr;
        };

        const auto* prompt = findProperty(L"SwarmUI.prompt");
        Expect(prompt != nullptr
                   && prompt->value == L"cinematic portrait, volumetric lighting, sharp focus",
               "SwarmUI prompt metadata was not extracted from the PNG parameters chunk");

        const auto* negativePrompt = findProperty(L"SwarmUI.negativeprompt");
        Expect(negativePrompt != nullptr
                   && negativePrompt->value == L"low quality, deformed hands, text",
               "SwarmUI negative prompt metadata was not extracted from the PNG parameters chunk");

        const auto* swarmVersion = findProperty(L"SwarmUI.swarm_version");
        Expect(swarmVersion != nullptr && swarmVersion->value == L"0.9.1.1",
               "SwarmUI version metadata was not extracted from the PNG parameters chunk");

        const std::wstring expanded = hyperbrowse::services::FormatImageInfoExpanded(*metadata);
        Expect(expanded.find(L"Prompt: cinematic portrait, volumetric lighting, sharp focus") != std::wstring::npos,
               "Expanded image information did not surface the extracted SwarmUI prompt");
        Expect(expanded.find(L"Negative prompt: low quality, deformed hands, text") != std::wstring::npos,
               "Expanded image information did not surface the extracted SwarmUI negative prompt");
    }

    void RunRawFormatAllowlistScenario()
    {
        const std::vector<std::wstring> supportedRawFormats{
            L"ARW",
            L"CR2",
            L"CR3",
            L"DNG",
            L"NEF",
            L"NRW",
            L"RAF",
            L"RW2",
        };

        for (const std::wstring& rawFormat : supportedRawFormats)
        {
            Expect(hyperbrowse::decode::IsRawFileType(rawFormat),
                   std::string("RAW allowlist omitted format: ") + Utf8FromWide(rawFormat));

            hyperbrowse::browser::BrowserItem item;
            item.fileName = L"sample." + rawFormat;
            item.filePath = L"C:\\Raw\\sample." + rawFormat;
            item.fileType = rawFormat;

            Expect(hyperbrowse::decode::CanDecodeThumbnail(item),
                   std::string("RAW thumbnail routing omitted format: ") + Utf8FromWide(rawFormat));
            Expect(hyperbrowse::decode::CanDecodeFullImage(item),
                   std::string("RAW full-image routing omitted format: ") + Utf8FromWide(rawFormat));
        }

        Expect(!hyperbrowse::decode::IsRawFileType(L"ORF"),
               "The RAW allowlist unexpectedly includes ORF before it was requested");
    }

        void RunRawHelperProtocolScenario()
        {
         TempFolder root(L"HyperBrowseRawHelperProtocol");
         const fs::path payloadPath = root.Root() / L"payload.bin";

         hyperbrowse::decode::RawHelperDecodedPixels payload;
         payload.bitmapWidth = 32;
         payload.bitmapHeight = 16;
         payload.sourceWidth = 64;
         payload.sourceHeight = 32;
         payload.bgraPixels.assign(32U * 16U * 4U, 0x7f);

         std::wstring errorMessage;
         Expect(hyperbrowse::decode::WriteRawHelperPayload(payloadPath.wstring(), payload, &errorMessage),
             "RAW helper protocol failed to write a valid payload");

         hyperbrowse::decode::RawHelperDecodedPixels loaded;
         Expect(hyperbrowse::decode::ReadRawHelperPayload(payloadPath.wstring(), &loaded, &errorMessage),
             "RAW helper protocol failed to read a valid payload");
         Expect(loaded.bitmapWidth == payload.bitmapWidth
              && loaded.bitmapHeight == payload.bitmapHeight
              && loaded.sourceWidth == payload.sourceWidth
              && loaded.sourceHeight == payload.sourceHeight
              && loaded.bgraPixels == payload.bgraPixels,
             "RAW helper protocol did not preserve a valid payload");

    #pragma pack(push, 1)
         struct TestRawHelperFileHeader
         {
             std::uint32_t magic{};
             std::uint32_t version{};
             std::uint32_t bitmapWidth{};
             std::uint32_t bitmapHeight{};
             std::uint32_t sourceWidth{};
             std::uint32_t sourceHeight{};
             std::uint64_t pixelBytes{};
         };
    #pragma pack(pop)

         TestRawHelperFileHeader validHeader{
             0x52425748,
             1,
             32,
             16,
             64,
             32,
             32ULL * 16ULL * 4ULL,
         };
         const auto writeHeaderAndPayload = [&](const TestRawHelperFileHeader& header, std::size_t payloadBytes)
         {
             std::ofstream stream(payloadPath, std::ios::binary | std::ios::trunc);
             stream.write(reinterpret_cast<const char*>(&header), sizeof(header));
             std::vector<unsigned char> bytes(payloadBytes, 0x7f);
             stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
         };

         TestRawHelperFileHeader oversizedHeader = validHeader;
         oversizedHeader.bitmapWidth = UINT32_MAX;
         writeHeaderAndPayload(oversizedHeader, 0);
         loaded = {};
         Expect(!hyperbrowse::decode::ReadRawHelperPayload(payloadPath.wstring(), &loaded, &errorMessage),
             "RAW helper protocol accepted oversized dimensions");

         writeHeaderAndPayload(validHeader, static_cast<std::size_t>(validHeader.pixelBytes - 1));
         loaded = {};
         Expect(!hyperbrowse::decode::ReadRawHelperPayload(payloadPath.wstring(), &loaded, &errorMessage),
             "RAW helper protocol accepted a truncated payload");

         writeHeaderAndPayload(validHeader, static_cast<std::size_t>(validHeader.pixelBytes + 1));
         loaded = {};
         Expect(!hyperbrowse::decode::ReadRawHelperPayload(payloadPath.wstring(), &loaded, &errorMessage),
             "RAW helper protocol accepted trailing payload data");
        }

    void RunRawDecoderScenario()
    {
        const fs::path fixtureRoot = TestSourceDirectory() / L"fixtures" / L"raw";
        const fs::path nefPath = fixtureRoot / L"RAW_NIKON_D1.NEF";
        const fs::path nrwPath = fixtureRoot / L"RAW_NIKON_P7000.NRW";

        Expect(fs::exists(nefPath), "The NEF fixture is missing from tests/fixtures/raw");
        Expect(fs::exists(nrwPath), "The NRW fixture is missing from tests/fixtures/raw");

        const hyperbrowse::browser::BrowserItem nefItem = hyperbrowse::browser::BuildBrowserItemFromPath(nefPath);
        const hyperbrowse::browser::BrowserItem nrwItem = hyperbrowse::browser::BuildBrowserItemFromPath(nrwPath);

        Expect(hyperbrowse::decode::CanDecodeThumbnail(nefItem), "NEF fixture should be thumbnail-decodable");
        Expect(hyperbrowse::decode::CanDecodeFullImage(nefItem), "NEF fixture should be full-image decodable");
        Expect(hyperbrowse::decode::CanDecodeThumbnail(nrwItem), "NRW fixture should be thumbnail-decodable");
        Expect(hyperbrowse::decode::CanDecodeFullImage(nrwItem), "NRW fixture should be full-image decodable");

        std::wstring errorMessage;
        const auto nefThumbnail = hyperbrowse::decode::DecodeThumbnail(MakeCacheKey(nefPath, nefItem.modifiedTimestampUtc), &errorMessage);
        Expect(nefThumbnail != nullptr, std::string("LibRaw failed to decode the NEF thumbnail fixture: ") + Utf8FromWide(errorMessage));
        Expect(nefThumbnail->SourceWidth() > 0 && nefThumbnail->SourceHeight() > 0,
               "LibRaw did not surface NEF thumbnail source dimensions");

        errorMessage.clear();
        const auto nrwThumbnail = hyperbrowse::decode::DecodeThumbnail(MakeCacheKey(nrwPath, nrwItem.modifiedTimestampUtc), &errorMessage);
        Expect(nrwThumbnail != nullptr, std::string("LibRaw failed to decode the NRW thumbnail fixture: ") + Utf8FromWide(errorMessage));
        Expect(nrwThumbnail->SourceWidth() > 0 && nrwThumbnail->SourceHeight() > 0,
               "LibRaw did not surface NRW thumbnail source dimensions");

        errorMessage.clear();
        const auto nefFullImage = hyperbrowse::decode::DecodeFullImage(nefItem, &errorMessage);
        Expect(nefFullImage != nullptr, std::string("LibRaw failed to decode the NEF full-image fixture: ") + Utf8FromWide(errorMessage));
        Expect(nefFullImage->SourceWidth() > 0 && nefFullImage->SourceHeight() > 0,
               "LibRaw did not surface NEF full-image source dimensions");

        errorMessage.clear();
        const auto nrwFullImage = hyperbrowse::decode::DecodeFullImage(nrwItem, &errorMessage);
        Expect(nrwFullImage != nullptr, std::string("LibRaw failed to decode the NRW full-image fixture: ") + Utf8FromWide(errorMessage));
        Expect(nrwFullImage->SourceWidth() > 0 && nrwFullImage->SourceHeight() > 0,
               "LibRaw did not surface NRW full-image source dimensions");
        }

    void RunBrowserPaneScenario(HINSTANCE instance)
    {
        HWND hostWindow = CreateUiHostWindow(instance);
        Expect(hostWindow != nullptr, "Failed to create the hidden UI host window");

        hyperbrowse::browser::BrowserPane browserPane(instance);
        Expect(browserPane.Create(hostWindow), "Failed to create the BrowserPane test control");

         WNDCLASSEXW browserPaneClass{};
         browserPaneClass.cbSize = sizeof(browserPaneClass);
         Expect(GetClassInfoExW(instance, L"HyperBrowseBrowserPane", &browserPaneClass) != FALSE,
             "Failed to query the BrowserPane window class");
         Expect((browserPaneClass.style & CS_DBLCLKS) != 0,
             "BrowserPane must register with CS_DBLCLKS so thumbnail double-clicks open the viewer");

        MoveWindow(browserPane.Hwnd(), 0, 0, 860, 620, TRUE);

        hyperbrowse::browser::BrowserModel model;
        std::vector<hyperbrowse::browser::BrowserItem> items;
        items.push_back(hyperbrowse::browser::BrowserItem{L"alpha.jpg", L"C:\\Alpha\\alpha.jpg", L"JPG", L"2026-04-11 10:00", 100, 40, 320, 240});
        items.push_back(hyperbrowse::browser::BrowserItem{L"테스트-漢字.png", L"C:\\Alpha\\테스트-漢字.png", L"PNG", L"2026-04-11 10:01", 200, 10, 640, 480});
        items.push_back(hyperbrowse::browser::BrowserItem{L"gamma.gif", L"C:\\Alpha\\gamma.gif", L"GIF", L"2026-04-11 10:02", 300, 70, 160, 120});
        items.push_back(hyperbrowse::browser::BrowserItem{L"delta.nef", L"C:\\Alpha\\delta.nef", L"NEF", L"2026-04-11 10:03", 400, 55, 1024, 768});
        model.Reset(L"C:\\Alpha", false);
        model.AppendItems(std::move(items), 4, 175);
        model.Complete();

        browserPane.SetModel(&model);
        browserPane.RefreshFromModel();

        SendMessageW(browserPane.Hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(50, 50));
        SendMessageW(browserPane.Hwnd(), WM_LBUTTONUP, 0, MAKELPARAM(50, 50));
        Expect(browserPane.SelectedCount() == 1, "Single-click selection in thumbnail mode failed");

        SetFocus(hostWindow);
        Expect(browserPane.HandleNavigationKey(WM_KEYDOWN, VK_RIGHT, 0),
               "Thumbnail navigation did not handle an arrow key from the main window focus path");
        Expect(GetFocus() == browserPane.Hwnd(),
               "Thumbnail navigation did not return keyboard focus to the browser pane");
        Expect(browserPane.FocusedFilePathSnapshot() == L"C:\\Alpha\\delta.nef",
               "Arrow navigation from main window focus did not advance the focused thumbnail");
        SendMessageW(browserPane.Hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(50, 50));
        SendMessageW(browserPane.Hwnd(), WM_LBUTTONUP, 0, MAKELPARAM(50, 50));

        SendMessageW(browserPane.Hwnd(), WM_LBUTTONDOWN, MK_LBUTTON | MK_SHIFT, MAKELPARAM(500, 50));
        SendMessageW(browserPane.Hwnd(), WM_LBUTTONUP, 0, MAKELPARAM(500, 50));
        Expect(browserPane.SelectedCount() == 3, "Shift-range selection in thumbnail mode failed");

        SendMessageW(browserPane.Hwnd(), WM_LBUTTONDOWN, MK_LBUTTON | MK_CONTROL, MAKELPARAM(300, 50));
        SendMessageW(browserPane.Hwnd(), WM_LBUTTONUP, 0, MAKELPARAM(300, 50));
        Expect(browserPane.SelectedCount() == 2, "Ctrl-toggle selection in thumbnail mode failed");

        browserPane.ClearSelection();
        SendMessageW(browserPane.Hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(5, 5));
        SendMessageW(browserPane.Hwnd(), WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(390, 210));
        SendMessageW(browserPane.Hwnd(), WM_LBUTTONUP, 0, MAKELPARAM(390, 210));
        Expect(browserPane.SelectedCount() == 2, "Rubber-band selection in thumbnail mode failed");

         Expect(browserPane.GetThumbnailSizePreset() == hyperbrowse::browser::ThumbnailSizePreset::Pixels192,
             "BrowserPane did not default to the expected thumbnail size preset");
         Expect(browserPane.IsCompactThumbnailLayoutEnabled(),
             "BrowserPane should default to the compact thumbnail layout");
         Expect(browserPane.AreThumbnailDetailsVisible(),
             "BrowserPane should default to showing thumbnail details");

         browserPane.SetThumbnailSizePreset(hyperbrowse::browser::ThumbnailSizePreset::Pixels96);
         browserPane.SetCompactThumbnailLayout(true);
         browserPane.SetThumbnailDetailsVisible(false);
         browserPane.RefreshFromModel();
         browserPane.ClearSelection();
         SendMessageW(browserPane.Hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(20, 20));
         SendMessageW(browserPane.Hwnd(), WM_LBUTTONUP, 0, MAKELPARAM(20, 20));
         Expect(browserPane.GetThumbnailSizePreset() == hyperbrowse::browser::ThumbnailSizePreset::Pixels96,
             "BrowserPane did not store the compact thumbnail size preset");
         Expect(browserPane.IsCompactThumbnailLayoutEnabled(),
             "BrowserPane did not enable compact thumbnail layout");
         Expect(!browserPane.AreThumbnailDetailsVisible(),
             "BrowserPane did not disable thumbnail details");
         Expect(browserPane.SelectedCount() == 1,
             "Thumbnail-only compact mode did not preserve thumbnail hit-testing");

         browserPane.SetThumbnailSizePreset(hyperbrowse::browser::ThumbnailSizePreset::Pixels320);
         browserPane.SetCompactThumbnailLayout(false);
         browserPane.SetThumbnailDetailsVisible(true);
         browserPane.RefreshFromModel();
         browserPane.ClearSelection();
         SendMessageW(browserPane.Hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(40, 40));
         SendMessageW(browserPane.Hwnd(), WM_LBUTTONUP, 0, MAKELPARAM(40, 40));
         Expect(browserPane.GetThumbnailSizePreset() == hyperbrowse::browser::ThumbnailSizePreset::Pixels320,
             "BrowserPane did not store the large thumbnail size preset");
         Expect(!browserPane.IsCompactThumbnailLayoutEnabled(),
             "BrowserPane did not disable the compact thumbnail layout");
         Expect(browserPane.AreThumbnailDetailsVisible(),
             "BrowserPane did not restore thumbnail details visibility");
         Expect(browserPane.SelectedCount() == 1,
             "Large thumbnail mode did not preserve thumbnail hit-testing");

        browserPane.SetViewMode(hyperbrowse::browser::BrowserViewMode::Details);
        browserPane.SetSortMode(hyperbrowse::browser::BrowserSortMode::FileSize);

        HWND listView = FindWindowExW(browserPane.Hwnd(), nullptr, WC_LISTVIEWW, nullptr);
        Expect(listView != nullptr, "Details-mode list view was not created");
        Expect(ListView_GetItemCount(listView) == 4, "Virtual details view item count is incorrect");
         Expect(SendMessageW(listView, LVM_GETUNICODEFORMAT, 0, 0) != FALSE,
             "Details-mode list view is not running in Unicode mode");

        wchar_t buffer[256]{};
        ListView_GetItemText(listView, 0, 0, buffer, static_cast<int>(std::size(buffer)));
         Expect(std::wstring(buffer) == L"테스트-漢字.png", "Details view did not preserve the Unicode filename text for the smallest item");

        Expect(model.UpdateDecodedDimensions(0, 20, 20), "Browser model did not accept a decoded-dimensions update");
        Expect(model.UpdateDecodedDimensions(1, 10, 10), "Browser model did not accept the second decoded-dimensions update");
        Expect(model.UpdateDecodedDimensions(2, 40, 40), "Browser model did not accept the third decoded-dimensions update");
        browserPane.SetSortMode(hyperbrowse::browser::BrowserSortMode::Dimensions);
        browserPane.RefreshFromModel();

        ListView_GetItemText(listView, 0, 0, buffer, static_cast<int>(std::size(buffer)));
        Expect(std::wstring(buffer) == L"테스트-漢字.png", "Dimension sort did not preserve the Unicode filename in details mode");
        ListView_GetItemText(listView, 0, 4, buffer, static_cast<int>(std::size(buffer)));
        Expect(std::wstring(buffer) == L"10x10", "Dimensions column did not surface the decoded dimensions");

        std::vector<hyperbrowse::browser::BrowserItem> pairedItems;
        pairedItems.push_back(hyperbrowse::browser::BrowserItem{L"pair.nef", L"C:\\Alpha\\pair.nef", L"NEF", L"2026-04-11 10:04", 500, 90, 4032, 3024});
        pairedItems.push_back(hyperbrowse::browser::BrowserItem{L"pair.jpg", L"C:\\Alpha\\pair.jpg", L"JPG", L"2026-04-11 10:05", 600, 20, 4032, 3024});
        pairedItems.push_back(hyperbrowse::browser::BrowserItem{L"solo.png", L"C:\\Alpha\\solo.png", L"PNG", L"2026-04-11 10:06", 700, 15, 640, 480});
        model.Reset(L"C:\\Alpha", false);
        model.AppendItems(std::move(pairedItems), 3, 205);
        model.Complete();

        browserPane.SetSortMode(hyperbrowse::browser::BrowserSortMode::FileName);
        browserPane.SetSortAscending(true);
        browserPane.SetRawJpegDisplayPreference(hyperbrowse::browser::RawJpegDisplayPreference::Jpeg);
        browserPane.SetRawJpegStackingEnabled(true);
        browserPane.RefreshFromModel();

        Expect(ListView_GetItemCount(listView) == 2, "Paired RAW+JPEG stacking did not collapse to a single details entry");
        browserPane.RestoreSelectionByFilePaths({L"C:\\Alpha\\pair.nef"}, L"C:\\Alpha\\pair.nef");
        Expect(browserPane.SelectedCount() == 1, "Stacked RAW+JPEG selection did not remap to the visible representative");
        const auto stackedSelectionPaths = browserPane.SelectedFilePathsSnapshot();
        Expect(stackedSelectionPaths.size() == 1
            && stackedSelectionPaths.front() == L"C:\\Alpha\\pair.jpg",
            "Stacked RAW+JPEG selection did not expose the JPEG browser representative");
        ListView_GetItemText(listView, 0, 1, buffer, static_cast<int>(std::size(buffer)));
        Expect(std::wstring(buffer) == L"JPG+NEF", "Stacked RAW+JPEG type label did not reveal the hidden companion");

        browserPane.SetRawJpegStackingEnabled(false);
        browserPane.RefreshFromModel();
        Expect(ListView_GetItemCount(listView) == 3, "Disabling RAW+JPEG stacking did not restore both browser items");

        DestroyWindow(hostWindow);
    }

    void RunBrowserModelBulkRemovalScenario()
    {
        hyperbrowse::browser::BrowserModel model;
        std::vector<hyperbrowse::browser::BrowserItem> items;
        items.push_back(hyperbrowse::browser::BrowserItem{L"alpha.jpg", L"C:\\Alpha\\alpha.jpg", L"JPG", L"", 1, 10});
        items.push_back(hyperbrowse::browser::BrowserItem{L"beta.jpg", L"C:\\Alpha\\beta.jpg", L"JPG", L"", 2, 20});
        items.push_back(hyperbrowse::browser::BrowserItem{L"gamma.jpg", L"C:\\Alpha\\gamma.jpg", L"JPG", L"", 3, 30});
        model.Reset(L"C:\\Alpha", false);
        model.AppendItems(std::move(items), 3, 60);
        model.Complete();

        Expect(model.RemoveItemsByPath({L"c:/alpha/BETA.jpg", L"C:\\Alpha\\missing.jpg"}),
               "Bulk model removal did not match normalized file paths");
        Expect(model.Items().size() == 2, "Bulk model removal removed the wrong number of items");
        Expect(model.TotalCount() == 2, "Bulk model removal did not update the item count");
        Expect(model.TotalBytes() == 40, "Bulk model removal did not update total bytes");
        Expect(model.FindItemIndexByPath(L"C:\\Alpha\\beta.jpg") < 0,
               "Bulk model removal left the requested item in the model");
    }

    void RunQuickSendModelScenario()
    {
        using hyperbrowse::ui::QuickSendAssignmentResult;
        using hyperbrowse::ui::QuickSendModel;

        QuickSendModel model;
        model.SetFavoriteDestinations({
            L"C:\\Favorites\\One\\",
            L"c:/favorites/one",
            L"D:\\Favorites\\Two",
            L"E:\\Favorites\\Three",
            L"F:\\Favorites\\Four",
            L"G:\\Favorites\\Five",
        });

        Expect(model.FavoriteDestinations().size() == 5,
            "Quick Send did not deduplicate favorite destinations while preserving more than four entries");
        Expect(model.FavoriteDestinations().front() == L"C:\\Favorites\\One\\",
            "Quick Send did not preserve the first favorite path for display");
        Expect(std::ranges::none_of(model.FavoriteDestinations(), [](const std::wstring& path)
            {
                return hyperbrowse::util::NormalizedPathEquals(path, L"H:\\RecentOnly");
            }),
            "Recent-only destinations leaked into the favorite Quick Send list");
        Expect(QuickSendModel::ShortcutIndexFromText(L"0") == 0,
            "Quick Send did not map the first digit shortcut");
        Expect(QuickSendModel::ShortcutIndexFromText(L"9") == 9,
            "Quick Send did not map the last digit shortcut");
        Expect(QuickSendModel::ShortcutIndexFromText(L"A") == 10,
            "Quick Send did not map the first letter shortcut");
        Expect(QuickSendModel::ShortcutIndexFromText(L"z") == 35,
            "Quick Send did not normalize the last lowercase letter shortcut");
        Expect(QuickSendModel::ShortcutIndexFromText(L"AB") == std::nullopt,
            "Quick Send accepted a multi-character shortcut key");
        Expect(QuickSendModel::ShortcutIndexFromText(L"!") == std::nullopt,
            "Quick Send accepted a non-alphanumeric shortcut key");
        Expect(QuickSendModel::ShortcutCharacter(0) == L'0'
                && QuickSendModel::ShortcutCharacter(9) == L'9'
                && QuickSendModel::ShortcutCharacter(10) == L'A'
                && QuickSendModel::ShortcutCharacter(35) == L'Z'
                && QuickSendModel::ShortcutCharacter(36) == L'\0',
            "Quick Send did not map shortcut indexes to display keys");

        Expect(model.SetShortcutForDestination(L"c:/FAVORITES/one/", L"2")
                == QuickSendAssignmentResult::Accepted,
            "Quick Send rejected a valid normalized favorite assignment");
        Expect(model.ShortcutForDestination(L"C:\\Favorites\\One") == 2,
            "Quick Send did not resolve a destination assignment by normalized path");
        Expect(model.ShortcutAssignmentsByKey()[2] == L"c:\\favorites\\one",
            "Quick Send did not persist assignments in normalized form");

        Expect(model.SetShortcutForDestination(L"D:\\Favorites\\Two", L"2")
                == QuickSendAssignmentResult::DuplicateShortcut,
            "Quick Send allowed two favorite destinations to claim one digit");
        Expect(model.ShortcutForDestination(L"C:\\Favorites\\One") == 2,
            "Quick Send duplicate rejection disturbed the existing assignment");
        Expect(model.AssignNextAvailableShortcut(L"D:\\Favorites\\Two") == 0,
            "Quick Send did not assign the lowest available shortcut");
        Expect(model.AssignNextAvailableShortcut(L"D:\\Favorites\\Two") == 0,
            "Quick Send changed an existing automatic shortcut assignment");
        Expect(model.SetShortcutForDestination(L"D:\\Favorites\\Two", L"12")
                == QuickSendAssignmentResult::InvalidShortcut,
            "Quick Send accepted a multi-character shortcut");
        Expect(model.SetShortcutForDestination(L"D:\\Favorites\\Two", L"x")
                == QuickSendAssignmentResult::Accepted
                && model.ShortcutForDestination(L"D:\\Favorites\\Two") == 33,
            "Quick Send did not accept and normalize a lowercase letter shortcut");
        Expect(model.SetShortcutForDestination(L"D:\\Favorites\\Two", L"!")
                == QuickSendAssignmentResult::InvalidShortcut,
            "Quick Send accepted a non-alphanumeric shortcut");
        Expect(model.SetShortcutForDestination(L"D:\\Favorites\\Two", {})
                == QuickSendAssignmentResult::Accepted,
            "Quick Send did not accept a blank shortcut to clear an assignment");

        QuickSendModel restoredModel;
        restoredModel.SetFavoriteDestinations(model.FavoriteDestinations());
        QuickSendModel::ShortcutAssignments persisted{};
        persisted[1] = L"C:\\FAVORITES\\ONE";
        persisted[2] = L"C:\\Removed\\Destination";
        persisted[3] = L"c:/favorites/one/";
        persisted[10] = L"E:\\Favorites\\Three";
        restoredModel.SetShortcutAssignments(persisted);
        Expect(restoredModel.ShortcutForDestination(L"C:\\Favorites\\One") == 1,
            "Quick Send did not restore a persisted path assignment");
        Expect(restoredModel.ShortcutForDestination(L"E:\\Favorites\\Three") == 10,
            "Quick Send did not restore a persisted letter assignment");
        Expect(restoredModel.DestinationForShortcut(2) == std::nullopt,
            "Quick Send did not prune a persisted destination that is no longer favorited");
        Expect(restoredModel.DestinationForShortcut(3) == std::nullopt,
            "Quick Send did not reject duplicate persisted assignments");

        restoredModel.SetFavoriteDestinations({
            L"G:\\Favorites\\Five",
            L"C:\\Favorites\\One",
            L"E:\\Favorites\\Three",
            L"D:\\Favorites\\Two",
            L"F:\\Favorites\\Four",
        });
        Expect(restoredModel.ShortcutForDestination(L"C:\\Favorites\\One") == 1,
            "Quick Send assignment changed when favorite ordering changed");

        QuickSendModel sortedModel;
        sortedModel.SetFavoriteDestinations({
            L"C:\\Favorites\\Unassigned",
            L"C:\\Favorites\\Letter",
            L"C:\\Favorites\\Digit",
        });
        Expect(sortedModel.SetShortcutForDestination(L"C:\\Favorites\\Letter", L"A")
                == QuickSendAssignmentResult::Accepted
                && sortedModel.SetShortcutForDestination(L"C:\\Favorites\\Digit", L"2")
                    == QuickSendAssignmentResult::Accepted,
            "Quick Send rejected valid shortcuts for sort coverage");
        sortedModel.SortFavoriteDestinationsByShortcut();
        Expect(sortedModel.FavoriteDestinations().size() == 3
                && sortedModel.FavoriteDestinations()[0] == L"C:\\Favorites\\Digit"
                && sortedModel.FavoriteDestinations()[1] == L"C:\\Favorites\\Letter"
                && sortedModel.FavoriteDestinations()[2] == L"C:\\Favorites\\Unassigned",
            "Quick Send did not sort favorites by digit-then-letter shortcuts with unassigned entries last");

        QuickSendModel fullModel;
        std::vector<std::wstring> fullFavorites;
        for (std::size_t index = 0; index < hyperbrowse::ui::kQuickSendShortcutCount; ++index)
        {
            fullFavorites.push_back(L"C:\\Favorites\\Shortcut" + std::to_wstring(index));
        }
        fullModel.SetFavoriteDestinations(fullFavorites);
        for (std::size_t index = 0; index < hyperbrowse::ui::kQuickSendShortcutCount; ++index)
        {
            Expect(fullModel.AssignNextAvailableShortcut(fullFavorites[index]) == static_cast<int>(index),
                "Quick Send did not consume shortcuts in digit-then-letter order");
        }
        Expect(fullModel.ShortcutForDestination(L"C:\\Favorites\\Shortcut10") == 10,
            "Quick Send did not assign A after the digit shortcuts");
        fullFavorites.push_back(L"C:\\Favorites\\New");
        fullModel.SetFavoriteDestinations(fullFavorites);
        Expect(fullModel.AssignNextAvailableShortcut(L"C:\\Favorites\\New") == std::nullopt,
            "Quick Send assigned a shortcut when all alphanumeric keys were already occupied");
    }

    void RunViewerWindowFitModeChecks(hyperbrowse::viewer::ViewerWindow& viewer,
                                      int currentImageWidth,
                                      int currentImageHeight)
    {
        Expect(viewer.CurrentIndex() == 1, "Viewer fit-mode scenario did not start on the middle image");
        while (viewer.RotationQuarterTurns() != 0)
        {
            SendMessageW(viewer.Hwnd(), WM_KEYDOWN, 'L', 0);
            PumpMessagesFor(100);
        }

        for (int index = 0; index < 5; ++index)
        {
            SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_OEM_PLUS, 0);
        }
        PumpMessagesFor(300);
        SendMessageW(viewer.Hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(120, 120));
        SendMessageW(viewer.Hwnd(), WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(180, 150));
        SendMessageW(viewer.Hwnd(), WM_LBUTTONUP, 0, MAKELPARAM(180, 150));
        PumpMessagesFor(100);
        Expect(viewer.PanOffset().x != 0 || viewer.PanOffset().y != 0,
            "Viewer fullscreen fit test did not establish a non-zero pan offset");

        SendMessageW(viewer.Hwnd(), WM_LBUTTONDBLCLK, 0, MAKELPARAM(100, 100));
        PumpMessagesFor(100);
        Expect(!viewer.IsFullScreen()
                && viewer.PanOffset().x == 0
                && viewer.PanOffset().y == 0,
            "Viewer exiting fullscreen did not fit the image to the resized window");

        for (int index = 0; index < 5; ++index)
        {
            SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_OEM_PLUS, 0);
        }
        PumpMessagesFor(300);
        SendMessageW(viewer.Hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(120, 120));
        SendMessageW(viewer.Hwnd(), WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(180, 150));
        SendMessageW(viewer.Hwnd(), WM_LBUTTONUP, 0, MAKELPARAM(180, 150));
        PumpMessagesFor(100);

        const POINT panBeforeHeightFit = viewer.PanOffset();
        Expect(panBeforeHeightFit.x != 0 || panBeforeHeightFit.y != 0,
            "Viewer fit-height test did not establish a non-zero pan offset");
        RECT heightFitBeforeRect{};
        Expect(GetWindowRect(viewer.Hwnd(), &heightFitBeforeRect) != FALSE,
            "Failed to read the viewer bounds before windowed FitHeight");
        const DWORD heightFitBeforeStyle = static_cast<DWORD>(GetWindowLongPtrW(viewer.Hwnd(), GWL_STYLE));

        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, 'H', 0);
        PumpMessagesFor(100);
        RECT heightFitAfterRect{};
        RECT heightFitClientRect{};
        Expect(GetWindowRect(viewer.Hwnd(), &heightFitAfterRect) != FALSE
                && GetClientRect(viewer.Hwnd(), &heightFitClientRect) != FALSE,
            "Failed to read the viewer bounds after windowed FitHeight");
        Expect(heightFitAfterRect.left == heightFitBeforeRect.left
                && heightFitAfterRect.top == heightFitBeforeRect.top
                && heightFitAfterRect.right == heightFitBeforeRect.right
                && heightFitAfterRect.bottom == heightFitBeforeRect.bottom,
            "Viewer FitHeight changed the windowed window geometry");
        Expect(static_cast<DWORD>(GetWindowLongPtrW(viewer.Hwnd(), GWL_STYLE)) == heightFitBeforeStyle,
            "Viewer FitHeight changed the windowed window style");
        const int expectedHeightFitZoom = std::max(
            1,
            static_cast<int>(std::lround(
                static_cast<double>(heightFitClientRect.bottom) / currentImageHeight * 100.0)));
        Expect(viewer.CurrentZoomPercent() == expectedHeightFitZoom,
            "Viewer FitHeight did not fit the image to the existing client height");
        Expect(viewer.PanOffset().x == 0 && viewer.PanOffset().y == 0,
            "Viewer FitHeight did not reset the pan offset");

        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_PRIOR, 0);
        Expect(PumpMessagesUntil([&]() { return viewer.CurrentIndex() == 0 && viewer.CurrentZoomPercent() > 0; }, 5000),
            "Viewer previous-image navigation failed while in FitHeight");
        PumpMessagesFor(100);
        RECT previousHeightFitRect{};
        RECT previousHeightFitClientRect{};
        Expect(GetWindowRect(viewer.Hwnd(), &previousHeightFitRect) != FALSE
                && GetClientRect(viewer.Hwnd(), &previousHeightFitClientRect) != FALSE,
            "Failed to read the viewer bounds after FitHeight navigation");
        Expect(previousHeightFitRect.left == heightFitBeforeRect.left
                && previousHeightFitRect.top == heightFitBeforeRect.top
                && previousHeightFitRect.right == heightFitBeforeRect.right
                && previousHeightFitRect.bottom == heightFitBeforeRect.bottom,
            "Viewer FitHeight navigation changed the window geometry");
        const int expectedPreviousHeightFitZoom = std::max(
            1,
            static_cast<int>(std::lround(
                static_cast<double>(previousHeightFitClientRect.bottom) / 48.0 * 100.0)));
        Expect(viewer.CurrentZoomPercent() == expectedPreviousHeightFitZoom,
            "Viewer FitHeight navigation did not recompute the image height scale");

        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_NEXT, 0);
        Expect(PumpMessagesUntil([&]() { return viewer.CurrentIndex() == 1 && viewer.CurrentZoomPercent() > 0; }, 5000),
            "Viewer next-image navigation failed while returning from FitHeight");

        RECT widthFitBeforeRect{};
        Expect(GetWindowRect(viewer.Hwnd(), &widthFitBeforeRect) != FALSE,
            "Failed to read the viewer bounds before FitWidth");
        const DWORD widthFitBeforeStyle = static_cast<DWORD>(GetWindowLongPtrW(viewer.Hwnd(), GWL_STYLE));
        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, 'W', 0);
        PumpMessagesFor(100);
        RECT widthFitAfterRect{};
        RECT widthFitClientRect{};
        Expect(GetWindowRect(viewer.Hwnd(), &widthFitAfterRect) != FALSE
                && GetClientRect(viewer.Hwnd(), &widthFitClientRect) != FALSE,
            "Failed to read the viewer bounds after FitWidth");
        Expect(widthFitAfterRect.left == widthFitBeforeRect.left
                && widthFitAfterRect.top == widthFitBeforeRect.top
                && widthFitAfterRect.right == widthFitBeforeRect.right
                && widthFitAfterRect.bottom == widthFitBeforeRect.bottom,
            "Viewer FitWidth changed the window geometry");
        Expect(static_cast<DWORD>(GetWindowLongPtrW(viewer.Hwnd(), GWL_STYLE)) == widthFitBeforeStyle,
            "Viewer FitWidth changed the window style");
        const int expectedWidthFitZoom = std::max(
            1,
            static_cast<int>(std::lround(
                static_cast<double>(widthFitClientRect.right) / currentImageWidth * 100.0)));
        Expect(viewer.CurrentZoomPercent() == expectedWidthFitZoom,
            "Viewer FitWidth did not fit the image to the existing client width");
        Expect(viewer.PanOffset().x == 0 && viewer.PanOffset().y == 0,
            "Viewer FitWidth did not reset the pan offset");

        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_RIGHT, 0);
        Expect(PumpMessagesUntil([&]() { return viewer.CurrentIndex() == 2 && viewer.CurrentZoomPercent() > 0; }, 5000),
            "Viewer next-image navigation failed while in FitWidth");
        RECT nextWidthFitRect{};
        RECT nextWidthFitClientRect{};
        Expect(GetWindowRect(viewer.Hwnd(), &nextWidthFitRect) != FALSE
                && GetClientRect(viewer.Hwnd(), &nextWidthFitClientRect) != FALSE,
            "Failed to read the viewer bounds after FitWidth navigation");
        Expect(nextWidthFitRect.left == widthFitBeforeRect.left
                && nextWidthFitRect.top == widthFitBeforeRect.top
                && nextWidthFitRect.right == widthFitBeforeRect.right
                && nextWidthFitRect.bottom == widthFitBeforeRect.bottom,
            "Viewer FitWidth navigation changed the window geometry");
        const int expectedNextWidthFitZoom = std::max(
            1,
            static_cast<int>(std::lround(
                static_cast<double>(nextWidthFitClientRect.right) / 40.0 * 100.0)));
        Expect(viewer.CurrentZoomPercent() == expectedNextWidthFitZoom,
            "Viewer FitWidth navigation did not recompute the image width scale");
        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_RIGHT, 0);
        Expect(PumpMessagesUntil([&]() { return viewer.CurrentIndex() == 3 && viewer.CurrentZoomPercent() > 0; }, 5000),
            "Viewer portrait-image navigation failed while in FitWidth");
        RECT constrainedWidthFitRect{};
        Expect(GetWindowRect(viewer.Hwnd(), &constrainedWidthFitRect) != FALSE,
            "Failed to read the viewer bounds after the portrait FitWidth navigation");
        Expect(constrainedWidthFitRect.left == widthFitBeforeRect.left
                && constrainedWidthFitRect.top == widthFitBeforeRect.top
                && constrainedWidthFitRect.right == widthFitBeforeRect.right
                && constrainedWidthFitRect.bottom == widthFitBeforeRect.bottom,
            "Viewer FitWidth navigation changed the window geometry for an oversized image height");

        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_LEFT, 0);
        Expect(PumpMessagesUntil([&]() { return viewer.CurrentIndex() == 2 && viewer.CurrentZoomPercent() > 0; }, 5000),
            "Viewer previous-image navigation failed after the constrained FitWidth check");
        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_LEFT, 0);
        Expect(PumpMessagesUntil([&]() { return viewer.CurrentIndex() == 1 && viewer.CurrentZoomPercent() > 0; }, 5000),
            "Viewer previous-image navigation failed while returning from FitWidth");

        SendMessageW(viewer.Hwnd(), WM_LBUTTONDBLCLK, 0, MAKELPARAM(100, 100));
        PumpMessagesFor(100);
        Expect(viewer.IsFullScreen(), "Viewer double-click did not re-enter full screen");
        RECT fullScreenHeightFitBeforeRect{};
        Expect(GetWindowRect(viewer.Hwnd(), &fullScreenHeightFitBeforeRect) != FALSE,
            "Failed to read the fullscreen bounds before FitHeight");
        const DWORD fullScreenHeightFitBeforeStyle = static_cast<DWORD>(GetWindowLongPtrW(viewer.Hwnd(), GWL_STYLE));
        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, 'H', 0);
        PumpMessagesFor(100);
        RECT fullScreenHeightFitAfterRect{};
        RECT fullScreenHeightFitClientRect{};
        Expect(viewer.IsFullScreen()
                && GetWindowRect(viewer.Hwnd(), &fullScreenHeightFitAfterRect) != FALSE
                && GetClientRect(viewer.Hwnd(), &fullScreenHeightFitClientRect) != FALSE,
            "Viewer FitHeight shortcut did not preserve F11 fullscreen");
        Expect(fullScreenHeightFitAfterRect.left == fullScreenHeightFitBeforeRect.left
                && fullScreenHeightFitAfterRect.top == fullScreenHeightFitBeforeRect.top
                && fullScreenHeightFitAfterRect.right == fullScreenHeightFitBeforeRect.right
                && fullScreenHeightFitAfterRect.bottom == fullScreenHeightFitBeforeRect.bottom,
            "Viewer FitHeight changed fullscreen window geometry");
        Expect(static_cast<DWORD>(GetWindowLongPtrW(viewer.Hwnd(), GWL_STYLE)) == fullScreenHeightFitBeforeStyle,
            "Viewer FitHeight changed fullscreen window style");
        const int expectedFullScreenHeightFitZoom = std::max(
            1,
            static_cast<int>(std::lround(
                static_cast<double>(fullScreenHeightFitClientRect.bottom) / currentImageHeight * 100.0)));
        Expect(viewer.CurrentZoomPercent() == expectedFullScreenHeightFitZoom,
            "Viewer fullscreen FitHeight did not fit the image to the client height");

        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_OEM_PLUS, 0);
        PumpMessagesFor(300);
        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, 'H', 0);
        PumpMessagesFor(100);
        Expect(viewer.IsFullScreen(), "Viewer FitHeight shortcut exited fullscreen after a custom zoom");
        RECT fullScreenCustomHeightFitRect{};
        Expect(GetWindowRect(viewer.Hwnd(), &fullScreenCustomHeightFitRect) != FALSE
                && fullScreenCustomHeightFitRect.left == fullScreenHeightFitBeforeRect.left
                && fullScreenCustomHeightFitRect.top == fullScreenHeightFitBeforeRect.top
                && fullScreenCustomHeightFitRect.right == fullScreenHeightFitBeforeRect.right
                && fullScreenCustomHeightFitRect.bottom == fullScreenHeightFitBeforeRect.bottom,
            "Viewer custom-zoom FitHeight changed fullscreen window geometry");

        Expect(viewer.IsFullScreen(), "Viewer fit-height fullscreen state was not preserved before testing FitWidth");
        const int fullScreenWidthFitImageWidth = currentImageWidth;
        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, 'W', 0);
        PumpMessagesFor(100);
        RECT fullScreenWidthFitRect{};
        RECT fullScreenWidthFitClientRect{};
        Expect(viewer.IsFullScreen()
                && GetWindowRect(viewer.Hwnd(), &fullScreenWidthFitRect) != FALSE
                && GetClientRect(viewer.Hwnd(), &fullScreenWidthFitClientRect) != FALSE,
            "Viewer FitWidth shortcut did not preserve fullscreen");
        Expect(fullScreenWidthFitRect.left == fullScreenHeightFitBeforeRect.left
                && fullScreenWidthFitRect.top == fullScreenHeightFitBeforeRect.top
                && fullScreenWidthFitRect.right == fullScreenHeightFitBeforeRect.right
                && fullScreenWidthFitRect.bottom == fullScreenHeightFitBeforeRect.bottom,
            "Viewer FitWidth changed fullscreen window geometry");
        Expect(static_cast<DWORD>(GetWindowLongPtrW(viewer.Hwnd(), GWL_STYLE)) == fullScreenHeightFitBeforeStyle,
            "Viewer FitWidth changed fullscreen window style");
        const int expectedFullScreenWidthFitZoom = std::max(
            1,
            static_cast<int>(std::lround(
                static_cast<double>(fullScreenWidthFitClientRect.right) / fullScreenWidthFitImageWidth * 100.0)));
        Expect(viewer.CurrentZoomPercent() == expectedFullScreenWidthFitZoom,
            "Viewer fullscreen FitWidth did not fit the image to the client width");

        BYTE originalKeyboardState[256]{};
        BYTE modifiedKeyboardState[256]{};
        Expect(GetKeyboardState(originalKeyboardState) != FALSE,
            "Failed to read the keyboard state before testing window geometry shortcuts");
        std::copy(std::begin(originalKeyboardState), std::end(originalKeyboardState), std::begin(modifiedKeyboardState));
        modifiedKeyboardState[VK_CONTROL] |= 0x80;
        modifiedKeyboardState[VK_SHIFT] |= 0x80;
        Expect(SetKeyboardState(modifiedKeyboardState) != FALSE,
            "Failed to stage Ctrl+Shift state before testing window geometry shortcuts");
        const RECT fullScreenGeometryBefore = fullScreenWidthFitRect;
        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, 'H', 0);
        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, 'W', 0);
        PumpMessagesFor(100);
        RECT fullScreenGeometryAfter{};
        Expect(viewer.IsFullScreen()
                && GetWindowRect(viewer.Hwnd(), &fullScreenGeometryAfter) != FALSE
                && fullScreenGeometryAfter.left == fullScreenGeometryBefore.left
                && fullScreenGeometryAfter.top == fullScreenGeometryBefore.top
                && fullScreenGeometryAfter.right == fullScreenGeometryBefore.right
                && fullScreenGeometryAfter.bottom == fullScreenGeometryBefore.bottom,
            "Ctrl+Shift+H/W changed fullscreen window geometry");
        Expect(SetKeyboardState(originalKeyboardState) != FALSE,
            "Failed to restore the keyboard state after testing fullscreen geometry shortcuts");

        viewer.SetEscapeKeyBehavior(hyperbrowse::viewer::EscapeKeyBehavior::ActualSize);
        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_ESCAPE, 0);
        PumpMessagesFor(100);
        Expect(!viewer.IsFullScreen(), "Failed to leave fullscreen before testing window geometry shortcuts");
        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, '0', 0);
        PumpMessagesFor(100);
        MONITORINFO monitorInfo{sizeof(MONITORINFO)};
        const HMONITOR monitor = MonitorFromWindow(viewer.Hwnd(), MONITOR_DEFAULTTONEAREST);
        Expect(monitor != nullptr && GetMonitorInfoW(monitor, &monitorInfo) != FALSE,
            "Failed to read the monitor work area before testing window geometry shortcuts");

        Expect(SetKeyboardState(modifiedKeyboardState) != FALSE,
            "Failed to stage Ctrl+Shift state for window geometry shortcuts");
        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, 'H', 0);
        PumpMessagesFor(100);
        RECT workAreaHeightRect{};
        Expect(GetWindowRect(viewer.Hwnd(), &workAreaHeightRect) != FALSE,
            "Failed to read the window bounds after Ctrl+Shift+H");
        Expect(workAreaHeightRect.top == monitorInfo.rcWork.top
                && workAreaHeightRect.bottom == monitorInfo.rcWork.bottom,
            "Ctrl+Shift+H did not size the window to the monitor work-area height");

        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, 'W', 0);
        PumpMessagesFor(100);
        RECT workAreaWidthRect{};
        Expect(GetWindowRect(viewer.Hwnd(), &workAreaWidthRect) != FALSE,
            "Failed to read the window bounds after Ctrl+Shift+W");
        Expect(workAreaWidthRect.left == monitorInfo.rcWork.left
                && workAreaWidthRect.right == monitorInfo.rcWork.right,
            "Ctrl+Shift+W did not size the window to the monitor work-area width");
        Expect(SetKeyboardState(originalKeyboardState) != FALSE,
            "Failed to restore the keyboard state after testing window geometry shortcuts");

        viewer.SetEscapeKeyBehavior(hyperbrowse::viewer::EscapeKeyBehavior::FitWidth);
        SendMessageW(viewer.Hwnd(), WM_LBUTTONDBLCLK, 0, MAKELPARAM(100, 100));
        PumpMessagesFor(100);
        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_ESCAPE, 0);
        PumpMessagesFor(100);
        RECT escapeWidthRect{};
        Expect(!viewer.IsFullScreen()
                && GetWindowRect(viewer.Hwnd(), &escapeWidthRect) != FALSE
                && escapeWidthRect.left == monitorInfo.rcWork.left
                && escapeWidthRect.right == monitorInfo.rcWork.right,
            "Fullscreen Escape Fit Width did not use the window work-area width action");

        viewer.SetEscapeKeyBehavior(hyperbrowse::viewer::EscapeKeyBehavior::FitHeight);
        SendMessageW(viewer.Hwnd(), WM_LBUTTONDBLCLK, 0, MAKELPARAM(100, 100));
        PumpMessagesFor(100);
        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_ESCAPE, 0);
        PumpMessagesFor(100);
        RECT escapeHeightRect{};
        Expect(!viewer.IsFullScreen()
                && GetWindowRect(viewer.Hwnd(), &escapeHeightRect) != FALSE
                && escapeHeightRect.top == monitorInfo.rcWork.top
                && escapeHeightRect.bottom == monitorInfo.rcWork.bottom,
            "Fullscreen Escape Fit Height did not use the window work-area height action");

        viewer.SetEscapeKeyBehavior(hyperbrowse::viewer::EscapeKeyBehavior::ActualSize);
        SendMessageW(viewer.Hwnd(), WM_LBUTTONDBLCLK, 0, MAKELPARAM(100, 100));
        PumpMessagesFor(100);
        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_ESCAPE, 0);
        PumpMessagesFor(100);
        Expect(!viewer.IsFullScreen() && viewer.CurrentZoomPercent() == 100,
            "Fullscreen Escape Actual Size did not use native image scale when it fit");

    }

    void RunViewerWindowFitModeScenario(HINSTANCE instance, HWND ownerWindow)
    {
        TempFolder root(L"HyperBrowsePrompt6ViewerFit");
        const fs::path firstPath = root.Root() / L"first.jpg";
        const fs::path secondPath = root.Root() / L"second.png";
        const fs::path thirdPath = root.Root() / L"third.png";
        const fs::path tallPath = root.Root() / L"tall.png";
        WriteTestImage(firstPath, TestImageFormat::Jpeg, 48, 24, 6);
        WriteTestImage(secondPath, TestImageFormat::Png, 64, 32);
        WriteTestImage(thirdPath, TestImageFormat::Png, 40, 16);
        WriteTestImage(tallPath, TestImageFormat::Png, 24, 48);

        std::vector<hyperbrowse::browser::BrowserItem> items;
        items.push_back(hyperbrowse::browser::BrowserItem{L"first.jpg", firstPath.wstring(), L"JPG", L"2026-04-11 12:00", 1, 10, 256, 256});
        items.push_back(hyperbrowse::browser::BrowserItem{L"second.png", secondPath.wstring(), L"PNG", L"2026-04-11 12:01", 2, 20, 256, 256});
        items.push_back(hyperbrowse::browser::BrowserItem{L"third.png", thirdPath.wstring(), L"PNG", L"2026-04-11 12:02", 3, 30, 256, 256});
        items.push_back(hyperbrowse::browser::BrowserItem{L"tall.png", tallPath.wstring(), L"PNG", L"2026-04-11 12:03", 4, 40, 256, 256});

        hyperbrowse::viewer::ViewerWindow viewer(instance);
        Expect(viewer.Open(ownerWindow, items, 1, false), "Viewer fit-mode window failed to open");
        Expect(PumpMessagesUntil([&]() { return viewer.CurrentZoomPercent() > 0; }, 5000),
            "Viewer fit-mode window did not finish the initial image decode");
        Expect(viewer.IsFullScreen(), "Viewer fit-mode window should open in full screen by default");
        RunViewerWindowFitModeChecks(viewer, 64, 32);

        Expect(viewer.CurrentIndex() == 1, "Viewer wraparound scenario did not start from the middle image");
        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_RIGHT, 0);
        Expect(PumpMessagesUntil([&]() { return viewer.CurrentIndex() == 2 && viewer.CurrentZoomPercent() > 0; }, 5000),
            "Viewer next-image navigation failed before the wraparound boundary");
        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_RIGHT, 0);
        Expect(PumpMessagesUntil([&]() { return viewer.CurrentIndex() == 3 && viewer.CurrentZoomPercent() > 0; }, 5000),
            "Viewer next-image navigation did not reach the last image");
        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_RIGHT, 0);
        Expect(PumpMessagesUntil([&]() { return viewer.CurrentIndex() == 0 && viewer.CurrentZoomPercent() > 0; }, 5000),
            "Viewer next-image navigation did not wrap from the last image to the first");
        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_LEFT, 0);
        Expect(PumpMessagesUntil([&]() { return viewer.CurrentIndex() == 3 && viewer.CurrentZoomPercent() > 0; }, 5000),
            "Viewer previous-image navigation did not wrap from the first image to the last");

        BYTE originalKeyboardState[256]{};
        BYTE controlKeyboardState[256]{};
        Expect(GetKeyboardState(originalKeyboardState) != FALSE,
            "Failed to read the keyboard state before testing viewer boundary shortcuts");
        for (int index = 0; index < static_cast<int>(std::size(originalKeyboardState)); ++index)
        {
            controlKeyboardState[index] = originalKeyboardState[index];
        }
        controlKeyboardState[VK_CONTROL] |= 0x80;
        Expect(SetKeyboardState(controlKeyboardState) != FALSE,
            "Failed to stage Ctrl state for the viewer boundary shortcut test");
        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_HOME, 0);
        Expect(PumpMessagesUntil([&]() { return viewer.CurrentIndex() == 0 && viewer.CurrentZoomPercent() > 0; }, 5000),
            "Viewer Ctrl+Home did not navigate to the first image");
        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_END, 0);
        Expect(PumpMessagesUntil([&]() { return viewer.CurrentIndex() == 3 && viewer.CurrentZoomPercent() > 0; }, 5000),
            "Viewer Ctrl+End did not navigate to the last image");
        Expect(SetKeyboardState(originalKeyboardState) != FALSE,
            "Failed to restore the keyboard state after testing viewer boundary shortcuts");

        SendMessageW(viewer.Hwnd(), WM_CLOSE, 0, 0);
        PumpMessagesFor(100);

        const fs::path oversizedPath = root.Root() / L"oversized.png";
        MONITORINFO oversizedMonitorInfo{sizeof(MONITORINFO)};
        const HMONITOR oversizedMonitor = MonitorFromWindow(ownerWindow, MONITOR_DEFAULTTONEAREST);
        Expect(oversizedMonitor != nullptr && GetMonitorInfoW(oversizedMonitor, &oversizedMonitorInfo) != FALSE,
            "Failed to read the monitor work area for oversized-image Escape coverage");
        const LONG workWidth = oversizedMonitorInfo.rcWork.right - oversizedMonitorInfo.rcWork.left;
        const LONG workHeight = oversizedMonitorInfo.rcWork.bottom - oversizedMonitorInfo.rcWork.top;
        WriteTestImage(oversizedPath, TestImageFormat::Png, static_cast<int>(workWidth + 1), static_cast<int>(workHeight + 1));
        std::vector<hyperbrowse::browser::BrowserItem> oversizedItems;
        oversizedItems.push_back(hyperbrowse::browser::BrowserItem{
            L"oversized.png", oversizedPath.wstring(), L"PNG", L"2026-04-11 12:04", 5, 50, 256, 256});
        hyperbrowse::viewer::ViewerWindow oversizedViewer(instance);
        oversizedViewer.SetEscapeKeyBehavior(hyperbrowse::viewer::EscapeKeyBehavior::ActualSize);
        Expect(oversizedViewer.Open(ownerWindow, oversizedItems, 0, false),
            "Oversized-image viewer failed to open for fullscreen Escape fallback coverage");
        Expect(PumpMessagesUntil([&]() { return oversizedViewer.CurrentZoomPercent() > 0; }, 5000),
            "Oversized-image viewer did not finish its initial image decode");
        SendMessageW(oversizedViewer.Hwnd(), WM_KEYDOWN, VK_ESCAPE, 0);
        PumpMessagesFor(100);
        Expect(!oversizedViewer.IsFullScreen(),
            "Fullscreen Escape Actual Size did not leave fullscreen for an oversized image");
        Expect(oversizedViewer.CurrentZoomPercent() < 100,
            "Fullscreen Escape Actual Size showed an oversized image at clipped native scale");
        SendMessageW(oversizedViewer.Hwnd(), WM_CLOSE, 0, 0);
        PumpMessagesFor(100);
    }

    void RunViewerWindowScenario(HINSTANCE instance, HWND ownerWindow)
    {
        ScopedRegistryDwordBackup overlaySettingBackup(kRegistryPath, kRegistryValueViewerInfoOverlaysVisible);
        ScopedRegistryDwordBackup overlayTextSizeBackup(kRegistryPath, kRegistryValueViewerInfoOverlayTextSize);
        ScopedRegistryDwordBackup windowedFullMetadataBackup(kRegistryPath, kRegistryValueViewerWindowedFullMetadataVisible);
        ScopedRegistryDwordBackup fullScreenFullMetadataBackup(kRegistryPath, kRegistryValueViewerFullScreenFullMetadataVisible);
        ScopedRegistryDwordBackup fullMetadataBackup(kRegistryPath, kRegistryValueViewerFullMetadataVisible);
        auto* state = reinterpret_cast<TestWindowState*>(GetWindowLongPtrW(ownerWindow, GWLP_USERDATA));
        Expect(state != nullptr, "Failed to locate the hidden test window state");

        {
            HKEY key{};
            if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegistryPath, 0, KEY_WRITE, &key) == ERROR_SUCCESS)
            {
                RegDeleteValueW(key, kRegistryValueViewerInfoOverlaysVisible);
                RegDeleteValueW(key, kRegistryValueViewerInfoOverlayTextSize);
                RegDeleteValueW(key, kRegistryValueViewerWindowedFullMetadataVisible);
                RegDeleteValueW(key, kRegistryValueViewerFullScreenFullMetadataVisible);
                RegDeleteValueW(key, kRegistryValueViewerFullMetadataVisible);
                RegCloseKey(key);
            }
        }

        TempFolder root(L"HyperBrowsePrompt6Viewer");
        const fs::path firstPath = root.Root() / L"first.jpg";
        const fs::path secondPath = root.Root() / L"second.png";
        const fs::path thirdPath = root.Root() / L"third.png";
        const fs::path tallPath = root.Root() / L"tall.png";
        WriteTestImage(firstPath, TestImageFormat::Jpeg, 48, 24, 6);
        WriteTestImage(secondPath, TestImageFormat::Png, 640, 320);
        WriteTestImage(thirdPath, TestImageFormat::Png, 40, 16);
        WriteTestImage(tallPath, TestImageFormat::Png, 24, 48);

        std::vector<hyperbrowse::browser::BrowserItem> items;
        items.push_back(hyperbrowse::browser::BrowserItem{L"first.jpg", firstPath.wstring(), L"JPG", L"2026-04-11 12:00", 1, 10, 256, 256});
        items.push_back(hyperbrowse::browser::BrowserItem{L"second.png", secondPath.wstring(), L"PNG", L"2026-04-11 12:01", 2, 20, 256, 256});
        items.push_back(hyperbrowse::browser::BrowserItem{L"third.png", thirdPath.wstring(), L"PNG", L"2026-04-11 12:02", 3, 30, 256, 256});
        items.push_back(hyperbrowse::browser::BrowserItem{L"tall.png", tallPath.wstring(), L"PNG", L"2026-04-11 12:03", 4, 40, 256, 256});

        hyperbrowse::viewer::ViewerWindow viewer(instance);
        Expect(viewer.Open(ownerWindow, items, 0, false), "Viewer window failed to open");
        Expect(PumpMessagesUntil([&]() { return viewer.CurrentZoomPercent() > 0; }, 5000),
               "Viewer window did not finish the initial image decode");
        Expect(viewer.IsFullScreen(), "Viewer should open in full screen by default");
        Expect(viewer.AreInfoOverlaysVisible(), "Viewer should default to showing info overlays when no persisted preference exists");
        Expect(viewer.OverlayTextSize() == hyperbrowse::viewer::InfoOverlayTextSize::Small,
            "Viewer should default to the small overlay text size when no persisted preference exists");
        Expect(!viewer.IsFullMetadataVisible(), "Viewer should default to hiding the full metadata pane when no persisted preference exists");

        RECT originalViewerRect{};
        Expect(GetWindowRect(viewer.Hwnd(), &originalViewerRect) != FALSE,
            "Failed to save the viewer bounds for the zoom transition test");
        SetWindowPos(viewer.Hwnd(), nullptr, 0, 0, 32, 32, SWP_NOZORDER | SWP_NOACTIVATE);
        PumpMessagesFor(50);
        const int fitZoomPercent = viewer.CurrentZoomPercent();
        Expect(fitZoomPercent < 100, "Viewer zoom regression setup did not produce a sub-100% fit scale");
        RECT viewerClientRect{};
        Expect(GetClientRect(viewer.Hwnd(), &viewerClientRect) != FALSE,
            "Failed to read the viewer client area for the zoom transition test");
        const double fitScale = std::min(
            static_cast<double>(viewerClientRect.right) / 24.0,
            static_cast<double>(viewerClientRect.bottom) / 48.0);
        const int firstZoomTargetPercent = std::max(
            1,
            static_cast<int>(std::lround(fitScale * 1.25 * 100.0)) + 2);
        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_OEM_PLUS, 0);
        PumpMessagesFor(50);
        Expect(viewer.CurrentZoomPercent() <= firstZoomTargetPercent,
            "Viewer zoom stepped past the target while leaving fit mode");
        for (int index = 0; index < 4; ++index)
        {
            SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_OEM_PLUS, 0);
        }
        PumpMessagesFor(250);
        const LONG initialHorizontalPan = viewer.PanOffset().x;
        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_RIGHT, 0);
        const POINT pannedRight = viewer.PanOffset();
        Expect(viewer.CurrentIndex() == 0 && pannedRight.x < initialHorizontalPan,
            "Viewer right arrow did not pan the viewport to the right by default");
        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_LEFT, 0);
        Expect(viewer.CurrentIndex() == 0 && viewer.PanOffset().x == initialHorizontalPan,
            "Viewer left arrow did not pan the horizontally zoomed image back");
        viewer.SetKeyboardPanningInverted(true);
        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_RIGHT, 0);
        Expect(viewer.IsKeyboardPanningInverted() && viewer.PanOffset().x > initialHorizontalPan,
            "Viewer inverted keyboard panning option did not restore the legacy right-arrow direction");
        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_LEFT, 0);
        Expect(viewer.PanOffset().x == initialHorizontalPan,
            "Viewer inverted keyboard panning option did not restore the legacy left-arrow direction");
        viewer.SetKeyboardPanningInverted(false);
        SetWindowPos(viewer.Hwnd(), nullptr,
                     originalViewerRect.left,
                     originalViewerRect.top,
                     originalViewerRect.right - originalViewerRect.left,
                     originalViewerRect.bottom - originalViewerRect.top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        PumpMessagesFor(50);

        viewer.SetOverlayTextSize(hyperbrowse::viewer::InfoOverlayTextSize::Large);
        Expect(viewer.OverlayTextSize() == hyperbrowse::viewer::InfoOverlayTextSize::Large,
            "Viewer did not apply the requested overlay text size");
        viewer.SetFullMetadataVisible(true);
        Expect(viewer.IsFullMetadataVisible(), "Viewer did not apply the requested full metadata visibility");

        SendMessageW(viewer.Hwnd(), WM_LBUTTONDBLCLK, 0, MAKELPARAM(100, 100));
        PumpMessagesFor(100);
        Expect(!viewer.IsFullScreen() && !viewer.IsFullMetadataVisible(),
            "Viewer did not switch to the independent windowed full metadata preference");
        viewer.SetFullMetadataVisible(true);
        SendMessageW(viewer.Hwnd(), WM_LBUTTONDBLCLK, 0, MAKELPARAM(100, 100));
        PumpMessagesFor(100);
        Expect(viewer.IsFullScreen() && viewer.IsFullMetadataVisible(),
            "Viewer did not restore the full-screen full metadata preference");
        SendMessageW(viewer.Hwnd(), WM_LBUTTONDBLCLK, 0, MAKELPARAM(100, 100));
        PumpMessagesFor(100);
        Expect(!viewer.IsFullScreen() && viewer.IsFullMetadataVisible(),
            "Viewer did not restore the windowed full metadata preference");
        SendMessageW(viewer.Hwnd(), WM_LBUTTONDBLCLK, 0, MAKELPARAM(100, 100));
        PumpMessagesFor(100);
        Expect(viewer.IsFullScreen() && viewer.IsFullMetadataVisible(),
            "Viewer did not restore the full-screen metadata state after returning from windowed mode");

        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_RIGHT, 0);
        Expect(PumpMessagesUntil([&]() { return viewer.CurrentIndex() == 1 && viewer.CurrentZoomPercent() > 0; }, 5000),
               "Viewer next-image navigation failed");

        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_UP, 0);
        Expect(PumpMessagesUntil([&]() { return viewer.CurrentIndex() == 0 && viewer.CurrentZoomPercent() > 0; }, 5000),
            "Viewer up arrow did not navigate to the previous image when fit to window");
        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_DOWN, 0);
        Expect(PumpMessagesUntil([&]() { return viewer.CurrentIndex() == 1 && viewer.CurrentZoomPercent() > 0; }, 5000),
            "Viewer down arrow did not navigate to the next image when fit to window");
        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_PRIOR, 0);
        Expect(PumpMessagesUntil([&]() { return viewer.CurrentIndex() == 0 && viewer.CurrentZoomPercent() > 0; }, 5000),
            "Viewer Page Up did not navigate to the previous image");
        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_NEXT, 0);
        Expect(PumpMessagesUntil([&]() { return viewer.CurrentIndex() == 1 && viewer.CurrentZoomPercent() > 0; }, 5000),
            "Viewer Page Down did not navigate to the next image");
        PumpMessagesFor(100);
        RECT metadataClientRect{};
        Expect(GetClientRect(viewer.Hwnd(), &metadataClientRect) != FALSE,
            "Failed to read the viewer client area for the metadata pane test");
        const POINT metadataSamplePoint{
            metadataClientRect.left + ((metadataClientRect.right - metadataClientRect.left) * 5 / 6),
            metadataClientRect.top + 24};
        COLORREF metadataPixelWithOverlays{};
        Expect(ReadClientPixel(viewer.Hwnd(), metadataSamplePoint, &metadataPixelWithOverlays),
            "Failed to sample the visible metadata pane");

        viewer.SetInfoOverlaysVisible(true);
        viewer.SetFullMetadataVisible(false);
        PumpMessagesFor(100);
        Expect(viewer.AreInfoOverlaysVisible() && !viewer.IsFullMetadataVisible(),
            "Viewer did not apply the overlays-on and full-metadata-off state");
        COLORREF metadataPixelWithoutFullMetadata{};
        Expect(ReadClientPixel(viewer.Hwnd(), metadataSamplePoint, &metadataPixelWithoutFullMetadata),
            "Failed to sample the viewer with full metadata hidden");
        Expect(metadataPixelWithoutFullMetadata != metadataPixelWithOverlays,
            "Viewer full metadata pane remained visible when full metadata was disabled");

        viewer.SetFullMetadataVisible(true);
        PumpMessagesFor(100);
        Expect(viewer.IsFullMetadataVisible(),
            "Viewer did not re-enable the full metadata pane");

        state->viewerStartFolderSlideshowRequests = 0;
        state->lastViewerStartFolderSlideshowSource = nullptr;
        BYTE originalKeyboardState[256]{};
        BYTE modifiedKeyboardState[256]{};
        Expect(GetKeyboardState(originalKeyboardState) != FALSE,
            "Failed to read the keyboard state before testing the folder slideshow shortcut");
        for (int index = 0; index < static_cast<int>(std::size(originalKeyboardState)); ++index)
        {
            modifiedKeyboardState[index] = originalKeyboardState[index];
        }
        modifiedKeyboardState[VK_CONTROL] |= 0x80;
        modifiedKeyboardState[VK_SHIFT] |= 0x80;
        Expect(SetKeyboardState(modifiedKeyboardState) != FALSE,
            "Failed to stage the keyboard state for the folder slideshow shortcut test");
        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, 'F', 0);
        PumpMessagesFor(100);
        SetKeyboardState(originalKeyboardState);
        Expect(state->viewerStartFolderSlideshowRequests == 1,
            "Viewer Ctrl+Shift+F did not request a folder slideshow from the owner window");
        Expect(state->lastViewerStartFolderSlideshowSource == viewer.Hwnd(),
            "Viewer folder slideshow request did not identify the active viewer window");

        state->viewerQuickSendRequests = 0;
        state->lastViewerQuickSendSource = nullptr;
        BYTE unmodifiedShortcutKeyboardState[256]{};
        Expect(GetKeyboardState(unmodifiedShortcutKeyboardState) != FALSE,
            "Failed to read the keyboard state before testing viewer Quick Send shortcuts");
        unmodifiedShortcutKeyboardState[VK_CONTROL] &= ~0x80;
        unmodifiedShortcutKeyboardState[VK_SHIFT] &= ~0x80;
        unmodifiedShortcutKeyboardState[VK_MENU] &= ~0x80;
        Expect(SetKeyboardState(unmodifiedShortcutKeyboardState) != FALSE,
            "Failed to clear modifier state for the viewer Quick Send shortcut test");
        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_F7, 0);
        PumpMessagesFor(100);
        Expect(state->viewerQuickSendRequests == 1
                && state->lastViewerQuickSendOperation == hyperbrowse::viewer::QuickSendOperation::Move,
            "Viewer F7 did not dispatch a Quick Send move request");
        Expect(state->lastViewerQuickSendSource == viewer.Hwnd(),
            "Viewer F7 Quick Send request did not identify the active viewer window");

        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_F7, 1LL << 30);
        PumpMessagesFor(100);
        Expect(state->viewerQuickSendRequests == 1,
            "Viewer Quick Send move request did not ignore key auto-repeat");

        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_F8, 0);
        PumpMessagesFor(100);
        Expect(state->viewerQuickSendRequests == 2
                && state->lastViewerQuickSendOperation == hyperbrowse::viewer::QuickSendOperation::Copy,
            "Viewer F8 did not dispatch a Quick Send copy request");
        Expect(SetKeyboardState(originalKeyboardState) != FALSE,
            "Failed to restore the keyboard state after testing viewer Quick Send shortcuts");

        modifiedKeyboardState[VK_CONTROL] |= 0x80;
        Expect(SetKeyboardState(modifiedKeyboardState) != FALSE,
            "Failed to stage Ctrl state for the viewer Quick Send modifier test");
        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_F7, 0);
        PumpMessagesFor(100);
        SetKeyboardState(originalKeyboardState);
        Expect(state->viewerQuickSendRequests == 2,
            "Viewer Quick Send did not ignore a Ctrl-modified shortcut");

        modifiedKeyboardState[VK_MENU] |= 0x80;
        Expect(SetKeyboardState(modifiedKeyboardState) != FALSE,
            "Failed to stage Alt state for the viewer Quick Send modifier test");
        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_F8, 0);
        PumpMessagesFor(100);
        SetKeyboardState(originalKeyboardState);
        Expect(state->viewerQuickSendRequests == 2,
            "Viewer Quick Send did not ignore an Alt-modified shortcut");

        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_TAB, 0);
        PumpMessagesFor(100);
        Expect(!viewer.AreInfoOverlaysVisible(), "Viewer Tab key did not hide the info overlays");
        COLORREF metadataPixelWithoutOverlays{};
        Expect(ReadClientPixel(viewer.Hwnd(), metadataSamplePoint, &metadataPixelWithoutOverlays),
            "Failed to sample the metadata pane with info overlays hidden");
        Expect(metadataPixelWithoutOverlays != metadataPixelWithOverlays,
            "Viewer full metadata pane remained visible when info overlays were hidden");

        RECT secondImageClientRect{};
        Expect(GetClientRect(viewer.Hwnd(), &secondImageClientRect) != FALSE,
            "Failed to read the viewer client area for the Enter zoom toggle test");
        const int secondImageFitZoomPercent = std::max(
            1,
            static_cast<int>(std::lround(std::min(
                static_cast<double>(secondImageClientRect.right) / 640.0,
                static_cast<double>(secondImageClientRect.bottom) / 320.0) * 100.0)));

        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_RETURN, 0);
        Expect(PumpMessagesUntil([&]() { return viewer.CurrentZoomPercent() == 100; }, 1000),
               "Viewer Enter key did not switch from fit-to-window to actual size");

        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_OEM_MINUS, 0);
        PumpMessagesFor(300);
        Expect(viewer.CurrentZoomPercent() == 100,
            "Viewer zoom-out should not shrink the image below the current window-fit bound");

        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_OEM_PLUS, 0);
        Expect(PumpMessagesUntil([&]() { return viewer.CurrentZoomPercent() > 100; }, 1000),
            "Viewer zoom-in command failed before testing the Enter fit fallback");

        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_RETURN, 0);
        Expect(PumpMessagesUntil([&]() { return viewer.CurrentZoomPercent() == secondImageFitZoomPercent; }, 1000),
            "Viewer Enter key did not choose fit-to-window from custom zoom");

        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_RETURN, 0);
        Expect(PumpMessagesUntil([&]() { return viewer.CurrentZoomPercent() == 100; }, 1000),
            "Viewer Enter key did not switch from fit-to-window to actual size");

        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_RETURN, 0);
        Expect(PumpMessagesUntil([&]() { return viewer.CurrentZoomPercent() == secondImageFitZoomPercent; }, 1000),
            "Viewer Enter key did not switch from actual size to fit-to-window");

        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, 'R', 0);
        PumpMessagesFor(100);
        Expect(viewer.RotationQuarterTurns() == 1, "Viewer rotate-right command failed");

        const int panFitZoomPercent = viewer.CurrentZoomPercent();
        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_OEM_PLUS, 0);
        Expect(PumpMessagesUntil([&]() { return viewer.CurrentZoomPercent() > panFitZoomPercent; }, 1000),
               "Viewer zoom-in command failed");

        const POINT initialPan = viewer.PanOffset();
        SendMessageW(viewer.Hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(120, 120));
        SendMessageW(viewer.Hwnd(), WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(160, 150));
        SendMessageW(viewer.Hwnd(), WM_LBUTTONUP, 0, MAKELPARAM(160, 150));
        PumpMessagesFor(100);
        const POINT movedPan = viewer.PanOffset();
        Expect(movedPan.x != initialPan.x || movedPan.y != initialPan.y, "Viewer pan interaction failed");

        const LONG panBeforeArrow = viewer.PanOffset().y;
        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_UP, 0);
        const LONG panAfterUp = viewer.PanOffset().y;
        Expect(panAfterUp > panBeforeArrow, "Viewer up arrow did not pan the viewport upward");
        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_DOWN, 0);
        Expect(viewer.PanOffset().y == panBeforeArrow, "Viewer down arrow did not pan the zoomed image downward");
        viewer.SetKeyboardPanningInverted(true);
        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_UP, 0);
        Expect(viewer.PanOffset().y < panBeforeArrow,
            "Viewer inverted keyboard panning option did not restore the legacy up-arrow direction");
        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_DOWN, 0);
        Expect(viewer.PanOffset().y == panBeforeArrow,
            "Viewer inverted keyboard panning option did not restore the legacy down-arrow direction");
        viewer.SetKeyboardPanningInverted(false);

        for (int index = 0; index < 128; ++index)
        {
            SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_UP, 0);
        }
        const LONG topPan = viewer.PanOffset().y;
        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_UP, 0);
        Expect(viewer.PanOffset().y == topPan, "Viewer up arrow exceeded the top image pan limit");

        for (int index = 0; index < 128; ++index)
        {
            SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_DOWN, 0);
        }
        const LONG bottomPan = viewer.PanOffset().y;
        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_DOWN, 0);
        Expect(viewer.PanOffset().y == bottomPan, "Viewer down arrow exceeded the bottom image pan limit");
        Expect(topPan > bottomPan, "Viewer vertical pan limits did not reflect image overflow");

         SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_RETURN, 0);
         PumpMessagesFor(100);
         const POINT fitPan = viewer.PanOffset();
         SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_UP, 0);
         SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_DOWN, 0);
         Expect(fitPan.x == 0 && fitPan.y == 0 && viewer.PanOffset().x == 0 && viewer.PanOffset().y == 0,
             "Viewer arrows panned an image while it was fit to the window");

        RunViewerWindowFitModeChecks(viewer, 640, 320);

        viewer.SetEscapeKeyBehavior(hyperbrowse::viewer::EscapeKeyBehavior::Close);
        SendMessageW(viewer.Hwnd(), WM_LBUTTONDBLCLK, 0, MAKELPARAM(100, 100));
        PumpMessagesFor(100);
        const HWND closeHandle = viewer.Hwnd();
        SendMessageW(closeHandle, WM_KEYDOWN, VK_ESCAPE, 0);
        PumpMessagesFor(100);
        Expect(closeHandle != nullptr && IsWindow(closeHandle) == FALSE && !viewer.IsOpen(),
            "Fullscreen Escape Close did not close the viewer window");

        SendMessageW(viewer.Hwnd(), WM_CLOSE, 0, 0);
        PumpMessagesFor(100);

         viewer.SetOverlayTextSize(hyperbrowse::viewer::InfoOverlayTextSize::Small);
         hyperbrowse::viewer::ViewerWindow::SetDefaultOverlayTextSize(
             hyperbrowse::viewer::InfoOverlayTextSize::Large);
         Expect(viewer.Open(ownerWindow, items, 0, false),
             "Viewer failed to reopen after changing the closed-viewer overlay default");
         Expect(viewer.OverlayTextSize() == hyperbrowse::viewer::InfoOverlayTextSize::Large,
             "Viewer did not reload the saved overlay text size when reopening a reused window object");
         SendMessageW(viewer.Hwnd(), WM_CLOSE, 0, 0);
         PumpMessagesFor(100);

         hyperbrowse::viewer::ViewerWindow restoredViewer(instance);
         Expect(restoredViewer.Open(ownerWindow, items, 0, false), "Restored viewer window failed to open");
         Expect(PumpMessagesUntil([&]() { return restoredViewer.CurrentZoomPercent() > 0; }, 5000),
             "Restored viewer window did not finish the initial image decode");
         Expect(!restoredViewer.AreInfoOverlaysVisible(), "Viewer did not restore the persisted info-overlay visibility");
         Expect(restoredViewer.OverlayTextSize() == hyperbrowse::viewer::InfoOverlayTextSize::Large,
             "Viewer did not restore the persisted overlay text size");
         Expect(restoredViewer.IsFullMetadataVisible(), "Viewer did not restore the persisted full metadata visibility");
         SendMessageW(restoredViewer.Hwnd(), WM_KEYDOWN, VK_TAB, 0);
         PumpMessagesFor(100);
         Expect(restoredViewer.AreInfoOverlaysVisible(), "Viewer Tab key did not restore the info overlays after reopening");
         SendMessageW(restoredViewer.Hwnd(), WM_CLOSE, 0, 0);
         PumpMessagesFor(100);
    }

    void RunAppTextSizeScenario(HINSTANCE instance)
    {
        using hyperbrowse::util::AppTextSize;

        Expect(hyperbrowse::util::AppTextSizeScale(AppTextSize::Small) == 0.90f,
               "Small app text size did not use the configured scale factor");
        Expect(hyperbrowse::util::AppTextSizeScale(AppTextSize::Medium) == 1.0f,
               "Medium app text size did not preserve the baseline scale factor");
        Expect(hyperbrowse::util::AppTextSizeScale(AppTextSize::Large) == 1.15f,
               "Large app text size did not use the configured scale factor");
        Expect(hyperbrowse::util::NormalizeAppTextSize(99) == AppTextSize::Medium,
               "Invalid app text size values did not normalize to Medium");
        Expect(hyperbrowse::util::ScaleAppTextDimension(100, AppTextSize::Small) == 90,
               "Small app text dimensions were not rounded as configured");
        Expect(hyperbrowse::util::ScaleAppTextDimension(100, AppTextSize::Large) == 115,
               "Large app text dimensions were not rounded as configured");

        ScopedRegistryDwordBackup appTextSizeBackup(kRegistryPath, kRegistryValueAppTextSize);
        ScopedRegistryDwordBackup overlayTextSizeBackup(kRegistryPath, kRegistryValueViewerInfoOverlayTextSize);
        ScopedRegistryDwordBackup escapeKeyBehaviorBackup(kRegistryPath, kRegistryValueViewerEscapeKeyBehavior);
        DeleteRegistryValue(kRegistryPath, kRegistryValueAppTextSize);
        SetRegistryDwordValue(kRegistryPath,
                              kRegistryValueViewerInfoOverlayTextSize,
                              static_cast<DWORD>(hyperbrowse::viewer::InfoOverlayTextSize::Large));
        SetRegistryDwordValue(kRegistryPath,
                              kRegistryValueViewerEscapeKeyBehavior,
                              static_cast<DWORD>(hyperbrowse::viewer::EscapeKeyBehavior::FitHeight));
        {
            hyperbrowse::ui::MainWindow mainWindow(instance);
            Expect(mainWindow.Create(), "Failed to create MainWindow for app text-size smoke coverage");

            Expect(mainWindow.ViewerEscapeKeyBehavior() == hyperbrowse::viewer::EscapeKeyBehavior::FitHeight,
                   "Viewer fullscreen Escape behavior did not restore its persisted setting");

            Expect(mainWindow.AppTextSize() == AppTextSize::Medium,
                   "App text size did not default to Medium when no value was persisted");

            HWND treeView = FindWindowExW(mainWindow.Hwnd(), nullptr, WC_TREEVIEWW, nullptr);
            Expect(treeView != nullptr, "MainWindow did not create a tree view for app text-size smoke coverage");
            HFONT mediumFont = reinterpret_cast<HFONT>(SendMessageW(treeView, WM_GETFONT, 0, 0));
            LOGFONTW mediumLogFont{};
            Expect(mediumFont != nullptr
                       && GetObjectW(mediumFont, sizeof(mediumLogFont), &mediumLogFont) == sizeof(mediumLogFont),
                   "Could not inspect the Medium app UI font");

            constexpr UINT smallCommand = 2223;
            constexpr UINT mediumCommand = 2224;
            constexpr UINT largeCommand = 2225;

            SendMessageW(mainWindow.Hwnd(), WM_COMMAND, MAKEWPARAM(largeCommand, 0), 0);
            Expect(mainWindow.AppTextSize() == AppTextSize::Large,
                   "Large app text size command was not applied");
            DWORD overlayTextSize = 0;
            Expect(TryReadRegistryDwordValue(kRegistryPath,
                                             kRegistryValueViewerInfoOverlayTextSize,
                                             &overlayTextSize)
                       && overlayTextSize == static_cast<DWORD>(hyperbrowse::viewer::InfoOverlayTextSize::Large),
                   "App text size command changed the independent viewer overlay text-size preference");
            HFONT largeFont = reinterpret_cast<HFONT>(SendMessageW(treeView, WM_GETFONT, 0, 0));
            LOGFONTW largeLogFont{};
            Expect(largeFont != nullptr
                       && GetObjectW(largeFont, sizeof(largeLogFont), &largeLogFont) == sizeof(largeLogFont),
                   "Could not inspect the Large app UI font");
            Expect(-largeLogFont.lfHeight > -mediumLogFont.lfHeight,
                   "Large app text size did not replace the native control font");

            SendMessageW(mainWindow.Hwnd(), WM_COMMAND, MAKEWPARAM(smallCommand, 0), 0);
            Expect(mainWindow.AppTextSize() == AppTextSize::Small,
                   "Small app text size command was not applied");
            HFONT smallFont = reinterpret_cast<HFONT>(SendMessageW(treeView, WM_GETFONT, 0, 0));
            LOGFONTW smallLogFont{};
            Expect(smallFont != nullptr
                       && GetObjectW(smallFont, sizeof(smallLogFont), &smallLogFont) == sizeof(smallLogFont),
                   "Could not inspect the Small app UI font");
            Expect(-smallLogFont.lfHeight < -mediumLogFont.lfHeight,
                   "Small app text size did not replace the native control font");

            SendMessageW(mainWindow.Hwnd(), WM_COMMAND, MAKEWPARAM(mediumCommand, 0), 0);
            Expect(mainWindow.AppTextSize() == AppTextSize::Medium,
                   "Medium app text size command was not applied");
            DestroyWindow(mainWindow.Hwnd());
            PumpMessagesFor(100);
        }

        SetRegistryDwordValue(kRegistryPath, kRegistryValueAppTextSize, static_cast<DWORD>(AppTextSize::Large));
        {
            hyperbrowse::ui::MainWindow restoredMainWindow(instance);
            Expect(restoredMainWindow.Create(), "Failed to recreate MainWindow for app text-size persistence coverage");
            Expect(restoredMainWindow.AppTextSize() == AppTextSize::Large,
                   "App text size did not persist across MainWindow recreation");
            DestroyWindow(restoredMainWindow.Hwnd());
            PumpMessagesFor(100);
        }

        SetRegistryDwordValue(kRegistryPath, kRegistryValueAppTextSize, 99);
        {
            hyperbrowse::ui::MainWindow normalizedMainWindow(instance);
            Expect(normalizedMainWindow.Create(), "Failed to create MainWindow for invalid app text-size coverage");
            Expect(normalizedMainWindow.AppTextSize() == AppTextSize::Medium,
                   "MainWindow did not normalize an invalid persisted app text size");
            DestroyWindow(normalizedMainWindow.Hwnd());
            PumpMessagesFor(100);
        }
    }

        void RunMainWindowCascadeScenario(HINSTANCE instance)
        {
         ScopedRegistryDwordBackup windowLeftBackup(kRegistryPath, kRegistryValueWindowLeft);
         ScopedRegistryDwordBackup windowTopBackup(kRegistryPath, kRegistryValueWindowTop);
         ScopedRegistryDwordBackup windowWidthBackup(kRegistryPath, kRegistryValueWindowWidth);
         ScopedRegistryDwordBackup windowHeightBackup(kRegistryPath, kRegistryValueWindowHeight);

         const HMONITOR monitor = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTONEAREST);
         MONITORINFO monitorInfo{};
         monitorInfo.cbSize = sizeof(monitorInfo);
         Expect(monitor != nullptr && GetMonitorInfoW(monitor, &monitorInfo),
             "Could not read a monitor work area for main-window cascade coverage");

         const LONG workAreaWidth = monitorInfo.rcWork.right - monitorInfo.rcWork.left;
         const LONG workAreaHeight = monitorInfo.rcWork.bottom - monitorInfo.rcWork.top;
         const LONG persistedWidth = workAreaWidth - 128;
         const LONG persistedHeight = workAreaHeight - 128;
         Expect(persistedWidth >= 960 && persistedHeight >= 640,
             "The monitor work area is too small for main-window cascade coverage");

         SetRegistryDwordValue(kRegistryPath, kRegistryValueWindowLeft, static_cast<DWORD>(monitorInfo.rcWork.left));
         SetRegistryDwordValue(kRegistryPath, kRegistryValueWindowTop, static_cast<DWORD>(monitorInfo.rcWork.top));
         SetRegistryDwordValue(kRegistryPath, kRegistryValueWindowWidth, static_cast<DWORD>(persistedWidth));
         SetRegistryDwordValue(kRegistryPath, kRegistryValueWindowHeight, static_cast<DWORD>(persistedHeight));

         hyperbrowse::ui::MainWindow firstWindow(instance);
         hyperbrowse::ui::MainWindow secondWindow(instance);
         Expect(firstWindow.Create(), "Failed to create the first MainWindow for cascade coverage");
         Expect(secondWindow.Create(), "Failed to create the second MainWindow for cascade coverage");

         RECT firstRect{};
         RECT secondRect{};
         Expect(GetWindowRect(firstWindow.Hwnd(), &firstRect) != FALSE,
             "Could not read the first MainWindow rectangle for cascade coverage");
         Expect(GetWindowRect(secondWindow.Hwnd(), &secondRect) != FALSE,
             "Could not read the second MainWindow rectangle for cascade coverage");
         Expect(firstRect.right - firstRect.left == secondRect.right - secondRect.left
                 && firstRect.bottom - firstRect.top == secondRect.bottom - secondRect.top,
             "Cascaded MainWindows did not retain the persisted size");
         Expect(firstRect.left != secondRect.left || firstRect.top != secondRect.top,
             "A second MainWindow opened on top of the first");

         DestroyWindow(secondWindow.Hwnd());
         PumpMessagesFor(100);
         DestroyWindow(firstWindow.Hwnd());
         PumpMessagesFor(100);
        }

    void RunDefaultSettingsScenario(HINSTANCE instance)
    {
        using hyperbrowse::ui::command_ids::ID_VIEW_SETTINGS;

        constexpr wchar_t kDialogClassName[] = L"HyperBrowseExperimentalSettingsDialog";
        constexpr wchar_t kSettingsUiEnvironment[] = L"HYPERBROWSE_SETTINGS_UI";
        ScopedRegistryDwordBackup appTextSizeBackup(kRegistryPath, kRegistryValueAppTextSize);
        ScopedRegistryDwordBackup thumbnailCacheBackup(kRegistryPath, kRegistryValueThumbnailCacheCapacityOverrideBytes);
        ScopedRegistryDwordBackup metadataCacheBackup(kRegistryPath, kRegistryValueMetadataCacheCapacityOverrideEntries);
        DeleteRegistryValue(kRegistryPath, kRegistryValueAppTextSize);
        DeleteRegistryValue(kRegistryPath, kRegistryValueThumbnailCacheCapacityOverrideBytes);
        DeleteRegistryValue(kRegistryPath, kRegistryValueMetadataCacheCapacityOverrideEntries);
        wchar_t previousValue[64]{};
        const DWORD previousLength = GetEnvironmentVariableW(
            kSettingsUiEnvironment,
            previousValue,
            static_cast<DWORD>(std::size(previousValue)));
        SetEnvironmentVariableW(kSettingsUiEnvironment, nullptr);

        hyperbrowse::ui::MainWindow mainWindow(instance);
        Expect(mainWindow.Create(), "Failed to create MainWindow for experimental-settings smoke coverage");
        std::atomic_bool done{false};
        std::string failure;
        std::thread worker([&]()
        {
            if (!PostMessageW(mainWindow.Hwnd(), WM_COMMAND, MAKEWPARAM(ID_VIEW_SETTINGS, 0), 0))
            {
                failure = "Failed to post the experimental Settings command";
                done.store(true, std::memory_order_release);
                return;
            }

            HWND dialog = nullptr;
            const ULONGLONG deadline = GetTickCount64() + 10000;
            while (GetTickCount64() < deadline && !(dialog = FindWindowW(kDialogClassName, nullptr)))
            {
                Sleep(10);
            }
            if (!dialog)
            {
                failure = "Experimental Settings dialog did not open";
                done.store(true, std::memory_order_release);
                return;
            }
            const auto failAndClose = [&](std::string message)
            {
                failure = std::move(message);
                SendMessageW(dialog, WM_CLOSE, 0, 0);
                done.store(true, std::memory_order_release);
            };
            if (GetDlgItem(dialog, 5601) != nullptr)
            {
                failAndClose("Experimental Settings unexpectedly created a native OK button");
                return;
            }
            HWND transitionCombo = nullptr;
            HWND overlayTextCombo = nullptr;
            HWND appTextCombo = nullptr;
            HWND thumbnailCombo = nullptr;
            HWND resourceCombo = nullptr;
            HWND escapeBehaviorCombo = nullptr;
            const ULONGLONG controlsDeadline = GetTickCount64() + 10000;
            while (GetTickCount64() < controlsDeadline)
            {
                transitionCombo = GetDlgItem(dialog, 5801);
                overlayTextCombo = GetDlgItem(dialog, 5815);
                appTextCombo = GetDlgItem(dialog, 5818);
                thumbnailCombo = GetDlgItem(dialog, 5819);
                resourceCombo = GetDlgItem(dialog, 5823);
                escapeBehaviorCombo = GetDlgItem(dialog, 5836);
                if (transitionCombo && overlayTextCombo && appTextCombo && thumbnailCombo && resourceCombo && escapeBehaviorCombo
                    && SendMessageW(transitionCombo, CB_GETCOUNT, 0, 0) >= 2
                    && SendMessageW(overlayTextCombo, CB_GETCOUNT, 0, 0) == 3
                    && SendMessageW(appTextCombo, CB_GETCOUNT, 0, 0) == 3
                    && SendMessageW(thumbnailCombo, CB_GETCOUNT, 0, 0) >= 2
                    && SendMessageW(resourceCombo, CB_GETCOUNT, 0, 0) == 4
                    && SendMessageW(escapeBehaviorCombo, CB_GETCOUNT, 0, 0) == 4)
                {
                    break;
                }
                Sleep(10);
            }
            if (!transitionCombo || !overlayTextCombo || !appTextCombo || !thumbnailCombo || !resourceCombo || !escapeBehaviorCombo
                || SendMessageW(transitionCombo, CB_GETCOUNT, 0, 0) < 2
                || SendMessageW(overlayTextCombo, CB_GETCOUNT, 0, 0) != 3
                || SendMessageW(appTextCombo, CB_GETCOUNT, 0, 0) != 3
                || SendMessageW(thumbnailCombo, CB_GETCOUNT, 0, 0) < 2
                || SendMessageW(resourceCombo, CB_GETCOUNT, 0, 0) != 4
                || SendMessageW(escapeBehaviorCombo, CB_GETCOUNT, 0, 0) != 4)
            {
                failAndClose("Experimental Settings did not create/populate all native combo boxes (transition="
                    + std::to_string(transitionCombo != nullptr) + ", overlay="
                    + std::to_string(overlayTextCombo != nullptr) + ", app="
                    + std::to_string(appTextCombo != nullptr) + ", thumbnail="
                    + std::to_string(thumbnailCombo != nullptr) + ", resource="
                    + std::to_string(resourceCombo != nullptr) + ", escape="
                    + std::to_string(escapeBehaviorCombo != nullptr) + ")");
                return;
            }
            SendMessageW(transitionCombo, CB_SHOWDROPDOWN, TRUE, 0);
            if (SendMessageW(transitionCombo, CB_GETDROPPEDSTATE, 0, 0) == FALSE)
            {
                failAndClose("Experimental Settings transition combo did not open a dropdown list");
                return;
            }
            SendMessageW(transitionCombo, CB_SHOWDROPDOWN, FALSE, 0);

            const HWND slideDurationEdit = GetDlgItem(dialog, 5700);
            const HWND transitionDurationEdit = GetDlgItem(dialog, 5701);
            const HWND thumbnailCacheEdit = GetDlgItem(dialog, 5702);
            const HWND metadataCacheEdit = GetDlgItem(dialog, 5703);
            const HWND slideDurationSpin = GetDlgItem(dialog, 5850);
            const HWND transitionDurationSpin = GetDlgItem(dialog, 5851);
            const HWND thumbnailCacheSpin = GetDlgItem(dialog, 5852);
            const HWND metadataCacheSpin = GetDlgItem(dialog, 5853);
            if (!slideDurationEdit || !transitionDurationEdit || !thumbnailCacheEdit || !metadataCacheEdit
                || !slideDurationSpin || !transitionDurationSpin || !thumbnailCacheSpin || !metadataCacheSpin
                || reinterpret_cast<HWND>(SendMessageW(slideDurationSpin, UDM_GETBUDDY, 0, 0)) != slideDurationEdit
                || reinterpret_cast<HWND>(SendMessageW(transitionDurationSpin, UDM_GETBUDDY, 0, 0)) != transitionDurationEdit
                || reinterpret_cast<HWND>(SendMessageW(thumbnailCacheSpin, UDM_GETBUDDY, 0, 0)) != thumbnailCacheEdit
                || reinterpret_cast<HWND>(SendMessageW(metadataCacheSpin, UDM_GETBUDDY, 0, 0)) != metadataCacheEdit)
            {
                failAndClose("Experimental Settings numeric fields did not create native spin buddies");
                return;
            }
            int slideMinimum = 0;
            int slideMaximum = 0;
            SendMessageW(slideDurationSpin, UDM_GETRANGE32, reinterpret_cast<WPARAM>(&slideMinimum), reinterpret_cast<LPARAM>(&slideMaximum));
            if (slideMinimum != 250 || slideMaximum != 60000)
            {
                failAndClose("Experimental Settings duration spin range was not configured");
                return;
            }
            const auto readControlText = [](HWND control)
            {
                wchar_t buffer[64]{};
                GetWindowTextW(control, buffer, static_cast<int>(std::size(buffer)));
                return std::wstring(buffer);
            };
            const auto selectedProfile = static_cast<hyperbrowse::util::ResourceProfile>(
                SendMessageW(resourceCombo, CB_GETCURSEL, 0, 0));
            const std::wstring expectedThumbnailCache = std::to_wstring(
                hyperbrowse::services::ThumbnailScheduler::ResolveCacheCapacityBytes(0, selectedProfile)
                / (1024ULL * 1024ULL));
            const std::wstring expectedMetadataCache = std::to_wstring(
                hyperbrowse::services::ImageMetadataService::ResolveCacheCapacityEntries(0, selectedProfile));
            if (readControlText(thumbnailCacheEdit) != expectedThumbnailCache
                || readControlText(metadataCacheEdit) != expectedMetadataCache)
            {
                failAndClose("Experimental Settings did not populate automatic cache values from the selected profile");
                return;
            }
            SendMessageW(resourceCombo, CB_SETCURSEL, 0, 0);
            SendMessageW(dialog, WM_COMMAND, MAKEWPARAM(5823, CBN_SELCHANGE), reinterpret_cast<LPARAM>(resourceCombo));
            const std::wstring expectedConservativeThumbnailCache = std::to_wstring(
                hyperbrowse::services::ThumbnailScheduler::ResolveCacheCapacityBytes(
                    0,
                    hyperbrowse::util::ResourceProfile::Conservative)
                / (1024ULL * 1024ULL));
            const std::wstring expectedConservativeMetadataCache = std::to_wstring(
                hyperbrowse::services::ImageMetadataService::ResolveCacheCapacityEntries(
                    0,
                    hyperbrowse::util::ResourceProfile::Conservative));
            if (readControlText(thumbnailCacheEdit) != expectedConservativeThumbnailCache
                || readControlText(metadataCacheEdit) != expectedConservativeMetadataCache)
            {
                failAndClose("Experimental Settings did not refresh automatic cache values after changing profile");
                return;
            }

            RECT client{};
            GetClientRect(dialog, &client);
            const int tabWidth = (client.right - 56) / 5;
            for (int index = 0; index < 5; ++index)
            {
                const int x = 28 + (index * tabWidth) + (tabWidth / 2);
                SendMessageW(dialog, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, 36));
                SendMessageW(dialog, WM_LBUTTONUP, 0, MAKELPARAM(x, 36));
                PumpMessagesFor(20);
                if (!FindWindowW(kDialogClassName, nullptr))
                {
                    failAndClose("Experimental Settings closed while switching pages");
                    return;
                }
            }

            const int appearanceTabX = 28 + (2 * tabWidth) + (tabWidth / 2);
            SendMessageW(dialog, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(appearanceTabX, 36));
            SendMessageW(dialog, WM_LBUTTONUP, 0, MAKELPARAM(appearanceTabX, 36));
            SendMessageW(appTextCombo, CB_SETCURSEL, 2, 0);
            SendMessageW(dialog, WM_COMMAND, MAKEWPARAM(5818, CBN_SELCHANGE), reinterpret_cast<LPARAM>(appTextCombo));
            SendMessageW(dialog, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(client.right - 300, client.bottom - 46));
            SendMessageW(dialog, WM_LBUTTONUP, 0, MAKELPARAM(client.right - 300, client.bottom - 46));
            PumpMessagesFor(100);
            if (mainWindow.AppTextSize() != hyperbrowse::util::AppTextSize::Large || !FindWindowW(kDialogClassName, nullptr))
            {
                failAndClose("Experimental Settings Apply did not update the live setting while keeping the dialog open");
                return;
            }
            SendMessageW(appTextCombo, CB_SETCURSEL, 1, 0);
            SendMessageW(dialog, WM_COMMAND, MAKEWPARAM(5818, CBN_SELCHANGE), reinterpret_cast<LPARAM>(appTextCombo));
            if (!PostMessageW(dialog, WM_KEYDOWN, VK_RETURN, 0))
            {
                failAndClose("Failed to post Enter to Experimental Settings");
                return;
            }
            const ULONGLONG enterDeadline = GetTickCount64() + 10000;
            while (GetTickCount64() < enterDeadline && FindWindowW(kDialogClassName, nullptr))
            {
                Sleep(10);
            }
            if (FindWindowW(kDialogClassName, nullptr)
                || mainWindow.AppTextSize() != hyperbrowse::util::AppTextSize::Medium)
            {
                failure = "Experimental Settings Enter did not apply and close the dialog";
                done.store(true, std::memory_order_release);
                return;
            }

            if (!PostMessageW(mainWindow.Hwnd(), WM_COMMAND, MAKEWPARAM(ID_VIEW_SETTINGS, 0), 0))
            {
                failure = "Failed to reopen Experimental Settings for Escape coverage";
                done.store(true, std::memory_order_release);
                return;
            }
            dialog = nullptr;
            const ULONGLONG reopenDeadline = GetTickCount64() + 10000;
            while (GetTickCount64() < reopenDeadline && !(dialog = FindWindowW(kDialogClassName, nullptr)))
            {
                Sleep(10);
            }
            if (!dialog)
            {
                failure = "Experimental Settings did not reopen for Escape coverage";
                done.store(true, std::memory_order_release);
                return;
            }
            if (!PostMessageW(dialog, WM_KEYDOWN, VK_ESCAPE, 0))
            {
                failure = "Failed to post Escape to Experimental Settings";
                SendMessageW(dialog, WM_CLOSE, 0, 0);
                done.store(true, std::memory_order_release);
                return;
            }
            const ULONGLONG escapeDeadline = GetTickCount64() + 10000;
            while (GetTickCount64() < escapeDeadline && FindWindowW(kDialogClassName, nullptr))
            {
                Sleep(10);
            }
            if (FindWindowW(kDialogClassName, nullptr))
            {
                failure = "Experimental Settings Escape did not close the dialog";
            }
            done.store(true, std::memory_order_release);
        });

        Expect(PumpMessagesUntil([&]() { return done.load(std::memory_order_acquire); }, 15000),
               "Experimental Settings interaction timed out");
        worker.join();
        Expect(failure.empty(), failure.empty() ? "Experimental Settings interaction failed" : failure.c_str());
        DestroyWindow(mainWindow.Hwnd());
        PumpMessagesFor(100);

        if (previousLength > 0)
        {
            SetEnvironmentVariableW(kSettingsUiEnvironment, previousValue);
        }
        else
        {
            SetEnvironmentVariableW(kSettingsUiEnvironment, nullptr);
        }
    }

    void RunMainWindowFolderTreeScenario(HINSTANCE instance)
    {
         const std::vector<std::wstring> expectedSpecialRoots = ExpectedSpecialFolderRootTexts();
         std::wstring persistedDriveRootText;

         {
             hyperbrowse::ui::MainWindow mainWindow(instance);
             Expect(mainWindow.Create(), "Failed to create the MainWindow for the folder-tree scenario");

             HWND mainWindowHandle = FindWindowW(L"HyperBrowseMainWindow", nullptr);
             Expect(mainWindowHandle != nullptr, "Failed to find the created MainWindow instance");

             HWND treeView = FindWindowExW(mainWindowHandle, nullptr, WC_TREEVIEWW, nullptr);
             Expect(treeView != nullptr, "MainWindow did not create the folder tree control");

             HTREEITEM rootItem = TreeView_GetRoot(treeView);
             Expect(rootItem != nullptr, "Folder tree did not populate any root items");

             HTREEITEM currentRoot = rootItem;
             for (const std::wstring& expectedRootText : expectedSpecialRoots)
             {
              Expect(currentRoot != nullptr, "Folder tree did not place special folders ahead of drive roots");
              Expect(ReadTreeItemText(treeView, currentRoot) == expectedRootText,
                  "Folder tree did not insert Desktop/Documents/Pictures above the drive roots in the expected order");
              currentRoot = TreeView_GetNextSibling(treeView, currentRoot);
             }

             Expect(currentRoot != nullptr, "Folder tree did not include any drive roots after the special folders");
             persistedDriveRootText = ReadTreeItemText(treeView, currentRoot);
             Expect(persistedDriveRootText.find(L"Open Folder") == std::wstring::npos,
                 "Folder tree still shows the old placeholder prompt instead of filesystem roots");

             TreeView_Expand(treeView, currentRoot, TVE_EXPAND);
             TreeView_SelectItem(treeView, currentRoot);
             PumpMessagesFor(300);

             wchar_t title[512]{};
             GetWindowTextW(mainWindowHandle, title, static_cast<int>(std::size(title)));
             Expect(std::wstring(title).find(L":\\") != std::wstring::npos,
                 "Selecting a tree node did not route the main window to a concrete filesystem folder");

             DestroyWindow(mainWindowHandle);
             PumpMessagesFor(100);
         }

         {
             hyperbrowse::ui::MainWindow restoredMainWindow(instance);
             Expect(restoredMainWindow.Create(), "Failed to recreate the MainWindow for persistence verification");

             HWND restoredHandle = FindWindowW(L"HyperBrowseMainWindow", nullptr);
             Expect(restoredHandle != nullptr, "Failed to find the recreated MainWindow instance");

             HWND restoredTreeView = FindWindowExW(restoredHandle, nullptr, WC_TREEVIEWW, nullptr);
             Expect(restoredTreeView != nullptr, "Recreated MainWindow did not create the folder tree control");

             PumpMessagesFor(300);
             const HTREEITEM selectedItem = TreeView_GetSelection(restoredTreeView);
             Expect(selectedItem != nullptr, "MainWindow did not restore any tree selection from the previous session");
             Expect(ReadTreeItemText(restoredTreeView, selectedItem) == persistedDriveRootText,
                 "MainWindow did not restore the previously selected folder tree item on startup");

             wchar_t title[512]{};
             GetWindowTextW(restoredHandle, title, static_cast<int>(std::size(title)));
             Expect(std::wstring(title).find(L":\\") != std::wstring::npos,
                 "MainWindow did not reload the persisted folder selection on startup");

             DestroyWindow(restoredHandle);
             PumpMessagesFor(100);
         }
    }

    void RunStartupViewerEnumerationScenario(HINSTANCE instance)
    {
        TempFolder root(L"HyperBrowseStartupViewerEnumeration");
        ScopedRegistryDwordBackup escapeKeyBehaviorBackup(kRegistryPath, kRegistryValueViewerEscapeKeyBehavior);
        SetRegistryDwordValue(kRegistryPath,
                              kRegistryValueViewerEscapeKeyBehavior,
                              static_cast<DWORD>(hyperbrowse::viewer::EscapeKeyBehavior::FitHeight));
        const fs::path targetPath = root.Root() / L"000-target.jpg";
        WriteTestImage(targetPath, TestImageFormat::Jpeg, 48, 24, 6);
        for (int index = 1; index < 40; ++index)
        {
            WriteTestImage(root.Root() / (L"image_" + std::to_wstring(index) + L".jpg"),
                           TestImageFormat::Jpeg,
                           48,
                           24,
                           6);
        }

        hyperbrowse::ui::MainWindow mainWindow(instance);
        mainWindow.SetStartupLaunchPath(targetPath.wstring());
        Expect(mainWindow.Create(), "Failed to create MainWindow for startup viewer enumeration coverage");

        HWND viewerHandle = nullptr;
        const bool openedWithCompleteFolder = PumpMessagesUntil([&]()
        {
            viewerHandle = FindWindowW(L"HyperBrowseViewerWindow", nullptr);
            if (!viewerHandle)
            {
                return false;
            }

            wchar_t title[512]{};
            GetWindowTextW(viewerHandle, title, static_cast<int>(std::size(title)));
            return std::wstring(title).find(L"/40)") != std::wstring::npos;
        }, 5000);
        Expect(openedWithCompleteFolder,
               "Viewer opened from a startup launch path with only the initial enumeration batch");

         Expect(mainWindow.ViewerEscapeKeyBehavior() == hyperbrowse::viewer::EscapeKeyBehavior::FitHeight,
             "Startup viewer did not receive the persisted Escape behavior");
         MONITORINFO monitorInfo{sizeof(MONITORINFO)};
         const HMONITOR monitor = MonitorFromWindow(mainWindow.Hwnd(), MONITOR_DEFAULTTONEAREST);
         Expect(monitor != nullptr && GetMonitorInfoW(monitor, &monitorInfo) != FALSE,
             "Failed to read the monitor work area for startup viewer Escape coverage");
         MSG escapeMessage{};
         escapeMessage.hwnd = mainWindow.Hwnd();
         escapeMessage.message = WM_KEYDOWN;
         escapeMessage.wParam = VK_ESCAPE;
         Expect(mainWindow.TranslateAcceleratorMessage(&escapeMessage),
             "MainWindow did not consume the forwarded startup viewer Escape message");
         PumpMessagesFor(100);
         RECT escapeRect{};
         Expect(viewerHandle && GetWindowRect(viewerHandle, &escapeRect) != FALSE,
             "MainWindow Escape routing left no readable viewer window");
         Expect(escapeRect.top == monitorInfo.rcWork.top && escapeRect.bottom == monitorInfo.rcWork.bottom,
             "MainWindow Escape routing did not apply the viewer's Fit Height behavior");

        if (viewerHandle && IsWindow(viewerHandle) != FALSE)
        {
            SendMessageW(viewerHandle, WM_CLOSE, 0, 0);
            PumpMessagesFor(100);
        }
        DestroyWindow(mainWindow.Hwnd());
        PumpMessagesFor(100);
    }
}

int main(int argc, char* argv[])
{
    try
    {
        ComScope comScope;
        HINSTANCE instance = GetModuleHandleW(nullptr);
        Expect(ConfigureSmokeSettingsRegistry(), "Failed to configure the smoke-test settings registry path");
        INITCOMMONCONTROLSEX commonControls{};
        commonControls.dwSize = sizeof(commonControls);
        commonControls.dwICC = ICC_LISTVIEW_CLASSES | ICC_TREEVIEW_CLASSES | ICC_BAR_CLASSES | ICC_STANDARD_CLASSES;
        InitCommonControlsEx(&commonControls);

        TestWindowState state{};
        HWND hwnd = CreateTestWindow(&state, instance);
        Expect(hwnd != nullptr, "Failed to create the hidden test window");

        const bool viewerFitOnly = argc > 1 && std::string_view(argv[1]) == "--viewer-fit";
        const bool appTextSizeOnly = argc > 1 && std::string_view(argv[1]) == "--app-text-size";
        const bool settingsOnly = argc > 1 && std::string_view(argv[1]) == "--settings";
        if (viewerFitOnly)
        {
            RunViewerWindowFitModeScenario(instance, hwnd);
        }
        else if (appTextSizeOnly)
        {
            RunAppTextSizeScenario(instance);
        }
        else if (settingsOnly)
        {
            RunDefaultSettingsScenario(instance);
        }
        else
        {
            RunShortcutCatalogScenario();
            RunBackgroundExecutorExceptionScenario();
            RunSingleInstanceIdleClientScenario();
            RunEnumerationScenario(hwnd, &state);
            RunFolderTreeEnumerationScenario(hwnd, &state);
            RunFolderWatchStartStopScenario(hwnd);
            RunFolderWatchNotificationParserScenario();
            RunThumbnailCacheNormalizationScenario();
            RunDiskThumbnailCacheCorruptionScenario();
            RunWicDecoderScenario();
            RunJpegOrientationAdjustmentScenario();
            RunBatchConvertCancellationScenario(hwnd);
            RunFileRenameOperationScenario(hwnd, &state);
            RunFileConflictPlanningScenario();
            RunThumbnailSchedulerWorkerAllocationScenario();
            RunThumbnailSchedulerScenario(hwnd, &state);
            RunThumbnailSchedulerFailureScenario(hwnd, &state);
            RunThumbnailFailureClassificationScenario();
            RunImageMetadataServiceScenario();
            RunSwarmUiMetadataExtractionScenario();
            RunRawFormatAllowlistScenario();
            RunRawHelperProtocolScenario();
            RunRawDecoderScenario();
            RunBrowserPaneScenario(instance);
            RunBrowserModelBulkRemovalScenario();
            RunQuickSendModelScenario();
            RunViewerWindowScenario(instance, hwnd);
            RunAppTextSizeScenario(instance);
            RunMainWindowCascadeScenario(instance);
            RunStartupViewerEnumerationScenario(instance);
            RunMainWindowFolderTreeScenario(instance);
        }

        DestroyWindow(hwnd);
        UnregisterClassW(kTestWindowClassName, instance);

        std::cout << "HyperBrowse thumbnail pipeline smoke tests passed\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "HyperBrowse smoke test failed: " << exception.what() << '\n';
        return 1;
    }
}
