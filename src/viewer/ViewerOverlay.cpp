#include "viewer/ViewerOverlay.h"

#include <algorithm>
#include <string>
#include <vector>

#include "util/SettingsRegistry.h"

namespace hyperbrowse::viewer
{
    namespace
    {
        constexpr std::array<const wchar_t*, kOverlayPaneCount> kKeys{
            L"FileInformation", L"ImageStatus", L"FullMetadata", L"Histogram"};
        constexpr std::array<int, kOverlayPaneCount> kRetention{3, 2, 0, 1};
        std::wstring Key(std::size_t pane, const wchar_t* field)
        {
            return L"ViewerOverlay" + std::wstring(kKeys[pane]) + field;
        }
    }

    const wchar_t* OverlayPaneName(OverlayPane pane) noexcept
    {
        switch (pane)
        {
        case OverlayPane::FileInformation: return L"File information";
        case OverlayPane::ImageStatus: return L"Image status";
        case OverlayPane::FullMetadata: return L"Full metadata";
        case OverlayPane::Histogram: return L"Histogram";
        default: return L"Overlay";
        }
    }

    ViewerOverlaySettings NormalizeOverlaySettings(ViewerOverlaySettings settings)
    {
        const ViewerOverlaySettings defaults;
        if (static_cast<unsigned>(settings.textSize) > 2)
            settings.textSize = defaults.textSize;
        for (std::size_t i = 0; i < kOverlayPaneCount; ++i)
        {
            auto& pane = settings.panes[i];
            if (static_cast<unsigned>(pane.corner) > 3)
                pane.corner = defaults.panes[i].corner;
            if (static_cast<unsigned>(pane.importance) > 2)
                pane.importance = defaults.panes[i].importance;
            if (pane.order < 0 || pane.order >= static_cast<int>(kOverlayPaneCount))
                pane.order = 0;
        }
        for (int corner = 0; corner < 4; ++corner)
        {
            std::vector<std::size_t> stack;
            for (std::size_t i = 0; i < kOverlayPaneCount; ++i)
                if (static_cast<int>(settings.panes[i].corner) == corner) stack.push_back(i);
            std::stable_sort(stack.begin(), stack.end(), [&](auto a, auto b)
                { return settings.panes[a].order < settings.panes[b].order; });
            for (std::size_t i = 0; i < stack.size(); ++i)
                settings.panes[stack[i]].order = static_cast<int>(i);
        }
        return settings;
    }

    void MoveOverlayPane(ViewerOverlaySettings& settings, OverlayPane pane, bool towardCorner)
    {
        settings = NormalizeOverlaySettings(settings);
        const auto index = static_cast<std::size_t>(pane);
        if (index >= kOverlayPaneCount) return;
        auto& selected = settings.panes[index];
        const int next = selected.order + (towardCorner ? -1 : 1);
        for (std::size_t i = 0; i < kOverlayPaneCount; ++i)
        {
            auto& other = settings.panes[i];
            if (i != index && other.corner == selected.corner && other.order == next)
            {
                std::swap(other.order, selected.order);
                return;
            }
        }
    }

    OverlayLayout CalculateOverlayLayout(SIZE viewport, const ViewerOverlaySettings& input,
                                         bool fullScreen, const OverlaySizes& sizes, int margin, int gap)
    {
        const auto settings = NormalizeOverlaySettings(input);
        OverlayLayout result{};
        margin = (std::max)(0, margin);
        gap = (std::max)(0, gap);
        for (std::size_t i = 0; i < kOverlayPaneCount; ++i)
        {
            const bool enabled = fullScreen ? settings.panes[i].fullScreenEnabled : settings.panes[i].windowedEnabled;
            result[i].visibility = !enabled ? OverlayVisibility::Disabled
                : !settings.masterVisible ? OverlayVisibility::MasterHidden : OverlayVisibility::Shown;
        }
        for (;;)
        {
            bool fits = true;
            for (int corner = 0; corner < 4; ++corner)
            {
                LONG stackWidth{};
                for (std::size_t i = 0; i < kOverlayPaneCount; ++i)
                    if (result[i].visibility == OverlayVisibility::Shown
                        && static_cast<int>(settings.panes[i].corner) == corner)
                        stackWidth = (std::max)(stackWidth, sizes[i].cx);
                LONG edge = corner < 2 ? margin : viewport.cy - margin;
                for (int order = 0; order < static_cast<int>(kOverlayPaneCount); ++order)
                {
                    for (std::size_t i = 0; i < kOverlayPaneCount; ++i)
                    {
                        if (result[i].visibility != OverlayVisibility::Shown
                            || static_cast<int>(settings.panes[i].corner) != corner
                            || settings.panes[i].order != order) continue;
                        const LONG left = corner % 2 == 0 ? margin : viewport.cx - margin - stackWidth;
                        const LONG top = corner < 2 ? edge : edge - sizes[i].cy;
                        result[i].rect = {left, top, left + stackWidth, top + sizes[i].cy};
                        edge = corner < 2 ? top + sizes[i].cy + gap : top - gap;
                        const auto& r = result[i].rect;
                        if (sizes[i].cx <= 0 || sizes[i].cy <= 0 || r.left < margin || r.top < margin
                            || r.right > viewport.cx - margin || r.bottom > viewport.cy - margin) fits = false;
                    }
                }
            }
            for (std::size_t i = 0; i < kOverlayPaneCount; ++i)
                for (std::size_t j = i + 1; j < kOverlayPaneCount; ++j)
                {
                    if (result[i].visibility != OverlayVisibility::Shown || result[j].visibility != OverlayVisibility::Shown) continue;
                    const auto& a = result[i].rect;
                    const auto& b = result[j].rect;
                    if (a.left < b.right + gap && b.left < a.right + gap
                        && a.top < b.bottom + gap && b.top < a.bottom + gap) fits = false;
                }
            if (fits) return result;
            std::size_t victim = kOverlayPaneCount;
            for (std::size_t i = 0; i < kOverlayPaneCount; ++i)
            {
                if (result[i].visibility != OverlayVisibility::Shown) continue;
                if (victim == kOverlayPaneCount || settings.panes[i].importance < settings.panes[victim].importance
                    || (settings.panes[i].importance == settings.panes[victim].importance && kRetention[i] < kRetention[victim]))
                    victim = i;
            }
            if (victim == kOverlayPaneCount) return result;
            result[victim] = {{}, OverlayVisibility::HiddenToFit};
        }
    }

