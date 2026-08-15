#include "app/Application.h"

#include <objbase.h>
#include <shellapi.h>
#include <shobjidl.h>

#include <string_view>

#include "ui/MainWindow.h"
#include "util/Diagnostics.h"
#include "util/Log.h"
#include "util/Timing.h"

namespace
{
    constexpr wchar_t kSingleInstanceMutexName[] = L"Local\\TheTheosopher.HyperBrowse.SingleInstance";
    constexpr wchar_t kSingleInstancePipeName[] = L"\\\\.\\pipe\\TheTheosopher.HyperBrowse.Launch";
    constexpr wchar_t kRegistryPath[] = L"Software\\HyperBrowse";
    constexpr wchar_t kRegistryValueSingleInstanceEnabled[] = L"SingleInstanceEnabled";

    struct StartupBenchmarkOptions
    {
        bool enabled{};
        std::wstring outputPath;
    };

    struct StartupOptions
    {
        StartupBenchmarkOptions benchmark;
        std::wstring launchPath;
    };

    StartupOptions ParseStartupOptions()
    {
        StartupOptions options;

        int argumentCount = 0;
        LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
        if (!arguments)
        {
            return options;
        }

        for (int index = 1; index < argumentCount; ++index)
        {
            const std::wstring_view argument(arguments[index]);
            if (argument == L"--bench-startup")
            {
                options.benchmark.enabled = true;
                if (index + 1 < argumentCount)
                {
                    const std::wstring_view nextArgument(arguments[index + 1]);
                    if (!nextArgument.empty() && nextArgument[0] != L'-')
                    {
                        options.benchmark.outputPath.assign(nextArgument);
                        ++index;
                    }
                }
                continue;
            }

            constexpr std::wstring_view kBenchStartupPrefix = L"--bench-startup=";
            if (argument.rfind(kBenchStartupPrefix, 0) == 0)
            {
                options.benchmark.enabled = true;
                options.benchmark.outputPath.assign(argument.substr(kBenchStartupPrefix.size()));
                continue;
            }

            if (options.launchPath.empty() && !argument.empty())
            {
                options.launchPath.assign(argument);
            }
        }

        LocalFree(arguments);
        return options;
    }
}

namespace hyperbrowse::app
{
    Application::Application(HINSTANCE instance)
        : instance_(instance)
    {
    }

