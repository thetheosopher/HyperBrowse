#include "decode/NvJpegDecoder.h"

#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <atomic>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <mutex>
#include <memory>
#include <span>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "decode/WicDecodeHelpers.h"
#include "util/Diagnostics.h"

namespace fs = std::filesystem;

namespace
{
#if defined(HYPERBROWSE_ENABLE_NVJPEG)
    std::atomic_int g_activeNvJpegBatchDecodes{0};

    using nvjpegHandle_t = void*;
    using nvjpegJpegState_t = void*;
    using cudaStream_t = void*;
    using nvjpegStatus_t = int;
    using cudaError_t = int;

    constexpr nvjpegStatus_t kNvjpegStatusSuccess = 0;
    constexpr cudaError_t kCudaSuccess = 0;
    constexpr unsigned int kCudaStreamNonBlocking = 1;
    constexpr int kNvjpegMaxComponents = 4;

    enum nvjpegOutputFormat_t : int
    {
        NVJPEG_OUTPUT_UNCHANGED = 0,
        NVJPEG_OUTPUT_Y = 1,
        NVJPEG_OUTPUT_YUV = 2,
        NVJPEG_OUTPUT_RGB = 3,
        NVJPEG_OUTPUT_BGR = 4,
        NVJPEG_OUTPUT_RGBI = 5,
        NVJPEG_OUTPUT_BGRI = 6,
    };

    enum cudaMemcpyKind : int
    {
        cudaMemcpyHostToHost = 0,
        cudaMemcpyHostToDevice = 1,
        cudaMemcpyDeviceToHost = 2,
        cudaMemcpyDeviceToDevice = 3,
        cudaMemcpyDefault = 4,
    };

    struct nvjpegImage_t
    {
        unsigned char* channel[kNvjpegMaxComponents]{};
        std::size_t pitch[kNvjpegMaxComponents]{};
    };

    using NvjpegCreateSimpleFn = nvjpegStatus_t(__cdecl*)(nvjpegHandle_t*);
    using NvjpegDestroyFn = nvjpegStatus_t(__cdecl*)(nvjpegHandle_t);
    using NvjpegJpegStateCreateFn = nvjpegStatus_t(__cdecl*)(nvjpegHandle_t, nvjpegJpegState_t*);
    using NvjpegJpegStateDestroyFn = nvjpegStatus_t(__cdecl*)(nvjpegJpegState_t);
    using NvjpegGetImageInfoFn = nvjpegStatus_t(__cdecl*)(nvjpegHandle_t,
                                                          const unsigned char*,
                                                          std::size_t,
                                                          int*,
                                                          int*,
                                                          int*,
                                                          int*);
    using NvjpegDecodeFn = nvjpegStatus_t(__cdecl*)(nvjpegHandle_t,
                                                    nvjpegJpegState_t,
                                                    const unsigned char*,
                                                    std::size_t,
                                                    nvjpegOutputFormat_t,
                                                    nvjpegImage_t*,
                                                    cudaStream_t);
    using NvjpegDecodeBatchedInitializeFn = nvjpegStatus_t(__cdecl*)(nvjpegHandle_t,
                                                                     nvjpegJpegState_t,
                                                                     int,
                                                                     int,
                                                                     nvjpegOutputFormat_t);
    using NvjpegDecodeBatchedFn = nvjpegStatus_t(__cdecl*)(nvjpegHandle_t,
                                                           nvjpegJpegState_t,
                                                           const unsigned char* const*,
                                                           const std::size_t*,
                                                           nvjpegImage_t*,
                                                           cudaStream_t);

    using CudaGetDeviceCountFn = cudaError_t(__cdecl*)(int*);
    using CudaSetDeviceFn = cudaError_t(__cdecl*)(int);
    using CudaStreamCreateFn = cudaError_t(__cdecl*)(cudaStream_t*);
    using CudaStreamCreateWithFlagsFn = cudaError_t(__cdecl*)(cudaStream_t*, unsigned int);
    using CudaStreamDestroyFn = cudaError_t(__cdecl*)(cudaStream_t);
    using CudaStreamSynchronizeFn = cudaError_t(__cdecl*)(cudaStream_t);
    using CudaMallocFn = cudaError_t(__cdecl*)(void**, std::size_t);
    using CudaFreeFn = cudaError_t(__cdecl*)(void*);
    using CudaMemcpyFn = cudaError_t(__cdecl*)(void*, const void*, std::size_t, cudaMemcpyKind);

    constexpr const wchar_t* kNvJpegDllCandidates[] = {
        L"nvjpeg64_12.dll",
        L"nvjpeg64_11.dll",
        L"nvjpeg64_10.dll",
    };

    constexpr const wchar_t* kCudaRuntimeDllCandidates[] = {
        L"cudart64_12.dll",
        L"cudart64_110.dll",
        L"cudart64_102.dll",
        L"cudart64_101.dll",
        L"cudart64_100.dll",
    };

    struct PreparedDecodeRequest
    {
        std::vector<std::uint8_t> fileBytes;
        int sourceWidth{};
        int sourceHeight{};
        int orientedWidth{};
        int orientedHeight{};
        std::uint16_t orientation{1};
        UINT scaledWidth{};
        UINT scaledHeight{};
        std::size_t sourceBufferSize{};
    };

    class ScratchByteBufferPool
    {
    public:
        class Lease
        {
        public:
            Lease() = default;

            Lease(ScratchByteBufferPool* owner,
                  std::size_t classBytes,
                  std::vector<std::uint8_t>&& buffer) noexcept
                : owner_(owner)
                , classBytes_(classBytes)
                , buffer_(std::move(buffer))
            {
            }

            Lease(const Lease&) = delete;
            Lease& operator=(const Lease&) = delete;

            Lease(Lease&& other) noexcept
                : owner_(other.owner_)
                , classBytes_(other.classBytes_)
                , buffer_(std::move(other.buffer_))
            {
                other.owner_ = nullptr;
                other.classBytes_ = 0;
            }

            Lease& operator=(Lease&& other) noexcept
            {
                if (this == &other)
                {
                    return *this;
                }

                Release();
                owner_ = other.owner_;
                classBytes_ = other.classBytes_;
                buffer_ = std::move(other.buffer_);
                other.owner_ = nullptr;
                other.classBytes_ = 0;
                return *this;
            }

