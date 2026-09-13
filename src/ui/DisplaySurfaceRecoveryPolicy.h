#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace hyperbrowse::ui
{
    class DisplaySurfaceRecoveryPolicy final
    {
    public:
        static constexpr int kRetryLimit = 3;

        void BeginRetries();
        void SetViewerTargets(std::span<const std::uintptr_t> targets);
        void ClearViewerTargets();
        int AdvanceRetry();
        bool ShouldRecoverViewer(std::uintptr_t target) const;
        bool ShouldRelayout() const;
        bool Exhausted() const;

    private:
        int attempt_{};
        std::vector<std::uintptr_t> viewerTargets_;
    };
}
