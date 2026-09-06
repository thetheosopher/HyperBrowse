#include <windows.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "browser/BrowserModel.h"
#include "decode/ImageDecoder.h"
#include "decode/RawHelperProtocol.h"

#include "smoke_decode.h"

namespace fs = std::filesystem;

namespace hyperbrowse::tests
{
    namespace
    {
        void Expect(bool condition, const std::string& message)
        {
            if (!condition)
            {
                throw std::runtime_error(message);
            }
        }

        class TempFolder
        {
        public:
            explicit TempFolder(std::wstring name)
                : root_(fs::temp_directory_path() / std::move(name))
            {
                std::error_code error;
                fs::remove_all(root_, error);
                fs::create_directories(root_);
            }

            ~TempFolder()
            {
                std::error_code error;
                fs::remove_all(root_, error);
            }

            const fs::path& Root() const noexcept
            {
                return root_;
            }

        private:
            fs::path root_;
        };

        void RunRawFormatAllowlistScenario()
        {
            const std::vector<std::wstring> supportedRawFormats{
                L"ARW",
                L"CR2",
                L"CR3",
                L"DNG",
                L"NEF",
                L"NRW",
                L"RAF",
                L"RW2",
            };

            for (const std::wstring& rawFormat : supportedRawFormats)
            {
                Expect(hyperbrowse::decode::IsRawFileType(rawFormat),
                       "RAW allowlist omitted a supported format");

                hyperbrowse::browser::BrowserItem item;
                item.fileName = L"sample." + rawFormat;
                item.filePath = L"C:\\Raw\\sample." + rawFormat;
                item.fileType = rawFormat;

                Expect(hyperbrowse::decode::CanDecodeThumbnail(item),
                       "RAW thumbnail routing omitted a supported format");
                Expect(hyperbrowse::decode::CanDecodeFullImage(item),
                       "RAW full-image routing omitted a supported format");
            }

            Expect(!hyperbrowse::decode::IsRawFileType(L"ORF"),
                   "The RAW allowlist unexpectedly includes ORF before it was requested");
        }

        void RunRawHelperProtocolScenario()
        {
            TempFolder root(L"HyperBrowseRawHelperProtocol");
            const fs::path payloadPath = root.Root() / L"payload.bin";

            hyperbrowse::decode::RawHelperDecodedPixels payload;
            payload.bitmapWidth = 32;
            payload.bitmapHeight = 16;
            payload.sourceWidth = 64;
            payload.sourceHeight = 32;
            payload.bgraPixels.assign(32U * 16U * 4U, 0x7f);

            std::wstring errorMessage;
            Expect(hyperbrowse::decode::WriteRawHelperPayload(payloadPath.wstring(), payload, &errorMessage),
                   "RAW helper protocol failed to write a valid payload");

            hyperbrowse::decode::RawHelperDecodedPixels loaded;
            Expect(hyperbrowse::decode::ReadRawHelperPayload(payloadPath.wstring(), &loaded, &errorMessage),
                   "RAW helper protocol failed to read a valid payload");
            Expect(loaded.bitmapWidth == payload.bitmapWidth
                       && loaded.bitmapHeight == payload.bitmapHeight
                       && loaded.sourceWidth == payload.sourceWidth
                       && loaded.sourceHeight == payload.sourceHeight
                       && loaded.bgraPixels == payload.bgraPixels,
                   "RAW helper protocol did not preserve a valid payload");

#pragma pack(push, 1)
            struct TestRawHelperFileHeader
            {
                std::uint32_t magic{};
                std::uint32_t version{};
                std::uint32_t bitmapWidth{};
                std::uint32_t bitmapHeight{};
                std::uint32_t sourceWidth{};
                std::uint32_t sourceHeight{};
                std::uint64_t pixelBytes{};
            };
#pragma pack(pop)

            TestRawHelperFileHeader validHeader{
                0x52425748,
                1,
                32,
                16,
                64,
                32,
                32ULL * 16ULL * 4ULL,
            };
            const auto writeHeaderAndPayload = [&](const TestRawHelperFileHeader& header, std::size_t payloadBytes)
            {
                std::ofstream stream(payloadPath, std::ios::binary | std::ios::trunc);
                stream.write(reinterpret_cast<const char*>(&header), sizeof(header));
                std::vector<unsigned char> bytes(payloadBytes, 0x7f);
                stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            };

            TestRawHelperFileHeader oversizedHeader = validHeader;
            oversizedHeader.bitmapWidth = UINT32_MAX;
            writeHeaderAndPayload(oversizedHeader, 0);
            loaded = {};
            Expect(!hyperbrowse::decode::ReadRawHelperPayload(payloadPath.wstring(), &loaded, &errorMessage),
                   "RAW helper protocol accepted oversized dimensions");

            writeHeaderAndPayload(validHeader, static_cast<std::size_t>(validHeader.pixelBytes - 1));
            loaded = {};
            Expect(!hyperbrowse::decode::ReadRawHelperPayload(payloadPath.wstring(), &loaded, &errorMessage),
                   "RAW helper protocol accepted a truncated payload");

            writeHeaderAndPayload(validHeader, static_cast<std::size_t>(validHeader.pixelBytes + 1));
            loaded = {};
            Expect(!hyperbrowse::decode::ReadRawHelperPayload(payloadPath.wstring(), &loaded, &errorMessage),
                   "RAW helper protocol accepted trailing payload data");
        }

        void RunThumbnailFailureClassificationScenario()
        {
            Expect(hyperbrowse::decode::ClassifyThumbnailDecodeFailure(L"The RAW helper timed out and was terminated.")
                       == hyperbrowse::decode::ThumbnailDecodeFailureKind::TimedOut,
                   "Timeout classification did not detect a helper timeout");
            Expect(hyperbrowse::decode::ClassifyThumbnailDecodeFailure(L"Failed to process the RAW thumbnail fallback.")
                       == hyperbrowse::decode::ThumbnailDecodeFailureKind::DecodeFailed,
                   "Decode-failure classification misidentified a generic decode failure");
        }
    }

    void RunDecodePolicyScenarios()
    {
        RunRawFormatAllowlistScenario();
        RunRawHelperProtocolScenario();
        RunThumbnailFailureClassificationScenario();
    }
}
