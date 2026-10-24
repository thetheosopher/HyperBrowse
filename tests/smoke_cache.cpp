#include "smoke_support.h"
#include "smoke_scenarios.h"

namespace hyperbrowse::tests::smoke
{

    void RunThumbnailCacheNormalizationScenario()
    {
        TempFolder root(L"HyperBrowseThumbnailCacheNormalization");
        const fs::path imagePath = root.Root() / L"MixedCase.png";
        WriteTestImage(imagePath, TestImageFormat::Png, 96, 48);

        hyperbrowse::decode::WicThumbnailDecoder decoder;
        const auto insertedKey = MakeCacheKey(imagePath, 17);
        const auto thumbnail = decoder.Decode(insertedKey);
        Expect(thumbnail != nullptr, "Failed to create the thumbnail used for cache normalization testing");

        hyperbrowse::cache::ThumbnailCache cache(4ULL * 1024ULL * 1024ULL);
        cache.Insert(insertedKey, thumbnail);

        auto lookupKey = insertedKey;
        lookupKey.filePath = imagePath.wstring();
        std::replace(lookupKey.filePath.begin(), lookupKey.filePath.end(), L'\\', L'/');
        std::transform(lookupKey.filePath.begin(), lookupKey.filePath.end(), lookupKey.filePath.begin(), [](wchar_t character)
        {
            return static_cast<wchar_t>(towupper(character));
        });

        Expect(cache.Find(lookupKey) != nullptr, "Thumbnail cache lookup did not normalize slash and case differences");
         const auto statisticsAfterHit = cache.GetStatistics();
         Expect(statisticsAfterHit.hitCount == 1 && statisticsAfterHit.missCount == 0,
             "Thumbnail cache did not record the normalized lookup hit");

        cache.InvalidateFilePaths({lookupKey.filePath});
        Expect(cache.Find(insertedKey) == nullptr, "Thumbnail cache invalidation did not normalize the supplied file path");
         const auto statisticsAfterMiss = cache.GetStatistics();
         Expect(statisticsAfterMiss.hitCount == 1 && statisticsAfterMiss.missCount == 1,
             "Thumbnail cache did not record the post-invalidation miss");

         cache.Insert(insertedKey, thumbnail);
         cache.SetCapacityBytes(1);
         Expect(cache.GetStatistics().evictionCount == 1,
             "Thumbnail cache did not record capacity eviction");
    }

