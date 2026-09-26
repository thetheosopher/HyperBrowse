#include <windows.h>

#include <commctrl.h>

#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "browser/BrowserModel.h"
#include "browser/BrowserPane.h"
#include "cache/DiskThumbnailCache.h"
#include "services/ThumbnailScheduler.h"
#include "util/Diagnostics.h"

namespace fs = std::filesystem;

namespace
{
    constexpr std::size_t kCacheCapacityBytes = 64ULL * 1024ULL * 1024ULL;
    constexpr int kCacheHitIterations = 256;
    constexpr int kStoreIterations = 128;
    constexpr int kQueueStatisticsIterations = 32;
    constexpr int kScrollIterations = 512;

    struct BenchmarkResults
    {
        double cacheHitAverageMs{};
        double storeEntriesPerSecond{};
        double compactionMs{};
        double cacheWorkerQueueDelayAverageMs{};
        double cacheWorkerQueueDelayMaxMs{};
        double scrollMessagesPerSecond{};
    };

    class TempFolder
    {
    public:
        explicit TempFolder(const wchar_t* name)
            : root_(fs::temp_directory_path() / name)
        {
            std::error_code error;
            fs::remove_all(root_, error);
            fs::create_directories(root_);
            if (error)
            {
                throw std::runtime_error("failed to create benchmark directory");
            }
        }

        ~TempFolder()
        {
            std::error_code error;
            fs::remove_all(root_, error);
        }

        const fs::path& Root() const noexcept
        {
            return root_;
        }

    private:
        fs::path root_;
    };

    class ComCleanup
    {
    public:
        ~ComCleanup()
        {
            CoUninitialize();
        }
    };

