#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <array>
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
#include "ui/BrowserPresentationPersistence.h"
#include "ui/CommandBarController.h"
#include "ui/CommandIds.h"
#include "ui/ClipboardFileTransfer.h"
#include "ui/DetailsPanelHistogram.h"
#include "ui/DetailsPanelLayout.h"
#include "ui/DisplaySurfaceRecoveryPolicy.h"
#include "ui/FileCommandController.h"
#include "ui/FileOperationJournal.h"
#include "ui/FolderHistory.h"
#include "ui/ImageWorkflowPersistence.h"
#include "ui/PairedRawJpegResolver.h"
#include "ui/QuickAccessLayout.h"
#include "ui/QuickAccessMenuBuilder.h"
#include "ui/QuickAccessPathList.h"
#include "ui/RightPaneHitTester.h"
#include "ui/SelectedPathPersistence.h"
#include "ui/ViewCommandController.h"
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

        void RunFolderHistoryScenario()
        {
            using hyperbrowse::ui::FolderHistory;
            using hyperbrowse::ui::FolderHistoryNavigationDirection;

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

            journal.Begin(UndoRedoOperation::Undo);
            journal.Complete(UndoRedoOperation::Undo, true);
            Expect(journal.CanUndo() && journal.CanRedo(),
                   "Successful undo did not move the entry to redo history");
            Expect(journal.RedoEntry() && journal.RedoEntry()->type == 4,
                   "File-operation journal stored the wrong redo entry");

            journal.Begin(UndoRedoOperation::Redo);
            journal.Complete(UndoRedoOperation::Redo, false);
            Expect(journal.CanUndo() && journal.CanRedo(),
                   "Failed redo changed journal history");
            Expect(journal.PendingOperation() == UndoRedoOperation::None,
                   "File-operation journal retained a failed pending operation");

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
            int detailsCallCount = 0;

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
            handlers.onDetails = [&detailsCallCount]
            {
                ++detailsCallCount;
            };
            controller.Configure(std::move(handlers));

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
            Expect(controller.Handle(ID_VIEW_DETAILS) && detailsCallCount == 1,
                   "View command controller did not route fixed view commands");
            Expect(!controller.Handle(ID_FILE_OPEN_FOLDER),
                   "View command controller claimed a file command outside its ownership");
        }

        void RunCommandBarControllerScenario()
        {
            using hyperbrowse::ui::CommandBarController;
            using namespace hyperbrowse::ui::command_ids;

            CommandBarController controller;
            controller.InitializeItems();
            controller.SetMenuButton(0, L"File", L'F', reinterpret_cast<HMENU>(static_cast<INT_PTR>(1)));
            controller.SetMenuButton(1, L"Edit", L'E', reinterpret_cast<HMENU>(static_cast<INT_PTR>(2)));
            controller.SetMenuButton(2, L"View", L'V', reinterpret_cast<HMENU>(static_cast<INT_PTR>(3)));
            controller.SetMenuButton(3, L"Help", L'H', reinterpret_cast<HMENU>(static_cast<INT_PTR>(4)));
            controller.Layout(900,
                              6,
                              nullptr,
                              [](HFONT, std::wstring_view text)
                              {
                                  return static_cast<int>(text.size() * 8);
                              });

            const auto& menuButtons = controller.MenuButtons();
            Expect(controller.Items().size() == 17, "Command-bar controller did not initialize toolbar items");
            Expect(controller.MenuHitTest(menuButtons[0].rect.left + 1, menuButtons[0].rect.top + 1) == 0,
                   "Command-bar controller did not hit-test the first menu button");

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
            state.recursiveChecked = true;
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
            Expect(findItem(ID_VIEW_RECURSIVE)->checked && findItem(ID_VIEW_THUMBNAILS)->checked,
                   "Command-bar controller did not apply toggle state");
            Expect(!findItem(ID_ACTION_THUMBNAIL_SIZE_MENU)->enabled
                       && findItem(ID_FILE_COMPARE_SELECTED)->enabled
                       && !findItem(ID_FILE_COPY_SELECTION)->enabled,
                   "Command-bar controller did not apply enabled state");

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

            const std::array<RECT, 2> tabRects{
                RECT{10, 10, 60, 30},
                RECT{64, 10, 114, 30}};
            const RECT tabStripRect{10, 10, 114, 30};
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
                       && result.tabRects[0].right == 216 && result.tabRects[1].left == 226 && result.tabRects[1].right == 328,
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
            Expect(narrowResult.tabRects[0].right == 180 && narrowResult.tabRects[1].left == 190
                       && narrowResult.tabRects[1].right == 256
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
                       && restored.invertKeyboardPanning,
                   "Viewer settings persistence did not restore valid viewer and slideshow values");

            values[L"SlideshowIntervalMs"] = 1;
            values[L"SlideshowTransitionStyle"] = 99;
            values[L"SlideshowTransitionDurationMs"] = 6001;
            values[L"ViewerMouseWheelBehavior"] = 99;
            values[L"ViewerEscapeKeyBehavior"] = 99;
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
                       && fallback.escapeKeyBehavior == EscapeKeyBehavior::Close,
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
                       && values[L"InvertKeyboardPanning"] == 1,
                   "Viewer settings persistence did not write the expected registry value contract");
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
                       && values[L"DetailsPanelWidth"] == 250,
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
                       && restored.showPressureStateInStatusBar
                       && restored.closeMainWindowOnEscape,
                   "Performance settings persistence did not restore valid settings");

            dwordValues[L"ResourceProfile"] = 99;
            dwordValues[L"PrefetchDepthOverride"] = 99;
            qwordValues[L"ThumbnailCacheCapacityOverrideBytes"] = std::numeric_limits<std::uint64_t>::max();
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
                       && fallback.thumbnailCacheCapacityOverrideBytes == std::numeric_limits<std::size_t>::max(),
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
                       && qwordValues[L"MetadataCacheCapacityOverrideEntries"] == 42,
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
    }

    void RunPolicyScenarios()
    {
        RunPrefetchSizingScenario();
        RunFolderHistoryScenario();
        RunFileOperationJournalScenario();
        RunFileCommandControllerScenario();
        RunViewCommandControllerScenario();
        RunCommandBarControllerScenario();
        RunQuickAccessMenuBuilderScenario();
        RunDetailsPanelHistogramScenario();
        RunRightPaneHitTesterScenario();
        RunQuickAccessLayoutScenario();
        RunDetailsPanelLayoutScenario();
        RunDisplaySurfaceRecoveryPolicyScenario();
        RunClipboardFileTransferScenario();
        RunQuickAccessPathListScenario();
        RunWindowBoundsPersistenceScenario();
        RunSelectedPathPersistenceScenario();
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
        if (scenario == "--quick-access")
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
        else
        {
            return false;
        }

        return true;
    }
}