    void RunDiskThumbnailCacheCorruptionScenario()
    {
        TempFolder root(L"HyperBrowseDiskThumbnailCacheCorruption");
        const fs::path imagePath = root.Root() / L"sample.png";
        const fs::path cacheRoot = root.Root() / L"cache";
        fs::create_directories(cacheRoot);
        WriteTestImage(imagePath, TestImageFormat::Png, 32, 16);

        hyperbrowse::decode::WicThumbnailDecoder decoder;
        const auto key = MakeCacheKey(imagePath, 29);
        const auto thumbnail = decoder.Decode(key);
        Expect(thumbnail != nullptr, "Failed to create the thumbnail used for persistent-cache testing");

        hyperbrowse::cache::DiskThumbnailCache cache(4ULL * 1024ULL * 1024ULL, cacheRoot.wstring());
        const auto diagnosticsCounterValue = [](std::wstring_view name)
        {
            const hyperbrowse::util::DiagnosticsSnapshot snapshot = hyperbrowse::util::CaptureDiagnosticsSnapshot();
            for (const hyperbrowse::util::DiagnosticCounterRow& row : snapshot.counters)
            {
                if (row.name == name)
                {
                    return row.value;
                }
            }
            return std::uint64_t{};
        };
        const std::uint64_t invalidEntryCountBefore = diagnosticsCounterValue(L"persistent_cache.invalid_entries");
        cache.Store(key, thumbnail);
        Expect(cache.Compact(), "Persistent thumbnail cache did not compact its initial journal");
         std::error_code formatVersionError;
         Expect(fs::is_regular_file(cacheRoot / L"format.version", formatVersionError) && !formatVersionError,
             "Persistent thumbnail cache did not write its format version");
         {
             std::ifstream formatStream(cacheRoot / L"format.version", std::ios::binary);
             const std::string formatVersion((std::istreambuf_iterator<char>(formatStream)),
                               std::istreambuf_iterator<char>());
             Expect(formatVersion == "2\n",
                 "Persistent thumbnail cache wrote an unexpected format version");
         }
         std::wstring indexLineBeforeHit;
         {
             std::wifstream indexStream(cacheRoot / L"index.tsv");
             Expect(static_cast<bool>(std::getline(indexStream, indexLineBeforeHit)),
                 "Persistent thumbnail cache did not write an index row");
         }
        Expect(cache.TryLoad(key) != nullptr, "Persistent thumbnail cache did not round-trip a valid entry");
         std::wstring indexLineAfterHit;
         {
             std::wifstream indexStream(cacheRoot / L"index.tsv");
             Expect(static_cast<bool>(std::getline(indexStream, indexLineAfterHit)),
                 "Persistent thumbnail cache lost its index row after a cache hit");
         }
         Expect(indexLineBeforeHit == indexLineAfterHit,
             "Persistent thumbnail cache rewrote the index on a single cache hit");
         for (int hit = 1; hit < 64; ++hit)
         {
             Expect(cache.TryLoad(key) != nullptr,
                 "Persistent thumbnail cache failed during bounded access persistence testing");
         }
         std::wstring journalLineAfterBatch;
         bool accessMetadataPersisted = false;
         for (int attempt = 0; attempt < 100 && !accessMetadataPersisted; ++attempt)
         {
             journalLineAfterBatch.clear();
             std::wifstream journalStream(cacheRoot / L"index.journal.tsv");
             while (std::getline(journalStream, journalLineAfterBatch))
             {
                 if (journalLineAfterBatch.starts_with(L"T\t"))
                 {
                     accessMetadataPersisted = true;
                     break;
                 }
             }
             if (!accessMetadataPersisted)
             {
                 Sleep(10);
             }
         }
         Expect(accessMetadataPersisted,
             "Persistent thumbnail cache did not append batched access metadata to its journal");

        const fs::path unicodeCacheRoot = root.Root() / L"unicode-cache";
        const fs::path unicodeFolder = root.Root() / L"\xD14C\xC2A4\xD2B8";
        const fs::path unicodeImagePath = unicodeFolder / L"\xC0AC\xC9C4.jpg";
        const fs::path followingImagePath = root.Root() / L"following.png";
        fs::create_directories(unicodeFolder);
        fs::create_directories(unicodeCacheRoot);
        WriteTestImage(unicodeImagePath, TestImageFormat::Png, 40, 20);
        WriteTestImage(followingImagePath, TestImageFormat::Png, 24, 12);

        const auto unicodeKey = MakeCacheKey(unicodeImagePath, 31);
        const auto followingKey = MakeCacheKey(followingImagePath, 37);
        const auto unicodeThumbnail = decoder.Decode(unicodeKey);
        const auto followingThumbnail = decoder.Decode(followingKey);
        Expect(unicodeThumbnail != nullptr && followingThumbnail != nullptr,
               "Failed to create thumbnails used for Unicode persistent-cache testing");

        std::string expectedSerializedUnicodePath = Utf8FromWide(
            hyperbrowse::util::NormalizePathForComparison(unicodeKey.filePath));
        for (std::size_t position = 0; position < expectedSerializedUnicodePath.size(); ++position)
        {
            if (expectedSerializedUnicodePath[position] == '\\')
            {
                expectedSerializedUnicodePath.insert(position, 1, '\\');
                ++position;
            }
        }
        {
            hyperbrowse::cache::DiskThumbnailCache unicodeCache(4ULL * 1024ULL * 1024ULL, unicodeCacheRoot.wstring());
            unicodeCache.Store(unicodeKey, unicodeThumbnail);
            unicodeCache.Store(followingKey, followingThumbnail);

            std::ifstream journalStream(unicodeCacheRoot / L"index.journal.tsv", std::ios::binary);
            const std::string journalBytes((std::istreambuf_iterator<char>(journalStream)),
                                            std::istreambuf_iterator<char>());
                 Expect(journalBytes.find(expectedSerializedUnicodePath) != std::string::npos,
                     "Persistent thumbnail cache did not write the Unicode path to the journal");
                 Expect(std::count(journalBytes.begin(), journalBytes.end(), '\n') == 2,
                     "Persistent thumbnail cache did not terminate adjacent journal rows");
            Expect(unicodeCache.Compact(),
                   "Persistent thumbnail cache failed to compact rows containing Unicode paths");

            std::ifstream indexStream(unicodeCacheRoot / L"index.tsv", std::ios::binary);
            const std::string indexBytes((std::istreambuf_iterator<char>(indexStream)),
                                         std::istreambuf_iterator<char>());
                     Expect(indexBytes.find(expectedSerializedUnicodePath) != std::string::npos,
                     "Persistent thumbnail cache did not write the Unicode path to the index");
                 Expect(std::count(indexBytes.begin(), indexBytes.end(), '\n') == 2,
                     "Persistent thumbnail cache did not terminate adjacent index rows");
        }
        {
            hyperbrowse::cache::DiskThumbnailCache reloadedUnicodeCache(
                4ULL * 1024ULL * 1024ULL,
                unicodeCacheRoot.wstring());
            Expect(reloadedUnicodeCache.TryLoad(unicodeKey) != nullptr
                       && reloadedUnicodeCache.TryLoad(followingKey) != nullptr,
                   "Persistent thumbnail cache did not reload adjacent Unicode and ASCII rows");
            std::error_code journalError;
            Expect(fs::file_size(unicodeCacheRoot / L"index.journal.tsv", journalError) == 0 && !journalError,
                   "Persistent thumbnail cache did not truncate the journal after Unicode compaction");
        }

        const fs::path journalReplayRoot = root.Root() / L"journal-replay-cache";
           fs::create_directories(journalReplayRoot);
        {
            hyperbrowse::cache::DiskThumbnailCache journalCache(4ULL * 1024ULL * 1024ULL, journalReplayRoot.wstring());
            journalCache.Store(key, thumbnail);
            std::wofstream journalStream(journalReplayRoot / L"index.journal.tsv", std::ios::app);
            journalStream << L"P\t";
        }
        {
            hyperbrowse::cache::DiskThumbnailCache replayedCache(4ULL * 1024ULL * 1024ULL, journalReplayRoot.wstring());
            Expect(replayedCache.TryLoad(key) != nullptr,
                "Persistent thumbnail cache did not replay a journal entry after restart");
            replayedCache.InvalidateFilePaths({key.filePath});
            Expect(replayedCache.Compact(),
                "Persistent thumbnail cache did not compact after replaying an invalidation");
        }
        {
            hyperbrowse::cache::DiskThumbnailCache invalidatedCache(4ULL * 1024ULL * 1024ULL, journalReplayRoot.wstring());
            Expect(invalidatedCache.TryLoad(key) == nullptr,
                "Persistent thumbnail cache replayed an invalidated entry");
            std::error_code journalError;
            Expect(fs::file_size(journalReplayRoot / L"index.journal.tsv", journalError) == 0 && !journalError,
                "Persistent thumbnail cache did not truncate its journal after compaction");
        }

#pragma pack(push, 1)
        struct TestDiskThumbnailHeader
        {
            char magic[8];
            std::uint32_t width{};
            std::uint32_t height{};
            std::uint32_t sourceWidth{};
            std::uint32_t sourceHeight{};
            std::uint64_t pixelBytes{};
        };
#pragma pack(pop)

        const auto corruptEntry = [&](TestDiskThumbnailHeader header, std::size_t payloadBytes, const char* message)
        {
            cache.Store(key, thumbnail);
            fs::path cacheFile;
            for (const fs::directory_entry& entry : fs::recursive_directory_iterator(cacheRoot))
            {
                if (entry.is_regular_file() && entry.path().extension() == L".bin")
                {
                    cacheFile = entry.path();
                    break;
                }
            }
            Expect(!cacheFile.empty(), "Persistent thumbnail cache did not create an entry file");

            std::ofstream stream(cacheFile, std::ios::binary | std::ios::trunc);
            stream.write(reinterpret_cast<const char*>(&header), sizeof(header));
            std::vector<char> payload(payloadBytes, '\0');
            stream.write(payload.data(), static_cast<std::streamsize>(payload.size()));
            stream.close();

            Expect(cache.TryLoad(key) == nullptr, message);
            Expect(!fs::exists(cacheFile), "Persistent thumbnail cache did not remove the corrupt entry");
        };

        TestDiskThumbnailHeader validHeader{};
        std::memcpy(validHeader.magic, "HBTHMB01", sizeof(validHeader.magic));
        validHeader.width = 32;
        validHeader.height = 16;
        validHeader.sourceWidth = 32;
        validHeader.sourceHeight = 16;
        validHeader.pixelBytes = 32ULL * 16ULL * 4ULL;

        TestDiskThumbnailHeader wrongMagic = validHeader;
        wrongMagic.magic[0] = 'X';
        corruptEntry(wrongMagic, static_cast<std::size_t>(wrongMagic.pixelBytes), "Persistent thumbnail cache accepted an invalid magic value");

        TestDiskThumbnailHeader oversizedDimensions = validHeader;
        oversizedDimensions.width = UINT32_MAX;
        corruptEntry(oversizedDimensions, 0, "Persistent thumbnail cache accepted oversized dimensions");

        TestDiskThumbnailHeader missingSourceDimensions = validHeader;
        missingSourceDimensions.sourceWidth = 0;
        corruptEntry(missingSourceDimensions,
                     static_cast<std::size_t>(missingSourceDimensions.pixelBytes),
                     "Persistent thumbnail cache accepted missing source dimensions");

        TestDiskThumbnailHeader wrongByteCount = validHeader;
        wrongByteCount.pixelBytes += 4;
        corruptEntry(wrongByteCount, static_cast<std::size_t>(wrongByteCount.pixelBytes), "Persistent thumbnail cache accepted a mismatched pixel byte count");

        corruptEntry(validHeader, static_cast<std::size_t>(validHeader.pixelBytes - 1), "Persistent thumbnail cache accepted a truncated payload");

        cache.Store(key, thumbnail);
        fs::path validCacheFile;
        for (const fs::directory_entry& entry : fs::recursive_directory_iterator(cacheRoot))
        {
            if (entry.is_regular_file() && entry.path().extension() == L".bin")
            {
                validCacheFile = entry.path();
                break;
            }
        }
        Expect(!validCacheFile.empty(), "Persistent thumbnail cache did not recreate a valid entry");
        std::wstring validIndexLine;
        {
            std::wifstream indexStream(cacheRoot / L"index.tsv");
            Expect(static_cast<bool>(std::getline(indexStream, validIndexLine)) && !validIndexLine.empty(),
                   "Persistent thumbnail cache did not serialize a valid index row");
        }
        {
            std::wofstream indexStream(cacheRoot / L"index.tsv", std::ios::trunc);
            indexStream << validIndexLine << L'\n'
                        << L"C:\\invalid\\overflow.jpg\t18446744073709551616\t29\t29\t0123456789abcdef.thumb\t100\t1\n"
                        << L"C:\\invalid\\text.jpg\tnot-a-number\t29\t29\t0123456789abcdef.thumb\t100\t1\n"
                        << L"C:\\invalid\\zero-width.jpg\t1\t0\t29\t0123456789abcdef.thumb\t100\t1\n"
                        << L"C:\\invalid\\unsafe-name.jpg\t1\t29\t29\t..\\outside.thumb\t100\t1\n";
        }
        {
            std::wofstream journalStream(cacheRoot / L"index.journal.tsv", std::ios::trunc);
        }

         {
             hyperbrowse::cache::DiskThumbnailCache reloadedCache(4ULL * 1024ULL * 1024ULL, cacheRoot.wstring());
             const auto statistics = reloadedCache.QueryStatistics();
             Expect(statistics.indexedEntryCount == 1,
                 "Persistent thumbnail cache did not skip malformed index rows");
             Expect(reloadedCache.TryLoad(key) != nullptr,
                 "Persistent thumbnail cache discarded a valid row beside malformed index rows");
         }
                Expect(diagnosticsCounterValue(L"persistent_cache.invalid_entries") > invalidEntryCountBefore,
                             "Persistent thumbnail cache did not count invalid entries");

        const fs::path outsideCacheFile = root.Root() / L"outside.thumb";
        {
            std::ofstream outsideStream(outsideCacheFile, std::ios::binary | std::ios::trunc);
            outsideStream << "keep";
        }

        {
            std::wofstream indexStream(cacheRoot / L"index.tsv", std::ios::trunc);
            indexStream << hyperbrowse::util::NormalizePathForComparison(key.filePath)
                        << L'\t' << key.modifiedTimestampUtc
                        << L'\t' << key.targetWidth
                        << L'\t' << key.targetHeight
                        << L"\t..\\outside.thumb\t"
                        << (sizeof(TestDiskThumbnailHeader) + key.targetWidth * key.targetHeight * 4)
                        << L"\t1\n";
        }
        {
            std::wofstream journalStream(cacheRoot / L"index.journal.tsv", std::ios::trunc);
        }

         hyperbrowse::cache::DiskThumbnailCache reloadedCache(4ULL * 1024ULL * 1024ULL, cacheRoot.wstring());
         Expect(reloadedCache.TryLoad(key) == nullptr,
             "Persistent thumbnail cache accepted an unsafe index file name");
        Expect(fs::exists(outsideCacheFile),
               "Persistent thumbnail cache cleanup escaped its cache directory");

         const fs::path legacyCacheRoot = root.Root() / L"legacy-cache";
         fs::create_directories(legacyCacheRoot);
         const fs::path legacyCacheFile = legacyCacheRoot / L"0123456789abcdef.thumb";
         std::error_code copyError;
         fs::copy_file(validCacheFile, legacyCacheFile, fs::copy_options::overwrite_existing, copyError);
         Expect(!copyError, "Failed to create the legacy persistent-cache fixture");
         {
            std::wstring legacyIndexLine = validIndexLine;
            std::size_t filenameStart = 0;
            for (int field = 0; field < 4; ++field)
            {
                filenameStart = legacyIndexLine.find(L'\t', filenameStart);
                Expect(filenameStart != std::wstring::npos,
                       "Failed to locate the legacy index filename field");
                ++filenameStart;
            }
            const std::size_t filenameEnd = legacyIndexLine.find(L'\t', filenameStart);
            Expect(filenameEnd != std::wstring::npos,
                   "Failed to locate the legacy index filename terminator");
            legacyIndexLine.replace(filenameStart,
                                    filenameEnd - filenameStart,
                                    L"0123456789abcdef.thumb");
             std::wofstream indexStream(legacyCacheRoot / L"index.tsv", std::ios::trunc);
            indexStream << legacyIndexLine << L'\n';
         }
         {
             hyperbrowse::cache::DiskThumbnailCache migratedCache(4ULL * 1024ULL * 1024ULL,
                                           legacyCacheRoot.wstring());
             Expect(migratedCache.TryLoad(key) != nullptr,
                 "Persistent thumbnail cache did not load a legacy flat entry during migration");
         }
        Expect(!fs::exists(legacyCacheFile),
             "Persistent thumbnail cache did not remove the migrated legacy entry");
         Expect(fs::is_regular_file(legacyCacheRoot / L"format.version"),
             "Persistent thumbnail cache did not mark the migrated layout version");
         std::size_t migratedEntryCount = 0;
         for (const fs::directory_entry& entry : fs::recursive_directory_iterator(legacyCacheRoot))
         {
             if (entry.is_regular_file() && entry.path().extension() == L".bin")
             {
              ++migratedEntryCount;
             }
         }
         Expect(migratedEntryCount == 1,
             "Persistent thumbnail cache did not create one sharded entry during migration");
         {
             hyperbrowse::cache::DiskThumbnailCache restartedMigratedCache(4ULL * 1024ULL * 1024ULL,
                                                 legacyCacheRoot.wstring());
             Expect(restartedMigratedCache.TryLoad(key) != nullptr,
                 "Persistent thumbnail cache did not reload a migrated entry after restart");
         }
    }

