add_executable(HyperBrowseTests
    smoke.cpp
)

target_compile_features(HyperBrowseTests PRIVATE cxx_std_20)

target_link_libraries(HyperBrowseTests PRIVATE HyperBrowseCore)

if(HYPERBROWSE_ENABLE_LIBRAW)
    if(TARGET HyperBrowseRawHelper)
        add_dependencies(HyperBrowseTests HyperBrowseRawHelper)
        add_custom_command(TARGET HyperBrowseTests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:HyperBrowseRawHelper>"
                "$<TARGET_FILE_DIR:HyperBrowseTests>")
    endif()
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

add_custom_command(TARGET HyperBrowseTests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:HyperBrowseTests>/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/../assets/toolbar-icons"
        "$<TARGET_FILE_DIR:HyperBrowseTests>/assets/toolbar-icons")

add_test(NAME HyperBrowseSmoke COMMAND HyperBrowseTests)
