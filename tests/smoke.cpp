#include <windows.h>

#include <commctrl.h>

#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#include <objbase.h>
#include <propvarutil.h>
#include <shlobj.h>
#include <shellapi.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "browser/BrowserModel.h"
#include "browser/BrowserPane.h"
#include "decode/ImageDecoder.h"
#include "decode/WicThumbnailDecoder.h"
#include "services/BatchConvertService.h"
#include "services/FileOperationService.h"
#include "services/FolderEnumerationService.h"
#include "services/FolderTreeEnumerationService.h"
#include "services/FolderWatchService.h"
#include "services/ImageMetadataService.h"
#include "services/JpegTransformService.h"
#include "services/ThumbnailScheduler.h"
#include "ui/MainWindow.h"
#include "viewer/ViewerWindow.h"

namespace fs = std::filesystem;

namespace
{
    using Microsoft::WRL::ComPtr;

    constexpr wchar_t kTestWindowClassName[] = L"HyperBrowseFolderEnumerationTestWindow";
    constexpr wchar_t kRegistryPath[] = L"Software\\HyperBrowse";
    constexpr wchar_t kRegistryValueViewerInfoOverlaysVisible[] = L"ViewerInfoOverlaysVisible";

    struct EnumerationResult
    {
        std::uint64_t totalCount{};
        std::uint64_t totalBytes{};
        std::vector<hyperbrowse::browser::BrowserItem> items;
        std::wstring errorMessage;
        bool completed{};
        bool failed{};
    };

    struct ThumbnailResult
    {
        std::uint64_t expectedSessionId{};
        int readyCount{};
        int failedCount{};
        std::vector<std::wstring> readyPaths;
        std::vector<std::wstring> failedPaths;
    };

    struct FolderTreeEnumerationResult
    {
        std::uint64_t expectedRequestId{};
        std::vector<std::wstring> childFolders;
        std::wstring errorMessage;
        bool completed{};
        bool failed{};
    };

    struct FileOperationResult
    {
        std::uint64_t expectedRequestId{};
        hyperbrowse::services::FileOperationUpdate update;
        bool completed{};
    };

    struct TestWindowState
    {
        std::uint64_t expectedRequestId{};
        EnumerationResult enumerationResult;
        ThumbnailResult thumbnailResult;
        FolderTreeEnumerationResult folderTreeEnumerationResult;
        FileOperationResult fileOperationResult;
    };

    class ComScope
    {
    public:
        ComScope()
        {
            const HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
            if (FAILED(result) && result != RPC_E_CHANGED_MODE)
            {
                throw std::runtime_error("Failed to initialize COM for tests");
            }

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

    class TempFolder
    {
    public:
        explicit TempFolder(std::wstring name)
            : root_(fs::temp_directory_path() / std::move(name))
        {
            std::error_code error;
            fs::remove_all(root_, error);
            fs::create_directories(root_);
        }

        ~TempFolder()
        {
            std::error_code error;
            fs::remove_all(root_, error);
        }

        const fs::path& Root() const noexcept
        {
            return root_;
        }

        void WriteFile(const fs::path& relativePath, std::size_t byteCount)
        {
            const fs::path absolutePath = root_ / relativePath;
            fs::create_directories(absolutePath.parent_path());
            std::ofstream stream(absolutePath, std::ios::binary);
            stream << std::string(byteCount, 'x');
        }

    private:
        fs::path root_;
    };

    class ScopedRegistryDwordBackup
    {
    public:
        ScopedRegistryDwordBackup(const wchar_t* path, const wchar_t* valueName)
            : path_(path)
            , valueName_(valueName)
        {
            HKEY key{};
            if (RegOpenKeyExW(HKEY_CURRENT_USER, path_, 0, KEY_READ, &key) != ERROR_SUCCESS)
            {
                return;
            }

            DWORD size = sizeof(value_);
            DWORD type = REG_DWORD;
            hadValue_ = RegQueryValueExW(key,
                                         valueName_,
                                         nullptr,
                                         &type,
                                         reinterpret_cast<LPBYTE>(&value_),
                                         &size) == ERROR_SUCCESS
                && type == REG_DWORD;
            RegCloseKey(key);
        }

        ~ScopedRegistryDwordBackup()
        {
            HKEY key{};
            DWORD disposition = 0;
            if (RegCreateKeyExW(HKEY_CURRENT_USER,
                                path_,
                                0,
                                nullptr,
                                0,
                                KEY_WRITE,
                                nullptr,
                                &key,
                                &disposition) != ERROR_SUCCESS)
            {
                return;
            }

            if (hadValue_)
            {
                RegSetValueExW(key,
                               valueName_,
                               0,
                               REG_DWORD,
                               reinterpret_cast<const BYTE*>(&value_),
                               sizeof(value_));
            }
            else
            {
                RegDeleteValueW(key, valueName_);
            }

            RegCloseKey(key);
        }

    private:
        const wchar_t* path_{};
        const wchar_t* valueName_{};
        DWORD value_{};
        bool hadValue_{};
    };

    enum class TestImageFormat
    {
        Jpeg,
        Png,
        Gif,
        Tiff,
    };

    void Expect(bool condition, const std::string& message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    void CheckHResult(HRESULT result, const char* message)
    {
        if (FAILED(result))
        {
            throw std::runtime_error(message);
        }
    }

    std::string Utf8FromWide(std::wstring_view text)
    {
        if (text.empty())
        {
            return {};
        }

        const int required = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
        if (required <= 0)
        {
            std::string fallback;
            fallback.reserve(text.size());
            for (wchar_t character : text)
            {
                fallback.push_back(character <= 0x7f ? static_cast<char>(character) : '?');
            }
            return fallback;
        }

        std::string utf8(static_cast<std::size_t>(required), '\0');
        WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), utf8.data(), required, nullptr, nullptr);
        return utf8;
    }

    void ResetEnumerationResult(TestWindowState* state)
    {
        state->enumerationResult = EnumerationResult{};
    }

    void ResetThumbnailResult(TestWindowState* state, std::uint64_t expectedSessionId)
    {
        state->thumbnailResult = ThumbnailResult{};
        state->thumbnailResult.expectedSessionId = expectedSessionId;
    }

    void ResetFolderTreeEnumerationResult(TestWindowState* state)
    {
        state->folderTreeEnumerationResult = FolderTreeEnumerationResult{};
    }

    bool PumpMessagesUntil(const std::function<bool()>& predicate, DWORD timeoutMs)
    {
        const ULONGLONG deadline = GetTickCount64() + timeoutMs;
        MSG msg{};
        while (GetTickCount64() < deadline)
        {
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }

            if (predicate())
            {
                return true;
            }

            MsgWaitForMultipleObjectsEx(0, nullptr, 25, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        }

        return predicate();
    }

    void PumpMessagesFor(DWORD durationMs)
    {
        const ULONGLONG deadline = GetTickCount64() + durationMs;
        MSG msg{};
        while (GetTickCount64() < deadline)
        {
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }

            MsgWaitForMultipleObjectsEx(0, nullptr, 25, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        }
    }

    std::wstring ReadTreeItemText(HWND treeView, HTREEITEM item)
    {
        wchar_t buffer[260]{};
        TVITEMW treeItem{};
        treeItem.mask = TVIF_TEXT;
        treeItem.hItem = item;
        treeItem.pszText = buffer;
        treeItem.cchTextMax = static_cast<int>(std::size(buffer));
        Expect(TreeView_GetItem(treeView, &treeItem) != FALSE, "Failed to read a tree item text");
        return buffer;
    }

    std::wstring QueryShellDisplayName(const std::wstring& folderPath)
    {
        SHFILEINFOW shellInfo{};
        if (SHGetFileInfoW(
            folderPath.c_str(),
            FILE_ATTRIBUTE_DIRECTORY,
            &shellInfo,
            sizeof(shellInfo),
            SHGFI_DISPLAYNAME | SHGFI_SYSICONINDEX | SHGFI_SMALLICON) != 0
            && shellInfo.szDisplayName[0] != L'\0')
        {
            return shellInfo.szDisplayName;
        }

        const fs::path path(folderPath);
        const std::wstring leaf = path.filename().wstring();
        return leaf.empty() ? folderPath : leaf;
    }

    std::wstring TryGetKnownFolderPathForTest(REFKNOWNFOLDERID folderId)
    {
        PWSTR rawPath = nullptr;
        const HRESULT result = SHGetKnownFolderPath(folderId, KF_FLAG_DEFAULT, nullptr, &rawPath);
        if (FAILED(result) || !rawPath)
        {
            return {};
        }

        std::wstring path = rawPath;
        CoTaskMemFree(rawPath);
        return path;
    }

    std::vector<std::wstring> ExpectedSpecialFolderRootTexts()
    {
        std::vector<std::wstring> rootTexts;
        const KNOWNFOLDERID folderIds[] = {
            FOLDERID_Desktop,
            FOLDERID_Documents,
            FOLDERID_Pictures,
        };

        for (const KNOWNFOLDERID& folderId : folderIds)
        {
            const std::wstring folderPath = TryGetKnownFolderPathForTest(folderId);
            if (folderPath.empty())
            {
                continue;
            }

            std::error_code error;
            if (!fs::is_directory(fs::path(folderPath), error) || error)
            {
                continue;
            }

            rootTexts.push_back(QueryShellDisplayName(folderPath));
        }

        return rootTexts;
    }

    LRESULT CALLBACK TestWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        (void)wParam;
        if (message == WM_NCCREATE)
        {
            auto* createStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(createStruct->lpCreateParams));
            return TRUE;
        }

