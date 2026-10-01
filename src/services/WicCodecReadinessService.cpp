#include "services/WicCodecReadinessService.h"

#include <array>
#include <atomic>
#include <mutex>
#include <utility>

#include "decode/WicDecodeHelpers.h"
#include "util/BackgroundExecutor.h"

namespace
{
    using namespace hyperbrowse::services;
    namespace wic = hyperbrowse::decode::wic_support;
    using Microsoft::WRL::ComPtr;

    void SetDiscoveryFailure(WicCodecDiscoveryResult& discovery,
                             std::wstring_view operation,
                             HRESULT result)
    {
        if (discovery.state != WicCodecDiscoveryState::Ready)
        {
            discovery.state = WicCodecDiscoveryState::DiscoveryFailed;
            discovery.decoderName.clear();
            wic::SetError(&discovery.errorMessage, operation, result);
        }
    }

    WicCodecDiscoverySnapshot FailedDiscovery(const std::wstring& error)
    {
        return {{WicCodecDiscoveryState::DiscoveryFailed, {}, error},
                {WicCodecDiscoveryState::DiscoveryFailed, {}, error}};
    }

    WicCodecDiscoverySnapshot DiscoverInstalledWicCodecs(const std::atomic_bool& canceled)
    {
        std::wstring error;
        wic::ComInitializationScope com(COINIT_MULTITHREADED, &error,
                                        L"Failed to initialize COM for codec discovery.");
        ComPtr<IWICImagingFactory> factory;
        if (!com.Succeeded() || !wic::InitializeWicFactory(&factory, &error))
        {
            return FailedDiscovery(error);
        }

        WicCodecDiscoverySnapshot snapshot;
        const auto failUnresolved = [&](std::wstring_view operation, HRESULT result)
        {
            SetDiscoveryFailure(snapshot.heic, operation, result);
            SetDiscoveryFailure(snapshot.jpegXl, operation, result);
        };
        ComPtr<IEnumUnknown> enumerator;
        HRESULT result = factory->CreateComponentEnumerator(WICDecoder, WICComponentEnumerateRefresh, &enumerator);
        if (FAILED(result) || !enumerator)
        {
            failUnresolved(L"Failed to enumerate installed WIC decoders.", FAILED(result) ? result : E_UNEXPECTED);
            return snapshot;
        }

        constexpr std::size_t kMaxComponents = 256;
        for (std::size_t index = 0; index <= kMaxComponents && !canceled.load(); ++index)
        {
            ComPtr<IUnknown> component;
            ULONG fetched = 0;
            result = enumerator->Next(1, &component, &fetched);
            if (result == S_FALSE && fetched == 0)
            {
                return snapshot;
            }
            if (FAILED(result) || fetched != 1 || !component || index == kMaxComponents)
            {
                failUnresolved(L"WIC decoder enumeration failed or exceeded its component limit.",
                               FAILED(result) ? result : E_UNEXPECTED);
                return snapshot;
            }

            ComPtr<IWICBitmapDecoderInfo> information;
            result = component.As(&information);
            std::array<wchar_t, 4096> extensions{};
            UINT actual = 0;
            if (SUCCEEDED(result))
            {
                result = information->GetFileExtensions(static_cast<UINT>(extensions.size()), extensions.data(), &actual);
            }
            if (FAILED(result) || actual == 0 || actual > extensions.size() || extensions[actual - 1] != L'\0')
            {
                failUnresolved(L"Failed to read bounded WIC decoder extensions.", FAILED(result) ? result : E_UNEXPECTED);
                continue;
            }

            const std::wstring_view listedExtensions(extensions.data(), actual - 1);
            const bool heic = wic::DecoderListsExtension(listedExtensions, L"heic");
            const bool jpegXl = wic::DecoderListsExtension(listedExtensions, L"jxl");
            if (!heic && !jpegXl)
            {
                continue;
            }

            ComPtr<IWICBitmapDecoder> decoder;
            result = information->CreateInstance(&decoder);
            if (FAILED(result) || !decoder)
            {
                if (heic)
                {
                    SetDiscoveryFailure(snapshot.heic, L"The registered HEIC decoder could not be created.",
                                        FAILED(result) ? result : E_UNEXPECTED);
                }
                if (jpegXl)
                {
                    SetDiscoveryFailure(snapshot.jpegXl, L"The registered JPEG XL decoder could not be created.",
                                        FAILED(result) ? result : E_UNEXPECTED);
                }
                continue;
            }

            std::array<wchar_t, 129> name{};
            actual = 0;
            result = information->GetFriendlyName(static_cast<UINT>(name.size()), name.data(), &actual);
            const std::wstring decoderName = SUCCEEDED(result) && actual > 0 && actual <= name.size()
                && name[actual - 1] == L'\0' ? std::wstring(name.data(), actual - 1) : L"WIC decoder";
            if (heic)
            {
                snapshot.heic = {WicCodecDiscoveryState::Ready, decoderName, {}};
            }
            if (jpegXl)
            {
                snapshot.jpegXl = {WicCodecDiscoveryState::Ready, decoderName, {}};
            }
            if (snapshot.heic.state == WicCodecDiscoveryState::Ready
                && snapshot.jpegXl.state == WicCodecDiscoveryState::Ready)
            {
                return snapshot;
            }
        }
        return snapshot;
    }

