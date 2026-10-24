# Reproduce CET's pinned xmake dependencies using the shared Microsoft ABI
# toolchain. Keep generated LuaJIT/configuration files outside upstream sources.
set(cet_src "${ESSENTIALS_ROOT}/sources/cyber-engine-tweaks")
set(cet_gen "${CMAKE_BINARY_DIR}/generated/CyberEngineTweaks")
find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_program(CET_HOST_CLANG NAMES clang REQUIRED)

set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
add_subdirectory("${dep}/cet-freetype" deps/cet-freetype EXCLUDE_FROM_ALL)

set(luajit "${dep}/cet-luajit")
set(luajit_gen "${CMAKE_BINARY_DIR}/generated/luajit")
file(GLOB_RECURSE luajit_inputs CONFIGURE_DEPENDS "${luajit}/src/*" "${luajit}/dynasm/*")
add_custom_command(OUTPUT "${luajit_gen}/src/lj_vm.obj"
  COMMAND "${Python3_EXECUTABLE}" "${ESSENTIALS_ROOT}/scripts/luajit_codegen.py"
    "${luajit}" "${luajit_gen}" "${CET_HOST_CLANG}"
  DEPENDS ${luajit_inputs} "${ESSENTIALS_ROOT}/scripts/luajit_codegen.py" VERBATIM)
add_custom_target(cet_luajit_codegen DEPENDS "${luajit_gen}/src/lj_vm.obj")
file(GLOB luajit_sources CONFIGURE_DEPENDS "${luajit}/src/lj_*.c" "${luajit}/src/lib_*.c")
add_library(cet_luajit STATIC ${luajit_sources} "${luajit_gen}/src/lj_vm.obj")
add_dependencies(cet_luajit cet_luajit_codegen)
target_include_directories(cet_luajit SYSTEM PUBLIC "${luajit}/src" PRIVATE "${luajit_gen}/src")
target_compile_definitions(cet_luajit PRIVATE _CRT_SECURE_NO_WARNINGS)

add_library(cet_sqlite STATIC "${dep}/cet-sqlite/sqlite3.c")
target_include_directories(cet_sqlite SYSTEM PUBLIC "${dep}/cet-sqlite")
target_compile_definitions(cet_sqlite PRIVATE SQLITE_THREADSAFE=1 SQLITE_ENABLE_FTS5
  SQLITE_ENABLE_EXPLAIN_COMMENTS SQLITE_ENABLE_DBPAGE_VTAB SQLITE_ENABLE_STMTVTAB
  SQLITE_ENABLE_DBSTAT_VTAB SQLITE_ENABLE_MATH_FUNCTIONS SQLITE_DEFAULT_WAL_SYNCHRONOUS=1)

add_library(cet_imgui STATIC
  "${dep}/cet-imgui/imgui.cpp" "${dep}/cet-imgui/imgui_draw.cpp"
  "${dep}/cet-imgui/imgui_tables.cpp" "${dep}/cet-imgui/imgui_widgets.cpp"
  "${dep}/cet-imgui/imgui_demo.cpp" "${dep}/cet-imgui/misc/freetype/imgui_freetype.cpp")
target_include_directories(cet_imgui SYSTEM PUBLIC "${dep}/cet-imgui")
target_compile_definitions(cet_imgui PUBLIC IMGUI_USE_WCHAR32 IMGUI_ENABLE_FREETYPE
  "IMGUI_USER_CONFIG=\"${cet_src}/src/imgui_impl/imgui_user_config.h\"")
target_link_libraries(cet_imgui PRIVATE freetype)

