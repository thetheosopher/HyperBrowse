#include "smoke_support.h"
#include "smoke_scenarios.h"

namespace hyperbrowse::tests::smoke
{

        void RunSavedSearchStoreScenario()
        {
         using hyperbrowse::services::SavedSearch;
         using hyperbrowse::services::SavedSearchStore;
         const fs::path directory = gSmokeCacheDirectory / L"saved-search-store";
         SavedSearchStore store(directory.wstring());
         std::wstring error;
         std::vector<SavedSearch> searches;
         Expect(store.LoadOnWorker(&searches, &error) && searches.empty(),
             "A missing saved-search store did not load as empty");
         Expect(!store.AddOnWorker({L" ", L"type:raw"}, &error) && !error.empty(),
             "Saved search accepted an empty name");
         Expect(!store.AddOnWorker({L"Empty", L" "}, &error), "Saved search accepted an empty expression");
         Expect(!store.AddOnWorker({std::wstring(129, L'x'), L"type:raw"}, &error),
             "Saved search accepted an oversized name");
         Expect(!store.AddOnWorker({L"Long", std::wstring(261, L'x')}, &error),
             "Saved search accepted an oversized expression");
         Expect(!store.AddOnWorker({L"Line\nbreak", L"type:raw"}, &error),
             "Saved search accepted a multiline display name");
         Expect(store.AddOnWorker({L"  Keepers  ", L"  rating:>=4 tag:keeper type:raw  "}, &error),
             "Saved search failed to add a valid expression");
         const SavedSearch unicode{L"\u30ad\u30fc\u30d1\u30fc", L"tag:pick\\keeper\tname\npart"};
         Expect(store.AddOnWorker(unicode, &error), "Saved search failed to persist Unicode and escaped text");
         Expect(!store.AddOnWorker({L"KEEPERS", L"type:jpg"}, &error),
             "Saved search silently overwrote a case-insensitive duplicate");
         SavedSearchStore restarted(directory.wstring());
         Expect(restarted.LoadOnWorker(&searches, &error)
             && searches == std::vector<SavedSearch>{{L"Keepers", L"rating:>=4 tag:keeper type:raw"}, unicode},
             "Saved-search restart lost order, Unicode, escaping, or trimmed values");
         Expect(restarted.AddOnWorker({L"Other window", L"rating:unrated"}, &error),
             "Another saved-search store could not add a record");
         Expect(store.UpdateOnWorker(L"keepers", L"type:nef"), "Saved search failed to update case-insensitively");
         Expect(store.RenameOnWorker(L"KEEPERS", L"NEF keepers", &error), "Saved search failed to rename");
         Expect(!store.RenameOnWorker(L"NEF keepers", L"OTHER WINDOW", &error),
             "Saved search renamed over another record");
         Expect(store.RemoveOnWorker(unicode.name, &error), "Saved search failed to delete");
         Expect(restarted.LoadOnWorker(&searches, &error)
             && searches == std::vector<SavedSearch>{{L"NEF keepers", L"type:nef"}, {L"Other window", L"rating:unrated"}},
             "Stale store mutation lost another window's saved search");
         Expect(!store.RemoveOnWorker(L"missing", &error), "Saved search silently deleted a missing name");

         const auto previous = searches;
         HANDLE reader = CreateFileW((directory / L"saved-searches.tsv").c_str(), GENERIC_READ,
                         FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
         Expect(reader != INVALID_HANDLE_VALUE, "Could not create saved-search atomic publication blocker");
         const bool published = store.AddOnWorker({L"Must not publish", L"type:png"}, &error);
         CloseHandle(reader);
         Expect(!published && !error.empty(), "Saved-search publication ignored a blocked atomic replace");
         Expect(store.LoadOnWorker(&searches, &error) && searches == previous,
             "Failed saved-search publication corrupted the prior file");
         for (const auto& entry : fs::directory_iterator(directory))
             Expect(entry.path().filename().wstring().find(L".tmp.") == std::wstring::npos,
                 "Failed saved-search publication left a temporary file");

         HANDLE lock = CreateFileW((directory / L"saved-searches.lock").c_str(), GENERIC_READ | GENERIC_WRITE,
                          0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_HIDDEN, nullptr);
         Expect(lock != INVALID_HANDLE_VALUE, "Could not create saved-search concurrent-writer blocker");
         const bool wroteWhileLocked = restarted.AddOnWorker({L"Locked", L"type:raw"}, &error);
         CloseHandle(lock);
         Expect(!wroteWhileLocked && !error.empty(), "Saved-search mutation bypassed the shared file lock");

         const auto expectMalformed = [&](std::string bytes)
         {
             std::ofstream corrupt(directory / L"saved-searches.tsv", std::ios::binary | std::ios::trunc);
             corrupt.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
             corrupt.close();
             searches = previous;
             Expect(!store.LoadOnWorker(&searches, &error) && !error.empty() && searches == previous,
                 "Malformed saved-search input replaced valid in-memory results");
             Expect(!store.AddOnWorker({L"Repair by overwrite", L"type:raw"}, &error),
                 "Mutation overwrote malformed saved-search storage");
             std::ifstream unchanged(directory / L"saved-searches.tsv", std::ios::binary);
             const std::string after((std::istreambuf_iterator<char>(unchanged)), std::istreambuf_iterator<char>());
             Expect(after == bytes, "Failed saved-search mutation changed malformed input");
         };
         const std::string header = "# HyperBrowse saved searches v1; encoding=utf-8\n";
         expectMalformed("unsupported version\nName\ttype:raw\n");
         expectMalformed(header + "Name\ttype:raw\nname\ttype:jpg\n");
         expectMalformed(header + "Name\ttype:raw\\q\n");
         expectMalformed(header + "Name\ttype:raw\textra\n");
         expectMalformed(header + "Name\t" + std::string(261, 'x') + "\n");
         expectMalformed(header + "Name\t" + std::string(1, static_cast<char>(0xff)) + "\n");
         expectMalformed(std::string(SavedSearchStore::kMaximumFileBytes + 1, 'x'));

         SavedSearchStore full((gSmokeCacheDirectory / L"saved-search-limit").wstring());
         for (std::size_t index = 0; index < SavedSearchStore::kMaximumSearches; ++index)
             Expect(full.AddOnWorker({L"Search " + std::to_wstring(index), L"type:raw"}, &error),
                 "Saved-search store rejected a record below its limit");
         Expect(!full.AddOnWorker({L"One too many", L"type:raw"}, &error),
             "Saved-search store exceeded its record bound");
         Expect(full.LoadOnWorker(&searches, &error) && searches.size() == SavedSearchStore::kMaximumSearches,
             "Saved-search limit failure changed existing records");
        }

            void RunSavedSearchControllerScenario(HINSTANCE instance)
            {
             using hyperbrowse::ui::SavedSearchController;
             const HWND host = CreateWindowExW(0, L"STATIC", L"Saved-search worker smoke", WS_OVERLAPPED,
                 0, 0, 200, 100, nullptr, nullptr, instance, nullptr);
             Expect(host != nullptr, "Failed to create saved-search notification owner");
             const std::wstring directory = (gSmokeCacheDirectory / L"saved-search-controller").wstring();
             {
                 SavedSearchController controller(host, directory);
                 const auto waitForResult = [&]
                 {
                  PumpMessagesUntil([&] { controller.ConsumeResult(); return !controller.Busy(); }, 5000);
                  Expect(!controller.Busy(), "Saved-search asynchronous operation did not finish");
                 };
                 Expect(controller.Load() && !controller.Load(), "Saved-search controller did not bound simultaneous requests");
                 waitForResult();
                 Expect(controller.Ready() && controller.Searches().empty() && controller.LastError().empty(),
                     "Saved-search controller failed to load an empty store");
                Expect(controller.Add({L" RAW ", L" type:raw "}), "Saved-search controller rejected an add");
                 controller.FilterEdited();
                 waitForResult();
                 Expect(controller.Searches().size() == 1 && !controller.Active(),
                     "Late saved-search completion restored identity after a filter edit");
                 const auto activated = controller.Activate(0);
                 Expect(activated && activated->expression == L"type:raw" && controller.Active(),
                     "Saved-search controller failed to activate a loaded expression");
                Expect(controller.RenameActive(L" RAW keepers "), "Saved-search controller rejected rename");
                 waitForResult();
                 Expect(controller.Active() && controller.Active()->name == L"RAW keepers",
                     "Saved-search rename did not reconcile active identity");
                 Expect(controller.UpdateActive(L"type:raw rating:>=4"), "Saved-search controller rejected update");
                 waitForResult();
                 Expect(controller.Active() && controller.Active()->expression == L"type:raw rating:>=4",
                     "Saved-search update did not persist the replacement expression");
                 Expect(controller.Add({L"RAW KEEPERS", L"type:jpg"}), "Saved-search controller rejected a worker validation request");
                 waitForResult();
                 Expect(!controller.LastError().empty() && controller.Searches().size() == 1,
                     "Saved-search duplicate failure did not preserve the loaded snapshot");
                 controller.FilterEdited();
                 Expect(!controller.Active() && !controller.RemoveActive(),
                     "Manual filter edit left a stale saved-search delete target");
                 Expect(controller.Load(), "Saved-search refresh was unavailable after an error");
                 waitForResult();
                 Expect(controller.LastError().empty() && !controller.Active(),
                     "Saved-search refresh applied a search or retained a resolved error");
                 Expect(controller.Activate(0).has_value() && controller.RemoveActive(),
                     "Saved-search controller rejected delete");
                 waitForResult();
                 Expect(controller.Searches().empty() && !controller.Active(), "Saved-search deletion left active identity");
                 Expect(controller.Load(), "Saved-search shutdown probe did not queue work");
                 controller.Shutdown();
                 Expect(!controller.Busy() && !controller.Load(), "Saved-search controller accepted work after shutdown");
             }
             {
                 SavedSearchController restarted(host, directory);
                 Expect(restarted.Load(), "Restarted saved-search controller could not load");
                 PumpMessagesUntil([&] { restarted.ConsumeResult(); return !restarted.Busy(); }, 5000);
                 Expect(restarted.Ready() && restarted.Searches().empty(), "Saved-search controller restart lost durable deletion");
             }
             DestroyWindow(host);
            }
namespace
{

            struct SavedSearchDialogResponse
            {
                std::wstring title;
                std::wstring text;
                int command{};
                ULONGLONG deadline{};
                bool answered{};
                bool timedOut{};
                std::wstring expectedContextLine;
                bool contextLineMatched{};
                bool textEntryCentered{};
                bool textEntryFieldClickFocused{};
            };

            void CALLBACK SavedSearchDialogTimer(HWND owner, UINT, UINT_PTR timerId, DWORD)
            {
                auto* response = reinterpret_cast<SavedSearchDialogResponse*>(GetPropW(owner, L"SavedSearchSmokeDialog"));
                if (!response || response->answered) return;
                const HWND dialog = FindWindowW(response->command == IDYES ? L"#32770" : L"HyperBrowseTextInputDialog",
                                               response->title.c_str());
                if (dialog && GetWindow(dialog, GW_OWNER) == owner)
                {
                    if (GetTickCount64() >= response->deadline)
                    {
                        response->timedOut = true;
                        PostMessageW(dialog, WM_CLOSE, 0, 0);
                    }
                    else
                    {
                        if (!response->expectedContextLine.empty())
                        {
                            const HWND contextWindow = GetDlgItem(dialog, 101);
                            wchar_t contextText[512]{};
                            response->contextLineMatched = contextWindow
                                && GetWindowTextW(contextWindow,
                                                  contextText,
                                                  static_cast<int>(std::size(contextText))) > 0
                                && response->expectedContextLine == contextText;

                            const HWND editWindow = GetDlgItem(dialog, 100);
                            RECT contextRect{};
                            RECT editRect{};
                            if (contextWindow && editWindow
                                && GetWindowRect(contextWindow, &contextRect)
                                && GetWindowRect(editWindow, &editRect))
                            {
                                POINT contextPoints[2]{{contextRect.left, contextRect.top},
                                                       {contextRect.right, contextRect.bottom}};
                                POINT editPoints[2]{{editRect.left, editRect.top},
                                                    {editRect.right, editRect.bottom}};
                                MapWindowPoints(nullptr, dialog, contextPoints, 2);
                                MapWindowPoints(nullptr, dialog, editPoints, 2);

                                const UINT dpi = (std::max)(96u, GetDpiForWindow(dialog));
                                const auto scaleDip = [dpi](int value)
                                {
                                    return MulDiv(value, static_cast<int>(dpi), 96);
                                };
                                const HFONT editFont = reinterpret_cast<HFONT>(
                                    SendMessageW(editWindow, WM_GETFONT, 0, 0));
                                HDC dc = GetDC(editWindow);
                                TEXTMETRICW textMetrics{};
                                bool hasTextMetrics = false;
                                if (dc)
                                {
                                    const HGDIOBJ oldFont = editFont ? SelectObject(dc, editFont) : nullptr;
                                    hasTextMetrics = GetTextMetricsW(dc, &textMetrics) != FALSE;
                                    if (oldFont)
                                    {
                                        SelectObject(dc, oldFont);
                                    }
                                    ReleaseDC(editWindow, dc);
                                }

                                if (hasTextMetrics)
                                {
                                    const int frameTop = contextPoints[1].y + scaleDip(8);
                                    const int frameHeight = scaleDip(30);
                                    const int expectedEditHeight = std::min(
                                        static_cast<int>(textMetrics.tmHeight) + scaleDip(2),
                                        frameHeight - 2);
                                    const int expectedEditTop = frameTop + (frameHeight - expectedEditHeight) / 2;
                                    response->textEntryCentered =
                                        editPoints[1].y - editPoints[0].y == expectedEditHeight
                                        && editPoints[0].y == expectedEditTop;
                                    const int clickX = scaleDip(22);
                                    const int clickY = frameTop + 1;
                                    SendMessageW(dialog,
                                                 WM_LBUTTONDOWN,
                                                 MK_LBUTTON,
                                                 MAKELPARAM(clickX, clickY));
                                    response->textEntryFieldClickFocused = GetFocus() == editWindow;
                                }
                            }
                        }
                        if (response->command != IDYES) SetWindowTextW(GetDlgItem(dialog, 100), response->text.c_str());
                        response->answered = true;
                        PostMessageW(dialog, WM_COMMAND, MAKEWPARAM(response->command, BN_CLICKED),
                                     reinterpret_cast<LPARAM>(GetDlgItem(dialog, response->command)));
                    }
                    KillTimer(owner, timerId);
                }
            }
}


            void RunSavedSearchMainWindowScenario(HINSTANCE instance)
            {
                using namespace hyperbrowse::ui::command_ids;
                hyperbrowse::ui::ToolbarIconLibrary icons;
                Expect(icons.Initialize(), "Failed to initialize toolbar icons for saved-search smoke");
                const HBITMAP saveBitmap = icons.GetBitmap("save", 24, RGB(20, 30, 40));
                DIBSECTION saveDib{};
                Expect(saveBitmap && GetObjectW(saveBitmap, sizeof(saveDib), &saveDib) > 0
                    && saveDib.dsBm.bmWidth == 24 && saveDib.dsBm.bmHeight == 24 && saveDib.dsBm.bmBits,
                    "Inline Save icon did not rasterize at its requested dimensions");
                const auto* savePixels = static_cast<const unsigned char*>(saveDib.dsBm.bmBits);
                bool hasSaveInk = false;
                bool hasAntialiasedSaveInk = false;
                for (int pixel = 0; pixel < 24 * 24; ++pixel)
                {
                    const unsigned char alpha = savePixels[pixel * 4 + 3];
                    hasSaveInk = hasSaveInk || alpha != 0;
                    hasAntialiasedSaveInk =
                        hasAntialiasedSaveInk || (alpha > 0 && alpha < 255);
                }
                Expect(hasSaveInk, "Inline Save icon rasterized as a blank bitmap");
                Expect(hasAntialiasedSaveInk, "Inline Save icon has no antialiased edge coverage");
                const HBITMAP thumbnailSizeBitmap = icons.GetBitmap("thumbnail-size", 24, RGB(20, 30, 40));
                DIBSECTION thumbnailSizeDib{};
                Expect(thumbnailSizeBitmap
                    && GetObjectW(thumbnailSizeBitmap, sizeof(thumbnailSizeDib), &thumbnailSizeDib) > 0
                    && thumbnailSizeDib.dsBm.bmWidth == 24
                    && thumbnailSizeDib.dsBm.bmHeight == 24
                    && thumbnailSizeDib.dsBm.bmBits,
                    "Thumbnail Size icon did not rasterize at its requested dimensions");
                const auto* thumbnailSizePixels =
                    static_cast<const unsigned char*>(thumbnailSizeDib.dsBm.bmBits);
                bool hasThumbnailSizeInk = false;
                bool hasAntialiasedThumbnailSizeInk = false;
                for (int pixel = 0; pixel < 24 * 24; ++pixel)
                {
                    const unsigned char alpha = thumbnailSizePixels[pixel * 4 + 3];
                    hasThumbnailSizeInk = hasThumbnailSizeInk || alpha != 0;
                    hasAntialiasedThumbnailSizeInk =
                        hasAntialiasedThumbnailSizeInk || (alpha > 0 && alpha < 255);
                }
                Expect(hasThumbnailSizeInk, "Thumbnail Size icon rasterized as a blank bitmap");
                Expect(hasAntialiasedThumbnailSizeInk, "Thumbnail Size icon has no antialiased edge coverage");
                hyperbrowse::services::SavedSearchStore store;
                std::wstring error;
                Expect(store.AddOnWorker({L"RAW UI", L"type:raw"}, &error), "Failed to seed the isolated UI saved search");
                // The folder tree lists every sibling on the path to the browsed folder. %TEMP% can hold thousands
                // of entries, which starves the modal dialog's WM_TIMER responder, so use a quiet parent instead.
                std::wstring modulePath(MAX_PATH, L'\0');
                modulePath.resize(GetModuleFileNameW(nullptr, modulePath.data(), static_cast<DWORD>(modulePath.size())));
                TempFolder imagesParent(fs::path(modulePath).parent_path(), L"HyperBrowseSavedSearchUiParent");
                TempFolder images(imagesParent.Root(), L"HyperBrowseSavedSearchUi");
                WriteTestImage(images.Root() / L"image.jpg", TestImageFormat::Jpeg, 32, 24, 1);
                WriteTestImage(images.Root() / L"image.png", TestImageFormat::Png, 32, 24, 2);
                {
                    hyperbrowse::ui::MainWindow window(instance);
                    window.SetStartupLaunchPath(images.Root().wstring());
                    Expect(window.Create(), "Failed to create the saved-search main window");
                    window.Show(SW_SHOWNORMAL);
                    ShowWindow(window.Hwnd(), SW_RESTORE);
                    SetWindowPos(window.Hwnd(), nullptr, 0, 0, 2400, 720, SWP_NOZORDER | SWP_NOACTIVATE);
                    const HWND filter = GetDlgItem(window.Hwnd(), ID_ACTION_FILTER_EDIT);
                    Expect(filter != nullptr, "Saved-search main window has no filter edit");
                    const auto text = [&]
                    {
                        wchar_t buffer[300]{};
                        GetWindowTextW(filter, buffer, static_cast<int>(std::size(buffer)));
                        return std::wstring(buffer);
                    };
                    PumpMessagesUntil([&]
                    {
                        SendMessageW(window.Hwnd(), WM_COMMAND, ID_FILE_OPEN_SAVED_SEARCH_BASE, 0);
                        return text() == L"type:raw";
                    }, 5000);
                    Expect(text() == L"type:raw", "File saved-search command did not apply its expression to the filter edit");
                    const std::wstring expectedContextLine = L"Parent folder: C:\\HyperBrowseSmokeParent";
                    SavedSearchDialogResponse contextResponse{
                        L"Parent Folder Context Smoke",
                        L"New Folder",
                        IDCANCEL,
                        GetTickCount64() + 5000};
                    contextResponse.expectedContextLine = expectedContextLine;
                    Expect(SetPropW(window.Hwnd(), L"SavedSearchSmokeDialog", &contextResponse) != FALSE,
                           "Failed to set text-dialog context response state");
                    constexpr UINT_PTR contextTimerId = 9928;
                    Expect(SetTimer(window.Hwnd(), contextTimerId, 20, SavedSearchDialogTimer) != 0,
                           "Failed to set text-dialog context response timer");
                    std::wstring ignoredFolderName;
                    const bool contextDialogAccepted = hyperbrowse::ui::PromptForSingleLineText(
                        window.Hwnd(),
                        instance,
                        hyperbrowse::util::kDefaultAppTextSize,
                        true,
                        contextResponse.title,
                        L"Enter a name for the new folder.",
                        L"Create",
                        contextResponse.text,
                        0,
                        static_cast<int>(contextResponse.text.size()),
                        &ignoredFolderName,
                        expectedContextLine);
                    KillTimer(window.Hwnd(), contextTimerId);
                    RemovePropW(window.Hwnd(), L"SavedSearchSmokeDialog");
                    Expect(!contextDialogAccepted && contextResponse.answered
                               && contextResponse.contextLineMatched && contextResponse.textEntryCentered
                               && contextResponse.textEntryFieldClickFocused
                               && !contextResponse.timedOut,
                           std::string("Single-line text dialog did not display its parent-folder context or correctly center and focus its entry field")
                               + " (accepted=" + std::to_string(contextDialogAccepted)
                               + " answered=" + std::to_string(contextResponse.answered)
                               + " contextMatched=" + std::to_string(contextResponse.contextLineMatched)
                               + " centered=" + std::to_string(contextResponse.textEntryCentered)
                               + " clickFocused=" + std::to_string(contextResponse.textEntryFieldClickFocused)
                               + " timedOut=" + std::to_string(contextResponse.timedOut) + ")");
                    ComPtr<IAccessible> accessible;
                    Expect(SUCCEEDED(ObjectFromLresult(SendMessageW(window.Hwnd(), WM_GETOBJECT, 0, OBJID_CLIENT),
                        IID_IAccessible, 0, reinterpret_cast<void**>(accessible.GetAddressOf()))),
                        "Saved-search window did not expose its accessible command bar");
                    const auto saveChild = [&]
                    {
                        long children = 0;
                        Expect(SUCCEEDED(accessible->get_accChildCount(&children)), "Could not read saved-search accessible children");
                        for (long childId = 1; childId <= children; ++childId)
                        {
                            VARIANT child{};
                            child.vt = VT_I4;
                            child.lVal = childId;
                            BSTR name = nullptr;
                            const HRESULT result = accessible->get_accName(child, &name);
                            const bool isSave = SUCCEEDED(result) && name && std::wstring_view(name, SysStringLen(name)) == L"Save Current Filter";
                            SysFreeString(name);
                            if (isSave) return child;
                        }
                        RECT actualClient{};
                        GetClientRect(window.Hwnd(), &actualClient);
                        throw std::runtime_error("Inline Save is missing from the accessible command bar: width="
                            + std::to_string(actualClient.right) + ", dpi=" + std::to_string(GetDpiForWindow(window.Hwnd())));
                    };
                    const auto saveUnavailable = [&]
                    {
                        VARIANT state{};
                        Expect(SUCCEEDED(accessible->get_accState(saveChild(), &state)) && state.vt == VT_I4,
                            "Inline Save did not expose an accessible enabled state");
                        return (state.lVal & STATE_SYSTEM_UNAVAILABLE) != 0;
                    };
                    VARIANT saveRole{};
                    Expect(SUCCEEDED(accessible->get_accRole(saveChild(), &saveRole)) && saveRole.vt == VT_I4
                        && saveRole.lVal == ROLE_SYSTEM_PUSHBUTTON && !saveUnavailable(),
                        "Inline Save did not expose an enabled push-button role");
                    SetWindowTextW(filter, L"");
                    Expect(saveUnavailable(), "Inline Save remained enabled for an empty filter");
                    SetWindowTextW(filter, L"type:raw");
                    Expect(!saveUnavailable(), "Inline Save did not enable after editing the filter");
                    SendMessageW(window.Hwnd(), WM_COMMAND, ID_FILE_RELOAD_SAVED_SEARCHES, 0);
                    Expect(saveUnavailable(), "Inline Save remained enabled during a saved-search load");
                    Expect(PumpMessagesUntil([&] { return !saveUnavailable(); }, 5000),
                        "Inline Save did not re-enable after its saved-search load");
                    const auto runDialog = [&](UINT command, std::wstring title, std::wstring input, int answer = IDOK, bool inlineSave = false)
                    {
                        SavedSearchDialogResponse response{std::move(title), std::move(input), answer, GetTickCount64() + 5000};
                        Expect(SetPropW(window.Hwnd(), L"SavedSearchSmokeDialog", &response) != FALSE,
                               "Failed to set saved-search dialog response state");
                        constexpr UINT_PTR timerId = 9927;
                        Expect(SetTimer(window.Hwnd(), timerId, 20, SavedSearchDialogTimer) != 0,
                               "Failed to set saved-search dialog response timer");
                        if (inlineSave)
                        {
                            Expect(SUCCEEDED(accessible->accDoDefaultAction(saveChild())), "Inline Save accessible action failed");
                            PumpMessagesUntil([&] { return response.answered || response.timedOut; }, 5000);
                        }
                        else SendMessageW(window.Hwnd(), WM_COMMAND, command, 0);
                        KillTimer(window.Hwnd(), timerId);
                        RemovePropW(window.Hwnd(), L"SavedSearchSmokeDialog");
                        Expect(response.answered && !response.timedOut, "Saved-search native command did not open its expected dialog");
                    };
                    const auto waitForRecord = [&](std::wstring_view name, std::wstring_view expression, std::size_t count)
                    {
                        bool matched = false;
                        PumpMessagesUntil([&]
                        {
                            std::vector<hyperbrowse::services::SavedSearch> searches;
                            matched = store.LoadOnWorker(&searches, &error) && searches.size() == count
                                && std::any_of(searches.begin(), searches.end(), [&](const auto& search)
                                    { return search.name == name && search.expression == expression; });
                            return matched;
                        }, 5000);
                        if (!matched)
                        {
                            std::vector<hyperbrowse::services::SavedSearch> actual;
                            store.LoadOnWorker(&actual, &error);
                            std::wcerr << L"Expected saved search: " << name << L" = " << expression
                                       << L"; count=" << count << L"; error=" << error << L'\n';
                            for (const auto& search : actual)
                                std::wcerr << L"Stored saved search: " << search.name << L" = " << search.expression << L'\n';
                            std::wcerr << L"Displayed filter: " << text() << L'\n';
                        }
                        Expect(matched, "Saved-search native command did not publish its durable record");
                        PumpMessagesFor(50);
                    };
                    runDialog(ID_FILE_SAVE_CURRENT_FILTER, L"Save Current Filter", L"Saved & kept", IDOK, true);
                    waitForRecord(L"Saved & kept", L"type:raw", 2);
                    runDialog(ID_FILE_RENAME_SAVED_SEARCH, L"Rename Saved Search", L"Renamed keeper");
                    waitForRecord(L"Renamed keeper", L"type:raw", 2);
                    runDialog(ID_FILE_UPDATE_SAVED_SEARCH, L"Update Saved Search", L"type:jpg");
                    waitForRecord(L"Renamed keeper", L"type:jpg", 2);
                    PumpMessagesUntil([&] { return text() == L"type:jpg"; }, 5000);
                    Expect(text() == L"type:jpg", "Updating a saved search did not synchronize the displayed filter");
                    runDialog(ID_FILE_DELETE_SAVED_SEARCH, L"Delete Saved Search", {}, IDYES);
                    waitForRecord(L"RAW UI", L"type:raw", 1);
                    Expect(text() == L"type:jpg", "Deleting a saved search cleared its visible filter");
                    SetWindowTextW(filter, L"manual filter");
                    Expect(text() == L"manual filter", "Manual filter edit was lost");
                    SendMessageW(window.Hwnd(), WM_COMMAND, ID_FILE_RELOAD_SAVED_SEARCHES, 0);
                    PumpMessagesFor(100);
                    Expect(text() == L"manual filter", "Saved-search reload applied a search over a manual filter");
                    DestroyWindow(window.Hwnd());
                    PumpMessagesFor(50);
                }
                {
                    hyperbrowse::ui::MainWindow restarted(instance);
                    restarted.SetStartupLaunchPath(images.Root().wstring());
                    Expect(restarted.Create(), "Failed to restart the saved-search main window");
                    const HWND filter = GetDlgItem(restarted.Hwnd(), ID_ACTION_FILTER_EDIT);
                    std::wstring expression;
                    PumpMessagesUntil([&]
                    {
                        SendMessageW(restarted.Hwnd(), WM_COMMAND, ID_FILE_OPEN_SAVED_SEARCH_BASE, 0);
                        wchar_t buffer[300]{};
                        GetWindowTextW(filter, buffer, static_cast<int>(std::size(buffer)));
                        expression = buffer;
                        return expression == L"type:raw";
                    }, 5000);
                    Expect(expression == L"type:raw", "Restarted main window could not reopen the durable saved search");
                    DestroyWindow(restarted.Hwnd());
                    PumpMessagesFor(50);
                }
                Expect(store.RemoveOnWorker(L"RAW UI", &error), "Failed to clean up the isolated UI saved search");
            }

            void RunSavedSearchFilterScenario(HINSTANCE instance)
    {
        hyperbrowse::services::UserMetadataStore metadata(
            (gSmokeCacheDirectory / L"saved-search-filter-metadata").wstring());
        metadata.SetRating({L"C:\\SavedSearch\\keeper.nef"}, 5);
        metadata.SetTags({L"C:\\SavedSearch\\keeper.nef"}, L"keeper");
        metadata.SetRating({L"C:\\SavedSearch\\keeper.jpg"}, 4);
        metadata.SetTags({L"C:\\SavedSearch\\keeper.jpg"}, L"keeper");
        std::wstring error;
        Expect(metadata.Flush(std::chrono::seconds(5), &error),
               "Saved-search filter fixture metadata did not persist");

        HWND hostWindow = CreateUiHostWindow(instance);
        Expect(hostWindow != nullptr, "Failed to create saved-search filter host");
        hyperbrowse::browser::BrowserPane pane(instance);
        Expect(pane.Create(hostWindow), "Failed to create saved-search filter browser");
        MoveWindow(pane.Hwnd(), 0, 0, 860, 620, TRUE);

        hyperbrowse::browser::BrowserModel model;
        std::vector<hyperbrowse::browser::BrowserItem> items{
            {L"keeper.nef", L"C:\\SavedSearch\\keeper.nef", L"NEF"},
            {L"unrated.nrw", L"C:\\SavedSearch\\unrated.nrw", L".NRW"},
            {L"unrated.arw", L"C:\\SavedSearch\\unrated.arw", L"arw"},
            {L"keeper.jpg", L"C:\\SavedSearch\\keeper.jpg", L"JPG"},
            {L"raw-in-name.png", L"C:\\SavedSearch\\raw-in-name.png", L"PNG"},
        };
        model.Reset(L"C:\\SavedSearch", false);
        model.AppendItems(std::move(items), 5, 0);
        model.Complete();
        pane.SetUserMetadataStore(&metadata);
        pane.SetModel(&model);
        pane.SetRawJpegStackingEnabled(false);

        const auto expectCount = [&](std::wstring query, std::uint64_t count, const char* message)
        {
            pane.SetFilterQuery(std::move(query));
            Expect(pane.DisplayedItemCount() == count, message);
        };
        expectCount(L" TYPE:RAW ", 3, "RAW family filter did not use the established classifier");
        Expect(pane.GetFilterQuery() == L"TYPE:RAW", "Type filter did not preserve trimmed query text");
        expectCount(L"type:nef", 1, "Exact type filter included another RAW extension");
        expectCount(L"type:.NRW", 1, "Dotted case-insensitive type filter did not match NRW");
        expectCount(L"type:raw rating:unrated", 2, "RAW type did not compose with unrated filtering");
        expectCount(L"type:raw rating:>=4 tag:keeper", 1, "RAW type did not compose with rating and tags");
        expectCount(L"type:jpg tag:keeper", 1, "Exact type did not compose with tags");
        expectCount(L"type:raw type:nef", 1, "Repeated type terms were not conjunctive");
        expectCount(L"type:raw type:jpg", 0, "Contradictory type terms silently broadened matches");
        expectCount(L"type:unsupported", 0, "Unsupported type token silently broadened matches");
        expectCount(L"type:", 0, "Empty type token silently broadened matches");
        expectCount(L"raw-in-name", 1, "Type support regressed plain filename filtering");
        expectCount(L"rating:>=4 tag:keeper", 2, "Type support regressed rating/tag filtering");
        expectCount(L"", 5, "Clearing the type filter did not restore all items");
        pane.SetViewMode(hyperbrowse::browser::BrowserViewMode::Details);
        expectCount(L"type:raw", 3, "Type filtering did not apply in details mode");
        HWND listView = FindWindowExW(pane.Hwnd(), nullptr, WC_LISTVIEWW, nullptr);
        Expect(listView && ListView_GetItemCount(listView) == 3,
               "Type filter did not synchronize the details row count");
        expectCount(L"", 5, "Clearing a details type filter did not restore all items");
        DestroyWindow(hostWindow);
    }
}
