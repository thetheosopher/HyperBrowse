#pragma once

#include <string>
#include <vector>

namespace hyperbrowse::services
{
    bool QueryFileAssociationDefaults(std::vector<bool>* defaults,
                                      std::wstring* errorMessage = nullptr);
    bool ApplyFileAssociationDefaults(const std::vector<bool>& defaults,
                                      std::wstring* errorMessage = nullptr,
                                      bool* defaultsRejected = nullptr);
}