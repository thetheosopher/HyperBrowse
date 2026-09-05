#include "ui/ExternalDropTarget.h"

#include <utility>

namespace hyperbrowse::ui
{
    ExternalDropTarget::ExternalDropTarget(HWND windowHandle,
                                           DropCallback dragOverCallback,
                                           DropCallback dropCallback,
                                           DragLeaveCallback dragLeaveCallback)
        : windowHandle_(windowHandle)
        , dragOverCallback_(std::move(dragOverCallback))
        , dropCallback_(std::move(dropCallback))
        , dragLeaveCallback_(std::move(dragLeaveCallback))
    {
    }

    HRESULT STDMETHODCALLTYPE ExternalDropTarget::QueryInterface(REFIID riid, void** object)
    {
        if (!object)
        {
            return E_POINTER;
        }

        *object = nullptr;
        if (riid == IID_IUnknown || riid == IID_IDropTarget)
        {
            *object = static_cast<IDropTarget*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE ExternalDropTarget::AddRef()
    {
        return ++refCount_;
    }

    ULONG STDMETHODCALLTYPE ExternalDropTarget::Release()
    {
        const ULONG remaining = --refCount_;
        if (remaining == 0)
        {
            delete this;
        }
        return remaining;
    }

    HRESULT STDMETHODCALLTYPE ExternalDropTarget::DragEnter(IDataObject* dataObject,
                                                             DWORD keyState,
                                                             POINTL point,
                                                             DWORD* effect)
    {
        lastDataObject_ = dataObject;
        return InvokeDragOver(keyState, point, effect);
    }

    HRESULT STDMETHODCALLTYPE ExternalDropTarget::DragOver(DWORD keyState,
                                                            POINTL point,
                                                            DWORD* effect)
    {
        return InvokeDragOver(keyState, point, effect);
    }

    HRESULT STDMETHODCALLTYPE ExternalDropTarget::DragLeave()
    {
        if (dragLeaveCallback_)
        {
            dragLeaveCallback_();
        }
        lastDataObject_ = nullptr;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE ExternalDropTarget::Drop(IDataObject* dataObject,
                                                        DWORD keyState,
                                                        POINTL point,
                                                        DWORD* effect)
    {
        if (!effect)
        {
            return E_POINTER;
        }

        if (!windowHandle_ || !dropCallback_)
        {
            *effect = DROPEFFECT_NONE;
        }
        else
        {
            POINT clientPoint{point.x, point.y};
            ScreenToClient(windowHandle_, &clientPoint);
            *effect = dropCallback_(dataObject, keyState, clientPoint);
        }

        lastDataObject_ = nullptr;
        return S_OK;
    }

    HRESULT ExternalDropTarget::InvokeDragOver(DWORD keyState, POINTL point, DWORD* effect)
    {
        if (!effect)
        {
            return E_POINTER;
        }

        if (!windowHandle_ || !dragOverCallback_)
        {
            *effect = DROPEFFECT_NONE;
            return S_OK;
        }

        POINT clientPoint{point.x, point.y};
        ScreenToClient(windowHandle_, &clientPoint);
        *effect = dragOverCallback_(lastDataObject_, keyState, clientPoint);
        return S_OK;
    }
}
