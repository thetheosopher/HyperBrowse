#include "smoke_metadata.h"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "services/UserMetadataStore.h"

namespace hyperbrowse::tests
{
    namespace
    {
        namespace fs = std::filesystem;

        constexpr std::chrono::seconds kSaveTimeout{10};
        constexpr wchar_t kFirstProcessPath[] = L"C:\\fixtures\\process-a.jpg";
        constexpr wchar_t kSecondProcessPath[] = L"C:\\fixtures\\process-b.jpg";

        void Expect(bool condition, const std::string& message)
        {
            if (!condition)
            {
                throw std::runtime_error(message);
            }
        }

        class UniqueHandle final
        {
        public:
            UniqueHandle() = default;
            explicit UniqueHandle(HANDLE handle) noexcept : handle_(handle) {}
            ~UniqueHandle()
            {
                if (handle_ && handle_ != INVALID_HANDLE_VALUE)
                {
                    CloseHandle(handle_);
                }
            }

            UniqueHandle(const UniqueHandle&) = delete;
            UniqueHandle& operator=(const UniqueHandle&) = delete;
            UniqueHandle(UniqueHandle&&) = delete;
            UniqueHandle& operator=(UniqueHandle&&) = delete;

            HANDLE Get() const noexcept
            {
                return handle_;
            }

            explicit operator bool() const noexcept
            {
                return handle_ && handle_ != INVALID_HANDLE_VALUE;
            }

            void Reset() noexcept
            {
                if (*this)
                {
                    CloseHandle(handle_);
                }
                handle_ = INVALID_HANDLE_VALUE;
            }

        private:
            HANDLE handle_{INVALID_HANDLE_VALUE};
        };

        fs::path SystemTempDirectory()
        {
            std::vector<wchar_t> buffer(32768);
            const DWORD length = GetTempPathW(static_cast<DWORD>(buffer.size()), buffer.data());
            Expect(length > 0 && static_cast<std::size_t>(length) < buffer.size(), "Could not resolve the metadata test temp directory");
            return fs::path(std::wstring(buffer.data(), length));
        }

        fs::path SharedProcessRoot(std::wstring_view token)
        {
            return SystemTempDirectory() / (L"HyperBrowseMetadataProcesses-" + std::wstring(token));
        }

        class TempMetadataDirectory final
        {
        public:
            explicit TempMetadataDirectory(std::wstring_view label)
            {
                static unsigned long counter = 0;
                root_ = SystemTempDirectory()
                    / (L"HyperBrowseMetadata-"
                       + std::to_wstring(GetCurrentProcessId())
                       + L"-"
                       + std::to_wstring(GetTickCount64())
                       + L"-"
                       + std::to_wstring(++counter)
                       + L"-"
                       + std::wstring(label));
                std::error_code error;
                fs::create_directories(root_, error);
                Expect(!error, "Could not create an isolated metadata test directory");
            }

            ~TempMetadataDirectory()
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

        void WriteBytes(const fs::path& path, std::string_view bytes)
        {
            UniqueHandle file(CreateFileW(
                path.c_str(),
                GENERIC_WRITE,
                0,
                nullptr,
                CREATE_ALWAYS,
                FILE_ATTRIBUTE_NORMAL,
                nullptr));
            Expect(static_cast<bool>(file), "Could not create a metadata test file");
            DWORD written = 0;
            Expect(bytes.size() <= static_cast<std::size_t>(MAXDWORD)
                       && WriteFile(file.Get(), bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) != FALSE
                       && static_cast<std::size_t>(written) == bytes.size(),
                   "Could not write a metadata test file");
        }

        std::string ReadBytes(const fs::path& path)
        {
            UniqueHandle file(CreateFileW(
                path.c_str(),
                GENERIC_READ,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                nullptr,
                OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL,
                nullptr));
            Expect(static_cast<bool>(file), "Could not open a metadata test file");
            LARGE_INTEGER size{};
            Expect(GetFileSizeEx(file.Get(), &size) != FALSE && size.QuadPart >= 0 && size.QuadPart <= MAXDWORD,
                   "Could not size a metadata test file");
            std::string bytes(static_cast<std::size_t>(size.QuadPart), '\0');
            DWORD read = 0;
            Expect(bytes.empty()
                       || (ReadFile(file.Get(), bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) != FALSE
                           && static_cast<std::size_t>(read) == bytes.size()),
                   "Could not read a metadata test file");
            return bytes;
        }

