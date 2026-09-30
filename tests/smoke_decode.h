#pragma once

#include <windows.h>

namespace hyperbrowse::tests
{
    void RunDecodePolicyScenarios();
    void RunColorManagementScenarios();
    void RunColorManagementWindowScenarios(HINSTANCE instance, HWND owner);
}
