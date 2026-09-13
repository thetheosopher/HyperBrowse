#include "ui/DisplaySurfaceRecoveryPolicy.h"

#include <algorithm>

namespace hyperbrowse::ui
{
    void DisplaySurfaceRecoveryPolicy::BeginRetries()
    {
        attempt_ = 0;
    }

    void DisplaySurfaceRecoveryPolicy::SetViewerTargets(std::span<const std::uintptr_t> targets)
    {
        viewerTargets_.assign(targets.begin(), targets.end());
    }

    void DisplaySurfaceRecoveryPolicy::ClearViewerTargets()
    {
        viewerTargets_.clear();
    }

    int DisplaySurfaceRecoveryPolicy::AdvanceRetry()
    {
        return ++attempt_;
    }

    bool DisplaySurfaceRecoveryPolicy::ShouldRecoverViewer(std::uintptr_t target) const
    {
        return std::find(viewerTargets_.begin(), viewerTargets_.end(), target) != viewerTargets_.end();
    }

    bool DisplaySurfaceRecoveryPolicy::ShouldRelayout() const
    {
        return attempt_ == 1;
    }

    bool DisplaySurfaceRecoveryPolicy::Exhausted() const
    {
        return attempt_ >= kRetryLimit;
    }
}