    void RunPersistentCacheCollisionAndLongPathScenario()
    {
        TempFolder root(L"HyperBrowsePersistentCachePathSafety");
        const fs::path sourceA = root.Root() / L"collision-a.png";
        const fs::path sourceB = root.Root() / L"collision-b.png";
        WriteTestImage(sourceA, TestImageFormat::Png, 32, 16);
        WriteTestImage(sourceB, TestImageFormat::Png, 32, 16);

        hyperbrowse::decode::WicThumbnailDecoder decoder;
        const auto keyA = MakeCacheKey(sourceA, 41);
        const auto keyB = MakeCacheKey(sourceB, 43);
        const auto thumbnail = decoder.Decode(keyB);
        Expect(thumbnail != nullptr, "Failed to create the thumbnail used for collision testing");

        const fs::path collisionCacheRoot = root.Root() / L"collision-cache";
        {
            hyperbrowse::cache::DiskThumbnailCache cache(4ULL * 1024ULL * 1024ULL,
                                                          collisionCacheRoot.wstring());
            cache.Store(keyB, thumbnail);
            Expect(cache.Compact(), "Failed to compact the collision fixture");
        }

        std::wstring collisionIndexLine;
        {
            std::wifstream indexStream(collisionCacheRoot / L"index.tsv");
            Expect(static_cast<bool>(std::getline(indexStream, collisionIndexLine)),
                   "Collision fixture did not produce an index row");
        }
        const std::size_t firstTab = collisionIndexLine.find(L'\t');
        Expect(firstTab != std::wstring::npos, "Collision fixture index row was malformed");
        collisionIndexLine.replace(0,
                                   firstTab,
                                   hyperbrowse::util::NormalizePathForComparison(keyA.filePath));
        {
            std::wofstream indexStream(collisionCacheRoot / L"index.tsv", std::ios::app);
            indexStream << collisionIndexLine << L'\n';
        }

        const auto counterValue = [](std::wstring_view name)
        {
            const auto snapshot = hyperbrowse::util::CaptureDiagnosticsSnapshot();
            for (const auto& row : snapshot.counters)
            {
                if (row.name == name)
                {
                    return row.value;
                }
            }
            return std::uint64_t{};
        };
        const std::uint64_t collisionCountBefore = counterValue(L"persistent_cache.hash_collisions");
        {
            hyperbrowse::cache::DiskThumbnailCache cache(4ULL * 1024ULL * 1024ULL,
                                                          collisionCacheRoot.wstring());
            Expect(cache.TryLoad(keyB) != nullptr,
                   "Collision fixture discarded the authoritative cache entry");
            cache.Store(keyB, thumbnail);
        }
        Expect(counterValue(L"persistent_cache.hash_collisions") > collisionCountBefore,
               "Persistent thumbnail cache did not reject a conflicting shard filename");

        std::wstring longFilePath = root.Root().wstring();
        for (int segment = 0; segment < 12; ++segment)
        {
            longFilePath.append(L"\\long_segment_");
            longFilePath.append(20, L'x');
        }
        longFilePath.append(L"\\photo.png");
        auto longPathKey = MakeCacheKey(root.Root() / L"long-path-placeholder.png", 47);
        longPathKey.filePath = longFilePath;

        const fs::path longPathCacheRoot = root.Root() / L"long-path-cache";
        {
            hyperbrowse::cache::DiskThumbnailCache cache(4ULL * 1024ULL * 1024ULL,
                                                          longPathCacheRoot.wstring());
            cache.Store(longPathKey, thumbnail);
        }
        {
            hyperbrowse::cache::DiskThumbnailCache restartedCache(4ULL * 1024ULL * 1024ULL,
                                                                   longPathCacheRoot.wstring());
            Expect(restartedCache.TryLoad(longPathKey) != nullptr,
                   "Persistent thumbnail cache did not round-trip a long normalized key");
        }
    }

