
if (${ENABLE_COVERAGE} AND NOT CMAKE_CONFIGURATION_TYPES)
    if (NOT BUILD_TESTS)
        message(FATAL_ERROR "Tests must be enabled for code coverage!")
    endif (NOT BUILD_TESTS)
    
    include(cmake/CodeCoverage.cmake)
    
    append_coverage_compiler_flags()
    # exclude /usr/include/, Testing and _deps folders from coverage
    set(COVERAGE_EXCLUDES "/usr/include/\\*;${CMAKE_BINARY_DIR}/Testing/\\*;${CMAKE_BINARY_DIR}/_deps/\\*;${CMAKE_SOURCE_DIR}/App/\\*")
    
    setup_target_for_coverage(NAME coverage EXECUTABLE ctest DEPENDENCIES coverage)
else (${ENABLE_COVERAGE} AND NOT CMAKE_CONFIGURATION_TYPES)
    
    if (CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Werror")
        set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g")
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O2")
    elseif (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4 /WX")
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O2")
    endif(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
endif (${ENABLE_COVERAGE} AND NOT CMAKE_CONFIGURATION_TYPES)

