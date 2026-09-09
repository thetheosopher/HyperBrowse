#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hyperbrowse::ui
{
    struct FileOperationPathState
    {
        std::uint64_t volumeSerial{};
        std::uint64_t fileIndex{};
        std::uint64_t size{};
        std::uint64_t lastWriteTime{};
        std::uint32_t attributes{};
        bool valid{};

        bool operator==(const FileOperationPathState&) const = default;
    };

    bool TryCaptureFileOperationPathState(std::wstring_view path,
                                          FileOperationPathState* state) noexcept;
    std::vector<FileOperationPathState> CaptureFileOperationPathStates(
        const std::vector<std::wstring>& paths);
    bool FileOperationPathStatesMatch(const std::vector<std::wstring>& paths,
                                      const std::vector<FileOperationPathState>& expectedStates,
                                      std::wstring* changedPath = nullptr) noexcept;

    struct FileOperationJournalEntry
    {
        int type{};
        std::vector<std::wstring> sourcePaths;
        std::vector<FileOperationPathState> sourceStates;
        std::vector<std::wstring> createdPaths;
        std::vector<FileOperationPathState> createdStates;
        std::vector<std::wstring> undoLeafNames;
        std::vector<std::wstring> redoLeafNames;
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
        void Complete(UndoRedoOperation operation,
                      bool succeeded,
                      std::optional<FileOperationJournalEntry> completedEntry = std::nullopt);
        void Discard(UndoRedoOperation operation) noexcept;

    private:
        std::deque<FileOperationJournalEntry> undoEntries_;
        std::deque<FileOperationJournalEntry> redoEntries_;
        std::size_t maximumDepth_{};
        UndoRedoOperation pendingOperation_{UndoRedoOperation::None};
    };
}
