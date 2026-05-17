#pragma once

#include <windows.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace hyperbrowse::util
{
    struct DiagnosticTimingRow
    {
        std::wstring name;
        std::uint64_t count{};
        double averageMs{};
        double lastMs{};
        double minMs{};
        double maxMs{};
    };

    struct DiagnosticCounterRow
    {
        std::wstring name;
        std::uint64_t value{};
    };

    struct DiagnosticValueRow
    {
        std::wstring name;
        std::wstring value;
    };

    struct DiagnosticsSnapshot
    {
        std::vector<DiagnosticTimingRow> timings;
        std::vector<DiagnosticCounterRow> counters;
        std::vector<DiagnosticValueRow> derived;
    };

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
    void EnableStartupBenchmark(std::wstring outputPath = {});
    bool IsStartupBenchmarkEnabled();
    void MarkStartupWindowVisible();
    void MarkStartupFirstThumbnailPainted();
    bool WriteStartupBenchmarkSnapshot(std::wstring* outputPath = nullptr);
    void ResetDiagnostics();
    DiagnosticsSnapshot CaptureDiagnosticsSnapshot();
    std::wstring BuildDiagnosticsReport();
}