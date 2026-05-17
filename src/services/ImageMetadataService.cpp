#include "services/ImageMetadataService.h"

#include <propsys.h>
#include <propvarutil.h>
#include <shobjidl.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <condition_variable>
#include <cwctype>
#include <cstring>
#include <string_view>
#include <thread>

#if defined(HYPERBROWSE_ENABLE_LIBRAW)
#include <libraw/libraw.h>
#endif

#include "decode/ImageDecoder.h"
#include "util/HashUtils.h"
#include "util/ResourceSizing.h"
#include "util/StringConvert.h"

namespace
{
    using Microsoft::WRL::ComPtr;

    constexpr std::size_t kDefaultMetadataWorkerCount = 1;
    constexpr std::size_t kMinimumMetadataWorkerCount = 2;
    constexpr std::size_t kMaximumMetadataWorkerCount = 8;
    constexpr std::size_t kDefaultMetadataCacheCapacityEntries = 512;
    constexpr std::size_t kConservativeMetadataCacheCapacityEntries = 2048;
    constexpr std::size_t kPerformanceMetadataCacheCapacityEntries = 8192;
    constexpr std::uint64_t kMinimumMetadataCacheCapacityEntries = 2048;
    constexpr std::uint64_t kConservativeMaximumMetadataCacheCapacityEntries = 8192;
    constexpr std::uint64_t kMaximumMetadataCacheCapacityEntries = 65536;

    std::size_t ResolveMetadataWorkerCount(std::size_t requestedWorkerCount,
                                           hyperbrowse::util::ResourceProfile resourceProfile)
    {
        if (requestedWorkerCount != 0)
        {
            return std::max<std::size_t>(1, requestedWorkerCount);
        }

        const unsigned int hardwareConcurrency = std::thread::hardware_concurrency();
        if (hardwareConcurrency == 0)
        {
            return kDefaultMetadataWorkerCount;
        }

        const std::size_t normalized = static_cast<std::size_t>(hardwareConcurrency);
        switch (resourceProfile)
        {
        case hyperbrowse::util::ResourceProfile::Conservative:
            return std::clamp<std::size_t>(normalized / 8U, 1U, 4U);
        case hyperbrowse::util::ResourceProfile::Performance:
            return std::clamp<std::size_t>(normalized / 2U, kMinimumMetadataWorkerCount, kMaximumMetadataWorkerCount);
        case hyperbrowse::util::ResourceProfile::Balanced:
        default:
        {
            const std::size_t preferredWorkerCount = std::max<std::size_t>(
                kMinimumMetadataWorkerCount,
                normalized / 4U);
            return std::min(preferredWorkerCount, kMaximumMetadataWorkerCount);
        }
        }
    }

    std::size_t ResolveMetadataCacheCapacityEntries(std::size_t requestedCapacityEntries,
                                                    hyperbrowse::util::ResourceProfile resourceProfile)
    {
        if (requestedCapacityEntries != 0)
        {
            return std::max<std::size_t>(1, requestedCapacityEntries);
        }

        const auto memorySnapshot = hyperbrowse::util::QueryMemorySnapshot();
        if (!memorySnapshot.IsValid() || memorySnapshot.availablePhysicalBytes == 0)
        {
            switch (resourceProfile)
            {
            case hyperbrowse::util::ResourceProfile::Conservative:
                return kConservativeMetadataCacheCapacityEntries;
            case hyperbrowse::util::ResourceProfile::Performance:
                return kPerformanceMetadataCacheCapacityEntries;
            case hyperbrowse::util::ResourceProfile::Balanced:
            default:
                return kDefaultMetadataCacheCapacityEntries;
            }
        }

        std::uint64_t preferredEntryCount = memorySnapshot.availablePhysicalBytes / (256ULL * 1024ULL);
        std::uint64_t maximumEntryCount = kMaximumMetadataCacheCapacityEntries;
        switch (resourceProfile)
        {
        case hyperbrowse::util::ResourceProfile::Conservative:
            preferredEntryCount = memorySnapshot.availablePhysicalBytes / (512ULL * 1024ULL);
            maximumEntryCount = kConservativeMaximumMetadataCacheCapacityEntries;
            break;
        case hyperbrowse::util::ResourceProfile::Performance:
            preferredEntryCount = memorySnapshot.availablePhysicalBytes / (128ULL * 1024ULL);
            break;
        case hyperbrowse::util::ResourceProfile::Balanced:
        default:
            break;
        }

        const std::uint64_t clampedEntryCount = std::clamp(preferredEntryCount,
                                                           kMinimumMetadataCacheCapacityEntries,
                                                           maximumEntryCount);
        return hyperbrowse::util::SaturatingCastToSizeT(clampedEntryCount);
    }

    class ComScope
    {
    public:
        ComScope()
        {
            const HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            shouldUninitialize_ = SUCCEEDED(result) || result == S_FALSE;
        }

        ~ComScope()
        {
            if (shouldUninitialize_)
            {
                CoUninitialize();
            }
        }

    private:
        bool shouldUninitialize_{};
    };

    std::wstring PropertyToString(IPropertyStore* propertyStore, REFPROPERTYKEY key)
    {
        if (!propertyStore)
        {
            return {};
        }

        PROPVARIANT value;
        PropVariantInit(&value);
        const HRESULT getResult = propertyStore->GetValue(key, &value);
        if (FAILED(getResult) || value.vt == VT_EMPTY)
        {
            PropVariantClear(&value);
            return {};
        }

        PWSTR displayValue = nullptr;
        const HRESULT formatResult = PSFormatForDisplayAlloc(key, value, PDFF_DEFAULT, &displayValue);
        std::wstring formattedValue;
        if (SUCCEEDED(formatResult) && displayValue)
        {
            formattedValue = displayValue;
            CoTaskMemFree(displayValue);
        }

        PropVariantClear(&value);
        if (formattedValue == L"No data")
        {
            return {};
        }

        return formattedValue;
    }

    std::wstring PropertyToString(IPropertyStore* propertyStore, PCWSTR canonicalName)
    {
        PROPERTYKEY key{};
        if (FAILED(PSGetPropertyKeyFromName(canonicalName, &key)))
        {
            return {};
        }

        return PropertyToString(propertyStore, key);
    }

    std::int64_t PropertyToFileTime(IPropertyStore* propertyStore, PCWSTR canonicalName)
    {
        if (!propertyStore)
        {
            return 0;
        }

        PROPERTYKEY key{};
        if (FAILED(PSGetPropertyKeyFromName(canonicalName, &key)))
        {
            return 0;
        }

        PROPVARIANT value;
        PropVariantInit(&value);
        const HRESULT getResult = propertyStore->GetValue(key, &value);
        if (FAILED(getResult) || value.vt != VT_FILETIME)
        {
            PropVariantClear(&value);
            return 0;
        }

        const ULARGE_INTEGER fileTime{value.filetime.dwLowDateTime, value.filetime.dwHighDateTime};
        PropVariantClear(&value);
        return static_cast<std::int64_t>(fileTime.QuadPart);
    }

