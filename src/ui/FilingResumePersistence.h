#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace hyperbrowse::ui
{
    struct FilingResumeFileIdentity
    {
        std::uint64_t volumeSerial{};
        std::uint64_t fileIndex{};

        bool IsValid() const noexcept
        {
            return volumeSerial != 0 || fileIndex != 0;
        }
    };

    struct FilingResumeRecord
    {
        std::wstring folderPath;
        FilingResumeFileIdentity folderIdentity;
        std::wstring targetPath;
        FilingResumeFileIdentity targetIdentity;
        int operationType{};
    };

    struct FilingResumePersistedState
    {
        std::vector<FilingResumeRecord> records;
    };

    class FilingResumePersistence final
    {
    public:
        static constexpr std::size_t kMaxRecordCount = 64;
        using ReadValue = std::function<bool(std::wstring_view valueName, std::wstring* value)>;
        using WriteValue = std::function<void(std::wstring_view valueName, std::wstring_view value)>;
        using DeleteValue = std::function<void(std::wstring_view valueName)>;

        static FilingResumePersistedState Load(const ReadValue& readValue);
        static void Save(const FilingResumePersistedState& state,
                         const WriteValue& writeValue,
                         const DeleteValue& deleteValue);
        static bool TryGetFileIdentity(std::wstring_view path,
                                       FilingResumeFileIdentity* identity);
    };
}
