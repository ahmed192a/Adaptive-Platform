file(GLOB diag_tests CONFIGURE_DEPENDS *.cpp)
ap_add_test(NAME ara_diag_ut SOURCES ${diag_tests} DEPENDS ara_diag_lib LABELS diag)

# the scripted UDS tester demo doubles as an integration test of the whole diag server
if(TARGET uds_scenario)
  ap_add_test(NAME uds_tester_ut SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/../../App/demo/uds_tester/uds_scenario_test.cpp
              DEPENDS uds_scenario LABELS diag demo)
endif()
