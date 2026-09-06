#include "ui/QuickAccessPathList.h"

#include <algorithm>
#include <cwchar>
#include <utility>

namespace hyperbrowse::ui
{
    namespace
    {
        std::wstring NormalizeFolderPath(std::wstring path)
        {
            std::replace(path.begin(), path.end(), L'/', L'\\');
            while (path.size() > 3 && !path.empty() && path.back() == L'\\')
            {
                path.pop_back();
            }

            if (path.size() == 2 && path[1] == L':')
            {
                path.push_back(L'\\');
            }

            return path;
        }

        bool FolderPathsEqual(std::wstring_view lhs, std::wstring_view rhs)
        {
            const std::wstring normalizedLeft = NormalizeFolderPath(std::wstring(lhs));
            const std::wstring normalizedRight = NormalizeFolderPath(std::wstring(rhs));
            return _wcsicmp(normalizedLeft.c_str(), normalizedRight.c_str()) == 0;
        }
    }

    bool QuickAccessPathList::Insert(std::vector<std::wstring>* paths,
                                     std::wstring folderPath,
                                     std::size_t maxCount,
                                     bool moveToFront)
    {
        if (!paths)
        {
            return false;
        }

        folderPath = NormalizeFolderPath(std::move(folderPath));
        if (folderPath.empty())
        {
            return false;
        }

        const auto existing = std::find_if(paths->begin(), paths->end(), [&](const std::wstring& candidate)
        {
            return FolderPathsEqual(candidate, folderPath);
        });

        if (existing != paths->end())
        {
            if (!moveToFront)
            {
                return false;
            }

            if (existing == paths->begin())
            {
                return false;
            }

            paths->erase(existing);
        }

        if (moveToFront)
        {
            paths->insert(paths->begin(), std::move(folderPath));
        }
        else if (paths->size() < maxCount)
        {
            paths->push_back(std::move(folderPath));
        }
        else
        {
            return false;
        }

        if (paths->size() > maxCount)
        {
            paths->resize(maxCount);
        }

        return true;
    }

    std::vector<std::wstring> QuickAccessPathList::Deserialize(std::wstring_view serialized,
                                                               std::size_t maxCount)
    {
        std::vector<std::wstring> paths;
        std::wstring current;
        for (const wchar_t character : serialized)
        {
            if (character == L'\r')
            {
                continue;
            }

            if (character == L'\n')
            {
                Insert(&paths, std::move(current), maxCount, false);
                current.clear();
                continue;
            }

            current.push_back(character);
        }

        Insert(&paths, std::move(current), maxCount, false);
        return paths;
    }

    std::wstring QuickAccessPathList::Serialize(const std::vector<std::wstring>& paths)
    {
        std::wstring serialized;
        for (std::size_t index = 0; index < paths.size(); ++index)
        {
            if (index > 0)
            {
                serialized.push_back(L'\n');
            }

            serialized.append(paths[index]);
        }

        return serialized;
    }
}
