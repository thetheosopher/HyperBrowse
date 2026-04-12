#pragma once

#include <windows.h>
#include <string>
#include <string_view>

#include "util/Log.h"

namespace hyperbrowse::util
{
    class ScopedTimer
    {
    public:
        explicit ScopedTimer(std::wstring_view name)
            : name_(name)
        {
            QueryPerformanceFrequency(&frequency_);
            QueryPerformanceCounter(&start_);
        }

        ~ScopedTimer()
        {
            LARGE_INTEGER end{};
            QueryPerformanceCounter(&end);
            const auto elapsedTicks = end.QuadPart - start_.QuadPart;
            const double elapsedMs = (static_cast<double>(elapsedTicks) * 1000.0) / static_cast<double>(frequency_.QuadPart);
            LogInfo(std::wstring(name_) + L" took " + std::to_wstring(elapsedMs) + L" ms");
        }

    private:
        std::wstring name_;
        LARGE_INTEGER frequency_{};
        LARGE_INTEGER start_{};
    };
}
