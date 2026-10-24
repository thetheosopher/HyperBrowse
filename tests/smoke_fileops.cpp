#include "smoke_support.h"
#include "smoke_scenarios.h"

namespace hyperbrowse::tests::smoke
{

    void RunImageCommandServiceScenario(HWND hwnd, TestWindowState* state)
    {
        TempFolder root(L"HyperBrowseImageCommandService");
        const fs::path pngPath = root.Root() / L"copy.png";
        const fs::path firstJpegPath = root.Root() / L"first.jpg";
        const fs::path secondJpegPath = root.Root() / L"second.jpg";
        WriteTestImage(pngPath, TestImageFormat::Png, 64, 32);
        WriteTestImage(firstJpegPath, TestImageFormat::Jpeg, 24, 48, 6);
        WriteTestImage(secondJpegPath, TestImageFormat::Jpeg, 24, 48, 6);

        hyperbrowse::services::ImageCommandService service;
        state->imageCommandResult = {};
        state->imageCommandResult.expectedRequestId = service.StartCopyPixels(
            hwnd,
            hwnd,
            hyperbrowse::browser::BuildBrowserItemFromPath(pngPath));
        Expect(PumpMessagesUntil([&]() { return state->imageCommandResult.completed; }, 10000),
               "Asynchronous Copy Image decode did not finish");
        Expect(state->imageCommandResult.update.succeededCount == 1
                   && state->imageCommandResult.update.image
                   && state->imageCommandResult.update.image->Bitmap(),
               "Asynchronous Copy Image did not return decoded pixels");

        state->imageCommandResult = {};
        state->imageCommandResult.expectedRequestId = service.StartInformation(
            hwnd,
            hwnd,
            hyperbrowse::browser::BuildBrowserItemFromPath(pngPath));
        Expect(PumpMessagesUntil([&]() { return state->imageCommandResult.completed; }, 10000),
               "Asynchronous Image Information extraction did not finish");
        Expect(state->imageCommandResult.update.succeededCount == 1
                   && state->imageCommandResult.update.metadata,
               "Asynchronous Image Information did not return metadata");

        state->imageCommandResult = {};
        state->imageCommandResult.expectedRequestId = service.StartJpegOrientation(
            hwnd,
            hwnd,
            {hyperbrowse::browser::BuildBrowserItemFromPath(firstJpegPath),
             hyperbrowse::browser::BuildBrowserItemFromPath(secondJpegPath)},
            1);
        Expect(PumpMessagesUntil([&]() { return state->imageCommandResult.completed; }, 10000),
               "Asynchronous JPEG orientation adjustment did not finish");
        std::wstring firstDecodeError;
        std::wstring secondDecodeError;
        const auto firstImage = hyperbrowse::decode::DecodeFullImage(
            hyperbrowse::browser::BuildBrowserItemFromPath(firstJpegPath),
            &firstDecodeError);
        const auto secondImage = hyperbrowse::decode::DecodeFullImage(
            hyperbrowse::browser::BuildBrowserItemFromPath(secondJpegPath),
            &secondDecodeError);
        Expect(state->imageCommandResult.update.completedCount == 2
                   && state->imageCommandResult.update.succeededCount == 2
                   && state->imageCommandResult.update.failedCount == 0
                   && state->imageCommandResult.update.updatedPaths.size() == 2
                   && firstImage
                   && secondImage
                   && firstImage->Width() == 24
                   && firstImage->Height() == 48
                   && secondImage->Width() == 24
                   && secondImage->Height() == 48,
               "Asynchronous JPEG orientation adjustment reported or wrote the wrong result (completed="
                   + std::to_string(state->imageCommandResult.update.completedCount)
                   + ", succeeded="
                   + std::to_string(state->imageCommandResult.update.succeededCount)
                   + ", failed="
                   + std::to_string(state->imageCommandResult.update.failedCount)
                   + ", paths="
                   + std::to_string(state->imageCommandResult.update.updatedPaths.size())
                   + ", service-error="
                   + Utf8FromWide(state->imageCommandResult.update.message)
                   + ", first-decode-error="
                   + Utf8FromWide(firstDecodeError)
                   + ", second-decode-error="
                   + Utf8FromWide(secondDecodeError)
                   + ")");
        service.Shutdown();
    }