    bool Application::IsSingleInstanceEnabled()
    {
        HKEY key{};
        if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegistryPath, 0, KEY_READ, &key) != ERROR_SUCCESS)
        {
            return false;
        }

        DWORD value = 0;
        DWORD valueSize = sizeof(value);
        DWORD valueType = REG_DWORD;
        const bool enabled = RegQueryValueExW(key,
                                              kRegistryValueSingleInstanceEnabled,
                                              nullptr,
                                              &valueType,
                                              reinterpret_cast<LPBYTE>(&value),
                                              &valueSize) == ERROR_SUCCESS
            && valueType == REG_DWORD
            && value != 0;
        RegCloseKey(key);
        return enabled;
    }

    void Application::SetSingleInstanceEnabled(bool enabled)
    {
        HKEY key{};
        DWORD disposition = 0;
        if (RegCreateKeyExW(HKEY_CURRENT_USER,
                            kRegistryPath,
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

        const DWORD value = enabled ? 1UL : 0UL;
        RegSetValueExW(key,
                       kRegistryValueSingleInstanceEnabled,
                       0,
                       REG_DWORD,
                       reinterpret_cast<const BYTE*>(&value),
                       sizeof(value));
        RegCloseKey(key);
    }

    Application::~Application()
    {
        StopInstanceListener();
        if (singleInstanceMutex_)
        {
            ReleaseMutex(singleInstanceMutex_);
            CloseHandle(singleInstanceMutex_);
            singleInstanceMutex_ = nullptr;
        }
    }

    bool Application::TryBecomePrimaryInstance(const std::wstring& launchPath)
    {
        singleInstanceMutex_ = CreateMutexW(nullptr, TRUE, kSingleInstanceMutexName);
        if (!singleInstanceMutex_)
        {
            return true; // Could not determine; proceed as primary.
        }

        if (GetLastError() != ERROR_ALREADY_EXISTS)
        {
            isPrimaryInstance_ = true;
            return true;
        }

        // Another instance is running: forward the launch path (if any) to it and exit.
        CloseHandle(singleInstanceMutex_);
        singleInstanceMutex_ = nullptr;

        if (!launchPath.empty())
        {
            HANDLE pipe = CreateFileW(kSingleInstancePipeName,
                                      GENERIC_WRITE,
                                      0,
                                      nullptr,
                                      OPEN_EXISTING,
                                      0,
                                      nullptr);
            if (pipe != INVALID_HANDLE_VALUE)
            {
                const DWORD bytesToWrite = static_cast<DWORD>((launchPath.size() + 1) * sizeof(wchar_t));
                DWORD bytesWritten = 0;
                WriteFile(pipe, launchPath.c_str(), bytesToWrite, &bytesWritten, nullptr);
                CloseHandle(pipe);
            }
            else
            {
                // Pipe not ready; still try to foreground the existing window by class name.
                if (HWND existing = FindWindowW(L"HyperBrowseMainWindow", nullptr))
                {
                    if (IsIconic(existing))
                    {
                        ShowWindow(existing, SW_RESTORE);
                    }
                    SetForegroundWindow(existing);
                }
            }
        }
        return false;
    }

    void Application::StartInstanceListener()
    {
        if (!isPrimaryInstance_ || listenerThread_.joinable())
        {
            return;
        }

        listenerStopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        listenerThread_ = std::thread([this]()
        {
            InstanceListenerLoop();
        });
    }

    void Application::StopInstanceListener()
    {
        if (listenerStopEvent_)
        {
            SetEvent(listenerStopEvent_);
        }
        if (listenerThread_.joinable())
        {
            listenerThread_.join();
        }
        if (listenerStopEvent_)
        {
            CloseHandle(listenerStopEvent_);
            listenerStopEvent_ = nullptr;
        }
    }

    void Application::InstanceListenerLoop()
    {
        while (true)
        {
            HANDLE pipe = CreateNamedPipeW(kSingleInstancePipeName,
                                           PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED,
                                           PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
                                           1,
                                           4096,
                                           4096,
                                           0,
                                           nullptr);
            if (pipe == INVALID_HANDLE_VALUE)
            {
                return;
            }

            OVERLAPPED overlapped{};
            overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            BOOL connected = ConnectNamedPipe(pipe, &overlapped);
            if (!connected)
            {
                const DWORD error = GetLastError();
                if (error == ERROR_PIPE_CONNECTED)
                {
                    connected = TRUE;
                }
                else if (error == ERROR_IO_PENDING)
                {
                    HANDLE waitHandles[] = {overlapped.hEvent, listenerStopEvent_};
                    const DWORD waitResult = WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE);
                    if (waitResult == WAIT_OBJECT_0)
                    {
                        DWORD bytes = 0;
                        connected = GetOverlappedResult(pipe, &overlapped, &bytes, FALSE);
                    }
                    else
                    {
                        connected = FALSE;
                    }
                }
            }

            if (connected)
            {
                wchar_t buffer[2048]{};
                DWORD bytesRead = 0;
                if (ReadFile(pipe, buffer, sizeof(buffer), &bytesRead, nullptr) && bytesRead >= sizeof(wchar_t))
                {
                    std::wstring path(buffer, bytesRead / sizeof(wchar_t));
                    while (!path.empty() && path.back() == L'\0')
                    {
                        path.pop_back();
                    }
                    if (!path.empty() && mainWindow_ && mainWindow_->Hwnd())
                    {
                        auto* payload = new std::wstring(std::move(path));
                        if (!PostMessageW(mainWindow_->Hwnd(),
                                          ui::MainWindow::kExternalLaunchMessage,
                                          0,
                                          reinterpret_cast<LPARAM>(payload)))
                        {
                            delete payload;
                        }
                    }
                }
            }

            if (overlapped.hEvent)
            {
                CloseHandle(overlapped.hEvent);
            }
            DisconnectNamedPipe(pipe);
            CloseHandle(pipe);

            if (listenerStopEvent_ && WaitForSingleObject(listenerStopEvent_, 0) == WAIT_OBJECT_0)
            {
                return;
            }
        }
    }

    int Application::Run(int nCmdShow)
    {
        util::ScopedTimer startupTimer{L"Application::Run"};
        util::Stopwatch startupStopwatch;
        util::LogInfo(L"Starting HyperBrowse application shell");

        const StartupOptions startupOptions = ParseStartupOptions();
        if (startupOptions.benchmark.enabled)
        {
            util::EnableStartupBenchmark(startupOptions.benchmark.outputPath);
            util::LogInfo(L"Startup benchmark capture enabled.");
        }

        const HRESULT oleResult = OleInitialize(nullptr);
        const bool shouldUninitializeOle = SUCCEEDED(oleResult) || oleResult == S_FALSE;
        if (FAILED(oleResult) && oleResult != RPC_E_CHANGED_MODE)
        {
            util::LogError(L"Failed to initialize OLE for the application shell");
            return -1;
        }

        // A stable AppUserModelID makes the taskbar jump list's "Recent" category
        // (populated via SHAddToRecentDocs) attach to HyperBrowse's taskbar button.
        SetCurrentProcessExplicitAppUserModelID(L"TheTheosopher.HyperBrowse");

        // Single instance is opt-in: a second launch forwards its path to the running window.
        if (IsSingleInstanceEnabled() && !TryBecomePrimaryInstance(startupOptions.launchPath))
        {
            util::LogInfo(L"Another HyperBrowse instance is running; forwarded launch path and exiting.");
            if (shouldUninitializeOle)
            {
                OleUninitialize();
            }
            return 0;
        }

        mainWindow_ = std::make_unique<ui::MainWindow>(instance_);
        if (!startupOptions.launchPath.empty())
        {
            mainWindow_->SetStartupLaunchPath(startupOptions.launchPath);
        }
        if (!mainWindow_->Create())
        {
            util::LogError(L"Failed to create main window");
            if (shouldUninitializeOle)
            {
                OleUninitialize();
            }
            return -1;
        }

        mainWindow_->Show(nCmdShow);
        StartInstanceListener();
        util::RecordTiming(L"app.startup", startupStopwatch.ElapsedMilliseconds());
    util::MarkStartupWindowVisible();

        MSG msg{};
        while (GetMessageW(&msg, nullptr, 0, 0) > 0)
        {
            if (mainWindow_ && mainWindow_->TranslateAcceleratorMessage(&msg))
            {
                continue;
            }

            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        util::LogInfo(L"Shutting down HyperBrowse");
        if (startupOptions.benchmark.enabled)
        {
            std::wstring outputPath;
            if (util::WriteStartupBenchmarkSnapshot(&outputPath))
            {
                util::LogInfo(std::wstring(L"Wrote startup benchmark snapshot to ") + outputPath);
            }
            else
            {
                util::LogError(L"Failed to write startup benchmark snapshot.");
            }
        }
        if (shouldUninitializeOle)
        {
            OleUninitialize();
        }
        return static_cast<int>(msg.wParam);
    }
}
