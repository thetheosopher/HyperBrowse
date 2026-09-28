#include "ui/PerformanceHud.h"

#include <cmath>
#include <iomanip>
#include <sstream>

namespace hyperbrowse::ui
{
    namespace
    {
        std::wstring FormatMilliseconds(const std::optional<double>& value)
        {
            if (!value || !std::isfinite(*value) || *value < 0.0)
            {
                return L"Not available";
            }

            std::wostringstream stream;
            stream << std::fixed << std::setprecision(1) << *value << L" ms";
            return stream.str();
        }

        std::wstring FormatPercentage(const std::optional<double>& value)
        {
            if (!value || !std::isfinite(*value) || *value < 0.0 || *value > 100.0)
            {
                return L"Not available";
            }

            std::wostringstream stream;
            stream << std::fixed << std::setprecision(0) << *value << L"%";
            return stream.str();
        }
    }

    std::wstring FormatPerformanceHudText(const PerformanceHudSnapshot& snapshot)
    {
        std::wstring text;
        text.append(L"Active decodes: ").append(std::to_wstring(snapshot.activeDecodes));
        text.append(L"\r\nScale average: ").append(FormatMilliseconds(snapshot.scaleAverageMs));
        text.append(L"\r\nThumbnail cache hit rate: ").append(FormatPercentage(snapshot.thumbnailCacheHitRatePercent));
        text.append(L"\r\nMemory pressure: ").append(snapshot.memoryPressureActive ? L"Active" : L"Normal");
        text.append(L"\r\nThumbnail queue: ").append(std::to_wstring(snapshot.pendingThumbnailJobs));
        return text;
    }
}
