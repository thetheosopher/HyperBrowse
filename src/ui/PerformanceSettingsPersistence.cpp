#include "ui/PerformanceSettingsPersistence.h"

#include <algorithm>

namespace hyperbrowse::ui
{
    namespace
    {
        constexpr std::wstring_view kPersistentThumbnailCacheEnabledValue = L"PersistentThumbnailCacheEnabled";
        constexpr std::wstring_view kResourceProfileValue = L"ResourceProfile";
        constexpr std::wstring_view kPrefetchDepthOverrideValue = L"PrefetchDepthOverride";
        constexpr std::wstring_view kThumbnailCacheCapacityOverrideBytesValue = L"ThumbnailCacheCapacityOverrideBytes";
        constexpr std::wstring_view kMetadataCacheCapacityOverrideEntriesValue = L"MetadataCacheCapacityOverrideEntries";
        constexpr std::wstring_view kShowPressureStateInStatusBarValue = L"ShowPressureStateInStatusBar";
        constexpr std::wstring_view kCloseMainWindowOnEscapeValue = L"CloseMainWindowOnEscape";

        bool TryParseResourceProfile(DWORD value, util::ResourceProfile* resourceProfile)
        {
            if (!resourceProfile)
            {
                return false;
            }

            if (value > static_cast<DWORD>(util::ResourceProfile::Aggressive))
            {
                return false;
            }

            *resourceProfile = static_cast<util::ResourceProfile>(value);
            return true;
        }
    }

    PerformanceSettingsState PerformanceSettingsPersistence::Load(const ReadDword& readDword,
                                                                  const ReadQword& readQword,
                                                                  PerformanceSettingsState state)
    {
        DWORD value = 0;
        if (readDword(kPersistentThumbnailCacheEnabledValue, &value))
        {
            state.persistentThumbnailCacheEnabled = value != 0;
        }

        if (readDword(kResourceProfileValue, &value))
        {
            TryParseResourceProfile(value, &state.resourceProfile);
        }

        if (readDword(kPrefetchDepthOverrideValue, &value)
            && value <= static_cast<DWORD>(util::kMaximumPrefetchDepth))
        {
            state.prefetchDepthOverride = static_cast<int>(value);
        }

        std::uint64_t qwordValue = 0;
        if (readQword(kThumbnailCacheCapacityOverrideBytesValue, &qwordValue))
        {
            state.thumbnailCacheCapacityOverrideBytes = util::SaturatingCastToSizeT(qwordValue);
        }

        if (readQword(kMetadataCacheCapacityOverrideEntriesValue, &qwordValue))
        {
            state.metadataCacheCapacityOverrideEntries = util::SaturatingCastToSizeT(qwordValue);
        }

        if (readDword(kShowPressureStateInStatusBarValue, &value))
        {
            state.showPressureStateInStatusBar = value != 0;
        }

        if (readDword(kCloseMainWindowOnEscapeValue, &value))
        {
            state.closeMainWindowOnEscape = value != 0;
        }

        return state;
    }

    void PerformanceSettingsPersistence::Save(const PerformanceSettingsState& state,
                                              const WriteDword& writeDword,
                                              const WriteQword& writeQword)
    {
        writeDword(kPersistentThumbnailCacheEnabledValue, state.persistentThumbnailCacheEnabled ? 1UL : 0UL);
        writeDword(kResourceProfileValue, static_cast<DWORD>(state.resourceProfile));
        writeDword(kPrefetchDepthOverrideValue, static_cast<DWORD>((std::clamp)(
            state.prefetchDepthOverride,
            util::kAutomaticPrefetchDepth,
            util::kMaximumPrefetchDepth)));
        writeDword(kShowPressureStateInStatusBarValue, state.showPressureStateInStatusBar ? 1UL : 0UL);
        writeDword(kCloseMainWindowOnEscapeValue, state.closeMainWindowOnEscape ? 1UL : 0UL);
        writeQword(kThumbnailCacheCapacityOverrideBytesValue,
                   static_cast<std::uint64_t>(state.thumbnailCacheCapacityOverrideBytes));
        writeQword(kMetadataCacheCapacityOverrideEntriesValue,
                   static_cast<std::uint64_t>(state.metadataCacheCapacityOverrideEntries));
    }
}
