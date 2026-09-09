#include "ui/MainWindowAccessibility.h"

#include <algorithm>
#include <atomic>
#include <new>
#include <utility>

namespace hyperbrowse::ui
{
    struct MainWindowAccessibility::Data
    {
        HWND window{};
        SnapshotProvider snapshotProvider;
        FocusedChildProvider focusedChildProvider;
        FocusProvider focusProvider;
        DefaultActionProvider defaultActionProvider;
        std::wstring rootName;
        std::wstring rootDescription;
        long rootRole{ROLE_SYSTEM_WINDOW};
    };

    namespace
    {
        HRESULT SetString(BSTR* destination, const std::wstring& value)
        {
            if (!destination)
            {
                return E_POINTER;
            }

            *destination = SysAllocStringLen(value.data(), static_cast<UINT>(value.size()));
            return *destination || value.empty() ? S_OK : E_OUTOFMEMORY;
        }

        void SetEmptyVariant(VARIANT* value)
        {
            if (value)
            {
                VariantInit(value);
            }
        }

        void SetChildVariant(VARIANT* value, long childId)
        {
            VariantInit(value);
            value->vt = VT_I4;
            value->lVal = childId;
        }
    }

    class MainWindowAccessibility::AccessibleObject final : public IAccessible
    {
    public:
        explicit AccessibleObject(std::shared_ptr<Data> data)
            : data_(std::move(data))
        {
        }

        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID interfaceId, void** object) override
        {
            if (!object)
            {
                return E_POINTER;
            }

            *object = nullptr;
            if (interfaceId == IID_IUnknown
                || interfaceId == IID_IDispatch
                || interfaceId == IID_IAccessible)
            {
                *object = static_cast<IAccessible*>(this);
                AddRef();
                return S_OK;
            }
            return E_NOINTERFACE;
        }

        ULONG STDMETHODCALLTYPE AddRef() override
        {
            return ++referenceCount_;
        }

        ULONG STDMETHODCALLTYPE Release() override
        {
            const ULONG remaining = --referenceCount_;
            if (remaining == 0)
            {
                delete this;
            }
            return remaining;
        }

