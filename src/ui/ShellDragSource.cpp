#include "ui/ShellDragSource.h"

#include <shlobj.h>
#include <shobjidl.h>

namespace hyperbrowse::ui
{
    namespace
    {
        class ShellFileDragSource final : public IDropSource
        {
        public:
            HRESULT STDMETHODCALLTYPE QueryInterface(REFIID interfaceId, void** object) override
            {
                if (!object)
                {
                    return E_POINTER;
                }

                *object = nullptr;
                if (interfaceId == IID_IUnknown || interfaceId == IID_IDropSource)
                {
                    *object = static_cast<IDropSource*>(this);
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
                const ULONG remainingReferences = --referenceCount_;
                if (remainingReferences == 0)
                {
                    delete this;
                }
                return remainingReferences;
            }

            HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL escapePressed, DWORD keyState) override
            {
                if (escapePressed)
                {
                    return DRAGDROP_S_CANCEL;
                }

                return (keyState & MK_LBUTTON) == 0 ? DRAGDROP_S_DROP : S_OK;
            }

            HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD effect) override
            {
                (void)effect;
                return DRAGDROP_S_USEDEFAULTCURSORS;
            }

        private:
            ULONG referenceCount_{1};
        };
    }

    Microsoft::WRL::ComPtr<IDropSource> CreateShellFileDragSource()
    {
        Microsoft::WRL::ComPtr<IDropSource> dropSource;
        dropSource.Attach(new ShellFileDragSource());
        return dropSource;
    }

    bool CreateShellFileDataObject(const std::vector<std::wstring>& paths,
                                   Microsoft::WRL::ComPtr<IDataObject>* dataObject)
    {
        if (!dataObject || paths.empty())
        {
            return false;
        }

        std::vector<PIDLIST_ABSOLUTE> itemPidls;
        std::vector<PCIDLIST_ABSOLUTE> absolutePidls;
        itemPidls.reserve(paths.size());
        absolutePidls.reserve(paths.size());
        for (const std::wstring& path : paths)
        {
            if (PIDLIST_ABSOLUTE itemPidl = ILCreateFromPathW(path.c_str()))
            {
                itemPidls.push_back(itemPidl);
                absolutePidls.push_back(itemPidl);
            }
        }

        if (itemPidls.empty())
        {
            return false;
        }

        Microsoft::WRL::ComPtr<IShellItemArray> shellItemArray;
        HRESULT result = SHCreateShellItemArrayFromIDLists(static_cast<UINT>(absolutePidls.size()),
                                                            absolutePidls.data(),
                                                            shellItemArray.GetAddressOf());
        if (SUCCEEDED(result) && shellItemArray)
        {
            result = shellItemArray->BindToHandler(nullptr,
                                                    BHID_DataObject,
                                                    IID_PPV_ARGS(dataObject->GetAddressOf()));
        }

        for (PIDLIST_ABSOLUTE itemPidl : itemPidls)
        {
            ILFree(itemPidl);
        }

        return SUCCEEDED(result) && *dataObject;
    }
}
