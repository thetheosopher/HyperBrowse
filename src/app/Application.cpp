#include "app/Application.h"

#include <objbase.h>

#include "ui/MainWindow.h"
#include "util/Diagnostics.h"
#include "util/Log.h"
#include "util/Timing.h"

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
        if (shouldUninitializeCom)
        {
            CoUninitialize();
        }
        return static_cast<int>(msg.wParam);
    }
}
