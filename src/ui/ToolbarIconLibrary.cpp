#include "ui/ToolbarIconLibrary.h"

#include <windows.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4244 4456 4702)
#endif

#define NANOSVG_IMPLEMENTATION
#include "nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvgrast.h"

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "util/Log.h"

namespace fs = std::filesystem;

namespace
{
    std::string NarrowUtf8(std::wstring_view wideText)
    {
        if (wideText.empty())
        {
            return {};
        }

        const int requiredSize = WideCharToMultiByte(
            CP_UTF8,
            0,
            wideText.data(),
            static_cast<int>(wideText.size()),
            nullptr,
            0,
            nullptr,
            nullptr);
        if (requiredSize <= 0)
        {
            return {};
        }

        std::string narrowText(static_cast<std::size_t>(requiredSize), '\0');
        WideCharToMultiByte(
            CP_UTF8,
            0,
            wideText.data(),
            static_cast<int>(wideText.size()),
            narrowText.data(),
            requiredSize,
            nullptr,
            nullptr);
        return narrowText;
    }
}

namespace hyperbrowse::ui
{
    bool ToolbarIconLibrary::BitmapKey::operator==(const BitmapKey& other) const
    {
        return pixelSize == other.pixelSize
            && color == other.color
            && iconName == other.iconName;
    }

    std::size_t ToolbarIconLibrary::BitmapKeyHasher::operator()(const BitmapKey& key) const
    {
        const std::size_t nameHash = std::hash<std::string>{}(key.iconName);
        const std::size_t sizeHash = std::hash<int>{}(key.pixelSize);
        const std::size_t colorHash = std::hash<unsigned int>{}(static_cast<unsigned int>(key.color));
        return nameHash ^ (sizeHash << 1) ^ (colorHash << 2);
    }

    ToolbarIconLibrary::ToolbarIconLibrary() = default;

    ToolbarIconLibrary::~ToolbarIconLibrary()
    {
        for (const auto& [_, bitmap] : bitmapCache_)
        {
            if (bitmap)
            {
                DeleteObject(bitmap);
            }
        }

        for (const auto& [_, image] : images_)
        {
            if (image)
            {
                nsvgDelete(image);
            }
        }

        if (rasterizer_)
        {
            nsvgDeleteRasterizer(rasterizer_);
        }
    }

    bool ToolbarIconLibrary::Initialize()
    {
        if (rasterizer_)
        {
            return true;
        }

        rasterizer_ = nsvgCreateRasterizer();
        if (!rasterizer_)
        {
            util::LogError(L"Failed to create NanoSVG rasterizer for toolbar icons");
            return false;
        }

        bool success = true;
        success = LoadIconFile(L"open-folder", L"open-folder.svg") && success;
        success = LoadIconFile(L"recursive", L"recursive.svg") && success;
        success = LoadIconFile(L"view-grid", L"view-grid.svg") && success;
        success = LoadIconFile(L"view-list", L"view-list.svg") && success;
        success = LoadIconFile(L"sort", L"sort.svg") && success;
        success = LoadIconFile(L"thumbnail-size", L"thumbnail-size.svg") && success;
        success = LoadIconFile(L"copy", L"copy.svg") && success;
        success = LoadIconFile(L"move", L"move.svg") && success;
        success = LoadIconFile(L"delete", L"delete.svg") && success;
        success = LoadIconFile(L"search", L"search.svg") && success;
        success = LoadIconFile(L"chevron-down", L"chevron-down.svg") && success;
        return success;
    }

