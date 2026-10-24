#include "smoke_support.h"
#include "smoke_scenarios.h"

namespace hyperbrowse::tests::smoke
{

    void RunEnumerationScenario(HWND hwnd, TestWindowState* state)
    {
        hyperbrowse::services::FolderEnumerationService service;

        TempFolder root(L"HyperBrowsePrompt3Root");
        root.WriteFile(L"one.jpg", 10);
        root.WriteFile(L"two.png", 20);
        root.WriteFile(L"six.cr2", 60);
        root.WriteFile(L"seven.cr3", 70);
        root.WriteFile(L"eight.arw", 80);
        root.WriteFile(L"nine.dng", 90);
        root.WriteFile(L"ten.raf", 100);
        root.WriteFile(L"eleven.rw2", 110);
        root.WriteFile(L"five.NRW", 50);
        root.WriteFile(L"ignore.txt", 5);
        root.WriteFile(L"nested\\three.gif", 30);
        root.WriteFile(L"nested\\four.nef", 40);

        ResetEnumerationResult(state);
        state->expectedRequestId = service.EnumerateFolderAsync(hwnd, root.Root().wstring(), false);
        Expect(PumpMessagesUntil([&]() { return state->enumerationResult.completed || state->enumerationResult.failed; }, 5000),
               "Non-recursive enumeration timed out or failed");
         Expect(state->enumerationResult.totalCount == 9, "Non-recursive enumeration returned the wrong supported-file count");
         Expect(state->enumerationResult.totalBytes == 590, "Non-recursive enumeration returned the wrong byte total");
         Expect(state->enumerationResult.items.size() == 9, "Non-recursive enumeration returned the wrong batch item count");
        Expect(state->enumerationResult.items.front().placeholderWidth == 256, "Placeholder width was not collected");
        Expect(state->enumerationResult.items.front().placeholderHeight == 256, "Placeholder height was not collected");

         ResetEnumerationResult(state);
         state->expectedRequestId = service.EnumerateFolderAsync(hwnd, root.Root().wstring(), false, true);
         Expect(PumpMessagesUntil([&]() { return state->enumerationResult.completed || state->enumerationResult.failed; }, 5000),
             "Subfolder enumeration timed out or failed");
         Expect(state->enumerationResult.totalCount == 10, "Subfolder enumeration returned the wrong item count");
         Expect(state->enumerationResult.totalBytes == 590, "Subfolder enumeration changed the file byte total");
         const auto nestedFolder = std::find_if(state->enumerationResult.items.begin(),
                                 state->enumerationResult.items.end(),
                                 [](const hyperbrowse::browser::BrowserItem& item)
                                 {
                                  return item.fileName == L"nested";
                                 });
         Expect(nestedFolder != state->enumerationResult.items.end() && nestedFolder->isDirectory,
             "Subfolder enumeration did not emit a directory item");

         TempFolder firstPage(L"HyperBrowseFirstPageEnumeration");
        for (int index = 0; index < 40; ++index)
         {
             firstPage.WriteFile(L"image_" + std::to_wstring(index) + L".jpg", 1);
         }
         ResetEnumerationResult(state);
         state->expectedRequestId = service.EnumerateFolderAsync(hwnd, firstPage.Root().wstring(), false);
         Expect(PumpMessagesUntil([&]() { return state->enumerationResult.completed || state->enumerationResult.failed; }, 5000),
             "First-page enumeration timed out or failed");
         Expect(state->enumerationResult.firstBatchSize == 16,
             "Folder enumeration did not publish the optimized first-page batch");
         Expect(state->enumerationResult.secondBatchSize == 16,
             "Folder enumeration did not publish the optimized second-page batch");
         Expect(state->enumerationResult.batchCount == 3,
             "Folder enumeration did not flush the remaining items after the priority batches");

        ResetEnumerationResult(state);
          state->expectedRequestId = service.EnumerateFolderAsync(hwnd, root.Root().wstring(), true, true);
        Expect(PumpMessagesUntil([&]() { return state->enumerationResult.completed || state->enumerationResult.failed; }, 5000),
               "Recursive enumeration timed out or failed");
           Expect(state->enumerationResult.totalCount == 12, "Recursive enumeration returned the wrong item count");
         Expect(state->enumerationResult.totalBytes == 660, "Recursive enumeration returned the wrong byte total");

        TempFolder slow(L"HyperBrowsePrompt3Slow");
        for (int index = 0; index < 400; ++index)
        {
            slow.WriteFile(L"bulk\\image_" + std::to_wstring(index) + L".jpg", 1);
        }

        TempFolder quick(L"HyperBrowsePrompt3Quick");
        quick.WriteFile(L"picked.png", 7);

        ResetEnumerationResult(state);
        service.EnumerateFolderAsync(hwnd, slow.Root().wstring(), false);
        state->expectedRequestId = service.EnumerateFolderAsync(hwnd, quick.Root().wstring(), false);
        Expect(PumpMessagesUntil([&]() { return state->enumerationResult.completed || state->enumerationResult.failed; }, 5000),
               "Cancellation scenario timed out or failed");
        Expect(state->enumerationResult.totalCount == 1, "Cancellation scenario did not surface the latest folder request");
        Expect(state->enumerationResult.items.size() == 1, "Cancellation scenario returned stale items from the superseded request");
        Expect(state->enumerationResult.items.front().fileName == L"picked.png", "Cancellation scenario returned the wrong final file");
        Expect(state->enumerationResult.items.front().fileType == L"PNG", "Enumeration did not capture the file type field");
        Expect(state->enumerationResult.items.front().modifiedTimestampUtc != 0, "Enumeration did not capture the modified timestamp field");
        Expect(PumpMessagesUntil([&]()
        {
            return service.ActiveTaskCount() == 0 && service.PendingTaskCount() == 0;
        }, 1000), "Folder enumeration service retained work after completion");
        Expect(service.RejectedTaskCount() == 0,
               "Folder enumeration service rejected work during the focused scenario");
        Expect(service.CancellationCount() >= 1,
               "Folder enumeration service did not record the superseded request");

        const std::uint64_t cancellationCountBeforeBurst = service.CancellationCount();
        for (int index = 0; index < 31; ++index)
        {
            service.EnumerateFolderAsync(hwnd, slow.Root().wstring(), true);
            Expect(service.ActiveTaskCount() <= 2 && service.PendingTaskCount() <= 2,
                   "Rapid folder navigation exceeded the bounded enumeration capacity");
        }
        ResetEnumerationResult(state);
        state->expectedRequestId = service.EnumerateFolderAsync(hwnd, quick.Root().wstring(), false);
        Expect(PumpMessagesUntil([&]() { return state->enumerationResult.completed || state->enumerationResult.failed; }, 5000),
               "Rapid folder navigation did not complete the latest request");
        Expect(!state->enumerationResult.failed && state->enumerationResult.totalCount == 1,
               "Rapid folder navigation surfaced a stale or failed request");
        Expect(state->enumerationResult.items.size() == 1
                   && state->enumerationResult.items.front().fileName == L"picked.png",
               "Rapid folder navigation did not preserve the latest folder result");
        Expect(service.CancellationCount() >= cancellationCountBeforeBurst + 31,
               "Rapid folder navigation did not advance the enumeration request epoch");
        Expect(service.PeakPendingTaskCount() <= 2,
               "Folder enumeration peak queue depth exceeded its configured bound");
        Expect(PumpMessagesUntil([&]()
        {
            return service.ActiveTaskCount() == 0 && service.PendingTaskCount() == 0;
        }, 1000), "Folder enumeration burst retained background work");
    }