        auto* state = reinterpret_cast<TestWindowState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == hyperbrowse::services::FolderEnumerationService::kMessageId)
        {
            std::unique_ptr<hyperbrowse::services::FolderEnumerationUpdate> update(
                reinterpret_cast<hyperbrowse::services::FolderEnumerationUpdate*>(lParam));

            if (!state || !update || update->requestId != state->expectedRequestId)
            {
                return 0;
            }

            switch (update->kind)
            {
            case hyperbrowse::services::FolderEnumerationUpdateKind::Batch:
                state->enumerationResult.totalCount = update->totalCount;
                state->enumerationResult.totalBytes = update->totalBytes;
                state->enumerationResult.items.insert(state->enumerationResult.items.end(),
                                                      std::make_move_iterator(update->items.begin()),
                                                      std::make_move_iterator(update->items.end()));
                return 0;
            case hyperbrowse::services::FolderEnumerationUpdateKind::Completed:
                state->enumerationResult.totalCount = update->totalCount;
                state->enumerationResult.totalBytes = update->totalBytes;
                state->enumerationResult.completed = true;
                return 0;
            case hyperbrowse::services::FolderEnumerationUpdateKind::Failed:
                state->enumerationResult.errorMessage = update->message;
                state->enumerationResult.failed = true;
                return 0;
            default:
                return 0;
            }
        }

        if (message == hyperbrowse::services::ThumbnailScheduler::kMessageId)
        {
            std::unique_ptr<hyperbrowse::services::ThumbnailReadyUpdate> update(
                reinterpret_cast<hyperbrowse::services::ThumbnailReadyUpdate*>(lParam));
            if (!state || !update || update->sessionId != state->thumbnailResult.expectedSessionId)
            {
                return 0;
            }

            if (update->success)
            {
                ++state->thumbnailResult.readyCount;
                state->thumbnailResult.readyPaths.push_back(update->cacheKey.filePath);
            }
            else
            {
                ++state->thumbnailResult.failedCount;
                state->thumbnailResult.failedPaths.push_back(update->cacheKey.filePath);
            }
            return 0;
        }

        if (message == hyperbrowse::services::FolderTreeEnumerationService::kMessageId)
        {
            std::unique_ptr<hyperbrowse::services::FolderTreeEnumerationUpdate> update(
                reinterpret_cast<hyperbrowse::services::FolderTreeEnumerationUpdate*>(lParam));
            if (!state || !update || update->requestId != state->folderTreeEnumerationResult.expectedRequestId)
            {
                return 0;
            }

            switch (update->kind)
            {
            case hyperbrowse::services::FolderTreeEnumerationUpdateKind::Completed:
                state->folderTreeEnumerationResult.childFolders = std::move(update->childFolders);
                state->folderTreeEnumerationResult.completed = true;
                return 0;
            case hyperbrowse::services::FolderTreeEnumerationUpdateKind::Failed:
                state->folderTreeEnumerationResult.errorMessage = update->message;
                state->folderTreeEnumerationResult.failed = true;
                return 0;
            default:
                return 0;
            }
        }

        if (message == hyperbrowse::services::FileOperationService::kMessageId)
        {
            std::unique_ptr<hyperbrowse::services::FileOperationUpdate> update(
                reinterpret_cast<hyperbrowse::services::FileOperationUpdate*>(lParam));
            if (!state || !update || update->requestId != state->fileOperationResult.expectedRequestId)
            {
                return 0;
            }

            state->fileOperationResult.update = std::move(*update);
            state->fileOperationResult.completed = true;
            return 0;
        }

        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    HWND CreateTestWindow(TestWindowState* state, HINSTANCE instance)
    {
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = &TestWindowProc;
        windowClass.hInstance = instance;
        windowClass.lpszClassName = kTestWindowClassName;
        RegisterClassExW(&windowClass);

        return CreateWindowExW(
            0,
            kTestWindowClassName,
            L"HyperBrowseTests",
            0,
            0,
            0,
            0,
            0,
            HWND_MESSAGE,
            nullptr,
            instance,
            state);
    }

    HWND CreateUiHostWindow(HINSTANCE instance)
    {
        return CreateWindowExW(
            0,
            kTestWindowClassName,
            L"HyperBrowseUiHost",
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            960,
            720,
            nullptr,
            nullptr,
            instance,
            nullptr);
    }

    GUID ContainerFormatGuid(TestImageFormat format)
    {
        switch (format)
        {
        case TestImageFormat::Jpeg:
            return GUID_ContainerFormatJpeg;
        case TestImageFormat::Png:
            return GUID_ContainerFormatPng;
        case TestImageFormat::Gif:
            return GUID_ContainerFormatGif;
        case TestImageFormat::Tiff:
        default:
            return GUID_ContainerFormatTiff;
        }
    }

    std::vector<BYTE> BuildPixelBuffer(UINT width, UINT height)
    {
        std::vector<BYTE> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U, 0);
        for (UINT y = 0; y < height; ++y)
        {
            for (UINT x = 0; x < width; ++x)
            {
                const std::size_t index = (static_cast<std::size_t>(y) * width + x) * 4U;
                pixels[index + 0] = static_cast<BYTE>((x * 37U) % 255U);
                pixels[index + 1] = static_cast<BYTE>((y * 53U) % 255U);
                pixels[index + 2] = static_cast<BYTE>(((x + y) * 29U) % 255U);
                pixels[index + 3] = 255;
            }
        }

        return pixels;
    }

    void InitPropVariantFromAnsiText(std::string_view text, PROPVARIANT* value)
    {
        if (!value)
        {
            return;
        }

        PropVariantInit(value);
        char* buffer = static_cast<char*>(CoTaskMemAlloc(text.size() + 1));
        Expect(buffer != nullptr, "Failed to allocate the PNG text metadata buffer");
        std::memcpy(buffer, text.data(), text.size());
        buffer[text.size()] = '\0';
        value->vt = VT_LPSTR;
        value->pszVal = buffer;
    }

    void WriteTestImage(const fs::path& path,
                        TestImageFormat format,
                        UINT width,
                        UINT height,
                        std::uint16_t orientation = 1,
                        std::wstring_view pngTextKey = {},
                        std::string_view pngTextValue = {})
    {
        fs::create_directories(path.parent_path());

        ComPtr<IWICImagingFactory> factory;
        CheckHResult(
            CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)),
            "Failed to create the WIC imaging factory for test image generation");

        std::vector<BYTE> pixels = BuildPixelBuffer(width, height);
        const UINT stride = width * 4;

        ComPtr<IWICBitmap> bitmap;
        CheckHResult(
            factory->CreateBitmapFromMemory(width,
                                            height,
                                            GUID_WICPixelFormat32bppBGRA,
                                            stride,
                                            static_cast<UINT>(pixels.size()),
                                            pixels.data(),
                                            &bitmap),
            "Failed to create the WIC bitmap backing store for a test image");

        ComPtr<IWICStream> stream;
        CheckHResult(factory->CreateStream(&stream), "Failed to create a WIC stream for a test image");
        CheckHResult(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE), "Failed to open the test image output path");

        ComPtr<IWICBitmapEncoder> encoder;
        CheckHResult(factory->CreateEncoder(ContainerFormatGuid(format), nullptr, &encoder), "Failed to create a WIC encoder");
        CheckHResult(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache), "Failed to initialize the WIC encoder");

        ComPtr<IWICBitmapFrameEncode> frame;
        ComPtr<IPropertyBag2> propertyBag;
        CheckHResult(encoder->CreateNewFrame(&frame, &propertyBag), "Failed to create a WIC frame encoder");
        CheckHResult(frame->Initialize(propertyBag.Get()), "Failed to initialize the WIC frame encoder");
        CheckHResult(frame->SetSize(width, height), "Failed to set the test image size");

        WICPixelFormatGUID pixelFormat = GUID_WICPixelFormat32bppBGRA;
        CheckHResult(frame->SetPixelFormat(&pixelFormat), "Failed to set the test image pixel format");

        if (format == TestImageFormat::Jpeg && orientation != 1)
        {
            ComPtr<IWICMetadataQueryWriter> metadataWriter;
            CheckHResult(frame->GetMetadataQueryWriter(&metadataWriter), "Failed to acquire the JPEG metadata writer");

            PROPVARIANT value;
            PropVariantInit(&value);
            CheckHResult(InitPropVariantFromUInt16(orientation, &value), "Failed to build the JPEG orientation metadata value");
            CheckHResult(metadataWriter->SetMetadataByName(L"/app1/ifd/{ushort=274}", &value), "Failed to write the JPEG orientation metadata");
            PropVariantClear(&value);
        }
        else if (format == TestImageFormat::Png && !pngTextKey.empty() && !pngTextValue.empty())
        {
            ComPtr<IWICMetadataQueryWriter> metadataWriter;
            CheckHResult(frame->GetMetadataQueryWriter(&metadataWriter), "Failed to acquire the PNG metadata writer");

            std::wstring query = L"/tEXt/{str=";
            query.append(pngTextKey);
            query.append(L"}");

            PROPVARIANT value;
            InitPropVariantFromAnsiText(pngTextValue, &value);
            CheckHResult(metadataWriter->SetMetadataByName(query.c_str(), &value), "Failed to write the PNG text metadata");
            PropVariantClear(&value);
        }

        CheckHResult(frame->WriteSource(bitmap.Get(), nullptr), "Failed to write the test image pixels");
        CheckHResult(frame->Commit(), "Failed to commit the test image frame");
        CheckHResult(encoder->Commit(), "Failed to commit the test image encoder");
    }

    std::uint16_t ReadJpegOrientation(const fs::path& path)
    {
        ComPtr<IWICImagingFactory> factory;
        CheckHResult(
            CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)),
            "Failed to create the WIC imaging factory for JPEG metadata inspection");

        ComPtr<IWICBitmapDecoder> decoder;
        CheckHResult(
            factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder),
            "Failed to open the JPEG test image for metadata inspection");

        ComPtr<IWICBitmapFrameDecode> frame;
        CheckHResult(decoder->GetFrame(0, &frame), "Failed to read the JPEG frame for metadata inspection");

        ComPtr<IWICMetadataQueryReader> metadataReader;
        CheckHResult(frame->GetMetadataQueryReader(&metadataReader), "Failed to acquire the JPEG metadata reader");

        PROPVARIANT value;
        PropVariantInit(&value);
        CheckHResult(metadataReader->GetMetadataByName(L"/app1/ifd/{ushort=274}", &value), "Failed to read the JPEG orientation metadata");

        std::uint16_t orientation = 1;
        switch (value.vt)
        {
        case VT_UI1:
            orientation = value.bVal;
            break;
        case VT_UI2:
            orientation = value.uiVal;
            break;
        case VT_UI4:
            orientation = static_cast<std::uint16_t>(value.ulVal);
            break;
        default:
            break;
        }

        PropVariantClear(&value);
        return orientation;
    }

    hyperbrowse::cache::ThumbnailCacheKey MakeCacheKey(const fs::path& path,
                                                       std::uint64_t modifiedTimestampUtc,
                                                       int targetWidth = 160,
                                                       int targetHeight = 112)
    {
        hyperbrowse::cache::ThumbnailCacheKey key;
        key.filePath = path.wstring();
        key.modifiedTimestampUtc = modifiedTimestampUtc;
        key.targetWidth = targetWidth;
        key.targetHeight = targetHeight;
        return key;
    }

    hyperbrowse::browser::BrowserItem MakeMetadataItem(std::wstring fileName,
                                                       std::wstring filePath,
                                                       std::uint64_t modifiedTimestampUtc)
    {
        hyperbrowse::browser::BrowserItem item;
        item.fileName = std::move(fileName);
        item.filePath = std::move(filePath);
        item.fileType = L"JPG";
        item.modifiedTimestampUtc = modifiedTimestampUtc;
        return item;
    }

    std::shared_ptr<const hyperbrowse::services::ImageMetadata> MakeMetadata(std::wstring cameraModel)
    {
        auto metadata = std::make_shared<hyperbrowse::services::ImageMetadata>();
        metadata->cameraModel = std::move(cameraModel);
        metadata->hasExif = !metadata->cameraModel.empty();
        return metadata;
    }

    fs::path TestSourceDirectory()
    {
#ifdef HYPERBROWSE_TESTS_SOURCE_DIR
        return fs::path(HYPERBROWSE_TESTS_SOURCE_DIR);
#else
        return fs::current_path();
#endif
    }

    void RunEnumerationScenario(HWND hwnd, TestWindowState* state)
    {
        hyperbrowse::services::FolderEnumerationService service;

        TempFolder root(L"HyperBrowsePrompt3Root");
        root.WriteFile(L"one.jpg", 10);
        root.WriteFile(L"two.png", 20);
        root.WriteFile(L"six.cr2", 60);
        root.WriteFile(L"seven.cr3", 70);
        root.WriteFile(L"eight.arw", 80);
        root.WriteFile(L"nine.dng", 90);
        root.WriteFile(L"ten.raf", 100);
        root.WriteFile(L"eleven.rw2", 110);
        root.WriteFile(L"five.NRW", 50);
        root.WriteFile(L"ignore.txt", 5);
        root.WriteFile(L"nested\\three.gif", 30);
        root.WriteFile(L"nested\\four.nef", 40);

        ResetEnumerationResult(state);
        state->expectedRequestId = service.EnumerateFolderAsync(hwnd, root.Root().wstring(), false);
        Expect(PumpMessagesUntil([&]() { return state->enumerationResult.completed || state->enumerationResult.failed; }, 5000),
               "Non-recursive enumeration timed out or failed");
         Expect(state->enumerationResult.totalCount == 9, "Non-recursive enumeration returned the wrong supported-file count");
         Expect(state->enumerationResult.totalBytes == 590, "Non-recursive enumeration returned the wrong byte total");
         Expect(state->enumerationResult.items.size() == 9, "Non-recursive enumeration returned the wrong batch item count");
        Expect(state->enumerationResult.items.front().placeholderWidth == 256, "Placeholder width was not collected");
        Expect(state->enumerationResult.items.front().placeholderHeight == 256, "Placeholder height was not collected");

        ResetEnumerationResult(state);
        state->expectedRequestId = service.EnumerateFolderAsync(hwnd, root.Root().wstring(), true);
        Expect(PumpMessagesUntil([&]() { return state->enumerationResult.completed || state->enumerationResult.failed; }, 5000),
               "Recursive enumeration timed out or failed");
         Expect(state->enumerationResult.totalCount == 11, "Recursive enumeration returned the wrong supported-file count");
         Expect(state->enumerationResult.totalBytes == 660, "Recursive enumeration returned the wrong byte total");

        TempFolder slow(L"HyperBrowsePrompt3Slow");
        for (int index = 0; index < 400; ++index)
        {
            slow.WriteFile(L"bulk\\image_" + std::to_wstring(index) + L".jpg", 1);
        }

        TempFolder quick(L"HyperBrowsePrompt3Quick");
        quick.WriteFile(L"picked.png", 7);

        ResetEnumerationResult(state);
        service.EnumerateFolderAsync(hwnd, slow.Root().wstring(), false);
        state->expectedRequestId = service.EnumerateFolderAsync(hwnd, quick.Root().wstring(), false);
        Expect(PumpMessagesUntil([&]() { return state->enumerationResult.completed || state->enumerationResult.failed; }, 5000),
               "Cancellation scenario timed out or failed");
        Expect(state->enumerationResult.totalCount == 1, "Cancellation scenario did not surface the latest folder request");
        Expect(state->enumerationResult.items.size() == 1, "Cancellation scenario returned stale items from the superseded request");
        Expect(state->enumerationResult.items.front().fileName == L"picked.png", "Cancellation scenario returned the wrong final file");
        Expect(state->enumerationResult.items.front().fileType == L"PNG", "Enumeration did not capture the file type field");
        Expect(state->enumerationResult.items.front().modifiedTimestampUtc != 0, "Enumeration did not capture the modified timestamp field");
    }

        void RunFolderTreeEnumerationScenario(HWND hwnd, TestWindowState* state)
        {
         hyperbrowse::services::FolderTreeEnumerationService service;

         TempFolder root(L"HyperBrowseFolderTreeEnumeration");
         fs::create_directories(root.Root() / L"gamma");
         fs::create_directories(root.Root() / L"alpha");
         fs::create_directories(root.Root() / L"beta");
         root.WriteFile(L"alpha\\image.jpg", 1);

         ResetFolderTreeEnumerationResult(state);
         state->folderTreeEnumerationResult.expectedRequestId = service.EnumerateChildDirectoriesAsync(hwnd, root.Root().wstring());
         Expect(PumpMessagesUntil([&]()
         {
             return state->folderTreeEnumerationResult.completed || state->folderTreeEnumerationResult.failed;
         }, 5000), "Folder-tree enumeration timed out or failed");
         Expect(!state->folderTreeEnumerationResult.failed,
             std::string("Folder-tree enumeration failed: ") + Utf8FromWide(state->folderTreeEnumerationResult.errorMessage));
         Expect(state->folderTreeEnumerationResult.childFolders.size() == 3,
             "Folder-tree enumeration returned the wrong number of child directories");
         Expect(fs::path(state->folderTreeEnumerationResult.childFolders[0]).filename().wstring() == L"alpha",
             "Folder-tree enumeration did not sort child folders alphabetically");
         Expect(fs::path(state->folderTreeEnumerationResult.childFolders[1]).filename().wstring() == L"beta",
             "Folder-tree enumeration did not preserve the expected alphabetical order");
         Expect(fs::path(state->folderTreeEnumerationResult.childFolders[2]).filename().wstring() == L"gamma",
             "Folder-tree enumeration omitted the last child folder");
        }

    void RunFolderWatchStartStopScenario(HWND hwnd)
    {
        TempFolder root(L"HyperBrowseFolderWatchStop");
        root.WriteFile(L"seed.jpg", 10);

        hyperbrowse::services::FolderWatchService service;
        for (int iteration = 0; iteration < 32; ++iteration)
        {
            service.StartWatching(hwnd, root.Root().wstring(), false);
            root.WriteFile(L"image_" + std::to_wstring(iteration) + L".jpg", static_cast<std::size_t>(iteration + 1));
            PumpMessagesFor(10);
            service.Stop();
        }
    }

    void RunThumbnailCacheNormalizationScenario()
    {
        TempFolder root(L"HyperBrowseThumbnailCacheNormalization");
        const fs::path imagePath = root.Root() / L"MixedCase.png";
        WriteTestImage(imagePath, TestImageFormat::Png, 96, 48);

        hyperbrowse::decode::WicThumbnailDecoder decoder;
        const auto insertedKey = MakeCacheKey(imagePath, 17);
        const auto thumbnail = decoder.Decode(insertedKey);
        Expect(thumbnail != nullptr, "Failed to create the thumbnail used for cache normalization testing");

        hyperbrowse::cache::ThumbnailCache cache(4ULL * 1024ULL * 1024ULL);
        cache.Insert(insertedKey, thumbnail);

        auto lookupKey = insertedKey;
        lookupKey.filePath = imagePath.wstring();
        std::replace(lookupKey.filePath.begin(), lookupKey.filePath.end(), L'\\', L'/');
        std::transform(lookupKey.filePath.begin(), lookupKey.filePath.end(), lookupKey.filePath.begin(), [](wchar_t character)
        {
            return static_cast<wchar_t>(towupper(character));
        });

        Expect(cache.Find(lookupKey) != nullptr, "Thumbnail cache lookup did not normalize slash and case differences");

        cache.InvalidateFilePaths({lookupKey.filePath});
        Expect(cache.Find(insertedKey) == nullptr, "Thumbnail cache invalidation did not normalize the supplied file path");
    }

    void RunWicDecoderScenario()
    {
        TempFolder root(L"HyperBrowsePrompt5Decoder");
        const fs::path jpegPath = root.Root() / L"rotated.jpg";
        const fs::path pngPath = root.Root() / L"sample.png";
        const fs::path gifPath = root.Root() / L"sample.gif";
        const fs::path tiffPath = root.Root() / L"sample.tif";

        WriteTestImage(jpegPath, TestImageFormat::Jpeg, 24, 48, 6);
        WriteTestImage(pngPath, TestImageFormat::Png, 96, 48);
        WriteTestImage(gifPath, TestImageFormat::Gif, 36, 18);
        WriteTestImage(tiffPath, TestImageFormat::Tiff, 18, 54);

        hyperbrowse::decode::WicThumbnailDecoder decoder;

        const auto jpegThumbnail = decoder.Decode(MakeCacheKey(jpegPath, 1));
        Expect(jpegThumbnail != nullptr, "WIC failed to decode the JPEG thumbnail");
         Expect(jpegThumbnail->Width() > jpegThumbnail->Height(), "WIC did not apply JPEG EXIF orientation");
         Expect(jpegThumbnail->SourceWidth() == 48 && jpegThumbnail->SourceHeight() == 24,
             "WIC did not surface the oriented JPEG source dimensions");

        const auto pngThumbnail = decoder.Decode(MakeCacheKey(pngPath, 2));
        Expect(pngThumbnail != nullptr, "WIC failed to decode the PNG thumbnail");
         Expect(pngThumbnail->Width() <= 160 && pngThumbnail->Height() <= 112, "PNG thumbnail scaling exceeded the target bounds");
         Expect(pngThumbnail->SourceWidth() == 96 && pngThumbnail->SourceHeight() == 48,
             "WIC did not surface the PNG source dimensions");

        const auto gifThumbnail = decoder.Decode(MakeCacheKey(gifPath, 3));
         Expect(gifThumbnail != nullptr, "WIC failed to decode the GIF first frame thumbnail");
         Expect(gifThumbnail->SourceWidth() == 36 && gifThumbnail->SourceHeight() == 18,
             "WIC did not surface the GIF source dimensions");

        const auto tiffThumbnail = decoder.Decode(MakeCacheKey(tiffPath, 4));
         Expect(tiffThumbnail != nullptr, "WIC failed to decode the TIFF first page thumbnail");
         Expect(tiffThumbnail->SourceWidth() == 18 && tiffThumbnail->SourceHeight() == 54,
             "WIC did not surface the TIFF source dimensions");
    }

    void RunJpegOrientationAdjustmentScenario()
    {
        TempFolder root(L"HyperBrowseJpegOrientation");

        const auto runCase = [&](const wchar_t* fileName, int quarterTurnsDelta, std::uint16_t expectedOrientation)
        {
            const fs::path jpegPath = root.Root() / fileName;
            WriteTestImage(jpegPath, TestImageFormat::Jpeg, 24, 48, 6);

            std::wstring errorMessage;
            Expect(hyperbrowse::services::AdjustJpegOrientation(jpegPath.wstring(), quarterTurnsDelta, &errorMessage),
                   std::string("JPEG orientation adjustment failed: ") + Utf8FromWide(errorMessage));
            Expect(ReadJpegOrientation(jpegPath) == expectedOrientation,
                   "JPEG orientation adjustment wrote the wrong EXIF orientation value");
        };

        runCase(L"minus-one.jpg", -1, 1);
        runCase(L"plus-one.jpg", +1, 3);
        runCase(L"minus-three.jpg", -3, 3);
        runCase(L"plus-three.jpg", +3, 1);
    }

    void RunBatchConvertCancellationScenario(HWND hwnd)
    {
        const fs::path fixtureRoot = TestSourceDirectory() / L"fixtures" / L"raw";
        const fs::path nefPath = fixtureRoot / L"RAW_NIKON_D1.NEF";
        const fs::path nrwPath = fixtureRoot / L"RAW_NIKON_P7000.NRW";

        Expect(fs::exists(nefPath), "The NEF fixture is missing from tests/fixtures/raw");
        Expect(fs::exists(nrwPath), "The NRW fixture is missing from tests/fixtures/raw");

        std::vector<hyperbrowse::browser::BrowserItem> items;
        items.push_back(hyperbrowse::browser::BuildBrowserItemFromPath(nefPath));
        items.push_back(hyperbrowse::browser::BuildBrowserItemFromPath(nrwPath));
        items.push_back(hyperbrowse::browser::BuildBrowserItemFromPath(nefPath));

        TempFolder output(L"HyperBrowseBatchCancel");
        hyperbrowse::services::BatchConvertService service;
        service.Start(hwnd, std::move(items), output.Root().wstring(), hyperbrowse::services::BatchConvertFormat::Png);

        PumpMessagesFor(100);
        const ULONGLONG start = GetTickCount64();
        service.Cancel();
        const ULONGLONG elapsed = GetTickCount64() - start;
        Expect(elapsed < 250, "Batch convert cancellation blocked instead of returning promptly");
    }

    void RunFileRenameOperationScenario(HWND hwnd, TestWindowState* state)
    {
        TempFolder root(L"HyperBrowseFileRenameOperation");
        const fs::path sourcePath = root.Root() / L"before.jpg";
        const fs::path renamedPath = root.Root() / L"after.jpg";
        root.WriteFile(L"before.jpg", 16);

        hyperbrowse::services::FileOperationService service;
        state->fileOperationResult = {};
        state->fileOperationResult.expectedRequestId = service.Start(
            hwnd,
            nullptr,
            hyperbrowse::services::FileOperationType::Rename,
            {sourcePath.wstring()},
            {},
            hyperbrowse::services::FileConflictPolicy::PromptShell,
            {L"after.jpg"});

        Expect(PumpMessagesUntil([&]() { return state->fileOperationResult.completed; }, 5000),
               "File rename operation timed out");
        Expect(state->fileOperationResult.update.failedCount == 0,
               "File rename operation reported a failure");
        Expect(!fs::exists(sourcePath), "The original file still exists after rename");
        Expect(fs::exists(renamedPath), "The renamed file was not created");
        Expect(state->fileOperationResult.update.createdPaths.size() == 1
                   && state->fileOperationResult.update.createdPaths.front() == renamedPath.wstring(),
               "File rename operation did not report the created path");
    }

        void RunFileConflictPlanningScenario()
        {
         TempFolder root(L"HyperBrowseFileConflictPlanning");
         root.WriteFile(L"source-a\\alpha.jpg", 16);
         root.WriteFile(L"source-b\\alpha.jpg", 16);
         root.WriteFile(L"source-c\\beta.png", 16);
         root.WriteFile(L"dest\\alpha.jpg", 16);
         root.WriteFile(L"dest\\beta.png", 16);
         root.WriteFile(L"dest\\beta.1.png", 16);

         const fs::path destinationFolder = root.Root() / L"dest";
         const std::vector<std::wstring> sourcePaths{
             (root.Root() / L"source-a" / L"alpha.jpg").wstring(),
             (root.Root() / L"source-b" / L"alpha.jpg").wstring(),
             (root.Root() / L"source-c" / L"beta.png").wstring(),
         };

         const hyperbrowse::services::FileConflictPlan overwritePlan = hyperbrowse::services::PlanDestinationConflicts(
             sourcePaths,
             destinationFolder.wstring(),
             hyperbrowse::services::FileConflictPolicy::OverwriteExisting);
         Expect(overwritePlan.conflictCount == 3,
             "Overwrite planning did not detect all copy/move destination conflicts");

         const hyperbrowse::services::FileConflictPlan renamePlan = hyperbrowse::services::PlanDestinationConflicts(
             sourcePaths,
             destinationFolder.wstring(),
             hyperbrowse::services::FileConflictPolicy::AutoRenameNumericSuffix);
         Expect(renamePlan.conflictCount == 3,
             "Auto-rename planning did not detect all destination conflicts");
         Expect(renamePlan.renamedCount == 3,
             "Auto-rename planning did not record the expected rename count");
         Expect(renamePlan.targetLeafNames.size() == sourcePaths.size(),
             "Auto-rename planning did not preserve target-name alignment");
         Expect(renamePlan.targetLeafNames[0] == L"alpha.1.jpg",
             "Auto-rename planning chose the wrong suffix for the first alpha conflict");
         Expect(renamePlan.targetLeafNames[1] == L"alpha.2.jpg",
             "Auto-rename planning chose the wrong suffix for the second alpha conflict");
         Expect(renamePlan.targetLeafNames[2] == L"beta.2.png",
             "Auto-rename planning did not skip the pre-existing beta.1 target");
        }

    void RunThumbnailSchedulerScenario(HWND hwnd, TestWindowState* state)
    {
        TempFolder root(L"HyperBrowsePrompt5Scheduler");
        const fs::path offscreenPngPath = root.Root() / L"offscreen.png";
        const fs::path visibleJpegPath = root.Root() / L"visible.jpg";
        const fs::path visibleGifPath = root.Root() / L"visible.gif";
        const fs::path offscreenTiffPath = root.Root() / L"offscreen.tif";

        WriteTestImage(offscreenPngPath, TestImageFormat::Png, 96, 48);
        WriteTestImage(visibleJpegPath, TestImageFormat::Jpeg, 24, 48, 6);
        WriteTestImage(visibleGifPath, TestImageFormat::Gif, 36, 18);
        WriteTestImage(offscreenTiffPath, TestImageFormat::Tiff, 18, 54);

        const auto offscreenPngKey = MakeCacheKey(offscreenPngPath, 10);
        const auto visibleJpegKey = MakeCacheKey(visibleJpegPath, 11);
        const auto visibleGifKey = MakeCacheKey(visibleGifPath, 12);
        const auto offscreenTiffKey = MakeCacheKey(offscreenTiffPath, 13);

        hyperbrowse::services::ThumbnailScheduler scheduler(8ULL * 1024ULL * 1024ULL, 1);
        scheduler.BindTargetWindow(hwnd);

        ResetThumbnailResult(state, 7);
        const std::vector<hyperbrowse::services::ThumbnailWorkItem> requests{
            {0, offscreenPngKey, 1},
            {1, visibleJpegKey, 0},
            {2, visibleGifKey, 0},
            {3, offscreenTiffKey, 1},
        };
        scheduler.Schedule(7, 1, requests);

        Expect(PumpMessagesUntil([&]() { return state->thumbnailResult.readyCount >= 4; }, 5000),
               "Thumbnail scheduler decode work timed out");
        Expect(!state->thumbnailResult.readyPaths.empty(), "Thumbnail scheduler did not post any ready messages");
        const std::wstring& firstReadyPath = state->thumbnailResult.readyPaths.front();
        Expect(firstReadyPath == visibleJpegPath.wstring() || firstReadyPath == visibleGifPath.wstring(),
               "Thumbnail scheduler did not prioritize visible work ahead of offscreen work");
        Expect(scheduler.FindCachedThumbnail(offscreenPngKey) != nullptr, "Offscreen PNG thumbnail was not cached");
        Expect(scheduler.FindCachedThumbnail(visibleJpegKey) != nullptr, "Visible JPEG thumbnail was not cached");
        Expect(scheduler.FindCachedThumbnail(visibleGifKey) != nullptr, "Visible GIF thumbnail was not cached");
        Expect(scheduler.FindCachedThumbnail(offscreenTiffKey) != nullptr, "Offscreen TIFF thumbnail was not cached");

        const auto visibleJpegThumbnail = scheduler.FindCachedThumbnail(visibleJpegKey);
        Expect(visibleJpegThumbnail->SourceWidth() == 48 && visibleJpegThumbnail->SourceHeight() == 24,
               "Thumbnail scheduler did not retain the JPEG source dimensions in cache");
        const auto offscreenPngThumbnail = scheduler.FindCachedThumbnail(offscreenPngKey);
        Expect(offscreenPngThumbnail->SourceWidth() == 96 && offscreenPngThumbnail->SourceHeight() == 48,
               "Thumbnail scheduler did not retain the PNG source dimensions in cache");

        ResetThumbnailResult(state, 7);
        scheduler.Schedule(7, 2, requests);
        PumpMessagesFor(300);
        Expect(state->thumbnailResult.readyCount == 0, "Cached thumbnails should not be re-decoded on the next schedule pass");
    }

        void RunThumbnailSchedulerFailureScenario(HWND hwnd, TestWindowState* state)
        {
         TempFolder root(L"HyperBrowsePrompt5SchedulerFailure");
         const fs::path missingRawPath = root.Root() / L"missing.nef";
         const auto missingRawKey = MakeCacheKey(missingRawPath, 1);

         hyperbrowse::services::ThumbnailScheduler scheduler(8ULL * 1024ULL * 1024ULL, 1);
         scheduler.BindTargetWindow(hwnd);

         ResetThumbnailResult(state, 8);
         scheduler.Schedule(8, 1, {{0, missingRawKey, 0, true}});
         Expect(PumpMessagesUntil([&]() { return state->thumbnailResult.failedCount >= 1; }, 5000),
             "Thumbnail scheduler did not surface a failed decode update");
         Expect(state->thumbnailResult.failedPaths.size() == 1
                 && state->thumbnailResult.failedPaths.front() == missingRawPath.wstring(),
             "Thumbnail scheduler reported the wrong failed path");
         Expect(scheduler.FindCachedThumbnail(missingRawKey) == nullptr,
             "Failed thumbnail decodes should not populate the thumbnail cache");
         Expect(scheduler.HasKnownFailure(missingRawKey),
             "Thumbnail scheduler did not retain the failed-thumbnail state");
         Expect(scheduler.KnownFailureKind(missingRawKey) == hyperbrowse::decode::ThumbnailDecodeFailureKind::DecodeFailed,
             "Thumbnail scheduler misclassified a generic decode failure");

         ResetThumbnailResult(state, 8);
         scheduler.Schedule(8, 2, {{0, missingRawKey, 0, true}});
         PumpMessagesFor(300);
         Expect(state->thumbnailResult.readyCount == 0 && state->thumbnailResult.failedCount == 0,
             "Known failed thumbnails should not be requeued until the file path is invalidated");

         scheduler.InvalidateFilePaths({missingRawPath.wstring()});
         Expect(!scheduler.HasKnownFailure(missingRawKey),
             "Invalidating a file path should clear the scheduler's known-failure state");
        }

    void RunThumbnailFailureClassificationScenario()
    {
        Expect(hyperbrowse::decode::ClassifyThumbnailDecodeFailure(L"The RAW helper timed out and was terminated.")
                   == hyperbrowse::decode::ThumbnailDecodeFailureKind::TimedOut,
               "Timeout classification did not detect a helper timeout");
        Expect(hyperbrowse::decode::ClassifyThumbnailDecodeFailure(L"Failed to process the RAW thumbnail fallback.")
                   == hyperbrowse::decode::ThumbnailDecodeFailureKind::DecodeFailed,
               "Decode-failure classification misidentified a generic decode failure");
    }

        void RunThumbnailSchedulerWorkerAllocationScenario()
        {
         hyperbrowse::services::ThumbnailScheduler minimumScheduler(8ULL * 1024ULL * 1024ULL, 1);
         Expect(minimumScheduler.WorkerCount() == 2,
             "Thumbnail scheduler should preserve two lanes when configured below the minimum worker count");
         Expect(minimumScheduler.GeneralWorkerCount() == 1,
             "Thumbnail scheduler should preserve a general worker when configured below the minimum worker count");
         Expect(minimumScheduler.RawWorkerCount() == 1,
             "Thumbnail scheduler should preserve a RAW worker when configured below the minimum worker count");

         hyperbrowse::services::ThumbnailScheduler scaledScheduler(8ULL * 1024ULL * 1024ULL, 8);
         Expect(scaledScheduler.WorkerCount() == 8,
             "Thumbnail scheduler did not preserve the requested worker count for a larger pool");
         Expect(scaledScheduler.GeneralWorkerCount() == 6,
             "Thumbnail scheduler did not allocate the expected number of general workers");
         Expect(scaledScheduler.RawWorkerCount() == 2,
             "Thumbnail scheduler did not scale the RAW worker allocation above one lane");
        }

    void RunImageMetadataServiceScenario()
    {
        const auto itemA = MakeMetadataItem(L"alpha.jpg", L"C:\\Metadata\\alpha.jpg", 11);
        const auto itemB = MakeMetadataItem(L"beta.jpg", L"C:\\Metadata\\beta.jpg", 12);
        const auto itemC = MakeMetadataItem(L"gamma.jpg", L"C:\\Metadata\\gamma.jpg", 13);

        {
            hyperbrowse::services::ImageMetadataService service(
                1,
                2,
                [](const hyperbrowse::browser::BrowserItem& item, std::wstring*)
                {
                    return MakeMetadata(item.fileName);
                });

            Expect(service.CacheCapacityEntries() == 2, "Metadata cache did not preserve the configured entry capacity");

            service.Schedule(1, {0, itemA, 0});
            service.Schedule(1, {1, itemB, 0});
            Expect(PumpMessagesUntil([&]()
            {
                return service.FindCachedMetadata(itemA) != nullptr && service.FindCachedMetadata(itemB) != nullptr;
            }, 5000), "Metadata service did not populate the initial cache entries");
            Expect(service.CacheEntryCount() == 2, "Metadata cache did not report the expected initial entry count");

            const auto cachedA = service.FindCachedMetadata(itemA);
            Expect(cachedA != nullptr && cachedA->cameraModel == L"alpha.jpg",
                   "Metadata service returned the wrong cached payload for the first item");

            service.Schedule(1, {2, itemC, 0});
            Expect(PumpMessagesUntil([&]()
            {
                return service.FindCachedMetadata(itemC) != nullptr;
            }, 5000), "Metadata service did not populate the replacement cache entry");
            Expect(service.CacheEntryCount() == 2, "Metadata cache grew past the configured entry capacity");
            Expect(service.FindCachedMetadata(itemA) != nullptr,
                   "Metadata cache evicted the most recently used entry instead of keeping it resident");
            Expect(service.FindCachedMetadata(itemB) == nullptr,
                   "Metadata cache did not evict the least recently used entry after reaching capacity");
        }

        {
            struct BlockingState
            {
                std::mutex mutex;
                std::condition_variable started;
                std::condition_variable released;
                bool extractionStarted{};
                bool allowCompletion{};
            } cancellationState;

            const auto cancelledItem = MakeMetadataItem(L"cancelled.jpg", L"C:\\Metadata\\cancelled.jpg", 21);
            const auto currentItem = MakeMetadataItem(L"current.jpg", L"C:\\Metadata\\current.jpg", 22);

            hyperbrowse::services::ImageMetadataService service(
                1,
                4,
                [&](const hyperbrowse::browser::BrowserItem& item, std::wstring*)
                {
                    if (item.modifiedTimestampUtc == cancelledItem.modifiedTimestampUtc)
                    {
                        std::unique_lock lock(cancellationState.mutex);
                        cancellationState.extractionStarted = true;
                        cancellationState.started.notify_all();
                        cancellationState.released.wait(lock, [&]()
                        {
                            return cancellationState.allowCompletion;
                        });
                    }

                    return MakeMetadata(item.fileName);
                });

            service.Schedule(41, {0, cancelledItem, 0});
            Expect(PumpMessagesUntil([&]()
            {
                std::scoped_lock lock(cancellationState.mutex);
                return cancellationState.extractionStarted;
            }, 1000), "Metadata cancellation scenario never started the blocked extraction");

            service.CancelOutstanding();
            service.Schedule(42, {1, currentItem, 0});

            {
                std::scoped_lock lock(cancellationState.mutex);
                cancellationState.allowCompletion = true;
            }
            cancellationState.released.notify_all();

            Expect(PumpMessagesUntil([&]()
            {
                return service.FindCachedMetadata(currentItem) != nullptr;
            }, 5000), "Metadata cancellation scenario did not cache the latest session item");
            PumpMessagesFor(100);
            Expect(service.FindCachedMetadata(cancelledItem) == nullptr,
                   "Metadata cancellation allowed an in-flight stale result to repopulate the cache");
        }

        {
            struct BlockingState
            {
                std::mutex mutex;
                std::condition_variable started;
                std::condition_variable released;
                bool extractionStarted{};
                bool allowCompletion{};
            } invalidationState;

            const auto staleItem = MakeMetadataItem(L"watched.jpg", L"C:\\Metadata\\watched.jpg", 31);
            const auto refreshedItem = MakeMetadataItem(L"watched.jpg", L"C:\\Metadata\\watched.jpg", 32);

            hyperbrowse::services::ImageMetadataService service(
                1,
                4,
                [&](const hyperbrowse::browser::BrowserItem& item, std::wstring*)
                {
                    if (item.modifiedTimestampUtc == staleItem.modifiedTimestampUtc)
                    {
                        std::unique_lock lock(invalidationState.mutex);
                        invalidationState.extractionStarted = true;
                        invalidationState.started.notify_all();
                        invalidationState.released.wait(lock, [&]()
                        {
                            return invalidationState.allowCompletion;
                        });
                    }

                    return MakeMetadata(item.fileName + L"-" + std::to_wstring(item.modifiedTimestampUtc));
                });

            service.Schedule(55, {0, staleItem, 0});
            Expect(PumpMessagesUntil([&]()
            {
                std::scoped_lock lock(invalidationState.mutex);
                return invalidationState.extractionStarted;
            }, 1000), "Metadata invalidation scenario never started the blocked extraction");

            service.InvalidateFilePaths({staleItem.filePath});
            service.Schedule(55, {1, refreshedItem, 0});

            {
                std::scoped_lock lock(invalidationState.mutex);
                invalidationState.allowCompletion = true;
            }
            invalidationState.released.notify_all();

            Expect(PumpMessagesUntil([&]()
            {
                return service.FindCachedMetadata(refreshedItem) != nullptr;
            }, 5000), "Metadata invalidation scenario did not cache the refreshed file metadata");
            PumpMessagesFor(100);

            const auto refreshedMetadata = service.FindCachedMetadata(refreshedItem);
            Expect(refreshedMetadata != nullptr && refreshedMetadata->cameraModel == L"watched.jpg-32",
                   "Metadata invalidation scenario cached the wrong refreshed payload");
            Expect(service.FindCachedMetadata(staleItem) == nullptr,
                   "Metadata invalidation allowed an in-flight stale result to repopulate the cache");
        }
    }

    void RunSwarmUiMetadataExtractionScenario()
    {
        TempFolder root(L"HyperBrowsePrompt9SwarmMetadata");
        const fs::path pngPath = root.Root() / L"swarm-ui.png";
        const std::string parameters =
            "{ sui_image_params : { prompt : cinematic portrait, volumetric lighting, sharp focus, negativeprompt : low quality, deformed hands, text, model : swarm-model-xl, seed : 12345, steps : 30, cfgscale : 7, swarm_version : 0.9.1.1, date : 2026-05-16, generation_time : 1.23 seconds } }";
        WriteTestImage(pngPath,
                       TestImageFormat::Png,
                       96,
                       48,
                       1,
                       L"parameters",
                       parameters);

        hyperbrowse::browser::BrowserItem item;
        item.fileName = pngPath.filename().wstring();
        item.filePath = pngPath.wstring();
        item.fileType = L"PNG";
        item.modifiedTimestampUtc = 77;

        std::wstring errorMessage;
        const auto metadata = hyperbrowse::services::ExtractImageMetadata(item, &errorMessage);
        Expect(metadata != nullptr, "SwarmUI PNG metadata extraction returned a null result");

        const auto findProperty = [&](std::wstring_view canonicalName)
            -> const hyperbrowse::services::MetadataPropertyEntry*
        {
            const auto match = std::find_if(metadata->properties.begin(),
                                            metadata->properties.end(),
                                            [&](const hyperbrowse::services::MetadataPropertyEntry& property)
                                            {
                                                return property.canonicalName == canonicalName;
                                            });
            return match != metadata->properties.end() ? &(*match) : nullptr;
        };

        const auto* prompt = findProperty(L"SwarmUI.prompt");
        Expect(prompt != nullptr
                   && prompt->value == L"cinematic portrait, volumetric lighting, sharp focus",
               "SwarmUI prompt metadata was not extracted from the PNG parameters chunk");

        const auto* negativePrompt = findProperty(L"SwarmUI.negativeprompt");
        Expect(negativePrompt != nullptr
                   && negativePrompt->value == L"low quality, deformed hands, text",
               "SwarmUI negative prompt metadata was not extracted from the PNG parameters chunk");

        const auto* swarmVersion = findProperty(L"SwarmUI.swarm_version");
        Expect(swarmVersion != nullptr && swarmVersion->value == L"0.9.1.1",
               "SwarmUI version metadata was not extracted from the PNG parameters chunk");

        const std::wstring expanded = hyperbrowse::services::FormatImageInfoExpanded(*metadata);
        Expect(expanded.find(L"Prompt: cinematic portrait, volumetric lighting, sharp focus") != std::wstring::npos,
               "Expanded image information did not surface the extracted SwarmUI prompt");
        Expect(expanded.find(L"Negative prompt: low quality, deformed hands, text") != std::wstring::npos,
               "Expanded image information did not surface the extracted SwarmUI negative prompt");
    }

    void RunRawFormatAllowlistScenario()
    {
        const std::vector<std::wstring> supportedRawFormats{
            L"ARW",
            L"CR2",
            L"CR3",
            L"DNG",
            L"NEF",
            L"NRW",
            L"RAF",
            L"RW2",
        };

        for (const std::wstring& rawFormat : supportedRawFormats)
        {
            Expect(hyperbrowse::decode::IsRawFileType(rawFormat),
                   std::string("RAW allowlist omitted format: ") + Utf8FromWide(rawFormat));

            hyperbrowse::browser::BrowserItem item;
            item.fileName = L"sample." + rawFormat;
            item.filePath = L"C:\\Raw\\sample." + rawFormat;
            item.fileType = rawFormat;

            Expect(hyperbrowse::decode::CanDecodeThumbnail(item),
                   std::string("RAW thumbnail routing omitted format: ") + Utf8FromWide(rawFormat));
            Expect(hyperbrowse::decode::CanDecodeFullImage(item),
                   std::string("RAW full-image routing omitted format: ") + Utf8FromWide(rawFormat));
        }

        Expect(!hyperbrowse::decode::IsRawFileType(L"ORF"),
               "The RAW allowlist unexpectedly includes ORF before it was requested");
    }

    void RunRawDecoderScenario()
    {
        const fs::path fixtureRoot = TestSourceDirectory() / L"fixtures" / L"raw";
        const fs::path nefPath = fixtureRoot / L"RAW_NIKON_D1.NEF";
        const fs::path nrwPath = fixtureRoot / L"RAW_NIKON_P7000.NRW";

        Expect(fs::exists(nefPath), "The NEF fixture is missing from tests/fixtures/raw");
        Expect(fs::exists(nrwPath), "The NRW fixture is missing from tests/fixtures/raw");

        const hyperbrowse::browser::BrowserItem nefItem = hyperbrowse::browser::BuildBrowserItemFromPath(nefPath);
        const hyperbrowse::browser::BrowserItem nrwItem = hyperbrowse::browser::BuildBrowserItemFromPath(nrwPath);

        Expect(hyperbrowse::decode::CanDecodeThumbnail(nefItem), "NEF fixture should be thumbnail-decodable");
        Expect(hyperbrowse::decode::CanDecodeFullImage(nefItem), "NEF fixture should be full-image decodable");
        Expect(hyperbrowse::decode::CanDecodeThumbnail(nrwItem), "NRW fixture should be thumbnail-decodable");
        Expect(hyperbrowse::decode::CanDecodeFullImage(nrwItem), "NRW fixture should be full-image decodable");

        std::wstring errorMessage;
        const auto nefThumbnail = hyperbrowse::decode::DecodeThumbnail(MakeCacheKey(nefPath, nefItem.modifiedTimestampUtc), &errorMessage);
        Expect(nefThumbnail != nullptr, std::string("LibRaw failed to decode the NEF thumbnail fixture: ") + Utf8FromWide(errorMessage));
        Expect(nefThumbnail->SourceWidth() > 0 && nefThumbnail->SourceHeight() > 0,
               "LibRaw did not surface NEF thumbnail source dimensions");

        errorMessage.clear();
        const auto nrwThumbnail = hyperbrowse::decode::DecodeThumbnail(MakeCacheKey(nrwPath, nrwItem.modifiedTimestampUtc), &errorMessage);
        Expect(nrwThumbnail != nullptr, std::string("LibRaw failed to decode the NRW thumbnail fixture: ") + Utf8FromWide(errorMessage));
        Expect(nrwThumbnail->SourceWidth() > 0 && nrwThumbnail->SourceHeight() > 0,
               "LibRaw did not surface NRW thumbnail source dimensions");

        errorMessage.clear();
        const auto nefFullImage = hyperbrowse::decode::DecodeFullImage(nefItem, &errorMessage);
        Expect(nefFullImage != nullptr, std::string("LibRaw failed to decode the NEF full-image fixture: ") + Utf8FromWide(errorMessage));
        Expect(nefFullImage->SourceWidth() > 0 && nefFullImage->SourceHeight() > 0,
               "LibRaw did not surface NEF full-image source dimensions");

        errorMessage.clear();
        const auto nrwFullImage = hyperbrowse::decode::DecodeFullImage(nrwItem, &errorMessage);
        Expect(nrwFullImage != nullptr, std::string("LibRaw failed to decode the NRW full-image fixture: ") + Utf8FromWide(errorMessage));
        Expect(nrwFullImage->SourceWidth() > 0 && nrwFullImage->SourceHeight() > 0,
               "LibRaw did not surface NRW full-image source dimensions");
        }

    void RunBrowserPaneScenario(HINSTANCE instance)
    {
        HWND hostWindow = CreateUiHostWindow(instance);
        Expect(hostWindow != nullptr, "Failed to create the hidden UI host window");

        hyperbrowse::browser::BrowserPane browserPane(instance);
        Expect(browserPane.Create(hostWindow), "Failed to create the BrowserPane test control");

         WNDCLASSEXW browserPaneClass{};
         browserPaneClass.cbSize = sizeof(browserPaneClass);
         Expect(GetClassInfoExW(instance, L"HyperBrowseBrowserPane", &browserPaneClass) != FALSE,
             "Failed to query the BrowserPane window class");
         Expect((browserPaneClass.style & CS_DBLCLKS) != 0,
             "BrowserPane must register with CS_DBLCLKS so thumbnail double-clicks open the viewer");

        MoveWindow(browserPane.Hwnd(), 0, 0, 860, 620, TRUE);

        hyperbrowse::browser::BrowserModel model;
        std::vector<hyperbrowse::browser::BrowserItem> items;
        items.push_back(hyperbrowse::browser::BrowserItem{L"alpha.jpg", L"C:\\Alpha\\alpha.jpg", L"JPG", L"2026-04-11 10:00", 100, 40, 320, 240});
        items.push_back(hyperbrowse::browser::BrowserItem{L"테스트-漢字.png", L"C:\\Alpha\\테스트-漢字.png", L"PNG", L"2026-04-11 10:01", 200, 10, 640, 480});
        items.push_back(hyperbrowse::browser::BrowserItem{L"gamma.gif", L"C:\\Alpha\\gamma.gif", L"GIF", L"2026-04-11 10:02", 300, 70, 160, 120});
        items.push_back(hyperbrowse::browser::BrowserItem{L"delta.nef", L"C:\\Alpha\\delta.nef", L"NEF", L"2026-04-11 10:03", 400, 55, 1024, 768});
        model.Reset(L"C:\\Alpha", false);
        model.AppendItems(std::move(items), 4, 175);
        model.Complete();

        browserPane.SetModel(&model);
        browserPane.RefreshFromModel();

        SendMessageW(browserPane.Hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(50, 50));
        SendMessageW(browserPane.Hwnd(), WM_LBUTTONUP, 0, MAKELPARAM(50, 50));
        Expect(browserPane.SelectedCount() == 1, "Single-click selection in thumbnail mode failed");

        SendMessageW(browserPane.Hwnd(), WM_LBUTTONDOWN, MK_LBUTTON | MK_SHIFT, MAKELPARAM(500, 50));
        SendMessageW(browserPane.Hwnd(), WM_LBUTTONUP, 0, MAKELPARAM(500, 50));
        Expect(browserPane.SelectedCount() == 3, "Shift-range selection in thumbnail mode failed");

        SendMessageW(browserPane.Hwnd(), WM_LBUTTONDOWN, MK_LBUTTON | MK_CONTROL, MAKELPARAM(300, 50));
        SendMessageW(browserPane.Hwnd(), WM_LBUTTONUP, 0, MAKELPARAM(300, 50));
        Expect(browserPane.SelectedCount() == 2, "Ctrl-toggle selection in thumbnail mode failed");

        browserPane.ClearSelection();
        SendMessageW(browserPane.Hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(5, 5));
        SendMessageW(browserPane.Hwnd(), WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(390, 210));
        SendMessageW(browserPane.Hwnd(), WM_LBUTTONUP, 0, MAKELPARAM(390, 210));
        Expect(browserPane.SelectedCount() == 2, "Rubber-band selection in thumbnail mode failed");

         Expect(browserPane.GetThumbnailSizePreset() == hyperbrowse::browser::ThumbnailSizePreset::Pixels192,
             "BrowserPane did not default to the expected thumbnail size preset");
         Expect(browserPane.IsCompactThumbnailLayoutEnabled(),
             "BrowserPane should default to the compact thumbnail layout");
         Expect(browserPane.AreThumbnailDetailsVisible(),
             "BrowserPane should default to showing thumbnail details");

         browserPane.SetThumbnailSizePreset(hyperbrowse::browser::ThumbnailSizePreset::Pixels96);
         browserPane.SetCompactThumbnailLayout(true);
         browserPane.SetThumbnailDetailsVisible(false);
         browserPane.RefreshFromModel();
         browserPane.ClearSelection();
         SendMessageW(browserPane.Hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(20, 20));
         SendMessageW(browserPane.Hwnd(), WM_LBUTTONUP, 0, MAKELPARAM(20, 20));
         Expect(browserPane.GetThumbnailSizePreset() == hyperbrowse::browser::ThumbnailSizePreset::Pixels96,
             "BrowserPane did not store the compact thumbnail size preset");
         Expect(browserPane.IsCompactThumbnailLayoutEnabled(),
             "BrowserPane did not enable compact thumbnail layout");
         Expect(!browserPane.AreThumbnailDetailsVisible(),
             "BrowserPane did not disable thumbnail details");
         Expect(browserPane.SelectedCount() == 1,
             "Thumbnail-only compact mode did not preserve thumbnail hit-testing");

         browserPane.SetThumbnailSizePreset(hyperbrowse::browser::ThumbnailSizePreset::Pixels320);
         browserPane.SetCompactThumbnailLayout(false);
         browserPane.SetThumbnailDetailsVisible(true);
         browserPane.RefreshFromModel();
         browserPane.ClearSelection();
         SendMessageW(browserPane.Hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(40, 40));
         SendMessageW(browserPane.Hwnd(), WM_LBUTTONUP, 0, MAKELPARAM(40, 40));
         Expect(browserPane.GetThumbnailSizePreset() == hyperbrowse::browser::ThumbnailSizePreset::Pixels320,
             "BrowserPane did not store the large thumbnail size preset");
         Expect(browserPane.IsCompactThumbnailLayoutEnabled(),
             "BrowserPane should remain in the compact thumbnail layout");
         Expect(browserPane.AreThumbnailDetailsVisible(),
             "BrowserPane did not restore thumbnail details visibility");
         Expect(browserPane.SelectedCount() == 1,
             "Large thumbnail mode did not preserve thumbnail hit-testing");

        browserPane.SetViewMode(hyperbrowse::browser::BrowserViewMode::Details);
        browserPane.SetSortMode(hyperbrowse::browser::BrowserSortMode::FileSize);

        HWND listView = FindWindowExW(browserPane.Hwnd(), nullptr, WC_LISTVIEWW, nullptr);
        Expect(listView != nullptr, "Details-mode list view was not created");
        Expect(ListView_GetItemCount(listView) == 4, "Virtual details view item count is incorrect");
         Expect(SendMessageW(listView, LVM_GETUNICODEFORMAT, 0, 0) != FALSE,
             "Details-mode list view is not running in Unicode mode");

        wchar_t buffer[256]{};
        ListView_GetItemText(listView, 0, 0, buffer, static_cast<int>(std::size(buffer)));
         Expect(std::wstring(buffer) == L"테스트-漢字.png", "Details view did not preserve the Unicode filename text for the smallest item");

        Expect(model.UpdateDecodedDimensions(0, 20, 20), "Browser model did not accept a decoded-dimensions update");
        Expect(model.UpdateDecodedDimensions(1, 10, 10), "Browser model did not accept the second decoded-dimensions update");
        Expect(model.UpdateDecodedDimensions(2, 40, 40), "Browser model did not accept the third decoded-dimensions update");
        browserPane.SetSortMode(hyperbrowse::browser::BrowserSortMode::Dimensions);
        browserPane.RefreshFromModel();

        ListView_GetItemText(listView, 0, 0, buffer, static_cast<int>(std::size(buffer)));
        Expect(std::wstring(buffer) == L"테스트-漢字.png", "Dimension sort did not preserve the Unicode filename in details mode");
        ListView_GetItemText(listView, 0, 4, buffer, static_cast<int>(std::size(buffer)));
        Expect(std::wstring(buffer) == L"10x10", "Dimensions column did not surface the decoded dimensions");

        DestroyWindow(hostWindow);
    }

    void RunViewerWindowScenario(HINSTANCE instance, HWND ownerWindow)
    {
        ScopedRegistryDwordBackup overlaySettingBackup(kRegistryPath, kRegistryValueViewerInfoOverlaysVisible);

        {
            HKEY key{};
            if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegistryPath, 0, KEY_WRITE, &key) == ERROR_SUCCESS)
            {
                RegDeleteValueW(key, kRegistryValueViewerInfoOverlaysVisible);
                RegCloseKey(key);
            }
        }

        TempFolder root(L"HyperBrowsePrompt6Viewer");
        const fs::path firstPath = root.Root() / L"first.jpg";
        const fs::path secondPath = root.Root() / L"second.png";
        WriteTestImage(firstPath, TestImageFormat::Jpeg, 48, 24, 6);
        WriteTestImage(secondPath, TestImageFormat::Png, 64, 32);

        std::vector<hyperbrowse::browser::BrowserItem> items;
        items.push_back(hyperbrowse::browser::BrowserItem{L"first.jpg", firstPath.wstring(), L"JPG", L"2026-04-11 12:00", 1, 10, 256, 256});
        items.push_back(hyperbrowse::browser::BrowserItem{L"second.png", secondPath.wstring(), L"PNG", L"2026-04-11 12:01", 2, 20, 256, 256});

        hyperbrowse::viewer::ViewerWindow viewer(instance);
        Expect(viewer.Open(ownerWindow, items, 0, false), "Viewer window failed to open");
        Expect(PumpMessagesUntil([&]() { return viewer.CurrentZoomPercent() > 0; }, 5000),
               "Viewer window did not finish the initial image decode");
         Expect(viewer.IsFullScreen(), "Viewer should open in full screen by default");
         Expect(viewer.AreInfoOverlaysVisible(), "Viewer should default to showing info overlays when no persisted preference exists");

        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_RIGHT, 0);
        Expect(PumpMessagesUntil([&]() { return viewer.CurrentIndex() == 1 && viewer.CurrentZoomPercent() > 0; }, 5000),
               "Viewer next-image navigation failed");

         SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_TAB, 0);
         PumpMessagesFor(100);
         Expect(!viewer.AreInfoOverlaysVisible(), "Viewer Tab key did not hide the info overlays");

        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, '1', 0);
        Expect(PumpMessagesUntil([&]() { return viewer.CurrentZoomPercent() == 100; }, 1000),
               "Viewer actual-size mode did not set zoom to 100%");

         SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_OEM_MINUS, 0);
         PumpMessagesFor(300);
         Expect(viewer.CurrentZoomPercent() == 100,
             "Viewer zoom-out should not shrink the image below the current window-fit bound");

        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, 'R', 0);
        PumpMessagesFor(100);
        Expect(viewer.RotationQuarterTurns() == 1, "Viewer rotate-right command failed");

        SendMessageW(viewer.Hwnd(), WM_KEYDOWN, VK_OEM_PLUS, 0);
        Expect(PumpMessagesUntil([&]() { return viewer.CurrentZoomPercent() > 100; }, 1000),
               "Viewer zoom-in command failed");

        const POINT initialPan = viewer.PanOffset();
        SendMessageW(viewer.Hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(120, 120));
        SendMessageW(viewer.Hwnd(), WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(160, 150));
        SendMessageW(viewer.Hwnd(), WM_LBUTTONUP, 0, MAKELPARAM(160, 150));
        PumpMessagesFor(100);
        const POINT movedPan = viewer.PanOffset();
        Expect(movedPan.x != initialPan.x || movedPan.y != initialPan.y, "Viewer pan interaction failed");

        SendMessageW(viewer.Hwnd(), WM_LBUTTONDBLCLK, 0, MAKELPARAM(100, 100));
        PumpMessagesFor(100);
        Expect(!viewer.IsFullScreen(), "Viewer double-click did not exit full screen");
        SendMessageW(viewer.Hwnd(), WM_LBUTTONDBLCLK, 0, MAKELPARAM(100, 100));
        PumpMessagesFor(100);
        Expect(viewer.IsFullScreen(), "Viewer double-click did not re-enter full screen");

        SendMessageW(viewer.Hwnd(), WM_CLOSE, 0, 0);
        PumpMessagesFor(100);

         hyperbrowse::viewer::ViewerWindow restoredViewer(instance);
         Expect(restoredViewer.Open(ownerWindow, items, 0, false), "Restored viewer window failed to open");
         Expect(PumpMessagesUntil([&]() { return restoredViewer.CurrentZoomPercent() > 0; }, 5000),
             "Restored viewer window did not finish the initial image decode");
         Expect(!restoredViewer.AreInfoOverlaysVisible(), "Viewer did not restore the persisted info-overlay visibility");
         SendMessageW(restoredViewer.Hwnd(), WM_KEYDOWN, VK_TAB, 0);
         PumpMessagesFor(100);
         Expect(restoredViewer.AreInfoOverlaysVisible(), "Viewer Tab key did not restore the info overlays after reopening");
         SendMessageW(restoredViewer.Hwnd(), WM_CLOSE, 0, 0);
         PumpMessagesFor(100);
    }

    void RunMainWindowFolderTreeScenario(HINSTANCE instance)
    {
         const std::vector<std::wstring> expectedSpecialRoots = ExpectedSpecialFolderRootTexts();
         std::wstring persistedDriveRootText;

         {
             hyperbrowse::ui::MainWindow mainWindow(instance);
             Expect(mainWindow.Create(), "Failed to create the MainWindow for the folder-tree scenario");

             HWND mainWindowHandle = FindWindowW(L"HyperBrowseMainWindow", nullptr);
             Expect(mainWindowHandle != nullptr, "Failed to find the created MainWindow instance");

             HWND treeView = FindWindowExW(mainWindowHandle, nullptr, WC_TREEVIEWW, nullptr);
             Expect(treeView != nullptr, "MainWindow did not create the folder tree control");

             HTREEITEM rootItem = TreeView_GetRoot(treeView);
             Expect(rootItem != nullptr, "Folder tree did not populate any root items");

             HTREEITEM currentRoot = rootItem;
             for (const std::wstring& expectedRootText : expectedSpecialRoots)
             {
              Expect(currentRoot != nullptr, "Folder tree did not place special folders ahead of drive roots");
              Expect(ReadTreeItemText(treeView, currentRoot) == expectedRootText,
                  "Folder tree did not insert Desktop/Documents/Pictures above the drive roots in the expected order");
              currentRoot = TreeView_GetNextSibling(treeView, currentRoot);
             }

             Expect(currentRoot != nullptr, "Folder tree did not include any drive roots after the special folders");
             persistedDriveRootText = ReadTreeItemText(treeView, currentRoot);
             Expect(persistedDriveRootText.find(L"Open Folder") == std::wstring::npos,
                 "Folder tree still shows the old placeholder prompt instead of filesystem roots");

             TreeView_Expand(treeView, currentRoot, TVE_EXPAND);
             TreeView_SelectItem(treeView, currentRoot);
             PumpMessagesFor(300);

             wchar_t title[512]{};
             GetWindowTextW(mainWindowHandle, title, static_cast<int>(std::size(title)));
             Expect(std::wstring(title).find(L":\\") != std::wstring::npos,
                 "Selecting a tree node did not route the main window to a concrete filesystem folder");

             DestroyWindow(mainWindowHandle);
             PumpMessagesFor(100);
         }

         {
             hyperbrowse::ui::MainWindow restoredMainWindow(instance);
             Expect(restoredMainWindow.Create(), "Failed to recreate the MainWindow for persistence verification");

             HWND restoredHandle = FindWindowW(L"HyperBrowseMainWindow", nullptr);
             Expect(restoredHandle != nullptr, "Failed to find the recreated MainWindow instance");

             HWND restoredTreeView = FindWindowExW(restoredHandle, nullptr, WC_TREEVIEWW, nullptr);
             Expect(restoredTreeView != nullptr, "Recreated MainWindow did not create the folder tree control");

             PumpMessagesFor(300);
             const HTREEITEM selectedItem = TreeView_GetSelection(restoredTreeView);
             Expect(selectedItem != nullptr, "MainWindow did not restore any tree selection from the previous session");
             Expect(ReadTreeItemText(restoredTreeView, selectedItem) == persistedDriveRootText,
                 "MainWindow did not restore the previously selected folder tree item on startup");

             wchar_t title[512]{};
             GetWindowTextW(restoredHandle, title, static_cast<int>(std::size(title)));
             Expect(std::wstring(title).find(L":\\") != std::wstring::npos,
                 "MainWindow did not reload the persisted folder selection on startup");

             DestroyWindow(restoredHandle);
             PumpMessagesFor(100);
         }
    }
}

