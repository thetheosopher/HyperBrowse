add_executable(HyperBrowseTests
    ../src/browser/BrowserModel.cpp
    ../src/browser/BrowserPane.cpp
    ../src/cache/ThumbnailCache.cpp
    ../src/decode/ImageDecoder.cpp
    ../src/decode/NvJpegDecoder.cpp
    ../src/decode/RawHelperProtocol.cpp
    ../src/decode/WicThumbnailDecoder.cpp
    ../src/services/BatchConvertService.cpp
    ../src/services/FileOperationService.cpp
    ../src/services/FolderEnumerationService.cpp
    ../src/services/FolderTreeEnumerationService.cpp
    ../src/services/FolderWatchService.cpp
    ../src/services/ImageMetadataService.cpp
    ../src/services/JpegTransformService.cpp
    ../src/ui/DiagnosticsWindow.cpp
    ../src/ui/MainWindow.cpp
    ../src/services/ThumbnailScheduler.cpp
    ../src/util/Diagnostics.cpp
    ../src/util/Log.cpp
    ../src/viewer/ViewerWindow.cpp
    smoke.cpp
)

target_compile_features(HyperBrowseTests PRIVATE cxx_std_20)

target_include_directories(HyperBrowseTests PRIVATE
    ../src
)

target_link_libraries(HyperBrowseTests PRIVATE
    comctl32
    dwmapi
    msimg32
    propsys
    shell32
    windowscodecs
)

if(HYPERBROWSE_ENABLE_LIBRAW)
    target_link_libraries(HyperBrowseTests PRIVATE HyperBrowse::LibRaw)
    target_compile_definitions(HyperBrowseTests PRIVATE HYPERBROWSE_ENABLE_LIBRAW=1)

    if(TARGET HyperBrowseRawHelper)
        add_dependencies(HyperBrowseTests HyperBrowseRawHelper)
        add_custom_command(TARGET HyperBrowseTests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:HyperBrowseRawHelper>"
                "$<TARGET_FILE_DIR:HyperBrowseTests>")
    endif()
endif()

if(HYPERBROWSE_ENABLE_NVJPEG)
    target_compile_definitions(HyperBrowseTests PRIVATE HYPERBROWSE_ENABLE_NVJPEG=1)
endif()

if(HYPERBROWSE_BUNDLED_CUDA_RUNTIME_DLLS)
    foreach(_hyperbrowse_cuda_runtime_dll IN LISTS HYPERBROWSE_BUNDLED_CUDA_RUNTIME_DLLS)
        add_custom_command(TARGET HyperBrowseTests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_hyperbrowse_cuda_runtime_dll}"
                "$<TARGET_FILE_DIR:HyperBrowseTests>")
    endforeach()
endif()

target_compile_definitions(HyperBrowseTests PRIVATE
    HYPERBROWSE_TESTS_SOURCE_DIR=L"${CMAKE_CURRENT_SOURCE_DIR}"
)

add_test(NAME HyperBrowseSmoke COMMAND HyperBrowseTests)
