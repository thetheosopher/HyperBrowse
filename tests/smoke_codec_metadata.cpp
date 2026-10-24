#include "smoke_support.h"
#include "smoke_scenarios.h"

namespace hyperbrowse::tests::smoke
{

    void RunWicDecoderScenario()
    {
        TempFolder root(L"HyperBrowsePrompt5Decoder");
        const fs::path jpegPath = root.Root() / L"rotated.jpg";
        const fs::path pngPath = root.Root() / L"sample.png";
        const fs::path gifPath = root.Root() / L"sample.gif";
        const fs::path tiffPath = root.Root() / L"sample.tif";
        const fs::path corruptJpegPath = root.Root() / L"corrupt.jpg";

        WriteTestImage(jpegPath, TestImageFormat::Jpeg, 24, 48, 6);
        WriteTestImage(pngPath, TestImageFormat::Png, 96, 48);
        WriteTestImage(gifPath, TestImageFormat::Gif, 36, 18);
        WriteTestImage(tiffPath, TestImageFormat::Tiff, 18, 54);
        {
            std::ofstream corruptJpeg(corruptJpegPath, std::ios::binary);
            corruptJpeg << "not a JPEG";
        }

        hyperbrowse::decode::WicThumbnailDecoder decoder;

        const auto jpegThumbnail = decoder.Decode(MakeCacheKey(jpegPath, 1));
        Expect(jpegThumbnail != nullptr, "WIC failed to decode the JPEG thumbnail");
         Expect(jpegThumbnail->Width() > jpegThumbnail->Height(), "WIC did not apply JPEG EXIF orientation");
         Expect(jpegThumbnail->SourceWidth() == 48 && jpegThumbnail->SourceHeight() == 24,
             "WIC did not surface the oriented JPEG source dimensions");

         const auto jpegItem = hyperbrowse::browser::BuildBrowserItemFromPath(jpegPath);
         std::wstring fullImageError;
         const auto jpegFullImage = hyperbrowse::decode::DecodeFullImage(jpegItem, &fullImageError);
         Expect(jpegFullImage != nullptr,
             std::string("WIC failed to decode the rotated JPEG full image: ") + Utf8FromWide(fullImageError));
         Expect(jpegFullImage->Width() == 48 && jpegFullImage->Height() == 24,
             "WIC did not apply JPEG EXIF orientation to the full image");

        const auto pngThumbnail = decoder.Decode(MakeCacheKey(pngPath, 2));
        Expect(pngThumbnail != nullptr, "WIC failed to decode the PNG thumbnail");
         Expect(pngThumbnail->Width() <= 160 && pngThumbnail->Height() <= 112, "PNG thumbnail scaling exceeded the target bounds");
         Expect(pngThumbnail->SourceWidth() == 96 && pngThumbnail->SourceHeight() == 48,
             "WIC did not surface the PNG source dimensions");

        const auto gifThumbnail = decoder.Decode(MakeCacheKey(gifPath, 3));
         Expect(gifThumbnail != nullptr, "WIC failed to decode the GIF first frame thumbnail");
         Expect(gifThumbnail->SourceWidth() == 36 && gifThumbnail->SourceHeight() == 18,
             "WIC did not surface the GIF source dimensions");

        const auto tiffThumbnail = decoder.Decode(MakeCacheKey(tiffPath, 4));
         Expect(tiffThumbnail != nullptr, "WIC failed to decode the TIFF first page thumbnail");
         Expect(tiffThumbnail->SourceWidth() == 18 && tiffThumbnail->SourceHeight() == 54,
             "WIC did not surface the TIFF source dimensions");

         std::wstring corruptJpegError;
         const auto corruptJpegThumbnail = decoder.Decode(MakeCacheKey(corruptJpegPath, 5), &corruptJpegError);
         Expect(corruptJpegThumbnail == nullptr, "WIC unexpectedly decoded a corrupt JPEG");
         Expect(corruptJpegError.find(L"HRESULT 0x") != std::wstring::npos,
             "WIC did not provide an HRESULT for the corrupt JPEG decode failure");
    }

