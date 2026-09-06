#include <windows.h>

#include <cstddef>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include "services/FolderWatchService.h"
#include "util/Diagnostics.h"

#include "smoke_watch.h"

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

        void AppendFolderWatchRecord(std::vector<BYTE>* buffer, DWORD action, const std::wstring& relativePath)
        {
            Expect(buffer != nullptr, "Folder watch test buffer was null");
            const std::size_t headerBytes = offsetof(FILE_NOTIFY_INFORMATION, FileName);
            const std::size_t recordBytes = headerBytes + relativePath.size() * sizeof(WCHAR);
            const std::size_t alignedRecordBytes = (recordBytes + sizeof(DWORD) - 1) & ~(sizeof(DWORD) - 1);
            const std::size_t recordOffset = buffer->size();
            buffer->resize(recordOffset + alignedRecordBytes);
            std::memset(buffer->data() + recordOffset, 0, alignedRecordBytes);

            if (recordOffset != 0)
            {
                std::size_t previousOffset = 0;
                while (true)
                {
                    auto* previous = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(buffer->data() + previousOffset);
                    if (previous->NextEntryOffset == 0)
                    {
                        previous->NextEntryOffset = static_cast<DWORD>(recordOffset - previousOffset);
                        break;
                    }
                    previousOffset += previous->NextEntryOffset;
                }
            }

            auto* record = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(buffer->data() + recordOffset);
            record->Action = action;
            record->FileNameLength = static_cast<DWORD>(relativePath.size() * sizeof(WCHAR));
            std::memcpy(record->FileName, relativePath.data(), record->FileNameLength);
        }

        void RunFolderWatchNotificationParserScenario()
        {
            constexpr wchar_t folderPath[] = L"C:\\HyperBrowseWatchTest";
            hyperbrowse::services::FolderWatchNotificationParser parser;
            const auto counterValue = [](std::wstring_view name)
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
            const std::uint64_t fallbackCountBefore = counterValue(L"folder_watch.full_reload_fallbacks");

            std::vector<BYTE> oldNameBuffer;
            AppendFolderWatchRecord(&oldNameBuffer, FILE_ACTION_RENAMED_OLD_NAME, L"old-name.jpg");
            hyperbrowse::services::FolderWatchUpdate oldNameUpdate;
            parser.Append(oldNameBuffer.data(), static_cast<DWORD>(oldNameBuffer.size()), folderPath, &oldNameUpdate);
            Expect(oldNameUpdate.events.empty() && !oldNameUpdate.requiresFullReload,
                   "Folder watcher did not retain the first half of a rename");

            std::vector<BYTE> newNameBuffer;
            AppendFolderWatchRecord(&newNameBuffer, FILE_ACTION_RENAMED_NEW_NAME, L"new-name.jpg");
            hyperbrowse::services::FolderWatchUpdate splitRenameUpdate;
            parser.Append(newNameBuffer.data(), static_cast<DWORD>(newNameBuffer.size()), folderPath, &splitRenameUpdate);
            Expect(splitRenameUpdate.events.size() == 1,
                   "Folder watcher did not pair rename records split across completions");
            Expect(splitRenameUpdate.events.front().kind == hyperbrowse::services::FolderWatchEventKind::Renamed
                       && fs::path(splitRenameUpdate.events.front().oldPath).filename() == L"old-name.jpg"
                       && fs::path(splitRenameUpdate.events.front().path).filename() == L"new-name.jpg",
                   "Folder watcher paired split rename records incorrectly");

            parser.Reset();
            std::vector<BYTE> sameBuffer;
            AppendFolderWatchRecord(&sameBuffer, FILE_ACTION_RENAMED_OLD_NAME, L"first.jpg");
            AppendFolderWatchRecord(&sameBuffer, FILE_ACTION_RENAMED_NEW_NAME, L"second.jpg");
            hyperbrowse::services::FolderWatchUpdate sameBufferUpdate;
            parser.Append(sameBuffer.data(), static_cast<DWORD>(sameBuffer.size()), folderPath, &sameBufferUpdate);
            Expect(sameBufferUpdate.events.size() == 1 && !sameBufferUpdate.requiresFullReload,
                   "Folder watcher did not pair rename records in one completion");

            parser.Reset();
            hyperbrowse::services::FolderWatchUpdate orphanNewUpdate;
            parser.Append(newNameBuffer.data(), static_cast<DWORD>(newNameBuffer.size()), folderPath, &orphanNewUpdate);
            Expect(orphanNewUpdate.requiresFullReload,
                   "Folder watcher did not request a full reload for an orphan new-name record");

            parser.Reset();
            hyperbrowse::services::FolderWatchUpdate pendingOldUpdate;
            parser.Append(oldNameBuffer.data(), static_cast<DWORD>(oldNameBuffer.size()), folderPath, &pendingOldUpdate);
            std::vector<BYTE> addedBuffer;
            AppendFolderWatchRecord(&addedBuffer, FILE_ACTION_ADDED, L"added.jpg");
            hyperbrowse::services::FolderWatchUpdate orphanOldUpdate;
            parser.Append(addedBuffer.data(), static_cast<DWORD>(addedBuffer.size()), folderPath, &orphanOldUpdate);
            Expect(orphanOldUpdate.requiresFullReload,
                   "Folder watcher did not request a full reload for an orphan old-name record");

            parser.Reset();
            hyperbrowse::services::FolderWatchUpdate resetOldUpdate;
            parser.Append(oldNameBuffer.data(), static_cast<DWORD>(oldNameBuffer.size()), folderPath, &resetOldUpdate);
            parser.Reset();
            hyperbrowse::services::FolderWatchUpdate resetNewUpdate;
            parser.Append(newNameBuffer.data(), static_cast<DWORD>(newNameBuffer.size()), folderPath, &resetNewUpdate);
            Expect(resetNewUpdate.requiresFullReload,
                   "Folder watcher did not clear rename state after reset");

            std::vector<BYTE> malformedBuffer(offsetof(FILE_NOTIFY_INFORMATION, FileName));
            auto* malformedRecord = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(malformedBuffer.data());
            malformedRecord->NextEntryOffset = 1;
            hyperbrowse::services::FolderWatchUpdate malformedUpdate;
            parser.Append(malformedBuffer.data(), static_cast<DWORD>(malformedBuffer.size()), folderPath, &malformedUpdate);
            Expect(malformedUpdate.requiresFullReload,
                   "Folder watcher did not request a full reload for a malformed notification");
                 Expect(counterValue(L"folder_watch.full_reload_fallbacks") >= fallbackCountBefore + 4,
                     "Folder watcher did not count distinct full-reload fallbacks");
        }
    }

    void RunWatchPolicyScenarios()
    {
        RunFolderWatchNotificationParserScenario();
    }
}
