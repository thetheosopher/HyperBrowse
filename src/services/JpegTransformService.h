#pragma once

#include <string>

namespace hyperbrowse::services
{
    bool ApplyLosslessJpegRotate(const std::wstring& filePath,
                                 int quarterTurnsDelta,
                                 std::wstring* errorMessage);
}