    int PropertyToInt32(IPropertyStore* propertyStore, PCWSTR canonicalName)
    {
        if (!propertyStore)
        {
            return 0;
        }

        PROPERTYKEY key{};
        if (FAILED(PSGetPropertyKeyFromName(canonicalName, &key)))
        {
            return 0;
        }

        PROPVARIANT value;
        PropVariantInit(&value);
        const HRESULT getResult = propertyStore->GetValue(key, &value);
        if (FAILED(getResult) || value.vt == VT_EMPTY)
        {
            PropVariantClear(&value);
            return 0;
        }

        ULONG convertedValue = 0;
        const HRESULT convertResult = PropVariantToUInt32(value, &convertedValue);
        PropVariantClear(&value);
        return SUCCEEDED(convertResult) ? static_cast<int>(convertedValue) : 0;
    }

    std::wstring PropertyCanonicalName(REFPROPERTYKEY key)
    {
        PWSTR canonicalName = nullptr;
        if (FAILED(PSGetNameFromPropertyKey(key, &canonicalName)) || !canonicalName)
        {
            return {};
        }

        std::wstring result = canonicalName;
        CoTaskMemFree(canonicalName);
        return result;
    }

    std::wstring PropertyDisplayName(REFPROPERTYKEY key)
    {
        ComPtr<IPropertyDescription> description;
        if (FAILED(PSGetPropertyDescription(key, IID_PPV_ARGS(description.GetAddressOf()))) || !description)
        {
            return {};
        }

        PWSTR displayName = nullptr;
        if (FAILED(description->GetDisplayName(&displayName)) || !displayName)
        {
            return {};
        }

        std::wstring result = displayName;
        CoTaskMemFree(displayName);
        return result;
    }

    bool PropertyNameHasPrefix(std::wstring_view canonicalName, std::wstring_view prefix)
    {
        return canonicalName.size() >= prefix.size()
            && canonicalName.substr(0, prefix.size()) == prefix;
    }

    bool PropertyNameHasSuffix(std::wstring_view canonicalName, std::wstring_view suffix)
    {
        return canonicalName.size() >= suffix.size()
            && canonicalName.substr(canonicalName.size() - suffix.size()) == suffix;
    }

    std::wstring TrimWhitespace(std::wstring_view text)
    {
        std::size_t start = 0;
        while (start < text.size() && iswspace(text[start]) != 0)
        {
            ++start;
        }

        std::size_t end = text.size();
        while (end > start && iswspace(text[end - 1]) != 0)
        {
            --end;
        }

        return std::wstring(text.substr(start, end - start));
    }

    std::wstring HumanizeMetadataToken(std::wstring_view token)
    {
        std::wstring result;
        result.reserve(token.size() + 8);

        for (std::size_t index = 0; index < token.size(); ++index)
        {
            const wchar_t current = token[index];
            const wchar_t previous = index > 0 ? token[index - 1] : L'\0';
            const wchar_t next = (index + 1) < token.size() ? token[index + 1] : L'\0';

            if (current == L'.' || current == L'_' || current == L'-')
            {
                if (!result.empty() && result.back() != L' ')
                {
                    result.push_back(L' ');
                }
                continue;
            }

            const bool needSpace = !result.empty()
                && result.back() != L' '
                && ((iswupper(current) != 0 && (iswlower(previous) != 0 || (iswupper(previous) != 0 && iswlower(next) != 0)))
                    || (iswdigit(current) != 0 && iswalpha(previous) != 0)
                    || (iswalpha(current) != 0 && iswdigit(previous) != 0));
            if (needSpace)
            {
                result.push_back(L' ');
            }

            result.push_back(current);
        }

        return TrimWhitespace(result);
    }

    std::wstring NormalizeMetadataDisplayName(std::wstring_view canonicalName, std::wstring_view displayName)
    {
        const std::wstring trimmedDisplayName = TrimWhitespace(displayName);
        if (!trimmedDisplayName.empty() && !PropertyNameHasPrefix(trimmedDisplayName, L"System."))
        {
            return trimmedDisplayName;
        }

        std::wstring prefix;
        std::wstring_view token = canonicalName;
        if (PropertyNameHasPrefix(token, L"System.Photo."))
        {
            token.remove_prefix(std::size(L"System.Photo.") - 1);
        }
        else if (PropertyNameHasPrefix(token, L"System.Image."))
        {
            token.remove_prefix(std::size(L"System.Image.") - 1);
        }
        else if (PropertyNameHasPrefix(token, L"System.GPS."))
        {
            prefix = L"GPS ";
            token.remove_prefix(std::size(L"System.GPS.") - 1);
        }
        else if (PropertyNameHasPrefix(token, L"System."))
        {
            token.remove_prefix(std::size(L"System.") - 1);
        }

        if (PropertyNameHasSuffix(token, L"Text"))
        {
            token = token.substr(0, token.size() - 4);
        }

        std::wstring normalized = HumanizeMetadataToken(token);
        if (!prefix.empty())
        {
            normalized.insert(0, prefix);
        }

        return normalized.empty() ? trimmedDisplayName : normalized;
    }

    void UpsertMetadataProperty(hyperbrowse::services::ImageMetadata* metadata,
                                std::wstring canonicalName,
                                std::wstring displayName,
                                std::wstring value);

    std::size_t FindInsensitive(std::wstring_view text, std::wstring_view needle)
    {
        if (needle.empty() || needle.size() > text.size())
        {
            return std::wstring_view::npos;
        }

        for (std::size_t index = 0; index + needle.size() <= text.size(); ++index)
        {
            if (hyperbrowse::util::EqualsIgnoreCaseOrdinal(text.substr(index, needle.size()), needle))
            {
                return index;
            }
        }

        return std::wstring_view::npos;
    }

    bool ContainsInsensitive(std::wstring_view text, std::wstring_view needle)
    {
        return FindInsensitive(text, needle) != std::wstring_view::npos;
    }

    std::wstring StripMatchingQuotes(std::wstring value)
    {
        value = TrimWhitespace(value);
        if (value.size() >= 2
            && ((value.front() == L'"' && value.back() == L'"')
                || (value.front() == L'\'' && value.back() == L'\'')))
        {
            value = value.substr(1, value.size() - 2);
        }

        return TrimWhitespace(value);
    }

    std::wstring WidenMetadataText(std::string_view text)
    {
        if (text.empty())
        {
            return {};
        }

        const int utf8Size = MultiByteToWideChar(CP_UTF8,
                                                 MB_ERR_INVALID_CHARS,
                                                 text.data(),
                                                 static_cast<int>(text.size()),
                                                 nullptr,
                                                 0);
        if (utf8Size > 0)
        {
            std::wstring utf8(static_cast<std::size_t>(utf8Size), L'\0');
            const int written = MultiByteToWideChar(CP_UTF8,
                                                    MB_ERR_INVALID_CHARS,
                                                    text.data(),
                                                    static_cast<int>(text.size()),
                                                    utf8.data(),
                                                    utf8Size);
            if (written > 0)
            {
                utf8.resize(static_cast<std::size_t>(written));
                return utf8;
            }
        }

        // PNG tEXt is Latin-1 by spec; fall back there when the payload is not UTF-8.
    return hyperbrowse::util::WidenWithCodePage(text, 28591U);
    }

