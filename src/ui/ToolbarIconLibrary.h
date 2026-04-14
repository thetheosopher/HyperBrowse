#pragma once

#include <windows.h>

#include <string>
#include <string_view>
#include <unordered_map>

struct NSVGimage;
struct NSVGrasterizer;

namespace hyperbrowse::ui
{
    class ToolbarIconLibrary
    {
    public:
        ToolbarIconLibrary();
        ~ToolbarIconLibrary();

        ToolbarIconLibrary(const ToolbarIconLibrary&) = delete;
        ToolbarIconLibrary& operator=(const ToolbarIconLibrary&) = delete;

        bool Initialize();
        HBITMAP GetBitmap(std::string_view iconName, int pixelSize, COLORREF color);

    private:
        struct BitmapKey
        {
            std::string iconName;
            int pixelSize{};
            COLORREF color{};

            bool operator==(const BitmapKey& other) const;
        };

        struct BitmapKeyHasher
        {
            std::size_t operator()(const BitmapKey& key) const;
        };

        bool LoadIconAsset(const wchar_t* iconName, int resourceId, const wchar_t* fileName);
        bool ParseAndStoreIcon(std::string iconKey, std::string svgMarkup, std::wstring_view sourceLabel);
        std::wstring GetIconDirectory() const;

        NSVGrasterizer* rasterizer_{};
        std::unordered_map<std::string, NSVGimage*> images_;
        std::unordered_map<BitmapKey, HBITMAP, BitmapKeyHasher> bitmapCache_;
    };
}