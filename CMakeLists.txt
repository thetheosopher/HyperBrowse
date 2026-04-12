cmake_minimum_required(VERSION 3.25)

project(HyperBrowse VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(HYPERBROWSE_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)
option(HYPERBROWSE_BUILD_TESTS "Build placeholder tests" ON)
option(HYPERBROWSE_ENABLE_LIBRAW "Build with vendored LibRaw support" ON)
option(HYPERBROWSE_ENABLE_NVJPEG "Enable optional nvJPEG acceleration plumbing" OFF)

if(MSVC)
    add_compile_options(/permissive- /Zc:__cplusplus /W4 /EHsc /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN)
    if(HYPERBROWSE_WARNINGS_AS_ERRORS)
        add_compile_options(/WX)
    endif()
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
    if(HYPERBROWSE_WARNINGS_AS_ERRORS)
        add_compile_options(-Werror)
    endif()
endif()

if(HYPERBROWSE_ENABLE_LIBRAW)
    add_subdirectory(external/libraw EXCLUDE_FROM_ALL)
endif()

add_executable(HyperBrowse WIN32
    src/app/WinMain.cpp
    src/app/Application.cpp
    src/browser/BrowserModel.cpp
    src/browser/BrowserPane.cpp
    src/cache/ThumbnailCache.cpp
    src/decode/ImageDecoder.cpp
    src/decode/WicThumbnailDecoder.cpp
    src/services/BatchConvertService.cpp
    src/services/FolderEnumerationService.cpp
    src/services/FolderWatchService.cpp
    src/services/ImageMetadataService.cpp
    src/services/JpegTransformService.cpp
    src/services/ThumbnailScheduler.cpp
    src/ui/MainWindow.cpp
    src/util/Log.cpp
    src/viewer/ViewerWindow.cpp
)

target_include_directories(HyperBrowse PRIVATE
    src
)

target_link_libraries(HyperBrowse PRIVATE
    comctl32
    dwmapi
    d2d1
    d3d11
    msimg32
    propsys
    windowscodecs
    shell32
    shlwapi
)

if(HYPERBROWSE_ENABLE_LIBRAW)
    target_link_libraries(HyperBrowse PRIVATE HyperBrowse::LibRaw)
    target_compile_definitions(HyperBrowse PRIVATE HYPERBROWSE_ENABLE_LIBRAW=1)
endif()

if(HYPERBROWSE_ENABLE_NVJPEG)
    target_compile_definitions(HyperBrowse PRIVATE HYPERBROWSE_ENABLE_NVJPEG=1)
endif()

if(MSVC)
    target_compile_definitions(HyperBrowse PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

set_target_properties(HyperBrowse PROPERTIES
    OUTPUT_NAME "HyperBrowse"
)

if(HYPERBROWSE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
