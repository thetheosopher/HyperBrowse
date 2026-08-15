#pragma once

#include <windows.h>
#include <memory>
#include <string>
#include <thread>

namespace hyperbrowse::ui
{
    class MainWindow;
}

namespace hyperbrowse::app
{
    class Application
    {
    public:
        explicit Application(HINSTANCE instance);
        ~Application();
        int Run(int nCmdShow);

        static bool IsSingleInstanceEnabled();
        static void SetSingleInstanceEnabled(bool enabled);

    private:
        bool TryBecomePrimaryInstance(const std::wstring& launchPath);
        void StartInstanceListener();
        void StopInstanceListener();
        void InstanceListenerLoop();

        HINSTANCE instance_{};
        std::unique_ptr<hyperbrowse::ui::MainWindow> mainWindow_;
        HANDLE singleInstanceMutex_{};
        HANDLE listenerStopEvent_{};
        std::thread listenerThread_;
        bool isPrimaryInstance_{};
    };
}