        void RunFolderTreeEnumerationScenario(HWND hwnd, TestWindowState* state)
        {
         hyperbrowse::services::FolderTreeEnumerationService service;

         TempFolder root(L"HyperBrowseFolderTreeEnumeration");
         fs::create_directories(root.Root() / L"gamma");
         fs::create_directories(root.Root() / L"alpha");
         fs::create_directories(root.Root() / L"beta");
         fs::create_directories(root.Root() / L"beta" / L"nested");
         const fs::path hiddenFolder = root.Root() / L"hidden";
         fs::create_directories(hiddenFolder);
         Expect(SetFileAttributesW(hiddenFolder.c_str(), FILE_ATTRIBUTE_HIDDEN) != FALSE,
             "Folder-tree enumeration test could not mark the hidden directory");
         root.WriteFile(L"alpha\\image.jpg", 1);

         SHELLFLAGSTATE shellState{};
         SHGetSettings(&shellState, SSF_SHOWALLOBJECTS);
         const bool showHiddenFolders = shellState.fShowAllObjects != FALSE;

         ResetFolderTreeEnumerationResult(state);
         state->folderTreeEnumerationResult.expectedRequestId = service.EnumerateChildDirectoriesAsync(hwnd, root.Root().wstring());
         Expect(PumpMessagesUntil([&]()
         {
             return state->folderTreeEnumerationResult.completed || state->folderTreeEnumerationResult.failed;
         }, 5000), "Folder-tree enumeration timed out or failed");
         Expect(!state->folderTreeEnumerationResult.failed,
             std::string("Folder-tree enumeration failed: ") + Utf8FromWide(state->folderTreeEnumerationResult.errorMessage));
         Expect(state->folderTreeEnumerationResult.childFolders.size() == (showHiddenFolders ? 4 : 3),
             "Folder-tree enumeration returned the wrong number of child directories");
         Expect(fs::path(state->folderTreeEnumerationResult.childFolders[0].path).filename().wstring() == L"alpha",
             "Folder-tree enumeration did not sort child folders alphabetically");
         Expect(fs::path(state->folderTreeEnumerationResult.childFolders[1].path).filename().wstring() == L"beta",
             "Folder-tree enumeration did not preserve the expected alphabetical order");
         Expect(fs::path(state->folderTreeEnumerationResult.childFolders[2].path).filename().wstring() == L"gamma",
             "Folder-tree enumeration omitted the last child folder");
         if (showHiddenFolders)
         {
             Expect(state->folderTreeEnumerationResult.childFolders.size() > 3
                        && fs::path(state->folderTreeEnumerationResult.childFolders[3].path).filename().wstring() == L"hidden",
                 "Folder-tree enumeration omitted a visible hidden child folder");
         }
         ResetFolderTreeEnumerationResult(state);
         std::vector<std::wstring> childPresencePaths = {
             (root.Root() / L"alpha").wstring(),
             (root.Root() / L"beta").wstring(),
             (root.Root() / L"gamma").wstring(),
         };
         state->folderTreeEnumerationResult.expectedRequestId = service.QueryChildDirectoryPresenceAsync(
             hwnd,
             std::move(childPresencePaths));
         Expect(PumpMessagesUntil([&]()
         {
             return state->folderTreeEnumerationResult.completed || state->folderTreeEnumerationResult.failed;
         }, 5000), "Folder-tree child-presence query timed out or failed");
         Expect(!state->folderTreeEnumerationResult.failed,
             std::string("Folder-tree child-presence query failed: ")
                 + Utf8FromWide(state->folderTreeEnumerationResult.errorMessage));
         Expect(state->folderTreeEnumerationResult.childPresenceResults.size() == 3,
             "Folder-tree child-presence query returned the wrong number of results");
         Expect(!state->folderTreeEnumerationResult.childPresenceResults[0].hasChildren,
             "Folder-tree child-presence query incorrectly marked a leaf folder as expandable");
         Expect(state->folderTreeEnumerationResult.childPresenceResults[1].hasChildren,
             "Folder-tree child-presence query did not detect a nested child folder");
         Expect(!state->folderTreeEnumerationResult.childPresenceResults[2].hasChildren,
             "Folder-tree child-presence query incorrectly marked an empty folder as expandable");
         Expect(SetFileAttributesW(hiddenFolder.c_str(), FILE_ATTRIBUTE_NORMAL) != FALSE,
             "Folder-tree enumeration test could not restore the hidden directory attributes");

         {
             TempFolder supersededRoot(L"HyperBrowseFolderTreeSuperseded");
             TempFolder latestRoot(L"HyperBrowseFolderTreeLatest");
             fs::create_directories(latestRoot.Root() / L"final-child");
             hyperbrowse::services::FolderTreeEnumerationService supersessionService;
             std::vector<std::wstring> supersededPaths;
             supersededPaths.reserve(100000);
             for (int index = 0; index < 100000; ++index)
             {
                 supersededPaths.push_back(
                     (supersededRoot.Root() / (L"missing_" + std::to_wstring(index))).wstring());
             }

             ResetFolderTreeEnumerationResult(state);
             state->folderTreeEnumerationResult.expectedRequestId = supersessionService.QueryChildDirectoryPresenceAsync(
                 hwnd,
                 std::move(supersededPaths));
             Expect(PumpMessagesUntil([&]() { return supersessionService.ActiveTaskCount() > 0; }, 5000),
                 "Folder-tree supersession task did not start");
             supersessionService.CancelAll();
             ResetFolderTreeEnumerationResult(state);
             state->folderTreeEnumerationResult.expectedRequestId = supersessionService.EnumerateChildDirectoriesAsync(
                 hwnd,
                 latestRoot.Root().wstring());
             Expect(PumpMessagesUntil([&]()
             {
                 return state->folderTreeEnumerationResult.completed || state->folderTreeEnumerationResult.failed;
             }, 5000), "Folder-tree supersession scenario timed out or failed");
             Expect(!state->folderTreeEnumerationResult.failed,
                 "Folder-tree supersession scenario reported a failure");
             Expect(state->folderTreeEnumerationResult.childFolders.size() == 1
                        && fs::path(state->folderTreeEnumerationResult.childFolders.front().path).filename().wstring() == L"final-child",
                 "Folder-tree supersession scenario surfaced stale child folders");
            Expect(supersessionService.CancellationCount() >= 1,
                   "Folder-tree service did not record generation cancellation");
            Expect(PumpMessagesUntil([&]()
            {
                return supersessionService.ActiveTaskCount() == 0
                    && supersessionService.PendingTaskCount() == 0;
            }, 1000), "Folder-tree service retained work after supersession completion");

            const std::uint64_t cancellationCountBeforeBurst = supersessionService.CancellationCount();
            ResetFolderTreeEnumerationResult(state);
            for (int index = 0; index < 31; ++index)
            {
                supersessionService.CancelAll();
                supersessionService.EnumerateChildDirectoriesAsync(hwnd, latestRoot.Root().wstring());
                Expect(supersessionService.ActiveTaskCount() <= 2
                           && supersessionService.PendingTaskCount() <= 8,
                       "Rapid folder-tree navigation exceeded the bounded service capacity");
            }
            supersessionService.CancelAll();
            state->folderTreeEnumerationResult.expectedRequestId =
                supersessionService.EnumerateChildDirectoriesAsync(hwnd, latestRoot.Root().wstring());
            Expect(PumpMessagesUntil([&]()
            {
                return state->folderTreeEnumerationResult.completed || state->folderTreeEnumerationResult.failed;
            }, 5000), "Rapid folder-tree navigation did not complete the latest request");
            Expect(!state->folderTreeEnumerationResult.failed
                       && state->folderTreeEnumerationResult.childFolders.size() == 1,
                   "Rapid folder-tree navigation surfaced stale or failed child folders");
            Expect(supersessionService.CancellationCount() >= cancellationCountBeforeBurst + 32,
                   "Rapid folder-tree navigation did not advance the cancellation generation");
            Expect(supersessionService.PeakPendingTaskCount() <= 8,
                   "Folder-tree peak queue depth exceeded its configured bound");
            Expect(PumpMessagesUntil([&]()
            {
                return supersessionService.ActiveTaskCount() == 0
                    && supersessionService.PendingTaskCount() == 0;
            }, 1000), "Folder-tree burst retained background work");
         }
        Expect(PumpMessagesUntil([&]()
        {
            return service.ActiveTaskCount() == 0 && service.PendingTaskCount() == 0;
        }, 1000), "Folder-tree service retained work after completion");
        Expect(service.RejectedTaskCount() == 0,
               "Folder-tree service rejected work during the focused scenario");
        }

    void RunFolderWatchStartStopScenario(HWND hwnd)
    {
        TempFolder root(L"HyperBrowseFolderWatchStop");
        root.WriteFile(L"seed.jpg", 10);

        hyperbrowse::services::FolderWatchService service;
        for (int iteration = 0; iteration < 32; ++iteration)
        {
            service.StartWatching(hwnd, root.Root().wstring(), false);
            root.WriteFile(L"image_" + std::to_wstring(iteration) + L".jpg", static_cast<std::size_t>(iteration + 1));
            PumpMessagesFor(10);
            service.Stop();
        }
    }
}
