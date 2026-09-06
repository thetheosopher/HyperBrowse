#include "ui/WindowBoundsPersistence.h"

#include <limits>
#include <string>

namespace hyperbrowse::ui
{
    namespace
    {
        constexpr std::wstring_view kWindowLeftValue = L"WindowLeft";
        constexpr std::wstring_view kWindowTopValue = L"WindowTop";
        constexpr std::wstring_view kWindowWidthValue = L"WindowWidth";
        constexpr std::wstring_view kWindowHeightValue = L"WindowHeight";
    }

    std::optional<RECT> WindowBoundsPersistence::Load(const ReadDword& readDword)
    {
        DWORD leftValue = 0;
        DWORD topValue = 0;
        DWORD widthValue = 0;
        DWORD heightValue = 0;
        if (!readDword(kWindowLeftValue, &leftValue)
            || !readDword(kWindowTopValue, &topValue)
            || !readDword(kWindowWidthValue, &widthValue)
            || !readDword(kWindowHeightValue, &heightValue))
        {
            return std::nullopt;
        }

        if (widthValue > static_cast<DWORD>(std::numeric_limits<LONG>::max())
            || heightValue > static_cast<DWORD>(std::numeric_limits<LONG>::max()))
        {
            return std::nullopt;
        }

        const LONG left = static_cast<LONG>(leftValue);
        const LONG top = static_cast<LONG>(topValue);
        const LONG width = static_cast<LONG>(widthValue);
        const LONG height = static_cast<LONG>(heightValue);
        if (width <= 0 || height <= 0)
        {
            return std::nullopt;
        }

        const long long right = static_cast<long long>(left) + static_cast<long long>(width);
        const long long bottom = static_cast<long long>(top) + static_cast<long long>(height);
        if (right < static_cast<long long>(std::numeric_limits<LONG>::min())
            || right > static_cast<long long>(std::numeric_limits<LONG>::max())
            || bottom < static_cast<long long>(std::numeric_limits<LONG>::min())
            || bottom > static_cast<long long>(std::numeric_limits<LONG>::max()))
        {
            return std::nullopt;
        }

        return RECT{left, top, static_cast<LONG>(right), static_cast<LONG>(bottom)};
    }

    bool WindowBoundsPersistence::IsWithinWorkArea(const RECT& bounds,
                                                    LONG minimumWidth,
                                                    LONG minimumHeight,
                                                    const RECT& workArea)
    {
        const long long width = static_cast<long long>(bounds.right) - static_cast<long long>(bounds.left);
        const long long height = static_cast<long long>(bounds.bottom) - static_cast<long long>(bounds.top);
        if (width < minimumWidth || height < minimumHeight)
        {
            return false;
        }

        return bounds.left >= workArea.left
            && bounds.top >= workArea.top
            && bounds.right <= workArea.right
            && bounds.bottom <= workArea.bottom;
    }

    bool WindowBoundsPersistence::Save(const RECT& bounds,
                                       LONG minimumWidth,
                                       LONG minimumHeight,
                                       const WriteDword& writeDword)
    {
        const long long width = static_cast<long long>(bounds.right) - static_cast<long long>(bounds.left);
        const long long height = static_cast<long long>(bounds.bottom) - static_cast<long long>(bounds.top);
        if (width < minimumWidth
            || height < minimumHeight
            || width > static_cast<long long>(std::numeric_limits<LONG>::max())
            || height > static_cast<long long>(std::numeric_limits<LONG>::max()))
        {
            return false;
        }

        writeDword(kWindowLeftValue, static_cast<DWORD>(bounds.left));
        writeDword(kWindowTopValue, static_cast<DWORD>(bounds.top));
        writeDword(kWindowWidthValue, static_cast<DWORD>(width));
        writeDword(kWindowHeightValue, static_cast<DWORD>(height));
        return true;
    }
}
