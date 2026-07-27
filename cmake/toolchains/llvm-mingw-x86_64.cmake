set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(
    LLVM_MINGW_ROOT
    "/Users/zhangtom/Desktop/Build for Windows/llvm-mingw-20260311-ucrt-macos-universal"
    CACHE PATH
    "Path to the llvm-mingw distribution"
)

set(_llvm_mingw_bin "${LLVM_MINGW_ROOT}/bin")
set(_llvm_mingw_target "x86_64-w64-mingw32")

set(CMAKE_C_COMPILER
    "${_llvm_mingw_bin}/${_llvm_mingw_target}-clang")
set(CMAKE_CXX_COMPILER
    "${_llvm_mingw_bin}/${_llvm_mingw_target}-clang++")
set(CMAKE_RC_COMPILER
    "${_llvm_mingw_bin}/${_llvm_mingw_target}-windres")
set(CMAKE_AR
    "${_llvm_mingw_bin}/${_llvm_mingw_target}-ar")
set(CMAKE_RANLIB
    "${_llvm_mingw_bin}/${_llvm_mingw_target}-ranlib")
set(CMAKE_STRIP
    "${_llvm_mingw_bin}/${_llvm_mingw_target}-strip")

set(CMAKE_FIND_ROOT_PATH
    "${LLVM_MINGW_ROOT}/${_llvm_mingw_target}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

unset(_llvm_mingw_bin)
unset(_llvm_mingw_target)