    void RunBatchConvertCancellationScenario(HWND hwnd, TestWindowState* state)
    {
        const fs::path fixtureRoot = TestSourceDirectory() / L"fixtures" / L"raw";
        const fs::path nefPath = fixtureRoot / L"RAW_NIKON_D1.NEF";
        const fs::path nrwPath = fixtureRoot / L"RAW_NIKON_P7000.NRW";

        Expect(fs::exists(nefPath), "The NEF fixture is missing from tests/fixtures/raw");
        Expect(fs::exists(nrwPath), "The NRW fixture is missing from tests/fixtures/raw");

        std::vector<hyperbrowse::browser::BrowserItem> items;
        items.push_back(hyperbrowse::browser::BuildBrowserItemFromPath(nefPath));
        items.push_back(hyperbrowse::browser::BuildBrowserItemFromPath(nrwPath));
        items.push_back(hyperbrowse::browser::BuildBrowserItemFromPath(nefPath));

        TempFolder output(L"HyperBrowseBatchCancel");
        hyperbrowse::services::BatchConvertService service;
        state->batchConvertResult = {};
        state->batchConvertResult.expectedRequestId = service.Start(
            hwnd,
            std::move(items),
            output.Root().wstring(),
            hyperbrowse::services::BatchConvertFormat::Png);

        PumpMessagesFor(100);
        const ULONGLONG start = GetTickCount64();
        service.Cancel();
        const ULONGLONG elapsed = GetTickCount64() - start;
        Expect(elapsed < 250, "Batch convert cancellation blocked instead of returning promptly");
        Expect(service.CancellationCount() >= 1,
               "Batch conversion service did not record cancellation");
        Expect(service.RejectedTaskCount() == 0,
               "Batch conversion service rejected work during the focused scenario");
        Expect(PumpMessagesUntil([&]() { return state->batchConvertResult.completed; }, 15000),
               "Cancelled batch conversion did not post its terminal state");
        Expect(state->batchConvertResult.update.succeededCount
                   + state->batchConvertResult.update.failedCount
                   <= state->batchConvertResult.update.completedCount
                   && state->batchConvertResult.update.completedCount
                   <= state->batchConvertResult.update.totalCount,
               "Cancelled batch conversion reported inconsistent totals");

        TempFolder burstOutput(L"HyperBrowseBatchBurst");
        const auto missingItem = hyperbrowse::browser::BuildBrowserItemFromPath(
            burstOutput.Root() / L"missing.jpg");
        const std::uint64_t cancellationCountBeforeBurst = service.CancellationCount();
        for (int index = 0; index < 31; ++index)
        {
            const std::uint64_t requestId = service.Start(
                nullptr,
                {missingItem},
                burstOutput.Root().wstring(),
                hyperbrowse::services::BatchConvertFormat::Png);
            Expect(requestId != 0, "Batch conversion burst did not allocate a request ID");
            Expect(service.ActiveTaskCount() <= 1 && service.PendingTaskCount() <= 1,
                   "Batch conversion burst exceeded its bounded service capacity");
        }
        Expect(PumpMessagesUntil([&]()
        {
            return service.ActiveTaskCount() == 0 && service.PendingTaskCount() == 0;
        }, 5000), "Batch conversion burst retained background work");
        Expect(service.CancellationCount() >= cancellationCountBeforeBurst + 31,
               "Batch conversion burst did not cancel superseded requests");
        Expect(service.PeakPendingTaskCount() <= 1,
               "Batch conversion peak queue depth exceeded its configured bound");

        const auto diagnostics = hyperbrowse::util::CaptureDiagnosticsSnapshot();
        const auto queueMetric = std::find_if(diagnostics.counters.begin(),
                                              diagnostics.counters.end(),
                                              [](const auto& row)
                                              {
                                                  return row.name == L"service.batch_convert.queue_depth_peak";
                                              });
        Expect(queueMetric != diagnostics.counters.end() && queueMetric->value <= 1,
               "Batch conversion queue-depth diagnostics were not surfaced");

        TempFolder input(L"HyperBrowseBatchInput");
        const fs::path sourcePath = input.Root() / L"source.png";
        WriteTestImage(sourcePath, TestImageFormat::Png, 48, 32);
        const auto sourceItem = hyperbrowse::browser::BuildBrowserItemFromPath(sourcePath);

        TempFolder successfulOutput(L"HyperBrowseBatchAtomicSuccess");
        successfulOutput.WriteFile(L"source.png", 7);
        state->batchConvertResult = {};
        state->batchConvertResult.expectedRequestId = service.Start(
            hwnd,
            {sourceItem},
            successfulOutput.Root().wstring(),
            hyperbrowse::services::BatchConvertFormat::Png);
        Expect(PumpMessagesUntil([&]() { return state->batchConvertResult.completed; }, 10000),
               "Successful batch conversion did not finish");
        Expect(state->batchConvertResult.update.completedCount == 1
                   && state->batchConvertResult.update.totalCount == 1
                   && state->batchConvertResult.update.succeededCount == 1
                   && state->batchConvertResult.update.failedCount == 0
                   && fs::exists(successfulOutput.Root() / L"source_1.png")
                   && fs::file_size(successfulOutput.Root() / L"source.png") == 7,
               "Batch conversion did not publish atomically without overwriting an occupied name");

        TempFolder exhaustedOutput(L"HyperBrowseBatchAtomicExhausted");
        for (int suffix = 0; suffix < 1000; ++suffix)
        {
            const std::wstring fileName = suffix == 0
                ? L"source.png"
                : L"source_" + std::to_wstring(suffix) + L".png";
            exhaustedOutput.WriteFile(fileName, 1);
        }
        state->batchConvertResult = {};
        state->batchConvertResult.expectedRequestId = service.Start(
            hwnd,
            {sourceItem},
            exhaustedOutput.Root().wstring(),
            hyperbrowse::services::BatchConvertFormat::Png);
        Expect(PumpMessagesUntil([&]() { return state->batchConvertResult.completed; }, 10000),
               "Filename-exhausted batch conversion did not finish");
        Expect(state->batchConvertResult.update.completedCount == 1
                   && state->batchConvertResult.update.totalCount == 1
                   && state->batchConvertResult.update.succeededCount == 0
                   && state->batchConvertResult.update.failedCount == 1,
               "Filename exhaustion double-counted a batch failure or reported an invalid success total");

        const auto hasTemporaryOutput = [](const fs::path& folder)
        {
            return std::any_of(fs::directory_iterator(folder), fs::directory_iterator(), [](const fs::directory_entry& entry)
            {
                return entry.path().filename().wstring().starts_with(L".hyperbrowse-convert-");
            });
        };
        Expect(!hasTemporaryOutput(successfulOutput.Root()) && !hasTemporaryOutput(exhaustedOutput.Root()),
               "Batch conversion retained an owned temporary output after completion");
        service.Shutdown();
    }