    std::wstring PropVariantToWideString(const PROPVARIANT& value)
    {
        switch (value.vt)
        {
        case VT_EMPTY:
        case VT_NULL:
            return {};
        case VT_LPWSTR:
            return value.pwszVal ? std::wstring(value.pwszVal) : std::wstring{};
        case VT_BSTR:
            return value.bstrVal ? std::wstring(value.bstrVal, SysStringLen(value.bstrVal)) : std::wstring{};
        case VT_LPSTR:
            return value.pszVal ? WidenMetadataText(value.pszVal) : std::wstring{};
        default:
            break;
        }

        PWSTR converted = nullptr;
        if (SUCCEEDED(PropVariantToStringAlloc(value, &converted)) && converted)
        {
            std::wstring result = converted;
            CoTaskMemFree(converted);
            return result;
        }

        return {};
    }

    struct SwarmUiFieldDefinition
    {
        std::wstring_view key;
        std::wstring_view displayName;
    };

    constexpr std::array<SwarmUiFieldDefinition, 13> kSwarmUiFieldDefinitions{{
        {L"prompt", L"Prompt"},
        {L"negativeprompt", L"Negative prompt"},
        {L"model", L"Model"},
        {L"seed", L"Seed"},
        {L"steps", L"Steps"},
        {L"sampler", L"Sampler"},
        {L"scheduler", L"Scheduler"},
        {L"cfgscale", L"CFG scale"},
        {L"width", L"Width"},
        {L"height", L"Height"},
        {L"swarm_version", L"Swarm version"},
        {L"date", L"Date"},
        {L"generation_time", L"Generation time"},
    }};

    constexpr std::array<std::wstring_view, 17> kKnownPngTextMetadataKeys{{
        L"parameters",
        L"sui_image_params",
        L"prompt",
        L"negativeprompt",
        L"model",
        L"seed",
        L"steps",
        L"sampler",
        L"scheduler",
        L"cfgscale",
        L"width",
        L"height",
        L"swarm_version",
        L"date",
        L"generation_time",
        L"comment",
        L"description",
    }};

    const SwarmUiFieldDefinition* FindSwarmUiFieldDefinition(std::wstring_view key)
    {
        const std::wstring normalizedKey = StripMatchingQuotes(std::wstring(key));
        const auto match = std::find_if(kSwarmUiFieldDefinitions.begin(),
                                        kSwarmUiFieldDefinitions.end(),
                                        [&](const SwarmUiFieldDefinition& field)
                                        {
                                            return hyperbrowse::util::EqualsIgnoreCaseOrdinal(normalizedKey, field.key);
                                        });
        return match != kSwarmUiFieldDefinitions.end() ? &(*match) : nullptr;
    }

    std::wstring BuildCustomMetadataCanonicalName(std::wstring_view prefix, std::wstring_view key)
    {
        std::wstring canonicalName(prefix);
        canonicalName.push_back(L'.');
        canonicalName.append(key);
        return canonicalName;
    }

    bool IsPngFile(const hyperbrowse::browser::BrowserItem& item)
    {
        if (hyperbrowse::util::EqualsIgnoreCaseOrdinal(item.fileType, L"PNG"))
        {
            return true;
        }

        const std::size_t extensionOffset = item.filePath.find_last_of(L'.');
        if (extensionOffset == std::wstring::npos || extensionOffset + 1 >= item.filePath.size())
        {
            return false;
        }

        return hyperbrowse::util::EqualsIgnoreCaseOrdinal(std::wstring_view(item.filePath).substr(extensionOffset + 1), L"png");
    }

    bool IsSwarmUiCarrierMetadataKey(std::wstring_view key)
    {
        return hyperbrowse::util::EqualsIgnoreCaseOrdinal(key, L"parameters")
            || hyperbrowse::util::EqualsIgnoreCaseOrdinal(key, L"sui_image_params")
            || hyperbrowse::util::EqualsIgnoreCaseOrdinal(key, L"comment")
            || hyperbrowse::util::EqualsIgnoreCaseOrdinal(key, L"description");
    }

    bool LooksLikeSwarmUiPayload(std::wstring_view text)
    {
        return ContainsInsensitive(text, L"sui_image_params")
            || (ContainsInsensitive(text, L"prompt")
                && ContainsInsensitive(text, L"negativeprompt")
                && (ContainsInsensitive(text, L"swarm_version")
                    || ContainsInsensitive(text, L"generation_time")
                    || ContainsInsensitive(text, L"model")));
    }

    std::wstring ExtractSwarmUiPayload(std::wstring_view text)
    {
        const std::wstring trimmed = TrimWhitespace(text);
        if (trimmed.empty())
        {
            return {};
        }

        const std::size_t anchor = FindInsensitive(trimmed, L"sui_image_params");
        const std::size_t searchStart = anchor == std::wstring_view::npos
            ? 0
            : anchor + std::size(L"sui_image_params") - 1;
        const std::size_t braceStart = trimmed.find(L'{', searchStart);
        if (braceStart == std::wstring::npos)
        {
            return trimmed;
        }

        int depth = 0;
        for (std::size_t index = braceStart; index < trimmed.size(); ++index)
        {
            if (trimmed[index] == L'{')
            {
                ++depth;
            }
            else if (trimmed[index] == L'}')
            {
                --depth;
                if (depth == 0)
                {
                    return trimmed.substr(braceStart, index - braceStart + 1);
                }
            }
        }

        return trimmed.substr(braceStart);
    }

    bool LooksLikeRecognizedSwarmUiFieldAfterComma(std::wstring_view text, std::size_t commaIndex)
    {
        std::size_t index = commaIndex + 1;
        while (index < text.size() && iswspace(text[index]) != 0)
        {
            ++index;
        }

        const std::size_t keyStart = index;
        while (index < text.size()
               && (iswalnum(text[index]) != 0 || text[index] == L'_' || text[index] == L'-'))
        {
            ++index;
        }
        if (keyStart == index)
        {
            return false;
        }

        const std::wstring key = StripMatchingQuotes(std::wstring(text.substr(keyStart, index - keyStart)));
        while (index < text.size() && iswspace(text[index]) != 0)
        {
            ++index;
        }

        return index < text.size() && text[index] == L':' && FindSwarmUiFieldDefinition(key) != nullptr;
    }

