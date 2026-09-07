#include "ui/FilingResumePersistence.h"

#include <windows.h>

#include <algorithm>
#include <cerrno>
#include <cwchar>
#include <limits>
#include <string>

namespace hyperbrowse::ui
{
    namespace
    {
        constexpr wchar_t kCountValue[] = L"FilingResumeCount";
        constexpr wchar_t kFieldFolderPath[] = L"FolderPath";
        constexpr wchar_t kFieldFolderVolume[] = L"FolderVolume";
        constexpr wchar_t kFieldFolderIndex[] = L"FolderIndex";
        constexpr wchar_t kFieldTargetPath[] = L"TargetPath";
        constexpr wchar_t kFieldTargetVolume[] = L"TargetVolume";
        constexpr wchar_t kFieldTargetIndex[] = L"TargetIndex";
        constexpr wchar_t kFieldOperation[] = L"Operation";

        std::wstring ValueName(std::size_t index, std::wstring_view field)
        {
            return L"FilingResume" + std::to_wstring(index) + std::wstring(field);
        }

        bool ReadUnsigned(const FilingResumePersistence::ReadValue& readValue,
                          std::wstring_view valueName,
                          std::uint64_t* value)
        {
            std::wstring text;
            if (!value || !readValue(valueName, &text) || text.empty() || text.front() == L'-')
            {
                return false;
            }

            wchar_t* end = nullptr;
            errno = 0;
            const unsigned long long parsed = wcstoull(text.c_str(), &end, 10);
            if (!end || end == text.c_str() || *end != L'\0' || errno == ERANGE
                || parsed > (std::numeric_limits<std::uint64_t>::max)())
            {
                return false;
            }

            *value = static_cast<std::uint64_t>(parsed);
            return true;
        }

        bool ReadSigned(const FilingResumePersistence::ReadValue& readValue,
                        std::wstring_view valueName,
                        int* value)
        {
            std::wstring text;
            if (!value || !readValue(valueName, &text) || text.empty())
            {
                return false;
            }

            wchar_t* end = nullptr;
            errno = 0;
            const long parsed = wcstol(text.c_str(), &end, 10);
            if (!end || end == text.c_str() || *end != L'\0' || errno == ERANGE
                || parsed < (std::numeric_limits<int>::min)()
                || parsed > (std::numeric_limits<int>::max)())
            {
                return false;
            }

            *value = static_cast<int>(parsed);
            return true;
        }

        void WriteUnsigned(const FilingResumePersistence::WriteValue& writeValue,
                           std::wstring_view valueName,
                           std::uint64_t value)
        {
            writeValue(valueName, std::to_wstring(value));
        }
    }

    FilingResumePersistedState FilingResumePersistence::Load(const ReadValue& readValue)
    {
        FilingResumePersistedState state;
        std::uint64_t count = 0;
        if (!ReadUnsigned(readValue, kCountValue, &count))
        {
            return state;
        }

        count = (std::min)(count, static_cast<std::uint64_t>(kMaxRecordCount));
        state.records.reserve(static_cast<std::size_t>(count));
        for (std::size_t index = 0; index < static_cast<std::size_t>(count); ++index)
        {
            FilingResumeRecord record;
            if (!readValue(ValueName(index, kFieldFolderPath), &record.folderPath)
                || record.folderPath.empty())
            {
                continue;
            }

            ReadUnsigned(readValue, ValueName(index, kFieldFolderVolume), &record.folderIdentity.volumeSerial);
            ReadUnsigned(readValue, ValueName(index, kFieldFolderIndex), &record.folderIdentity.fileIndex);
            readValue(ValueName(index, kFieldTargetPath), &record.targetPath);
            ReadUnsigned(readValue, ValueName(index, kFieldTargetVolume), &record.targetIdentity.volumeSerial);
            ReadUnsigned(readValue, ValueName(index, kFieldTargetIndex), &record.targetIdentity.fileIndex);
            ReadSigned(readValue, ValueName(index, kFieldOperation), &record.operationType);
            state.records.push_back(std::move(record));
        }
        return state;
    }

    void FilingResumePersistence::Save(const FilingResumePersistedState& state,
                                       const WriteValue& writeValue,
                                       const DeleteValue& deleteValue)
    {
        const std::size_t recordCount = (std::min)(state.records.size(), kMaxRecordCount);
        writeValue(kCountValue, std::to_wstring(recordCount));

        for (std::size_t index = 0; index < kMaxRecordCount; ++index)
        {
            const auto writeField = [&](std::wstring_view field, std::wstring_view value)
            {
                const std::wstring name = ValueName(index, field);
                if (index < recordCount)
                {
                    writeValue(name, value);
                }
                else
                {
                    deleteValue(name);
                }
            };

            if (index >= recordCount)
            {
                writeField(kFieldFolderPath, {});
                writeField(kFieldTargetPath, {});
                writeField(kFieldOperation, {});
                deleteValue(ValueName(index, kFieldFolderVolume));
                deleteValue(ValueName(index, kFieldFolderIndex));
                deleteValue(ValueName(index, kFieldTargetVolume));
                deleteValue(ValueName(index, kFieldTargetIndex));
                continue;
            }

            const FilingResumeRecord& record = state.records[index];
            writeField(kFieldFolderPath, record.folderPath);
            writeField(kFieldTargetPath, record.targetPath);
            WriteUnsigned([&](std::wstring_view name, std::wstring_view value)
            {
                writeValue(ValueName(index, name), value);
            }, kFieldFolderVolume, record.folderIdentity.volumeSerial);
            WriteUnsigned([&](std::wstring_view name, std::wstring_view value)
            {
                writeValue(ValueName(index, name), value);
            }, kFieldFolderIndex, record.folderIdentity.fileIndex);
            WriteUnsigned([&](std::wstring_view name, std::wstring_view value)
            {
                writeValue(ValueName(index, name), value);
            }, kFieldTargetVolume, record.targetIdentity.volumeSerial);
            WriteUnsigned([&](std::wstring_view name, std::wstring_view value)
            {
                writeValue(ValueName(index, name), value);
            }, kFieldTargetIndex, record.targetIdentity.fileIndex);
            writeField(kFieldOperation, std::to_wstring(record.operationType));
        }
    }

    bool FilingResumePersistence::TryGetFileIdentity(std::wstring_view path,
                                                      FilingResumeFileIdentity* identity)
    {
        if (!identity || path.empty())
        {
            return false;
        }

        const std::wstring pathCopy(path);
        HANDLE handle = CreateFileW(pathCopy.c_str(),
                                    FILE_READ_ATTRIBUTES,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                    nullptr,
                                    OPEN_EXISTING,
                                    FILE_FLAG_BACKUP_SEMANTICS,
                                    nullptr);
        if (handle == INVALID_HANDLE_VALUE)
        {
            return false;
        }

        BY_HANDLE_FILE_INFORMATION information{};
        const bool success = GetFileInformationByHandle(handle, &information) != FALSE;
        CloseHandle(handle);
        if (!success)
        {
            return false;
        }

        identity->volumeSerial = information.dwVolumeSerialNumber;
        identity->fileIndex = (static_cast<std::uint64_t>(information.nFileIndexHigh) << 32)
            | information.nFileIndexLow;
        return identity->IsValid();
    }
}
