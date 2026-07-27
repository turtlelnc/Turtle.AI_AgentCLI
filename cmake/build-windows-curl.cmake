cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED CURL_SOURCE_DIR OR NOT IS_DIRECTORY "${CURL_SOURCE_DIR}")
    message(FATAL_ERROR "Pass -DCURL_SOURCE_DIR=/path/to/curl-source")
endif()

get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(TOOLCHAIN
    "${PROJECT_ROOT}/cmake/toolchains/llvm-mingw-x86_64.cmake")
set(BUILD_DIR "${PROJECT_ROOT}/third_party/curl-windows-x64-build")
set(INSTALL_DIR "${PROJECT_ROOT}/third_party/curl-windows-x64")

execute_process(
    COMMAND "${CMAKE_COMMAND}"
        -S "${CURL_SOURCE_DIR}"
        -B "${BUILD_DIR}"
        -DCMAKE_TOOLCHAIN_FILE=${TOOLCHAIN}
        -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_INSTALL_PREFIX=${INSTALL_DIR}
        -DBUILD_SHARED_LIBS=OFF
        -DBUILD_CURL_EXE=OFF
        -DBUILD_EXAMPLES=OFF
        -DBUILD_LIBCURL_DOCS=OFF
        -DBUILD_MISC_DOCS=OFF
        -DBUILD_TESTING=OFF
        -DCURL_USE_SCHANNEL=ON
        -DCURL_USE_LIBPSL=OFF
        -DCURL_ZLIB=OFF
        -DCURL_BROTLI=OFF
        -DCURL_ZSTD=OFF
        -DCURL_DISABLE_LDAP=ON
        -DCURL_DISABLE_LDAPS=ON
    COMMAND_ERROR_IS_FATAL ANY
)
execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${BUILD_DIR}" --parallel 4
    COMMAND_ERROR_IS_FATAL ANY
)
execute_process(
    COMMAND "${CMAKE_COMMAND}" --install "${BUILD_DIR}"
    COMMAND_ERROR_IS_FATAL ANY
)
