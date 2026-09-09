#include "ui/FileOperationJournal.h"

#include <windows.h>

#include <algorithm>
#include <utility>

namespace hyperbrowse::ui
{
    bool TryCaptureFileOperationPathState(std::wstring_view path,
                                          FileOperationPathState* state) noexcept
    {
        if (!state || path.empty())
        {
            return false;
        }

        *state = {};
        const std::wstring pathCopy(path);
        const HANDLE file = CreateFileW(
            pathCopy.c_str(),
            FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS,
            nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            return false;
        }

        BY_HANDLE_FILE_INFORMATION information{};
        const bool captured = GetFileInformationByHandle(file, &information) != FALSE;
        CloseHandle(file);
        if (!captured)
        {
            return false;
        }

        state->volumeSerial = information.dwVolumeSerialNumber;
        state->fileIndex = (static_cast<std::uint64_t>(information.nFileIndexHigh) << 32)
            | information.nFileIndexLow;
        state->size = (static_cast<std::uint64_t>(information.nFileSizeHigh) << 32)
            | information.nFileSizeLow;
        state->lastWriteTime = (static_cast<std::uint64_t>(information.ftLastWriteTime.dwHighDateTime) << 32)
            | information.ftLastWriteTime.dwLowDateTime;
        state->attributes = information.dwFileAttributes;
        state->valid = true;
        return true;
    }

    std::vector<FileOperationPathState> CaptureFileOperationPathStates(
        const std::vector<std::wstring>& paths)
    {
        std::vector<FileOperationPathState> states;
        states.reserve(paths.size());
        for (const std::wstring& path : paths)
        {
            FileOperationPathState state;
            TryCaptureFileOperationPathState(path, &state);
            states.push_back(state);
        }
        return states;
    }

    bool FileOperationPathStatesMatch(const std::vector<std::wstring>& paths,
                                      const std::vector<FileOperationPathState>& expectedStates,
                                      std::wstring* changedPath) noexcept
    {
        if (paths.size() != expectedStates.size())
        {
            if (changedPath)
            {
                changedPath->clear();
            }
            return false;
        }

        for (std::size_t index = 0; index < paths.size(); ++index)
        {
            FileOperationPathState current;
            if (!expectedStates[index].valid
                || !TryCaptureFileOperationPathState(paths[index], &current)
                || current != expectedStates[index])
            {
                if (changedPath)
                {
                    *changedPath = paths[index];
                }
                return false;
            }
        }
        if (changedPath)
        {
            changedPath->clear();
        }
        return true;
    }

    FileOperationJournal::FileOperationJournal(std::size_t maximumDepth)
        : maximumDepth_(std::max<std::size_t>(maximumDepth, 1))
    {
    }

    void FileOperationJournal::Record(FileOperationJournalEntry entry)
    {
        undoEntries_.push_back(std::move(entry));
        while (undoEntries_.size() > maximumDepth_)
        {
            undoEntries_.pop_front();
        }
        redoEntries_.clear();
    }

    const FileOperationJournalEntry* FileOperationJournal::UndoEntry() const noexcept
    {
        return undoEntries_.empty() ? nullptr : &undoEntries_.back();
    }

    const FileOperationJournalEntry* FileOperationJournal::RedoEntry() const noexcept
    {
        return redoEntries_.empty() ? nullptr : &redoEntries_.back();
    }

    bool FileOperationJournal::CanUndo() const noexcept
    {
        return !undoEntries_.empty();
    }

    bool FileOperationJournal::CanRedo() const noexcept
    {
        return !redoEntries_.empty();
    }

    void FileOperationJournal::Begin(UndoRedoOperation operation) noexcept
    {
        pendingOperation_ = operation;
    }

    UndoRedoOperation FileOperationJournal::PendingOperation() const noexcept
    {
        return pendingOperation_;
    }

    void FileOperationJournal::CancelPending() noexcept
    {
        pendingOperation_ = UndoRedoOperation::None;
    }

    void FileOperationJournal::Complete(
        UndoRedoOperation operation,
        bool succeeded,
        std::optional<FileOperationJournalEntry> completedEntry)
    {
        if (succeeded)
        {
            if (operation == UndoRedoOperation::Undo && !undoEntries_.empty())
            {
                redoEntries_.push_back(completedEntry
                    ? std::move(*completedEntry)
                    : std::move(undoEntries_.back()));
                undoEntries_.pop_back();
            }
            else if (operation == UndoRedoOperation::Redo && !redoEntries_.empty())
            {
                undoEntries_.push_back(completedEntry
                    ? std::move(*completedEntry)
                    : std::move(redoEntries_.back()));
                redoEntries_.pop_back();
            }
        }

        pendingOperation_ = UndoRedoOperation::None;
    }

    void FileOperationJournal::Discard(UndoRedoOperation operation) noexcept
    {
        if (operation == UndoRedoOperation::Undo && !undoEntries_.empty())
        {
            undoEntries_.pop_back();
        }
        else if (operation == UndoRedoOperation::Redo && !redoEntries_.empty())
        {
            redoEntries_.pop_back();
        }
        pendingOperation_ = UndoRedoOperation::None;
    }
}
