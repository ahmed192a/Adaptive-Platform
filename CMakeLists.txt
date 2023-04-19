cmake_minimum_required(VERSION 3.13...3.20)
project(
  Adaptive_Platform 
  VERSION 0.1.0
  LANGUAGES CXX)


if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "Setting build type to 'Release' as no build type was specified")
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the build type (Debug/Release)" FORCE)
endif (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)

# This AUTOSAR Adaptive Platform requires C++17
set(CMAKE_CXX_STANDARD              17)
set(CMAKE_CXX_STANDARD_REQUIRED     ON)

# Enable 'rdyamic' switch to export the executable's symbols for debugging
set(CMAKE_ENABLE_EXPORTS            1)

# Let's nicely support folders in IDEs
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# set the install prefix folder
set(CMAKE_INSTALL_PREFIX                ${CMAKE_BINARY_DIR}/install)
set (CMAKE_INSTALL_INCLUDEDIR           ${CMAKE_BINARY_DIR}/install/include)
set (CMAKE_INSTALL_LIBDIR               ${CMAKE_BINARY_DIR}/install/lib)
set (CMAKE_INSTALL_BINDIR               ${CMAKE_BINARY_DIR}/install/bin)


# set option BUILD_TESTS to ON to build tests
option(BUILD_TESTS      "Build tests"           OFF)
option(ENABLE_COVERAGE  "Enable code coverage"  OFF)

# set option BUILD_TESTS to ON to build tests
set (BUILD_TESTS                                ON)
set (ENABLE_COVERAGE                            OFF)
set (LIB_BUILD_TYPE                             SHARED)

message(STATUS "BUILD_TESTS: ${BUILD_TESTS}")
message(STATUS "ENABLE_COVERAGE: ${ENABLE_COVERAGE}")
message(STATUS "LIB_BUILD_TYPE: ${LIB_BUILD_TYPE}")



# Add the cmake folder to the module path
add_subdirectory(cmake)


# The compiled library code is here
add_subdirectory(Cloud)
add_subdirectory(src)
add_subdirectory(App)
# add_subdirectory(test_partial_processes)

## For Subsystem testing

# add_subdirectory(test_partial_processes/SAM/server)
# add_subdirectory(test_partial_processes/SD_TEST/server)
# add_subdirectory(test_partial_processes/SD_TEST/client)
# add_subdirectory(test_partial_processes/SD_TEST/SD)
# add_subdirectory(test_partial_processes/SD_TEST/ucm_client)
# add_subdirectory(test_partial_processes/SD_TEST/ucm_server)
# add_subdirectory(test_partial_processes/SD_TEST/Classic)

# add_subdirectory(test_partial_processes/SomeIP/some_client)
# add_subdirectory(test_partial_processes/SomeIP/some_server)




# copy manifest files to the build directory
file(COPY App/Manifests             DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/App)
file(COPY App/UCM/sw_packages.json  DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/App/processes/UCM_SERVER)
file(COPY App/UCM/config.conf       DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/App/processes/UCM_SERVER)

file(MAKE_DIRECTORY                ${CMAKE_CURRENT_BINARY_DIR}/App/processes/redirected)


if (BUILD_TESTS)
    enable_testing()
    add_subdirectory(Testing)
endif (BUILD_TESTS)