            ~Lease()
            {
                Release();
            }

            std::uint8_t* data() noexcept
            {
                return buffer_.data();
            }

            std::span<const std::uint8_t> view() const noexcept
            {
                return std::span<const std::uint8_t>(buffer_.data(), buffer_.size());
            }

        private:
            friend class ScratchByteBufferPool;

            void Release() noexcept
            {
                if (!owner_)
                {
                    return;
                }

                owner_->Release(classBytes_, std::move(buffer_));
                owner_ = nullptr;
                classBytes_ = 0;
            }

            ScratchByteBufferPool* owner_{};
            std::size_t classBytes_{};
            std::vector<std::uint8_t> buffer_;
        };

        Lease Acquire(std::size_t byteCount)
        {
            const std::size_t classBytes = ClassBytesFor(byteCount);
            std::vector<std::uint8_t> buffer;

            {
                std::scoped_lock lock(mutex_);
                auto iterator = freeBuffers_.find(classBytes);
                if (iterator != freeBuffers_.end() && !iterator->second.empty())
                {
                    buffer = std::move(iterator->second.back());
                    iterator->second.pop_back();
                    hyperbrowse::util::IncrementCounter(L"thumbnail.decode.nvjpeg.scratch_pool.hit");
                }
            }

            if (buffer.capacity() < classBytes)
            {
                buffer.reserve(classBytes);
                hyperbrowse::util::IncrementCounter(L"thumbnail.decode.nvjpeg.scratch_pool.miss");
            }
            else if (buffer.empty())
            {
                hyperbrowse::util::IncrementCounter(L"thumbnail.decode.nvjpeg.scratch_pool.reused");
            }

            buffer.resize(byteCount);
            return Lease(this, classBytes, std::move(buffer));
        }

    private:
        static constexpr std::size_t kMinimumClassBytes = 256 * 1024;
        static constexpr std::size_t kMaximumBuffersPerClass = 4;

        static std::size_t ClassBytesFor(std::size_t byteCount) noexcept
        {
            std::size_t classBytes = kMinimumClassBytes;
            while (classBytes < byteCount && classBytes <= (std::numeric_limits<std::size_t>::max() / 2))
            {
                classBytes *= 2;
            }

            return std::max(classBytes, byteCount);
        }

        void Release(std::size_t classBytes, std::vector<std::uint8_t>&& buffer) noexcept
        {
            if (classBytes == 0)
            {
                return;
            }

            buffer.clear();

            std::scoped_lock lock(mutex_);
            std::vector<std::vector<std::uint8_t>>& bucket = freeBuffers_[classBytes];
            if (bucket.size() >= kMaximumBuffersPerClass)
            {
                hyperbrowse::util::IncrementCounter(L"thumbnail.decode.nvjpeg.scratch_pool.discarded");
                return;
            }

            bucket.push_back(std::move(buffer));
        }

        std::mutex mutex_;
        std::unordered_map<std::size_t, std::vector<std::vector<std::uint8_t>>> freeBuffers_;
    };

    ScratchByteBufferPool& NvJpegScratchByteBufferPool()
    {
        static ScratchByteBufferPool pool;
        return pool;
    }

    HMODULE LoadFirstAvailableModule(const wchar_t* const* candidateNames,
                                     std::size_t candidateCount,
                                     std::wstring* loadedName)
    {
        for (std::size_t index = 0; index < candidateCount; ++index)
        {
            HMODULE module = LoadLibraryW(candidateNames[index]);
            if (!module)
            {
                continue;
            }

            if (loadedName)
            {
                *loadedName = candidateNames[index];
            }
            return module;
        }

        return nullptr;
    }

    template <typename ProcType>
    bool LoadProc(HMODULE module, const char* procName, ProcType* proc)
    {
        if (!module || !proc)
        {
            return false;
        }

        *proc = reinterpret_cast<ProcType>(GetProcAddress(module, procName));
        return *proc != nullptr;
    }

    bool IsJpegFilePath(const std::wstring& filePath)
    {
        std::wstring extension = fs::path(filePath).extension().wstring();
        std::transform(extension.begin(), extension.end(), extension.begin(), [](wchar_t value)
        {
            return static_cast<wchar_t>(towlower(value));
        });
        return extension == L".jpg" || extension == L".jpeg";
    }

    bool OrientationSwapsDimensions(std::uint16_t orientation)
    {
        switch (orientation)
        {
        case 5:
        case 6:
        case 7:
        case 8:
            return true;
        default:
            return false;
        }
    }

    std::uint16_t NormalizeOrientation(std::uint16_t orientation)
    {
        return orientation >= 1 && orientation <= 8 ? orientation : 1;
    }

    bool ComputeBufferSize(int width, int height, int channels, std::size_t* bufferSize)
    {
        if (!bufferSize || width <= 0 || height <= 0 || channels <= 0)
        {
            return false;
        }

        const unsigned long long candidate = static_cast<unsigned long long>(width)
            * static_cast<unsigned long long>(height)
            * static_cast<unsigned long long>(channels);
        if (candidate > static_cast<unsigned long long>(std::numeric_limits<std::size_t>::max()))
        {
            return false;
        }

        *bufferSize = static_cast<std::size_t>(candidate);
        return true;
    }

    bool ReadFileBytes(const std::wstring& filePath,
                       std::vector<std::uint8_t>* bytes,
                       std::wstring* errorMessage)
    {
        if (!bytes)
        {
            return false;
        }

        std::ifstream input(fs::path(filePath), std::ios::binary | std::ios::ate);
        if (!input)
        {
            if (errorMessage)
            {
                *errorMessage = L"Failed to open the JPEG file for nvJPEG decode.";
            }
            return false;
        }

        const std::streamoff byteCount = input.tellg();
        if (byteCount <= 0)
        {
            if (errorMessage)
            {
                *errorMessage = L"The JPEG file is empty or unreadable.";
            }
            return false;
        }

        bytes->resize(static_cast<std::size_t>(byteCount));
        input.seekg(0, std::ios::beg);
        input.read(reinterpret_cast<char*>(bytes->data()), static_cast<std::streamsize>(bytes->size()));
        if (!input)
        {
            bytes->clear();
            if (errorMessage)
            {
                *errorMessage = L"Failed to read the JPEG file into memory for nvJPEG decode.";
            }
            return false;
        }

        return true;
    }

