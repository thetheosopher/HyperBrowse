#pragma once

#include <windows.h>

#include <array>
#include <functional>
#include <string_view>

namespace hyperbrowse::viewer
{
    enum class InfoOverlayTextSize : int { Small, Medium, Large };
    enum class OverlayPane : std::size_t { FileInformation, ImageStatus, FullMetadata, Histogram, Count };
    enum class OverlayCorner : int { TopLeft, TopRight, BottomLeft, BottomRight };
    enum class OverlayImportance : int { Low, Normal, High };
    enum class OverlayVisibility { Disabled, MasterHidden, HiddenToFit, Shown };

    inline constexpr std::size_t kOverlayPaneCount = static_cast<std::size_t>(OverlayPane::Count);

    struct OverlayPaneSettings
    {
        bool windowedEnabled{true};
        bool fullScreenEnabled{true};
        OverlayCorner corner{OverlayCorner::TopLeft};
        int order{};
        OverlayImportance importance{OverlayImportance::High};
        bool operator==(const OverlayPaneSettings&) const = default;
    };

    struct ViewerOverlaySettings
    {
        bool masterVisible{true};
        InfoOverlayTextSize textSize{InfoOverlayTextSize::Small};
        std::array<OverlayPaneSettings, kOverlayPaneCount> panes{{
            {true, true, OverlayCorner::TopLeft, 0, OverlayImportance::High},
            {true, true, OverlayCorner::BottomRight, 0, OverlayImportance::High},
            {false, false, OverlayCorner::TopRight, 0, OverlayImportance::Low},
            {false, false, OverlayCorner::BottomLeft, 0, OverlayImportance::Normal},
        }};
        bool operator==(const ViewerOverlaySettings&) const = default;
    };

    struct OverlayPlacement
    {
        RECT rect{};
        OverlayVisibility visibility{OverlayVisibility::Disabled};
    };
    using OverlayLayout = std::array<OverlayPlacement, kOverlayPaneCount>;
    using OverlaySizes = std::array<SIZE, kOverlayPaneCount>;

    ViewerOverlaySettings NormalizeOverlaySettings(ViewerOverlaySettings settings);
    void MoveOverlayPane(ViewerOverlaySettings& settings, OverlayPane pane, bool towardCorner);
    OverlayLayout CalculateOverlayLayout(SIZE viewport, const ViewerOverlaySettings& settings,
                                         bool fullScreen, const OverlaySizes& sizes, int margin, int gap);
    const wchar_t* OverlayPaneName(OverlayPane pane) noexcept;

    class ViewerOverlayPersistence final
    {
    public:
        using ReadDword = std::function<bool(std::wstring_view, DWORD*)>;
        using WriteDword = std::function<void(std::wstring_view, DWORD)>;
        static ViewerOverlaySettings Load(const ReadDword& read);
        static void Save(const ViewerOverlaySettings& settings, const WriteDword& write);
        static ViewerOverlaySettings LoadRegistry();
        static bool SaveRegistry(const ViewerOverlaySettings& settings);
    };
}
