#include "ui/QuickAccessShortcutEditPolicy.h"

namespace hyperbrowse::ui
{
    QuickAccessShortcutEditPolicy::Result QuickAccessShortcutEditPolicy::Reconcile(
        QuickSendAssignmentResult assignmentResult,
        std::optional<int> assignedShortcut,
        std::wstring_view enteredText)
    {
        Result result;
        result.assignedShortcut = assignedShortcut.value_or(-1);

        const wchar_t shortcutCharacter = assignedShortcut
            ? QuickSendModel::ShortcutCharacter(*assignedShortcut)
            : L'\0';
        if (shortcutCharacter != L'\0')
        {
            result.canonicalText.push_back(shortcutCharacter);
        }

        if (assignmentResult != QuickSendAssignmentResult::Accepted)
        {
            result.updateText = true;
        }
        else if (assignedShortcut)
        {
            result.updateText = result.canonicalText != enteredText;
        }

        return result;
    }
}
