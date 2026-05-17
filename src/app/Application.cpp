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

    Application::~Application() = default;

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

        const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        const bool shouldUninitializeCom = SUCCEEDED(comResult) || comResult == S_FALSE;
        if (FAILED(comResult) && comResult != RPC_E_CHANGED_MODE)
        {
            util::LogError(L"Failed to initialize COM for the application shell");
            return -1;
        }

        mainWindow_ = std::make_unique<ui::MainWindow>(instance_);
        if (!startupOptions.launchPath.empty())
        {
            mainWindow_->SetStartupLaunchPath(startupOptions.launchPath);
        }
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
        if (shouldUninitializeCom)
        {
            CoUninitialize();
        }
        return static_cast<int>(msg.wParam);
    }
}
