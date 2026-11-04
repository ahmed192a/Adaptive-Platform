cmake_minimum_required(VERSION 3.16)

project(Adaptive_Platform
        VERSION 0.2.0
        DESCRIPTION "AUTOSAR Adaptive Platform for Linux"
        LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/cmake)

include(AdaptiveOptions)
include(AdaptiveCompilerOptions)
include(AdaptiveDependencies)
include(AdaptiveCluster)

# Staged install tree inside the build folder unless the user chooses one.
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT AND AP_IS_TOP_LEVEL)
  set(CMAKE_INSTALL_PREFIX ${CMAKE_BINARY_DIR}/install CACHE PATH "" FORCE)
endif()

# ---------------------------------------------------------------------------
# Libraries: ara::core, ara::exec, ara::com, ara::sm, ara::ucm, ara::log, ...
# ---------------------------------------------------------------------------
add_subdirectory(src)

# Process manifests are copied next to the binaries; the unit tests and the
# platform processes read them through paths relative to the build folder.
file(COPY App/Manifests            DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/App)
file(COPY App/UCM/sw_packages.json DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/App/processes/UCM_SERVER)
file(COPY App/UCM/config.conf      DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/App/processes/UCM_SERVER)
file(MAKE_DIRECTORY                       ${CMAKE_CURRENT_BINARY_DIR}/App/processes/redirected)

if(AP_BUILD_APPS)
  add_subdirectory(Cloud)
  add_subdirectory(App)
endif()
if(AP_BUILD_TOOLS)
  add_subdirectory(Tools/cpp)
endif()
if(AP_BUILD_EXAMPLES)
  add_subdirectory(test_partial_processes)
endif()

if(AP_BUILD_TESTS)
  include(AdaptiveTesting)
  enable_testing()
  add_subdirectory(Testing)
endif()

if(AP_INSTALL)
  include(CMakePackageConfigHelpers)
  set(AP_CMAKE_INSTALL_DIR ${CMAKE_INSTALL_LIBDIR}/cmake/AdaptivePlatform)
  install(EXPORT AdaptivePlatformTargets
          NAMESPACE AdaptivePlatform:: DESTINATION ${AP_CMAKE_INSTALL_DIR})
  configure_package_config_file(cmake/AdaptivePlatformConfig.cmake.in
          ${CMAKE_CURRENT_BINARY_DIR}/AdaptivePlatformConfig.cmake
          INSTALL_DESTINATION ${AP_CMAKE_INSTALL_DIR})
  write_basic_package_version_file(
          ${CMAKE_CURRENT_BINARY_DIR}/AdaptivePlatformConfigVersion.cmake
          VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
  install(FILES ${CMAKE_CURRENT_BINARY_DIR}/AdaptivePlatformConfig.cmake
                ${CMAKE_CURRENT_BINARY_DIR}/AdaptivePlatformConfigVersion.cmake
          DESTINATION ${AP_CMAKE_INSTALL_DIR})
endif()
