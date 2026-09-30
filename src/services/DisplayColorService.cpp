#include "services/DisplayColorService.h"

#include <icm.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <unordered_map>

#include "decode/ImageDecoder.h"
#include "decode/WicColorTransform.h"
#include "util/Diagnostics.h"
#include "util/Log.h"

namespace hyperbrowse::services
{
    namespace
    {
        constexpr std::size_t kMaximumEntries = 128;

        std::wstring ColorProfileDirectory()
        {
            DWORD bytes = 0;
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4996)
#endif
            GetColorDirectoryW(nullptr, nullptr, &bytes);
            std::wstring directory(bytes / sizeof(wchar_t), L'\0');
            const bool available = bytes != 0 && GetColorDirectoryW(nullptr, directory.data(), &bytes);
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
            if (!available) return {};
            const auto terminator = directory.find(L'\0');
            if (terminator != std::wstring::npos) directory.resize(terminator);
            return directory;
        }

        std::wstring ProfileIdentity(const std::wstring& path, const std::vector<unsigned char>& bytes)
        {
            std::uint64_t hash = 14695981039346656037ULL;
            for (unsigned char value : bytes)
            {
                hash = (hash ^ value) * 1099511628211ULL;
            }
            return path + L":" + std::to_wstring(hash);
        }

