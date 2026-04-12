#include "services/JpegTransformService.h"

#include <array>
#include <cstdint>
#include <fstream>
#include <vector>

namespace
{
    std::uint16_t ReadUInt16(const std::vector<std::uint8_t>& bytes, std::size_t offset, bool littleEndian)
    {
        if (littleEndian)
        {
            return static_cast<std::uint16_t>(bytes[offset])
                | (static_cast<std::uint16_t>(bytes[offset + 1]) << 8);
        }

        return (static_cast<std::uint16_t>(bytes[offset]) << 8)
            | static_cast<std::uint16_t>(bytes[offset + 1]);
    }

    std::uint32_t ReadUInt32(const std::vector<std::uint8_t>& bytes, std::size_t offset, bool littleEndian)
    {
        if (littleEndian)
        {
            return static_cast<std::uint32_t>(bytes[offset])
                | (static_cast<std::uint32_t>(bytes[offset + 1]) << 8)
                | (static_cast<std::uint32_t>(bytes[offset + 2]) << 16)
                | (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
        }

        return (static_cast<std::uint32_t>(bytes[offset]) << 24)
            | (static_cast<std::uint32_t>(bytes[offset + 1]) << 16)
            | (static_cast<std::uint32_t>(bytes[offset + 2]) << 8)
            | static_cast<std::uint32_t>(bytes[offset + 3]);
    }

    void WriteUInt16(std::vector<std::uint8_t>* bytes, std::size_t offset, bool littleEndian, std::uint16_t value)
    {
        if (littleEndian)
        {
            (*bytes)[offset] = static_cast<std::uint8_t>(value & 0xff);
            (*bytes)[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xff);
            return;
        }

        (*bytes)[offset] = static_cast<std::uint8_t>((value >> 8) & 0xff);
        (*bytes)[offset + 1] = static_cast<std::uint8_t>(value & 0xff);
    }

    std::uint16_t RotateRightOrientation(std::uint16_t orientation)
    {
        static constexpr std::array<std::uint16_t, 9> kRotationMap{0, 6, 7, 8, 5, 2, 3, 4, 1};
        return orientation < kRotationMap.size() ? kRotationMap[orientation] : 6;
    }

    int NormalizeClockwiseQuarterTurns(int quarterTurnsDelta)
    {
        return ((quarterTurnsDelta % 4) + 4) % 4;
    }
}

namespace hyperbrowse::services
{
    bool AdjustJpegOrientation(const std::wstring& filePath,
                               int quarterTurnsDelta,
                               std::wstring* errorMessage)
    {
        if (quarterTurnsDelta == 0)
        {
            return true;
        }

        std::ifstream input(filePath, std::ios::binary);
        if (!input)
        {
            if (errorMessage)
            {
                *errorMessage = L"Failed to open the JPEG file for orientation adjustment.";
            }
            return false;
        }

        std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        if (bytes.size() < 4 || bytes[0] != 0xff || bytes[1] != 0xd8)
        {
            if (errorMessage)
            {
                *errorMessage = L"The selected file is not a valid JPEG stream.";
            }
            return false;
        }

        bool updated = false;
        for (std::size_t offset = 2; offset + 4 <= bytes.size();)
        {
            if (bytes[offset] != 0xff)
            {
                break;
            }

            const std::uint8_t marker = bytes[offset + 1];
            if (marker == 0xda || marker == 0xd9)
            {
                break;
            }

            if (offset + 4 > bytes.size())
            {
                break;
            }

            const std::uint16_t segmentLength = static_cast<std::uint16_t>((bytes[offset + 2] << 8) | bytes[offset + 3]);
            if (segmentLength < 2 || offset + 2 + segmentLength > bytes.size())
            {
                break;
            }

            const std::size_t segmentDataOffset = offset + 4;
            const std::size_t segmentDataSize = segmentLength - 2;
            if (marker == 0xe1 && segmentDataSize >= 14
                && bytes[segmentDataOffset + 0] == 'E'
                && bytes[segmentDataOffset + 1] == 'x'
                && bytes[segmentDataOffset + 2] == 'i'
                && bytes[segmentDataOffset + 3] == 'f'
                && bytes[segmentDataOffset + 4] == 0
                && bytes[segmentDataOffset + 5] == 0)
            {
                const std::size_t tiffOffset = segmentDataOffset + 6;
                const bool littleEndian = bytes[tiffOffset] == 'I' && bytes[tiffOffset + 1] == 'I';
                if (!littleEndian && !(bytes[tiffOffset] == 'M' && bytes[tiffOffset + 1] == 'M'))
                {
                    break;
                }

                const std::uint32_t ifdOffset = ReadUInt32(bytes, tiffOffset + 4, littleEndian);
                const std::size_t ifdPosition = tiffOffset + ifdOffset;
                if (ifdPosition + 2 > bytes.size())
                {
                    break;
                }

                const std::uint16_t entryCount = ReadUInt16(bytes, ifdPosition, littleEndian);
                for (std::uint16_t entryIndex = 0; entryIndex < entryCount; ++entryIndex)
                {
                    const std::size_t entryOffset = ifdPosition + 2 + (static_cast<std::size_t>(entryIndex) * 12U);
                    if (entryOffset + 12 > bytes.size())
                    {
                        break;
                    }

                    const std::uint16_t tag = ReadUInt16(bytes, entryOffset, littleEndian);
                    const std::uint16_t type = ReadUInt16(bytes, entryOffset + 2, littleEndian);
                    const std::uint32_t count = ReadUInt32(bytes, entryOffset + 4, littleEndian);
                    if (tag != 0x0112 || type != 3 || count != 1)
                    {
                        continue;
                    }

                    std::uint16_t orientation = ReadUInt16(bytes, entryOffset + 8, littleEndian);
                    if (orientation < 1 || orientation > 8)
                    {
                        orientation = 1;
                    }

                    const int clockwiseTurns = NormalizeClockwiseQuarterTurns(quarterTurnsDelta);
                    for (int step = 0; step < clockwiseTurns; ++step)
                    {
                        orientation = RotateRightOrientation(orientation);
                    }

                    WriteUInt16(&bytes, entryOffset + 8, littleEndian, orientation);
                    updated = true;
                    break;
                }

                if (updated)
                {
                    break;
                }
            }

            offset += static_cast<std::size_t>(segmentLength) + 2;
        }

        if (!updated)
        {
            if (errorMessage)
            {
                *errorMessage = L"The JPEG file does not expose an editable EXIF orientation tag.";
            }
            return false;
        }

        std::ofstream output(filePath, std::ios::binary | std::ios::trunc);
        if (!output)
        {
            if (errorMessage)
            {
                *errorMessage = L"Failed to reopen the JPEG file for writing the adjusted orientation metadata.";
            }
            return false;
        }

        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!output)
        {
            if (errorMessage)
            {
                *errorMessage = L"Failed to write the updated EXIF orientation metadata back to disk.";
            }
            return false;
        }

        return true;
    }
}