#pragma once

#include <string_view>

namespace hyperbrowse::util
{
    void LogInfo(std::wstring_view message);
    void LogError(std::wstring_view message);
    void LogLastError(std::wstring_view context);
}
