#pragma once

namespace hyperbrowse::viewer
{
    enum class EscapeKeyBehavior : int
    {
        Close = 0,
        FitWidth = 1,
        FitHeight = 2,
        ActualSize = 3,
        ExitFullScreen = 4,
    };
}
