# ---------------------------------------------------------------------------
# Project-wide build options.  Every option is prefixed with AP_ so that the
# project can be consumed through add_subdirectory()/FetchContent without
# clashing with options of the parent project.
# ---------------------------------------------------------------------------
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
  set(AP_IS_TOP_LEVEL ON)
else()
  set(AP_IS_TOP_LEVEL OFF)
endif()

option(AP_BUILD_SHARED        "Build the ara:: functional clusters as shared libraries" ON)
option(AP_BUILD_TESTS         "Build the unit tests (GoogleTest)"                    ${AP_IS_TOP_LEVEL})
option(AP_BUILD_APPS          "Build the platform processes (EM, SM, UCM, SD, OTA)"  ${AP_IS_TOP_LEVEL})
option(AP_BUILD_TOOLS         "Build the C++ command line tools"                     ${AP_IS_TOP_LEVEL})
option(AP_BUILD_EXAMPLES      "Build examples and the partial-process test programs" ${AP_IS_TOP_LEVEL})
option(AP_ENABLE_COVERAGE     "Instrument code for gcov/lcov coverage (needs Debug)" OFF)
option(AP_ENABLE_SANITIZERS   "Build with AddressSanitizer + UBSan"                  OFF)
option(AP_WARNINGS_AS_ERRORS  "Treat warnings in new/migrated code as errors"        ON)
option(AP_USE_SYSTEM_DEPS     "Prefer find_package() over FetchContent downloads"    OFF)
option(AP_INSTALL             "Generate install rules and the CMake package"         ${AP_IS_TOP_LEVEL})

# ---- backward compatibility with the old option names ---------------------
if(DEFINED BUILD_TESTS)
  set(AP_BUILD_TESTS ${BUILD_TESTS})
endif()
if(DEFINED ENABLE_COVERAGE)
  set(AP_ENABLE_COVERAGE ${ENABLE_COVERAGE})
endif()

if(AP_BUILD_SHARED)
  set(LIB_BUILD_TYPE SHARED)   # kept for the legacy create_library() callers
else()
  set(LIB_BUILD_TYPE STATIC)
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "No build type given, defaulting to 'Release'")
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

message(STATUS "AP: tests=${AP_BUILD_TESTS} apps=${AP_BUILD_APPS} tools=${AP_BUILD_TOOLS} "
               "examples=${AP_BUILD_EXAMPLES} shared=${AP_BUILD_SHARED} "
               "coverage=${AP_ENABLE_COVERAGE} sanitizers=${AP_ENABLE_SANITIZERS}")
