#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace hyperbrowse::decode
{
    struct RawHelperDecodedPixels
    {
        int bitmapWidth{};
        int bitmapHeight{};
        int sourceWidth{};
        int sourceHeight{};
        std::vector<unsigned char> bgraPixels;
    };

    bool WriteRawHelperPayload(const std::wstring& filePath,
                               const RawHelperDecodedPixels& payload,
                               std::wstring* errorMessage);
    bool ReadRawHelperPayload(const std::wstring& filePath,
                              RawHelperDecodedPixels* payload,
                              std::wstring* errorMessage);
}