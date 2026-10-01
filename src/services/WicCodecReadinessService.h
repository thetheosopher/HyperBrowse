#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace hyperbrowse::util
{
    class BackgroundExecutor;
}

namespace hyperbrowse::services
{
    enum class WicCodecDiscoveryState
    {
        NotChecked,
        Ready,
        Missing,
        DiscoveryFailed,
    };

    enum class WicCodecDecodeKind
    {
        Thumbnail,
        FullImage,
    };

    enum class WicCodecDecodeState
    {
        NotAttempted,
        Succeeded,
        DecodeFailed,
    };

    struct WicCodecDiscoveryResult
    {
        WicCodecDiscoveryState state{WicCodecDiscoveryState::NotChecked};
        std::wstring decoderName;
        std::wstring errorMessage;
    };

    struct WicCodecDiscoverySnapshot
    {
        WicCodecDiscoveryResult heic{WicCodecDiscoveryState::Missing, {}, {}};
        WicCodecDiscoveryResult jpegXl{WicCodecDiscoveryState::Missing, {}, {}};
    };

    struct WicCodecDecodeObservation
    {
        WicCodecDecodeState state{WicCodecDecodeState::NotAttempted};
        std::uint64_t successes{};
        std::uint64_t failures{};
    };

    struct WicCodecReadiness
    {
        WicCodecDiscoveryResult discovery;
        WicCodecDecodeObservation thumbnail;
        WicCodecDecodeObservation fullImage;
    };

    struct WicCodecReadinessSnapshot
    {
        WicCodecReadiness heic;
        WicCodecReadiness jpegXl;
        bool refreshing{};
        std::uint64_t generation{};
    };

    class WicCodecReadinessService
    {
    public:
        using DiscoveryProvider = std::function<WicCodecDiscoverySnapshot()>;

        explicit WicCodecReadinessService(DiscoveryProvider provider = {});
        ~WicCodecReadinessService();

        WicCodecReadinessService(const WicCodecReadinessService&) = delete;
        WicCodecReadinessService& operator=(const WicCodecReadinessService&) = delete;

        bool Refresh();
        WicCodecReadinessSnapshot Snapshot() const;
        void RecordDecode(std::wstring_view fileType, WicCodecDecodeKind kind, bool succeeded);
        void ResetDecodeObservations();
        void Shutdown();

    private:
        struct State;
        std::shared_ptr<State> state_;
        DiscoveryProvider provider_;
        std::unique_ptr<util::BackgroundExecutor> executor_;
    };

    WicCodecReadinessService& GetWicCodecReadinessService();
}
