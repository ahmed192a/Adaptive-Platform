# ---------------------------------------------------------------------------
# Third-party dependencies.  With AP_USE_SYSTEM_DEPS=ON find_package() is tried
# first; otherwise (or when nothing is found) FetchContent downloads a pinned
# version at configure time.
# ---------------------------------------------------------------------------
include(FetchContent)
set(FETCHCONTENT_QUIET ON)
# CMake 4 dropped compatibility with cmake_minimum_required(< 3.5), which some pinned third-party
# projects (e.g. nlohmann_json 3.9.1) still declare.  Harmless on older CMake versions.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "Minimum policy version for third-party projects")

# CMake >= 4 removed compatibility with cmake_minimum_required(VERSION < 3.5), which some of the pinned
# third-party versions below still declare. This variable (CMake >= 3.31) lets them configure; older CMake
# versions simply ignore it. A user-supplied value wins.
if(NOT DEFINED CMAKE_POLICY_VERSION_MINIMUM)
  set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
endif()

# CMake >= 4 rejects dependencies that still declare cmake_minimum_required(< 3.5)
# (nlohmann_json 3.9.1, googletest). Let them configure with the 3.5 policy set.
if(NOT DEFINED CMAKE_POLICY_VERSION_MINIMUM)
  set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
endif()

# CMake >= 4.0 rejects the old cmake_minimum_required() (< 3.5) of pinned third-party projects such as
# nlohmann_json 3.9.1.  This variable is inherited by the FetchContent sub-projects and ignored by older CMake.
if(NOT DEFINED CMAKE_POLICY_VERSION_MINIMUM)
  set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
endif()

macro(ap_dependency NAME TARGET)
  # usage: ap_dependency(<fetch-name> <imported-target> [FIND <pkg>] GIT_REPOSITORY .. GIT_TAG ..)
  cmake_parse_arguments(_D "" "FIND" "" ${ARGN})
  set(_found FALSE)
  if(AP_USE_SYSTEM_DEPS AND _D_FIND)
    find_package(${_D_FIND} QUIET)
    if(TARGET ${TARGET})
      set(_found TRUE)
      message(STATUS "Using system ${NAME}")
    endif()
  endif()
  if(NOT _found)
    message(STATUS "Fetching ${NAME}")
    FetchContent_Declare(${NAME} ${_D_UNPARSED_ARGUMENTS} GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(${NAME})
  endif()
endmacro()

# CMake >= 4 rejects the old cmake_minimum_required of pinned third-party deps (json v3.9.1).
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)
set(JSON_BuildTests OFF CACHE INTERNAL "")
set(JSON_Install    ${AP_INSTALL} CACHE INTERNAL "")
ap_dependency(json nlohmann_json::nlohmann_json FIND nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json GIT_TAG v3.11.3)

set(SPDLOG_BUILD_EXAMPLE OFF CACHE INTERNAL "")
set(SPDLOG_BUILD_TESTS   OFF CACHE INTERNAL "")
set(SPDLOG_BUILD_BENCH   OFF CACHE INTERNAL "")
set(SPDLOG_INSTALL       ${AP_INSTALL} CACHE INTERNAL "")
ap_dependency(spdlog spdlog::spdlog FIND spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog.git GIT_TAG v1.11.0)

if(AP_BUILD_TESTS)
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  ap_dependency(googletest GTest::gtest_main FIND GTest
    GIT_REPOSITORY https://github.com/google/googletest.git GIT_TAG release-1.11.0)
endif()

if(AP_BUILD_EXAMPLES OR AP_BUILD_TOOLS)
  set(CLI11_INSTALL OFF CACHE INTERNAL "")
  ap_dependency(cli11 CLI11::CLI11 FIND CLI11
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git GIT_TAG v2.3.2)
endif()

if(AP_BUILD_EXAMPLES)
  ap_dependency(p7 P7
    GIT_REPOSITORY https://github.com/CMakePorts/libp7-baical.git GIT_TAG v5.0)
endif()
