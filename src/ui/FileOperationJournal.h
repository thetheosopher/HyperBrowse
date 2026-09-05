#pragma once

#include <cstddef>
#include <deque>
#include <string>
#include <vector>

namespace hyperbrowse::ui
{
    struct FileOperationJournalEntry
    {
        int type{};
        std::vector<std::wstring> sourcePaths;
        std::vector<std::wstring> createdPaths;
        std::wstring destinationFolder;
        std::wstring description;
    };

    enum class UndoRedoOperation
    {
        None,
        Undo,
        Redo,
    };

    class FileOperationJournal
    {
    public:
        explicit FileOperationJournal(std::size_t maximumDepth = 32);

        void Record(FileOperationJournalEntry entry);
        const FileOperationJournalEntry* UndoEntry() const noexcept;
        const FileOperationJournalEntry* RedoEntry() const noexcept;
        bool CanUndo() const noexcept;
        bool CanRedo() const noexcept;
        void Begin(UndoRedoOperation operation) noexcept;
        UndoRedoOperation PendingOperation() const noexcept;
        void CancelPending() noexcept;
        void Complete(UndoRedoOperation operation, bool succeeded);

    private:
        std::deque<FileOperationJournalEntry> undoEntries_;
        std::deque<FileOperationJournalEntry> redoEntries_;
        std::size_t maximumDepth_{};
        UndoRedoOperation pendingOperation_{UndoRedoOperation::None};
    };
}