    void RunWicErrorReportingScenario()
    {
        namespace wic = hyperbrowse::decode::wic_support;

        std::wstring errorMessage;
        wic::SetError(&errorMessage, L"Invalid WIC input.", HRESULT_FROM_WIN32(ERROR_INVALID_DATA));
        Expect(errorMessage.find(L"HRESULT 0x8007000D") != std::wstring::npos,
               "WIC error formatting did not preserve the failing HRESULT");
        Expect(errorMessage.find(L"HRESULT 0x00000000") == std::wstring::npos,
               "WIC error formatting reported success for a failure status");

        void* bits = nullptr;
        HRESULT bitmapResult = S_OK;
        HBITMAP bitmap = wic::CreateBitmapBuffer(0, 1, &bits, &bitmapResult);
        if (bitmap)
        {
            DeleteObject(bitmap);
        }
        Expect(bitmap == nullptr, "WIC bitmap helper accepted an invalid width");
        Expect(bitmapResult == E_INVALIDARG, "WIC bitmap helper lost its invalid-argument status");
    }

    void RunJpegOrientationAdjustmentScenario()
    {
        TempFolder root(L"HyperBrowseJpegOrientation");

        const fs::path missingOrientationPath = root.Root() / L"missing-orientation.jpg";
        WriteTestImage(missingOrientationPath, TestImageFormat::Jpeg, 24, 48);
        const std::vector<BYTE> missingOrientationScan = ReadJpegScanData(missingOrientationPath);
        std::wstring missingOrientationError;
        const bool adjustedMissingOrientation = hyperbrowse::services::AdjustJpegOrientation(
            missingOrientationPath.wstring(),
            1,
            &missingOrientationError);
        Expect(adjustedMissingOrientation,
               std::string("JPEG orientation adjustment without an existing EXIF orientation failed: ")
                   + Utf8FromWide(missingOrientationError));
        Expect(ReadJpegOrientation(missingOrientationPath) == 6,
               "JPEG orientation adjustment did not add the missing EXIF orientation value");
        Expect(ReadJpegScanData(missingOrientationPath) == missingOrientationScan,
               "Adding JPEG orientation metadata changed the compressed image data");

        const fs::path existingExifPath = root.Root() / L"existing-exif-no-orientation.jpg";
        constexpr std::string_view kJpegDescription = "metadata remains intact";
        WriteTestImage(
            existingExifPath,
            TestImageFormat::Jpeg,
            24,
            48,
            1,
            {},
            {},
            kJpegDescription);
        const std::vector<BYTE> existingExifScan = ReadJpegScanData(existingExifPath);
        std::wstring existingExifError;
        const bool adjustedExistingExif = hyperbrowse::services::AdjustJpegOrientation(
            existingExifPath.wstring(),
            1,
            &existingExifError);
        Expect(adjustedExistingExif,
               std::string("JPEG orientation adjustment in existing EXIF metadata failed: ")
                   + Utf8FromWide(existingExifError));
        Expect(ReadJpegOrientation(existingExifPath) == 6,
               "JPEG orientation adjustment did not add the missing EXIF Orientation entry");
        Expect(ReadJpegMetadataString(existingExifPath, L"/app1/ifd/{ushort=270}") == kJpegDescription,
               "Adding JPEG Orientation did not preserve existing EXIF metadata");
        Expect(ReadJpegScanData(existingExifPath) == existingExifScan,
               "Adding JPEG Orientation changed the compressed image data");

        const auto runCase = [&](const wchar_t* fileName, int quarterTurnsDelta, std::uint16_t expectedOrientation)
        {
            const fs::path jpegPath = root.Root() / fileName;
            WriteTestImage(jpegPath, TestImageFormat::Jpeg, 24, 48, 6);
            const std::vector<BYTE> originalScan = ReadJpegScanData(jpegPath);

            std::wstring errorMessage;
            Expect(hyperbrowse::services::AdjustJpegOrientation(jpegPath.wstring(), quarterTurnsDelta, &errorMessage),
                   std::string("JPEG orientation adjustment failed: ") + Utf8FromWide(errorMessage));
            Expect(ReadJpegOrientation(jpegPath) == expectedOrientation,
                   "JPEG orientation adjustment wrote the wrong EXIF orientation value");
            Expect(ReadJpegScanData(jpegPath) == originalScan,
                   "Updating JPEG orientation metadata changed the compressed image data");
        };

        runCase(L"minus-one.jpg", -1, 1);
        runCase(L"plus-one.jpg", +1, 3);
        runCase(L"minus-three.jpg", -3, 3);
        runCase(L"plus-three.jpg", +3, 1);
    }

