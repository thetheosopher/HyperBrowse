#pragma once

#include <windows.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "services/SavedSearchStore.h"

namespace hyperbrowse::util { class BackgroundExecutor; }

namespace hyperbrowse::ui
{
    class SavedSearchController final
    {
    public:
        static constexpr UINT kChangedMessage = WM_APP + 83;

        explicit SavedSearchController(HWND owner, std::wstring storageDirectory = {});
        ~SavedSearchController();
        SavedSearchController(const SavedSearchController&) = delete;
        SavedSearchController& operator=(const SavedSearchController&) = delete;

        bool Load();
        bool Add(services::SavedSearch search);
        bool UpdateActive(std::wstring expression);
        bool RenameActive(std::wstring name);
        bool RemoveActive();
        bool ConsumeResult();
        std::optional<services::SavedSearch> Activate(std::size_t index);
        void FilterEdited();
        void Shutdown();

        bool Busy() const noexcept { return busy_; }
        bool Ready() const noexcept { return ready_; }
        const std::wstring& LastError() const noexcept { return lastError_; }
        const std::vector<services::SavedSearch>& Searches() const noexcept { return searches_; }
        const std::optional<services::SavedSearch>& Active() const noexcept { return active_; }

    private:
        enum class Operation { Load, Add, Update, Rename, Remove };
        struct Mailbox;
        bool Queue(Operation operation, services::SavedSearch search, std::wstring previousName = {});

        std::wstring storageDirectory_;
        std::shared_ptr<Mailbox> mailbox_;
        std::unique_ptr<util::BackgroundExecutor> executor_;
        std::vector<services::SavedSearch> searches_;
        std::optional<services::SavedSearch> active_;
        std::wstring lastError_;
        std::uint64_t filterGeneration_{};
        bool busy_{};
        bool ready_{};
    };
}
