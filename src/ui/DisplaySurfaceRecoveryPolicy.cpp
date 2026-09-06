#include "ui/DisplaySurfaceRecoveryPolicy.h"

namespace hyperbrowse::ui
{
    void DisplaySurfaceRecoveryPolicy::BeginRetries()
    {
        attempt_ = 0;
    }

    int DisplaySurfaceRecoveryPolicy::AdvanceRetry()
    {
        return ++attempt_;
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