    void RunFileRenameOperationScenario(HWND hwnd, TestWindowState* state)
    {
        TempFolder root(L"HyperBrowseFileRenameOperation");
        const fs::path sourcePath = root.Root() / L"before.jpg";
        const fs::path renamedPath = root.Root() / L"after.jpg";
        root.WriteFile(L"before.jpg", 16);

        hyperbrowse::services::FileOperationService service;
        state->fileOperationResult = {};
        state->fileOperationResult.expectedRequestId = service.Start(
            hwnd,
            nullptr,
            hyperbrowse::services::FileOperationType::Rename,
            {sourcePath.wstring()},
            {},
            hyperbrowse::services::FileConflictPolicy::PromptShell,
            {L"after.jpg"});

        Expect(PumpMessagesUntil([&]() { return state->fileOperationResult.completed; }, 5000),
               "File rename operation timed out");
        Expect(state->fileOperationResult.update.failedCount == 0,
               "File rename operation reported a failure");
        Expect(!fs::exists(sourcePath), "The original file still exists after rename");
        Expect(fs::exists(renamedPath), "The renamed file was not created");
        Expect(state->fileOperationResult.update.createdPaths.size() == 1
                   && fs::equivalent(state->fileOperationResult.update.createdPaths.front(), renamedPath),
               "File rename operation did not report the created path");

        hyperbrowse::ui::FileOperationPathState renamedState;
        Expect(hyperbrowse::ui::TryCaptureFileOperationPathState(renamedPath.wstring(), &renamedState)
                   && hyperbrowse::ui::FileOperationPathStatesMatch(
                       {renamedPath.wstring()},
                       {renamedState}),
               "File-operation journal identity did not match an unchanged result");
        root.WriteFile(L"after.jpg", 64);
        Expect(!hyperbrowse::ui::FileOperationPathStatesMatch(
                   {renamedPath.wstring()},
                   {renamedState}),
               "File-operation journal identity accepted an externally changed result");
    }

