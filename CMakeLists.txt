cmake_minimum_required(VERSION 3.25)

project(HyperBrowse VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(HYPERBROWSE_STATIC_MSVC_RUNTIME "Statically link the MSVC runtime to reduce deployed files" ON)

if(MSVC)
    if(HYPERBROWSE_STATIC_MSVC_RUNTIME)
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    else()
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    endif()
endif()

set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP TRUE)
if(MSVC AND NOT HYPERBROWSE_STATIC_MSVC_RUNTIME)
    set(CMAKE_INSTALL_UCRT_LIBRARIES TRUE)
    include(InstallRequiredSystemLibraries)
endif()

set(HYPERBROWSE_INSTALLER_BIN_DIR "bin")
set(HYPERBROWSE_INSTALLER_DOC_DIR "docs")

set(HYPERBROWSE_VERSION_RESOURCE "${CMAKE_CURRENT_BINARY_DIR}/HyperBrowseVersion.rc")
set(HYPERBROWSE_BUILD_INFO_HEADER "${CMAKE_CURRENT_BINARY_DIR}/HyperBrowseBuildInfo.h")
set(HYPERBROWSE_PORTABLE_README "${CMAKE_CURRENT_BINARY_DIR}/PortableREADME.txt")
set(HYPERBROWSE_RUNTIME_DEPENDENCIES "${CMAKE_CURRENT_BINARY_DIR}/RuntimeDependencies.txt")
set(HYPERBROWSE_INNO_SETUP_SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/HyperBrowseInstaller.iss")
set(HYPERBROWSE_TOOLBAR_ICONS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/assets/toolbar-icons")
set(HYPERBROWSE_APP_PUBLISHER "thetheosopher")
set(HYPERBROWSE_APP_PUBLISHER_URL "https://github.com/thetheosopher/HyperBrowse")
file(TO_NATIVE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/assets/HyperBrowse.ico" HYPERBROWSE_APP_ICON_PATH)

option(HYPERBROWSE_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)
option(HYPERBROWSE_BUILD_TESTS "Build the HyperBrowse smoke/integration test suite" ON)
option(HYPERBROWSE_ENABLE_LIBRAW "Build with vendored LibRaw support" ON)
option(HYPERBROWSE_ENABLE_NVJPEG "Enable optional nvJPEG acceleration plumbing" ON)
option(HYPERBROWSE_BUNDLE_CUDA_REDIST "Download and bundle NVIDIA CUDA runtime redistributables with HyperBrowse" ON)
set(HYPERBROWSE_INNO_SETUP_COMPILER "" CACHE FILEPATH "Optional path to the Inno Setup 6 compiler (ISCC.exe) used for release packaging")

include(CTest)

set(HYPERBROWSE_BUNDLED_CUDA_RUNTIME_DLLS "")
set(HYPERBROWSE_BUNDLED_CUDA_LICENSE_FILES "")
set(HYPERBROWSE_PORTABLE_MSVC_RUNTIME_LINE "- No separate Microsoft Visual C++ runtime DLLs; the runtime is linked statically into HyperBrowse.exe and HyperBrowseRawHelper.exe")
set(HYPERBROWSE_RUNTIME_DEPENDENCY_MSVC_LINE "No separate Microsoft Visual C++ runtime DLLs, because this build uses the static MSVC runtime")
set(HYPERBROWSE_PORTABLE_NVIDIA_RUNTIME_LINE "The app uses WIC by default and only uses nvJPEG when that optional runtime is available.")
set(HYPERBROWSE_RUNTIME_DEPENDENCY_NVIDIA_LINE "nvJPEG, because acceleration is optional and the application stays correct without NVIDIA runtime support")
set(HYPERBROWSE_PORTABLE_NVIDIA_CONTENT_LINE "")

if(MSVC AND NOT HYPERBROWSE_STATIC_MSVC_RUNTIME)
    set(HYPERBROWSE_PORTABLE_MSVC_RUNTIME_LINE "- Microsoft Visual C++ runtime DLLs staged by CMake for this toolchain")
    set(HYPERBROWSE_RUNTIME_DEPENDENCY_MSVC_LINE "Microsoft Visual C++ runtime DLLs copied by CMake when the active toolchain requires them")
endif()

if(HYPERBROWSE_ENABLE_NVJPEG AND HYPERBROWSE_BUNDLE_CUDA_REDIST)
    include(cmake/BundleCudaRedistributables.cmake)
    hyperbrowse_prepare_cuda_redistributables(HYPERBROWSE_BUNDLED_CUDA_RUNTIME_DLLS HYPERBROWSE_BUNDLED_CUDA_LICENSE_FILES)
    set(HYPERBROWSE_PORTABLE_NVIDIA_CONTENT_LINE "- Bundled NVIDIA CUDA runtime DLLs: cudart64_12.dll and nvjpeg64_12.dll")
    set(HYPERBROWSE_PORTABLE_NVIDIA_RUNTIME_LINE "NVIDIA CUDA and nvJPEG runtime DLLs are bundled beside the executable, so nvJPEG does not depend on PATH or a machine-wide CUDA install.")
    set(HYPERBROWSE_RUNTIME_DEPENDENCY_NVIDIA_LINE "NVIDIA CUDA runtime DLLs (cudart64_12.dll and nvjpeg64_12.dll) downloaded from official NVIDIA redistributable archives and staged beside HyperBrowse.exe when nvJPEG support is enabled")
endif()

configure_file(cmake/HyperBrowseVersion.rc.in "${HYPERBROWSE_VERSION_RESOURCE}" @ONLY)
configure_file(cmake/HyperBrowseBuildInfo.h.in "${HYPERBROWSE_BUILD_INFO_HEADER}" @ONLY)
configure_file(cmake/PortableREADME.txt.in "${HYPERBROWSE_PORTABLE_README}" @ONLY)
configure_file(cmake/RuntimeDependencies.txt.in "${HYPERBROWSE_RUNTIME_DEPENDENCIES}" @ONLY)
configure_file(cmake/HyperBrowseInstaller.iss.in "${HYPERBROWSE_INNO_SETUP_SCRIPT}" @ONLY)

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

# ---------------------------------------------------------------------------
# HyperBrowseCore — static library shared by the application and tests so each
# translation unit is compiled exactly once per build.
# ---------------------------------------------------------------------------
add_library(HyperBrowseCore STATIC
    src/browser/BrowserModel.cpp
    src/browser/BrowserPane.cpp
    src/cache/DiskThumbnailCache.cpp
    src/cache/ThumbnailCache.cpp
    src/decode/ImageDecoder.cpp
    src/decode/NvJpegDecoder.cpp
    src/decode/RawHelperProtocol.cpp
    src/decode/WicThumbnailDecoder.cpp
    src/services/BatchConvertService.cpp
    src/services/FileOperationService.cpp
    src/services/FolderEnumerationService.cpp
    src/services/FolderTreeEnumerationService.cpp
    src/services/FolderWatchService.cpp
    src/services/ImageMetadataService.cpp
    src/services/JpegTransformService.cpp
    src/services/ThumbnailScheduler.cpp
    src/services/UserMetadataStore.cpp
    src/render/D2DRenderer.cpp
    src/ui/DiagnosticsWindow.cpp
    src/ui/MainWindow.cpp
    src/ui/ToolbarIconLibrary.cpp
    src/util/Diagnostics.cpp
    src/util/Log.cpp
    src/viewer/ViewerWindow.cpp
)

target_include_directories(HyperBrowseCore
    PUBLIC
        src
        external/nanosvg
    PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}"
)

target_link_libraries(HyperBrowseCore PUBLIC
    comctl32
    dwmapi
    d2d1
    d3d11
    dwrite
    msimg32
    propsys
    windowscodecs
    shell32
    shlwapi
)

if(HYPERBROWSE_ENABLE_LIBRAW)
    target_link_libraries(HyperBrowseCore PUBLIC HyperBrowse::LibRaw)
    target_compile_definitions(HyperBrowseCore PUBLIC HYPERBROWSE_ENABLE_LIBRAW=1)
endif()

if(HYPERBROWSE_ENABLE_NVJPEG)
    target_compile_definitions(HyperBrowseCore PUBLIC HYPERBROWSE_ENABLE_NVJPEG=1)
endif()

if(MSVC)
    target_compile_definitions(HyperBrowseCore PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

add_executable(HyperBrowse WIN32
    src/app/WinMain.cpp
    src/app/Application.cpp
    "${HYPERBROWSE_VERSION_RESOURCE}"
)

target_link_libraries(HyperBrowse PRIVATE HyperBrowseCore)

if(MSVC)
    target_compile_definitions(HyperBrowse PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

if(HYPERBROWSE_ENABLE_LIBRAW)
    add_executable(HyperBrowseRawHelper
        src/tools/RawHelperMain.cpp
        src/cache/ThumbnailCache.cpp
        src/decode/ImageDecoder.cpp
        src/decode/NvJpegDecoder.cpp
        src/decode/RawHelperProtocol.cpp
        src/decode/WicThumbnailDecoder.cpp
        src/util/Diagnostics.cpp
    )

    target_include_directories(HyperBrowseRawHelper PRIVATE
        src
    )

    target_link_libraries(HyperBrowseRawHelper PRIVATE
        msimg32
        propsys
        shell32
        shlwapi
        windowscodecs
        HyperBrowse::LibRaw
    )

    target_compile_definitions(HyperBrowseRawHelper PRIVATE HYPERBROWSE_ENABLE_LIBRAW=1)

    if(HYPERBROWSE_ENABLE_NVJPEG)
        target_compile_definitions(HyperBrowseRawHelper PRIVATE HYPERBROWSE_ENABLE_NVJPEG=1)
    endif()

    if(MSVC)
        target_compile_definitions(HyperBrowseRawHelper PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()

    set_target_properties(HyperBrowseRawHelper PROPERTIES
        OUTPUT_NAME "HyperBrowseRawHelper"
        VERSION ${PROJECT_VERSION}
    )

    add_dependencies(HyperBrowse HyperBrowseRawHelper)
    add_custom_command(TARGET HyperBrowse POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:HyperBrowseRawHelper>"
            "$<TARGET_FILE_DIR:HyperBrowse>")
endif()

set_target_properties(HyperBrowse PROPERTIES
    OUTPUT_NAME "HyperBrowse"
    VERSION ${PROJECT_VERSION}
)

add_custom_command(TARGET HyperBrowse POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E remove_directory "$<TARGET_FILE_DIR:HyperBrowse>/assets")

if(HYPERBROWSE_BUNDLED_CUDA_RUNTIME_DLLS)
    foreach(_hyperbrowse_cuda_runtime_dll IN LISTS HYPERBROWSE_BUNDLED_CUDA_RUNTIME_DLLS)
        add_custom_command(TARGET HyperBrowse POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_hyperbrowse_cuda_runtime_dll}"
                "$<TARGET_FILE_DIR:HyperBrowse>")
    endforeach()
endif()

install(TARGETS HyperBrowse
    RUNTIME DESTINATION "."
    COMPONENT Portable)

install(TARGETS HyperBrowse
    RUNTIME DESTINATION "${HYPERBROWSE_INSTALLER_BIN_DIR}"
    COMPONENT Runtime)

if(HYPERBROWSE_ENABLE_LIBRAW)
    install(TARGETS HyperBrowseRawHelper
        RUNTIME DESTINATION "."
        COMPONENT Portable)

    install(TARGETS HyperBrowseRawHelper
        RUNTIME DESTINATION "${HYPERBROWSE_INSTALLER_BIN_DIR}"
        COMPONENT Runtime)
endif()

install(FILES "${HYPERBROWSE_PORTABLE_README}"
    DESTINATION "."
    COMPONENT Portable
    RENAME "README.txt")

install(FILES "${HYPERBROWSE_RUNTIME_DEPENDENCIES}"
    DESTINATION "."
    COMPONENT Portable
    RENAME "RUNTIME-DEPENDENCIES.txt")

install(FILES "${HYPERBROWSE_PORTABLE_README}"
    DESTINATION "${HYPERBROWSE_INSTALLER_DOC_DIR}"
    COMPONENT Runtime
    RENAME "README-portable.txt")

install(FILES "${HYPERBROWSE_RUNTIME_DEPENDENCIES}"
    DESTINATION "${HYPERBROWSE_INSTALLER_DOC_DIR}"
    COMPONENT Runtime
    RENAME "runtime-dependencies.txt")

if(HYPERBROWSE_BUNDLED_CUDA_RUNTIME_DLLS)
    install(PROGRAMS ${HYPERBROWSE_BUNDLED_CUDA_RUNTIME_DLLS}
        DESTINATION "."
        COMPONENT Portable)

    install(PROGRAMS ${HYPERBROWSE_BUNDLED_CUDA_RUNTIME_DLLS}
        DESTINATION "${HYPERBROWSE_INSTALLER_BIN_DIR}"
        COMPONENT Runtime)
endif()

if(HYPERBROWSE_BUNDLED_CUDA_LICENSE_FILES)
    list(GET HYPERBROWSE_BUNDLED_CUDA_LICENSE_FILES 0 _hyperbrowse_cudart_license)
    list(GET HYPERBROWSE_BUNDLED_CUDA_LICENSE_FILES 1 _hyperbrowse_nvjpeg_license)

    install(FILES "${_hyperbrowse_cudart_license}"
        DESTINATION "."
        COMPONENT Portable
        RENAME "NVIDIA-CUDA-RUNTIME-LICENSE.txt")
    install(FILES "${_hyperbrowse_nvjpeg_license}"
        DESTINATION "."
        COMPONENT Portable
        RENAME "NVIDIA-NVJPEG-LICENSE.txt")

    install(FILES "${_hyperbrowse_cudart_license}"
        DESTINATION "${HYPERBROWSE_INSTALLER_DOC_DIR}"
        COMPONENT Runtime
        RENAME "nvidia-cuda-runtime-license.txt")
    install(FILES "${_hyperbrowse_nvjpeg_license}"
        DESTINATION "${HYPERBROWSE_INSTALLER_DOC_DIR}"
        COMPONENT Runtime
        RENAME "nvidia-nvjpeg-license.txt")
endif()

if(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS)
    install(PROGRAMS ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS}
        DESTINATION "."
        COMPONENT Portable)

    install(PROGRAMS ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS}
        DESTINATION "${HYPERBROWSE_INSTALLER_BIN_DIR}"
        COMPONENT Runtime)
endif()

if(HYPERBROWSE_BUILD_TESTS AND BUILD_TESTING)
    add_subdirectory(tests)
endif()

set(_hyperbrowse_release_artifact_dependencies HyperBrowse)
if(HYPERBROWSE_ENABLE_LIBRAW)
    list(APPEND _hyperbrowse_release_artifact_dependencies HyperBrowseRawHelper)
endif()
if(HYPERBROWSE_BUILD_TESTS AND BUILD_TESTING)
    list(APPEND _hyperbrowse_release_artifact_dependencies HyperBrowseTests)
endif()

set(_hyperbrowse_release_package_command
    -ExecutionPolicy Bypass
    -NoProfile
    -File "${CMAKE_CURRENT_SOURCE_DIR}/tools/PackageRelease.ps1"
    -ProjectRoot "${CMAKE_CURRENT_SOURCE_DIR}"
    -BuildDir "${CMAKE_BINARY_DIR}"
    -Configuration "$<CONFIG>"
    -SkipBuild)

if(HYPERBROWSE_INNO_SETUP_COMPILER)
    list(APPEND _hyperbrowse_release_package_command
        -InnoSetupCompiler "${HYPERBROWSE_INNO_SETUP_COMPILER}")
endif()

if(NOT (HYPERBROWSE_BUILD_TESTS AND BUILD_TESTING))
    list(APPEND _hyperbrowse_release_package_command -SkipTests)
endif()

add_custom_target(HyperBrowseReleaseArtifacts
    COMMAND powershell.exe ${_hyperbrowse_release_package_command}
    BYPRODUCTS
        "${CMAKE_BINARY_DIR}/dist/HyperBrowse-${PROJECT_VERSION}-portable-win64.zip"
        "${CMAKE_BINARY_DIR}/dist/HyperBrowse-${PROJECT_VERSION}-installer.exe"
    DEPENDS ${_hyperbrowse_release_artifact_dependencies}
    USES_TERMINAL
    VERBATIM)
