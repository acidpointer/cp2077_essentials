# Injected after RED4ext's project() by our Linux build driver.
# Use pinned toml11 headers directly: its optional build/install configuration
# sets an obsolete CMake policy even though RED4ext needs only the headers.
include_guard(GLOBAL)
include(FetchContent)
if(NOT EXISTS "${FETCHCONTENT_SOURCE_DIR_TOML11}/include/toml11/parser.hpp")
  message(FATAL_ERROR "The pinned toml11 source directory is required.")
endif()
if(TOML11_PRECOMPILE)
  message(FATAL_ERROR "The essentials loader uses header-only toml11.")
endif()
FetchContent_Declare(toml11 SOURCE_DIR "${FETCHCONTENT_SOURCE_DIR_TOML11}"
  SOURCE_SUBDIR __essentials_header_only__)
add_library(toml11 INTERFACE)
add_library(toml11::toml11 ALIAS toml11)
target_include_directories(toml11 INTERFACE "${FETCHCONTENT_SOURCE_DIR_TOML11}/include")
target_compile_definitions(toml11 INTERFACE
  "$<$<BOOL:${TOML11_ENABLE_ACCESS_CHECK}>:TOML11_ENABLE_ACCESS_CHECK>")
if(MSVC)
  target_compile_options(toml11 INTERFACE /utf-8 /Zc:preprocessor)
endif()