    void RunFileOperationShutdownScenario()
    {
        TempFolder root(L"HyperBrowseFileOperationShutdown");
        const fs::path sourcePath = root.Root() / L"source.bin";
        const fs::path destinationFolder = root.Root() / L"destination";
        const fs::path destinationPath = destinationFolder / sourcePath.filename();
        root.WriteFile(L"source.bin", 1024);
        fs::create_directories(destinationFolder);

        std::mutex performMutex;
        std::condition_variable performCondition;
        bool performEntered = false;
        bool releasePerform = false;
        bool ownerValidDuringPerform = false;
        bool shutdownCompleted = false;
        bool destructionCompleted = false;
        HWND operationOwnerWindow = CreateTestWindow(nullptr, GetModuleHandleW(nullptr));
        Expect(operationOwnerWindow != nullptr,
               "File operation shutdown scenario could not create its shell owner window");

        auto service = std::make_unique<hyperbrowse::services::FileOperationService>([&]() -> HRESULT
        {
            std::unique_lock lock(performMutex);
            performEntered = true;
            ownerValidDuringPerform = IsWindow(operationOwnerWindow) != FALSE;
            performCondition.notify_all();
            performCondition.wait(lock, [&]() { return releasePerform; });
            return E_ABORT;
        });
        service->Start(operationOwnerWindow,
                       operationOwnerWindow,
                       hyperbrowse::services::FileOperationType::Copy,
                       {sourcePath.wstring()},
                       destinationFolder.wstring(),
                       hyperbrowse::services::FileConflictPolicy::PromptShell);

        bool reachedPerform = false;
        {
            std::unique_lock lock(performMutex);
            reachedPerform = performCondition.wait_for(lock,
                                                       std::chrono::seconds(5),
                                                       [&]() { return performEntered; });
            if (!reachedPerform)
            {
                releasePerform = true;
            }
        }
        performCondition.notify_all();
        Expect(reachedPerform, "File operation shutdown scenario did not enter the blocked shell operation");
         Expect(ownerValidDuringPerform,
             "File operation shutdown scenario did not keep its shell owner valid during PerformOperations");

        service->Cancel();
         Expect(service->IsCancellationRequested(),
             "File operation service did not expose its requested cancellation state");
        std::vector<std::uint64_t> burstRequestIds;
        burstRequestIds.reserve(16);
        for (int index = 0; index < 16; ++index)
        {
            burstRequestIds.push_back(service->Start(
                nullptr,
                nullptr,
                hyperbrowse::services::FileOperationType::Copy,
                {sourcePath.wstring()},
                destinationFolder.wstring(),
                hyperbrowse::services::FileConflictPolicy::PromptShell));
        }
        Expect(std::all_of(burstRequestIds.begin(), burstRequestIds.end(), [](std::uint64_t requestId)
        {
            return requestId != 0;
        }), "File operation queue burst did not allocate request IDs");
        Expect(service->ActiveTaskCount() == 1,
               "File operation service did not report its blocked active task");
        Expect(service->PendingTaskCount() == 1,
               "File operation service did not retain its bounded pending task");
        Expect(service->RejectedTaskCount() == 15,
               "File operation service did not reject work beyond its pending limit during the burst");
        Expect(service->PeakPendingTaskCount() == 1,
               "File operation service exceeded its configured peak queue depth");
        Expect(service->CancellationCount() >= 18,
               "File operation service did not record supersession and shutdown cancellation requests");
         auto* serviceAddress = service.get();
         std::thread shutdownThread([service = std::move(service),
                         &performMutex,
                         &performCondition,
                         &shutdownCompleted,
                         &destructionCompleted]() mutable
        {
            service->Shutdown();
            {
                std::scoped_lock lock(performMutex);
                shutdownCompleted = true;
            }
            performCondition.notify_all();
            service.reset();
            {
                std::scoped_lock lock(performMutex);
                destructionCompleted = true;
            }
            performCondition.notify_all();
        });

        Expect(PumpMessagesUntil([&]() { return serviceAddress->IsShutdownRequested(); }, 1000),
               "File operation service did not expose its shutdown request while shell work was blocked");

         bool returnedBeforeRelease = false;
        {
            std::unique_lock lock(performMutex);
            returnedBeforeRelease = performCondition.wait_for(lock,
                                                               std::chrono::milliseconds(250),
                                                               [&]() { return shutdownCompleted; });
        }
        Expect(!returnedBeforeRelease,
               "File operation service shutdown returned while shell work was still blocked");

        {
            std::scoped_lock lock(performMutex);
            releasePerform = true;
        }
        performCondition.notify_all();
        shutdownThread.join();

        Expect(shutdownCompleted, "File operation service shutdown did not complete after shell work was released");
        Expect(destructionCompleted, "File operation service destruction did not complete after shell work was released");
        Expect(IsWindow(operationOwnerWindow) != FALSE,
               "File operation shutdown scenario destroyed its shell owner before worker completion");
        DestroyWindow(operationOwnerWindow);
        Expect(!fs::exists(destinationPath),
               "Cancelled file operation created a destination after shutdown began");
    }

