#include "services/FileAssociationService.h"

#include <shlobj.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include <cwchar>
#include <limits>
#include <span>
#include <string_view>
#include <vector>

#include "decode/ImageDecoder.h"

namespace
{
    using Microsoft::WRL::ComPtr;

    constexpr wchar_t kApplicationRegistryPath[] = L"Software\\HyperBrowse";
    constexpr wchar_t kCapabilitiesRegistryPath[] = L"Software\\HyperBrowse\\Capabilities";
    constexpr wchar_t kFileAssociationsRegistryPath[] = L"Software\\HyperBrowse\\Capabilities\\FileAssociations";
    constexpr wchar_t kRegisteredApplicationsRegistryPath[] = L"Software\\RegisteredApplications";
    constexpr wchar_t kProgId[] = L"HyperBrowse.Image";
    constexpr wchar_t kRegisteredApplicationName[] = L"HyperBrowse";

    std::wstring FormatFailure(std::wstring_view action, DWORD errorCode)
    {
        wchar_t messageBuffer[256]{};
        const DWORD messageLength = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                                   nullptr,
                                                   errorCode,
                                                   0,
                                                   messageBuffer,
                                                   static_cast<DWORD>(sizeof(messageBuffer) / sizeof(messageBuffer[0])),
                                                   nullptr);
        std::wstring message(action);
        if (messageLength != 0)
        {
            message.append(L" ");
            message.append(messageBuffer, messageLength);
            while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n'))
            {
                message.pop_back();
            }
        }
        else
        {
            message.append(L" (error ");
            message.append(std::to_wstring(errorCode));
            message.push_back(L')');
        }
        return message;
    }

    std::wstring FormatHResultFailure(std::wstring_view action, HRESULT result)
    {
        return FormatFailure(action, static_cast<DWORD>(result));
    }

    bool SetRegistryString(const wchar_t* path,
                           const wchar_t* valueName,
                           std::wstring_view value,
                           std::wstring* errorMessage)
    {
        if (value.size() > (std::numeric_limits<DWORD>::max() / sizeof(wchar_t)) - 1)
        {
            if (errorMessage)
            {
                *errorMessage = L"The application path is too long to register.";
            }
            return false;
        }

        HKEY key{};
        DWORD disposition = 0;
        const LSTATUS openResult = RegCreateKeyExW(HKEY_CURRENT_USER,
                                                   path,
                                                   0,
                                                   nullptr,
                                                   0,
                                                   KEY_WRITE,
                                                   nullptr,
                                                   &key,
                                                   &disposition);
        if (openResult != ERROR_SUCCESS)
        {
            if (errorMessage)
            {
                *errorMessage = FormatFailure(L"Could not open the HyperBrowse registry settings.", openResult);
            }
            return false;
        }

        const DWORD byteCount = static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
        const LSTATUS setResult = RegSetValueExW(key,
                                                 valueName,
                                                 0,
                                                 REG_SZ,
                                                 reinterpret_cast<const BYTE*>(value.data()),
                                                 byteCount);
        RegCloseKey(key);
        if (setResult != ERROR_SUCCESS)
        {
            if (errorMessage)
            {
                *errorMessage = FormatFailure(L"Could not save the HyperBrowse file association.", setResult);
            }
            return false;
        }

        return true;
    }

    bool GetCurrentExecutablePath(std::wstring* executablePath, std::wstring* errorMessage)
    {
        if (!executablePath)
        {
            return false;
        }

        std::vector<wchar_t> buffer(260);
        while (true)
        {
            const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
            if (length == 0)
            {
                if (errorMessage)
                {
                    *errorMessage = FormatFailure(L"Could not locate the HyperBrowse executable.", GetLastError());
                }
                return false;
            }

            if (length < buffer.size() - 1)
            {
                executablePath->assign(buffer.data(), length);
                return true;
            }

            buffer.resize(buffer.size() * 2);
        }
    }

    bool EnsureApplicationRegistration(std::wstring* errorMessage)
    {
        std::wstring executablePath;
        if (!GetCurrentExecutablePath(&executablePath, errorMessage))
        {
            return false;
        }

        std::wstring openCommand = L"\"";
        openCommand.append(executablePath);
        openCommand.append(L"\" \"%1\"");
        std::wstring iconPath = executablePath;
        iconPath.append(L",0");

        if (!SetRegistryString(kApplicationRegistryPath,
                               L"",
                               L"HyperBrowse Image Browser",
                               errorMessage)
            || !SetRegistryString(kCapabilitiesRegistryPath,
                                  L"ApplicationName",
                                  L"HyperBrowse",
                                  errorMessage)
            || !SetRegistryString(kCapabilitiesRegistryPath,
                                  L"ApplicationDescription",
                                  L"High-performance native image browser for Windows",
                                  errorMessage)
            || !SetRegistryString(kCapabilitiesRegistryPath,
                                  L"ApplicationIcon",
                                  iconPath,
                                  errorMessage)
            || !SetRegistryString(kRegisteredApplicationsRegistryPath,
                                  kRegisteredApplicationName,
                                  kCapabilitiesRegistryPath,
                                  errorMessage)
            || !SetRegistryString(L"Software\\Classes\\HyperBrowse.Image",
                                  L"",
                                  L"HyperBrowse Image",
                                  errorMessage)
            || !SetRegistryString(L"Software\\Classes\\HyperBrowse.Image\\DefaultIcon",
                                  L"",
                                  iconPath,
                                  errorMessage)
            || !SetRegistryString(L"Software\\Classes\\HyperBrowse.Image\\shell\\open\\command",
                                  L"",
                                  openCommand,
                                  errorMessage))
        {
            return false;
        }

        for (const hyperbrowse::decode::SupportedFileType& fileType : hyperbrowse::decode::SupportedFileTypes())
        {
            std::wstring extension = L".";
            extension.append(fileType.extension);
            const std::wstring openWithPath = std::wstring(L"Software\\Classes\\")
                + extension
                + L"\\OpenWithProgids";
            if (!SetRegistryString(kFileAssociationsRegistryPath,
                                   extension.c_str(),
                                   kProgId,
                                   errorMessage)
                || !SetRegistryString(openWithPath.c_str(),
                                      kProgId,
                                      L"",
                                      errorMessage))
            {
                return false;
            }
        }

        return true;
    }

    bool CreateAssociationRegistration(ComPtr<IApplicationAssociationRegistration>* registration,
                                       std::wstring* errorMessage)
    {
        if (!registration)
        {
            return false;
        }

        const HRESULT result = CoCreateInstance(CLSID_ApplicationAssociationRegistration,
                                                 nullptr,
                                                 CLSCTX_INPROC_SERVER,
                                                 IID_PPV_ARGS(registration->GetAddressOf()));
        if (FAILED(result))
        {
            if (errorMessage)
            {
                *errorMessage = FormatHResultFailure(L"Windows could not open its file association service.", result);
            }
            return false;
        }

        return true;
    }

    bool QueryCurrentDefault(ComPtr<IApplicationAssociationRegistration>& registration,
                             const std::wstring& extension,
                             bool* isHyperBrowseDefault,
                             std::wstring* errorMessage)
    {
        if (!isHyperBrowseDefault)
        {
            return false;
        }

        LPWSTR association = nullptr;
        const HRESULT result = registration->QueryCurrentDefault(extension.c_str(),
                                                                  AT_FILEEXTENSION,
                                                                  AL_EFFECTIVE,
                                                                  &association);
        if (FAILED(result))
        {
            if (errorMessage)
            {
                *errorMessage = FormatHResultFailure(L"Windows could not read the current file associations.", result);
            }
            return false;
        }

        *isHyperBrowseDefault = association && _wcsicmp(association, kProgId) == 0;
        CoTaskMemFree(association);
        return true;
    }
}