file(READ "${ESSENTIALS_ROOT}/sources.lock.json" cet_lock)
string(JSON CET_VERSION_SIMPLE GET "${cet_lock}" projects cyber-engine-tweaks version)
string(REPLACE "." ";" cet_version_parts "${CET_VERSION_SIMPLE}")
list(GET cet_version_parts 0 CET_VERSION_MAJOR)
list(GET cet_version_parts 1 CET_VERSION_MINOR)
list(GET cet_version_parts 2 CET_VERSION_PATCH)
set(CET_DEBUG_BUILD 0)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(CET_DEBUG_BUILD 1)
endif()
set(CET_GIT_TAG "v${CET_VERSION_SIMPLE}")
set(CET_GIT_TAG_LONG "${CET_GIT_TAG}")
set(CET_GIT_BRANCH "essentials")
execute_process(COMMAND git -C "${cet_src}" rev-parse HEAD OUTPUT_VARIABLE CET_GIT_COMMIT_LONG
  OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
string(SUBSTRING "${CET_GIT_COMMIT_LONG}" 0 7 CET_GIT_COMMIT)
execute_process(COMMAND git -C "${cet_src}" show -s --format=%cd --date=format:%Y%m%d%H%M%S HEAD
  OUTPUT_VARIABLE CET_GIT_COMMIT_DATE OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
string(SUBSTRING "${CET_GIT_COMMIT_DATE}" 0 4 CET_CURRENT_YEAR)
set(CET_VERSION_FULL "${CET_GIT_TAG} [essentials]")
set(CET_VERSION "${CET_VERSION_FULL}")
set(CET_PRODUCT_NAME "Cyber Engine Tweaks")
set(CET_GROUP_NAME "Cyber Engine Tweaks Team")
set(CET_FILE_NAME "CyberEngineTweaks.dll")
file(READ "${cet_src}/src/xmake/CETVersion.h.in" cet_header)
foreach(variable CET_DEBUG_BUILD CET_VERSION_MAJOR CET_VERSION_MINOR CET_VERSION_PATCH CET_CURRENT_YEAR)
  string(REPLACE "\${define ${variable}}" "#define ${variable} ${${variable}}" cet_header "${cet_header}")
endforeach()
foreach(variable CET_GIT_TAG CET_GIT_TAG_LONG CET_GIT_BRANCH CET_GIT_COMMIT CET_GIT_COMMIT_LONG
    CET_GIT_COMMIT_DATE CET_VERSION_SIMPLE CET_VERSION_FULL CET_VERSION CET_PRODUCT_NAME CET_GROUP_NAME CET_FILE_NAME)
  string(REPLACE "\${define ${variable}}" "#define ${variable} \"${${variable}}\"" cet_header "${cet_header}")
endforeach()
file(CONFIGURE OUTPUT "${cet_gen}/config/CETVersion.h" CONTENT "${cet_header}" @ONLY)
configure_file("${cet_src}/src/xmake/Resource.rc.in" "${cet_gen}/Resources.rc")

file(GLOB_RECURSE cet_sources CONFIGURE_DEPENDS "${cet_src}/src/*.cpp")
add_library(CyberEngineTweaks SHARED ${cet_sources} "${cet_gen}/Resources.rc"
  "${ESSENTIALS_ROOT}/cmake/AbiChecks.cpp")
set_target_properties(CyberEngineTweaks PROPERTIES PREFIX "" OUTPUT_NAME CyberEngineTweaks)
add_dependencies(CyberEngineTweaks cet_luajit_codegen)
target_include_directories(CyberEngineTweaks PRIVATE "${cet_gen}" "${cet_src}/src")
target_include_directories(CyberEngineTweaks SYSTEM PRIVATE
  "${RED4EXT_SDK_DIR}/include" "${RED4EXT_SDK_DIR}/vendor/D3D12MemAlloc"
  "${dep}/cet-json/include" "${dep}/cet-mem/include" "${dep}/cet-sol2/include"
  "${dep}/cet-xbyak" "${dep}/cet-stb")
target_precompile_headers(CyberEngineTweaks PRIVATE "${cet_src}/src/stdafx.h")
target_compile_definitions(CyberEngineTweaks PRIVATE WINVER=0x0A00 _WIN32_WINNT=0x0A00
  WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS
  SOL_ALL_SAFETIES_ON SOL_LUAJIT=1 SOL_EXCEPTIONS_SAFE_PROPAGATION
  SPDLOG_WCHAR_TO_UTF8_SUPPORT SPDLOG_WCHAR_FILENAMES SPDLOG_WCHAR_SUPPORT
  "$<$<CONFIG:Debug>:CET_DEBUG>")
target_compile_options(CyberEngineTweaks PRIVATE /GR- /EHsc /bigobj /utf-8)
target_link_options(CyberEngineTweaks PRIVATE /DEBUG:FULL /INCREMENTAL:NO
  "$<$<CONFIG:Release>:/OPT:REF>" "$<$<CONFIG:Release>:/OPT:ICF>")
target_link_libraries(CyberEngineTweaks PRIVATE TiltedCore minhook spdlog::spdlog_header_only
  cet_luajit cet_sqlite cet_imgui user32 version d3d11 d3d12 dxgi d3dcompiler)