    HBITMAP ToolbarIconLibrary::GetBitmap(std::string_view iconName, int pixelSize, COLORREF color)
    {
        if (pixelSize <= 0)
        {
            return nullptr;
        }

        const BitmapKey key{std::string(iconName), pixelSize, color};
        if (const auto cached = bitmapCache_.find(key); cached != bitmapCache_.end())
        {
            return cached->second;
        }

        const auto imageIt = images_.find(std::string(iconName));
        if (imageIt == images_.end() || !imageIt->second || !rasterizer_)
        {
            return nullptr;
        }

        const float sourceWidth = imageIt->second->width > 0.0f ? imageIt->second->width : 24.0f;
        const float sourceHeight = imageIt->second->height > 0.0f ? imageIt->second->height : 24.0f;
        const float scale = static_cast<float>(pixelSize) / (std::max)(sourceWidth, sourceHeight);
        const int bitmapWidth = pixelSize;
        const int bitmapHeight = pixelSize;

        std::vector<unsigned char> rgbaPixels(static_cast<std::size_t>(bitmapWidth * bitmapHeight * 4), 0);
        nsvgRasterize(
            rasterizer_,
            imageIt->second,
            0.0f,
            0.0f,
            scale,
            rgbaPixels.data(),
            bitmapWidth,
            bitmapHeight,
            bitmapWidth * 4);

        BITMAPINFO bitmapInfo{};
        bitmapInfo.bmiHeader.biSize = sizeof(bitmapInfo.bmiHeader);
        bitmapInfo.bmiHeader.biWidth = bitmapWidth;
        bitmapInfo.bmiHeader.biHeight = -bitmapHeight;
        bitmapInfo.bmiHeader.biPlanes = 1;
        bitmapInfo.bmiHeader.biBitCount = 32;
        bitmapInfo.bmiHeader.biCompression = BI_RGB;

        void* dibBits = nullptr;
        HBITMAP dib = CreateDIBSection(nullptr, &bitmapInfo, DIB_RGB_COLORS, &dibBits, nullptr, 0);
        if (!dib || !dibBits)
        {
            if (dib)
            {
                DeleteObject(dib);
            }
            return nullptr;
        }

        const BYTE red = GetRValue(color);
        const BYTE green = GetGValue(color);
        const BYTE blue = GetBValue(color);
        auto* outputPixels = static_cast<unsigned char*>(dibBits);
        for (int index = 0; index < bitmapWidth * bitmapHeight; ++index)
        {
            const BYTE alpha = rgbaPixels[static_cast<std::size_t>(index * 4 + 3)];
            outputPixels[static_cast<std::size_t>(index * 4 + 0)] = static_cast<BYTE>((blue * alpha + 127) / 255);
            outputPixels[static_cast<std::size_t>(index * 4 + 1)] = static_cast<BYTE>((green * alpha + 127) / 255);
            outputPixels[static_cast<std::size_t>(index * 4 + 2)] = static_cast<BYTE>((red * alpha + 127) / 255);
            outputPixels[static_cast<std::size_t>(index * 4 + 3)] = alpha;
        }

        bitmapCache_.emplace(key, dib);
        return dib;
    }

    bool ToolbarIconLibrary::LoadIconFile(const wchar_t* iconName, const wchar_t* fileName)
    {
        const fs::path iconPath = fs::path(GetIconDirectory()) / fileName;
        std::ifstream fileStream(iconPath, std::ios::binary);
        if (!fileStream)
        {
            util::LogError(L"Failed to open toolbar icon SVG: " + iconPath.wstring());
            return false;
        }

        std::string svgMarkup((std::istreambuf_iterator<char>(fileStream)), std::istreambuf_iterator<char>());
        if (svgMarkup.empty())
        {
            util::LogError(L"Toolbar icon SVG was empty: " + iconPath.wstring());
            return false;
        }

        svgMarkup.push_back('\0');
        NSVGimage* image = nsvgParse(svgMarkup.data(), "px", 96.0f);
        if (!image)
        {
            util::LogError(L"Failed to parse toolbar icon SVG: " + iconPath.wstring());
            return false;
        }

        images_[NarrowUtf8(iconName)] = image;
        return true;
    }

    std::wstring ToolbarIconLibrary::GetIconDirectory() const
    {
        std::wstring modulePath(MAX_PATH, L'\0');
        const DWORD written = GetModuleFileNameW(nullptr, modulePath.data(), static_cast<DWORD>(modulePath.size()));
        modulePath.resize(written);
        return (fs::path(modulePath).parent_path() / L"assets" / L"toolbar-icons").wstring();
    }
}