    void ParseSwarmUiMetadataPayload(std::wstring_view rawText,
                                     hyperbrowse::services::ImageMetadata* metadata)
    {
        if (!metadata || rawText.empty() || !LooksLikeSwarmUiPayload(rawText))
        {
            return;
        }

        const std::wstring payload = ExtractSwarmUiPayload(rawText);
        if (payload.empty())
        {
            return;
        }

        std::size_t index = 0;
        while (index < payload.size())
        {
            while (index < payload.size()
                   && (iswspace(payload[index]) != 0 || payload[index] == L',' || payload[index] == L'{'))
            {
                ++index;
            }
            if (index >= payload.size() || payload[index] == L'}')
            {
                break;
            }

            const std::size_t keyStart = index;
            while (index < payload.size()
                   && payload[index] != L':'
                   && payload[index] != L','
                   && payload[index] != L'}')
            {
                ++index;
            }
            if (index >= payload.size() || payload[index] != L':')
            {
                while (index < payload.size() && payload[index] != L',' && payload[index] != L'}')
                {
                    ++index;
                }
                if (index < payload.size() && payload[index] == L',')
                {
                    ++index;
                }
                continue;
            }

            const std::wstring key = StripMatchingQuotes(payload.substr(keyStart, index - keyStart));
            ++index;
            while (index < payload.size() && iswspace(payload[index]) != 0)
            {
                ++index;
            }

            const std::size_t valueStart = index;
            int braceDepth = 0;
            int bracketDepth = 0;
            int parenDepth = 0;
            bool inSingleQuote = false;
            bool inDoubleQuote = false;
            bool escaped = false;
            while (index < payload.size())
            {
                const wchar_t current = payload[index];

                if (escaped)
                {
                    escaped = false;
                    ++index;
                    continue;
                }

                if ((inSingleQuote || inDoubleQuote) && current == L'\\')
                {
                    escaped = true;
                    ++index;
                    continue;
                }

                if (!inDoubleQuote && current == L'\'')
                {
                    inSingleQuote = !inSingleQuote;
                    ++index;
                    continue;
                }
                if (!inSingleQuote && current == L'"')
                {
                    inDoubleQuote = !inDoubleQuote;
                    ++index;
                    continue;
                }

                if (!inSingleQuote && !inDoubleQuote)
                {
                    switch (current)
                    {
                    case L'{':
                        ++braceDepth;
                        break;
                    case L'[':
                        ++bracketDepth;
                        break;
                    case L'(':
                        ++parenDepth;
                        break;
                    case L'}':
                        if (braceDepth == 0 && bracketDepth == 0 && parenDepth == 0)
                        {
                            goto SwarmValueDone;
                        }
                        --braceDepth;
                        break;
                    case L']':
                        if (bracketDepth > 0)
                        {
                            --bracketDepth;
                        }
                        break;
                    case L')':
                        if (parenDepth > 0)
                        {
                            --parenDepth;
                        }
                        break;
                    case L',':
                        if (braceDepth == 0
                            && bracketDepth == 0
                            && parenDepth == 0
                            && LooksLikeRecognizedSwarmUiFieldAfterComma(payload, index))
                        {
                            goto SwarmValueDone;
                        }
                        break;
                    default:
                        break;
                    }
                }

                ++index;
            }

SwarmValueDone:
            if (const SwarmUiFieldDefinition* field = FindSwarmUiFieldDefinition(key))
            {
                const std::wstring value = StripMatchingQuotes(payload.substr(valueStart, index - valueStart));
                if (!value.empty())
                {
                    UpsertMetadataProperty(metadata,
                                           BuildCustomMetadataCanonicalName(L"SwarmUI", field->key),
                                           std::wstring(field->displayName),
                                           value);
                }
            }

            if (index < payload.size() && payload[index] == L',')
            {
                ++index;
            }
            else if (index < payload.size() && payload[index] == L'}')
            {
                ++index;
            }
        }
    }

    std::wstring QueryMetadataText(IWICMetadataQueryReader* metadataReader, std::wstring_view query)
    {
        if (!metadataReader || query.empty())
        {
            return {};
        }

        PROPVARIANT value;
        PropVariantInit(&value);
        const HRESULT result = metadataReader->GetMetadataByName(std::wstring(query).c_str(), &value);
        std::wstring text = SUCCEEDED(result) ? StripMatchingQuotes(PropVariantToWideString(value)) : std::wstring{};
        PropVariantClear(&value);
        return text;
    }

    void ExtractCustomPngTextProperty(std::wstring key,
                                      std::wstring value,
                                      hyperbrowse::services::ImageMetadata* metadata)
    {
        if (!metadata)
        {
            return;
        }

        key = StripMatchingQuotes(std::move(key));
        value = StripMatchingQuotes(std::move(value));
        if (key.empty() || value.empty())
        {
            return;
        }

        const bool duplicateComment = !metadata->comment.empty()
            && IsSwarmUiCarrierMetadataKey(key)
            && hyperbrowse::util::EqualsIgnoreCaseOrdinal(metadata->comment, value);

        if (const SwarmUiFieldDefinition* field = FindSwarmUiFieldDefinition(key))
        {
            UpsertMetadataProperty(metadata,
                                   BuildCustomMetadataCanonicalName(L"SwarmUI", field->key),
                                   std::wstring(field->displayName),
                                   value);
        }
        else if (!duplicateComment)
        {
            UpsertMetadataProperty(metadata,
                                   BuildCustomMetadataCanonicalName(L"PNG.Text", key),
                                   HumanizeMetadataToken(key),
                                   value);
        }

        if (IsSwarmUiCarrierMetadataKey(key) || LooksLikeSwarmUiPayload(value))
        {
            ParseSwarmUiMetadataPayload(value, metadata);
        }
    }