int main()
{
    try
    {
        ComScope comScope;
        HINSTANCE instance = GetModuleHandleW(nullptr);
        INITCOMMONCONTROLSEX commonControls{};
        commonControls.dwSize = sizeof(commonControls);
        commonControls.dwICC = ICC_LISTVIEW_CLASSES | ICC_TREEVIEW_CLASSES | ICC_BAR_CLASSES | ICC_STANDARD_CLASSES;
        InitCommonControlsEx(&commonControls);

        TestWindowState state{};
        HWND hwnd = CreateTestWindow(&state, instance);
        Expect(hwnd != nullptr, "Failed to create the hidden test window");

        RunEnumerationScenario(hwnd, &state);
        RunFolderTreeEnumerationScenario(hwnd, &state);
        RunFolderWatchStartStopScenario(hwnd);
        RunThumbnailCacheNormalizationScenario();
        RunWicDecoderScenario();
        RunJpegOrientationAdjustmentScenario();
        RunBatchConvertCancellationScenario(hwnd);
        RunFileRenameOperationScenario(hwnd, &state);
        RunFileConflictPlanningScenario();
        RunThumbnailSchedulerWorkerAllocationScenario();
        RunThumbnailSchedulerScenario(hwnd, &state);
        RunThumbnailSchedulerFailureScenario(hwnd, &state);
        RunThumbnailFailureClassificationScenario();
        RunImageMetadataServiceScenario();
        RunSwarmUiMetadataExtractionScenario();
        RunRawFormatAllowlistScenario();
        RunRawDecoderScenario();
        RunBrowserPaneScenario(instance);
        RunViewerWindowScenario(instance, hwnd);
        RunMainWindowFolderTreeScenario(instance);

        DestroyWindow(hwnd);
        UnregisterClassW(kTestWindowClassName, instance);

        std::cout << "HyperBrowse thumbnail pipeline smoke tests passed\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "HyperBrowse smoke test failed: " << exception.what() << '\n';
        return 1;
    }
}
