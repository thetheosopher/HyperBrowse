#pragma once

#include "ui/QuickSend.h"

#include <optional>
#include <string>
#include <string_view>

namespace hyperbrowse::ui
{
    class QuickAccessShortcutEditPolicy final
    {
    public:
        struct Result
        {
            int assignedShortcut{-1};
            std::wstring canonicalText;
            bool updateText{};
        };

        static Result Reconcile(QuickSendAssignmentResult assignmentResult,
                                std::optional<int> assignedShortcut,
                                std::wstring_view enteredText);
    };
}