    void ExtractSwarmUiPngTextMetadata(const std::wstring& filePath,
                                       hyperbrowse::services::ImageMetadata* metadata)
    {
        if (!metadata || filePath.empty())
        {
            return;
        }

        ComPtr<IWICImagingFactory> factory;
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory,
                                    nullptr,
                                    CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(factory.GetAddressOf())))
            || !factory)
        {
            return;
        }

        ComPtr<IWICBitmapDecoder> decoder;
        if (FAILED(factory->CreateDecoderFromFilename(filePath.c_str(),
                                                      nullptr,
                                                      GENERIC_READ,
                                                      WICDecodeMetadataCacheOnLoad,
                                                      decoder.GetAddressOf()))
            || !decoder)
        {
            return;
        }

        ComPtr<IWICBitmapFrameDecode> frame;
        if (FAILED(decoder->GetFrame(0, frame.GetAddressOf())) || !frame)
        {
            return;
        }

        ComPtr<IWICMetadataQueryReader> metadataReader;
        if (FAILED(frame->GetMetadataQueryReader(metadataReader.GetAddressOf())) || !metadataReader)
        {
            return;
        }

        for (std::wstring_view key : kKnownPngTextMetadataKeys)
        {
            std::wstring query = L"/tEXt/{str=";
            query.append(key);
            query.append(L"}");

            const std::wstring value = QueryMetadataText(metadataReader.Get(), query);
            if (!value.empty())
            {
                ExtractCustomPngTextProperty(std::wstring(key), value, metadata);
            }
        }
    }

    bool ShouldSuppressDetailedMetadataProperty(std::wstring_view canonicalName)
    {
        return canonicalName == L"System.Image.Dimensions"
            || PropertyNameHasSuffix(canonicalName, L"Numerator")
            || PropertyNameHasSuffix(canonicalName, L"Denominator");
    }

    bool EqualsInsensitive(std::wstring_view lhs, std::wstring_view rhs)
    {
        return _wcsicmp(std::wstring(lhs).c_str(), std::wstring(rhs).c_str()) == 0;
    }

    bool IsDetailedImageMetadataProperty(std::wstring_view canonicalName)
    {
        return PropertyNameHasPrefix(canonicalName, L"System.Photo.")
            || PropertyNameHasPrefix(canonicalName, L"System.Image.")
            || PropertyNameHasPrefix(canonicalName, L"System.GPS.")
            || canonicalName == L"System.Title"
            || canonicalName == L"System.Subject"
            || canonicalName == L"System.Comment"
            || canonicalName == L"System.Author"
            || canonicalName == L"System.Keywords"
            || canonicalName == L"System.Rating"
            || canonicalName == L"System.SimpleRating"
            || canonicalName == L"System.Copyright";
    }

    void UpsertMetadataProperty(hyperbrowse::services::ImageMetadata* metadata,
                                std::wstring canonicalName,
                                std::wstring displayName,
                                std::wstring value)
    {
        if (!metadata || canonicalName.empty() || value.empty() || ShouldSuppressDetailedMetadataProperty(canonicalName))
        {
            return;
        }

        displayName = NormalizeMetadataDisplayName(canonicalName, displayName);
        if (displayName.empty())
        {
            displayName = canonicalName;
        }

        auto existing = std::find_if(metadata->properties.begin(),
                                     metadata->properties.end(),
                                     [&](const hyperbrowse::services::MetadataPropertyEntry& entry)
                                     {
                                         return entry.canonicalName == canonicalName;
                                     });
        if (existing != metadata->properties.end())
        {
            existing->displayName = std::move(displayName);
            existing->value = std::move(value);
            return;
        }

        metadata->properties.push_back(hyperbrowse::services::MetadataPropertyEntry{
            std::move(canonicalName),
            std::move(displayName),
            std::move(value),
        });
    }

    void EnumerateDetailedMetadataProperties(IPropertyStore* propertyStore,
                                             hyperbrowse::services::ImageMetadata* metadata)
    {
        if (!propertyStore || !metadata)
        {
            return;
        }

        DWORD propertyCount = 0;
        if (FAILED(propertyStore->GetCount(&propertyCount)))
        {
            return;
        }

        for (DWORD index = 0; index < propertyCount; ++index)
        {
            PROPERTYKEY key{};
            if (FAILED(propertyStore->GetAt(index, &key)))
            {
                continue;
            }

            const std::wstring canonicalName = PropertyCanonicalName(key);
            if (canonicalName.empty() || !IsDetailedImageMetadataProperty(canonicalName))
            {
                continue;
            }

            const std::wstring value = PropertyToString(propertyStore, key);
            if (value.empty())
            {
                continue;
            }

            UpsertMetadataProperty(metadata, canonicalName, PropertyDisplayName(key), value);
        }
    }

    bool IsCuratedMetadataProperty(std::wstring_view canonicalName)
    {
        return canonicalName == L"System.Image.Dimensions"
            || canonicalName == L"System.Image.HorizontalSize"
            || canonicalName == L"System.Image.VerticalSize"
            || canonicalName == L"System.Photo.CameraManufacturer"
            || canonicalName == L"System.Photo.CameraModel"
            || canonicalName == L"System.Photo.DateTaken"
            || canonicalName == L"System.Photo.ExposureTime"
            || canonicalName == L"System.Photo.FNumber"
            || canonicalName == L"System.Photo.ISOSpeed"
            || canonicalName == L"System.Photo.FocalLength"
            || canonicalName == L"System.Title"
            || canonicalName == L"System.Author"
            || canonicalName == L"System.Keywords"
            || canonicalName == L"System.Comment";
    }

#if defined(HYPERBROWSE_ENABLE_LIBRAW)
    std::wstring WideText(const char* value)
    {
        if (!value || *value == '\0')
        {
            return {};
        }

        return std::wstring(value, value + std::strlen(value));
    }

    int OpenRawFile(LibRaw& processor, const std::wstring& filePath)
    {
#if defined(_WIN32) && defined(LIBRAW_WIN32_UNICODEPATHS)
        return processor.open_file(filePath.c_str());
#else
        return processor.open_file(std::string(filePath.begin(), filePath.end()).c_str());
#endif
    }

    std::wstring FormatTimestamp(time_t timestamp)
    {
        if (timestamp <= 0)
        {
            return {};
        }

        const __time64_t rawTimestamp = static_cast<__time64_t>(timestamp);
        std::tm localTime{};
        if (_localtime64_s(&localTime, &rawTimestamp) != 0)
        {
            return {};
        }

        wchar_t buffer[64]{};
        wcsftime(buffer, std::size(buffer), L"%Y-%m-%d %H:%M", &localTime);
        return buffer;
    }

    void PopulateRawFallback(const hyperbrowse::browser::BrowserItem& item, hyperbrowse::services::ImageMetadata* metadata)
    {
        if (!metadata || !hyperbrowse::decode::IsRawFileType(item.fileType))
        {
            return;
        }

        LibRaw processor;
        if (OpenRawFile(processor, item.filePath) != LIBRAW_SUCCESS)
        {
            return;
        }

        if (processor.adjust_sizes_info_only() != LIBRAW_SUCCESS)
        {
            return;
        }

        if (metadata->cameraMake.empty())
        {
            metadata->cameraMake = WideText(processor.imgdata.idata.make);
        }
        if (metadata->cameraModel.empty())
        {
            metadata->cameraModel = WideText(processor.imgdata.idata.model);
        }
        if (metadata->dateTaken.empty())
        {
            metadata->dateTaken = FormatTimestamp(processor.imgdata.other.timestamp);
        }
        if (metadata->dateTakenTimestampUtc == 0 && processor.imgdata.other.timestamp > 0)
        {
            metadata->dateTakenTimestampUtc = static_cast<std::int64_t>(processor.imgdata.other.timestamp);
        }
        if (metadata->exposureTime.empty() && processor.imgdata.other.shutter > 0.0f)
        {
            metadata->exposureTime = std::to_wstring(processor.imgdata.other.shutter) + L" s";
        }
        if (metadata->fNumber.empty() && processor.imgdata.other.aperture > 0.0f)
        {
            metadata->fNumber = L"f/" + std::to_wstring(processor.imgdata.other.aperture);
        }
        if (metadata->isoSpeed.empty() && processor.imgdata.other.iso_speed > 0.0f)
        {
            metadata->isoSpeed = std::to_wstring(static_cast<int>(processor.imgdata.other.iso_speed));
        }
        if (metadata->focalLength.empty() && processor.imgdata.other.focal_len > 0.0f)
        {
            metadata->focalLength = std::to_wstring(processor.imgdata.other.focal_len) + L" mm";
        }

        UpsertMetadataProperty(metadata, L"System.Photo.CameraManufacturer", L"Camera manufacturer", metadata->cameraMake);
        UpsertMetadataProperty(metadata, L"System.Photo.CameraModel", L"Camera model", metadata->cameraModel);
        UpsertMetadataProperty(metadata, L"System.Photo.DateTaken", L"Date taken", metadata->dateTaken);
        UpsertMetadataProperty(metadata, L"System.Photo.ExposureTime", L"Exposure time", metadata->exposureTime);
        UpsertMetadataProperty(metadata, L"System.Photo.FNumber", L"F-number", metadata->fNumber);
        UpsertMetadataProperty(metadata, L"System.Photo.ISOSpeed", L"ISO speed", metadata->isoSpeed);
        UpsertMetadataProperty(metadata, L"System.Photo.FocalLength", L"Focal length", metadata->focalLength);

        metadata->hasExif = metadata->hasExif
            || !metadata->cameraMake.empty()
            || !metadata->cameraModel.empty()
            || !metadata->dateTaken.empty()
            || !metadata->exposureTime.empty()
            || !metadata->fNumber.empty()
            || !metadata->isoSpeed.empty()
            || !metadata->focalLength.empty();
    }