    void RunNvJpegHardwareScenario(const fs::path& jpegPath)
    {
        Expect(fs::is_regular_file(jpegPath), "nvJPEG hardware fixture does not exist");
        hyperbrowse::decode::SetNvJpegAccelerationEnabled(true);
        hyperbrowse::decode::NvJpegDecoder decoder;
        std::wstring errorMessage;
        const auto thumbnail = decoder.Decode(MakeCacheKey(jpegPath, 1), &errorMessage);
        Expect(thumbnail != nullptr,
               std::string("nvJPEG did not decode the hardware fixture: ") + Utf8FromWide(errorMessage));
        Expect(thumbnail->Width() > 0 && thumbnail->Height() > 0,
               "nvJPEG returned an empty hardware-decoded thumbnail");
    }

    void RunImageMetadataServiceScenario()
    {
        const auto itemA = MakeMetadataItem(L"alpha.jpg", L"C:\\Metadata\\alpha.jpg", 11);
        const auto itemB = MakeMetadataItem(L"beta.jpg", L"C:\\Metadata\\beta.jpg", 12);
        const auto itemC = MakeMetadataItem(L"gamma.jpg", L"C:\\Metadata\\gamma.jpg", 13);

        {
            hyperbrowse::services::ImageMetadataService service(
                1,
                2,
                hyperbrowse::util::ResourceProfile::Balanced,
                [](const hyperbrowse::browser::BrowserItem& item, std::wstring*)
                {
                    return MakeMetadata(item.fileName);
                });

            Expect(service.CacheCapacityEntries() == 2, "Metadata cache did not preserve the configured entry capacity");

            service.Schedule(1, {0, itemA, 0});
            service.Schedule(1, {1, itemB, 0});
            Expect(PumpMessagesUntil([&]()
            {
                return service.FindCachedMetadata(itemA) != nullptr && service.FindCachedMetadata(itemB) != nullptr;
            }, 5000), "Metadata service did not populate the initial cache entries");
            Expect(service.CacheEntryCount() == 2, "Metadata cache did not report the expected initial entry count");

            const auto cachedA = service.FindCachedMetadata(itemA);
            Expect(cachedA != nullptr && cachedA->cameraModel == L"alpha.jpg",
                   "Metadata service returned the wrong cached payload for the first item");

            service.Schedule(1, {2, itemC, 0});
            Expect(PumpMessagesUntil([&]()
            {
                return service.FindCachedMetadata(itemC) != nullptr;
            }, 5000), "Metadata service did not populate the replacement cache entry");
            Expect(service.CacheEntryCount() == 2, "Metadata cache grew past the configured entry capacity");
            Expect(service.FindCachedMetadata(itemA) != nullptr,
                   "Metadata cache evicted the most recently used entry instead of keeping it resident");
            Expect(service.FindCachedMetadata(itemB) == nullptr,
                   "Metadata cache did not evict the least recently used entry after reaching capacity");
                 const auto statistics = service.GetCacheStatistics();
                 Expect(statistics.hitCount >= 4 && statistics.missCount >= 1 && statistics.evictionCount == 1,
                     "Metadata cache statistics did not report lookup and eviction activity");
        }

        {
            struct BlockingState
            {
                std::mutex mutex;
                std::condition_variable started;
                std::condition_variable released;
                bool extractionStarted{};
                bool allowCompletion{};
            } cancellationState;

            const auto cancelledItem = MakeMetadataItem(L"cancelled.jpg", L"C:\\Metadata\\cancelled.jpg", 21);
            const auto currentItem = MakeMetadataItem(L"current.jpg", L"C:\\Metadata\\current.jpg", 22);

            hyperbrowse::services::ImageMetadataService service(
                1,
                4,
                hyperbrowse::util::ResourceProfile::Balanced,
                [&](const hyperbrowse::browser::BrowserItem& item, std::wstring*)
                {
                    if (item.modifiedTimestampUtc == cancelledItem.modifiedTimestampUtc)
                    {
                        std::unique_lock lock(cancellationState.mutex);
                        cancellationState.extractionStarted = true;
                        cancellationState.started.notify_all();
                        cancellationState.released.wait(lock, [&]()
                        {
                            return cancellationState.allowCompletion;
                        });
                    }

                    return MakeMetadata(item.fileName);
                });

            service.Schedule(41, {0, cancelledItem, 0});
            Expect(PumpMessagesUntil([&]()
            {
                std::scoped_lock lock(cancellationState.mutex);
                return cancellationState.extractionStarted;
            }, 1000), "Metadata cancellation scenario never started the blocked extraction");

            service.CancelOutstanding();
            service.Schedule(42, {1, currentItem, 0});

            {
                std::scoped_lock lock(cancellationState.mutex);
                cancellationState.allowCompletion = true;
            }
            cancellationState.released.notify_all();

            Expect(PumpMessagesUntil([&]()
            {
                return service.FindCachedMetadata(currentItem) != nullptr;
            }, 5000), "Metadata cancellation scenario did not cache the latest session item");
            PumpMessagesFor(100);
            Expect(service.FindCachedMetadata(cancelledItem) == nullptr,
                   "Metadata cancellation allowed an in-flight stale result to repopulate the cache");
        }

        {
            struct BlockingState
            {
                std::mutex mutex;
                std::condition_variable started;
                std::condition_variable released;
                bool extractionStarted{};
                bool allowCompletion{};
            } invalidationState;

            const auto staleItem = MakeMetadataItem(L"watched.jpg", L"C:\\Metadata\\watched.jpg", 31);
            const auto refreshedItem = MakeMetadataItem(L"watched.jpg", L"C:\\Metadata\\watched.jpg", 32);

            hyperbrowse::services::ImageMetadataService service(
                1,
                4,
                hyperbrowse::util::ResourceProfile::Balanced,
                [&](const hyperbrowse::browser::BrowserItem& item, std::wstring*)
                {
                    if (item.modifiedTimestampUtc == staleItem.modifiedTimestampUtc)
                    {
                        std::unique_lock lock(invalidationState.mutex);
                        invalidationState.extractionStarted = true;
                        invalidationState.started.notify_all();
                        invalidationState.released.wait(lock, [&]()
                        {
                            return invalidationState.allowCompletion;
                        });
                    }

                    return MakeMetadata(item.fileName + L"-" + std::to_wstring(item.modifiedTimestampUtc));
                });

            service.Schedule(55, {0, staleItem, 0});
            Expect(PumpMessagesUntil([&]()
            {
                std::scoped_lock lock(invalidationState.mutex);
                return invalidationState.extractionStarted;
            }, 1000), "Metadata invalidation scenario never started the blocked extraction");

            service.InvalidateFilePaths({staleItem.filePath});
            service.Schedule(55, {1, refreshedItem, 0});

            {
                std::scoped_lock lock(invalidationState.mutex);
                invalidationState.allowCompletion = true;
            }
            invalidationState.released.notify_all();

            Expect(PumpMessagesUntil([&]()
            {
                return service.FindCachedMetadata(refreshedItem) != nullptr;
            }, 5000), "Metadata invalidation scenario did not cache the refreshed file metadata");
            PumpMessagesFor(100);

            const auto refreshedMetadata = service.FindCachedMetadata(refreshedItem);
            Expect(refreshedMetadata != nullptr && refreshedMetadata->cameraModel == L"watched.jpg-32",
                   "Metadata invalidation scenario cached the wrong refreshed payload");
            Expect(service.FindCachedMetadata(staleItem) == nullptr,
                   "Metadata invalidation allowed an in-flight stale result to repopulate the cache");
        }
    }

