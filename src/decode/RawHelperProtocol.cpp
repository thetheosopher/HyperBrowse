#include "decode/RawHelperProtocol.h"

#include <filesystem>
#include <fstream>
#include <limits>

namespace fs = std::filesystem;

namespace
{
    constexpr std::uint32_t kRawHelperMagic = 0x52425748; // 'HWBR'
    constexpr std::uint32_t kRawHelperVersion = 1;
    constexpr std::uint32_t kMaximumBitmapDimension = 32768;
    constexpr std::uint64_t kMaximumPixelBytes = 512ULL * 1024ULL * 1024ULL;

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

    bool TryComputePixelBytes(std::uint64_t width,
                              std::uint64_t height,
                              std::uint64_t* pixelBytes) noexcept
    {
        if (!pixelBytes
            || width == 0
            || height == 0
            || width > kMaximumBitmapDimension
            || height > kMaximumBitmapDimension
            || width > std::numeric_limits<std::uint64_t>::max() / height)
        {
            return false;
        }

        const std::uint64_t pixelCount = width * height;
        if (pixelCount > kMaximumPixelBytes / 4ULL)
        {
            return false;
        }

        *pixelBytes = pixelCount * 4ULL;
        return true;
    }

    bool SetError(std::wstring* errorMessage, const wchar_t* message)
    {
        if (errorMessage)
        {
            *errorMessage = message;
        }
        return false;
    }
}

namespace hyperbrowse::decode
{
    bool WriteRawHelperPayload(const std::wstring& filePath,
                               const RawHelperDecodedPixels& payload,
                               std::wstring* errorMessage)
    {
        std::uint64_t expectedBytes = 0;
        if (!TryComputePixelBytes(static_cast<std::uint64_t>(payload.bitmapWidth),
                                  static_cast<std::uint64_t>(payload.bitmapHeight),
                                  &expectedBytes))
        {
            return SetError(errorMessage, L"The RAW helper payload does not contain valid or supported bitmap dimensions.");
        }

        if (payload.sourceWidth < 0
            || payload.sourceHeight < 0
            || payload.sourceWidth > static_cast<int>(kMaximumBitmapDimension)
            || payload.sourceHeight > static_cast<int>(kMaximumBitmapDimension)
            || expectedBytes > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())
            || payload.bgraPixels.size() != static_cast<std::size_t>(expectedBytes))
        {
            return SetError(errorMessage, L"The RAW helper payload pixel buffer does not match the bitmap dimensions.");
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

        std::uint64_t expectedBytes = 0;
        if (!TryComputePixelBytes(header.bitmapWidth, header.bitmapHeight, &expectedBytes)
            || header.sourceWidth > kMaximumBitmapDimension
            || header.sourceHeight > kMaximumBitmapDimension
            || header.sourceWidth > static_cast<std::uint32_t>(std::numeric_limits<int>::max())
            || header.sourceHeight > static_cast<std::uint32_t>(std::numeric_limits<int>::max()))
        {
            return SetError(errorMessage, L"The RAW helper output file does not contain valid or supported bitmap dimensions.");
        }

        if (header.pixelBytes != expectedBytes)
        {
            return SetError(errorMessage, L"The RAW helper output payload size does not match the bitmap dimensions.");
        }

        input.seekg(0, std::ios::end);
        const std::streamoff fileSize = input.tellg();
        if (fileSize < 0
            || static_cast<std::uint64_t>(fileSize) != sizeof(RawHelperFileHeader) + expectedBytes)
        {
            return SetError(errorMessage, L"The RAW helper output file length does not match its header.");
        }

        input.seekg(static_cast<std::streamoff>(sizeof(RawHelperFileHeader)), std::ios::beg);
        if (!input)
        {
            return SetError(errorMessage, L"The RAW helper output file could not be positioned for reading.");
        }

        try
        {
            payload->bgraPixels.resize(static_cast<std::size_t>(expectedBytes));
        }
        catch (const std::exception&)
        {
            payload->bgraPixels.clear();
            return SetError(errorMessage, L"The RAW helper output payload is too large to load.");
        }

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

        payload->bitmapWidth = static_cast<int>(header.bitmapWidth);
        payload->bitmapHeight = static_cast<int>(header.bitmapHeight);
        payload->sourceWidth = static_cast<int>(header.sourceWidth);
        payload->sourceHeight = static_cast<int>(header.sourceHeight);
        return true;
    }
}