#endif

    std::wstring JoinLine(std::wstring label, const std::wstring& value)
    {
        if (value.empty())
        {
            return {};
        }

        label.append(value);
        label.append(L"\r\n");
        return label;
    }

    void AppendAllAvailableMetadata(std::wstring* text, const hyperbrowse::services::ImageMetadata& metadata)
    {
        if (!text)
        {
            return;
        }

        bool wroteHeader = false;
        std::vector<hyperbrowse::services::MetadataPropertyEntry> renderedProperties;
        for (const hyperbrowse::services::MetadataPropertyEntry& property : metadata.properties)
        {
            if (property.value.empty() || IsCuratedMetadataProperty(property.canonicalName))
            {
                continue;
            }

            const bool duplicateProperty = std::any_of(renderedProperties.begin(),
                                                       renderedProperties.end(),
                                                       [&](const hyperbrowse::services::MetadataPropertyEntry& candidate)
                                                       {
                                                           return EqualsInsensitive(candidate.displayName, property.displayName)
                                                               && EqualsInsensitive(candidate.value, property.value);
                                                       });
            if (duplicateProperty)
            {
                continue;
            }

            if (!wroteHeader)
            {
                if (!text->empty())
                {
                    text->append(L"\r\n");
                }
                text->append(L"All Available Metadata\r\n");
                wroteHeader = true;
            }

            text->append(JoinLine(property.displayName + L": ", property.value));
            renderedProperties.push_back(property);
        }
    }
}

namespace hyperbrowse::services
{
    std::shared_ptr<const ImageMetadata> ExtractImageMetadata(const browser::BrowserItem& item,
                                                              std::wstring* errorMessage)
    {
        ComScope comScope;

        auto metadata = std::make_shared<ImageMetadata>();
        ComPtr<IPropertyStore> propertyStore;
        const HRESULT storeResult = SHGetPropertyStoreFromParsingName(item.filePath.c_str(),
                                                                      nullptr,
                                                                      GPS_BESTEFFORT,
                                                                      IID_PPV_ARGS(propertyStore.GetAddressOf()));
        if (SUCCEEDED(storeResult) && propertyStore)
        {
            EnumerateDetailedMetadataProperties(propertyStore.Get(), metadata.get());
            metadata->imageWidth = PropertyToInt32(propertyStore.Get(), L"System.Image.HorizontalSize");
            metadata->imageHeight = PropertyToInt32(propertyStore.Get(), L"System.Image.VerticalSize");
            metadata->cameraMake = PropertyToString(propertyStore.Get(), L"System.Photo.CameraManufacturer");
            metadata->cameraModel = PropertyToString(propertyStore.Get(), L"System.Photo.CameraModel");
            metadata->dateTaken = PropertyToString(propertyStore.Get(), L"System.Photo.DateTaken");
            metadata->exposureTime = PropertyToString(propertyStore.Get(), L"System.Photo.ExposureTime");
            metadata->fNumber = PropertyToString(propertyStore.Get(), L"System.Photo.FNumber");
            metadata->isoSpeed = PropertyToString(propertyStore.Get(), L"System.Photo.ISOSpeed");
            metadata->focalLength = PropertyToString(propertyStore.Get(), L"System.Photo.FocalLength");
            metadata->title = PropertyToString(propertyStore.Get(), L"System.Title");
            metadata->author = PropertyToString(propertyStore.Get(), L"System.Author");
            metadata->keywords = PropertyToString(propertyStore.Get(), L"System.Keywords");
            metadata->comment = PropertyToString(propertyStore.Get(), L"System.Comment");
            metadata->dateTakenTimestampUtc = PropertyToFileTime(propertyStore.Get(), L"System.Photo.DateTaken");
        }

        if (IsPngFile(item))
        {
            ExtractSwarmUiPngTextMetadata(item.filePath, metadata.get());
        }

        if (!metadata->comment.empty())
        {
            ParseSwarmUiMetadataPayload(metadata->comment, metadata.get());
        }

#if defined(HYPERBROWSE_ENABLE_LIBRAW)
        PopulateRawFallback(item, metadata.get());
#endif

        metadata->hasExif = metadata->hasExif
            || (metadata->imageWidth > 0 && metadata->imageHeight > 0)
            || !metadata->cameraMake.empty()
            || !metadata->cameraModel.empty()
            || !metadata->dateTaken.empty()
            || !metadata->exposureTime.empty()
            || !metadata->fNumber.empty()
            || !metadata->isoSpeed.empty()
            || !metadata->focalLength.empty();
        metadata->hasIptc = !metadata->author.empty() || !metadata->keywords.empty() || !metadata->comment.empty();
        metadata->hasXmp = !metadata->title.empty() || !metadata->author.empty() || !metadata->keywords.empty() || !metadata->comment.empty();

        std::sort(metadata->properties.begin(),
                  metadata->properties.end(),
                  [](const MetadataPropertyEntry& lhs, const MetadataPropertyEntry& rhs)
                  {
                      const int displayCompare = _wcsicmp(lhs.displayName.c_str(), rhs.displayName.c_str());
                      if (displayCompare != 0)
                      {
                          return displayCompare < 0;
                      }

                      return _wcsicmp(lhs.canonicalName.c_str(), rhs.canonicalName.c_str()) < 0;
                  });

        if (!metadata->hasExif && !metadata->hasIptc && !metadata->hasXmp)
        {
            if (errorMessage)
            {
                *errorMessage = L"No EXIF, IPTC, or XMP metadata was available for this image.";
            }
        }

        return metadata;
    }