        void FlushStore(services::UserMetadataStore* store, const std::string& message)
        {
            std::wstring error;
            if (!store->Flush(kSaveTimeout, &error))
            {
                std::string readableError;
                readableError.reserve(error.size());
                for (const wchar_t character : error)
                {
                    readableError.push_back(character >= 0x20 && character <= 0x7e
                        ? static_cast<char>(character)
                        : '?');
                }
                throw std::runtime_error(message + ": " + readableError);
            }
            Expect(error.empty() && store->LastSaveError().empty(), "A successful metadata save retained an error");
        }

        bool WaitForPath(const fs::path& path, DWORD timeoutMs)
        {
            const ULONGLONG deadline = GetTickCount64() + timeoutMs;
            std::error_code error;
            while (GetTickCount64() < deadline)
            {
                if (fs::exists(path, error) && !error)
                {
                    return true;
                }
                error.clear();
                Sleep(10);
            }
            return false;
        }

        void RunUnicodeAndMigrationScenario()
        {
            TempMetadataDirectory directory(L"unicode");
            const std::wstring unicodePath = L"C:\\fixtures\\日本\\café-😀.jpg";
            const std::wstring escapedPath = L"C:\\fixtures\\tab\tline\nphoto.jpg";
            {
                services::UserMetadataStore store(directory.Root().wstring());
                store.SetRating({unicodePath, escapedPath}, 5);
                store.SetTags({unicodePath}, L"café; 日本; 😀; slash\\tag");
                FlushStore(&store, "Unicode metadata did not save");
            }

            const std::string bytes = ReadBytes(directory.Root() / L"user-metadata.tsv");
            Expect(bytes.starts_with("# HyperBrowse user metadata v2; encoding=utf-8\n"),
                   "Metadata did not use the versioned UTF-8 schema");
            Expect(bytes.find("caf\xC3\xA9") != std::string::npos,
                   "Metadata did not contain UTF-8 accented text");

            {
                services::UserMetadataStore reopened(directory.Root().wstring());
                const services::UserMetadataEntry unicodeEntry = reopened.EntryForPath(unicodePath);
                Expect(unicodeEntry.rating == 5
                           && unicodeEntry.tags.find(L"café") != std::wstring::npos
                           && unicodeEntry.tags.find(L"日本") != std::wstring::npos
                           && unicodeEntry.tags.find(L"😀") != std::wstring::npos
                           && unicodeEntry.tags.find(L"slash\\tag") != std::wstring::npos,
                       "Unicode ratings or tags did not survive restart");
                Expect(reopened.EntryForPath(escapedPath).rating == 5,
                       "Escaped path characters did not survive restart");
            }

            TempMetadataDirectory legacyDirectory(L"legacy");
            WriteBytes(
                legacyDirectory.Root() / L"user-metadata.tsv",
                "c:\\\\fixtures\\\\legacy.jpg\t4\told\\ttag\n");
            {
                services::UserMetadataStore legacy(legacyDirectory.Root().wstring());
                const services::UserMetadataEntry entry = legacy.EntryForPath(L"C:\\fixtures\\legacy.jpg");
                Expect(entry.rating == 4 && entry.tags == L"old\ttag",
                       "Readable legacy metadata was not migrated (rating="
                           + std::to_string(entry.rating)
                           + ", tag-length="
                           + std::to_string(entry.tags.size())
                           + ")");
                legacy.SetRating({L"C:\\fixtures\\new.jpg"}, 3);
                FlushStore(&legacy, "Migrated legacy metadata did not save");
            }
            Expect(ReadBytes(legacyDirectory.Root() / L"user-metadata.tsv").starts_with(
                       "# HyperBrowse user metadata v2; encoding=utf-8\n"),
                   "A legacy metadata update did not publish the versioned schema");
        }

