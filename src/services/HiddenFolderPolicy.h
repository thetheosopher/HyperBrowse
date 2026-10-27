#pragma once

#include <windows.h>
#include <shlobj.h>

namespace hyperbrowse::services
{
    // Mirrors Explorer's "Hidden files and folders" and "Hide protected operating system files"
    // options so the folder tree lists the same folders Explorer would.
    struct HiddenFolderPolicy
    {
        bool showHidden{false};
        bool showProtectedSystem{false};

        bool IsVisible(DWORD attributes) const noexcept
        {
            if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_HIDDEN) == 0)
            {
                return true;
            }

            if (!showHidden)
            {
                return false;
            }

            return (attributes & FILE_ATTRIBUTE_SYSTEM) == 0 || showProtectedSystem;
        }

        static HiddenFolderPolicy FromShellSettings()
        {
            HiddenFolderPolicy policy;

            SHELLFLAGSTATE shellState{};
            SHGetSettings(&shellState, SSF_SHOWALLOBJECTS);
            policy.showHidden = shellState.fShowAllObjects != FALSE;

            DWORD showSuperHidden = 0;
            DWORD valueSize = sizeof(showSuperHidden);
            if (RegGetValueW(HKEY_CURRENT_USER,
                             L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced",
                             L"ShowSuperHidden",
                             RRF_RT_REG_DWORD,
                             nullptr,
                             &showSuperHidden,
                             &valueSize) == ERROR_SUCCESS)
            {
                policy.showProtectedSystem = showSuperHidden != 0;
            }

            return policy;
        }
    };
}
