#pragma once

#include "viewer/ViewerOverlay.h"
#include "util/UiTextSize.h"

namespace hyperbrowse::viewer
{
    using ApplyViewerOverlays = std::function<bool(const ViewerOverlaySettings&)>;
    bool ShowViewerOverlayDialog(HWND owner, HINSTANCE instance, bool darkTheme, util::AppTextSize appTextSize,
                                 ViewerOverlaySettings settings, SIZE previewViewport, UINT previewDpi,
                                 const ApplyViewerOverlays& apply);
}
