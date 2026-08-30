#pragma once

#include <windows.h>

#include <string>

namespace hyperbrowse::util
{
    inline constexpr wchar_t kSettingsRegistryEnvironmentVariable[] = L"HYPERBROWSE_SETTINGS_REGISTRY_PATH";
    inline constexpr wchar_t kDefaultSettingsRegistryPath[] = L"Software\\HyperBrowse";

    inline std::wstring SettingsRegistryPath()
    {
        const DWORD requiredLength = GetEnvironmentVariableW(kSettingsRegistryEnvironmentVariable, nullptr, 0);
        if (requiredLength == 0)
        {
            return kDefaultSettingsRegistryPath;
        }

        std::wstring path(requiredLength + 1, L'\0');
        const DWORD copiedLength = GetEnvironmentVariableW(
            kSettingsRegistryEnvironmentVariable,
            path.data(),
            static_cast<DWORD>(path.size()));
        if (copiedLength == 0 || copiedLength >= path.size())
        {
            return kDefaultSettingsRegistryPath;
        }

        path.resize(copiedLength);
        return path.empty() ? std::wstring(kDefaultSettingsRegistryPath) : path;
    }

    inline LSTATUS OpenSettingsRegistryKey(REGSAM desiredAccess, HKEY* key)
    {
        if (!key)
        {
            return ERROR_INVALID_PARAMETER;
        }

        const std::wstring path = SettingsRegistryPath();
        return RegOpenKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, desiredAccess, key);
    }

    inline LSTATUS CreateSettingsRegistryKey(REGSAM desiredAccess, HKEY* key)
    {
        if (!key)
        {
            return ERROR_INVALID_PARAMETER;
        }

        const std::wstring path = SettingsRegistryPath();
        DWORD disposition = 0;
        return RegCreateKeyExW(HKEY_CURRENT_USER,
                               path.c_str(),
                               0,
                               nullptr,
                               0,
                               desiredAccess,
                               nullptr,
                               key,
                               &disposition);
    }
}