        void RunFileConflictPlanningScenario(HWND hwnd, TestWindowState* state)
        {
         TempFolder root(L"HyperBrowseFileConflictPlanning");
         root.WriteFile(L"source-a\\alpha.jpg", 16);
         root.WriteFile(L"source-b\\alpha.jpg", 16);
         root.WriteFile(L"source-c\\beta.png", 16);
         root.WriteFile(L"dest\\alpha.jpg", 16);
         root.WriteFile(L"dest\\beta.png", 16);
         root.WriteFile(L"dest\\beta.1.png", 16);

         const fs::path destinationFolder = root.Root() / L"dest";
         const std::vector<std::wstring> sourcePaths{
             (root.Root() / L"source-a" / L"alpha.jpg").wstring(),
             (root.Root() / L"source-b" / L"alpha.jpg").wstring(),
             (root.Root() / L"source-c" / L"beta.png").wstring(),
         };

         const hyperbrowse::services::FileConflictPlan overwritePlan = hyperbrowse::services::PlanDestinationConflicts(
             sourcePaths,
             destinationFolder.wstring(),
             hyperbrowse::services::FileConflictPolicy::OverwriteExisting);
         Expect(overwritePlan.conflictCount == 3,
             "Overwrite planning did not detect all copy/move destination conflicts");

         const hyperbrowse::services::FileConflictPlan renamePlan = hyperbrowse::services::PlanDestinationConflicts(
             sourcePaths,
             destinationFolder.wstring(),
             hyperbrowse::services::FileConflictPolicy::AutoRenameNumericSuffix);
         Expect(renamePlan.conflictCount == 3,
             "Auto-rename planning did not detect all destination conflicts");
         Expect(renamePlan.renamedCount == 3,
             "Auto-rename planning did not record the expected rename count");
         Expect(renamePlan.targetLeafNames.size() == sourcePaths.size(),
             "Auto-rename planning did not preserve target-name alignment");
         Expect(renamePlan.targetLeafNames[0] == L"alpha.1.jpg",
             "Auto-rename planning chose the wrong suffix for the first alpha conflict");
         Expect(renamePlan.targetLeafNames[1] == L"alpha.2.jpg",
             "Auto-rename planning chose the wrong suffix for the second alpha conflict");
         Expect(renamePlan.targetLeafNames[2] == L"beta.2.png",
             "Auto-rename planning did not skip the pre-existing beta.1 target");

         root.WriteFile(L"dest\\alpha.1.jpg", 16);
         const std::wstring sameFolderSource = (destinationFolder / L"alpha.jpg").wstring();
         const hyperbrowse::services::FileConflictPlan duplicatePlan =
             hyperbrowse::services::PlanDestinationConflicts(
                 {sameFolderSource},
                 destinationFolder.wstring(),
                 hyperbrowse::services::FileConflictPolicy::AutoRenameNumericSuffix);
         Expect(duplicatePlan.conflictCount == 1
                    && duplicatePlan.renamedCount == 1
                    && duplicatePlan.targetLeafNames == std::vector<std::wstring>{L"alpha.2.jpg"}
                    && !fs::exists(destinationFolder / duplicatePlan.targetLeafNames.front()),
                "Same-folder duplicate planning did not select an available name distinct from its source");

         hyperbrowse::services::FileOperationService duplicateService;
         state->fileOperationResult = {};
         const std::uint64_t requestId = duplicateService.Start(
             hwnd,
             nullptr,
             hyperbrowse::services::FileOperationType::Copy,
             {sameFolderSource},
             destinationFolder.wstring(),
             hyperbrowse::services::FileConflictPolicy::AutoRenameNumericSuffix,
             duplicatePlan.targetLeafNames);
         Expect(requestId != 0, "Same-folder duplicate copy did not start");
         state->fileOperationResult.expectedRequestId = requestId;
         Expect(PumpMessagesUntil([&]() { return state->fileOperationResult.completed; }, 5000),
                "Same-folder duplicate copy timed out");
         const fs::path duplicatePath = destinationFolder / duplicatePlan.targetLeafNames.front();
         Expect(state->fileOperationResult.update.failedCount == 0
                    && fs::exists(duplicatePath)
                    && fs::file_size(duplicatePath) == fs::file_size(sameFolderSource),
                "Same-folder duplicate copy did not create the planned file");
         duplicateService.Shutdown();
        }
}
