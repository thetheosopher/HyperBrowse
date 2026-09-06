#include "ui/FolderTreeDropPolicy.h"

#include "browser/BrowserModel.h"
#include "util/PathUtils.h"

namespace hyperbrowse::ui
{
    bool FolderTreeDropPolicy::IsValid(const Input& input)
    {
        if (input.sourcePath.empty() || input.destinationPath.empty())
        {
            return false;
        }

        if (!input.destinationExists || !input.sameDrive)
        {
            return false;
        }

        if (hyperbrowse::util::NormalizedPathEquals(input.sourcePath, input.destinationPath)
            || hyperbrowse::browser::PathHasPrefix(input.destinationPath, input.sourcePath))
        {
            return false;
        }

        return input.sourceParentPath.empty()
            || !hyperbrowse::util::NormalizedPathEquals(input.sourceParentPath, input.destinationPath);
    }
}
