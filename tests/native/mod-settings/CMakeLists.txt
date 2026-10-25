cmake_minimum_required(VERSION 3.26)
project(ModSettingsRegressions LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded)
get_filename_component(ESSENTIALS_ROOT "../../.." ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
add_executable(mod-settings-runtime runtime.cpp discovery.cpp)
target_include_directories(mod-settings-runtime PRIVATE
  "${ESSENTIALS_ROOT}/sources/mod-settings/src/red4ext")
target_include_directories(mod-settings-runtime SYSTEM PRIVATE
  "${ESSENTIALS_ROOT}/build/generated/mod-settings-redlib/include"
  "${ESSENTIALS_ROOT}/build/generated/mod-settings-redlib/vendor"
  "${ESSENTIALS_ROOT}/sources/red4ext-sdk/include")
target_compile_definitions(mod-settings-runtime PRIVATE
  RED4EXT_STATIC_LIB NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
target_compile_options(mod-settings-runtime PRIVATE /EHsc /utf-8 /Gy /GR- -Werror)
target_link_options(mod-settings-runtime PRIVATE /OPT:REF)
add_library(mod-settings-resolver SHARED resolver.cpp)
set_target_properties(mod-settings-resolver PROPERTIES PREFIX "" OUTPUT_NAME RED4ext)
