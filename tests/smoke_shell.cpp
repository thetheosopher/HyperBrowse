#include "smoke_support.h"
#include "smoke_scenarios.h"

namespace hyperbrowse::tests::smoke
{
namespace
{

    struct DataObjectLifetimeState
    {
        bool destroyed{};
    };

    class TrackingDataObject final : public IDataObject
    {
    public:
        explicit TrackingDataObject(DataObjectLifetimeState* state)
            : state_(state)
        {
        }

        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override
        {
            if (!object)
            {
                return E_POINTER;
            }

            *object = nullptr;
            if (riid == IID_IUnknown || riid == IID_IDataObject)
            {
                *object = static_cast<IDataObject*>(this);
                AddRef();
                return S_OK;
            }
            return E_NOINTERFACE;
        }

        ULONG STDMETHODCALLTYPE AddRef() override
        {
            return ++refCount_;
        }

        ULONG STDMETHODCALLTYPE Release() override
        {
            const ULONG remaining = --refCount_;
            if (remaining == 0)
            {
                state_->destroyed = true;
                delete this;
            }
            return remaining;
        }

        HRESULT STDMETHODCALLTYPE GetData(FORMATETC*, STGMEDIUM*) override
        {
            return E_NOTIMPL;
        }

        HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC*, STGMEDIUM*) override
        {
            return E_NOTIMPL;
        }

        HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC*) override
        {
            return E_NOTIMPL;
        }

        HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC*, FORMATETC*) override
        {
            return E_NOTIMPL;
        }

        HRESULT STDMETHODCALLTYPE SetData(FORMATETC*, STGMEDIUM*, BOOL) override
        {
            return E_NOTIMPL;
        }

        HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD, IEnumFORMATETC**) override
        {
            return E_NOTIMPL;
        }

        HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override
        {
            return E_NOTIMPL;
        }

        HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) override
        {
            return E_NOTIMPL;
        }

        HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA**) override
        {
            return E_NOTIMPL;
        }

    private:
        DataObjectLifetimeState* state_{};
        ULONG refCount_{1};
    };
}


        void RunExternalDropTargetDataObjectLifetimeScenario()
        {
         DataObjectLifetimeState leaveState;
         auto* leaveDataObject = new TrackingDataObject(&leaveState);
         int dragOverCalls = 0;
         int dragLeaveCalls = 0;
         auto* leaveTarget = new hyperbrowse::ui::ExternalDropTarget(
             GetDesktopWindow(),
             [&](IDataObject* dataObject, DWORD, POINT)
             {
              ++dragOverCalls;
              Expect(dataObject != nullptr && !leaveState.destroyed,
                  "DragOver received a data object after its source reference was released");
              return DROPEFFECT_COPY;
             },
             [](IDataObject*, DWORD, POINT)
             {
              return DROPEFFECT_NONE;
             },
             [&]()
             {
              ++dragLeaveCalls;
             });

         DWORD effect = DROPEFFECT_NONE;
         Expect(SUCCEEDED(leaveTarget->DragEnter(leaveDataObject, 0, POINTL{0, 0}, &effect)),
             "ExternalDropTarget rejected DragEnter in the lifetime scenario");
         Expect(effect == DROPEFFECT_COPY && dragOverCalls == 1,
             "ExternalDropTarget did not invoke the initial drag-over callback");

         leaveDataObject->Release();
         Expect(!leaveState.destroyed,
             "ExternalDropTarget did not retain the data object after DragEnter returned");

         Expect(SUCCEEDED(leaveTarget->DragOver(0, POINTL{0, 0}, &effect)),
             "ExternalDropTarget rejected DragOver after the source released its reference");
         Expect(dragOverCalls == 2,
             "ExternalDropTarget did not invoke DragOver with its retained data object");

         Expect(SUCCEEDED(leaveTarget->DragLeave()), "ExternalDropTarget rejected DragLeave");
         Expect(dragLeaveCalls == 1 && leaveState.destroyed,
             "ExternalDropTarget did not release the retained data object on DragLeave");
         leaveTarget->Release();

         DataObjectLifetimeState dropState;
         auto* dropDataObject = new TrackingDataObject(&dropState);
         int dropCalls = 0;
         auto* dropTarget = new hyperbrowse::ui::ExternalDropTarget(
             GetDesktopWindow(),
             [&](IDataObject* dataObject, DWORD, POINT)
             {
              Expect(dataObject != nullptr && !dropState.destroyed,
                  "Drop scenario lost its data object during DragEnter");
              return DROPEFFECT_COPY;
             },
             [&](IDataObject* dataObject, DWORD, POINT)
             {
              ++dropCalls;
              Expect(dataObject == dropDataObject && !dropState.destroyed,
                  "Drop callback received an invalid data object");
              return DROPEFFECT_COPY;
             },
             []()
             {
             });

         Expect(SUCCEEDED(dropTarget->DragEnter(dropDataObject, 0, POINTL{0, 0}, &effect)),
             "ExternalDropTarget rejected DragEnter in the drop lifetime scenario");
         dropDataObject->Release();
         Expect(!dropState.destroyed,
             "ExternalDropTarget did not retain the data object before Drop");

         dropDataObject->AddRef();
         Expect(SUCCEEDED(dropTarget->Drop(dropDataObject, 0, POINTL{0, 0}, &effect)),
             "ExternalDropTarget rejected Drop in the lifetime scenario");
         Expect(dropCalls == 1 && !dropState.destroyed,
             "ExternalDropTarget released the data object before Drop completed");
         dropDataObject->Release();
         Expect(dropState.destroyed,
             "ExternalDropTarget did not release the retained data object on Drop");
         dropTarget->Release();
        }

    void RunSingleInstanceIdleClientScenario()
    {
        constexpr wchar_t kMainWindowClassName[] = L"HyperBrowseMainWindow";
        const std::wstring testNamespace = L"Smoke-" + std::to_wstring(GetCurrentProcessId());
        const std::wstring singleInstancePipeName = L"\\\\.\\pipe\\TheTheosopher.HyperBrowse.Launch." + testNamespace;

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

        std::wstring commandLine = L"\"" + applicationPath.wstring()
            + L"\" --test-single-instance-isolated=" + testNamespace;
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
            pipe = CreateFileW(singleInstancePipeName.c_str(),
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
            WaitNamedPipeW(singleInstancePipeName.c_str(), 100);
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
        const DWORD shutdownWait = WaitForSingleObject(processInfo.hProcess, 5000);
        if (shutdownWait != WAIT_OBJECT_0)
        {
            const bool windowExists = IsWindow(mainWindow) != FALSE;
            const bool windowVisible = IsWindowVisible(mainWindow) != FALSE;
            const bool residentEnabled = hyperbrowse::app::Application::IsKeepInNotificationAreaEnabled();
            cleanup();
            throw std::runtime_error(
            "An idle single-instance client prevented HyperBrowse from shutting down"
            " (window exists=" + std::to_string(windowExists)
            + ", visible=" + std::to_string(windowVisible)
            + ", resident=" + std::to_string(residentEnabled)
            + ", wait=" + std::to_string(shutdownWait) + ")");
        }

        DWORD exitCode = 1;
        require(GetExitCodeProcess(processInfo.hProcess, &exitCode) != FALSE && exitCode == 0,
                "HyperBrowse exited unsuccessfully during idle-client testing");
        cleanup();
    }

    void RunResidentSingleInstanceScenario()
    {
        constexpr wchar_t kMainWindowClassName[] = L"HyperBrowseMainWindow";
        constexpr wchar_t kTrayMessageName[] = L"TheTheosopher.HyperBrowse.TrayIcon";
        const std::wstring testNamespace = L"Smoke-" + std::to_wstring(GetCurrentProcessId());
        const std::wstring singleInstancePipeName = L"\\\\.\\pipe\\TheTheosopher.HyperBrowse.Launch." + testNamespace;
        const std::wstring isolatedSingleInstanceArgument = L"--test-single-instance-isolated=" + testNamespace;

        using hyperbrowse::ui::command_ids::ID_FILE_EXIT;

        ScopedRegistryDwordBackup singleInstanceBackup(kRegistryPath, kRegistryValueSingleInstanceEnabled);
        ScopedRegistryDwordBackup keepInNotificationAreaBackup(kRegistryPath, kRegistryValueKeepInNotificationAreaEnabled);
        hyperbrowse::app::Application::SetSingleInstanceEnabled(true);
        hyperbrowse::app::Application::SetKeepInNotificationAreaEnabled(true);

        wchar_t modulePath[MAX_PATH]{};
        const DWORD modulePathLength = GetModuleFileNameW(nullptr, modulePath, static_cast<DWORD>(std::size(modulePath)));
        Expect(modulePathLength > 0 && modulePathLength < std::size(modulePath),
               "Failed to locate the smoke-test executable for resident-mode testing");

        const fs::path testDirectory = fs::path(modulePath).parent_path();
        const fs::path applicationPath = testDirectory.parent_path().parent_path()
            / testDirectory.filename()
            / L"HyperBrowse.exe";
        Expect(fs::exists(applicationPath), "Failed to locate the HyperBrowse executable for resident-mode testing");

        PROCESS_INFORMATION processInfo{};
        HANDLE pipe = INVALID_HANDLE_VALUE;
        const auto cleanupProcess = [](PROCESS_INFORMATION* process)
        {
            if (!process)
            {
                return;
            }
            if (process->hProcess)
            {
                if (WaitForSingleObject(process->hProcess, 0) == WAIT_TIMEOUT)
                {
                    TerminateProcess(process->hProcess, 1);
                    WaitForSingleObject(process->hProcess, 5000);
                }
                CloseHandle(process->hProcess);
                process->hProcess = nullptr;
            }
            if (process->hThread)
            {
                CloseHandle(process->hThread);
                process->hThread = nullptr;
            }
        };
        const auto cleanup = [&]()
        {
            if (pipe != INVALID_HANDLE_VALUE)
            {
                CloseHandle(pipe);
                pipe = INVALID_HANDLE_VALUE;
            }
            cleanupProcess(&processInfo);
        };
        const auto require = [&](bool condition, const char* message)
        {
            if (!condition)
            {
                cleanup();
                throw std::runtime_error(message);
            }
        };

        std::wstring primaryCommandLine = L"\"" + applicationPath.wstring() + L"\" " + isolatedSingleInstanceArgument;
        std::vector<wchar_t> mutablePrimaryCommandLine(primaryCommandLine.begin(), primaryCommandLine.end());
        mutablePrimaryCommandLine.push_back(L'\0');
        STARTUPINFOW startupInfo{};
        startupInfo.cb = sizeof(startupInfo);
        Expect(CreateProcessW(applicationPath.c_str(),
                              mutablePrimaryCommandLine.data(),
                              nullptr,
                              nullptr,
                              FALSE,
                              0,
                              nullptr,
                              applicationPath.parent_path().c_str(),
                              &startupInfo,
                              &processInfo) != FALSE,
               "Failed to launch HyperBrowse for resident-mode testing");

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
        require(mainWindow != nullptr, "HyperBrowse did not create its main window for resident-mode testing");

        const auto waitForVisibility = [&](bool visible)
        {
            const ULONGLONG deadline = GetTickCount64() + 5000;
            while (GetTickCount64() < deadline)
            {
                if (IsWindow(mainWindow) != FALSE && (IsWindowVisible(mainWindow) != FALSE) == visible)
                {
                    return true;
                }
                Sleep(25);
            }
            return IsWindow(mainWindow) != FALSE && (IsWindowVisible(mainWindow) != FALSE) == visible;
        };
        const auto sendLaunchPath = [&](const std::wstring& path)
        {
            HANDLE launchPipe = INVALID_HANDLE_VALUE;
            DWORD pipeError = ERROR_SUCCESS;
            const ULONGLONG deadline = GetTickCount64() + 10000;
            while (GetTickCount64() < deadline)
            {
                launchPipe = CreateFileW(singleInstancePipeName.c_str(),
                                         FILE_WRITE_DATA,
                                         0,
                                         nullptr,
                                         OPEN_EXISTING,
                                         0,
                                         nullptr);
                if (launchPipe != INVALID_HANDLE_VALUE)
                {
                    break;
                }
                pipeError = GetLastError();
                if (pipeError != ERROR_PIPE_BUSY && pipeError != ERROR_FILE_NOT_FOUND)
                {
                    break;
                }
                WaitNamedPipeW(singleInstancePipeName.c_str(), 100);
            }
            Expect(launchPipe != INVALID_HANDLE_VALUE,
                   "Failed to connect to the resident HyperBrowse instance (Win32 error "
                       + std::to_string(pipeError) + ")");
            const DWORD bytesToWrite = static_cast<DWORD>((path.size() + 1) * sizeof(wchar_t));
            DWORD bytesWritten = 0;
            const BOOL written = WriteFile(launchPipe,
                                           path.c_str(),
                                           bytesToWrite,
                                           &bytesWritten,
                                           nullptr);
            CloseHandle(launchPipe);
            Expect(written != FALSE && bytesWritten == bytesToWrite,
                   "Failed to forward a launch path to the resident HyperBrowse instance");
        };
        const auto launchClient = [&](const std::wstring& arguments)
        {
            PROCESS_INFORMATION client{};
            std::wstring commandLine = L"\"" + applicationPath.wstring() + L"\" " + arguments;
            std::vector<wchar_t> mutableCommandLine(commandLine.begin(), commandLine.end());
            mutableCommandLine.push_back(L'\0');
            STARTUPINFOW clientStartupInfo{};
            clientStartupInfo.cb = sizeof(clientStartupInfo);
            Expect(CreateProcessW(applicationPath.c_str(),
                                  mutableCommandLine.data(),
                                  nullptr,
                                  nullptr,
                                  FALSE,
                                  0,
                                  nullptr,
                                  applicationPath.parent_path().c_str(),
                                  &clientStartupInfo,
                                  &client) != FALSE,
                   "Failed to launch a resident-mode single-instance client");
            Expect(WaitForSingleObject(client.hProcess, 5000) == WAIT_OBJECT_0,
                   "A resident-mode single-instance client did not exit after forwarding its launch");
            DWORD exitCode = 1;
            Expect(GetExitCodeProcess(client.hProcess, &exitCode) != FALSE && exitCode == 0,
                   "A resident-mode single-instance client exited unsuccessfully");
            cleanupProcess(&client);
        };

        require(PostMessageW(mainWindow, WM_CLOSE, 0, 0) != FALSE,
                "Failed to request resident-mode window close");
        require(waitForVisibility(false), "Resident-mode close did not hide the main window");
        require(WaitForSingleObject(processInfo.hProcess, 0) == WAIT_TIMEOUT,
                "Resident-mode close terminated HyperBrowse");

        const UINT trayMessage = RegisterWindowMessageW(kTrayMessageName);
        require(trayMessage != 0 && PostMessageW(mainWindow, trayMessage, 0, WM_LBUTTONUP) != FALSE,
                "Failed to post the tray activation message");
        require(waitForVisibility(true), "Tray activation did not restore the resident window");

        launchClient(isolatedSingleInstanceArgument);
        require(WaitForSingleObject(processInfo.hProcess, 0) == WAIT_TIMEOUT,
                "Activation-only launch terminated the resident HyperBrowse instance");

        launchClient(isolatedSingleInstanceArgument + L" \"" + applicationPath.wstring() + L"\"");
        require(WaitForSingleObject(processInfo.hProcess, 0) == WAIT_TIMEOUT,
                "File launch terminated the resident HyperBrowse instance");

        require(PostMessageW(mainWindow, WM_COMMAND, MAKEWPARAM(ID_FILE_EXIT, 0), 0) != FALSE,
                "Failed to request File > Exit from the resident HyperBrowse instance");
        require(WaitForSingleObject(processInfo.hProcess, 5000) == WAIT_OBJECT_0,
                "File > Exit did not terminate the resident HyperBrowse instance");
        DWORD exitCode = 1;
        require(GetExitCodeProcess(processInfo.hProcess, &exitCode) != FALSE && exitCode == 0,
                "Resident HyperBrowse exited unsuccessfully after File > Exit");
        cleanup();
    }
}
