# ap_add_test(NAME <target> SOURCES a.cpp ... [DEPENDS libs...] [WORKING_DIRECTORY dir])
# Creates a GoogleTest executable, registers every TEST with CTest and links
# the project warning / sanitizer / coverage interface targets.
include(GoogleTest)

function(ap_add_test)
  cmake_parse_arguments(T "" "NAME;WORKING_DIRECTORY" "SOURCES;DEPENDS;LABELS" ${ARGN})
  add_executable(${T_NAME} ${T_SOURCES})
  target_link_libraries(${T_NAME} PRIVATE ${T_DEPENDS} GTest::gtest_main GTest::gmock
                                          ap::coverage ap::sanitizers)
  if(NOT T_WORKING_DIRECTORY)
    set(T_WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
  endif()
  set_target_properties(${T_NAME} PROPERTIES FOLDER "tests")
  set(_extra)
  if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.18)
    list(APPEND _extra DISCOVERY_MODE PRE_TEST)
  endif()
  gtest_discover_tests(${T_NAME} WORKING_DIRECTORY ${T_WORKING_DIRECTORY}
                       PROPERTIES LABELS "${T_LABELS}" ${_extra})
endfunction()
