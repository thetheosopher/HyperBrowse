#pragma once

#include "smoke_support.h"

namespace hyperbrowse::tests::smoke
{
    void RunExternalDropTargetDataObjectLifetimeScenario();
    void RunSingleInstanceIdleClientScenario();
    void RunResidentSingleInstanceScenario();
    void RunEnumerationScenario(HWND hwnd, TestWindowState* state);
    void RunFolderTreeEnumerationScenario(HWND hwnd, TestWindowState* state);
    void RunFolderWatchStartStopScenario(HWND hwnd);
    void RunThumbnailCacheNormalizationScenario();
    void RunDiskThumbnailCacheCorruptionScenario();
    void RunPersistentCacheCollisionAndLongPathScenario();
    void RunRedactedDiagnosticsExportScenario();
    void RunWicDecoderScenario();
    void RunWicErrorReportingScenario();
    void RunJpegOrientationAdjustmentScenario();
    void RunNvJpegHardwareScenario(const fs::path& jpegPath);
    void RunImageMetadataServiceScenario();
    void RunSwarmUiMetadataExtractionScenario();
    void RunRawDecoderScenario();
    void RunImageCommandServiceScenario(HWND hwnd, TestWindowState* state);
    void RunBatchConvertCancellationScenario(HWND hwnd, TestWindowState* state);
    void RunFileRenameOperationScenario(HWND hwnd, TestWindowState* state);
    void RunFileOperationShutdownScenario();
    void RunFileConflictPlanningScenario(HWND hwnd, TestWindowState* state);
    void RunThumbnailSchedulerScenario(HWND hwnd, TestWindowState* state);
    void RunThumbnailReadyBeforePersistenceScenario(HWND hwnd, TestWindowState* state);
    void RunThumbnailStaleCompletionScenario(HWND hwnd, TestWindowState* state);
    void RunThumbnailDiskLookupRescheduleScenario(HWND hwnd, TestWindowState* state);
    void RunThumbnailSchedulerFailureScenario(HWND hwnd, TestWindowState* state);
    void RunThumbnailSchedulerWorkerAllocationScenario();
    void RunBackgroundServiceRetirementScenario();
    void RunThumbnailPersistenceMaintenanceScenario();
    void RunPersistentThumbnailCacheCapacityScenario();
    void RunSavedSearchStoreScenario();
    void RunSavedSearchControllerScenario(HINSTANCE instance);
    void RunSavedSearchMainWindowScenario(HINSTANCE instance);
    void RunSavedSearchFilterScenario(HINSTANCE instance);
    void RunBrowserPaneScenario(HINSTANCE instance);
    void RunViewerWindowFitModeScenario(HINSTANCE instance, HWND ownerWindow);
    void RunViewerWindowScenario(HINSTANCE instance, HWND ownerWindow);
    void RunAppTextSizeScenario(HINSTANCE instance);
    void RunMainWindowCascadeScenario(HINSTANCE instance);
    void RunDialogDpiHelperScenario();
    void RunDialogShellGeometryScenario();
    void RunDialogGeometryScenario();
    void RunSettingsLayoutGeometryScenario();
    void RunDefaultSettingsScenario(HINSTANCE instance);
    void RunColorManagementSettingsScenario(HINSTANCE instance);
    void RunMultiViewerSettingsScenario(HINSTANCE instance);
    void RunMainWindowFolderTreeScenario(HINSTANCE instance);
    void RunMainWindowKeyboardFocusScenario(HINSTANCE instance);
    void RunMainWindowAccessibilityScenario(HINSTANCE instance);
    void RunMainWindowTextInputAcceleratorScenario(HINSTANCE instance);
    void RunStartupViewerEnumerationScenario(HINSTANCE instance);
}
