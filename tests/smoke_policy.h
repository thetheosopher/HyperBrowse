#pragma once

#include <string_view>

namespace hyperbrowse::tests
{
    void RunPolicyScenarios();
    bool RunFocusedPolicyScenario(std::string_view scenario);
}
