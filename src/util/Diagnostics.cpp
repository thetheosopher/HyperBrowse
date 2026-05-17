#include "util/Diagnostics.h"

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <mutex>
#include <sstream>
#include <string>

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
        bool startupBenchmarkEnabled{};
        bool startupWindowVisibleRecorded{};
        bool startupThumbnailPaintedRecorded{};
        double startupFirstWindowVisibleMs{};
        double startupFirstThumbnailPaintedMs{};
        std::wstring startupBenchmarkOutputPath;
        hyperbrowse::util::Stopwatch startupBenchmarkStopwatch;
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

    std::wstring DefaultStartupBenchmarkOutputPath()
    {
        namespace fs = std::filesystem;

        std::error_code error;
        fs::path path = fs::temp_directory_path(error);
        if (error)
        {
            error.clear();
            path = fs::current_path(error);
        }
        if (error)
        {
            return L"HyperBrowse-startup-benchmark.json";
        }

        path /= L"HyperBrowse-startup-benchmark.json";
        return path.wstring();
    }

    std::string WideToUtf8(std::wstring_view value)
    {
        if (value.empty())
        {
            return {};
        }

        const int requiredBytes = WideCharToMultiByte(CP_UTF8,
                                                      0,
                                                      value.data(),
                                                      static_cast<int>(value.size()),
                                                      nullptr,
                                                      0,
                                                      nullptr,
                                                      nullptr);
        if (requiredBytes <= 0)
        {
            return {};
        }

        std::string utf8(static_cast<std::size_t>(requiredBytes), '\0');
        WideCharToMultiByte(CP_UTF8,
                            0,
                            value.data(),
                            static_cast<int>(value.size()),
                            utf8.data(),
                            requiredBytes,
                            nullptr,
                            nullptr);
        return utf8;
    }

    void AppendEscapedJsonString(std::string* output, std::wstring_view value)
    {
        if (!output)
        {
            return;
        }

        output->push_back('"');
        for (unsigned char character : WideToUtf8(value))
        {
            switch (character)
            {
            case '\\':
                output->append("\\\\");
                break;
            case '"':
                output->append("\\\"");
                break;
            case '\b':
                output->append("\\b");
                break;
            case '\f':
                output->append("\\f");
                break;
            case '\n':
                output->append("\\n");
                break;
            case '\r':
                output->append("\\r");
                break;
            case '\t':
                output->append("\\t");
                break;
            default:
                if (character < 0x20)
                {
                    std::ostringstream escape;
                    escape << "\\u"
                           << std::hex
                           << std::uppercase
                           << std::setw(4)
                           << std::setfill('0')
                           << static_cast<int>(character);
                    output->append(escape.str());
                }
                else
                {
                    output->push_back(static_cast<char>(character));
                }
                break;
            }
        }
        output->push_back('"');
    }

    void AppendJsonNumber(std::string* output, double value)
    {
        if (!output)
        {
            return;
        }

        std::ostringstream stream;
        stream << std::fixed << std::setprecision(2) << value;
        output->append(stream.str());
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

    void EnableStartupBenchmark(std::wstring outputPath)
    {
        DiagnosticsStore& store = GetStore();
        std::scoped_lock lock(store.mutex);
        store.startupBenchmarkEnabled = true;
        store.startupWindowVisibleRecorded = false;
        store.startupThumbnailPaintedRecorded = false;
        store.startupFirstWindowVisibleMs = 0.0;
        store.startupFirstThumbnailPaintedMs = 0.0;
        store.startupBenchmarkOutputPath = outputPath.empty()
            ? DefaultStartupBenchmarkOutputPath()
            : std::move(outputPath);
        store.startupBenchmarkStopwatch = Stopwatch{};
    }

    bool IsStartupBenchmarkEnabled()
    {
        DiagnosticsStore& store = GetStore();
        std::scoped_lock lock(store.mutex);
        return store.startupBenchmarkEnabled;
    }

    void MarkStartupWindowVisible()
    {
        double elapsedMs = 0.0;
        bool shouldRecord = false;

        DiagnosticsStore& store = GetStore();
        {
            std::scoped_lock lock(store.mutex);
            if (!store.startupBenchmarkEnabled || store.startupWindowVisibleRecorded)
            {
                return;
            }

            elapsedMs = store.startupBenchmarkStopwatch.ElapsedMilliseconds();
            store.startupFirstWindowVisibleMs = elapsedMs;
            store.startupWindowVisibleRecorded = true;
            shouldRecord = true;
        }

        if (shouldRecord)
        {
            RecordTiming(L"startup.process_to_first_window_visible", elapsedMs);
        }
    }

    void MarkStartupFirstThumbnailPainted()
    {
        double elapsedSinceWindowVisibleMs = 0.0;
        double elapsedSinceProcessStartMs = 0.0;
        bool shouldRecord = false;

        DiagnosticsStore& store = GetStore();
        {
            std::scoped_lock lock(store.mutex);
            if (!store.startupBenchmarkEnabled
                || !store.startupWindowVisibleRecorded
                || store.startupThumbnailPaintedRecorded)
            {
                return;
            }

            elapsedSinceProcessStartMs = store.startupBenchmarkStopwatch.ElapsedMilliseconds();
            elapsedSinceWindowVisibleMs = std::max(0.0, elapsedSinceProcessStartMs - store.startupFirstWindowVisibleMs);
            store.startupFirstThumbnailPaintedMs = elapsedSinceWindowVisibleMs;
            store.startupThumbnailPaintedRecorded = true;
            shouldRecord = true;
        }

        if (shouldRecord)
        {
            RecordTiming(L"startup.first_window_visible_to_first_thumbnail_painted", elapsedSinceWindowVisibleMs);
            RecordTiming(L"startup.process_to_first_thumbnail_painted", elapsedSinceProcessStartMs);
        }
    }

    bool WriteStartupBenchmarkSnapshot(std::wstring* outputPath)
    {
        DiagnosticsSnapshot snapshot = CaptureDiagnosticsSnapshot();

        bool enabled = false;
        bool windowVisibleRecorded = false;
        bool thumbnailPaintedRecorded = false;
        double firstWindowVisibleMs = 0.0;
        double firstThumbnailPaintedMs = 0.0;
        std::wstring snapshotPath;

        DiagnosticsStore& store = GetStore();
        {
            std::scoped_lock lock(store.mutex);
            enabled = store.startupBenchmarkEnabled;
            windowVisibleRecorded = store.startupWindowVisibleRecorded;
            thumbnailPaintedRecorded = store.startupThumbnailPaintedRecorded;
            firstWindowVisibleMs = store.startupFirstWindowVisibleMs;
            firstThumbnailPaintedMs = store.startupFirstThumbnailPaintedMs;
            snapshotPath = store.startupBenchmarkOutputPath;
        }

        if (!enabled)
        {
            return false;
        }

        namespace fs = std::filesystem;
        fs::path path(snapshotPath.empty() ? DefaultStartupBenchmarkOutputPath() : snapshotPath);
        std::error_code error;
        if (path.has_parent_path())
        {
            fs::create_directories(path.parent_path(), error);
            if (error)
            {
                return false;
            }
        }

        std::string json;
        json.reserve(4096);
        json.append("{\n  \"startup\": {\n    \"windowVisibleCaptured\": ");
        json.append(windowVisibleRecorded ? "true" : "false");
        json.append(",\n    \"firstThumbnailPaintedCaptured\": ");
        json.append(thumbnailPaintedRecorded ? "true" : "false");
        json.append(",\n    \"processToFirstWindowVisibleMs\": ");
        if (windowVisibleRecorded)
        {
            AppendJsonNumber(&json, firstWindowVisibleMs);
        }
        else
        {
            json.append("null");
        }
        json.append(",\n    \"firstWindowVisibleToFirstThumbnailPaintedMs\": ");
        if (thumbnailPaintedRecorded)
        {
            AppendJsonNumber(&json, firstThumbnailPaintedMs);
        }
        else
        {
            json.append("null");
        }
        json.append(",\n    \"processToFirstThumbnailPaintedMs\": ");
        if (windowVisibleRecorded && thumbnailPaintedRecorded)
        {
            AppendJsonNumber(&json, firstWindowVisibleMs + firstThumbnailPaintedMs);
        }
        else
        {
            json.append("null");
        }
        json.append("\n  },\n  \"timings\": [");

        for (std::size_t index = 0; index < snapshot.timings.size(); ++index)
        {
            const DiagnosticTimingRow& row = snapshot.timings[index];
            if (index != 0)
            {
                json.append(",");
            }
            json.append("\n    {\"name\": ");
            AppendEscapedJsonString(&json, row.name);
            json.append(", \"count\": ");
            json.append(std::to_string(row.count));
            json.append(", \"averageMs\": ");
            AppendJsonNumber(&json, row.averageMs);
            json.append(", \"lastMs\": ");
            AppendJsonNumber(&json, row.lastMs);
            json.append(", \"minMs\": ");
            AppendJsonNumber(&json, row.minMs);
            json.append(", \"maxMs\": ");
            AppendJsonNumber(&json, row.maxMs);
            json.append("}");
        }
        json.append(snapshot.timings.empty() ? "]" : "\n  ]");

        json.append(",\n  \"counters\": [");
        for (std::size_t index = 0; index < snapshot.counters.size(); ++index)
        {
            const DiagnosticCounterRow& row = snapshot.counters[index];
            if (index != 0)
            {
                json.append(",");
            }
            json.append("\n    {\"name\": ");
            AppendEscapedJsonString(&json, row.name);
            json.append(", \"value\": ");
            json.append(std::to_string(row.value));
            json.append("}");
        }
        json.append(snapshot.counters.empty() ? "]" : "\n  ]");

        json.append(",\n  \"derived\": [");
        for (std::size_t index = 0; index < snapshot.derived.size(); ++index)
        {
            const DiagnosticValueRow& row = snapshot.derived[index];
            if (index != 0)
            {
                json.append(",");
            }
            json.append("\n    {\"name\": ");
            AppendEscapedJsonString(&json, row.name);
            json.append(", \"value\": ");
            AppendEscapedJsonString(&json, row.value);
            json.append("}");
        }
        json.append(snapshot.derived.empty() ? "]" : "\n  ]");
        json.append("\n}\n");

        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        if (!stream)
        {
            return false;
        }

        stream.write(json.data(), static_cast<std::streamsize>(json.size()));
        if (!stream)
        {
            return false;
        }

        if (outputPath)
        {
            *outputPath = path.wstring();
        }
        return true;
    }

    void ResetDiagnostics()
    {
        DiagnosticsStore& store = GetStore();
        std::scoped_lock lock(store.mutex);
        store.timings.clear();
        store.counters.clear();
    }

    DiagnosticsSnapshot CaptureDiagnosticsSnapshot()
    {
        DiagnosticsStore& store = GetStore();
        std::map<std::wstring, TimingStats, std::less<>> timings;
        std::map<std::wstring, std::uint64_t, std::less<>> counters;
        {
            std::scoped_lock lock(store.mutex);
            timings = store.timings;
            counters = store.counters;
        }

        DiagnosticsSnapshot snapshot;
        snapshot.timings.reserve(timings.size());
        snapshot.counters.reserve(counters.size());

        for (const auto& [name, stats] : timings)
        {
            const double averageMs = stats.count == 0
                ? 0.0
                : stats.totalMs / static_cast<double>(stats.count);
            snapshot.timings.push_back(DiagnosticTimingRow{
                name,
                stats.count,
                averageMs,
                stats.lastMs,
                std::isfinite(stats.minMs) ? stats.minMs : 0.0,
                stats.maxMs,
            });
        }

        for (const auto& [name, value] : counters)
        {
            snapshot.counters.push_back(DiagnosticCounterRow{name, value});
        }

        const auto appendDerivedMetric = [&snapshot](std::wstring_view name, const std::wstring& value)
        {
            snapshot.derived.push_back(DiagnosticValueRow{std::wstring(name), value});
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

        const auto cpuJpegTimingIterator = timings.find(L"thumbnail.decode.jpeg.cpu");
        const auto gpuBatchTimingIterator = timings.find(L"thumbnail.decode.nvjpeg.batch");
        const auto gpuBatchImagesIterator = counters.find(L"thumbnail.decode.nvjpeg.batch.images");
        if (cpuJpegTimingIterator != timings.end()
            && gpuBatchTimingIterator != timings.end()
            && gpuBatchImagesIterator != counters.end())
        {
            const TimingStats& cpuStats = cpuJpegTimingIterator->second;
            const TimingStats& gpuStats = gpuBatchTimingIterator->second;
            const std::uint64_t gpuImageCount = gpuBatchImagesIterator->second;

            const bool hasCpuImages = cpuStats.count > 0;
            const bool hasGpuImages = gpuImageCount > 0;
            if (hasCpuImages && hasGpuImages)
            {
                const double cpuPerImageMs = cpuStats.totalMs / static_cast<double>(cpuStats.count);
                const double gpuPerImageMs = gpuStats.totalMs / static_cast<double>(gpuImageCount);

                std::wstring cpuLabel = FormatMilliseconds(cpuPerImageMs);
                cpuLabel.append(L" ms/image");
                appendDerivedMetric(L"thumbnail.decode.compare.cpu_jpeg.per_image", cpuLabel);

                std::wstring gpuLabel = FormatMilliseconds(gpuPerImageMs);
                gpuLabel.append(L" ms/image");
                appendDerivedMetric(L"thumbnail.decode.compare.nvjpeg_batch.per_image", gpuLabel);

                if (gpuPerImageMs > 0.0)
                {
                    std::wstring speedup = FormatMilliseconds(cpuPerImageMs / gpuPerImageMs);
                    speedup.append(L"x");
                    appendDerivedMetric(L"thumbnail.decode.compare.nvjpeg_batch.speedup_vs_cpu", speedup);
                }

                std::wstring delta = FormatMilliseconds(cpuPerImageMs - gpuPerImageMs);
                delta.append(L" ms/image");
                appendDerivedMetric(L"thumbnail.decode.compare.cpu_minus_gpu", delta);
            }
        }

        return snapshot;
    }

    std::wstring BuildDiagnosticsReport()
    {
        const DiagnosticsSnapshot snapshot = CaptureDiagnosticsSnapshot();

        std::wstring report = L"Diagnostics Snapshot";
        report.append(L"\r\n\r\nTimings\r\n");
        if (snapshot.timings.empty())
        {
            report.append(L"- No timings recorded yet.\r\n");
        }
        else
        {
            for (const DiagnosticTimingRow& row : snapshot.timings)
            {
                report.append(L"- ");
                report.append(row.name);
                report.append(L": count=");
                report.append(std::to_wstring(row.count));
                report.append(L", avg=");
                report.append(FormatMilliseconds(row.averageMs));
                report.append(L" ms, last=");
                report.append(FormatMilliseconds(row.lastMs));
                report.append(L" ms, min=");
                report.append(FormatMilliseconds(row.minMs));
                report.append(L" ms, max=");
                report.append(FormatMilliseconds(row.maxMs));
                report.append(L" ms\r\n");
            }
        }

        report.append(L"\r\nCounters\r\n");
        if (snapshot.counters.empty())
        {
            report.append(L"- No counters recorded yet.\r\n");
        }
        else
        {
            for (const DiagnosticCounterRow& row : snapshot.counters)
            {
                report.append(L"- ");
                report.append(row.name);
                report.append(L": ");
                report.append(std::to_wstring(row.value));
                report.append(L"\r\n");
            }
        }

        if (!snapshot.derived.empty())
        {
            report.append(L"\r\nDerived\r\n");
            for (const DiagnosticValueRow& row : snapshot.derived)
            {
                report.append(L"- ");
                report.append(row.name);
                report.append(L": ");
                report.append(row.value);
                report.append(L"\r\n");
            }
        }

        return report;
    }
}