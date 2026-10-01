#include <windows.h>
#include <commctrl.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <thread>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "browser/BrowserModel.h"
#include "browser/BrowserPane.h"
#include "cache/DiskThumbnailCache.h"
#include "decode/ImageDecoder.h"
#include "decode/RawHelperProtocol.h"
#include "decode/WicColorTransform.h"
#include "decode/WicDecodeHelpers.h"
#include "decode/WicThumbnailDecoder.h"
#include "services/DisplayColorService.h"
#include "services/WicCodecReadinessService.h"
#include "ui/DiagnosticsWindow.h"
#include "util/Diagnostics.h"
#include "viewer/ViewerWindow.h"

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

             void RunWebpFormatAllowlistScenario()
             {
                 Expect(hyperbrowse::decode::IsWicFileType(L"webp"),
                     "WIC allowlist omitted WebP");
                 Expect(hyperbrowse::decode::IsWicFileType(L".WEBP"),
                     "WIC allowlist did not normalize the WebP extension");
                 Expect(hyperbrowse::browser::IsSupportedImageExtension(L".WEBP"),
                     "Browser extension policy omitted WebP");

                 hyperbrowse::browser::BrowserItem item;
                 item.fileName = L"sample.webp";
                 item.filePath = L"C:\\Images\\sample.webp";
                 item.fileType = L"WEBP";

                 Expect(hyperbrowse::decode::CanDecodeThumbnail(item),
                     "WebP thumbnail routing did not use the WIC decoder");
                 Expect(hyperbrowse::decode::CanDecodeFullImage(item),
                     "WebP full-image routing did not use the WIC decoder");
                 Expect(!hyperbrowse::decode::IsRawFileType(L"webp"),
                     "WebP was incorrectly classified as a RAW format");
             }

             void RunHeicFormatAllowlistScenario()
             {
                 Expect(hyperbrowse::decode::IsWicFileType(L"heic"),
                     "WIC allowlist omitted HEIC");
                 Expect(hyperbrowse::decode::IsWicFileType(L".HEIC"),
                     "WIC allowlist did not normalize the HEIC extension");
                 Expect(hyperbrowse::browser::IsSupportedImageExtension(L".HEIC"),
                     "Browser extension policy omitted HEIC");

                 hyperbrowse::browser::BrowserItem item;
                 item.fileName = L"sample.heic";
                 item.filePath = L"C:\\Images\\sample.heic";
                 item.fileType = L"HEIC";

                 Expect(hyperbrowse::decode::CanDecodeThumbnail(item),
                     "HEIC thumbnail routing did not use the WIC decoder");
                 Expect(hyperbrowse::decode::CanDecodeFullImage(item),
                     "HEIC full-image routing did not use the WIC decoder");
                 Expect(!hyperbrowse::decode::IsRawFileType(L"heic"),
                     "HEIC was incorrectly classified as a RAW format");
             }

             void RunJpegXlFormatAllowlistScenario()
             {
                 Expect(hyperbrowse::decode::IsWicFileType(L"jxl"),
                     "WIC allowlist omitted JPEG XL");
                 Expect(hyperbrowse::decode::IsWicFileType(L".JXL"),
                     "WIC allowlist did not normalize the JPEG XL extension");
                 Expect(hyperbrowse::browser::IsSupportedImageExtension(L".JXL"),
                     "Browser extension policy omitted JPEG XL");

                 hyperbrowse::browser::BrowserItem item;
                 item.fileName = L"sample.jxl";
                 item.filePath = L"C:\\Images\\sample.jxl";
                 item.fileType = L"JXL";

                 Expect(hyperbrowse::decode::CanDecodeThumbnail(item),
                     "JPEG XL thumbnail routing did not use the WIC decoder");
                 Expect(hyperbrowse::decode::CanDecodeFullImage(item),
                     "JPEG XL full-image routing did not use the WIC decoder");
                 Expect(!hyperbrowse::decode::IsRawFileType(L"jxl"),
                     "JPEG XL was incorrectly classified as a RAW format");
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

        void WriteProfileDword(std::vector<unsigned char>& bytes, std::size_t offset, std::uint32_t value)
        {
            for (std::size_t index = 0; index < 4; ++index)
            {
                bytes[offset + index] = static_cast<unsigned char>(value >> ((3 - index) * 8));
            }
        }

        void WriteProfileSignature(std::vector<unsigned char>& bytes, std::size_t offset, const char* signature)
        {
            std::memcpy(bytes.data() + offset, signature, 4);
        }

        std::vector<unsigned char> GeneratedRgbProfile(double gamma)
        {
            struct Tag
            {
                const char* signature;
                std::vector<unsigned char> bytes;
            };
            const auto xyz = [](double first, double second, double third)
            {
                std::vector<unsigned char> bytes(20);
                WriteProfileSignature(bytes, 0, "XYZ ");
                const std::array<double, 3> values{first, second, third};
                for (std::size_t index = 0; index < values.size(); ++index)
                {
                    WriteProfileDword(bytes, 8 + index * 4, static_cast<std::uint32_t>(std::lround(values[index] * 65536)));
                }
                return bytes;
            };
            std::vector<unsigned char> curve(14);
            WriteProfileSignature(curve, 0, "curv");
            WriteProfileDword(curve, 8, 1);
            const auto fixedGamma = static_cast<std::uint16_t>(std::lround(gamma * 256));
            curve[12] = static_cast<unsigned char>(fixedGamma >> 8);
            curve[13] = static_cast<unsigned char>(fixedGamma);
            const std::string label = "Generated HyperBrowse test profile";
            std::vector<unsigned char> description(90 + label.size() + 1);
            WriteProfileSignature(description, 0, "desc");
            WriteProfileDword(description, 8, static_cast<std::uint32_t>(label.size() + 1));
            std::memcpy(description.data() + 12, label.c_str(), label.size() + 1);
            std::vector<unsigned char> text(8 + label.size() + 1);
            WriteProfileSignature(text, 0, "text");
            std::memcpy(text.data() + 8, label.c_str(), label.size() + 1);
            std::vector<Tag> tags{
                {"desc", std::move(description)}, {"cprt", std::move(text)},
                {"wtpt", xyz(0.9642, 1, 0.8249)},
                {"rXYZ", xyz(0.4360747, 0.2225045, 0.0139322)},
                {"gXYZ", xyz(0.3850649, 0.7168786, 0.0971045)},
                {"bXYZ", xyz(0.1430804, 0.0606169, 0.7141733)},
                {"rTRC", curve}, {"gTRC", curve}, {"bTRC", curve},
            };
            std::vector<unsigned char> profile(132 + tags.size() * 12);
            WriteProfileDword(profile, 8, 0x02100000);
            WriteProfileSignature(profile, 12, "mntr");
            WriteProfileSignature(profile, 16, "RGB ");
            WriteProfileSignature(profile, 20, "XYZ ");
            profile[24] = 7;
            profile[25] = 208;
            profile[27] = 1;
            profile[29] = 1;
            WriteProfileSignature(profile, 36, "acsp");
            WriteProfileSignature(profile, 40, "MSFT");
            WriteProfileDword(profile, 68, static_cast<std::uint32_t>(std::lround(0.9642 * 65536)));
            WriteProfileDword(profile, 72, 65536);
            WriteProfileDword(profile, 76, static_cast<std::uint32_t>(std::lround(0.8249 * 65536)));
            WriteProfileDword(profile, 128, static_cast<std::uint32_t>(tags.size()));
            for (std::size_t index = 0; index < tags.size(); ++index)
            {
                const std::size_t offset = profile.size();
                WriteProfileSignature(profile, 132 + index * 12, tags[index].signature);
                WriteProfileDword(profile, 136 + index * 12, static_cast<std::uint32_t>(offset));
                WriteProfileDword(profile, 140 + index * 12, static_cast<std::uint32_t>(tags[index].bytes.size()));
                profile.insert(profile.end(), tags[index].bytes.begin(), tags[index].bytes.end());
                while (profile.size() % 4 != 0) profile.push_back(0);
            }
            WriteProfileDword(profile, 0, static_cast<std::uint32_t>(profile.size()));
            return profile;
        }

        void RunWicColorPixelsScenario()
        {
            using namespace hyperbrowse;
            const auto linear = GeneratedRgbProfile(1);
            const auto square = GeneratedRgbProfile(2);
            std::wstring error;
            Expect(decode::color::IsUsableRgbProfile(linear, &error), "Generated linear ICC profile is invalid");
            Expect(decode::color::IsUsableRgbProfile(square, &error), "Generated gamma-2 ICC profile is invalid");
            void* bits = nullptr;
            HBITMAP bitmap = decode::wic_support::CreateBitmapBuffer(3, 1, &bits);
            Expect(bitmap && bits, "Color fixture bitmap allocation failed");
            const std::array<unsigned char, 12> pixels{32, 64, 128, 255, 16, 32, 64, 128, 0, 0, 0, 0};
            std::memcpy(bits, pixels.data(), pixels.size());
            const cache::CachedThumbnail source(bitmap, 3, 1, pixels.size(), 3, 1,
                                                 {cache::SourceColorKind::Icc, linear});
            const auto converted = decode::color::TransformForDisplay(source, source.SourceColor(), square, &error);
            Expect(converted != nullptr, "WIC did not convert generated RGB ICC pixels");
            DIBSECTION dib{};
            Expect(GetObjectW(converted->Bitmap(), sizeof(dib), &dib) == sizeof(dib), "Cannot inspect transformed pixels");
            const auto* output = static_cast<const unsigned char*>(dib.dsBm.bmBits);
            const std::array<int, 12> expected{90, 128, 181, 255, 45, 64, 91, 128, 0, 0, 0, 0};
            for (std::size_t index = 0; index < expected.size(); ++index)
            {
                Expect(std::abs(static_cast<int>(output[index]) - expected[index]) <= (index % 4 == 3 ? 0 : 3),
                       "WIC converted color/alpha pixels outside the expected tolerance");
            }
            Expect(std::memcmp(bits, pixels.data(), pixels.size()) == 0, "Color conversion modified canonical pixels");
            Expect(!decode::color::TransformForDisplay(*converted, source.SourceColor(), square, &error),
                   "Color conversion accepted already display-converted pixels");
            Expect(!decode::color::TransformForDisplay(source, {cache::SourceColorKind::Icc, {1, 2, 3}}, square, &error)
                       && !error.empty(), "Malformed source ICC did not return a diagnostic fallback");
            Expect(!decode::color::TransformForDisplay(source, source.SourceColor(), std::vector<unsigned char>{1, 2, 3}, &error),
                   "Malformed destination ICC was accepted");
            Expect(!decode::color::TransformForDisplay(source, {cache::SourceColorKind::Unsupported, {}}, square, &error),
                   "Unsupported source context was transformed");
            const auto untagged = decode::color::TransformForDisplay(source, {cache::SourceColorKind::Srgb, {}}, linear, &error);
            Expect(untagged != nullptr, "Untagged sRGB fallback did not transform");
            GetObjectW(untagged->Bitmap(), sizeof(dib), &dib);
            output = static_cast<const unsigned char*>(dib.dsBm.bmBits);
            const std::array<int, 3> srgbExpected{4, 13, 55};
            for (std::size_t channel = 0; channel < srgbExpected.size(); ++channel)
            {
                Expect(std::abs(static_cast<int>(output[channel]) - srgbExpected[channel]) <= 3,
                       "Untagged sRGB fallback has incorrect transfer-curve pixels");
            }
        }

             void RunColorMetadataPersistenceScenario()
             {
                 using namespace hyperbrowse;
                 TempFolder root(L"HyperBrowseColorMetadata");
                 const cache::SourceColorInfo info{cache::SourceColorKind::Icc, GeneratedRgbProfile(1)};
                 decode::RawHelperDecodedPixels payload;
                 payload.bitmapWidth = 3;
                 payload.bitmapHeight = 1;
                 payload.sourceWidth = 3;
                 payload.sourceHeight = 1;
                 payload.bgraPixels = {32, 64, 128, 255, 16, 32, 64, 128, 0, 0, 0, 0};
                 payload.sourceColor = info;
                 std::wstring error;
                 const auto payloadPath = (root.Root() / L"raw.bin").wstring();
                 Expect(decode::WriteRawHelperPayload(payloadPath, payload, &error), "RAW helper color metadata write failed");
                 decode::RawHelperDecodedPixels loaded;
                 Expect(decode::ReadRawHelperPayload(payloadPath, &loaded, &error)
                      && loaded.sourceColor.kind == info.kind && loaded.sourceColor.iccProfile == info.iccProfile
                      && loaded.bgraPixels == payload.bgraPixels,
                     "RAW helper did not preserve source ICC and canonical pixels");
                 void* bits = nullptr;
                 HBITMAP bitmap = decode::wic_support::CreateBitmapBuffer(3, 1, &bits);
                 Expect(bitmap && bits, "Color metadata bitmap allocation failed");
                 std::memcpy(bits, payload.bgraPixels.data(), payload.bgraPixels.size());
                 auto canonical = std::make_shared<cache::CachedThumbnail>(bitmap, 3, 1, 12, 3, 1, info);
                 const cache::ThumbnailCacheKey key{(root.Root() / L"image.png").wstring(), 7, 3, 1};
                 cache::DiskThumbnailCache disk(65536, (root.Root() / L"cache").wstring());
                 disk.Store(key, canonical);
                 auto fromDisk = disk.TryLoad(key);
                 Expect(fromDisk && fromDisk->SourceColor().kind == info.kind
                      && fromDisk->SourceColor().iccProfile == info.iccProfile,
                     "Disk thumbnail cache lost canonical source ICC metadata");
                 const auto converted = decode::color::TransformForDisplay(*canonical, info, GeneratedRgbProfile(2), &error);
                 Expect(converted != nullptr, "Cannot prepare a display-cache isolation fixture");
                 cache::ThumbnailCache memory(65536);
                 memory.Insert(key, canonical);
                 memory.Insert(key, converted);
                 Expect(memory.Find(key) == canonical, "Display pixels contaminated the shared memory cache");
                 disk.Store(key, converted);
                 fromDisk = disk.TryLoad(key);
                 Expect(fromDisk && fromDisk->SourceColor().iccProfile == info.iccProfile,
                     "Display pixels contaminated the persistent source cache");
                 std::stringstream encoded(std::ios::in | std::ios::out | std::ios::binary);
                 Expect(cache::WriteSourceColorInfo(encoded, info), "Source context serialization failed");
                 cache::SourceColorInfo decoded;
                 Expect(!cache::ReadSourceColorInfo(encoded, cache::kMaximumSourceProfileBytes + 9, &decoded),
                     "Source context reader accepted an oversized block");
                 Expect(!cache::WriteSourceColorInfo(encoded, {cache::SourceColorKind::DisplayConverted, {}}),
                     "Source context writer accepted display-specific pixels");
             }

            template<class Predicate>
            void WaitForColor(Predicate predicate, const char* message)
            {
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
                while (!predicate() && std::chrono::steady_clock::now() < deadline)
                {
                    std::this_thread::yield();
                }
                Expect(predicate(), message);
            }

            void RunDisplayColorServiceScenario()
            {
                using namespace hyperbrowse;
                using services::DisplayColorService;
                using services::MonitorColorProfile;
                const auto linear = GeneratedRgbProfile(1);
                const auto square = GeneratedRgbProfile(2);
                std::atomic_bool replaced{};
                const auto provider = [&](const std::wstring& device)
                {
                    MonitorColorProfile profile;
                    profile.status = services::MonitorProfileStatus::Available;
                    profile.identity = device + (replaced ? L"-new" : L"-old");
                    profile.bytes = device == L"B" || replaced ? square : linear;
                    return profile;
                };
                Expect(services::MonitorProfileScopeFor(true) == services::MonitorProfileScope::CurrentUser
                           && services::MonitorProfileScopeFor(false) == services::MonitorProfileScope::System,
                       "Monitor association did not choose current-user/system scope correctly");
                void* bits = nullptr;
                HBITMAP bitmap = decode::wic_support::CreateBitmapBuffer(3, 1, &bits);
                Expect(bitmap && bits, "Display service fixture allocation failed");
                const std::array<unsigned char, 12> pixels{32, 64, 128, 255, 16, 32, 64, 128, 0, 0, 0, 0};
                std::memcpy(bits, pixels.data(), pixels.size());
                auto source = std::make_shared<cache::CachedThumbnail>(bitmap, 3, 1, 12, 3, 1,
                                                                      cache::SourceColorInfo{cache::SourceColorKind::Icc, linear});
                const cache::ThumbnailCacheKey key{L"C:\\color-fixture.png", 7, 3, 1};
                DisplayColorService first(12, provider);
                DisplayColorService second(12, provider);
                first.RefreshMonitor(L"A");
                second.RefreshMonitor(L"B");
                WaitForColor([&]() { return !first.GetStatistics().profileLookupPending && !second.GetStatistics().profileLookupPending; },
                             "Injected monitor profile lookup did not complete");
                Expect(first.ImageForDisplay(key, source) == source, "Pending transform did not preserve canonical rendering");
                second.ImageForDisplay(key, source);
                WaitForColor([&]() { return first.GetStatistics().convertedBytes == 12 && second.GetStatistics().convertedBytes == 12; },
                             "Per-window display conversion did not complete");
                const auto firstOutput = first.ImageForDisplay(key, source);
                const auto secondOutput = second.ImageForDisplay(key, source);
                DIBSECTION firstDib{}, secondDib{};
                GetObjectW(firstOutput->Bitmap(), sizeof(firstDib), &firstDib);
                GetObjectW(secondOutput->Bitmap(), sizeof(secondDib), &secondDib);
                Expect(std::memcmp(firstDib.dsBm.bmBits, secondDib.dsBm.bmBits, pixels.size()) != 0,
                       "Equal-DPI monitor destinations produced the same profile-specific output");
                Expect(std::memcmp(bits, pixels.data(), pixels.size()) == 0, "Per-window conversion contaminated canonical pixels");
                first.SetEnabled(false);
                Expect(first.ImageForDisplay(key, source) == source, "Toggle off did not restore canonical pixels immediately");
                first.SetEnabled(true);
                WaitForColor([&]() { return !first.GetStatistics().profileLookupPending; }, "Toggle on did not re-resolve the monitor profile");
                first.ImageForDisplay(key, source);
                WaitForColor([&]() { return first.GetStatistics().convertedBytes == 12; }, "Toggle on did not refresh display pixels");
                replaced = true;
                const auto prior = first.ImageForDisplay(key, source);
                const auto oldGeneration = first.GetStatistics().profileGeneration;
                first.RefreshMonitor(L"A", true);
                Expect(first.ImageForDisplay(key, source) == prior, "Profile refresh blanked prior display output");
                WaitForColor([&]() { return first.GetStatistics().profileGeneration != oldGeneration; }, "Profile replacement on a stationary monitor was not detected");
                first.ImageForDisplay(key, source);
                WaitForColor([&]() { return first.ImageForDisplay(key, source) != prior; }, "Profile replacement did not refresh converted output");
                Expect(second.ImageForDisplay(key, source) == secondOutput, "One window's profile refresh altered another window");
                for (int index = 0; index < 3; ++index)
                {
                    cache::ThumbnailCacheKey other = key;
                    other.modifiedTimestampUtc += static_cast<std::uint64_t>(index + 1);
                    first.ImageForDisplay(other, source);
                }
                WaitForColor([&]() { return first.GetStatistics().revision >= 7; }, "Bounded cache conversion did not settle");
                Expect(first.GetStatistics().convertedBytes <= 12, "Display cache exceeded its configured byte bound");

                std::mutex gateMutex;
                std::condition_variable gateCondition;
                bool release{};
                std::atomic_bool started{};
                DisplayColorService stale(4096, provider,
                    [&](const cache::ThumbnailCacheKey&, const cache::CachedThumbnail& input, const MonitorColorProfile& destination, std::wstring* error)
                    {
                        started = true;
                        std::unique_lock gateLock(gateMutex);
                        gateCondition.wait_for(gateLock, std::chrono::seconds(5), [&]() { return release; });
                        gateLock.unlock();
                        return decode::color::TransformForDisplay(input, input.SourceColor(), destination.bytes, error);
                    });
                stale.RefreshMonitor(L"A");
                WaitForColor([&]() { return !stale.GetStatistics().profileLookupPending; }, "Stale-result fixture profile did not resolve");
                stale.ImageForDisplay(key, source);
                WaitForColor([&]() { return started.load(); }, "Stale-result fixture transform did not start");
                stale.SetEnabled(false);
                {
                    std::scoped_lock gateLock(gateMutex);
                    release = true;
                }
                gateCondition.notify_all();
                WaitForColor([&]() { return stale.GetStatistics().staleCompletions != 0; }, "A disabled setting accepted an old async transform");
                Expect(stale.ImageForDisplay(key, source) == source && stale.GetStatistics().convertedBytes == 0,
                       "Stale transform replaced the non-color-managed image");
                for (bool monitorMove : {false, true})
                {
                    {
                        std::scoped_lock gateLock(gateMutex);
                        release = false;
                    }
                    started = false;
                    DisplayColorService changed(4096, provider,
                        [&](const cache::ThumbnailCacheKey&, const cache::CachedThumbnail& input, const MonitorColorProfile& destination, std::wstring* error)
                        {
                            started = true;
                            std::unique_lock gateLock(gateMutex);
                            gateCondition.wait_for(gateLock, std::chrono::seconds(5), [&]() { return release; });
                            gateLock.unlock();
                            return decode::color::TransformForDisplay(input, input.SourceColor(), destination.bytes, error);
                        });
                    changed.RefreshMonitor(L"A");
                    WaitForColor([&]() { return !changed.GetStatistics().profileLookupPending; }, "Stale identity profile did not resolve");
                    changed.ImageForDisplay(key, source);
                    WaitForColor([&]() { return started.load(); }, "Stale identity transform did not start");
                    if (monitorMove) changed.RefreshMonitor(L"B");
                    else changed.InvalidateImages();
                    {
                        std::scoped_lock gateLock(gateMutex);
                        release = true;
                    }
                    gateCondition.notify_all();
                    WaitForColor([&]() { return changed.GetStatistics().staleCompletions != 0; },
                                 "Source invalidation or equal-DPI monitor move accepted an obsolete transform");
                    Expect(changed.GetStatistics().convertedBytes == 0, "An obsolete source/profile transform leaked into display output");
                }
            }

            const std::array<unsigned char, 24> kColorFixturePixels{
                32, 64, 128, 255, 64, 128, 192, 255, 32, 64, 128, 128,
                96, 160, 224, 255, 0, 0, 0, 0, 192, 128, 64, 255};

            void WriteColorFixture(const fs::path& path, const std::vector<unsigned char>& profile, bool oriented = false)
            {
                using Microsoft::WRL::ComPtr;
                ComPtr<IWICImagingFactory> factory;
                ComPtr<IWICStream> stream;
                ComPtr<IWICBitmapEncoder> encoder;
                ComPtr<IWICBitmapFrameEncode> frame;
                ComPtr<IPropertyBag2> properties;
                Expect(hyperbrowse::decode::wic_support::InitializeWicFactory(&factory, nullptr), "Color fixture WIC factory failed");
                Expect(SUCCEEDED(factory->CreateStream(&stream))
                           && SUCCEEDED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE))
                           && SUCCEEDED(factory->CreateEncoder(oriented ? GUID_ContainerFormatTiff : GUID_ContainerFormatPng, nullptr, &encoder))
                           && SUCCEEDED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache))
                           && SUCCEEDED(encoder->CreateNewFrame(&frame, &properties))
                           && SUCCEEDED(frame->Initialize(properties.Get()))
                           && SUCCEEDED(frame->SetSize(3, 2)),
                       "Color fixture WIC encoder initialization failed");
                WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
                Expect(SUCCEEDED(frame->SetPixelFormat(&format)) && format == GUID_WICPixelFormat32bppBGRA,
                       "Color fixture WIC encoder does not support straight-alpha BGRA");
                if (!profile.empty())
                {
                    ComPtr<IWICColorContext> context;
                    Expect(SUCCEEDED(factory->CreateColorContext(&context))
                               && SUCCEEDED(context->InitializeFromMemory(profile.data(), static_cast<UINT>(profile.size()))),
                           "Color fixture ICC context initialization failed");
                    IWICColorContext* contexts[]{context.Get()};
                    Expect(SUCCEEDED(frame->SetColorContexts(1, contexts)), "Color fixture encoder lost its embedded ICC profile");
                }
                if (oriented)
                {
                    ComPtr<IWICMetadataQueryWriter> metadata;
                    PROPVARIANT orientation{};
                    orientation.vt = VT_UI2;
                    orientation.uiVal = 6;
                    Expect(SUCCEEDED(frame->GetMetadataQueryWriter(&metadata))
                               && SUCCEEDED(metadata->SetMetadataByName(L"/ifd/{ushort=274}", &orientation)),
                           "Color fixture TIFF orientation could not be written");
                    if (!profile.empty())
                    {
                        PROPVARIANT embedded{};
                        embedded.vt = VT_VECTOR | VT_UI1;
                        embedded.caub.cElems = static_cast<ULONG>(profile.size());
                        embedded.caub.pElems = const_cast<unsigned char*>(profile.data());
                        Expect(SUCCEEDED(metadata->SetMetadataByName(L"/ifd/{ushort=34675}", &embedded)),
                               "Color fixture TIFF ICC metadata could not be written");
                    }
                }
                auto pixels = kColorFixturePixels;
                Expect(SUCCEEDED(frame->WritePixels(2, 12, static_cast<UINT>(pixels.size()), pixels.data()))
                           && SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit()),
                       "Color fixture WIC encoding failed");
            }

            template<class Predicate>
            void PumpColorUntil(Predicate predicate, const char* message)
            {
                WaitForColor([&]()
                {
                    MSG pending{};
                    for (std::size_t dispatched = 0; dispatched < 64 && PeekMessageW(&pending, nullptr, 0, 0, PM_REMOVE); ++dispatched)
                    {
                        if (pending.message != WM_QUIT)
                        {
                            TranslateMessage(&pending);
                            DispatchMessageW(&pending);
                        }
                    }
                    return predicate();
                }, message);
            }

            void RunEmbeddedColorFixtureScenario(const fs::path& path, const fs::path& orientedPath,
                                                 const std::vector<unsigned char>& linear, const std::vector<unsigned char>& square)
            {
                using namespace hyperbrowse;
                std::wstring error;
                const auto decoded = decode::DecodeFullImage(browser::BuildBrowserItemFromPath(path), &error);
                Expect(decoded && decoded->SourceColor().kind == cache::SourceColorKind::Icc
                           && decoded->SourceColor().iccProfile == linear,
                       "Full WIC decode did not retain the embedded source ICC profile");
                const cache::ThumbnailCacheKey thumbnailKey{path.wstring(), 7, 3, 2};
                const auto thumbnail = decode::WicThumbnailDecoder{}.Decode(thumbnailKey, &error);
                Expect(thumbnail && thumbnail->SourceColor().iccProfile == linear,
                       "WIC thumbnail decode did not retain the embedded source ICC profile");
                const auto oriented = decode::DecodeFullImage(browser::BuildBrowserItemFromPath(orientedPath), &error);
                Expect(oriented && oriented->Width() == 2 && oriented->Height() == 3,
                       "Color fixture canonical decode lost EXIF orientation");
                const auto transformed = decode::color::TransformForDisplay(*oriented, oriented->SourceColor(), square, &error);
                Expect(transformed && transformed->Width() == 2 && transformed->Height() == 3,
                       "Color transform changed oriented image dimensions");
                DIBSECTION dib{};
                GetObjectW(transformed->Bitmap(), sizeof(dib), &dib);
                const auto* output = static_cast<const unsigned char*>(dib.dsBm.bmBits);
                const std::array<std::size_t, 6> rotatedOrder{3, 0, 4, 1, 5, 2};
                for (std::size_t pixel = 0; pixel < rotatedOrder.size(); ++pixel)
                {
                    const auto sourcePixel = rotatedOrder[pixel] * 4;
                    const unsigned int alpha = kColorFixturePixels[sourcePixel + 3];
                    Expect(output[pixel * 4 + 3] == alpha, "Color transform did not preserve oriented alpha");
                    for (std::size_t channel = 0; channel < 3; ++channel)
                    {
                        const int expected = static_cast<int>(std::lround(std::sqrt(kColorFixturePixels[sourcePixel + channel] / 255.0) * alpha));
                        Expect(std::abs(static_cast<int>(output[pixel * 4 + channel]) - expected) <= 4,
                               "Embedded ICC color/alpha conversion lost oriented pixel identity");
                    }
                }
                const auto info = decode::color::ReadSourceColorInfo(path.wstring());
                Expect(info.iccProfile == linear, "WIC did not resolve the source context for nvJPEG/legacy canonical pixels");
                     std::ifstream encodedFile(path, std::ios::binary);
                     const std::vector<unsigned char> encodedBytes((std::istreambuf_iterator<char>(encodedFile)), std::istreambuf_iterator<char>());
                     Expect(decode::color::ReadSourceColorInfo(encodedBytes).iccProfile == linear,
                         "The GPU-independent nvJPEG encoded-byte source context lost the embedded ICC profile");
                     const std::array<unsigned char, 2> malformedEncoded{0, 1};
                     Expect(decode::color::ReadSourceColorInfo(malformedEncoded).kind == cache::SourceColorKind::Unsupported,
                         "Malformed encoded source bytes did not select the diagnostic fallback");
            }
    }

        void RunDisplayColorFallbackScenario()
        {
            using namespace hyperbrowse;
            TempFolder root(L"HyperBrowseColorFallback");
            const auto linear = GeneratedRgbProfile(1);
            const auto untaggedPath = root.Root() / L"untagged.png";
            const auto malformedPath = root.Root() / L"malformed.tif";
            const auto unsupportedPath = root.Root() / L"unsupported.tif";
            WriteColorFixture(untaggedPath, {});
            auto malformed = linear;
            std::fill(malformed.begin() + 128, malformed.begin() + 132, static_cast<unsigned char>(0xFF));
            WriteColorFixture(malformedPath, malformed, true);
            auto unsupported = linear;
            std::memcpy(unsupported.data() + 16, "Lab ", 4);
            WriteColorFixture(unsupportedPath, unsupported, true);
            const auto provider = [&](const std::wstring& device)
            {
                services::MonitorColorProfile profile;
                profile.identity = device;
                profile.status = device == L"missing" ? services::MonitorProfileStatus::Unavailable
                    : device == L"invalid" ? services::MonitorProfileStatus::Invalid : services::MonitorProfileStatus::Available;
                profile.bytes = device == L"transform-error" ? std::vector<unsigned char>{0, 1} : linear;
                profile.diagnostic = L"Generated diagnostic fallback fixture";
                return profile;
            };
            const auto counter = [](std::wstring_view name)
            {
                const auto snapshot = util::CaptureDiagnosticsSnapshot();
                const auto found = std::find_if(snapshot.counters.begin(), snapshot.counters.end(), [&](const auto& row) { return row.name == name; });
                return found == snapshot.counters.end() ? std::uint64_t{} : found->value;
            };
            std::wstring error;
            const auto untagged = decode::DecodeFullImage(browser::BuildBrowserItemFromPath(untaggedPath), &error);
            Expect(untagged && untagged->SourceColor().kind == cache::SourceColorKind::Srgb,
                   "Untagged WIC input did not receive an explicit sRGB source context");
            const cache::ThumbnailCacheKey key{untaggedPath.wstring(), 1, 3, 2};
            {
                auto executor = std::make_unique<util::BackgroundExecutor>(1);
                services::DisplayColorService stopped(4096, provider, {}, executor.get());
                stopped.Shutdown();
                executor.reset();
                stopped.BindTargetWindow(nullptr);
                stopped.RefreshMonitor(L"available", true);
                Expect(stopped.ImageForDisplay(key, untagged) == untagged
                    && !stopped.GetStatistics().profileLookupPending && stopped.GetStatistics().entryCount == 0,
                    "Stopped display service accessed its retired borrowed executor");
                stopped.Shutdown();
            }
            for (const std::wstring device : {L"missing", L"invalid"})
            {
                const auto counterName = device == L"missing" ? L"color.profile.unavailable" : L"color.profile.invalid";
                const auto previous = counter(counterName);
                services::DisplayColorService service(4096, provider);
                service.RefreshMonitor(device);
                WaitForColor([&]() { return !service.GetStatistics().profileLookupPending && counter(counterName) > previous; },
                             "Missing/invalid monitor profile did not publish diagnostic fallback");
                Expect(service.ImageForDisplay(key, untagged) == untagged && service.GetStatistics().convertedBytes == 0,
                       "Missing/invalid monitor profile changed canonical display output");
            }
            const auto expectTransformFallback = [&](const fs::path& path, const std::wstring& device, cache::SourceColorKind expectedKind)
            {
                const auto image = decode::DecodeFullImage(browser::BuildBrowserItemFromPath(path), &error);
                  Expect(image && image->SourceColor().kind == expectedKind,
                      "Embedded ICC fallback was not retained for " + path.filename().string()
                      + ": kind=" + (image ? std::to_string(static_cast<int>(image->SourceColor().kind)) : "decode-failed"));
                services::DisplayColorService service(4096, provider);
                const auto previous = counter(L"color.transform_fallback");
                service.RefreshMonitor(device);
                WaitForColor([&]() { return !service.GetStatistics().profileLookupPending; }, "Transform-failure profile did not resolve");
                const cache::ThumbnailCacheKey imageKey{path.wstring(), 1, 3, 2};
                service.ImageForDisplay(imageKey, image);
                WaitForColor([&]() { return service.GetStatistics().failedConversions == 1 && counter(L"color.transform_fallback") > previous; },
                             "Transform failure did not settle with diagnostic fallback");
                for (int repeat = 0; repeat < 8; ++repeat)
                    Expect(service.ImageForDisplay(imageKey, image) == image, "Transform failure replaced existing canonical output");
                Expect(service.GetStatistics().completedConversions == 0 && service.GetStatistics().failedConversions == 1,
                       "Settled transform failure retried on every paint");
            };
            expectTransformFallback(malformedPath, L"available", cache::SourceColorKind::Unsupported);
            expectTransformFallback(unsupportedPath, L"available", cache::SourceColorKind::Unsupported);
            expectTransformFallback(untaggedPath, L"transform-error", cache::SourceColorKind::Srgb);

            for (bool legacyRaw : {false, true})
            {
                void* bits = nullptr;
                HBITMAP bitmap = decode::wic_support::CreateBitmapBuffer(3, 1, &bits);
                const std::array<unsigned char, 12> pixels{32, 64, 128, 255, 16, 32, 64, 128, 0, 0, 0, 0};
                Expect(bitmap && bits, "Legacy source-color fixture allocation failed");
                std::memcpy(bits, pixels.data(), pixels.size());
                auto image = std::make_shared<cache::CachedThumbnail>(bitmap, 3, 1, 12, 3, 1);
                const cache::ThumbnailCacheKey legacyKey{legacyRaw ? L"legacy.dng" : untaggedPath.wstring(), 1, 3, 1};
                services::DisplayColorService service(4096, provider);
                const auto previous = counter(L"color.source_context.worker_resolved");
                service.RefreshMonitor(L"available");
                WaitForColor([&]() { return !service.GetStatistics().profileLookupPending; }, "Legacy source profile did not resolve");
                service.ImageForDisplay(legacyKey, image);
                WaitForColor([&]() { return service.GetStatistics().completedConversions == 1; }, "Legacy RAW/nvJPEG source fallback did not convert");
                const auto display = service.ImageForDisplay(legacyKey, image);
                DIBSECTION dib{};
                GetObjectW(display->Bitmap(), sizeof(dib), &dib);
                const auto* output = static_cast<const unsigned char*>(dib.dsBm.bmBits);
                const std::array<int, 3> expected{4, 13, 55};
                for (std::size_t channel = 0; channel < expected.size(); ++channel)
                    Expect(std::abs(output[channel] - expected[channel]) <= 3, "Legacy RAW/nvJPEG sRGB fallback was not applied exactly once");
                Expect(counter(L"color.source_context.worker_resolved") > previous && std::memcmp(bits, pixels.data(), pixels.size()) == 0,
                       "Legacy source fallback was not diagnosed or contaminated canonical pixels");
            }
            void* transientBits = nullptr;
            HBITMAP transientBitmap = decode::wic_support::CreateBitmapBuffer(3, 1, &transientBits);
            Expect(transientBitmap && transientBits, "Display-cache source ownership fixture allocation failed");
            std::memset(transientBits, 0, 12);
            auto transient = std::make_shared<cache::CachedThumbnail>(transientBitmap, 3, 1, 12, 3, 1,
                                                                    cache::SourceColorInfo{cache::SourceColorKind::Srgb, {}});
            services::DisplayColorService tooSmall(8, provider);
            const auto oversizedBefore = counter(L"color.display_cache.too_large");
            tooSmall.RefreshMonitor(L"available");
            WaitForColor([&]() { return !tooSmall.GetStatistics().profileLookupPending; }, "Bounded fallback profile did not resolve");
            Expect(tooSmall.ImageForDisplay(key, transient) == transient && tooSmall.GetStatistics().entryCount == 0
                       && counter(L"color.display_cache.too_large") > oversizedBefore,
                   "An over-budget image did not select a bounded, diagnosed canonical fallback");
            services::DisplayColorService bounded(12, provider);
            bounded.RefreshMonitor(L"available");
            WaitForColor([&]() { return !bounded.GetStatistics().profileLookupPending; }, "Source ownership profile did not resolve");
            bounded.ImageForDisplay(key, transient);
            WaitForColor([&]() { return bounded.GetStatistics().completedConversions == 1; }, "Source ownership conversion did not settle");
            std::weak_ptr<const cache::CachedThumbnail> retired = transient;
            transient.reset();
            WaitForColor([&]() { return retired.expired(); }, "Settled display cache pinned a retired canonical source image");
            Expect(bounded.GetStatistics().convertedBytes <= 12, "Source release exceeded the display-byte budget");
        }

    void RunColorManagementScenarios()
    {
        RunWicColorPixelsScenario();
        RunColorMetadataPersistenceScenario();
        RunRawHelperProtocolScenario();
        RunDisplayColorServiceScenario();
        RunDisplayColorFallbackScenario();
    }

    void RunColorManagementWindowScenarios(HINSTANCE instance, HWND owner)
    {
        using namespace hyperbrowse;
        TempFolder root(L"HyperBrowseColorWindows");
        const auto linear = GeneratedRgbProfile(1);
        const auto square = GeneratedRgbProfile(2);
        std::vector<browser::BrowserItem> items;
        for (int index = 0; index < 4; ++index)
        {
            const auto path = root.Root() / (L"color-" + std::to_wstring(index) + L".png");
            WriteColorFixture(path, linear);
            items.push_back(browser::BuildBrowserItemFromPath(path));
        }
        const auto orientedPath = root.Root() / L"oriented.tif";
        WriteColorFixture(orientedPath, linear, true);
        RunEmbeddedColorFixtureScenario(fs::path(items.front().filePath), orientedPath, linear, square);
        std::atomic_bool replaced{};
        const DWORD uiThread = GetCurrentThreadId();
        const auto provider = [&](const std::wstring& device)
        {
            Expect(GetCurrentThreadId() != uiThread, "Monitor profile resolution ran on the UI thread");
            Expect(!device.empty(), "Rendering surface did not capture its own monitor identity");
            services::MonitorColorProfile profile;
            profile.status = services::MonitorProfileStatus::Available;
            profile.identity = replaced ? L"generated-linear" : L"generated-square";
            profile.bytes = replaced ? linear : square;
            return profile;
        };
        browser::BrowserModel model;
        model.Reset(root.Root().wstring(), false);
        model.AppendItems(std::vector<browser::BrowserItem>(items), items.size(), 0);
        model.Complete();
        browser::BrowserPane browserPane(instance);
        browserPane.SetPersistentThumbnailCacheEnabled(false);
        browserPane.SetColorProfileProvider(provider);
        Expect(browserPane.Create(owner), "Color-management browser surface could not be created");
        SetWindowPos(browserPane.Hwnd(), nullptr, 0, 0, 640, 480, SWP_NOZORDER | SWP_NOACTIVATE);
        browserPane.SetModel(&model);
        browserPane.RefreshFromModel();
        PumpColorUntil([&]()
        {
            SendMessageW(browserPane.Hwnd(), WM_PAINT, 0, 0);
            return browserPane.DisplayColorStatistics().completedConversions >= 1;
        }, "Browser thumbnail paint did not prepare monitor-specific color output");
        browserPane.SetColorManagementEnabled(false);
        Expect(!browserPane.IsColorManagementEnabled() && browserPane.DisplayColorStatistics().convertedBytes == 0,
               "Browser toggle off retained display-specific pixels");
        browserPane.SetColorManagementEnabled(true);
        const auto browserCompleted = browserPane.DisplayColorStatistics().completedConversions;
        PumpColorUntil([&]()
        {
            SendMessageW(browserPane.Hwnd(), WM_PAINT, 0, 0);
            return browserPane.DisplayColorStatistics().completedConversions > browserCompleted;
        }, "Browser toggle on did not refresh the existing thumbnail surface");

        viewer::ViewerWindow first(instance);
        viewer::ViewerWindow second(instance);
        first.SetColorProfileProvider(provider);
        second.SetColorProfileProvider([&](const std::wstring&)
        {
            services::MonitorColorProfile profile;
            profile.status = services::MonitorProfileStatus::Available;
            profile.identity = L"independent-linear";
            profile.bytes = linear;
            return profile;
        });
        first.SetManualTransitionEnabled(false);
        second.SetManualTransitionEnabled(false);
        Expect(first.Open(owner, items, 0, false) && second.Open(owner, items, 0, false),
               "Independent color-management viewer windows could not open");
        PumpColorUntil([&]()
        {
            SendMessageW(first.Hwnd(), WM_PAINT, 0, 0);
            SendMessageW(second.Hwnd(), WM_PAINT, 0, 0);
            return first.DisplayColorStatistics().completedConversions >= 1 && second.DisplayColorStatistics().completedConversions >= 1;
        }, "Single viewer paint did not prepare independent display-color output");
        Expect(first.DisplayColorStatistics().profileIdentity != second.DisplayColorStatistics().profileIdentity,
               "Two windows sharing a source image did not retain independent destination identities");
        for (std::size_t tileCount = 2; tileCount <= 4; ++tileCount)
        {
            std::vector<int> indexes;
            for (std::size_t index = 0; index < tileCount; ++index) indexes.push_back(static_cast<int>(index));
            Expect(first.BeginCompareSession(indexes), "Color-managed compare session did not start");
            PumpColorUntil([&]()
            {
                SendMessageW(first.Hwnd(), WM_PAINT, 0, 0);
                return first.DisplayColorStatistics().convertedBytes >= tileCount * 24;
            }, "Compare paint did not color-convert every tile");
            Expect(first.CompareTileCount() == tileCount && first.IsCompareSessionActive(), "Display conversion changed compare tile state");
        }
        const int focused = first.FocusedCompareTile();
        const POINT pan = first.PanOffset();
        const auto independentCompletions = second.DisplayColorStatistics().completedConversions;
        const auto priorConversions = first.DisplayColorStatistics().completedConversions;
        replaced = true;
        SendMessageW(first.Hwnd(), WM_SETTINGCHANGE, 0, 0);
        PumpColorUntil([&]()
        {
            SendMessageW(first.Hwnd(), WM_PAINT, 0, 0);
            return first.DisplayColorStatistics().completedConversions >= priorConversions + 4;
        }, "Stationary viewer profile change did not refresh all compare tiles");
        Expect(first.FocusedCompareTile() == focused && first.PanOffset().x == pan.x && first.PanOffset().y == pan.y
                   && second.DisplayColorStatistics().completedConversions == independentCompletions,
               "Profile change reset compare geometry or changed another viewer's output");
        first.SetColorManagementEnabled(false);
        Expect(!first.IsColorManagementEnabled() && first.DisplayColorStatistics().convertedBytes == 0 && first.CompareTileCount() == 4,
               "Viewer toggle off did not retain compare state and restore canonical rendering");
        first.SetColorManagementEnabled(true);
        const auto enabledConversions = first.DisplayColorStatistics().completedConversions;
        first.RecoverDisplaySurface();
        PumpColorUntil([&]()
        {
            SendMessageW(first.Hwnd(), WM_PAINT, 0, 0);
            return first.DisplayColorStatistics().completedConversions >= enabledConversions + 4;
        }, "Toggle/display recovery did not refresh all compare tiles");
        for (int navigation = 0; navigation < 3; ++navigation)
            SendMessageW(second.Hwnd(), WM_KEYDOWN, VK_RIGHT, 0);
        PumpColorUntil([&]()
        {
            SendMessageW(second.Hwnd(), WM_PAINT, 0, 0);
            return second.CurrentFilePath() == items[3].filePath
                && second.DisplayColorStatistics().completedConversions > independentCompletions;
        }, "Rapid viewer navigation did not settle on the final source identity");
        const std::vector<browser::BrowserItem> replacedItems{items[2], items[1]};
        Expect(second.ReplaceItems(replacedItems, 0), "Viewer source replacement did not preserve the window");
        PumpColorUntil([&]()
        {
            SendMessageW(second.Hwnd(), WM_PAINT, 0, 0);
            return second.CurrentFilePath() == items[2].filePath
                && second.DisplayColorStatistics().convertedBytes != 0;
        }, "Reused viewer indexes retained the wrong display-pixel identity");
        SendMessageW(first.Hwnd(), WM_CLOSE, 0, 0);
        SendMessageW(second.Hwnd(), WM_CLOSE, 0, 0);
        DestroyWindow(browserPane.Hwnd());
        std::atomic_bool lookupStarted{};
        std::mutex closeMutex;
        std::condition_variable closeCondition;
        bool releaseLookup{};
        viewer::ViewerWindow closing(instance);
        closing.SetColorProfileProvider([&](const std::wstring&)
        {
            lookupStarted = true;
            std::unique_lock closeLock(closeMutex);
            closeCondition.wait_for(closeLock, std::chrono::seconds(5), [&]() { return releaseLookup; });
            services::MonitorColorProfile profile;
            profile.status = services::MonitorProfileStatus::Available;
            profile.identity = L"closed-profile";
            profile.bytes = square;
            return profile;
        });
        Expect(closing.Open(owner, items, 0, false), "Close-during-profile-resolution viewer did not open");
        PumpColorUntil([&]() { return lookupStarted.load(); }, "Close-during-profile-resolution lookup did not start");
        const auto beforeClose = std::chrono::steady_clock::now();
        SendMessageW(closing.Hwnd(), WM_CLOSE, 0, 0);
        Expect(!closing.IsOpen() && std::chrono::steady_clock::now() - beforeClose < std::chrono::seconds(1),
               "Closing a viewer waited for profile resolution on the UI thread");
        {
            std::scoped_lock closeLock(closeMutex);
            releaseLookup = true;
        }
        closeCondition.notify_all();
        PumpColorUntil([&]() { return closing.DisplayColorStatistics().staleCompletions != 0; },
                       "Closed viewer accepted an obsolete profile completion");
    }

    void RunCodecReadinessScenarios()
    {
        using namespace services;
        RunHeicFormatAllowlistScenario();
        RunJpegXlFormatAllowlistScenario();
        Expect(!decode::IsWicFileType(L"heif") && !browser::IsSupportedImageExtension(L".heif"),
               "HEIF recognition changed without decoder-backed fixture evidence");
        Expect(decode::wic_support::DecoderListsExtension(L".jpg,.HEIC,.heif", L"heic"),
               "WIC decoder extension matching did not normalize case and a leading dot");
        Expect(decode::wic_support::DecoderListsExtension(L" .webp ; .JXL ", L".jxl"),
               "WIC decoder extension matching did not handle a separate exact token");
        Expect(!decode::wic_support::DecoderListsExtension(L".heif,.heic2,.jxl2", L"heic")
                   && !decode::wic_support::DecoderListsExtension(L".heif,.heic2,.jxl2", L"jxl"),
               "WIC decoder extension matching accepted an unrelated format or substring");
        Expect(!decode::wic_support::DecoderListsExtension(L"", L"heic")
                   && !decode::wic_support::DecoderListsExtension(L".heic", L"")
                   && !decode::wic_support::DecoderListsExtension(std::wstring(4097, L'x'), L"heic"),
               "WIC decoder extension matching did not reject empty or oversized input");

        std::atomic<int> phase{};
        std::atomic<int> calls{};
        std::atomic<DWORD> workerThread{};
        WicCodecReadinessService service([&]()
        {
            ++calls;
            workerThread.store(GetCurrentThreadId());
            if (phase.load() == 2)
            {
                throw std::runtime_error("Injected discovery failure");
            }
            WicCodecDiscoverySnapshot discovery;
            if (phase.load() == 1)
            {
                discovery.heic = {WicCodecDiscoveryState::Ready, L"Injected HEIC decoder", {}};
            }
            else if (phase.load() == 3)
            {
                discovery.heic = {WicCodecDiscoveryState::Ready, std::wstring(300, L'x'), {}};
                discovery.jpegXl = {WicCodecDiscoveryState::DiscoveryFailed, {}, std::wstring(1024, L'y')};
            }
            else if (phase.load() == 4)
            {
                discovery.jpegXl = {WicCodecDiscoveryState::Ready, L"Injected \u65e5\u672c JPEG XL decoder\r\n", {}};
            }
            return discovery;
        });
        Expect(calls.load() == 0 && service.Snapshot().heic.discovery.state == WicCodecDiscoveryState::NotChecked,
               "Codec service performed discovery during construction or a cached read");
        const auto refresh = [&]()
        {
            Expect(service.Refresh(), "Codec readiness refresh was not queued");
            PumpColorUntil([&]() { return !service.Snapshot().refreshing; }, "Codec discovery did not complete");
        };
        refresh();
        Expect(workerThread.load() != GetCurrentThreadId()
                   && service.Snapshot().heic.discovery.state == WicCodecDiscoveryState::Missing
                   && service.Snapshot().jpegXl.discovery.state == WicCodecDiscoveryState::Missing,
               "Missing decoder discovery was not completed off the calling thread");
        service.Snapshot();
        service.Snapshot();
        Expect(calls.load() == 1, "Cached readiness reads repeated discovery");
        phase.store(1);
        refresh();
        auto snapshot = service.Snapshot();
        Expect(snapshot.heic.discovery.state == WicCodecDiscoveryState::Ready
                   && snapshot.heic.fullImage.state == WicCodecDecodeState::NotAttempted,
               "Registered decoder readiness was incorrectly treated as a successful decode");
        service.RecordDecode(L".HEIC", WicCodecDecodeKind::Thumbnail, true);
        service.RecordDecode(L"heic", WicCodecDecodeKind::FullImage, false);
        service.RecordDecode(L"JXL", WicCodecDecodeKind::FullImage, true);
        service.RecordDecode(L"jpg", WicCodecDecodeKind::FullImage, false);
        snapshot = service.Snapshot();
        Expect(snapshot.heic.thumbnail.state == WicCodecDecodeState::Succeeded
                   && snapshot.heic.thumbnail.successes == 1
                   && snapshot.heic.fullImage.state == WicCodecDecodeState::DecodeFailed
                   && snapshot.heic.fullImage.failures == 1
                   && snapshot.jpegXl.fullImage.successes == 1,
               "Codec observations did not distinguish formats and thumbnail/full-image outcomes");
        phase.store(2);
        refresh();
        snapshot = service.Snapshot();
        Expect(snapshot.heic.discovery.state == WicCodecDiscoveryState::DiscoveryFailed
                   && !snapshot.heic.discovery.errorMessage.empty()
                   && snapshot.heic.thumbnail.successes == 1 && snapshot.jpegXl.fullImage.successes == 1
                   && decode::IsWicFileType(L"heic") && decode::IsWicFileType(L"jxl"),
               "Discovery failure erased decode evidence or changed extension recognition");
        phase.store(3);
        refresh();
        snapshot = service.Snapshot();
        Expect(snapshot.heic.discovery.decoderName.size() == 128
                   && snapshot.jpegXl.discovery.errorMessage.size() == 512,
               "Codec readiness snapshot retained unbounded provider text");
        util::DiagnosticsSnapshot diagnostics;
        diagnostics.derived.push_back({L"existing.metric", L"preserved"});
        diagnostics.counters.push_back({L"existing.counter", 7});
        util::UpdateWicCodecReadinessDiagnostics(diagnostics, snapshot);
        const auto derivedCount = diagnostics.derived.size();
        const auto counterCount = diagnostics.counters.size();
        util::UpdateWicCodecReadinessDiagnostics(diagnostics, snapshot);
        const auto value = [&](std::wstring_view name)
        {
            for (const auto& row : diagnostics.derived)
            {
                if (row.name == name)
                {
                    return row.value;
                }
            }
            return std::wstring{};
        };
        Expect(diagnostics.derived.size() == derivedCount && diagnostics.counters.size() == counterCount
                   && value(L"existing.metric") == L"preserved" && diagnostics.counters.front().value == 7,
               "Codec diagnostics duplicated rows or replaced unrelated captured metrics");
        Expect(value(L"codecs.heic.decoder") == L"Ready (decoder created)"
                   && value(L"codecs.heic.full_image") == L"Decode failed"
                   && value(L"codecs.jxl.decoder") == L"Discovery failed"
                   && value(L"codecs.jxl.full_image") == L"Succeeded",
               "Diagnostics conflated discovery readiness with a completed decode outcome");
        TempFolder exportRoot(L"HyperBrowseCodecRedaction-" + std::to_wstring(GetCurrentProcessId()));
        const fs::path exportPath = exportRoot.Root() / L"codec-diagnostics.json";
        const std::wstring privatePath = L"C:\\Users\\PrivateCodecUser\\codec.dll";
        for (auto& row : diagnostics.derived)
        {
            if (row.name.ends_with(L".decoder_name") || row.name.ends_with(L".discovery_error"))
            {
                row.value = privatePath;
            }
        }
        Expect(util::WriteRedactedDiagnosticsSnapshot(exportPath.wstring(), diagnostics),
               "Injected codec diagnostics could not be exported");
        std::ifstream exported(exportPath, std::ios::binary);
        const std::string json((std::istreambuf_iterator<char>(exported)), std::istreambuf_iterator<char>());
        Expect(json.find("PrivateCodecUser") == std::string::npos && json.find("decoder_name") == std::string::npos
                   && json.find("discovery_error") == std::string::npos
                   && json.find("codecs.heic.decoder") != std::string::npos && json.find("Decode failed") != std::string::npos,
               "Redacted codec export retained external path-bearing text or removed the useful states");
        phase.store(4);
        refresh();
        snapshot = service.Snapshot();
        Expect(snapshot.heic.discovery.state == WicCodecDiscoveryState::Missing
                   && snapshot.jpegXl.discovery.state == WicCodecDiscoveryState::Ready
                   && snapshot.jpegXl.discovery.decoderName.find(L"\u65e5\u672c") != std::wstring::npos
                   && snapshot.jpegXl.discovery.decoderName.find_first_of(L"\r\n") == std::wstring::npos,
               "JPEG XL readiness did not recover from failure or preserve bounded Unicode decoder text");
        service.ResetDecodeObservations();
        snapshot = service.Snapshot();
        Expect(snapshot.jpegXl.discovery.state == WicCodecDiscoveryState::Ready
                   && snapshot.heic.thumbnail.state == WicCodecDecodeState::NotAttempted
                   && snapshot.jpegXl.fullImage.successes == 0,
               "Resetting codec observations invalidated decoder discovery or retained decode counts");
        service.Shutdown();
        service.Shutdown();
        service.RecordDecode(L"heic", WicCodecDecodeKind::FullImage, true);
        Expect(!service.Refresh() && service.Snapshot().heic.fullImage.successes == 0,
               "Stopped codec service accepted another refresh or decode observation");

        std::mutex discoveryMutex;
        std::condition_variable discoveryCondition;
        bool releaseDiscovery = false;
        std::atomic_bool started{};
        std::atomic<int> blockedCalls{};
        WicCodecReadinessService blocked([&]()
        {
            const int call = ++blockedCalls;
            started.store(true);
            std::unique_lock lock(discoveryMutex);
            discoveryCondition.wait_for(lock, std::chrono::seconds(5), [&]() { return releaseDiscovery; });
            WicCodecDiscoverySnapshot discovery;
            if (call == 1)
            {
                discovery.heic = {WicCodecDiscoveryState::Ready, L"Injected concurrent decoder", {}};
            }
            else
            {
                discovery.jpegXl = {WicCodecDiscoveryState::Ready, L"Injected stale decoder", {}};
            }
            return discovery;
        });
        Expect(blocked.Refresh(), "Blocking discovery provider was not queued");
        PumpColorUntil([&]() { return started.load(); }, "Blocking discovery provider did not start");
        const auto beforeBusyRequest = std::chrono::steady_clock::now();
        Expect(!blocked.Refresh() && blocked.Snapshot().refreshing
                   && std::chrono::steady_clock::now() - beforeBusyRequest < std::chrono::seconds(1),
               "A busy discovery request blocked the caller or queued redundant work");
        blocked.RecordDecode(L"jxl", WicCodecDecodeKind::FullImage, true);
        {
            std::scoped_lock lock(discoveryMutex);
            releaseDiscovery = true;
        }
        discoveryCondition.notify_all();
        PumpColorUntil([&]() { return !blocked.Snapshot().refreshing; }, "Concurrent codec discovery did not complete");
        snapshot = blocked.Snapshot();
        Expect(snapshot.heic.discovery.state == WicCodecDiscoveryState::Ready && snapshot.jpegXl.fullImage.successes == 1,
               "Completing discovery replaced a decode observation recorded while it was running");
        {
            std::scoped_lock lock(discoveryMutex);
            releaseDiscovery = false;
        }
        started.store(false);
        Expect(blocked.Refresh(), "Codec shutdown fixture could not refresh its cached result");
        PumpColorUntil([&]() { return started.load(); }, "Codec shutdown provider did not restart");
        std::jthread shutdown([&]() { blocked.Shutdown(); });
        PumpColorUntil([&]() { return !blocked.Snapshot().refreshing; }, "Codec shutdown did not reject its pending result");
        Expect(!blocked.Refresh(), "Codec shutdown accepted new work");
        {
            std::scoped_lock lock(discoveryMutex);
            releaseDiscovery = true;
        }
        discoveryCondition.notify_all();
        shutdown.join();
        snapshot = blocked.Snapshot();
        Expect(blockedCalls.load() == 2 && snapshot.heic.discovery.state == WicCodecDiscoveryState::Ready
               && snapshot.jpegXl.discovery.state == WicCodecDiscoveryState::Missing
                   && snapshot.jpegXl.fullImage.successes == 1,
               "Codec service accepted a stale discovery completion or lost a concurrent decode observation");

         TempFolder fixtures(L"HyperBrowseOptionalCodecObservations-" + std::to_wstring(GetCurrentProcessId()));
         auto& observed = GetWicCodecReadinessService();
         observed.ResetDecodeObservations();
         for (std::wstring_view format : {L"heic", L"jxl"})
         {
             const fs::path renamed = fixtures.Root() / (L"renamed-png." + std::wstring(format));
             WriteColorFixture(renamed, {});
             browser::BrowserItem item;
             item.filePath = renamed.wstring();
             item.fileName = renamed.filename().wstring();
             item.fileType = format;
             cache::ThumbnailCacheKey key;
             key.filePath = item.filePath;
             key.targetWidth = 32;
             key.targetHeight = 32;
             std::wstring error;
             Expect(decode::DecodeFullImage(item, &error) != nullptr
                  && decode::WicThumbnailDecoder{}.Decode(key, &error) != nullptr,
                 "Optional codec telemetry altered WIC's content-based decode fallback");
         }
         snapshot = observed.Snapshot();
         Expect(snapshot.heic.fullImage.state == WicCodecDecodeState::NotAttempted
                 && snapshot.heic.thumbnail.state == WicCodecDecodeState::NotAttempted
                 && snapshot.jpegXl.fullImage.state == WicCodecDecodeState::NotAttempted
                 && snapshot.jpegXl.thumbnail.state == WicCodecDecodeState::NotAttempted,
             "A PNG renamed to an optional extension was treated as HEIC/JPEG XL decode evidence");
         for (std::wstring_view format : {L"heic", L"jxl"})
         {
             const fs::path invalid = fixtures.Root() / (L"malformed." + std::wstring(format));
             {
              std::ofstream stream(invalid, std::ios::binary);
              stream << "HyperBrowse malformed optional-codec smoke fixture";
             }
             browser::BrowserItem item;
             item.filePath = invalid.wstring();
             item.fileType = format;
             cache::ThumbnailCacheKey key;
             key.filePath = item.filePath;
             key.targetWidth = 32;
             key.targetHeight = 32;
             std::wstring error;
             Expect(decode::DecodeFullImage(item, &error) == nullptr && !error.empty(),
                 "Malformed optional full-image input did not retain its decode/error contract");
             Expect(decode::WicThumbnailDecoder{}.Decode(key, &error) == nullptr && !error.empty(),
                 "Malformed optional thumbnail input did not retain its decode/error contract");
         }
         snapshot = observed.Snapshot();
         Expect(snapshot.heic.fullImage.failures == 1 && snapshot.heic.thumbnail.failures == 1
                 && snapshot.jpegXl.fullImage.failures == 1 && snapshot.jpegXl.thumbnail.failures == 1,
             "WIC optional-codec failures were not recorded once at their actual decode owners");
         Expect(util::BuildDiagnosticsReport().find(fixtures.Root().wstring()) == std::wstring::npos,
             "Optional codec diagnostics retained a fixture filesystem path");
         util::ResetDiagnostics();
         Expect(observed.Snapshot().heic.fullImage.state == WicCodecDecodeState::NotAttempted,
             "Diagnostics reset did not clear native optional-codec observations");
    }

    void RunCodecReadinessWindowScenarios(HINSTANCE instance, HWND owner)
    {
        using namespace services;
        std::mutex providerMutex;
        std::condition_variable providerCondition;
        bool releaseProvider = false;
        std::atomic<int> phase{};
        std::atomic_bool started{};
        WicCodecReadinessService service([&]()
        {
            started.store(true);
            std::unique_lock lock(providerMutex);
            providerCondition.wait_for(lock, std::chrono::seconds(5), [&]() { return releaseProvider; });
            if (phase.load() == 1)
            {
                throw std::runtime_error("Injected window discovery failure");
            }
            WicCodecDiscoverySnapshot discovery;
            if (phase.load() == 2)
            {
                discovery.jpegXl = {WicCodecDiscoveryState::Ready, L"Injected window JPEG XL decoder", {}};
            }
            else
            {
                discovery.heic = {WicCodecDiscoveryState::Ready, L"Injected window HEIC decoder", {}};
            }
            return discovery;
        });
        ui::DiagnosticsWindow window(instance, [&]() { return service.Snapshot(); });
        const auto begin = [&]()
        {
            {
                std::scoped_lock lock(providerMutex);
                releaseProvider = false;
            }
            started.store(false);
            Expect(service.Refresh(), "Window codec discovery was not queued");
            PumpColorUntil([&]() { return started.load(); }, "Window codec provider did not start");
            util::DiagnosticsSnapshot snapshot;
            snapshot.derived.push_back({L"existing.metric", L"preserved"});
            window.Show(owner, L"CPU", L"WIC", L"(test)", std::move(snapshot), false);
            Expect(window.IsOpen(), "Codec diagnostics window did not open");
        };
        const auto release = [&]()
        {
            {
                std::scoped_lock lock(providerMutex);
                releaseProvider = true;
            }
            providerCondition.notify_all();
        };
        begin();
        struct WindowMatch
        {
            HWND owner{};
            HWND found{};
        } match{owner};
        EnumWindows([](HWND candidate, LPARAM parameter) -> BOOL
        {
            auto& result = *reinterpret_cast<WindowMatch*>(parameter);
            DWORD processId = 0;
            GetWindowThreadProcessId(candidate, &processId);
            std::array<wchar_t, 128> className{};
            GetClassNameW(candidate, className.data(), static_cast<int>(className.size()));
            if (processId == GetCurrentProcessId() && GetWindow(candidate, GW_OWNER) == result.owner
                && std::wstring_view(className.data()) == L"HyperBrowseDiagnosticsWindow")
            {
                result.found = candidate;
                return FALSE;
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&match));
        Expect(match.found != nullptr, "Native codec diagnostics window was not found by PID, owner, and class");
        std::vector<HWND> lists;
        EnumChildWindows(match.found, [](HWND child, LPARAM parameter) -> BOOL
        {
            std::array<wchar_t, 64> className{};
            GetClassNameW(child, className.data(), static_cast<int>(className.size()));
            if (std::wstring_view(className.data()) == WC_LISTVIEWW)
            {
                reinterpret_cast<std::vector<HWND>*>(parameter)->push_back(child);
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&lists));
        const auto value = [&](std::wstring_view name)
        {
            for (HWND list : lists)
            {
                for (int row = 0; row < ListView_GetItemCount(list); ++row)
                {
                    std::array<wchar_t, 128> label{};
                    ListView_GetItemText(list, row, 0, label.data(), static_cast<int>(label.size()));
                    if (std::wstring_view(label.data()) == name)
                    {
                        std::array<wchar_t, 768> text{};
                        ListView_GetItemText(list, row, 1, text.data(), static_cast<int>(text.size()));
                        return std::wstring(text.data());
                    }
                }
            }
            return std::wstring{};
        };
        Expect(value(L"codecs.discovery") == L"Refreshing" && value(L"existing.metric") == L"preserved",
               "Native codec diagnostics did not show pending discovery and existing snapshot data");
        release();
        PumpColorUntil([&]() { return value(L"codecs.heic.decoder") == L"Ready (decoder created)"; },
                       "Native codec diagnostics did not consume the completed cached result");
        Expect(value(L"codecs.jxl.decoder") == L"Missing" && value(L"codecs.heic.full_image") == L"Not observed"
                   && value(L"existing.metric") == L"preserved",
               "Native codec diagnostics conflated readiness with decode or dropped unrelated data");
        phase.store(1);
        begin();
        release();
        PumpColorUntil([&]() { return value(L"codecs.heic.decoder") == L"Discovery failed"; },
                       "Native codec diagnostics did not expose discovery failure");
        Expect(value(L"codecs.heic.decoder_name").empty() && !value(L"codecs.heic.discovery_error").empty(),
               "Native codec diagnostics retained a misleading ready name after failure");
         phase.store(2);
         begin();
         release();
         PumpColorUntil([&]() { return value(L"codecs.jxl.decoder") == L"Ready (decoder created)"; },
                  "Native JPEG XL diagnostics did not recover after refresh");
         Expect(value(L"codecs.heic.decoder") == L"Missing" && value(L"codecs.jxl.discovery_error").empty(),
             "Native codec refresh retained the previous failure or another format's ready state");
        phase.store(0);
        begin();
        const auto beforeClose = std::chrono::steady_clock::now();
        SendMessageW(match.found, WM_CLOSE, 0, 0);
        Expect(!window.IsOpen() && service.Snapshot().refreshing
                   && std::chrono::steady_clock::now() - beforeClose < std::chrono::seconds(1),
               "Closing codec diagnostics waited for discovery on the UI thread");
        release();
        PumpColorUntil([&]() { return !service.Snapshot().refreshing; },
                       "Codec discovery did not finish safely after its window closed");
    }

    std::wstring CaptureInstalledWicCodecReport()
    {
        auto& service = services::GetWicCodecReadinessService();
        Expect(service.Refresh(), "Installed WIC inventory could not be queued");
        PumpColorUntil([&]() { return !service.Snapshot().refreshing; }, "Installed WIC inventory did not complete");
        util::DiagnosticsSnapshot diagnostics;
        util::UpdateWicCodecReadinessDiagnostics(diagnostics, service.Snapshot());
        std::wstring report = L"Installed WIC inventory: registration/creation only, not fixture decode\n";
        for (const auto& row : diagnostics.derived)
        {
            report.append(row.name + L": " + row.value + L"\n");
        }
        return report;
    }

    void RunDecodePolicyScenarios()
    {
        RunRawFormatAllowlistScenario();
        RunWebpFormatAllowlistScenario();
        RunCodecReadinessScenarios();
        RunRawHelperProtocolScenario();
        RunThumbnailFailureClassificationScenario();
        RunColorManagementScenarios();
    }
}
