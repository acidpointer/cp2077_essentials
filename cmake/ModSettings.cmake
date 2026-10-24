# Use our shared SDK and upstream-pinned RedLib headers. Upstream's build
# helpers initialize a private SDK and copy directly to a game directory.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${ESSENTIALS_ROOT}/scripts/mod_settings.py" prepare
  WORKING_DIRECTORY "${ESSENTIALS_ROOT}" COMMAND_ERROR_IS_FATAL ANY)
set(ms_src "${ESSENTIALS_ROOT}/sources/mod-settings")
file(READ "${ESSENTIALS_ROOT}/sources.lock.json" ms_lock)
string(JSON ms_version GET "${ms_lock}" projects mod-settings version)
string(REPLACE "." ";" ms_parts "${ms_version}")
list(GET ms_parts 0 ms_major)
list(GET ms_parts 1 ms_minor)
list(GET ms_parts 2 ms_patch)
file(GLOB_RECURSE ms_sources CONFIGURE_DEPENDS "${ms_src}/src/red4ext/*.cpp")
add_library(ModSettings SHARED ${ms_sources} "${ESSENTIALS_ROOT}/cmake/AbiChecks.cpp")
set_target_properties(ModSettings PROPERTIES PREFIX "" OUTPUT_NAME mod_settings)
target_include_directories(ModSettings PRIVATE "${ms_src}/src/red4ext" "${ms_src}/include"
  "${ESSENTIALS_ROOT}/sources/archive-xl/support/red4ext")
target_include_directories(ModSettings SYSTEM PRIVATE "${RED4EXT_SDK_DIR}/include"
  "${RED4EXT_SDK_DIR}/vendor/D3D12MemAlloc"
  "${ESSENTIALS_ROOT}/build/generated/mod-settings-redlib/include"
  "${ESSENTIALS_ROOT}/build/generated/mod-settings-redlib/vendor")
target_precompile_headers(ModSettings PRIVATE "${ms_src}/src/red4ext/stdafx.hpp")
target_compile_definitions(ModSettings PRIVATE
  WINVER=0x0601 _WIN32_WINNT=0x0601 WIN32_LEAN_AND_MEAN NOMINMAX
  UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS MOD_SETTINGS_DLLDIR_EX
  MOD_VERSION_STR="${ms_version}" MOD_VERSION_MAJOR=${ms_major}
  MOD_VERSION_MINOR=${ms_minor} MOD_VERSION_PATCH=${ms_patch})
target_compile_options(ModSettings PRIVATE /GR- /EHsc /bigobj /utf-8
  "$<$<CONFIG:Release>:/Ob2>" "$<$<CONFIG:Release>:/Gy>")
target_link_options(ModSettings PRIVATE /DEBUG:FULL /INCREMENTAL:NO
  "$<$<CONFIG:Release>:/OPT:REF>" "$<$<CONFIG:Release>:/OPT:ICF>")