        std::shared_ptr<const cache::CachedThumbnail> ConvertCanonicalImage(
            const cache::ThumbnailCacheKey& key, const cache::CachedThumbnail& source,
            const MonitorColorProfile& profile, std::wstring* error)
        {
            cache::SourceColorInfo info = source.SourceColor();
            if (info.kind == cache::SourceColorKind::Unspecified)
            {
                info = decode::IsRawFileType(std::filesystem::path(key.filePath).extension().wstring())
                    ? cache::SourceColorInfo{cache::SourceColorKind::Srgb, {}}
                    : decode::color::ReadSourceColorInfo(key.filePath);
                util::IncrementCounter(L"color.source_context.worker_resolved");
            }
            if (info.kind == cache::SourceColorKind::Srgb)
            {
                util::IncrementCounter(L"color.source_context.srgb");
            }
            return decode::color::TransformForDisplay(source, info, profile.bytes, error);
        }
    }

    MonitorProfileScope MonitorProfileScopeFor(bool usePerUserProfiles) noexcept
    {
        return usePerUserProfiles ? MonitorProfileScope::CurrentUser : MonitorProfileScope::System;
    }

    std::wstring MonitorDeviceForWindow(HWND window)
    {
        MONITORINFOEXW monitor{};
        monitor.cbSize = sizeof(monitor);
        const HMONITOR handle = window ? MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST) : nullptr;
        return handle && GetMonitorInfoW(handle, &monitor) ? monitor.szDevice : std::wstring{};
    }

    MonitorColorProfile ResolveMonitorColorProfile(const std::wstring& deviceName)
    {
        MonitorColorProfile profile;
        if (deviceName.empty())
        {
            profile.diagnostic = L"No rendering-window monitor device is available.";
            return profile;
        }
        BOOL perUser = FALSE;
        const BOOL scopeResolved = WcsGetUsePerUserProfiles(deviceName.c_str(), CLASS_MONITOR, &perUser);
        profile.scope = MonitorProfileScopeFor(scopeResolved && perUser);
        const auto scope = profile.scope == MonitorProfileScope::CurrentUser
            ? WCS_PROFILE_MANAGEMENT_SCOPE_CURRENT_USER : WCS_PROFILE_MANAGEMENT_SCOPE_SYSTEM_WIDE;
        DWORD nameBytes = 0;
        std::wstring path;
        if (scopeResolved && WcsGetDefaultColorProfileSize(scope, deviceName.c_str(), CPT_ICC, CPST_NONE, 0, &nameBytes)
            && nameBytes >= sizeof(wchar_t) && nameBytes <= 65536 && nameBytes % sizeof(wchar_t) == 0)
        {
            path.resize(nameBytes / sizeof(wchar_t));
            if (!WcsGetDefaultColorProfile(scope, deviceName.c_str(), CPT_ICC, CPST_NONE, 0, nameBytes, path.data()))
            {
                path.clear();
            }
        }
        if (path.empty())
        {
            HDC display = CreateDCW(L"DISPLAY", deviceName.c_str(), nullptr, nullptr);
            if (display)
            {
                DWORD characters = 0;
                GetICMProfileW(display, &characters, nullptr);
                if (characters > 0 && characters <= 32768)
                {
                    path.resize(characters);
                    if (!GetICMProfileW(display, &characters, path.data())) path.clear();
                }
                DeleteDC(display);
            }
        }
        if (path.empty())
        {
            profile.diagnostic = L"No usable Windows ICC association for monitor " + deviceName + L" (error " + std::to_wstring(GetLastError()) + L").";
            return profile;
        }
        const auto terminator = path.find(L'\0');
        if (terminator != std::wstring::npos) path.resize(terminator);
        if (std::filesystem::path(path).is_relative())
        {
            const std::wstring directory = ColorProfileDirectory();
            if (directory.empty())
            {
                profile.diagnostic = L"Windows color-profile directory is unavailable.";
                return profile;
            }
            path = (std::filesystem::path(directory) / path).wstring();
        }
        profile.source = path;
        std::ifstream stream(std::filesystem::path(path), std::ios::binary | std::ios::ate);
        const auto size = stream ? stream.tellg() : std::streampos{-1};
        if (size < 128 || size > static_cast<std::streamoff>(decode::color::kMaximumIccProfileBytes))
        {
            profile.diagnostic = L"Monitor ICC file is inaccessible or has an unsupported size: " + path;
            return profile;
        }
        profile.bytes.resize(static_cast<std::size_t>(size));
        stream.seekg(0);
        stream.read(reinterpret_cast<char*>(profile.bytes.data()), static_cast<std::streamsize>(profile.bytes.size()));
        if (!stream || !decode::color::IsUsableRgbProfile(profile.bytes, &profile.diagnostic))
        {
            profile.status = MonitorProfileStatus::Invalid;
            profile.bytes.clear();
            return profile;
        }
        profile.status = MonitorProfileStatus::Available;
        profile.identity = ProfileIdentity(path, profile.bytes);
        return profile;
    }

    struct DisplayColorService::State
    {
        struct Key
        {
            cache::ThumbnailCacheKey sourceKey;
            const cache::CachedThumbnail* pixels{};
            bool operator==(const Key& other) const noexcept
            {
                return pixels == other.pixels && sourceKey == other.sourceKey;
            }
        };
        struct Hasher
        {
            std::size_t operator()(const Key& key) const noexcept
            {
                return cache::ThumbnailCacheKeyHasher{}(key.sourceKey) ^ std::hash<const void*>{}(key.pixels);
            }
        };
        struct Entry
        {
            std::weak_ptr<const cache::CachedThumbnail> source;
            std::shared_ptr<const cache::CachedThumbnail> display;
            std::uint64_t requestedProfile{};
            std::uint64_t requestedSetting{};
            std::uint64_t lastUse{};
            bool pending{};
            bool settled{};
        };

        void Notify()
        {
            ++revision;
            if (target && !notificationPending)
            {
                notificationPending = PostMessageW(target, kReadyMessage, 0, 0) != FALSE;
            }
        }

        void Trim()
        {
            while (!entries.empty() && (convertedBytes > capacityBytes || entries.size() > kMaximumEntries))
            {
                auto oldest = std::min_element(entries.begin(), entries.end(), [](const auto& first, const auto& second)
                {
                    return first.second.lastUse < second.second.lastUse;
                });
                if (oldest->second.display) convertedBytes -= oldest->second.display->ByteCount();
                entries.erase(oldest);
                util::IncrementCounter(L"color.display_cache.evicted");
            }
        }

        mutable std::mutex mutex;
        HWND target{};
        bool enabled{true};
        bool shutdown{};
        bool notificationPending{};
        bool profileLookupPending{};
        std::wstring monitorDevice;
        std::shared_ptr<const MonitorColorProfile> profile;
        std::uint64_t revision{};
        std::uint64_t profileGeneration{};
        std::uint64_t profileRequest{};
        std::uint64_t settingGeneration{};
        std::uint64_t sessionGeneration{};
        std::uint64_t imageGeneration{};
        std::uint64_t staleCompletions{};
        std::uint64_t completedConversions{};
        std::uint64_t failedConversions{};
        std::uint64_t ordinal{};
        std::size_t capacityBytes{};
        std::size_t convertedBytes{};
        std::unordered_map<Key, Entry, Hasher> entries;
    };

    DisplayColorService::DisplayColorService(std::size_t capacityBytes, ProfileProvider provider,
                                           Converter converter, util::BackgroundExecutor* executor)
        : state_(std::make_shared<State>())
        , provider_(provider ? std::move(provider) : ResolveMonitorColorProfile)
        , converter_(converter ? std::move(converter) : ConvertCanonicalImage)
        , ownedExecutor_(executor ? nullptr : std::make_unique<util::BackgroundExecutor>(2, 64))
        , executor_(executor ? executor : ownedExecutor_.get())
    {
        state_->capacityBytes = capacityBytes;
    }

    DisplayColorService::~DisplayColorService()
    {
        {
            std::scoped_lock lock(state_->mutex);
            state_->shutdown = true;
            state_->target = nullptr;
            ++state_->sessionGeneration;
        }
        ownedExecutor_.reset();
    }

    void DisplayColorService::BindTargetWindow(HWND window)
    {
        std::scoped_lock lock(state_->mutex);
        state_->target = window;
        ++state_->sessionGeneration;
        ++state_->profileRequest;
        ++state_->profileGeneration;
        state_->notificationPending = false;
        state_->profileLookupPending = false;
        state_->profile.reset();
        state_->monitorDevice.clear();
        state_->entries.clear();
        state_->convertedBytes = 0;
    }

    void DisplayColorService::SetEnabled(bool enabled)
    {
        std::wstring monitor;
        {
            std::scoped_lock lock(state_->mutex);
            if (state_->enabled == enabled) return;
            state_->enabled = enabled;
            ++state_->settingGeneration;
            ++state_->profileGeneration;
            ++state_->profileRequest;
            state_->profileLookupPending = false;
            state_->profile.reset();
            state_->entries.clear();
            state_->convertedBytes = 0;
            monitor = state_->monitorDevice;
            state_->Notify();
        }
        if (enabled) RefreshMonitor(std::move(monitor), true);
    }

    bool DisplayColorService::IsEnabled() const
    {
        std::scoped_lock lock(state_->mutex);
        return state_->enabled;
    }

    void DisplayColorService::SetProfileProvider(ProfileProvider provider)
    {
        provider_ = provider ? std::move(provider) : ResolveMonitorColorProfile;
        std::wstring monitor;
        {
            std::scoped_lock lock(state_->mutex);
            monitor = state_->monitorDevice;
            ++state_->profileRequest;
            state_->profileLookupPending = false;
        }
        RefreshMonitor(std::move(monitor), true);
    }

    void DisplayColorService::RefreshMonitor(std::wstring deviceName, bool force)
    {
        const auto state = state_;
        std::scoped_lock lock(state->mutex);
        const bool moved = state->monitorDevice != deviceName;
        if (state->shutdown || !state->enabled || deviceName.empty()
            || (!moved && (!force || state->profileLookupPending)))
        {
            return;
        }
        if (moved)
        {
            state->monitorDevice = deviceName;
            ++state->profileGeneration;
            state->profile.reset();
        }
        const auto request = ++state->profileRequest;
        const auto session = state->sessionGeneration;
        const auto setting = state->settingGeneration;
        state->profileLookupPending = true;
        if (!executor_->Post([state, provider = provider_, deviceName = std::move(deviceName), request, session, setting]()
        {
            MonitorColorProfile candidate;
            try
            {
                candidate = provider(deviceName);
            }
            catch (...)
            {
                candidate.diagnostic = L"Monitor profile provider failed.";
            }
            auto resolved = std::make_shared<const MonitorColorProfile>(std::move(candidate));
            {
            std::scoped_lock completedLock(state->mutex);
            if (state->shutdown || state->sessionGeneration != session || state->profileRequest != request
                || state->settingGeneration != setting || !state->enabled)
            {
                ++state->staleCompletions;
                util::IncrementCounter(L"color.stale_completion");
                return;
            }
            state->profileLookupPending = false;
            const bool changed = !state->profile || state->profile->identity != resolved->identity
                || state->profile->bytes != resolved->bytes || state->profile->status != resolved->status;
            if (!changed) return;
            state->profile = resolved;
            ++state->profileGeneration;
            for (auto& [key, entry] : state->entries)
            {
                entry.pending = false;
                entry.settled = false;
                if (state->profile->status != MonitorProfileStatus::Available)
                {
                    if (entry.display) state->convertedBytes -= entry.display->ByteCount();
                    entry.display.reset();
                }
            }
            state->Notify();
            }
            if (resolved->status == MonitorProfileStatus::Available)
            {
                util::IncrementCounter(L"color.profile.selected");
                util::LogInfo(L"Color display profile: monitor=" + deviceName + L", profile=" + resolved->identity);
            }
            else
            {
                util::IncrementCounter(resolved->status == MonitorProfileStatus::Invalid ? L"color.profile.invalid" : L"color.profile.unavailable");
                util::LogInfo(L"Color display fallback: monitor=" + deviceName + L", " + resolved->diagnostic);
            }
        }))
        {
            state->profileLookupPending = false;
            util::IncrementCounter(L"color.queue_rejected");
        }
    }

    void DisplayColorService::InvalidateImages()
    {
        std::scoped_lock lock(state_->mutex);
        ++state_->imageGeneration;
        state_->entries.clear();
        state_->convertedBytes = 0;
    }

    void DisplayColorService::AcknowledgeNotification()
    {
        std::scoped_lock lock(state_->mutex);
        state_->notificationPending = false;
    }

    std::shared_ptr<const cache::CachedThumbnail> DisplayColorService::ImageForDisplay(
        const cache::ThumbnailCacheKey& key, std::shared_ptr<const cache::CachedThumbnail> source)
    {
        if (!source) return {};
        const auto state = state_;
        std::scoped_lock lock(state->mutex);
        if (!state->enabled || state->shutdown || source->SourceColor().kind == cache::SourceColorKind::DisplayConverted)
        {
            return source;
        }
        const State::Key displayKey{key, source.get()};
        auto existing = state->entries.find(displayKey);
        if (existing != state->entries.end() && existing->second.source.lock() != source)
        {
            if (existing->second.display) state->convertedBytes -= existing->second.display->ByteCount();
            state->entries.erase(existing);
            existing = state->entries.end();
        }
        if (!state->profile || state->profile->status != MonitorProfileStatus::Available)
        {
            return existing != state->entries.end() && existing->second.display ? existing->second.display : source;
        }
        const auto pixelBytes = static_cast<std::uint64_t>(source->Width()) * static_cast<std::uint64_t>(source->Height()) * 4;
        if (pixelBytes > state->capacityBytes)
        {
            util::IncrementCounter(L"color.display_cache.too_large");
            return source;
        }
        auto& entry = state->entries[displayKey];
        entry.source = source;
        entry.lastUse = ++state->ordinal;
        const auto profileGeneration = state->profileGeneration;
        const auto settingGeneration = state->settingGeneration;
        const auto session = state->sessionGeneration;
        const auto imageGeneration = state->imageGeneration;
        if (entry.requestedProfile != profileGeneration || entry.requestedSetting != settingGeneration)
        {
            entry.pending = false;
            entry.settled = false;
        }
        if (!entry.pending && !entry.settled)
        {
            entry.requestedProfile = profileGeneration;
            entry.requestedSetting = settingGeneration;
            entry.pending = true;
            if (!executor_->Post([state, converter = converter_, source, displayKey, profile = state->profile,
                                 profileGeneration, settingGeneration, session, imageGeneration]()
            {
                {
                    std::scoped_lock startLock(state->mutex);
                    if (state->shutdown || !state->enabled || state->sessionGeneration != session
                        || state->imageGeneration != imageGeneration
                        || state->profileGeneration != profileGeneration || state->settingGeneration != settingGeneration)
                    {
                        ++state->staleCompletions;
                        util::IncrementCounter(L"color.stale_completion");
                        return;
                    }
                }
                std::wstring error;
                std::shared_ptr<const cache::CachedThumbnail> converted;
                try
                {
                    converted = converter(displayKey.sourceKey, *source, *profile, &error);
                }
                catch (...)
                {
                    error = L"Display pixel converter failed.";
                }
                {
                std::scoped_lock completedLock(state->mutex);
                const auto found = state->entries.find(displayKey);
                if (state->shutdown || !state->enabled || state->sessionGeneration != session
                    || state->imageGeneration != imageGeneration
                    || state->profileGeneration != profileGeneration || state->settingGeneration != settingGeneration
                    || found == state->entries.end() || found->second.requestedProfile != profileGeneration
                    || found->second.requestedSetting != settingGeneration)
                {
                    ++state->staleCompletions;
                    util::IncrementCounter(L"color.stale_completion");
                    return;
                }
                auto& completed = found->second;
                if (completed.display) state->convertedBytes -= completed.display->ByteCount();
                completed.display = converted;
                completed.pending = false;
                completed.settled = true;
                if (completed.display)
                {
                    ++state->completedConversions;
                    state->convertedBytes += completed.display->ByteCount();
                    util::IncrementCounter(L"color.transformed");
                }
                else
                {
                    ++state->failedConversions;
                    util::IncrementCounter(L"color.transform_fallback");
                }
                state->Trim();
                state->Notify();
                }
                if (!converted)
                {
                    util::LogInfo(L"Color transform fallback: source=" + displayKey.sourceKey.filePath
                                  + L", profile=" + profile->identity + L", " + error);
                }
            }))
            {
                entry.pending = false;
                util::IncrementCounter(L"color.queue_rejected");
            }
        }
        auto result = entry.display ? entry.display : source;
        state->Trim();
        return result;
    }

    DisplayColorService::Statistics DisplayColorService::GetStatistics() const
    {
        std::scoped_lock lock(state_->mutex);
        return {state_->revision, state_->profileGeneration, state_->settingGeneration,
                state_->staleCompletions, state_->convertedBytes, state_->entries.size(),
                state_->profileLookupPending, state_->profile ? state_->profile->identity : std::wstring{},
                state_->completedConversions, state_->failedConversions};
    }
}