    std::wstring FormatImageMetadataReport(const browser::BrowserItem& item, const ImageMetadata& metadata)
    {
        (void)item;

        std::wstring report;
        report.reserve(768);

        std::wstring exifSection;
        exifSection.append(JoinLine(L"Camera Make: ", metadata.cameraMake));
        exifSection.append(JoinLine(L"Camera Model: ", metadata.cameraModel));
        exifSection.append(JoinLine(L"Date Taken: ", metadata.dateTaken));
        exifSection.append(JoinLine(L"Exposure: ", metadata.exposureTime));
        exifSection.append(JoinLine(L"Aperture: ", metadata.fNumber));
        exifSection.append(JoinLine(L"ISO: ", metadata.isoSpeed));
        exifSection.append(JoinLine(L"Focal Length: ", metadata.focalLength));
        if (!exifSection.empty())
        {
            report.append(L"EXIF\r\n");
            report.append(exifSection);
        }

        std::wstring descriptiveSection;
        descriptiveSection.append(JoinLine(L"Title: ", metadata.title));
        descriptiveSection.append(JoinLine(L"Author: ", metadata.author));
        descriptiveSection.append(JoinLine(L"Keywords: ", metadata.keywords));
        descriptiveSection.append(JoinLine(L"Comment: ", metadata.comment));
        if (!descriptiveSection.empty())
        {
            if (!report.empty())
            {
                report.append(L"\r\n");
            }
            report.append(L"IPTC / XMP\r\n");
            report.append(descriptiveSection);
        }

        AppendAllAvailableMetadata(&report, metadata);
        return report;
    }

    std::wstring FormatImageInfoContent(const browser::BrowserItem& item)
    {
        std::wstring content;
        content.reserve(512);
        content.append(L"Path: ");
        content.append(item.filePath);
        content.append(L"\r\nType: ");
        content.append(item.fileType);
        content.append(L"\r\nSize: ");
        content.append(browser::FormatByteSize(item.fileSizeBytes));
        content.append(L"\r\nDimensions: ");
        content.append(browser::FormatDimensionsForItem(item));
        content.append(L"\r\nModified: ");
        content.append(item.modifiedDate);
        return content;
    }

    std::wstring FormatImageInfoExpanded(const ImageMetadata& metadata)
    {
        std::wstring expanded;
        expanded.reserve(512);

        std::wstring exifSection;
        exifSection.append(JoinLine(L"Camera Make: ", metadata.cameraMake));
        exifSection.append(JoinLine(L"Camera Model: ", metadata.cameraModel));
        exifSection.append(JoinLine(L"Date Taken: ", metadata.dateTaken));
        exifSection.append(JoinLine(L"Exposure: ", metadata.exposureTime));
        exifSection.append(JoinLine(L"Aperture: ", metadata.fNumber));
        exifSection.append(JoinLine(L"ISO: ", metadata.isoSpeed));
        exifSection.append(JoinLine(L"Focal Length: ", metadata.focalLength));
        if (!exifSection.empty())
        {
            expanded.append(L"EXIF\r\n");
            expanded.append(exifSection);
        }

        std::wstring descriptiveSection;
        descriptiveSection.append(JoinLine(L"Title: ", metadata.title));
        descriptiveSection.append(JoinLine(L"Author: ", metadata.author));
        descriptiveSection.append(JoinLine(L"Keywords: ", metadata.keywords));
        descriptiveSection.append(JoinLine(L"Comment: ", metadata.comment));
        if (!descriptiveSection.empty())
        {
            if (!expanded.empty())
            {
                expanded.append(L"\r\n");
            }
            expanded.append(L"IPTC / XMP\r\n");
            expanded.append(descriptiveSection);
        }

        AppendAllAvailableMetadata(&expanded, metadata);
        // Trim any trailing whitespace
        while (!expanded.empty() && (expanded.back() == L'\r' || expanded.back() == L'\n'))
        {
            expanded.pop_back();
        }
        return expanded;
    }

    std::size_t ImageMetadataService::MetadataCacheKeyHasher::operator()(const MetadataCacheKey& key) const noexcept
    {
        std::size_t seed = 0;
        util::HashCombine(&seed, util::NormalizePathForComparison(key.filePath));
        util::HashCombine(&seed, key.modifiedTimestampUtc);
        return seed;
    }

    ImageMetadataService::ImageMetadataService(std::size_t workerCount,
                                               std::size_t cacheCapacityEntries,
                                               util::ResourceProfile resourceProfile,
                                               MetadataExtractor extractor)
        : cacheCapacityEntries_(ResolveMetadataCacheCapacityEntries(cacheCapacityEntries, resourceProfile))
        , extractor_(extractor ? std::move(extractor) : MetadataExtractor{ExtractImageMetadata})
    {
        const std::size_t resolvedWorkerCount = ResolveMetadataWorkerCount(workerCount, resourceProfile);
        activeWorkerLimit_ = std::max<std::size_t>(1, resolvedWorkerCount);
        workers_.reserve(resolvedWorkerCount);
        for (std::size_t index = 0; index < resolvedWorkerCount; ++index)
        {
            workers_.emplace_back([this]()
            {
                WorkerLoop();
            });
        }
    }

    ImageMetadataService::~ImageMetadataService()
    {
        {
            std::scoped_lock lock(mutex_);
            shuttingDown_ = true;
            pendingJobs_.clear();
            queuedKeys_.clear();
            inflightKeys_.clear();
            cache_.clear();
            cacheLruOrder_.clear();
        }

        workAvailable_.notify_all();
        for (std::thread& worker : workers_)
        {
            if (worker.joinable())
            {
                worker.join();
            }
        }
    }

    void ImageMetadataService::BindTargetWindow(HWND targetWindow)
    {
        std::scoped_lock lock(mutex_);
        targetWindow_ = targetWindow;
    }

    void ImageMetadataService::Schedule(std::uint64_t sessionId, MetadataWorkItem workItem)
    {
        MetadataCacheKey key{workItem.item.filePath, workItem.item.modifiedTimestampUtc};
        key.filePath = util::NormalizePathForComparison(key.filePath);

        {
            std::scoped_lock lock(mutex_);
            activeSessionId_ = sessionId;
            if (cache_.contains(key) || queuedKeys_.contains(key) || inflightKeys_.contains(key))
            {
                return;
            }

            const auto generation = pathGenerations_.find(key.filePath);
            pendingJobs_.push_back(PendingJob{
                sessionId,
                nextSequence_++,
                generation == pathGenerations_.end() ? 0 : generation->second,
                key,
                std::move(workItem),
            });
            queuedKeys_.insert(key);
        }

        workAvailable_.notify_one();
    }

    void ImageMetadataService::Schedule(std::uint64_t sessionId, std::vector<MetadataWorkItem> workItems)
    {
        {
            std::scoped_lock lock(mutex_);
            activeSessionId_ = sessionId;
            pendingJobs_.clear();
            queuedKeys_.clear();

            for (MetadataWorkItem& workItem : workItems)
            {
                MetadataCacheKey key{workItem.item.filePath, workItem.item.modifiedTimestampUtc};
                key.filePath = util::NormalizePathForComparison(key.filePath);

                if (cache_.contains(key) || queuedKeys_.contains(key) || inflightKeys_.contains(key))
                {
                    continue;
                }

                const auto generation = pathGenerations_.find(key.filePath);
                pendingJobs_.push_back(PendingJob{
                    sessionId,
                    nextSequence_++,
                    generation == pathGenerations_.end() ? 0 : generation->second,
                    key,
                    std::move(workItem),
                });
                queuedKeys_.insert(key);
            }
        }

        workAvailable_.notify_all();
    }

    void ImageMetadataService::CancelOutstanding()
    {
        std::scoped_lock lock(mutex_);
        ++activeSessionId_;
        pendingJobs_.clear();
        queuedKeys_.clear();
    }