    bool ReadBigEndian16(const std::vector<std::uint8_t>& bytes,
                         std::size_t offset,
                         std::uint16_t* value)
    {
        if (!value || offset + 1 >= bytes.size())
        {
            return false;
        }

        *value = static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes[offset]) << 8)
                                            | static_cast<std::uint16_t>(bytes[offset + 1]));
        return true;
    }

    bool ReadTiff16(const std::uint8_t* bytes,
                    std::size_t length,
                    std::size_t offset,
                    bool littleEndian,
                    std::uint16_t* value)
    {
        if (!bytes || !value || offset + 1 >= length)
        {
            return false;
        }

        if (littleEndian)
        {
            *value = static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[offset])
                                                | (static_cast<std::uint16_t>(bytes[offset + 1]) << 8));
        }
        else
        {
            *value = static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes[offset]) << 8)
                                                | static_cast<std::uint16_t>(bytes[offset + 1]));
        }

        return true;
    }

    bool ReadTiff32(const std::uint8_t* bytes,
                    std::size_t length,
                    std::size_t offset,
                    bool littleEndian,
                    std::uint32_t* value)
    {
        if (!bytes || !value || offset + 3 >= length)
        {
            return false;
        }

        if (littleEndian)
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

    std::uint16_t ParseExifOrientationSegment(const std::uint8_t* bytes, std::size_t length)
    {
        if (!bytes || length < 14 || std::memcmp(bytes, "Exif\0\0", 6) != 0)
        {
            return 1;
        }

        const std::uint8_t* tiffData = bytes + 6;
        const std::size_t tiffLength = length - 6;
        const bool littleEndian = tiffLength >= 2 && tiffData[0] == 'I' && tiffData[1] == 'I';
        const bool bigEndian = tiffLength >= 2 && tiffData[0] == 'M' && tiffData[1] == 'M';
        if (!littleEndian && !bigEndian)
        {
            return 1;
        }

        std::uint16_t magic = 0;
        if (!ReadTiff16(tiffData, tiffLength, 2, littleEndian, &magic) || magic != 42)
        {
            return 1;
        }

        std::uint32_t ifdOffset = 0;
        if (!ReadTiff32(tiffData, tiffLength, 4, littleEndian, &ifdOffset) || ifdOffset + 2 > tiffLength)
        {
            return 1;
        }

        std::uint16_t entryCount = 0;
        if (!ReadTiff16(tiffData, tiffLength, ifdOffset, littleEndian, &entryCount))
        {
            return 1;
        }

        std::size_t entryOffset = static_cast<std::size_t>(ifdOffset) + 2;
        for (std::uint16_t entryIndex = 0; entryIndex < entryCount; ++entryIndex, entryOffset += 12)
        {
            if (entryOffset + 12 > tiffLength)
            {
                return 1;
            }

            std::uint16_t tag = 0;
            std::uint16_t type = 0;
            std::uint32_t count = 0;
            if (!ReadTiff16(tiffData, tiffLength, entryOffset, littleEndian, &tag)
                || !ReadTiff16(tiffData, tiffLength, entryOffset + 2, littleEndian, &type)
                || !ReadTiff32(tiffData, tiffLength, entryOffset + 4, littleEndian, &count))
            {
                return 1;
            }

            if (tag != 0x0112 || type != 3 || count == 0)
            {
                continue;
            }

            std::uint16_t orientation = 1;
            if (count == 1)
            {
                if (!ReadTiff16(tiffData, tiffLength, entryOffset + 8, littleEndian, &orientation))
                {
                    return 1;
                }
                return NormalizeOrientation(orientation);
            }

            std::uint32_t valueOffset = 0;
            if (!ReadTiff32(tiffData, tiffLength, entryOffset + 8, littleEndian, &valueOffset)
                || !ReadTiff16(tiffData, tiffLength, valueOffset, littleEndian, &orientation))
            {
                return 1;
            }
            return NormalizeOrientation(orientation);
        }

        return 1;
    }

    std::uint16_t ReadJpegOrientationFromBytes(const std::vector<std::uint8_t>& bytes)
    {
        if (bytes.size() < 4 || bytes[0] != 0xFF || bytes[1] != 0xD8)
        {
            return 1;
        }

        std::size_t offset = 2;
        while (offset + 3 < bytes.size())
        {
            while (offset < bytes.size() && bytes[offset] != 0xFF)
            {
                ++offset;
            }

            while (offset < bytes.size() && bytes[offset] == 0xFF)
            {
                ++offset;
            }

            if (offset >= bytes.size())
            {
                break;
            }

            const std::uint8_t marker = bytes[offset++];
            if (marker == 0xD9 || marker == 0xDA)
            {
                break;
            }

            if ((marker >= 0xD0 && marker <= 0xD7) || marker == 0x01)
            {
                continue;
            }

            std::uint16_t segmentLength = 0;
            if (!ReadBigEndian16(bytes, offset, &segmentLength) || segmentLength < 2)
            {
                break;
            }

            const std::size_t segmentDataOffset = offset + 2;
            const std::size_t segmentDataLength = static_cast<std::size_t>(segmentLength) - 2;
            if (segmentDataOffset + segmentDataLength > bytes.size())
            {
                break;
            }

            if (marker == 0xE1)
            {
                const std::uint16_t orientation = ParseExifOrientationSegment(bytes.data() + segmentDataOffset,
                                                                              segmentDataLength);
                if (orientation != 1)
                {
                    return orientation;
                }
            }

            offset = segmentDataOffset + segmentDataLength;
        }

        return 1;
    }

    struct AxisSample
    {
        int first{};
        int second{};
        std::uint16_t weight256{};
    };

    std::vector<AxisSample> BuildAxisSamples(int sourceExtent, int destinationExtent, bool reversed)
    {
        std::vector<AxisSample> samples(static_cast<std::size_t>(std::max(0, destinationExtent)));
        if (sourceExtent <= 0 || destinationExtent <= 0)
        {
            return samples;
        }

        if (sourceExtent == 1)
        {
            for (AxisSample& sample : samples)
            {
                sample.first = 0;
                sample.second = 0;
                sample.weight256 = 0;
            }
            return samples;
        }

        const double scale = static_cast<double>(sourceExtent) / static_cast<double>(destinationExtent);
        for (int destinationIndex = 0; destinationIndex < destinationExtent; ++destinationIndex)
        {
            double sourcePosition = ((static_cast<double>(destinationIndex) + 0.5) * scale) - 0.5;
            if (reversed)
            {
                sourcePosition = static_cast<double>(sourceExtent - 1) - sourcePosition;
            }

            sourcePosition = std::clamp(sourcePosition, 0.0, static_cast<double>(sourceExtent - 1));
            const int first = static_cast<int>(std::floor(sourcePosition));
            const int second = std::min(sourceExtent - 1, first + 1);
            const int weight256 = second == first
                ? 0
                : std::clamp(static_cast<int>(std::lround((sourcePosition - static_cast<double>(first)) * 256.0)), 0, 256);

            samples[static_cast<std::size_t>(destinationIndex)] = AxisSample{
                first,
                second,
                static_cast<std::uint16_t>(weight256),
            };
        }

        return samples;
    }

    void CopyBgrToBgra(std::span<const std::uint8_t> sourcePixels,
                      int width,
                      int height,
                      std::uint8_t* destinationPixels)
    {
        const int sourceStride = width * 3;
        const int destinationStride = width * 4;
        for (int y = 0; y < height; ++y)
        {
            const std::uint8_t* sourceRow = sourcePixels.data() + (y * sourceStride);
            std::uint8_t* destinationRow = destinationPixels + (y * destinationStride);
            for (int x = 0; x < width; ++x)
            {
                destinationRow[(x * 4) + 0] = sourceRow[(x * 3) + 0];
                destinationRow[(x * 4) + 1] = sourceRow[(x * 3) + 1];
                destinationRow[(x * 4) + 2] = sourceRow[(x * 3) + 2];
                destinationRow[(x * 4) + 3] = 255;
            }
        }
    }

    void ResampleOrientedBgrToBgra(std::span<const std::uint8_t> sourcePixels,
                                   int sourceWidth,
                                   int sourceHeight,
                                   std::uint16_t orientation,
                                   int destinationWidth,
                                   int destinationHeight,
                                   std::uint8_t* destinationPixels)
    {
        if (!destinationPixels || destinationWidth <= 0 || destinationHeight <= 0)
        {
            return;
        }

        const std::uint16_t normalizedOrientation = NormalizeOrientation(orientation);
        if (normalizedOrientation == 1 && destinationWidth == sourceWidth && destinationHeight == sourceHeight)
        {
            CopyBgrToBgra(sourcePixels, sourceWidth, sourceHeight, destinationPixels);
            return;
        }

        const bool axisSwap = normalizedOrientation >= 5;
        int xSourceExtent = sourceWidth;
        int ySourceExtent = sourceHeight;
        bool xReversed = false;
        bool yReversed = false;
        switch (normalizedOrientation)
        {
        case 2:
            xReversed = true;
            break;
        case 3:
            xReversed = true;
            yReversed = true;
            break;
        case 4:
            yReversed = true;
            break;
        case 5:
            xSourceExtent = sourceHeight;
            ySourceExtent = sourceWidth;
            break;
        case 6:
            xSourceExtent = sourceHeight;
            ySourceExtent = sourceWidth;
            yReversed = true;
            break;
        case 7:
            xSourceExtent = sourceHeight;
            ySourceExtent = sourceWidth;
            xReversed = true;
            yReversed = true;
            break;
        case 8:
            xSourceExtent = sourceHeight;
            ySourceExtent = sourceWidth;
            xReversed = true;
            break;
        default:
            break;
        }

        const std::vector<AxisSample> xSamples = BuildAxisSamples(xSourceExtent, destinationWidth, xReversed);
        const std::vector<AxisSample> ySamples = BuildAxisSamples(ySourceExtent, destinationHeight, yReversed);
        const int sourceStride = sourceWidth * 3;
        const int destinationStride = destinationWidth * 4;

        for (int y = 0; y < destinationHeight; ++y)
        {
            std::uint8_t* destinationRow = destinationPixels + (y * destinationStride);
            const AxisSample& yAxis = ySamples[static_cast<std::size_t>(y)];
            for (int x = 0; x < destinationWidth; ++x)
            {
                const AxisSample& xAxis = xSamples[static_cast<std::size_t>(x)];
                const AxisSample& sourceXAxis = axisSwap ? yAxis : xAxis;
                const AxisSample& sourceYAxis = axisSwap ? xAxis : yAxis;

                const int sourceX0 = sourceXAxis.first;
                const int sourceX1 = sourceXAxis.second;
                const int sourceY0 = sourceYAxis.first;
                const int sourceY1 = sourceYAxis.second;
                const int weightX = sourceXAxis.weight256;
                const int weightY = sourceYAxis.weight256;

                const std::uint8_t* sourceRow0 = sourcePixels.data() + (sourceY0 * sourceStride);
                const std::uint8_t* sourceRow1 = sourcePixels.data() + (sourceY1 * sourceStride);
                const int index00 = sourceX0 * 3;
                const int index10 = sourceX1 * 3;
                for (int componentIndex = 0; componentIndex < 3; ++componentIndex)
                {
                    const int sample00 = sourceRow0[index00 + componentIndex];
                    const int sample10 = sourceRow0[index10 + componentIndex];
                    const int sample01 = sourceRow1[index00 + componentIndex];
                    const int sample11 = sourceRow1[index10 + componentIndex];

                    const int top = (sample00 * (256 - weightX)) + (sample10 * weightX);
                    const int bottom = (sample01 * (256 - weightX)) + (sample11 * weightX);
                    destinationRow[(x * 4) + componentIndex] = static_cast<std::uint8_t>(
                        ((top * (256 - weightY)) + (bottom * weightY) + 32768) >> 16);
                }
                destinationRow[(x * 4) + 3] = 255;
            }
        }
    }

    class NvJpegRuntime
    {
    public:
        static NvJpegRuntime& Instance()
        {
            static NvJpegRuntime runtime;
            return runtime;
        }

        ~NvJpegRuntime()
        {
            if (handle_ && nvjpegDestroy_)
            {
                nvjpegDestroy_(handle_);
                handle_ = nullptr;
            }
            if (nvjpegModule_)
            {
                FreeLibrary(nvjpegModule_);
                nvjpegModule_ = nullptr;
            }
            if (cudaRuntimeModule_)
            {
                FreeLibrary(cudaRuntimeModule_);
                cudaRuntimeModule_ = nullptr;
            }
        }

        bool Available() const noexcept
        {
            return available_;
        }

        const std::wstring& FailureReason() const noexcept
        {
            return failureReason_;
        }

        nvjpegHandle_t Handle() const noexcept
        {
            return handle_;
        }

        NvjpegJpegStateCreateFn nvjpegJpegStateCreate_{};
        NvjpegJpegStateDestroyFn nvjpegJpegStateDestroy_{};
        NvjpegGetImageInfoFn nvjpegGetImageInfo_{};
        NvjpegDecodeFn nvjpegDecode_{};
        NvjpegDecodeBatchedInitializeFn nvjpegDecodeBatchedInitialize_{};
        NvjpegDecodeBatchedFn nvjpegDecodeBatched_{};
        CudaSetDeviceFn cudaSetDevice_{};
        CudaStreamCreateFn cudaStreamCreate_{};
        CudaStreamCreateWithFlagsFn cudaStreamCreateWithFlags_{};
        CudaStreamDestroyFn cudaStreamDestroy_{};
        CudaStreamSynchronizeFn cudaStreamSynchronize_{};
        CudaMallocFn cudaMalloc_{};
        CudaFreeFn cudaFree_{};
        CudaMemcpyFn cudaMemcpy_{};

    private:
        NvJpegRuntime()
        {
            Initialize();
        }

        void Initialize()
        {
            std::wstring loadedModuleName;
            nvjpegModule_ = LoadFirstAvailableModule(kNvJpegDllCandidates,
                                                     std::size(kNvJpegDllCandidates),
                                                     &loadedModuleName);
            if (!nvjpegModule_)
            {
                failureReason_ = L"nvJPEG runtime DLL was not found on PATH.";
                return;
            }

            cudaRuntimeModule_ = LoadFirstAvailableModule(kCudaRuntimeDllCandidates,
                                                          std::size(kCudaRuntimeDllCandidates),
                                                          &loadedModuleName);
            if (!cudaRuntimeModule_)
            {
                failureReason_ = L"CUDA runtime DLL was not found on PATH.";
                return;
            }

            if (!LoadProc(nvjpegModule_, "nvjpegCreateSimple", &nvjpegCreateSimple_)
                || !LoadProc(nvjpegModule_, "nvjpegDestroy", &nvjpegDestroy_)
                || !LoadProc(nvjpegModule_, "nvjpegJpegStateCreate", &nvjpegJpegStateCreate_)
                || !LoadProc(nvjpegModule_, "nvjpegJpegStateDestroy", &nvjpegJpegStateDestroy_)
                || !LoadProc(nvjpegModule_, "nvjpegGetImageInfo", &nvjpegGetImageInfo_)
                || !LoadProc(nvjpegModule_, "nvjpegDecode", &nvjpegDecode_)
                || !LoadProc(cudaRuntimeModule_, "cudaGetDeviceCount", &cudaGetDeviceCount_)
                || !LoadProc(cudaRuntimeModule_, "cudaSetDevice", &cudaSetDevice_)
                || !LoadProc(cudaRuntimeModule_, "cudaStreamDestroy", &cudaStreamDestroy_)
                || !LoadProc(cudaRuntimeModule_, "cudaStreamSynchronize", &cudaStreamSynchronize_)
                || !LoadProc(cudaRuntimeModule_, "cudaMalloc", &cudaMalloc_)
                || !LoadProc(cudaRuntimeModule_, "cudaFree", &cudaFree_)
                || !LoadProc(cudaRuntimeModule_, "cudaMemcpy", &cudaMemcpy_))
            {
                failureReason_ = L"Failed to resolve the required nvJPEG or CUDA runtime entry points.";
                return;
            }

            LoadProc(nvjpegModule_, "nvjpegDecodeBatchedInitialize", &nvjpegDecodeBatchedInitialize_);
            LoadProc(nvjpegModule_, "nvjpegDecodeBatched", &nvjpegDecodeBatched_);

            if (!LoadProc(cudaRuntimeModule_, "cudaStreamCreateWithFlags", &cudaStreamCreateWithFlags_)
                && !LoadProc(cudaRuntimeModule_, "cudaStreamCreate", &cudaStreamCreate_))
            {
                failureReason_ = L"Failed to resolve a CUDA stream creation entry point.";
                return;
            }

            int deviceCount = 0;
            if (cudaGetDeviceCount_(&deviceCount) != kCudaSuccess || deviceCount <= 0)
            {
                failureReason_ = L"No CUDA-capable NVIDIA device was detected for nvJPEG.";
                return;
            }

            if (cudaSetDevice_(0) != kCudaSuccess)
            {
                failureReason_ = L"Failed to select CUDA device 0 for nvJPEG.";
                return;
            }

            if (nvjpegCreateSimple_(&handle_) != kNvjpegStatusSuccess || !handle_)
            {
                failureReason_ = L"Failed to initialize the nvJPEG runtime.";
                return;
            }

            available_ = true;
        }

        HMODULE nvjpegModule_{};
        HMODULE cudaRuntimeModule_{};
        bool available_{};
        std::wstring failureReason_;
        nvjpegHandle_t handle_{};
        NvjpegCreateSimpleFn nvjpegCreateSimple_{};
        NvjpegDestroyFn nvjpegDestroy_{};
        CudaGetDeviceCountFn cudaGetDeviceCount_{};
    };

    class ScopedCudaStream
    {
    public:
        explicit ScopedCudaStream(NvJpegRuntime& runtime)
            : runtime_(runtime)
        {
        }

        ~ScopedCudaStream()
        {
            if (stream_ && runtime_.cudaStreamDestroy_)
            {
                runtime_.cudaStreamDestroy_(stream_);
            }
        }

        bool Create(std::wstring* errorMessage)
        {
            cudaError_t result = kCudaSuccess;
            if (runtime_.cudaStreamCreateWithFlags_)
            {
                result = runtime_.cudaStreamCreateWithFlags_(&stream_, kCudaStreamNonBlocking);
            }
            else if (runtime_.cudaStreamCreate_)
            {
                result = runtime_.cudaStreamCreate_(&stream_);
            }

            if (result != kCudaSuccess || !stream_)
            {
                if (errorMessage)
                {
                    *errorMessage = L"Failed to create a CUDA stream for nvJPEG decode.";
                }
                return false;
            }

            return true;
        }

        cudaStream_t Get() const noexcept
        {
            return stream_;
        }

    private:
        NvJpegRuntime& runtime_;
        cudaStream_t stream_{};
    };

    class ScopedCudaBuffer
    {
    public:
        explicit ScopedCudaBuffer(NvJpegRuntime& runtime)
            : runtime_(runtime)
        {
        }

        ~ScopedCudaBuffer()
        {
            if (buffer_ && runtime_.cudaFree_)
            {
                runtime_.cudaFree_(buffer_);
            }
        }

        bool Allocate(std::size_t byteCount, std::wstring* errorMessage)
        {
            if (runtime_.cudaMalloc_(&buffer_, byteCount) != kCudaSuccess || !buffer_)
            {
                if (errorMessage)
                {
                    *errorMessage = L"Failed to allocate CUDA device memory for nvJPEG decode.";
                }
                return false;
            }

            return true;
        }

        void* Get() const noexcept
        {
            return buffer_;
        }

    private:
        NvJpegRuntime& runtime_;
        void* buffer_{};
    };

    class ScopedNvJpegState
    {
    public:
        explicit ScopedNvJpegState(NvJpegRuntime& runtime)
            : runtime_(runtime)
        {
        }

        ~ScopedNvJpegState()
        {
            if (state_ && runtime_.nvjpegJpegStateDestroy_)
            {
                runtime_.nvjpegJpegStateDestroy_(state_);
            }
        }

        bool Create(std::wstring* errorMessage)
        {
            if (runtime_.nvjpegJpegStateCreate_(runtime_.Handle(), &state_) != kNvjpegStatusSuccess || !state_)
            {
                if (errorMessage)
                {
                    *errorMessage = L"Failed to create an nvJPEG decode state.";
                }
                return false;
            }

            return true;
        }

        nvjpegJpegState_t Get() const noexcept
        {
            return state_;
        }

    private:
        NvJpegRuntime& runtime_;
        nvjpegJpegState_t state_{};
    };

    void SetAllErrorMessages(std::vector<std::wstring>* errorMessages,
                             std::size_t count,
                             const std::wstring& message)
    {
        if (!errorMessages)
        {
            return;
        }

        errorMessages->assign(count, message);
    }

    std::shared_ptr<const hyperbrowse::cache::CachedThumbnail> BuildCachedThumbnailFromBgrPixels(
        std::span<const std::uint8_t> sourcePixels,
        int sourceWidth,
        int sourceHeight,
        std::uint16_t orientation,
        UINT scaledWidth,
        UINT scaledHeight,
        std::wstring* errorMessage)
    {
        void* bitmapBits = nullptr;
        HBITMAP bitmap = hyperbrowse::decode::wic_support::CreateBitmapBuffer(scaledWidth, scaledHeight, &bitmapBits);
        if (!bitmap || !bitmapBits)
        {
            if (bitmap)
            {
                DeleteObject(bitmap);
            }
            if (errorMessage)
            {
                *errorMessage = L"Failed to allocate the output bitmap for the nvJPEG thumbnail.";
            }
            return {};
        }

        hyperbrowse::util::Stopwatch resampleStopwatch;
        ResampleOrientedBgrToBgra(sourcePixels,
                                  sourceWidth,
                                  sourceHeight,
                                  orientation,
                                  static_cast<int>(scaledWidth),
                                  static_cast<int>(scaledHeight),
                                  static_cast<std::uint8_t*>(bitmapBits));
        hyperbrowse::util::RecordTiming(L"thumbnail.decode.nvjpeg.resample", resampleStopwatch.ElapsedMilliseconds());

        const int orientedWidth = OrientationSwapsDimensions(orientation) ? sourceHeight : sourceWidth;
        const int orientedHeight = OrientationSwapsDimensions(orientation) ? sourceWidth : sourceHeight;
        const std::size_t outputByteCount = static_cast<std::size_t>(scaledWidth) * static_cast<std::size_t>(scaledHeight) * 4ULL;
        return std::make_shared<hyperbrowse::cache::CachedThumbnail>(bitmap,
                                                                     static_cast<int>(scaledWidth),
                                                                     static_cast<int>(scaledHeight),
                                                                     outputByteCount,
                                                                     orientedWidth,
                                                                     orientedHeight);
    }

    class ScopedNvJpegBatchReservation
    {
    public:
        ScopedNvJpegBatchReservation()
            : activeBatchCount_(g_activeNvJpegBatchDecodes.fetch_add(1, std::memory_order_acq_rel) + 1)
        {
        }

        ~ScopedNvJpegBatchReservation()
        {
            g_activeNvJpegBatchDecodes.fetch_sub(1, std::memory_order_acq_rel);
        }

        int ResolveCpuThreadCount(std::size_t batchSize) const
        {
            const unsigned int hardwareConcurrency = std::thread::hardware_concurrency();
            const int totalCpuThreads = hardwareConcurrency == 0
                ? static_cast<int>(std::max<std::size_t>(1U, batchSize))
                : static_cast<int>(hardwareConcurrency);
            const int perBatchBudget = (totalCpuThreads + activeBatchCount_ - 1) / activeBatchCount_;
            return std::clamp(perBatchBudget,
                              1,
                              static_cast<int>(std::max<std::size_t>(1U, batchSize)));
        }

    private:
        int activeBatchCount_{1};
    };

    bool PrepareDecodeRequest(NvJpegRuntime& runtime,
                              const hyperbrowse::cache::ThumbnailCacheKey& key,
                              PreparedDecodeRequest* request,
                              std::wstring* errorMessage)
    {
        if (!request)
        {
            return false;
        }

        if (!IsJpegFilePath(key.filePath))
        {
            if (errorMessage)
            {
                *errorMessage = L"nvJPEG only applies to JPEG thumbnails.";
            }
            return false;
        }

        if (!ReadFileBytes(key.filePath, &request->fileBytes, errorMessage))
        {
            return false;
        }

        int componentCount = 0;
        int chromaSubsampling = 0;
        std::array<int, kNvjpegMaxComponents> componentWidths{};
        std::array<int, kNvjpegMaxComponents> componentHeights{};
        if (runtime.nvjpegGetImageInfo_(runtime.Handle(),
                                        request->fileBytes.data(),
                                        request->fileBytes.size(),
                                        &componentCount,
                                        &chromaSubsampling,
                                        componentWidths.data(),
                                        componentHeights.data()) != kNvjpegStatusSuccess)
        {
            if (errorMessage)
            {
                *errorMessage = L"nvJPEG failed to read JPEG image information.";
            }
            return false;
        }

        if (componentCount <= 0 || componentCount > 3 || componentWidths[0] <= 0 || componentHeights[0] <= 0)
        {
            if (errorMessage)
            {
                *errorMessage = L"nvJPEG does not support this JPEG component layout.";
            }
            return false;
        }

        request->sourceWidth = componentWidths[0];
        request->sourceHeight = componentHeights[0];
    hyperbrowse::util::Stopwatch orientationStopwatch;
    request->orientation = NormalizeOrientation(ReadJpegOrientationFromBytes(request->fileBytes));
    hyperbrowse::util::RecordTiming(L"thumbnail.decode.nvjpeg.orientation", orientationStopwatch.ElapsedMilliseconds());
        request->orientedWidth = OrientationSwapsDimensions(request->orientation) ? request->sourceHeight : request->sourceWidth;
        request->orientedHeight = OrientationSwapsDimensions(request->orientation) ? request->sourceWidth : request->sourceHeight;

        hyperbrowse::decode::wic_support::ComputeScaledSize(static_cast<UINT>(request->orientedWidth),
                                                            static_cast<UINT>(request->orientedHeight),
                                                            key.targetWidth,
                                                            key.targetHeight,
                                                            &request->scaledWidth,
                                                            &request->scaledHeight);
        if (request->scaledWidth == 0 || request->scaledHeight == 0)
        {
            if (errorMessage)
            {
                *errorMessage = L"nvJPEG produced invalid thumbnail dimensions.";
            }
            return false;
        }

        if (!ComputeBufferSize(request->sourceWidth, request->sourceHeight, 3, &request->sourceBufferSize))
        {
            if (errorMessage)
            {
                *errorMessage = L"The nvJPEG source buffer size overflowed the supported range.";
            }
            return false;
        }

        return true;
    }
#endif
}

