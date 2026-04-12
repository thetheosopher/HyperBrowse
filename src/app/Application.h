#pragma once

#include <windows.h>
#include <memory>

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

    private:
        HINSTANCE instance_{};
        std::unique_ptr<hyperbrowse::ui::MainWindow> mainWindow_;
    };
}
