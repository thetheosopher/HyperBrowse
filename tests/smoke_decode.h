#pragma once

#include <windows.h>
#include <string>

namespace hyperbrowse::tests
{
    void RunDecodePolicyScenarios();
    void RunCodecReadinessScenarios();
    void RunCodecReadinessWindowScenarios(HINSTANCE instance, HWND owner);
    std::wstring CaptureInstalledWicCodecReport();
    void RunColorManagementScenarios();
    void RunColorManagementWindowScenarios(HINSTANCE instance, HWND owner);
}
