#pragma once

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "cache/ThumbnailCache.h"
#include "util/BackgroundExecutor.h"

namespace hyperbrowse::services
{
    enum class MonitorProfileStatus
    {
        Available,
        Unavailable,
        Invalid,
    };

    enum class MonitorProfileScope
    {
        System,
        CurrentUser,
    };

    struct MonitorColorProfile
    {
        MonitorProfileStatus status{MonitorProfileStatus::Unavailable};
        MonitorProfileScope scope{MonitorProfileScope::System};
        std::wstring identity;
        std::wstring source;
        std::vector<unsigned char> bytes;
        std::wstring diagnostic;
    };

    MonitorProfileScope MonitorProfileScopeFor(bool usePerUserProfiles) noexcept;
    std::wstring MonitorDeviceForWindow(HWND window);
    MonitorColorProfile ResolveMonitorColorProfile(const std::wstring& deviceName);

    class DisplayColorService
    {
    public:
        static constexpr UINT kReadyMessage = WM_APP + 82;
        static constexpr UINT_PTR kProfilePollTimer = 0x4842434D;
        static constexpr UINT kProfilePollIntervalMs = 2000;
        using ProfileProvider = std::function<MonitorColorProfile(const std::wstring& deviceName)>;
        using Converter = std::function<std::shared_ptr<const cache::CachedThumbnail>(
            const cache::ThumbnailCacheKey&, const cache::CachedThumbnail&, const MonitorColorProfile&, std::wstring*)>;

        struct Statistics
        {
            std::uint64_t revision{};
            std::uint64_t profileGeneration{};
            std::uint64_t settingGeneration{};
            std::uint64_t staleCompletions{};
            std::size_t convertedBytes{};
            std::size_t entryCount{};
            bool profileLookupPending{};
            std::wstring profileIdentity;
            std::uint64_t completedConversions{};
            std::uint64_t failedConversions{};
        };

        explicit DisplayColorService(std::size_t capacityBytes,
                                     ProfileProvider provider = {},
                                     Converter converter = {},
                                     util::BackgroundExecutor* executor = nullptr);
        ~DisplayColorService();
        DisplayColorService(const DisplayColorService&) = delete;
        DisplayColorService& operator=(const DisplayColorService&) = delete;

        void Shutdown();
        void BindTargetWindow(HWND window);
        void SetEnabled(bool enabled);
        bool IsEnabled() const;
        void SetProfileProvider(ProfileProvider provider);
        void RefreshMonitor(std::wstring deviceName, bool force = false);
        void InvalidateImages();
        void AcknowledgeNotification();
        std::shared_ptr<const cache::CachedThumbnail> ImageForDisplay(
            const cache::ThumbnailCacheKey& key, std::shared_ptr<const cache::CachedThumbnail> source);
        Statistics GetStatistics() const;

    private:
        struct State;
        std::shared_ptr<State> state_;
        ProfileProvider provider_;
        Converter converter_;
        std::unique_ptr<util::BackgroundExecutor> ownedExecutor_;
        util::BackgroundExecutor* executor_{};
    };
}
