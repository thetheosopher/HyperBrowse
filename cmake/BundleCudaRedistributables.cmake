include_guard(GLOBAL)

set(HYPERBROWSE_CUDA_REDIST_LABEL "12.6.3" CACHE STRING "Pinned NVIDIA CUDA redistributable manifest label used for bundled nvJPEG runtime assets")
set(HYPERBROWSE_CUDA_REDIST_BASE_URL "https://developer.download.nvidia.com/compute/cuda/redist" CACHE STRING "Base URL for NVIDIA CUDA redistributable archives")

function(hyperbrowse_download_verified_archive)
    set(options)
    set(oneValueArgs URL SHA256 OUT_FILE)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "" ${ARGN})

    if(NOT ARG_URL OR NOT ARG_SHA256 OR NOT ARG_OUT_FILE)
        message(FATAL_ERROR "hyperbrowse_download_verified_archive requires URL, SHA256, and OUT_FILE.")
    endif()

    get_filename_component(_archive_dir "${ARG_OUT_FILE}" DIRECTORY)
    file(MAKE_DIRECTORY "${_archive_dir}")

    if(EXISTS "${ARG_OUT_FILE}")
        file(SHA256 "${ARG_OUT_FILE}" _existing_sha)
        if(_existing_sha STREQUAL ARG_SHA256)
            return()
        endif()

        file(REMOVE "${ARG_OUT_FILE}")
    endif()

    set(_tmp_file "${ARG_OUT_FILE}.download")
    file(REMOVE "${_tmp_file}")

    find_program(_hyperbrowse_curl NAMES curl.exe curl)
    if(_hyperbrowse_curl)
        execute_process(
            COMMAND "${_hyperbrowse_curl}" --ssl-no-revoke -L "${ARG_URL}" -o "${_tmp_file}"
            RESULT_VARIABLE _curl_result
            OUTPUT_QUIET
            ERROR_VARIABLE _curl_error)
        if(NOT _curl_result EQUAL 0)
            file(REMOVE "${_tmp_file}")
            message(FATAL_ERROR "Failed to download ${ARG_URL}: ${_curl_error}")
        endif()
    else()
        file(DOWNLOAD "${ARG_URL}" "${_tmp_file}" SHOW_PROGRESS STATUS _download_status LOG _download_log)
        list(GET _download_status 0 _download_code)
        list(GET _download_status 1 _download_message)
        if(NOT _download_code EQUAL 0)
            file(REMOVE "${_tmp_file}")
            message(FATAL_ERROR "Failed to download ${ARG_URL}: ${_download_message}\n${_download_log}")
        endif()
    endif()

    file(SHA256 "${_tmp_file}" _downloaded_sha)
    if(NOT _downloaded_sha STREQUAL ARG_SHA256)
        file(REMOVE "${_tmp_file}")
        message(FATAL_ERROR "SHA256 mismatch for ${ARG_URL}. Expected ${ARG_SHA256} but got ${_downloaded_sha}.")
    endif()

    file(RENAME "${_tmp_file}" "${ARG_OUT_FILE}")
endfunction()

function(hyperbrowse_prepare_cuda_package)
    set(options)
    set(oneValueArgs RELATIVE_PATH SHA256 DLL_NAME DOWNLOAD_DIR EXTRACT_DIR OUT_DLL OUT_LICENSE)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "" ${ARGN})

    if(NOT ARG_RELATIVE_PATH OR NOT ARG_SHA256 OR NOT ARG_DLL_NAME OR NOT ARG_DOWNLOAD_DIR OR NOT ARG_EXTRACT_DIR OR NOT ARG_OUT_DLL OR NOT ARG_OUT_LICENSE)
        message(FATAL_ERROR "hyperbrowse_prepare_cuda_package requires RELATIVE_PATH, SHA256, DLL_NAME, DOWNLOAD_DIR, EXTRACT_DIR, OUT_DLL, and OUT_LICENSE.")
    endif()

    get_filename_component(_archive_name "${ARG_RELATIVE_PATH}" NAME)
    set(_archive_path "${ARG_DOWNLOAD_DIR}/${_archive_name}")
    string(REGEX REPLACE "\\.zip$" "" _package_root_name "${_archive_name}")
    set(_package_root "${ARG_EXTRACT_DIR}/${_package_root_name}")
    set(_dll_path "${_package_root}/bin/${ARG_DLL_NAME}")
    set(_license_path "${_package_root}/LICENSE")

    hyperbrowse_download_verified_archive(
        URL "${HYPERBROWSE_CUDA_REDIST_BASE_URL}/${ARG_RELATIVE_PATH}"
        SHA256 "${ARG_SHA256}"
        OUT_FILE "${_archive_path}")

    if(NOT EXISTS "${_dll_path}" OR NOT EXISTS "${_license_path}")
        file(REMOVE_RECURSE "${_package_root}")
        file(MAKE_DIRECTORY "${ARG_EXTRACT_DIR}")
        file(ARCHIVE_EXTRACT INPUT "${_archive_path}" DESTINATION "${ARG_EXTRACT_DIR}")
    endif()

    if(NOT EXISTS "${_dll_path}")
        message(FATAL_ERROR "Expected bundled CUDA runtime DLL was not found after extracting ${_archive_name}: ${_dll_path}")
    endif()

    if(NOT EXISTS "${_license_path}")
        message(FATAL_ERROR "Expected CUDA redistribution license was not found after extracting ${_archive_name}: ${_license_path}")
    endif()

    set(${ARG_OUT_DLL} "${_dll_path}" PARENT_SCOPE)
    set(${ARG_OUT_LICENSE} "${_license_path}" PARENT_SCOPE)
endfunction()

function(hyperbrowse_prepare_cuda_redistributables out_dlls out_licenses)
    set(_download_dir "${CMAKE_BINARY_DIR}/cuda-redist/downloads")
    set(_extract_dir "${CMAKE_BINARY_DIR}/cuda-redist/extracted")

    hyperbrowse_prepare_cuda_package(
        RELATIVE_PATH "cuda_cudart/windows-x86_64/cuda_cudart-windows-x86_64-12.6.77-archive.zip"
        SHA256 "7a313bc0c93b1a50bb03aa9783a199ae70c3b66e2d8084da65e8254a8577b925"
        DLL_NAME "cudart64_12.dll"
        DOWNLOAD_DIR "${_download_dir}"
        EXTRACT_DIR "${_extract_dir}"
        OUT_DLL _cudart_dll
        OUT_LICENSE _cudart_license)

    hyperbrowse_prepare_cuda_package(
        RELATIVE_PATH "libnvjpeg/windows-x86_64/libnvjpeg-windows-x86_64-12.3.3.54-archive.zip"
        SHA256 "15c1b28e623598eb75f48d02bfd34efa83b35e3f8d1d5241c93a385d0bea9d18"
        DLL_NAME "nvjpeg64_12.dll"
        DOWNLOAD_DIR "${_download_dir}"
        EXTRACT_DIR "${_extract_dir}"
        OUT_DLL _nvjpeg_dll
        OUT_LICENSE _nvjpeg_license)

    set(${out_dlls} "${_cudart_dll};${_nvjpeg_dll}" PARENT_SCOPE)
    set(${out_licenses} "${_cudart_license};${_nvjpeg_license}" PARENT_SCOPE)
endfunction()