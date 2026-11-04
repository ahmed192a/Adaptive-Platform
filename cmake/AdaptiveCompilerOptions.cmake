# ---------------------------------------------------------------------------
# Compiler configuration expressed as INTERFACE targets instead of global
# CMAKE_CXX_FLAGS, so that third-party code fetched with FetchContent is never
# affected and every target opts in explicitly.
#
#   ap::warnings   strict warning set (+ -Werror when AP_WARNINGS_AS_ERRORS)
#   ap::legacy     relaxed warning set for not-yet-cleaned legacy code
#   ap::coverage   gcov instrumentation   (AP_ENABLE_COVERAGE)
#   ap::sanitizers ASan + UBSan            (AP_ENABLE_SANITIZERS)
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD          17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_ENABLE_EXPORTS        ON)   # export executable symbols for backtraces
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

find_package(Threads REQUIRED)

add_library(ap_warnings INTERFACE)
add_library(ap::warnings ALIAS ap_warnings)
add_library(ap_legacy INTERFACE)
add_library(ap::legacy ALIAS ap_legacy)
add_library(ap_coverage INTERFACE)
add_library(ap::coverage ALIAS ap_coverage)
add_library(ap_sanitizers INTERFACE)
add_library(ap::sanitizers ALIAS ap_sanitizers)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(ap_warnings INTERFACE
    -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -Wcast-align
    -Woverloaded-virtual -Wconversion -Wsign-conversion -Wnull-dereference
    -Wformat=2 -Wduplicated-cond)
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(ap_warnings INTERFACE -Wlogical-op -Wuseless-cast)
  endif()
  if(AP_WARNINGS_AS_ERRORS)
    target_compile_options(ap_warnings INTERFACE -Werror)
  endif()
  target_compile_options(ap_legacy INTERFACE -Wall)
elseif(MSVC)
  target_compile_options(ap_warnings INTERFACE /W4 $<$<BOOL:${AP_WARNINGS_AS_ERRORS}>:/WX>)
  target_compile_options(ap_legacy INTERFACE /W3)
endif()

if(AP_ENABLE_COVERAGE)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    message(FATAL_ERROR "AP_ENABLE_COVERAGE needs GCC or Clang")
  endif()
  if(NOT AP_BUILD_TESTS)
    message(FATAL_ERROR "AP_ENABLE_COVERAGE needs AP_BUILD_TESTS=ON")
  endif()
  target_compile_options(ap_coverage INTERFACE -O0 -g --coverage -fprofile-arcs -ftest-coverage)
  target_link_options(ap_coverage INTERFACE --coverage)
  include(${CMAKE_CURRENT_LIST_DIR}/CodeCoverage.cmake)
  set(COVERAGE_EXCLUDES "/usr/include/*" "${CMAKE_BINARY_DIR}/_deps/*"
                        "${PROJECT_SOURCE_DIR}/Testing/*" "${PROJECT_SOURCE_DIR}/App/*")
endif()

if(AP_ENABLE_SANITIZERS)
  target_compile_options(ap_sanitizers INTERFACE -fsanitize=address,undefined
                                                  -fno-omit-frame-pointer -fno-sanitize-recover=undefined)
  target_link_options(ap_sanitizers INTERFACE -fsanitize=address,undefined)
endif()
