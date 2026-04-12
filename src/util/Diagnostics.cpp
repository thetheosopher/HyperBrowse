#include "util/Diagnostics.h"

#include <algorithm>
#include <cmath>
#include <cwchar>
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

        std::wstring derived;
        const auto appendDerivedMetric = [&derived](std::wstring_view name, const std::wstring& value)
        {
            derived.append(L"- ");
            derived.append(name);
            derived.append(L": ");
            derived.append(value);
            derived.append(L"\r\n");
        };

        const auto hitsIterator = counters.find(L"viewer.prefetch.hit");
        const auto missesIterator = counters.find(L"viewer.prefetch.miss");
        if (hitsIterator != counters.end() || missesIterator != counters.end())
        {
            const std::uint64_t hits = hitsIterator != counters.end() ? hitsIterator->second : 0;
            const std::uint64_t misses = missesIterator != counters.end() ? missesIterator->second : 0;
            const double hitRate = (hits + misses) == 0
                ? 0.0
                : (static_cast<double>(hits) * 100.0) / static_cast<double>(hits + misses);
            std::wstring hitRateValue = FormatMilliseconds(hitRate);
            hitRateValue.append(L"%");
            appendDerivedMetric(L"viewer.prefetch.hit_rate", hitRateValue);
        }

        const auto batchSubmissionsIterator = counters.find(L"thumbnail.decode.nvjpeg.batch.submissions");
        if (batchSubmissionsIterator != counters.end())
        {
            const std::uint64_t batchSubmissions = batchSubmissionsIterator->second;
            const auto batchImagesIterator = counters.find(L"thumbnail.decode.nvjpeg.batch.images");
            const auto successImagesIterator = counters.find(L"thumbnail.decode.nvjpeg.batch.success_images");
            const auto fallbackImagesIterator = counters.find(L"thumbnail.decode.nvjpeg.batch.fallback_images");
            const auto fullSuccessIterator = counters.find(L"thumbnail.decode.nvjpeg.batch.full_success_submissions");

            const std::uint64_t batchImages = batchImagesIterator != counters.end() ? batchImagesIterator->second : 0;
            const std::uint64_t successImages = successImagesIterator != counters.end() ? successImagesIterator->second : 0;
            const std::uint64_t fallbackImages = fallbackImagesIterator != counters.end() ? fallbackImagesIterator->second : 0;
            const std::uint64_t fullSuccessSubmissions = fullSuccessIterator != counters.end() ? fullSuccessIterator->second : 0;

            if (batchSubmissions > 0)
            {
                appendDerivedMetric(L"thumbnail.decode.nvjpeg.batch.avg_size",
                                    FormatMilliseconds(static_cast<double>(batchImages) / static_cast<double>(batchSubmissions)));

                std::wstring fullSuccessRate = FormatMilliseconds(
                    (static_cast<double>(fullSuccessSubmissions) * 100.0) / static_cast<double>(batchSubmissions));
                fullSuccessRate.append(L"%");
                appendDerivedMetric(L"thumbnail.decode.nvjpeg.batch.full_success_rate", fullSuccessRate);
            }

            if (batchImages > 0)
            {
                std::wstring fallbackRate = FormatMilliseconds(
                    (static_cast<double>(fallbackImages) * 100.0) / static_cast<double>(batchImages));
                fallbackRate.append(L"%");
                appendDerivedMetric(L"thumbnail.decode.nvjpeg.batch.fallback_image_rate", fallbackRate);

                std::wstring successRate = FormatMilliseconds(
                    (static_cast<double>(successImages) * 100.0) / static_cast<double>(batchImages));
                successRate.append(L"%");
                appendDerivedMetric(L"thumbnail.decode.nvjpeg.batch.success_image_rate", successRate);
            }

            const std::wstring batchSizePrefix = L"thumbnail.decode.nvjpeg.batch.size.";
            std::map<int, std::uint64_t> batchDistribution;
            for (const auto& [name, value] : counters)
            {
                if (name.rfind(batchSizePrefix, 0) != 0)
                {
                    continue;
                }

                wchar_t* parseEnd = nullptr;
                const long batchSize = std::wcstol(name.c_str() + batchSizePrefix.size(), &parseEnd, 10);
                if (parseEnd == name.c_str() + batchSizePrefix.size() || *parseEnd != L'\0' || batchSize <= 0)
                {
                    continue;
                }

                batchDistribution[static_cast<int>(batchSize)] = value;
            }

            if (!batchDistribution.empty())
            {
                std::wstring distribution;
                bool first = true;
                for (const auto& [batchSize, count] : batchDistribution)
                {
                    if (!first)
                    {
                        distribution.append(L", ");
                    }

                    distribution.append(std::to_wstring(batchSize));
                    distribution.append(L"=");
                    distribution.append(std::to_wstring(count));
                    first = false;
                }

                appendDerivedMetric(L"thumbnail.decode.nvjpeg.batch.distribution", distribution);
            }
        }

        if (!derived.empty())
        {
            report.append(L"\r\nDerived\r\n");
            report.append(derived);
        }

        return report;
    }
}