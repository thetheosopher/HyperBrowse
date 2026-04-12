#include "util/Diagnostics.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <map>
#include <mutex>
#include <sstream>

namespace
{
    struct TimingStats
    {
        std::uint64_t count{};
        double totalMs{};
        double minMs{std::numeric_limits<double>::infinity()};
        double maxMs{};
        double lastMs{};
    };

    struct DiagnosticsStore
    {
        std::mutex mutex;
        std::map<std::wstring, TimingStats, std::less<>> timings;
        std::map<std::wstring, std::uint64_t, std::less<>> counters;
    };

    DiagnosticsStore& GetStore()
    {
        static DiagnosticsStore store;
        return store;
    }

    std::wstring FormatMilliseconds(double milliseconds)
    {
        std::wostringstream stream;
        stream << std::fixed << std::setprecision(2) << milliseconds;
        return stream.str();
    }
}

namespace hyperbrowse::util
{
    Stopwatch::Stopwatch()
    {
        QueryPerformanceFrequency(&frequency_);
        QueryPerformanceCounter(&start_);
    }

    double Stopwatch::ElapsedMilliseconds() const
    {
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        const auto elapsedTicks = now.QuadPart - start_.QuadPart;
        return (static_cast<double>(elapsedTicks) * 1000.0) / static_cast<double>(frequency_.QuadPart);
    }

    ScopedMetricTimer::ScopedMetricTimer(std::wstring_view metricName)
        : metricName_(metricName)
    {
    }

    ScopedMetricTimer::~ScopedMetricTimer()
    {
        if (active_)
        {
            RecordTiming(metricName_, stopwatch_.ElapsedMilliseconds());
        }
    }

    void ScopedMetricTimer::CommitNow()
    {
        if (!active_)
        {
            return;
        }

        RecordTiming(metricName_, stopwatch_.ElapsedMilliseconds());
        active_ = false;
    }

    void ScopedMetricTimer::Dismiss() noexcept
    {
        active_ = false;
    }

    void RecordTiming(std::wstring_view metricName, double milliseconds)
    {
        DiagnosticsStore& store = GetStore();
        std::scoped_lock lock(store.mutex);
        TimingStats& stats = store.timings[std::wstring(metricName)];
        ++stats.count;
        stats.totalMs += milliseconds;
        stats.minMs = std::min(stats.minMs, milliseconds);
        stats.maxMs = std::max(stats.maxMs, milliseconds);
        stats.lastMs = milliseconds;
    }

    void IncrementCounter(std::wstring_view counterName, std::uint64_t delta)
    {
        DiagnosticsStore& store = GetStore();
        std::scoped_lock lock(store.mutex);
        store.counters[std::wstring(counterName)] += delta;
    }

    void ResetDiagnostics()
    {
        DiagnosticsStore& store = GetStore();
        std::scoped_lock lock(store.mutex);
        store.timings.clear();
        store.counters.clear();
    }

    std::wstring BuildDiagnosticsReport()
    {
        DiagnosticsStore& store = GetStore();
        std::map<std::wstring, TimingStats, std::less<>> timings;
        std::map<std::wstring, std::uint64_t, std::less<>> counters;
        {
            std::scoped_lock lock(store.mutex);
            timings = store.timings;
            counters = store.counters;
        }

        std::wstring report = L"Diagnostics Snapshot";
        report.append(L"\r\n\r\nTimings\r\n");
        if (timings.empty())
        {
            report.append(L"- No timings recorded yet.\r\n");
        }
        else
        {
            for (const auto& [name, stats] : timings)
            {
                const double averageMs = stats.count == 0
                    ? 0.0
                    : stats.totalMs / static_cast<double>(stats.count);
                report.append(L"- ");
                report.append(name);
                report.append(L": count=");
                report.append(std::to_wstring(stats.count));
                report.append(L", avg=");
                report.append(FormatMilliseconds(averageMs));
                report.append(L" ms, last=");
                report.append(FormatMilliseconds(stats.lastMs));
                report.append(L" ms, min=");
                report.append(FormatMilliseconds(std::isfinite(stats.minMs) ? stats.minMs : 0.0));
                report.append(L" ms, max=");
                report.append(FormatMilliseconds(stats.maxMs));
                report.append(L" ms\r\n");
            }
        }

        report.append(L"\r\nCounters\r\n");
        if (counters.empty())
        {
            report.append(L"- No counters recorded yet.\r\n");
        }
        else
        {
            for (const auto& [name, value] : counters)
            {
                report.append(L"- ");
                report.append(name);
                report.append(L": ");
                report.append(std::to_wstring(value));
                report.append(L"\r\n");
            }
        }

        const auto hitsIterator = counters.find(L"viewer.prefetch.hit");
        const auto missesIterator = counters.find(L"viewer.prefetch.miss");
        if (hitsIterator != counters.end() || missesIterator != counters.end())
        {
            const std::uint64_t hits = hitsIterator != counters.end() ? hitsIterator->second : 0;
            const std::uint64_t misses = missesIterator != counters.end() ? missesIterator->second : 0;
            const double hitRate = (hits + misses) == 0
                ? 0.0
                : (static_cast<double>(hits) * 100.0) / static_cast<double>(hits + misses);
            report.append(L"\r\nDerived\r\n- viewer.prefetch.hit_rate: ");
            report.append(FormatMilliseconds(hitRate));
            report.append(L"%\r\n");
        }

        return report;
    }
}