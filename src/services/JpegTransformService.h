#pragma once

#include <string>

namespace hyperbrowse::services
{
    bool AdjustJpegOrientation(const std::wstring& filePath,
                               int quarterTurnsDelta,
                               std::wstring* errorMessage);
}