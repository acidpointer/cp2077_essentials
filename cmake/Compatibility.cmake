# Opt-in isolated core. This file is intentionally not included by production
# package targets while the admission/ownership gates remain open.
find_package(Python3 3.12 REQUIRED COMPONENTS Interpreter)
set(_compat "${ESSENTIALS_ROOT}/compat")
set(_profile "${CMAKE_CURRENT_BINARY_DIR}/generated/compat/Profile.hpp")
add_custom_command(OUTPUT "${_profile}"
  COMMAND "${Python3_EXECUTABLE}" "${_compat}/tools/generate_schema.py"
    "${_compat}/schema/2.31-to-2.50.json" --out "${_profile}"
  DEPENDS "${_compat}/tools/generate_schema.py" "${_compat}/schema/2.31-to-2.50.json"
  VERBATIM)
add_library(compat_core STATIC
  "${_compat}/src/Domain.cpp" "${_compat}/src/DomainRegistry.cpp"
  "${_compat}/src/Schema.cpp" "${_compat}/src/AddressCatalog.cpp"
  "${_compat}/src/ObjectRegistry.cpp" "${_compat}/src/ValueMarshaller.cpp"
  "${_compat}/src/BorrowedValue.cpp" "${_compat}/src/LiveMemory.cpp"
  "${_compat}/src/InstructionPlan.cpp" "${_compat}/src/FrameAdapters.cpp" "${_profile}")
if(WIN32)
  target_sources(compat_core PRIVATE "${_compat}/src/ViewArena.cpp" "${_compat}/src/ExceptionBackend.cpp")
  target_sources(compat_core PRIVATE "${_compat}/src/FunctionAdapter.cpp")
  target_sources(compat_core PRIVATE "${_compat}/src/ArrayAdapter.cpp")
  # Native-memory probes must retain asynchronous SEH regions even when memcpy
  # or atomics become intrinsics. /EHsc can optimize those scopes away.
  set_source_files_properties("${_compat}/src/LiveMemory.cpp" PROPERTIES COMPILE_OPTIONS "/EHa")
endif()
target_include_directories(compat_core PUBLIC "${_compat}/include" "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_compile_features(compat_core PUBLIC cxx_std_20)
if(MSVC)
  target_compile_options(compat_core PRIVATE /W4 /WX /EHsc /utf-8)
else()
  target_compile_options(compat_core PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