        HRESULT STDMETHODCALLTYPE GetTypeInfoCount(UINT* typeInfoCount) override
        {
            if (!typeInfoCount)
            {
                return E_POINTER;
            }
            *typeInfoCount = 0;
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE GetTypeInfo(UINT, LCID, ITypeInfo**) override
        {
            return E_NOTIMPL;
        }

        HRESULT STDMETHODCALLTYPE GetIDsOfNames(REFIID,
                                                LPOLESTR*,
                                                UINT,
                                                LCID,
                                                DISPID*) override
        {
            return E_NOTIMPL;
        }

        HRESULT STDMETHODCALLTYPE Invoke(DISPID,
                                         REFIID,
                                         LCID,
                                         WORD,
                                         DISPPARAMS*,
                                         VARIANT*,
                                         EXCEPINFO*,
                                         UINT*) override
        {
            return E_NOTIMPL;
        }

        HRESULT STDMETHODCALLTYPE get_accParent(IDispatch** parent) override
        {
            if (!parent)
            {
                return E_POINTER;
            }
            *parent = nullptr;
            return S_FALSE;
        }

        HRESULT STDMETHODCALLTYPE get_accChildCount(long* childCount) override
        {
            if (!childCount)
            {
                return E_POINTER;
            }
            *childCount = static_cast<long>(Snapshot().size());
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE get_accChild(VARIANT childId, IDispatch** child) override
        {
            if (!child)
            {
                return E_POINTER;
            }
            *child = nullptr;
            if (IsSelf(childId))
            {
                *child = static_cast<IDispatch*>(this);
                AddRef();
                return S_OK;
            }
            return SUCCEEDED(TryGetItem(childId, nullptr)) ? S_FALSE : E_INVALIDARG;
        }

        HRESULT STDMETHODCALLTYPE get_accName(VARIANT childId, BSTR* name) override
        {
            if (IsSelf(childId))
            {
                return SetString(name, data_ ? data_->rootName : std::wstring{});
            }

            Item item;
            const HRESULT result = TryGetItem(childId, &item);
            return SUCCEEDED(result) ? SetString(name, item.name) : result;
        }

        HRESULT STDMETHODCALLTYPE get_accValue(VARIANT childId, BSTR* value) override
        {
            if (IsSelf(childId))
            {
                return SetString(value, data_ ? data_->rootName : std::wstring{});
            }

            Item item;
            const HRESULT result = TryGetItem(childId, &item);
            return SUCCEEDED(result) ? SetString(value, item.value) : result;
        }

        HRESULT STDMETHODCALLTYPE get_accDescription(VARIANT childId, BSTR* description) override
        {
            if (IsSelf(childId))
            {
                return SetString(description, data_ ? data_->rootDescription : std::wstring{});
            }

            Item item;
            const HRESULT result = TryGetItem(childId, &item);
            return SUCCEEDED(result) ? SetString(description, item.description) : result;
        }

        HRESULT STDMETHODCALLTYPE get_accRole(VARIANT childId, VARIANT* role) override
        {
            if (!role)
            {
                return E_POINTER;
            }
            VariantInit(role);
            role->vt = VT_I4;
            if (IsSelf(childId))
            {
                role->lVal = data_ ? data_->rootRole : ROLE_SYSTEM_WINDOW;
                return S_OK;
            }

            Item item;
            const HRESULT result = TryGetItem(childId, &item);
            if (FAILED(result))
            {
                return result;
            }
            role->lVal = item.role;
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE get_accState(VARIANT childId, VARIANT* state) override
        {
            if (!state)
            {
                return E_POINTER;
            }
            VariantInit(state);
            state->vt = VT_I4;
            if (IsSelf(childId))
            {
                state->lVal = 0;
                return S_OK;
            }

            const auto items = Snapshot();
            Item item;
            const HRESULT result = TryGetItem(childId, &item, &items);
            if (FAILED(result))
            {
                return result;
            }
            state->lVal = item.state;
            if (FocusedChildId() == ChildId(childId))
            {
                state->lVal |= STATE_SYSTEM_FOCUSED;
            }
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE get_accHelp(VARIANT childId, BSTR* help) override
        {
            if (IsSelf(childId))
            {
                return SetString(help, L"");
            }

            Item item;
            const HRESULT result = TryGetItem(childId, &item);
            if (FAILED(result))
            {
                return result;
            }
            return SetString(help, item.description);
        }

        HRESULT STDMETHODCALLTYPE get_accHelpTopic(BSTR* helpFile, VARIANT childId, long* topic) override
        {
            if (!helpFile || !topic)
            {
                return E_POINTER;
            }
            *helpFile = nullptr;
            *topic = 0;
            return IsSelf(childId) || TryGetItem(childId, nullptr) == S_OK ? S_FALSE : E_INVALIDARG;
        }

        HRESULT STDMETHODCALLTYPE get_accKeyboardShortcut(VARIANT childId, BSTR* shortcut) override
        {
            if (IsSelf(childId))
            {
                return SetString(shortcut, L"");
            }

            Item item;
            const HRESULT result = TryGetItem(childId, &item);
            if (FAILED(result))
            {
                return result;
            }
            return SetString(shortcut, item.keyboardShortcut);
        }

        HRESULT STDMETHODCALLTYPE get_accFocus(VARIANT* focus) override
        {
            if (!focus)
            {
                return E_POINTER;
            }
            SetEmptyVariant(focus);
            const long childId = FocusedChildId();
            const auto items = Snapshot();
            if (childId > 0 && childId <= static_cast<long>(items.size()))
            {
                SetChildVariant(focus, childId);
            }
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE get_accSelection(VARIANT* selection) override
        {
            if (!selection)
            {
                return E_POINTER;
            }
            SetEmptyVariant(selection);
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE get_accDefaultAction(VARIANT childId, BSTR* action) override
        {
            if (IsSelf(childId))
            {
                return SetString(action, L"");
            }

            Item item;
            const HRESULT result = TryGetItem(childId, &item);
            if (FAILED(result))
            {
                return result;
            }
            return SetString(action, item.defaultAction);
        }

        HRESULT STDMETHODCALLTYPE accSelect(long selectFlags, VARIANT childId) override
        {
            if (!IsSelf(childId))
            {
                const HRESULT itemResult = TryGetItem(childId, nullptr);
                if (FAILED(itemResult))
                {
                    return itemResult;
                }
            }
            if ((selectFlags & SELFLAG_TAKEFOCUS) == 0)
            {
                return E_NOTIMPL;
            }
            if (!data_ || !data_->focusProvider)
            {
                return E_NOTIMPL;
            }
            const long id = IsSelf(childId) ? 0 : ChildId(childId);
            return data_->focusProvider(id) ? S_OK : S_FALSE;
        }

        HRESULT STDMETHODCALLTYPE accLocation(long* left,
                                              long* top,
                                              long* width,
                                              long* height,
                                              VARIANT childId) override
        {
            if (!left || !top || !width || !height)
            {
                return E_POINTER;
            }

            RECT bounds{};
            if (IsSelf(childId))
            {
                if (!data_ || !data_->window || !GetWindowRect(data_->window, &bounds))
                {
                    return E_FAIL;
                }
            }
            else
            {
                Item item;
                const HRESULT result = TryGetItem(childId, &item);
                if (FAILED(result))
                {
                    return result;
                }
                bounds = item.bounds;
            }

            *left = bounds.left;
            *top = bounds.top;
            *width = std::max<LONG>(0, bounds.right - bounds.left);
            *height = std::max<LONG>(0, bounds.bottom - bounds.top);
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE accNavigate(long direction,
                                              VARIANT start,
                                              VARIANT* end) override
        {
            if (!end)
            {
                return E_POINTER;
            }
            SetEmptyVariant(end);
            const auto items = Snapshot();
            const long count = static_cast<long>(items.size());
            if (count == 0)
            {
                return S_FALSE;
            }

            long startId = IsSelf(start) ? 0 : ChildId(start);
            if (!IsSelf(start) && (startId <= 0 || startId > count))
            {
                return E_INVALIDARG;
            }

            long resultId = 0;
            switch (direction)
            {
            case NAVDIR_FIRSTCHILD:
                if (!IsSelf(start))
                {
                    return S_FALSE;
                }
                resultId = 1;
                break;
            case NAVDIR_LASTCHILD:
                if (!IsSelf(start))
                {
                    return S_FALSE;
                }
                resultId = count;
                break;
            case NAVDIR_NEXT:
                resultId = startId == 0 ? 1 : startId + 1;
                break;
            case NAVDIR_PREVIOUS:
                resultId = startId == 0 ? count : startId - 1;
                break;
            case NAVDIR_UP:
                if (startId > 0)
                {
                    SetChildVariant(end, CHILDID_SELF);
                    return S_OK;
                }
                return S_FALSE;
            default:
                return E_NOTIMPL;
            }

            if (resultId <= 0 || resultId > count)
            {
                return S_FALSE;
            }
            SetChildVariant(end, resultId);
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE accHitTest(long left, long top, VARIANT* childId) override
        {
            if (!childId)
            {
                return E_POINTER;
            }
            SetEmptyVariant(childId);
            const POINT point{left, top};
            const auto items = Snapshot();
            for (std::size_t index = 0; index < items.size(); ++index)
            {
                if (PtInRect(&items[index].bounds, point) != FALSE)
                {
                    SetChildVariant(childId, static_cast<long>(index + 1));
                    return S_OK;
                }
            }

            RECT windowRect{};
            if (data_ && data_->window && GetWindowRect(data_->window, &windowRect)
                && PtInRect(&windowRect, point) != FALSE)
            {
                SetChildVariant(childId, CHILDID_SELF);
                return S_OK;
            }
            return S_FALSE;
        }

        HRESULT STDMETHODCALLTYPE accDoDefaultAction(VARIANT childId) override
        {
            if (IsSelf(childId))
            {
                return E_NOTIMPL;
            }
            if (FAILED(TryGetItem(childId, nullptr)) || !data_ || !data_->defaultActionProvider)
            {
                return E_INVALIDARG;
            }
            return data_->defaultActionProvider(ChildId(childId)) ? S_OK : S_FALSE;
        }

        HRESULT STDMETHODCALLTYPE put_accName(VARIANT, BSTR) override
        {
            return E_NOTIMPL;
        }

        HRESULT STDMETHODCALLTYPE put_accValue(VARIANT, BSTR) override
        {
            return E_NOTIMPL;
        }

    private:
        static bool IsSelf(const VARIANT& childId)
        {
            return childId.vt == VT_I4 && childId.lVal == CHILDID_SELF;
        }

        static long ChildId(const VARIANT& childId)
        {
            return childId.lVal;
        }

        std::vector<Item> Snapshot() const
        {
            if (!data_ || !data_->snapshotProvider)
            {
                return {};
            }
            try
            {
                return data_->snapshotProvider();
            }
            catch (...)
            {
                return {};
            }
        }

        long FocusedChildId() const
        {
            if (!data_ || !data_->focusedChildProvider)
            {
                return 0;
            }
            try
            {
                return data_->focusedChildProvider();
            }
            catch (...)
            {
                return 0;
            }
        }

        HRESULT TryGetItem(const VARIANT& childId,
                           Item* item,
                           const std::vector<Item>* existingItems = nullptr) const
        {
            if (item)
            {
                *item = Item{};
            }
            if (childId.vt != VT_I4 || childId.lVal <= CHILDID_SELF)
            {
                return E_INVALIDARG;
            }

            const std::vector<Item> items = existingItems ? std::vector<Item>{} : Snapshot();
            const std::vector<Item>* source = existingItems ? existingItems : &items;
            const long index = childId.lVal - 1;
            if (index < 0 || index >= static_cast<long>(source->size()))
            {
                return E_INVALIDARG;
            }
            if (item)
            {
                *item = (*source)[static_cast<std::size_t>(index)];
            }
            return S_OK;
        }

        std::atomic<ULONG> referenceCount_{1};
        std::shared_ptr<Data> data_;
    };

    MainWindowAccessibility::MainWindowAccessibility(HWND window,
                                                     SnapshotProvider snapshotProvider,
                                                     FocusedChildProvider focusedChildProvider,
                                                     FocusProvider focusProvider,
                                                     DefaultActionProvider defaultActionProvider,
                                                     std::wstring rootName,
                                                     std::wstring rootDescription,
                                                     long rootRole)
        : data_(std::make_shared<Data>())
    {
        data_->window = window;
        data_->snapshotProvider = std::move(snapshotProvider);
        data_->focusedChildProvider = std::move(focusedChildProvider);
        data_->focusProvider = std::move(focusProvider);
        data_->defaultActionProvider = std::move(defaultActionProvider);
        data_->rootName = std::move(rootName);
        data_->rootDescription = std::move(rootDescription);
        data_->rootRole = rootRole;
    }

    MainWindowAccessibility::~MainWindowAccessibility()
    {
        if (data_)
        {
            data_->window = nullptr;
            data_->snapshotProvider = {};
            data_->focusedChildProvider = {};
            data_->focusProvider = {};
            data_->defaultActionProvider = {};
        }
    }

    LRESULT MainWindowAccessibility::HandleGetObject(WPARAM wParam, LPARAM lParam) const
    {
        if (!data_ || lParam != OBJID_CLIENT)
        {
            return 0;
        }

        auto* object = new (std::nothrow) AccessibleObject(data_);
        if (!object)
        {
            return 0;
        }
        const LRESULT result = LresultFromObject(IID_IAccessible, wParam, object);
        object->Release();
        return result;
    }

    void MainWindowAccessibility::NotifyFocusChanged() const
    {
        if (!data_ || !data_->window)
        {
            return;
        }
        long childId = CHILDID_SELF;
        if (data_->focusedChildProvider)
        {
            try
            {
                childId = data_->focusedChildProvider();
            }
            catch (...)
            {
                childId = CHILDID_SELF;
            }
        }
        NotifyWinEvent(EVENT_OBJECT_FOCUS, data_->window, OBJID_CLIENT, childId);
    }

    void MainWindowAccessibility::NotifyStateChanged(long childId) const
    {
        if (data_ && data_->window)
        {
            NotifyWinEvent(EVENT_OBJECT_STATECHANGE,
                           data_->window,
                           OBJID_CLIENT,
                           childId > CHILDID_SELF ? childId : CHILDID_SELF);
        }
    }
}
