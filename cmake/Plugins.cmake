function(essentials_plugin name directory)
  set(src "${ESSENTIALS_ROOT}/${directory}")
  set(gen "${CMAKE_BINARY_DIR}/generated/${name}")
  file(READ "${src}/xmake.lua" xmake)
  string(REGEX MATCH "set_version\\(\"([0-9]+)\\.([0-9]+)\\.([0-9]+)\"" ignored "${xmake}")
  set(VERSION_MAJOR "${CMAKE_MATCH_1}")
  set(VERSION_MINOR "${CMAKE_MATCH_2}")
  set(VERSION_ALTER "${CMAKE_MATCH_3}")
  set(VERSION "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_ALTER}")
  set(VERSION_BUILD 250)
  set(NAME "${name}")
  set(AUTHOR psiberx)
  string(TOLOWER "${CMAKE_BUILD_TYPE}" MODE)
  set(DEBUG "${MODE}")
  configure_file("${src}/config/Project.hpp.in" "${gen}/App/Project.hpp")
  configure_file("${src}/config/Version.rc.in" "${gen}/App/Version.rc")
  file(GLOB_RECURSE sources CONFIGURE_DEPENDS "${src}/src/*.cpp" "${src}/lib/*.cpp")
  add_library(${name} SHARED ${sources} "${gen}/App/Version.rc" "${ESSENTIALS_ROOT}/cmake/AbiChecks.cpp")
  set_target_properties(${name} PROPERTIES PREFIX "" OUTPUT_NAME "${name}")
  target_include_directories(${name} PRIVATE "${gen}" "${src}/src" "${src}/lib")
  target_include_directories(${name} SYSTEM PRIVATE
    "${RED4EXT_SDK_DIR}/include" "${RED4EXT_SDK_DIR}/vendor/D3D12MemAlloc"
    "${src}/vendor/nameof/include" "${src}/vendor/semver/include"
    "${src}/vendor/wil/include")
  target_precompile_headers(${name} PRIVATE "${src}/src/pch.hpp")
  target_compile_definitions(${name} PRIVATE
    WINVER=0x0601 _WIN32_WINNT=0x0601 WIN32_LEAN_AND_MEAN NOMINMAX
    UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS)
  # Keep these plugin sources warning-free, including current SDK API usage.
  target_compile_options(${name} PRIVATE /GR- /EHsc /bigobj /utf-8
    "$<$<COMPILE_LANG_AND_ID:CXX,Clang>:-Werror>"
    "$<$<CONFIG:Release>:/Ob2>" "$<$<CONFIG:Release>:/Gy>")
  target_link_options(${name} PRIVATE /DEBUG:FULL /INCREMENTAL:NO
    "$<$<CONFIG:Release>:/OPT:REF>" "$<$<CONFIG:Release>:/OPT:ICF>")
  target_link_libraries(${name} PRIVATE TiltedCore minhook spdlog::spdlog version user32)
  if(NOT name STREQUAL "Codeware")
    target_link_libraries(${name} PRIVATE yaml-cpp::yaml-cpp)
  endif()
  if(name STREQUAL "Codeware")
    target_sources(${name} PRIVATE "${src}/vendor/clip/clip.cpp" "${src}/vendor/clip/clip_win.cpp")
    target_include_directories(${name} SYSTEM PRIVATE "${src}/vendor/clip")
    target_link_libraries(${name} PRIVATE ole32 shell32 gdi32 shlwapi)
  elseif(name STREQUAL "TweakXL")
    target_include_directories(${name} SYSTEM PRIVATE "${src}/vendor/pegtl/include")
  elseif(name STREQUAL "ArchiveXL")
    find_program(LLVM_ML llvm-ml REQUIRED)
    set(registers "${CMAKE_BINARY_DIR}/Registers.obj")
    add_custom_command(OUTPUT "${registers}"
      COMMAND "${LLVM_ML}" --m64 /c "/Fo${registers}" "${src}/src/App/Utils/Registers.asm"
      DEPENDS "${src}/src/App/Utils/Registers.asm" VERBATIM)
    target_sources(${name} PRIVATE "${registers}")
  endif()
endfunction()
essentials_plugin(Codeware sources/codeware)
essentials_plugin(TweakXL sources/tweak-xl)
essentials_plugin(ArchiveXL sources/archive-xl)

# Input Loader has its own source layout; keep upstream build helpers (and their
# private SDK/dependency initialization) outside the shared essentials build.
set(input_src "${ESSENTIALS_ROOT}/sources/input-loader")
file(READ "${ESSENTIALS_ROOT}/sources.lock.json" essentials_lock)
string(JSON input_version GET "${essentials_lock}" projects input-loader version)
string(REPLACE "." ";" input_version_parts "${input_version}")
list(GET input_version_parts 0 input_major)
list(GET input_version_parts 1 input_minor)
list(GET input_version_parts 2 input_patch)
file(GLOB_RECURSE input_sources CONFIGURE_DEPENDS "${input_src}/src/*.cpp")
add_library(InputLoader SHARED ${input_sources} "${ESSENTIALS_ROOT}/cmake/AbiChecks.cpp")
set_target_properties(InputLoader PROPERTIES PREFIX "" OUTPUT_NAME input_loader)
target_include_directories(InputLoader PRIVATE "${input_src}/src" "${input_src}/include")
target_include_directories(InputLoader SYSTEM PRIVATE "${RED4EXT_SDK_DIR}/include"
  "${RED4EXT_SDK_DIR}/vendor/D3D12MemAlloc")
target_precompile_headers(InputLoader PRIVATE "${input_src}/src/stdafx.hpp")
target_compile_definitions(InputLoader PRIVATE
  # FileIdInfo supplies the full 128-bit identity (including ReFS). The game
  # itself requires a newer Windows version than this API's Windows 8 minimum.
  WINVER=0x0602 _WIN32_WINNT=0x0602 WIN32_LEAN_AND_MEAN NOMINMAX
  UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS DLLDIR_EX
  SPDLOG_WCHAR_TO_UTF8_SUPPORT SPDLOG_WCHAR_FILENAMES
  MOD_VERSION_STR="${input_version}" MOD_VERSION_MAJOR=${input_major}
  MOD_VERSION_MINOR=${input_minor} MOD_VERSION_PATCH=${input_patch})
target_compile_options(InputLoader PRIVATE /GR- /EHsc /bigobj /utf-8
  "$<$<CONFIG:Release>:/Ob2>" "$<$<CONFIG:Release>:/Gy>")
target_link_options(InputLoader PRIVATE /DEBUG:FULL /INCREMENTAL:NO
  "$<$<CONFIG:Release>:/OPT:REF>" "$<$<CONFIG:Release>:/OPT:ICF>")
# Header-only spdlog keeps Input Loader's wide-character configuration isolated
# from the compiled spdlog used by the other plugins.
target_link_libraries(InputLoader PRIVATE spdlog::spdlog_header_only pugixml-static bcrypt)