namespace hyperbrowse::services
{
    bool QueryFileAssociationDefaults(std::vector<bool>* defaults, std::wstring* errorMessage)
    {
        if (!defaults)
        {
            return false;
        }

        if (!EnsureApplicationRegistration(errorMessage))
        {
            return false;
        }

        ComPtr<IApplicationAssociationRegistration> registration;
        if (!CreateAssociationRegistration(&registration, errorMessage))
        {
            return false;
        }

        defaults->clear();
        defaults->reserve(hyperbrowse::decode::SupportedFileTypes().size());
        for (const hyperbrowse::decode::SupportedFileType& fileType : hyperbrowse::decode::SupportedFileTypes())
        {
            std::wstring extension = L".";
            extension.append(fileType.extension);
            bool isDefault = false;
            if (!QueryCurrentDefault(registration, extension, &isDefault, errorMessage))
            {
                defaults->clear();
                return false;
            }
            defaults->push_back(isDefault);
        }

        return true;
    }

    bool ApplyFileAssociationDefaults(const std::vector<bool>& defaults,
                                      std::wstring* errorMessage,
                                      bool* defaultsRejected)
    {
        if (defaultsRejected)
        {
            *defaultsRejected = false;
        }

        const std::span<const hyperbrowse::decode::SupportedFileType> supportedFileTypes =
            hyperbrowse::decode::SupportedFileTypes();
        if (defaults.size() != supportedFileTypes.size())
        {
            if (errorMessage)
            {
                *errorMessage = L"The selected file association list is out of date. Please reopen the page and try again.";
            }
            return false;
        }

        if (!EnsureApplicationRegistration(errorMessage))
        {
            return false;
        }

        ComPtr<IApplicationAssociationRegistration> registration;
        if (!CreateAssociationRegistration(&registration, errorMessage))
        {
            return false;
        }

        for (std::size_t index = 0; index < supportedFileTypes.size(); ++index)
        {
            if (!defaults[index])
            {
                continue;
            }

            std::wstring extension = L".";
            extension.append(supportedFileTypes[index].extension);
            const HRESULT result = registration->SetAppAsDefault(kRegisteredApplicationName,
                                                                  extension.c_str(),
                                                                  AT_FILEEXTENSION);
            if (FAILED(result))
            {
                const std::wstring extensionRegistryPath = std::wstring(L"Software\\Classes\\") + extension;
                if (!SetRegistryString(extensionRegistryPath.c_str(), L"", kProgId, errorMessage))
                {
                    return false;
                }
            }

            bool isDefault = false;
            if (!QueryCurrentDefault(registration, extension, &isDefault, errorMessage))
            {
                return false;
            }
            if (!isDefault)
            {
                if (defaultsRejected)
                {
                    *defaultsRejected = true;
                }
                if (errorMessage)
                {
                    *errorMessage = L"Windows did not accept HyperBrowse as the default for "
                        + extension
                        + L". Use Windows Default apps to choose it for that format.";
                }
                return false;
            }
        }

        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
        return true;
    }
}