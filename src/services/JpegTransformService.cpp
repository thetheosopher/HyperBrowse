#include "services/JpegTransformService.h"

#include "decode/WicDecodeHelpers.h"

#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
#include <string>

namespace
{
    using Microsoft::WRL::ComPtr;

    constexpr const wchar_t* kOrientationMetadataQueries[] = {
        L"System.Photo.Orientation",
        L"/app1/ifd/{ushort=274}",
        L"/ifd/{ushort=274}",
    };

    std::uint16_t RotateRightOrientation(std::uint16_t orientation)
    {
        static constexpr std::array<std::uint16_t, 9> kRotationMap{0, 6, 7, 8, 5, 2, 3, 4, 1};
        return orientation < kRotationMap.size() ? kRotationMap[orientation] : 6;
    }

    int NormalizeClockwiseQuarterTurns(int quarterTurnsDelta)
    {
        return ((quarterTurnsDelta % 4) + 4) % 4;
    }

    bool TrySetOrientationMetadata(IWICMetadataQueryWriter* queryWriter, std::uint16_t orientation)
    {
        if (!queryWriter)
        {
            return false;
        }

        PROPVARIANT value{};
        value.vt = VT_UI2;
        value.uiVal = orientation;

        for (const wchar_t* query : kOrientationMetadataQueries)
        {
            if (SUCCEEDED(queryWriter->SetMetadataByName(query, &value)))
            {
                return true;
            }
        }

        return false;
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
        HRESULT result = factory->CreateDecoderFromFilename(filePath.c_str(),
                                                            nullptr,
                                                            GENERIC_READ | GENERIC_WRITE,
                                                            WICDecodeMetadataCacheOnDemand,
                                                            decoder.GetAddressOf());
        if (FAILED(result) || !decoder)
        {
            if (errorMessage)
            {
                *errorMessage = L"Failed to open the JPEG file for metadata editing.";
            }
            return false;
        }

        ComPtr<IWICBitmapFrameDecode> frame;
        result = decoder->GetFrame(0, frame.GetAddressOf());
        if (FAILED(result) || !frame)
        {
            if (errorMessage)
            {
                *errorMessage = L"Failed to read the JPEG frame for orientation adjustment.";
            }
            return false;
        }

        std::uint16_t orientation = hyperbrowse::decode::wic_support::ReadOrientation(frame.Get());
        if (orientation < 1 || orientation > 8)
        {
            orientation = 1;
        }

        const int clockwiseTurns = NormalizeClockwiseQuarterTurns(quarterTurnsDelta);
        for (int step = 0; step < clockwiseTurns; ++step)
        {
            orientation = RotateRightOrientation(orientation);
        }

        ComPtr<IWICFastMetadataEncoder> metadataEncoder;
        result = factory->CreateFastMetadataEncoderFromFrameDecode(frame.Get(), metadataEncoder.GetAddressOf());
        if (FAILED(result) || !metadataEncoder)
        {
            if (errorMessage)
            {
                *errorMessage = L"Failed to create the JPEG metadata editor.";
            }
            return false;
        }

        ComPtr<IWICMetadataQueryWriter> metadataWriter;
        result = metadataEncoder->GetMetadataQueryWriter(metadataWriter.GetAddressOf());
        if (FAILED(result) || !metadataWriter)
        {
            if (errorMessage)
            {
                *errorMessage = L"Failed to access the JPEG metadata writer.";
            }
            return false;
        }

        if (!TrySetOrientationMetadata(metadataWriter.Get(), orientation))
        {
            if (errorMessage)
            {
                *errorMessage = L"Failed to update the JPEG EXIF orientation metadata.";
            }
            return false;
        }

        result = metadataEncoder->Commit();
        if (FAILED(result))
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