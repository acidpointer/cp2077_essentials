# Native Linux LLVM tools, targeting the Microsoft x64 C++ ABI.
# XWIN_ROOT is the output of `xwin splat` (with case-correcting symlinks).
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)

set(XWIN_ROOT "$ENV{XWIN_ROOT}" CACHE PATH "Microsoft CRT and Windows SDK from xwin")
if(NOT EXISTS "${XWIN_ROOT}/crt/include" OR NOT EXISTS "${XWIN_ROOT}/sdk/include/um")
  message(FATAL_ERROR "Set XWIN_ROOT to an xwin splat directory (see README.md).")
endif()
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES XWIN_ROOT)

find_program(CMAKE_CXX_COMPILER NAMES clang-cl REQUIRED)
set(CMAKE_C_COMPILER "${CMAKE_CXX_COMPILER}")
set(CMAKE_C_COMPILER_TARGET x86_64-pc-windows-msvc)
set(CMAKE_CXX_COMPILER_TARGET x86_64-pc-windows-msvc)
find_program(CMAKE_LINKER NAMES lld-link REQUIRED)
find_program(CMAKE_AR NAMES llvm-lib REQUIRED)
find_program(CMAKE_MT NAMES llvm-mt REQUIRED)
find_program(CMAKE_RC_COMPILER NAMES llvm-rc REQUIRED)

set(CMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES
  "${XWIN_ROOT}/crt/include"
  "${XWIN_ROOT}/sdk/include/ucrt"
  "${XWIN_ROOT}/sdk/include/shared"
  "${XWIN_ROOT}/sdk/include/um"
  "${XWIN_ROOT}/sdk/include/winrt"
)
set(CMAKE_C_STANDARD_INCLUDE_DIRECTORIES "${CMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES}")
set(CMAKE_RC_FLAGS_INIT
  "/I\"${XWIN_ROOT}/sdk/include/um\" /I\"${XWIN_ROOT}/sdk/include/shared\" /I\"${XWIN_ROOT}/sdk/include/ucrt\"")
set(_red4ext_libpaths
  "/libpath:\"${XWIN_ROOT}/crt/lib/x86_64\" /libpath:\"${XWIN_ROOT}/sdk/lib/ucrt/x86_64\" /libpath:\"${XWIN_ROOT}/sdk/lib/um/x86_64\"")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${_red4ext_libpaths}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${_red4ext_libpaths}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${_red4ext_libpaths}")

# Ignore MSVC-only switches that clang-cl accepts but does not implement.
set(CMAKE_CXX_FLAGS_INIT "-Wno-unused-command-line-argument")
set(CMAKE_C_FLAGS_INIT "${CMAKE_CXX_FLAGS_INIT}")
set(CMAKE_FIND_ROOT_PATH "${XWIN_ROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