namespace hyperbrowse::decode
{
    bool NvJpegDecoder::IsRuntimeAvailable()
    {
#if defined(HYPERBROWSE_ENABLE_NVJPEG)
        return NvJpegRuntime::Instance().Available();
#else
        return false;
#endif
    }

    std::shared_ptr<const cache::CachedThumbnail> NvJpegDecoder::Decode(const cache::ThumbnailCacheKey& key,
                                                                        std::wstring* errorMessage) const
    {
#if !defined(HYPERBROWSE_ENABLE_NVJPEG)
        if (errorMessage)
        {
            *errorMessage = L"This build does not include nvJPEG support.";
        }
        return {};
#else
        NvJpegRuntime& runtime = NvJpegRuntime::Instance();
        if (!runtime.Available())
        {
            if (errorMessage)
            {
                *errorMessage = runtime.FailureReason();
            }
            return {};
        }

        if (runtime.cudaSetDevice_(0) != kCudaSuccess)
        {
            if (errorMessage)
            {
                *errorMessage = L"Failed to select CUDA device 0 for nvJPEG decode.";
            }
            return {};
        }

        PreparedDecodeRequest request;
        if (!PrepareDecodeRequest(runtime, key, &request, errorMessage))
        {
            return {};
        }

        ScopedNvJpegState decodeState(runtime);
        if (!decodeState.Create(errorMessage))
        {
            return {};
        }

        ScopedCudaStream stream(runtime);
        if (!stream.Create(errorMessage))
        {
            return {};
        }

        ScopedCudaBuffer deviceBuffer(runtime);
        if (!deviceBuffer.Allocate(request.sourceBufferSize, errorMessage))
        {
            return {};
        }

        nvjpegImage_t destination{};
        destination.channel[0] = static_cast<unsigned char*>(deviceBuffer.Get());
        destination.pitch[0] = static_cast<std::size_t>(request.sourceWidth) * 3ULL;
        if (runtime.nvjpegDecode_(runtime.Handle(),
                                  decodeState.Get(),
                                  request.fileBytes.data(),
                                  request.fileBytes.size(),
                                  NVJPEG_OUTPUT_BGRI,
                                  &destination,
                                  stream.Get()) != kNvjpegStatusSuccess)
        {
            if (errorMessage)
            {
                *errorMessage = L"nvJPEG failed to decode the JPEG image.";
            }
            return {};
        }

        if (runtime.cudaStreamSynchronize_(stream.Get()) != kCudaSuccess)
        {
            if (errorMessage)
            {
                *errorMessage = L"The CUDA stream did not complete the nvJPEG decode successfully.";
            }
            return {};
        }

        auto sourcePixels = NvJpegScratchByteBufferPool().Acquire(request.sourceBufferSize);
        if (runtime.cudaMemcpy_(sourcePixels.data(),
                                deviceBuffer.Get(),
                                request.sourceBufferSize,
                                cudaMemcpyDeviceToHost) != kCudaSuccess)
        {
            if (errorMessage)
            {
                *errorMessage = L"Failed to copy the nvJPEG decode result back to host memory.";
            }
            return {};
        }

        return BuildCachedThumbnailFromBgrPixels(sourcePixels.view(),
                                                 request.sourceWidth,
                                                 request.sourceHeight,
                                                 request.orientation,
                                                 request.scaledWidth,
                                                 request.scaledHeight,
                                                 errorMessage);
#endif
    }

