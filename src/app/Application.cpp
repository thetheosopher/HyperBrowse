#include "app/Application.h"

#include <objbase.h>
#include <shellapi.h>

#include <string_view>

#include "ui/MainWindow.h"
#include "util/Diagnostics.h"
#include "util/Log.h"
#include "util/Timing.h"

namespace
{
    struct StartupBenchmarkOptions
    {
        bool enabled{};
        std::wstring outputPath;
    };

    StartupBenchmarkOptions ParseStartupBenchmarkOptions()
    {
        StartupBenchmarkOptions options;

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
                options.enabled = true;
                if (index + 1 < argumentCount)
                {
                    const std::wstring_view nextArgument(arguments[index + 1]);
                    if (!nextArgument.empty() && nextArgument[0] != L'-')
                    {
                        options.outputPath.assign(nextArgument);
                        ++index;
                    }
                }
                break;
            }

            constexpr std::wstring_view kBenchStartupPrefix = L"--bench-startup=";
            if (argument.rfind(kBenchStartupPrefix, 0) == 0)
            {
                options.enabled = true;
                options.outputPath.assign(argument.substr(kBenchStartupPrefix.size()));
                break;
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

    Application::~Application() = default;

    int Application::Run(int nCmdShow)
    {
        util::ScopedTimer startupTimer{L"Application::Run"};
        util::Stopwatch startupStopwatch;
        util::LogInfo(L"Starting HyperBrowse application shell");

        const StartupBenchmarkOptions startupBenchmarkOptions = ParseStartupBenchmarkOptions();
        if (startupBenchmarkOptions.enabled)
        {
            util::EnableStartupBenchmark(startupBenchmarkOptions.outputPath);
            util::LogInfo(L"Startup benchmark capture enabled.");
        }

        const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        const bool shouldUninitializeCom = SUCCEEDED(comResult) || comResult == S_FALSE;
        if (FAILED(comResult) && comResult != RPC_E_CHANGED_MODE)
        {
            util::LogError(L"Failed to initialize COM for the application shell");
            return -1;
        }

        mainWindow_ = std::make_unique<ui::MainWindow>(instance_);
        if (!mainWindow_->Create())
        {
            util::LogError(L"Failed to create main window");
            if (shouldUninitializeCom)
            {
                CoUninitialize();
            }
            return -1;
        }

        mainWindow_->Show(nCmdShow);
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
        if (startupBenchmarkOptions.enabled)
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
        if (shouldUninitializeCom)
        {
            CoUninitialize();
        }
        return static_cast<int>(msg.wParam);
    }
}
