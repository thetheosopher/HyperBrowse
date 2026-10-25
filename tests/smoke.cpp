#include "smoke_support.h"
#include "smoke_scenarios.h"

#include "smoke_policy.h"
#include "smoke_decode.h"
#include "smoke_model.h"
#include "smoke_metadata.h"
#include "smoke_runtime.h"
#include "smoke_watch.h"

#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

using namespace hyperbrowse::tests::smoke;

int main(int argc, char* argv[])
{
    if (hyperbrowse::tests::IsUserMetadataChildScenario(argc, argv))
    {
        return hyperbrowse::tests::RunUserMetadataChildScenario(argc, argv);
    }

    try
    {
        ComScope comScope;
        ScopedSmokeEnvironmentCleanup smokeEnvironmentCleanup;
        HINSTANCE instance = GetModuleHandleW(nullptr);
        Expect(ConfigureSmokeEnvironment(), "Failed to configure the isolated smoke-test environment");
        {
            hyperbrowse::cache::DiskThumbnailCache environmentCache(1024 * 1024);
            Expect(fs::path(environmentCache.QueryStatistics().cacheDirectory) == gSmokeCacheDirectory,
                   "Default persistent cache did not use the isolated smoke-test directory");
            const fs::path explicitDirectory = gSmokeCacheDirectory / L"explicit";
            hyperbrowse::cache::DiskThumbnailCache explicitCache(1024 * 1024, explicitDirectory.wstring());
            Expect(fs::path(explicitCache.QueryStatistics().cacheDirectory) == explicitDirectory,
                   "Environment cache directory overrode an explicit cache directory");
        }
        INITCOMMONCONTROLSEX commonControls{};
        commonControls.dwSize = sizeof(commonControls);
        commonControls.dwICC = ICC_LISTVIEW_CLASSES | ICC_TREEVIEW_CLASSES | ICC_BAR_CLASSES | ICC_STANDARD_CLASSES;
        InitCommonControlsEx(&commonControls);

        TestWindowState state{};
        HWND hwnd = CreateTestWindow(&state, instance);
        Expect(hwnd != nullptr, "Failed to create the hidden test window");

        const bool viewerFitOnly = argc > 1 && std::string_view(argv[1]) == "--viewer-fit";
        const bool viewerInteractionOnly = argc > 1 && std::string_view(argv[1]) == "--viewer-interaction";
        const bool jpegOrientationOnly = argc > 1 && std::string_view(argv[1]) == "--jpeg-orientation";
        const bool codecReadinessOnly = argc > 1 && std::string_view(argv[1]) == "--codec-readiness";
        const bool codecReadinessWindowOnly = argc > 1 && std::string_view(argv[1]) == "--codec-readiness-window";
        const bool codecInventoryOnly = argc > 1 && std::string_view(argv[1]) == "--codec-inventory";
        const bool runtimeOnly = argc > 1 && std::string_view(argv[1]) == "--runtime";
        const bool singleInstanceOnly = argc > 1 && std::string_view(argv[1]) == "--single-instance";
        const bool savedSearchesOnly = argc > 1 && std::string_view(argv[1]) == "--saved-searches";
        const bool colorManagementOnly = argc > 1 && std::string_view(argv[1]) == "--color-management";
        const bool thumbnailPersistenceOnly = argc > 1 && std::string_view(argv[1]) == "--thumbnail-persistence";
        const bool thumbnailPathSafetyOnly = argc > 1 && std::string_view(argv[1]) == "--thumbnail-path-safety";
        const bool thumbnailMaintenanceOnly = argc > 1 && std::string_view(argv[1]) == "--thumbnail-maintenance";
        const bool thumbnailStaleCompletionOnly = argc > 1 && std::string_view(argv[1]) == "--thumbnail-stale-completion";
        const bool thumbnailFailureOnly = argc > 1 && std::string_view(argv[1]) == "--thumbnail-failure";
        const bool fileRenameOnly = argc > 1 && std::string_view(argv[1]) == "--file-rename";
        const bool appTextSizeOnly = argc > 1 && std::string_view(argv[1]) == "--app-text-size";
        const bool folderTreeRestoreOnly = argc > 1 && std::string_view(argv[1]) == "--folder-tree-restore";
        const bool folderTreeLargeOnly = argc > 1 && std::string_view(argv[1]) == "--folder-tree-large";
        const bool accessibilityOnly = argc > 1 && std::string_view(argv[1]) == "--accessibility";
        const bool dialogGeometryOnly = argc > 1 && std::string_view(argv[1]) == "--dialog-geometry";
        const bool settingsOnly = argc > 1 && std::string_view(argv[1]) == "--settings";
        const bool multiViewerSettingsOnly = argc > 1 && std::string_view(argv[1]) == "--multi-viewer-settings";
        const bool userMetadataOnly = argc > 1 && std::string_view(argv[1]) == "--user-metadata";
        const bool externalDropTargetOnly = argc > 1 && std::string_view(argv[1]) == "--external-drop-target";
        const bool nvJpegHardwareOnly = argc > 1 && std::string_view(argv[1]) == "--nvjpeg-hardware";
        const std::string_view selectedScenario = argc > 1 ? std::string_view(argv[1]) : std::string_view{};
        const bool policyOnly = hyperbrowse::tests::RunFocusedPolicyScenario(selectedScenario);
        if (policyOnly)
        {
        }
        else if (codecReadinessOnly)
        {
            hyperbrowse::tests::RunCodecReadinessScenarios();
        }
        else if (codecReadinessWindowOnly)
        {
            hyperbrowse::tests::RunCodecReadinessWindowScenarios(instance, hwnd);
        }
        else if (codecInventoryOnly)
        {
            std::wcout << hyperbrowse::tests::CaptureInstalledWicCodecReport();
        }
        else if (runtimeOnly)
        {
            hyperbrowse::tests::RunRuntimeScenarios();
        }
        else if (singleInstanceOnly)
        {
            RunSingleInstanceIdleClientScenario();
            RunResidentSingleInstanceScenario();
        }
        else if (savedSearchesOnly)
        {
            RunSavedSearchStoreScenario();
            RunSavedSearchControllerScenario(instance);
            RunSavedSearchMainWindowScenario(instance);
            RunSavedSearchFilterScenario(instance);
        }
        else if (colorManagementOnly)
        {
            hyperbrowse::tests::RunColorManagementScenarios();
            hyperbrowse::tests::RunColorManagementWindowScenarios(instance, hwnd);
            RunColorManagementSettingsScenario(instance);
        }
        else if (viewerFitOnly)
        {
            RunViewerWindowFitModeScenario(instance, hwnd);
        }
        else if (viewerInteractionOnly)
        {
            RunViewerWindowScenario(instance, hwnd);
        }
        else if (jpegOrientationOnly)
        {
            RunJpegOrientationAdjustmentScenario();
        }
        else if (thumbnailPersistenceOnly)
        {
            RunThumbnailReadyBeforePersistenceScenario(hwnd, &state);
        }
        else if (thumbnailPathSafetyOnly)
        {
            RunPersistentCacheCollisionAndLongPathScenario();
        }
        else if (thumbnailMaintenanceOnly)
        {
            RunThumbnailPersistenceMaintenanceScenario();
        }
        else if (thumbnailStaleCompletionOnly)
        {
            RunThumbnailStaleCompletionScenario(hwnd, &state);
            RunThumbnailDiskLookupRescheduleScenario(hwnd, &state);
        }
        else if (thumbnailFailureOnly)
        {
            RunThumbnailSchedulerFailureScenario(hwnd, &state);
        }
        else if (fileRenameOnly)
        {
            RunFileRenameOperationScenario(hwnd, &state);
        }
        else if (appTextSizeOnly)
        {
            RunAppTextSizeScenario(instance);
        }
        else if (folderTreeRestoreOnly)
        {
            RunMainWindowFolderTreeScenario(instance);
        }
        else if (folderTreeLargeOnly)
        {
            RunMainWindowFolderTreeLargeScenario(instance);
        }
        else if (accessibilityOnly)
        {
            RunMainWindowAccessibilityScenario(instance);
        }
        else if (dialogGeometryOnly)
        {
            RunDialogDpiHelperScenario();
            RunDialogShellGeometryScenario();
            RunDialogGeometryScenario();
            RunSettingsLayoutGeometryScenario();
        }
        else if (settingsOnly)
        {
            RunDialogDpiHelperScenario();
            RunDialogShellGeometryScenario();
            RunSettingsLayoutGeometryScenario();
            RunDefaultSettingsScenario(instance);
            RunBackgroundServiceRetirementScenario();
        }
        else if (multiViewerSettingsOnly)
        {
            RunMultiViewerSettingsScenario(instance);
        }
        else if (userMetadataOnly)
        {
            hyperbrowse::tests::RunUserMetadataScenarios();
        }
        else if (externalDropTargetOnly)
        {
            RunExternalDropTargetDataObjectLifetimeScenario();
        }
        else if (nvJpegHardwareOnly)
        {
            Expect(argc > 2, "--nvjpeg-hardware requires a JPEG fixture path");
            RunNvJpegHardwareScenario(fs::path(argv[2]));
        }
        else
        {
            hyperbrowse::tests::RunPolicyScenarios();
            hyperbrowse::tests::RunRuntimeScenarios();
            RunSingleInstanceIdleClientScenario();
            RunResidentSingleInstanceScenario();
            RunEnumerationScenario(hwnd, &state);
            RunFolderTreeEnumerationScenario(hwnd, &state);
            RunFolderWatchStartStopScenario(hwnd);
            hyperbrowse::tests::RunWatchPolicyScenarios();
            RunThumbnailCacheNormalizationScenario();
            RunDiskThumbnailCacheCorruptionScenario();
            RunPersistentCacheCollisionAndLongPathScenario();
            RunRedactedDiagnosticsExportScenario();
            RunWicDecoderScenario();
            hyperbrowse::tests::RunColorManagementWindowScenarios(instance, hwnd);
            hyperbrowse::tests::RunCodecReadinessWindowScenarios(instance, hwnd);
            RunColorManagementSettingsScenario(instance);
            RunWicErrorReportingScenario();
            RunJpegOrientationAdjustmentScenario();
            RunImageCommandServiceScenario(hwnd, &state);
            RunBatchConvertCancellationScenario(hwnd, &state);
            RunFileRenameOperationScenario(hwnd, &state);
            RunFileOperationShutdownScenario();
            RunFileConflictPlanningScenario(hwnd, &state);
            RunThumbnailSchedulerWorkerAllocationScenario();
            RunPersistentThumbnailCacheCapacityScenario();
            RunThumbnailPersistenceMaintenanceScenario();
            RunThumbnailSchedulerScenario(hwnd, &state);
            RunThumbnailReadyBeforePersistenceScenario(hwnd, &state);
            RunThumbnailStaleCompletionScenario(hwnd, &state);
            RunThumbnailDiskLookupRescheduleScenario(hwnd, &state);
            RunThumbnailSchedulerFailureScenario(hwnd, &state);
            RunImageMetadataServiceScenario();
            RunSwarmUiMetadataExtractionScenario();
            hyperbrowse::tests::RunDecodePolicyScenarios();
            RunRawDecoderScenario();
            RunSavedSearchStoreScenario();
            RunSavedSearchControllerScenario(instance);
            RunSavedSearchMainWindowScenario(instance);
            RunSavedSearchFilterScenario(instance);
            RunBrowserPaneScenario(instance);
            hyperbrowse::tests::RunModelScenarios();
            RunViewerWindowScenario(instance, hwnd);
            RunAppTextSizeScenario(instance);
            RunMainWindowCascadeScenario(instance);
            RunStartupViewerEnumerationScenario(instance);
            RunMainWindowFolderTreeScenario(instance);
            RunMainWindowKeyboardFocusScenario(instance);
            RunMainWindowAccessibilityScenario(instance);
            RunMainWindowTextInputAcceleratorScenario(instance);
            RunExternalDropTargetDataObjectLifetimeScenario();
        }

        DestroyWindow(hwnd);
        UnregisterClassW(kTestWindowClassName, instance);

        std::cout << "HyperBrowse thumbnail pipeline smoke tests passed\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "HyperBrowse smoke test failed: " << exception.what() << '\n';
        return 1;
    }
}
