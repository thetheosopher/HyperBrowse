#include "decode/NvJpegDecoder.h"

#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "decode/WicDecodeHelpers.h"

namespace fs = std::filesystem;

namespace
{
#if defined(HYPERBROWSE_ENABLE_NVJPEG)
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
        unsigned int pitch[kNvjpegMaxComponents]{};
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

    struct FloatPoint
    {
        double x{};
        double y{};
    };

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

    FloatPoint MapOrientedToSource(double orientedX,
                                   double orientedY,
                                   int sourceWidth,
                                   int sourceHeight,
                                   std::uint16_t orientation)
    {
        switch (NormalizeOrientation(orientation))
        {
        case 2:
            return FloatPoint{static_cast<double>(sourceWidth - 1) - orientedX, orientedY};
        case 3:
            return FloatPoint{static_cast<double>(sourceWidth - 1) - orientedX,
                              static_cast<double>(sourceHeight - 1) - orientedY};
        case 4:
            return FloatPoint{orientedX, static_cast<double>(sourceHeight - 1) - orientedY};
        case 5:
            return FloatPoint{orientedY, orientedX};
        case 6:
            return FloatPoint{orientedY, static_cast<double>(sourceHeight - 1) - orientedX};
        case 7:
            return FloatPoint{static_cast<double>(sourceWidth - 1) - orientedY,
                              static_cast<double>(sourceHeight - 1) - orientedX};
        case 8:
            return FloatPoint{static_cast<double>(sourceWidth - 1) - orientedY, orientedX};
        case 1:
        default:
            return FloatPoint{orientedX, orientedY};
        }
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

    std::uint16_t ReadJpegOrientation(const std::wstring& filePath)
    {
        namespace wic = hyperbrowse::decode::wic_support;
        using Microsoft::WRL::ComPtr;

        std::wstring ignoredError;
        wic::ComInitializationScope comInitialization(
            COINIT_MULTITHREADED,
            &ignoredError,
            L"Failed to initialize COM for JPEG metadata decode.");
        if (!comInitialization.Succeeded())
        {
            return 1;
        }

        ComPtr<IWICImagingFactory> factory;
        if (!wic::InitializeWicFactory(&factory, &ignoredError))
        {
            return 1;
        }

        ComPtr<IWICBitmapDecoder> decoder;
        const HRESULT decoderResult = factory->CreateDecoderFromFilename(
            filePath.c_str(),
            nullptr,
            GENERIC_READ,
            WICDecodeMetadataCacheOnDemand,
            &decoder);
        if (FAILED(decoderResult) || !decoder)
        {
            return 1;
        }

        ComPtr<IWICBitmapFrameDecode> frame;
        if (FAILED(decoder->GetFrame(0, &frame)) || !frame)
        {
            return 1;
        }

        return wic::ReadOrientation(frame.Get());
    }

    double SampleBgrComponent(const std::vector<std::uint8_t>& sourcePixels,
                              int sourceWidth,
                              int sourceHeight,
                              double sourceX,
                              double sourceY,
                              int componentIndex)
    {
        const int stride = sourceWidth * 3;
        const double clampedX = std::clamp(sourceX, 0.0, static_cast<double>(sourceWidth - 1));
        const double clampedY = std::clamp(sourceY, 0.0, static_cast<double>(sourceHeight - 1));

        const int x0 = static_cast<int>(std::floor(clampedX));
        const int y0 = static_cast<int>(std::floor(clampedY));
        const int x1 = std::min(sourceWidth - 1, x0 + 1);
        const int y1 = std::min(sourceHeight - 1, y0 + 1);

        const double tx = clampedX - static_cast<double>(x0);
        const double ty = clampedY - static_cast<double>(y0);

        const double sample00 = sourcePixels[(y0 * stride) + (x0 * 3) + componentIndex];
        const double sample10 = sourcePixels[(y0 * stride) + (x1 * 3) + componentIndex];
        const double sample01 = sourcePixels[(y1 * stride) + (x0 * 3) + componentIndex];
        const double sample11 = sourcePixels[(y1 * stride) + (x1 * 3) + componentIndex];

        const double top = sample00 + ((sample10 - sample00) * tx);
        const double bottom = sample01 + ((sample11 - sample01) * tx);
        return top + ((bottom - top) * ty);
    }

    std::uint8_t ClampToByte(double value)
    {
        const long rounded = std::lround(value);
        const long clamped = std::clamp(rounded, 0L, 255L);
        return static_cast<std::uint8_t>(clamped);
    }

    void ResampleOrientedBgrToBgra(const std::vector<std::uint8_t>& sourcePixels,
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

        const int orientedWidth = OrientationSwapsDimensions(orientation) ? sourceHeight : sourceWidth;
        const int orientedHeight = OrientationSwapsDimensions(orientation) ? sourceWidth : sourceHeight;
        const double xScale = static_cast<double>(orientedWidth) / static_cast<double>(destinationWidth);
        const double yScale = static_cast<double>(orientedHeight) / static_cast<double>(destinationHeight);
        const int destinationStride = destinationWidth * 4;

        for (int y = 0; y < destinationHeight; ++y)
        {
            std::uint8_t* destinationRow = destinationPixels + (y * destinationStride);
            const double orientedY = ((static_cast<double>(y) + 0.5) * yScale) - 0.5;
            for (int x = 0; x < destinationWidth; ++x)
            {
                const double orientedX = ((static_cast<double>(x) + 0.5) * xScale) - 0.5;
                const FloatPoint sourcePoint = MapOrientedToSource(orientedX,
                                                                   orientedY,
                                                                   sourceWidth,
                                                                   sourceHeight,
                                                                   orientation);

                destinationRow[(x * 4) + 0] = ClampToByte(
                    SampleBgrComponent(sourcePixels, sourceWidth, sourceHeight, sourcePoint.x, sourcePoint.y, 0));
                destinationRow[(x * 4) + 1] = ClampToByte(
                    SampleBgrComponent(sourcePixels, sourceWidth, sourceHeight, sourcePoint.x, sourcePoint.y, 1));
                destinationRow[(x * 4) + 2] = ClampToByte(
                    SampleBgrComponent(sourcePixels, sourceWidth, sourceHeight, sourcePoint.x, sourcePoint.y, 2));
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
        if (!IsJpegFilePath(key.filePath))
        {
            if (errorMessage)
            {
                *errorMessage = L"nvJPEG only applies to JPEG thumbnails.";
            }
            return {};
        }

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

        std::vector<std::uint8_t> fileBytes;
        if (!ReadFileBytes(key.filePath, &fileBytes, errorMessage))
        {
            return {};
        }

        int componentCount = 0;
        int chromaSubsampling = 0;
        std::array<int, kNvjpegMaxComponents> componentWidths{};
        std::array<int, kNvjpegMaxComponents> componentHeights{};
        if (runtime.nvjpegGetImageInfo_(runtime.Handle(),
                                        fileBytes.data(),
                                        fileBytes.size(),
                                        &componentCount,
                                        &chromaSubsampling,
                                        componentWidths.data(),
                                        componentHeights.data()) != kNvjpegStatusSuccess)
        {
            if (errorMessage)
            {
                *errorMessage = L"nvJPEG failed to read JPEG image information.";
            }
            return {};
        }

        if (componentCount <= 0 || componentCount > 3 || componentWidths[0] <= 0 || componentHeights[0] <= 0)
        {
            if (errorMessage)
            {
                *errorMessage = L"nvJPEG does not support this JPEG component layout.";
            }
            return {};
        }

        const int sourceWidth = componentWidths[0];
        const int sourceHeight = componentHeights[0];
        const std::uint16_t orientation = NormalizeOrientation(ReadJpegOrientation(key.filePath));
        const int orientedWidth = OrientationSwapsDimensions(orientation) ? sourceHeight : sourceWidth;
        const int orientedHeight = OrientationSwapsDimensions(orientation) ? sourceWidth : sourceHeight;

        UINT scaledWidth = 0;
        UINT scaledHeight = 0;
        wic_support::ComputeScaledSize(static_cast<UINT>(orientedWidth),
                                       static_cast<UINT>(orientedHeight),
                                       key.targetWidth,
                                       key.targetHeight,
                                       &scaledWidth,
                                       &scaledHeight);
        if (scaledWidth == 0 || scaledHeight == 0)
        {
            if (errorMessage)
            {
                *errorMessage = L"nvJPEG produced invalid thumbnail dimensions.";
            }
            return {};
        }

        std::size_t sourceBufferSize = 0;
        if (!ComputeBufferSize(sourceWidth, sourceHeight, 3, &sourceBufferSize))
        {
            if (errorMessage)
            {
                *errorMessage = L"The nvJPEG source buffer size overflowed the supported range.";
            }
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
        if (!deviceBuffer.Allocate(sourceBufferSize, errorMessage))
        {
            return {};
        }

        nvjpegImage_t destination{};
        destination.channel[0] = static_cast<unsigned char*>(deviceBuffer.Get());
        destination.pitch[0] = static_cast<unsigned int>(sourceWidth * 3);
        if (runtime.nvjpegDecode_(runtime.Handle(),
                                  decodeState.Get(),
                                  fileBytes.data(),
                                  fileBytes.size(),
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

        std::vector<std::uint8_t> sourcePixels(sourceBufferSize);
        if (runtime.cudaMemcpy_(sourcePixels.data(),
                                deviceBuffer.Get(),
                                sourceBufferSize,
                                cudaMemcpyDeviceToHost) != kCudaSuccess)
        {
            if (errorMessage)
            {
                *errorMessage = L"Failed to copy the nvJPEG decode result back to host memory.";
            }
            return {};
        }

        void* bitmapBits = nullptr;
        HBITMAP bitmap = wic_support::CreateBitmapBuffer(scaledWidth, scaledHeight, &bitmapBits);
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

        ResampleOrientedBgrToBgra(sourcePixels,
                                  sourceWidth,
                                  sourceHeight,
                                  orientation,
                                  static_cast<int>(scaledWidth),
                                  static_cast<int>(scaledHeight),
                                  static_cast<std::uint8_t*>(bitmapBits));

        const std::size_t outputByteCount = static_cast<std::size_t>(scaledWidth) * static_cast<std::size_t>(scaledHeight) * 4ULL;
        return std::make_shared<cache::CachedThumbnail>(bitmap,
                                                        static_cast<int>(scaledWidth),
                                                        static_cast<int>(scaledHeight),
                                                        outputByteCount,
                                                        orientedWidth,
                                                        orientedHeight);
#endif
    }
}