cmake_minimum_required(VERSION 3.26)
project(LoadFreezeRegressions LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded)
get_filename_component(ESSENTIALS_ROOT "../../.." ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

function(freeze_test name)
  add_executable(${name} "../${name}.cpp")
  target_include_directories(${name} SYSTEM PRIVATE "${ESSENTIALS_ROOT}/sources/red4ext-sdk/include")
  target_include_directories(${name} PRIVATE "${ESSENTIALS_ROOT}/sources/archive-xl/src")
  target_compile_definitions(${name} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
  target_compile_options(${name} PRIVATE /EHsc /utf-8 /Gy /GR- -Werror)
  target_link_options(${name} PRIVATE /OPT:REF)
endfunction()

freeze_test(sdk_transaction_system_abi)
freeze_test(archive_attachment_locking)
freeze_test(archive_puppet_listener_lifetime)
target_include_directories(archive_puppet_listener_lifetime PRIVATE
  "${ESSENTIALS_ROOT}/sources/archive-xl/lib" "${ESSENTIALS_ROOT}/build/plugins/release/include"
  "${ESSENTIALS_ROOT}/build/deps/hopscotch-map/include")

# Optional negative control: an include overlay holding only the previous SDK
# transaction-system header. No source checkout or Git mutation is needed.
set(OLD_SDK_OVERLAY "" CACHE PATH "Header overlay for the negative ABI control")
if(OLD_SDK_OVERLAY)
  add_executable(sdk_transaction_system_abi_old ../sdk_transaction_system_abi.cpp)
  target_include_directories(sdk_transaction_system_abi_old BEFORE PRIVATE "${OLD_SDK_OVERLAY}")
  target_include_directories(sdk_transaction_system_abi_old SYSTEM PRIVATE "${ESSENTIALS_ROOT}/sources/red4ext-sdk/include")
  target_compile_definitions(sdk_transaction_system_abi_old PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
  target_compile_options(sdk_transaction_system_abi_old PRIVATE /EHsc /utf-8 /Gy /GR- -Werror)
  target_link_options(sdk_transaction_system_abi_old PRIVATE /OPT:REF)
endif()
