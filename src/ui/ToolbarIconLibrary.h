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

        bool LoadIconFile(const wchar_t* iconName, const wchar_t* fileName);
        std::wstring GetIconDirectory() const;

        NSVGrasterizer* rasterizer_{};
        std::unordered_map<std::string, NSVGimage*> images_;
        std::unordered_map<BitmapKey, HBITMAP, BitmapKeyHasher> bitmapCache_;
    };
}