#include <windows.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>

#include "ui/CommandIds.h"
#include "ui/ShortcutCatalog.h"
#include "util/BackgroundExecutor.h"

#include "smoke_runtime.h"

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

        void RunShortcutCatalogScenario()
        {
            using hyperbrowse::ui::ShortcutContext;
            using hyperbrowse::ui::ShortcutDefinition;
            using namespace hyperbrowse::ui::command_ids;

            Expect(hyperbrowse::ui::kMainMenuMnemonicCatalogValid,
                   "Main command-bar menu contains duplicate access keys");
            Expect(hyperbrowse::ui::MainMenuMnemonicIndexFromVirtualKey('F') == 0,
                   "Alt+F does not select the File menu");
            Expect(hyperbrowse::ui::MainMenuMnemonicIndexFromVirtualKey('E') == 1,
                   "Alt+E does not select the Edit menu");
            Expect(hyperbrowse::ui::MainMenuMnemonicIndexFromVirtualKey('V') == 2,
                   "Alt+V does not select the View menu");
            Expect(hyperbrowse::ui::MainMenuMnemonicIndexFromVirtualKey('H') == 3,
                   "Alt+H does not select the Help menu");
            Expect(hyperbrowse::ui::MainMenuMnemonicIndexFromVirtualKey('x') == -1,
                   "An unrelated access key selected a command-bar menu");

            Expect(hyperbrowse::ui::kShortcutCatalogValid,
                   "Main-window shortcut catalog contains duplicate accelerator ownership");
            Expect(!hyperbrowse::ui::HasDuplicateShortcuts(hyperbrowse::ui::kViewerShortcutCatalog),
                   "Viewer shortcut catalog contains duplicate keyboard behavior");

            const auto hasShortcut = [](std::span<const ShortcutDefinition> catalog,
                                        ShortcutContext context,
                                        UINT commandId,
                                        WORD virtualKey,
                                        BYTE modifiers)
            {
                return std::any_of(catalog.begin(),
                                   catalog.end(),
                                   [&](const ShortcutDefinition& shortcut)
                                   {
                                       return shortcut.context == context
                                           && shortcut.commandId == commandId
                                           && shortcut.virtualKey == virtualKey
                                           && shortcut.modifiers == modifiers;
                                   });
            };

            const auto mainShortcuts = hyperbrowse::ui::MainWindowShortcuts();
            Expect(hasShortcut(mainShortcuts, ShortcutContext::MainWindow, ID_FILE_ESCAPE, VK_ESCAPE, 0),
                   "Escape is not owned by the dedicated Escape command");
            Expect(hasShortcut(mainShortcuts, ShortcutContext::MainWindow, ID_FILE_MINIMIZE, 'W', FCONTROL),
                   "Ctrl+W no longer owns the main-window minimize command");
            Expect(!hasShortcut(mainShortcuts, ShortcutContext::MainWindow, ID_FILE_MINIMIZE, VK_ESCAPE, 0),
                   "Escape still shares the Ctrl+W minimize command");
            Expect(hasShortcut(mainShortcuts, ShortcutContext::MainWindow, ID_EDIT_CUT, 'X', FCONTROL),
                   "Ctrl+X is missing from the main-window shortcut catalog");
            Expect(hasShortcut(mainShortcuts, ShortcutContext::MainWindow, ID_VIEW_NAVIGATE_FORWARD_FOLDER, VK_RIGHT, FALT),
                   "Alt+Right is missing from the folder navigation catalog");
            Expect(hasShortcut(mainShortcuts, ShortcutContext::MainWindow, ID_VIEW_THUMBNAIL_SIZE_INCREASE, VK_ADD, 0),
                   "Numpad plus is missing from the thumbnail stepping catalog");
            Expect(hasShortcut(hyperbrowse::ui::ViewerShortcuts(),
                               ShortcutContext::Viewer,
                               ID_FILE_COPY_IMAGE_PIXELS,
                               'I',
                               FCONTROL | FSHIFT),
                   "Viewer Copy Image shortcut is missing from the shared catalog");
            Expect(hasShortcut(hyperbrowse::ui::ViewerShortcuts(), ShortcutContext::Viewer, 0, '0', 0),
                   "Viewer 0 fit shortcut is missing from the shared catalog");
            Expect(hasShortcut(hyperbrowse::ui::ViewerShortcuts(), ShortcutContext::Viewer, 0, '1', 0),
                   "Viewer 1 actual-size shortcut is missing from the shared catalog");
            Expect(hasShortcut(hyperbrowse::ui::ViewerShortcuts(),
                               ShortcutContext::Viewer,
                               ID_VIEW_SLIDESHOW_FOLDER,
                               'F',
                               FCONTROL | FSHIFT),
                   "Viewer Ctrl+Shift+F slideshow shortcut is missing from the shared catalog");
            Expect(hasShortcut(hyperbrowse::ui::ViewerShortcuts(),
                               ShortcutContext::Viewer,
                               ID_FILE_QUICK_SEND_MOVE,
                               VK_F7,
                               0),
                   "Viewer F7 Quick Actions shortcut is missing from the shared catalog");
            Expect(hasShortcut(hyperbrowse::ui::ViewerShortcuts(),
                               ShortcutContext::Viewer,
                               ID_FILE_QUICK_SEND_COPY,
                               VK_F8,
                               0),
                   "Viewer F8 Quick Actions shortcut is missing from the shared catalog");
            Expect(hasShortcut(hyperbrowse::ui::ViewerShortcuts(), ShortcutContext::Viewer, 0, 'H', 0),
                   "Viewer H fit-height shortcut is missing from the shared catalog");
            Expect(hasShortcut(hyperbrowse::ui::ViewerShortcuts(), ShortcutContext::Viewer, 0, 'W', 0),
                   "Viewer W fit-width shortcut is missing from the shared catalog");
            Expect(hasShortcut(hyperbrowse::ui::ViewerShortcuts(), ShortcutContext::Viewer, 0, 'L', 0),
                   "Viewer L rotate-left shortcut is missing from the shared catalog");
            Expect(hasShortcut(hyperbrowse::ui::ViewerShortcuts(), ShortcutContext::Viewer, 0, 'R', 0),
                   "Viewer R rotate-right shortcut is missing from the shared catalog");
            Expect(hasShortcut(hyperbrowse::ui::ViewerShortcuts(), ShortcutContext::Viewer, 0, 'C', 0),
                   "Viewer C compare shortcut is missing from the shared catalog");
            Expect(hasShortcut(hyperbrowse::ui::ViewerShortcuts(), ShortcutContext::Viewer, 0, 'X', 0),
                   "Viewer X compared-image shortcut is missing from the shared catalog");
            Expect(hasShortcut(hyperbrowse::ui::ViewerShortcuts(), ShortcutContext::Viewer, 0, VK_TAB, 0),
                   "Viewer Tab overlay shortcut is missing from the shared catalog");
            Expect(hasShortcut(hyperbrowse::ui::ViewerShortcuts(), ShortcutContext::Viewer, 0, VK_SPACE, 0),
                   "Viewer Space slideshow shortcut is missing from the shared catalog");
            Expect(hasShortcut(hyperbrowse::ui::ViewerShortcuts(), ShortcutContext::Viewer, 0, VK_F11, 0),
                   "Viewer F11 full-screen shortcut is missing from the shared catalog");
            Expect(hasShortcut(hyperbrowse::ui::ViewerShortcuts(), ShortcutContext::Viewer, 0, VK_RETURN, FCONTROL),
                   "Viewer Ctrl+Enter full-screen shortcut is missing from the shared catalog");
            Expect(hasShortcut(hyperbrowse::ui::ViewerShortcuts(), ShortcutContext::Viewer, 0, VK_PRIOR, 0),
                   "Viewer Page Up shortcut is missing from the shared catalog");
            Expect(hasShortcut(hyperbrowse::ui::ViewerShortcuts(), ShortcutContext::Viewer, 0, VK_NEXT, 0),
                   "Viewer Page Down shortcut is missing from the shared catalog");
            Expect(hasShortcut(hyperbrowse::ui::ViewerShortcuts(), ShortcutContext::Viewer, 0, VK_RETURN, 0),
                   "Viewer Enter fit-toggle shortcut is missing from the shared catalog");
            Expect(hasShortcut(hyperbrowse::ui::ViewerShortcuts(), ShortcutContext::Viewer, 0, VK_OEM_PLUS, 0),
                   "Viewer plus zoom shortcut is missing from the shared catalog");
            Expect(hasShortcut(hyperbrowse::ui::ViewerShortcuts(), ShortcutContext::Viewer, 0, VK_OEM_MINUS, 0),
                   "Viewer minus zoom shortcut is missing from the shared catalog");
        }

        void RunBackgroundExecutorExceptionScenario()
        {
            hyperbrowse::util::BackgroundExecutor executor(1, 2);
            std::mutex completionMutex;
            std::condition_variable completionCondition;
            bool completed = false;

            Expect(executor.Post([]()
            {
                throw std::runtime_error("intentional background executor test failure");
            }), "Background executor rejected the exception-isolation test task");
            Expect(executor.Post([&]()
            {
                {
                    std::scoped_lock lock(completionMutex);
                    completed = true;
                }
                completionCondition.notify_one();
            }), "Background executor rejected the post-exception recovery task");

            std::unique_lock lock(completionMutex);
            Expect(completionCondition.wait_for(lock, std::chrono::seconds(2), [&]() { return completed; }),
                   "Background executor did not continue after a task exception");
        }

        void RunBackgroundExecutorCapacityScenario()
        {
            std::mutex mutex;
            std::condition_variable condition;
            bool firstStarted = false;
            bool releaseFirst = false;
            bool secondCompleted = false;

            hyperbrowse::util::BackgroundExecutor executor(1, 1);
            Expect(executor.Post([&]()
            {
                std::unique_lock lock(mutex);
                firstStarted = true;
                condition.notify_all();
                condition.wait(lock, [&]() { return releaseFirst; });
            }), "Background executor rejected its active task");

            {
                std::unique_lock lock(mutex);
                Expect(condition.wait_for(lock, std::chrono::seconds(2), [&]() { return firstStarted; }),
                       "Background executor did not start its active task");
            }
            Expect(executor.ActiveTaskCount() == 1, "Background executor did not report active work");

            Expect(executor.Post([&]()
            {
                {
                    std::scoped_lock lock(mutex);
                    secondCompleted = true;
                }
                condition.notify_all();
            }), "Background executor rejected its available pending slot");
            Expect(executor.PendingTaskCount() == 1, "Background executor did not report pending work");
            Expect(!executor.Post([]() {}), "Background executor accepted work beyond its pending limit");
            Expect(executor.RejectedTaskCount() == 1, "Background executor did not count rejected work");

            {
                std::scoped_lock lock(mutex);
                releaseFirst = true;
            }
            condition.notify_all();

            {
                std::unique_lock lock(mutex);
                Expect(condition.wait_for(lock, std::chrono::seconds(2), [&]() { return secondCompleted; }),
                       "Background executor did not drain pending work");
            }
            Expect(executor.ActiveTaskCount() == 0, "Background executor retained an active-work count after completion");
            Expect(executor.PendingTaskCount() == 0, "Background executor retained a pending-work count after completion");

            auto destructionExecutor = std::make_unique<hyperbrowse::util::BackgroundExecutor>(1, 1);
            firstStarted = false;
            releaseFirst = false;
            bool queuedRan = false;
            bool destructionCompleted = false;

            Expect(destructionExecutor->Post([&]()
            {
                std::unique_lock lock(mutex);
                firstStarted = true;
                condition.notify_all();
                condition.wait(lock, [&]() { return releaseFirst; });
            }), "Background executor rejected its destruction test task");
            {
                std::unique_lock lock(mutex);
                Expect(condition.wait_for(lock, std::chrono::seconds(2), [&]() { return firstStarted; }),
                       "Background executor destruction task did not start");
            }
            Expect(destructionExecutor->Post([&]() { queuedRan = true; }),
                   "Background executor rejected the queued destruction test task");

            std::thread destructionThread([&]()
            {
                destructionExecutor.reset();
                {
                    std::scoped_lock lock(mutex);
                    destructionCompleted = true;
                }
                condition.notify_all();
            });

            {
                std::unique_lock lock(mutex);
                Expect(!condition.wait_for(lock, std::chrono::milliseconds(100), [&]() { return destructionCompleted; }),
                       "Background executor destruction returned before its active task completed");
                releaseFirst = true;
            }
            condition.notify_all();
            destructionThread.join();
            Expect(destructionCompleted, "Background executor destruction did not complete");
            Expect(!queuedRan, "Background executor ran queued work after destruction began");
        }
    }

    void RunRuntimeScenarios()
    {
        RunShortcutCatalogScenario();
        RunBackgroundExecutorExceptionScenario();
        RunBackgroundExecutorCapacityScenario();
    }
}
