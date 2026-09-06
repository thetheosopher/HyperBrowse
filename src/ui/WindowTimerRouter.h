#pragma once

#include <windows.h>

#include <functional>
#include <optional>

namespace hyperbrowse::ui
{
    class WindowTimerRouter final
    {
    public:
        using Handler = std::function<std::optional<LRESULT>()>;

        struct TimerIds
        {
            UINT_PTR fileOperationShutdown{};
            UINT_PTR folderPresentation{};
            UINT_PTR memoryPressure{};
            UINT_PTR displaySurfaceRecovery{};
        };

        struct Handlers
        {
            Handler onFileOperationShutdown;
            Handler onFolderPresentation;
            Handler onMemoryPressure;
            Handler onDisplaySurfaceRecovery;
        };

        WindowTimerRouter() = default;
        WindowTimerRouter(const WindowTimerRouter&) = delete;
        WindowTimerRouter& operator=(const WindowTimerRouter&) = delete;

        void Configure(TimerIds timerIds, Handlers handlers);
        std::optional<LRESULT> Handle(UINT_PTR timerId) const;

    private:
        TimerIds timerIds_;
        Handlers handlers_;
    };
}
