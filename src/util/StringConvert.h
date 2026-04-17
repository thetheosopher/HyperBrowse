#pragma once

#include <windows.h>

#include <string>
#include <string_view>

namespace hyperbrowse::util
{
    // Convert a narrow byte sequence to wide using the given Windows code page.
    // Falls back to a lossy ASCII-promoted copy if the conversion fails so the caller
    // always receives a non-empty diagnostic when input is non-empty.
    inline std::wstring WidenWithCodePage(std::string_view text, UINT codePage) noexcept
    {
        if (text.empty())
        {
            return {};
        }

        const int sizeNeeded = MultiByteToWideChar(
            codePage,
            0,
            text.data(),
            static_cast<int>(text.size()),
            nullptr,
            0);
        if (sizeNeeded > 0)
        {
            std::wstring result(static_cast<std::size_t>(sizeNeeded), L'\0');
            const int written = MultiByteToWideChar(
                codePage,
                0,
                text.data(),
                static_cast<int>(text.size()),
                result.data(),
                sizeNeeded);
            if (written > 0)
            {
                result.resize(static_cast<std::size_t>(written));
                return result;
            }
        }

        // Fallback: promote each byte to its low-7-bit equivalent. Any non-ASCII byte
        // becomes a replacement character so we never emit potentially-misleading garbage.
        std::wstring fallback;
        fallback.reserve(text.size());
        for (unsigned char byte : text)
        {
            fallback.push_back(byte < 0x80 ? static_cast<wchar_t>(byte) : L'?');
        }
        return fallback;
    }

    // std::exception::what() returns text in the system code page. Use this whenever
    // surfacing an exception message into a wide-character UI surface.
    inline std::wstring WidenExceptionMessage(const char* message) noexcept
    {
        if (!message)
        {
            return {};
        }
        return WidenWithCodePage(std::string_view{message}, CP_ACP);
    }

    // Case-insensitive ordinal comparison without allocating. Faster and correct for
    // identifier-style strings (paths, property names) than _wcsicmp on string_views,
    // which would force a copy to obtain a null-terminated buffer.
    inline bool EqualsIgnoreCaseOrdinal(std::wstring_view lhs, std::wstring_view rhs) noexcept
    {
        if (lhs.size() != rhs.size())
        {
            return false;
        }
        if (lhs.empty())
        {
            return true;
        }
        const int result = CompareStringOrdinal(
            lhs.data(),
            static_cast<int>(lhs.size()),
            rhs.data(),
            static_cast<int>(rhs.size()),
            TRUE);
        return result == CSTR_EQUAL;
    }
}
