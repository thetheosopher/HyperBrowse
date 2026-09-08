#pragma once

namespace hyperbrowse::ui
{
    constexpr bool ShouldUseViewerTransition(bool slideshowNavigation, bool manualTransitionEnabled) noexcept
    {
        return slideshowNavigation || manualTransitionEnabled;
    }
}
