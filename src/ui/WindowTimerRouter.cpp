#include "ui/WindowTimerRouter.h"

#include <utility>

namespace hyperbrowse::ui
{
    void WindowTimerRouter::Configure(TimerIds timerIds, Handlers handlers)
    {
        timerIds_ = timerIds;
        handlers_ = std::move(handlers);
    }

    std::optional<LRESULT> WindowTimerRouter::Handle(UINT_PTR timerId) const
    {
        const Handler* handler = nullptr;
        if (timerId == timerIds_.fileOperationShutdown)
        {
            handler = &handlers_.onFileOperationShutdown;
        }
        else if (timerId == timerIds_.folderPresentation)
        {
            handler = &handlers_.onFolderPresentation;
        }
        else if (timerId == timerIds_.memoryPressure)
        {
            handler = &handlers_.onMemoryPressure;
        }
        else if (timerId == timerIds_.displaySurfaceRecovery)
        {
            handler = &handlers_.onDisplaySurfaceRecovery;
        }

        if (!handler || !*handler)
        {
            return std::nullopt;
        }

        return (*handler)();
    }
}
