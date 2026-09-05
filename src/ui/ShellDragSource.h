#pragma once

#include <objidl.h>
#include <wrl/client.h>

#include <string>
#include <vector>

namespace hyperbrowse::ui
{
    bool CreateShellFileDataObject(const std::vector<std::wstring>& paths,
                                   Microsoft::WRL::ComPtr<IDataObject>* dataObject);
    Microsoft::WRL::ComPtr<IDropSource> CreateShellFileDragSource();
}
