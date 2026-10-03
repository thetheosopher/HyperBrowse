#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cwchar>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "services/BatchConvertService.h"
#include "services/FileOperationService.h"
#include "browser/ThumbnailRatingKeyPolicy.h"
#include "ui/BrowserItemScopeCollector.h"
#include "viewer/CompareSessionPolicy.h"
#include "ui/BrowserPresentationPersistence.h"
#include "ui/CommandBarController.h"
#include "ui/CommandIds.h"
#include "util/Log.h"
#include "ui/ClipboardFileTransfer.h"
#include "ui/DetailsPanelHistogram.h"
#include "ui/DetailsPanelLayout.h"
#include "ui/DisplaySurfaceRecoveryPolicy.h"
#include "ui/FileCommandController.h"
#include "ui/FileOperationJournal.h"
#include "ui/FileOperationReconciler.h"
#include "ui/FilingResumePersistence.h"
#include "ui/FolderTreeDropPolicy.h"
#include "ui/FolderHistory.h"
#include "ui/PerformanceHud.h"
#include "ui/ImageWorkflowPersistence.h"
#include "ui/ItemNumberNavigationPolicy.h"
#include "ui/PairedRawJpegResolver.h"
#include "ui/QuickAccessDestinationBuilder.h"
#include "ui/QuickAccessLayout.h"
#include "ui/QuickAccessMenuBuilder.h"
#include "ui/QuickAccessPathList.h"
#include "ui/QuickAccessShortcutEditPolicy.h"
#include "ui/QuickSendConfirmation.h"
#include "ui/RightPaneHitTester.h"
#include "ui/MenuMetrics.h"
#include "ui/SelectedPathPersistence.h"
#include "ui/SelectionRatingPolicy.h"
#include "ui/ViewCommandController.h"
#include "ui/ShortcutCatalog.h"
#include "ui/ViewerItemSelectionPolicy.h"
#include "ui/ViewerPendingOperationState.h"
#include "ui/ViewerSynchronizer.h"
#include "ui/ViewerTransitionPolicy.h"
#include "ui/ViewerSettingsPersistence.h"
#include "ui/WindowAsyncMessageRouter.h"
#include "ui/WindowBoundsPersistence.h"
#include "ui/WindowTimerRouter.h"
#include "ui/PerformanceSettingsPersistence.h"
#include "util/ResourceSizing.h"
#include "util/PathUtils.h"

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

        void RunPrefetchSizingScenario()
        {
            using hyperbrowse::util::ResourceProfile;
            Expect(hyperbrowse::util::DefaultPrefetchDepth(ResourceProfile::Conservative) == 1,
                   "Conservative prefetch default changed unexpectedly");
            Expect(hyperbrowse::util::DefaultPrefetchDepth(ResourceProfile::Balanced) == 3,
                   "Balanced prefetch default changed unexpectedly");
            Expect(hyperbrowse::util::DefaultPrefetchDepth(ResourceProfile::Performance) == 8,
                   "Performance prefetch default changed unexpectedly");
            Expect(hyperbrowse::util::DefaultPrefetchDepth(ResourceProfile::Aggressive) == 12,
                   "Aggressive prefetch default changed unexpectedly");
            Expect(hyperbrowse::util::ResolvePrefetchDepth(ResourceProfile::Performance, hyperbrowse::util::kAutomaticPrefetchDepth) == 8,
                   "Automatic prefetch depth did not follow the resource profile");
            Expect(hyperbrowse::util::ResolvePrefetchDepth(ResourceProfile::Balanced, 1) == 1,
                   "Explicit minimum prefetch depth was not preserved");
            Expect(hyperbrowse::util::ResolvePrefetchDepth(ResourceProfile::Balanced, 16) == 16,
                   "Explicit maximum prefetch depth was not preserved");
            Expect(hyperbrowse::util::ResolvePrefetchDepth(ResourceProfile::Balanced, -4) == 1,
                   "Prefetch depth did not clamp below the supported range");
            Expect(hyperbrowse::util::ResolvePrefetchDepth(ResourceProfile::Balanced, 99) == 16,
                   "Prefetch depth did not clamp above the supported range");
        }

        void RunResourceSizingRangeScenario()
        {
            using hyperbrowse::util::MemorySnapshot;
            using hyperbrowse::util::ResourceProfile;

            hyperbrowse::util::SetMemorySnapshotOverrideForTests(MemorySnapshot{
                16ULL * 1024ULL * 1024ULL * 1024ULL,
                6ULL * 1024ULL * 1024ULL * 1024ULL});
            const auto thumbnailRange = hyperbrowse::util::RecommendedThumbnailCacheRange(ResourceProfile::Performance);
            const auto metadataRange = hyperbrowse::util::RecommendedMetadataCacheRange(ResourceProfile::Performance);
            const auto persistentRange = hyperbrowse::util::RecommendedPersistentThumbnailCacheRange(ResourceProfile::Performance);
            Expect(thumbnailRange.IsValid() && thumbnailRange.maximum <= 2ULL * 1024ULL * 1024ULL * 1024ULL,
                   "Performance thumbnail recommendations exceeded the available-memory hard cap");
            Expect(metadataRange.IsValid() && persistentRange.IsValid(),
                   "Profile cache recommendation ranges were not valid");
            Expect(hyperbrowse::services::ThumbnailScheduler::ResolveCacheCapacityBytes(
                       8ULL * 1024ULL * 1024ULL * 1024ULL,
                       ResourceProfile::Performance)
                       == 2ULL * 1024ULL * 1024ULL * 1024ULL,
                   "Performance thumbnail cache override did not honor the available-memory hard cap");
            hyperbrowse::util::ClearMemorySnapshotOverrideForTests();
        }

        void RunViewerTransitionPolicyScenario()
        {
            Expect(!hyperbrowse::ui::ShouldUseViewerTransition(false, false),
                   "Manual navigation enabled a transition while the setting was disabled");
            Expect(hyperbrowse::ui::ShouldUseViewerTransition(false, true),
                   "Manual navigation ignored the enabled transition setting");
            Expect(hyperbrowse::ui::ShouldUseViewerTransition(true, false),
                   "Slideshow navigation ignored the configured transition");
            Expect(hyperbrowse::ui::ShouldUseViewerTransition(true, true),
                   "Slideshow navigation policy changed when manual transitions were enabled");
        }

         void RunCompareSessionPolicyScenario()
         {
             using hyperbrowse::viewer::CompareTileBounds;
             using hyperbrowse::viewer::HitTestCompareTile;
             using hyperbrowse::viewer::ImageCenterFromPan;
             using hyperbrowse::viewer::IsSupportedCompareTileCount;
             using hyperbrowse::viewer::NormalizedImageCenter;
             using hyperbrowse::viewer::NextAvailableCompareCandidate;
             using hyperbrowse::viewer::PanFromImageCenter;

             const RECT client{0, 0, 1000, 800};
             Expect(!IsSupportedCompareTileCount(1)
                        && IsSupportedCompareTileCount(2)
                        && IsSupportedCompareTileCount(4)
                        && !IsSupportedCompareTileCount(5),
                    "Compare selection eligibility did not enforce the two-to-four tile range");
             const std::vector<RECT> pairBounds = CompareTileBounds(client, 2, 16);
             Expect(pairBounds.size() == 2 && pairBounds[0].right < pairBounds[1].left,
                 "Two-image compare panes were not separated by a gap");
             Expect(HitTestCompareTile(pairBounds, POINT{10, 10}) == 0
                  && HitTestCompareTile(pairBounds, POINT{990, 10}) == 1
                  && HitTestCompareTile(pairBounds, POINT{500, 10}) == -1,
                 "Two-image compare hit testing did not respect tile bounds and gap");

             const std::vector<RECT> tripleBounds = CompareTileBounds(client, 3, 16);
             const std::vector<RECT> quadBounds = CompareTileBounds(client, 4, 16);
             Expect(tripleBounds.size() == 3 && quadBounds.size() == 4,
                 "N-up compare layout did not create the requested tile count");
             Expect(tripleBounds[0].right < tripleBounds[1].left
                  && tripleBounds[0].bottom < tripleBounds[2].top
                  && quadBounds[2].right < quadBounds[3].left,
                 "N-up compare bounds overlapped or omitted the configured gutters");
             Expect(CompareTileBounds(client, 1, 16).empty()
                  && CompareTileBounds(client, 5, 16).empty(),
                 "Compare layout accepted a tile count outside the supported range");

             const std::array visibleIndices{1, 3, 5};
             Expect(NextAvailableCompareCandidate(5, visibleIndices, 6, 1) == 0,
                 "Forward compare candidate cycling did not wrap to the next unused candidate");
             Expect(NextAvailableCompareCandidate(1, visibleIndices, 6, -1) == 0,
                 "Reverse compare candidate cycling did not skip occupied candidates");
                 const std::array allVisibleIndices{0, 1, 2};
                 Expect(NextAvailableCompareCandidate(1, allVisibleIndices, 3, 1) == -1,
                 "Compare candidate cycling did not report that all candidates are already visible");

             const RECT smallClient{0, 0, 400, 300};
             const POINT clampedPan = PanFromImageCenter(1000, 800, 1.0, smallClient, {0.0, 0.0});
             Expect(clampedPan.x == 300 && clampedPan.y == 250,
                 "Normalized compare pan was not clamped to the image edges");
             const auto normalizedCenter = ImageCenterFromPan(1000, 800, 1.0, clampedPan.x, clampedPan.y);
             Expect(normalizedCenter.x > 0.19 && normalizedCenter.x < 0.21
                  && normalizedCenter.y > 0.18 && normalizedCenter.y < 0.20,
                 "Compare pan conversion did not preserve the visible normalized image center");

             const RECT landscapeTile{0, 0, 500, 300};
             const RECT portraitTile{0, 0, 300, 500};
             const NormalizedImageCenter sharedCenter{0.7, 0.3};
             const POINT landscapePan = PanFromImageCenter(4000, 3000, 0.3, landscapeTile, sharedCenter);
             const POINT portraitPan = PanFromImageCenter(3000, 4000, 0.3, portraitTile, sharedCenter);
             const NormalizedImageCenter landscapeCenter = ImageCenterFromPan(4000, 3000, 0.3,
                                                                             landscapePan.x, landscapePan.y);
             const NormalizedImageCenter portraitCenter = ImageCenterFromPan(3000, 4000, 0.3,
                                                                             portraitPan.x, portraitPan.y);
             Expect(std::abs(landscapeCenter.x - sharedCenter.x) < 0.01
                        && std::abs(landscapeCenter.y - sharedCenter.y) < 0.01
                        && std::abs(portraitCenter.x - sharedCenter.x) < 0.01
                        && std::abs(portraitCenter.y - sharedCenter.y) < 0.01,
                    "Synchronized pan did not preserve normalized image center across unequal image dimensions");
         }

        void RunFileOperationMediaCacheInvalidationScenario()
        {
            using hyperbrowse::services::FileOperationType;
            using hyperbrowse::services::FileOperationUpdate;

            const auto isInCurrentScope = [](std::wstring_view path)
            {
                return path == L"C:\\Images\\source.jpg"
                    || path == L"C:\\Images\\created.jpg"
                    || path == L"C:\\Images\\renamed.jpg";
            };

            FileOperationUpdate copyOutside;
            copyOutside.type = FileOperationType::Copy;
            copyOutside.succeededSourcePaths = {L"C:\\Images\\source.jpg"};
            copyOutside.createdPaths = {L"D:\\Favorites\\source.jpg"};
            Expect(hyperbrowse::ui::BuildMediaCacheInvalidationPaths(
                       copyOutside, L"C:\\Images", isInCurrentScope).empty(),
                   "Copying an image outside the current folder invalidated visible media caches");

            FileOperationUpdate copyInside;
            copyInside.type = FileOperationType::Copy;
            copyInside.succeededSourcePaths = {L"D:\\Incoming\\source.jpg"};
            copyInside.createdPaths = {L"C:\\Images\\created.jpg"};
            const std::vector<std::wstring> copyInsidePaths = hyperbrowse::ui::BuildMediaCacheInvalidationPaths(
                copyInside, L"C:\\Images", isInCurrentScope);
            Expect(copyInsidePaths == std::vector<std::wstring>{L"C:\\Images\\created.jpg"},
                   "Copying an image into the current folder did not invalidate the created path");

            FileOperationUpdate moveOutside;
            moveOutside.type = FileOperationType::Move;
            moveOutside.succeededSourcePaths = {L"C:\\Images\\source.jpg"};
            moveOutside.createdPaths = {L"D:\\Favorites\\source.jpg"};
            const std::vector<std::wstring> moveOutsidePaths = hyperbrowse::ui::BuildMediaCacheInvalidationPaths(
                moveOutside, L"C:\\Images", isInCurrentScope);
            Expect(moveOutsidePaths == std::vector<std::wstring>{L"C:\\Images\\source.jpg"},
                   "Moving an image out of the current folder did not preserve source invalidation");

            FileOperationUpdate deleteInside;
            deleteInside.type = FileOperationType::DeleteRecycleBin;
            deleteInside.succeededSourcePaths = {L"C:\\Images\\source.jpg"};
            const std::vector<std::wstring> deleteInsidePaths = hyperbrowse::ui::BuildMediaCacheInvalidationPaths(
                deleteInside, L"C:\\Images", isInCurrentScope);
            Expect(deleteInsidePaths == std::vector<std::wstring>{L"C:\\Images\\source.jpg"},
                   "Deleting an image from the current folder did not invalidate its media cache");
        }

        void RunFolderHistoryScenario()
        {
            using hyperbrowse::ui::FolderHistory;
            using hyperbrowse::ui::FolderHistoryNavigationDirection;
                 using namespace hyperbrowse::ui::command_ids;

                 Expect(CommandIdFromXButton(XBUTTON1) == ID_VIEW_NAVIGATE_BACK_FOLDER,
                     "Mouse back button did not map to folder back navigation");
            const auto driveSegments = hyperbrowse::ui::BuildFolderBreadcrumbSegments(L"C:\\Pictures\\Trips\\");
            Expect(driveSegments.size() == 3
                       && driveSegments[0].label == L"C:\\"
                       && driveSegments[0].targetFolderPath == L"C:\\"
                       && driveSegments[1].label == L"Pictures"
                       && driveSegments[1].targetFolderPath == L"C:\\Pictures"
                       && driveSegments[2].label == L"Trips"
                       && driveSegments[2].targetFolderPath == L"C:\\Pictures\\Trips",
                   "Drive breadcrumb segments did not preserve root and ancestor paths");

            const auto uncSegments = hyperbrowse::ui::BuildFolderBreadcrumbSegments(L"\\\\server\\share\\photos");
            Expect(uncSegments.size() == 2
                       && uncSegments[0].label == L"\\\\server\\share"
                       && uncSegments[0].targetFolderPath == L"\\\\server\\share\\"
                       && uncSegments[1].label == L"photos"
                       && uncSegments[1].targetFolderPath == L"\\\\server\\share\\photos",
                   "UNC breadcrumb segments did not keep the share as the navigable root");

            Expect(hyperbrowse::ui::BuildFolderBreadcrumbSegments(L"").empty(),
                   "An empty folder path produced breadcrumb segments");
                 Expect(CommandIdFromXButton(XBUTTON2) == ID_VIEW_NAVIGATE_FORWARD_FOLDER,
                     "Mouse forward button did not map to folder forward navigation");
                 Expect(CommandIdFromXButton(0) == 0,
                     "Unknown mouse button unexpectedly mapped to folder navigation");

            FolderHistory history(4);
            history.RecordOpenedFolder(L"C:\\one");
            history.RecordOpenedFolder(L"C:\\two");
            history.RecordOpenedFolder(L"C:\\missing");
            history.RecordOpenedFolder(L"C:\\four");

            const auto resolveExistingFolder = [](std::wstring_view path)
            {
                return path == L"C:\\missing" ? std::wstring(L"C:\\recovered") : std::wstring(path);
            };
            const auto compareFolderPaths = [](std::wstring_view lhs, std::wstring_view rhs)
            {
                return _wcsicmp(std::wstring(lhs).c_str(), std::wstring(rhs).c_str()) == 0;
            };

            const auto back = history.FindBack(L"C:\\four", resolveExistingFolder, compareFolderPaths);
            Expect(back.has_value(), "Folder history did not find a previous folder");
            Expect(back->direction == FolderHistoryNavigationDirection::Back
                       && back->targetIndex == 2
                       && back->folderPath == L"C:\\recovered",
                   "Folder history did not resolve a missing folder to its existing ancestor");

            history.BeginNavigation(back->direction, back->targetIndex);
            history.RecordOpenedFolder(L"C:\\recovered");
            const auto forward = history.FindForward(L"C:\\RECOVERED", resolveExistingFolder, compareFolderPaths);
            Expect(forward.has_value() && forward->folderPath == L"C:\\four",
                   "Folder history did not preserve forward navigation after ancestor replacement");

            history.BeginNavigation(forward->direction, forward->targetIndex);
            history.RecordOpenedFolder(L"C:\\four");
            history.RecordOpenedFolder(L"C:\\five");
            history.RecordOpenedFolder(L"C:\\five");
            const auto branchedBack = history.FindBack(L"C:\\five", resolveExistingFolder, compareFolderPaths);
            Expect(branchedBack.has_value() && branchedBack->folderPath == L"C:\\four",
                   "Folder history did not truncate the forward branch or suppress duplicates");
        }

        void RunFileOperationJournalScenario()
        {
            using hyperbrowse::ui::FileOperationJournal;
            using hyperbrowse::ui::FileOperationJournalEntry;
            using hyperbrowse::ui::UndoRedoOperation;

            FileOperationJournal journal(2);
            FileOperationJournalEntry first;
            first.type = 1;
            first.sourcePaths = {L"C:\\source\\one.jpg"};
            first.createdPaths = {L"C:\\destination\\one.jpg"};
            journal.Record(first);

            FileOperationJournalEntry second;
            second.type = 4;
            second.sourcePaths = {L"C:\\source\\old.jpg"};
            second.createdPaths = {L"C:\\source\\new.jpg"};
            journal.Record(second);

            Expect(journal.CanUndo() && !journal.CanRedo(),
                   "File-operation journal did not record a reversible operation");
            Expect(journal.UndoEntry() && journal.UndoEntry()->type == 4,
                   "File-operation journal did not expose the newest undo entry");

            FileOperationJournalEntry restoredSecond = second;
            restoredSecond.sourcePaths = {L"C:\\source\\restored-after-conflict.jpg"};
            journal.Begin(UndoRedoOperation::Undo);
            journal.Complete(UndoRedoOperation::Undo, true, restoredSecond);
            Expect(journal.CanUndo() && journal.CanRedo(),
                   "Successful undo did not move the entry to redo history");
            Expect(journal.RedoEntry() && journal.RedoEntry()->type == 4,
                   "File-operation journal stored the wrong redo entry");
            Expect(journal.RedoEntry()->sourcePaths == restoredSecond.sourcePaths,
                   "File-operation journal did not retain the actual inverse result mapping");

            journal.Begin(UndoRedoOperation::Redo);
            journal.Complete(UndoRedoOperation::Redo, false);
            Expect(journal.CanUndo() && journal.CanRedo(),
                   "Failed redo changed journal history");
            Expect(journal.PendingOperation() == UndoRedoOperation::None,
                   "File-operation journal retained a failed pending operation");

            journal.Discard(UndoRedoOperation::Redo);
            Expect(!journal.CanRedo(), "File-operation journal retained an unsafe discarded redo entry");

            FileOperationJournalEntry third;
            third.type = 0;
            journal.Record(std::move(third));
            Expect(!journal.CanRedo() && journal.UndoEntry() && journal.UndoEntry()->type == 0,
                   "A new file operation did not clear redo history");
        }

        void RunFileCommandControllerScenario()
        {
            using hyperbrowse::services::BatchConvertFormat;
            using hyperbrowse::ui::FileCommandController;
            using namespace hyperbrowse::ui::command_ids;

            FileCommandController controller;
            int copyCallCount = 0;
            bool deletePermanent = false;
            int rotationDelta = 0;
            bool batchSelectionScope = false;
            BatchConvertFormat batchFormat = BatchConvertFormat::Jpeg;
            std::size_t recentFolderIndex = 0;
            std::size_t favoriteIndex = 0;
            bool resumeFilingCalled = false;
            bool navigateParentCalled = false;

            FileCommandController::Handlers handlers;
            handlers.onCopySelection = [&copyCallCount]
            {
                ++copyCallCount;
            };
            handlers.onDeleteSelection = [&deletePermanent](bool permanent)
            {
                deletePermanent = permanent;
            };
            handlers.onRotateJpeg = [&rotationDelta](int delta)
            {
                rotationDelta = delta;
            };
            handlers.onBatchConvert = [&batchSelectionScope, &batchFormat](bool selectionScope, BatchConvertFormat format)
            {
                batchSelectionScope = selectionScope;
                batchFormat = format;
            };
            handlers.onCopySelectionToFavorite = [&favoriteIndex](std::size_t index)
            {
                favoriteIndex = index;
            };
            handlers.onOpenRecentFolder = [&recentFolderIndex](std::size_t index)
            {
                recentFolderIndex = index;
            };
            handlers.onResumeFiling = [&resumeFilingCalled]
            {
                resumeFilingCalled = true;
            };
            handlers.onNavigateParentFolder = [&navigateParentCalled]
            {
                navigateParentCalled = true;
            };
            controller.Configure(std::move(handlers));

                 Expect(controller.Handle(ID_FILE_COPY_SELECTION),
                   "File command controller did not handle selection copy");
            Expect(controller.Handle(ID_FILE_COPY_SELECTION_BROWSE) && copyCallCount == 2,
                   "File command controller did not preserve copy command aliases");
            Expect(controller.Handle(ID_FILE_DELETE_SELECTION_PERMANENT) && deletePermanent,
                   "File command controller did not forward permanent-delete state");
            Expect(controller.Handle(ID_FILE_ROTATE_JPEG_LEFT) && rotationDelta == -1,
                   "File command controller did not forward JPEG rotation direction");
            Expect(controller.Handle(ID_FILE_BATCH_CONVERT_SELECTION_PNG)
                       && batchSelectionScope
                       && batchFormat == BatchConvertFormat::Png,
                   "File command controller did not forward batch-convert scope and format");
            Expect(controller.Handle(ID_FILE_COPY_SELECTION_FAVORITE_BASE + 2) && favoriteIndex == 2,
                   "File command controller did not decode favorite destination index");
            Expect(controller.Handle(ID_FILE_OPEN_RECENT_FOLDER_BASE + 3) && recentFolderIndex == 3,
                   "File command controller did not decode recent-folder index");
                 Expect(controller.Handle(ID_FILE_RESUME_FILING) && resumeFilingCalled,
                     "File command controller did not forward resume filing");
                     Expect(controller.Handle(ID_VIEW_NAVIGATE_PARENT_FOLDER) && navigateParentCalled,
                         "File command controller did not forward parent-folder navigation");
            Expect(!controller.Handle(ID_VIEW_THUMBNAILS),
                   "File command controller claimed a view command outside its ownership");
        }

        void RunViewCommandControllerScenario()
        {
            using hyperbrowse::ui::ViewCommandController;
            using namespace hyperbrowse::ui::command_ids;

            ViewCommandController controller;
            UINT appTextSizeCommand = 0;
            UINT thumbnailSizeCommand = 0;
            UINT sortCommand = 0;
            UINT performanceProfileCommand = 0;
            int performanceHudCallCount = 0;
            int colorManagementCallCount = 0;
            int detailsCallCount = 0;
            int itemNumberCallCount = 0;
            int openLogCallCount = 0;
            std::wstring launchedLogDirectory;

            ViewCommandController::Handlers handlers;
            handlers.onAppTextSize = [&appTextSizeCommand](UINT commandId)
            {
                appTextSizeCommand = commandId;
            };
            handlers.onThumbnailSizePreset = [&thumbnailSizeCommand](UINT commandId)
            {
                thumbnailSizeCommand = commandId;
            };
            handlers.onSortMode = [&sortCommand](UINT commandId)
            {
                sortCommand = commandId;
            };
            handlers.onPerformanceProfile = [&performanceProfileCommand](UINT commandId)
            {
                performanceProfileCommand = commandId;
            };
            handlers.onPerformanceHud = [&performanceHudCallCount]
            {
                ++performanceHudCallCount;
            };
            handlers.onColorManagement = [&colorManagementCallCount]
            {
                ++colorManagementCallCount;
            };
            handlers.onDetails = [&detailsCallCount]
            {
                ++detailsCallCount;
            };
            handlers.onGoToItemNumber = [&itemNumberCallCount]
            {
                ++itemNumberCallCount;
            };
            handlers.onOpenLogFolder = [&](std::wstring_view directory)
            {
                ++openLogCallCount;
                launchedLogDirectory = directory;
            };
            controller.Configure(std::move(handlers));

            Expect(controller.Handle(ID_HELP_OPEN_LOG_FOLDER) && openLogCallCount == 1
                && launchedLogDirectory == hyperbrowse::util::GetLogDirectory(hyperbrowse::util::GetLogFilePath()),
                "Open Log Folder did not route the logger's actual directory to the shell callback");
            Expect(hyperbrowse::util::GetLogDirectory(L"C:\\Log Profiles\\\u65e5\u672c\\session.log") == L"C:\\Log Profiles\\\u65e5\u672c",
                "Log directory resolution changed a Unicode destination");
            Expect(hyperbrowse::util::GetLogDirectory(L"relative.log") == L"."
                && hyperbrowse::util::GetLogDirectory(L"relative\\session.log") == L"relative",
                "Log directory resolution changed a relative destination");
            Expect(hyperbrowse::util::GetLogDirectory(L"\\\\server\\share\\session.log") == L"\\\\server\\share",
                "Log directory resolution changed a UNC share destination");
            const auto logShortcuts = hyperbrowse::ui::MainWindowShortcuts();
            Expect(std::none_of(logShortcuts.begin(), logShortcuts.end(), [](const auto& shortcut)
                { return shortcut.commandId == ID_HELP_OPEN_LOG_FOLDER; }),
                "Open Log Folder added an undocumented shortcut");

                 Expect(controller.Handle(ID_VIEW_COLOR_MANAGEMENT) && colorManagementCallCount == 1,
                     "View command controller did not route the color-management toggle");
                const auto colorShortcuts = hyperbrowse::ui::MainWindowShortcuts();
                Expect(std::none_of(colorShortcuts.begin(), colorShortcuts.end(), [](const auto& shortcut)
                {
                    return shortcut.commandId == ID_VIEW_COLOR_MANAGEMENT;
                }), "Color management added an undocumented shortcut");
            Expect(controller.Handle(ID_VIEW_APP_TEXT_SIZE_LARGE)
                       && appTextSizeCommand == ID_VIEW_APP_TEXT_SIZE_LARGE,
                   "View command controller did not route app text-size commands");
            Expect(controller.Handle(ID_VIEW_THUMBNAIL_SIZE_320)
                       && thumbnailSizeCommand == ID_VIEW_THUMBNAIL_SIZE_320,
                   "View command controller did not route thumbnail-size commands");
            Expect(controller.Handle(ID_VIEW_SORT_TAGS) && sortCommand == ID_VIEW_SORT_TAGS,
                   "View command controller did not route sort commands");
            Expect(controller.Handle(ID_HELP_PERFORMANCE_PROFILE_AGGRESSIVE)
                       && performanceProfileCommand == ID_HELP_PERFORMANCE_PROFILE_AGGRESSIVE,
                   "View command controller did not route performance-profile commands");
                 Expect(controller.Handle(ID_VIEW_PERFORMANCE_HUD) && performanceHudCallCount == 1,
                     "View command controller did not route the performance HUD toggle");
                     Expect(controller.Handle(ID_VIEW_GO_TO_ITEM_NUMBER) && itemNumberCallCount == 1,
                         "View command controller did not route go-to-item-number");
                const auto shortcuts = hyperbrowse::ui::MainWindowShortcuts();
                const auto performanceHudShortcut = std::find_if(shortcuts.begin(), shortcuts.end(), [](const auto& shortcut)
                {
                    return shortcut.commandId == ID_VIEW_PERFORMANCE_HUD;
                });
                Expect(performanceHudShortcut != shortcuts.end()
                           && performanceHudShortcut->virtualKey == static_cast<WORD>('P')
                           && performanceHudShortcut->modifiers == (FCONTROL | FSHIFT),
                       "Performance HUD shortcut catalog entry changed or is missing");
            Expect(controller.Handle(ID_VIEW_DETAILS) && detailsCallCount == 1,
                   "View command controller did not route fixed view commands");
            Expect(!controller.Handle(ID_FILE_OPEN_FOLDER),
                   "View command controller claimed a file command outside its ownership");
        }

        void RunCommandBarControllerScenario()
        {
            using hyperbrowse::ui::CommandBarController;
            using hyperbrowse::ui::MakeMenuMetrics;
            using namespace hyperbrowse::ui::command_ids;

            CommandBarController controller;
            controller.InitializeItems();
            controller.SetMenuButton(0, L"File", L'F', reinterpret_cast<HMENU>(static_cast<INT_PTR>(1)));
            controller.SetMenuButton(1, L"Edit", L'E', reinterpret_cast<HMENU>(static_cast<INT_PTR>(2)));
            controller.SetMenuButton(2, L"View", L'V', reinterpret_cast<HMENU>(static_cast<INT_PTR>(3)));
            controller.SetMenuButton(3, L"Tools", L'T', reinterpret_cast<HMENU>(static_cast<INT_PTR>(4)));
            controller.SetMenuButton(4, L"Help", L'H', reinterpret_cast<HMENU>(static_cast<INT_PTR>(5)));
            controller.Layout(900,
                              6,
                              MakeMenuMetrics(hyperbrowse::util::kDefaultAppTextSize),
                              nullptr,
                              [](HFONT, std::wstring_view text)
                              {
                                  return static_cast<int>(text.size() * 8);
                              });

            const auto& menuButtons = controller.MenuButtons();
            Expect(controller.Items().size() == 18, "Command-bar controller did not initialize toolbar items");
                 Expect(menuButtons.size() == 5, "Command-bar controller did not retain all five top-level menus");
            Expect(controller.MenuHitTest(menuButtons[0].rect.left + 1, menuButtons[0].rect.top + 1) == 0,
                   "Command-bar controller did not hit-test the first menu button");
                 Expect(controller.MenuHitTest(menuButtons[4].rect.left + 1, menuButtons[4].rect.top + 1) == 4,
                     "Command-bar controller did not hit-test the fifth menu button");

            const auto thumbnailItem = std::find_if(controller.Items().begin(),
                                                    controller.Items().end(),
                                                    [](const CommandBarController::ToolbarItem& item)
                                                    {
                                                        return item.commandId == ID_VIEW_THUMBNAILS;
                                                    });
            Expect(thumbnailItem != controller.Items().end(),
                   "Command-bar controller did not initialize the thumbnail toolbar item");
            Expect(controller.ToolbarHitTest(thumbnailItem->rect.left + 1, thumbnailItem->rect.top + 1)
                       == static_cast<int>(std::distance(controller.Items().begin(), thumbnailItem)),
                   "Command-bar controller did not hit-test a toolbar item");

            CommandBarController::ToolbarState state;
            state.canNavigateBack = true;
            state.canNavigateForward = true;
            state.thumbnailsChecked = true;
            state.thumbnailSizeEnabled = false;
            state.compareEnabled = true;
            state.selectionActionsEnabled = false;
            controller.UpdateItemStates(state);

            const auto findItem = [&controller](UINT commandId)
            {
                return std::find_if(controller.Items().begin(),
                                    controller.Items().end(),
                                    [commandId](const CommandBarController::ToolbarItem& item)
                                    {
                                        return item.commandId == commandId;
                                    });
            };
            const auto sortItem = findItem(ID_ACTION_SORT_MENU);
            const auto thumbnailSizeItem = findItem(ID_ACTION_THUMBNAIL_SIZE_MENU);
            const auto toolbarMetrics = MakeMenuMetrics(hyperbrowse::util::kDefaultAppTextSize);
            const int expectedDropdownWidth = toolbarMetrics.ScaleDip(toolbarMetrics.commandBarItemSizeDip)
                + toolbarMetrics.ScaleDip(toolbarMetrics.commandBarDropdownButtonExtraWidthDip);
            Expect(sortItem != controller.Items().end()
                       && thumbnailSizeItem != controller.Items().end()
                       && sortItem->kind == CommandBarController::ToolbarItemKind::IconDropdown
                       && thumbnailSizeItem->kind == CommandBarController::ToolbarItemKind::IconDropdown
                       && sortItem->rect.right - sortItem->rect.left == expectedDropdownWidth
                       && thumbnailSizeItem->rect.right - thumbnailSizeItem->rect.left == expectedDropdownWidth
                       && sortItem->rect.right < thumbnailSizeItem->rect.left,
                   "Sort and thumbnail-size dropdowns did not reserve balanced icon and chevron space");
            Expect(findItem(ID_VIEW_RECURSIVE) == controller.Items().end()
                       && findItem(ID_VIEW_THUMBNAILS)->checked,
                   "Command-bar controller did not apply toggle state");
            Expect(!findItem(ID_ACTION_THUMBNAIL_SIZE_MENU)->enabled
                       && findItem(ID_FILE_COMPARE_SELECTED)->enabled
                       && !findItem(ID_FILE_COPY_SELECTION)->enabled,
                   "Command-bar controller did not apply enabled state");

            const auto saveItem = findItem(ID_FILE_SAVE_CURRENT_FILTER);
            Expect(saveItem != controller.Items().end() && saveItem->iconName == "save"
                && saveItem->tooltip == L"Save Current Filter" && !saveItem->enabled,
                "Inline saved-search command did not expose its icon, tooltip, or disabled state");
            state.saveFilterEnabled = true;
            controller.UpdateItemStates(state);
            Expect(saveItem->enabled, "Inline saved-search command did not enable for an available filter");
            state.saveFilterEnabled = false;
            controller.UpdateItemStates(state);
            Expect(!saveItem->enabled, "Inline saved-search command remained enabled while unavailable");

            controller.Layout(2400,
                              6,
                              MakeMenuMetrics(hyperbrowse::util::kDefaultAppTextSize),
                              nullptr,
                              [](HFONT, std::wstring_view text)
                              {
                                  return static_cast<int>(text.size() * 8);
                              });
            const auto clearFilterItem = findItem(ID_ACTION_CLEAR_FILTER);
            Expect(clearFilterItem != controller.Items().end()
                       && clearFilterItem->kind == CommandBarController::ToolbarItemKind::FilterClear
                       && clearFilterItem->tooltip == L"Clear Filter"
                       && !clearFilterItem->enabled
                       && !IsRectEmpty(&clearFilterItem->rect),
                   "Clear-filter toolbar action did not expose its tooltip, bounds, or disabled state");
            state.clearFilterEnabled = true;
            controller.UpdateItemStates(state);
            const int clearFilterIndex = static_cast<int>(std::distance(controller.Items().begin(), clearFilterItem));
            Expect(clearFilterItem->enabled
                       && controller.ToolbarHitTest((clearFilterItem->rect.left + clearFilterItem->rect.right) / 2,
                                                    (clearFilterItem->rect.top + clearFilterItem->rect.bottom) / 2)
                           == clearFilterIndex,
                   "Clear-filter toolbar action did not become hit-testable with an active filter");
            state.clearFilterEnabled = false;
            controller.UpdateItemStates(state);
            Expect(!clearFilterItem->enabled
                       && controller.ToolbarHitTest((clearFilterItem->rect.left + clearFilterItem->rect.right) / 2,
                                                    (clearFilterItem->rect.top + clearFilterItem->rect.bottom) / 2)
                           == -1,
                   "Clear-filter toolbar action remained hit-testable without an active filter");

            for (const auto textSize : {hyperbrowse::util::AppTextSize::Small,
                                       hyperbrowse::util::AppTextSize::Medium,
                                       hyperbrowse::util::AppTextSize::Large})
            {
                for (const UINT dpi : {96U, 144U, 192U})
                {
                    const auto metrics = MakeMenuMetrics(textSize, dpi);
                    int expectedFilterLeft = -1;
                    for (const int width : {640, 900, 1600, 2400})
                    {
                        controller.Layout(width, 6, metrics, nullptr, [&](HFONT, std::wstring_view label)
                            { return metrics.ScaleDip(static_cast<int>(label.size() * 8)); });
                        const auto filter = std::find_if(controller.Items().begin(), controller.Items().end(),
                            [](const auto& item) { return item.kind == CommandBarController::ToolbarItemKind::FilterEdit; });
                        Expect(filter != controller.Items().end(), "Inline Save layout lost the filter edit");
                        const auto clear = findItem(ID_ACTION_CLEAR_FILTER);
                        Expect(clear != controller.Items().end()
                                   && clear->rect.left >= filter->rect.left
                                   && clear->rect.right <= filter->rect.right
                                   && clear->rect.top >= filter->rect.top
                                   && clear->rect.bottom <= filter->rect.bottom,
                               "Clear-filter action escaped the filter field bounds");
                        const int filterWidth = static_cast<int>(filter->rect.right - filter->rect.left);
                        const int filterMaxWidth = metrics.ScaleDip(metrics.commandBarFilterEditMaxWidthDip);
                        Expect(filterWidth <= filterMaxWidth, "Filter field exceeded its configured maximum width");
                        if (expectedFilterLeft < 0)
                            expectedFilterLeft = filter->rect.left;
                        else
                            Expect(filter->rect.left == expectedFilterLeft,
                                   "Filter field moved when the command-bar width changed");
                        if (width == 2400
                            && textSize == hyperbrowse::util::AppTextSize::Medium
                            && dpi == 96U)
                            Expect(filterWidth == filterMaxWidth,
                                   "Filter field did not reach its maximum width in a wide command bar");
                        if (!IsRectEmpty(&saveItem->rect))
                        {
                            Expect(filter->rect.right + metrics.ScaleDip(metrics.commandBarFilterSaveButtonGapDip)
                                       == saveItem->rect.left
                                && filter->rect.right - filter->rect.left >= metrics.ScaleDip(80)
                                && saveItem->rect.right <= width
                                && saveItem->rect.right - saveItem->rect.left == metrics.ScaleDip(metrics.commandBarItemSizeDip),
                                "Inline Save lost its padded position or fixed scaled bounds");
                        }
                        if (width == 640)
                            Expect(IsRectEmpty(&saveItem->rect), "Inline Save did not yield space in a narrow command bar");
                        if (width == 2400)
                            Expect(!IsRectEmpty(&saveItem->rect), "Inline Save did not return in a wide command bar");
                    }
                }
            }

            const CommandBarController::KeyboardInputState inactiveState{};
            const auto f10Result = controller.HandleKeyboardInput(WM_SYSKEYDOWN, VK_F10, inactiveState);
            Expect(f10Result.handled
                       && f10Result.action == CommandBarController::KeyboardAction::Activate
                       && f10Result.index == 0,
                   "Command-bar controller did not activate keyboard mode with F10");

            const CommandBarController::KeyboardInputState activeState{true, 0, false, false};
            const auto rightResult = controller.HandleKeyboardInput(WM_KEYDOWN, VK_RIGHT, activeState);
            Expect(rightResult.handled
                       && rightResult.action == CommandBarController::KeyboardAction::Activate
                       && rightResult.index == 1,
                   "Command-bar controller did not navigate menu buttons with Right");

            const auto mnemonicResult = controller.HandleKeyboardInput(WM_SYSKEYDOWN, L'V', activeState);
            Expect(mnemonicResult.handled
                       && mnemonicResult.action == CommandBarController::KeyboardAction::ActivateAndOpenMenu
                       && mnemonicResult.index == 2,
                   "Command-bar controller did not route menu mnemonics");

            const auto openResult = controller.HandleKeyboardInput(WM_KEYDOWN, VK_DOWN, activeState);
            Expect(openResult.handled
                       && openResult.action == CommandBarController::KeyboardAction::OpenMenu
                       && openResult.index == 0,
                   "Command-bar controller did not open the active menu");

            const auto escapeResult = controller.HandleKeyboardInput(WM_KEYDOWN, VK_ESCAPE, activeState);
            Expect(escapeResult.handled
                       && escapeResult.action == CommandBarController::KeyboardAction::Deactivate,
                   "Command-bar controller did not deactivate keyboard mode with Escape");
        }

        void RunMenuMetricsScenario()
        {
            using hyperbrowse::ui::CommandBarController;
            using hyperbrowse::ui::MakeMenuMetrics;
            using hyperbrowse::util::AppTextSize;

            const auto smallMetrics = MakeMenuMetrics(AppTextSize::Small, 96);
            const auto mediumMetrics = MakeMenuMetrics(AppTextSize::Medium, 144);
            const auto largeMetrics = MakeMenuMetrics(AppTextSize::Large, 192);
                 const auto mediumReferenceMetrics = MakeMenuMetrics(AppTextSize::Medium, 96);
                 Expect(hyperbrowse::util::EffectiveTextDpi(96) == hyperbrowse::util::kReferenceTextDpi
                      && hyperbrowse::util::EffectiveTextDpi(120) == hyperbrowse::util::kReferenceTextDpi
                      && hyperbrowse::util::EffectiveTextDpi(144) == hyperbrowse::util::kReferenceTextDpi
                      && hyperbrowse::util::EffectiveTextDpi(192) == 192,
                     "Effective text DPI did not preserve the shared tree reference policy");
                 Expect(mediumReferenceMetrics.dpi == mediumMetrics.dpi
                      && mediumReferenceMetrics.ScaleDip(mediumReferenceMetrics.popupItemHeightDip)
                          == mediumMetrics.ScaleDip(mediumMetrics.popupItemHeightDip)
                      && mediumReferenceMetrics.CommandBarHeight() == mediumMetrics.CommandBarHeight()
                      && largeMetrics.dpi > mediumMetrics.dpi,
                     "Menu metrics did not consume the shared effective text DPI");
            Expect(hyperbrowse::util::AppTextSizeScale(AppTextSize::Small)
                       < hyperbrowse::util::AppTextSizeScale(AppTextSize::Medium)
                       && hyperbrowse::util::AppTextSizeScale(AppTextSize::Medium)
                       < hyperbrowse::util::AppTextSizeScale(AppTextSize::Large),
                   "Application text-size scale factors were not ordered");
            Expect(hyperbrowse::util::AppTextTreeRowHeight(AppTextSize::Small)
                       < hyperbrowse::util::AppTextTreeRowHeight(AppTextSize::Medium)
                       && hyperbrowse::util::AppTextTreeRowHeight(AppTextSize::Medium)
                       < hyperbrowse::util::AppTextTreeRowHeight(AppTextSize::Large),
                   "Folder-tree row height did not scale with application text size");
            Expect(smallMetrics.ScaleDip(smallMetrics.popupItemHeightDip)
                       < mediumMetrics.ScaleDip(mediumMetrics.popupItemHeightDip)
                       && mediumMetrics.ScaleDip(mediumMetrics.popupItemHeightDip)
                       < largeMetrics.ScaleDip(largeMetrics.popupItemHeightDip),
                   "Menu item height did not increase monotonically with DPI and text size");
            Expect(smallMetrics.ScaleDip(smallMetrics.popupTextPaddingDip)
                       < largeMetrics.ScaleDip(largeMetrics.popupTextPaddingDip),
                   "Menu text padding did not compose DPI and application text size");
                 Expect(mediumMetrics.CommandBarHeight() > mediumMetrics.ScaleDip(mediumMetrics.commandBarItemSizeDip),
                     "Command-bar strip height did not leave vertical room around controls");
                 const auto smallTextMetrics = MakeMenuMetrics(AppTextSize::Small, 144);
                 const auto mediumTextMetrics = MakeMenuMetrics(AppTextSize::Medium, 144);
                 const auto largeTextMetrics = MakeMenuMetrics(AppTextSize::Large, 144);
                 Expect(smallTextMetrics.CommandBarHeight() < mediumTextMetrics.CommandBarHeight()
                            && mediumTextMetrics.CommandBarHeight() < largeTextMetrics.CommandBarHeight(),
                        "Command-bar strip height did not refresh for application text size");
                 Expect(smallMetrics.CommandBarHeight() < largeMetrics.CommandBarHeight(),
                     "Command-bar strip height did not scale with DPI and application text size");

            CommandBarController controller;
            controller.InitializeItems();
            controller.SetMenuButton(0, L"File", L'F', reinterpret_cast<HMENU>(static_cast<INT_PTR>(1)));
            controller.Layout(2400,
                              largeMetrics.ScaleDip(6),
                              largeMetrics,
                              nullptr,
                              [](HFONT, std::wstring_view text)
                              {
                                  return static_cast<int>(text.size() * 8);
                              });

            const auto& buttons = controller.MenuButtons();
            Expect(buttons[0].rect.bottom - buttons[0].rect.top == largeMetrics.ScaleDip(largeMetrics.commandBarItemSizeDip),
                   "Command-bar item height did not use shared menu metrics");
            Expect(buttons[0].rect.right > buttons[0].rect.left,
                   "Command-bar menu button received an invalid rectangle");
            for (const auto& item : controller.Items())
            {
                Expect(item.rect.right >= item.rect.left && item.rect.bottom >= item.rect.top,
                       "Command-bar item received an invalid scaled rectangle");
            }
        }

        void RunResponsivePanelSizingScenario()
        {
            using hyperbrowse::ui::MakeMenuMetrics;
            using hyperbrowse::ui::QuickAccessLayout;
            using hyperbrowse::util::AppTextSize;

            const auto smallMetrics = MakeMenuMetrics(AppTextSize::Small, 144);
            const auto mediumMetrics = MakeMenuMetrics(AppTextSize::Medium, 144);
            const auto largeMetrics = MakeMenuMetrics(AppTextSize::Large, 144);
            const int smallTabHeight = smallMetrics.ScaleDip(30);
            const int mediumTabHeight = mediumMetrics.ScaleDip(30);
            const int largeTabHeight = largeMetrics.ScaleDip(30);
            const int smallHistogramHeight = smallMetrics.ScaleDip(88);
            const int mediumHistogramHeight = mediumMetrics.ScaleDip(88);
            const int largeHistogramHeight = largeMetrics.ScaleDip(88);
            Expect(smallTabHeight < mediumTabHeight && mediumTabHeight < largeTabHeight
                       && smallHistogramHeight < mediumHistogramHeight
                       && mediumHistogramHeight < largeHistogramHeight,
                   "Details-panel tab and histogram dimensions did not scale with application text size");

            const auto buildLayout = [](int scale)
            {
                QuickAccessLayout::Input input;
                input.innerLeft = 10;
                input.innerRight = 360;
                input.top = 40;
                input.viewportTop = 80;
                input.panelBottom = 200;
                input.contentRight = 340;
                input.sortLabelWidth = 80;
                input.sortButtonGap = scale;
                input.sortButtonSize = scale * 4;
                input.metrics.headerHeight = scale * 4;
                input.metrics.rowHeight = scale * 10;
                input.metrics.labelTopInset = scale;
                input.metrics.labelHeight = scale * 3;
                input.metrics.metadataTopInset = scale * 5;
                input.metrics.metadataBottomInset = scale;
                input.metrics.buttonHeight = scale * 4;
                input.metrics.buttonTopInset = scale * 3;
                input.metrics.rowGap = scale;
                input.metrics.buttonWidth = scale * 8;
                input.metrics.buttonGap = scale;
                input.metrics.buttonRightInset = scale;
                input.metrics.removeButtonWidth = scale * 4;
                input.metrics.shortcutWidth = scale * 4;
                input.metrics.shortcutGap = scale;
                input.destinations = {
                    QuickAccessLayout::Destination{L"C:\\One", L"One", L"1 image", 2, true}};
                return QuickAccessLayout::Build(input);
            };

            const QuickAccessLayout::Result smallLayout = buildLayout(4);
            const QuickAccessLayout::Result largeLayout = buildLayout(7);
            Expect((smallLayout.sortButtonRect.bottom - smallLayout.sortButtonRect.top) == 16
                       && (largeLayout.sortButtonRect.bottom - largeLayout.sortButtonRect.top) == 28
                       && (smallLayout.rows[0].copyRect.bottom - smallLayout.rows[0].copyRect.top) == 16
                       && (largeLayout.rows[0].copyRect.bottom - largeLayout.rows[0].copyRect.top) == 28
                       && (largeLayout.rows[0].copyRect.right - largeLayout.rows[0].copyRect.left)
                              > (smallLayout.rows[0].copyRect.right - smallLayout.rows[0].copyRect.left),
                   "Quick Actions controls did not preserve scaled header and action dimensions");
        }

        void RunQuickAccessMenuBuilderScenario()
        {
            using hyperbrowse::ui::QuickAccessMenuBuilder;
            using namespace hyperbrowse::ui::command_ids;

            struct MenuHandles
            {
                HMENU fileMenu{};
                HMENU openRecentFolderMenu{};
                HMENU copySelectionToMenu{};
                HMENU moveSelectionToMenu{};

                ~MenuHandles()
                {
                    DestroyMenu(fileMenu);
                    DestroyMenu(openRecentFolderMenu);
                    DestroyMenu(copySelectionToMenu);
                    DestroyMenu(moveSelectionToMenu);
                }
            } menus{
                CreateMenu(),
                CreatePopupMenu(),
                CreatePopupMenu(),
                CreatePopupMenu()};

            Expect(menus.fileMenu && menus.openRecentFolderMenu && menus.copySelectionToMenu && menus.moveSelectionToMenu,
                   "Quick-access menu scenario could not create temporary menus");
            AppendMenuW(menus.fileMenu,
                        MF_STRING,
                        ID_FILE_TOGGLE_CURRENT_FOLDER_FAVORITE_DESTINATION,
                        L"Initial toggle label");

            const std::vector<std::wstring> recentFolders{L"C:\\Photos\\Trips", L"D:\\Archive"};
            const std::vector<std::wstring> favoriteDestinations{L"C:\\Destinations\\Keep"};
            const std::vector<std::wstring> recentDestinations{L"D:\\Destinations\\Recent"};
            QuickAccessMenuBuilder builder;
            builder.Refresh(menus.fileMenu,
                            menus.openRecentFolderMenu,
                            menus.copySelectionToMenu,
                            menus.moveSelectionToMenu,
                            true,
                            true,
                            true,
                            recentFolders,
                            favoriteDestinations,
                            recentDestinations);

            const auto menuText = [](HMENU menu, UINT position)
            {
                wchar_t text[512]{};
                const int length = GetMenuStringW(menu, position, text, static_cast<int>(std::size(text)), MF_BYPOSITION);
                return std::wstring(text, static_cast<std::size_t>((std::max)(length, 0)));
            };
            const auto menuState = [](HMENU menu, UINT position)
            {
                return GetMenuState(menu, position, MF_BYPOSITION);
            };

            Expect(menuText(menus.fileMenu, 0) == L"Remove Current Folder from Favorite &Destinations",
                   "Quick-access builder did not update the favorite toggle label");
            Expect(GetMenuItemCount(menus.openRecentFolderMenu) == 4
                       && GetMenuItemID(menus.openRecentFolderMenu, 0) == ID_FILE_OPEN_RECENT_FOLDER_BASE
                       && menuText(menus.openRecentFolderMenu, 0) == L"Trips (C:\\Photos\\Trips)"
                       && GetMenuItemID(menus.openRecentFolderMenu, 1) == ID_FILE_OPEN_RECENT_FOLDER_BASE + 1
                       && menuText(menus.openRecentFolderMenu, 1) == L"Archive (D:\\Archive)"
                       && GetMenuItemID(menus.openRecentFolderMenu, 3) == ID_FILE_CLEAR_RECENT_FOLDERS,
                   "Quick-access builder did not populate recent folders with stable commands and labels");
            Expect(GetMenuItemCount(menus.copySelectionToMenu) == 9
                       && GetMenuItemID(menus.copySelectionToMenu, 0) == ID_FILE_COPY_SELECTION_BROWSE
                       && GetMenuItemID(menus.copySelectionToMenu, 3) == ID_FILE_COPY_SELECTION_FAVORITE_BASE
                       && menuText(menus.copySelectionToMenu, 3) == L"Keep (C:\\Destinations\\Keep)"
                       && GetMenuItemID(menus.copySelectionToMenu, 6) == ID_FILE_COPY_SELECTION_RECENT_BASE
                       && menuText(menus.copySelectionToMenu, 6) == L"Recent (D:\\Destinations\\Recent)"
                       && GetMenuItemID(menus.copySelectionToMenu, 8) == ID_FILE_CLEAR_RECENT_DESTINATIONS,
                   "Quick-access builder did not populate copy destinations with stable commands and labels");
            Expect((menuState(menus.copySelectionToMenu, 0) & MF_GRAYED) == 0
                       && (menuState(menus.copySelectionToMenu, 2) & MF_GRAYED) != 0
                       && (menuState(menus.copySelectionToMenu, 3) & MF_GRAYED) == 0,
                   "Quick-access builder did not apply destination menu enabled states");
            Expect(GetMenuItemCount(menus.moveSelectionToMenu) == 9
                       && GetMenuItemID(menus.moveSelectionToMenu, 0) == ID_FILE_MOVE_SELECTION_BROWSE
                       && GetMenuItemID(menus.moveSelectionToMenu, 3) == ID_FILE_MOVE_SELECTION_FAVORITE_BASE
                       && GetMenuItemID(menus.moveSelectionToMenu, 6) == ID_FILE_MOVE_SELECTION_RECENT_BASE,
                   "Quick-access builder did not populate move destinations with the move command ranges");

            builder.Refresh(menus.fileMenu,
                            menus.openRecentFolderMenu,
                            menus.copySelectionToMenu,
                            menus.moveSelectionToMenu,
                            false,
                            false,
                            false,
                            {},
                            {},
                            {});
            Expect(menuText(menus.fileMenu, 0) == L"Add Current Folder to Favorite &Destinations"
                       && GetMenuItemCount(menus.openRecentFolderMenu) == 1
                       && menuText(menus.openRecentFolderMenu, 0) == L"(No recent folders)"
                       && (menuState(menus.openRecentFolderMenu, 0) & MF_GRAYED) != 0,
                   "Quick-access builder did not render the empty recent-folder state");
            Expect(GetMenuItemCount(menus.copySelectionToMenu) == 3
                       && GetMenuItemID(menus.copySelectionToMenu, 0) == ID_FILE_COPY_SELECTION_BROWSE
                       && (menuState(menus.copySelectionToMenu, 0) & MF_GRAYED) != 0
                       && menuText(menus.copySelectionToMenu, 2) == L"(No favorite or recent destinations)"
                       && (menuState(menus.copySelectionToMenu, 2) & MF_GRAYED) != 0,
                   "Quick-access builder did not render disabled empty destination state");
        }

        void RunDetailsPanelHistogramScenario()
        {
            using hyperbrowse::ui::DetailsPanelHistogram;

            BITMAPINFO bitmapInfo{};
            bitmapInfo.bmiHeader.biSize = sizeof(bitmapInfo.bmiHeader);
            bitmapInfo.bmiHeader.biWidth = 4;
            bitmapInfo.bmiHeader.biHeight = -1;
            bitmapInfo.bmiHeader.biPlanes = 1;
            bitmapInfo.bmiHeader.biBitCount = 32;
            bitmapInfo.bmiHeader.biCompression = BI_RGB;
            void* bits = nullptr;
            HBITMAP bitmap = CreateDIBSection(nullptr, &bitmapInfo, DIB_RGB_COLORS, &bits, nullptr, 0);
            Expect(bitmap && bits, "Details-panel histogram scenario could not create a test bitmap");

            auto* pixels = static_cast<RGBQUAD*>(bits);
            pixels[0] = RGBQUAD{0, 0, 255, 0};
            pixels[1] = RGBQUAD{0, 128, 0, 0};
            pixels[2] = RGBQUAD{255, 0, 0, 0};
            pixels[3] = RGBQUAD{255, 255, 255, 0};

            DetailsPanelHistogram::Result result;
            const bool computed = DetailsPanelHistogram::Compute(bitmap, &result);
            DeleteObject(bitmap);

            Expect(computed && result.visible && result.peak == 2
                       && result.red[63] == 2
                       && result.green[32] == 1
                       && result.green[63] == 1
                       && result.blue[63] == 2,
                   "Details-panel histogram did not preserve RGB bins and peak visibility");

            DetailsPanelHistogram::Result emptyResult;
            Expect(!DetailsPanelHistogram::Compute(nullptr, &emptyResult) && !emptyResult.visible && emptyResult.peak == 0,
                   "Details-panel histogram did not reset invalid input to an empty result");
        }

        void RunRightPaneHitTesterScenario()
        {
            using hyperbrowse::ui::RightPaneHitTester;

            const std::array<RECT, 3> tabRects{
                RECT{10, 10, 60, 30},
                RECT{64, 10, 114, 30},
                RECT{118, 10, 168, 30}};
            const RECT tabStripRect{10, 10, 168, 30};
            Expect(RightPaneHitTester::Tab(true, tabStripRect, tabRects, 20, 20) == 0
                       && RightPaneHitTester::Tab(true, tabStripRect, tabRects, 70, 20) == 1
                       && RightPaneHitTester::Tab(true, tabStripRect, tabRects, 60, 20) == -1
                       && RightPaneHitTester::Tab(false, tabStripRect, tabRects, 20, 20) == -1,
                   "Right-pane hit tester did not preserve tab visibility and edge behavior");

            const RECT closeButtonRect{120, 10, 138, 28};
            const RECT sortButtonRect{10, 40, 28, 58};
            Expect(RightPaneHitTester::CloseButton(true, closeButtonRect, 120, 10) == 0
                       && RightPaneHitTester::CloseButton(true, closeButtonRect, 138, 28) == -1
                       && RightPaneHitTester::CloseButton(false, closeButtonRect, 124, 14) == -1
                       && RightPaneHitTester::SortButton(sortButtonRect, 20, 50) == 0
                       && RightPaneHitTester::SortButton(sortButtonRect, 30, 50) == -1,
                   "Right-pane hit tester did not preserve close and sort button geometry");
        }

        void RunDetailsPanelLayoutScenario()
        {
            using hyperbrowse::ui::DetailsPanelLayout;

            DetailsPanelLayout::Input input;
            input.panelRect = RECT{100, 20, 420, 400};
            input.margin = 14;
            input.tabHeight = 30;
            input.tabGap = 10;
            input.tabButtonGap = 10;
            input.tabButtonHorizontalPadding = 16;
            input.tabMinButtonWidth = 96;
            input.closeButtonSize = 18;
            input.closeButtonMargin = 8;
            input.closeButtonGap = 8;
            input.tabLabelWidth = 70;
            input.titleHeight = 22;
            input.summaryHeight = 18;
            input.histogramHeight = 88;
            input.textTopGap = 14;
            input.fileDetailsActive = true;
            input.histogramVisible = true;

            const DetailsPanelLayout::Result result = DetailsPanelLayout::Build(input);
            Expect(result.tabRects[0].left == 114 && result.tabRects[0].top == 34
                       && result.tabRects[0].right == 198 && result.tabRects[1].left == 208 && result.tabRects[1].right == 292
                       && result.tabRects[2].left == 302 && result.tabRects[2].right == 386,
                   "Details-panel layout changed tab geometry");
            Expect(result.contentRect.left == 114 && result.contentRect.top == 74
                       && result.contentRect.right == 406 && result.contentRect.bottom == 386
                       && result.closeButtonRect.left == 394 && result.closeButtonRect.top == 28,
                   "Details-panel layout changed content or close-button geometry");
            Expect(result.histogramRect.left == 114 && result.histogramRect.top == 128
                       && result.histogramRect.right == 406 && result.histogramRect.bottom == 216
                       && result.textRect.top == 230 && result.textRect.bottom == 386,
                   "Details-panel layout changed histogram or text placement");

            input.panelRect = RECT{100, 20, 270, 200};
            input.histogramVisible = false;
            const DetailsPanelLayout::Result narrowResult = DetailsPanelLayout::Build(input);
            Expect(narrowResult.tabRects[0].right == 154 && narrowResult.tabRects[1].left == 164
                       && narrowResult.tabRects[1].right == 204
                       && narrowResult.tabRects[2].left == 214 && narrowResult.tabRects[2].right == 254
                       && IsRectEmpty(&narrowResult.closeButtonRect),
                   "Details-panel layout did not preserve narrow-panel tab and close-button behavior");
        }

        void RunDisplaySurfaceRecoveryPolicyScenario()
        {
            using hyperbrowse::ui::DisplaySurfaceRecoveryPolicy;

            DisplaySurfaceRecoveryPolicy policy;
            policy.BeginRetries();
            Expect(!policy.ShouldRelayout() && !policy.Exhausted(),
                   "Display-surface recovery policy did not reset before the first retry");
            Expect(policy.AdvanceRetry() == 1 && policy.ShouldRelayout() && !policy.Exhausted(),
                   "Display-surface recovery policy did not request relayout on the first retry");
            Expect(policy.AdvanceRetry() == 2 && !policy.ShouldRelayout() && !policy.Exhausted(),
                   "Display-surface recovery policy changed later retry behavior");
            Expect(policy.AdvanceRetry() == DisplaySurfaceRecoveryPolicy::kRetryLimit
                       && policy.Exhausted(),
                   "Display-surface recovery policy did not stop at its retry limit");

                 const std::array<std::uintptr_t, 2> viewerTargets{0x10, 0x20};
                 policy.SetViewerTargets(viewerTargets);
                 Expect(policy.ShouldRecoverViewer(0x10)
                      && policy.ShouldRecoverViewer(0x20)
                      && !policy.ShouldRecoverViewer(0x30),
                     "Display-surface recovery policy did not retain only viewers present at recovery start");
                 policy.ClearViewerTargets();
                 Expect(!policy.ShouldRecoverViewer(0x10),
                     "Display-surface recovery policy did not clear viewer targets");

            policy.BeginRetries();
            Expect(!policy.ShouldRelayout() && !policy.Exhausted() && policy.AdvanceRetry() == 1,
                   "Display-surface recovery policy did not reset after exhaustion");
        }

        void RunClipboardFileTransferScenario()
        {
            const std::vector<std::wstring> expectedPaths{
                L"C:\\Clipboard\\first.jpg",
                L"D:\\Clipboard\\second.png",
            };

            Expect(hyperbrowse::ui::CopyFilePathsToClipboard(nullptr, expectedPaths, true),
                   "Clipboard file transfer failed to publish a cut selection");
            DWORD preferredDropEffect = 0;
            const std::vector<std::wstring> cutPaths = hyperbrowse::ui::ReadClipboardFilePaths(
                nullptr,
                &preferredDropEffect);
            Expect(cutPaths == expectedPaths && preferredDropEffect == DROPEFFECT_MOVE,
                   "Clipboard file transfer did not preserve cut paths and move semantics");

            Expect(hyperbrowse::ui::CopyFilePathsToClipboard(nullptr, expectedPaths, false),
                   "Clipboard file transfer failed to publish a copied selection");
            preferredDropEffect = 0;
            const std::vector<std::wstring> copiedPaths = hyperbrowse::ui::ReadClipboardFilePaths(
                nullptr,
                &preferredDropEffect);
            Expect(copiedPaths == expectedPaths && preferredDropEffect == DROPEFFECT_COPY,
                   "Clipboard file transfer did not preserve copy paths and copy semantics");
        }

        void RunQuickAccessPathListScenario()
        {
            using hyperbrowse::ui::QuickAccessPathList;

            std::vector<std::wstring> paths{L"C:\\One"};
            Expect(!QuickAccessPathList::Insert(&paths, L"c:/one", 2, false)
                       && paths == std::vector<std::wstring>{L"C:\\One"},
                   "Quick Access path insertion did not suppress normalized duplicates");
            Expect(QuickAccessPathList::Insert(&paths, L"D:\\Two", 2, false)
                       && paths == std::vector<std::wstring>{L"C:\\One", L"D:\\Two"},
                   "Quick Access path insertion did not append within its cap");
            Expect(QuickAccessPathList::Insert(&paths, L"E:/Three", 2, true)
                       && paths == std::vector<std::wstring>{L"E:\\Three", L"C:\\One"},
                   "Quick Access path insertion did not move a new path to the front and trim the cap");

            const std::vector<std::wstring> deserialized = QuickAccessPathList::Deserialize(
                L"C:/One\r\nc:\\one\nD:\\Two\nE:\\Three",
                2);
            Expect(deserialized == std::vector<std::wstring>{L"C:\\One", L"D:\\Two"},
                   "Quick Access path deserialization changed normalization, duplicate, or cap behavior");
            Expect(QuickAccessPathList::Serialize(deserialized) == L"C:\\One\nD:\\Two",
                   "Quick Access path serialization changed list order or separators");
        }

        void RunWindowBoundsPersistenceScenario()
        {
            using hyperbrowse::ui::WindowBoundsPersistence;

            std::map<std::wstring, DWORD> values;
            const RECT originalBounds{-100, 50, 900, 750};
            Expect(WindowBoundsPersistence::Save(
                       originalBounds,
                       800,
                       600,
                       [&values](std::wstring_view valueName, DWORD value)
                       {
                           values[std::wstring(valueName)] = value;
                       }),
                   "Window-bounds persistence rejected a valid normal rectangle");

            const std::optional<RECT> restoredBounds = WindowBoundsPersistence::Load(
                [&values](std::wstring_view valueName, DWORD* value)
                {
                    const auto found = values.find(std::wstring(valueName));
                    if (found == values.end())
                    {
                        return false;
                    }

                    *value = found->second;
                    return true;
                });
            Expect(restoredBounds
                       && restoredBounds->left == originalBounds.left
                       && restoredBounds->top == originalBounds.top
                       && restoredBounds->right == originalBounds.right
                       && restoredBounds->bottom == originalBounds.bottom,
                   "Window-bounds persistence did not round-trip signed coordinates and dimensions");

            const RECT workArea{0, 0, 1920, 1080};
            const RECT visibleBounds{100, 100, 1100, 800};
            Expect(WindowBoundsPersistence::IsWithinWorkArea(visibleBounds, 800, 600, workArea)
                       && !WindowBoundsPersistence::IsWithinWorkArea(visibleBounds, 1200, 600, workArea)
                       && !WindowBoundsPersistence::IsWithinWorkArea(originalBounds, 800, 600, workArea),
                   "Window-bounds persistence changed minimum-size or work-area validation");

            values[L"WindowWidth"] = 0;
            Expect(!WindowBoundsPersistence::Load(
                        [&values](std::wstring_view valueName, DWORD* value)
                        {
                            const auto found = values.find(std::wstring(valueName));
                            if (found == values.end())
                            {
                                return false;
                            }

                            *value = found->second;
                            return true;
                        }),
                   "Window-bounds persistence accepted a non-positive stored width");

            values[L"WindowWidth"] = 100;
            values[L"WindowLeft"] = static_cast<DWORD>(std::numeric_limits<LONG>::max());
            Expect(!WindowBoundsPersistence::Load(
                        [&values](std::wstring_view valueName, DWORD* value)
                        {
                            const auto found = values.find(std::wstring(valueName));
                            if (found == values.end())
                            {
                                return false;
                            }

                            *value = found->second;
                            return true;
                        }),
                   "Window-bounds persistence accepted a rectangle whose right edge overflowed LONG");

            values.clear();
            Expect(!WindowBoundsPersistence::Save(
                       RECT{0, 0, 100, 100},
                       800,
                       600,
                       [&values](std::wstring_view valueName, DWORD value)
                       {
                           values[std::wstring(valueName)] = value;
                       })
                       && values.empty(),
                   "Window-bounds persistence wrote an undersized rectangle");
        }

        void RunSelectedPathPersistenceScenario()
        {
            using hyperbrowse::ui::SelectedPathPersistence;
            using hyperbrowse::ui::SelectedPathState;

            std::map<std::wstring, std::wstring> values;
            const SelectedPathState initialState{
                L"C:\\Pictures",
                L"C:\\Pictures\\selected.jpg",
            };
            SelectedPathPersistence::Save(
                initialState,
                [&values](std::wstring_view valueName, std::wstring_view value)
                {
                    values[std::wstring(valueName)] = value;
                },
                [&values](std::wstring_view valueName)
                {
                    values.erase(std::wstring(valueName));
                });

            const SelectedPathState restoredState = SelectedPathPersistence::Load(
                [&values](std::wstring_view valueName, std::wstring* value)
                {
                    const auto found = values.find(std::wstring(valueName));
                    if (found == values.end())
                    {
                        return false;
                    }

                    *value = found->second;
                    return true;
                });
            Expect(restoredState.folderPath == initialState.folderPath
                       && restoredState.imagePath == initialState.imagePath,
                   "Selected-path persistence did not restore folder and image paths");

            SelectedPathPersistence::Save(
                SelectedPathState{},
                [&values](std::wstring_view valueName, std::wstring_view value)
                {
                    values[std::wstring(valueName)] = value;
                },
                [&values](std::wstring_view valueName)
                {
                    values.erase(std::wstring(valueName));
                });
            Expect(values[L"SelectedFolderPath"] == initialState.folderPath
                       && !values.contains(L"SelectedImagePath"),
                   "Selected-path persistence overwrote a valid folder or retained a stale image path");
        }

        void RunFilingResumePersistenceScenario()
        {
            using hyperbrowse::ui::FilingResumePersistence;
            using hyperbrowse::ui::FilingResumePersistedState;
            using hyperbrowse::ui::FilingResumeRecord;

            std::map<std::wstring, std::wstring> values;
            FilingResumePersistedState state;
            for (int index = 0; index < 70; ++index)
            {
                state.records.push_back(FilingResumeRecord{
                    L"C:\\Pictures\\Folder" + std::to_wstring(index),
                    {7, static_cast<std::uint64_t>(index + 1)},
                    L"C:\\Pictures\\Folder" + std::to_wstring(index) + L"\\image.jpg",
                    {7, static_cast<std::uint64_t>(index + 100)},
                    index % 2});
            }

            FilingResumePersistence::Save(
                state,
                [&](std::wstring_view name, std::wstring_view value)
                {
                    values[std::wstring(name)] = std::wstring(value);
                },
                [&](std::wstring_view name)
                {
                    values.erase(std::wstring(name));
                });
            const FilingResumePersistedState loaded = FilingResumePersistence::Load(
                [&](std::wstring_view name, std::wstring* value)
                {
                    const auto found = values.find(std::wstring(name));
                    if (found == values.end())
                    {
                        return false;
                    }
                    *value = found->second;
                    return true;
                });

            Expect(loaded.records.size() == FilingResumePersistence::kMaxRecordCount,
                   "Filing resume persistence did not enforce the 64-record cap");
            Expect(loaded.records.front().folderPath == L"C:\\Pictures\\Folder0"
                       && loaded.records.front().operationType == 0,
                   "Filing resume persistence did not round-trip the newest record");
            Expect(loaded.records[1].folderPath == L"C:\\Pictures\\Folder1"
                       && loaded.records[1].targetIdentity.fileIndex == 101,
                   "Filing resume persistence did not preserve target identity data");

            values[L"FilingResumeCount"] = L"not-a-number";
            Expect(FilingResumePersistence::Load(
                       [&](std::wstring_view name, std::wstring* value)
                       {
                           const auto found = values.find(std::wstring(name));
                           if (found == values.end())
                           {
                               return false;
                           }
                           *value = found->second;
                           return true;
                       }).records.empty(),
                   "Filing resume persistence accepted an invalid record count");
        }

        void RunViewerSettingsPersistenceScenario()
        {
            using hyperbrowse::ui::ViewerSettingsPersistence;
            using hyperbrowse::ui::ViewerSettingsState;
            using hyperbrowse::viewer::EscapeKeyBehavior;
            using hyperbrowse::viewer::MouseWheelBehavior;
            using hyperbrowse::viewer::TransitionStyle;

            std::map<std::wstring, DWORD> values{
                {L"SlideshowIntervalMs", 5000},
                {L"SlideshowTransitionStyle", static_cast<DWORD>(TransitionStyle::Push)},
                {L"SlideshowTransitionDurationMs", 900},
                {L"UseSlideshowTransition", 1},
                {L"ViewerMouseWheelBehavior", static_cast<DWORD>(MouseWheelBehavior::Navigate)},
                {L"ViewerEscapeKeyBehavior", static_cast<DWORD>(EscapeKeyBehavior::ActualSize)},
                {L"InvertKeyboardPanning", 1},
                {L"ColorManagementEnabled", 0},
            };

            const ViewerSettingsState restored = ViewerSettingsPersistence::Load(
                [&values](std::wstring_view valueName, DWORD* value)
                {
                    const auto found = values.find(std::wstring(valueName));
                    if (found == values.end())
                    {
                        return false;
                    }

                    *value = found->second;
                    return true;
                });
            Expect(restored.slideshowIntervalMs == 5000
                       && restored.slideshowTransitionStyle == TransitionStyle::Push
                       && restored.slideshowTransitionDurationMs == 900
                       && restored.useSlideshowTransition
                       && restored.mouseWheelBehavior == MouseWheelBehavior::Navigate
                       && restored.escapeKeyBehavior == EscapeKeyBehavior::ActualSize
                       && restored.invertKeyboardPanning
                       && !restored.colorManagementEnabled,
                   "Viewer settings persistence did not restore valid viewer and slideshow values");

            values[L"SlideshowIntervalMs"] = 1;
            values[L"SlideshowTransitionStyle"] = 99;
            values[L"SlideshowTransitionDurationMs"] = 6001;
            values[L"ViewerMouseWheelBehavior"] = 99;
            values[L"ViewerEscapeKeyBehavior"] = 99;
            values[L"ColorManagementEnabled"] = 99;
            const ViewerSettingsState fallback = ViewerSettingsPersistence::Load(
                [&values](std::wstring_view valueName, DWORD* value)
                {
                    const auto found = values.find(std::wstring(valueName));
                    if (found == values.end())
                    {
                        return false;
                    }

                    *value = found->second;
                    return true;
                });
            Expect(fallback.slideshowIntervalMs == 3000
                       && fallback.slideshowTransitionStyle == TransitionStyle::Crossfade
                       && fallback.slideshowTransitionDurationMs == 350
                       && fallback.mouseWheelBehavior == MouseWheelBehavior::Zoom
                       && fallback.escapeKeyBehavior == EscapeKeyBehavior::Close
                       && fallback.colorManagementEnabled,
                   "Viewer settings persistence did not apply defaults to invalid persisted values");

            values.clear();
            ViewerSettingsPersistence::Save(
                restored,
                [&values](std::wstring_view valueName, DWORD value)
                {
                    values[std::wstring(valueName)] = value;
                });
            Expect(values[L"SlideshowIntervalMs"] == 5000
                       && values[L"SlideshowTransitionStyle"] == static_cast<DWORD>(TransitionStyle::Push)
                       && values[L"SlideshowTransitionDurationMs"] == 900
                       && values[L"UseSlideshowTransition"] == 1
                       && values[L"ViewerMouseWheelBehavior"] == static_cast<DWORD>(MouseWheelBehavior::Navigate)
                       && values[L"ViewerEscapeKeyBehavior"] == static_cast<DWORD>(EscapeKeyBehavior::ActualSize)
                       && values[L"InvertKeyboardPanning"] == 1
                       && values[L"ColorManagementEnabled"] == 0,
                   "Viewer settings persistence did not write the expected registry value contract");
            const auto defaults = ViewerSettingsPersistence::Load([](std::wstring_view, DWORD*) { return false; });
            Expect(defaults.colorManagementEnabled, "Missing color-management preference did not default on");
        }

        void RunBrowserPresentationPersistenceScenario()
        {
            using hyperbrowse::browser::BrowserSortMode;
            using hyperbrowse::browser::ThumbnailSizePreset;
            using hyperbrowse::ui::BrowserPresentationPersistence;
            using hyperbrowse::ui::BrowserPresentationState;
            using hyperbrowse::util::AppTextSize;

            std::map<std::wstring, DWORD> values{
                {L"LeftPaneWidth", 420},
                {L"BrowserMode", 1},
                {L"ThemeMode", 1},
                {L"AppTextSize", static_cast<DWORD>(AppTextSize::Large)},
                {L"ThumbnailSizePreset", static_cast<DWORD>(ThumbnailSizePreset::Pixels640)},
                {L"CompactThumbnailLayout", 0},
                {L"ThumbnailDetailsVisible", 0},
                {L"ShowSubfoldersInBrowser", 1},
                {L"SortMode", static_cast<DWORD>(BrowserSortMode::Tags)},
                {L"SortAscending", 0},
                {L"DetailsStripVisible", 0},
                {L"DetailsPanelWidth", 510},
            };

            const BrowserPresentationState restored = BrowserPresentationPersistence::Load(
                [&values](std::wstring_view valueName, DWORD* value)
                {
                    const auto found = values.find(std::wstring(valueName));
                    if (found == values.end())
                    {
                        return false;
                    }

                    *value = found->second;
                    return true;
                });
            Expect(restored.leftPaneWidth == 420
                       && restored.browserMode == 1
                       && restored.themeMode == 1
                       && restored.appTextSize == AppTextSize::Large
                       && restored.thumbnailSizePreset == ThumbnailSizePreset::Pixels640
                       && !restored.compactThumbnailLayout
                       && !restored.thumbnailDetailsVisible
                       && restored.showSubfoldersInBrowser
                       && restored.sortMode == BrowserSortMode::Tags
                       && !restored.sortAscending
                       && !restored.detailsStripVisible
                       && restored.detailsPanelWidth == 510,
                   "Browser presentation persistence did not restore valid settings");

            values[L"BrowserMode"] = 99;
            values[L"ThemeMode"] = 99;
            values[L"AppTextSize"] = 99;
            values[L"ThumbnailSizePreset"] = 999;
            values[L"SortMode"] = 999;
            const BrowserPresentationState fallback = BrowserPresentationPersistence::Load(
                [&values](std::wstring_view valueName, DWORD* value)
                {
                    const auto found = values.find(std::wstring(valueName));
                    if (found == values.end())
                    {
                        return false;
                    }

                    *value = found->second;
                    return true;
                });
            Expect(fallback.browserMode == 0
                       && fallback.themeMode == 0
                       && fallback.appTextSize == AppTextSize::Medium
                       && fallback.thumbnailSizePreset == ThumbnailSizePreset::Pixels192
                       && fallback.sortMode == BrowserSortMode::FileName,
                   "Browser presentation persistence did not apply defaults to invalid enum values");

            BrowserPresentationState toSave = restored;
            toSave.leftPaneWidth = 100;
            toSave.detailsPanelWidth = 100;
            values.clear();
            BrowserPresentationPersistence::Save(
                toSave,
                [&values](std::wstring_view valueName, DWORD value)
                {
                    values[std::wstring(valueName)] = value;
                });
            Expect(values[L"LeftPaneWidth"] == 250
                       && values[L"BrowserMode"] == 1
                       && values[L"ThemeMode"] == 1
                       && values[L"AppTextSize"] == static_cast<DWORD>(AppTextSize::Large)
                       && values[L"ThumbnailSizePreset"] == static_cast<DWORD>(ThumbnailSizePreset::Pixels640)
                       && values[L"CompactThumbnailLayout"] == 0
                       && values[L"ThumbnailDetailsVisible"] == 0
                       && values[L"ShowSubfoldersInBrowser"] == 1
                       && values[L"SortMode"] == static_cast<DWORD>(BrowserSortMode::Tags)
                       && values[L"SortAscending"] == 0
                       && values[L"DetailsStripVisible"] == 0
                       && values[L"DetailsPanelWidth"] == 540,
                   "Browser presentation persistence did not write the expected registry value contract");
        }

        void RunImageWorkflowPersistenceScenario()
        {
            using hyperbrowse::browser::RawJpegDisplayPreference;
            using hyperbrowse::ui::ImageWorkflowPersistence;
            using hyperbrowse::ui::ImageWorkflowState;

            std::map<std::wstring, DWORD> values{
                {L"NvJpegEnabled", 1},
                {L"LibRawOutOfProcessEnabled", 0},
                {L"RawJpegPairedOperationsEnabled", 1},
                {L"PairedRawJpegViewerPreference", static_cast<DWORD>(RawJpegDisplayPreference::Jpeg)},
                {L"DefaultViewerToSecondaryMonitor", 1},
            };

            const ImageWorkflowState restored = ImageWorkflowPersistence::Load(
                [&values](std::wstring_view valueName, DWORD* value)
                {
                    const auto found = values.find(std::wstring(valueName));
                    if (found == values.end())
                    {
                        return false;
                    }

                    *value = found->second;
                    return true;
                });
            Expect(restored.nvJpegEnabled
                       && !restored.libRawOutOfProcessEnabled
                       && restored.rawJpegPairedOperationsEnabled
                       && restored.pairedRawJpegViewerPreference == RawJpegDisplayPreference::Jpeg
                       && restored.defaultViewerToSecondaryMonitor,
                   "Image workflow persistence did not restore valid settings");

            values[L"PairedRawJpegViewerPreference"] = 99;
            const ImageWorkflowState fallback = ImageWorkflowPersistence::Load(
                [&values](std::wstring_view valueName, DWORD* value)
                {
                    const auto found = values.find(std::wstring(valueName));
                    if (found == values.end())
                    {
                        return false;
                    }

                    *value = found->second;
                    return true;
                });
            Expect(fallback.pairedRawJpegViewerPreference == RawJpegDisplayPreference::Raw,
                   "Image workflow persistence accepted an invalid RAW/JPEG preference");

            values.clear();
            const ImageWorkflowState defaults = ImageWorkflowPersistence::Load(
                [&values](std::wstring_view valueName, DWORD* value)
                {
                    const auto found = values.find(std::wstring(valueName));
                    if (found == values.end())
                    {
                        return false;
                    }

                    *value = found->second;
                    return true;
                });
            Expect(!defaults.nvJpegEnabled
                       && defaults.libRawOutOfProcessEnabled
                       && !defaults.rawJpegPairedOperationsEnabled
                       && defaults.pairedRawJpegViewerPreference == RawJpegDisplayPreference::Raw
                       && !defaults.defaultViewerToSecondaryMonitor,
                   "Image workflow persistence changed its missing-value defaults");

            values.clear();
            ImageWorkflowPersistence::Save(
                restored,
                [&values](std::wstring_view valueName, DWORD value)
                {
                    values[std::wstring(valueName)] = value;
                });
            Expect(values[L"NvJpegEnabled"] == 1
                       && values[L"LibRawOutOfProcessEnabled"] == 0
                       && values[L"RawJpegPairedOperationsEnabled"] == 1
                       && values[L"PairedRawJpegViewerPreference"] == static_cast<DWORD>(RawJpegDisplayPreference::Jpeg)
                       && values[L"DefaultViewerToSecondaryMonitor"] == 1,
                   "Image workflow persistence did not write the expected registry value contract");
        }

        void RunPerformanceSettingsPersistenceScenario()
        {
            using hyperbrowse::ui::PerformanceSettingsPersistence;
            using hyperbrowse::ui::PerformanceSettingsState;
            using hyperbrowse::util::ResourceProfile;

            std::map<std::wstring, DWORD> dwordValues{
                {L"PersistentThumbnailCacheEnabled", 0},
                {L"ResourceProfile", static_cast<DWORD>(ResourceProfile::Aggressive)},
                {L"PrefetchDepthOverride", 8},
                {L"ShowPressureStateInStatusBar", 1},
                {L"CloseMainWindowOnEscape", 1},
            };
            std::map<std::wstring, std::uint64_t> qwordValues{
                {L"ThumbnailCacheCapacityOverrideBytes", 4096},
                {L"MetadataCacheCapacityOverrideEntries", 42},
                {L"PersistentThumbnailCacheCapBytes", 8192},
            };

            const PerformanceSettingsState restored = PerformanceSettingsPersistence::Load(
                [&dwordValues](std::wstring_view valueName, DWORD* value)
                {
                    const auto found = dwordValues.find(std::wstring(valueName));
                    if (found == dwordValues.end())
                    {
                        return false;
                    }

                    *value = found->second;
                    return true;
                },
                [&qwordValues](std::wstring_view valueName, std::uint64_t* value)
                {
                    const auto found = qwordValues.find(std::wstring(valueName));
                    if (found == qwordValues.end())
                    {
                        return false;
                    }

                    *value = found->second;
                    return true;
                });
            Expect(!restored.persistentThumbnailCacheEnabled
                       && restored.resourceProfile == ResourceProfile::Aggressive
                       && restored.prefetchDepthOverride == 8
                       && restored.thumbnailCacheCapacityOverrideBytes == 4096
                       && restored.metadataCacheCapacityOverrideEntries == 42
                       && restored.persistentThumbnailCacheCapacityOverrideBytes == 8192
                       && restored.showPressureStateInStatusBar
                       && restored.closeMainWindowOnEscape,
                   "Performance settings persistence did not restore valid settings");

            dwordValues[L"ResourceProfile"] = 99;
            dwordValues[L"PrefetchDepthOverride"] = 99;
            qwordValues[L"ThumbnailCacheCapacityOverrideBytes"] = std::numeric_limits<std::uint64_t>::max();
            qwordValues[L"PersistentThumbnailCacheCapBytes"] = std::numeric_limits<std::uint64_t>::max();
            const PerformanceSettingsState fallback = PerformanceSettingsPersistence::Load(
                [&dwordValues](std::wstring_view valueName, DWORD* value)
                {
                    const auto found = dwordValues.find(std::wstring(valueName));
                    if (found == dwordValues.end())
                    {
                        return false;
                    }

                    *value = found->second;
                    return true;
                },
                [&qwordValues](std::wstring_view valueName, std::uint64_t* value)
                {
                    const auto found = qwordValues.find(std::wstring(valueName));
                    if (found == qwordValues.end())
                    {
                        return false;
                    }

                    *value = found->second;
                    return true;
                });
            Expect(fallback.resourceProfile == ResourceProfile::Balanced
                       && fallback.prefetchDepthOverride == 0
                       && fallback.thumbnailCacheCapacityOverrideBytes == std::numeric_limits<std::size_t>::max()
                       && fallback.persistentThumbnailCacheCapacityOverrideBytes == std::numeric_limits<std::size_t>::max(),
                   "Performance settings persistence did not preserve defaults or saturate cache capacity");

            dwordValues.clear();
            qwordValues.clear();
            PerformanceSettingsState toSave = restored;
            toSave.prefetchDepthOverride = 99;
            PerformanceSettingsPersistence::Save(
                toSave,
                [&dwordValues](std::wstring_view valueName, DWORD value)
                {
                    dwordValues[std::wstring(valueName)] = value;
                },
                [&qwordValues](std::wstring_view valueName, std::uint64_t value)
                {
                    qwordValues[std::wstring(valueName)] = value;
                });
            Expect(dwordValues[L"PersistentThumbnailCacheEnabled"] == 0
                       && dwordValues[L"ResourceProfile"] == static_cast<DWORD>(ResourceProfile::Aggressive)
                       && dwordValues[L"PrefetchDepthOverride"] == 16
                       && dwordValues[L"ShowPressureStateInStatusBar"] == 1
                       && dwordValues[L"CloseMainWindowOnEscape"] == 1
                       && qwordValues[L"ThumbnailCacheCapacityOverrideBytes"] == 4096
                       && qwordValues[L"MetadataCacheCapacityOverrideEntries"] == 42
                       && qwordValues[L"PersistentThumbnailCacheCapBytes"] == 8192
                       && qwordValues[L"PersistentThumbnailCacheCapacityOverrideBytes"] == 8192,
                   "Performance settings persistence did not write the expected value contract");
        }

        void RunPairedRawJpegResolverScenario()
        {
            using hyperbrowse::browser::BrowserItem;
            using hyperbrowse::browser::RawJpegDisplayPreference;
            using hyperbrowse::ui::PairedRawJpegResolver;

            const auto MakeItem = [](std::wstring path, std::wstring fileType)
            {
                BrowserItem item;
                item.filePath = std::move(path);
                item.fileType = std::move(fileType);
                return item;
            };
            const BrowserItem jpeg = MakeItem(L"C:\\Pictures\\IMG_001.jpg", L"jpg");
            const BrowserItem raw = MakeItem(L"C:\\Pictures\\IMG_001.nef", L"nef");
            const BrowserItem otherFolderRaw = MakeItem(L"C:\\Other\\IMG_001.nef", L"nef");
            const std::vector<BrowserItem> candidates{jpeg, raw, otherFolderRaw};
            const PairedRawJpegResolver::FolderPathEquals folderPathEquals =
                [](std::wstring_view lhs, std::wstring_view rhs)
                {
                    return util::NormalizedPathEquals(lhs, rhs);
                };

            const BrowserItem rawPreferred = PairedRawJpegResolver::Resolve(
                jpeg,
                candidates,
                RawJpegDisplayPreference::Raw,
                folderPathEquals);
            Expect(rawPreferred.filePath == raw.filePath,
                   "Paired RAW/JPEG resolver did not select the same-folder RAW companion");

            const BrowserItem jpegPreferred = PairedRawJpegResolver::Resolve(
                raw,
                candidates,
                RawJpegDisplayPreference::Jpeg,
                folderPathEquals);
            Expect(jpegPreferred.filePath == jpeg.filePath,
                   "Paired RAW/JPEG resolver did not select the same-folder JPEG companion");

            const BrowserItem unmatched = MakeItem(L"C:\\Pictures\\IMG_002.jpg", L"jpg");
            const BrowserItem unchanged = PairedRawJpegResolver::Resolve(
                unmatched,
                candidates,
                RawJpegDisplayPreference::Raw,
                folderPathEquals);
            Expect(unchanged.filePath == unmatched.filePath,
                   "Paired RAW/JPEG resolver matched a different stem or folder");

            const std::vector<BrowserItem> resolved = PairedRawJpegResolver::ResolveItems(
                std::vector<BrowserItem>{jpeg, raw},
                candidates,
                RawJpegDisplayPreference::Raw,
                folderPathEquals);
            Expect(resolved.size() == 2
                       && resolved[0].filePath == raw.filePath
                       && resolved[1].filePath == raw.filePath,
                   "Paired RAW/JPEG resolver did not apply the preference to all items");

            const std::vector<std::wstring> expanded = PairedRawJpegResolver::ExpandPaths(
                std::vector<std::wstring>{jpeg.filePath},
                candidates,
                folderPathEquals);
            Expect(expanded.size() == 2
                       && expanded[0] == jpeg.filePath
                       && expanded[1] == raw.filePath,
                   "Paired RAW/JPEG resolver did not expand a same-folder companion");

            const std::vector<std::wstring> alreadyExpanded = PairedRawJpegResolver::ExpandPaths(
                std::vector<std::wstring>{jpeg.filePath, raw.filePath},
                candidates,
                folderPathEquals);
            Expect(alreadyExpanded.size() == 2,
                   "Paired RAW/JPEG resolver duplicated an existing companion path");
        }

        void RunWindowTimerRouterScenario()
        {
            using hyperbrowse::ui::WindowTimerRouter;

            WindowTimerRouter router;
            std::vector<int> calls;
            router.Configure(
                WindowTimerRouter::TimerIds{11, 12, 13, 14},
                WindowTimerRouter::Handlers{
                    [&]() -> std::optional<LRESULT>
                    {
                        calls.push_back(11);
                        return 101;
                    },
                    [&]() -> std::optional<LRESULT>
                    {
                        calls.push_back(12);
                        return 102;
                    },
                    [&]() -> std::optional<LRESULT>
                    {
                        calls.push_back(13);
                        return std::nullopt;
                    },
                    [&]() -> std::optional<LRESULT>
                    {
                        calls.push_back(14);
                        return 104;
                    }});

            Expect(router.Handle(11) == std::optional<LRESULT>(101)
                       && router.Handle(12) == std::optional<LRESULT>(102)
                       && !router.Handle(13).has_value()
                       && router.Handle(14) == std::optional<LRESULT>(104)
                       && !router.Handle(99).has_value()
                       && calls == std::vector<int>{11, 12, 13, 14},
                   "Window timer router changed timer-ID dispatch or inactive-handler behavior");
        }

        void RunWindowAsyncMessageRouterScenario()
        {
            using hyperbrowse::ui::WindowAsyncMessageRouter;

            WindowAsyncMessageRouter router;
            std::vector<int> calls;
            WindowAsyncMessageRouter::Handlers handlers;
            handlers.onExternalLaunch = [&](LPARAM lParam)
            {
                calls.push_back(static_cast<int>(lParam));
                return static_cast<LRESULT>(201);
            };
            handlers.onMemoryPressureSampled = [&](LPARAM lParam)
            {
                calls.push_back(static_cast<int>(lParam) + 10);
                return static_cast<LRESULT>(202);
            };
            handlers.onPersistentThumbnailCacheMaintenance = [&](WPARAM wParam)
            {
                calls.push_back(static_cast<int>(wParam) + 20);
                return static_cast<LRESULT>(203);
            };
            handlers.onDeferredMenuState = [&]()
            {
                calls.push_back(24);
                return static_cast<LRESULT>(204);
            };
            router.Configure(
                WindowAsyncMessageRouter::MessageIds{21, 22, 23, 24},
                std::move(handlers));

            Expect(router.Handle(21, 0, 1) == std::optional<LRESULT>(201)
                       && router.Handle(22, 0, 2) == std::optional<LRESULT>(202)
                       && router.Handle(23, 3, 0) == std::optional<LRESULT>(203)
                       && router.Handle(24, 0, 0) == std::optional<LRESULT>(204)
                       && !router.Handle(99, 0, 0).has_value()
                       && calls == std::vector<int>{1, 12, 23, 24},
                   "Window async message router changed private-message dispatch or argument forwarding");

            WindowAsyncMessageRouter legacyRouter;
            legacyRouter.Configure(WindowAsyncMessageRouter::Handlers{});
            Expect(!legacyRouter.Handle(0, 0, 0).has_value(),
                   "Window async message router changed zero-ID legacy configuration behavior");
        }

        void RunQuickAccessLayoutScenario()
        {
            using hyperbrowse::ui::QuickAccessLayout;

            QuickAccessLayout::Input input;
            input.innerLeft = 10;
            input.innerRight = 260;
            input.top = 40;
            input.viewportTop = 70;
            input.panelBottom = 160;
            input.contentRight = 240;
            input.scrollOffset = 12;
            input.sortLabelWidth = 80;
            input.sortButtonGap = 6;
            input.sortButtonSize = 18;
            input.metrics.headerHeight = 18;
            input.metrics.rowHeight = 40;
            input.metrics.labelTopInset = 5;
            input.metrics.labelHeight = 15;
            input.metrics.metadataTopInset = 24;
            input.metrics.metadataBottomInset = 6;
            input.metrics.buttonHeight = 28;
            input.metrics.buttonTopInset = 6;
            input.metrics.rowGap = 6;
            input.metrics.buttonWidth = 56;
            input.metrics.buttonGap = 8;
            input.metrics.buttonRightInset = 8;
            input.metrics.removeButtonWidth = 24;
            input.metrics.shortcutWidth = 24;
            input.metrics.shortcutGap = 8;
            input.destinations = {
                QuickAccessLayout::Destination{L"C:\\One", L"One", L"1 image", 2, true},
                QuickAccessLayout::Destination{L"D:\\Two", L"Two", L"2 images", -1, true},
            };

            const QuickAccessLayout::Result result = QuickAccessLayout::Build(input);
            Expect(result.panelRect.left == 10 && result.panelRect.top == 40
                       && result.panelRect.right == 260 && result.panelRect.bottom == 160,
                   "Quick Actions layout did not preserve the panel bounds");
            Expect(result.viewportRect.left == 10 && result.viewportRect.top == 70
                       && result.viewportRect.right == 240 && result.viewportRect.bottom == 160,
                   "Quick Actions layout did not preserve the viewport bounds");
            Expect(result.sortButtonRect.left == 96 && result.sortButtonRect.top == 40
                       && result.sortButtonRect.right == 114 && result.sortButtonRect.bottom == 58,
                   "Quick Actions layout did not place the sort button from the header label");
            Expect(result.rows.size() == 2
                       && result.rows[0].destinationPath == L"C:\\One"
                       && result.rows[0].rowRect.top == 58
                       && result.rows[1].rowRect.top == 104
                       && result.rows[0].copyRect.left == 80
                       && result.rows[0].moveRect.left == 144
                       && result.rows[0].removeRect.left == 208
                       && result.rows[0].shortcutRect.left == 48,
                   "Quick Actions layout did not preserve scrolled row and control geometry");
        }

        void RunQuickAccessDestinationBuilderScenario()
        {
            using hyperbrowse::ui::QuickAccessDestinationBuilder;

            const std::vector<std::wstring> favoriteDestinations = {
                L"C:\\Favorites\\One",
                L"D:\\Favorites\\Two",
            };
            const std::vector<hyperbrowse::ui::QuickAccessLayout::Destination> destinations =
                QuickAccessDestinationBuilder::Build(
                    favoriteDestinations,
                    [](std::wstring_view destinationPath)
                    {
                        return std::wstring(L"Metadata: ") + std::wstring(destinationPath);
                    },
                    [](std::wstring_view destinationPath) -> std::optional<int>
                    {
                        return destinationPath == L"C:\\Favorites\\One"
                            ? std::optional<int>(2)
                            : std::nullopt;
                    });

            Expect(destinations.size() == 2
                       && destinations[0].destinationPath == L"C:\\Favorites\\One"
                       && destinations[0].displayLabel == L"One (C:\\Favorites\\One)"
                       && destinations[0].metadataLabel == L"Metadata: C:\\Favorites\\One"
                       && destinations[0].assignedShortcut == 2
                       && destinations[0].favorite
                       && destinations[1].destinationPath == L"D:\\Favorites\\Two"
                       && destinations[1].assignedShortcut == -1
                       && destinations[1].favorite,
                   "Quick Actions destination builder changed the layout snapshot contract");
        }

        void RunBrowserItemScopeCollectorScenario()
        {
            using hyperbrowse::browser::BrowserItem;
            using hyperbrowse::ui::BrowserItemScopeCollector;

            std::vector<BrowserItem> modelItems = {
                BrowserItem{L"zero.jpg", L"C:\\Images\\zero.jpg", L"JPG", L"", 0, 10},
                BrowserItem{L"one.jpg", L"C:\\Images\\one.jpg", L"JPG", L"", 1, 20},
                BrowserItem{L"two.jpg", L"C:\\Images\\two.jpg", L"JPG", L"", 2, 30},
            };
            BrowserItem directoryItem{L"subfolder", L"C:\\Images\\subfolder", L"Folder", L"", 3, 0};
            directoryItem.isDirectory = true;
            modelItems.push_back(directoryItem);
            const std::vector<int> orderedModelIndices = {2, 0, 3, 99, -1};
            const std::vector<int> selectedModelIndices = {1, 2, 3, -1};

            const std::vector<BrowserItem> selectedItems = BrowserItemScopeCollector::Collect({
                modelItems,
                orderedModelIndices,
                selectedModelIndices,
                true,
                true});
            Expect(selectedItems.size() == 2
                       && selectedItems[0].filePath == L"C:\\Images\\one.jpg"
                       && selectedItems[1].filePath == L"C:\\Images\\two.jpg",
                   "Browser item scope collector changed ordered selection filtering");

            const std::vector<BrowserItem> orderedItems = BrowserItemScopeCollector::Collect({
                modelItems,
                orderedModelIndices,
                selectedModelIndices,
                false,
                true});
            Expect(orderedItems.size() == 2
                       && orderedItems[0].filePath == L"C:\\Images\\two.jpg"
                       && orderedItems[1].filePath == L"C:\\Images\\zero.jpg",
                   "Browser item scope collector changed ordered model filtering");

            const std::vector<BrowserItem> fallbackItems = BrowserItemScopeCollector::Collect({
                modelItems,
                {},
                {},
                false,
                false});
            Expect(fallbackItems.size() == 3
                       && fallbackItems[0].filePath == L"C:\\Images\\zero.jpg"
                       && fallbackItems[2].filePath == L"C:\\Images\\two.jpg",
                   "Browser item scope collector changed empty-order fallback behavior");
        }

        void RunFolderTreeDropPolicyScenario()
        {
            using hyperbrowse::ui::FolderTreeDropPolicy;

            Expect(FolderTreeDropPolicy::IsValid({
                       L"C:\\Images\\Source",
                       L"C:\\Images\\Destination",
                       L"C:\\Images",
                       true,
                       true}),
                   "Folder tree drop policy rejected a valid destination");
            Expect(!FolderTreeDropPolicy::IsValid({
                        L"C:\\Images\\Source",
                        L"C:\\Images\\Destination",
                        L"C:\\Images",
                        false,
                        true})
                       && !FolderTreeDropPolicy::IsValid({
                           L"C:\\Images\\Source",
                           L"D:\\Images\\Destination",
                           L"C:\\Images",
                           true,
                           false}),
                   "Folder tree drop policy accepted an unavailable or cross-drive destination");
            Expect(!FolderTreeDropPolicy::IsValid({
                        L"C:\\Images\\Source",
                        L"C:\\Images\\Source",
                        L"C:\\Images",
                        true,
                        true})
                       && !FolderTreeDropPolicy::IsValid({
                           L"C:\\Images\\Source",
                           L"C:\\Images\\Source\\Child",
                           L"C:\\Images",
                           true,
                           true})
                       && !FolderTreeDropPolicy::IsValid({
                           L"C:\\Images\\Source",
                           L"C:\\Images",
                           L"C:\\Images",
                           true,
                           true}),
                   "Folder tree drop policy accepted self, child, or parent destinations");
        }

        void RunSelectionRatingPolicyScenario()
        {
            using hyperbrowse::ui::SelectionRatingPolicy;

            const std::vector<int> emptyRatings;
            const std::vector<int> commonRatings = {4, 4, 4};
            const std::vector<int> clampedRatings = {-2, 0, 0};
            const std::vector<int> saturatedRatings = {7, 5, 9};
            const std::vector<int> mixedRatings = {2, 3};
            Expect(SelectionRatingPolicy::CommonRating(emptyRatings) == -1
                       && SelectionRatingPolicy::CommonRating(commonRatings) == 4
                       && SelectionRatingPolicy::CommonRating(clampedRatings) == 0
                       && SelectionRatingPolicy::CommonRating(saturatedRatings) == 5
                       && SelectionRatingPolicy::CommonRating(mixedRatings) == -1,
                   "Selection rating policy changed common and mixed-rating behavior");
        }

        void RunThumbnailRatingKeyPolicyScenario()
        {
            using hyperbrowse::browser::ThumbnailRatingKeyPolicy;

            for (int rating = 0; rating <= 5; ++rating)
            {
                const UINT topRowKey = static_cast<UINT>('0' + rating);
                const UINT numpadKey = static_cast<UINT>(VK_NUMPAD0 + rating);
                Expect(ThumbnailRatingKeyPolicy::RatingFromVirtualKey(topRowKey, false, false, false) == rating,
                       "Top-row thumbnail rating key did not map to its rating");
                Expect(ThumbnailRatingKeyPolicy::RatingFromVirtualKey(numpadKey, false, false, false) == rating,
                       "Numpad thumbnail rating key did not map to its rating");
            }

            Expect(!ThumbnailRatingKeyPolicy::RatingFromVirtualKey('6', false, false, false)
                       && !ThumbnailRatingKeyPolicy::RatingFromVirtualKey(VK_NUMPAD6, false, false, false)
                       && !ThumbnailRatingKeyPolicy::RatingFromVirtualKey(VK_F1, false, false, false),
                   "An unsupported key was mapped to a thumbnail rating");
            Expect(!ThumbnailRatingKeyPolicy::RatingFromVirtualKey('3', true, false, false)
                       && !ThumbnailRatingKeyPolicy::RatingFromVirtualKey('3', false, true, false)
                       && !ThumbnailRatingKeyPolicy::RatingFromVirtualKey('3', false, false, true)
                       && !ThumbnailRatingKeyPolicy::RatingFromVirtualKey(VK_NUMPAD3, true, false, false)
                       && !ThumbnailRatingKeyPolicy::RatingFromVirtualKey(VK_NUMPAD3, false, true, false)
                       && !ThumbnailRatingKeyPolicy::RatingFromVirtualKey(VK_NUMPAD3, false, false, true),
                   "A modified key was mapped to a thumbnail rating");
        }

        void RunViewerItemSelectionPolicyScenario()
        {
            using hyperbrowse::browser::BrowserItem;
            using hyperbrowse::ui::ViewerItemSelectionPolicy;

            const std::vector<BrowserItem> modelItems = {
                BrowserItem{L"zero.jpg", L"C:\\Images\\zero.jpg", L"JPG", L"", 0, 10},
                BrowserItem{L"one.jpg", L"C:\\Images\\one.jpg", L"JPG", L"", 1, 20},
                BrowserItem{L"two.jpg", L"C:\\Images\\two.jpg", L"JPG", L"", 2, 30},
            };
            const std::vector<int> orderedModelIndices = {2, 0, 99, -1};

            const ViewerItemSelectionPolicy::Result selected = ViewerItemSelectionPolicy::Build({
                modelItems,
                orderedModelIndices,
                0,
                {},
                {},
                -1});
            Expect(selected.items.size() == 2
                       && selected.selectedIndex == 1
                       && selected.items[0].filePath == L"C:\\Images\\two.jpg"
                       && selected.items[1].filePath == L"C:\\Images\\zero.jpg",
                   "Viewer item selection policy changed ordered model selection");

            const ViewerItemSelectionPolicy::Result preferred = ViewerItemSelectionPolicy::Build({
                modelItems,
                orderedModelIndices,
                -1,
                L"C:\\Images\\zero.jpg",
                L"C:\\Images\\two.jpg",
                0});
            Expect(preferred.selectedIndex == 1,
                   "Viewer item selection policy did not prefer the requested path");

            const ViewerItemSelectionPolicy::Result currentPath = ViewerItemSelectionPolicy::Build({
                modelItems,
                orderedModelIndices,
                -1,
                L"C:\\Images\\missing.jpg",
                L"C:\\Images\\two.jpg",
                1});
            Expect(currentPath.selectedIndex == 0,
                   "Viewer item selection policy did not preserve the current path");

            const ViewerItemSelectionPolicy::Result fallback = ViewerItemSelectionPolicy::Build({
                modelItems,
                {},
                -1,
                L"C:\\Images\\missing.jpg",
                L"C:\\Images\\also-missing.jpg",
                2});
            Expect(fallback.items.size() == 3 && fallback.selectedIndex == 2,
                   "Viewer item selection policy changed index fallback behavior");

            BrowserItem directory{L"folder", L"C:\\Images\\folder"};
            directory.isDirectory = true;
            const std::vector<BrowserItem> modelItemsWithDirectory = {
                directory,
                BrowserItem{L"first.jpg", L"C:\\Images\\first.jpg", L"JPG", L"", 3, 30},
                BrowserItem{L"second.jpg", L"C:\\Images\\second.jpg", L"JPG", L"", 4, 40},
            };
            const std::vector<int> orderedModelIndicesWithDirectory = {0, 1, 2};
            const ViewerItemSelectionPolicy::Result filtered = ViewerItemSelectionPolicy::Build({
                modelItemsWithDirectory,
                orderedModelIndicesWithDirectory,
                1,
                {},
                {},
                -1});
            Expect(filtered.items.size() == 2
                       && filtered.selectedIndex == 0
                       && filtered.items[0].filePath == L"C:\\Images\\first.jpg"
                       && filtered.items[1].filePath == L"C:\\Images\\second.jpg",
                   "Viewer item selection policy did not remove directories or remap the selected image");
        }

        void RunItemNumberNavigationPolicyScenario()
        {
            int zeroBasedIndex = -1;
            Expect(hyperbrowse::ui::TryParseItemNumber(L"1", 4, &zeroBasedIndex)
                       && zeroBasedIndex == 0,
                   "Item-number navigation did not map the first item");
            Expect(hyperbrowse::ui::TryParseItemNumber(L"004", 4, &zeroBasedIndex)
                       && zeroBasedIndex == 3,
                   "Item-number navigation did not accept a zero-padded last item");
            Expect(!hyperbrowse::ui::TryParseItemNumber(L"0", 4, &zeroBasedIndex)
                       && !hyperbrowse::ui::TryParseItemNumber(L"5", 4, &zeroBasedIndex)
                       && !hyperbrowse::ui::TryParseItemNumber(L"abc", 4, &zeroBasedIndex)
                       && !hyperbrowse::ui::TryParseItemNumber(L"999999999999999999999", 4, &zeroBasedIndex),
                   "Item-number navigation accepted invalid or out-of-range input");
        }

         void RunViewerPendingOperationStateScenario()
         {
             using hyperbrowse::services::FileOperationType;
             using hyperbrowse::ui::ViewerPendingOperationState;

             ViewerPendingOperationState state;
             ViewerPendingOperationState::DeleteRequest firstDelete;
             firstDelete.viewerHwnd = reinterpret_cast<HWND>(0x101);
             firstDelete.sourcePath = L"C:\\images\\first.jpg";
             firstDelete.sourcePaths = {firstDelete.sourcePath};
             firstDelete.preferredFocusPath = firstDelete.sourcePath;
             state.SetActiveDelete(std::move(firstDelete));

             ViewerPendingOperationState::DeleteRequest queuedDelete;
             queuedDelete.viewerHwnd = reinterpret_cast<HWND>(0x202);
             queuedDelete.sourcePath = L"C:\\images\\second.jpg";
             queuedDelete.sourcePaths = {queuedDelete.sourcePath};
             queuedDelete.permanent = true;
             state.QueueDelete(std::move(queuedDelete));

             Expect(state.HasActiveDelete() && state.HasQueuedDeletes(),
                 "Viewer pending-operation state did not retain active and queued deletes");
             const auto activeDelete = state.TakeActiveDelete();
             Expect(activeDelete && activeDelete->sourcePath == L"C:\\images\\first.jpg"
                   && activeDelete->viewerHwnd == reinterpret_cast<HWND>(0x101)
                  && !state.HasActiveDelete() && state.HasQueuedDeletes(),
                 "Viewer pending-operation state changed active delete order");
             const auto nextDelete = state.TakeNextDelete();
             Expect(nextDelete && nextDelete->sourcePath == L"C:\\images\\second.jpg"
                   && nextDelete->viewerHwnd == reinterpret_cast<HWND>(0x202)
                  && nextDelete->permanent && !state.HasQueuedDeletes(),
                 "Viewer pending-operation state did not dequeue the next delete");

             ViewerPendingOperationState::QuickSendRequest quickSend;
             quickSend.viewerHwnd = reinterpret_cast<HWND>(0x303);
             quickSend.type = FileOperationType::Copy;
             quickSend.sourcePath = L"C:\\images\\first.jpg";
             quickSend.sourcePaths = {quickSend.sourcePath};
             state.SetQuickSend(std::move(quickSend));
               Expect(state.HasActiveQuickSend() && state.ActiveQuickSend()->active
                   && state.ActiveQuickSend()->viewerHwnd == reinterpret_cast<HWND>(0x303),
                 "Viewer pending-operation state did not activate Quick Send");
             Expect(state.TakeQuickSend() && !state.HasActiveQuickSend(),
                 "Viewer pending-operation state did not consume Quick Send");

             state.SetActiveDelete({});
             state.QueueDelete({});
             state.SetQuickSend({});
             state.Clear();
             Expect(!state.HasActiveDelete() && !state.HasQueuedDeletes() && !state.HasActiveQuickSend(),
                 "Viewer pending-operation state did not clear on viewer close");
         }

        void RunViewerSynchronizerScenario()
        {
            using hyperbrowse::browser::BrowserItem;
            using hyperbrowse::ui::ViewerSynchronizer;

            BrowserItem directory{L"folder", L"C:\\images\\folder"};
            directory.isDirectory = true;
            const std::vector<BrowserItem> modelItems = {
                directory,
                BrowserItem{L"first.jpg", L"C:\\images\\first.jpg"},
                BrowserItem{L"second.jpg", L"C:\\images\\second.jpg"}};
            bool resolverSawSlideshow = false;
            const ViewerSynchronizer::Result synchronized = ViewerSynchronizer::Build(
                modelItems,
                {0, 1, 2},
                L"C:\\images\\second.jpg",
                L"C:\\images\\first.jpg",
                0,
                true,
                [&resolverSawSlideshow](std::vector<BrowserItem> items, bool slideshowActive)
                {
                    resolverSawSlideshow = slideshowActive;
                    return items;
                });
            Expect(!synchronized.closeRequested
                       && synchronized.items.size() == 2
                       && synchronized.selectedIndex == 1
                       && synchronized.items[0].filePath == L"C:\\images\\first.jpg"
                       && synchronized.items[1].filePath == L"C:\\images\\second.jpg"
                       && resolverSawSlideshow,
                   "Viewer synchronizer did not remove directories or preserve preferred selection and slideshow state");

            const ViewerSynchronizer::Result empty = ViewerSynchronizer::Build(
                {},
                {},
                {},
                {},
                -1,
                false,
                {});
            Expect(empty.closeRequested && empty.items.empty(),
                   "Viewer synchronizer did not request close for an empty model");
        }

        void RunQuickAccessShortcutEditPolicyScenario()
        {
            using hyperbrowse::ui::QuickAccessShortcutEditPolicy;
            using hyperbrowse::ui::QuickSendAssignmentResult;

            const QuickAccessShortcutEditPolicy::Result rejected =
                QuickAccessShortcutEditPolicy::Reconcile(
                    QuickSendAssignmentResult::DuplicateShortcut,
                    12,
                    L"x");
            Expect(rejected.assignedShortcut == 12
                       && rejected.canonicalText == L"C"
                       && rejected.updateText,
                   "Quick Actions shortcut policy did not restore a rejected assignment");

            const QuickAccessShortcutEditPolicy::Result normalized =
                QuickAccessShortcutEditPolicy::Reconcile(
                    QuickSendAssignmentResult::Accepted,
                    33,
                    L"x");
            Expect(normalized.assignedShortcut == 33
                       && normalized.canonicalText == L"X"
                       && normalized.updateText,
                   "Quick Actions shortcut policy did not normalize an accepted shortcut");

            const QuickAccessShortcutEditPolicy::Result unchanged =
                QuickAccessShortcutEditPolicy::Reconcile(
                    QuickSendAssignmentResult::Accepted,
                    38,
                    L"!");
            Expect(unchanged.assignedShortcut == 38
                       && unchanged.canonicalText == L"!"
                       && !unchanged.updateText,
                   "Quick Actions shortcut policy rewrote an already canonical shortcut");

            const QuickAccessShortcutEditPolicy::Result cleared =
                QuickAccessShortcutEditPolicy::Reconcile(
                    QuickSendAssignmentResult::Accepted,
                    std::nullopt,
                    L"");
            Expect(cleared.assignedShortcut == -1
                       && cleared.canonicalText.empty()
                       && !cleared.updateText,
                   "Quick Actions shortcut policy changed accepted clearing behavior");
        }

        void RunQuickSendConfirmationScenario()
        {
            using hyperbrowse::services::FileOperationType;
            using hyperbrowse::ui::BuildQuickSendConfirmation;
            using hyperbrowse::ui::QuickSendConfirmationRequest;

            const auto copied = BuildQuickSendConfirmation(QuickSendConfirmationRequest{
                FileOperationType::Copy,
                1,
                1,
                L"C:\\Images\\first.jpg",
                L"D:\\Favorites\\Travel",
                L'8'});
            Expect(copied
                       && *copied == L"Copied first.jpg to Travel (D:\\Favorites\\Travel) [8]",
                   "Quick Send copy confirmation did not include the file, destination, and shortcut");

            const auto partial = BuildQuickSendConfirmation(QuickSendConfirmationRequest{
                FileOperationType::Move,
                3,
                2,
                {},
                L"D:\\Favorites\\Travel",
                std::nullopt});
            Expect(partial
                       && *partial == L"Moved 2 of 3 items to Travel (D:\\Favorites\\Travel)",
                   "Quick Send partial confirmation did not report the successful item count");

            const auto failed = BuildQuickSendConfirmation(QuickSendConfirmationRequest{
                FileOperationType::Copy,
                1,
                0,
                L"C:\\Images\\first.jpg",
                L"D:\\Favorites\\Travel",
                L'8'});
            Expect(!failed, "Quick Send displayed a success confirmation after a failed operation");
        }
    }

    void RunPerformanceHudFormattingScenario()
    {
        hyperbrowse::ui::PerformanceHudSnapshot snapshot;
        snapshot.activeDecodes = 3;
        snapshot.scaleAverageMs = 12.34;
        snapshot.thumbnailCacheHitRatePercent = 75.0;
        snapshot.memoryPressureActive = true;
        snapshot.pendingThumbnailJobs = 9;

        const std::wstring text = hyperbrowse::ui::FormatPerformanceHudText(snapshot);
        Expect(text == L"Active decodes: 3\r\nScale average: 12.3 ms\r\nThumbnail cache hit rate: 75%\r\nMemory pressure: Active\r\nThumbnail queue: 9",
               "Performance HUD formatting changed metric labels or units");

        snapshot.scaleAverageMs.reset();
        snapshot.thumbnailCacheHitRatePercent.reset();
        snapshot.memoryPressureActive = false;
        const std::wstring unavailableText = hyperbrowse::ui::FormatPerformanceHudText(snapshot);
        Expect(unavailableText.find(L"Scale average: Not available") != std::wstring::npos
                   && unavailableText.find(L"Thumbnail cache hit rate: Not available") != std::wstring::npos
                   && unavailableText.find(L"Memory pressure: Normal") != std::wstring::npos,
               "Performance HUD did not distinguish unavailable metrics from zero");
    }

    void RunPolicyScenarios()
    {
        RunPrefetchSizingScenario();
        RunResourceSizingRangeScenario();
        RunViewerTransitionPolicyScenario();
        RunCompareSessionPolicyScenario();
        RunFileOperationMediaCacheInvalidationScenario();
        RunFolderHistoryScenario();
        RunPerformanceHudFormattingScenario();
        RunFileOperationJournalScenario();
        RunFileCommandControllerScenario();
        RunViewCommandControllerScenario();
        RunCommandBarControllerScenario();
        RunMenuMetricsScenario();
        RunResponsivePanelSizingScenario();
        RunQuickAccessMenuBuilderScenario();
        RunDetailsPanelHistogramScenario();
        RunRightPaneHitTesterScenario();
        RunQuickAccessLayoutScenario();
        RunQuickAccessDestinationBuilderScenario();
        RunBrowserItemScopeCollectorScenario();
        RunFolderTreeDropPolicyScenario();
        RunSelectionRatingPolicyScenario();
        RunThumbnailRatingKeyPolicyScenario();
        RunViewerItemSelectionPolicyScenario();
        RunItemNumberNavigationPolicyScenario();
        RunViewerPendingOperationStateScenario();
        RunViewerSynchronizerScenario();
        RunQuickAccessShortcutEditPolicyScenario();
        RunQuickSendConfirmationScenario();
        RunDetailsPanelLayoutScenario();
        RunDisplaySurfaceRecoveryPolicyScenario();
        RunClipboardFileTransferScenario();
        RunQuickAccessPathListScenario();
        RunWindowBoundsPersistenceScenario();
        RunSelectedPathPersistenceScenario();
        RunFilingResumePersistenceScenario();
        RunViewerSettingsPersistenceScenario();
        RunBrowserPresentationPersistenceScenario();
        RunImageWorkflowPersistenceScenario();
        RunPerformanceSettingsPersistenceScenario();
        RunPairedRawJpegResolverScenario();
        RunWindowTimerRouterScenario();
        RunWindowAsyncMessageRouterScenario();
    }

    bool RunFocusedPolicyScenario(std::string_view scenario)
    {
        if (scenario == "--folder-history")
        {
            RunFolderHistoryScenario();
        }
        else if (scenario == "--file-operation-media-cache")
        {
            RunFileOperationMediaCacheInvalidationScenario();
        }
        else if (scenario == "--quick-access")
        {
            RunQuickAccessMenuBuilderScenario();
        }
        else if (scenario == "--details-histogram")
        {
            RunDetailsPanelHistogramScenario();
        }
        else if (scenario == "--right-pane-hit-test")
        {
            RunRightPaneHitTesterScenario();
        }
        else if (scenario == "--quick-access-layout")
        {
            RunQuickAccessLayoutScenario();
        }
        else if (scenario == "--quick-access-destinations")
        {
            RunQuickAccessDestinationBuilderScenario();
        }
        else if (scenario == "--browser-item-scope")
        {
            RunBrowserItemScopeCollectorScenario();
        }
        else if (scenario == "--folder-tree-drop")
        {
            RunFolderTreeDropPolicyScenario();
        }
        else if (scenario == "--selection-rating")
        {
            RunSelectionRatingPolicyScenario();
        }
        else if (scenario == "--viewer-item-selection")
        {
            RunViewerItemSelectionPolicyScenario();
        }
        else if (scenario == "--viewer-transition-policy")
        {
            RunViewerTransitionPolicyScenario();
        }
        else if (scenario == "--compare-session-policy")
        {
            RunCompareSessionPolicyScenario();
        }
        else if (scenario == "--item-number-navigation")
        {
            RunItemNumberNavigationPolicyScenario();
        }
        else if (scenario == "--viewer-pending-operations")
        {
            RunViewerPendingOperationStateScenario();
        }
        else if (scenario == "--viewer-synchronizer")
        {
            RunViewerSynchronizerScenario();
        }
        else if (scenario == "--quick-access-shortcut")
        {
            RunQuickAccessShortcutEditPolicyScenario();
        }
        else if (scenario == "--quick-send-confirmation")
        {
            RunQuickSendConfirmationScenario();
        }
        else if (scenario == "--details-layout")
        {
            RunDetailsPanelLayoutScenario();
        }
        else if (scenario == "--display-recovery")
        {
            RunDisplaySurfaceRecoveryPolicyScenario();
        }
        else if (scenario == "--clipboard")
        {
            RunClipboardFileTransferScenario();
        }
        else if (scenario == "--quick-access-paths")
        {
            RunQuickAccessPathListScenario();
        }
        else if (scenario == "--window-bounds")
        {
            RunWindowBoundsPersistenceScenario();
        }
        else if (scenario == "--selected-paths")
        {
            RunSelectedPathPersistenceScenario();
        }
        else if (scenario == "--viewer-settings")
        {
            RunViewerSettingsPersistenceScenario();
        }
        else if (scenario == "--browser-presentation")
        {
            RunBrowserPresentationPersistenceScenario();
        }
        else if (scenario == "--image-workflow")
        {
            RunImageWorkflowPersistenceScenario();
        }
        else if (scenario == "--performance-settings")
        {
            RunPerformanceSettingsPersistenceScenario();
        }
        else if (scenario == "--resource-sizing")
        {
            RunResourceSizingRangeScenario();
        }
        else if (scenario == "--paired-raw-jpeg")
        {
            RunPairedRawJpegResolverScenario();
        }
        else if (scenario == "--timer-router")
        {
            RunWindowTimerRouterScenario();
        }
        else if (scenario == "--async-router")
        {
            RunWindowAsyncMessageRouterScenario();
        }
        else if (scenario == "--command-bar")
        {
            RunViewCommandControllerScenario();
            RunCommandBarControllerScenario();
            RunMenuMetricsScenario();
        }
        else if (scenario == "--menu-metrics")
        {
            RunMenuMetricsScenario();
        }
        else if (scenario == "--responsive-panel")
        {
            RunResponsivePanelSizingScenario();
        }
        else
        {
            return false;
        }

        return true;
    }
}