    void ImageMetadataService::SetPressureModeEnabled(bool enabled)
    {
        {
            std::scoped_lock lock(mutex_);
            pressureModeEnabled_ = enabled;
            activeWorkerLimit_ = enabled
                ? std::max<std::size_t>(1, workers_.size() / 2)
                : std::max<std::size_t>(1, workers_.size());
        }

        workAvailable_.notify_all();
    }

    void ImageMetadataService::TrimCacheToEntries(std::size_t targetEntries)
    {
        std::scoped_lock lock(mutex_);
        TrimCacheToEntriesLocked(targetEntries);
    }

    std::shared_ptr<const ImageMetadata> ImageMetadataService::FindCachedMetadata(const browser::BrowserItem& item) const
    {
        MetadataCacheKey key{item.filePath, item.modifiedTimestampUtc};
        key.filePath = util::NormalizePathForComparison(key.filePath);

        std::scoped_lock lock(mutex_);
        const auto iterator = cache_.find(key);
        if (iterator == cache_.end())
        {
            return nullptr;
        }

        cacheLruOrder_.splice(cacheLruOrder_.begin(), cacheLruOrder_, iterator->second.lruIterator);
        iterator->second.lruIterator = cacheLruOrder_.begin();
        return iterator->second.metadata;
    }

    void ImageMetadataService::InvalidateFilePaths(const std::vector<std::wstring>& filePaths)
    {
        if (filePaths.empty())
        {
            return;
        }

        const std::unordered_set<std::wstring> normalizedPaths = [&]()
        {
            std::unordered_set<std::wstring> paths;
            paths.reserve(filePaths.size());
            for (const std::wstring& filePath : filePaths)
            {
                paths.insert(util::NormalizePathForComparison(filePath));
            }
            return paths;
        }();

        std::scoped_lock lock(mutex_);
        for (const std::wstring& normalizedPath : normalizedPaths)
        {
            pathGenerations_[normalizedPath] = nextPathGeneration_++;
        }

        for (auto iterator = pendingJobs_.begin(); iterator != pendingJobs_.end();)
        {
            if (!normalizedPaths.contains(iterator->cacheKey.filePath))
            {
                ++iterator;
                continue;
            }

            queuedKeys_.erase(iterator->cacheKey);
            iterator = pendingJobs_.erase(iterator);
        }

        for (auto iterator = cache_.begin(); iterator != cache_.end();)
        {
            if (normalizedPaths.contains(iterator->first.filePath))
            {
                cacheLruOrder_.erase(iterator->second.lruIterator);
                iterator = cache_.erase(iterator);
            }
            else
            {
                ++iterator;
            }
        }
    }

    std::size_t ImageMetadataService::CacheEntryCount() const
    {
        std::scoped_lock lock(mutex_);
        return cache_.size();
    }

    std::size_t ImageMetadataService::CacheCapacityEntries() const noexcept
    {
        return cacheCapacityEntries_;
    }

    std::size_t ImageMetadataService::WorkerCount() const
    {
        return workers_.size();
    }

    bool ImageMetadataService::HasDispatchableWorkLocked() const
    {
        return !pendingJobs_.empty() && activeWorkerCount_ < activeWorkerLimit_;
    }

    void ImageMetadataService::WorkerLoop()
    {
        while (true)
        {
            PendingJob job;
            {
                std::unique_lock lock(mutex_);
                workAvailable_.wait(lock, [this]()
                {
                    return shuttingDown_ || HasDispatchableWorkLocked();
                });

                if (shuttingDown_)
                {
                    return;
                }

                const auto jobIterator = std::min_element(pendingJobs_.begin(), pendingJobs_.end(), [](const PendingJob& lhs, const PendingJob& rhs)
                {
                    if (lhs.workItem.priority != rhs.workItem.priority)
                    {
                        return lhs.workItem.priority < rhs.workItem.priority;
                    }
                    return lhs.sequence < rhs.sequence;
                });

                job = *jobIterator;
                ++activeWorkerCount_;
                queuedKeys_.erase(job.cacheKey);
                inflightKeys_.insert(job.cacheKey);
                pendingJobs_.erase(jobIterator);
            }

            std::wstring errorMessage;
            std::shared_ptr<const ImageMetadata> metadata = extractor_(job.workItem.item, &errorMessage);

            bool shouldNotify = false;
            {
                std::scoped_lock lock(mutex_);
                inflightKeys_.erase(job.cacheKey);

                const auto generation = pathGenerations_.find(job.cacheKey.filePath);
                const std::uint64_t currentPathGeneration = generation == pathGenerations_.end() ? 0 : generation->second;
                const bool isCurrentSession = job.sessionId == activeSessionId_;
                const bool isCurrentPathGeneration = currentPathGeneration == job.pathGeneration;
                if (metadata && isCurrentSession && isCurrentPathGeneration)
                {
                    InsertCacheEntryLocked(job.cacheKey, metadata);
                }

                shouldNotify = metadata != nullptr
                    && targetWindow_ != nullptr
                    && isCurrentSession
                    && isCurrentPathGeneration;

                if (activeWorkerCount_ > 0)
                {
                    --activeWorkerCount_;
                }
            }

            workAvailable_.notify_all();

            if (shouldNotify)
            {
                PostReady(job.sessionId, job.workItem.modelIndex, job.workItem.item, metadata != nullptr);
            }
        }
    }

    void ImageMetadataService::TrimCacheToEntriesLocked(std::size_t targetEntries)
    {
        while (cache_.size() > targetEntries && !cacheLruOrder_.empty())
        {
            const MetadataCacheKey keyToEvict = cacheLruOrder_.back();
            cacheLruOrder_.pop_back();
            cache_.erase(keyToEvict);
        }
    }

    void ImageMetadataService::InsertCacheEntryLocked(MetadataCacheKey key, std::shared_ptr<const ImageMetadata> metadata)
    {
        if (!metadata)
        {
            return;
        }

        const auto existing = cache_.find(key);
        if (existing != cache_.end())
        {
            cacheLruOrder_.erase(existing->second.lruIterator);
            cache_.erase(existing);
        }

        cacheLruOrder_.push_front(key);
        cache_.emplace(cacheLruOrder_.front(), CacheEntry{std::move(metadata), cacheLruOrder_.begin()});

        TrimCacheToEntriesLocked(cacheCapacityEntries_);
    }

    void ImageMetadataService::PostReady(std::uint64_t sessionId, int modelIndex, const browser::BrowserItem& item, bool success) const
    {
        HWND targetWindow = nullptr;
        {
            std::scoped_lock lock(mutex_);
            targetWindow = targetWindow_;
        }

        if (!targetWindow)
        {
            return;
        }

        auto update = std::make_unique<MetadataReadyUpdate>();
        update->sessionId = sessionId;
        update->modelIndex = modelIndex;
        update->item = item;
        update->success = success;
        if (!PostMessageW(targetWindow, kMessageId, 0, reinterpret_cast<LPARAM>(update.get())))
        {
            return;
        }

        update.release();
    }
}