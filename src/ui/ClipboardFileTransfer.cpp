#include "ui/ClipboardFileTransfer.h"

#include <shellapi.h>
#include <shlobj.h>

#include <cstring>
#include <limits>

namespace hyperbrowse::ui
{
    bool CopyTextToClipboard(HWND ownerWindow, std::wstring_view text)
    {
        if (!OpenClipboard(ownerWindow))
        {
            return false;
        }

        if (!EmptyClipboard())
        {
            CloseClipboard();
            return false;
        }

        const std::size_t characterCount = text.size() + 1;
        if (characterCount > std::numeric_limits<SIZE_T>::max() / sizeof(wchar_t))
        {
            CloseClipboard();
            return false;
        }

        HGLOBAL buffer = GlobalAlloc(GMEM_MOVEABLE, characterCount * sizeof(wchar_t));
        if (!buffer)
        {
            CloseClipboard();
            return false;
        }

        void* locked = GlobalLock(buffer);
        if (!locked)
        {
            GlobalFree(buffer);
            CloseClipboard();
            return false;
        }

        std::memcpy(locked, text.data(), text.size() * sizeof(wchar_t));
        static_cast<wchar_t*>(locked)[text.size()] = L'\0';
        GlobalUnlock(buffer);

        if (!SetClipboardData(CF_UNICODETEXT, buffer))
        {
            GlobalFree(buffer);
            CloseClipboard();
            return false;
        }

        CloseClipboard();
        return true;
    }

    bool CopyFilePathsToClipboard(HWND ownerWindow,
                                  const std::vector<std::wstring>& paths,
                                  bool movePreferred)
    {
        if (paths.empty())
        {
            return false;
        }

        std::size_t pathCharacters = 0;
        for (const std::wstring& path : paths)
        {
            if (path.size() > std::numeric_limits<std::size_t>::max() - pathCharacters - 1)
            {
                return false;
            }
            pathCharacters += path.size() + 1;
        }

        if (pathCharacters > std::numeric_limits<SIZE_T>::max() / sizeof(wchar_t) - 1
            || pathCharacters * sizeof(wchar_t) > std::numeric_limits<SIZE_T>::max() - sizeof(DROPFILES))
        {
            return false;
        }

        const SIZE_T totalBytes = sizeof(DROPFILES)
            + static_cast<SIZE_T>(pathCharacters + 1) * sizeof(wchar_t);
        HGLOBAL buffer = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, totalBytes);
        if (!buffer)
        {
            return false;
        }

        auto* dropFiles = static_cast<DROPFILES*>(GlobalLock(buffer));
        if (!dropFiles)
        {
            GlobalFree(buffer);
            return false;
        }

        dropFiles->pFiles = sizeof(DROPFILES);
        dropFiles->fWide = TRUE;
        wchar_t* cursor = reinterpret_cast<wchar_t*>(reinterpret_cast<BYTE*>(dropFiles) + sizeof(DROPFILES));
        for (const std::wstring& path : paths)
        {
            std::memcpy(cursor, path.data(), path.size() * sizeof(wchar_t));
            cursor += path.size() + 1;
        }
        GlobalUnlock(buffer);

        HGLOBAL effectBuffer = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(DWORD));
        if (effectBuffer)
        {
            if (DWORD* effect = static_cast<DWORD*>(GlobalLock(effectBuffer)))
            {
                *effect = movePreferred ? DROPEFFECT_MOVE : DROPEFFECT_COPY;
                GlobalUnlock(effectBuffer);
            }
            else
            {
                GlobalFree(effectBuffer);
                effectBuffer = nullptr;
            }
        }

        if (!OpenClipboard(ownerWindow))
        {
            GlobalFree(buffer);
            if (effectBuffer)
            {
                GlobalFree(effectBuffer);
            }
            return false;
        }

        bool success = EmptyClipboard() != FALSE;
        if (success && SetClipboardData(CF_HDROP, buffer) == nullptr)
        {
            success = false;
        }
        if (success && effectBuffer)
        {
            const UINT effectFormat = RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT);
            if (SetClipboardData(effectFormat, effectBuffer) == nullptr)
            {
                GlobalFree(effectBuffer);
            }
        }
        if (!success)
        {
            GlobalFree(buffer);
        }
        CloseClipboard();

        return success;
    }

    std::vector<std::wstring> ReadClipboardFilePaths(HWND ownerWindow, DWORD* preferredDropEffect)
    {
        std::vector<std::wstring> paths;
        if (preferredDropEffect)
        {
            *preferredDropEffect = DROPEFFECT_COPY;
        }

        if (!OpenClipboard(ownerWindow))
        {
            return paths;
        }

        if (IsClipboardFormatAvailable(CF_HDROP))
        {
            if (HGLOBAL data = GetClipboardData(CF_HDROP))
            {
                const SIZE_T dataBytes = GlobalSize(data);
                auto* dropFiles = static_cast<const DROPFILES*>(GlobalLock(data));
                if (dropFiles
                    && dataBytes >= sizeof(DROPFILES)
                    && dropFiles->fWide
                    && dropFiles->pFiles <= dataBytes)
                {
                    const auto* cursor = reinterpret_cast<const wchar_t*>(
                        reinterpret_cast<const BYTE*>(dropFiles) + dropFiles->pFiles);
                    const SIZE_T remainingBytes = dataBytes - dropFiles->pFiles;
                    const SIZE_T remainingCharacters = remainingBytes / sizeof(wchar_t);
                    SIZE_T consumedCharacters = 0;
                    while (consumedCharacters < remainingCharacters && cursor[consumedCharacters] != L'\0')
                    {
                        const SIZE_T pathStart = consumedCharacters;
                        while (consumedCharacters < remainingCharacters && cursor[consumedCharacters] != L'\0')
                        {
                            ++consumedCharacters;
                        }
                        if (consumedCharacters > pathStart)
                        {
                            paths.emplace_back(cursor + pathStart, consumedCharacters - pathStart);
                        }
                        if (consumedCharacters < remainingCharacters)
                        {
                            ++consumedCharacters;
                        }
                    }
                }
                if (dropFiles)
                {
                    GlobalUnlock(data);
                }
            }

            if (preferredDropEffect)
            {
                const UINT effectFormat = RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT);
                if (HGLOBAL effectData = GetClipboardData(effectFormat))
                {
                    if (GlobalSize(effectData) >= sizeof(DWORD))
                    {
                        if (const DWORD* effect = static_cast<const DWORD*>(GlobalLock(effectData)))
                        {
                            *preferredDropEffect = *effect;
                            GlobalUnlock(effectData);
                        }
                    }
                }
            }
        }

        CloseClipboard();
        return paths;
    }
}
