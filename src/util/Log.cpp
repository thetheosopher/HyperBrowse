#include "util/Log.h"

#include <windows.h>
#include <string>

namespace
{
    std::wstring LogFilePath()
    {
        wchar_t tempPath[MAX_PATH]{};
        const DWORD length = GetTempPathW(static_cast<DWORD>(std::size(tempPath)), tempPath);
        if (length == 0 || length >= std::size(tempPath))
        {
            return L"HyperBrowse-debug.log";
        }

        std::wstring path(tempPath, tempPath + length);
        if (!path.empty() && path.back() != L'\\')
        {
            path.push_back(L'\\');
        }

        path.append(L"HyperBrowse-debug.log");
        return path;
    }

    std::wstring TimestampPrefix()
    {
        SYSTEMTIME localTime{};
        GetLocalTime(&localTime);

        wchar_t buffer[32]{};
        swprintf_s(buffer,
                   L"[%02u:%02u:%02u.%03u] ",
                   localTime.wHour,
                   localTime.wMinute,
                   localTime.wSecond,
                   localTime.wMilliseconds);
        return buffer;
    }

    void AppendLineToFile(std::wstring_view line)
    {
        const std::wstring path = LogFilePath();
        HANDLE handle = CreateFileW(path.c_str(),
                                    FILE_APPEND_DATA,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                                    nullptr,
                                    OPEN_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL,
                                    nullptr);
        if (handle == INVALID_HANDLE_VALUE)
        {
            return;
        }

        const int utf8Length = WideCharToMultiByte(CP_UTF8, 0, line.data(), static_cast<int>(line.size()), nullptr, 0, nullptr, nullptr);
        if (utf8Length > 0)
        {
            std::string utf8Line(static_cast<std::size_t>(utf8Length), '\0');
            WideCharToMultiByte(CP_UTF8, 0, line.data(), static_cast<int>(line.size()), utf8Line.data(), utf8Length, nullptr, nullptr);
            DWORD written = 0;
            WriteFile(handle, utf8Line.data(), static_cast<DWORD>(utf8Line.size()), &written, nullptr);
        }

        CloseHandle(handle);
    }

    void WriteLine(std::wstring_view prefix, std::wstring_view message)
    {
        std::wstring line;
        const std::wstring timestamp = TimestampPrefix();
        line.reserve(timestamp.size() + prefix.size() + message.size() + 4);
        line.append(timestamp);
        line.append(prefix);
        line.append(message);
        line.append(L"\r\n");
        OutputDebugStringW(line.c_str());
        AppendLineToFile(line);
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
