#include "services/JpegTransformService.h"

#include "decode/WicDecodeHelpers.h"

#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    using Byte = std::uint8_t;
    using Microsoft::WRL::ComPtr;

    constexpr std::array<Byte, 6> kExifSignature{ 'E', 'x', 'i', 'f', 0, 0 };
    constexpr std::uint16_t kOrientationTag = 0x0112;
    constexpr std::uint16_t kTiffShortType = 3;

    class FileHandle final
    {
    public:
        explicit FileHandle(HANDLE handle = INVALID_HANDLE_VALUE) noexcept
            : handle_(handle)
        {
        }

        ~FileHandle()
        {
            Reset();
        }

        FileHandle(const FileHandle&) = delete;
        FileHandle& operator=(const FileHandle&) = delete;

        HANDLE Get() const noexcept
        {
            return handle_;
        }

        explicit operator bool() const noexcept
        {
            return handle_ != INVALID_HANDLE_VALUE;
        }

        HANDLE Release() noexcept
        {
            const HANDLE handle = handle_;
            handle_ = INVALID_HANDLE_VALUE;
            return handle;
        }

        bool Close() noexcept
        {
            if (handle_ == INVALID_HANDLE_VALUE)
            {
                return true;
            }
            const HANDLE handle = Release();
            return CloseHandle(handle) != FALSE;
        }

        void Reset(HANDLE handle = INVALID_HANDLE_VALUE) noexcept
        {
            if (handle_ != INVALID_HANDLE_VALUE)
            {
                CloseHandle(handle_);
            }
            handle_ = handle;
        }

    private:
        HANDLE handle_;
    };

    struct TemporaryPathCleanup
    {
        std::filesystem::path path;
        bool published{};

        ~TemporaryPathCleanup()
        {
            if (!published && !path.empty())
            {
                DeleteFileW(path.c_str());
            }
        }
    };

    bool Fail(std::wstring* errorMessage, std::wstring_view message)
    {
        if (errorMessage)
        {
            errorMessage->assign(message);
        }
        return false;
    }

    bool FailWithWin32Error(std::wstring* errorMessage,
                            std::wstring_view operation,
                            DWORD errorCode)
    {
        if (errorCode == ERROR_SUCCESS)
        {
            errorCode = ERROR_GEN_FAILURE;
        }
        hyperbrowse::decode::wic_support::SetError(
            errorMessage,
            operation,
            HRESULT_FROM_WIN32(errorCode));
        return false;
    }

    bool ValidateJpegWithWic(const std::wstring& filePath, std::wstring* errorMessage)
    {
        hyperbrowse::decode::wic_support::ComInitializationScope comInitialization(
            COINIT_MULTITHREADED,
            errorMessage,
            L"Failed to initialize COM for JPEG orientation adjustment.");
        if (!comInitialization.Succeeded())
        {
            return false;
        }

        ComPtr<IWICImagingFactory> factory;
        if (!hyperbrowse::decode::wic_support::InitializeWicFactory(&factory, errorMessage))
        {
            return false;
        }

        ComPtr<IWICBitmapDecoder> decoder;
        HRESULT result = factory->CreateDecoderFromFilename(
            filePath.c_str(),
            nullptr,
            GENERIC_READ,
            WICDecodeMetadataCacheOnDemand,
            decoder.GetAddressOf());
        if (FAILED(result))
        {
            hyperbrowse::decode::wic_support::SetError(
                errorMessage,
                L"Failed to open the JPEG file for orientation adjustment.",
                result);
            return false;
        }
        if (!decoder)
        {
            return Fail(errorMessage, L"WIC returned no decoder for the JPEG file.");
        }

        ComPtr<IWICBitmapFrameDecode> frame;
        result = decoder->GetFrame(0, frame.GetAddressOf());
        if (FAILED(result))
        {
            hyperbrowse::decode::wic_support::SetError(
                errorMessage,
                L"Failed to read the JPEG frame for orientation adjustment.",
                result);
            return false;
        }
        if (!frame)
        {
            return Fail(errorMessage, L"WIC returned no frame for the JPEG file.");
        }
        return true;
    }

    bool ReadBytesAt(HANDLE file,
                     std::uint64_t fileSize,
                     std::uint64_t offset,
                     std::span<Byte> bytes,
                     std::wstring* errorMessage)
    {
        if (offset > fileSize || bytes.size() > fileSize - offset)
        {
            return Fail(errorMessage, L"The JPEG contains a truncated marker or metadata segment.");
        }

        LARGE_INTEGER position{};
        position.QuadPart = static_cast<LONGLONG>(offset);
        if (!SetFilePointerEx(file, position, nullptr, FILE_BEGIN))
        {
            return FailWithWin32Error(errorMessage, L"Seeking in the JPEG file failed.", GetLastError());
        }

        std::size_t completed = 0;
        while (completed < bytes.size())
        {
            const DWORD requested = static_cast<DWORD>(std::min<std::size_t>(
                bytes.size() - completed,
                std::numeric_limits<DWORD>::max()));
            DWORD bytesRead = 0;
            if (!ReadFile(file, bytes.data() + completed, requested, &bytesRead, nullptr))
            {
                return FailWithWin32Error(errorMessage, L"Reading the JPEG file failed.", GetLastError());
            }
            if (bytesRead == 0)
            {
                return FailWithWin32Error(errorMessage, L"Reading the JPEG file failed.", ERROR_HANDLE_EOF);
            }
            completed += bytesRead;
        }
        return true;
    }

    bool WriteBytes(HANDLE file, std::span<const Byte> bytes, std::wstring* errorMessage)
    {
        std::size_t completed = 0;
        while (completed < bytes.size())
        {
            const DWORD requested = static_cast<DWORD>(std::min<std::size_t>(
                bytes.size() - completed,
                std::numeric_limits<DWORD>::max()));
            DWORD bytesWritten = 0;
            if (!WriteFile(file, bytes.data() + completed, requested, &bytesWritten, nullptr))
            {
                return FailWithWin32Error(
                    errorMessage,
                    L"Writing the updated JPEG file failed.",
                    GetLastError());
            }
            if (bytesWritten == 0)
            {
                return FailWithWin32Error(
                    errorMessage,
                    L"Writing the updated JPEG file failed.",
                    ERROR_WRITE_FAULT);
            }
            completed += bytesWritten;
        }
        return true;
    }

    bool CopyFileRange(HANDLE source,
                      HANDLE destination,
                      std::uint64_t fileSize,
                      std::uint64_t offset,
                      std::uint64_t byteCount,
                      std::wstring* errorMessage)
    {
        if (offset > fileSize || byteCount > fileSize - offset)
        {
            return Fail(errorMessage, L"The JPEG file changed while its metadata was being updated.");
        }

        LARGE_INTEGER position{};
        position.QuadPart = static_cast<LONGLONG>(offset);
        if (!SetFilePointerEx(source, position, nullptr, FILE_BEGIN))
        {
            return FailWithWin32Error(errorMessage, L"Seeking in the JPEG file failed.", GetLastError());
        }

        std::array<Byte, 64 * 1024> buffer{};
        std::uint64_t remaining = byteCount;
        while (remaining > 0)
        {
            const DWORD requested = static_cast<DWORD>(std::min<std::uint64_t>(remaining, buffer.size()));
            DWORD bytesRead = 0;
            if (!ReadFile(source, buffer.data(), requested, &bytesRead, nullptr))
            {
                return FailWithWin32Error(errorMessage, L"Reading the JPEG file failed.", GetLastError());
            }
            if (bytesRead == 0)
            {
                return FailWithWin32Error(errorMessage, L"Reading the JPEG file failed.", ERROR_HANDLE_EOF);
            }
            if (!WriteBytes(destination, std::span<const Byte>(buffer.data(), bytesRead), errorMessage))
            {
                return false;
            }
            remaining -= bytesRead;
        }
        return true;
    }

    enum class TiffByteOrder
    {
        LittleEndian,
        BigEndian,
    };

    bool ReadUInt16(std::span<const Byte> bytes,
                    std::size_t offset,
                    TiffByteOrder byteOrder,
                    std::uint16_t* value)
    {
        if (!value || offset > bytes.size() || bytes.size() - offset < 2)
        {
            return false;
        }

        if (byteOrder == TiffByteOrder::LittleEndian)
        {
            *value = static_cast<std::uint16_t>(bytes[offset])
                | static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[offset + 1]) << 8);
        }
        else
        {
            *value = static_cast<std::uint16_t>(
                (static_cast<std::uint16_t>(bytes[offset]) << 8)
                | static_cast<std::uint16_t>(bytes[offset + 1]));
        }
        return true;
    }

    bool ReadUInt32(std::span<const Byte> bytes,
                    std::size_t offset,
                    TiffByteOrder byteOrder,
                    std::uint32_t* value)
    {
        if (!value || offset > bytes.size() || bytes.size() - offset < 4)
        {
            return false;
        }

        if (byteOrder == TiffByteOrder::LittleEndian)
        {
            *value = static_cast<std::uint32_t>(bytes[offset])
                | (static_cast<std::uint32_t>(bytes[offset + 1]) << 8)
                | (static_cast<std::uint32_t>(bytes[offset + 2]) << 16)
                | (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
        }
        else
        {
            *value = (static_cast<std::uint32_t>(bytes[offset]) << 24)
                | (static_cast<std::uint32_t>(bytes[offset + 1]) << 16)
                | (static_cast<std::uint32_t>(bytes[offset + 2]) << 8)
                | static_cast<std::uint32_t>(bytes[offset + 3]);
        }
        return true;
    }

    void WriteUInt16(std::span<Byte> bytes,
                     std::size_t offset,
                     TiffByteOrder byteOrder,
                     std::uint16_t value)
    {
        if (byteOrder == TiffByteOrder::LittleEndian)
        {
            bytes[offset] = static_cast<Byte>(value & 0xff);
            bytes[offset + 1] = static_cast<Byte>((value >> 8) & 0xff);
        }
        else
        {
            bytes[offset] = static_cast<Byte>((value >> 8) & 0xff);
            bytes[offset + 1] = static_cast<Byte>(value & 0xff);
        }
    }

    void WriteUInt32(std::span<Byte> bytes,
                     std::size_t offset,
                     TiffByteOrder byteOrder,
                     std::uint32_t value)
    {
        if (byteOrder == TiffByteOrder::LittleEndian)
        {
            bytes[offset] = static_cast<Byte>(value & 0xff);
            bytes[offset + 1] = static_cast<Byte>((value >> 8) & 0xff);
            bytes[offset + 2] = static_cast<Byte>((value >> 16) & 0xff);
            bytes[offset + 3] = static_cast<Byte>((value >> 24) & 0xff);
        }
        else
        {
            bytes[offset] = static_cast<Byte>((value >> 24) & 0xff);
            bytes[offset + 1] = static_cast<Byte>((value >> 16) & 0xff);
            bytes[offset + 2] = static_cast<Byte>((value >> 8) & 0xff);
            bytes[offset + 3] = static_cast<Byte>(value & 0xff);
        }
    }

    struct ExifDirectoryInfo
    {
        TiffByteOrder byteOrder{ TiffByteOrder::LittleEndian };
        std::size_t firstIfdOffset{};
        std::size_t entriesOffset{};
        std::size_t nextIfdOffset{};
        std::uint16_t entryCount{};
        std::uint16_t orientationType{};
        std::uint16_t orientation{};
        bool hasOrientation{};
    };

    bool ParseExifDirectory(std::span<const Byte> payload,
                            ExifDirectoryInfo* directory,
                            std::wstring* errorMessage)
    {
        if (!directory || payload.size() < kExifSignature.size() + 8
            || !std::equal(kExifSignature.begin(), kExifSignature.end(), payload.begin()))
        {
            return Fail(errorMessage, L"The JPEG contains an invalid EXIF header.");
        }

        const std::span<const Byte> tiff = payload.subspan(kExifSignature.size());
        TiffByteOrder byteOrder{};
        if (tiff[0] == 'I' && tiff[1] == 'I')
        {
            byteOrder = TiffByteOrder::LittleEndian;
        }
        else if (tiff[0] == 'M' && tiff[1] == 'M')
        {
            byteOrder = TiffByteOrder::BigEndian;
        }
        else
        {
            return Fail(errorMessage, L"The JPEG contains an invalid EXIF byte order.");
        }

        std::uint16_t magic = 0;
        std::uint32_t firstIfdOffset = 0;
        if (!ReadUInt16(tiff, 2, byteOrder, &magic)
            || magic != 42
            || !ReadUInt32(tiff, 4, byteOrder, &firstIfdOffset)
            || firstIfdOffset > tiff.size()
            || tiff.size() - firstIfdOffset < 2)
        {
            return Fail(errorMessage, L"The JPEG contains an invalid EXIF TIFF header.");
        }

        std::uint16_t entryCount = 0;
        if (!ReadUInt16(tiff, firstIfdOffset, byteOrder, &entryCount))
        {
            return Fail(errorMessage, L"The JPEG contains an invalid EXIF directory.");
        }

        const std::size_t entriesOffset = static_cast<std::size_t>(firstIfdOffset) + 2;
        const std::size_t entriesSize = static_cast<std::size_t>(entryCount) * 12;
        if (entriesOffset > tiff.size()
            || entriesSize > tiff.size() - entriesOffset
            || tiff.size() - entriesOffset - entriesSize < 4)
        {
            return Fail(errorMessage, L"The JPEG contains a truncated EXIF directory.");
        }

        directory->byteOrder = byteOrder;
        directory->firstIfdOffset = firstIfdOffset;
        directory->entriesOffset = entriesOffset;
        directory->nextIfdOffset = entriesOffset + entriesSize;
        directory->entryCount = entryCount;
        directory->hasOrientation = false;
        directory->orientationType = 0;
        directory->orientation = 1;

        for (std::size_t index = 0; index < entryCount; ++index)
        {
            const std::size_t entryOffset = entriesOffset + index * 12;
            std::uint16_t tag = 0;
            if (!ReadUInt16(tiff, entryOffset, byteOrder, &tag))
            {
                return Fail(errorMessage, L"The JPEG contains an invalid EXIF directory entry.");
            }
            if (tag != kOrientationTag)
            {
                continue;
            }
            if (directory->hasOrientation)
            {
                return Fail(errorMessage, L"The JPEG contains duplicate EXIF Orientation tags.");
            }

            std::uint16_t type = 0;
            std::uint32_t count = 0;
            if (!ReadUInt16(tiff, entryOffset + 2, byteOrder, &type)
                || !ReadUInt32(tiff, entryOffset + 4, byteOrder, &count)
                || count != 1)
            {
                return Fail(errorMessage, L"The JPEG contains an unsupported EXIF Orientation entry.");
            }

            std::uint32_t orientationValue = 0;
            if (type == 1)
            {
                orientationValue = tiff[entryOffset + 8];
            }
            else if (type == kTiffShortType)
            {
                std::uint16_t shortValue = 0;
                if (!ReadUInt16(tiff, entryOffset + 8, byteOrder, &shortValue))
                {
                    return Fail(errorMessage, L"The JPEG contains an invalid EXIF Orientation value.");
                }
                orientationValue = shortValue;
            }
            else if (type == 4)
            {
                if (!ReadUInt32(tiff, entryOffset + 8, byteOrder, &orientationValue))
                {
                    return Fail(errorMessage, L"The JPEG contains an invalid EXIF Orientation value.");
                }
            }
            else
            {
                return Fail(errorMessage, L"The JPEG contains an unsupported EXIF Orientation type.");
            }

            directory->hasOrientation = true;
            directory->orientationType = type;
            directory->orientation = orientationValue >= 1 && orientationValue <= 8
                ? static_cast<std::uint16_t>(orientationValue)
                : 1;
        }
        return true;
    }

    std::uint16_t RotateRightOrientation(std::uint16_t orientation)
    {
        static constexpr std::array<std::uint16_t, 9> kRotationMap{ 0, 6, 7, 8, 5, 2, 3, 4, 1 };
        return orientation < kRotationMap.size() ? kRotationMap[orientation] : 6;
    }

    int NormalizeClockwiseQuarterTurns(int quarterTurnsDelta)
    {
        return ((quarterTurnsDelta % 4) + 4) % 4;
    }

    std::uint16_t ApplyQuarterTurns(std::uint16_t orientation, int quarterTurnsDelta)
    {
        for (int step = 0; step < NormalizeClockwiseQuarterTurns(quarterTurnsDelta); ++step)
        {
            orientation = RotateRightOrientation(orientation);
        }
        return orientation;
    }

    bool AddOrientationEntry(std::span<const Byte> originalPayload,
                             const ExifDirectoryInfo& directory,
                             std::uint16_t orientation,
                             std::vector<Byte>* updatedPayload,
                             std::wstring* errorMessage)
    {
        if (!updatedPayload || directory.entryCount == std::numeric_limits<std::uint16_t>::max())
        {
            return Fail(errorMessage, L"The EXIF directory has no room for an Orientation entry.");
        }

        const std::span<const Byte> originalTiff = originalPayload.subspan(kExifSignature.size());
        const std::size_t newIfdOffset = originalTiff.size() + (originalTiff.size() & 1);
        const std::size_t newEntryCount = static_cast<std::size_t>(directory.entryCount) + 1;
        const std::size_t newDirectorySize = 2 + newEntryCount * 12 + 4;
        if (newIfdOffset > std::numeric_limits<std::uint32_t>::max()
            || newDirectorySize > std::numeric_limits<std::size_t>::max() - newIfdOffset)
        {
            return Fail(errorMessage, L"The EXIF directory is too large to update.");
        }

        const std::size_t newTiffSize = newIfdOffset + newDirectorySize;
        if (kExifSignature.size() + newTiffSize + 2 > std::numeric_limits<std::uint16_t>::max())
        {
            return Fail(
                errorMessage,
                L"Adding EXIF Orientation would exceed the JPEG APP1 metadata size limit.");
        }

        std::vector<Byte> result(originalPayload.begin(), originalPayload.end());
        result.resize(kExifSignature.size() + newTiffSize, 0);
        std::span<Byte> updatedTiff(result.data() + kExifSignature.size(), newTiffSize);
        if ((originalTiff.size() & 1) != 0)
        {
            updatedTiff[originalTiff.size()] = 0;
        }

        WriteUInt16(updatedTiff, newIfdOffset, directory.byteOrder,
                    static_cast<std::uint16_t>(newEntryCount));
        const std::size_t newEntriesOffset = newIfdOffset + 2;
        const std::size_t originalEntriesSize = static_cast<std::size_t>(directory.entryCount) * 12;
        std::copy_n(originalTiff.begin() + directory.entriesOffset,
                    originalEntriesSize,
                    updatedTiff.begin() + newEntriesOffset);

        const std::size_t newOrientationEntryOffset = newEntriesOffset + originalEntriesSize;
        WriteUInt16(updatedTiff, newOrientationEntryOffset, directory.byteOrder, kOrientationTag);
        WriteUInt16(updatedTiff, newOrientationEntryOffset + 2, directory.byteOrder, kTiffShortType);
        WriteUInt32(updatedTiff, newOrientationEntryOffset + 4, directory.byteOrder, 1);
        WriteUInt16(updatedTiff, newOrientationEntryOffset + 8, directory.byteOrder, orientation);

        const std::size_t newNextIfdOffset = newOrientationEntryOffset + 12;
        std::copy_n(originalTiff.begin() + directory.nextIfdOffset,
                    4,
                    updatedTiff.begin() + newNextIfdOffset);
        WriteUInt32(updatedTiff, 4, directory.byteOrder, static_cast<std::uint32_t>(newIfdOffset));

        *updatedPayload = std::move(result);
        return true;
    }

    bool UpdateExifPayload(std::span<const Byte> originalPayload,
                           int quarterTurnsDelta,
                           std::vector<Byte>* updatedPayload,
                           std::wstring* errorMessage)
    {
        ExifDirectoryInfo directory;
        if (!ParseExifDirectory(originalPayload, &directory, errorMessage))
        {
            return false;
        }

        const std::uint16_t orientation = ApplyQuarterTurns(directory.orientation, quarterTurnsDelta);
        if (!directory.hasOrientation)
        {
            return AddOrientationEntry(
                originalPayload,
                directory,
                orientation,
                updatedPayload,
                errorMessage);
        }

        if (!updatedPayload)
        {
            return false;
        }
        updatedPayload->assign(originalPayload.begin(), originalPayload.end());
        std::span<Byte> tiff(updatedPayload->data() + kExifSignature.size(),
                             updatedPayload->size() - kExifSignature.size());
        std::size_t entryOffset = directory.entriesOffset;
        bool found = false;
        for (std::size_t index = 0; index < directory.entryCount; ++index)
        {
            std::uint16_t tag = 0;
            if (!ReadUInt16(tiff, entryOffset, directory.byteOrder, &tag))
            {
                return Fail(errorMessage, L"The JPEG contains an invalid EXIF directory entry.");
            }
            if (tag == kOrientationTag)
            {
                found = true;
                const std::size_t valueOffset = entryOffset + 8;
                if (directory.orientationType == 1)
                {
                    tiff[valueOffset] = static_cast<Byte>(orientation);
                }
                else if (directory.orientationType == kTiffShortType)
                {
                    WriteUInt16(tiff, valueOffset, directory.byteOrder, orientation);
                }
                else
                {
                    WriteUInt32(tiff, valueOffset, directory.byteOrder, orientation);
                }
                break;
            }
            entryOffset += 12;
        }
        if (!found)
        {
            return Fail(errorMessage, L"The JPEG EXIF Orientation entry could not be located.");
        }
        return true;
    }

    std::vector<Byte> CreateMinimalExifPayload(std::uint16_t orientation)
    {
        std::vector<Byte> payload(kExifSignature.size() + 26, 0);
        std::copy(kExifSignature.begin(), kExifSignature.end(), payload.begin());
        std::span<Byte> tiff(payload.data() + kExifSignature.size(), 26);
        tiff[0] = 'I';
        tiff[1] = 'I';
        WriteUInt16(tiff, 2, TiffByteOrder::LittleEndian, 42);
        WriteUInt32(tiff, 4, TiffByteOrder::LittleEndian, 8);
        WriteUInt16(tiff, 8, TiffByteOrder::LittleEndian, 1);
        WriteUInt16(tiff, 10, TiffByteOrder::LittleEndian, kOrientationTag);
        WriteUInt16(tiff, 12, TiffByteOrder::LittleEndian, kTiffShortType);
        WriteUInt32(tiff, 14, TiffByteOrder::LittleEndian, 1);
        WriteUInt16(tiff, 18, TiffByteOrder::LittleEndian, orientation);
        return payload;
    }

    struct ExifSegment
    {
        std::uint64_t markerOffset{};
        std::uint64_t lengthOffset{};
        std::uint64_t payloadOffset{};
        std::uint64_t endOffset{};
    };

    bool ReadByteAt(HANDLE file,
                    std::uint64_t fileSize,
                    std::uint64_t offset,
                    Byte* value,
                    std::wstring* errorMessage)
    {
        return value && ReadBytesAt(file, fileSize, offset, std::span<Byte>(value, 1), errorMessage);
    }

    bool FindExifSegment(HANDLE file,
                         std::uint64_t fileSize,
                         bool* foundExif,
                         ExifSegment* exifSegment,
                         std::uint64_t* insertionOffset,
                         std::wstring* errorMessage)
    {
        if (!foundExif || !exifSegment || !insertionOffset || fileSize < 4)
        {
            return Fail(errorMessage, L"The file is not a valid JPEG image.");
        }

        std::array<Byte, 2> signature{};
        if (!ReadBytesAt(file, fileSize, 0, signature, errorMessage))
        {
            return false;
        }
        if (signature[0] != 0xff || signature[1] != 0xd8)
        {
            return Fail(errorMessage, L"The file is not a valid JPEG image.");
        }

        *foundExif = false;
        *insertionOffset = 2;
        bool leadingApp0 = true;
        std::uint64_t position = 2;
        while (position < fileSize)
        {
            const std::uint64_t markerOffset = position;
            Byte prefix = 0;
            if (!ReadByteAt(file, fileSize, position, &prefix, errorMessage) || prefix != 0xff)
            {
                return Fail(errorMessage, L"The JPEG contains an invalid marker sequence.");
            }

            Byte marker = 0xff;
            std::uint64_t codeOffset = position;
            while (marker == 0xff)
            {
                ++codeOffset;
                if (!ReadByteAt(file, fileSize, codeOffset, &marker, errorMessage))
                {
                    return false;
                }
            }
            position = codeOffset + 1;

            if (marker == 0xda)
            {
                return true;
            }
            if (marker == 0xd9)
            {
                return Fail(errorMessage, L"The JPEG ended before its image data.");
            }
            if (marker == 0xd8 || marker == 0x00)
            {
                return Fail(errorMessage, L"The JPEG contains an invalid marker sequence.");
            }
            if (marker == 0x01 || (marker >= 0xd0 && marker <= 0xd7))
            {
                leadingApp0 = false;
                continue;
            }

            std::array<Byte, 2> lengthBytes{};
            const std::uint64_t lengthOffset = position;
            if (!ReadBytesAt(file, fileSize, lengthOffset, lengthBytes, errorMessage))
            {
                return false;
            }
            const std::uint16_t segmentLength = static_cast<std::uint16_t>(
                (static_cast<std::uint16_t>(lengthBytes[0]) << 8)
                | static_cast<std::uint16_t>(lengthBytes[1]));
            if (segmentLength < 2 || lengthOffset > fileSize || segmentLength > fileSize - lengthOffset)
            {
                return Fail(errorMessage, L"The JPEG contains an invalid marker segment length.");
            }

            const std::uint64_t payloadOffset = lengthOffset + 2;
            const std::uint64_t endOffset = lengthOffset + segmentLength;
            const std::uint64_t payloadSize = segmentLength - 2;
            if (marker == 0xe0 && leadingApp0)
            {
                *insertionOffset = endOffset;
            }
            else
            {
                leadingApp0 = false;
            }

            if (marker == 0xe1 && payloadSize >= kExifSignature.size())
            {
                std::array<Byte, kExifSignature.size()> payloadSignature{};
                if (!ReadBytesAt(file, fileSize, payloadOffset, payloadSignature, errorMessage))
                {
                    return false;
                }
                if (std::equal(kExifSignature.begin(), kExifSignature.end(), payloadSignature.begin()))
                {
                    *foundExif = true;
                    *exifSegment = ExifSegment{ markerOffset, lengthOffset, payloadOffset, endOffset };
                    return true;
                }
            }

            position = endOffset;
        }
        return Fail(errorMessage, L"The JPEG ended before its image data.");
    }

    bool CreateTemporaryFile(const std::filesystem::path& originalPath,
                             TemporaryPathCleanup* cleanup,
                             FileHandle* temporaryFile,
                             std::wstring* errorMessage)
    {
        if (!cleanup || !temporaryFile)
        {
            return Fail(errorMessage, L"Failed to create a temporary JPEG file.");
        }

        static std::atomic_uint64_t nextTemporaryId{ 0 };
        const std::filesystem::path parent = originalPath.parent_path();
        for (unsigned int attempt = 0; attempt < 32; ++attempt)
        {
            const std::wstring temporaryName = L".hyperbrowse-orientation-"
                + std::to_wstring(GetCurrentProcessId())
                + L"-"
                + std::to_wstring(GetCurrentThreadId())
                + L"-"
                + std::to_wstring(nextTemporaryId.fetch_add(1, std::memory_order_relaxed) + 1)
                + L".tmp";
            const std::filesystem::path candidate = parent / temporaryName;
            FileHandle candidateFile(CreateFileW(
                candidate.c_str(),
                GENERIC_WRITE,
                0,
                nullptr,
                CREATE_NEW,
                FILE_ATTRIBUTE_NORMAL,
                nullptr));
            if (candidateFile)
            {
                cleanup->path = candidate;
                temporaryFile->Reset(candidateFile.Release());
                return true;
            }

            const DWORD errorCode = GetLastError();
            if (errorCode != ERROR_FILE_EXISTS && errorCode != ERROR_ALREADY_EXISTS)
            {
                return FailWithWin32Error(
                    errorMessage,
                    L"Creating a temporary JPEG file failed.",
                    errorCode);
            }
        }
        return Fail(errorMessage, L"No temporary filename was available for the JPEG update.");
    }

    bool WriteUpdatedJpeg(FileHandle* source,
                          std::uint64_t sourceSize,
                          const std::filesystem::path& originalPath,
                          bool hasExif,
                          const ExifSegment& exifSegment,
                          std::uint64_t insertionOffset,
                          std::span<const Byte> updatedPayload,
                          std::wstring* errorMessage)
    {
        if (updatedPayload.size() + 2 > std::numeric_limits<std::uint16_t>::max())
        {
            return Fail(
                errorMessage,
                L"The updated EXIF data exceeds the JPEG APP1 metadata size limit.");
        }

        TemporaryPathCleanup cleanup;
        FileHandle temporaryFile;
        if (!CreateTemporaryFile(originalPath, &cleanup, &temporaryFile, errorMessage))
        {
            return false;
        }

        const std::uint16_t newSegmentLength = static_cast<std::uint16_t>(updatedPayload.size() + 2);
        const std::array<Byte, 2> segmentLengthBytes{
            static_cast<Byte>((newSegmentLength >> 8) & 0xff),
            static_cast<Byte>(newSegmentLength & 0xff),
        };

        if (hasExif)
        {
            if (!source
                || !CopyFileRange(source->Get(), temporaryFile.Get(), sourceSize, 0, exifSegment.markerOffset, errorMessage)
                || !CopyFileRange(source->Get(),
                                  temporaryFile.Get(),
                                  sourceSize,
                                  exifSegment.markerOffset,
                                  exifSegment.lengthOffset - exifSegment.markerOffset,
                                  errorMessage)
                || !WriteBytes(temporaryFile.Get(), segmentLengthBytes, errorMessage)
                || !WriteBytes(temporaryFile.Get(), updatedPayload, errorMessage)
                || !CopyFileRange(source->Get(),
                                  temporaryFile.Get(),
                                  sourceSize,
                                  exifSegment.endOffset,
                                  sourceSize - exifSegment.endOffset,
                                  errorMessage))
            {
                return false;
            }
        }
        else
        {
            const std::array<Byte, 4> app1Header{
                0xff,
                0xe1,
                segmentLengthBytes[0],
                segmentLengthBytes[1],
            };
            if (!source
                || !CopyFileRange(source->Get(), temporaryFile.Get(), sourceSize, 0, insertionOffset, errorMessage)
                || !WriteBytes(temporaryFile.Get(), app1Header, errorMessage)
                || !WriteBytes(temporaryFile.Get(), updatedPayload, errorMessage)
                || !CopyFileRange(source->Get(),
                                  temporaryFile.Get(),
                                  sourceSize,
                                  insertionOffset,
                                  sourceSize - insertionOffset,
                                  errorMessage))
            {
                return false;
            }
        }

        if (!FlushFileBuffers(temporaryFile.Get()))
        {
            return FailWithWin32Error(
                errorMessage,
                L"Flushing the updated JPEG file failed.",
                GetLastError());
        }

        temporaryFile.Reset();
        if (!source || !source->Close())
        {
            return FailWithWin32Error(
                errorMessage,
                L"Closing the original JPEG file failed.",
                GetLastError());
        }

        if (!ReplaceFileW(originalPath.c_str(), cleanup.path.c_str(), nullptr, 0, nullptr, nullptr))
        {
            return FailWithWin32Error(
                errorMessage,
                L"Replacing the JPEG with its updated metadata failed.",
                GetLastError());
        }
        cleanup.published = true;
        return true;
    }

    bool UpdateJpegExifOrientation(const std::wstring& filePath,
                                   int quarterTurnsDelta,
                                   std::wstring* errorMessage)
    {
        if (quarterTurnsDelta == 0)
        {
            return true;
        }
        if (!ValidateJpegWithWic(filePath, errorMessage))
        {
            return false;
        }

        const std::filesystem::path originalPath(filePath);
        FileHandle source(CreateFileW(
            originalPath.c_str(),
            GENERIC_READ,
            FILE_SHARE_READ,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr));
        if (!source)
        {
            return FailWithWin32Error(
                errorMessage,
                L"Opening the JPEG file for orientation adjustment failed.",
                GetLastError());
        }

        LARGE_INTEGER fileSizeValue{};
        if (!GetFileSizeEx(source.Get(), &fileSizeValue))
        {
            return FailWithWin32Error(
                errorMessage,
                L"Reading the JPEG file size failed.",
                GetLastError());
        }
        if (fileSizeValue.QuadPart < 0)
        {
            return Fail(errorMessage, L"The JPEG file has an invalid size.");
        }
        const std::uint64_t fileSize = static_cast<std::uint64_t>(fileSizeValue.QuadPart);

        bool hasExif = false;
        ExifSegment exifSegment{};
        std::uint64_t insertionOffset = 2;
        if (!FindExifSegment(
                source.Get(),
                fileSize,
                &hasExif,
                &exifSegment,
                &insertionOffset,
                errorMessage))
        {
            return false;
        }

        std::vector<Byte> updatedPayload;
        if (hasExif)
        {
            const std::uint64_t payloadSize = exifSegment.endOffset - exifSegment.payloadOffset;
            std::vector<Byte> originalPayload(static_cast<std::size_t>(payloadSize));
            if (!ReadBytesAt(
                    source.Get(),
                    fileSize,
                    exifSegment.payloadOffset,
                    originalPayload,
                    errorMessage))
            {
                return false;
            }

            ExifDirectoryInfo directory;
            if (!ParseExifDirectory(originalPayload, &directory, errorMessage))
            {
                return false;
            }
            if (!UpdateExifPayload(
                    originalPayload,
                    quarterTurnsDelta,
                    &updatedPayload,
                    errorMessage))
            {
                return false;
            }
        }
        else
        {
            const std::uint16_t orientation = ApplyQuarterTurns(1, quarterTurnsDelta);
            updatedPayload = CreateMinimalExifPayload(orientation);
        }

        return WriteUpdatedJpeg(
            &source,
            fileSize,
            originalPath,
            hasExif,
            exifSegment,
            insertionOffset,
            updatedPayload,
            errorMessage);
    }
}

namespace hyperbrowse::services
{
    bool AdjustJpegOrientation(const std::wstring& filePath,
                               int quarterTurnsDelta,
                               std::wstring* errorMessage)
    {
        if (errorMessage)
        {
            errorMessage->clear();
        }
        return UpdateJpegExifOrientation(filePath, quarterTurnsDelta, errorMessage);
    }
}