    ViewerOverlaySettings ViewerOverlayPersistence::Load(const ReadDword& read)
    {
        ViewerOverlaySettings settings;
        DWORD value{};
        if (read(L"ViewerInfoOverlaysVisible", &value)) settings.masterVisible = value != 0;
        if (read(L"ViewerInfoOverlayTextSize", &value)) settings.textSize = static_cast<InfoOverlayTextSize>(value);
        for (int mode = 0; mode < 2; ++mode)
        {
            const auto name = mode == 0 ? L"ViewerWindowedFullMetadataVisible" : L"ViewerFullScreenFullMetadataVisible";
            if (read(name, &value) || read(L"ViewerFullMetadataVisible", &value))
            {
                auto& metadata = settings.panes[static_cast<std::size_t>(OverlayPane::FullMetadata)];
                (mode == 0 ? metadata.windowedEnabled : metadata.fullScreenEnabled) = value != 0;
            }
        }
        for (std::size_t i = 0; i < kOverlayPaneCount; ++i)
        {
            auto& pane = settings.panes[i];
            if (read(Key(i, L"WindowedEnabled"), &value)) pane.windowedEnabled = value != 0;
            if (read(Key(i, L"FullScreenEnabled"), &value)) pane.fullScreenEnabled = value != 0;
            if (read(Key(i, L"Corner"), &value)) pane.corner = static_cast<OverlayCorner>(value);
            if (read(Key(i, L"Order"), &value)) pane.order = static_cast<int>(value);
            if (read(Key(i, L"Importance"), &value)) pane.importance = static_cast<OverlayImportance>(value);
        }
        return NormalizeOverlaySettings(settings);
    }

    void ViewerOverlayPersistence::Save(const ViewerOverlaySettings& input, const WriteDword& write)
    {
        const auto settings = NormalizeOverlaySettings(input);
        write(L"ViewerInfoOverlaysVisible", settings.masterVisible);
        write(L"ViewerInfoOverlayTextSize", static_cast<DWORD>(settings.textSize));
        const auto& metadata = settings.panes[static_cast<std::size_t>(OverlayPane::FullMetadata)];
        write(L"ViewerWindowedFullMetadataVisible", metadata.windowedEnabled);
        write(L"ViewerFullScreenFullMetadataVisible", metadata.fullScreenEnabled);
        for (std::size_t i = 0; i < kOverlayPaneCount; ++i)
        {
            const auto& pane = settings.panes[i];
            write(Key(i, L"WindowedEnabled"), pane.windowedEnabled);
            write(Key(i, L"FullScreenEnabled"), pane.fullScreenEnabled);
            write(Key(i, L"Corner"), static_cast<DWORD>(pane.corner));
            write(Key(i, L"Order"), static_cast<DWORD>(pane.order));
            write(Key(i, L"Importance"), static_cast<DWORD>(pane.importance));
        }
    }

    ViewerOverlaySettings ViewerOverlayPersistence::LoadRegistry()
    {
        HKEY key{};
        if (util::OpenSettingsRegistryKey(KEY_READ, &key) != ERROR_SUCCESS) return {};
        const auto settings = Load([key](std::wstring_view name, DWORD* value)
        {
            DWORD type{}, size = sizeof(*value);
            return RegQueryValueExW(key, std::wstring(name).c_str(), nullptr, &type,
                reinterpret_cast<BYTE*>(value), &size) == ERROR_SUCCESS && type == REG_DWORD && size == sizeof(*value);
        });
        RegCloseKey(key);
        return settings;
    }

    bool ViewerOverlayPersistence::SaveRegistry(const ViewerOverlaySettings& settings)
    {
        HKEY key{};
        if (util::CreateSettingsRegistryKey(KEY_WRITE, &key) != ERROR_SUCCESS) return false;
        bool success = true;
        Save(settings, [&](std::wstring_view name, DWORD value)
        {
            if (RegSetValueExW(key, std::wstring(name).c_str(), 0, REG_DWORD,
                reinterpret_cast<const BYTE*>(&value), sizeof(value)) != ERROR_SUCCESS) success = false;
        });
        RegCloseKey(key);
        return success;
    }
}