    void RunSwarmUiMetadataExtractionScenario()
    {
        TempFolder root(L"HyperBrowsePrompt9SwarmMetadata");
        const fs::path pngPath = root.Root() / L"swarm-ui.png";
        const std::string parameters =
            "{ sui_image_params : { prompt : cinematic portrait, volumetric lighting, sharp focus, negativeprompt : low quality, deformed hands, text, model : swarm-model-xl, seed : 12345, steps : 30, cfgscale : 7, swarm_version : 0.9.1.1, date : 2026-05-16, generation_time : 1.23 seconds } }";
        WriteTestImage(pngPath,
                       TestImageFormat::Png,
                       96,
                       48,
                       1,
                       L"parameters",
                       parameters);

        hyperbrowse::browser::BrowserItem item;
        item.fileName = pngPath.filename().wstring();
        item.filePath = pngPath.wstring();
        item.fileType = L"PNG";
        item.modifiedTimestampUtc = 77;

        std::wstring errorMessage;
        const auto metadata = hyperbrowse::services::ExtractImageMetadata(item, &errorMessage);
        Expect(metadata != nullptr, "SwarmUI PNG metadata extraction returned a null result");

        const auto findProperty = [&](std::wstring_view canonicalName)
            -> const hyperbrowse::services::MetadataPropertyEntry*
        {
            const auto match = std::find_if(metadata->properties.begin(),
                                            metadata->properties.end(),
                                            [&](const hyperbrowse::services::MetadataPropertyEntry& property)
                                            {
                                                return property.canonicalName == canonicalName;
                                            });
            return match != metadata->properties.end() ? &(*match) : nullptr;
        };

        const auto* prompt = findProperty(L"SwarmUI.prompt");
        Expect(prompt != nullptr
                   && prompt->value == L"cinematic portrait, volumetric lighting, sharp focus",
               "SwarmUI prompt metadata was not extracted from the PNG parameters chunk");

        const auto* negativePrompt = findProperty(L"SwarmUI.negativeprompt");
        Expect(negativePrompt != nullptr
                   && negativePrompt->value == L"low quality, deformed hands, text",
               "SwarmUI negative prompt metadata was not extracted from the PNG parameters chunk");

        const auto* swarmVersion = findProperty(L"SwarmUI.swarm_version");
        Expect(swarmVersion != nullptr && swarmVersion->value == L"0.9.1.1",
               "SwarmUI version metadata was not extracted from the PNG parameters chunk");

        const std::wstring expanded = hyperbrowse::services::FormatImageInfoExpanded(*metadata);
        Expect(expanded.find(L"Prompt: cinematic portrait, volumetric lighting, sharp focus") != std::wstring::npos,
               "Expanded image information did not surface the extracted SwarmUI prompt");
        Expect(expanded.find(L"Negative prompt: low quality, deformed hands, text") != std::wstring::npos,
               "Expanded image information did not surface the extracted SwarmUI negative prompt");
         Expect(hyperbrowse::services::ExtractImagePrompt(*metadata)
                 == L"cinematic portrait, volumetric lighting, sharp focus",
             "Image prompt extraction returned more than the positive prompt");

         hyperbrowse::services::ImageMetadata jsonMetadata;
         jsonMetadata.properties.push_back({
             L"PNG.Text.parameters",
             L"Parameters",
             L"{\"prompt\":\"a quiet forest\",\"steps\":30,\"sampler\":\"Euler\"}"});
         const std::wstring formattedJson = hyperbrowse::services::FormatImageInfoExpanded(jsonMetadata);
         Expect(formattedJson.find(L"{\r\n  \"prompt\": \"a quiet forest\",") != std::wstring::npos,
             "JSON metadata was not formatted as an indented object");
         Expect(hyperbrowse::services::ExtractImagePrompt(jsonMetadata) == L"a quiet forest",
             "JSON metadata prompt extraction did not return only the prompt value");

        hyperbrowse::services::ImageMetadata comfyPromptMetadata;
        comfyPromptMetadata.properties.push_back({
            L"PNG.Text.prompt",
            L"Prompt",
            L"{\"prompt\":\"a mountain observatory at dawn\",\"model\":\"example-model\",\"steps\":30}"});
        Expect(hyperbrowse::services::ExtractImagePrompt(comfyPromptMetadata)
                   == L"a mountain observatory at dawn",
               "JSON-valued prompt metadata returned the complete ComfyUI chunk");

        const std::string quotedParameters =
            R"({"prompt":"photo of a refinery at sunset\nwith dramatic smoke","model":"Flux Diffusion/pixelwave_flux1_dev_Q6_K_03.gguf","steps":50,"negativeprompt":""})";
        WriteTestImage(pngPath,
                       TestImageFormat::Png,
                       96,
                       48,
                       1,
                       L"parameters",
                       quotedParameters);
        const auto quotedMetadata = hyperbrowse::services::ExtractImageMetadata(item, &errorMessage);
        Expect(quotedMetadata != nullptr, "Quoted SwarmUI metadata extraction returned a null result");
        const auto quotedPrompt = std::find_if(quotedMetadata->properties.begin(),
                                               quotedMetadata->properties.end(),
                                               [](const hyperbrowse::services::MetadataPropertyEntry& property)
                                               {
                                                   return property.canonicalName == L"SwarmUI.prompt";
                                               });
        Expect(quotedPrompt != quotedMetadata->properties.end()
                   && quotedPrompt->value == L"photo of a refinery at sunset\nwith dramatic smoke",
               "Quoted SwarmUI metadata included following JSON fields in the prompt");
        Expect(hyperbrowse::services::ExtractImagePrompt(*quotedMetadata)
                   == L"photo of a refinery at sunset\nwith dramatic smoke",
               "Quoted SwarmUI prompt extraction returned following JSON fields");

         hyperbrowse::services::ImageMetadata negativeOnlyMetadata;
         negativeOnlyMetadata.properties.push_back({
             L"PNG.Text.parameters",
             L"Parameters",
             L"Negative prompt: low quality"});
         Expect(hyperbrowse::services::ExtractImagePrompt(negativeOnlyMetadata).empty(),
             "Negative prompt metadata was incorrectly exposed as the image prompt");
    }

