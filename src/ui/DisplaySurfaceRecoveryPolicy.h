#pragma once

namespace hyperbrowse::ui
{
    class DisplaySurfaceRecoveryPolicy final
    {
    public:
        static constexpr int kRetryLimit = 3;

        void BeginRetries();
        int AdvanceRetry();
        bool ShouldRelayout() const;
        bool Exhausted() const;

    private:
        int attempt_{};
    };
}