        void RunPathRemappingScenario()
        {
            TempMetadataDirectory directory(L"remapping");
            services::UserMetadataStore store(directory.Root().wstring());
            const std::wstring caseSource = L"C:\\Pictures\\Photo.JPG";
            const std::wstring caseDestination = L"c:\\pictures\\photo.jpg";
            store.SetRating({caseSource}, 4);
            store.ApplyFileOperationUpdate(
                services::FileOperationType::Rename,
                {caseSource},
                {caseDestination});
            Expect(store.EntryForPath(caseDestination).rating == 4,
                   "A case-only rename erased metadata");

            const std::wstring folderSource = L"C:\\old-日本";
            const std::wstring folderDestination = L"C:\\new-日本";
            const std::wstring nestedSource = folderSource + L"\\nested\\photo.jpg";
            const std::wstring sibling = L"C:\\old-日本2\\photo.jpg";
            store.SetRating({nestedSource}, 5);
            store.SetRating({sibling}, 2);
            store.ApplyFileOperationUpdate(
                services::FileOperationType::Move,
                {folderSource},
                {folderDestination});
            const std::wstring nestedDestination = folderDestination + L"\\nested\\photo.jpg";
            Expect(store.EntryForPath(nestedSource).rating == 0
                       && store.EntryForPath(nestedDestination).rating == 5
                       && store.EntryForPath(sibling).rating == 2,
                   "Folder metadata remapping crossed a path boundary or lost a descendant");

            const std::wstring copyDestination = L"C:\\copy-日本";
            store.ApplyFileOperationUpdate(
                services::FileOperationType::Copy,
                {folderDestination},
                {copyDestination});
            const std::wstring copiedNested = copyDestination + L"\\nested\\photo.jpg";
            Expect(store.EntryForPath(nestedDestination).rating == 5
                       && store.EntryForPath(copiedNested).rating == 5,
                   "Folder-copy metadata did not retain both source and destination entries");

            store.ApplyFileOperationUpdate(
                services::FileOperationType::DeletePermanent,
                {copyDestination},
                {});
            Expect(store.EntryForPath(copiedNested).rating == 0,
                   "Folder deletion retained descendant metadata");

            std::vector<std::wstring> rehashPaths;
            rehashPaths.reserve(1024);
            for (int index = 0; index < 1024; ++index)
            {
                rehashPaths.push_back(L"C:\\rehash\\item-" + std::to_wstring(index) + L".jpg");
            }
            store.SetRating(rehashPaths, 3);
            store.ApplyFileOperationUpdate(
                services::FileOperationType::Rename,
                {rehashPaths.front()},
                {L"C:\\rehash\\renamed.jpg"});
            Expect(store.EntryForPath(rehashPaths.front()).rating == 0
                       && store.EntryForPath(L"C:\\rehash\\renamed.jpg").rating == 3,
                   "Metadata rename failed at a map-growth boundary");
            FlushStore(&store, "Remapped metadata did not save");
        }

        void RunSaveFailureAndRetryScenario()
        {
            TempMetadataDirectory directory(L"failure");
            const fs::path metadataPath = directory.Root() / L"user-metadata.tsv";
            const std::wstring firstPath = L"C:\\fixtures\\durable.jpg";
            const std::wstring secondPath = L"C:\\fixtures\\retry.jpg";
            services::UserMetadataStore store(directory.Root().wstring());
            store.SetRating({firstPath}, 3);
            FlushStore(&store, "Initial metadata snapshot did not save");
            const std::string lastGoodBytes = ReadBytes(metadataPath);

            UniqueHandle replacementBlocker(CreateFileW(
                metadataPath.c_str(),
                GENERIC_READ,
                FILE_SHARE_READ,
                nullptr,
                OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL,
                nullptr));
            Expect(static_cast<bool>(replacementBlocker), "Could not block metadata replacement for failure coverage");
            store.SetRating({secondPath}, 4);
            std::wstring saveError;
            Expect(!store.Flush(kSaveTimeout, &saveError)
                       && !saveError.empty()
                       && !store.LastSaveError().empty(),
                   "A failed metadata replacement was acknowledged as durable");
            Expect(ReadBytes(metadataPath) == lastGoodBytes,
                   "A failed metadata replacement changed the last good file");

            replacementBlocker.Reset();
            {
                services::UserMetadataStore lastGood(directory.Root().wstring());
                Expect(lastGood.EntryForPath(firstPath).rating == 3
                           && lastGood.EntryForPath(secondPath).rating == 0,
                       "A failed metadata replacement leaked into the durable snapshot");
            }

            store.SetTags({secondPath}, L"retry");
            FlushStore(&store, "A later metadata edit did not retry retained dirty changes");
            {
                services::UserMetadataStore reopened(directory.Root().wstring());
                const services::UserMetadataEntry retried = reopened.EntryForPath(secondPath);
                Expect(reopened.EntryForPath(firstPath).rating == 3
                           && retried.rating == 4
                           && retried.tags == L"retry",
                       "Retained dirty metadata did not survive a successful retry");
            }

            for (int index = 0; index < 20; ++index)
            {
                services::UserMetadataStore cycle(directory.Root().wstring());
                cycle.SetRating({L"C:\\fixtures\\cycle.jpg"}, (index % 5) + 1);
                FlushStore(&cycle, "Repeated metadata create/save/destroy failed");
            }
        }

        std::wstring CurrentExecutablePath()
        {
            std::vector<wchar_t> path(32768);
            const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
            Expect(length > 0 && static_cast<std::size_t>(length) < path.size(), "Could not resolve the metadata test executable");
            return std::wstring(path.data(), length);
        }

