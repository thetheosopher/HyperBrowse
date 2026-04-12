#include "util/Log.h"

#include <windows.h>
#include <string>

namespace
{
    void WriteLine(std::wstring_view prefix, std::wstring_view message)
    {
        std::wstring line;
        line.reserve(prefix.size() + message.size() + 4);
        line.append(prefix);
        line.append(message);
        line.append(L"\r\n");
        OutputDebugStringW(line.c_str());
    }
}

namespace hyperbrowse::util
{
    void LogInfo(std::wstring_view message)
    {
        WriteLine(L"[INFO] ", message);
    }

    void LogError(std::wstring_view message)
    {
        WriteLine(L"[ERROR] ", message);
    }

    void LogLastError(std::wstring_view context)
    {
        const DWORD error = GetLastError();
        std::wstring message = std::wstring(context) + L" failed. GetLastError=" + std::to_wstring(error);
        LogError(message);
    }
}
