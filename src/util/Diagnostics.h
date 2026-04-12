#pragma once

#include <windows.h>

#include <cstdint>
#include <string>
#include <string_view>

namespace hyperbrowse::util
{
    class Stopwatch
    {
    public:
        Stopwatch();
        double ElapsedMilliseconds() const;

    private:
        LARGE_INTEGER frequency_{};
        LARGE_INTEGER start_{};
    };

    class ScopedMetricTimer
    {
    public:
        explicit ScopedMetricTimer(std::wstring_view metricName);
        ~ScopedMetricTimer();

        void CommitNow();
        void Dismiss() noexcept;

    private:
        Stopwatch stopwatch_;
        std::wstring metricName_;
        bool active_{true};
    };

    void RecordTiming(std::wstring_view metricName, double milliseconds);
    void IncrementCounter(std::wstring_view counterName, std::uint64_t delta = 1);
    void ResetDiagnostics();
    std::wstring BuildDiagnosticsReport();
}