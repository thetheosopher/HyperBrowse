#pragma once

#include <string>
#include <string_view>

namespace hyperbrowse::util
{
    std::wstring GetLogFilePath();
    std::wstring GetLogDirectory(std::wstring_view filePath = {});
    void LogInfo(std::wstring_view message);
    void LogError(std::wstring_view message);
    void LogLastError(std::wstring_view context);
}