    void Expect(bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    std::shared_ptr<const hyperbrowse::cache::CachedThumbnail> MakeBenchmarkThumbnail()
    {
        constexpr int width = 32;
        constexpr int height = 32;
        std::vector<unsigned char> pixels(static_cast<std::size_t>(width * height * 4), 0x7f);

        BITMAPINFO bitmapInfo{};
        bitmapInfo.bmiHeader.biSize = sizeof(bitmapInfo.bmiHeader);
        bitmapInfo.bmiHeader.biWidth = width;
        bitmapInfo.bmiHeader.biHeight = -height;
        bitmapInfo.bmiHeader.biPlanes = 1;
        bitmapInfo.bmiHeader.biBitCount = 32;
        bitmapInfo.bmiHeader.biCompression = BI_RGB;

        void* dibBits = nullptr;
        HBITMAP bitmap = CreateDIBSection(nullptr, &bitmapInfo, DIB_RGB_COLORS, &dibBits, nullptr, 0);
        Expect(bitmap != nullptr && dibBits != nullptr, "failed to create benchmark bitmap");
        std::memcpy(dibBits, pixels.data(), pixels.size());
        return std::make_shared<const hyperbrowse::cache::CachedThumbnail>(
            bitmap,
            width,
            height,
            pixels.size(),
            width,
            height);
    }

    hyperbrowse::cache::ThumbnailCacheKey MakeKey(const fs::path& root, int index)
    {
        const fs::path sourcePath = root / (L"source_" + std::to_wstring(index) + L".jpg");
        std::ofstream source(sourcePath, std::ios::binary | std::ios::trunc);
        Expect(static_cast<bool>(source), "failed to create benchmark source file");
        source << "benchmark";

        hyperbrowse::cache::ThumbnailCacheKey key;
        key.filePath = sourcePath.wstring();
        key.modifiedTimestampUtc = static_cast<std::uint64_t>(index + 1);
        key.targetWidth = 32;
        key.targetHeight = 32;
        return key;
    }

    double FindTimingAverage(const hyperbrowse::util::DiagnosticsSnapshot& snapshot,
                             std::wstring_view name,
                             double* maximum)
    {
        for (const hyperbrowse::util::DiagnosticTimingRow& row : snapshot.timings)
        {
            if (row.name == name)
            {
                if (maximum)
                {
                    *maximum = row.maxMs;
                }
                return row.averageMs;
            }
        }

        throw std::runtime_error("benchmark timing was not recorded");
    }

    void RunDiskBenchmark(const fs::path& root, BenchmarkResults* results)
    {
        const fs::path cacheRoot = root / L"disk-cache";
        const auto thumbnail = MakeBenchmarkThumbnail();
        const auto firstKey = MakeKey(root, 0);
        hyperbrowse::cache::DiskThumbnailCache cache(kCacheCapacityBytes, cacheRoot.wstring());
        cache.Store(firstKey, thumbnail);
        Expect(cache.Compact(), "benchmark cache warm-up compaction failed");

        hyperbrowse::util::Stopwatch hitTimer;
        for (int iteration = 0; iteration < kCacheHitIterations; ++iteration)
        {
            Expect(cache.TryLoad(firstKey) != nullptr, "benchmark cache hit failed");
        }
        results->cacheHitAverageMs = hitTimer.ElapsedMilliseconds() / kCacheHitIterations;

        hyperbrowse::util::Stopwatch storeTimer;
        for (int iteration = 1; iteration <= kStoreIterations; ++iteration)
        {
            cache.Store(MakeKey(root, iteration), thumbnail);
        }
        const double storeElapsedSeconds = storeTimer.ElapsedMilliseconds() / 1000.0;
        results->storeEntriesPerSecond = storeElapsedSeconds > 0.0
            ? static_cast<double>(kStoreIterations) / storeElapsedSeconds
            : static_cast<double>(kStoreIterations);

        hyperbrowse::util::Stopwatch compactionTimer;
        Expect(cache.Compact(), "benchmark cache compaction failed");
        results->compactionMs = compactionTimer.ElapsedMilliseconds();
    }

    void RunCacheWorkerQueueBenchmark(const fs::path& root, BenchmarkResults* results)
    {
        std::mutex barrierMutex;
        std::condition_variable barrierCondition;
        bool barrierEntered = false;
        bool releaseBarrier = false;
        std::mutex callbackMutex;
        std::condition_variable callbackCondition;
        int callbackCount = 0;

        hyperbrowse::services::ThumbnailScheduler scheduler(
            8ULL * 1024ULL * 1024ULL,
            1,
            hyperbrowse::util::ResourceProfile::Balanced,
            [&]()
            {
                std::unique_lock lock(barrierMutex);
                if (barrierEntered)
                {
                    return;
                }

                barrierEntered = true;
                barrierCondition.notify_all();
                barrierCondition.wait(lock, [&]()
                {
                    return releaseBarrier;
                });
            },
            {},
            kCacheCapacityBytes,
            (root / L"worker-cache").wstring());

        const auto callback = [&](bool, hyperbrowse::cache::DiskThumbnailCache::Statistics)
        {
            std::scoped_lock lock(callbackMutex);
            ++callbackCount;
            callbackCondition.notify_all();
        };

        Expect(scheduler.QueuePersistentCacheStatistics(callback), "failed to queue benchmark barrier");
        {
            std::unique_lock lock(barrierMutex);
            Expect(barrierCondition.wait_for(lock, std::chrono::seconds(5), [&]()
            {
                return barrierEntered;
            }), "benchmark cache worker barrier did not start");
        }

        for (int iteration = 0; iteration < kQueueStatisticsIterations; ++iteration)
        {
            Expect(scheduler.QueuePersistentCacheStatistics(callback), "failed to queue benchmark statistics");
        }

        Sleep(50);

        {
            std::scoped_lock lock(barrierMutex);
            releaseBarrier = true;
        }
        barrierCondition.notify_all();

        {
            std::unique_lock lock(callbackMutex);
            Expect(callbackCondition.wait_for(lock, std::chrono::seconds(10), [&]()
            {
                return callbackCount == kQueueStatisticsIterations + 1;
            }), "benchmark cache worker callbacks timed out");
        }

        const hyperbrowse::util::DiagnosticsSnapshot snapshot = hyperbrowse::util::CaptureDiagnosticsSnapshot();
        results->cacheWorkerQueueDelayAverageMs = FindTimingAverage(
            snapshot,
            L"persistent_cache.queue_delay",
            &results->cacheWorkerQueueDelayMaxMs);
    }

    LRESULT CALLBACK BenchmarkHostWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    double RunScrollBenchmark(HINSTANCE instance)
    {
        constexpr wchar_t kHostClassName[] = L"HyperBrowsePerformanceBenchmarkHost";
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.hInstance = instance;
        windowClass.lpfnWndProc = BenchmarkHostWindowProc;
        windowClass.lpszClassName = kHostClassName;
        Expect(RegisterClassExW(&windowClass) != 0, "failed to register benchmark host window");

        HWND hostWindow = CreateWindowExW(
            0,
            kHostClassName,
            L"HyperBrowse Performance Benchmark",
            WS_OVERLAPPEDWINDOW,
            0,
            0,
            1280,
            800,
            nullptr,
            nullptr,
            instance,
            nullptr);
        Expect(hostWindow != nullptr, "failed to create benchmark host window");

        double messagesPerSecond = 0.0;
        {
            hyperbrowse::browser::BrowserPane browserPane(instance);
            Expect(browserPane.Create(hostWindow), "failed to create benchmark browser pane");
            MoveWindow(browserPane.Hwnd(), 0, 0, 1280, 800, FALSE);

            hyperbrowse::browser::BrowserModel model;
            std::vector<hyperbrowse::browser::BrowserItem> items;
            items.reserve(1024);
            for (int index = 0; index < 1024; ++index)
            {
                items.push_back(hyperbrowse::browser::BrowserItem{
                    L"item_" + std::to_wstring(index) + L".txt",
                    L"C:\\HyperBrowsePerformanceBenchmark\\item_" + std::to_wstring(index) + L".txt",
                    L"TXT",
                    L"2026-09-14 12:00",
                    static_cast<std::uint64_t>(index + 1),
                    static_cast<std::uint64_t>(index + 1),
                    0,
                    0,
                });
            }
            model.Reset(L"C:\\HyperBrowsePerformanceBenchmark", false);
            model.AppendItems(std::move(items), 1024, 1024);
            model.Complete();
            browserPane.SetModel(&model);
            browserPane.RefreshFromModel();

            hyperbrowse::util::Stopwatch scrollTimer;
            for (int iteration = 0; iteration < kScrollIterations; ++iteration)
            {
                const WPARAM command = iteration % 2 == 0 ? SB_PAGEDOWN : SB_PAGEUP;
                SendMessageW(browserPane.Hwnd(), WM_VSCROLL, MAKEWPARAM(command, 0), 0);
            }
            const double elapsedSeconds = scrollTimer.ElapsedMilliseconds() / 1000.0;
            messagesPerSecond = elapsedSeconds > 0.0
                ? static_cast<double>(kScrollIterations) / elapsedSeconds
                : static_cast<double>(kScrollIterations);
        }

        DestroyWindow(hostWindow);
        UnregisterClassW(kHostClassName, instance);
        return messagesPerSecond;
    }

    void WriteResults(const fs::path& outputPath, const BenchmarkResults& results)
    {
        std::error_code error;
        if (outputPath.has_parent_path())
        {
            fs::create_directories(outputPath.parent_path(), error);
            Expect(!error, "failed to create benchmark output directory");
        }

        std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
        Expect(static_cast<bool>(output), "failed to create benchmark output");
        output << std::fixed << std::setprecision(2)
               << "{\n"
               << "  \"cacheHitAverageMs\": " << results.cacheHitAverageMs << ",\n"
               << "  \"storeEntriesPerSecond\": " << results.storeEntriesPerSecond << ",\n"
               << "  \"compactionMs\": " << results.compactionMs << ",\n"
               << "  \"cacheWorkerQueueDelayAverageMs\": " << results.cacheWorkerQueueDelayAverageMs << ",\n"
               << "  \"cacheWorkerQueueDelayMaxMs\": " << results.cacheWorkerQueueDelayMaxMs << ",\n"
               << "  \"scrollMessagesPerSecond\": " << results.scrollMessagesPerSecond << "\n"
               << "}\n";
        Expect(static_cast<bool>(output), "failed to write benchmark output");
    }
}

int wmain(int argc, wchar_t* argv[])
{
    try
    {
        fs::path outputPath = fs::current_path() / L"persistent-cache-benchmark.json";
        for (int index = 1; index + 1 < argc; ++index)
        {
            if (std::wstring_view(argv[index]) == L"--output")
            {
                outputPath = argv[++index];
            }
        }

        const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        Expect(SUCCEEDED(comResult), "failed to initialize COM for benchmark");
        ComCleanup comCleanup;

        INITCOMMONCONTROLSEX commonControls{};
        commonControls.dwSize = sizeof(commonControls);
        commonControls.dwICC = ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES;
        InitCommonControlsEx(&commonControls);

        TempFolder root(L"HyperBrowsePersistentCachePerformanceBenchmark");
        BenchmarkResults results;
        RunDiskBenchmark(root.Root(), &results);
        RunCacheWorkerQueueBenchmark(root.Root(), &results);
        results.scrollMessagesPerSecond = RunScrollBenchmark(GetModuleHandleW(nullptr));
        WriteResults(outputPath, results);
        return 0;
    }
    catch (const std::exception& exception)
    {
        OutputDebugStringA(exception.what());
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