        UniqueHandle LaunchMetadataChild(std::wstring_view token, int childIndex)
        {
            std::wstring commandLine = L"\"" + CurrentExecutablePath() + L"\" --user-metadata-child ";
            commandLine.append(token);
            commandLine.push_back(L' ');
            commandLine.append(std::to_wstring(childIndex));
            std::vector<wchar_t> mutableCommandLine(commandLine.begin(), commandLine.end());
            mutableCommandLine.push_back(L'\0');

            STARTUPINFOW startupInfo{sizeof(startupInfo)};
            PROCESS_INFORMATION processInfo{};
            const BOOL created = CreateProcessW(
                nullptr,
                mutableCommandLine.data(),
                nullptr,
                nullptr,
                FALSE,
                CREATE_NO_WINDOW,
                nullptr,
                nullptr,
                &startupInfo,
                &processInfo);
            Expect(created != FALSE, "Could not launch an independent metadata writer process");
            CloseHandle(processInfo.hThread);
            return UniqueHandle(processInfo.hProcess);
        }

        void RunIndependentProcessScenario()
        {
            const std::wstring token = std::to_wstring(GetCurrentProcessId())
                + L"-"
                + std::to_wstring(GetTickCount64());
            const fs::path root = SharedProcessRoot(token);
            std::error_code error;
            fs::create_directories(root, error);
            Expect(!error, "Could not create the cross-process metadata directory");

            {
                services::UserMetadataStore seed(root.wstring());
                seed.SetTags({L"C:\\fixtures\\seed.jpg"}, L"seed");
                FlushStore(&seed, "Could not seed cross-process metadata");
            }

            UniqueHandle first = LaunchMetadataChild(token, 1);
            UniqueHandle second = LaunchMetadataChild(token, 2);
            Expect(WaitForPath(root / L"ready-1", 10000)
                       && WaitForPath(root / L"ready-2", 10000),
                   "Independent metadata writers did not load the common snapshot");
            WriteBytes(root / L"go", "go");

            const HANDLE processes[]{first.Get(), second.Get()};
            Expect(WaitForMultipleObjects(2, processes, TRUE, 15000) == WAIT_OBJECT_0,
                   "Independent metadata writers did not finish");
            DWORD firstExit = 1;
            DWORD secondExit = 1;
            Expect(GetExitCodeProcess(first.Get(), &firstExit) != FALSE
                       && GetExitCodeProcess(second.Get(), &secondExit) != FALSE
                       && firstExit == 0
                       && secondExit == 0,
                   "An independent metadata writer failed");

            {
                services::UserMetadataStore merged(root.wstring());
                Expect(merged.EntryForPath(kFirstProcessPath).rating == 4
                           && merged.EntryForPath(kSecondProcessPath).rating == 5
                           && merged.EntryForPath(L"C:\\fixtures\\seed.jpg").tags == L"seed",
                       "Independent metadata writers overwrote each other's changes");
            }
            fs::remove_all(root, error);
        }
    }

    bool IsUserMetadataChildScenario(int argc, char* argv[]) noexcept
    {
        return argc >= 4 && std::string_view(argv[1]) == "--user-metadata-child";
    }

    int RunUserMetadataChildScenario(int argc, char* argv[])
    {
        if (!IsUserMetadataChildScenario(argc, argv))
        {
            return 2;
        }
        try
        {
            std::wstring token;
            for (const char* cursor = argv[2]; *cursor != '\0'; ++cursor)
            {
                token.push_back(static_cast<wchar_t>(static_cast<unsigned char>(*cursor)));
            }
            const int childIndex = std::stoi(argv[3]);
            const fs::path root = SharedProcessRoot(token);
            std::error_code error;
            fs::create_directories(root, error);
            Expect(!error, "Child could not create its metadata directory");

            services::UserMetadataStore store(root.wstring());
            (void)store.EntryForPath(L"C:\\fixtures\\seed.jpg");
            WriteBytes(root / (L"ready-" + std::to_wstring(childIndex)), "ready");
            Expect(WaitForPath(root / L"go", 10000), "Child did not receive the metadata write signal");
            if (childIndex == 1)
            {
                store.SetRating({kFirstProcessPath}, 4);
            }
            else
            {
                store.SetRating({kSecondProcessPath}, 5);
            }
            FlushStore(&store, "Child metadata save failed");
            return 0;
        }
        catch (...)
        {
            return 1;
        }
    }

    void RunUserMetadataScenarios()
    {
        RunUnicodeAndMigrationScenario();
        RunPathRemappingScenario();
        RunSaveFailureAndRetryScenario();
        RunIndependentProcessScenario();
    }
}