    std::wstring BoundedText(std::wstring text, std::size_t limit)
    {
        text.resize((std::min)(text.size(), limit));
        for (wchar_t& character : text)
        {
            if (character < L' ')
            {
                character = L' ';
            }
        }
        return text;
    }

    WicCodecDiscoveryResult NormalizeDiscovery(WicCodecDiscoveryResult result)
    {
        if (result.state == WicCodecDiscoveryState::NotChecked)
        {
            result = {WicCodecDiscoveryState::DiscoveryFailed, {}, L"Decoder discovery returned no state."};
        }
        result.decoderName = BoundedText(std::move(result.decoderName), 128);
        result.errorMessage = BoundedText(std::move(result.errorMessage), 512);
        return result;
    }
}

namespace hyperbrowse::services
{
    struct WicCodecReadinessService::State
    {
        std::mutex mutex;
        std::atomic_bool canceled{};
        WicCodecReadinessSnapshot snapshot;
    };

    WicCodecReadinessService::WicCodecReadinessService(DiscoveryProvider provider)
        : state_(std::make_shared<State>())
        , provider_(std::move(provider))
    {
    }

    WicCodecReadinessService::~WicCodecReadinessService()
    {
        Shutdown();
    }

    bool WicCodecReadinessService::Refresh()
    {
        std::scoped_lock lock(state_->mutex);
        if (state_->canceled.load() || state_->snapshot.refreshing)
        {
            return false;
        }
        state_->snapshot.refreshing = true;
        const auto generation = ++state_->snapshot.generation;
        try
        {
            if (!executor_)
            {
                executor_ = std::make_unique<util::BackgroundExecutor>(1, 1);
            }
            if (executor_->Post([state = state_, provider = provider_, generation]()
            {
                WicCodecDiscoverySnapshot discovery;
                try
                {
                    discovery = provider ? provider() : DiscoverInstalledWicCodecs(state->canceled);
                    discovery.heic = NormalizeDiscovery(std::move(discovery.heic));
                    discovery.jpegXl = NormalizeDiscovery(std::move(discovery.jpegXl));
                }
                catch (...)
                {
                    discovery = FailedDiscovery(L"Decoder discovery provider failed.");
                }
                std::scoped_lock completionLock(state->mutex);
                if (state->canceled.load() || state->snapshot.generation != generation)
                {
                    return;
                }
                state->snapshot.heic.discovery = std::move(discovery.heic);
                state->snapshot.jpegXl.discovery = std::move(discovery.jpegXl);
                state->snapshot.refreshing = false;
            }))
            {
                return true;
            }
        }
        catch (...)
        {
        }
        const auto failure = FailedDiscovery(L"Codec discovery could not be queued.");
        state_->snapshot.heic.discovery = failure.heic;
        state_->snapshot.jpegXl.discovery = failure.jpegXl;
        state_->snapshot.refreshing = false;
        return false;
    }

    WicCodecReadinessSnapshot WicCodecReadinessService::Snapshot() const
    {
        std::scoped_lock lock(state_->mutex);
        return state_->snapshot;
    }

    void WicCodecReadinessService::RecordDecode(std::wstring_view fileType,
                                               WicCodecDecodeKind kind,
                                               bool succeeded)
    {
        if (!fileType.empty() && fileType.front() == L'.')
        {
            fileType.remove_prefix(1);
        }
        const bool heic = fileType.size() == 4
            && CompareStringOrdinal(fileType.data(), 4, L"heic", 4, TRUE) == CSTR_EQUAL;
        const bool jpegXl = fileType.size() == 3
            && CompareStringOrdinal(fileType.data(), 3, L"jxl", 3, TRUE) == CSTR_EQUAL;
        if (!heic && !jpegXl)
        {
            return;
        }
        std::scoped_lock lock(state_->mutex);
        if (state_->canceled.load())
        {
            return;
        }
        auto& codec = heic ? state_->snapshot.heic : state_->snapshot.jpegXl;
        auto& observation = kind == WicCodecDecodeKind::FullImage ? codec.fullImage : codec.thumbnail;
        observation.state = succeeded ? WicCodecDecodeState::Succeeded : WicCodecDecodeState::DecodeFailed;
        if (succeeded)
        {
            ++observation.successes;
        }
        else
        {
            ++observation.failures;
        }
    }

    void WicCodecReadinessService::ResetDecodeObservations()
    {
        std::scoped_lock lock(state_->mutex);
        if (!state_->canceled.load())
        {
            state_->snapshot.heic.thumbnail = {};
            state_->snapshot.heic.fullImage = {};
            state_->snapshot.jpegXl.thumbnail = {};
            state_->snapshot.jpegXl.fullImage = {};
        }
    }

    void WicCodecReadinessService::Shutdown()
    {
        std::unique_ptr<util::BackgroundExecutor> executor;
        {
            std::scoped_lock lock(state_->mutex);
            state_->canceled.store(true);
            state_->snapshot.refreshing = false;
            ++state_->snapshot.generation;
            executor = std::move(executor_);
        }
        executor.reset();
    }

    WicCodecReadinessService& GetWicCodecReadinessService()
    {
        static WicCodecReadinessService service;
        return service;
    }
}
