#pragma once

#include <windows.h>
#include <oleidl.h>

#include <functional>

namespace hyperbrowse::ui
{
    class ExternalDropTarget final : public IDropTarget
    {
    public:
        using DropCallback = std::function<DWORD(IDataObject*, DWORD, POINT)>;
        using DragLeaveCallback = std::function<void()>;

        ExternalDropTarget(HWND windowHandle,
                           DropCallback dragOverCallback,
                           DropCallback dropCallback,
                           DragLeaveCallback dragLeaveCallback);

        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override;
        ULONG STDMETHODCALLTYPE AddRef() override;
        ULONG STDMETHODCALLTYPE Release() override;
        HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* dataObject,
                                            DWORD keyState,
                                            POINTL point,
                                            DWORD* effect) override;
        HRESULT STDMETHODCALLTYPE DragOver(DWORD keyState,
                                           POINTL point,
                                           DWORD* effect) override;
        HRESULT STDMETHODCALLTYPE DragLeave() override;
        HRESULT STDMETHODCALLTYPE Drop(IDataObject* dataObject,
                                        DWORD keyState,
                                        POINTL point,
                                        DWORD* effect) override;

    private:
        HRESULT InvokeDragOver(DWORD keyState, POINTL point, DWORD* effect);

        HWND windowHandle_{};
        IDataObject* lastDataObject_{};
        DropCallback dragOverCallback_;
        DropCallback dropCallback_;
        DragLeaveCallback dragLeaveCallback_;
        ULONG refCount_{1};
    };
}