    void RunRawDecoderScenario()
    {
        const fs::path fixtureRoot = TestSourceDirectory() / L"fixtures" / L"raw";
        const fs::path nefPath = fixtureRoot / L"RAW_NIKON_D1.NEF";
        const fs::path nrwPath = fixtureRoot / L"RAW_NIKON_P7000.NRW";

        Expect(fs::exists(nefPath), "The NEF fixture is missing from tests/fixtures/raw");
        Expect(fs::exists(nrwPath), "The NRW fixture is missing from tests/fixtures/raw");

        const hyperbrowse::browser::BrowserItem nefItem = hyperbrowse::browser::BuildBrowserItemFromPath(nefPath);
        const hyperbrowse::browser::BrowserItem nrwItem = hyperbrowse::browser::BuildBrowserItemFromPath(nrwPath);

        Expect(hyperbrowse::decode::CanDecodeThumbnail(nefItem), "NEF fixture should be thumbnail-decodable");
        Expect(hyperbrowse::decode::CanDecodeFullImage(nefItem), "NEF fixture should be full-image decodable");
        Expect(hyperbrowse::decode::CanDecodeThumbnail(nrwItem), "NRW fixture should be thumbnail-decodable");
        Expect(hyperbrowse::decode::CanDecodeFullImage(nrwItem), "NRW fixture should be full-image decodable");

        std::wstring errorMessage;
        const auto nefThumbnail = hyperbrowse::decode::DecodeThumbnail(MakeCacheKey(nefPath, nefItem.modifiedTimestampUtc), &errorMessage);
        Expect(nefThumbnail != nullptr, std::string("LibRaw failed to decode the NEF thumbnail fixture: ") + Utf8FromWide(errorMessage));
        Expect(nefThumbnail->SourceWidth() > 0 && nefThumbnail->SourceHeight() > 0,
               "LibRaw did not surface NEF thumbnail source dimensions");

        errorMessage.clear();
        const auto nrwThumbnail = hyperbrowse::decode::DecodeThumbnail(MakeCacheKey(nrwPath, nrwItem.modifiedTimestampUtc), &errorMessage);
        Expect(nrwThumbnail != nullptr, std::string("LibRaw failed to decode the NRW thumbnail fixture: ") + Utf8FromWide(errorMessage));
        Expect(nrwThumbnail->SourceWidth() > 0 && nrwThumbnail->SourceHeight() > 0,
               "LibRaw did not surface NRW thumbnail source dimensions");

        errorMessage.clear();
        const auto nefFullImage = hyperbrowse::decode::DecodeFullImage(nefItem, &errorMessage);
        Expect(nefFullImage != nullptr, std::string("LibRaw failed to decode the NEF full-image fixture: ") + Utf8FromWide(errorMessage));
        Expect(nefFullImage->SourceWidth() > 0 && nefFullImage->SourceHeight() > 0,
               "LibRaw did not surface NEF full-image source dimensions");

        errorMessage.clear();
        const auto nrwFullImage = hyperbrowse::decode::DecodeFullImage(nrwItem, &errorMessage);
        Expect(nrwFullImage != nullptr, std::string("LibRaw failed to decode the NRW full-image fixture: ") + Utf8FromWide(errorMessage));
        Expect(nrwFullImage->SourceWidth() > 0 && nrwFullImage->SourceHeight() > 0,
               "LibRaw did not surface NRW full-image source dimensions");
        }
}
