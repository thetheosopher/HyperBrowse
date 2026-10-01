#include "ui/SavedSearchController.h"

#include <algorithm>
#include <cwctype>
#include <exception>
#include <mutex>
#include <utility>

#include "util/BackgroundExecutor.h"
#include "util/StringConvert.h"

namespace hyperbrowse::ui
{
    struct SavedSearchController::Mailbox
    {
        struct Result
        {
            Operation operation{Operation::Load};
            services::SavedSearch search;
            std::vector<services::SavedSearch> searches;
            std::wstring error;
            std::uint64_t filterGeneration{};
            bool succeeded{};
        };
        std::mutex mutex;
        HWND owner{};
        std::optional<Result> result;
    };

    SavedSearchController::SavedSearchController(HWND owner, std::wstring storageDirectory)
        : storageDirectory_(std::move(storageDirectory))
        , mailbox_(std::make_shared<Mailbox>())
        , executor_(std::make_unique<util::BackgroundExecutor>(1, 1))
    {
        mailbox_->owner = owner;
    }

    SavedSearchController::~SavedSearchController()
    {
        Shutdown();
    }

    void SavedSearchController::Shutdown()
    {
        {
            std::scoped_lock lock(mailbox_->mutex);
            mailbox_->owner = nullptr;
        }
        executor_.reset();
        {
            std::scoped_lock lock(mailbox_->mutex);
            mailbox_->result.reset();
        }
        busy_ = false;
    }

    bool SavedSearchController::Queue(Operation operation, services::SavedSearch search, std::wstring previousName)
    {
        if (!executor_ || busy_ || (operation != Operation::Load && !ready_)) return false;
        const auto trim = [](std::wstring value)
        {
            const auto isSpace = [](wchar_t character) { return iswspace(character) != 0; };
            const auto first = std::find_if_not(value.begin(), value.end(), isSpace);
            const auto last = std::find_if_not(value.rbegin(), value.rend(), isSpace).base();
            return first < last ? std::wstring(first, last) : std::wstring{};
        };
        search.name = trim(std::move(search.name));
        search.expression = trim(std::move(search.expression));
        busy_ = true;
        const bool accepted = executor_->Post([state = mailbox_, directory = storageDirectory_,
            operation, search = std::move(search), previousName = std::move(previousName),
            generation = filterGeneration_]() mutable
        {
            Mailbox::Result result;
            result.operation = operation;
            result.search = std::move(search);
            result.filterGeneration = generation;
            try
            {
                services::SavedSearchStore store(directory);
                switch (operation)
                {
                case Operation::Load:
                    result.succeeded = store.LoadOnWorker(&result.searches, &result.error);
                    break;
                case Operation::Add:
                    result.succeeded = store.AddOnWorker(result.search, &result.error, &result.searches);
                    break;
                case Operation::Update:
                    result.succeeded = store.UpdateOnWorker(result.search.name, result.search.expression,
                                                            &result.error, &result.searches);
                    break;
                case Operation::Rename:
                    result.succeeded = store.RenameOnWorker(previousName, result.search.name,
                                                            &result.error, &result.searches);
                    break;
                case Operation::Remove:
                    result.succeeded = store.RemoveOnWorker(result.search.name, &result.error, &result.searches);
                    break;
                }
            }
            catch (const std::exception&)
            {
                result.succeeded = false;
                result.error = L"The saved-search operation could not be completed.";
            }
            catch (...)
            {
                result.succeeded = false;
                result.error = L"The saved-search operation failed unexpectedly.";
            }
            std::scoped_lock lock(state->mutex);
            if (state->owner)
            {
                state->result = std::move(result);
                PostMessageW(state->owner, kChangedMessage, 0, 0);
            }
        });
        if (!accepted)
        {
            busy_ = false;
            lastError_ = L"The saved-search worker is unavailable.";
        }
        return accepted;
    }

    bool SavedSearchController::Load() { return Queue(Operation::Load, {}); }
    bool SavedSearchController::Add(services::SavedSearch search) { return Queue(Operation::Add, std::move(search)); }
    bool SavedSearchController::UpdateActive(std::wstring expression)
    {
        return active_ && Queue(Operation::Update, {active_->name, std::move(expression)});
    }
    bool SavedSearchController::RenameActive(std::wstring name)
    {
        return active_ && Queue(Operation::Rename, {std::move(name), active_->expression}, active_->name);
    }
    bool SavedSearchController::RemoveActive()
    {
        return active_ && Queue(Operation::Remove, *active_);
    }

    std::optional<services::SavedSearch> SavedSearchController::Activate(std::size_t index)
    {
        if (!ready_ || busy_ || index >= searches_.size()) return std::nullopt;
        ++filterGeneration_;
        active_ = searches_[index];
        return active_;
    }

    void SavedSearchController::FilterEdited()
    {
        ++filterGeneration_;
        active_.reset();
    }

    bool SavedSearchController::ConsumeResult()
    {
        std::optional<Mailbox::Result> result;
        {
            std::scoped_lock lock(mailbox_->mutex);
            result = std::move(mailbox_->result);
            mailbox_->result.reset();
        }
        if (!result) return false;
        busy_ = false;
        lastError_ = std::move(result->error);
        if (!result->succeeded) return true;
        ready_ = true;
        searches_ = std::move(result->searches);
        if (result->filterGeneration == filterGeneration_
            && (result->operation == Operation::Add || result->operation == Operation::Update
                || result->operation == Operation::Rename))
        {
            const auto selected = std::find_if(searches_.begin(), searches_.end(), [&](const services::SavedSearch& search)
            {
                return util::EqualsIgnoreCaseOrdinal(search.name, result->search.name);
            });
            if (selected != searches_.end()) active_ = *selected;
        }
        if (active_ && std::none_of(searches_.begin(), searches_.end(), [&](const services::SavedSearch& search)
            { return search == *active_; })) active_.reset();
        return true;
    }
}
