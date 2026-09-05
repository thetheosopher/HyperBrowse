#include "ui/FileOperationJournal.h"

#include <algorithm>
#include <utility>

namespace hyperbrowse::ui
{
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

    void FileOperationJournal::Complete(UndoRedoOperation operation, bool succeeded)
    {
        if (succeeded)
        {
            if (operation == UndoRedoOperation::Undo && !undoEntries_.empty())
            {
                redoEntries_.push_back(std::move(undoEntries_.back()));
                undoEntries_.pop_back();
            }
            else if (operation == UndoRedoOperation::Redo && !redoEntries_.empty())
            {
                undoEntries_.push_back(std::move(redoEntries_.back()));
                redoEntries_.pop_back();
            }
        }

        pendingOperation_ = UndoRedoOperation::None;
    }
}
