#pragma once

#include <windows.h>

#include <cstdint>
#include <functional>
#include <string_view>

#include "util/ResourceSizing.h"

namespace hyperbrowse::ui
{
    struct PerformanceSettingsState
    {
        bool persistentThumbnailCacheEnabled{true};
        util::ResourceProfile resourceProfile{util::ResourceProfile::Balanced};
        int prefetchDepthOverride{util::kAutomaticPrefetchDepth};
        std::size_t thumbnailCacheCapacityOverrideBytes{};
        std::size_t metadataCacheCapacityOverrideEntries{};
        bool showPressureStateInStatusBar{};
        bool closeMainWindowOnEscape{};
    };

    class PerformanceSettingsPersistence
    {
    public:
        using ReadDword = std::function<bool(std::wstring_view valueName, DWORD* value)>;
        using WriteDword = std::function<void(std::wstring_view valueName, DWORD value)>;
        using ReadQword = std::function<bool(std::wstring_view valueName, std::uint64_t* value)>;
        using WriteQword = std::function<void(std::wstring_view valueName, std::uint64_t value)>;

        static PerformanceSettingsState Load(const ReadDword& readDword,
                                             const ReadQword& readQword,
                                             PerformanceSettingsState state = {});
        static void Save(const PerformanceSettingsState& state,
                         const WriteDword& writeDword,
                         const WriteQword& writeQword);
    };
}