    std::vector<std::shared_ptr<const cache::CachedThumbnail>> NvJpegDecoder::DecodeBatch(
        const std::vector<cache::ThumbnailCacheKey>& keys,
        std::vector<std::wstring>* errorMessages) const
    {
#if !defined(HYPERBROWSE_ENABLE_NVJPEG)
        SetAllErrorMessages(errorMessages, keys.size(), L"This build does not include nvJPEG support.");
        return std::vector<std::shared_ptr<const cache::CachedThumbnail>>(keys.size());
#else
        std::vector<std::shared_ptr<const cache::CachedThumbnail>> thumbnails(keys.size());
        if (errorMessages)
        {
            errorMessages->assign(keys.size(), std::wstring{});
        }

        if (keys.empty())
        {
            return thumbnails;
        }

        NvJpegRuntime& runtime = NvJpegRuntime::Instance();
        if (!runtime.Available())
        {
            SetAllErrorMessages(errorMessages, keys.size(), runtime.FailureReason());
            return thumbnails;
        }

        if (!runtime.nvjpegDecodeBatchedInitialize_ || !runtime.nvjpegDecodeBatched_)
        {
            SetAllErrorMessages(errorMessages, keys.size(), L"The active nvJPEG runtime does not expose batched decode APIs.");
            return thumbnails;
        }

        if (runtime.cudaSetDevice_(0) != kCudaSuccess)
        {
            SetAllErrorMessages(errorMessages, keys.size(), L"Failed to select CUDA device 0 for nvJPEG decode.");
            return thumbnails;
        }

        std::vector<PreparedDecodeRequest> requests(keys.size());
        for (std::size_t index = 0; index < keys.size(); ++index)
        {
            std::wstring error;
            if (!PrepareDecodeRequest(runtime, keys[index], &requests[index], &error))
            {
                if (errorMessages)
                {
                    (*errorMessages)[index] = std::move(error);
                }
                return thumbnails;
            }
        }

        ScopedNvJpegState decodeState(runtime);
        std::wstring batchError;
        if (!decodeState.Create(&batchError))
        {
            SetAllErrorMessages(errorMessages, keys.size(), batchError);
            return thumbnails;
        }

        ScopedCudaStream stream(runtime);
        if (!stream.Create(&batchError))
        {
            SetAllErrorMessages(errorMessages, keys.size(), batchError);
            return thumbnails;
        }

        ScopedNvJpegBatchReservation batchReservation;
        const int batchCpuThreadCount = batchReservation.ResolveCpuThreadCount(keys.size());
        std::wstring cpuThreadCounter = L"thumbnail.decode.nvjpeg.batch.cpu_threads.";
        cpuThreadCounter.append(std::to_wstring(static_cast<unsigned long long>(batchCpuThreadCount)));
        hyperbrowse::util::IncrementCounter(cpuThreadCounter);

        if (runtime.nvjpegDecodeBatchedInitialize_(runtime.Handle(),
                                                   decodeState.Get(),
                                                   static_cast<int>(keys.size()),
                                                   batchCpuThreadCount,
                                                   NVJPEG_OUTPUT_BGRI) != kNvjpegStatusSuccess)
        {
            SetAllErrorMessages(errorMessages, keys.size(), L"nvJPEG failed to initialize the batched JPEG decoder state.");
            return thumbnails;
        }

        std::vector<std::unique_ptr<ScopedCudaBuffer>> deviceBuffers;
        deviceBuffers.reserve(keys.size());
        std::vector<nvjpegImage_t> destinations(keys.size());
        std::vector<const unsigned char*> fileData(keys.size());
        std::vector<std::size_t> fileLengths(keys.size());
        for (std::size_t index = 0; index < keys.size(); ++index)
        {
            deviceBuffers.push_back(std::make_unique<ScopedCudaBuffer>(runtime));
            if (!deviceBuffers.back()->Allocate(requests[index].sourceBufferSize, &batchError))
            {
                SetAllErrorMessages(errorMessages, keys.size(), batchError);
                return thumbnails;
            }

            destinations[index].channel[0] = static_cast<unsigned char*>(deviceBuffers.back()->Get());
            destinations[index].pitch[0] = static_cast<std::size_t>(requests[index].sourceWidth) * 3ULL;
            fileData[index] = requests[index].fileBytes.data();
            fileLengths[index] = requests[index].fileBytes.size();
        }

        if (runtime.nvjpegDecodeBatched_(runtime.Handle(),
                                         decodeState.Get(),
                                         fileData.data(),
                                         fileLengths.data(),
                                         destinations.data(),
                                         stream.Get()) != kNvjpegStatusSuccess)
        {
            SetAllErrorMessages(errorMessages, keys.size(), L"nvJPEG failed to decode the JPEG thumbnail batch.");
            return thumbnails;
        }

        if (runtime.cudaStreamSynchronize_(stream.Get()) != kCudaSuccess)
        {
            SetAllErrorMessages(errorMessages, keys.size(), L"The CUDA stream did not complete the nvJPEG thumbnail batch successfully.");
            return thumbnails;
        }

        std::vector<ScratchByteBufferPool::Lease> sourcePixelBuffers;
        sourcePixelBuffers.reserve(keys.size());
        for (std::size_t index = 0; index < keys.size(); ++index)
        {
            sourcePixelBuffers.push_back(NvJpegScratchByteBufferPool().Acquire(requests[index].sourceBufferSize));
            if (runtime.cudaMemcpy_(sourcePixelBuffers.back().data(),
                                    deviceBuffers[index]->Get(),
                                    requests[index].sourceBufferSize,
                                    cudaMemcpyDeviceToHost) != kCudaSuccess)
            {
                if (errorMessages)
                {
                    (*errorMessages)[index] = L"Failed to copy the nvJPEG batch result back to host memory.";
                }
                continue;
            }

            thumbnails[index] = BuildCachedThumbnailFromBgrPixels(sourcePixelBuffers.back().view(),
                                                                  requests[index].sourceWidth,
                                                                  requests[index].sourceHeight,
                                                                  requests[index].orientation,
                                                                  requests[index].scaledWidth,
                                                                  requests[index].scaledHeight,
                                                                  errorMessages ? &(*errorMessages)[index] : nullptr);
        }

        return thumbnails;
#endif
    }
}