        void RunRedactedDiagnosticsExportScenario()
        {
         TempFolder root(L"HyperBrowseRedactedDiagnostics");
         const fs::path outputPath = root.Root() / L"diagnostics.json";
         hyperbrowse::util::ResetDiagnostics();
         hyperbrowse::util::EnableStartupBenchmark();
         auto startupSnapshot = hyperbrowse::util::CaptureStartupDiagnosticsSnapshot();
         Expect(!startupSnapshot.processToFirstWindowVisibleMs.has_value()
                    && !startupSnapshot.firstWindowVisibleToFirstThumbnailPaintedMs.has_value(),
                "Unrecorded startup timings were not represented as unavailable");
         hyperbrowse::util::MarkStartupWindowVisible();
         startupSnapshot = hyperbrowse::util::CaptureStartupDiagnosticsSnapshot();
         Expect(startupSnapshot.processToFirstWindowVisibleMs.has_value()
                    && !startupSnapshot.firstWindowVisibleToFirstThumbnailPaintedMs.has_value(),
                "First-window timing availability was not tracked independently");
         hyperbrowse::util::MarkStartupFirstThumbnailPainted();
         startupSnapshot = hyperbrowse::util::CaptureStartupDiagnosticsSnapshot();
         Expect(startupSnapshot.processToFirstWindowVisibleMs.has_value()
                    && startupSnapshot.firstWindowVisibleToFirstThumbnailPaintedMs.has_value()
                    && *startupSnapshot.processToFirstWindowVisibleMs >= 0.0
                    && *startupSnapshot.firstWindowVisibleToFirstThumbnailPaintedMs >= 0.0,
                "Recorded startup spans were not exposed with non-negative values");
         hyperbrowse::util::RecordTiming(L"diagnostics.test.timing", 1.0);
         hyperbrowse::util::IncrementCounter(L"diagnostics.test.counter", 3);

         Expect(hyperbrowse::util::WriteRedactedDiagnosticsSnapshot(outputPath.wstring()),
             "Redacted diagnostics snapshot export failed");

         std::ifstream stream(outputPath, std::ios::binary);
         const std::string json((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
         Expect(json.find("\"redacted\": true") != std::string::npos,
             "Diagnostics export did not declare its redacted form");
         Expect(json.find("\"pathsIncluded\": false") != std::string::npos,
             "Diagnostics export did not declare that paths were omitted");
         Expect(json.find("diagnostics.test.counter") != std::string::npos,
             "Diagnostics export omitted a recorded counter");
         Expect(json.find(Utf8FromWide(root.Root().wstring())) == std::string::npos,
             "Diagnostics export leaked the output path");
         Expect(json.find("folderScope") == std::string::npos && json.find("jpegPath") == std::string::npos,
             "Diagnostics export included UI path fields");
         hyperbrowse::util::ResetDiagnostics();
        }
}
