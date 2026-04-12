#include "decode/RawHelperProtocol.h"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace
{
    constexpr std::uint32_t kRawHelperMagic = 0x52425748; // 'HWBR'
    constexpr std::uint32_t kRawHelperVersion = 1;

    struct RawHelperFileHeader
    {
        std::uint32_t magic{};
        std::uint32_t version{};
        std::uint32_t bitmapWidth{};
        std::uint32_t bitmapHeight{};
        std::uint32_t sourceWidth{};
        std::uint32_t sourceHeight{};
        std::uint64_t pixelBytes{};
    };
}

namespace hyperbrowse::decode
{
    bool WriteRawHelperPayload(const std::wstring& filePath,
                               const RawHelperDecodedPixels& payload,
                               std::wstring* errorMessage)
    {
        if (payload.bitmapWidth <= 0 || payload.bitmapHeight <= 0)
        {
            if (errorMessage)
            {
                *errorMessage = L"The RAW helper payload does not contain valid bitmap dimensions.";
            }
            return false;
        }

        const std::size_t expectedBytes = static_cast<std::size_t>(payload.bitmapWidth)
            * static_cast<std::size_t>(payload.bitmapHeight)
            * 4U;
        if (payload.bgraPixels.size() != expectedBytes)
        {
            if (errorMessage)
            {
                *errorMessage = L"The RAW helper payload pixel buffer does not match the bitmap dimensions.";
            }
            return false;
        }

        std::ofstream output(fs::path(filePath), std::ios::binary | std::ios::trunc);
        if (!output)
        {
            if (errorMessage)
            {
                *errorMessage = L"Failed to open the RAW helper output file for writing.";
            }
            return false;
        }

        const RawHelperFileHeader header{
            kRawHelperMagic,
            kRawHelperVersion,
            static_cast<std::uint32_t>(payload.bitmapWidth),
            static_cast<std::uint32_t>(payload.bitmapHeight),
            static_cast<std::uint32_t>(payload.sourceWidth),
            static_cast<std::uint32_t>(payload.sourceHeight),
            static_cast<std::uint64_t>(payload.bgraPixels.size()),
        };

        output.write(reinterpret_cast<const char*>(&header), sizeof(header));
        output.write(reinterpret_cast<const char*>(payload.bgraPixels.data()),
                     static_cast<std::streamsize>(payload.bgraPixels.size()));
        if (!output)
        {
            if (errorMessage)
            {
                *errorMessage = L"Failed to write the RAW helper output payload.";
            }
            return false;
        }

        return true;
    }

    bool ReadRawHelperPayload(const std::wstring& filePath,
                              RawHelperDecodedPixels* payload,
                              std::wstring* errorMessage)
    {
        if (!payload)
        {
            return false;
        }

        payload->bgraPixels.clear();
        payload->bitmapWidth = 0;
        payload->bitmapHeight = 0;
        payload->sourceWidth = 0;
        payload->sourceHeight = 0;

        std::ifstream input(fs::path(filePath), std::ios::binary);
        if (!input)
        {
            if (errorMessage)
            {
                *errorMessage = L"Failed to open the RAW helper output file for reading.";
            }
            return false;
        }

        RawHelperFileHeader header{};
        input.read(reinterpret_cast<char*>(&header), sizeof(header));
        if (!input)
        {
            if (errorMessage)
            {
                *errorMessage = L"The RAW helper output file is truncated.";
            }
            return false;
        }

        if (header.magic != kRawHelperMagic || header.version != kRawHelperVersion)
        {
            if (errorMessage)
            {
                *errorMessage = L"The RAW helper output file uses an unknown format.";
            }
            return false;
        }

        if (header.bitmapWidth == 0 || header.bitmapHeight == 0)
        {
            if (errorMessage)
            {
                *errorMessage = L"The RAW helper output file does not contain valid bitmap dimensions.";
            }
            return false;
        }

        const std::uint64_t expectedBytes = static_cast<std::uint64_t>(header.bitmapWidth)
            * static_cast<std::uint64_t>(header.bitmapHeight)
            * 4ULL;
        if (header.pixelBytes != expectedBytes)
        {
            if (errorMessage)
            {
                *errorMessage = L"The RAW helper output payload size does not match the bitmap dimensions.";
            }
            return false;
        }

        payload->bitmapWidth = static_cast<int>(header.bitmapWidth);
        payload->bitmapHeight = static_cast<int>(header.bitmapHeight);
        payload->sourceWidth = static_cast<int>(header.sourceWidth);
        payload->sourceHeight = static_cast<int>(header.sourceHeight);
        payload->bgraPixels.resize(static_cast<std::size_t>(header.pixelBytes));

        input.read(reinterpret_cast<char*>(payload->bgraPixels.data()),
                   static_cast<std::streamsize>(payload->bgraPixels.size()));
        if (!input)
        {
            payload->bgraPixels.clear();
            if (errorMessage)
            {
                *errorMessage = L"The RAW helper output pixel buffer is truncated.";
            }
            return false;
        }

        return true;
    }
}